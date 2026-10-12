#include "mobile_geometry.h"
#include "mobile_frontend.h"
#include "mobile_onnx.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <memory>
#include <cstdlib>
#include <array>
#include <mutex>
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
void run(const char* path,const std::vector<std::vector<float>*>& input,const std::vector<std::vector<float>*>& output,int threads,const mg_ort_options* config,void* resident){
 void* handle=resident?resident:mg_ort_open_ex(path,threads,config);if(!handle)throw std::runtime_error(mg_ort_error());
 std::unique_ptr<void,decltype(&mg_ort_close)> guard(resident?nullptr:handle,mg_ort_close);
 std::vector<const float*> in;std::vector<float*> out;std::vector<size_t> ni,no;
 for(auto p:input){in.push_back(p->data());ni.push_back(p->size());}
 for(auto p:output){out.push_back(p->data());no.push_back(p->size());}
 if(mg_ort_run(handle,in.data(),ni.data(),in.size(),out.data(),no.data(),out.size()))throw std::runtime_error(mg_ort_error());
}
void write(const fs::path& path,const std::vector<float>& data){std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(data.data()),data.size()*sizeof(float));if(!f)throw std::runtime_error("output write failed");}
void write_int(const fs::path& path,const std::vector<int>& data){std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(data.data()),data.size()*sizeof(int));if(!f)throw std::runtime_error("output write failed");}
namespace { thread_local std::string pipeline_error; }
static int pipeline_main(int argc,const char* const* argv,const float* prepared=nullptr,const char* sampler_override=nullptr,int thread_override=4,const mg_ort_options* configs=nullptr,void* const* resident=nullptr){
 if(argc!=8){std::cerr<<"usage: photo_pipeline depth.onnx features.onnx head.onnx rgb_chw_f32.bin count out_directory fov_degrees\n";return 2;}
 try{
  pipeline_error.clear();
  int count=std::stoi(argv[5]);double fov=std::stod(argv[7]);if(count<1000||count>500000||!std::isfinite(fov)||fov<=1||fov>=179)throw std::runtime_error("invalid count/FOV");
  const char* mode_env=std::getenv("MOBILEGS_SAMPLER");
  std::string sampler=sampler_override?sampler_override:(mode_env?mode_env:"reference");
  if(sampler!="reference"&&sampler!="balanced"&&sampler!="stable")throw std::runtime_error("invalid MOBILEGS_SAMPLER");
  int threads=thread_override;if(threads<1||threads>16)throw std::runtime_error("invalid thread count");
  bool accelerated=false;
  if(configs)for(int i=0;i<3;i++){
   std::string b=configs[i].backend?configs[i].backend:"cpu";
   if(b!="cpu"&&b!="coreml"&&b!="coreml-ane"&&b!="coreml-gpu")throw std::runtime_error("Invalid iOS backend");
   accelerated=accelerated||b!="cpu";
  }
  fs::path outdir(argv[6]);if(!fs::create_directory(outdir))throw std::runtime_error("output must not already exist");
  constexpr int side=518,pixels=side*side;std::vector<float> rgb(3*pixels);
  if(prepared)std::copy_n(prepared,rgb.size(),rgb.data());
  else {std::ifstream input(argv[4],std::ios::binary);if(!input.read(reinterpret_cast<char*>(rgb.data()),rgb.size()*4)||input.peek()!=std::char_traits<char>::eof())throw std::runtime_error("RGB input size mismatch");}
  write(outdir/"rgb.f32",rgb);
  for(float v:rgb)if(!std::isfinite(v)||v<0||v>1)throw std::runtime_error("RGB must be finite [0,1]");
  auto start=Clock::now(),last=start;std::vector<std::pair<std::string,double>> times;
  auto stage=[&](const char* name){auto now=Clock::now();double t=std::chrono::duration<double>(now-last).count();times.emplace_back(name,t);last=now;std::cout<<name<<": "<<t<<" s"<<std::endl;};
  std::vector<float> affine(pixels*3),mask(pixels),scale(1),depth(pixels);
  run(argv[1],{&rgb},{&affine,&mask,&scale},threads,configs?&configs[0]:nullptr,resident?resident[0]:nullptr);stage("depth_network");
  if(scale[0]<=0)throw std::runtime_error("invalid metric scale");
  std::vector<unsigned char> valid(pixels);for(int i=0;i<pixels;i++)valid[i]=mask[i]>.5f;
  double focal=1/std::sqrt(2.0)/std::tan(fov*std::acos(-1)/360),shift=0;
  if(mg_recover(affine.data(),valid.data(),side,side,focal,std::log(scale[0]),depth.data(),&shift))throw std::runtime_error("depth recovery failed");
  int valid_count=0;for(int i=0;i<pixels;i++){if(!valid[i]||depth[i]<=0)depth[i]=0;else valid_count++;}
  if(valid_count<128)throw std::runtime_error("insufficient valid depth");
  write(outdir/"depth.f32",depth);stage("depth_recovery");
  std::vector<float>().swap(affine);std::vector<float>().swap(mask);std::vector<unsigned char>().swap(valid);
  double fx=side/(2*std::tan(fov*std::acos(-1)/360));
  std::vector<float> coords(count*2),areas(count);std::vector<int> kinds(count);
  auto sample_fn=sampler=="stable"?mg_surface_samples_stable:(sampler=="balanced"?mg_surface_samples_balanced:mg_surface_samples);
  int n=sample_fn(depth.data(),rgb.data(),side,side,fx,fx,side/2.0,side/2.0,count,coords.data(),kinds.data(),areas.data());
  if(n!=count)throw std::runtime_error("surface sampling count mismatch");
  write(outdir/"coords.f32",coords);write(outdir/"areas.f32",areas);write_int(outdir/"kinds.i32",kinds);stage("surface_sampling");
  std::vector<float> spatial(1024*32*32),basic(128*129*129);
  run(argv[2],{&rgb},{&spatial,&basic},threads,configs?&configs[1]:nullptr,resident?resident[1]:nullptr);stage("feature_network");
  std::vector<float> depths(count),rgbs(count*3),deltas(count*14);
  if(mg_sample(depth.data(),1,side,side,coords.data(),count,depths.data())||mg_sample(rgb.data(),3,side,side,coords.data(),count,rgbs.data()))throw std::runtime_error("sample maps failed");
  double min_depth=*std::min_element(depths.begin(),depths.end());if(min_depth<=0)throw std::runtime_error("nonpositive sampled depth");
  void* handle=resident?resident[2]:mg_ort_open_ex(argv[3],threads,configs?&configs[2]:nullptr);if(!handle)throw std::runtime_error(mg_ort_error());
  std::unique_ptr<void,decltype(&mg_ort_close)> head(resident?nullptr:handle,mg_ort_close);
  const float* fixed_inputs[]={spatial.data(),basic.data()};size_t fixed_sizes[]={spatial.size(),basic.size()};
  std::unique_ptr<void,decltype(&mg_ort_release_prefix)> prefix(mg_ort_prepare_prefix(fixed_inputs,fixed_sizes,2),mg_ort_release_prefix);
  if(!prefix)throw std::runtime_error(mg_ort_error());
  std::vector<float> cc(2000),dd(14000);
  for(int offset=0;offset<count;offset+=1000){
   int take=std::min(1000,count-offset);
   for(int j=0;j<1000;j++)for(int k=0;k<2;k++)cc[2*j+k]=coords[2*(offset+std::min(j,take-1))+k];
   const float* inputs[]={cc.data()};size_t sizes[]={cc.size()};float* outputs[]={dd.data()};size_t output_sizes[]={dd.size()};
   if(mg_ort_run_with_prefix(handle,prefix.get(),inputs,sizes,1,outputs,output_sizes,1))throw std::runtime_error(mg_ort_error());
   std::copy_n(dd.data(),take*14,deltas.data()+offset*14);
  }
  head.reset();stage("gaussian_head");
  std::vector<float> means(count*3),covs(count*9),colors(count*3),opacity(count);
  if(mg_decode(deltas.data(),coords.data(),depths.data(),rgbs.data(),kinds.data(),areas.data(),count,side,side,fx,fx,side/2.0,side/2.0,min_depth,means.data(),covs.data(),colors.data(),opacity.data()))throw std::runtime_error("Gaussian decode failed");
  write(outdir/"means.f32",means);write(outdir/"covariances.f32",covs);write(outdir/"colors.f32",colors);write(outdir/"opacities.f32",opacity);stage("decode_and_write");
  std::ofstream meta(outdir/"report.json");meta<<"{\"complete\":true,\"backend\":\""<<(accelerated?"ONNX Runtime CoreML requested + native C++ geometry":"ONNX Runtime CPU + native C++ geometry")<<"\",\"model_variant\":\"MoGe3 ViT-L + public InfiniSplat (not S+)\",\"width\":518,\"height\":518,\"gaussians\":"<<count<<",\"fx\":"<<fx<<",\"fov_x_degrees\":"<<fov<<",\"valid_pixels\":"<<valid_count<<",\"seconds\":{";
  for(size_t i=0;i<times.size();i++){if(i)meta<<',';meta<<'"'<<times[i].first<<"\":"<<times[i].second;}
  if(configs){
   meta<<"},\"network_backends\":{";
   const char* labels[]={"depth","features","head"};
   for(int i=0;i<3;i++){if(i)meta<<',';meta<<'"'<<labels[i]<<"\":{\"requested\":\""<<(configs[i].backend?configs[i].backend:"cpu")<<"\",\"cpu_fallback_allowed\":"<<(configs[i].allow_cpu_fallback?"true":"false")<<'}';}
  }
  meta<<"},\"session_reused\":"<<(resident?"true":"false")<<",\"cpu_worker_spinning\":false,\"sampler\":\""<<sampler<<"\",\"threads\":"<<threads<<",\"preprocess_contract\":\""<<(prepared?"rgb8-center-aa-v1":"caller-float32")<<"\"}\n";if(!meta)throw std::runtime_error("report write failed");return 0;
 }catch(const std::exception& e){pipeline_error=e.what();std::cerr<<e.what()<<std::endl;return 1;}
}

namespace {
struct PhotoSession {
 std::array<std::string,3> models,backends,libraries,diagnostics,caches;
 std::array<mg_ort_options,3> options{};
 std::array<void*,3> contexts{};
 int threads=0;
 std::mutex mutex;
 ~PhotoSession(){for(auto p:contexts)mg_ort_close(p);}
};
}
extern "C" void* mg_photo_session_open(const char* depth,const char* feature,const char* head,int threads,const mg_ort_options* configs){
 try{
  pipeline_error.clear();
  if(!depth||!feature||!head||threads<1||threads>16)throw std::runtime_error("Invalid session configuration");
  auto session=std::make_unique<PhotoSession>();session->models={depth,feature,head};session->threads=threads;
  for(int i=0;i<3;i++){
   auto copy=[](const char* text){return text?std::string(text):std::string();};
   session->backends[i]=configs&&configs[i].backend?configs[i].backend:"cpu";
   session->libraries[i]=configs?copy(configs[i].library_dir):"";
   session->diagnostics[i]=configs?copy(configs[i].diagnostics_dir):"";
   session->caches[i]=configs?copy(configs[i].cache_output):"";
   session->options[i]={session->backends[i].c_str(),session->libraries[i].c_str(),session->diagnostics[i].c_str(),session->caches[i].c_str(),configs?configs[i].allow_cpu_fallback:0};
   session->contexts[i]=mg_ort_open_ex(session->models[i].c_str(),threads,&session->options[i]);
   if(!session->contexts[i])throw std::runtime_error(mg_ort_error());
  }
  return session.release();
 }catch(const std::exception& e){pipeline_error=e.what();return nullptr;}
}
extern "C" void mg_photo_session_close(void* session){delete static_cast<PhotoSession*>(session);}
extern "C" int mg_photo_session_reconstruct_rgb8(void* opaque,const unsigned char* rgb,size_t bytes,int width,int height,int count,const char* out,double fov,int sampler){
 try{
  if(!opaque||!out||sampler<0||sampler>2)throw std::runtime_error("Invalid session input");
  auto* session=static_cast<PhotoSession*>(opaque);
  std::unique_lock<std::mutex> lock(session->mutex,std::try_to_lock);
  if(!lock.owns_lock())throw std::runtime_error("Session is already in use");
  std::vector<float> input(3*518*518);
  if(mg_preprocess_rgb8(rgb,bytes,width,height,input.data(),input.size()))throw std::runtime_error("Invalid RGB8 input");
  std::string n=std::to_string(count),f=std::to_string(fov);
  const char* argv[]={"photo_session",session->models[0].c_str(),session->models[1].c_str(),session->models[2].c_str(),"",n.c_str(),out,f.c_str()};
  const char* modes[]={"reference","balanced","stable"};
  return pipeline_main(8,argv,input.data(),modes[sampler],session->threads,session->options.data(),session->contexts.data());
 }catch(const std::exception& e){pipeline_error=e.what();return 1;}
}

extern "C" const char* mg_photo_error(){return pipeline_error.c_str();}
extern "C" int mg_photo_reconstruct(const char* depth,const char* feature,const char* head,const char* rgb,int count,const char* out,double fov){
 try {
  if(!depth||!feature||!head||!rgb||!out){pipeline_error="Null path";return 1;}
  std::string n=std::to_string(count),f=std::to_string(fov);
  const char* argv[]={"photo_pipeline",depth,feature,head,rgb,n.c_str(),out,f.c_str()};
  return pipeline_main(8,argv);
 }catch(const std::exception& e){pipeline_error=e.what();return 1;}
}

extern "C" int mg_photo_reconstruct_rgb8(const char* depth,const char* feature,const char* head,
 const unsigned char* rgb,size_t bytes,int width,int height,int count,const char* out,double fov,int sampler){
 return mg_photo_reconstruct_rgb8_v3(depth,feature,head,rgb,bytes,width,height,count,out,fov,sampler,4);
}
extern "C" int mg_photo_reconstruct_rgb8_v3(const char* depth,const char* feature,const char* head,
 const unsigned char* rgb,size_t bytes,int width,int height,int count,const char* out,double fov,int sampler,int threads){
 return mg_photo_reconstruct_rgb8_v4(depth,feature,head,rgb,bytes,width,height,count,out,fov,sampler,threads,nullptr);
}
extern "C" int mg_photo_reconstruct_rgb8_v4(const char* depth,const char* feature,const char* head,
 const unsigned char* rgb,size_t bytes,int width,int height,int count,const char* out,double fov,int sampler,int threads,const mg_ort_options* configs){
 try{
  if(!depth||!feature||!head||!out||sampler<0||sampler>2||threads<1||threads>16){pipeline_error="Invalid paths, sampler, or threads";return 1;}
  std::vector<float> input(3*518*518);
  if(mg_preprocess_rgb8(rgb,bytes,width,height,input.data(),input.size())){pipeline_error="Invalid RGB8 input";return 1;}
  std::string n=std::to_string(count),f=std::to_string(fov);
  const char* argv[]={"photo_rgb8",depth,feature,head,"",n.c_str(),out,f.c_str()};
  const char* modes[]={"reference","balanced","stable"};
  return pipeline_main(8,argv,input.data(),modes[sampler],threads,configs);
 }catch(const std::exception& e){pipeline_error=e.what();return 1;}
}
