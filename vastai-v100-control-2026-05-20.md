# Vast.ai V100 Control - 2026-05-20

## Purpose

Run the same DeepBench-shaped FP16 mixed-math cuBLAS Tensor Core GEMM probe on known-good Vast.ai Tesla V100 instances. This provides a live full-speed control for the local V100-personality cards that are capped near `6.25%` sustained Tensor throughput.

Artifacts:

```text
runs/20260520-vast-v100-control/
```

The Vast API key was stored in the root `.env`, which is ignored by git and set to mode `0600`. Per-instance API keys returned by Vast create calls were redacted from saved artifacts.

## Instances

| Contract | Offer | GPU | Power limit | Max SM clock | Max mem clock | Result |
| ---: | ---: | --- | ---: | ---: | ---: | --- |
| `37107473` | `33341796` | Tesla V100-FHHL-16GB | 150 W | 1290 MHz | 810 MHz | Destroyed |
| `37107662` | `30970796` | Tesla V100-SXM2-16GB | 300 W | 1530 MHz | 877 MHz | Destroyed |

Both instances were destroyed after the benchmark. A post-destroy check found no remaining test instances.

## Benchmark Results

Same source and problem set as local testing:

```text
projects/gpu-falcon-research/scripts/deepbench_gemm_probe.cu
```

| Shape | Local GPU14 V100-personality | Vast 150W V100 | Vast 150W / Local | Vast 300W V100 | Vast 300W / Local | Public V100 | Vast 300W / Public |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| DeepBench-V100-FP16-row9 | 5.633 | 59.550 | 10.57x | 82.345 | 14.62x | 89.415 | 0.921 |
| DeepBench-V100-FP16-row14 | 6.296 | 59.109 | 9.39x | 81.889 | 13.01x | 98.030 | 0.835 |
| DeepBench-V100-FP16-row19 | 6.909 | 69.831 | 10.11x | 88.160 | 12.76x | 103.790 | 0.849 |
| DeepBench-V100-FP16-row24 | 7.046 | 76.786 | 10.90x | 94.084 | 13.35x | 112.276 | 0.838 |

The 150 W FHHL card is full Tensor-capable but power/clock limited relative to public DGX-class V100 numbers. The 300 W SXM2 card is the cleaner control and lands within `83.5%` to `92.1%` of the public DeepBench V100 results.

## Profiler Counter Attempt

Nsight Compute was present on both Vast instances, but host policy blocked performance counters:

```text
ERR_NVGPUCTRPERM
```

So the Vast run provides timing/throughput control evidence, not remote tensor-pipe counter evidence.

## Interpretation

The live Vast.ai control confirms the local result is not a benchmark or cuBLAS artifact. The same probe that produces only `5.6-7.0 TFLOPS` on local V100-personality cards produces:

```text
59.6-76.8 TFLOPS on a 150 W V100-FHHL
82.3-94.1 TFLOPS on a 300 W V100-SXM2
```

The local cards are therefore about `12.8-14.6x` slower than a current full-speed 300 W V100 control on the same source, same shapes, same CUDA major generation, and same SM architecture target.

This reinforces the existing conclusion: local V100-personality cards execute Tensor Core kernels but are limited by a lower-level tensor throughput gate, not by the benchmark path.
