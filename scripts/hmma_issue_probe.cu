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

__global__ void hmma_dependent_kernel(const half *a, const half *b, float *out, unsigned long long *cycles, int loops) {
    using FragA = wmma::fragment<wmma::matrix_a, 16, 16, 16, half, wmma::row_major>;
    using FragB = wmma::fragment<wmma::matrix_b, 16, 16, 16, half, wmma::col_major>;
    using FragC = wmma::fragment<wmma::accumulator, 16, 16, 16, float>;

    FragA fa;
    FragB fb;
    FragC acc;
    wmma::load_matrix_sync(fa, a, 16);
    wmma::load_matrix_sync(fb, b, 16);
    wmma::fill_fragment(acc, 0.0f);

    __syncthreads();
    const unsigned long long start = clock64();
    #pragma unroll 1
    for (int i = 0; i < loops; ++i) {
        wmma::mma_sync(acc, fa, fb, acc);
    }
    const unsigned long long stop = clock64();

    wmma::store_matrix_sync(out + blockIdx.x * 256, acc, 16, wmma::mem_row_major);
    if (threadIdx.x == 0) {
        cycles[blockIdx.x] = stop - start;
    }
}

__global__ void hmma_independent4_kernel(const half *a, const half *b, float *out, unsigned long long *cycles, int loops) {
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

    wmma::store_matrix_sync(out + blockIdx.x * 1024, acc0, 16, wmma::mem_row_major);
    wmma::store_matrix_sync(out + blockIdx.x * 1024 + 256, acc1, 16, wmma::mem_row_major);
    wmma::store_matrix_sync(out + blockIdx.x * 1024 + 512, acc2, 16, wmma::mem_row_major);
    wmma::store_matrix_sync(out + blockIdx.x * 1024 + 768, acc3, 16, wmma::mem_row_major);
    if (threadIdx.x == 0) {
        cycles[blockIdx.x] = stop - start;
    }
}

template <typename Kernel>
static void run_case(const char *label, Kernel kernel, int blocks, int loops, int mma_per_loop) {
    half *a = nullptr;
    half *b = nullptr;
    float *out = nullptr;
    unsigned long long *d_cycles = nullptr;
    unsigned long long *h_cycles = nullptr;

    const size_t out_elems = static_cast<size_t>(blocks) * mma_per_loop * 256;
    check_cuda(cudaMalloc(&a, 16 * 16 * sizeof(half)), "malloc a");
    check_cuda(cudaMalloc(&b, 16 * 16 * sizeof(half)), "malloc b");
    check_cuda(cudaMalloc(&out, out_elems * sizeof(float)), "malloc out");
    check_cuda(cudaMalloc(&d_cycles, blocks * sizeof(unsigned long long)), "malloc cycles");
    h_cycles = static_cast<unsigned long long *>(std::malloc(blocks * sizeof(unsigned long long)));
    if (!h_cycles) {
        std::fprintf(stderr, "host malloc failed\n");
        std::exit(2);
    }

    check_cuda(cudaMemset(a, 1, 16 * 16 * sizeof(half)), "memset a");
    check_cuda(cudaMemset(b, 2, 16 * 16 * sizeof(half)), "memset b");
    check_cuda(cudaMemset(out, 0, out_elems * sizeof(float)), "memset out");

    kernel<<<blocks, 32>>>(a, b, out, d_cycles, loops);
    check_cuda(cudaGetLastError(), "warmup launch");
    check_cuda(cudaDeviceSynchronize(), "warmup sync");

    cudaEvent_t start;
    cudaEvent_t stop;
    check_cuda(cudaEventCreate(&start), "event start");
    check_cuda(cudaEventCreate(&stop), "event stop");
    check_cuda(cudaEventRecord(start), "record start");
    kernel<<<blocks, 32>>>(a, b, out, d_cycles, loops);
    check_cuda(cudaGetLastError(), "timed launch");
    check_cuda(cudaEventRecord(stop), "record stop");
    check_cuda(cudaEventSynchronize(stop), "sync stop");
    float elapsed_ms = 0.0f;
    check_cuda(cudaEventElapsedTime(&elapsed_ms, start, stop), "event elapsed");

    check_cuda(cudaMemcpy(h_cycles, d_cycles, blocks * sizeof(unsigned long long), cudaMemcpyDeviceToHost), "copy cycles");

    unsigned long long min_cycles = h_cycles[0];
    unsigned long long max_cycles = h_cycles[0];
    long double sum_cycles = 0.0L;
    for (int i = 0; i < blocks; ++i) {
        if (h_cycles[i] < min_cycles) min_cycles = h_cycles[i];
        if (h_cycles[i] > max_cycles) max_cycles = h_cycles[i];
        sum_cycles += static_cast<long double>(h_cycles[i]);
    }

    const long double avg_cycles = sum_cycles / static_cast<long double>(blocks);
    const long double mma_ops_per_block = static_cast<long double>(loops) * mma_per_loop;
    const long double cycles_per_mma = avg_cycles / mma_ops_per_block;
    const long double total_mma = static_cast<long double>(blocks) * mma_ops_per_block;
    const long double flops_per_mma = 8192.0L;
    const long double tflops = total_mma * flops_per_mma / (static_cast<long double>(elapsed_ms) / 1000.0L) / 1.0e12L;

    std::printf("    {\n");
    std::printf("      \"label\": \"%s\",\n", label);
    std::printf("      \"blocks\": %d,\n", blocks);
    std::printf("      \"loops\": %d,\n", loops);
    std::printf("      \"mma_per_loop\": %d,\n", mma_per_loop);
    std::printf("      \"event_ms\": %.6f,\n", elapsed_ms);
    std::printf("      \"min_cycles\": %llu,\n", min_cycles);
    std::printf("      \"avg_cycles\": %.2Lf,\n", avg_cycles);
    std::printf("      \"max_cycles\": %llu,\n", max_cycles);
    std::printf("      \"cycles_per_mma_per_warp\": %.4Lf,\n", cycles_per_mma);
    std::printf("      \"kernel_tflops_from_mma_count\": %.6Lf\n", tflops);
    std::printf("    }");

    check_cuda(cudaEventDestroy(start), "destroy start");
    check_cuda(cudaEventDestroy(stop), "destroy stop");
    check_cuda(cudaFree(a), "free a");
    check_cuda(cudaFree(b), "free b");
    check_cuda(cudaFree(out), "free out");
    check_cuda(cudaFree(d_cycles), "free cycles");
    std::free(h_cycles);
}

int main(int argc, char **argv) {
    int loops = 4096;
    int blocks = 4096;
    if (argc > 1) loops = std::atoi(argv[1]);
    if (argc > 2) blocks = std::atoi(argv[2]);

    cudaDeviceProp prop {};
    check_cuda(cudaGetDeviceProperties(&prop, 0), "device props");

    std::printf("{\n");
    std::printf("  \"device_name\": \"%s\",\n", prop.name);
    std::printf("  \"compute_capability\": \"%d.%d\",\n", prop.major, prop.minor);
    std::printf("  \"sm_count\": %d,\n", prop.multiProcessorCount);
    std::printf("  \"clock_rate_khz\": %d,\n", prop.clockRate);
    std::printf("  \"cases\": [\n");
    run_case("dependent_accumulator", hmma_dependent_kernel, blocks, loops, 1);
    std::printf(",\n");
    run_case("independent_4_accumulators", hmma_independent4_kernel, blocks, loops, 4);
    std::printf("\n  ]\n");
    std::printf("}\n");
    return 0;
}
