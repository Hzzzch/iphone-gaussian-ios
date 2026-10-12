#include "mobile_frontend.h"
#include <algorithm>
#include <cmath>
#include <vector>
namespace {
struct Tap { int index; double weight; };
using Filter=std::vector<std::vector<Tap>>;
Filter filter(int length,int output){
 Filter result(output);double scale=double(length)/output,radius=std::max(scale,1.0);
 for(int o=0;o<output;o++){
  double center=(o+.5)*scale-.5,sum=0;
  int first=std::max(0,int(std::ceil(center-radius))),last=std::min(length-1,int(std::floor(center+radius)));
  for(int i=first;i<=last;i++){double weight=std::max(0.0,1-std::abs(i-center)/radius);if(weight>0){result[o].push_back({i,weight});sum+=weight;}}
  for(auto& t:result[o])t.weight/=sum;
 }
 return result;
}
}
extern "C" int mg_preprocess_rgb8(const unsigned char* input,size_t bytes,int w,int h,float* output,size_t floats){
 if(!input||!output||w<2||h<2||w>16384||h>16384||size_t(w)*h>40000000||bytes!=size_t(w)*h*3||floats!=size_t(3)*518*518)return -1;
 try{
  constexpr int side=518;int crop=std::min(w,h),x0=(w-crop)/2,y0=(h-crop)/2;
  auto taps=filter(crop,side);std::vector<float> horizontal(size_t(crop)*side*3);
  for(int y=0;y<crop;y++)for(int x=0;x<side;x++)for(int c=0;c<3;c++){
   double value=0;for(auto t:taps[x])value+=t.weight*input[(size_t(y+y0)*w+x0+t.index)*3+c];
   horizontal[(size_t(y)*side+x)*3+c]=float(value/255);
  }
  for(int y=0;y<side;y++)for(int x=0;x<side;x++)for(int c=0;c<3;c++){
   double value=0;for(auto t:taps[y])value+=t.weight*horizontal[(size_t(t.index)*side+x)*3+c];
   output[(c*side+y)*side+x]=float(value);
  }
  return 0;
 }catch(...){return -3;}
}
