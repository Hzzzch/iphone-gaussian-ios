#include "mobile_onnx.h"
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <filesystem>
#include <mutex>
#include <cstring>
#include <unordered_map>
namespace {
thread_local std::string error;
Ort::Env& environment(){static Ort::Env env{ORT_LOGGING_LEVEL_WARNING,"mobilegs"};return env;}
struct Context {
 Ort::SessionOptions options;
 std::unique_ptr<Ort::Session> session;
 bool profiling=false;
 std::vector<std::string> in_names,out_names;
 std::vector<std::vector<int64_t>> in_shapes;
 std::vector<size_t> in_sizes,out_sizes;
 template<class T> static size_t count(const T& shape) {
  if(shape.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)throw std::runtime_error("Only float32 tensors are supported");
  size_t n=1;for(auto d:shape.GetShape()){if(d<=0||size_t(d)>std::numeric_limits<size_t>::max()/sizeof(float)/n)throw std::runtime_error("Invalid/dynamic tensor shape");n*=size_t(d);}return n;
 }
 Context(const char* path,int threads,const mg_ort_options* config){
  options.SetIntraOpNumThreads(threads);options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
  std::string backend=config&&config->backend?config->backend:"cpu";
  bool coreml=backend=="coreml"||backend=="coreml-ane"||backend=="coreml-gpu";
  if(backend!="cpu"&&!coreml)throw std::runtime_error("iOS backend must be cpu, coreml, coreml-ane or coreml-gpu; QNN caches are not supported");
  options.AddConfigEntry("session.intra_op.allow_spinning","0");
  if(config&&config->diagnostics_dir&&*config->diagnostics_dir){
   std::filesystem::create_directories(config->diagnostics_dir);
   auto prefix=std::filesystem::path(config->diagnostics_dir)/"ort-profile";
   options.EnableProfiling(prefix.c_str());profiling=true;
  }
  if(coreml){
#ifdef MOBILEGS_COREML
   std::unordered_map<std::string,std::string> ep={
    {"ModelFormat","MLProgram"},
    {"MLComputeUnits",backend=="coreml-ane"?"CPUAndNeuralEngine":(backend=="coreml-gpu"?"CPUAndGPU":"ALL")},
    {"RequireStaticInputShapes","1"}, {"EnableOnSubgraphs","0"}};
   options.AppendExecutionProvider("CoreML",ep);
   if(!config->allow_cpu_fallback)options.AddConfigEntry("session.disable_cpu_ep_fallback","1");
#else
   throw std::runtime_error("CoreML requested but this build does not enable it");
#endif
  }
  session=std::make_unique<Ort::Session>(environment(),path,options);Ort::AllocatorWithDefaultOptions a;
  for(size_t i=0;i<session->GetInputCount();i++){
   in_names.emplace_back(session->GetInputNameAllocated(i,a).get());
   auto type=session->GetInputTypeInfo(i);auto shape=type.GetTensorTypeAndShapeInfo();
   in_shapes.push_back(shape.GetShape());in_sizes.push_back(count(session->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo()));
  }
  for(size_t i=0;i<session->GetOutputCount();i++){
   out_names.emplace_back(session->GetOutputNameAllocated(i,a).get());
   out_sizes.push_back(count(session->GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo()));
  }
 }
 ~Context(){if(profiling&&session){try{Ort::AllocatorWithDefaultOptions a;session->EndProfilingAllocated(a);}catch(...){}}}
};
struct InputPrefix {std::vector<std::vector<float>> buffers;};
}
extern "C" const char* mg_ort_error(){return error.c_str();}
extern "C" void* mg_ort_open(const char* path,int threads){
 return mg_ort_open_ex(path,threads,nullptr);
}
extern "C" void* mg_ort_open_ex(const char* path,int threads,const mg_ort_options* config){
 try{error.clear();if(!path||threads<1||threads>64)throw std::runtime_error("Invalid model path/thread count");return new Context(path,threads,config);}
 catch(const std::exception& e){error=e.what();return nullptr;}
}
extern "C" void mg_ort_close(void* context){delete static_cast<Context*>(context);}
static int run_impl(void* context,const float* const* inputs,const size_t* input_counts,size_t ni,
 float* const* outputs,const size_t* output_counts,size_t no,size_t validated_prefix){
 try{
  error.clear();auto* c=static_cast<Context*>(context);
  if(!c||!inputs||!input_counts||!outputs||!output_counts||ni!=c->in_names.size()||no!=c->out_names.size())throw std::runtime_error("Input/output count mismatch");
  std::vector<const char*> names,out_names;std::vector<Ort::Value> values;
  auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
  for(size_t i=0;i<ni;i++){
   if(!inputs[i]||input_counts[i]!=c->in_sizes[i])throw std::runtime_error("Input buffer size mismatch");
   if(i>=validated_prefix)for(size_t j=0;j<input_counts[i];j++)if(!std::isfinite(inputs[i][j]))throw std::runtime_error("Nonfinite input");
   names.push_back(c->in_names[i].c_str());values.push_back(Ort::Value::CreateTensor<float>(memory,const_cast<float*>(inputs[i]),input_counts[i],c->in_shapes[i].data(),c->in_shapes[i].size()));
  }
  for(size_t i=0;i<no;i++){if(!outputs[i]||output_counts[i]!=c->out_sizes[i])throw std::runtime_error("Output buffer size mismatch");out_names.push_back(c->out_names[i].c_str());}
  auto result=c->session->Run(Ort::RunOptions{nullptr},names.data(),values.data(),ni,out_names.data(),no);
  for(size_t i=0;i<no;i++){
   if(result[i].GetTensorTypeAndShapeInfo().GetElementCount()!=output_counts[i])throw std::runtime_error("Unexpected output shape");
   const float* data=result[i].GetTensorData<float>();
   for(size_t j=0;j<output_counts[i];j++)if(!std::isfinite(data[j]))throw std::runtime_error("Nonfinite model output");
   std::copy_n(data,output_counts[i],outputs[i]);
  }
  return 0;
 }catch(const std::exception& e){error=e.what();return -1;}
}
extern "C" int mg_ort_run(void* context,const float* const* inputs,const size_t* counts,size_t n,
 float* const* outputs,const size_t* output_counts,size_t no){
 return run_impl(context,inputs,counts,n,outputs,output_counts,no,0);
}
extern "C" void* mg_ort_prepare_prefix(const float* const* inputs,const size_t* counts,size_t n){
 try{
  error.clear();if(!inputs||!counts||n==0||n>64)throw std::runtime_error("Invalid fixed input prefix");
  auto prefix=std::make_unique<InputPrefix>();
  for(size_t i=0;i<n;i++){
   if(!inputs[i]||!counts[i]||counts[i]>std::numeric_limits<size_t>::max()/sizeof(float))throw std::runtime_error("Invalid fixed input buffer");
   prefix->buffers.emplace_back(inputs[i],inputs[i]+counts[i]);
   for(float value:prefix->buffers.back())if(!std::isfinite(value))throw std::runtime_error("Nonfinite fixed input");
  }
  return prefix.release();
 }catch(const std::exception& e){error=e.what();return nullptr;}
}
extern "C" void mg_ort_release_prefix(void* prefix){delete static_cast<InputPrefix*>(prefix);}
extern "C" int mg_ort_run_with_prefix(void* context,const void* opaque,const float* const* inputs,
 const size_t* counts,size_t n,float* const* outputs,const size_t* output_counts,size_t no){
 try{
  if(!opaque||n>64||(n&&(!inputs||!counts)))throw std::runtime_error("Invalid prefix run");
  auto* prefix=static_cast<const InputPrefix*>(opaque);
  std::vector<const float*> merged;std::vector<size_t> sizes;
  for(const auto& buffer:prefix->buffers){merged.push_back(buffer.data());sizes.push_back(buffer.size());}
  for(size_t i=0;i<n;i++){merged.push_back(inputs[i]);sizes.push_back(counts[i]);}
  return run_impl(context,merged.data(),sizes.data(),merged.size(),outputs,output_counts,no,prefix->buffers.size());
 }catch(const std::exception& e){error=e.what();return -1;}
}
