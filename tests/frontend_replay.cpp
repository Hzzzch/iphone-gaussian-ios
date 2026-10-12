#include "mobile_geometry.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
#include <stdexcept>
template<class T> std::vector<T> read(std::ifstream& f,size_t n){std::vector<T> v(n);if(!f.read(reinterpret_cast<char*>(v.data()),n*sizeof(T)))throw std::runtime_error("truncated fixture");return v;}
int main(int argc,char** argv){
 if(argc!=2){std::cerr<<"usage: frontend_replay fixture.bin\n";return 2;}
 try{
  std::ifstream f(argv[1],std::ios::binary);auto header=read<uint32_t>(f,4);
  if(header[0]!=0x4d475331||header[1]>4096||header[2]>4096||!header[1]||!header[2]||header[3]>100000||!header[3])throw std::runtime_error("invalid fixture");
  int h=header[1],w=header[2],n=header[3];size_t pixels=size_t(h)*w;
  auto points=read<float>(f,pixels*3);auto mask=read<unsigned char>(f,pixels);auto focal=read<double>(f,1);auto refdepth=read<float>(f,pixels);
  std::vector<float> depth(pixels);double shift=0;
  if(mg_recover(points.data(),mask.data(),h,w,focal[0],0,depth.data(),&shift))throw std::runtime_error("depth failed");
  double de=0;for(size_t i=0;i<pixels;i++)de=std::max(de,std::abs(double(depth[i])-refdepth[i]));
  auto delta=read<float>(f,n*14),coords=read<float>(f,n*2),d=read<float>(f,n),rgb=read<float>(f,n*3);auto kinds=read<int32_t>(f,n);auto areas=read<float>(f,n);
  auto expected=read<float>(f,n*16);std::vector<float> means(n*3),cov(n*9),colors(n*3),opacity(n);
  if(mg_decode(delta.data(),coords.data(),d.data(),rgb.data(),kinds.data(),areas.data(),n,518,518,490,470,259,257,*std::min_element(d.begin(),d.end()),means.data(),cov.data(),colors.data(),opacity.data()))throw std::runtime_error("decode failed");
  size_t offset=0;double ge=0;for(auto p:{&means,&cov,&colors,&opacity})for(float v:*p)ge=std::max(ge,std::abs(double(v)-expected[offset++]));
  bool ok=de<2e-5&&ge<5e-5;
  std::cout<<"{\"passed\":"<<(ok?"true":"false")<<",\"depth_max_abs\":"<<de<<",\"gaussian_max_abs\":"<<ge<<",\"scope\":\"native fixture replay, no neural inference\"}\n";
  return ok?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
