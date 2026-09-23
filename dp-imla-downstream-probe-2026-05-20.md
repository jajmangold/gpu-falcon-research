# DP / IMLA Downstream Probe - 2026-05-20

## Purpose

The decoded GV100 VBIOS init write sets:

```text
0x409664 = 0x999
  IMLA reduced + override
  FMLA reduced + override
  DP reduced + override
```

FMLA/HMMA was already measured through Nsight Compute and the HMMA issue-spacing probe. This pass adds read-only downstream checks for DP and IMLA.

## Artifacts

Sources:

```text
scripts/dp_imla_issue_probe.cu
scripts/dgemm_probe.cu
```

Run directory:

```text
runs/20260520-dp-imla-issue/
```

Compiled binaries:

```text
runs/20260520-dp-imla-issue/dp_imla_issue_probe
runs/20260520-dp-imla-issue/dgemm_probe
```

The binaries were compiled with CUDA 12.8 inside the existing `local/wan2gp:sm70-gguf-cuda` image using `-arch=sm_70`.

## Devices

The host CUDA runtime did not enumerate some physical indices cleanly from the bare host, so the final runs were executed inside the existing per-GPU containers:

| Role | Container | Device | UUID | PCI bus |
| --- | --- | --- | --- | --- |
| V100-personality | `wan2gp-gpu11` | `Tesla V100-PCIE-12GB` | `GPU-1abdcc29-1477-c2d3-885b-8286abb29cd7` | `00000000:14:00.0` |
| CMP baseline | `wan2gp-gpu5` | `NVIDIA CMP 100-210` | `GPU-68309a3d-8ef1-8660-8634-6953b027b34a` | `00000000:0C:00.0` |

Both report a `120 W` power limit.

## SASS Check

`cuobjdump` confirms the probe contains the intended instruction families:

```text
DFMA
IMAD
```

Artifact:

```text
runs/20260520-dp-imla-issue/dp-imla-sass-snippet.txt
```

## One-Wave Scalar Issue Probe

This run uses block count near SM count to reduce multi-wave scheduling effects:

```text
V100-personality: loops=65536 blocks=80 threads=128
CMP baseline:     loops=65536 blocks=68 threads=128
```

| Case | V100-personality cycles/op/thread | V100 throughput | CMP cycles/op/thread | CMP throughput |
| --- | ---: | ---: | ---: | ---: |
| DP dependent DFMA | `64.0108` | `365.41 GFLOP/s` | `64.0102` | `310.51 GFLOP/s` |
| DP independent4 DFMA | `64.0008` | `366.71 GFLOP/s` | `64.0008` | `311.68 GFLOP/s` |
| IMLA dependent IMAD | `28.0091` | `415.84 GOP/s` | `28.0077` | `361.02 GOP/s` |
| IMLA independent4 IMAD | `6.5020` | `1788.16 GOP/s` | `6.5020` | `1551.69 GOP/s` |

Artifacts:

```text
runs/20260520-dp-imla-issue/gpu11-v100-onewave-dp-imla-issue-probe.json
runs/20260520-dp-imla-issue/gpu5-cmp-onewave-dp-imla-issue-probe.json
```

## Saturating Scalar Issue Probe

This run uses many more blocks:

```text
loops=32768 blocks=2048 threads=128
```

| Case | V100-personality throughput | CMP throughput |
| --- | ---: | ---: |
| DP dependent DFMA | `293.65 GFLOP/s` | `345.32 GFLOP/s` |
| DP independent4 DFMA | `354.18 GFLOP/s` | `354.96 GFLOP/s` |
| IMLA dependent IMAD | `2460.72 GOP/s` | `2135.05 GOP/s` |
| IMLA independent4 IMAD | `5454.23 GOP/s` | `4747.37 GOP/s` |

Artifacts:

```text
runs/20260520-dp-imla-issue/gpu11-v100-docker-dp-imla-issue-probe.json
runs/20260520-dp-imla-issue/gpu5-cmp-docker-dp-imla-issue-probe.json
```

## cuBLAS DGEMM Probe

DGEMM is a stronger library-level DP check than the scalar loop:

```text
n=4096
iters=5
```

| Device | SMs | DGEMM TFLOP/s |
| --- | ---: | ---: |
| V100-personality | 80 | `0.4368` |
| CMP baseline | 68 | `0.3665` |

Artifacts:

```text
runs/20260520-dp-imla-issue/gpu11-v100-dgemm-probe.json
runs/20260520-dp-imla-issue/gpu5-cmp-dgemm-probe.json
```

## Nsight Compute

Nsight Compute exists in the CUDA container, but counter collection is blocked:

```text
ERR_NVGPUCTRPERM
```

Artifact:

```text
runs/20260520-dp-imla-issue/gpu11-v100-dp-imla-ncu.csv
```

The file contains program stdout plus the permission error, not usable counter values.

## Interpretation

DP now has a strong downstream signal:

```text
V100-personality DGEMM: 0.4368 TFLOP/s
CMP DGEMM:             0.3665 TFLOP/s
```

Those values are far below normal full-speed V100 FP64-class throughput and scale roughly with visible SM count:

```text
80 / 68 = 1.176
0.4368 / 0.3665 = 1.192
```

That matches the idea that DP is rate-limited on both board personalities, while the V100-personality image mainly changes visible SM count.

IMLA is less decisive. The integer probe runs much faster than DP and does not show a HMMA-like 15-16x collapse from local-only evidence. Without a full-speed GV100 integer-control run, the IMLA downstream result should be treated as inconclusive rather than proof that IMLA is or is not reduced.

## Current Conclusion

Measured downstream effects now align as:

| Field asserted by `0x999` | Downstream result |
| --- | --- |
| `FMLA_REDUCED + FMLA_OVERRIDE` | Strongly supported by `6.25%` Nsight tensor throughput and `14.5-15.7x` HMMA cycle cost. |
| `DP_REDUCED + DP_OVERRIDE` | Supported by very low local DGEMM/DFMA throughput, scaling mainly with SM count. |
| `IMLA_REDUCED + IMLA_OVERRIDE` | Not yet proven from local-only integer measurements; needs full-speed GV100 integer-control comparison or counters. |

This makes the VBIOS `0x409664 = 0x999` write look even more like a real downstream policy surface, not a dead or ignored init-script write.
