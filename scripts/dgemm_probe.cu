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

static void run_case(cublasHandle_t handle, int n, int iters) {
    double *a = nullptr;
    double *b = nullptr;
    double *c = nullptr;
    const size_t elems = static_cast<size_t>(n) * n;
    check_cuda(cudaMalloc(&a, elems * sizeof(double)), "malloc a");
    check_cuda(cudaMalloc(&b, elems * sizeof(double)), "malloc b");
    check_cuda(cudaMalloc(&c, elems * sizeof(double)), "malloc c");

    std::vector<double> host(elems, 1.0);
    check_cuda(cudaMemcpy(a, host.data(), elems * sizeof(double), cudaMemcpyHostToDevice), "copy a");
    check_cuda(cudaMemcpy(b, host.data(), elems * sizeof(double), cudaMemcpyHostToDevice), "copy b");
    check_cuda(cudaMemset(c, 0, elems * sizeof(double)), "memset c");

    const double alpha = 1.0;
    const double beta = 0.0;
    check_cublas(cublasDgemm(handle, CUBLAS_OP_N, CUBLAS_OP_N, n, n, n, &alpha, a, n, b, n, &beta, c, n), "warmup dgemm");
    check_cuda(cudaDeviceSynchronize(), "warmup sync");

    cudaEvent_t start;
    cudaEvent_t stop;
    check_cuda(cudaEventCreate(&start), "event start");
    check_cuda(cudaEventCreate(&stop), "event stop");
    check_cuda(cudaEventRecord(start), "record start");
    for (int i = 0; i < iters; ++i) {
        check_cublas(cublasDgemm(handle, CUBLAS_OP_N, CUBLAS_OP_N, n, n, n, &alpha, a, n, b, n, &beta, c, n), "timed dgemm");
    }
    check_cuda(cudaEventRecord(stop), "record stop");
    check_cuda(cudaEventSynchronize(stop), "sync stop");

    float elapsed_ms = 0.0f;
    check_cuda(cudaEventElapsedTime(&elapsed_ms, start, stop), "event elapsed");
    const long double flops = static_cast<long double>(iters) * 2.0L * n * n * n;
    const long double tflops = flops / (static_cast<long double>(elapsed_ms) / 1000.0L) / 1.0e12L;

    std::printf("    {\n");
    std::printf("      \"n\": %d,\n", n);
    std::printf("      \"iters\": %d,\n", iters);
    std::printf("      \"event_ms\": %.6f,\n", elapsed_ms);
    std::printf("      \"dgemm_tflops\": %.6Lf\n", tflops);
    std::printf("    }");

    check_cuda(cudaEventDestroy(start), "destroy start");
    check_cuda(cudaEventDestroy(stop), "destroy stop");
    check_cuda(cudaFree(a), "free a");
    check_cuda(cudaFree(b), "free b");
    check_cuda(cudaFree(c), "free c");
}

int main(int argc, char **argv) {
    int n = 4096;
    int iters = 5;
    if (argc > 1) n = std::atoi(argv[1]);
    if (argc > 2) iters = std::atoi(argv[2]);

    cudaDeviceProp prop {};
    check_cuda(cudaGetDeviceProperties(&prop, 0), "device props");

    cublasHandle_t handle;
    check_cublas(cublasCreate(&handle), "create handle");

    std::printf("{\n");
    std::printf("  \"device_name\": \"%s\",\n", prop.name);
    std::printf("  \"compute_capability\": \"%d.%d\",\n", prop.major, prop.minor);
    std::printf("  \"sm_count\": %d,\n", prop.multiProcessorCount);
    std::printf("  \"clock_rate_khz\": %d,\n", prop.clockRate);
    std::printf("  \"cases\": [\n");
    run_case(handle, n, iters);
    std::printf("\n  ]\n");
    std::printf("}\n");

    check_cublas(cublasDestroy(handle), "destroy handle");
    return 0;
}
