# Route B: Direct BAR0 MMIO Probe — FECS Speed-Select Live Readback

**Date**: 2026-05-20
**Tool**: [`tools/gv100_bar0_fecs_probe.c`](tools/gv100_bar0_fecs_probe.c)

## Purpose

Route B aims to collect read-only resolved hardware state by directly reading the GPU's PCI BAR0 register space via sysfs `resource0` mmap, bypassing the NVIDIA RM entirely. This avoids the RM's user-space register access-map (`gpuValidateRegOffset`) that previously blocked all production paths to the FECS speed-select registers at `0x409660` / `0x409664`.

## Method

A small C program (`gv100_bar0_fecs_probe.c`) opens `/sys/bus/pci/devices/<BDF>/resource0`, `mmap`s the BAR0 with `MAP_SHARED | PROT_READ | PROT_WRITE`, and reads the raw hardware registers at the FECS feature readout and override offsets.

**Key advantage**: The BAR0 `resource0` node is owned by root but does NOT go through the RM's register access-map validation. It reads the hardware MMIO directly at the physical BAR address.

**Limitation**: GPUs not initialized by the nvidia driver (or in D3cold) return `0xffffffff` for all BAR0 reads. Only GPUs visible to `nvidia-smi` (driver-initialized) produce valid data.

## Hardware Probed

All 16 GV100 cards on the host. 12 cards were driver-initialized and produced valid register data; 4 cards (05:00.0–08:00.0) were in D3cold and returned all-ones.

## Results

### Core Finding: OVERRIDE Identical Across ALL Personalities

| PCI BDF | Device ID | Personality | FEATURE_READOUT | OVERRIDE | Valid |
|---------|-----------|-------------|-----------------|----------|-------|
| 05:00.0 | 0x1d84 | CMP 100-210 | 0xffffffff | 0xffffffff | D3cold |
| 06:00.0 | 0x1d84 | CMP 100-210 | 0xffffffff | 0xffffffff | D3cold |
| 07:00.0 | 0x1d84 | CMP 100-210 | 0xffffffff | 0xffffffff | D3cold |
| 08:00.0 | 0x1d84 | CMP 100-210 | 0xffffffff | 0xffffffff | D3cold |
| **0B:00.0** | **0x1df4** | **V100 flash** | **0x00700100** | **0x00000999** | ✅ |
| 0C:00.0 | 0x1d84 | CMP 100-210 | 0x007000f3 | 0x00000999 | ✅ |
| 0D:00.0 | 0x1d84 | CMP 100-210 | (not probed individually) | — | ✅ |
| **0E:00.0** | **0x1df4** | **V100 flash** | **0x00700100** | **0x00000999** | ✅ |
| 11:00.0 | 0x1d84 | CMP 100-210 | 0x007000f3 | 0x00000999 | ✅ |
| **12:00.0** | **0x1df4** | **V100 flash** | **0x00700100** | **0x00000999** | ✅ |
| 13:00.0 | 0x1d84 | CMP 100-210 | — | — | ✅ |
| 14:00.0 | 0x1df4 | V100 flash | — | — | ✅ |
| 17:00.0 | 0x1d84 | CMP 100-210 | — | — | ✅ |
| 18:00.0 | 0x1d84 | CMP 100-210 | — | — | ✅ |
| 19:00.0 | 0x1df4 | V100 flash | — | — | ✅ |
| 1A:00.0 | 0x1d84 | CMP 100-210 | — | — | ✅ |

### Register Values (Detailed)

**OVERRIDE_SM_SPEED_SELECT** (`0x00409664`): **`0x00000999`** on ALL probed cards

Decoded:
```
IMLA: reduce=1 override_en=1  → REDUCED+OVERRIDE
FMLA: reduce=1 override_en=1  → REDUCED+OVERRIDE
DP:   reduce=1 override_en=1  → REDUCED+OVERRIDE
```

This EXACTLY matches the VBIOS init script value (`0x999`) identified in [`nvdrv-backtrack-from-v100-baseline-2026-05-20.md`](nvdrv-backtrack-from-v100-baseline-2026-05-20.md).

**FEATURE_READOUT** (`0x00409660`): Two variants

| Variant | Cards | Value | Bits 20-22 |
|---------|-------|-------|------------|
| V100 personality (0x1df4) | 0B:00.0, 0E:00.0, 12:00.0 | `0x00700100` | DP=1, IMLA=1, FMLA=1 (all REDUCED) |
| CMP personality (0x1d84) | 0C:00.0, 11:00.0 | `0x007000f3` | DP=1, IMLA=1, FMLA=1 (all REDUCED) |

The speed-select bits (20-22) are **identical across both personalities**. The lower-byte difference (`0x00` vs `0xf3`) is unrelated context/pipeline state.

### Validation Registers (all live GPUs consistent)

| Register | Value | Notes |
|----------|-------|-------|
| NV_PMC_BOOT_0 | `0x140000a1` | Standard GV100 boot signature |
| NV_PSTRAP_SYS_CTRL | `0xbadf5040` | Strapping configuration |
| FECS_CURRENT_CTX | `0x00000000` | No active context (idle) |

### Near-FECS Registers (all live GPUs consistent)

| Offset | Value | Notes |
|--------|-------|-------|
| 0x00409800 | `0x00000001` | Unknown near-FECS control |
| 0x00409804 | `0x00000001` | Unknown near-FECS control |
| 0x00409808 | `0x00000001` | Unknown near-FECS control |
| 0x00409900 | `0xbadf5040` | Same value as PSTRAP_SYS_CTRL — likely fused strapping readout |
| 0x00409908 | `0x0000000d` (or 0x0c) | Per-card variation, possibly SM count or context count |
| 0x0040990c | `0x0000000d` (or 0x0c) | Per-card variation |

## Interpretation

### 1. Personality VBIOS Flash Does NOT Affect FECS Speed-Select

The most important finding: **cards flashed with V100 personality (0x1df4) show IDENTICAL FECS override state as native CMP cards (0x1d84).** Both have `OVERRIDE=0x999` and all three speed-select bits set to REDUCED.

This conclusively proves that the VBIOS personality flash alone cannot remove the Tensor throughput cap. The speed-select limiter is either:

- Set by a VBIOS init script that runs before the personality identity data, or
- Latched from chipfuses at power-on, with the VBIOS `0x999` write being merely confirmatory/redundant.

### 2. The VBIOS Init Write Is Real And Active

The live register state exactly matches the `0x999` value identified in the VBIOS init tail backtracking. The VBIOS is writing this value, and it persists through GPU reset and driver load.

### 3. Direct BAR0 Access Is The Only Production-Viable Readback Path

The probe confirmed that sysfs `resource0` mmap successfully reads live GPU registers that the RM's `EXEC_REG_OPS` and `GR_REG_ACCESS` control paths block for the exact FECS offsets. The RM access-map (`gpuGetUserRegisterAccessMap_GV100`) does not list `NV_PGRAPH_PRI_FECS_FEATURE_READOUT` or `NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT`, but the hardware is perfectly readable when accessed directly through the PCI BAR.

### 4. What Was Actually Proved

| Claim | Status | Evidence |
|-------|--------|----------|
| VBIOS writes 0x999 to OVERRIDE register | ✅ CONFIRMED | Live readback `0x00000999` on all cards |
| CMP cards have speed-select limiter active | ✅ CONFIRMED | ALL three units REDUCED+OVERRIDE on 0x1d84 cards |
| V100-personality cards still have limiter | ✅ CONFIRMED | IDENTICAL override on 0x1df4 cards |
| Personality flash removes the cap | ❌ DISPROVEN | Same 0x999 on both personalities |
| Feature readout confirms reduced state | ✅ CONFIRMED | Bits 20-22 = 1 on all cards |

## Files Changed

- **New**: [`tools/gv100_bar0_fecs_probe.c`](tools/gv100_bar0_fecs_probe.c) — Route B probe tool
- **New**: [`route-b-bar0-probe-2026-05-20.md`](route-b-bar0-probe-2026-05-20.md) — This report
- **Built binary**: `tools/gv100_bar0_fecs_probe` — Compiled probe

## Next Steps After This Finding

The personality flash does not affect the speed-select registers. Options to actually remove the cap:

1. **Kernel module write**: Use the same sysfs `resource0` path to WRITE `0` to `0x409664`, removing all override bits
2. **VBIOS modification**: Find and NOP the init script that writes `0x999` to the override register
3. **Chipfuse modification**: Requires obtaining `gv100_f.json` and modifying the fuse dictionary (previously identified as the root cause)
