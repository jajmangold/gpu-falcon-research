#include <cublas_v2.h>
#include <cuda_fp16.h>
#include <cuda_runtime.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

struct Problem {
    int m;
    int n;
    int k;
    bool trans_a;
    bool trans_b;
    const char *label;
    double public_tflops;
};

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

static double tflops(const Problem &p, float avg_ms) {
    return (2.0 * static_cast<double>(p.m) * p.n * p.k) / (static_cast<double>(avg_ms) / 1000.0) / 1.0e12;
}

int main(int argc, char **argv) {
    int iters = 30;
    int warmup = 5;
    if (argc > 1) iters = std::atoi(argv[1]);
    if (argc > 2) warmup = std::atoi(argv[2]);

    const std::vector<Problem> problems = {
        {1760, 7000, 1760, false, false, "DeepBench-V100-FP16-row9", 89.415257731958761},
        {2048, 7000, 2048, false, false, "DeepBench-V100-FP16-row14", 98.030477462437403},
        {2560, 7000, 2560, false, false, "DeepBench-V100-FP16-row19", 103.79004524886878},
        {4096, 7000, 4096, false, false, "DeepBench-V100-FP16-row24", 112.2758240917782},
    };

    cudaDeviceProp prop {};
    check_cuda(cudaGetDeviceProperties(&prop, 0), "cudaGetDeviceProperties");

    cublasHandle_t handle;
    check_cublas(cublasCreate(&handle), "cublasCreate");
    check_cublas(cublasSetMathMode(handle, CUBLAS_TENSOR_OP_MATH), "cublasSetMathMode");

    std::printf("{\n");
    std::printf("  \"device_name\": \"%s\",\n", prop.name);
    std::printf("  \"compute_capability\": \"%d.%d\",\n", prop.major, prop.minor);
    std::printf("  \"sm_count\": %d,\n", prop.multiProcessorCount);
    std::printf("  \"benchmark_source\": \"DeepBench_NV_V100.xlsx Results - FP16 ip, Mixed math\",\n");
    std::printf("  \"problems\": [\n");

    for (size_t idx = 0; idx < problems.size(); ++idx) {
        const Problem &p = problems[idx];
        const int rows_a = p.trans_a ? p.k : p.m;
        const int cols_a = p.trans_a ? p.m : p.k;
        const int rows_b = p.trans_b ? p.n : p.k;
        const int cols_b = p.trans_b ? p.k : p.n;
        const int lda = rows_a;
        const int ldb = rows_b;
        const int ldc = p.m;
        const size_t a_elems = static_cast<size_t>(rows_a) * cols_a;
        const size_t b_elems = static_cast<size_t>(rows_b) * cols_b;
        const size_t c_elems = static_cast<size_t>(p.m) * p.n;

        __half *a = nullptr;
        __half *b = nullptr;
        __half *c = nullptr;
        check_cuda(cudaMalloc(&a, a_elems * sizeof(__half)), "malloc a");
        check_cuda(cudaMalloc(&b, b_elems * sizeof(__half)), "malloc b");
        check_cuda(cudaMalloc(&c, c_elems * sizeof(__half)), "malloc c");
        check_cuda(cudaMemset(a, 1, a_elems * sizeof(__half)), "memset a");
        check_cuda(cudaMemset(b, 2, b_elems * sizeof(__half)), "memset b");
        check_cuda(cudaMemset(c, 0, c_elems * sizeof(__half)), "memset c");

        const float alpha = 1.0f;
        const float beta = 0.0f;
        const cublasOperation_t op_a = p.trans_a ? CUBLAS_OP_T : CUBLAS_OP_N;
        const cublasOperation_t op_b = p.trans_b ? CUBLAS_OP_T : CUBLAS_OP_N;
        const float avg_ms = time_ms(
            [&]() {
                check_cublas(
                    cublasGemmEx(
                        handle,
                        op_a,
                        op_b,
                        p.m,
                        p.n,
                        p.k,
                        &alpha,
                        a,
                        CUDA_R_16F,
                        lda,
                        b,
                        CUDA_R_16F,
                        ldb,
                        &beta,
                        c,
                        CUDA_R_16F,
                        ldc,
                        CUBLAS_COMPUTE_32F_FAST_16F,
                        CUBLAS_GEMM_DEFAULT_TENSOR_OP),
                    "cublasGemmEx");
            },
            warmup,
            iters);

        check_cuda(cudaFree(a), "free a");
        check_cuda(cudaFree(b), "free b");
        check_cuda(cudaFree(c), "free c");

        const double local_tflops = tflops(p, avg_ms);
        std::printf("    {\n");
        std::printf("      \"label\": \"%s\",\n", p.label);
        std::printf("      \"m\": %d, \"n\": %d, \"k\": %d,\n", p.m, p.n, p.k);
        std::printf("      \"trans_a\": \"%c\", \"trans_b\": \"%c\",\n", p.trans_a ? 'T' : 'N', p.trans_b ? 'T' : 'N');
        std::printf("      \"avg_ms\": %.6f,\n", avg_ms);
        std::printf("      \"local_tflops\": %.6f,\n", local_tflops);
        std::printf("      \"public_v100_tflops\": %.6f,\n", p.public_tflops);
        std::printf("      \"local_to_public_ratio\": %.6f\n", local_tflops / p.public_tflops);
        std::printf("    }%s\n", idx + 1 == problems.size() ? "" : ",");
    }

    check_cublas(cublasDestroy(handle), "cublasDestroy");
    std::printf("  ]\n");
    std::printf("}\n");
    return 0;
}
