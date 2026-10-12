#include "mobile_frontend.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
int main(int argc,char** argv){
 try{
  if(argc!=17&&argc!=18)throw std::runtime_error("usage: photo_session_bench DEPTH FEATURES HEAD RGB8 W H OUT FOV THREADS RUNS DEPTH_EP FEATURE_EP HEAD_EP LIB_DIR cold|reuse DIAGNOSTICS_0_OR_1 [ALLOW_DEPTH_CPU_FALLBACK_0_OR_1]");
  int w=std::stoi(argv[5]),h=std::stoi(argv[6]),threads=std::stoi(argv[9]),runs=std::stoi(argv[10]);
  if(w<2||h<2||w>16384||h>16384||size_t(w)*h>40000000||threads<1||threads>16||runs<1||runs>20)throw std::runtime_error("Invalid parameters");
  std::string mode=argv[15],diag=argv[16];
  std::string fallback=argc==18?argv[17]:"0";
  if(fallback!="0"&&fallback!="1")throw std::runtime_error("Invalid depth fallback option");
  if((mode!="cold"&&mode!="reuse")||(diag!="0"&&diag!="1"))throw std::runtime_error("Invalid mode");
  for(int i=11;i<14;i++){std::string ep=argv[i];if(ep!="cpu"&&ep!="coreml"&&ep!="coreml-ane"&&ep!="coreml-gpu")throw std::runtime_error("Invalid backend");}
  fs::path out=argv[7];if(!fs::create_directory(out))throw std::runtime_error("Output must not exist");
  std::vector<unsigned char> rgb(size_t(w)*h*3);std::ifstream input(argv[4],std::ios::binary);
  if(!input.read(reinterpret_cast<char*>(rgb.data()),rgb.size())||input.peek()!=EOF)throw std::runtime_error("Invalid RGB8 file");
  std::array<mg_ort_options,3> options{};std::array<std::string,3> paths;
  for(int i=0;i<3;i++){
   if(diag=="1")paths[i]=(out/("diagnostics-"+std::to_string(i))).string();
   options[i]={argv[11+i],argv[14],paths[i].c_str(),nullptr,1};
   if(i==0&&fallback=="1")options[i].allow_cpu_fallback=1;
  }
  std::unique_ptr<void,decltype(&mg_photo_session_close)> session(nullptr,mg_photo_session_close);
  double setup=0;auto start=Clock::now();
  if(mode=="reuse"){
   session.reset(mg_photo_session_open(argv[1],argv[2],argv[3],threads,options.data()));
   if(!session)throw std::runtime_error(mg_photo_error());
   setup=std::chrono::duration<double>(Clock::now()-start).count();
  }
  std::cout<<"session_setup_seconds: "<<setup<<std::endl;
  std::vector<double> elapsed;
  for(int run=0;run<runs;run++){
   auto output=(out/("run-"+std::to_string(run))).string();start=Clock::now();
   int rc=session?mg_photo_session_reconstruct_rgb8(session.get(),rgb.data(),rgb.size(),w,h,100000,output.c_str(),std::stod(argv[8]),2):
     mg_photo_reconstruct_rgb8_v4(argv[1],argv[2],argv[3],rgb.data(),rgb.size(),w,h,100000,output.c_str(),std::stod(argv[8]),2,threads,options.data());
   elapsed.push_back(std::chrono::duration<double>(Clock::now()-start).count());
   if(rc)throw std::runtime_error(mg_photo_error());
   std::cout<<"photo_seconds["<<run<<"]: "<<elapsed.back()<<std::endl;
  }
  start=Clock::now();session.reset();double teardown=std::chrono::duration<double>(Clock::now()-start).count();
  std::ofstream report(out/"benchmark.json");
  report<<"{\"complete\":true,\"mode\":\""<<mode<<"\",\"diagnostics\":"<<(diag=="1"?"true":"false")<<",\"threads\":"<<threads<<",\"setup_seconds\":"<<setup<<",\"teardown_seconds\":"<<teardown<<",\"photo_seconds\":[";
  for(size_t i=0;i<elapsed.size();i++){if(i)report<<',';report<<elapsed[i];}report<<"]}\n";
  if(!report)throw std::runtime_error("Report write failed");
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
