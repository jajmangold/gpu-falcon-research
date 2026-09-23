#include <cublas_v2.h>
#include <cuda_runtime.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

static void check_cuda(cudaError_t status, const char *what) {
    if (status != cudaSuccess) {
        std::fprintf(stderr, "CUDA error at %s: %s\n", what, cudaGetErrorString(status));
        std::exit(2);
    }
}

static void check_cublas(cublasStatus_t status, const char *what) {
    if (status != CUBLAS_STATUS_SUCCESS) {
        std::fprintf(stderr, "cuBLAS error at %s: %d\n", what, static_cast<int>(status));
        std::exit(2);
    }
}

template <typename T>
static void fill_matrix(T *device, size_t elems, T value) {
    std::vector<T> host(elems, value);
    check_cuda(cudaMemcpy(device, host.data(), elems * sizeof(T), cudaMemcpyHostToDevice), "copy matrix");
}

static long double time_sgemm(cublasHandle_t handle, int n, int iters, float *a, float *b, float *c) {
    const float alpha = 1.0f;
    const float beta = 0.0f;
    check_cublas(cublasSgemm(handle, CUBLAS_OP_N, CUBLAS_OP_N, n, n, n, &alpha, a, n, b, n, &beta, c, n), "warmup sgemm");
    check_cuda(cudaDeviceSynchronize(), "warmup sgemm sync");

    cudaEvent_t start;
    cudaEvent_t stop;
    check_cuda(cudaEventCreate(&start), "sgemm event start");
    check_cuda(cudaEventCreate(&stop), "sgemm event stop");
    check_cuda(cudaEventRecord(start), "sgemm record start");
    for (int i = 0; i < iters; ++i) {
        check_cublas(cublasSgemm(handle, CUBLAS_OP_N, CUBLAS_OP_N, n, n, n, &alpha, a, n, b, n, &beta, c, n), "timed sgemm");
    }
    check_cuda(cudaEventRecord(stop), "sgemm record stop");
    check_cuda(cudaEventSynchronize(stop), "sgemm sync stop");

    float elapsed_ms = 0.0f;
    check_cuda(cudaEventElapsedTime(&elapsed_ms, start, stop), "sgemm elapsed");
    check_cuda(cudaEventDestroy(start), "sgemm destroy start");
    check_cuda(cudaEventDestroy(stop), "sgemm destroy stop");

    const long double flops = static_cast<long double>(iters) * 2.0L * n * n * n;
    return flops / (static_cast<long double>(elapsed_ms) / 1000.0L) / 1.0e12L;
}

static long double time_dgemm(cublasHandle_t handle, int n, int iters, double *a, double *b, double *c) {
    const double alpha = 1.0;
    const double beta = 0.0;
    check_cublas(cublasDgemm(handle, CUBLAS_OP_N, CUBLAS_OP_N, n, n, n, &alpha, a, n, b, n, &beta, c, n), "warmup dgemm");
    check_cuda(cudaDeviceSynchronize(), "warmup dgemm sync");

    cudaEvent_t start;
    cudaEvent_t stop;
    check_cuda(cudaEventCreate(&start), "dgemm event start");
    check_cuda(cudaEventCreate(&stop), "dgemm event stop");
    check_cuda(cudaEventRecord(start), "dgemm record start");
    for (int i = 0; i < iters; ++i) {
        check_cublas(cublasDgemm(handle, CUBLAS_OP_N, CUBLAS_OP_N, n, n, n, &alpha, a, n, b, n, &beta, c, n), "timed dgemm");
    }
    check_cuda(cudaEventRecord(stop), "dgemm record stop");
    check_cuda(cudaEventSynchronize(stop), "dgemm sync stop");

    float elapsed_ms = 0.0f;
    check_cuda(cudaEventElapsedTime(&elapsed_ms, start, stop), "dgemm elapsed");
    check_cuda(cudaEventDestroy(start), "dgemm destroy start");
    check_cuda(cudaEventDestroy(stop), "dgemm destroy stop");

    const long double flops = static_cast<long double>(iters) * 2.0L * n * n * n;
    return flops / (static_cast<long double>(elapsed_ms) / 1000.0L) / 1.0e12L;
}

int main(int argc, char **argv) {
    int n = 4096;
    int sgemm_iters = 10;
    int dgemm_iters = 5;
    if (argc > 1) n = std::atoi(argv[1]);
    if (argc > 2) sgemm_iters = std::atoi(argv[2]);
    if (argc > 3) dgemm_iters = std::atoi(argv[3]);

    cudaDeviceProp prop {};
    check_cuda(cudaGetDeviceProperties(&prop, 0), "device props");

    const size_t elems = static_cast<size_t>(n) * n;
    float *sf_a = nullptr;
    float *sf_b = nullptr;
    float *sf_c = nullptr;
    double *df_a = nullptr;
    double *df_b = nullptr;
    double *df_c = nullptr;
    check_cuda(cudaMalloc(&sf_a, elems * sizeof(float)), "malloc sf_a");
    check_cuda(cudaMalloc(&sf_b, elems * sizeof(float)), "malloc sf_b");
    check_cuda(cudaMalloc(&sf_c, elems * sizeof(float)), "malloc sf_c");
    check_cuda(cudaMalloc(&df_a, elems * sizeof(double)), "malloc df_a");
    check_cuda(cudaMalloc(&df_b, elems * sizeof(double)), "malloc df_b");
    check_cuda(cudaMalloc(&df_c, elems * sizeof(double)), "malloc df_c");

    fill_matrix(sf_a, elems, 1.0f);
    fill_matrix(sf_b, elems, 1.0f);
    check_cuda(cudaMemset(sf_c, 0, elems * sizeof(float)), "memset sf_c");
    fill_matrix(df_a, elems, 1.0);
    fill_matrix(df_b, elems, 1.0);
    check_cuda(cudaMemset(df_c, 0, elems * sizeof(double)), "memset df_c");

    cublasHandle_t handle;
    check_cublas(cublasCreate(&handle), "create handle");
    check_cublas(cublasSetMathMode(handle, CUBLAS_DEFAULT_MATH), "set default math mode");

    const long double sgemm_tflops = time_sgemm(handle, n, sgemm_iters, sf_a, sf_b, sf_c);
    const long double dgemm_tflops = time_dgemm(handle, n, dgemm_iters, df_a, df_b, df_c);

    std::printf("{\n");
    std::printf("  \"device_name\": \"%s\",\n", prop.name);
    std::printf("  \"compute_capability\": \"%d.%d\",\n", prop.major, prop.minor);
    std::printf("  \"sm_count\": %d,\n", prop.multiProcessorCount);
    std::printf("  \"clock_rate_khz\": %d,\n", prop.clockRate);
    std::printf("  \"memory_clock_rate_khz\": %d,\n", prop.memoryClockRate);
    std::printf("  \"n\": %d,\n", n);
    std::printf("  \"sgemm_iters\": %d,\n", sgemm_iters);
    std::printf("  \"dgemm_iters\": %d,\n", dgemm_iters);
    std::printf("  \"sgemm_tflops\": %.6Lf,\n", sgemm_tflops);
    std::printf("  \"dgemm_tflops\": %.6Lf,\n", dgemm_tflops);
    std::printf("  \"sgemm_to_dgemm_ratio\": %.6Lf,\n", sgemm_tflops / dgemm_tflops);
    std::printf("  \"dgemm_per_sm_tflops\": %.9Lf,\n", dgemm_tflops / prop.multiProcessorCount);
    std::printf("  \"sgemm_per_sm_tflops\": %.9Lf\n", sgemm_tflops / prop.multiProcessorCount);
    std::printf("}\n");

    check_cublas(cublasDestroy(handle), "destroy handle");
    check_cuda(cudaFree(sf_a), "free sf_a");
    check_cuda(cudaFree(sf_b), "free sf_b");
    check_cuda(cudaFree(sf_c), "free sf_c");
    check_cuda(cudaFree(df_a), "free df_a");
    check_cuda(cudaFree(df_b), "free df_b");
    check_cuda(cudaFree(df_c), "free df_c");
    return 0;
}
