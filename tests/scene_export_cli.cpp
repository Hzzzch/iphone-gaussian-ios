#include "scene_export.h"
#include <exception>
#include <iostream>
#include <cstdlib>
int main(int argc,char** argv){
 if(argc!=3)return 2;
 try{gs_export_scene(argv[1],std::stod(argv[2]));return 0;}
 catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
