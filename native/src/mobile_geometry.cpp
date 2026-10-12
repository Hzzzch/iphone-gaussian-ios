#include "mobile_geometry.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
namespace {
bool size_ok(int c,int h,int w) {
    if(c<=0 || h<=0 || w<=0) return false;
    const auto limit=std::numeric_limits<std::size_t>::max()/sizeof(float);
    return std::size_t(h)<=limit/std::size_t(w) &&
           std::size_t(c)<=limit/(std::size_t(h)*w);
}
bool finite(const float* p,std::size_t n) {
    if(!p) return false;
    for(std::size_t i=0;i<n;++i) if(!std::isfinite(p[i])) return false;
    return true;
}
float bilinear(const float* f,int h,int w,double y,double x) {
    x=std::clamp(x,0.0,double(w-1)); y=std::clamp(y,0.0,double(h-1));
    int x0=int(x),y0=int(y),x1=std::min(x0+1,w-1),y1=std::min(y0+1,h-1);
    double wx=x-x0,wy=y-y0;
    double top=f[std::size_t(y0)*w+x0]*(1-wx)+f[std::size_t(y0)*w+x1]*wx;
    double bot=f[std::size_t(y1)*w+x0]*(1-wx)+f[std::size_t(y1)*w+x1]*wx;
    return float(top*(1-wy)+bot*wy);
}
}
extern "C" int mg_sample(const float* f,int c,int h,int w,const float* xy,int n,float* out) {
    if(!size_ok(c,h,w)||!size_ok(n,c,1)||!size_ok(n,2,1)||!out||
       !finite(f,std::size_t(c)*h*w)||!finite(xy,std::size_t(n)*2)) return -1;
    for(int i=0;i<n;++i) for(int k=0;k<c;++k)
        out[std::size_t(i)*c+k]=bilinear(f+std::size_t(k)*h*w,h,w,
            (xy[2*std::size_t(i)]+1.0f)*(h/2.0f)-.5f,
            (xy[2*std::size_t(i)+1]+1.0f)*(w/2.0f)-.5f);
    return 0;
}
extern "C" int mg_resize(const float* f,int c,int h,int w,int oh,int ow,float* out) {
    if(!size_ok(c,h,w)||!size_ok(c,oh,ow)||!out||!finite(f,std::size_t(c)*h*w)) return -1;
    for(int k=0;k<c;++k) for(int y=0;y<oh;++y) for(int x=0;x<ow;++x)
        out[(std::size_t(k)*oh+y)*ow+x]=bilinear(f+std::size_t(k)*h*w,h,w,
            (y+.5)*h/oh-.5,(x+.5)*w/ow-.5);
    return 0;
}
extern "C" int mg_affine(const float* c,int h,int w,float* out) {
    if(!size_ok(3,h,w)||!out||!finite(c,std::size_t(3)*h*w)) return -1;
    const std::size_t n=std::size_t(h)*w;
    for(std::size_t i=0;i<n;++i) {
        double z=std::exp(double(c[2*n+i]));
        out[3*i]=float(c[i]*z); out[3*i+1]=float(c[n+i]*z); out[3*i+2]=float(z);
    }
    return finite(out,3*n)?0:-2;
}
extern "C" int mg_unproject(const float* d,int h,int w,float fx,float fy,float cx,float cy,float* out) {
    if(!size_ok(3,h,w)||!out||!finite(d,std::size_t(h)*w)||
       !std::isfinite(fx)||!std::isfinite(fy)||!std::isfinite(cx)||!std::isfinite(cy)||fx<=0||fy<=0) return -1;
    for(std::size_t i=0;i<std::size_t(h)*w;++i) if(d[i]<=0) return -1;
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        std::size_t i=std::size_t(y)*w+x;
        out[3*i]=float((double(x)-cx)*d[i]/fx);
        out[3*i+1]=float((double(y)-cy)*d[i]/fy); out[3*i+2]=d[i];
    }
    return finite(out,std::size_t(3)*h*w)?0:-2;
}

extern "C" int mg_sample_features(const float* f,int c,int h,int w,const float* xy,int n,float* out) {
    if(!size_ok(c,h,w)||!size_ok(n,c,1)||!size_ok(n,2,1)||!out||
       !finite(f,std::size_t(c)*h*w)||!finite(xy,std::size_t(n)*2)) return -1;
    for(int i=0;i<n;++i) {
        float yn=std::clamp(xy[2*std::size_t(i)],-1.0f+1e-6f,1.0f-1e-6f);
        float xn=std::clamp(xy[2*std::size_t(i)+1],-1.0f+1e-6f,1.0f-1e-6f);
        double y=(yn+1.0f)*(h/2.0f)-.5f, x=(xn+1.0f)*(w/2.0f)-.5f;
        int y0=int(std::floor(y)),x0=int(std::floor(x));
        double wy=y-y0,wx=x-x0;
        for(int k=0;k<c;++k) {
            auto at=[&](int yy,int xx)->double {
                return yy<0||xx<0||yy>=h||xx>=w ? 0 : f[(std::size_t(k)*h+yy)*w+xx];
            };
            double a=at(y0,x0)*(1-wx)+at(y0,x0+1)*wx;
            double b=at(y0+1,x0)*(1-wx)+at(y0+1,x0+1)*wx;
            out[std::size_t(i)*c+k]=float(a*(1-wy)+b*wy);
        }
    }
    return 0;
}
