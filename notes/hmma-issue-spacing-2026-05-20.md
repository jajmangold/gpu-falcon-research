# HMMA Issue Spacing Microbenchmark - 2026-05-20

## Purpose

The previous DeepBench and Nsight Compute results proved an effective `1/16` Tensor throughput cap on local GV100 cards. This pass narrows the mechanism by running a minimal WMMA/HMMA kernel with device-side `clock64` timing.

The question:

```text
Is the local slowdown visible in a tiny HMMA loop itself, or only in cuBLAS/CUTLASS scheduling?
```

Artifacts:

```text
runs/20260520-hmma-issue-spacing/
```

Source:

```text
scripts/hmma_issue_probe.cu
```

The compiled SASS contains real Volta HMMA instructions:

```text
HMMA.884.F32.F32.STEP0
HMMA.884.F32.F32.STEP1
HMMA.884.F32.F32.STEP2
HMMA.884.F32.F32.STEP3
```

## Test Design

Two kernels were timed:

| Case | Purpose |
| --- | --- |
| `dependent_accumulator` | Repeated `wmma::mma_sync()` into one accumulator. This exposes a dependency-heavy path. |
| `independent_4_accumulators` | Four independent accumulators per loop. This gives the scheduler more independent HMMA work. |

Each run used:

```text
4096 blocks
32 threads/block
4096 loop iterations
```

The reported `cycles / WMMA op` is measured from `clock64()` inside each block and averaged across blocks. A WMMA op here is one `wmma::mma_sync()` for a 16x16x16 tile, counted as `8192` FLOPs.

## Results

| Case | Device | SMs | TFLOPS | cycles / WMMA op | Ratio vs Vast cycles |
| --- | --- | ---: | ---: | ---: | ---: |
| dependent_accumulator | GPU14 V100-personality | 80 | 5.365 | 3536.3 | 14.57x |
| dependent_accumulator | GPU2 CMP | 68 | 4.694 | 3674.8 | 15.14x |
| dependent_accumulator | Vast 300W V100 | 80 | 93.142 | 242.8 | 1.00x |
| independent_4_accumulators | GPU14 V100-personality | 80 | 5.536 | 3416.5 | 14.45x |
| independent_4_accumulators | GPU2 CMP | 68 | 5.158 | 3714.7 | 15.71x |
| independent_4_accumulators | Vast 300W V100 | 80 | 90.903 | 236.4 | 1.00x |

The Vast control was a `Tesla V100-SXM2-16GB` with 80 SMs, 300 W power limit, 1530 MHz max SM clock, and 877 MHz memory clock. The Vast instance was destroyed after the run, and a post-destroy check found no remaining HMMA test instance.

## Interpretation

This result is stronger than the prior cuBLAS-only comparison.

The same tiny HMMA loop that reaches about `91-93 TFLOPS` on the full-speed Vast V100 reaches only about `5.4-5.5 TFLOPS` on the local V100-personality card. The local per-WMMA cycle cost is `14.45-14.57x` higher than the full-speed control.

The CMP identity card is similar or slightly slower, with `15.14-15.71x` higher cycle cost than the full-speed control.

This supports an issue-rate or issue-spacing limiter much more directly than an active-lane-only explanation:

```text
Full-speed V100:       ~236-243 cycles / WMMA op in this test
Local V100-persona:   ~3416-3536 cycles / WMMA op
Multiplier:           ~14.5x
```

The result is not exactly `16.0x`, but it is close enough given clock, scheduling, occupancy, and measurement overhead. It is consistent with the earlier Nsight Compute `6.25%` sustained throughput result.

## Current Conclusion

The best current model is:

```text
Tensor Core ISA path exists.
HMMA instructions execute.
The limiter is visible in minimal HMMA loops.
The local cards pay roughly a 15-16x issue/cycle cost per WMMA operation.
```

That makes a scheduler issue-rate divider or firmware-programmed tensor issue throttle the leading explanation.

An active-lane mask is not impossible, but this microbenchmark shifts weight toward an issue-rate/issue-spacing mechanism because the extra cost appears directly as many more cycles per HMMA operation.
