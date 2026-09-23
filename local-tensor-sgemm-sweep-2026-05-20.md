# Local Tensor/SGEMM Sweep - 2026-05-20

Purpose: sweep the accessible local GV100 containers with the existing cuBLAS Tensor probe to confirm whether the `FMLA_REDUCED + FMLA_OVERRIDE` downstream effect is uniform across V100-personality and CMP-labelled cards.

This is normal CUDA/cuBLAS execution only. It does not read or write protected registers.

## Probe

Rebuilt:

```text
scripts/cublas_tensor_probe.cu
runs/20260520-tensor-sgemm-sweep/cublas_tensor_probe
```

Build:

```text
docker exec wan2gp-gpu15 sh -lc 'cd /workspace/projects/gpu-falcon-research && /usr/local/cuda/bin/nvcc -O3 -std=c++17 -arch=sm_70 -cudart static scripts/cublas_tensor_probe.cu -lcublas -o runs/20260520-tensor-sgemm-sweep/cublas_tensor_probe'
```

Run shape:

```text
FP16 Tensor GEMM n = 8192
FP32 SGEMM n       = 4096
Iterations         = 10
```

## Results

| Container | Reported device | SMs | FP16 tensor TFLOP/s | FP32 TFLOP/s | Tensor/FP32 | Tensor/SM |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `wan2gp-gpu7` | Tesla V100-PCIE-12GB | 80 | 6.979 | 6.535 | 1.07 | 0.08724 |
| `wan2gp-gpu9` | Tesla V100-PCIE-12GB | 80 | 6.940 | 6.464 | 1.07 | 0.08675 |
| `wan2gp-gpu11` | Tesla V100-PCIE-12GB | 80 | 6.979 | 6.532 | 1.07 | 0.08724 |
| `wan2gp-gpu5` | NVIDIA CMP 100-210 | 68 | 5.959 | 5.677 | 1.05 | 0.08763 |
| `wan2gp-gpu6` | NVIDIA CMP 100-210 | 68 | 5.856 | 5.663 | 1.03 | 0.08611 |
| `wan2gp-gpu8` | NVIDIA CMP 100-210 | 68 | 5.863 | 5.664 | 1.04 | 0.08622 |
| `wan2gp-gpu10` | NVIDIA CMP 100-210 | 68 | 5.929 | 5.675 | 1.04 | 0.08719 |
| `wan2gp-gpu13` | NVIDIA CMP 100-210 | 68 | 5.939 | 5.673 | 1.05 | 0.08733 |
| `wan2gp-gpu15` | NVIDIA CMP 100-210 | 68 | 5.918 | 5.675 | 1.04 | 0.08703 |

Group averages:

| Group | Count | FP16 tensor TFLOP/s avg | FP32 TFLOP/s avg | Tensor/FP32 avg | Tensor/SM avg |
| --- | ---: | ---: | ---: | ---: | ---: |
| V100-personality | 3 | 6.966 | 6.510 | 1.070 | 0.08707 |
| CMP-labelled | 6 | 5.910 | 5.671 | 1.042 | 0.08692 |

The tensor throughput scales with visible SM count:

```text
80 / 68 = 1.176
6.966 / 5.910 = 1.179
```

## Interpretation

This confirms the local FMLA/HMMA cap is uniform across the accessible local card set. The V100-personality cards expose 80 SMs and therefore produce more total Tensor TFLOP/s than 68-SM CMP-labelled cards, but both groups have the same tensor throughput per SM:

```text
V100-personality Tensor/SM: 0.08707 TFLOP/s
CMP-labelled Tensor/SM:    0.08692 TFLOP/s
```

That matches the previous conclusion: V100 identity changes visible identity and SM count, but it does not clear the lower-level GV100 FECS speed-select policy.

Together with `sgemm-dgemm-ratio-probe-2026-05-20.md`, the downstream picture is now:

```text
FMLA/HMMA: capped uniformly across local cards.
DP/FP64:   capped uniformly across local cards.
IMLA:      still inconclusive from local-only measurements.
```

## Raw Artifacts

```text
runs/20260520-tensor-sgemm-sweep/wan2gp-gpu5.json
runs/20260520-tensor-sgemm-sweep/wan2gp-gpu6.json
runs/20260520-tensor-sgemm-sweep/wan2gp-gpu7.json
runs/20260520-tensor-sgemm-sweep/wan2gp-gpu8.json
runs/20260520-tensor-sgemm-sweep/wan2gp-gpu9.json
runs/20260520-tensor-sgemm-sweep/wan2gp-gpu10.json
runs/20260520-tensor-sgemm-sweep/wan2gp-gpu11.json
runs/20260520-tensor-sgemm-sweep/wan2gp-gpu13.json
runs/20260520-tensor-sgemm-sweep/wan2gp-gpu15.json
```
