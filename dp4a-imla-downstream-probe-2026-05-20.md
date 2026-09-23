# DP4A / IMLA Downstream Probe - 2026-05-20

Purpose: refine the weak `IMLA_REDUCED + IMLA_OVERRIDE` downstream evidence from the `0x999` GV100 FECS speed-select mask.

The earlier integer probe used generic `IMAD`, which is not clearly the same instruction family as NVIDIA's `IMLA` speed-select fields. A source pass found a closer later-generation mapping:

```text
Turing:
  Instr::IMMA_8816_S32_S8 -> IMLA0
  Instr::IDP4A_S32_S8     -> IMLA3

Ampere:
  Instr::IMMA_8816_S32_S8 / IMMA_16832_S32_S8 / IMMA_16864SP_S32_S8 -> IMLA0
  Instr::IMMA_16864_S32_S4                                          -> IMLA1
  Instr::BMMA_*                                                     -> IMLA2
  Instr::IDP4A_S32_S8                                               -> IMLA3
  Instr::HMMA_*_TF32                                                -> IMLA4
```

Source anchors:

```text
/home/josh/containers/temp/nvdrv/integ/gpu_drv/stage_rel/diag/mods/gpu/turinggpu.cpp:1655-1710
/home/josh/containers/temp/nvdrv/integ/gpu_drv/stage_rel/diag/mods/gpu/amperegpu.cpp:4736-4895
```

This does not prove GV100 uses the same internal mapping, but it makes `IDP4A` a better local read-only probe than generic `IMAD`.

## Probe

Added:

```text
scripts/dp4a_issue_probe.cu
runs/20260520-dp4a-imla-probe/dp4a_issue_probe
```

The compiled SASS contains real Volta `IDP.4A.S8.S8` instructions:

```text
runs/20260520-dp4a-imla-probe/dp4a-sass-snippet.txt
```

Run shape:

```text
loops  = 65536
blocks = 2048
threads = 128
```

## Results

| Container | Reported device | SMs | Dep cycles/DP4A | Ind4 cycles/DP4A | Ind4 GDP4A/s | Ind4 Gint8-madd/s | Ind4 GDP4A/s/SM |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `wan2gp-gpu7` | Tesla V100-PCIE-12GB | 80 | 97.47 | 30.94 | 4539.0 | 18155.9 | 56.74 |
| `wan2gp-gpu9` | Tesla V100-PCIE-12GB | 80 | 96.86 | 30.90 | 4952.0 | 19807.9 | 61.90 |
| `wan2gp-gpu11` | Tesla V100-PCIE-12GB | 80 | 97.15 | 32.70 | 4471.2 | 17885.0 | 55.89 |
| `wan2gp-gpu5` | NVIDIA CMP 100-210 | 68 | 105.78 | 34.37 | 4115.3 | 16461.4 | 60.52 |
| `wan2gp-gpu6` | NVIDIA CMP 100-210 | 68 | 105.72 | 34.46 | 4114.3 | 16457.3 | 60.50 |
| `wan2gp-gpu8` | NVIDIA CMP 100-210 | 68 | 105.98 | 34.74 | 4613.6 | 18454.3 | 67.85 |
| `wan2gp-gpu10` | NVIDIA CMP 100-210 | 68 | 105.89 | 34.39 | 4108.8 | 16435.2 | 60.42 |
| `wan2gp-gpu13` | NVIDIA CMP 100-210 | 68 | 105.92 | 34.51 | 4113.6 | 16454.3 | 60.49 |
| `wan2gp-gpu15` | NVIDIA CMP 100-210 | 68 | 105.74 | 34.35 | 4247.1 | 16988.5 | 62.46 |

Group averages:

| Group | Count | Dep cycles/DP4A | Ind4 cycles/DP4A | Ind4 GDP4A/s | Ind4 GDP4A/s/SM |
| --- | ---: | ---: | ---: | ---: | ---: |
| V100-personality | 3 | 97.16 | 31.52 | 4654.1 | 58.18 |
| CMP-labelled | 6 | 105.84 | 34.47 | 4218.8 | 62.04 |

## Interpretation

This does not show the same kind of collapse seen in HMMA/FMLA or DGEMM/DP. The DP4A result is broadly healthy and does not separate V100-personality from CMP-labelled cards by a large issue-rate factor.

What this proves:

```text
The previous generic IMAD probe was too weakly mapped to IMLA.
The new DP4A probe hits a later-source-mapped IMLA-family instruction: IDP4A_S32_S8 -> IMLA3.
Local DP4A does not show an HMMA/DP-class 1/16 collapse.
```

What this does not prove:

```text
It does not prove GV100's older single IMLA bit has no effect.
It does not test IMLA0 integer matrix multiply, because GV100 lacks the newer IMMA path exposed in Turing/Ampere mappings.
It does not replace a real full-speed GV100 control for IMLA.
```

Current status:

```text
FMLA/HMMA: proven downstream cap.
DP/FP64:   proven downstream cap.
IMLA:      no DP4A-class collapse observed; GV100 IMLA effect remains unresolved.
```
