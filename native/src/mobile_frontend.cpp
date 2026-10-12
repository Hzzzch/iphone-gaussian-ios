#include "mobile_geometry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <limits>
namespace {
using V=std::array<double,4>;
bool good(const float* p,size_t n){if(!p)return false;for(size_t i=0;i<n;i++)if(!std::isfinite(p[i]))return false;return true;}
double sigmoid(double x){return x>=0?1/(1+std::exp(-x)):std::exp(x)/(1+std::exp(x));}
double softplus(double x){return std::log1p(std::exp(-std::abs(x)))+std::max(x,0.0);}
}
extern "C" int mg_recover(const float* points,const unsigned char* mask,int h,int w,
    double focal,double logscale,float* depth,double* solved_shift){
    if(!points||!mask||!depth||!solved_shift||h<=0||w<=0||!std::isfinite(focal)||focal<=0||!std::isfinite(logscale))return -1;
    try {
        std::vector<V> p; double zmin=INFINITY,zmax=-INFINITY,norm=0;
        const double diag=std::hypot(double(w),double(h));
        for(int i=0;i<64;i++)for(int j=0;j<64;j++){
            int y=std::min(int((i+.5)*h/64),h-1),x=std::min(int((j+.5)*w/64),w-1);
            size_t id=size_t(y)*w+x;const float* a=points+3*id;
            if(!mask[id]||!good(a,3))continue;
            // Store projection dot products to reduce objective work.
            double u=(2*x+1-w)/diag,v=(2*y+1-h)/diag;
            p.push_back({double(a[0])*a[0]+double(a[1])*a[1],a[0]*u+a[1]*v,u*u+v*v,a[2]});
            norm+=p.back()[0];zmin=std::min(zmin,double(a[2]));zmax=std::max(zmax,double(a[2]));
        }
        if(p.size()<8||norm<1e-24)return -1;
        auto objective=[&](double s){double sum=0;for(auto a:p){double q=focal/(a[3]+s);sum+=q*q*a[0]-2*q*a[1]+a[2];}return sum;};
        double eps=std::max({std::abs(zmin),std::abs(zmax),1.0})*1e-9;
        double lo=-zmin+eps,span=std::max(zmax-zmin,1e-3)*2+1,hi=0;
        bool bracket=false;
        for(int k=0;k<32;k++){
            hi=lo+span;double deriv=0;
            for(auto a:p){double q=focal/(a[3]+hi);deriv+=-2*(q*q*a[0]-q*a[1])/(a[3]+hi);}
            if(std::isfinite(deriv)&&deriv>0){bracket=true;break;}span*=4;
        }
        if(!bracket)return -2;
        std::vector<double> grid;
        for(int i=0;i<512;i++){double t=i/511.0;grid.push_back(lo+std::pow(t,1.5)*span);grid.push_back(lo+std::exp(std::log(eps)*(1-t)+std::log(span)*t));}
        if(lo<0&&hi>0)grid.push_back(0);
        std::sort(grid.begin(),grid.end());grid.erase(std::unique(grid.begin(),grid.end()),grid.end());
        size_t best=0;double val=INFINITY;
        for(size_t i=0;i<grid.size();i++){double v=objective(grid[i]);if(!std::isfinite(v))return -2;if(v<val){val=v;best=i;}}
        if(best==0||best+1==grid.size())return -2;
        double a=grid[best-1],b=grid[best+1],g=(std::sqrt(5.0)-1)/2,c=b-g*(b-a),d=a+g*(b-a),fc=objective(c),fd=objective(d);
        for(int i=0;i<80;i++){
            if(fc<fd){b=d;d=c;fd=fc;c=b-g*(b-a);fc=objective(c);}else{a=c;c=d;fc=fd;d=a+g*(b-a);fd=objective(d);}
        }
        *solved_shift=(a+b)/2;double scale=std::exp(logscale);
        for(size_t i=0;i<size_t(h)*w;i++)depth[i]=float((points[3*i+2]+*solved_shift)*scale);
        return good(depth,size_t(h)*w)?0:-2;
    }catch(...){return -3;}
}
extern "C" int mg_decode(const float* delta,const float* coords,const float* depths,
    const float* rgb,const int* kinds,const float* areas,int n,int h,int w,
    double fx,double fy,double cx,double cy,double global_depth_min,
    float* means,float* covs,float* colors,float* opacities){
    if(n<=0||h<=0||w<=0||!means||!covs||!colors||!opacities||!kinds||
       !good(delta,size_t(n)*14)||!good(coords,size_t(n)*2)||!good(depths,n)||!good(rgb,size_t(n)*3)||!good(areas,n)||
       !std::isfinite(fx)||!std::isfinite(fy)||!std::isfinite(cx)||!std::isfinite(cy)||fx<=0||fy<=0||!std::isfinite(global_depth_min)||global_depth_min<=0)return -1;
    double df=1/(global_depth_min+1e-6),gs=1/df;
    double lin[3][3]={{w/(2*fx),0,(w/2.0-cx)/fx},{0,h/(2*fy),(h/2.0-cy)/fy},{0,0,1}};
    for(int i=0;i<n;i++){
        if(depths[i]<=0||areas[i]<0||(kinds[i]!=0&&kinds[i]!=1))return -1;
        const float* d=delta+size_t(i)*14;
        double z=std::min(depths[i]*df,100.0),iz=1/std::max(z,1e-3);
        double isp=iz>20?iz:std::log(std::expm1(std::max(iz,1e-6)));
        double pred=softplus(isp+.001*d[2]),zz=1/(pred+.001);
        double mu[3]={zz*(coords[2*size_t(i)+1]+.001*d[0])*gs,zz*(coords[2*size_t(i)]+.001*d[1])*gs,zz*gs};
        double base=kinds[i]==0?z*2/std::sqrt(double(h)*w):std::sqrt(std::max(double(areas[i]),1e-12))*df*std::sqrt((2*fx/w)*(2*fy/h));
        double sv[3];for(int k=0;k<3;k++)sv[k]=base*iz/std::max(pred,1e-6)*4*sigmoid(4.0/3*d[3+k]+std::log(1.0/3))*gs;
        double q[4]={1.0+d[6],d[7],d[8],d[9]},len=0;for(double v:q)len+=v*v;
        if(len<1e-24)return -1;
        for(double& v:q)v/=std::sqrt(len);
        double qw=q[0],x=q[1],y=q[2],zq=q[3];
        double r[3][3]={{1-2*(y*y+zq*zq),2*(x*y-qw*zq),2*(x*zq+qw*y)},
                        {2*(x*y+qw*zq),1-2*(x*x+zq*zq),2*(y*zq-qw*x)},
                        {2*(x*zq-qw*y),2*(y*zq+qw*x),1-2*(x*x+y*y)}};
        double lr[3][3]={};
        for(int a=0;a<3;a++)for(int b=0;b<3;b++)for(int k=0;k<3;k++)lr[a][b]+=lin[a][k]*r[k][b];
        for(int a=0;a<3;a++){
            double v=0;for(int k=0;k<3;k++)v+=lin[a][k]*mu[k];means[3*size_t(i)+a]=float(v);
            double c=std::clamp(double(rgb[3*size_t(i)+a]),.01,.99);
            c=sigmoid(std::log(c/(1-c))+.1*d[10+a]);colors[3*size_t(i)+a]=float(c<=.04045?c/12.92:std::pow((c+.055)/1.055,2.4));
            for(int b=0;b<3;b++){double v2=0;for(int k=0;k<3;k++)v2+=lr[a][k]*sv[k]*sv[k]*lr[b][k];covs[9*size_t(i)+3*a+b]=float(v2);}
        }
        opacities[i]=float(sigmoid(d[13]));
    }
    return good(means,size_t(n)*3)&&good(covs,size_t(n)*9)&&good(colors,size_t(n)*3)&&good(opacities,n)?0:-2;
}
