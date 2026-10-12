#include "mobile_geometry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <vector>
namespace {
using V=std::array<double,3>;
struct Face {int a,b,c;double area,z;};
double quantile(std::vector<double> v,double q){std::sort(v.begin(),v.end());double k=q*(v.size()-1);size_t i=size_t(k);return v[i]+(v[std::min(i+1,v.size()-1)]-v[i])*(k-i);}
}
static int sample_surface(const float* depth,const float* rgb,int h,int w,
 double fx,double fy,double cx,double cy,int count,float* coords,int* kinds,float* areas,int mode){
 if(!depth||!coords||!kinds||!areas||h<2||w<2||count<=0||fx<=0||fy<=0||!std::isfinite(fx)||!std::isfinite(fy)||!std::isfinite(cx)||!std::isfinite(cy)||static_cast<long long>(h)*w>10000000)return -1;
 try{
  int n=h*w;std::vector<V> v(n);std::vector<Face> faces;std::vector<bool> valid(n,false);
  for(int y=0;y<h;y++)for(int x=0;x<w;x++){int i=y*w+x;v[i]={(x-cx)/fx*depth[i],(y-cy)/fy*depth[i],depth[i]};}
  for(int type=0;type<2;type++)for(int y=0;y<h-1;y++)for(int x=0;x<w-1;x++){
   int a=type?(y+1)*w+x+1:y*w+x,b=type?y*w+x+1:(y+1)*w+x,c=type?(y+1)*w+x:y*w+x+1;
   auto decision_z=[&](int i){return mode==2?std::floor(v[i][2]*10.0+.5)/10.0:v[i][2];};
   double za=decision_z(a),zb=decision_z(b),zc=decision_z(c);
   double z0=std::min({za,zb,zc}),z1=std::max({za,zb,zc}),zm=(za+zb+zc)/3;
   if(!std::isfinite(zm)||z0<=0||z1/z0>=1.05+.05*std::clamp(std::log10(std::max(zm,1.0))/2,0.0,1.0))continue;
   V ab,ac;for(int k=0;k<3;k++){ab[k]=v[b][k]-v[a][k];ac[k]=v[c][k]-v[a][k];}
   V cross={ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]};
   double area=.5*std::sqrt(cross[0]*cross[0]+cross[1]*cross[1]+cross[2]*cross[2]);
   faces.push_back({a,b,c,area,zm});valid[a]=valid[b]=valid[c]=true;
  }
  std::vector<int> ids;std::vector<double> zs,grads,gradient(n,0),gray(n,0),priority(n,-INFINITY);
  for(int i=0;i<n;i++)if(valid[i]){ids.push_back(i);zs.push_back(depth[i]);}
  if(ids.empty())return 0;
  if(rgb){
   for(int i=0;i<n;i++){
    if(!std::isfinite(rgb[i])||!std::isfinite(rgb[n+i])||!std::isfinite(rgb[2*n+i]))return -1;
    auto c=[&](double value){return mode==2?std::floor(value*4096+.5)/4096:value;};
    gray[i]=.299*c(rgb[i])+.587*c(rgb[n+i])+.114*c(rgb[2*n+i]);
   }
   auto at=[&](int y,int x){return gray[std::clamp(y,0,h-1)*w+std::clamp(x,0,w-1)];};
   for(int i:ids){int y=i/w,x=i%w;double gx=(at(y-1,x+1)+2*at(y,x+1)+at(y+1,x+1)-at(y-1,x-1)-2*at(y,x-1)-at(y+1,x-1))/8;
    double gy=(at(y+1,x-1)+2*at(y+1,x)+at(y+1,x+1)-at(y-1,x-1)-2*at(y-1,x)-at(y-1,x+1))/8;
    gradient[i]=std::sqrt(gx*gx+gy*gy+1e-12);grads.push_back(gradient[i]);}
  }
  double norm=rgb?std::max(quantile(grads,.9),1e-6):1,zref=std::max(quantile(zs,.5),1e-6);
  for(int i:ids)priority[i]=mode==2?std::clamp(gradient[i]/norm,0.0,1.0):.7*std::clamp(gradient[i]/norm,0.0,1.0)+.3*std::clamp(zref/std::max(double(depth[i]),1e-6),0.0,4.0)/4;
  auto cmp=[&](int a,int b){return priority[a]>priority[b];};
  int desired=std::max(1,int(std::nearbyint(count*.65)));
  int stride=mode==1?std::max(2,int(std::ceil(std::sqrt(double(n)/desired)))):2;
  int cols=(w+stride-1)/stride;
  std::vector<int> cells(((h+stride-1)/stride)*cols,-1);
  for(int i:ids){int cell=(i/w/stride)*cols+(i%w/stride);if(cells[cell]<0||priority[i]>priority[cells[cell]])cells[cell]=i;}
  std::vector<int> anchors;for(int i:cells)if(i>=0)anchors.push_back(i);
  int target=std::min({mode==1?desired:std::max(int(anchors.size()),desired),int(ids.size()),count});
  if(int(anchors.size())>target){std::stable_sort(anchors.begin(),anchors.end(),cmp);anchors.resize(target);}
  std::vector<bool> chosen(n,false);for(int i:anchors)chosen[i]=true;
  if(int(anchors.size())<target){std::vector<int> remaining;for(int i:ids)if(!chosen[i])remaining.push_back(i);std::stable_sort(remaining.begin(),remaining.end(),cmp);anchors.insert(anchors.end(),remaining.begin(),remaining.begin()+target-anchors.size());}
  int out=0;
  for(int i:anchors){coords[2*out]=float(2*((i/w+.5)/h)-1);coords[2*out+1]=float(2*((i%w+.5)/w)-1);kinds[out]=0;areas[out]=float(double(depth[i])*depth[i]/std::max(fx*fy,1e-6));out++;}
  int budget=count-out;if(!budget)return out;
  std::vector<double> cdf;double sum=0;for(auto f:faces){sum+=mode==2?1.0:f.area*std::clamp(f.z/zref,.25,4.0);cdf.push_back(sum);}
  if(!std::isfinite(sum)||sum<=0)return -2;
  for(double& t:cdf){t/=sum;}
  cdf.back()=1;
  std::vector<int> selected(budget),counts(faces.size(),0),occ(faces.size(),0);
  for(int i=0;i<budget;i++){selected[i]=int(std::lower_bound(cdf.begin(),cdf.end(),(i+.5)/budget)-cdf.begin());counts[selected[i]]++;}
  double phi=(std::sqrt(5.0)-1)/2,psi=std::sqrt(2.0)-1;
  for(int i:selected){auto f=faces[i];int local=++occ[i];double u=std::fmod(.5+local*phi,1),t=std::fmod(.5+local*psi,1);if(u+t>1){u=1-u;t=1-t;}
   double y=(1-u-t)*(f.a/w)+u*(f.b/w)+t*(f.c/w),x=(1-u-t)*(f.a%w)+u*(f.b%w)+t*(f.c%w);
   coords[2*out]=float(2*((y+.5)/h)-1);coords[2*out+1]=float(2*((x+.5)/w)-1);kinds[out]=1;areas[out]=float(f.area/counts[i]);out++;
  }
  return out;
 }catch(...){return -3;}
}

extern "C" int mg_surface_samples(const float* depth,const float* rgb,int h,int w,
 double fx,double fy,double cx,double cy,int count,float* coords,int* kinds,float* areas){
 return sample_surface(depth,rgb,h,w,fx,fy,cx,cy,count,coords,kinds,areas,0);
}
extern "C" int mg_surface_samples_balanced(const float* depth,const float* rgb,int h,int w,
 double fx,double fy,double cx,double cy,int count,float* coords,int* kinds,float* areas){
 return sample_surface(depth,rgb,h,w,fx,fy,cx,cy,count,coords,kinds,areas,1);
}
extern "C" int mg_surface_samples_stable(const float* depth,const float* rgb,int h,int w,
 double fx,double fy,double cx,double cy,int count,float* coords,int* kinds,float* areas){
 if(!depth||h<2||w<2)return -1;
 return sample_surface(depth,rgb,h,w,fx,fy,cx,cy,count,coords,kinds,areas,2);
}
