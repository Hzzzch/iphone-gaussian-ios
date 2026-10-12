#import "GSEngine.h"
#include "mobile_frontend.h"
#include "scene_export.h"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

@implementation GSEngine {
    void *_session;
    std::string _key;
}
- (void)close { if (_session) mg_photo_session_close(_session); _session=nullptr; _key.clear(); }
- (void)dealloc { [self close]; }
- (NSDictionary *)reconstruct:(UIImage *)image models:(NSURL *)models output:(NSURL *)output
    backend:(NSString *)backend fov:(double)fov error:(NSError **)error {
    using Clock=std::chrono::steady_clock;
    auto seconds=[](Clock::time_point t){return std::chrono::duration<double>(Clock::now()-t).count();};
    try {
        if (!std::isfinite(fov)||fov<10||fov>120||image.size.width<=0||image.size.height<=0)
            throw std::runtime_error("Invalid image/FOV");
        // UIKit drawing applies UIImage orientation; explicit RGBA byte order.
        double factor=std::min(1.0,1280.0/std::max(image.size.width,image.size.height));
        size_t w=std::max(1.0,std::floor(image.size.width*factor));
        size_t h=std::max(1.0,std::floor(image.size.height*factor));
        UIGraphicsImageRendererFormat *format=[UIGraphicsImageRendererFormat defaultFormat];
        format.scale=1; format.opaque=YES;
        UIGraphicsImageRenderer *renderer=[[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(w,h) format:format];
        UIImage *upright=[renderer imageWithActions:^(UIGraphicsImageRendererContext *ctx){
            [[UIColor blackColor] setFill]; [ctx fillRect:CGRectMake(0,0,w,h)];
            [image drawInRect:CGRectMake(0,0,w,h)];
        }];
        std::vector<unsigned char> rgba(w*h*4),rgb(w*h*3);
        CGColorSpaceRef color=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef context=CGBitmapContextCreate(rgba.data(),w,h,8,w*4,color,kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);
        CGColorSpaceRelease(color);
        if(!context)throw std::runtime_error("Cannot create RGB image buffer");
        CGContextDrawImage(context,CGRectMake(0,0,w,h),upright.CGImage); CGContextRelease(context);
        for(size_t i=0;i<w*h;i++)for(int c=0;c<3;c++)rgb[i*3+c]=rgba[i*4+c];
        std::string key=std::string(models.path.UTF8String)+"|"+backend.UTF8String;
        double setup=0; auto start=Clock::now();
        if(!_session||_key!=key){
            [self close];
            NSURL *profile=[[output URLByDeletingLastPathComponent] URLByAppendingPathComponent:[output.lastPathComponent stringByAppendingString:@"-profiles"]];
            [[NSFileManager defaultManager] createDirectoryAtURL:profile withIntermediateDirectories:YES attributes:nil error:nil];
            std::string dirs[3]; mg_ort_options options[3]{};
            for(int i=0;i<3;i++){
                dirs[i]=std::string(profile.path.UTF8String)+"/"+std::to_string(i);
                options[i]={backend.UTF8String,nullptr,dirs[i].c_str(),nullptr,1};
            }
            _session=mg_photo_session_open(
                [[models URLByAppendingPathComponent:@"depth.onnx"] fileSystemRepresentation],
                [[models URLByAppendingPathComponent:@"features.onnx"] fileSystemRepresentation],
                [[models URLByAppendingPathComponent:@"head.onnx"] fileSystemRepresentation],4,options);
            if(!_session)throw std::runtime_error(mg_photo_error());
            _key=key; setup=seconds(start);
        }
        start=Clock::now();
        if(mg_photo_session_reconstruct_rgb8(_session,rgb.data(),rgb.size(),int(w),int(h),100000,
            output.fileSystemRepresentation,fov,2))throw std::runtime_error(mg_photo_error());
        double inference=seconds(start); start=Clock::now();
        gs_export_scene(output.fileSystemRepresentation,fov);
        NSDictionary *result=@{@"setup_seconds":@(setup),@"reconstruct_seconds":@(inference),
            @"export_seconds":@(seconds(start)),@"requested_backend":backend,
            @"ane_verified":@NO,@"cpu_fallback_allowed":@YES,@"gaussians":@100000};
        NSData *json=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil];
        if(![json writeToURL:[output URLByAppendingPathComponent:@"app_result.json"] options:NSDataWritingAtomic error:error])return nil;
        return result;
    } catch(const std::exception& e) {
        if(error)*error=[NSError errorWithDomain:@"MobileGS" code:1 userInfo:@{NSLocalizedDescriptionKey:@(e.what())}];
        return nil;
    }
}
@end
