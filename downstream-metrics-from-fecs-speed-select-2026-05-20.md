# Downstream Metrics From GV100 FECS Speed Select - 2026-05-20

## Purpose

The VBIOS init-script backtrack found a direct write:

```text
NV_REG R[0x409664] &= 0xfffff666 |= 0x00000999
```

On GV100 this asserts:

```text
IMLA reduced + override
FMLA reduced + override
DP reduced + override
```

Even when live readback of `0x409660` / `0x409664` is blocked by RM user-access maps, those states should have measurable downstream effects.

## Already Measured

### FMLA / HMMA / Tensor

Nsight Compute on local V100-personality cards:

```text
sm__throughput.avg.pct_of_peak_sustained_elapsed = 6.25%
sm__inst_executed_pipe_tensor.sum                = 461373440
```

Interpretation: Tensor instructions execute, but sustained throughput is exactly `1/16` of peak.

Minimal HMMA loop:

```text
Local V100-personality: 3416-3536 cycles / WMMA op
Local CMP:              3675-3715 cycles / WMMA op
Full-speed Vast V100:    236-243 cycles / WMMA op
```

Interpretation: the limiter is visible in a tiny HMMA loop, not just in cuBLAS/CUTLASS scheduling.

Existing reports:

```text
tensor-throughput-slowdown-diagnosis-2026-05-19.md
hmma-issue-spacing-2026-05-20.md
```

## Highest-Value Additional Downstream Probes

| FECS field from `0x999` | Expected affected path | Read-only measurement | Useful comparison |
| --- | --- | --- | --- |
| `FMLA_REDUCED + FMLA_OVERRIDE` | HMMA / Tensor Core FP16 mixed GEMM | Existing NCU tensor throughput and HMMA issue-spacing | Local V100-personality vs Vast/full-speed V100 |
| `DP_REDUCED + DP_OVERRIDE` | FP64 / DFMA throughput | FP64 FMA microbenchmark, DGEMM, or NCU FP64 pipe metrics | Local V100-personality vs public/full-speed V100 |
| `IMLA_REDUCED + IMLA_OVERRIDE` | Integer multiply-add style issue path | INT32 multiply-add microbenchmark and integer pipe issue counters | Local V100-personality vs local non-GV100 control or public expected rate |

The strongest new discriminator is DP. If `DP_REDUCED` is really asserted by the same VBIOS write, FP64 throughput should show an issue-rate anomaly separate from Tensor Core behavior.

## Metrics To Prefer

Nsight Compute metrics, when permissions allow:

```text
sm__throughput.avg.pct_of_peak_sustained_elapsed
sm__cycles_elapsed.avg
sm__cycles_active.avg
sm__inst_executed_pipe_tensor.sum
sm__inst_executed_pipe_fp64.sum
sm__inst_executed_pipe_adu.sum
smsp__issue_active.avg.pct_of_peak_sustained_active
smsp__warps_active.avg.pct_of_peak_sustained_active
smsp__warp_issue_stalled_selected_per_warp_active.pct
smsp__warp_issue_stalled_short_scoreboard_per_warp_active.pct
smsp__warp_issue_stalled_math_pipe_throttle_per_warp_active.pct
```

If some exact metric names differ by Nsight Compute version, the fallback is to collect the full `--set full` CSV for one short kernel and filter for:

```text
pipe_tensor
pipe_fp64
pipe_adu
issue_active
stall
throttle
```

## Microbenchmarks

Use short, fixed-iteration kernels with device-side `clock64()` timing, as in `scripts/hmma_issue_probe.cu`.

Recommended additions:

1. `dp_issue_probe.cu`

   Repeated dependent and independent FP64 FMA loops. Report cycles per DFMA and achieved FP64 GFLOPS.

2. `imla_issue_probe.cu`

   Repeated integer multiply-add loops. Report cycles per IMAD-style operation and integer GOPS.

3. `ncu_metric_probe.sh`

   Runs one short HMMA, FP64, and IMLA kernel under Nsight Compute with a targeted metric list plus a fallback full metric capture.

## Interpretation Rules

If FMLA/HMMA, DP, and IMLA all show matching issue-rate collapse, that strongly supports the exact `0x999` FECS speed-select override as the live downstream policy.

If only FMLA/HMMA is slow while DP and IMLA are normal, then the VBIOS `0x999` write may still assert the register, but the effective limiter would be Tensor-specific firmware policy below the binary GV100 reduced/full abstraction.

If DP is slow and HMMA is slow but IMLA is normal, the likely split is floating-point/math scheduler policy rather than a broad SM issue gate.

If all three are normal on a full-speed V100 control that lacks the `0x999` opcode, the VBIOS init-script difference is a strong causal candidate even without live FECS register readback.

## Current Status

Already proven:

```text
FMLA/HMMA downstream impact exists and is large.
DP downstream impact is now supported by local DFMA/DGEMM probes.
```

New report:

```text
dp-imla-downstream-probe-2026-05-20.md
sgemm-dgemm-ratio-probe-2026-05-20.md
```

The SGEMM/DGEMM ratio sweep is the cleaner DP downstream measurement. Across 3 V100-personality cards and 6 CMP-labelled cards:

```text
V100-personality average SGEMM/DGEMM ratio: 27.61
CMP-labelled average SGEMM/DGEMM ratio:     29.34
```

Using the local SGEMM result as the FP32 reference, observed DGEMM is only about `6.8-7.3%` of the expected full-speed V100 FP64 rate. That is a strong downstream signature for `DP_REDUCED + DP_OVERRIDE`.

Not yet proven in this workspace:

```text
IMLA downstream impact from IMLA_REDUCED.
NCU stall/issue-counter signature across HMMA vs FP64 vs integer kernels.
```

IMLA remains inconclusive from local-only measurements because the integer probes do not have a full-speed GV100 control and do not show a HMMA-like collapse by themselves.

A closer IMLA-family DP4A probe was added after source backtracking showed later-generation MODS maps:

```text
IDP4A_S32_S8 -> IMLA3
```

New report:

```text
dp4a-imla-downstream-probe-2026-05-20.md
```

That probe emits real `IDP.4A.S8.S8` SASS and still does not show an HMMA/DP-class collapse. This weakens the idea that every `IMLA`-named downstream path is visibly capped through normal CUDA integer instructions, but it does not prove GV100's older single IMLA bit has no effect.

Nsight Compute issue/stall counters remain blocked by `ERR_NVGPUCTRPERM` in the container used for the DP/IMLA pass.
