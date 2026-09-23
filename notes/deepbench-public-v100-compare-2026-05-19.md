# DeepBench Public V100 Compare - 2026-05-19

## Public Benchmark Source

Source repository:

```text
https://github.com/baidu-research/DeepBench
```

The public result file used here is:

```text
results/train/DeepBench_NV_V100.xlsx
Sheet: Results - FP16 ip, Mixed math
System sheet: NVIDIA V100, CUDA 10.1.243, driver 418.67, DGX-1
```

DeepBench documents the benchmark command form as:

```text
bin/gemm_bench <inference|train> <int8|float|half>
```

For this run, I copied four exact DeepBench training GEMM shapes from the V100 FP16-input/mixed-math sheet and ran them locally with cuBLAS Tensor Op math.

## Local Runner

```text
scripts/deepbench_gemm_probe.cu
runs/20260519-173454-tensor-probe-gpu2-gpu14/deepbench_gemm_probe
```

The runner uses:

```text
cublasSetMathMode(handle, CUBLAS_TENSOR_OP_MATH)
cublasGemmEx(... CUDA_R_16F inputs, CUDA_R_16F output, CUBLAS_COMPUTE_32F_FAST_16F, CUBLAS_GEMM_DEFAULT_TENSOR_OP)
```

The local cards were selected by GPU UUID, not numeric CUDA ordinal, to avoid device-order ambiguity.

## Results

### GPU 14 V100-Personality

```text
UUID: GPU-71be175a-2ae4-735d-78e1-6eeeea51fc53
Reported device: Tesla V100-PCIE-12GB
SMs: 80
Output: runs/20260519-173454-tensor-probe-gpu2-gpu14/deepbench-public-compare/gpu14-uuid-deepbench-gemm.json
```

| DeepBench row | M | N | K | Public V100 TFLOPS | Local TFLOPS | Local/Public |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 9 | 1760 | 7000 | 1760 | 89.415 | 5.633 | 0.063 |
| 14 | 2048 | 7000 | 2048 | 98.030 | 6.296 | 0.064 |
| 19 | 2560 | 7000 | 2560 | 103.790 | 6.909 | 0.067 |
| 24 | 4096 | 7000 | 4096 | 112.276 | 7.046 | 0.063 |

### GPU 2 CMP

```text
UUID: GPU-4f0356c3-2160-54bd-6464-8b62eaf5b23c
Reported device: NVIDIA CMP 100-210
SMs: 68
Output: runs/20260519-173454-tensor-probe-gpu2-gpu14/deepbench-public-compare/gpu2-uuid-deepbench-gemm.json
```

| DeepBench row | M | N | K | Public V100 TFLOPS | Local TFLOPS | Local/Public |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 9 | 1760 | 7000 | 1760 | 89.415 | 4.754 | 0.053 |
| 14 | 2048 | 7000 | 2048 | 98.030 | 5.517 | 0.056 |
| 19 | 2560 | 7000 | 2560 | 103.790 | 5.662 | 0.055 |
| 24 | 4096 | 7000 | 4096 | 112.276 | 5.965 | 0.053 |

## Interpretation

This reproduces the same public DeepBench GEMM shapes locally and shows the local V100-personality card remains around 6% of public full-speed V100 FP16 mixed-math throughput.

The V100-personality card is slightly faster than the CMP card, consistent with the 80-SM versus 68-SM difference, but it does not show the expected full-speed Tensor Core regime. This strengthens the previous conclusion: the V100 identity/SM/profile unlock is real, but the FP16 tensor-throughput gate is still present.

## Sources

- DeepBench repository and benchmark invocation: https://github.com/baidu-research/DeepBench
- NVIDIA Tensor Core/cuBLAS rules: https://developer.nvidia.com/blog/programming-tensor-cores-cuda-9/
- Tensor Core performance paper reporting up to 83 TFLOPS on Tesla V100: https://arxiv.org/abs/1803.04014
