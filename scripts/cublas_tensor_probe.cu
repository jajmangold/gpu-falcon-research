#include <cublas_v2.h>
#include <cuda_fp16.h>
#include <cuda_runtime.h>

#include <cstdio>
#include <cstdlib>

static void check_cuda(cudaError_t status, const char *what) {
    if (status != cudaSuccess) {
        std::fprintf(stderr, "CUDA error at %s: %s\n", what, cudaGetErrorString(status));
        std::exit(2);
    }
}

static void check_cublas(cublasStatus_t status, const char *what) {
    if (status != CUBLAS_STATUS_SUCCESS) {
        std::fprintf(stderr, "cuBLAS error at %s: %d\n", what, static_cast<int>(status));
        std::exit(3);
    }
}

template <typename Fn>
static float time_ms(Fn fn, int warmup, int iters) {
    cudaEvent_t start;
    cudaEvent_t stop;
    check_cuda(cudaEventCreate(&start), "cudaEventCreate(start)");
    check_cuda(cudaEventCreate(&stop), "cudaEventCreate(stop)");

    for (int i = 0; i < warmup; ++i) {
        fn();
    }
    check_cuda(cudaDeviceSynchronize(), "warmup sync");

    check_cuda(cudaEventRecord(start), "record start");
    for (int i = 0; i < iters; ++i) {
        fn();
    }
    check_cuda(cudaEventRecord(stop), "record stop");
    check_cuda(cudaEventSynchronize(stop), "sync stop");

    float elapsed_ms = 0.0f;
    check_cuda(cudaEventElapsedTime(&elapsed_ms, start, stop), "elapsed");
    check_cuda(cudaEventDestroy(start), "destroy start");
    check_cuda(cudaEventDestroy(stop), "destroy stop");
    return elapsed_ms / static_cast<float>(iters);
}

static double gemm_tflops(int n, float avg_ms) {
    return (2.0 * static_cast<double>(n) * n * n) / (static_cast<double>(avg_ms) / 1000.0) / 1.0e12;
}

int main(int argc, char **argv) {
    int fp16_n = 8192;
    int fp32_n = 4096;
    int warmup = 5;
    int iters = 30;

    if (argc > 1) fp16_n = std::atoi(argv[1]);
    if (argc > 2) fp32_n = std::atoi(argv[2]);
    if (argc > 3) iters = std::atoi(argv[3]);

    cudaDeviceProp prop {};
    check_cuda(cudaGetDeviceProperties(&prop, 0), "cudaGetDeviceProperties");

    cublasHandle_t handle;
    check_cublas(cublasCreate(&handle), "cublasCreate");
    check_cublas(cublasSetMathMode(handle, CUBLAS_TENSOR_OP_MATH), "cublasSetMathMode");

    __half *a16 = nullptr;
    __half *b16 = nullptr;
    __half *c16 = nullptr;
    const size_t fp16_elems = static_cast<size_t>(fp16_n) * fp16_n;
    check_cuda(cudaMalloc(&a16, fp16_elems * sizeof(__half)), "malloc a16");
    check_cuda(cudaMalloc(&b16, fp16_elems * sizeof(__half)), "malloc b16");
    check_cuda(cudaMalloc(&c16, fp16_elems * sizeof(__half)), "malloc c16");
    check_cuda(cudaMemset(a16, 1, fp16_elems * sizeof(__half)), "memset a16");
    check_cuda(cudaMemset(b16, 2, fp16_elems * sizeof(__half)), "memset b16");
    check_cuda(cudaMemset(c16, 0, fp16_elems * sizeof(__half)), "memset c16");

    const float alpha32 = 1.0f;
    const float beta32 = 0.0f;
    const float fp16_avg_ms = time_ms(
        [&]() {
            check_cublas(
                cublasGemmEx(
                    handle,
                    CUBLAS_OP_N,
                    CUBLAS_OP_N,
                    fp16_n,
                    fp16_n,
                    fp16_n,
                    &alpha32,
                    a16,
                    CUDA_R_16F,
                    fp16_n,
                    b16,
                    CUDA_R_16F,
                    fp16_n,
                    &beta32,
                    c16,
                    CUDA_R_16F,
                    fp16_n,
                    CUBLAS_COMPUTE_32F_FAST_16F,
                    CUBLAS_GEMM_DEFAULT_TENSOR_OP),
                "cublasGemmEx fp16 tensor");
        },
        warmup,
        iters);

    check_cuda(cudaFree(a16), "free a16");
    check_cuda(cudaFree(b16), "free b16");
    check_cuda(cudaFree(c16), "free c16");

    float *a32 = nullptr;
    float *b32 = nullptr;
    float *c32 = nullptr;
    const size_t fp32_elems = static_cast<size_t>(fp32_n) * fp32_n;
    check_cuda(cudaMalloc(&a32, fp32_elems * sizeof(float)), "malloc a32");
    check_cuda(cudaMalloc(&b32, fp32_elems * sizeof(float)), "malloc b32");
    check_cuda(cudaMalloc(&c32, fp32_elems * sizeof(float)), "malloc c32");
    check_cuda(cudaMemset(a32, 1, fp32_elems * sizeof(float)), "memset a32");
    check_cuda(cudaMemset(b32, 2, fp32_elems * sizeof(float)), "memset b32");
    check_cuda(cudaMemset(c32, 0, fp32_elems * sizeof(float)), "memset c32");

    const float fp32_avg_ms = time_ms(
        [&]() {
            check_cublas(
                cublasSgemm(
                    handle,
                    CUBLAS_OP_N,
                    CUBLAS_OP_N,
                    fp32_n,
                    fp32_n,
                    fp32_n,
                    &alpha32,
                    a32,
                    fp32_n,
                    b32,
                    fp32_n,
                    &beta32,
                    c32,
                    fp32_n),
                "cublasSgemm fp32");
        },
        warmup,
        iters);

    check_cuda(cudaFree(a32), "free a32");
    check_cuda(cudaFree(b32), "free b32");
    check_cuda(cudaFree(c32), "free c32");
    check_cublas(cublasDestroy(handle), "cublasDestroy");

    std::printf("{\n");
    std::printf("  \"device_name\": \"%s\",\n", prop.name);
    std::printf("  \"compute_capability\": \"%d.%d\",\n", prop.major, prop.minor);
    std::printf("  \"sm_count\": %d,\n", prop.multiProcessorCount);
    std::printf("  \"fp16_tensor_n\": %d,\n", fp16_n);
    std::printf("  \"fp16_tensor_avg_ms\": %.6f,\n", fp16_avg_ms);
    std::printf("  \"fp16_tensor_tflops\": %.6f,\n", gemm_tflops(fp16_n, fp16_avg_ms));
    std::printf("  \"fp32_n\": %d,\n", fp32_n);
    std::printf("  \"fp32_avg_ms\": %.6f,\n", fp32_avg_ms);
    std::printf("  \"fp32_tflops\": %.6f\n", gemm_tflops(fp32_n, fp32_avg_ms));
    std::printf("}\n");
    return 0;
}

