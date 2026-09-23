# SGEMM/DGEMM Ratio Probe - 2026-05-20

Purpose: measure a downstream effect of the `DP_REDUCED + DP_OVERRIDE` bits in the GV100 FECS speed-select mask without reading or writing protected registers.

The decoded capped init script writes:

```text
NV_REG R[0x409664] &= 0xfffff666 |= 0x00000999
```

For GV100, the same register defines:

```text
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT_IMLA  bit 0
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT_FMLA  bit 4
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT_DP    bit 8

NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT_IMLA_OVERRIDE bit 3
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT_FMLA_OVERRIDE bit 7
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT_DP_OVERRIDE   bit 11
```

So `0x999` asserts reduced-speed plus override for IMLA, FMLA, and DP.

Source anchors:

```text
/home/josh/containers/temp/nvdrv/integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_graphics_nobundle.h:5436-5472
/home/josh/containers/temp/nvdrv/integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_ctxsw_firmware.h:3173-3209
```

## Probe

Added:

```text
scripts/sgemm_dgemm_ratio_probe.cu
runs/20260520-sgemm-dgemm-ratio/sgemm_dgemm_ratio_probe
```

Build:

```text
docker exec wan2gp-gpu15 sh -lc 'cd /workspace/projects/gpu-falcon-research && /usr/local/cuda/bin/nvcc -O3 -std=c++17 -arch=sm_70 -cudart static scripts/sgemm_dgemm_ratio_probe.cu -lcublas -o runs/20260520-sgemm-dgemm-ratio/sgemm_dgemm_ratio_probe'
```

Run shape:

```text
n = 4096
SGEMM iterations = 10
DGEMM iterations = 5
CUBLAS_DEFAULT_MATH
```

## Results

| Container | Reported device | SMs | SGEMM TFLOP/s | DGEMM TFLOP/s | SGEMM/DGEMM | SGEMM/SM | DGEMM/SM |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `wan2gp-gpu7` | Tesla V100-PCIE-12GB | 80 | 12.260 | 0.437 | 28.07 | 0.15325 | 0.005461 |
| `wan2gp-gpu9` | Tesla V100-PCIE-12GB | 80 | 11.850 | 0.434 | 27.29 | 0.14812 | 0.005428 |
| `wan2gp-gpu11` | Tesla V100-PCIE-12GB | 80 | 12.005 | 0.437 | 27.48 | 0.15006 | 0.005460 |
| `wan2gp-gpu5` | NVIDIA CMP 100-210 | 68 | 10.784 | 0.366 | 29.42 | 0.15859 | 0.005390 |
| `wan2gp-gpu6` | NVIDIA CMP 100-210 | 68 | 10.732 | 0.366 | 29.32 | 0.15783 | 0.005383 |
| `wan2gp-gpu8` | NVIDIA CMP 100-210 | 68 | 10.775 | 0.367 | 29.40 | 0.15845 | 0.005390 |
| `wan2gp-gpu10` | NVIDIA CMP 100-210 | 68 | 10.699 | 0.367 | 29.19 | 0.15734 | 0.005390 |
| `wan2gp-gpu13` | NVIDIA CMP 100-210 | 68 | 10.777 | 0.367 | 29.40 | 0.15849 | 0.005390 |
| `wan2gp-gpu15` | NVIDIA CMP 100-210 | 68 | 10.742 | 0.366 | 29.31 | 0.15797 | 0.005389 |

Group averages:

| Group | Count | SGEMM TFLOP/s avg | DGEMM TFLOP/s avg | SGEMM/DGEMM avg | SGEMM/SM avg | DGEMM/SM avg |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| V100-personality | 3 | 12.038 | 0.436 | 27.61 | 0.15048 | 0.005450 |
| CMP-labelled | 6 | 10.751 | 0.366 | 29.34 | 0.15811 | 0.005389 |

The DGEMM result scales almost exactly with visible SM count:

```text
80 / 68 = 1.176
0.436 / 0.366 = 1.19
```

This means the V100-personality cards are not full-speed FP64 V100s; they are the same capped class with 80 visible SMs instead of 68.

## Interpretation

On a normal V100-class part, FP64 throughput should be roughly one half of FP32 throughput. Using local SGEMM as the per-card FP32 reference:

```text
expected full-speed DGEMM ~= SGEMM / 2
observed DGEMM fraction   ~= 0.068 to 0.073 of that expectation
inverse                   ~= 13.6x to 14.7x slower than the local FP32-implied FP64 rate
```

This is a strong downstream signature for `DP_REDUCED + DP_OVERRIDE`.

The existing Tensor/HMMA evidence already proves `FMLA_REDUCED + FMLA_OVERRIDE` has a downstream effect. This SGEMM/DGEMM pass shows the same capped VBIOS/FECS path also affects FP64/DP throughput.

IMLA remains less proven because the local integer probe does not have a known full-speed GV100 integer-control card and did not show a clean HMMA/DP-class collapse by itself.

## Raw Artifacts

```text
runs/20260520-sgemm-dgemm-ratio/wan2gp-gpu5.json
runs/20260520-sgemm-dgemm-ratio/wan2gp-gpu6.json
runs/20260520-sgemm-dgemm-ratio/wan2gp-gpu7.json
runs/20260520-sgemm-dgemm-ratio/wan2gp-gpu8.json
runs/20260520-sgemm-dgemm-ratio/wan2gp-gpu9.json
runs/20260520-sgemm-dgemm-ratio/wan2gp-gpu10.json
runs/20260520-sgemm-dgemm-ratio/wan2gp-gpu11.json
runs/20260520-sgemm-dgemm-ratio/wan2gp-gpu13.json
runs/20260520-sgemm-dgemm-ratio/wan2gp-gpu15.json
```
