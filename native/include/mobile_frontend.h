#pragma once
#include <stddef.h>
#include "mobile_onnx.h"
#ifdef __cplusplus
extern "C" {
#endif
// CPU baseline, synchronous. Call on a background thread, never Android UI thread.
// RGB file is exactly 1x3x518x518 little-endian float32 [0,1].
// Output directory must not exist. No shared model contexts across calls.
int mg_photo_reconstruct(const char* depth_model,const char* feature_model,
 const char* head_model,const char* rgb_file,int count,const char* output_dir,double fov_degrees);
// Version 2: decoded, upright, packed RGB8; exact bytes=width*height*3.
// Native integer center square crop, antialiased bilinear to 518, CHW float [0,1].
// No EXIF decoding here: caller applies orientation before providing RGB.
int mg_preprocess_rgb8(const unsigned char* input,size_t bytes,int width,int height,
 float* output,size_t output_floats);
// sampler: 0=reference, 1=balanced, 2=stable. Explicit per-call option.
// FOV describes the cropped square, not the original uncropped photograph.
int mg_photo_reconstruct_rgb8(const char* depth_model,const char* feature_model,
 const char* head_model,const unsigned char* rgb,size_t bytes,int width,int height,
 int count,const char* output_dir,double fov_degrees,int sampler);
// V3 adds explicit ORT intra-op threads. The legacy RGB8 API uses 4 threads.
int mg_photo_reconstruct_rgb8_v3(const char* depth_model,const char* feature_model,
 const char* head_model,const unsigned char* rgb,size_t bytes,int width,int height,
 int count,const char* output_dir,double fov_degrees,int sampler,int threads);
const char* mg_photo_error(void);
// V4: three explicit stage options (depth/features/head). CPU APIs above retain
// their behavior. Diagnostics and optional EPContext cache paths are per stage.
int mg_photo_reconstruct_rgb8_v4(const char* depth_model,const char* feature_model,
 const char* head_model,const unsigned char* rgb,size_t bytes,int width,int height,
 int count,const char* output_dir,double fov_degrees,int sampler,int threads,
const mg_ort_options* stage_options);
// Resident V5 session: owns all three model contexts. Options are copied at open.
// Open/close on a background thread. Do not close concurrently with a call.
// Concurrent reconstruction on one session is rejected (use separate sessions).
void* mg_photo_session_open(const char* depth_model,const char* feature_model,
 const char* head_model,int threads,const mg_ort_options* stage_options);
int mg_photo_session_reconstruct_rgb8(void* session,const unsigned char* rgb,
 size_t bytes,int width,int height,int count,const char* output_dir,
 double fov_degrees,int sampler);
void mg_photo_session_close(void* session);
#ifdef __cplusplus
}
#endif
