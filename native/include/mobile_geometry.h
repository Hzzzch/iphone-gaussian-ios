#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// Contiguous float32 buffers, no aliasing. Caller owns storage.
// Return 0 on success, -1 invalid input, -2 nonfinite result. No exceptions cross ABI.
// fmap C,H,W; coords N,2 in (y,x) normalized coordinates; output N,C.
int mg_sample(const float* fmap, int c, int h, int w,
              const float* coords, int n, float* output);
// Exported feature sampler: clamp coords to +/- (1-1e-6), zero padding.
int mg_sample_features(const float* fmap, int c, int h, int w,
                       const float* coords, int n, float* output);
// align_corners=false, border padding. Input/output CHW.
int mg_resize(const float* input, int c, int h, int w, int oh, int ow, float* output);
// Factorized MoGe coordinate head (3,H,W) -> affine points (H,W,3).
// This does NOT perform focal/shift solving or metric scale recovery.
int mg_affine(const float* coord, int h, int w, float* points);
// Finite positive depth H,W -> camera XYZ H,W,3. Integer pixel-center convention.
int mg_unproject(const float* depth, int h, int w, float fx, float fy,
                 float cx, float cy, float* points);
// Fixed-focal depth recovery. points H,W,3; mask bytes; logscale is log metric scale.
// -3 allocation failure. Invalid output must be discarded on any nonzero status.
int mg_recover(const float* points,const unsigned char* mask,int h,int w,
               double focal,double logscale,float* depth,double* solved_shift);
// Default InfiniSplat config; camera frame, no external pose. Float inputs;
// global_depth_min is min over ALL sampled depths, shared across chunks.
// Outputs means N,3; covariance N,3,3; linear RGB N,3; opacity N.
int mg_decode(const float* delta,const float* coords,const float* depths,
              const float* rgb,const int* kinds,const float* areas,int n,int h,int w,
              double fx,double fy,double cx,double cy,double global_depth_min,
              float* means,float* covs,float* colors,float* opacities);
// Deterministic default surface sampler. RGB CHW optional. Returns produced count,
// 0 when no supported surface, negative on error. Output capacities: 2*count,count,count.
int mg_surface_samples(const float* depth,const float* rgb,int h,int w,
 double fx,double fy,double cx,double cy,int count,float* coords,int* kinds,float* areas);
// Adaptive grid anchors with a reserved face budget for low point counts.
int mg_surface_samples_balanced(const float* depth,const float* rgb,int h,int w,
 double fx,double fy,double cx,double cy,int count,float* coords,int* kinds,float* areas);
// Cross-architecture mode: canonicalizes depth/RGB before all discrete sampling
// decisions. Designed for reproducible ARM64/x86 validation.
int mg_surface_samples_stable(const float* depth,const float* rgb,int h,int w,
 double fx,double fy,double cx,double cy,int count,float* coords,int* kinds,float* areas);
#ifdef __cplusplus
}
#endif
