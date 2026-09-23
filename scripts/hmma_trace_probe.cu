#include <cuda_fp16.h>
#include <cuda_runtime.h>
#include <mma.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace nvcuda;

static void check_cuda(cudaError_t status, const char *what) {
    if (status != cudaSuccess) {
        std::fprintf(stderr, "CUDA error at %s: %s\n", what, cudaGetErrorString(status));
        std::exit(2);
    }
}

__global__ void hmma_trace_kernel(const half *a,
                                  const half *b,
                                  float *out,
                                  unsigned long long *trace,
                                  int samples,
                                  int group_mmas) {
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

    for (int s = 0; s < samples; ++s) {
        const unsigned long long start = clock64();

#pragma unroll 1
        for (int g = 0; g < group_mmas; ++g) {
            const int sel = g & 3;
            if (sel == 0) {
                wmma::mma_sync(acc0, fa, fb, acc0);
            } else if (sel == 1) {
                wmma::mma_sync(acc1, fa, fb, acc1);
            } else if (sel == 2) {
                wmma::mma_sync(acc2, fa, fb, acc2);
            } else {
                wmma::mma_sync(acc3, fa, fb, acc3);
            }
        }

        const unsigned long long stop = clock64();
        if (threadIdx.x == 0) {
            trace[static_cast<size_t>(blockIdx.x) * samples + s] = stop - start;
        }
    }

    wmma::store_matrix_sync(out + blockIdx.x * 1024, acc0, 16, wmma::mem_row_major);
    wmma::store_matrix_sync(out + blockIdx.x * 1024 + 256, acc1, 16, wmma::mem_row_major);
    wmma::store_matrix_sync(out + blockIdx.x * 1024 + 512, acc2, 16, wmma::mem_row_major);
    wmma::store_matrix_sync(out + blockIdx.x * 1024 + 768, acc3, 16, wmma::mem_row_major);
}

static long double mean_for_block(const std::vector<unsigned long long> &trace,
                                  int block,
                                  int samples) {
    long double sum = 0.0L;
    for (int i = 0; i < samples; ++i) {
        sum += static_cast<long double>(trace[static_cast<size_t>(block) * samples + i]);
    }
    return sum / static_cast<long double>(samples);
}

int main(int argc, char **argv) {
    int samples = 256;
    int group_mmas = 1;
    int blocks = 1;
    if (argc > 1) samples = std::atoi(argv[1]);
    if (argc > 2) group_mmas = std::atoi(argv[2]);
    if (argc > 3) blocks = std::atoi(argv[3]);

    if (samples <= 0 || group_mmas <= 0 || blocks <= 0) {
        std::fprintf(stderr, "usage: %s [samples>0] [group_mmas>0] [blocks>0]\n", argv[0]);
        return 2;
    }

    cudaDeviceProp prop {};
    check_cuda(cudaGetDeviceProperties(&prop, 0), "device props");

    half *a = nullptr;
    half *b = nullptr;
    float *out = nullptr;
    unsigned long long *d_trace = nullptr;

    const size_t trace_count = static_cast<size_t>(blocks) * samples;
    check_cuda(cudaMalloc(&a, 16 * 16 * sizeof(half)), "malloc a");
    check_cuda(cudaMalloc(&b, 16 * 16 * sizeof(half)), "malloc b");
    check_cuda(cudaMalloc(&out, static_cast<size_t>(blocks) * 1024 * sizeof(float)), "malloc out");
    check_cuda(cudaMalloc(&d_trace, trace_count * sizeof(unsigned long long)), "malloc trace");
    check_cuda(cudaMemset(a, 1, 16 * 16 * sizeof(half)), "memset a");
    check_cuda(cudaMemset(b, 2, 16 * 16 * sizeof(half)), "memset b");
    check_cuda(cudaMemset(out, 0, static_cast<size_t>(blocks) * 1024 * sizeof(float)), "memset out");
    check_cuda(cudaMemset(d_trace, 0, trace_count * sizeof(unsigned long long)), "memset trace");

    hmma_trace_kernel<<<blocks, 32>>>(a, b, out, d_trace, 16, group_mmas);
    check_cuda(cudaGetLastError(), "warmup launch");
    check_cuda(cudaDeviceSynchronize(), "warmup sync");

    hmma_trace_kernel<<<blocks, 32>>>(a, b, out, d_trace, samples, group_mmas);
    check_cuda(cudaGetLastError(), "trace launch");
    check_cuda(cudaDeviceSynchronize(), "trace sync");

    std::vector<unsigned long long> trace(trace_count);
    check_cuda(cudaMemcpy(trace.data(), d_trace, trace_count * sizeof(unsigned long long),
                          cudaMemcpyDeviceToHost),
               "copy trace");

    unsigned long long min_delta = trace[0];
    unsigned long long max_delta = trace[0];
    long double sum_delta = 0.0L;
    for (unsigned long long v : trace) {
        min_delta = std::min(min_delta, v);
        max_delta = std::max(max_delta, v);
        sum_delta += static_cast<long double>(v);
    }
    const long double avg_delta = sum_delta / static_cast<long double>(trace_count);
    const long double avg_per_mma = avg_delta / static_cast<long double>(group_mmas);

    std::printf("{\n");
    std::printf("  \"device_name\": \"%s\",\n", prop.name);
    std::printf("  \"compute_capability\": \"%d.%d\",\n", prop.major, prop.minor);
    std::printf("  \"sm_count\": %d,\n", prop.multiProcessorCount);
    std::printf("  \"clock_rate_khz\": %d,\n", prop.clockRate);
    std::printf("  \"samples\": %d,\n", samples);
    std::printf("  \"group_mmas\": %d,\n", group_mmas);
    std::printf("  \"blocks\": %d,\n", blocks);
    std::printf("  \"min_delta_cycles_per_group\": %llu,\n", min_delta);
    std::printf("  \"avg_delta_cycles_per_group\": %.4Lf,\n", avg_delta);
    std::printf("  \"max_delta_cycles_per_group\": %llu,\n", max_delta);
    std::printf("  \"avg_cycles_per_mma\": %.4Lf,\n", avg_per_mma);
    std::printf("  \"block_means\": [");
    for (int bidx = 0; bidx < blocks; ++bidx) {
        if (bidx) std::printf(", ");
        std::printf("%.4Lf", mean_for_block(trace, bidx, samples));
    }
    std::printf("],\n");
    std::printf("  \"trace_first_block\": [");
    const int first_n = samples < 128 ? samples : 128;
    for (int i = 0; i < first_n; ++i) {
        if (i) std::printf(", ");
        std::printf("%llu", trace[i]);
    }
    std::printf("]\n");
    std::printf("}\n");

    check_cuda(cudaFree(a), "free a");
    check_cuda(cudaFree(b), "free b");
    check_cuda(cudaFree(out), "free out");
    check_cuda(cudaFree(d_trace), "free trace");
    return 0;
}
