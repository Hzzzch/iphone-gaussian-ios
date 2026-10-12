#include "mobile_geometry.h"
#include <cmath>
#include <iostream>
int main() {
    float image[]={1,2,3,4}, coords[]={0,0}, result[3]={};
    if(mg_sample(image,1,2,2,coords,1,result)||std::abs(result[0]-2.5f)>1e-6f) return 1;
    if(mg_sample(image,1,0,2,coords,1,result)!=-1) return 2;
    std::cout<<"geometry smoke passed; inference and rendering not integrated\n";
}
