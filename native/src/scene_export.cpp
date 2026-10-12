#include "scene_export.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace fs=std::filesystem;
namespace {
std::vector<float> read(const fs::path& p,size_t n){std::vector<float> v(n);std::ifstream f(p,std::ios::binary);f.read(reinterpret_cast<char*>(v.data()),n*4);if(!f||f.peek()!=EOF)throw std::runtime_error("Invalid tensor file: "+p.string());for(float x:v)if(!std::isfinite(x))throw std::runtime_error("Nonfinite output");return v;}
// Jacobi eigendecomposition for a symmetric 3x3 covariance; columns are axes.
void axes(const float* cov,float* scales,float* q){
 double a[3][3],v[3][3]={{1,0,0},{0,1,0},{0,0,1}};
 for(int i=0;i<3;i++)for(int j=0;j<3;j++)a[i][j]=(double(cov[i*3+j])+cov[j*3+i])*.5;
 for(int step=0;step<24;step++){
  int p=0,r=1;for(int i=0;i<3;i++)for(int j=i+1;j<3;j++)if(std::abs(a[i][j])>std::abs(a[p][r])){p=i;r=j;}
  if(std::abs(a[p][r])<1e-15)break;
  double angle=.5*std::atan2(2*a[p][r],a[r][r]-a[p][p]),c=std::cos(angle),s=std::sin(angle);
  double pp=a[p][p],rr=a[r][r],pr=a[p][r];
  for(int k=0;k<3;k++)if(k!=p&&k!=r){double ap=a[k][p],ar=a[k][r];a[k][p]=a[p][k]=c*ap-s*ar;a[k][r]=a[r][k]=s*ap+c*ar;}
  a[p][p]=c*c*pp-2*s*c*pr+s*s*rr;a[r][r]=s*s*pp+2*s*c*pr+c*c*rr;a[p][r]=a[r][p]=0;
  for(int k=0;k<3;k++){double vp=v[k][p],vr=v[k][r];v[k][p]=c*vp-s*vr;v[k][r]=s*vp+c*vr;}
 }
 for(int i=0;i<3;i++){if(a[i][i]<-1e-7)throw std::runtime_error("Invalid covariance");scales[i]=std::log(std::sqrt(std::max(a[i][i],1e-12)));}
 double t=v[0][0]+v[1][1]+v[2][2],w,x,y,z;
 if(t>0){double s=std::sqrt(t+1)*2;w=.25*s;x=(v[2][1]-v[1][2])/s;y=(v[0][2]-v[2][0])/s;z=(v[1][0]-v[0][1])/s;}
 else if(v[0][0]>v[1][1]&&v[0][0]>v[2][2]){double s=std::sqrt(1+v[0][0]-v[1][1]-v[2][2])*2;w=(v[2][1]-v[1][2])/s;x=.25*s;y=(v[0][1]+v[1][0])/s;z=(v[0][2]+v[2][0])/s;}
 else if(v[1][1]>v[2][2]){double s=std::sqrt(1+v[1][1]-v[0][0]-v[2][2])*2;w=(v[0][2]-v[2][0])/s;x=(v[0][1]+v[1][0])/s;y=.25*s;z=(v[1][2]+v[2][1])/s;}
 else{double s=std::sqrt(1+v[2][2]-v[0][0]-v[1][1])*2;w=(v[1][0]-v[0][1])/s;x=(v[0][2]+v[2][0])/s;y=(v[1][2]+v[2][1])/s;z=.25*s;}
 double norm=std::sqrt(w*w+x*x+y*y+z*z);q[0]=w/norm;q[1]=x/norm;q[2]=y/norm;q[3]=z/norm;
}
void scene(const fs::path& dir,double fov){
 constexpr size_t n=100000;auto means=read(dir/"means.f32",n*3),cov=read(dir/"covariances.f32",n*9),colors=read(dir/"colors.f32",n*3),opacity=read(dir/"opacities.f32",n);
 std::ofstream raw(dir/"scene.ply",std::ios::binary),browser(dir/"scene_browser.ply",std::ios::binary);
 std::string header="ply\nformat binary_little_endian 1.0\nelement vertex 100000\n";
 for(const char* name:{"x","y","z","nx","ny","nz","f_dc_0","f_dc_1","f_dc_2","opacity","scale_0","scale_1","scale_2","rot_0","rot_1","rot_2","rot_3"})header+="property float "+std::string(name)+"\n";
 header+="end_header\n";raw<<header;browser<<header;std::vector<float> zs;zs.reserve(n);
 for(size_t i=0;i<n;i++){
  std::array<float,17> row{};std::copy_n(&means[i*3],3,row.data());
  for(int k=0;k<3;k++)row[6+k]=(colors[i*3+k]-.5f)/.28209479177387814f;
  float alpha=std::clamp(opacity[i],1e-6f,1-1e-6f);row[9]=std::log(alpha/(1-alpha));axes(&cov[i*9],row.data()+10,row.data()+13);
  raw.write(reinterpret_cast<char*>(row.data()),sizeof(row));
  for(int k=0;k<3;k++){float c=std::clamp(colors[i*3+k],0.f,1.f);c=c<=.0031308f?12.92f*c:1.055f*std::pow(c,1/2.4f)-.055f;row[6+k]=(c-.5f)/.28209479177387814f;}
  browser.write(reinterpret_cast<char*>(row.data()),sizeof(row));if(means[i*3+2]>0)zs.push_back(means[i*3+2]);
 }
 if(!raw||!browser||zs.empty())throw std::runtime_error("Scene export failed");
 std::nth_element(zs.begin(),zs.begin()+zs.size()/2,zs.end());std::ofstream cfg(dir/"viewer_config.json");cfg<<"{\"count\":100000,\"fov\":"<<fov<<",\"targetZ\":"<<zs[zs.size()/2]<<"}";
}
}
void gs_export_scene(const char* directory,double fov){if(!directory||!std::isfinite(fov)||fov<10||fov>120)throw std::runtime_error("Invalid output path/FOV");scene(directory,fov);}
