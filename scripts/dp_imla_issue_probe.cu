#include <cuda_runtime.h>

#include <cstdio>
#include <cstdlib>

static void check_cuda(cudaError_t status, const char *what) {
    if (status != cudaSuccess) {
        std::fprintf(stderr, "CUDA error at %s: %s\n", what, cudaGetErrorString(status));
        std::exit(2);
    }
}

__device__ __forceinline__ double dfma_rn(double a, double b, double c) {
    double d;
    asm volatile("fma.rn.f64 %0, %1, %2, %3;" : "=d"(d) : "d"(a), "d"(b), "d"(c));
    return d;
}

__device__ __forceinline__ unsigned int imad_lo(unsigned int a, unsigned int b, unsigned int c) {
    unsigned int d;
    asm volatile("mad.lo.u32 %0, %1, %2, %3;" : "=r"(d) : "r"(a), "r"(b), "r"(c));
    return d;
}

__global__ void dp_dependent_kernel(double *out, unsigned long long *cycles, int loops) {
    double a = 1.00000011920928955078125 + threadIdx.x * 0.000001;
    double b = 0.999999940395355224609375 + blockIdx.x * 0.0000001;
    double acc = 0.25 + threadIdx.x * 0.001;

    __syncthreads();
    unsigned long long start = clock64();
    #pragma unroll 1
    for (int i = 0; i < loops; ++i) {
        acc = dfma_rn(a, b, acc);
    }
    unsigned long long stop = clock64();

    out[blockIdx.x * blockDim.x + threadIdx.x] = acc;
    if (threadIdx.x == 0) cycles[blockIdx.x] = stop - start;
}

__global__ void dp_independent4_kernel(double *out, unsigned long long *cycles, int loops) {
    double a = 1.00000011920928955078125 + threadIdx.x * 0.000001;
    double b = 0.999999940395355224609375 + blockIdx.x * 0.0000001;
    double acc0 = 0.25 + threadIdx.x * 0.001;
    double acc1 = 0.50 + threadIdx.x * 0.001;
    double acc2 = 0.75 + threadIdx.x * 0.001;
    double acc3 = 1.00 + threadIdx.x * 0.001;

    __syncthreads();
    unsigned long long start = clock64();
    #pragma unroll 1
    for (int i = 0; i < loops; ++i) {
        acc0 = dfma_rn(a, b, acc0);
        acc1 = dfma_rn(a, b, acc1);
        acc2 = dfma_rn(a, b, acc2);
        acc3 = dfma_rn(a, b, acc3);
    }
    unsigned long long stop = clock64();

    const int base = (blockIdx.x * blockDim.x + threadIdx.x) * 4;
    out[base + 0] = acc0;
    out[base + 1] = acc1;
    out[base + 2] = acc2;
    out[base + 3] = acc3;
    if (threadIdx.x == 0) cycles[blockIdx.x] = stop - start;
}

__global__ void imla_dependent_kernel(unsigned int *out, unsigned long long *cycles, int loops) {
    unsigned int a = 1664525u + threadIdx.x;
    unsigned int b = 1013904223u + blockIdx.x;
    unsigned int acc = threadIdx.x ^ (blockIdx.x * 2654435761u);

    __syncthreads();
    unsigned long long start = clock64();
    #pragma unroll 1
    for (int i = 0; i < loops; ++i) {
        acc = imad_lo(acc, a, b);
    }
    unsigned long long stop = clock64();

    out[blockIdx.x * blockDim.x + threadIdx.x] = acc;
    if (threadIdx.x == 0) cycles[blockIdx.x] = stop - start;
}

__global__ void imla_independent4_kernel(unsigned int *out, unsigned long long *cycles, int loops) {
    unsigned int a = 1664525u + threadIdx.x;
    unsigned int b = 1013904223u + blockIdx.x;
    unsigned int acc0 = threadIdx.x ^ (blockIdx.x * 2654435761u);
    unsigned int acc1 = acc0 + 0x11111111u;
    unsigned int acc2 = acc0 + 0x22222222u;
    unsigned int acc3 = acc0 + 0x33333333u;

    __syncthreads();
    unsigned long long start = clock64();
    #pragma unroll 1
    for (int i = 0; i < loops; ++i) {
        acc0 = imad_lo(acc0, a, b);
        acc1 = imad_lo(acc1, a, b);
        acc2 = imad_lo(acc2, a, b);
        acc3 = imad_lo(acc3, a, b);
    }
    unsigned long long stop = clock64();

    const int base = (blockIdx.x * blockDim.x + threadIdx.x) * 4;
    out[base + 0] = acc0;
    out[base + 1] = acc1;
    out[base + 2] = acc2;
    out[base + 3] = acc3;
    if (threadIdx.x == 0) cycles[blockIdx.x] = stop - start;
}

template <typename T, typename Kernel>
static void run_case(
    const char *label,
    const char *unit,
    Kernel kernel,
    int blocks,
    int threads,
    int loops,
    int ops_per_loop,
    int results_per_thread,
    long double work_per_op) {

    T *out = nullptr;
    unsigned long long *d_cycles = nullptr;
    unsigned long long *h_cycles = nullptr;

    const size_t out_elems = static_cast<size_t>(blocks) * threads * results_per_thread;
    check_cuda(cudaMalloc(&out, out_elems * sizeof(T)), "malloc out");
    check_cuda(cudaMalloc(&d_cycles, blocks * sizeof(unsigned long long)), "malloc cycles");
    h_cycles = static_cast<unsigned long long *>(std::malloc(blocks * sizeof(unsigned long long)));
    if (!h_cycles) {
        std::fprintf(stderr, "host malloc failed\n");
        std::exit(2);
    }

    kernel<<<blocks, threads>>>(out, d_cycles, loops);
    check_cuda(cudaGetLastError(), "warmup launch");
    check_cuda(cudaDeviceSynchronize(), "warmup sync");

    cudaEvent_t start;
    cudaEvent_t stop;
    check_cuda(cudaEventCreate(&start), "event start");
    check_cuda(cudaEventCreate(&stop), "event stop");
    check_cuda(cudaEventRecord(start), "record start");
    kernel<<<blocks, threads>>>(out, d_cycles, loops);
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
    const long double ops_per_thread = static_cast<long double>(loops) * ops_per_loop;
    const long double cycles_per_op_per_thread = avg_cycles / ops_per_thread;
    const long double total_ops = static_cast<long double>(blocks) * threads * ops_per_thread;
    const long double throughput = total_ops * work_per_op / (static_cast<long double>(elapsed_ms) / 1000.0L) / 1.0e9L;

    std::printf("    {\n");
    std::printf("      \"label\": \"%s\",\n", label);
    std::printf("      \"unit\": \"%s\",\n", unit);
    std::printf("      \"blocks\": %d,\n", blocks);
    std::printf("      \"threads_per_block\": %d,\n", threads);
    std::printf("      \"loops\": %d,\n", loops);
    std::printf("      \"ops_per_loop_per_thread\": %d,\n", ops_per_loop);
    std::printf("      \"event_ms\": %.6f,\n", elapsed_ms);
    std::printf("      \"min_cycles\": %llu,\n", min_cycles);
    std::printf("      \"avg_cycles\": %.2Lf,\n", avg_cycles);
    std::printf("      \"max_cycles\": %llu,\n", max_cycles);
    std::printf("      \"cycles_per_op_per_thread\": %.6Lf,\n", cycles_per_op_per_thread);
    std::printf("      \"throughput_giga_units_per_sec\": %.6Lf\n", throughput);
    std::printf("    }");

    check_cuda(cudaEventDestroy(start), "destroy start");
    check_cuda(cudaEventDestroy(stop), "destroy stop");
    check_cuda(cudaFree(out), "free out");
    check_cuda(cudaFree(d_cycles), "free cycles");
    std::free(h_cycles);
}

int main(int argc, char **argv) {
    int loops = 65536;
    int blocks = 4096;
    int threads = 128;
    if (argc > 1) loops = std::atoi(argv[1]);
    if (argc > 2) blocks = std::atoi(argv[2]);
    if (argc > 3) threads = std::atoi(argv[3]);

    cudaDeviceProp prop {};
    check_cuda(cudaGetDeviceProperties(&prop, 0), "device props");

    std::printf("{\n");
    std::printf("  \"device_name\": \"%s\",\n", prop.name);
    std::printf("  \"compute_capability\": \"%d.%d\",\n", prop.major, prop.minor);
    std::printf("  \"sm_count\": %d,\n", prop.multiProcessorCount);
    std::printf("  \"clock_rate_khz\": %d,\n", prop.clockRate);
    std::printf("  \"cases\": [\n");

    run_case<double>("dp_dependent_fma", "fp64_flop", dp_dependent_kernel, blocks, threads, loops, 1, 1, 2.0L);
    std::printf(",\n");
    run_case<double>("dp_independent4_fma", "fp64_flop", dp_independent4_kernel, blocks, threads, loops, 4, 4, 2.0L);
    std::printf(",\n");
    run_case<unsigned int>("imla_dependent_mad", "imad_op", imla_dependent_kernel, blocks, threads, loops, 1, 1, 1.0L);
    std::printf(",\n");
    run_case<unsigned int>("imla_independent4_mad", "imad_op", imla_independent4_kernel, blocks, threads, loops, 4, 4, 1.0L);
    std::printf("\n  ]\n");
    std::printf("}\n");
    return 0;
}
