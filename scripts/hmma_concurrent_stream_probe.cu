#include <cuda_fp16.h>
#include <cuda_runtime.h>
#include <mma.h>

#include <cstdio>
#include <cstdlib>
#include <chrono>

using namespace nvcuda;

static void check_cuda(cudaError_t status, const char *what) {
    if (status != cudaSuccess) {
        std::fprintf(stderr, "CUDA error at %s: %s\n", what, cudaGetErrorString(status));
        std::exit(2);
    }
}

__global__ void hmma_independent4_kernel(const half *a,
                                         const half *b,
                                         float *out,
                                         unsigned long long *cycles,
                                         int loops) {
    using FragA = wmma::fragment<wmma::matrix_a, 16, 16, 16, half, wmma::row_major>;
    using FragB = wmma::fragment<wmma::matrix_b, 16, 16, 16, half, wmma::col_major>;
    using FragC = wmma::fragment<wmma::accumulator, 16, 16, 16, float>;

    FragA fa;
    FragB fb;
    FragC acc0;
    FragC acc1;
    FragC acc2;
    FragC acc3;
    wmma::load_matrix_sync(fa, a, 16);
    wmma::load_matrix_sync(fb, b, 16);
    wmma::fill_fragment(acc0, 0.0f);
    wmma::fill_fragment(acc1, 0.0f);
    wmma::fill_fragment(acc2, 0.0f);
    wmma::fill_fragment(acc3, 0.0f);

    __syncthreads();
    const unsigned long long start = clock64();
#pragma unroll 1
    for (int i = 0; i < loops; ++i) {
        wmma::mma_sync(acc0, fa, fb, acc0);
        wmma::mma_sync(acc1, fa, fb, acc1);
        wmma::mma_sync(acc2, fa, fb, acc2);
        wmma::mma_sync(acc3, fa, fb, acc3);
    }
    const unsigned long long stop = clock64();

    const size_t base = static_cast<size_t>(blockIdx.x) * 1024;
    wmma::store_matrix_sync(out + base, acc0, 16, wmma::mem_row_major);
    wmma::store_matrix_sync(out + base + 256, acc1, 16, wmma::mem_row_major);
    wmma::store_matrix_sync(out + base + 512, acc2, 16, wmma::mem_row_major);
    wmma::store_matrix_sync(out + base + 768, acc3, 16, wmma::mem_row_major);

    if (threadIdx.x == 0) {
        cycles[blockIdx.x] = stop - start;
    }
}

struct Buffers {
    half *a = nullptr;
    half *b = nullptr;
    float *out = nullptr;
    unsigned long long *cycles = nullptr;
};

static void alloc_buffers(Buffers *buf, int blocks) {
    check_cuda(cudaMalloc(&buf->a, 16 * 16 * sizeof(half)), "malloc a");
    check_cuda(cudaMalloc(&buf->b, 16 * 16 * sizeof(half)), "malloc b");
    check_cuda(cudaMalloc(&buf->out, static_cast<size_t>(blocks) * 1024 * sizeof(float)), "malloc out");
    check_cuda(cudaMalloc(&buf->cycles, static_cast<size_t>(blocks) * sizeof(unsigned long long)), "malloc cycles");
    check_cuda(cudaMemset(buf->a, 1, 16 * 16 * sizeof(half)), "memset a");
    check_cuda(cudaMemset(buf->b, 2, 16 * 16 * sizeof(half)), "memset b");
    check_cuda(cudaMemset(buf->out, 0, static_cast<size_t>(blocks) * 1024 * sizeof(float)), "memset out");
    check_cuda(cudaMemset(buf->cycles, 0, static_cast<size_t>(blocks) * sizeof(unsigned long long)), "memset cycles");
}

static void free_buffers(Buffers *buf) {
    check_cuda(cudaFree(buf->a), "free a");
    check_cuda(cudaFree(buf->b), "free b");
    check_cuda(cudaFree(buf->out), "free out");
    check_cuda(cudaFree(buf->cycles), "free cycles");
}

static long double tflops_for(int kernels, int blocks, int loops, float elapsed_ms) {
    const long double mma_per_kernel = static_cast<long double>(blocks) * loops * 4.0L;
    const long double flops_per_mma = 8192.0L;
    return kernels * mma_per_kernel * flops_per_mma /
           (static_cast<long double>(elapsed_ms) / 1000.0L) / 1.0e12L;
}

static void run_case(const char *label, int kernels, int blocks, int loops) {
    Buffers b0;
    Buffers b1;
    alloc_buffers(&b0, blocks);
    if (kernels == 2) {
        alloc_buffers(&b1, blocks);
    }

    cudaStream_t s0;
    cudaStream_t s1;
    check_cuda(cudaStreamCreateWithFlags(&s0, cudaStreamNonBlocking), "create s0");
    check_cuda(cudaStreamCreateWithFlags(&s1, cudaStreamNonBlocking), "create s1");

    hmma_independent4_kernel<<<blocks, 32, 0, s0>>>(b0.a, b0.b, b0.out, b0.cycles, 16);
    if (kernels == 2) {
        hmma_independent4_kernel<<<blocks, 32, 0, s1>>>(b1.a, b1.b, b1.out, b1.cycles, 16);
    }
    check_cuda(cudaGetLastError(), "warmup launch");
    check_cuda(cudaDeviceSynchronize(), "warmup sync");

    const auto start = std::chrono::steady_clock::now();
    hmma_independent4_kernel<<<blocks, 32, 0, s0>>>(b0.a, b0.b, b0.out, b0.cycles, loops);
    if (kernels == 2) {
        hmma_independent4_kernel<<<blocks, 32, 0, s1>>>(b1.a, b1.b, b1.out, b1.cycles, loops);
    }
    check_cuda(cudaGetLastError(), "timed launch");
    check_cuda(cudaDeviceSynchronize(), "timed sync");
    const auto stop = std::chrono::steady_clock::now();
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(stop - start).count();

    std::printf("    {\n");
    std::printf("      \"label\": \"%s\",\n", label);
    std::printf("      \"kernels\": %d,\n", kernels);
    std::printf("      \"blocks_per_kernel\": %d,\n", blocks);
    std::printf("      \"loops\": %d,\n", loops);
    std::printf("      \"host_elapsed_ms\": %.6f,\n", elapsed_ms);
    std::printf("      \"combined_tflops\": %.6Lf,\n", tflops_for(kernels, blocks, loops, elapsed_ms));
    std::printf("      \"per_kernel_tflops_if_even_split\": %.6Lf\n", tflops_for(kernels, blocks, loops, elapsed_ms) / kernels);
    std::printf("    }");

    check_cuda(cudaStreamDestroy(s0), "destroy s0");
    check_cuda(cudaStreamDestroy(s1), "destroy s1");
    free_buffers(&b0);
    if (kernels == 2) {
        free_buffers(&b1);
    }
}

int main(int argc, char **argv) {
    int blocks = 2048;
    int loops = 1024;
    if (argc > 1) blocks = std::atoi(argv[1]);
    if (argc > 2) loops = std::atoi(argv[2]);

    if (blocks <= 0 || loops <= 0) {
        std::fprintf(stderr, "usage: %s [blocks>0] [loops>0]\n", argv[0]);
        return 2;
    }

    cudaDeviceProp prop {};
    check_cuda(cudaGetDeviceProperties(&prop, 0), "device props");

    std::printf("{\n");
    std::printf("  \"device_name\": \"%s\",\n", prop.name);
    std::printf("  \"compute_capability\": \"%d.%d\",\n", prop.major, prop.minor);
    std::printf("  \"sm_count\": %d,\n", prop.multiProcessorCount);
    std::printf("  \"clock_rate_khz\": %d,\n", prop.clockRate);
    std::printf("  \"cases\": [\n");
    run_case("single_kernel", 1, blocks, loops);
    std::printf(",\n");
    run_case("two_concurrent_kernels", 2, blocks, loops);
    std::printf("\n  ]\n");
    std::printf("}\n");
    return 0;
}
