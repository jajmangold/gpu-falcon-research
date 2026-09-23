#include <cuda_fp16.h>
#include <cuda_runtime.h>
#include <mma.h>

#include <cstdio>
#include <cstdlib>

using namespace nvcuda;

static void check_cuda(cudaError_t status, const char *what) {
    if (status != cudaSuccess) {
        std::fprintf(stderr, "CUDA error at %s: %s\n", what, cudaGetErrorString(status));
        std::exit(2);
    }
}

__global__ void wmma_gemm_kernel(const half *a, const half *b, float *c, int n) {
    const int tile_m = blockIdx.y;
    const int tile_n = blockIdx.x;

    wmma::fragment<wmma::matrix_a, 16, 16, 16, half, wmma::row_major> a_frag;
    wmma::fragment<wmma::matrix_b, 16, 16, 16, half, wmma::row_major> b_frag;
    wmma::fragment<wmma::accumulator, 16, 16, 16, float> c_frag;
    wmma::fill_fragment(c_frag, 0.0f);

    for (int k = 0; k < n; k += 16) {
        const half *a_tile = a + (tile_m * 16) * n + k;
        const half *b_tile = b + k * n + (tile_n * 16);
        wmma::load_matrix_sync(a_frag, a_tile, n);
        wmma::load_matrix_sync(b_frag, b_tile, n);
        wmma::mma_sync(c_frag, a_frag, b_frag, c_frag);
    }

    float *c_tile = c + (tile_m * 16) * n + (tile_n * 16);
    wmma::store_matrix_sync(c_tile, c_frag, n, wmma::mem_row_major);
}

static float time_ms(int n, int warmup, int iters, const half *a, const half *b, float *c) {
    dim3 block(32, 1, 1);
    dim3 grid(n / 16, n / 16, 1);

    cudaEvent_t start;
    cudaEvent_t stop;
    check_cuda(cudaEventCreate(&start), "cudaEventCreate(start)");
    check_cuda(cudaEventCreate(&stop), "cudaEventCreate(stop)");

    for (int i = 0; i < warmup; ++i) {
        wmma_gemm_kernel<<<grid, block>>>(a, b, c, n);
    }
    check_cuda(cudaGetLastError(), "warmup kernel launch");
    check_cuda(cudaDeviceSynchronize(), "warmup sync");

    check_cuda(cudaEventRecord(start), "record start");
    for (int i = 0; i < iters; ++i) {
        wmma_gemm_kernel<<<grid, block>>>(a, b, c, n);
    }
    check_cuda(cudaEventRecord(stop), "record stop");
    check_cuda(cudaEventSynchronize(stop), "sync stop");

    float elapsed_ms = 0.0f;
    check_cuda(cudaEventElapsedTime(&elapsed_ms, start, stop), "elapsed");
    check_cuda(cudaEventDestroy(start), "destroy start");
    check_cuda(cudaEventDestroy(stop), "destroy stop");
    return elapsed_ms / static_cast<float>(iters);
}

int main(int argc, char **argv) {
    int n = 4096;
    int iters = 10;
    int warmup = 3;
    if (argc > 1) n = std::atoi(argv[1]);
    if (argc > 2) iters = std::atoi(argv[2]);
    if (n % 16 != 0) {
        std::fprintf(stderr, "n must be divisible by 16\n");
        return 2;
    }

    cudaDeviceProp prop {};
    check_cuda(cudaGetDeviceProperties(&prop, 0), "cudaGetDeviceProperties");

    half *a = nullptr;
    half *b = nullptr;
    float *c = nullptr;
    const size_t elems = static_cast<size_t>(n) * n;
    check_cuda(cudaMalloc(&a, elems * sizeof(half)), "malloc a");
    check_cuda(cudaMalloc(&b, elems * sizeof(half)), "malloc b");
    check_cuda(cudaMalloc(&c, elems * sizeof(float)), "malloc c");
    check_cuda(cudaMemset(a, 1, elems * sizeof(half)), "memset a");
    check_cuda(cudaMemset(b, 2, elems * sizeof(half)), "memset b");
    check_cuda(cudaMemset(c, 0, elems * sizeof(float)), "memset c");

    const float avg_ms = time_ms(n, warmup, iters, a, b, c);
    const double tflops = (2.0 * static_cast<double>(n) * n * n) / (static_cast<double>(avg_ms) / 1000.0) / 1.0e12;

    check_cuda(cudaFree(a), "free a");
    check_cuda(cudaFree(b), "free b");
    check_cuda(cudaFree(c), "free c");

    std::printf("{\n");
    std::printf("  \"device_name\": \"%s\",\n", prop.name);
    std::printf("  \"compute_capability\": \"%d.%d\",\n", prop.major, prop.minor);
    std::printf("  \"sm_count\": %d,\n", prop.multiProcessorCount);
    std::printf("  \"wmma_n\": %d,\n", n);
    std::printf("  \"wmma_avg_ms\": %.6f,\n", avg_ms);
    std::printf("  \"wmma_tflops\": %.6f\n", tflops);
    std::printf("}\n");
    return 0;
}

