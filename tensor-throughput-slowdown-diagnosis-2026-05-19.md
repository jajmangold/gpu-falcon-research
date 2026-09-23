# Tensor Throughput Slowdown Diagnosis - 2026-05-19

## Short Answer

The slowdown does not look like a normal CUDA/cuBLAS fallback and does not look like a simple power-limit effect.

The current best diagnosis is:

```text
Tensor Core instructions are allowed to issue, but effective tensor throughput is capped at about 1/16 of peak.
```

That points below ordinary CUDA application behavior: signed GR/FECS/GPCCS/SEC2 policy, driver policy keyed from device identity/fuses, or a hardware fuse/disable path. The evidence does not support a VBIOS-visible table such as `M_RESTRICT`, `M_TYPE`, or `M_UNK0D` as the standalone gate.

## Evidence

### 1. Same Public GEMM Shapes Are ~16x Slow

DeepBench public V100 FP16 mixed-math GEMM:

```text
Source: Baidu Research DeepBench
File: results/train/DeepBench_NV_V100.xlsx
Sheet: Results - FP16 ip, Mixed math
System: NVIDIA V100, CUDA 10.1.243, driver 418.67, DGX-1
```

Local V100-personality GPU 14 on the same GEMM shapes:

| Shape | Public V100 TFLOPS | Local V100-Personality TFLOPS | Ratio |
| --- | ---: | ---: | ---: |
| 1760 x 7000 x 1760 | 89.415 | 5.633 | 0.063 |
| 2048 x 7000 x 2048 | 98.030 | 6.296 | 0.064 |
| 2560 x 7000 x 2560 | 103.790 | 6.909 | 0.067 |
| 4096 x 7000 x 4096 | 112.276 | 7.046 | 0.063 |

This is the key pattern: local throughput is about `1/16` of public full-speed V100 throughput.

### 2. cuBLAS Is Selecting Tensor-Op Kernels

Prior cuBLAS logs show:

```text
MathMode=CUBLAS_TENSOR_OP_MATH
computeType=CUBLAS_COMPUTE_32F / CUBLAS_COMPUTE_32F_FAST_16F
algo=CUBLAS_GEMM_DEFAULT_TENSOR_OP
```

Nsight Compute captured Volta Tensor Core kernel names:

```text
volta_fp16_s884gemm_fp16_128x64_ldg8_f2f_nn
volta_s884gemm_128x128_ldg8_f2f_nn
```

That rules out the simplest explanation that cuBLAS is only choosing an ordinary SIMT FP32/FP16 path.

### 3. Tensor-Pipe Instructions Are Visible

Existing Nsight Compute captures on GPU 14 show `sm__inst_executed_pipe_tensor.sum` populated for WMMA and cuBLAS kernels. The V100-personality card also permits profiler access, unlike the CMP identity path that returns the CMP profiler block.

So the issue is not "no tensor instructions can execute." The issue is that tensor instructions do not translate into full-speed Tensor Core throughput.

### 3a. Nsight Compute Shows A 1/16 Throughput Cap

A privileged Nsight Compute pass on a DeepBench-shaped cuBLAS/CUTLASS Tensor Core kernel captured:

```text
Kernel:
void Kernel2<cutlass_70_tensorop_f16_s884gemm_relu_f16_128x128_nn_align8>(Params)

sm__inst_executed_pipe_tensor.sum: 461373440
sm__cycles_elapsed.avg:           46203103.65
sm__cycles_active.avg:            46181596.25
gpu__time_duration.sum:           40278464 ns
sm__throughput.avg.pct_of_peak_sustained_elapsed: 6.25%
```

`6.25%` is exactly `1/16` of sustained peak. On an 80-SM V100-class identity with the nominal `80 x 8 = 640` Tensor Core layout, that is equivalent to about:

```text
640 / 16 = 40 full-speed Tensor Cores
```

This does not prove that exactly 40 physical Tensor Cores are enabled. It does strongly support a one-sixteenth effective tensor-issue or active-lane cap.

### 3b. The Cap Is Uniform Across V100-Personality Cards

A UUID-targeted Nsight Compute sweep across all local cards currently reporting `Tesla V100-PCIE-12GB` produced the same counter on every card:

| Host GPU | Reported Device | SMs | Tensor Instructions | Kernel Time ns | SM Throughput % Peak |
| ---: | --- | ---: | ---: | ---: | ---: |
| 4 | Tesla V100-PCIE-12GB | 80 | 461373440 | 40275488 | 6.25 |
| 7 | Tesla V100-PCIE-12GB | 80 | 461373440 | 40479104 | 6.25 |
| 9 | Tesla V100-PCIE-12GB | 80 | 461373440 | 40273632 | 6.25 |
| 11 | Tesla V100-PCIE-12GB | 80 | 461373440 | 40257312 | 6.25 |
| 14 | Tesla V100-PCIE-12GB | 80 | 461373440 | 40268832 | 6.25 |

Raw sweep outputs:

```text
runs/20260519-173454-tensor-probe-gpu2-gpu14/deepbench-public-compare/v100-personality-ncu-sweep/
```

This uniformity argues against a one-off bad card or unstable benchmark path. The cap follows the local V100-personality population.

### 3c. Vast.ai Full-Speed V100 Control

A live Vast.ai control run used the same `deepbench_gemm_probe.cu` source and the same four DeepBench V100 FP16 mixed-math GEMM shapes.

The cleaner control was a `Tesla V100-SXM2-16GB` with an 80-SM CUDA 7.0 device, 300 W power limit, 1530 MHz max SM clock, and 877 MHz memory clock. It reached:

| Shape | Local GPU14 TFLOPS | Vast 300W V100 TFLOPS | Vast / Local | Vast / public V100 |
| --- | ---: | ---: | ---: | ---: |
| row9 | 5.633 | 82.345 | 14.62x | 0.921 |
| row14 | 6.296 | 81.889 | 13.01x | 0.835 |
| row19 | 6.909 | 88.160 | 12.76x | 0.849 |
| row24 | 7.046 | 94.084 | 13.35x | 0.838 |

This confirms the local slowdown is not a cuBLAS, source, or shape artifact. The same test reaches full-speed V100-class throughput on a current rented V100 control.

Raw report:

```text
vastai-v100-control-2026-05-20.md
```

### 3d. Minimal HMMA Loop Also Shows 15-16x Cycle Cost

A dedicated WMMA/HMMA microbenchmark was added to time minimal Tensor Core loops with device-side `clock64()`:

```text
scripts/hmma_issue_probe.cu
```

The emitted SASS contains real `HMMA.884.F32.F32` instructions. Compared against a 300 W Vast `Tesla V100-SXM2-16GB` control:

| Case | Device | TFLOPS | cycles / WMMA op | Ratio vs Vast cycles |
| --- | --- | ---: | ---: | ---: |
| dependent accumulator | Local GPU14 V100-personality | 5.365 | 3536.3 | 14.57x |
| dependent accumulator | Vast 300W V100 | 93.142 | 242.8 | 1.00x |
| independent 4 accumulators | Local GPU14 V100-personality | 5.536 | 3416.5 | 14.45x |
| independent 4 accumulators | Vast 300W V100 | 90.903 | 236.4 | 1.00x |

GPU2 CMP identity was similar, with `15.14-15.71x` higher cycle cost than the Vast control.

Raw report:

```text
hmma-issue-spacing-2026-05-20.md
```

This is the strongest evidence so far for an issue-rate or issue-spacing limiter: the extra cost appears directly in a tiny HMMA loop, not only in cuBLAS/CUTLASS GEMM scheduling.

### 4. Power Cap Is Not The Main Explanation

The cards are capped at `120 W`, whereas reference V100 PCIe boards are `250 W` class. That matters, but the sampled DeepBench run on GPU 14 did not hit even the local 120 W cap:

```text
Power samples: min 27.74 W, max 72.35 W, avg 40.50 W
SM clock samples: min 135 MHz, max 1380 MHz, avg 628.45 MHz
Memory clock: 810 MHz
GPU utilization: min 0%, max 100%, avg 21.6%
```

A power cap can explain lower clocks under load, but it does not explain a stable ~16x tensor-throughput collapse while the workload is not drawing near the cap.

### 5. The VBIOS M-Table Lead Weakened

The expanded ROM set found a Tesla V100 32 GB control with byte-identical `M_RESTRICT`, `M_TYPE`, and `M_UNK0D` regions to `266855`, while the V100 16 GB control differs there. That makes those narrow M-table deltas look like board or memory-capacity metadata rather than the tensor-throughput gate.

The BIT `p[0x00]` / `FALCON_UCODE` lead was also reassessed as a false positive into compressed EFI payload bytes.

## What They Most Likely Did

Most likely, NVIDIA did not slow these cards down by making CUDA choose a bad GEMM algorithm. The observed behavior is more specific:

```text
The card advertises enough Volta capability to run HMMA/Tensor Core kernels, but a lower-level policy makes the tensor path deliver one-sixteenth of normal Tensor Core throughput.
```

The most plausible implementations are:

1. **Hardware fuse / SKU fuse:** Tensor throughput ratio or tensor pipe enablement is fused independently from SM visibility. V100 identity can expose 80 SMs and profiling, but the physical or fuse-derived tensor-rate state remains capped.

2. **Signed GR firmware policy:** FECS/GPCCS or related signed firmware may read SKU/fuse state and configure tensor issue rate, tensor pipe masks, scheduler mode, or admission policy before user kernels run.

3. **Driver policy keyed by fuse/identity tuple:** The driver may allow Tensor Core kernels and profiler access under V100 personality, but still program a conservative mode after reading a CMP-derived fuse or board state.

These are not mutually exclusive; firmware and driver may both be consuming fuse-derived SKU state.

## Current Confidence

| Hypothesis | Fit | Notes |
| --- | --- | --- |
| cuBLAS fallback to non-tensor kernels | Low | Tensor-op algorithms, Volta `s884gemm` kernels, and tensor-pipe instructions are visible. |
| Power limit only | Low to medium | 120 W cap matters, but sampled DeepBench run peaks around 72 W and still shows ~16x collapse. |
| VBIOS-visible M-table policy | Low | V100 32 GB control matches `266855` in narrow M tables. |
| Signed firmware / driver policy from fuse state | High | Explains identity unlock plus retained tensor cap. |
| Hardware fuse limiting tensor throughput | High | Explains persistence across VBIOS/personality and FP32-class tensor result. |
| 1/16 tensor issue-rate or active-lane cap | High | Nsight Compute reports exactly `6.25%` SM throughput on a Tensor Core GEMM kernel across all local V100-personality cards. |

## Next Non-Destructive Proof Points

The cleanest remaining non-destructive evidence would be:

1. Capture Nsight Compute issue-rate / active-cycle metrics for one DeepBench-shaped cuBLAS kernel on GPU 14 and compare to the public full-speed expectation.
2. Compare low-level attributes and profiler counters across GPU 14 V100-personality and any real full-speed V100 card if available.
3. Identify whether tensor-pipe instruction count is normal but tensor-pipe active cycles or issue throughput is throttled.

If instruction count is normal but elapsed cycles are ~16x too high, the slowdown is effectively a tensor issue-rate or tensor active-lane cap. If instruction count is itself unusual, the driver/library path may be emitting a degraded Tensor Core kernel variant despite the `s884gemm` name.

The first pass supports the former: tensor instructions execute, but the profiled kernel reports `6.25%` of peak sustained SM throughput.

## Sources

- DeepBench public V100 comparison: `deepbench-public-v100-compare-2026-05-19.md`
- Local VBIOS control validation: `full-speed-v100-control-validation-2026-05-19.md`
- Prior board/personality findings: `board-personality-findings-2026-05-19.md`
