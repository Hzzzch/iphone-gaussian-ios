#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Static float32 ONNX models only. CPU EP baseline; no QNN implied.
// One context must not be used concurrently. Error text is thread-local.
void* mg_ort_open(const char* model_path,int threads);
// iOS copy: cpu, coreml (ALL), coreml-ane (CPUAndNeuralEngine), coreml-gpu.
// CoreML requires MOBILEGS_COREML and a matching ORT build. allow_cpu_fallback
// permits unsupported ONNX nodes on CPU. library_dir/cache_output are reserved.
typedef struct {
 const char* backend;
 const char* library_dir;
 const char* diagnostics_dir;
 const char* cache_output;
 int allow_cpu_fallback;
} mg_ort_options;
void* mg_ort_open_ex(const char* model_path,int threads,const mg_ort_options* options);
void mg_ort_close(void* context);
const char* mg_ort_error(void);
int mg_ort_run(void* context,const float* const* inputs,const size_t* input_counts,
 size_t num_inputs,float* const* outputs,const size_t* output_counts,size_t num_outputs);
// Immutable, owned snapshots of a model's first N inputs. Validated once at
// creation; caller buffers can change or be released afterward. Model shape
// checks and finite checks of variable inputs / every output still run per call.
void* mg_ort_prepare_prefix(const float* const* inputs,const size_t* counts,size_t n);
void mg_ort_release_prefix(void* prefix);
int mg_ort_run_with_prefix(void* context,const void* prefix,const float* const* inputs,
 const size_t* counts,size_t n,float* const* outputs,const size_t* output_counts,size_t num_outputs);
#ifdef __cplusplus
}
#endif
