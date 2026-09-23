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

__global__ void dp4a_dependent_kernel(int loops, unsigned long long *cycles_out, int *sink_out) {
    const int tid = blockIdx.x * blockDim.x + threadIdx.x;
    int acc = tid & 0x7f;
    int a = 0x01020304 ^ tid;
    int b = 0x05060708;
    const unsigned long long start = clock64();
    #pragma unroll 1
    for (int i = 0; i < loops; ++i) {
        acc = __dp4a(a, b, acc);
        a ^= acc;
    }
    const unsigned long long stop = clock64();
    cycles_out[tid] = stop - start;
    sink_out[tid] = acc;
}

__global__ void dp4a_independent4_kernel(int loops, unsigned long long *cycles_out, int *sink_out) {
    const int tid = blockIdx.x * blockDim.x + threadIdx.x;
    int acc0 = tid & 0x7f;
    int acc1 = acc0 + 1;
    int acc2 = acc0 + 2;
    int acc3 = acc0 + 3;
    const int a0 = 0x01020304 ^ tid;
    const int a1 = 0x11121314 ^ tid;
    const int a2 = 0x21222324 ^ tid;
    const int a3 = 0x31323334 ^ tid;
    const int b0 = 0x05060708;
    const int b1 = 0x15161718;
    const int b2 = 0x25262728;
    const int b3 = 0x35363738;
    const unsigned long long start = clock64();
    #pragma unroll 1
    for (int i = 0; i < loops; ++i) {
        acc0 = __dp4a(a0, b0, acc0);
        acc1 = __dp4a(a1, b1, acc1);
        acc2 = __dp4a(a2, b2, acc2);
        acc3 = __dp4a(a3, b3, acc3);
    }
    const unsigned long long stop = clock64();
    cycles_out[tid] = stop - start;
    sink_out[tid] = acc0 ^ acc1 ^ acc2 ^ acc3;
}

static double average_cycles(const std::vector<unsigned long long>& values) {
    long double sum = 0.0L;
    for (const auto v : values) {
        sum += static_cast<long double>(v);
    }
    return static_cast<double>(sum / values.size());
}

template <typename Kernel>
static void run_case(const char *name, Kernel kernel, int blocks, int threads, int loops, int dp4a_per_loop) {
    const int total_threads = blocks * threads;
    unsigned long long *cycles = nullptr;
    int *sink = nullptr;
    check_cuda(cudaMalloc(&cycles, total_threads * sizeof(unsigned long long)), "malloc cycles");
    check_cuda(cudaMalloc(&sink, total_threads * sizeof(int)), "malloc sink");

    kernel<<<blocks, threads>>>(loops, cycles, sink);
    check_cuda(cudaGetLastError(), "launch warmup");
    check_cuda(cudaDeviceSynchronize(), "warmup sync");

    cudaEvent_t start;
    cudaEvent_t stop;
    check_cuda(cudaEventCreate(&start), "event start");
    check_cuda(cudaEventCreate(&stop), "event stop");
    check_cuda(cudaEventRecord(start), "record start");
    kernel<<<blocks, threads>>>(loops, cycles, sink);
    check_cuda(cudaGetLastError(), "launch timed");
    check_cuda(cudaEventRecord(stop), "record stop");
    check_cuda(cudaEventSynchronize(stop), "sync stop");

    float elapsed_ms = 0.0f;
    check_cuda(cudaEventElapsedTime(&elapsed_ms, start, stop), "elapsed");

    std::vector<unsigned long long> host_cycles(total_threads);
    std::vector<int> host_sink(total_threads);
    check_cuda(cudaMemcpy(host_cycles.data(), cycles, total_threads * sizeof(unsigned long long), cudaMemcpyDeviceToHost), "copy cycles");
    check_cuda(cudaMemcpy(host_sink.data(), sink, total_threads * sizeof(int), cudaMemcpyDeviceToHost), "copy sink");
    int checksum = 0;
    for (int v : host_sink) {
        checksum ^= v;
    }

    const double avg_cycles = average_cycles(host_cycles);
    const double dp4a_ops = static_cast<double>(total_threads) * loops * dp4a_per_loop;
    const double int8_madd_ops = dp4a_ops * 4.0;
    const double gdp4a_per_s = dp4a_ops / (elapsed_ms / 1000.0) / 1.0e9;
    const double gint8_madd_per_s = int8_madd_ops / (elapsed_ms / 1000.0) / 1.0e9;
    const double cycles_per_dp4a = avg_cycles / (loops * dp4a_per_loop);

    std::printf("    {\n");
    std::printf("      \"case\": \"%s\",\n", name);
    std::printf("      \"blocks\": %d,\n", blocks);
    std::printf("      \"threads\": %d,\n", threads);
    std::printf("      \"loops\": %d,\n", loops);
    std::printf("      \"dp4a_per_loop\": %d,\n", dp4a_per_loop);
    std::printf("      \"event_ms\": %.6f,\n", elapsed_ms);
    std::printf("      \"avg_cycles\": %.3f,\n", avg_cycles);
    std::printf("      \"cycles_per_dp4a_per_thread\": %.6f,\n", cycles_per_dp4a);
    std::printf("      \"gdp4a_per_s\": %.3f,\n", gdp4a_per_s);
    std::printf("      \"gint8_madd_per_s\": %.3f,\n", gint8_madd_per_s);
    std::printf("      \"checksum\": %d\n", checksum);
    std::printf("    }");

    check_cuda(cudaEventDestroy(start), "destroy start");
    check_cuda(cudaEventDestroy(stop), "destroy stop");
    check_cuda(cudaFree(cycles), "free cycles");
    check_cuda(cudaFree(sink), "free sink");
}

int main(int argc, char **argv) {
    int loops = 65536;
    int blocks = 2048;
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
    run_case("dependent", dp4a_dependent_kernel, blocks, threads, loops, 1);
    std::printf(",\n");
    run_case("independent4", dp4a_independent4_kernel, blocks, threads, loops, 4);
    std::printf("\n  ]\n");
    std::printf("}\n");
    return 0;
}
