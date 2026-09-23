# NVDRV Backtrack From V100 Baseline - 2026-05-20

## Purpose

Backtrack the observed CMP/V100-personality `1/16` Tensor Core throughput cap through the local NVIDIA driver/source tree at:

```text
/home/josh/containers/temp/nvdrv
```

File discovery in this pass used `fdfind` first, then targeted `rg`/line reads inside the `fdfind`-selected file set.

## Observed Anchors

Local V100-personality cards:

```text
Device:        Tesla V100-PCIE-12GB
PCI ID:        10de:1df4
Subsystem:     10de:12b8
VBIOS:         88.00.51.00.04
InfoROM IMG:   G001.0000.01.04
SMs:           80
Tensor result: 6.25% of sustained SM peak on Tensor Core GEMM
```

Known full-speed V100 controls:

```text
Device:        Tesla V100-SXM2-16GB
PCI ID:        10de:1db1
Subsystem:     10de:1212
VBIOS:         88.00.4F.00.09 / 88.00.13.00.02
InfoROM IMG:   G503.0201.00.03
Tensor result: full-speed V100-class HMMA throughput
```

## Backtracking Map

### 0. A100/CMP 170HX Gist Transfer

The A100 gist is useful as a search vocabulary source, not as a direct GV100 offset map.

Gist:

```text
https://gist.github.com/duggasco/819f65d599be39c148b8fcaea42c4d8d
```

The transferable terms are:

```text
SM_SPEED_SELECT
FMLA16
FMLA32
IMLA0
FEATURE_READOUT
FEATURE_OVERRIDE
EN_SW_OVERRIDE
CTRL_OPT
```

Those terms led to two distinct local paths:

1. Ampere/GA100 fuse-register definitions, where `FMLA16` has a 3-bit speed-select value and explicit `1/16` override values.
2. Volta/GV100 FECS/CTXSW firmware feature override definitions, where the exposed GV100 field is coarser: `FMLA_FULL_SPEED` vs `FMLA_REDUCED_SPEED`.

This means the A100 gist does not prove the GV100 offset, but it points at the correct mechanism family.

### 1. `1DF4` Is Not A Clean Proof Of Tesla Board Class

`nvlConsts.h` maps GV100 IDs in a way that makes the local V100-personality PCI ID ambiguous:

```text
NV_GV100_TESLA_4_B  0x1DF4
NV_GV100_PG500_8    0x1DF4
```

Source:

```text
integ/gpu_drv/stage_rel/drivers/display/lddm/nvlddmkm/nvlConsts.h:1759
integ/gpu_drv/stage_rel/drivers/display/lddm/nvlddmkm/nvlConsts.h:1771
```

`nvlFilter.cpp` also whitelists both the Tesla and PG500/PG503 GV100 identities in the same GV100 device list:

```text
integ/gpu_drv/stage_rel/drivers/display/lddm/nvlddmkm/nvlFilter.cpp:615-644
```

Interpretation: changing visible PCI ID to `1DF4` is not enough evidence that every internal policy path now treats the board as a full V100. The source itself names `1DF4` as both a Tesla-family ID and a `PG500` ID.

### 2. RM Explicitly Models The Exact `1/16` Issue-Rate State

The RM control header exposes:

```text
NV2080_CTRL_CMD_GR_GET_SM_ISSUE_RATE_MODIFIER
```

It returns speed-select values for `IMLA0`, `FMLA16`, `DP`, `FMLA32`, `FFMA`, and other instruction classes.

The exact observed divisor is represented directly:

```text
NV2080_CTRL_GR_GET_SM_ISSUE_RATE_MODIFIER_FMLA16_REDUCED_SPEED_1_16 = 0x4
```

Source:

```text
integ/gpu_drv/stage_rel/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080gr.h:1726-1835
```

There is also an internal static form:

```text
NV2080_CTRL_CMD_INTERNAL_STATIC_KGR_GET_SM_ISSUE_RATE_MODIFIER = 0x20800a34
NV2080_CTRL_CMD_INTERNAL_STATIC_GR_GET_SM_ISSUE_RATE_MODIFIER  = 0x20800a35
```

Source:

```text
integ/gpu_drv/stage_rel/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080internal.h:663-690
```

Interpretation: `1/16` is not just a benchmark coincidence. It is an explicit NVIDIA RM concept named as an SM instruction issue-rate modifier.

### 3. Internal Chip Info Has The Fields We Care About

`ctrl2080internal.h` defines an internal chip-info query containing:

```text
isCmpSku
bar1Size
pciDeviceId
pciSubDeviceId
pciRevisionId
regBases[]
```

Source:

```text
integ/gpu_drv/stage_rel/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080internal.h:698-717
```

Interpretation: the driver has an internal distinction for CMP SKU state that is separate from the ordinary public product name. This matches our observed problem: public identity changed, but the throughput policy did not.

### 4. InfoROM/OEM/OBD Is A Major Identity Path

Public GPU controls expose InfoROM object and image queries:

```text
NV2080_CTRL_CMD_GPU_GET_INFOROM_OBJECT_VERSION = 0x2080014b
NV2080_CTRL_CMD_GPU_GET_INFOROM_IMAGE_VERSION  = 0x20800156
NV2080_CTRL_CMD_GPU_QUERY_INFOROM_ECC_SUPPORT  = 0x20800157
```

Relevant object names include:

```text
ECC
IMG
OBD
OEM
PWR
```

Source:

```text
integ/gpu_drv/stage_rel/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080gpu.h:2788-3018
```

The fwd-compat R440 header also exposes OEM board and OEM blob controls:

```text
NV2080_CTRL_CMD_GPU_GET_OEM_BOARD_INFO
NV2080_CTRL_CMD_GPU_GET_OEM_INFO
```

Source:

```text
integ/gpu_drv/stage_rel/drivers/gpgpu/cuda/import/fwd_compat/r440/27570321/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080gpu.h:2013-2076
integ/gpu_drv/stage_rel/drivers/gpgpu/cuda/import/fwd_compat/r440/27570321/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080gpu.h:3347-3371
```

PMU/SMBPBI code explicitly caches static OBD/OEM data in `INFOROMHALINFO_FERMI`:

```text
The static info like OBD and OEM is cached in INFOROMHALINFO_FERMI
```

Source:

```text
integ/gpu_drv/stage_rel/pmu_sw/prod_app/smbpbi/nv/smbpbi.c:130-190
```

Interpretation: this gives a plausible path for `G001` / CMP board residue to survive a VBIOS identity change and still feed board/SKU policy.

### 5. MODS Has A Concrete Issue-Rate Override Mechanism On Later Chips

The MODS Turing/Ampere paths contain concrete code for reading and setting instruction issue-rate override fields. The relevant unit for Tensor Core FP16 HMMA is `FMLA16`.

Ampere example:

```text
FMLA16 full speed -> 1.0
FMLA16 reduced 1/16 -> 1.0 / 16.0
```

Source:

```text
integ/gpu_drv/stage_rel/diag/mods/gpu/amperegpu.cpp:4637-4675
```

The same file checks whether the override path is supported and whether the register is protected:

```text
Chip under test does not support instruction-issue rate override (Volta-953)
Instruction issue-rate override ... not supported on this board
```

Source:

```text
integ/gpu_drv/stage_rel/diag/mods/gpu/amperegpu.cpp:5002-5041
```

Turing has a parallel path:

```text
integ/gpu_drv/stage_rel/diag/mods/gpu/turinggpu.cpp:1790-1915
```

Interpretation: this is not direct GV100 proof, but it is strong architectural evidence that NVIDIA diagnostics understand instruction issue-rate as a real, register-backed speed-select mechanism. The code comments call this path `Volta-953`, which is a useful internal breadcrumb.

### 6. GV100 Fuse Dictionary Is Still Missing

The initial `fdfind` pass found the small GV100 `swref` fuse addendum:

```text
integ/gpu_drv/stage_rel/drivers/common/inc/swref/volta/gv100/dev_fuse_addendum.h
```

That file only contains ADC calibration addendum fields, not the full GV100 fuse dictionary and not SM speed-select fields.

The later A100-gist-guided pass found the full GV100 `hwref` fuse and FECS/CTXSW firmware headers:

```text
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_fuse.h
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_fuse_off.h
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_ctxsw_firmware.h
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_graphics_nobundle.h
```

GV100 has the software-fuse override register:

```text
NV_FUSE_EN_SW_OVERRIDE = 0x00021040
```

Source:

```text
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_fuse.h:99-104
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_fuse_off.h:31
```

GV100 also has FECS/CTXSW firmware speed-select readout and override definitions:

```text
NV_CTXSW_FIRMWARE_FEATURE_READOUT_SM_SPEED_SELECT_FMLA
NV_CTXSW_FIRMWARE_FEATURE_OVERRIDE_SM_SPEED_SELECT = 0x00019900
NV_CTXSW_FIRMWARE_FEATURE_OVERRIDE_SM_SPEED_SELECT_FMLA
NV_CTXSW_FIRMWARE_FEATURE_OVERRIDE_SM_SPEED_SELECT_FMLA_OVERRIDE
```

Source:

```text
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_ctxsw_firmware.h:3173-3181
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_ctxsw_firmware.h:3190-3209
```

The GV100 FECS/CTXSW definitions are coarser than the Ampere definitions found from the A100 search terms. They expose `FULL_SPEED` vs `REDUCED_SPEED` for `FMLA`, not explicit `FMLA16_REDUCED_SPEED_1_16` in the hwref header. The explicit `1/16` value still appears in RM control headers:

```text
NV2080_CTRL_GR_GET_SM_ISSUE_RATE_MODIFIER_FMLA16_REDUCED_SPEED_1_16 = 0x4
```

Later chips do expose speed-select addendum names such as:

```text
NV_FUSE_FEATURE_OVERRIDE_SM_SPEED_SELECT_FMLA16_REDUCED_SPEED_1_64
```

Sources:

```text
integ/gpu_drv/stage_rel/drivers/common/inc/swref/ampere/ga100/dev_fuse_addendum.h
integ/gpu_drv/stage_rel/drivers/common/inc/swref/ampere/ga102/dev_fuse_addendum.h
integ/gpu_drv/stage_rel/drivers/common/inc/swref/hopper/gh100/dev_fuse_addendum.h
```

Interpretation: the current tree exposes enough to identify the GV100 control family:

```text
FECS/CTXSW firmware feature override -> SM speed select -> FMLA reduced speed
```

What is still missing is the exact mapping from GV100's coarse `FMLA_REDUCED_SPEED` bit to the measured `1/16` issue-rate divisor. That mapping likely lives in closed RM/firmware policy or in generated metadata not surfaced as a simple GV100 `dev_fuse.h` enum.

### 7. MODS Tesla Specs Expect Full HMMA On PG500/PG503 Test Boards

The Tesla MODS specs include HMMA linpack lower bounds for GV100 board classes:

```text
PG500SKU200 CudaLinpackHMMAgemm:           71346 GFLOPS
PG500SKU200 CudaLinpackHMMAgemm.h884_fp16: 75341 GFLOPS
PG503SKU201 CudaLinpackHMMAgemm:           79600 GFLOPS
PG503SKU201 CudaLinpackHMMAgemm.h884_fp16: 85239 GFLOPS
```

Source:

```text
integ/gpu_drv/stage_rel/diag/mods/contrib/tesla/tesla_boards.spc:55-63
integ/gpu_drv/stage_rel/diag/mods/contrib/tesla/tesla_boards.spc:134-147
```

Interpretation: the historical Tesla diagnostic path expected normal HMMA throughput on PG500/PG503 GV100 variants. The local card's `6.25%` result is therefore a policy/SKU cap, not a normal PG500/V100 performance envelope.

### 8. GV100 Has FECS Speed-Select Bits, But RM Stubs Public Readback Before Turing

The `dev` RM tree contains the live control implementation for later chips:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/gr/nv/objgr.c:4711
nv2080CtrlCmdGrGetSmIssueRateModifier(...)
  -> grGetSmIssueRateModifier_HAL(...)
```

The HAL dispatch table limits that implementation to Turing and newer:

```text
dev/gpu_drv/stage_rel/drivers/resman/config/haldefs/gr.def:7500-7509
GET_SM_ISSUE_RATE_MODIFIER
  _TU102 => [ TURING, ]
  _GA100 => [ AMPERE_and_later, ]
  _STUB  => [ pre_TURING, ]
  STUB_RETURNS => NV_ERR_NOT_SUPPORTED
```

This matches the local runtime result where `NV2080_CTRL_CMD_GR_GET_SM_ISSUE_RATE_MODIFIER` returned unsupported on GV100.

For Turing, RM reads the FECS feature readout register and maps it directly to the public issue-rate enum:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/gr/turing/grtu102.c:1011-1028
regVal = GPU_REG_RD32(pGpu, NV_PGRAPH_PRI_FECS_FEATURE_READOUT_1)
pParams->fmla16 = REF_VAL(...SM_SPEED_SELECT_FMLA16, regVal)
```

For Ampere, RM reads the FUSE feature readout register instead:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/gr/ampere/grga100.c:1523-1543
fuseRead_HAL(... _FEATURE_READOUT_1 ...)
pParams->fmla16 = REF_VAL(...SM_SPEED_SELECT_FMLA16, regVal)
```

Both later implementations assert that the values exposed by the RM control match the hardware override enum values, including:

```text
FMLA16_REDUCED_SPEED_1_16
```

GV100 still has the older binary FECS speed-select fields:

```text
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_graphics_nobundle.h:5436-5444
NV_PGRAPH_PRI_FECS_FEATURE_READOUT_SM_SPEED_SELECT_FMLA
  FULL_SPEED
  REDUCED_SPEED

integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_graphics_nobundle.h:5453-5472
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
  FMLA
  FMLA_OVERRIDE
```

Interpretation: for GV100, the public RM source exposes the existence of FECS speed-select state but deliberately does not provide the newer public readback control. The exact mapping from GV100's binary `FMLA_REDUCED_SPEED` state to the observed `1/16` divisor is therefore likely resolved below the public GV100 RM HAL path, in signed FECS/GR firmware policy or private RM metadata.

### 9. MODS Does Not Implement The Issue-Rate Override Path For GV100

MODS has a generic `OverrideIssueRate` wrapper, but the base implementation reports unsupported:

```text
integ/gpu_drv/stage_rel/diag/mods/gpu/gpusbdev.cpp:8317-8339
CheckIssueRateOverride()
GetIssueRateOverride()
IssueRateOverride()
```

The issue-rate override/readback methods are implemented for Turing and Ampere:

```text
integ/gpu_drv/stage_rel/diag/mods/gpu/turinggpu.cpp:1531-2115
integ/gpu_drv/stage_rel/diag/mods/gpu/amperegpu.cpp:4569-5110
```

The Turing/Ampere code names this feature `Volta-953`, but the GV100/Volta subclasses do not implement the override methods:

```text
integ/gpu_drv/stage_rel/diag/mods/gpu/gv100gpu.cpp
integ/gpu_drv/stage_rel/diag/mods/gpu/voltagpu.cpp
```

Interpretation: there is no obvious MODS CLI/software path in this tree that can override GV100 issue-rate state. The feature lineage may have started in Volta, but the available source wires practical multi-rate read/override support into Turing/Ampere, not GV100.

### 10. Later Fuse Models Make The GV100 Boundary Clearer

A producer-side search did not find GV100 FECS firmware source that derives the binary `FMLA_REDUCED_SPEED` state. The Volta FECS artifacts present in the tree are mostly headers/bootloader blobs/objdump-style files, not the policy source that decides the value.

The same search did find newer `g00x` fuse definitions with direct 3-bit speed selectors:

```text
dev/gpu_drv/stage_rel/drivers/common/inc/hwref/g00x/g000/dev_fuse.h

NV_FUSE_OPT_SM_SPEED_SELECT_FMLA16
NV_FUSE_FEATURE_READOUT_1_SM_SPEED_SELECT_FMLA16
NV_FUSE_FEATURE_OVERRIDE_SM_SPEED_SELECT_FMLA16
NV_FUSE_FEATURE_OVERRIDE_SM_SPEED_SELECT_FMLA16_REDUCED_SPEED_1_16 = 0x4
```

That reinforces the generation split:

```text
GV100:
  FECS/PGRAPH feature readout exposes FMLA full/reduced as a binary state.
  Public RM issue-rate readback is stubbed for pre-Turing.

Turing/Ampere/later:
  RM readback is implemented.
  The exposed enum includes explicit 1/2, 1/4, 1/8, 1/16, 1/32 selectors.
  Later fuse models carry direct 3-bit SM speed-select fields.
```

Interpretation: the local `1/16` cap lines up with the later explicit enum value, but on GV100 the public source stops at a binary reduced-state boundary. The exact reduced-state divisor is likely encoded in private FECS/GR firmware or hidden RM metadata rather than in the public GV100 `dev_fuse.h` register dictionary.

### 11. The Policy Hooks Are In The Code

The source tree does contain explicit issue-rate / scheduler policy hooks. The most direct normal-driver hook is:

```text
dev/gpu_drv/stage_rel/drivers/resman/interface/nvRmReg.h
integ/gpu_drv/stage_rel/drivers/resman/interface/nvRmReg.h

NV_REG_STR_RM_SM_OVERRIDE_SPEED_SELECT   "RMOverrideSmSpeedSelect"
NV_REG_STR_RM_SM_OVERRIDE_SPEED_SELECT_1 "RMOverrideSmSpeedSelect1"
```

The comments say these allow setting `SM_SPEED_SELECT/1` through regkeys, but only in verification builds:

```text
Allow setting the SM_SPEED_SELECT/1 through regkeys. This takes effect
if verif builds only
```

Common GR init reads those values:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/gr/nv/objgr.c
  3175: osReadRegistryDword(... NV_REG_STR_RM_SM_OVERRIDE_SPEED_SELECT ...)
  3178: thisGr->smOverrideSpeedSelect = data

integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/gr/graphics.c
  2596: osReadRegistryDword(... NV_REG_STR_RM_SM_OVERRIDE_SPEED_SELECT ...)
  2599: pGr->smOverrideSpeedSelect = data
```

The implementation that actually writes the override is wired for Turing and Ampere:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/gr/turing/grcxtu102.c
  352: GPU_REG_WR32(... NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT ...)
  394: grFeatureOverrideSmSpeedSelect_HAL(... &pGr->smOverrideSpeedSelect ...)

dev/gpu_drv/stage_rel/drivers/resman/kernel/gr/ampere/grga100.c
  616: GPU_REG_WR32(... NV_FUSE_FEATURE_OVERRIDE_SM_SPEED_SELECT ...)
```

There is a second scheduler-level policy hook:

```text
NV_REG_STR_RM_SCH_MICRO_SCHED "RMSchMicroSched"

NV_PTPC_PRI_SM_SCH_MICRO_SCHED_FUSE_SLOWDOWN_JITTER_HMMA16
NV_PTPC_PRI_SM_SCH_MICRO_SCHED_FUSE_SLOWDOWN_JITTER_HMMA32
NV_PTPC_PRI_SM_SCH_MICRO_SCHED_FUSE_SLOWDOWN_JITTER_HMMAE8M10
```

The Turing apply path masks and writes those slowdown/jitter fields only for internal SKUs:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/gr/turing/grcxtu102.c
  411: if (gpuIsInternalSku_HAL(pGpu) && ... SLOWDOWN_JITTER_CONTROL_OVERRIDE)
  416: mask includes ..._FUSE_SLOWDOWN_JITTER_HMMA16
  430: GPU_REG_WR32(... NV_PGRAPH_PRI_GPCS_TPCS_SM_SCH_MICRO_SCHED ...)
```

Interpretation: the codebase absolutely knows how to express "slow Tensor/HMMA issue" as a scheduler policy. What it does not expose is a normal GV100 production path for overriding or reading the exact divisor. The public RM HAL still gates issue-rate readback/override to Turing and later, while GV100 only has the older FECS/CTXSW binary `FMLA_REDUCED_SPEED` state in the headers.

### 12. CMP SKU Is Also In Code, But It Is Not The Tensor Limiter By Itself

The source also has a real CMP SKU path:

```text
integ/gpu_drv/stage_rel/drivers/resman/inc/kernel/gpu/gpu.h
  3296: Detect if chip is a CMP (Crypto Mining Processor) SKU
  3299: PASCAL... : _GV100

integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/arch/volta/gpu_gv100.c
  866: gpuGetIsCmpSku_GV100
```

For GV100, CMP detection is based on two fuse options:

```text
HAL_FUSE_OPT_PCIE_BOOT_GEN3_DISABLE
HAL_FUSE_OPT_PCIE_BOOT_GEN23_DISABLE
```

Both must read as fused/disabled for `gpuGetIsCmpSku_GV100()` to return `NV_TRUE`.

RM exports that state to internal chip info and public GPU info:

```text
subdevice_ctrl_gpu.c
  660: pParams->isCmpSku = gpuGetIsCmpSku_HAL(pGpu)

subdevice_ctrl_gpu_kernel.c
  947: case NV2080_CTRL_GPU_INFO_INDEX_CMP_SKU
  949: if (gpuGetChipInfo(pGpu) && gpuGetChipInfo(pGpu)->isCmpSku)
```

CUDA/devtools consume it:

```text
tools_device.c
  425: CU_TOOLS_DEVICE_ATTRIBUTE_IS_CMP_SKU
  947: if (device->state.isCmpSku) supportsDebugging = 0

cudbgdriver.c
  2336: if (dev->state.isCmpSku) isDebuggable = false

check_mc_context.c
  779: if (... isCmpSku) return CUDA_ERROR_NOT_SUPPORTED
```

Interpretation: the CMP policy is visible in source, but the visible uses found so far are reporting and devtool/debug restrictions. The exact `1/16` HMMA/Tensor limiter is better explained by the separate GR scheduler/SM-speed-select policy path, likely fed by hidden fuse/firmware state below the public GV100 HAL boundary.

### 13. Engineering Notes Confirm The 1-Bit To 3-Bit Speed-Select Transition

A follow-up search for the internal breadcrumbs around `SM_SPEED_SELECT` found two useful engineering-note entries:

```text
integ/gpu_drv/stage_rel/diag/mods/docs/engnotes_willow.txt

1999526:
Submitting a manuals change to add the REDUCED_SPEED defines for the new
SM_SPEED_SELECT feature override registers.

1950344,1989900,200349705,200349708:
Submitting change to add new 3 bit SM_SPEED_SELECT fuses and remove the
older 1 bit fuses (except SM_SPEED_SELECT_DP).
```

This is a strong fit for the source boundary:

```text
Older/Volta-style model:
  FMLA speed select is a 1-bit full/reduced state.

Newer Turing/Ampere-style model:
  FMLA16 speed select is a 3-bit value with explicit divisors.
```

The same search also found a firmware-side example on a later Tegra/ACR path:

```text
uproc/tegra_acr/riscv/security/t23x/acr_rv_sect234.c

acrEmulateMode_T234()
  reads NV_FUSE_FEATURE_OVERRIDE_SM_SPEED_SELECT
  sets IMLA0/FMLA16/FMLA32/... to REDUCED_SPEED_1_2
  sets the matching *_OVERRIDE_TRUE bits
  writes NV_FUSE_FEATURE_OVERRIDE_SM_SPEED_SELECT
```

Interpretation: the exact policy class is real at both RM and firmware levels. The missing GV100 piece is not whether NVIDIA has speed-select policy code; it is where the GV100 reduced bit is resolved into the actual hardware divisor. The visible code implies that later chips moved that selector into explicit 3-bit fuse/readout fields, while GV100-era material exposes only the older reduced/full abstraction.

### 14. Exact Visible Location Of The Limiter State

The narrowest exact location visible in this source tree is the GV100 FECS/CTXSW SM speed-select state:

```text
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_graphics_nobundle.h:5436-5444

NV_PGRAPH_PRI_FECS_FEATURE_READOUT_SM_SPEED_SELECT_DP     bit 20
NV_PGRAPH_PRI_FECS_FEATURE_READOUT_SM_SPEED_SELECT_IMLA   bit 21
NV_PGRAPH_PRI_FECS_FEATURE_READOUT_SM_SPEED_SELECT_FMLA   bit 22

FMLA_FULL_SPEED    = 0
FMLA_REDUCED_SPEED = 1
```

The matching GV100 override register is:

```text
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_graphics_nobundle.h:5453-5472

NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT = 0x00409664
  IMLA bit 0,  override bit 3
  FMLA bit 4,  override bit 7
  DP   bit 8,  override bit 11
```

The CTXSW firmware view exposes the same state in FECS firmware space:

```text
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_ctxsw_firmware.h:3173-3181
NV_CTXSW_FIRMWARE_FEATURE_READOUT_SM_SPEED_SELECT_{DP,IMLA,FMLA}

integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_ctxsw_firmware.h:3190-3209
NV_CTXSW_FIRMWARE_FEATURE_OVERRIDE_SM_SPEED_SELECT = 0x00019900
```

The exact production code path that maps that GV100 `FMLA_REDUCED_SPEED` bit to the measured `1/16` divisor is not present as a named GV100 C implementation in this tree. The evidence is negative but strong:

```text
dev/gpu_drv/stage_rel/drivers/resman/config/haldefs/gr.def:4233-4241
FEATURE_OVERRIDE_SM_SPEED_SELECT
  _TU102 => TURING
  _GA100 => AMPERE_and_later
  _STUB  => pre_TURING

dev/gpu_drv/stage_rel/drivers/resman/config/haldefs/gr.def:7500-7509
GET_SM_ISSUE_RATE_MODIFIER
  _TU102 => TURING
  _GA100 => AMPERE_and_later
  _STUB  => pre_TURING, returns NV_ERR_NOT_SUPPORTED
```

By contrast, the visible Turing and Ampere implementations show exactly how the later public path works:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/gr/turing/grtu102.c:1018-1028
reads NV_PGRAPH_PRI_FECS_FEATURE_READOUT_1
extracts ...SM_SPEED_SELECT_FMLA16

dev/gpu_drv/stage_rel/drivers/resman/kernel/gr/ampere/grga100.c:1533-1543
reads NV_FUSE_FEATURE_READOUT_1
extracts ...SM_SPEED_SELECT_FMLA16

dev/gpu_drv/stage_rel/drivers/resman/kernel/gr/ampere/grga100.c:1569-1578
asserts public RM enum values match hardware values, including FMLA16_REDUCED_SPEED_1_16
```

The normal-driver hooks that would apply overrides are also visible, but again not wired for GV100:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/gr/nv/objgr.c:3175-3185
reads RMOverrideSmSpeedSelect / RMOverrideSmSpeedSelect1

dev/gpu_drv/stage_rel/drivers/resman/kernel/gr/turing/grcxtu102.c:341-358
writes NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT

dev/gpu_drv/stage_rel/drivers/resman/kernel/gr/ampere/grga100.c:605-622
writes NV_FUSE_FEATURE_OVERRIDE_SM_SPEED_SELECT
```

Interpretation: the limiter's exact visible register-level location on GV100 is the FECS/CTXSW `SM_SPEED_SELECT_FMLA` reduced bit. The missing hidden piece is not the register; it is the producer/policy that decides to set `FMLA_REDUCED_SPEED` and the private GV100 definition of what reduced means electrically or microarchitecturally. In this tree, that producer is outside the visible GV100 RM C path, likely in signed FECS/GR firmware, generated chipfuse/IFF inputs not present locally, or private RM metadata.

### 15. GV100 Context-State Store Carries The Override Register

The generated GV100 netlist path is NETD:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/inc/ctxsw_ucode/volta/gv100/net/netlists.h:25-29
GV100_NETLIST_D 24

dev/gpu_drv/stage_rel/drivers/resman/kernel/inc/ctxsw_ucode/volta/gv100/net/netlists.h:65-69
GV100_NETLIST_D_DEV_GRAPHICS    "volta/gv100/net/NETD_dev_graphics_nobundle.h"
GV100_NETLIST_D_CTX_STATE       "volta/gv100/net/NETD_ctx_state_store.h"
GV100_NETLIST_D_FECS_UCODE_IMG  "volta/gv100/net/NETD_fecs_ucode_img.h"
```

That NETD generated header confirms the same FECS registers:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/inc/ctxsw_ucode/volta/gv100/net/NETD_dev_graphics_nobundle.h:392-395

NET24_NV_PGRAPH_PRI_FECS_FEATURE_READOUT                  0x00409660
NET24_NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT 0x00409664
```

Most importantly, the NETD context-state store includes the speed-select override register in the delayed-reset-value list:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/inc/ctxsw_ucode/volta/gv100/net/NETD_ctx_state_store.h:20240-20248

NET24_LIST_REGS_WITH_IB_DLY_RESET_VALUE(...)
  NET24_NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT

integ/gpu_drv/stage_rel/drivers/resman/kernel/inc/ctxsw_ucode/volta/gv100/net/NETD_ctx_state_store.h:20343-20350
same inclusion in the integ tree
```

Interpretation: this is a stronger location than the earlier plain hwref header. It proves the GV100 FECS speed-select override register is not just a manual artifact; it is part of the generated GV100 FECS context-state surface. It still does not identify the production policy producer. The visible C implementation that writes speed-select override remains Turing/Ampere-only by HAL dispatch.

### 16. IFF/Chipfuse Is Still The Most Likely Missing Producer

MODS has explicit IFF support:

```text
dev/gpu_drv/stage_rel/diag/mods/gpu/fuse/gm20x_fuse.cpp
  ApplyIffPatches(...)

dev/gpu_drv/stage_rel/diag/mods/gpu/fuse/gm10x_fuse.cpp
  DecodeIff()
  DecodeSkuIff(...)
  DecodeIffRecords(...)

dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp
  ParseIFFPatches(...)
  IFF_patches section in <chip>_f.json
```

The relevant chipfuse inputs are still absent locally. `fdfind` found GV100 MODS fuse parser code:

```text
diag/mods/gpu/fuse/gv100_fuse.cpp
diag/mods/gpu/fuse/gv100_fuse.h
```

but did not find the expected GV100 chipfuse data file:

```text
gv100_f.json
gv100_f.xml
```

Interpretation: if there is a "before boot" data-source path that sets or preserves the FECS speed-select override/readout, the most plausible visible-format artifact would be a GV100 SKU/IFF row in the missing chipfuse database. The generated ctx-state store proves the register is in the GV100 state surface; the missing chipfuse/IFF source would tell whether a SKU row writes it.

### 17. `gv100_f.json/jsone` Is A Confirmed Runtime Input Path

The missing GV100 chipfuse file is not just a build-list guess. MODS maps GV100 directly to:

```text
dev/gpu_drv/stage_rel/diag/mods/gpu/fuse/fuseutils.cpp:43
Device::NvDeviceId::GV100 -> "gv100_f.json"
```

The branch package lists the same file:

```text
dev/gpu_drv/stage_rel/diag/mods/modular_branch.inc:98
basic_release_files$(ONLY_DGPU) += $(CHIP_XML_DIR)/xml_v1/gv100_f.json

dev/gpu_drv/stage_rel/diag/mods/modular_branch.inc:119
basic_release_files$(ONLY_DGPU) += $(SHARED_MODS_FILES_DIR)/por_data/gv100_POR.json
```

The encrypted release form is also explicit:

```text
dev/gpu_drv/stage_rel/diag/mods/tools/modsbld_stealth_end_user.sh:98
gv100_f.jsone

dev/gpu_drv/stage_rel/diag/mods/tools/modsbld_stealth_end_user.sh:116
gv100_f.jsone
```

The build rule says `.jsone` is encrypted JSON:

```text
dev/gpu_drv/stage_rel/diag/mods/makerule.inc:212-222
Rule to build .jsone (encrypted json) files
$(o_dir)/%.jsone: %.json encrypt
```

At runtime, the JSON parser checks for both clear and encrypted forms and decrypts if needed:

```text
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:48-83
ParseFileImpl(...)
  FindUnredactedFile(...)
  ParseJson(...)

dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:85-106
GV100+ POR sidecar support:
  <chip>_POR.json

dev/gpu_drv/stage_rel/diag/mods/core/utility/utility.cpp:3736-3794
ReadPossiblyEncryptedFile(...)
  FindPkgFile(...)
  Decryptor::DecryptFile(...)
```

The IFF parser then reads the `IFF_patches` section:

```text
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:951-1015
ParseIFFPatches(...)
```

Interpretation: `gv100_f.json` / `gv100_f.jsone` is a confirmed runtime data-source path for GV100 SKU and IFF policy, with `gv100_POR.json` as a confirmed GV100+ POR sidecar. The local tree references and can consume these artifacts, but the artifacts themselves are absent under both `/home/josh/containers/temp/nvdrv` and `/srv/nvme-data`. If the limiter is selected before normal driver operation by visible-format SKU data, this is now the highest-value missing file set.

### 18. Exact Chipfuse Sections To Inspect When The File Is Found

The loader/parser path establishes the exact JSON sections that matter.

`Mkt_options` is the main SKU-to-fuse map:

```text
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:416-490
ParseMktOptions(...)
  iterates each SKU
  each SKU entry maps fuse name -> string value
  fuse names must already exist in the FuseDefMap
```

`IFF_patches` is the SKU-specific IFF row list:

```text
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:951-1023
ParseIFFPatches(...)
  iterates each SKU
  each patch has:
    sequence
    rows[]
      value
  appends records to SkuConfig::iffPatches
```

The parsed SKU structure carries both direct fuse values and IFF patches:

```text
dev/gpu_drv/stage_rel/diag/mods/core/include/fuseutil.h:146
IFFInfo

dev/gpu_drv/stage_rel/diag/mods/core/include/fuseutil.h:220-228
SkuConfig
  FuseList
  iffPatches
```

`Por_data` is also SKU-keyed and only applies to SKUs present in `Mkt_options`:

```text
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:1093-1137
ParsePorData(...)
  Por_data[SKU]["Bin_POR::CVB"]

dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:1219-1260
InterpretPorJson(...)
  Por_data[SKU]["por_properties"]
```

The file lookup path is:

```text
dev/gpu_drv/stage_rel/diag/mods/core/utility/utility.cpp:3670-3704
FindPkgFile(...)
  look for clear filename
  look for encrypted filename

dev/gpu_drv/stage_rel/diag/mods/core/utility/utility.cpp:3736-3794
ReadPossiblyEncryptedFile(...)
  open selected file
  decrypt if encrypted
```

Interpretation: when `gv100_f.json` or `gv100_f.jsone` is acquired, the exact inspection targets are:

```text
Mkt_options:
  GV100 SKU names and fuse values

IFF_patches:
  any SKU row sequence whose data decodes to FECS/CTXSW/feature-override policy

Por_data / gv100_POR.json:
  SKU-linked POR binning data that may distinguish capped board classes
```

This pass still does not prove that chipfuse/IFF sets `SM_SPEED_SELECT_FMLA`; it proves where such a visible-format producer would be represented if the local tree is only missing the runtime data file.

### 19. Local Encrypted Artifact Search Was Negative

A direct filesystem search under:

```text
/srv/nvme-data/containers
/home/josh/containers/temp/nvdrv
```

found zero MODS-style encrypted files with these extensions:

```text
*.jsone
*.xme
*.xmle
*.jse
*.dbe
*.spe
*.he
```

Targeted searches for:

```text
gv100_f.json
gv100_f.jsone
gv100_POR.json
gv100_POR.jsone
```

also found no actual data artifact. Matches were limited to source references, build rules, package allow-lists, generated headers, and GV100 fuse parser source.

An archive member-name scan across local `nvdrv` package archives also found no members matching:

```text
gv100_f
gv100_POR
*.jsone
*.xme
```

Interpretation: in the searched local workspace, the encrypted chipfuse/POR artifacts are not present as standalone files or archive members. The source tree references and can load them, but the actual `gv100_f.*` / `gv100_POR.*` data files appear to be omitted from this checkout/package.

### 20. Fresh ROM Backtracking Points Away From A Decoded VBIOS Limiter Table

Fresh PROM dumps from a CMP card and a V100-personality card were compared against archived/local baselines:

```text
projects/gpu-falcon-research/runs/20260520-bootrom-dump/card0-cmp-05.rom
projects/gpu-falcon-research/runs/20260520-bootrom-dump/card4-v100-0b.rom
```

Same-version comparisons were effectively identical for decoded table analysis:

```text
fresh local V100 vs TechPowerUp-derived V100:
  5 byte differences in the compared prefix
  all in OBD ASCII/serial-like metadata

fresh CMP vs previous CMP dump:
  8 byte differences
  all in OBD ASCII/serial-like metadata
```

The CMP-vs-V100 decoded `BIT M` table deltas remain small and local:

```text
M_RESTRICT:
  +0x014: e2 -> 07
  +0x015: 55 -> 56

M_TYPE:
  +0x005: 46 -> 6b

M_UNK0D:
  +0x0ed: 14 -> 39
```

The `M_TYPE +0x005` delta is not a RAM-type entry in the known parser. Envytools parses `M_TYPE` as:

```text
tools/envytools/nvbios/mem.c:293-333
  version, header length, record length, entry count
  entries begin at mt->offset + mt->hlen

tools/envytools/nvbios/nvbios.c:966-984
  mem_type() uses bios->data[start] & 0x0f
```

For these ROMs the table is version `0x10`, header length `0x07`, record length `0x08`, entries `0x10`; therefore `+0x005` is in the header/reserved region, not in an HBM2 entry payload.

The `M_RESTRICT` path also backtracks to memory initialization/restrict opcodes, not GR/SM speed select:

```text
tools/envytools/nvbios/nvbios.c:597-605
  RAM_RESTRICT_PLL

tools/envytools/nvbios/nvbios.c:648-662
  RAM_RESTRICT_ZM_REG_GROUP
```

Interpretation: the fresh ROM evidence strengthens the split. Visible VBIOS differences explain board/personality, memory metadata, and OBD identity differences; they do not expose a decoded `SM_SPEED_SELECT`, `FMLA`, `HMMA`, Tensor issue-rate, or FECS feature-override table.

### 21. HMMA Devinit Exists, But The Visible Source Places It In Later NAFLL/PMU Paths

The broader backtrack did find another explicit HMMA slowdown hook, but it is in Ampere/Turing NAFLL/PMU clock restoration, not the GV100 decoded VBIOS table path:

```text
integ/gpu_drv/stage_rel/pmu_sw/prod_app/clk/ampere/pmu_clkavfsga10x.c:139-141
  Cache the Slowdown/HMMA devinit settings
  NV_PTRIM_GPC_BCAST_AVFS_QUICK_SLOWDOWN_CTRL

integ/gpu_drv/stage_rel/pmu_sw/prod_app/clk/ampere/pmu_clkavfsga10x.c:310-325
  Program extended settings
  Restore the Slowdown/HMMA devinit settings
```

This matches the already-found `RMSchMicroSched` and later `SM_SPEED_SELECT` implementation family: NVIDIA has explicit code paths for Tensor/HMMA slowdown policy, but the visible implementation is generation-specific and not surfaced as a normal GV100 VBIOS table edit.

The MODS RegHal surface also shows the generation split:

```text
integ/gpu_drv/stage_rel/diag/mods/gpu/reghal/reghal.def:7314-7406
  NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
  FMLA16 REDUCED_SPEED_1_16

integ/gpu_drv/stage_rel/diag/mods/gpu/reghal/reghal.def:7425-7431
  NV_PGRAPH_PRI_FECS_FEATURE_READOUT_1
  SM_SPEED_SELECT_FMLA16 FULL_SPEED / REDUCED_SPEED
```

Interpretation: the exact `1/16` value is visible in later RegHal/RM policy surfaces, while GV100-era readout remains binary reduced/full. The producer still backtracks toward missing chipfuse/IFF/POR data or signed FECS/GR firmware policy, not toward the small visible ROM table deltas.

### 22. CodeSPELUNKER Ranked Search Confirms The Same Boundary

`CodeSPELUNKER` (`/home/josh/.local/bin/cs`, version 3.1.0) was run as a ranked code-aware search over the local `nvdrv` tree. The useful searches were:

```text
SM_SPEED_SELECT FMLA16
gv100_f OR IFF_patches OR Mkt_options OR Por_data
HMMA devinit slowdown
FEATURE_OVERRIDE_SM_SPEED_SELECT GV100
gpuGetIsCmpSku GV100 fuse
```

The highest-ranked `SM_SPEED_SELECT FMLA16` results were the same later-generation implementation path already found by direct `rg`:

```text
diag/mods/gpu/turinggpu.cpp
  GetIssueRate reads PGRAPH FECS speed-select and maps FMLA16 divisors.

diag/mods/gpu/amperegpu.cpp
diag/mods/gpu/adagpu.cpp
diag/mods/gpu/hoppergpu.cpp
diag/mods/gpu/blackwellgpu.cpp
  GetIssueRate reads FUSE speed-select and maps FMLA16 divisors.

drivers/resman/kernel/gr/turing/grtu102.c
drivers/resman/kernel/gr/ampere/grga100.c
  RM readback maps hardware values to public issue-rate enums.
```

The highest-ranked GV100-specific results remained generated register surfaces and context-state surfaces, not a policy producer:

```text
drivers/common/inc/hwref/volta/gv100/dev_graphics_nobundle.h
drivers/common/inc/hwref/volta/gv100/dev_ctxsw_firmware.h
drivers/resman/kernel/inc/ctxsw_ucode/volta/gv100/net/NETD_dev_graphics_nobundle.h
```

The `gv100_f OR IFF_patches OR Mkt_options OR Por_data` search ranked the missing chipfuse/POR input path highest:

```text
diag/mods/core/utility/fusejsonparser.cpp
  IFF_patches rows are validated against Mkt_options.
  Por_data rows are validated against Mkt_options.

diag/mods/core/utility/fusexml_parser.cpp
  XML parser handles Iff_patches and Mkt_options.

diag/mods/gpu/fuse/fuseutils.cpp
  Device::NvDeviceId::GV100 -> "gv100_f.json"

diag/mods/modular_branch.inc
  package rules reference gv100_f.json and gv100_POR.json.
```

The `HMMA devinit slowdown` search added one more later-generation confirmation:

```text
integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/devinit/arch/t23x/vbiost234.c:506-528
  "HMMA slowdown programming"
  NV_PTRIM_GPC_BCAST_AVFS_QUICK_SLOWDOWN_CFG1
  NV_PTRIM_GPC_BCAST_AVFS_QUICK_SLOWDOWN_CFG2
  NV_PTRIM_GPC_BCAST_AVFS_QUICK_SLOWDOWN_CTRL

integ/gpu_drv/stage_rel/pmu_sw/prod_app/clk/nv/pmu_clklpwr.c:386-433
  Restore NAFLL HMMA settings.
  Restore devinit setting for NAFLL extended features.
  Restore slowdown settings before LUT programming.
```

CodeSPELUNKER did not surface a GV100 C implementation that derives or writes `FMLA_REDUCED_SPEED`. It strengthened the current boundary:

```text
Visible GV100 code:
  generated FECS/CTXSW speed-select register surfaces
  generated NETD context-state inclusion
  CMP fuse detection

Visible later-generation code:
  explicit FMLA16 divisors, including 1/16
  HMMA slowdown devinit/NAFLL restore paths

Missing locally:
  GV100 chipfuse/POR data artifacts
  GV100 FECS/GR firmware policy source
  a named GV100 RM C producer for SM_SPEED_SELECT_FMLA
```

### 23. `gv100_f.json` Is Packaged/Encrypted, Not Generated Locally

A targeted search for generation paths found three distinct pieces:

1. Source locations expected by the build.
2. Packaging/encryption rules.
3. Runtime loading/decryption/parsing.

The MODS Makefile defines the shared source locations:

```text
dev/gpu_drv/stage_rel/diag/mods/Makefile:302-305
SHARED_MODS_FILES_DIR ?= $(COMMON_WS_DIR)/sw/mods
CHIP_XML_DIR          ?= $(SHARED_MODS_FILES_DIR)/chipfuse
```

The Git build helper maps those to a separate chipfuse tree, not to generated files inside this checkout:

```text
dev/gpu_drv/stage_rel/diag/mods/tools/build_git.sh:70-75
SHARED_MODS_FILES_DIR="$P4ROOT/sw/mods"
CHIP_XML_DIR="$TEGRA_TOP/gpu/chipfuse/chipfuse"
```

The package rules list `gv100_f.json` and `gv100_POR.json` as release inputs:

```text
dev/gpu_drv/stage_rel/diag/mods/modular_branch.inc:88-99
$(CHIP_XML_DIR)/xml_v1/gv100_f.json

dev/gpu_drv/stage_rel/diag/mods/modular_branch.inc:116-120
CLIENTPATHS += //sw/mods/por_data/...
$(SHARED_MODS_FILES_DIR)/por_data/gv100_POR.json
```

The build can convert JSON input into encrypted package output:

```text
dev/gpu_drv/stage_rel/diag/mods/makerule.inc:212-219
$(o_dir)/%.jsone: %.json encrypt
  $(ENCRYPT_TOOL) ... -o$(o_dir) $<

dev/gpu_drv/stage_rel/diag/mods/Makefile:2902-2922
move_to_odir(..., json, jsone)
```

Internal packaging explicitly expects encrypted GV100 fuse files:

```text
dev/gpu_drv/stage_rel/diag/mods/tools/modsbld_stealth_end_user.sh:96-117
gv100_f.jsone
```

Runtime can load either the clear or encrypted form:

```text
dev/gpu_drv/stage_rel/diag/mods/core/utility/utility.cpp:3670-3704
FindPkgFile(...)
  find clear filename
  derive encrypted filename

dev/gpu_drv/stage_rel/diag/mods/core/utility/utility.cpp:3736-3783
ReadPossiblyEncryptedFile(...)
  decrypt if needed
```

`FuseJsonParser` then interprets `gv100_f.json`, optionally loads the matching POR sidecar, and parses the policy sections:

```text
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:48-107
  parse <chip>_f.json
  optionally parse <chip>_POR.json
  optionally parse <chip>_fpf.json

dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:951-1015
  ParseIFFPatches(...)

dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:1093-1125
  ParsePorData(...)
```

Interpretation: this tree shows how to package, encrypt, locate, decrypt, and parse `gv100_f.json` / `gv100_f.jsone`, but it does not contain a generator for the source `gv100_f.json`. The source artifact is expected to come from a separate shared MODS/chipfuse depot path:

```text
//sw/mods/chipfuse/xml_v1/gv100_f.json
//sw/mods/por_data/gv100_POR.json
```

### 24. Known Required Fields In `gv100_f.json` / `gv100_POR.json`

The parser gives a concrete schema for the missing files.

For the main `<chip>_f.json` file, these top-level sections are required:

```text
header
options
```

Source:

```text
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:158-195
```

The required `header` fields are:

```text
Perforce file revision
Generation time
Project Name
Status
Version
```

Source:

```text
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:235-270
```

The required `options` subsections are:

```text
Chip_options
Map_options
Mkt_options
Fuse_encoding
```

Optional but important `options` subsections are:

```text
Pseudo_fuses
Floorsweeping_options
Bootrom_patches
IFF_patches
Fuse_macro
Sku_ids
Slt_data
Por_data
```

For each `Chip_options` fuse definition, required fields are:

```text
bits
customer_visible
type
offset
```

`offset` can be either a string or an unsigned integer.

Source:

```text
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:275-337
```

For each non-fuseless/non-pseudo `Map_options` entry, required fields are:

```text
fuse_num: [
  { "<fuse-row-number>": "<msb>:<lsb>" }
]
```

Optional redundant locations use:

```text
redundant_fuse_num
```

Source:

```text
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:342-414
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:1560-1574
```

`Mkt_options` is keyed by SKU name. Each SKU entry contains fuse-name keys whose values are strings:

```text
Mkt_options: {
  "<SKU name>": {
    "<FUSE_NAME>": "<string value>"
  }
}
```

Every fuse named in a SKU must already exist in `Chip_options`.

Source:

```text
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:416-495
```

`Fuse_encoding` requires:

```text
ramRepairFuseBlockBegin
ramRepairFuseBlockEnd
configFields
fuselessFuse
```

Each `configFields` entry uses:

```text
blockIndex
blockOffset
chainId
lsbIndex
msbIndex
name
```

Each `fuselessFuse` entry uses:

```text
fuseName
jtagConfigFields
```

Source:

```text
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:497-527
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:732-818
```

`IFF_patches` is optional, but it is the highest-value section for this investigation. It is keyed by SKU and then patch name:

```text
IFF_patches: {
  "<SKU name>": {
    "<patch name>": {
      "sequence": <number>,
      "rows": [
        { "value": "<hex row value or ATE:>" }
      ]
    },
    "checksum": ...
  }
}
```

Every SKU in `IFF_patches` must exist in `Mkt_options`.

Source:

```text
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:951-1028
```

`Bootrom_patches` has a similar SKU/patch shape, but row entries contain address/value pairs:

```text
Bootrom_patches: {
  "<SKU name>": {
    "<patch name>": {
      "sequence": <number>,
      "rows": [
        {
          "address": <number>,
          "value": "<hex string>"
        }
      ]
    },
    "checksum": ...
  }
}
```

Source:

```text
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:882-949
```

`Fuse_macro` uses:

```text
num_fuse_rows
num_fuse_cols
fuse_record_start
```

`Sku_ids` maps SKU names from `Mkt_options` to numeric IDs.

Source:

```text
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:1030-1074
```

For the GV100+ sidecar `<chip>_POR.json`, top-level required fields are:

```text
id
Por_data
```

Each `Por_data` SKU can contain:

```text
por_properties: {
  "Bin_POR::Sub_Bins": [
    {
      "b": "<string>",
      "c": "<string>",
      "Min_Speedo": <number>,
      "Max_Speedo": <number>,
      "Max_IDDQ": <number>,
      "Min_SRAM_Bin": <number>
    }
  ]
}
```

Accepted POR bin names include:

```text
Bin_POR::SOC::Sub_Bins
Bin_POR::CPU::Sub_Bins
Bin_POR::GPU::Sub_Bins
Bin_POR::Sub_Bins
```

Source:

```text
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:1198-1325
dev/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp:1327-1345
```

Investigation target: if the limiter is represented in this file family, the most likely evidence is:

```text
Chip_options:
  a fuse definition whose name resembles SM_SPEED_SELECT, FMLA, HMMA, issue-rate, scheduler, or feature override

Mkt_options:
  CMP/V100 SKU rows with different values for that fuse

IFF_patches:
  SKU-specific rows that encode a policy patch touching FECS/CTXSW feature state

Por_data:
  SKU/bin distinctions that explain why the same visible VBIOS personality still receives different policy
```

### 25. What Can Be Deduced Without `gv100_f.json`

The local source and ROM data can reconstruct the limiter-relevant slice of the missing chipfuse data, but not the complete market table.

High-confidence `Chip_options`-style entries can be inferred from `dev_fuse.h`:

```text
NV_FUSE_OPT_DP_SPEED_SELECT
  offset: 0x00021224
  bits: 1
  data: 0:0
  values: DISABLE/FULL-style 0, ENABLE/REDUCED-style 1
  raw aliases:
    primary row 0x06 bit 6
    redundant row 0x07 bit 6
    control primary row 0x06 bit 7
    control redundant row 0x07 bit 7

NV_FUSE_OPT_SM_IMLA_SPEED_SELECT
  offset: 0x00021410
  bits: 1
  data: 0:0
  values: DISABLE/FULL-style 0, ENABLE/REDUCED-style 1
  raw aliases:
    primary row 0x10 bit 17
    redundant row 0x11 bit 17
    control primary row 0x10 bit 18
    control redundant row 0x11 bit 18

NV_FUSE_OPT_SM_FMLA_SPEED_SELECT
  offset: 0x000214e0
  bits: 1
  data: 0:0
  values: DISABLE/FULL-style 0, ENABLE/REDUCED-style 1
  raw aliases:
    primary row 0x12 bit 10
    redundant row 0x13 bit 10
    control primary row 0x12 bit 11
    control redundant row 0x13 bit 11

NV_FUSE_OPT_SHF_SPEED_SELECT
  offset: 0x0002159c
  bits: 1
  data: 0:0
  values: DISABLE/FULL-style 0, ENABLE/REDUCED-style 1
  raw aliases:
    primary row 0x16 bit 10
    redundant row 0x17 bit 10
    control primary row 0x16 bit 11
    control redundant row 0x17 bit 11
```

Those entries give a plausible `Map_options` reconstruction for the speed-select fuses: the row/bit data above is exactly the sort of information `FuseJsonParser` expects through `fuse_num` and optional `redundant_fuse_num`.

The source also identifies the CMP classifier fuses:

```text
NV_FUSE_OPT_PCIE_BOOT_GEN23_DISABLE
  offset: 0x0002157c
  data: 0:0

NV_FUSE_OPT_PCIE_BOOT_GEN3_DISABLE
  offset: 0x00021580
  data: 0:0
```

GV100 RM returns `isCmpSku = true` only when both classifier fuses read as fused/disabled:

```text
gpuGetIsCmpSku_GV100()
  HAL_FUSE_OPT_PCIE_BOOT_GEN3_DISABLE  != 0
  HAL_FUSE_OPT_PCIE_BOOT_GEN23_DISABLE != 0
```

What can be deduced:

```text
1. The speed-select fuse names.
2. Their register offsets.
3. Their widths.
4. Their raw primary/redundant row-bit aliases for DP/IMLA/FMLA/SHF speed select.
5. The FECS/CTXSW readout fields that consume the speed-select state.
6. The CMP classifier fuse names and register offsets.
7. The VBIOS-visible SKU/personality labels: PG500 SKU 110 for CMP and PG500 SKU 111 for V100.
```

What cannot be deduced from the current files alone:

```text
1. The exact `Mkt_options` values NVIDIA assigned to PG500 SKU 110 vs SKU 111.
2. Whether `Mkt_options` directly sets `NV_FUSE_OPT_SM_FMLA_SPEED_SELECT` for the CMP SKU.
3. Whether an `IFF_patches` row sets or redirects FECS/CTXSW speed-select state.
4. Any hidden SKU/bin properties in `gv100_POR.json`.
5. The private mapping from GV100 binary `FMLA_REDUCED_SPEED` to the observed exact `1/16` issue-rate divisor.
```

Interpretation: yes, the current data is enough to reconstruct the likely `Chip_options`/`Map_options` entries for the speed-select mechanism. It is not enough to recover the production `Mkt_options`, `IFF_patches`, or POR-sidecar values without either the missing chipfuse/POR artifacts or a read-only dump of the resolved fuse/IFF state from both a capped card and a full-speed V100.

### 26. How To Get The Missing Values

There are three useful acquisition routes.

#### Route A: Acquire the actual MODS chipfuse/POR artifacts

The source tree names the expected depot inputs:

```text
//sw/mods/chipfuse/xml_v1/gv100_f.json
//sw/mods/por_data/gv100_POR.json
```

Release packages may contain encrypted equivalents:

```text
gv100_f.jsone
gv100_POR.jsone
```

Useful places to search:

```text
MODS release tarballs/zips
factory diagnostic packages
board-validation packages
RMA/service bundles
old P4 workspace mirrors
build-worker caches
CI artifacts for MODS/basic release packaging
vendor/OEM debug or qualification bundles
```

If either clear JSON or packaged `.jsone` is found, the immediate inspection targets are:

```text
Mkt_options["PG500 SKU 110"]
Mkt_options["PG500 SKU 111"]
IFF_patches["PG500 SKU 110"]
IFF_patches["PG500 SKU 111"]
Por_data entries for the same SKUs
any Chip_options/Map_options entry around SM_FMLA_SPEED_SELECT, SM_SPEED_SELECT, FMLA, HMMA, or scheduler slowdown
```

#### Route B: Collect read-only resolved state from hardware

If the source artifact cannot be acquired, the next best source is resolved state from real cards:

```text
capped CMP-derived GV100 card
known full-speed V100 with the same generation/board family
```

The useful values are:

```text
resolved fuse options:
  NV_FUSE_OPT_SM_FMLA_SPEED_SELECT
  NV_FUSE_OPT_SM_IMLA_SPEED_SELECT
  NV_FUSE_OPT_DP_SPEED_SELECT
  NV_FUSE_OPT_SHF_SPEED_SELECT
  NV_FUSE_OPT_PCIE_BOOT_GEN3_DISABLE
  NV_FUSE_OPT_PCIE_BOOT_GEN23_DISABLE

resolved FECS/CTXSW state:
  NV_CTXSW_FIRMWARE_FEATURE_READOUT_SM_SPEED_SELECT_FMLA
  NV_CTXSW_FIRMWARE_FEATURE_READOUT_SM_SPEED_SELECT_IMLA
  NV_CTXSW_FIRMWARE_FEATURE_READOUT_SM_SPEED_SELECT_DP

board/SKU identity:
  PCI device/subdevice IDs
  VBIOS board string
  InfoROM OBD/OEM/PWR object metadata
```

This route does not recover the full `gv100_f.json`, but it can prove which reconstructed field actually differs between capped and full-speed hardware.

#### Route C: Infer policy from more matched controls

The current local CMP-vs-V100 VBIOS comparison did not expose a decoded Tensor limiter table. A stronger inference set would use:

```text
multiple capped PG500 SKU 110 cards
multiple full-speed PG500 SKU 111 or nearby V100 cards
same VBIOS branch where possible
matching debugdump / InfoROM / ROM / benchmark outputs
```

The key discriminator is whether all capped cards share:

```text
SM_FMLA_SPEED_SELECT reduced state
or CMP classifier fuses set
or an InfoROM/POR/SKU property absent from full-speed V100
```

Interpretation: the cleanest path is still Route A, because it directly supplies the missing `Mkt_options`, `IFF_patches`, and `Por_data`. Route B is the most practical fallback because it can identify the resolved field even without the production JSON. Route C is slower, but can separate VBIOS-visible board metadata from fuse/firmware policy.

### 27. Local Search For Full Silicon Schematics / RTL

A workspace search for full silicon design artifacts did not find actual GV100 schematics, RTL, gate netlists, or physical-design files.

Searched for names/extensions around:

```text
schematic
netlist
Verilog / SystemVerilog / VHDL
RTL
GDS
LEF/DEF
SPICE
EDIF
floorplan
Willow / GV100
```

The closest local hits are not full silicon schematics:

```text
drivers/resman/kernel/inc/ctxsw_ucode/volta/gv100/net/
```

This directory contains generated context-switch/firmware netlist artifacts:

```text
NETD_dev_graphics_nobundle.h
NETD_ctx_state_store.h
NETD_fecs_ucode_img.h
NETD_gpccs_ucode_img.h
NETD_img.bin
NETD_img.bin.gz
netlists.h
```

These are useful because they expose generated FECS/CTXSW register/state surfaces, including the speed-select state already identified. They are not transistor/gate-level GV100 silicon schematics.

Another close-sounding directory is:

```text
drivers/fsf/dvs/gv100/
```

That directory contains host/guest scripts for DVS/virtualization-style validation, not RTL or a chip schematic database.

Interpretation: locally available "netlist" material is generated firmware/context-state material, not the full silicon design. It helps us map the runtime FECS/CTXSW state, but it does not replace the missing chipfuse/POR data or reveal private SKU policy directly.

### 28. VBIOS Init Scripts Explicitly Touch The FECS SM Speed-Select Override

A broader local search found a stronger visible artifact than the missing chipfuse/POR files: decoded VBIOS init scripts write the GV100 FECS speed-select override register.

The target register is:

```text
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT = 0x00409664
```

GV100 field map:

```text
bit 0   IMLA speed select        0 full, 1 reduced
bit 3   IMLA override enable     0 false, 1 true
bit 4   FMLA speed select        0 full, 1 reduced
bit 7   FMLA override enable     0 false, 1 true
bit 8   DP speed select          0 full, 1 reduced
bit 11  DP override enable       0 false, 1 true
```

Local capped/CMP-derived ROMs contain:

```text
runs/20260520-bootrom-dump/card0-cmp-05-nvbios-vbu.txt:659
NV_REG R[0x409664] &= 0xfffff666 |= 0x00000999

runs/20260520-bootrom-dump/card4-v100-0b-nvbios-vbu.txt:657
NV_REG R[0x409664] &= 0xfffff666 |= 0x00000999

runs/20260519-173454-tensor-probe-gpu2-gpu14/online-vbios/266855-extracted-nvbios-selected.txt:657
NV_REG R[0x409664] &= 0xfffff666 |= 0x00000999
```

`0x999` decodes as:

```text
imla_reduced:  1
imla_override: 1
fmla_reduced:  1
fmla_override: 1
dp_reduced:    1
dp_override:   1
```

The mask `0xfffff666` clears exactly those speed-select and override bits before setting `0x999`.

Two Titan V control ROMs contain the same register touch with a no-op value:

```text
NVIDIA.TitanV.12288.180904-extracted-pcirom.txt:791
NV_REG R[0x409664] &= 0xffffffff |= 0x00000000

NVIDIA.TitanV.32768.180602-extracted-pcirom.txt:787
NV_REG R[0x409664] &= 0xffffffff |= 0x00000000
```

Interpretation: this is now the strongest local visible evidence. The VBIOS init script directly programs the FECS SM speed-select override surface and sets IMLA/FMLA/DP to reduced with override enabled on the local capped/CMP-derived images. Titan V controls leave the same register unchanged. This does not by itself prove the exact `1/16` divisor, because GV100's visible field remains binary reduced/full, but it does identify a concrete visible path that sets the FMLA reduced state before normal driver operation.

### 29. User-Space Vast.ai Read-Only Probe

Because a Vast.ai control generally provides user-space access only, a read-only collection wrapper was added:

```text
tools/vast_userspace_fecs_probe.sh
```

It collects:

```text
nvidia-smi -L
nvidia-smi -q
nvidia-debugdump --list, if available
nvidia-debugdump --dumpall, if available and permitted
read-only RM ioctl probe output for each /dev/nvidiaN
```

The RM probe source is:

```text
tools/rm_sm_issue_rate_probe.c
```

It now attempts read-only GR register access for the exact GV100 FECS speed-select registers:

```text
0x00409660  NV_PGRAPH_PRI_FECS_FEATURE_READOUT
0x00409664  NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
```

A local smoke run was saved under:

```text
runs/20260520-vast-userspace-fecs-read/local-smoke/
```

On the local driver, user-space RM object allocation succeeds, but the read-only GR controls return unsupported:

```text
get_sm_issue_rate_modifier status=0x00000056

gr_reg_read cmd=0x20801226 GV100 FECS FEATURE_READOUT
  offset=0x00409660 status=0x00000056

gr_reg_read cmd=0x20801226 GV100 FECS SM_SPEED_SELECT override
  offset=0x00409664 status=0x00000056

gr_reg_read cmd=0x20800a15 GV100 FECS FEATURE_READOUT
  offset=0x00409660 status=0x00000056

gr_reg_read cmd=0x20800a15 GV100 FECS SM_SPEED_SELECT override
  offset=0x00409664 status=0x00000056
```

Interpretation: the user-space package is ready for Vast.ai controls. If Vast's driver exposes the debugdump path or permits these RM controls, it should capture the live values. If it returns the same `0x56` unsupported statuses, then user-space-only access is insufficient for live FECS register readback and the comparison must rely on VBIOS init-table evidence plus benchmarks/debugdump data available to unprivileged users.

### 30. Live Vast.ai V100 User-Space Probe Result

A short-lived Vast.ai control was rented and destroyed after the probe:

```text
contract: 37119173
offer: 30970800
GPU: Tesla V100-SXM2-16GB
UUID: GPU-a1eab244-87d8-76fa-d4d0-d61e863309b6
PCI: 00000000:AF:00.0
minor: 3
VBIOS: 88.00.13.00.02
Board Part Number: 900-2G503-0100-000
GPU Part Number: 1DB1-895-A1
InfoROM Image: G503.0201.00.03
```

Artifacts:

```text
runs/20260520-vast-userspace-fecs-live/
runs/20260520-vast-userspace-fecs-live/vast-fecs-run.tar.gz
```

`nvidia-debugdump --list` could identify the GPU, but `--dumpall` failed due to insufficient permissions:

```text
ERROR: GetCaptureBuffer failed, Insufficient Permissions
ERROR: internal_getDumpBuffer failed, return code: 0x4
```

The RM probe found four physical GV100 devices on the host, but the container-exposed GPU mapped to minor `3` / board `0xaf00`. That is the only minor where RM object allocation completed.

Read-only RM controls still returned unsupported for the exact GV100 FECS speed-select registers:

```text
get_sm_issue_rate_modifier status=0x00000056

gr_reg_read cmd=0x20801226 GV100 FECS FEATURE_READOUT
  offset=0x00409660 status=0x00000056

gr_reg_read cmd=0x20801226 GV100 FECS SM_SPEED_SELECT override
  offset=0x00409664 status=0x00000056

gr_reg_read cmd=0x20800a15 GV100 FECS FEATURE_READOUT
  offset=0x00409660 status=0x00000056

gr_reg_read cmd=0x20800a15 GV100 FECS SM_SPEED_SELECT override
  offset=0x00409664 status=0x00000056
```

Interpretation: Vast.ai user-space access is not enough to read live `0x409660` / `0x409664` through the normal RM control paths on this full-speed V100. The live run supports the earlier source finding: GV100 production RM stubs the public issue-rate path and does not expose a user-space FECS register readback control here. The useful full-speed control evidence from Vast remains the identity data plus any benchmark comparison, not live FECS register capture.

### 31. Production User-Space Register Paths Reject The FECS Offsets

A follow-up code/probe pass checked whether there is another legitimate user-space RM path to get the live GV100 FECS speed-select values.

The internal static issue-rate controls exist in the headers:

```text
integ/gpu_drv/stage_rel/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080internal.h

NV2080_CTRL_CMD_INTERNAL_STATIC_KGR_GET_SM_ISSUE_RATE_MODIFIER = 0x20800a34
NV2080_CTRL_CMD_INTERNAL_STATIC_GR_GET_SM_ISSUE_RATE_MODIFIER  = 0x20800a35
```

But both still return unsupported on local GV100 cards:

```text
runs/20260520-static-issue-rate-controls/rm-probe-gpu0-exec-reg-ops.txt
runs/20260520-static-issue-rate-controls/rm-probe-gpu4-exec-reg-ops.txt

get_sm_issue_rate_modifier status=0x00000056
static_sm_issue_rate cmd=0x20800a34 INTERNAL_STATIC_KGR status=0x00000056
static_sm_issue_rate cmd=0x20800a35 INTERNAL_STATIC_GR  status=0x00000056
```

The generic register-read path is `NV2080_CTRL_CMD_GPU_EXEC_REG_OPS`:

```text
integ/gpu_drv/stage_rel/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080gpu.h

NV2080_CTRL_CMD_GPU_EXEC_REG_OPS = 0x20800122
NV2080_CTRL_GPU_REG_OP_STATUS_INVALID_OFFSET = 0x04
```

That path accepts the command, but rejects the exact FECS offsets per operation:

```text
exec_reg_ops_read GLOBAL 0x00409660 status=0x00000000 regStatus=0x04
exec_reg_ops_read GLOBAL 0x00409664 status=0x00000000 regStatus=0x04
exec_reg_ops_read GR_CTX 0x00409660 status=0x00000000 regStatus=0x04
exec_reg_ops_read GR_CTX 0x00409664 status=0x00000000 regStatus=0x04
exec_reg_ops_read DEVICE 0x00409660 status=0x00000000 regStatus=0x06
exec_reg_ops_read DEVICE 0x00409664 status=0x00000000 regStatus=0x06
```

Interpretation of those statuses:

```text
0x04 = INVALID_OFFSET
0x06 = INVALID_TYPE | INVALID_OFFSET
```

The source explains why this is deterministic rather than a timing/race problem. `EXEC_REG_OPS` validates offsets against the chip user-register access map:

```text
integ/gpu_drv/stage_rel/drivers/resman/src/kernel/gpu/subdevice/subdevice_ctrl_gpu_regops.c
  gpuValidateRegOffset(...)

integ/gpu_drv/stage_rel/drivers/resman/src/kernel/gpu/gpu_register_access_map.c
  gpuGetUserRegisterAccessPermissions_IMPL(...)
  tests pGpu->pUserRegisterAccessMap

integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/arch/volta/gpu_gv100.c
  gpuGetUserRegisterAccessMap_GV100(...)
  returns UserAccessMapCompr
```

So the normal production user-space path is:

```text
RM accepts EXEC_REG_OPS
  -> validates requested BAR0 offset against GV100 user access map
  -> rejects 0x409660 / 0x409664 as INVALID_OFFSET
```

The generated GV100 access-map files exist locally:

```text
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/user_access_map.bin
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/user_access_map.h
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/user_access_map_gzip.h
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/user_access_map_reglist.h
```

The source generator is:

```text
integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/Makefile:94
python gen_access_map.py build ... user_access_map.bin ... user_access_map_reglist.h

integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/gen_access_map.py:84-105
parse manuals -> filter allowed register names -> am.permit(name, value, 4)
```

The Makefile passes all default whitelist configs:

```text
integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/Makefile:10
CONFIGS=$(wildcard config/*.yml)

integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/config/common.yml
integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/config/cuda_tools.yml
integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/config/cuda_tools_discovery.yml
integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/config/tesla_debugger.yml
integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/config/tesla_profiler.yml
```

`gen_access_map.py` treats list-valued YAML entries as exact names or patterns for the current family:

```text
integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/gen_access_map.py:41-53
if family is in the YAML family list:
  f.add_pattern(name)

integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/accessmap/utils.py:148-188
exact names use set intersection
patterns with `$` use regex
other non-word patterns use fnmatch
```

The default configs include only these FECS entries:

```text
config/common.yml:15
NV_PGRAPH_PRI_FECS_CURRENT_CTX: [hopper, ada, ampere, turing, volta, pascal, maxwell, kepler]

config/cuda_tools.yml:65-67
NV_PGRAPH_PRI_FECS_FALCON_PMM: [hopper, ada, ampere, turing, volta, pascal, maxwell, kepler]
NV_PGRAPH_PRI_FECS_PERFMON: [hopper, ada, ampere, turing, volta, pascal, maxwell, kepler, fermi]
NV_PGRAPH_PRI_FECS_PRI_PMM: [kepler, fermi]
```

Targeted searches found no default whitelist entry for:

```text
NV_PGRAPH_PRI_FECS_FEATURE_READOUT
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
0x409660
0x409664
SM_SPEED_SELECT
```

Searching the generated GV100 user-access reglist for the FECS feature registers found only nearby FECS/profiling entries, not the speed-select feature registers:

```text
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/user_access_map_reglist.h:54806-54808
ACCESS_MAP_REGISTER(NV_PGRAPH_PRI_FECS_CURRENT_CTX)
ACCESS_MAP_REGISTER(NV_PGRAPH_PRI_FECS_FALCON_PMM)
ACCESS_MAP_REGISTER(NV_PGRAPH_PRI_FECS_PERFMON)

integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/user_access_map_pm_muxes_regranges.h:170-171
ACCESS_MAP_REGISTER_RANGE(0x004090A8, 0x004090A8)
ACCESS_MAP_REGISTER_RANGE(0x004098A0, 0x004098A0)
```

The target registers are present in the hardware manual, but absent from the generated user-access map:

```text
NV_PGRAPH_PRI_FECS_FEATURE_READOUT                       0x00409660
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT      0x00409664
```

The more direct GR register access control remains unavailable outside the diagnostic/MODS/SMC-gated path:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/gr/nv/objgr.c
integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/gr/graphics.c

NV2080_CTRL_CMD_GR_REG_ACCESS
  disabled outside MODS / SMC partitioning
  returns NV_ERR_NOT_SUPPORTED in production GV100 user-space
```

Conclusion: "spamming" the card through these RM controls will not reveal the FECS values. The rejection is an access-map decision made before the register read, not a transient lock. The remaining code-backed ways to get those live values are diagnostic/authorized read surfaces: a MODS or debug RM build with GR register access enabled, a debugdump environment with sufficient permissions, or a maintenance path that exposes the GV100 user access map/register list differently.

### 29. Host Evidence Narrows The Issue To FECS Speed-Select State

The local host now has enough data to stop treating this as a broad VBIOS mystery.

Read-only host checks showed:

```text
16 GV100 cards are visible through NVIDIA RM:
  CMP personality: 10de:1d84, VBIOS 88.00.9D.00.00
  V100 personality: 10de:1df4, VBIOS 88.00.51.00.04

/sys/bus/pci/devices/*/rom exists for the LSI controller and Quadro K620,
but not for the GV100 CMP/V100 devices.

nvidia-debugdump succeeds as root, but its zip contains RM/debug/log protobufs,
not raw VBIOS ROM bytes.

envytools nvagetbios can read GV100 PROM directly and already produced:
  runs/20260520-bootrom-dump/card0-cmp-05.rom
  runs/20260520-bootrom-dump/card4-v100-0b.rom
```

Those PROM dumps are useful for identity and table comparison, but they did not expose a clear Tensor issue-rate table. The strongest code-backed location remains:

```text
NV_PGRAPH_PRI_FECS_FEATURE_READOUT                      0x00409660
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT     0x00409664
NV_CTXSW_FIRMWARE_FEATURE_OVERRIDE_SM_SPEED_SELECT      0x00019900
```

For GV100, the relevant field is the binary FECS/CTXSW `SM_SPEED_SELECT_FMLA` state:

```text
FMLA_FULL_SPEED
FMLA_REDUCED_SPEED
FMLA_OVERRIDE
```

The exact divisor is not represented in GV100 public hwref as a 3-bit value. Later chips expose explicit values such as `FMLA16_REDUCED_SPEED_1_16`; GV100 exposes only the older reduced/full abstraction. Therefore the issue is:

```text
GV100 FECS/CTXSW FMLA reduced-speed state is asserted
  -> FECS/GR firmware or private RM metadata interprets reduced as the observed issue divisor
  -> production RM blocks direct user-space read/write of the FECS feature registers
```

This also explains the local `INVALID_OFFSET` behavior. `EXEC_REG_OPS` does not reach raw MMIO for `0x409660` / `0x409664`; RM rejects those offsets because the generated GV100 user-access map does not include the FECS speed-select feature registers.

### 30. Evidence DAG And Exact ROM Byte Anchor

An evidence DAG builder was added:

```text
tools/gv100_limiter_evidence_dag.py
```

It emits:

```text
runs/20260520-evidence-dag/gv100-limiter-evidence-dag.md
runs/20260520-evidence-dag/gv100-limiter-evidence-dag.json
runs/20260520-evidence-dag/gv100-limiter-evidence-dag.dot
runs/20260520-evidence-dag/gv100-vbios-main-init-tail-compare.md
```

The useful new result is that the decoded VBIOS init write is not only a textual parser artifact. The exact opcode bytes are present in the raw ROMs:

```text
6e 64 96 40 00 66 f6 ff ff 99 09 00 00
```

This decodes as:

```text
NV_REG R[0x409664] &= 0xfffff666 |= 0x00000999
```

Pattern scan results:

```text
card0-cmp-05.rom
  set_reduced_999 at 0x008212

card4-v100-0b.rom
  set_reduced_999 at 0x008222

266855-extracted-pcirom.rom
  set_reduced_999 at 0x008222

NVIDIA.TeslaV100.16384.170728-extracted-pcirom.rom
  no set_reduced_999 / no same-register no-op pattern

NVIDIA.TeslaV100.32768.180223-extracted-pcirom.rom
  no set_reduced_999 / no same-register no-op pattern

NVIDIA.QuadroGV100.32768.180214-extracted-pcirom.rom
  no set_reduced_999 / no same-register no-op pattern

NVIDIA.TitanV.12288.180904-extracted-pcirom.rom
  same-register no-op at 0x008242

NVIDIA.TitanV.32768.180602-extracted-pcirom.rom
  same-register no-op at 0x008242
```

The decoded main-init script tail makes the control contrast sharper. Local V100-personality / 266855 reaches the common GPC setup writes and then inserts the 13-byte FECS set-reduced opcode before `DONE`:

```text
266855-extracted-nvbios-vbu.txt
0x00008207: ZM_REG R[0x137330] = 0x87100606
0x00008210: ZM_REG R[0x137310] = 0x87101e1e
0x00008219: ZM_REG R[0x137300] = 0x20000000
0x00008222: NV_REG R[0x409664] &= 0xfffff666 |= 0x00000999
0x0000822f: DONE
```

Full-speed Tesla V100 32GB and Quadro GV100 controls reach `DONE` at the same `0x8222` point instead:

```text
NVIDIA.TeslaV100.32768.180223-extracted-pcirom.txt
0x00008207: ZM_REG R[0x137330] = 0x87100606
0x00008210: ZM_REG R[0x137310] = 0x87101e1e
0x00008219: ZM_REG R[0x137300] = 0x20000000
0x00008222: DONE

NVIDIA.QuadroGV100.32768.180214-extracted-pcirom.txt
0x00008207: ZM_REG R[0x137330] = 0x87100606
0x00008210: ZM_REG R[0x137310] = 0x87101e1e
0x00008219: ZM_REG R[0x137300] = 0x20000000
0x00008222: DONE
```

Interpretation: the local capped/CMP-derived GV100 images contain a concrete VBIOS init opcode that asserts the FECS speed-select reduced/override bits. The full-speed Tesla V100 and Quadro GV100 controls in the local corpus do not contain that exact set-reduced opcode. Two Titan V controls touch the same register but write a no-op value. This strengthens the conclusion that the visible pre-driver assertion point is the VBIOS init script at raw ROM offset `0x8212` / `0x8222`, while the remaining unknown is the private meaning of GV100 `FMLA_REDUCED_SPEED` as the observed `1/16` issue divisor.

### 31. Main Init Tail Backtrack

A second offline helper was added:

```text
tools/gv100_vbios_init_tail_compare.py
```

It extracts the selected main init script, the relevant tail, and the FECS/DONE anchor from decoded `nvbios` output.

The important result is that the target write is not hidden behind a separate display script or conditional table. Each image has one selected main init script:

```text
cmp-card0                 table 0x48f7, script0 0x76ba, anchor 0x8212, FECS write
v100-personality-card4    table 0x4934, script0 0x76ca, anchor 0x8222, FECS write
v100-personality-266855   table 0x4934, script0 0x76ca, anchor 0x8222, FECS write
tesla-v100-16gb-control   table 0x4932, script0 0x7665, anchor 0x81bd, DONE after common tail
tesla-v100-32gb-control   table 0x4934, script0 0x76ca, anchor 0x8222, DONE after common tail
quadro-gv100-control      table 0x4934, script0 0x76ca, anchor 0x8222, DONE after common tail
titan-v-12gb-control      table 0x4934, script0 0x76ea, anchor 0x8242, same-register no-op
```

The shared tail before the anchor is:

```text
NV_REG R[0x00020c] &= 0xdfeffff3 |= 0x2010000c
ZM_REG R[0x137330] = 0x87100606
ZM_REG R[0x137310] = 0x87101e1e
ZM_REG R[0x137300] = 0x20000000
```

Then the images split:

```text
capped/CMP-derived:
  NV_REG R[0x409664] &= 0xfffff666 |= 0x00000999
  DONE

full-speed Tesla V100 / Quadro GV100 controls:
  DONE

Titan V controls:
  NV_REG R[0x409664] &= 0xffffffff |= 0x00000000
  DONE
```

Interpretation: the visible pre-driver limiter assertion is an unconditional main-init-script tail opcode in the capped/CMP-derived VBIOS images. The closest full-speed GV100 controls with the same script table/script0 location simply terminate at that point.

### 32. Local NVIDIA Falcon HS/PUB Docs Explain The Internal PLM Path

The Hexkyz/SciresM Falcon article is useful because it gives the public
vocabulary for Falcon execution modes: IMEM, DMEM, CSB, SCP, secure boot ROM,
and Heavy Secure mode. The local tree contains an NVIDIA-side counterpart in
the SBR/PUB safety docs.

Key local sources:

```text
integ/gpu_drv/stage_rel/safety/SBR-TU10A/SWE-SBR-007-SWADS.md
integ/gpu_drv/stage_rel/safety/SBR-TU10A/Unit_Design/SWE-SBR-009-SWUD-LITE.md
```

Those docs describe a SEC2 Falcon microcode called PUB, the PLM Update Binary:

```text
PUB code is loaded into SEC2 Falcon IMEM.
PUB data is loaded into SEC2 Falcon DMEM and consists only of the HS signature.
PUB has no callable external interface; status is reported through a Falcon mailbox.
PUB-NS sets up Falcon bootrom parameters.
PUB-HS runs Heavy Secure HS Lib and PUB Core.
PUB Core lowers selected PLMs so PMU and CTXSW FECS/GPCCS ucodes can access protected registers.
PUB must complete before PMU and CTXSW microcodes are loaded.
PUB Core sequence includes Lower PMU PLMs and Lower CTXSW PLMs.
```

The docs also make the signing boundary explicit:

```text
Production PUB has no test/debug interface.
Debug HS-signed PUB runs only on debug boards.
Production HS signing is limited.
PUB HS ucode is encrypted.
PUB HS ucode is PKC RSA-3K signed and verified by SEC2 Falcon bootrom.
PUB version acceptance is tied to fuse-based revocation/version checks.
```

The same document says the actual PLM list was maintained outside this
checkout:

```text
PMU and CTXSW elements need to publish the list of PLMs as part of their requirements/SWAD.
Currently the list of PLMs to be updated by PUB is maintained in RMChipsSecurity Confluence.
PUB must lower priv levels for all registers accessed in PMU and CTXSW microcodes to privilege level 0.
Alternative to PUB is ACR, where the microcodes are booted at LS level.
```

Interpretation: this gives a concrete NVIDIA internal mechanism for changing
PLMs. It is not a normal RM register write and not a user-space path. If the
GV100 FECS feature-override PLM at `0x409650` is held closed by board/SKU/fuse
policy, the source-supported way NVIDIA would alter that class of gate is a
signed Falcon HS flow on supported debug/manufacturing hardware, not a public
production driver control. The high-value missing artifact is now narrower:
the PMU/CTXSW PUB PLM list, not another generic Falcon exploit write-up.

## Current Hypothesis

The best-supported chain is:

```text
Hidden board/SKU/fuse/InfoROM state
  -> RM/firmware resolves CMP or reduced-throughput policy
  -> SM issue-rate modifier for FMLA16/HMMA is set to 1/16
  -> public CUDA still sees SM 7.0 and Tensor Core kernels still execute
  -> Nsight reports exactly 6.25% sustained SM throughput
```

The source supports this chain because:

1. `1/16` is an explicit RM speed-select enum.
2. Internal chip info includes `isCmpSku`.
3. `1DF4` is ambiguous between `TESLA_4_B` and `PG500_8`.
4. InfoROM/OEM/OBD data is a first-class cached identity source.
5. Later MODS code implements instruction issue-rate override/readback logic for `FMLA16`.
6. RM stubs `GET_SM_ISSUE_RATE_MODIFIER` for `pre_TURING`, explaining why GV100 returns unsupported through the public control despite having FECS speed-select bits.
7. GV100 exposes only binary FECS `FMLA_REDUCED_SPEED`; the observed exact `1/16` divisor is likely selected by firmware/private policy behind that reduced state.
8. Later `g00x` fuse definitions carry direct 3-bit `OPT_SM_SPEED_SELECT_FMLA16` fields, showing that the explicit selector exists in newer generations but is not exposed as a GV100 `NV_FUSE_OPT_*` field in this tree.
9. `RMOverrideSmSpeedSelect*` and `RMSchMicroSched` show explicit code paths for SM speed select and HMMA scheduler slowdown policy, but they are verification/internal/Turing-or-later paths in the visible source.
10. `gpuGetIsCmpSku_GV100()` proves GV100 CMP state is code-visible and fuse-derived, but the found consumers mainly affect reporting and CUDA devtools, not the Tensor issue-rate cap directly.
11. Internal engineering notes explicitly describe the transition from older 1-bit speed-select fuses to newer 3-bit `SM_SPEED_SELECT` fuses, matching the GV100-vs-later split seen in headers and RM HAL wiring.
12. The exact visible GV100 location is `NV_PGRAPH_PRI_FECS_FEATURE_READOUT_SM_SPEED_SELECT_FMLA` / `NV_CTXSW_FIRMWARE_FEATURE_READOUT_SM_SPEED_SELECT_FMLA`; the exact producer of that bit is not present as a named GV100 RM C implementation in this tree.
13. GV100 NETD generated context state includes `NET24_NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT`, proving the override register is part of the GV100 FECS context-state surface.
14. MODS IFF/chipfuse parsing supports SKU-specific IFF rows, but the actual GV100 chipfuse data file that could name such a row is still missing locally.
15. `gv100_f.json` / encrypted `gv100_f.jsone` is a confirmed MODS runtime input path with decrypt-and-parse support, including `IFF_patches`; `gv100_POR.json` is a confirmed GV100+ POR sidecar input.
16. The exact chipfuse sections to inspect are `Mkt_options`, `IFF_patches`, and `Por_data`; `IFF_patches` carries per-SKU ordered row values that would be the most likely visible-format representation of a SKU-specific pre-driver policy patch.
17. Local searches found no actual encrypted `*.jsone`/`*.xme` artifacts or archive members for `gv100_f` / `gv100_POR`; the files are referenced by source/build logic but absent from this workspace.
18. Fresh PROM dumps confirm that same-version ROM differences are OBD metadata only, while CMP-vs-V100 visible `BIT M` deltas backtrack into memory table/restrict parsing rather than Tensor/GR issue-rate policy.
19. Ampere/Turing PMU code explicitly caches/restores `Slowdown/HMMA devinit settings`, reinforcing that the policy exists in code, but not as a decoded GV100 VBIOS `M` table limiter.
20. CodeSPELUNKER ranked search confirmed the same split: later chips have explicit divisor and HMMA slowdown code; GV100 has generated FECS/CTXSW surfaces and missing chipfuse/POR inputs, but no visible named C producer for `SM_SPEED_SELECT_FMLA`.
21. Local build code can encrypt/package `gv100_f.json` into `gv100_f.jsone` and runtime code can decrypt/parse either form, but the source JSON is expected from separate `//sw/mods/chipfuse` and `//sw/mods/por_data` trees rather than generated by this checkout.
22. `EXEC_REG_OPS` accepts normal user-space register-read requests, but rejects the exact GV100 FECS offsets `0x409660` and `0x409664` as `INVALID_OFFSET` because they are not exposed through the GV100 user-register access map.
23. The internal static KGR/GR issue-rate controls also return `NV_ERR_NOT_SUPPORTED` on both local CMP and local V100 GV100 cards, so they do not provide a production user-space readback path for the hidden speed-select values.
24. The generated GV100 `user_access_map_reglist.h` permits a few nearby FECS/profiling registers, but does not include `NV_PGRAPH_PRI_FECS_FEATURE_READOUT` or `NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT`; this directly explains the `EXEC_REG_OPS` `INVALID_OFFSET` result.
25. The regaccess generator's default YAML whitelist is the reason: `common.yml` and `cuda_tools.yml` explicitly include selected FECS registers, but no default whitelist config includes the GV100 FECS speed-select feature readout/override registers or their raw offsets.
26. The VBIOS init write is anchored to exact raw ROM bytes: CMP at `0x8212`, local V100-personality / 266855 at `0x8222`; full-speed Tesla V100 and Quadro GV100 controls do not contain that set-reduced opcode, while two Titan V controls contain a same-register no-op at `0x8242`.
27. A local SGEMM/DGEMM ratio sweep shows that FP64/DP is capped on both local V100-personality and CMP-labelled cards. SGEMM is normal per SM, while DGEMM is only about `6.8-7.3%` of the FP32-implied full-speed V100 FP64 rate, consistent with `DP_REDUCED + DP_OVERRIDE` in the same `0x999` FECS mask.
28. A local Tensor/SGEMM sweep shows the FMLA/HMMA cap is also uniform across the local card set: V100-personality cards average `0.08707` FP16 tensor TFLOP/s/SM and CMP-labelled cards average `0.08692`, so the V100 personality changes total throughput via SM count but not tensor issue class.
29. A targeted DP4A probe now tests a later-source-mapped IMLA-family path (`IDP4A_S32_S8 -> IMLA3`) and emits real `IDP.4A.S8.S8` SASS, but it does not show an HMMA/DP-class collapse. Therefore IMLA remains unresolved rather than proven capped.
30. Local SBR/PUB docs show NVIDIA's internal PLM-lowering mechanism: a SEC2 Falcon PLM Update Binary runs in Heavy Secure mode, is bootrom-verified, encrypted/signed, debug-runnable only on debug boards, and explicitly lowers PLMs needed by PMU and CTXSW FECS/GPCCS ucodes.

## What Is Not Yet Proven

This pass did not find the closed GV100 RM implementation that decides the speed-select policy.

This pass did not find the full GV100 chipfuse XML/JSON dictionary naming a specific `NV_FUSE_*` field for `FMLA16`/HMMA issue rate.

This pass did not prove that a runtime override is available or unprotected on GV100. The Turing/Ampere MODS code explicitly checks feature support and protection before using its issue-rate override path.

This pass found evidence against an ordinary GV100 MODS/RM readback path: RM explicitly stubs public issue-rate readback for `pre_TURING`, and MODS GV100/Volta classes do not implement issue-rate override/readback.

## Blocker State After Exact ROM Anchor

The investigation is now blocked on unlocking, not on locating the visible assertion point.

What is now established:

```text
local capped/CMP-derived VBIOS init
  -> writes 0x409664 with 0x999
  -> asserts IMLA/FMLA/DP reduced + override
  -> normal production RM user-space cannot read the live register
  -> Tensor/HMMA throughput remains at the observed 1/16 class
```

What would be required to go further:

```text
1. A safe/authorized maintenance path that can read GV100 FECS feature state live.
2. The missing gv100_f.json/gv100_f.jsone and gv100_POR.json data, if the producer is an IFF/chipfuse SKU row.
3. A fuller GV100 FECS/GR firmware or private RM source drop that defines the reduced-state divisor.
4. Hardware/firmware modification or privileged MMIO access to alter the VBIOS init behavior or FECS register state.
```

The first three are unavailable in the current local workspace. The fourth is outside the read-only/source-analysis boundary used here and was not attempted.

## Downstream Measurement Update

A downstream DP/IMLA probe pass was added:

```text
scripts/dp_imla_issue_probe.cu
scripts/dgemm_probe.cu
dp-imla-downstream-probe-2026-05-20.md
runs/20260520-dp-imla-issue/
```

This tests the other bit families set by `0x999`:

```text
DP_REDUCED + DP_OVERRIDE
IMLA_REDUCED + IMLA_OVERRIDE
```

Important results:

```text
V100-personality DGEMM: 0.4368 TFLOP/s
CMP DGEMM:             0.3665 TFLOP/s

V100-personality one-wave DFMA: 64.0008 cycles/op/thread
CMP one-wave DFMA:             64.0008 cycles/op/thread
```

The DGEMM numbers are very low for V100-class FP64 and scale roughly with visible SM count:

```text
80 / 68 = 1.176
0.4368 / 0.3665 = 1.192
```

Interpretation: DP now has a measurable downstream anomaly consistent with `DP_REDUCED`. IMLA remains less decisive because the integer probe does not have a full-speed GV100 control and does not show a HMMA-like collapse by itself.

Nsight Compute issue/stall counters were attempted from the CUDA container, but the run hit:

```text
ERR_NVGPUCTRPERM
```

So this pass adds timing/throughput evidence, not issue/stall counter evidence.

## SGEMM/DGEMM Ratio Update

A second DP-focused downstream pass was added:

```text
scripts/sgemm_dgemm_ratio_probe.cu
sgemm-dgemm-ratio-probe-2026-05-20.md
runs/20260520-sgemm-dgemm-ratio/
```

The probe compares FP32 GEMM and FP64 GEMM on the same local cards. This matters because a normal V100-class part should have FP64 throughput near one half of FP32 throughput.

Group averages:

```text
V100-personality: SGEMM 12.038 TFLOP/s, DGEMM 0.436 TFLOP/s, ratio 27.61
CMP-labelled:     SGEMM 10.751 TFLOP/s, DGEMM 0.366 TFLOP/s, ratio 29.34
```

Using SGEMM as the local FP32 reference:

```text
expected full-speed DGEMM ~= SGEMM / 2
observed DGEMM fraction   ~= 0.068 to 0.073
inverse                   ~= 13.6x to 14.7x slow
```

Interpretation: the `DP_REDUCED + DP_OVERRIDE` part of the `0x999` mask has a measurable downstream effect. The V100-personality cards are not full-speed V100 FP64 devices; they share the same capped DP class as the CMP-labelled cards and differ mainly by visible SM count.

## Local Tensor/SGEMM Sweep Update

A broader local FMLA/HMMA sweep was also added:

```text
scripts/cublas_tensor_probe.cu
local-tensor-sgemm-sweep-2026-05-20.md
runs/20260520-tensor-sgemm-sweep/
```

Group averages:

```text
V100-personality: FP16 tensor 6.966 TFLOP/s, FP32 6.510 TFLOP/s, tensor/SM 0.08707
CMP-labelled:     FP16 tensor 5.910 TFLOP/s, FP32 5.671 TFLOP/s, tensor/SM 0.08692
```

The total tensor throughput scales with visible SM count:

```text
80 / 68 = 1.176
6.966 / 5.910 = 1.179
```

Interpretation: the V100-personality cards expose more SMs, but the FMLA/HMMA issue class per SM is the same capped class as the CMP-labelled cards. This reinforces that the V100 identity/personality does not clear the lower-level FECS speed-select policy.

## DP4A / IMLA Update

A better IMLA-family downstream probe was added:

```text
scripts/dp4a_issue_probe.cu
dp4a-imla-downstream-probe-2026-05-20.md
runs/20260520-dp4a-imla-probe/
```

Reason: later-generation MODS source maps `IDP4A_S32_S8` to `IMLA3`:

```text
Turing: turinggpu.cpp:1697-1710
Ampere: amperegpu.cpp:4877-4895
```

The compiled local probe emits real Volta `IDP.4A.S8.S8` instructions, but the throughput does not show an HMMA/DP-class issue-rate collapse:

```text
V100-personality average independent4 DP4A: 4654.1 GDP4A/s
CMP-labelled average independent4 DP4A:     4218.8 GDP4A/s
```

Interpretation: the earlier generic `IMAD` probe was not a strong IMLA test, and this DP4A probe is better. But even this closer path does not prove an IMLA downstream cap. The currently supported conclusion is:

```text
FMLA/HMMA: capped.
DP/FP64:   capped.
IMLA:      unresolved; no DP4A-class collapse observed.
```

## NVIDIA Internal Debug Path Update

The driver source does contain the internal/debug style path NVIDIA would use for SM speed-select experiments.

The relevant RM regkeys are defined in both `dev` and `integ` trees:

```text
dev/gpu_drv/stage_rel/drivers/resman/interface/nvRmReg.h:9208-9211
integ/gpu_drv/stage_rel/drivers/resman/interface/nvRmReg.h:9535-9538

RMOverrideSmSpeedSelect
RMOverrideSmSpeedSelect1
```

The source comment is explicit:

```text
Allow setting the SM_SPEED_SELECT/1 through regkeys.
This takes effect if verif builds only.
```

Common GR init reads those values:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/gr/nv/objgr.c:3175-3184
integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/gr/graphics.c:2596-2605
```

The HAL writer is not wired for GV100:

```text
dev/gpu_drv/stage_rel/drivers/resman/config/haldefs/gr.def:4233-4242

FEATURE_OVERRIDE_SM_SPEED_SELECT
  _TU102 => [ TURING, ]
  _GA100 => [ AMPERE_and_later, ]
  _STUB  => [ pre_TURING, ]
```

The Turing implementation writes the FECS override register:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/gr/turing/grcxtu102.c:344-357
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
```

The Ampere implementation writes the later FUSE override register:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/gr/ampere/grga100.c:606-622
NV_FUSE_FEATURE_OVERRIDE_SM_SPEED_SELECT
```

The apply path also shows a scheduler-level debug hook:

```text
RMSchMicroSched
NV_PGRAPH_PRI_GPCS_TPCS_SM_SCH_MICRO_SCHED_FUSE_SLOWDOWN_JITTER_HMMA16
```

but that apply block is in the Turing override path and is additionally gated by `gpuIsInternalSku_HAL(pGpu)`:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/gr/turing/grcxtu102.c:410-431
```

Interpretation: yes, an NVIDIA internal debug path exists. It is the `RMOverrideSmSpeedSelect*` / `grFeatureOverrideSmSpeedSelect_HAL` path, with a related `RMSchMicroSched` slowdown/jitter path. In this visible source tree, however, the writer is explicitly stubbed for `pre_TURING`, and GV100's `APPLY_OVERRIDES` dispatch resolves to the older GM200-era path rather than the Turing path that applies these overrides. That means the normal production GV100 RM does not expose the internal speed-select disable path even though the concept and register are present.

## Repository Documentation Hints For The Debug Path

The repo has several documentation-like hints that point at the same mechanism.

### MODS Engineering Notes

`engnotes_willow.txt` records a manuals update for the speed-select feature:

```text
integ/gpu_drv/stage_rel/diag/mods/docs/engnotes_willow.txt:643

1999526:
Submitting a manuals change to add the REDUCED_SPEED defines for the new
SM_SPEED_SELECT feature override registers.
```

The same file also documents internal-only support packaging:

```text
integ/gpu_drv/stage_rel/diag/mods/docs/engnotes_willow.txt:1198

1972559:
Allow internal only chip skus with bypass.bin
```

### RM API Documentation

The FINN RM API comments describe the public readback interface:

```text
integ/gpu_drv/stage_rel/drivers/resman/interface/rmapi/finn/ctrl/ctrl2080/ctrl2080gr.finn:1654-1686
```

The command is documented as returning speed-select values for:

```text
IMLA0
FMLA16
DP
FMLA32
FFMA
IMLA1
IMLA2
IMLA3
IMLA4
```

and the enum includes:

```text
FMLA16_REDUCED_SPEED_1_16 = 0x4
```

### MODS Test Naming

MODS names the issue-rate override feature `Volta-953`:

```text
integ/gpu_drv/stage_rel/diag/mods/gpu/js/gpuargs.js:10920
Override issue rate (Volta-953) if requested

dev/gpu_drv/stage_rel/diag/mods/gpu/turinggpu.cpp:1786-1798
Chip under test does not support instruction-issue rate override (Volta-953)
Instruction issue-rate override (Volta-953) not supported on this board
(FECS_FEATURE_OVERRIDE is PRIV protected)
```

That error text is especially useful because it confirms that even when the register exists, board-level PRIV protection can block the override path.

### Regaccess Config

The register access tooling explicitly lists the Volta FECS speed-select override register:

```text
integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/config/common.yml:16-17

NV_PGRAPH_PRI_FECS_FEATURE_READOUT: [volta]
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT: [volta]
```

This is a documentation/config hint that the register is a known Volta target, independent of the later Turing/Ampere RM override implementation.

### Simulation Argument

MODS simulation args expose the scheduler slowdown/jitter debug register:

```text
dev/gpu_drv/stage_rel/diag/mods/gpu/js/simargs.js:320
SchMicroSched -> Registry.ResourceManager.RMSchMicroSched
Set bits of NV_PTPC_PRI_SM_SCH_MICRO_SCHED_FUSE_SLOWDOWN_JITTER register
```

Interpretation: yes, repo docs strongly hint at the internal/debug path. The public-facing names are `Volta-953`, `SM_SPEED_SELECT feature override`, `RMOverrideSmSpeedSelect*`, and `RMSchMicroSched`. The docs also explain why this is not a normal user path: verification builds, internal-only bypass packaging, and board-level PRIV protection all appear in the nearby documentation/comments.

## What These Hints Let Us Do

The documentation and code hints give a concrete blocker map.

### 1. Use `Volta-953` As The Canonical MODS Feature Name

The MODS argument surface is:

```text
integ/gpu_drv/stage_rel/diag/mods/gpu/js/gpuargs.js:664

override_issue_rate
  -> g_OvrIssueRate
  -> GpuSubdev.OverrideIssueRate(...)
```

The generic test path applies it here:

```text
integ/gpu_drv/stage_rel/diag/mods/gpu/js/gpuargs.js:10920-10923
integ/gpu_drv/stage_rel/diag/mods/gpu/tests/gputest.cpp:228-230
```

and the C++ wrapper parses/restores it here:

```text
integ/gpu_drv/stage_rel/diag/mods/gpu/gpusbdev.cpp:8260-8295
```

This gives a reliable term and call chain for all further searching:

```text
Volta-953 -> IssueRateOverride -> CheckIssueRateOverride -> FECS_FEATURE_OVERRIDE
```

### 2. Separate The Two Failure Classes

MODS distinguishes two different blockers:

```text
Chip under test does not support instruction-issue rate override (Volta-953)
Instruction issue-rate override (Volta-953) not supported on this board
(FECS_FEATURE_OVERRIDE is PRIV protected)
```

Source:

```text
dev/gpu_drv/stage_rel/diag/mods/gpu/turinggpu.cpp:1781-1798
integ/gpu_drv/stage_rel/diag/mods/gpu/turinggpu.cpp:1790-1807
```

That distinction is useful:

```text
Unsupported feature:
  RegHal/register dictionary does not expose the override register for that chip path.

PRIV protected:
  The register exists, but board/security state blocks writes.
```

Our earlier user-space `invalid offset` result aligns with the first class for normal RM regops: the production user register access map does not expose this register.

### 3. Know Which Protection Register To Investigate Read-Only

The PRIV protection check is against:

```text
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK
```

GV100 ctxsw generated files include that register in the context-state store:

```text
integ/gpu_drv/stage_rel/drivers/resman/kernel/inc/ctxsw_ucode/volta/gv100/net/NETD_dev_graphics_nobundle.h:388-389
integ/gpu_drv/stage_rel/drivers/resman/kernel/inc/ctxsw_ucode/volta/gv100/net/NETD_ctx_state_store.h:20344-20345
```

This means the next useful non-destructive comparison is not only `0x409664`; it is also the associated PLM/PRIV mask state around:

```text
0x40964c  FECS_FEATURE_OVERRIDE_ECC_PRIV_LEVEL_MASK
0x409650  FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK
0x409664  FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
```

### 4. Explain Why `bypass.bin` Matters

The docs show `bypass.bin` / `bypass.INTERNAL.bin` is how MODS enables internal-only paths:

```text
engnotes_willow.txt:1196-1198
modstest.js:3896-3900
comngpu.js:64-65
```

Related switches:

```text
BoardDB.AllowInternalSkus
Fuse.CheckInternalFuses
```

Interpretation: if an NVIDIA lab could disable this class of limiter internally, it likely used some combination of internal MODS build, bypass packaging, internal SKU recognition, and a board state where FECS feature override was not PRIV-blocked. That is materially different from a production Linux RM path.

### 5. Drive The Next Read-Only Work

The hints give three concrete next read-only targets:

```text
1. Build a source-level DAG from:
   override_issue_rate -> CheckIssueRateOverride -> RegHal support -> PRIV mask -> FECS override.

2. Backtrack `NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK`
   through ctxsw NETD generated files and RM register access-map generation.

3. Compare capped GV100 vs full V100 evidence for:
   FECS feature readout,
   FECS feature override PLM,
   and whether the register is absent from user regop access or present-but-protected.
```

## Access-Map Backtrack: Why User Regops Return `INVALID_OFFSET`

The source path for user-space register ops makes the earlier `INVALID_OFFSET` result more precise.

In the modern `integ` RM tree:

```text
integ/gpu_drv/stage_rel/drivers/resman/src/kernel/gpu/subdevice/subdevice_ctrl_gpu_regops.c:650-693
gpuValidateRegOps(...)
  -> gpuValidateRegOffset(...)
  -> if status != NV_OK, set NV2080_CTRL_GPU_REG_OP_STATUS_INVALID_OFFSET
```

The offset validator itself distinguishes out-of-range offsets from permission failures:

```text
integ/gpu_drv/stage_rel/drivers/resman/src/kernel/gpu/gpu_access.c:1693-1721
gpuValidateRegOffset_IMPL(...)
  if offset > BAR0 size: NV_ERR_INVALID_ARGUMENT
  if !administrator && !gpuGetUserRegisterAccessPermissions(...):
      NV_ERR_INSUFFICIENT_PERMISSIONS
```

But `gpuValidateRegOps()` collapses either failure into `NV2080_CTRL_GPU_REG_OP_STATUS_INVALID_OFFSET`.

Interpretation: an `EXEC_REG_OPS` invalid-offset result for `0x409660` or `0x409664` does not prove the register is absent. It can mean the production user register access map denies the register.

### Embedded GV100 Maps Deny The FECS Speed-Select Registers

GV100 production RM embeds a compressed user access map:

```text
integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/arch/volta/gpu_gv100.c:27
#include "volta/gv100/user_access_map_gzip.h"

integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/arch/volta/gpu_gv100.c:118-130
gpuGetUserRegisterAccessMap_GV100(...)
  *ppComprData = UserAccessMapCompr
```

Decoding the checked-in GV100 compressed maps gives:

```text
user_access_map_gzip.h inflated size: 524288 bytes
global_access_map_gzip.h inflated size: 524288 bytes

0x409b00 NV_PGRAPH_PRI_FECS_CURRENT_CTX                         allowed
0x409660 NV_PGRAPH_PRI_FECS_FEATURE_READOUT                     denied
0x409664 NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT    denied
0x40964c NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_ECC_PRIV_LEVEL_MASK denied
0x409650 NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK     denied
```

This exactly explains why the public regop path cannot read the FECS speed-select state on this host.

### Current Regaccess Config Would Allow Two Of Them

The checked-in regaccess generator config is different from the checked-in compressed GV100 map:

```text
integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/config/common.yml:15-17
NV_PGRAPH_PRI_FECS_CURRENT_CTX: [hopper, ada, ampere, turing, volta, pascal, maxwell, kepler]
NV_PGRAPH_PRI_FECS_FEATURE_READOUT: [volta]
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT: [volta]
```

Regenerating a GV100 map from that current `common.yml` and the checked-in GV100 hwref headers produced:

```text
projects/gpu-falcon-research/runs/20260520-accessmap/gv100-common-accessmap.bin

0x409660 NV_PGRAPH_PRI_FECS_FEATURE_READOUT                  allowed
0x409664 NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT allowed
0x40964c FECS_FEATURE_OVERRIDE_ECC_PRIV_LEVEL_MASK           denied
0x409650 FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK               denied
```

Interpretation: the source tree contains an internal/debug register-access intent that would allow readback of the FECS feature readout and speed-select override registers on Volta. The production GV100 compressed maps used by RM do not include those bits. This is the best explanation so far for the host behavior:

```text
known register -> denied by production access map -> regops reports INVALID_OFFSET
```

The important unresolved point is whether a debug/develop RM build would merely expose readback or also run into the board-level `FECS_FEATURE_OVERRIDE is PRIV protected` path when writes are attempted.

### Passthrough Specs Also Exclude The Feature-Override Range

The passthrough tooling gives a second, independent access-policy view:

```text
integ/gpu_drv/stage_rel/drivers/resman/tools/passthrough/gpu_regs.yml
```

That file classifies registers as whitelist, blacklist, or documentation-only entries, with explicit security-risk labels such as:

```text
user-dos
user-leak
user-escape
shared-gpu-dos
shared-gpu-leak
shared-gpu-escape
passthrough-dos
passthrough-leak
passthrough-escape
hw-damage
```

It names many FECS registers for passthrough policy, including FECS current context, method data/push, mailbox, perfmon, and Falcon/debug registers. It does not list:

```text
NV_PGRAPH_PRI_FECS_FEATURE_READOUT
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_ECC_PRIV_LEVEL_MASK
```

The generated GV100 passthrough spec has the same shape around the relevant offsets:

```text
integ/gpu_drv/stage_rel/drivers/resman/tools/passthrough/specs/gv100_passthrough_spec.yml:10391-10417

0x409500-0x409507  FECS method data/push allowed
0x409614           FECS ctxsw reset control allowed
0x409618-0x4097ff  denied / mask 0
0x409800-...       FECS ctxsw mailboxes allowed
```

That denied `0x409618-0x4097ff` span covers the FECS feature-override/readout region:

```text
0x40964c  NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_ECC_PRIV_LEVEL_MASK
0x409650  NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK
0x409660  NV_PGRAPH_PRI_FECS_FEATURE_READOUT
0x409664  NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
0x409670  NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_1
```

There is also a useful `dev` versus `integ` split in the regaccess generator config:

```text
dev/gpu_drv/stage_rel/drivers/resman/tools/regaccess/config/common.yml:15
NV_PGRAPH_PRI_FECS_CURRENT_CTX: [ampere, turing, volta, pascal, maxwell, kepler]

integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/config/common.yml:15-17
NV_PGRAPH_PRI_FECS_CURRENT_CTX: [hopper, ada, ampere, turing, volta, pascal, maxwell, kepler]
NV_PGRAPH_PRI_FECS_FEATURE_READOUT: [volta]
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT: [volta]
```

Interpretation: there are three visible policy layers:

```text
1. Production generated GV100 RM access maps deny FECS feature readout/override.
2. GV100 passthrough specs deny the same FECS feature range.
3. A newer/current integ regaccess config whitelists Volta FECS feature readout and speed-select override.
```

So the source tree contains evidence of an internal/debug intent to expose those two Volta FECS registers, but the checked-in production and passthrough maps for GV100 do not expose them. The next provenance target is the map-generation path between `common.yml` and `user_access_map_gzip.h`, including any branch-specific exclusions or stale generated outputs.

### Access-Map Lifecycle Explains The Internal Debug Path

RM builds the user register access map by inflating the chip-specific compressed map:

```text
integ/gpu_drv/stage_rel/drivers/resman/src/kernel/gpu/gpu_register_access_map.c:197-238
gpuConstructUserRegisterAccessMap_IMPL(...)
  -> gpuGetUserRegisterAccessMap_HAL(...)

integ/gpu_drv/stage_rel/drivers/resman/src/kernel/gpu/gpu_register_access_map.c:260-287
  allocate pUserRegisterAccessMap
  inflate compressedData into pUserRegisterAccessMap

integ/gpu_drv/stage_rel/drivers/resman/src/kernel/gpu/gpu_register_access_map.c:290-305
  copy pUserRegisterAccessMap to pUnrestrictedRegisterAccessMap
  then remove profiling ranges only from pUserRegisterAccessMap when profiling is admin-only
```

That makes the word `unrestricted` narrower than it sounds. The unrestricted/admin map is not an all-register map; it is a copy of the generated architecture map before profiling counters are removed.

The GR/FECS path then commits both maps to hardware-visible context buffers:

```text
integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/gr/arch/kepler/gr_gk104.c:6405-6420
Commit user register access map
Commit admin user register access map

dev/gpu_drv/stage_rel/drivers/resman/config/haldefs/gr.def:429-431
UCODE_SUPPORTS_PRIV_ACCESS_MAP_DEF
  FECS ucode uses the register access map
```

Channel selection between normal and unrestricted maps is based on RM client privilege:

```text
integ/gpu_drv/stage_rel/drivers/resman/src/kernel/gpu/gr/kernel_graphics_context.c:3142-3172
kgrctxGetRegisterAccessMapId_PF(...)
  if privLevel >= RS_PRIV_LEVEL_USER_ROOT:
      GR_GLOBALCTX_BUFFER_UNRESTRICTED_PRIV_ACCESS_MAP
  else:
      GR_GLOBALCTX_BUFFER_PRIV_ACCESS_MAP
```

Interpretation: root/admin can keep profiling-register access that ordinary users lose, but root/admin does not automatically get FECS speed-select if the generated base map omitted that register.

There is a more direct internal test hook:

```text
integ/gpu_drv/stage_rel/drivers/resman/interface/rmapi/finn/ctrl/ctrl208f/ctrl208fgpu.finn:219-258
NV208F_CTRL_CMD_GPU_SET_USER_REGISTER_ACCESS_PERMISSIONS
  tests the mechanism that restricts user-space access to privileged registers
  exposes sensitive information and functionality
  should only ever be enabled on verification builds
  can swap RM's buffer for a provided one

integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/subdevice/subdevice_diag_ctrl.c:406-447
diagapiCtrlCmdGpuSetUserRegisterAccessPermissions_IMPL(...)
  compiled under NV_VERIF_FEATURES or VERIF_ONLY_CONTROLS
  can replace pGpu->pUserRegisterAccessMap
  can update an offset range
  recommits the map when FECS ucode supports priv access maps
```

This explains the likely NVIDIA/MODS internal debug path: a verification/control build can alter the RM access-map buffer and recommit it. It does not imply the same path exists in a production Linux RM, and it still does not bypass a board-level FECS `PRIV_LEVEL_MASK` denial if the underlying register is protected below the map layer.

### Regaccess Generator Provenance

The regaccess Makefile confirms that the normal generator path consumes every config file under `config/*.yml`:

```text
integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/Makefile:10
CONFIGS=$(wildcard config/*.yml)

integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/Makefile:58-94
gen_access_map.py build
  --manual_dir ...
  --binary user_access_map.bin
  --template accessmap.h user_access_map.h
  --template accessmap_gzip.h user_access_map_gzip.h
  --template reglist.h user_access_map_reglist.h
  $(CONFIGS)
```

The generator is simple: it filters manual/header defines by config patterns and emits permitted 32-bit register offsets:

```text
integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/gen_access_map.py:42-86
parse_config(...)
  include patterns whose family list contains the chip family

integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/gen_access_map.py:89-128
parse_manuals(...)
  parse manual/header register defines
  permit matching register values
```

The regaccess README makes clear that this is meant to be a reviewed access-control artifact:

```text
integ/gpu_drv/stage_rel/drivers/resman/tools/regaccess/README:11-38
Sign off process
  Assess Need
  Assess Risk
  Security Team Review/Signoff
  Implementation
```

Interpretation: the current `integ` source can mechanically generate a Volta map that includes `NV_PGRAPH_PRI_FECS_FEATURE_READOUT` and `NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT`. The checked-in GV100 compressed map and reglist do not contain those entries in either local `dev` or `integ` trees:

```text
dev/.../hwref/volta/gv100/user_access_map_reglist.h
  FECS_CURRENT_CTX
  FECS_FALCON_PMM
  FECS_PERFMON

integ/.../hwref/volta/gv100/user_access_map_reglist.h
  FECS_CURRENT_CTX
  FECS_FALCON_PMM
  FECS_PERFMON
```

That leaves two likely source-provenance explanations:

```text
1. The checked-in compressed GV100 maps were generated from an older config, matching current `dev/common.yml`.
2. A later security/signoff step or branch-specific generation run intentionally omitted the FECS feature registers despite the current `integ/common.yml` entries.
```

There is no evidence that the generator itself has a special FECS speed-select filter. The divergence is in config/provenance/signoff, not in a hidden parser rule.

### FECS Feature-Override PLM Is The Next Lower Gate

The GV100 hwref explicitly ties the speed-select override register to the feature-override privilege mask:

```text
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_graphics_nobundle.h:5216-5282
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_ECC_PRIV_LEVEL_MASK = 0x0040964c
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK     = 0x00409650

integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_graphics_nobundle.h:5453-5454
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT = 0x00409664
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT__PRIV_LEVEL_MASK = 0x00000650
```

So the speed-select override register's PLM is the shared feature-override PLM at `0x409650`.

MODS already has the exact lower-gate check on the Turing implementation:

```text
integ/gpu_drv/stage_rel/diag/mods/gpu/turinggpu.cpp:1790-1808
CheckIssueRateOverride()
  if !IsSupported(FEATURE_OVERRIDE_SM_SPEED_SELECT):
      unsupported hardware feature

  if !Test32(FEATURE_OVERRIDE_PRIV_LEVEL_MASK_WRITE_PROTECTION_LEVEL0_ENABLE):
      "FECS_FEATURE_OVERRIDE is PRIV protected"
      RC::PRIV_LEVEL_VIOLATION
```

The RegHal definition exposes only the level-0 write-protection bit needed for that check:

```text
integ/gpu_drv/stage_rel/diag/mods/gpu/reghal/reghal.def:7310-7313
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK
  WRITE_PROTECTION_LEVEL0
    ENABLE
```

Interpretation: even if a debug RM map allowed `0x409664`, MODS expects a second gate at `0x409650`. If `WRITE_PROTECTION_LEVEL0_ENABLE` is not visible/set for the board, the tool reports the feature as board-level PRIV protected. This is the clean source explanation for why the access-map problem and the board-policy problem are separable:

```text
access map denies 0x409664 -> RM user regops returns INVALID_OFFSET
access map allows 0x409664 but PLM denies writes -> MODS returns PRIV_LEVEL_VIOLATION
```

### Feature-Override PLM Fuse-Signal Breadcrumb

The shared feature-override PLM has a stronger provenance clue in newer manual/generated sources.

On GV100, the ECC-specific FECS feature-override PLM names its fuse signal:

```text
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_graphics_nobundle.h:5234
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_ECC_PRIV_LEVEL_MASK_WRITE_PROTECTION__FUSE_SIGNAL
  "feature_override_ecc"
```

The adjacent shared PLM at `0x409650` does not expose a `__FUSE_SIGNAL` string in the GV100 hwref, but the same register family does expose the shared feature-override signal on later generated manual sources:

```text
dev/gpu_drv/stage_rel/tools/pmlsplitter/src/tu101/dev_graphics_nobundle.h:5932
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK_WRITE_PROTECTION__FUSE_SIGNAL
  "feature_override_quadro"

dev/gpu_drv/stage_rel/tools/pmlsplitter/src/ga100/dev_graphics_nobundle.h:6425
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK_WRITE_PROTECTION__FUSE_SIGNAL
  "feature_override_quadro"
```

Later FUSE headers move this PLM into the FUSE register space and keep the same signal names:

```text
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/ampere/ga100/dev_fuse.h:267-270
NV_FUSE_FEATURE_OVERRIDE_PRIV_LEVEL_MASK_WRITE_PROTECTION__FUSE_SIGNAL
  "feature_override_quadro"
NV_FUSE_FEATURE_OVERRIDE_PRIV_LEVEL_MASK_WRITE_PROTECTION_ALL_LEVELS_ENABLED_FUSE0
NV_FUSE_FEATURE_OVERRIDE_PRIV_LEVEL_MASK_WRITE_PROTECTION_ONLY_LEVEL3_ENABLED_FUSE1

integ/gpu_drv/stage_rel/drivers/common/inc/hwref/ampere/ga100/dev_fuse.h:316-319
NV_FUSE_FEATURE_OVERRIDE_ECC_PRIV_LEVEL_MASK_WRITE_PROTECTION__FUSE_SIGNAL
  "feature_override_ecc"
```

The same `feature_override_quadro` / `feature_override_ecc` pairing appears across Turing, Ampere, Hopper, Ada, Blackwell, and `g00x` FUSE or pmlsplitter sources.

Interpretation: for the lower gate at `0x409650`, the most likely source-provenance name is `feature_override_quadro`. GV100's public hwref omits that signal string for the shared PLM, but the later manual/fuse model makes the pairing explicit. This identifies the likely fuse/manual policy input behind the PLM gate; it does not expose a production writable path.

## Switch Fusee Lead

Repository inspected:

```text
https://github.com/erdzan12/switch-fusee
```

The repository is a Fusée Gelée / CVE-2018-6242 launcher for Tegra recovery mode. Its README describes a proof-of-concept arbitrary code loader for Tegra processors over USB RCM. The report describes execution in Tegra bootROM/BPMP context before boot lock-outs, and notes that T210 fuses and keydata are accessible before those lock-outs.

Useful analogy:

```text
early boot context before lock-outs -> fuse/secret access -> payload can inspect state
```

Why it is not directly portable to GV100:

```text
Switch Fusee target: Tegra SoC, USB RCM, BPMP/T210 bootROM
GV100 target: discrete PCIe GPU, RM/FECS/GPCCS/Falcon, BAR0/PRIV/PLM access controls
```

The repo does not provide a GV100 bootROM path, a PCIe Falcon entrypoint, an nvflash bypass, or a FECS access-map bypass. It is still useful conceptually because it names the class of thing we would need for GV100: execution before the relevant lock-outs or access-map restrictions are applied.

For our case, the local evidence still points to this blocker chain:

```text
VBIOS init script writes 0x409664
  -> FECS speed-select reduced/override bits become active
  -> production RM user/global access maps deny 0x409660/0x409664
  -> public EXEC_REG_OPS reports INVALID_OFFSET
  -> RM issue-rate control is stubbed for pre-Turing
```

## CVE-2026-24190 Lead

Official NVIDIA source:

```text
https://nvidia.custhelp.com/app/answers/detail/a_id/5821
Security Bulletin: NVIDIA GPU Display Drivers - May 2026
Updated: 2026-05-18
```

NVIDIA describes `CVE-2026-24190` as a Windows/Linux display-driver vulnerability in the kernel-mode layer where a user could cause improper access to GPU resources.

Bulletin details:

```text
CVSS v3.1: 7.8 High
Vector: AV:L/AC:L/PR:L/UI:N/S:U/C:H/I:H/A:H
CWE: CWE-862 Missing Authorization
Impacts: DoS, privilege escalation, information disclosure, data tampering, code execution
Reported by: Sihyun Roh and Byoungyoung Lee from Compsec, SNU
```

Local host status checked on 2026-05-20:

```text
nvidia-smi reports driver 580.142
NVIDIA bulletin lists Tesla Linux R580 fixed at 580.159.03
```

Therefore this host is in the affected-version range according to NVIDIA's bulletin.

Relationship to this GV100 limiter investigation:

```text
Strong thematic overlap:
  improper access to GPU resources
  missing authorization
  kernel-mode driver resource access control

Known local blocker:
  gpuValidateRegOffset_IMPL returns NV_ERR_INSUFFICIENT_PERMISSIONS
  gpuValidateRegOps collapses denied access to INVALID_OFFSET
  production GV100 access maps deny 0x409660 and 0x409664

Unknown:
  NVIDIA's public CVE text does not identify regops, BAR0, FECS, user access maps,
  UVM/HMM, debugger, profiler, or vGPU as the affected subcomponent.
```

The reporters also have a 2026 paper titled `GHost in the Shell: A GPU-to-Host Memory Attack and Its Mitigation`. That paper focuses on HMM/UVM-style GPU access to host memory and driver page-fault mediation, not FECS speed-select registers. It is useful as a clue that `CVE-2026-24190` may be about GPU resource authorization at the memory/page-fault layer rather than about BAR0 register access.

Interpretation: keep `CVE-2026-24190` as a high-priority security lead and host-risk finding, but do not treat it as proof of a path to `0x409664` unless a patch diff, advisory expansion, or public artifact ties it to RM regops/access-map authorization.

### CVE-2026-24190 Source Diff Lead

Public NVIDIA open-gpu-kernel-modules tags were compared locally:

```text
/tmp/nvidia-ogkm/repo   -> 580.142
/tmp/nvidia-ogkm/fixed -> 580.159.03
```

The strongest patch-shaped candidate for `CVE-2026-24190` is in UVM/HMM permission computation, not BAR0 regops.

In the affected public `580.142` tag, `uvm_va_block_page_compute_highest_permission()` does not special-case HMM blocks before computing GPU mapping permissions:

```text
/tmp/nvidia-ogkm/repo/kernel-open/nvidia-uvm/uvm_va_block.c:11368-11384
```

In the fixed `580.159.03` tag, NVIDIA adds an HMM-specific mapping-protection helper:

```text
/tmp/nvidia-ogkm/fixed/kernel-open/nvidia-uvm/uvm_hmm.c:1586-1608
uvm_hmm_compute_mapping_prot(...)
  if CPU is not mapped -> UVM_PROT_NONE
  if CPU PTE write bit is set -> READ_WRITE or READ_WRITE_ATOMIC
  if CPU PTE read bit is set -> READ_ONLY
  otherwise -> UVM_PROT_NONE
```

The fixed `uvm_va_block_page_compute_highest_permission()` calls that helper before the generic resident-processor path:

```text
/tmp/nvidia-ogkm/fixed/kernel-open/nvidia-uvm/uvm_va_block.c:11377-11382
if (uvm_va_block_is_hmm(va_block))
    return uvm_hmm_compute_mapping_prot(...)
```

The local nvdrv tree has the pre-fix shape. It contains the older TODO and no `uvm_hmm_compute_mapping_prot()` helper:

```text
/home/josh/containers/temp/nvdrv/integ/gpu_drv/stage_rel/drivers/mm/uvm/kernel/linux/uvm_va_block.c:8644-8648
// TODO: Bug 1750144: check logical permissions from HMM to know what's the
//       maximum allowed.
uvm_va_block_page_compute_highest_permission(...)

/home/josh/containers/temp/nvdrv/integ/gpu_drv/stage_rel/drivers/mm/uvm/kernel/linux/uvm_va_block.c:8694-8697
if (!uvm_page_mask_test(&va_block->maybe_mapped_pages, page_index))
    return UVM_PROT_READ_WRITE_ATOMIC;

/home/josh/containers/temp/nvdrv/integ/gpu_drv/stage_rel/drivers/mm/uvm/kernel/linux/uvm_va_block.c:8770
prot_to_map = uvm_va_block_page_compute_highest_permission(...)
```

The older `dev` UVM8 copy has the same unresolved TODO:

```text
/home/josh/containers/temp/nvdrv/dev/gpu_drv/stage_rel/drivers/mm/uvm/kernel/linux/uvm8/uvm8_va_block.c:9048
// TODO: Bug 1750144: check logical permissions from HMM to know what's the
```

Local HMM is present and gated by the `uvm_disable_hmm` module parameter:

```text
/home/josh/containers/temp/nvdrv/integ/gpu_drv/stage_rel/drivers/mm/uvm/kernel/linux/uvm_hmm.c:26-32
uvm_disable_hmm
```

Interpretation: this HMM diff is a real local security-relevant pre-fix shape, but the full bulletin table means it should not be over-assigned to `CVE-2026-24190` without NVIDIA's internal CVE-to-commit mapping. The HMM patch fits `CWE-862 Missing Authorization`, but NVIDIA also lists separate Linux UVM CVEs (`CVE-2026-24194` and `CVE-2026-24195`) in the same bulletin. Treat this as a UVM/HMM host-risk finding and a possible `24190` candidate, not as the only candidate.

### Stronger CVE-2026-24190 Candidate: Non-Privileged DMA PTE Control

The full May 2026 CVE table makes the DMA/PTE diff a stronger match for:

```text
CVE-2026-24190
kernel mode layer
improper access to GPU resources
CWE-862 Missing Authorization
```

In public `580.142`, `NV0080_CTRL_CMD_DMA_SET_PTE_INFO` is exported through the Device control table:

```text
/tmp/nvidia-ogkm/repo/src/nvidia/generated/g_device_nvoc.c:792-805
methodId: 0x80180a
func: deviceCtrlCmdDmaSetPteInfo
flags: 0x10008
```

In fixed public `580.159.03`, that exported method entry is removed. The same diff also changes `DMA_GET_PTE_INFO` flags:

```text
/tmp/nvidia-ogkm/fixed/src/nvidia/generated/g_device_nvoc.c:732-745
DMA_GET_PTE_INFO flags: 0x100008
```

The implementation removed from the fixed public tree was:

```text
/tmp/nvidia-ogkm/repo/src/nvidia/src/kernel/gpu/mem_mgr/dma.c:653-680
deviceCtrlCmdDmaSetPteInfo_IMPL(...)
  vaspaceGetByHandleOrDeviceDefault(...)
  vaspaceSetPteInfo(...)
```

The local nvdrv tree still has this control exposed as non-privileged:

```text
/home/josh/containers/temp/nvdrv/integ/gpu_drv/stage_rel/drivers/resman/inc/kernel/gpu/device/device.h:232-235
RMCTRL_EXPORT(NV0080_CTRL_CMD_DMA_SET_PTE_INFO,
              RMCTRL_FLAGS(NON_PRIVILEGED))
NV_STATUS deviceCtrlCmdDmaSetPteInfo(...)
```

And it still implements the method:

```text
/home/josh/containers/temp/nvdrv/integ/gpu_drv/stage_rel/drivers/resman/src/kernel/gpu/mem_mgr/dma.c:915-962
deviceCtrlCmdDmaSetPteInfo_IMPL(...)
  vaspaceGetByHandleOrDeviceDefault(...)
  vaspaceSetPteInfo(...)
```

The older `dev` tree has the same non-privileged control definition:

```text
/home/josh/containers/temp/nvdrv/dev/gpu_drv/stage_rel/drivers/resman/config/ctrldefs/ctrl0080.def:111-115
NV0080_CTRL_CMD_DMA_SET_PTE_INFO
FLAGS => ':NO_STATIC:NON_PRIVILEGED'

/home/josh/containers/temp/nvdrv/dev/gpu_drv/stage_rel/drivers/resman/kernel/dma/nv/dma.c:1081-1124
nv0080CtrlCmdDmaSetPteInfo(...)
```

Interpretation: this is the strongest source-level candidate for `CVE-2026-24190` in the local code. A non-privileged RM control that reaches `vaspaceSetPteInfo()` is exactly the kind of kernel-mode authorization surface that can be described publicly as improper access to GPU resources. It is still not evidence for FECS `0x409664` register access; it is a GPU virtual-address/PTE control path.

### Other May 2026 Patch Clusters

The same public `580.142` to `580.159.03` diff contains other clusters that line up with other CVEs in the pasted bulletin:

```text
nv-mmap.c / nv-usermap.c / nv.c:
  mmap_context valid load/store changed to acquire/release
  allocation usage_count retained and released around mmap_context
  likely aligns with use-after-free or memory-order race CVEs

fabric_vaspace.c:
  size/pageSize is now checked before narrowing to NvU32
  likely aligns with incorrect numeric conversion / heap overflow

mem_fabric_import_ref.c:
  numPfns is now bounded against pfnArray length
  likely aligns with input validation / bounds CVEs

uvm_va_range.c:
  DEVICE_P2P range teardown now checks that the range belongs to the unregistering GPU
  likely aligns with UVM/P2P resource lifetime or input-validation issues

uvm_hmm.c / uvm_va_block.c:
  HMM mapping permissions are now clamped to CPU PTE read/write state
  likely aligns with UVM/HMM authorization or input-validation issues
```

This means the local tree likely contains multiple May 2026 bulletin pre-fix shapes, not just one. The DMA PTE export is the best fit for `CVE-2026-24190`; the HMM diff remains important but is no longer the sole attribution candidate.

## CVE-2025-33219 Lead

Official NVIDIA source:

```text
https://nvidia.custhelp.com/app/answers/detail/a_id/5747
Security Bulletin: NVIDIA GPU Display Drivers - January 2026
Updated: 2026-04-02
```

NVIDIA describes `CVE-2025-33219` as a Linux NVIDIA kernel-module integer overflow or wraparound issue:

```text
CVSS v3.1: 7.8 High
CWE: CWE-190 Integer Overflow or Wraparound
Linux R580 fixed version: 580.126.09
Reporter: Sam Lovejoy and Valentina Palmiotti
```

Local host status checked on 2026-05-20:

```text
nvidia-smi reports driver 580.142
```

Therefore the running host driver is newer than the January 2026 R580 fixed version. The local nvdrv source tree, however, still contains pre-fix source shapes.

The strongest public source-diff candidate is the range utility patch between public tags:

```text
580.105.08 -> 580.126.09
src/nvidia/inc/libraries/utils/nvrange.h
```

Affected public shape:

```text
rangeSplit(...)
  pSecondPartAfterSplit->hi = pBigRange->hi
  pBigRange->hi = rangeToSplit.lo
  pSecondPartAfterSplit->lo = rangeToSplit.hi + 1
```

Fixed public shape:

```text
rangeSplit(...)
  if pBigRange->lo == rangeToSplit.lo:
      left range becomes empty
  else:
      pBigRange->hi = rangeToSplit.lo - 1

  if pSecondPartAfterSplit->hi == rangeToSplit.hi:
      right range becomes empty
  else:
      pSecondPartAfterSplit->lo = rangeToSplit.hi + 1
```

This is an explicit underflow/wraparound hardening patch around the exact operations named by the CVE class.

The local `integ` tree still has the affected arithmetic:

```text
/home/josh/containers/temp/nvdrv/integ/gpu_drv/stage_rel/drivers/resman/inc/libraries/utils/nvrange.h:261-272
pSecondPartAfterSplit->hi = pBigRange->hi
pBigRange->hi = rangeToSplit.lo
pSecondPartAfterSplit->lo = rangeToSplit.hi + 1
```

The local `dev` tree also has the affected arithmetic:

```text
/home/josh/containers/temp/nvdrv/dev/gpu_drv/stage_rel/drivers/common/shared/inc/utils/nvrange.h:248-259
pSecondPartAfterSplit->hi = pBigRange->hi
pBigRange->hi = rangeToSplit.lo
pSecondPartAfterSplit->lo = rangeToSplit.hi + 1
```

The same public diff also changes the coherent CPU FB mapping path:

```text
src/nvidia/src/kernel/gpu/mem_sys/kern_mem_sys.c
  old: fbSize = fbTotalMemSizeMb << 20
  old: coherentCpuFbEnd = coherentCpuFbBase + fbSize
  old: numaOnlineSize = KMEMSYS_FB_NUMA_ONLINE_SIZE(fbSize - totalRsvdBytes, ...)

  new: uses coherentCpuFbSize from OS/VBIOS where possible
```

And it introduces `rangesCarveout()` for GH100 coherent CPU mapping, backed by the fixed `rangeSplit()` helper:

```text
src/nvidia/src/kernel/gpu/bus/arch/hopper/kern_bus_gh100.c
  reservedRegions[0] = rangeMake(start, end - 1)
  rangesCarveout(... wprRegions ...)
```

Interpretation: `CVE-2025-33219` most likely corresponds to January 2026 hardening around RM range/coherent-memory arithmetic, with `nvrange.h::rangeSplit()` as the clearest CWE-190 patch signature. The local source contains the pre-fix helper. The running host driver version does not appear affected by this January issue because it is newer than NVIDIA's fixed R580 version.

## NVIDIA Driver CVE Retrospective

The May 2026 bulletin is only the newest public clue. A broader pass over NVIDIA GPU Display Driver bulletins from February 2024 through May 2026 shows several recurring vulnerability classes:

```text
RM/kernel authorization mistakes
RM exported-control permission mistakes
UVM/HMM/P2P mapping permission mistakes
mmap/usermap lifetime and memory-ordering mistakes
integer/range arithmetic mistakes
vGPU guest-to-host resource isolation mistakes
display/video/parser memory safety mistakes
```

Official bulletin sources checked:

```text
February 2024: https://nvidia.custhelp.com/app/answers/detail/a_id/5520
June 2024:     https://nvidia.custhelp.com/app/answers/detail/a_id/5551
July 2024:     https://nvidia.custhelp.com/app/answers/detail/a_id/5557
October 2024:  https://nvidia.custhelp.com/app/answers/detail/a_id/5586
January 2025:  https://nvidia.custhelp.com/app/answers/detail/a_id/5614
April 2025:    https://nvidia.custhelp.com/app/answers/detail/a_id/5630
July 2025:     https://nvidia.custhelp.com/app/answers/detail/a_id/5670
October 2025:  https://nvidia.custhelp.com/app/answers/detail/a_id/5703
January 2026:  https://nvidia.custhelp.com/app/answers/detail/a_id/5747
May 2026:      https://nvidia.custhelp.com/app/answers/detail/a_id/5821
```

Host exposure split:

```text
Running host driver: 580.142

January 2026:
  R580 Linux fixed at 580.126.09
  Host 580.142 is newer, so the running host should contain that fix.

May 2026:
  Tesla/RTX Linux R580 fixed at 580.159.03
  Host 580.142 is older, so the running host is in the affected range.

Older 2024/2025 bulletins:
  Host 580.142 is newer than the listed fixed Linux branches.
  They remain useful as source-pattern leads, not as live-host exposure claims.
```

Public open-gpu-kernel-modules diff pairs checked:

```text
550.78      -> 550.90.07    June 2024-class fixed branch
550.120     -> 550.127.05   October 2024-class fixed branch
550.127.05  -> 550.144.03   January 2025-class fixed branch
550.144.03  -> 550.163.01   April 2025-class fixed branch
570.158.01  -> 570.172.08   July 2025-class fixed branch
580.82.07   -> 580.95.05    October 2025-class fixed branch
580.105.08  -> 580.126.09   January 2026 fixed branch
580.142     -> 580.159.03   May 2026 fixed branch
```

The most relevant older findings are:

```text
April 2025, CVE-2025-23244:
  Official class:
    Linux unprivileged escalation / improper authorization.

  Public diff shape:
    550.144.03 -> 550.163.01
    kernel-open/nvidia/nv-mmap.c
      nvidia_vma_access() now rejects write access if mmap_context is not writable.
    kernel-open/nvidia/nv.c
      open-file tracking moved under mmap_lock and tied to successful usage_count ownership.
    kernel-open/nvidia-uvm/uvm_migrate.c
      HMM migration atomic mapping behavior is tightened.

  Relevance:
    This is an authorization/mapping class, but it is mmap/UVM-facing.
    It does not show direct BAR0, regops, FECS, or 0x409664 access.

July 2025, CVE-2025-23277 / CVE-2025-23278 / CVE-2025-23279 / CVE-2025-23286:
  Official class:
    Memory outside permitted bounds, improper index validation, installer race,
    invalid memory read.

  Public diff shape:
    570.158.01 -> 570.172.08
    Changed files include UVM HMM block eviction state, dma-buf attachment checks,
    mmap lock helpers, and large platform/GSP/MIG updates.

  Relevance:
    The best match is memory/resource bounds hardening.
    It does not currently look like a FECS/register-access path.

October 2025, CVE-2025-23280 / CVE-2025-23282 / CVE-2025-23300 /
CVE-2025-23330 / CVE-2025-23332 / CVE-2025-23345:
  Official class:
    Linux UAF, race escalation, null dereferences, video-decoder OOB read.

  Public diff shape:
    580.82.07 -> 580.95.05
    kernel-open/nvidia-uvm/uvm_pmm_gpu.c
      devmem / device-p2p page ownership and cleanup changes.
    generated display/subdevice control tables changed.

  Relevance:
    The race/UAF class could produce kernel compromise in theory, but the public
    patch shape does not point to a documented user-space path for legal register
    access to FECS speed-select state.

January 2026, CVE-2025-33219:
  Official class:
    Linux kernel-module integer overflow or wraparound.

  Public diff shape:
    580.105.08 -> 580.126.09
    src/nvidia/inc/libraries/utils/nvrange.h
      rangeSplit() now handles empty left/right ranges before subtract/add.
    mem_sys / GH100 coherent CPU FB mapping arithmetic also changed.

  Relevance:
    Strong source match for the CVE class.
    Useful for host security triage, not for GV100 FECS access.

May 2026, CVE-2026-24190:
  Official class:
    Kernel-mode improper access to GPU resources / missing authorization.

  Public diff shape:
    580.142 -> 580.159.03
    NV0080_CTRL_CMD_DMA_SET_PTE_INFO export removed.
    DMA_GET_PTE_INFO flags changed from 0x10008 to 0x100008.
    Removed implementation reached vaspaceSetPteInfo().

  Relevance:
    This is the closest source-level "full access" style finding so far,
    because it is an exported, non-privileged RM control reaching GPU VA/PTE state.
    It still points at GPU virtual-address/PTE control, not at FECS BAR0/regops.

May 2026, CVE-2026-24194 / CVE-2026-24195 and related UVM findings:
  Official class:
    Linux UVM improper input validation.

  Public diff shape:
    580.142 -> 580.159.03
    uvm_hmm_compute_mapping_prot() added.
    HMM mappings are clamped to CPU PTE read/write state.

  Relevance:
    Strong HMM/UVM permission finding.
    It is not evidence for changing the FECS speed-select register.
```

Interpretation for the limiter project:

```text
The older CVEs do help, but mainly by showing where NVIDIA has historically
made authorization mistakes:

  RM exported control tables
  UVM/HMM mapping permissions
  mmap/usermap lifetime
  vGPU guest/host resource isolation

The strongest directly relevant search pattern is therefore:

  RMCTRL_EXPORT / generated NVOC method tables
  NON_PRIVILEGED -> restricted or removed controls
  method entries removed from g_*_nvoc.c
  controls that reach physical mappings, GPU VA/PTE state, or register access

So far, the only high-confidence "improper GPU resource access" source candidate
is the May 2026 DMA_SET_PTE_INFO removal. It is valuable, but it does not solve
the GV100 FECS limiter. The production blocker for 0x409664 remains the RM
regops/user-access-map path:

  gpuValidateRegOffset_IMPL -> NV_ERR_INSUFFICIENT_PERMISSIONS
  gpuValidateRegOps        -> public INVALID_OFFSET result
  GV100 user/global maps   -> deny 0x409660/0x409664
```

Practical consequence:

```text
If the goal is "find a legitimate debug path to FECS speed-select," keep
backtracking NVIDIA's internal/debug authorization surfaces:

  regops access-map generation
  MODS/RegHal feature gates
  RMCTRL exported method flags
  internal-only controls around GR/FECS/ctxsw state
  chipfuse/IFF producer inputs

If the goal is host security triage, update the running driver past 580.159.03.
```

## Full-Access CVEs Versus FECS Access

A broader "full access" search must include the NVIDIA software stack around the GPU, not only the display-driver RM path.

Officially confirmed high-impact stack-level examples:

```text
CVE-2025-23266
  Product: NVIDIA Container Toolkit / GPU Operator
  Bulletin: https://nvidia.custhelp.com/app/answers/detail/a_id/5659
  Class: container hook elevated-code execution
  Severity: 9.0 Critical
  Fixed: NVIDIA Container Toolkit 1.17.8, GPU Operator 25.3.2

CVE-2024-0132
  Product: NVIDIA Container Toolkit / GPU Operator
  Bulletin: https://nvidia.custhelp.com/app/answers/detail/a_id/5582
  Class: TOCTOU container escape / host filesystem access
  Severity: 9.0 Critical
  Fixed: NVIDIA Container Toolkit 1.16.2, GPU Operator 24.6.2

CVE-2025-33219
  Product: Linux NVIDIA Display Driver / vGPU guest driver
  Bulletin: https://nvidia.custhelp.com/app/answers/detail/a_id/5747
  Class: integer overflow or wraparound
  Severity: 7.8 High
  Fixed Linux R580 display driver: 580.126.09

CVE-2025-33220
  Product: NVIDIA vGPU Manager
  Bulletin: https://nvidia.custhelp.com/app/answers/detail/a_id/5747
  Class: malicious guest can trigger heap use-after-free in vGPU Manager
  Severity: 7.8 High
```

Important corrections to keep the map precise:

```text
CVE-2025-33217 and CVE-2025-33218:
  Windows display-driver issues in the January 2026 bulletin.
  They are not Linux display-driver findings.

CVE-2025-33220:
  vGPU Manager issue.
  Relevant to hosts running NVIDIA vGPU software, not to ordinary bare-metal
  /dev/nvidiactl access unless that vGPU stack is present.

Container Toolkit CVEs:
  Can yield host-level compromise in containerized GPU environments.
  They do not imply direct access to GV100 FECS speed-select registers.
```

Local host package check on 2026-05-20:

```text
/usr/bin/nvidia-container-cli exists
/usr/bin/nvidia-ctk exists
nvidia-container-toolkit      1.19.0-1
nvidia-container-runtime      installed
nvidia-container-runtime-hook installed
```

Interpretation:

```text
The local NVIDIA Container Toolkit is newer than both fixed versions:
  CVE-2024-0132 fixed at 1.16.2
  CVE-2025-23266 fixed at 1.17.8

So the container escape class is important for cluster threat modeling, but it
does not appear to be a live local exposure from the package version alone.
```

Relationship to the GV100 limiter:

```text
Host/root/hypervisor access is not the same as FECS register authorization.

Even full host privilege still runs into separate GPU-side access controls:
  production RM user/global access maps
  PGRAPH/FECS privilege level masks
  GR/FECS engine power/reset state
  signed firmware and SKU/fuse policy

This is why the most relevant CVE/source-diff pattern for the limiter remains
RM resource authorization, especially exported controls that touch register,
mapping, or GPU-VA state. Container and vGPU CVEs are serious security findings,
but they are not direct evidence for clearing `0x409664`.
```

## Historical Register-Access CVE Leads

The older CVE lead that most closely matches "user-mode clients reach privileged GPU control paths" is not `CVE-2021-1051`; it is `CVE-2021-1052`.

Official January 2021 NVIDIA bulletin:

```text
https://nvidia.custhelp.com/app/answers/detail/a_id/5142
```

Relevant official descriptions:

```text
CVE-2021-1051:
  Windows only.
  DxgkDdiEscape lets a local user get elevated privileges to modify display
  configuration data.

CVE-2021-1052:
  Windows and Linux.
  DxgkDdiEscape or IOCTL allows user-mode clients to access legacy privileged
  APIs.
  Impacts: denial of service, escalation of privileges, information disclosure.

CVE-2021-1056:
  Linux.
  nvidia.ko does not completely honor OS file-system permissions for GPU
  device-level isolation.
```

Local source has the corresponding legacy-control vocabulary:

```text
integ/gpu_drv/stage_rel/drivers/resman/interface/deprecated/rmapi_deprecated.h

RM_GSS_LEGACY_MASK                   0x00008000
RM_GSS_LEGACY_MASK_NON_PRIVILEGED    0x00008000
RM_GSS_LEGACY_MASK_PRIVILEGED        0x0000C000
```

And the GSS legacy forwarding path:

```text
integ/gpu_drv/stage_rel/drivers/resman/interface/deprecated/rmapi_gss_legacy_control.c

RmGssLegacyRpcCmd(...)
  if command has RM_GSS_LEGACY_MASK_PRIVILEGED
  and caller privLevel < RS_PRIV_LEVEL_USER_ROOT
      return NV_ERR_INSUFFICIENT_PERMISSIONS

  copy params from user
  acquire GPU lock
  forward command to GPU_GET_PHYSICAL_RMAPI(pGpu)->Control(...)
```

Interpretation:

```text
CVE-2021-1052 is directly relevant as a historical class:
  user-mode -> legacy privileged API -> GPU/RM control path.

It does not name GV100 FECS, but it supports searching the local tree for:
  GSS legacy controls
  opaque privileged / non-privileged FINN interfaces
  legacy privileged API masks
  RM control forwarding to physical RM/GSP
```

The local tree also exposes two register-op-adjacent surfaces that should not be conflated:

```text
1. Debugger regops:

integ/gpu_drv/stage_rel/drivers/resman/interface/rmapi/finn/ctrl/ctrl83de/ctrl83dedebug.finn
  NV83DE_CTRL_CMD_DEBUG_EXEC_REG_OPS

integ/gpu_drv/stage_rel/drivers/resman/inc/kernel/gpu/gr/kernel_sm_debugger_session.h
  RMCTRL_EXPORT(NV83DE_CTRL_CMD_DEBUG_EXEC_REG_OPS,
                RMCTRL_FLAGS(NON_PRIVILEGED, ROUTE_TO_VGPU_HOST))

integ/gpu_drv/stage_rel/drivers/resman/inc/physical/gpu/gr/sm_debugger_session.h
  NV83DE_CTRL_CMD_INTERNAL_EXEC_REG_OPS
  RMCTRL_FLAGS(INTERNAL)

Meaning:
  Public debugger regops are non-privileged as an RM control surface, but the
  actual register offsets still pass through register validation/access policy.
  Internal regops exist, but are marked INTERNAL.

2. vGPU VF regops:

integ/gpu_drv/stage_rel/drivers/resman/interface/rmapi/finn/ctrl/ctrla082.finn
  NVA082_CTRL_CMD_HOST_VGPU_DEVICE_EXEC_VF_REG_OPS
  READ_32 / WRITE_32

integ/gpu_drv/stage_rel/drivers/resman/kernel/inc/hostvgpudeviceapi.h
  RMCTRL_EXPORT(NVA082_CTRL_CMD_HOST_VGPU_DEVICE_EXEC_VF_REG_OPS,
                RMCTRL_FLAGS(NON_PRIVILEGED, COPYOUT_ON_ERROR))

integ/gpu_drv/stage_rel/drivers/resman/kernel/vgpu/nv/hostvgpudeviceapi.c
  hostvgpudeviceapiValidateVfRegOps(...)
  hostVgpuDeviceExecVfRegOps(...)

integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/arch/ampere/gpu_ga100.c
  gpuValidateVfRegOffset_GA100(...)
  gpuGetVfAliasRegOffset_GA100(...)
```

The visible GA100 vGPU path whitelists only VF aliases for:

```text
MMU fault buffer registers
MMU TLB invalidate registers
access-counter notify buffer size
```

Interpretation:

```text
The vGPU VF regop surface is real and non-privileged inside its resource model,
but the visible implementation is not a general BAR0 write primitive and does
not map to GV100 FECS `0x409664`.
```

Other pasted CVEs:

```text
CVE-2022-31606:
  Windows only.
  NVIDIA GPU Display Driver nvlddmkm.sys DxgkDdiEscape validation failure
  leading to kernel-mode out-of-bounds access.
  Useful as another Windows escape/class lead, not a Linux GV100 host lead.

CVE-2025-23352:
  vGPU Manager uninitialized pointer access.
  Official October 2025 bulletin:
    https://nvidia.custhelp.com/app/answers/detail/a_id/5703
  Relevant to hosts actually running NVIDIA vGPU Manager.
  The local source has vGPU register-operation plumbing, but the visible VF
  regops are whitelisted MMU/access-counter aliases, not FECS speed-select.
```

Current conclusion from the historical CVE pass:

```text
Most useful new search branch:
  CVE-2021-1052 -> legacy privileged APIs -> GSS legacy masks and opaque
  privileged RM control interfaces.

Most useful local source branch:
  RmGssLegacyRpcCmd and the FINN legacy privileged/non-privileged interfaces.

Still not shown:
  a production non-internal control that can write GV100
  NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT at 0x409664.
```

### Legacy/Opaque RM Control Branch

The `CVE-2021-1052` lead justified checking the local legacy and opaque RM
control surfaces for a hidden GR/FECS path. The visible FINN control categories
are:

```text
integ/gpu_drv/stage_rel/drivers/resman/interface/rmapi/finn/ctrl/ctrl2080/ctrl2080base.finn

NV2080_CTRL_GPU_LEGACY_NON_PRIVILEGED
NV2080_CTRL_CLK_LEGACY_PRIVILEGED
NV2080_CTRL_CLK_LEGACY_NON_PRIVILEGED
NV2080_CTRL_PERF_LEGACY_PRIVILEGED
NV2080_CTRL_PERF_LEGACY_NON_PRIVILEGED
NV2080_CTRL_PMGR_LEGACY_NON_PRIVILEGED
NV2080_CTRL_VOLT_LEGACY_PRIVILEGED
NV2080_CTRL_VOLT_LEGACY_NON_PRIVILEGED
```

Notably absent:

```text
GR_LEGACY_PRIVILEGED
GR_LEGACY_NON_PRIVILEGED
FECS_LEGACY_PRIVILEGED
FECS_LEGACY_NON_PRIVILEGED
```

The GSS legacy forwarding path is also narrower than a generic local escape
hatch:

```text
integ/gpu_drv/stage_rel/drivers/resman/interface/deprecated/rmapi_deprecated_control.c:153
  if (pGpu && IS_GSP_CLIENT(pGpu) && IsGssLegacyCall(pArgs->cmd))
      return RmGssLegacyRpcCmd

integ/gpu_drv/stage_rel/drivers/resman/interface/deprecated/rmapi_gss_legacy_control.c:36
  if privileged GSS legacy command and caller privLevel < RS_PRIV_LEVEL_USER_ROOT
      return NV_ERR_INSUFFICIENT_PERMISSIONS
```

Interpretation:

```text
The legacy opaque interfaces are useful for reconstructing the historical
CVE-2021-1052 class, but the visible local control taxonomy does not include a
GR/FECS opaque category. GSS legacy forwarding requires GSP-client routing and
root-level RM privilege for privileged commands.
```

### Perf And Debug Controls Are Not The Tensor Issue-Rate Limiter

Several nearby performance/debug controls looked tempting, but the source
separates them from FECS speed select.

`CUDA_LIMIT` is a P-state limiter:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/perfctl/nv/cuda_limit.c:41-84
  DevinitGetCudaLimitTable(...)
  find maximum P-state with PSTATE_FLAGS_CUDA_SAFE

dev/gpu_drv/stage_rel/drivers/resman/kernel/perfctl/nv/cuda_limit.c:145-194
  perfCudaLimitEvaluateLimit(...)
  set NV2080_CTRL_PERF_PERF_LIMIT_ID_CUDA_MAX
```

The GV100-specific CUDA_LIMIT patch is board-table cleanup / override support
for Titan V, not HMMA/FMLA issue-rate control:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/perfctl/volta/perfgv100.c:246-286
  perfPatchCudaLimitTable_GV100(...)
  Patches errors in the VBIOS CUDA_LIMIT table for TITAN V GV100
  pCudaLimit->bLimitApplyOverrideSupported = NV_TRUE
```

`PERF_DEBUG_MODE` is clock/P-state debug mode:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/perfctl/nv/debug_mode.c:130-138
  get clkDomainsDebugModeEnabled(...)

dev/gpu_drv/stage_rel/drivers/resman/kernel/perfctl/nv/debug_mode.c:300-373
  reset user clock offsets
  perfPstateSetDebugModeVpstates_3X(...)
  update PMU perf tables
```

`GPU_SET_GPU_DEBUG_MODE` is not a register-access mode:

```text
integ/gpu_drv/stage_rel/drivers/resman/interface/rmapi/finn/ctrl/ctrl2080/ctrl2080gpu.finn:2371-2376
  while enabled, timeout-prone RM calls return NV_ERR_BUSY_RETRY
```

`RMPrivSecurity` is the closest-looking register-security knob, but its own
comments and HAL descriptions limit it to Tegra bring-up/debug-build use:

```text
dev/gpu_drv/stage_rel/drivers/resman/interface/nvRmReg.h:7513-7526
  Disable priv security.
  Only available in Tegra development environments...
  Not supported in FMODEL & SILICON production boards.

dev/gpu_drv/stage_rel/drivers/resman/config/haldefs/gpu.def:1883-1896
  ENABLE_PRIV_PROTECTED_REGISTERS / PRIV_SEC_FORCE_*:
  Only for use in Tegra bringups in debug builds
```

`NV0080_CTRL_DMA_ENABLE_PRIVILEGED_RANGE` is also unrelated to BAR0/PRI/FECS
authorization:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/dma/nv/dma.c:1637-1662
integ/gpu_drv/stage_rel/drivers/resman/interface/rmapi/finn/ctrl/ctrl0080/ctrl0080dma.finn:915-929

It marks a client VASpace for a privileged kernel address-space mirror before
any VA allocations exist. It does not grant general MMIO register access.
```

Interpretation:

```text
The visible local debug/perf controls are real, but they do not target the GV100
FECS speed-select limiter. The durable debug path remains the internal
RMOverrideSmSpeedSelect* / RMSchMicroSched family, and the visible apply path is
Turing/Ampere, not production GV100.
```

## Next Safe Backtracking Steps

1. Compare the checked-in GV100 `user_access_map_gzip.h` generation inputs against current `regaccess/config/common.yml` history if a fuller Perforce/source history becomes available.
2. Acquire the missing GV100 chipfuse/IFF runtime inputs, either `gv100_f.json` or encrypted `gv100_f.jsone` plus `gv100_POR.json` if available, and inspect `IFF_patches`, `Mkt_options`, POR sidecar data, and any row that targets FECS speed-select state.
3. Search a fuller FECS/CTXSW firmware source drop for `FEATURE_READOUT_SM_SPEED_SELECT`, `FMLA_REDUCED_SPEED`, or writes to FECS feature override/readout storage.
4. Compare GV100 NETD ctx-state image/register defaults between capped hardware and a full-speed V100 in a read-only maintenance environment.
5. If read-only register access becomes available, compare the GV100 FECS readout value between a capped CMP-derived card and a known full-speed V100.
6. Compare InfoROM `OBD`/`OEM`/`PWR` object contents between local capped cards and a full-speed V100 baseline if a provider allows debugdump or InfoROM object queries.
7. Search any fuller NVIDIA source drop for the PUB PLM list referenced by the local SBR docs, especially entries naming `feature_override_quadro`, `NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK`, or CTXSW FECS/GPCCS PLM lowering rows.

## 40. Firmware-Level CVE Analysis: VBIOS Flash, FECS Microcode, And Related Vectors

### 40.1 Scope And Motivation

After exhausting all software unlock paths for clearing the GV100 CMP 100-210
`SM_SPEED_SELECT` override (`0x00409664`, value `0x00000999` to target
`0x00000000`), this section surveys CVEs and public research that could
enable firmware-level attack vectors:

1. VBIOS flash (software-based firmware update on the GPU)
2. FECS microcode exploit (load custom FECS Falcon firmware)
3. Other firmware-level attack vectors

### 40.2 VBIOS Flash CVEs

#### CVE-2019-5688 (November 2019) -- NVFlash Kernel-Mode Driver

- **Scope**: NVIDIA NVFlash kernel-mode driver (`nvflash.sys` /
  `nvflsh32.sys` / `nvflsh64.sys`).
- **CVSS**: Not assigned (NVIDIA SB201911263, Windows only).
- **Impact**: Authenticated admin users could access device memory and
  registers of other devices not managed by NVIDIA.
- **Relevance to GV100**: **None.** Windows-only kernel driver issue.
  Does not apply to Linux GV100, does not bypass VBIOS signature
  verification, and allows access to *other* devices' memory/registers,
  not manipulation of the local GPU VBIOS.

#### CVE-2024-0141 (March 2025) -- HGX Hopper vBIOS Register Write

- **Scope**: Hopper HGX (H100/H200 baseboard) GPU vBIOS.
- **CVSS 6.8** (medium).
- **Impact**: "GPU vBIOS contains a vulnerability that may allow a
  malicious actor with tenant level GPU access to write to an unsupported
  registry causing a bad state." This is a denial-of-service finding,
  not an unlock path.
- **Relevance to GV100**: **None.** Hopper HGX only. Even on Hopper,
  it is a DoS finding, not a mechanism to bypass VBIOS signature
  verification or inject arbitrary firmware.

#### nvflashk (Third-Party Tool)

- Not a CVE, but the most relevant existing tool for VBIOS flashing.
- Patches NVIDIA nvflash to bypass Board ID checks.
- **Critical limitation**: Does **not** bypass VBIOS cryptographic
  signature verification. The flashed BIOS must still be signed by
  NVIDIA with a valid RSA-1K certificate chain.
- For GV100 CMP 100-210, there is no publicly known signed donor BIOS
  that would clear the `SM_SPEED_SELECT` override.

### 40.3 VBIOS Security Architecture (Linux)

The VBIOS flash path on Linux is entirely RM-internal. Key files:

- `drivers/resman/kernel/pmgr/fermi/spidev_rom_gf100.c` -- SPI bit-banged
  ROM programming (page program, sector erase, write protect). Functions
  like `_pmgrSpiDevicePageProgram_ROM()` are RM-internal and not
  exposed via any user-accessible RM IOCTL.
- `drivers/resman/kernel/devinit/maxwell/vbiossecuritygm200.c` -- VBIOS
  image signature verification using RSA-1K certificates and chain-of-trust
  validation from a root certificate built into the GPU boot ROM.
  HULK license injection and FWSECLIC Falcon firmware loading are also
  internal to RM stateLoad paths.

**No user-space SPI interface** is exposed via RM IOCTL. The SPI ROM
programming routines are only reachable through RM kernel code during
devinit or VBIOS update flows that require prior authenticated access.

### 40.4 FECS / Falcon Microcode CVEs

**No CVEs found** that provide a mechanism to load unsigned Falcon
microcode on production (fused) GPUs. The FWSECLIC/HULK loading path
(via `_vbiosFwseclicCmdOffloadToFlcn()` and `vbiosHandleHulk_GM200()`)
requires:

1. Firmware binary embedded in a signed VBIOS image.
2. Signed VBIOS passing cryptographic chain-of-trust verification.
3. Firmware loaded through RM-internal paths (no user-facing "load
   custom Falcon firmware" IOCTL).

Key functions in the load path:
- `_vbiosFwseclicCmdOffloadToFlcn()` (line 1213 in
  `vbiossecuritygm200.c`): Loads the FWSECLIC firmware binary into a
  Falcon (PMU/SEC2/GSP) using bootloader descriptor, loading both
  secure and non-secure code sections into Falcon IMEM/DMEM.
- `vbiosProcessFwseclicCommand_GM200()` (line 1408): Coordinates ucode
  descriptor parsing, buffer allocation, and command-specific payloads.
- `vbiosHandleHulk_GM200()` (line 1703): Processes HULK license binary
  on PMU/SEC2/GSP. If HULK fails, RM warns with `DBG_BREAKPOINT()` but
  **does not fail** stateLoad -- the GPU continues initializing.

### 40.5 VBIOS Signature Bypass Status

The VBIOS image is signed with RSA-1K, verified against a root
certificate built into the GPU boot ROM.

**No publicly known CVE bypasses RSA-1K VBIOS signature verification**
on production-fused GV100 silicon.

### 40.6 May 2026 Bulletin CVEs (SB2026051922)

The May 2026 NVIDIA Security Bulletin lists several CVEs that were
examined for firmware-level exploit potential:

| CVE | CWE | CVSS | Linux? | Relevance |
|-----|-----|------|--------|-----------|
| CVE-2026-24190 | CWE-862 Missing Authorization | 7.8 | Yes | Analyzed: `NV0080_CTRL_CMD_DMA_SET_PTE_INFO` non-privileged. Cannot reach FECS register range. |
| CVE-2026-24193 | CWE-787 OOB Write | 7.8 | Yes | OOB write in display driver. Could enable LPE, but no direct FECS path. Would need specific OOB targeting FECS register range. |
| CVE-2026-24192 | CWE-681 Numeric Conversion | 7.8 | Yes | Linux heap overflow via numeric conversion. LPE vector, no direct FECS path. |
| CVE-2026-24187 | CWE-416 Use-After-Free | 7.8 | Yes | UAF in Linux GPU Display Driver. LPE vector, no direct FECS path. |

### 40.7 Fundamental Blocker

The FECS `SM_SPEED_SELECT` register at **`0x00409664`** has a
**hardware-level PRI decode write-enable gate** that drops writes after
VBIOS/PMU boot completes. This is not a software-controllable lock --
it is a physical hardware write-lock implemented in the PRI decode
logic of the FECS engine.

**No software CVE can bypass a physical hardware write-lock.** The
register's value is set once during boot (from VBIOS init scripts or
chipfuse override), and subsequent writes are silently dropped by the
hardware decode logic regardless of privilege level or RM capabilities.

### 40.8 Remaining (Non-CVE) Vectors

These vectors do not have associated CVEs but are listed for completeness:

1. **Physical VBIOS flash** -- Requires CH341A programmer + SOIC clip,
   board disassembly, and a signed donor VBIOS that does not set the
   `SM_SPEED_SELECT` override.
2. **Falcon microcode replacement via ACR (Authenticated Code
   Replacement) bypass** -- Requires finding an ACR vulnerability that
   accepts unsigned firmware on production (non-debug) GPUs. No such
   vulnerability is publicly known for GV100.
3. **Power glitching / voltage fault injection** -- Physical attack to
   bypass boot ROM signature verification. Requires hardware access,
   oscilloscope, and precise timing injection.
4. **Accept the limitation** -- The GV100 CMP 100-210 `SM_SPEED_SELECT`
   bit appears to be a permanent SKU differentiation that cannot be
   overridden through software or firmware alone.

### 40.9 Summary

| Attack Vector | CVEs Found | Feasible? |
|---------------|-----------|-----------|
| VBIOS flash via CVE | CVE-2019-5688 (Windows only), CVE-2024-0141 (Hopper DoS only) | No. No CVE bypasses RSA-1K VBIOS signing. |
| FECS microcode load via CVE | None | No. No CVE loads unsigned Falcon firmware. |
| VBIOS signature bypass | None publicly known | No. RSA-1K chain-of-trust is intact. |
| Software register write to `0x00409664` | N/A (hardware write-lock) | No. PRI decode gate drops writes post-boot. |
| Physical VBIOS flash | N/A | Requires hardware programmer and signed donor BIOS. |
| Power glitching | N/A | Requires physical access and fault injection equipment. |

### 40.10 Public Falcon Security Research: "Je Ne Sais Quoi" (hexkyz/SciresM, 2021)

The blog post at
[hexkyz.blogspot.com/2021/11/je-ne-sais-quoi-falcons-over-horizon.html](https://hexkyz.blogspot.com/2021/11/je-ne-sais-quoi-falcons-over-horizon.html)
is the definitive public research on NVIDIA Falcon security, covering the
Tegra X1 TSEC (Tegra Security Coprocessor) Falcon unit. Co-authored by
hexkyz and SciresM (creators of the Atmosphère custom firmware).

#### Key Technical Contributions

1. **Falcon Security Architecture Documentation**: Detailed reverse-engineering
   of Falcon security modes (Non-Secure, Light Secure, Heavy Secure), the SCP
   (Secure Co-Processor) block, and the ACR (Authenticated Code Replacement)
   authentication mechanism.

2. **CAUTH Algorithm Break**: Completely reverse-engineered and broke the
   TSEC secure boot ROM authentication algorithm:
   - Discovered SCP crypto register `$c3` through `$c7` contents survive
     the HS mode authentication process (developer forgot to clear them).
   - Recovered the signing key via `csigenc` side-channel: signature-bound
     encryption allows comparing secret register values even when unreadable.
   - Achieved ROP under HS mode using a stack buffer overflow in the HOVI
     firmware's secure stage (the `maconstack` bug).

3. **DMA Async Vulnerability (Universal Falcon Hardware Issue)**: The Falcon
   Secure Boot ROM does not execute `xdwait` on entry, leaving previous
   asynchronous DMA transactions in flight. By scheduling 0x4000 DMA writes
   before invoking ROM, attacker-controlled data overwrites the ROM stack
   (including the trailer block and return addresses), achieving ROP under
   the Secure Boot ROM itself.

4. **Debug Register Exploitation**:
   - `IBRKPT.SKIP` (bit 30): Works on ROM addresses. Setting SKIP on a ROM
     address causes it to be cleared when that instruction executes, enabling
     instruction-location mapping without code access.
   - `TRACEPC`: Records last 8 branch/jump/call/return addresses even in HS
     mode (a hardware bug -- meant to be disabled), providing branch-trace
     information for ROM reverse-engineering.
   - Combined brute-force of SKIP breakpoints and TRACEPC dumps over 2.5
     weeks produced full instruction-size map of ROM.

5. **Universal Applicability to SCP-Equipped Falcons**: The article confirms:
   "Because this is a (unmitigable!) hardware issue in all Falcons which have
   SCP, not just TSEC -- we were also able to use the same attack on the
   Falcon unit used for GPU power management [PMU], recovering its (different)
   signing key as well."

6. **Arbitrary HS Code Execution**: Final exploit achieves HS mode entry with
   signature = arbitrary controlled value, completely breaking the TSEC
   security model from a cryptographic perspective. Recovered all keys from
   firmware versions 6.2.0 and 8.1.0.

#### Relevance to GV100 FECS SM_SPEED_SELECT

**Direct applicability is limited** by two fundamental constraints:

1. **FECS is not a secretful Falcon**. FECS (Front-End Context Switch) is a
   Falcon-variant processor but does NOT have SCP (Secure Co-Processor). It
   does not support Heavy Secure or Light Secure modes. The Je Ne Sais Quoi
   attack specifically targets the SCP-based HS mode authentication. FECS
   firmware loading uses ACR (Authenticated Code Replacement), which has a
   different security architecture.

2. **Timing mismatch**. The `SM_SPEED_SELECT` register at `0x00409664` is
   written during VBIOS POST init scripts (before any OS driver loads). The
   hardware PRI decode write-lock engages at boot completion. FECS firmware
   loaded by the RM driver during `grStateLoad_IMPL()` happens after this
   window has closed. Even with custom FECS firmware, the register cannot be
   modified.

**Indirect value**:

- The PMU Falcon on desktop GPUs IS a secretful Falcon (has SCP). If PMU
  firmware controls any aspect of the speed-select gating (unconfirmed), the
  Je Ne Sais Quoi attack could theoretically load custom PMU firmware.
- The DMA async vulnerability pattern (no `xdwait` on ROM entry) is worth
  checking in GV100 ACR firmware loaders. If a similar vulnerability exists
  in the GPU's ACR boot flow, it might allow unsigned firmware injection
  into non-SCP Falcons.
- The public Falcon debug register knowledge (TRACEPC, IBRKPT, SKIP) is
  directly applicable to any Falcon-based research on GV100.

## 41. CSV / Header Reconstruction Of GV100 FECS PLM Candidates

The local CSV files checked so far do not contain NVIDIA's private SEC2/PUB PLM
lowering list. They are mostly benchmark, profiler, topology, and probe output.
No CSV found contained a direct PUB list, `feature_override_quadro`, or a usable
mapping for `0x00409650` / `0x00409664`.

However, the GV100 hwref headers contain enough structured metadata to
reconstruct a candidate PLM map. I added a read-only reconstruction tool:

```text
projects/gpu-falcon-research/tools/reconstruct_gv100_pub_plm_candidates.py
```

Generated outputs:

```text
projects/gpu-falcon-research/runs/20260520-pub-plm-candidate-reconstruction/gv100_priv_level_candidates_dev.csv
projects/gpu-falcon-research/runs/20260520-pub-plm-candidate-reconstruction/gv100_priv_level_candidates_integ.csv
```

The important reconstructed rows from the `integ` GV100 headers are:

```text
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
  register_addr:     0x00409664
  plm_offset:        0x00000650
  resolved_plm_addr: 0x00409650
  plm_name:          NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK

NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_QUADRO
  register_addr:     0x00409654
  plm_offset:        0x00000650
  resolved_plm_addr: 0x00409650
  plm_name:          NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK

NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_ECC
  register_addr:     0x00409658
  plm_offset:        0x0000064c
  resolved_plm_addr: 0x0040964c
  plm_name:          NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_ECC_PRIV_LEVEL_MASK

NV_PGRAPH_PRI_FECS_FEATURE_SECURITY_LOCK
  register_addr:     0x00409674
  plm_offset:        0x00000670
  resolved_plm_addr: 0x00409670
  plm_name:          NV_PGRAPH_PRI_FECS_FEATURE_SECURITY_PRIV_LEVEL_MASK
```

The CTXSW firmware address view carries the same feature group through low
PLM offsets:

```text
NV_CTXSW_FIRMWARE_FEATURE_OVERRIDE_SM_SPEED_SELECT
  register_addr:     0x00019900
  plm_offset:        0x00000650
  resolved_plm_addr: 0x00019650

NV_CTXSW_FIRMWARE_FEATURE_OVERRIDE_QUADRO
  register_addr:     0x00019500
  plm_offset:        0x00000650
  resolved_plm_addr: 0x00019650
```

Interpretation: this is not the actual private PUB list, but it is strong
source-derived reconstruction of the register-side PLMs that such a list would
need to touch for the FECS feature override region. The key result is that
`SM_SPEED_SELECT` and `FEATURE_OVERRIDE_QUADRO` share the same PLM group:

```text
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK = 0x00409650
```

That aligns with the earlier source trail where later code refers to the
shared fuse signal as `feature_override_quadro`. It makes `0x00409650` the
best reconstructed PUB/CTXSW FECS PLM candidate for the limiter-adjacent
feature-override block, while `0x0040964c` and `0x00409670` are neighboring
ECC/security PLM candidates.

## 42. Is The Reconstructed PLM Map Enough?

It is enough to identify the next exact blocker, but not enough to recover the
private PUB runtime list by itself.

Current known state:

| Item | Status | Evidence |
|------|--------|----------|
| Limiter register | Proven | `NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT = 0x00409664` |
| Limiter value | Proven | Live BAR0 readback and VBIOS init both show `0x00000999` |
| Reduced readout | Proven | Live BAR0 `0x00409660` has DP/IMLA/FMLA reduced bits set |
| RM user path | Proven blocked | Production GV100 user/global access maps deny `0x409660` and `0x409664` |
| PLM controlling speed-select | Reconstructed strongly | `0x409664.__PRIV_LEVEL_MASK = 0x650 -> 0x00409650` |
| PLM source-provenance name | Strong inference | Later manuals/fuse models name the shared signal `feature_override_quadro` |
| PUB private lowering list | Still missing | Local SBR docs reference private Confluence/list; not present locally |
| Live PLM value on cards | Still missing | Existing Route B BAR0 report captured `0x409660`/`0x409664`, not `0x409650` |

The exact state matrix we still need is:

```text
0x40964c  NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_ECC_PRIV_LEVEL_MASK
0x409650  NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK
0x409654  NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_QUADRO
0x409658  NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_ECC
0x409660  NV_PGRAPH_PRI_FECS_FEATURE_READOUT
0x409664  NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
0x409670  NV_PGRAPH_PRI_FECS_FEATURE_SECURITY_PRIV_LEVEL_MASK
0x409674  NV_PGRAPH_PRI_FECS_FEATURE_SECURITY_LOCK
```

Existing live data fills only:

```text
0x409660  FEATURE_READOUT = 0x00700100 or 0x007000f3, reduced bits set
0x409664  SM_SPEED_SELECT override = 0x00000999 on all initialized local cards
```

Interpretation: the reconstructed CSV map gets us to the correct PLM block.
The remaining discriminator is a read-only live comparison of `0x409650` and
neighbors on initialized V100-personality and CMP-personality cards. If
`0x409650` reads as a normal PLM value, the blocker is privilege policy. If it
reads all-ones while nearby validation registers are live, the feature-override
subrange may be gated below the PLM layer. If it differs between card
personalities, it becomes a direct candidate for the board/SKU policy split.

## 43. Backtracking The PUB PLM Source Finds A Negative Match

The local tree does contain actual PUB PLM lowering source for TU10x:

```text
integ/gpu_drv/stage_rel/uproc/spark/ucode/pub/spark_code/src/common/nv/plm_lowering.adb
integ/gpu_drv/stage_rel/uproc/spark/ucode/pub/spark_code/src/common/tu10x/update_fecs_plms_tu10x.adb
integ/gpu_drv/stage_rel/uproc/spark/ucode/pub/spark_code/src/shared_src/register_whitelist_ucode_specific.ads
```

`Lower_Plms()` calls:

```text
Lower_Fecs_Plms()
Lower_Gpcs_Plms()
Lower_Pmu_Plms()
```

The TU10x `Lower_Fecs_Plms()` routine lowers this FECS-related set:

```text
NV_PGRAPH_PRI_FECS_PVS_PRIV_LEVEL_MASK
NV_PGRAPH_PRI_FECS_RC_LANE_CMD_PRIV_LEVEL_MASK
NV_PGRAPH_PRI_FECS_ARB_WPR_PRIV_LEVEL_MASK
NV_PGRAPH_PRI_FECS_FALCON_MTHDCTX_PRIV_LEVEL_MASK
NV_PGRAPH_PRI_FECS_FALCON_IRQTMR_PRIV_LEVEL_MASK
NV_PGRAPH_PRI_FECS_ARB_FALCON_REGIONCFG_PRIV_LEVEL_MASK
NV_PGRAPH_PRI_FECS_FALCON_EXE_PRIV_LEVEL_MASK
NV_PPRIV_SYS_PRIV_HOLDOFF_PRIV_LEVEL_MASK
NV_PFB_PRI_MMU_PRIV_LEVEL_MASK
NV_FUSE_FLOORSWEEP_PRIV_LEVEL_MASK
```

There is also a commented future item:

```text
NV_PGC6_AON_SECURE_SCRATCH_GROUP_15_PRIV_LEVEL_MASK
```

The PUB register whitelist mirrors the same class of addresses. It allows FECS
PVS, RC lane command, ARB WPR, MTHDCTX, IRQTMR, ARB FALCON REGIONCFG, EXE, plus
GPCS/GPCCS and PMU PLMs. It does not include:

```text
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_ECC_PRIV_LEVEL_MASK
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
NV_PGRAPH_PRI_FECS_FEATURE_READOUT
```

That is a useful negative result. The local PUB source proves that the normal
TU10x PUB PLM-lowering list is not a blanket "lower every CTXSW/FECS PLM"
operation, and it specifically does not lower the FECS feature-override PLM
family.

The feature-override PLM still has a source-level fuse/manual identity:

```text
dev/gpu_drv/stage_rel/tools/pmlsplitter/src/tu101/dev_graphics_nobundle.h

NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK_WRITE_PROTECTION__FUSE_SIGNAL
  "feature_override_quadro"

NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK_WRITE_PROTECTION_ALL_LEVELS_ENABLED_FUSE0
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK_WRITE_PROTECTION_ONLY_LEVEL3_ENABLED_FUSE1
```

The same signal is carried forward in later FUSE-space models:

```text
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/ampere/ga100/dev_fuse.h

NV_FUSE_FEATURE_OVERRIDE_PRIV_LEVEL_MASK_WRITE_PROTECTION__FUSE_SIGNAL
  "feature_override_quadro"
```

GV100 has the same FECS feature-override PLM address, but its older hwref form
does not expose the later `feature_override_quadro` fuse-signal string:

```text
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK = 0x00409650
READ_PROTECTION  = 2:0
WRITE_PROTECTION = 6:4
WRITE_PROTECTION_DEFAULT_PRIV_LEVEL = 0x00000000
```

Interpretation: the missing information is no longer likely to be in the
normal PUB PLM lowering list. The better model is:

```text
0x409650 FECS feature-override PLM
  -> source/fuse signal family: feature_override_quadro
  -> normal PUB TU10x lowering source: does not include it
  -> likely controlled by fuse/manual board policy or an internal/non-PUB path
```

This makes live readback of `0x409650` even more important: it would tell
whether the board is in the all-levels-enabled form, a level-restricted form,
or a lower decode/security state not represented by the normal PUB source.

## 44. Read-Only Live PLM Probe Attempt From Current Container

To avoid using the older write-capable tools, I added a read-only-only BAR0
probe:

```text
projects/gpu-falcon-research/tools/gv100_bar0_fecs_plm_readonly.c
projects/gpu-falcon-research/tools/gv100_bar0_fecs_plm_readonly
```

It opens sysfs `resource0` with:

```text
O_RDONLY | O_SYNC
PROT_READ
```

and targets only this state matrix:

```text
0x0040964c  FECS_FEATURE_OVERRIDE_ECC_PLM
0x00409650  FECS_FEATURE_OVERRIDE_PLM
0x00409654  FECS_FEATURE_OVERRIDE_QUADRO
0x00409658  FECS_FEATURE_OVERRIDE_ECC
0x00409660  FECS_FEATURE_READOUT
0x00409664  FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
0x00409670  FECS_FEATURE_SECURITY_PLM
0x00409674  FECS_FEATURE_SECURITY_LOCK
```

Run output was saved here:

```text
projects/gpu-falcon-research/runs/20260520-fecs-plm-readonly/gv100-fecs-plm-readonly.txt
```

The current container could enumerate all 16 GV100-class devices, but every
`resource0` open failed:

```text
open /sys/bus/pci/devices/<BDF>/resource0 failed: Permission denied
```

The reason is environmental, not a device finding:

```text
/sys/bus/pci/devices/.../resource0 = -rw------- root root
current uid/euid = 1000(josh)
```

Interpretation: the read-only tool is ready, but this container session does
not currently have permission to open BAR0 resources. The missing empirical
value remains live readback of `0x409650`; source backtracking now says this
value is more likely fuse/manual board policy than normal PUB PLM lowering.

## 45. Sudo Read-Only PLM Probe Result

The same read-only BAR0 probe was rerun with sudo. The output is saved here:

```text
projects/gpu-falcon-research/runs/20260520-fecs-plm-readonly/gv100-fecs-plm-readonly-sudo.txt
```

The tool remained read-only:

```text
open(..., O_RDONLY | O_SYNC)
mmap(..., PROT_READ, MAP_SHARED, ...)
```

The first four CMP devices were not live through BAR0 in this state. They
returned all ones even for non-FECS registers:

```text
0000:05:00.0
0000:06:00.0
0000:07:00.0
0000:08:00.0

NV_PMC_BOOT_0      = 0xffffffff
NV_PSTRAP_SYS_CTRL = 0xffffffff
```

That means their `0xffffffff` FECS reads are not evidence of a specifically
disabled FECS feature-override range. The whole BAR0 read path was not
returning initialized device state for those cards.

For initialized GV100-class cards, the live PLM result is consistent across
both CMP identity and V100-personality cards:

```text
FECS_FEATURE_OVERRIDE_PLM 0x00409650 = 0x0000008f
FECS_FEATURE_SECURITY_PLM 0x00409670 = 0x0000008f
FECS_FEATURE_SECURITY_LOCK 0x00409674 = 0x00000000
```

Using the GV100 field layout:

```text
READ_PROTECTION  = bits 2:0
WRITE_PROTECTION = bits 6:4
```

`0x8f` decodes as:

```text
read_mask  = 0x7
write_mask = 0x0
```

So the feature-override PLM itself does not distinguish normal initialized
CMP cards from V100-personality cards in this host state. The earlier source
breadcrumb to `feature_override_quadro` is still useful, but live readback
says `0x409650` is not the reason these initialized cards keep the limiter.

The normal initialized state is:

```text
FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT 0x00409664 = 0x00000999
```

This matches the decoded VBIOS init-script write:

```text
NV_REG R[0x409664] &= 0xfffff666 |= 0x00000999
```

One outlier is important:

```text
GPU 14 / 0000:19:00.0 / Tesla V100-PCIE-12GB personality

FECS_FEATURE_OVERRIDE_PLM             0x00409650 = 0x00000006
FECS_FEATURE_OVERRIDE_ECC             0x00409658 = 0xbadf510e
FECS_FEATURE_READOUT                  0x00409660 = 0x00700100
FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT 0x00409664 = 0x00000000
FECS_FEATURE_SECURITY_PLM             0x00409670 = 0x0000008f
FECS_FEATURE_SECURITY_LOCK            0x00409674 = 0x00000000
```

`nvidia-smi` maps this BDF to GPU 14:

```text
14, 00000000:19:00.0, Tesla V100-PCIE-12GB, GPU-71be175a-2ae4-735d-78e1-6eeeea51fc53
```

That is the same V100-personality comparator previously measured at
approximately `1/16` tensor throughput. Therefore the cleared live override
register on this card is not enough to prove an unlocked state. The readout
register still has the reduced-speed readout bits set:

```text
0x00409660 = 0x00700100
```

Interpretation: `0x409664` is the visible VBIOS-set override command, but the
effective limiter should now be modeled as:

```text
VBIOS / firmware / fuse policy
  -> FECS feature readout latch at 0x409660
  -> reduced tensor/HMMA issue behavior
```

The strongest current discriminator is `0x409660`, not just `0x409664`.
Personality flashing and a cleared current override register can coexist with
the reduced readout state and observed slow tensor throughput.

## 46. Current-State GPU14 Benchmark Rerun Attempt

After finding GPU14's unusual live register state, I tried to rerun the
existing cuBLAS tensor probe against GPU14 in the current host state:

```text
projects/gpu-falcon-research/runs/20260520-tensor-sgemm-sweep/cublas_tensor_probe
```

The direct host run failed before device selection:

```text
CUDA error at cudaGetDeviceProperties: no CUDA-capable device is detected
```

Using the GPU UUID changed the failure to:

```text
CUDA error at cudaGetDeviceProperties: initialization error
```

A temporary Docker run using the normal NVIDIA runtime and `--gpus device=14`
failed in the NVIDIA prestart hook:

```text
nvidia-container-cli: detection error: nvml error: unknown error
```

A manual non-hook container with `/dev/nvidia14`, `/dev/nvidiactl`,
`/dev/nvidia-uvm`, and host driver libraries mounted also failed to make CUDA
see a usable device.

Interpretation: the current-state GPU14 benchmark rerun is blocked by the
local CUDA/container runtime path. It does not change the earlier benchmark
evidence: GPU14 / `0000:19:00.0` was already measured as a capped
V100-personality card. The new register finding should therefore be treated as
strong evidence that `0x409664 == 0` alone is not the effective unlock
condition; `0x409660` readout/latch state remains the better discriminator.

## 47. After-Reboot Readback And GPU14 Rerun

After reboot, all 16 local GV100-class cards returned to normal live state in
`nvidia-smi`. The read-only FECS/PLM probe was rerun with sudo and saved here:

```text
projects/gpu-falcon-research/runs/20260520-fecs-plm-readonly/gv100-fecs-plm-readonly-after-reboot-sudo.txt
```

The earlier `0xffffffff` reads on GPUs 0-3 disappeared. Those cards now return
normal BAR0 state:

```text
NV_PMC_BOOT_0      = 0x140000a1
NV_PSTRAP_SYS_CTRL = 0xbadf5040
```

Across initialized CMP and V100-personality cards, the FECS feature-override
PLM remains uniform:

```text
0x00409650 FECS_FEATURE_OVERRIDE_PLM = 0x0000008f
0x00409670 FECS_FEATURE_SECURITY_PLM = 0x0000008f
0x00409674 FECS_FEATURE_SECURITY_LOCK = 0x00000000
```

The SM speed-select override also returned to the normal capped value on every
initialized card:

```text
0x00409664 FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT = 0x00000999
```

GPU14 / `0000:19:00.0` no longer shows the pre-reboot outlier state. Its
after-reboot values are:

```text
0x00409650 FECS_FEATURE_OVERRIDE_PLM             = 0x0000008f
0x00409660 FECS_FEATURE_READOUT                  = 0x00700100
0x00409664 FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT = 0x00000999
```

The GPU14 CUDA path also works again through a temporary Docker run bound to
`device=14`. Result:

```text
projects/gpu-falcon-research/runs/20260520-gpu14-current-state/gpu14-cublas-tensor-after-reboot-docker-8192.json
```

Measured result:

```text
device_name:             Tesla V100-PCIE-12GB
sm_count:                80
fp16_tensor_n:           8192
fp16_tensor_tflops:      6.975860
fp32_n:                  4096
fp32_tflops:             6.487898
```

That matches the previous capped V100-personality class:

```text
~6.97 TFLOP/s FP16 Tensor
~6.5  TFLOP/s FP32
Tensor/FP32 ratio ~= 1.07
```

Interpretation: the earlier GPU14 `0x409664 == 0` readback was transient or
pre-reboot state, not a persistent unlock. After a clean reboot, the live
hardware state, VBIOS init-script evidence, and CUDA performance all line up:

```text
0x409650 PLM is uniform and not the differentiator.
0x409664 is restored to the VBIOS-programmed 0x999 capped override.
0x409660 readout remains reduced.
GPU14 remains capped in normal CUDA workloads.
```

## 48. November 2021 NVIDIA Internal-Microcontroller CVE Checklist

Question: did the investigation cover this CVE cluster?

```text
CVE-2021-34400
CVE-2021-34399
CVE-2021-23219
CVE-2021-1125
CVE-2021-1105
CVE-2021-1088
```

Short answer: the mechanism family was covered, but these exact CVE IDs were
not previously tracked as a checklist.

Official NVIDIA source:

```text
Security Notice: NVIDIA GPU and Tegra Hardware - November 2021
https://nvidia.custhelp.com/app/answers/detail/a_id/5263
```

NVIDIA states:

```text
Ampere products are not impacted.
If the hypervisor is compromised or the attacker has direct GPU access with
bare metal or pass through, Turing, Volta, Pascal or Maxwell products can be
exposed.
In all cases, an attacker requires OS administrative/kernel rights.
```

The affected architecture table includes Volta/V100 for the relevant entries,
so this cluster is in scope for GV100.

Coverage map:

| CVE | Official class | Prior local coverage | Relevance to limiter |
| --- | --- | --- | --- |
| `CVE-2021-34400` | Unscrubbed microcontroller memory disclosure | Partially covered through Falcon IMEM/DMEM, WPR, ACR/PUB, and debug-readback research | Useful only as possible information leak. No known direct path to change `0x409660`/`0x409664`. |
| `CVE-2021-34399` | Unscrubbed microcontroller register disclosure | Partially covered through Falcon register/debug-register notes | Potentially relevant to observing internal Falcon state, not to clearing the limiter. |
| `CVE-2021-23219` | Protected information exposure by identifying/exploiting/loading vulnerable microcode | Covered by class through hexkyz/SciresM Falcon notes, ACR/PUB signing, and GV100 firmware-loading review | Relevant conceptually, but no local source path was found that lets GV100 load useful vulnerable/custom FECS/SEC2/PMU microcode. |
| `CVE-2021-1125` | Internal microcontroller program-data corruption | Lightly covered as part of the Falcon data/DMEM/ACR class, but not by exact ID | Could matter only if a controlled corruption primitive exists. We have not found one in the local GV100 driver/firmware path. |
| `CVE-2021-1105` | Runtime debug-register access | Covered by class through TRACEPC/IBRKPT/ICD/debug-register research | Most relevant for read-only observability. It does not by itself imply write access to FECS speed-select state. |
| `CVE-2021-1088` | Debug mechanisms with insufficient access control | Covered by class through Falcon debug mechanism research and local PLM/access-map work | Relevant to possible internal-state disclosure. No direct production GV100 unlock path found. |

Important distinction:

```text
These CVEs are hardware/internal-microcontroller information disclosure or
data-corruption classes requiring elevated privilege. They are not documented
as production driver IOCTLs, VBIOS bypasses, or direct FECS speed-select
controls.
```

How this changes the current model:

```text
It strengthens the "look for observability/debug leftovers" branch:
  TRACEPC
  IBRKPT
  ICD/debug register exposure
  Falcon IMEM/DMEM scrub state
  ACR/PUB/SEC2/PMU diagnostic state

It does not yet weaken the current limiter model:
  VBIOS programs 0x409664 = 0x999.
  FECS readout 0x409660 remains reduced.
  GPU14 remains capped after reboot.
  No visible GV100 RM/MODS production path clears the effective readout state.
```

Next safe backtracking item:

```text
Build a read-only CVE-2021-Falcon checklist against local source/docs:
  1. enumerate GV100 Falcon debug/TRACEPC/IBRKPT/ICD registers and PLMs;
  2. compare which are present in production access maps vs internal/regaccess maps;
  3. check whether any read-only path can observe FECS/PMU/SEC2 scrub/debug state
     without changing device state.
```

## 49. Direct Bare-Metal FECS Falcon Read-Only Probe

After reboot, direct bare-metal access was available, so the CVE-2021 Falcon
branch was tested as read-only observability rather than as an exploit path.

Tool:

```text
projects/gpu-falcon-research/tools/gv100_bar0_fecs_plm_readonly.c
```

Output:

```text
projects/gpu-falcon-research/runs/20260520-falcon-cve2021-readonly/gv100-fecs-falcon-plm-after-reboot-sudo.txt
```

The tool opens `resource0` read-only and maps BAR0 with `PROT_READ` only. It
does not write registers.

GV100 source anchors:

```text
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_graphics_nobundle.h

NV_PGRAPH_PRI_FECS_FALCON_TRACEPC = 0x0040914c
NV_PGRAPH_PRI_FECS_FALCON_IBRKPT1 = 0x00409098
NV_PGRAPH_PRI_FECS_FALCON_DBGCTL  = 0x00409260
NV_PGRAPH_PRI_FECS_FALCON_ICD_CMD = 0x00409200
NV_PGRAPH_PRI_FECS_FALCON_SCTL    = 0x00409240

NV_PGRAPH_PRI_FECS_FALCON_IMEM_PRIV_LEVEL_MASK   = 0x00409280
NV_PGRAPH_PRI_FECS_FALCON_DMEM_PRIV_LEVEL_MASK   = 0x00409284
NV_PGRAPH_PRI_FECS_FALCON_CPUCTL_PRIV_LEVEL_MASK = 0x00409288
NV_PGRAPH_PRI_FECS_FALCON_EXE_PRIV_LEVEL_MASK    = 0x0040928c
NV_PGRAPH_PRI_FECS_FALCON_SCTL_PRIV_LEVEL_MASK   = 0x00409298
```

All 16 GV100-class cards returned live FECS/Falcon register state. The stable
cross-card values were:

```text
FECS_FALCON_TRACEPC       = 0x00007225
FECS_FALCON_SCTL          = 0x00007021
FECS_FALCON_DEBUG1        = 0x00000040
FECS_FALCON_DBGCTL        = 0x00000000
FECS_FALCON_ICD_CMD       = 0x00000000
FECS_FALCON_IBRKPT1..5    = 0x00000000
FECS_FALCON_CPUCTL        = 0x00000060
FECS_FALCON_BOOTVEC       = 0x00007e00
FECS_FALCON_DMEM_DUMMY    = 0xbadf510a
```

The FECS Falcon PLMs were also stable across CMP and V100-personality cards:

```text
FECS_FALCON_IMEM_PLM      = 0x000000af  read_mask=0x7 write_mask=0x2
FECS_FALCON_DMEM_PLM      = 0x000000aa  read_mask=0x2 write_mask=0x2
FECS_FALCON_CPUCTL_PLM    = 0x000000ef  read_mask=0x7 write_mask=0x6
FECS_FALCON_EXE_PLM       = 0x000000ef  read_mask=0x7 write_mask=0x6
FECS_FALCON_IRQTMR_PLM    = 0x000000ff  read_mask=0x7 write_mask=0x7
FECS_FALCON_MTHDCTX_PLM   = 0x000000ff  read_mask=0x7 write_mask=0x7
FECS_FALCON_SCTL_PLM      = 0x000000cf  read_mask=0x7 write_mask=0x4
FECS_FALCON_WDTMR_PLM     = 0x00000077  read_mask=0x7 write_mask=0x7
```

The only consistent identity split in this read-only pass remained:

```text
CMP 100-210 / 0x1d84:        FECS_FEATURE_READOUT = 0x007000f3
V100 personality / 0x1df4:   FECS_FEATURE_READOUT = 0x00700100

All 16 GV100-class cards:    FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT = 0x00000999
```

Interpretation:

```text
Direct bare-metal access confirms that the Falcon debug/observability surface
is readable on this host, including TRACEPC and breakpoint registers. However,
the readable Falcon debug state and Falcon PLMs do not distinguish capped CMP
cards from V100-personality cards. This keeps the CVE-2021 debug-register
cluster useful for observability, but it does not reveal a production runtime
path that clears the effective FECS speed-select readout.
```

## 50. GV100 MODS IssueRateOverride Port Validation

### Scope
Validated the GV100 IssueRateOverride implementation (Volta-953) in
[`voltagpu.cpp`](temp/nvdrv/dev/gpu_drv/stage_rel/diag/mods/gpu/voltagpu.cpp)
against the GV100 hwref register definitions, context-state store, and rmtest
register tables.  The original code was a direct copy from
[`turinggpu.cpp`](temp/nvdrv/dev/gpu_drv/stage_rel/diag/mods/gpu/turinggpu.cpp)
and contained five silently incorrect code blocks.

### Findings

#### A. `GetIssueRate()` — Rewritten (lines 3611–3691)

**Before (Turing copy):**
- Read `_SM_SPEED_SELECT_1` (does not exist on GV100)
- Uses 3-bit multi-rate FFMA field (no FFMA field exists on GV100)
- Uses FMLA16/FMLA32 sub-fields (GV100 has only single FMLA field at bit 4)
- Uses IMLA0/IMLA1/IMLA2/IMLA3 sub-fields (GV100 has only single IMLA field at bit 0)
- Silent `return 1.0` for unknown state

**After (GV100-correct):**
- Reads only `SM_SPEED_SELECT` (0x00409664)
- Reads `FEATURE_READOUT` (0x00409660) for live-state checking
- DP → bit 8 override + bit 20 readout, FMLA → bit 4 override + bit 22 readout, IMLA → bit 0 override + bit 21 readout
- FFMA, HFMA2 return 1.0 directly (no override path)
- `IMMA_16832_S32_S8` returns 1.0 (IMLA4 is Turing+ only)
- `MASSERT` on invalid state instead of silent `return 1.0`

#### B. `CheckIssueRateOverride()` — Already Correct (lines 3693–3713)

No changes needed.  This method only checks `SM_SPEED_SELECT` existence and
PRIV-level protection — neither path references `_SM_SPEED_SELECT_1`.

#### C. `GetIssueRateOverride()` — Fixed (lines 3715–3721)

**Before:** Read `_SM_SPEED_SELECT_1` into `speedSelect1`.
**After:** `speedSelect1 = 0` with comment noting GV100 has no `_1` register.

#### D. `IssueRateOverride(settings)` — Fixed (lines 3723–3732)

**Before:** Wrote `_SM_SPEED_SELECT_1`.
**After:** Only writes `SM_SPEED_SELECT`; comment notes `_1` does not exist.

#### E. `IssueRateOverride(string, UINT32)` — Rewritten (lines 3734–3810)

**Before (Turing copy, ~280 lines):**
- Supported DP, FFMA, FMLA16, FMLA32, IMLA0–4
- Every non-DP unit used 3-bit multi-rate fields (1/2 through 1/64)
- IMLA4 used `_SM_SPEED_SELECT_1` register
- Wrote `_SM_SPEED_SELECT_1` at the end

**After (GV100-correct, ~80 lines):**
- Supported units: DP, FMLA, IMLA only
- All use 1-bit binary fields: FULL_SPEED (case 1) or REDUCED_SPEED (case 2)
- No `_SM_SPEED_SELECT_1` register read or write
- Clear error message listing valid units on GV100

### Register-Discrepancy Summary

| Feature | Turing (TU102) | GV100 | Evidence |
|---------|---------------|-------|----------|
| SM_SPEED_SELECT_1 | 0x0040966c | Does not exist | gv100 hwref, rmtest table, ctx state store |
| SM_SPEED_SELECT IMLA field | 3-bit (0:2) | 1-bit (0:0) | `gv100/dev_graphics_nobundle.h` |
| SM_SPEED_SELECT FMLA field | 3-bit (4:6) | 1-bit (4:4) | `gv100/dev_graphics_nobundle.h` |
| SM_SPEED_SELECT DP field | 3-bit (8:10) | 1-bit (8:8) | `gv100/dev_graphics_nobundle.h` |
| SM_SPEED_SELECT FFMA field | 3-bit (12:14) | Absent | `gv100/dev_graphics_nobundle.h` |
| IMLA sub-fields (0–4) | Separate | Single IMLA | hwref + `Instr` enum analysis |
| FMLA16/FMLA32 sub-fields | Separate | Single FMLA | hwref field definitions |
| FEATURE_READOUT | Has `_1` variant | 0x00409660 only | gv100 hwref lines 5391–5441 |

### Validation Status

All five methods in the GV100 IssueRateOverride path are now correctly
implemented for GV100's hardware register layout.  The port removes all Turing
register references (`_SM_SPEED_SELECT_1`, multi-rate encodings, FFMA/IMLA0–4/
FMLA16/FMLA32 unit names) and uses only the 1-bit binary fields confirmed by the
GV100 hwref header.

## 51. Dynamic FECS Read-Only Sampling During Tensor GEMM

After the bare-metal FECS Falcon register pass, a dynamic read-only sample was
run while a normal cuBLAS Tensor GEMM was active. This was designed to answer a
narrow question: do the visible FECS readout, speed-select override, TRACEPC, or
debug-control registers change while HMMA/Tensor work is actually executing?

Commands used the existing normal CUDA benchmark and the read-only BAR0 probe:

```text
projects/gpu-falcon-research/runs/20260520-tensor-sgemm-sweep/cublas_tensor_probe
projects/gpu-falcon-research/tools/gv100_bar0_fecs_plm_readonly
```

Outputs:

```text
projects/gpu-falcon-research/runs/20260520-falcon-cve2021-readonly/dynamic/gpu14-cublas-dynamic.json
projects/gpu-falcon-research/runs/20260520-falcon-cve2021-readonly/dynamic/gpu14-fecs-samples-during-cublas.txt
projects/gpu-falcon-research/runs/20260520-falcon-cve2021-readonly/dynamic/gpu0-cublas-dynamic.json
projects/gpu-falcon-research/runs/20260520-falcon-cve2021-readonly/dynamic/gpu0-fecs-samples-during-cublas.txt
```

GPU14, V100 personality, `0000:19:00.0`:

```text
device_name:              Tesla V100-PCIE-12GB
sm_count:                 80
fp16_tensor_tflops:       6.976595
fp32_tflops:              6.515213

All 45 samples:
FECS_FALCON_TRACEPC                       0x00007225
FECS_FALCON_SCTL                          0x00007021
FECS_FALCON_ICD_CMD                       0x00000000
FECS_FALCON_DBGCTL                        0x00000000
FECS_FEATURE_READOUT                      0x00700100
FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT     0x00000999
```

GPU0, CMP identity, `0000:05:00.0`:

```text
device_name:              NVIDIA CMP 100-210
sm_count:                 68
fp16_tensor_tflops:       5.898726
fp32_tflops:              5.674231

All 45 samples:
FECS_FALCON_TRACEPC                       0x00007225
FECS_FALCON_SCTL                          0x00007021
FECS_FALCON_ICD_CMD                       0x00000000
FECS_FALCON_DBGCTL                        0x00000000
FECS_FEATURE_READOUT                      0x007000f3
FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT     0x00000999
```

Interpretation:

```text
The visible FECS feature readout and debug-status registers were static during
active Tensor GEMM. This supports the model that `0x409660`/`0x409664` are
configuration/readout state, not live counters. The Tensor cap manifests in
throughput, but the sampled FECS debug registers do not expose a live transition
or runtime escape hatch during normal CUDA execution.
```

## 52. `tx2hax` Applicability To GV100

External reference:

```text
https://github.com/EliseZeroTwo/tx2hax
```

The repository describes Tegra X2 attacks, not discrete GV100 PCIe GPU attacks.
Its README says the project contains exploit implementations for Tegra X2, with
one path targeting USB Recovery Mode BootROM execution and another targeting
Magic Leap One devices.

Relevant conceptual mapping:

```text
tx2hax target:     Tegra X2 SoC
entry surface:     USB Recovery Mode / SoC boot path
goal:              BootROM-context execution or BootROM extraction
security moment:   before boot-chain handoff and lock-outs

GV100 local target: discrete PCIe GPU
entry surface:      PCIe BAR0/RM/NVML/VBIOS option ROM after GPU init
goal:               explain or observe FECS speed-select state
security moment:    after VBIOS/RM/ACR/FECS initialization has run
```

The `obtaining-bootrom.md` writeup says the Tegra X2 BootROM is not generally
visible after it hands off to MB1, except for a small non-secure portion, and
that a debug-loader path exists under fuse-conditioned circumstances. The
writeup then uses a physical fault-injection setup to reach that path and read
the BootROM.

The useful lesson for GV100 is the class of thing required:

```text
To beat an early secure-policy decision cleanly, one needs either:
  - execution before the relevant lock-outs are applied;
  - a debug/manufacturing path left enabled by fuses/straps;
  - or an information leak sufficient to reconstruct hidden firmware state.
```

Local source/doc search results:

```text
SBR/PUB docs discuss Falcon bootrom trust chains:
  PUB-NS sets up Falcon BootROM parameters.
  PUB-HS is PKC/RSA signed and verified by SEC2 Falcon BootROM.
  HS privilege is granted only after signature verification.

PMU source contains XUSB messaging support on Turing paths, but that is PMU/BIF
coordination code, not a GV100 PCIe BootROM recovery/debug-loader entrypoint.

No local GV100 path was found that resembles Tegra USB RCM, Tegra debug UART
loader entry, or a host-accessible dGPU BootROM extraction mode.
```

Interpretation:

```text
`tx2hax` is a strong analogy for "pre-lockout execution matters", but it does
not provide a directly portable GV100 path. For these cards, the closest
already-observed equivalent is not USB RCM; it is the VBIOS init-script write to
`0x409664` plus the signed Falcon/ACR/SEC2/FECS chain that follows. The open
question remains the same: whether any enabled dGPU manufacturing/debug path can
observe or influence the hidden state before FECS readout settles to reduced.
```

## 53. Fault Injection And BootROM Side-Channel Boundary

Fault injection and BootROM side-channel attacks remain conceptually relevant,
but they are a different class from the BAR0/RM/VBIOS work above. The useful
question is not "can we write the register after boot?" but:

```text
Can anything observe or influence the secure-boot / manufacturing-debug state
before the FECS feature readout and speed-select policy settle?
```

Local evidence that this is a real NVIDIA design boundary:

```text
integ/gpu_drv/stage_rel/safety/SBR-TU10A/SWE-SBR-007-SWADS.md

PUB-NS sets Falcon BootROM parameters.
PUB-HS is PKC/RSA signed and verified by SEC2 Falcon BootROM.
HS privilege is granted by BootROM only after signature verification.
Signature failure leaves Falcon halted in NS and RM is expected to abort/reset.
```

The same source family explicitly treats reset/PLM/debug state as part of the
security boundary:

```text
PMU Falcon reset is Priv-level 3 protected.
PUB checks Falcon reset protection state.
PUB scrubs non-secure IMEM after HS entry.
PUB clears stale SCP signature state.
```

The local PLM/fuse candidate reconstruction also shows the expected
manufacturing/debug surface exists as named hardware state:

```text
runs/20260520-pub-plm-candidate-reconstruction/gv100_priv_level_candidates_integ.csv
runs/20260520-pub-plm-candidate-reconstruction/gv100_priv_level_candidates_dev.csv

NV_FUSE_DEBUGCTRL
NV_FUSE_DEBUGCTRL_PRIV_LEVEL_MASK
NV_PJTAG_ACCESS_CTRL
NV_PJTAG_ACCESS_PRIV_LEVEL_MASK
NV_FUSE_OPT_JTAGKEY_ACC_DIS
NV_FUSE_OPT_SECURE_PMU_DEBUG_DIS
NV_FUSE_OPT_SECURE_SECENGINE_DEBUG_DIS
NV_FUSE_OPT_SECURE_SEC_DEBUG_DIS
NV_FUSE_OPT_SECURE_SCANDEBUG_ACCESS_DISABLE
NV_FUSE_OPT_SECURE_HOST2JTAG_BOUNDARY_SCAN_DISABLE
NV_FUSE_OPT_SECURE_HOST2JTAG_SHA2_EN
NV_FUSE_OPT_SECURE_JTAG_SECUREID_VALID
```

Interpretation:

```text
NVIDIA almost certainly has internal/manufacturing paths that can diagnose or
provision this state. The local tree names the fuse, JTAG/PJTAG, PLM, BootROM,
and HS-authentication surfaces that such paths would depend on.

The local tree does not show an enabled production PCIe-host path equivalent to
Tegra USB RCM, Tegra debug-loader entry, or a dGPU BootROM extraction mode.
```

Bare-metal read-only probing after reboot also argues against a simple
left-enabled debug route through FECS BAR0:

```text
runs/20260520-falcon-cve2021-readonly/gv100-fecs-falcon-plm-after-reboot-sudo.txt

FECS_FALCON_TRACEPC  = 0x00007225 on all sampled cards
FECS_FALCON_SCTL     = 0x00007021 on all sampled cards
FECS_FALCON_DBGCTL   = 0x00000000 on all sampled cards
FECS_FALCON_ICD_CMD  = 0x00000000 on all sampled cards
Falcon PLMs are uniform across CMP and V100-personality cards
```

The read-only dynamic CUDA sampling also found no visible FECS debug-state
transition while Tensor GEMM was active. The only stable identity split remains:

```text
CMP identity:          FECS_FEATURE_READOUT = 0x007000f3
V100 personality:      FECS_FEATURE_READOUT = 0x00700100
Both personalities:    SM_SPEED_SELECT override = 0x00000999
```

Safe conclusion:

```text
Fault injection / side-channel work is plausible only as board-level secure
boot research against the pre-lockout window. It is not the same as the current
software unlock path, and the local source does not give a production-host
recipe for it.
```

Non-destructive work that still helps:

```text
1. Keep building the debug/fuse/PLM inventory from local headers and CSVs.
2. Compare board-visible straps, ROM identity, InfoROM objects, and read-only
   debug/fuse state across CMP, V100-personality CMP, and known full-speed V100.
3. Treat Tegra BootROM research as conceptual vocabulary only unless a discrete
   GV100 PCIe BootROM/debug-loader entrypoint is found in source or public docs.
4. Preserve all read-only boot/debug state immediately after reboot, before
   CUDA workloads or RM reinitialization paths perturb state.
```

## 54. `falcon-tools` Applicability To GV100

External reference:

```text
https://github.com/CAmadeus/falcon-tools
```

Local clone for offline inspection:

```text
projects/gpu-falcon-research/tools/falcon-tools
```

The repository is explicitly aimed at Tegra X1 TSEC Falcon v5 research:

```text
falcon-tools/README.md

A toolbox for researching and hacking NVIDIA Falcon microprocessors used in
TSEC engines on the Tegra X1.
```

Useful pieces for this project:

```text
falcon-tools/requiem/README.md

Non-secure mode (NS):
  possible without NVIDIA-signed code, but access may be restricted.

Light Secure mode (LS):
  more privilege than NS, fewer than HS, mostly debugging rather than production.

Heavy Secure mode (HS):
  granted after authentication; internal state is mostly inaccessible from the
  host while the Falcon runs at highest privilege.
```

This matches the local NVIDIA-side GV100 vocabulary:

```text
dev/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_falcon_v4.h

NV_PFALCON_FALCON_SCTL_LSMODE
NV_PFALCON_FALCON_SCTL_HSMODE
NV_PFALCON_FALCON_SCTL_AUTH_EN
NV_PFALCON_FALCON_ICD_CMD_IDX_SEC
NV_PFALCON_FALCON_ICD_CMD_IDX_PC
NV_PFALCON_FALCON_ICD_CMD_IDX_IMB
NV_PFALCON_FALCON_ICD_CMD_IDX_DMB
```

`libfaucon/mmio.asm` is also useful as a generic Falcon register-name checklist:

```text
FALCON_CPUCTL
FALCON_BOOTVEC
FALCON_DEBUG1
FALCON_TRACEPC
FALCON_ICD_CMD / ADDR / WDATA / RDATA
FALCON_SCTL
FALCON_IMEMC / IMEMD / IMEMT
FALCON_DMEMC / DMEMD
FALCON_SPROT_*
```

The authoritative GV100 offsets, however, remain the local NVIDIA headers:

```text
dev_falcon_v4.h:
  NV_PFALCON_FALCON_SCTL                 = 0x00000240
  NV_PFALCON_FALCON_CPUCTL               = 0x00000100
  NV_PFALCON_FALCON_BOOTVEC              = 0x00000104
  NV_PFALCON_FALCON_TRACEPC              = 0x0000014c
  NV_PFALCON_FALCON_ICD_CMD              = 0x00000200

dev_graphics_nobundle.h:
  NV_PGRAPH_PRI_FECS_FALCON_SCTL         = 0x00409240
  NV_PGRAPH_PRI_FECS_FALCON_CPUCTL       = 0x00409100
  NV_PGRAPH_PRI_FECS_FALCON_BOOTVEC      = 0x00409104
  NV_PGRAPH_PRI_FECS_FALCON_TRACEPC      = 0x0040914c
  NV_PGRAPH_PRI_FECS_FALCON_ICD_CMD      = 0x00409200
```

Non-portable pieces:

```text
keygenldr/
keygen/
secureboot/
requiem/
```

Those payloads assume a Switch/Tegra TSEC execution environment, RCM/hekate-style
control, Nintendo Package1/TSEC firmware stages, and a specific Tegra Falcon
secure-boot flaw. They do not provide a GV100 PCIe host entrypoint, a FECS
microcode loader, or a production dGPU path for entering Heavy Secure mode.

Interpretation:

```text
`falcon-tools` strengthens the model that the limiter sits behind Falcon
security-mode and authentication boundaries. It is directly useful for language,
register checklists, and comparing Falcon concepts. It is not a direct unlock
path for GV100.

The safe reuse is to improve read-only annotation of GV100 FECS Falcon state:
decode SCTL/CPUCTL/ICD/TRACEPC/IMEM/DMEM/PLM fields using local `dev_falcon_v4.h`
as authority and `falcon-tools` as a sanity-check vocabulary source.
```

## 55. `faucon` And PR 12

External references:

```text
https://github.com/vbe0201/faucon
https://github.com/vbe0201/faucon/pull/12
```

Local clone:

```text
projects/gpu-falcon-research/tools/faucon
```

The base repository is more directly useful than `falcon-tools` for offline
analysis because it is a Falcon assembler/disassembler/emulator project:

```text
faucon/README.md

faucon aims to provide:
  assembler
  disassembler
  CPU emulator
  architecture documentation

Current stated target:
  fuc5 generation
```

Important local mismatch:

```text
GV100 FECS authoritative local header:
  dev/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_falcon_v4.h

faucon stated target:
  fuc5
```

So `faucon` may be useful for general Falcon ISA work, but it should not be
blindly trusted for GV100 FECS until an extracted GV100 FECS/SEC2/PMU artifact is
confirmed to use a matching ISA revision. The local header names the FECS core as
`dev_falcon_v4.h`.

PR 12 is especially relevant conceptually:

```text
PR #12 title:
  Add support for Light Secure Mode and Heavy Secure Mode emulation

State:
  open draft, unmerged

Changed scope:
  faucon-asm crypto instruction parsing/disassembly
  faucon-emu SCP model
  crypto register ACL model
  AES/MAC helpers
  ROM/HS/LS mode work items
```

The PR body names the exact boundary we keep reaching:

```text
NS:
  unsigned code, restricted register/DMA capability

LS:
  debugging-oriented mode with more privilege than NS, less than HS

HS:
  authenticated mode with highest Falcon privilege and host-inaccessible state

Falcon ROM mode:
  internal hidden ROM segment used during crypto/authentication operations
```

Safe applicability:

```text
1. Use `faucon`/PR 12 as an offline conceptual model for:
     SCP
     crypto-register ACLs
     HS/LS/NS state
     Falcon ROM mode
     Falcon crypto-command disassembly

2. Do not treat it as a GV100 loader or unlock path:
     it provides no PCIe host entrypoint
     it provides no signed GV100 FECS/SEC2 payload
     it does not bypass GV100 VBIOS/RM/ACR/FECS policy
     it targets fuc5 while GV100 FECS headers are v4

3. If we later obtain a legitimate extracted Falcon IMEM/firmware blob from a
   matching engine/revision, `faucon` is a candidate offline disassembler or
   cross-check against envytools/ghidra-falcon.
```

Local build note:

```text
`cargo` is not installed on this host, so `faucon` was not built during this pass.
The repo and PR branch were cloned/fetched for source inspection only:

git clone --depth=1 https://github.com/vbe0201/faucon.git \
  projects/gpu-falcon-research/tools/faucon

git fetch origin pull/12/head:pr-12
```

Interpretation:

```text
`faucon` and PR 12 strengthen the "hidden Falcon auth/SCP/ROM boundary" model.
They do not make the current cards closer to a software unlock. They are useful
if the next phase becomes offline decoding of extracted Falcon code or
constructing a more precise read-only annotation of Falcon security state.
```

## 56. Public GitHub/Nouveau GV100 FECS Firmware Surface

The GitHub code search URL:

```text
https://github.com/search?q=nvidia+fecs+v100&type=code
```

does not expose code results to a logged-out session. The equivalent public
source path is Nouveau in the upstream Linux tree.

Follow-up web searches for:

```text
"nvidia/gv100/gr/fecs_inst.bin"
"gv100" "fecs_inst.bin" "GitHub"
"nvidia fecs v100" GitHub
"NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT" GitHub
```

found public firmware listings, Debian packaging/bug references, and
linux-firmware mirrors. They did not surface public producer source, JSON
chipfuse/POR data, or a GV100 FECS speed-select implementation.

Public Nouveau GV100 GR source names the exact firmware surface:

```text
drivers/gpu/drm/nouveau/nvkm/engine/gr/gv100.c

MODULE_FIRMWARE("nvidia/gv100/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gv100/gr/sw_method_init.bin");

gv100_gr_fwif:
  { 0, gm200_gr_load, &gv100_gr, &gp108_gr_fecs_acr, &gp108_gr_gpccs_acr }
```

The shared `gm200_gr_load()` path loads FECS and GPCCS as signed ACR-managed
firmware objects:

```text
drivers/gpu/drm/nouveau/nvkm/engine/gr/gm200.c

nvkm_acr_lsfw_load_bl_inst_data_sig(... NVKM_ACR_LSF_FECS,  "gr/fecs_", ...)
nvkm_acr_lsfw_load_bl_inst_data_sig(... NVKM_ACR_LSF_GPCCS, "gr/gpccs_", ...)
```

The host already has those public firmware artifacts installed under
`/lib/firmware/nvidia/gv100`. A local offline copy/decompression pass was saved
here:

```text
projects/gpu-falcon-research/runs/20260520-public-gv100-firmware/
```

Decompressed sizes:

```text
acr/ucode_load.bin       18688
acr/ucode_unload.bin      6400
gr/fecs_bl.bin             576
gr/fecs_data.bin          4788
gr/fecs_inst.bin         25632
gr/fecs_sig.bin            192
gr/gpccs_data.bin         2128
gr/gpccs_inst.bin        12643
gr/gpccs_sig.bin           192
gr/sw_bundle_init.bin     7664
gr/sw_ctx.bin             9756
gr/sw_method_init.bin    12296
gr/sw_nonctx.bin          2728
nvdec/scrubber.bin        4352
sec2/desc.bin              656
sec2/image.bin           91136
sec2/sig.bin               192
```

Selected SHA256 values:

```text
gr/fecs_inst.bin   00db0996214aa8f54450e4052af32c98b4c0fe29c0c6c4983d234025ddc1dab5
gr/fecs_data.bin   8912bf456eb0ca5e3026ab900003fe45a0795eaea29e7f26216dfc0ae1747431
gr/gpccs_inst.bin  81ca0d73b1d5212cec30522a848326928f0c039d7b5b5caf44a423a42de6cecb
sec2/image.bin     1ff237d56dd51aa2577261f324ae680b2e5f973d8217595a3a8929e4bea3a718
```

The FECS data blob carries a build timestamp:

```text
Aug  8 2017
20:06:24
```

`envydis` can disassemble the public FECS/GPCCS/SEC2/ACR images as Falcon
`fuc5` without disassembler errors. Saved outputs include:

```text
fecs_inst.envydis.fuc5.txt
gpccs_inst.envydis.fuc5.txt
sec2_image.envydis.fuc5.txt
acr_ucode_load.envydis.fuc5.txt
acr_ucode_unload.envydis.fuc5.txt
```

Static searches of the decompressed binaries and disassembly found no direct
occurrence of:

```text
0x00409664  NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
0x00409660  NV_PGRAPH_PRI_FECS_FEATURE_READOUT
0x00409650  NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK
0x00019900  NV_CTXSW_FIRMWARE_FEATURE_OVERRIDE_SM_SPEED_SELECT
0x00000999  observed local speed-select override value
```

The exact little-endian `0x00000999` byte pattern appears in unrelated-looking
positions in `acr/ucode_load.bin`, `gr/sw_bundle_init.bin`, and
`sec2/image.bin`; none is associated with the FECS speed-select address or a
named speed-select register.

The FECS instruction image does directly reference nearby normal FECS control
and status registers:

```text
fecs_inst.envydis.fuc5.txt

00001517: mov $r10 0x409614
  NV_PGRAPH_PRI_FECS_CTXSW_RESET_CTL

000061e2: mov $r10 0x409604
000061f5: mov $r10 0x409604
00006260: mov $r10 0x409604
  NV_PGRAPH_PRI_FECS_FS
```

Other public firmware references include:

```text
0x409418  NV_PGRAPH_PRI_FECS_COMP_SET_DONE(i)
0x409a20  NV_PGRAPH_PRI_FECS_ARB_CMD_OVERRIDE
0x409a2c  NV_PGRAPH_PRI_FECS_ARB_WPR
0x409c1c  NV_PGRAPH_PRI_FECS_HOST_INT_SET
```

Interpretation:

```text
The public GitHub/Nouveau path gives us a real, reproducible GV100 FECS/GPCCS/
SEC2/ACR firmware corpus and proves Nouveau loads GV100 FECS/GPCCS through the
signed ACR path.

It does not expose source or a direct static reference to the limiter register.
The public FECS image touches normal FECS reset/status/host-interrupt surfaces,
but the observed limiter write to 0x409664 remains in the VBIOS init script and
the effective reduced/full divisor mapping remains below the visible public
firmware-source boundary.
```

## 57. What Is HAL/Generated Versus Missing Policy

The speed-select mechanism is partly generated, but not all of it.

Generated or HAL-derived pieces that are present:

```text
1. GV100 hwref/register headers:
   NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT = 0x00409664
   NV_CTXSW_FIRMWARE_FEATURE_OVERRIDE_SM_SPEED_SELECT  = 0x00019900

2. GV100 context-state generated net files:
   drivers/resman/kernel/inc/ctxsw_ucode/volta/gv100/net/NETD_dev_graphics_nobundle.h
     NET24_NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT = 0x00409664

   drivers/resman/kernel/inc/ctxsw_ucode/volta/gv100/net/NETD_ctx_state_store.h
     NET24_LIST_REGS_WITH_IB_DLY_RESET_VALUE(...)
       NET24_NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT

3. RM HAL dispatch metadata:
   drivers/resman/config/haldefs/gr.def

   FEATURE_OVERRIDE_SM_SPEED_SELECT:
     _TU102 => [ TURING, ]
     _GA100 => [ AMPERE_and_later, ]
     _STUB  => [ pre_TURING, ]

   GET_SM_ISSUE_RATE_MODIFIER:
     _TU102 => [ TURING, ]
     _GA100 => [ AMPERE_and_later, ]
     _STUB  => [ pre_TURING, ]

4. Generated user-access maps:
   These are generated from access-map metadata. The checked-in GV100 maps omit
   FECS feature readout/override, which explains `EXEC_REG_OPS` returning
   invalid offset for 0x409660/0x409664.
```

So the correct model is:

```text
HAL/generated layers know the register exists.
HAL/generated layers decide which chips get public RM read/override functions.
HAL/generated user maps decide whether normal RM regops can touch it.
GV100 is present in generated context/register surfaces but excluded from public
issue-rate read/override HAL paths and from production user-access maps.
```

What is still not present in this checkout:

```text
1. The SKU/chipfuse/POR/IFF producer data that decides to request reduced mode.
2. A GV100 HAL implementation that maps reduced FMLA to the observed 1/16 divisor.
3. A visible GV100 C source path that writes 0x409664 = 0x999 in production RM.
4. Public source for the signed FECS policy code.
```

Interpretation:

```text
Yes, the surface is generated with HAL/register metadata. The missing thing is
not the register definition or the context-state inclusion. The missing thing is
the producer input/policy that causes the generated surface to be initialized as
reduced on these boards.

That producer still points back to one of:
  VBIOS init script table material
  absent gv100_f.json / gv100_POR.json / IFF data
  signed FECS/GR firmware policy
  private RM metadata not present in this source tree
```

## 58. Older Board Speed-Select Model

Older Pascal-family headers show the predecessor to the GV100 mechanism.

On Pascal/GP100, the visible FECS/CTXSW speed-select mechanism is DP-only and
binary:

```text
pascal/gp100/dev_graphics_nobundle.h

NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_DP_SPEED_SELECT = 0x0040965c
  SM_FULL_SPEED
  SM_REDUCED_SPEED
  SM_OVERRIDE

pascal/gp100/dev_ctxsw_firmware.h

NV_CTXSW_FIRMWARE_FEATURE_OVERRIDE_DP_SPEED_SELECT = 0x0000065c
  SM_FULL_SPEED
  SM_REDUCED_SPEED
  SM_OVERRIDE
```

The same DP-only pattern appears across Pascal GP10x headers. No Pascal GP100
hit was found for FMLA/IMLA `SM_SPEED_SELECT`, which matches the architecture:
Pascal predates Tensor cores.

GV100 extends that older binary model:

```text
volta/gv100/dev_graphics_nobundle.h

NV_PGRAPH_PRI_FECS_FEATURE_READOUT_SM_SPEED_SELECT_DP
NV_PGRAPH_PRI_FECS_FEATURE_READOUT_SM_SPEED_SELECT_IMLA
NV_PGRAPH_PRI_FECS_FEATURE_READOUT_SM_SPEED_SELECT_FMLA

NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT = 0x00409664
  IMLA_FULL_SPEED / IMLA_REDUCED_SPEED / IMLA_OVERRIDE
  FMLA_FULL_SPEED / FMLA_REDUCED_SPEED / FMLA_OVERRIDE
  DP_FULL_SPEED   / DP_REDUCED_SPEED   / DP_OVERRIDE
```

Turing then moves to explicit multi-bit selectors:

```text
turing/tu102/dev_graphics_nobundle.h

NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT = 0x00409664
  IMLA0_REDUCED_SPEED_1_2 ... _1_64
  FMLA16_REDUCED_SPEED_1_2 ... _1_32
  FMLA32_REDUCED_SPEED_1_2 ... _1_32
  FFMA_REDUCED_SPEED_1_2 ... _1_32
  IMLA1/2/3/4_REDUCED_SPEED_1_2 ... _1_64
```

This matches the engineering-note evidence:

```text
engnotes_willow.txt

Submitting a manuals change to add the REDUCED_SPEED defines for the new
SM_SPEED_SELECT feature override registers.

Submitting change to add new 3 bit SM_SPEED_SELECT fuses and remove the
older 1 bit fuses (except SM_SPEED_SELECT_DP).
```

Interpretation:

```text
Yes, we know the broad older-board model:

Pascal:
  FECS/CTXSW binary reduced/full speed-select existed, but only for DP.

GV100:
  The same binary FECS/CTXSW model expanded to DP + IMLA + FMLA.
  The local CMP/V100-personality limiter uses this era's binary reduced state.

Turing and later:
  The binary abstraction was replaced by explicit multi-rate selectors, including
  1/16 values for FMLA16/FMLA32/IMLA/FFMA.

That makes GV100 the awkward middle generation: it can throttle Tensor/FMLA, but
the public register model only says REDUCED, not which divisor REDUCED means.
The observed 1/16 divisor is therefore the implementation of the GV100 binary
FMLA_REDUCED state, not a value exposed in the GV100 register field itself.
```

### 59. Evidence DAG Consolidation Pass

The Python evidence DAG was expanded after reviewing the current Atlas knowledge
set and the local Markdown history.

Files:

```text
tools/gv100_limiter_evidence_dag.py
runs/20260520-evidence-dag/gv100-limiter-evidence-dag.json
runs/20260520-evidence-dag/gv100-limiter-evidence-dag.md
runs/20260520-evidence-dag/gv100-limiter-evidence-dag.dot
```

Current graph shape:

```text
56 nodes
72 edges
```

The expanded graph now includes:

```text
Benchmark/control evidence:
  DeepBench public V100 control
  Vast.ai full-speed V100 controls
  HMMA issue-spacing microbenchmark
  local FP16/FP64/DP4A downstream probes

Board and identity evidence:
  CMP vs V100-personality split
  visible V100 identity not clearing the cap
  InfoROM/debugdump board-management differences

ROM/VBIOS evidence:
  local PROM dumps
  selected init-script tail write to 0x409664
  raw ROM pattern scan
  same-family ROM metadata-only diffs
  M-table and stale Falcon-table negative controls

Live FECS evidence:
  all-card post-reboot 0x409664=0x999 readback
  CMP vs V100-personality FECS readout split
  GPU14 transient-zero non-unlock
  dynamic FECS sampling during Tensor GEMM

Source/generation evidence:
  GV100 binary FECS/CTXSW speed-select
  generated NETD context-state surface
  RM pre-Turing issue-rate stubs
  MODS Turing/Ampere-only override path
  internal RM speed-select/scheduler hooks
  older Pascal -> GV100 -> Turing speed-select transition

Blocked or negative branches:
  missing gv100_f/gv100_POR chipfuse inputs
  nvspec is not chipfuse data
  debugdump is not a raw PFUSE dump
  public GV100 firmware has no direct 0x409664 producer hit
  Falcon/Tegra public tooling is offline or analogy-only here
  PLM/PUB reconstruction did not expose a host-side unlock path
  privileged RM regops read back FECS speed-select state but do not establish a
  control path
  CVE/exploitability lens added as an active per-surface triage step
  2016 physical-memory mmap CVEs as class-only references
  CVE-2017-0352 sensitive-register access-control class
  SBR/PUB firmware-mediated PLM access model
```

Interpretation: the DAG now represents the investigation as a read-only evidence
map. It identifies the asserted state, its measured downstream effects, the
visible source surfaces that know about it, and the remaining hard blocker:
the producer/mapping for GV100 binary `FMLA_REDUCED` to the measured `1/16`
divisor remains in missing chipfuse/POR/IFF data, signed/private Falcon policy,
or internal NVIDIA build artifacts.

### 60. RM Regops Privilege Audit

The apparent contradiction around `NV2080_CTRL_CMD_GPU_EXEC_REG_OPS` is now
resolved.

Source says the normal RM register-offset validator skips the generated user
access map for an administrator/CAP_SYS_ADMIN caller:

```text
integ/gpu_drv/stage_rel/drivers/resman/src/kernel/gpu/gpu_access.c

gpuValidateRegOffset_IMPL(...)
  if (!osIsAdministrator() &&
      !gpuGetUserRegisterAccessPermissions(pGpu, offset))
      return NV_ERR_INSUFFICIENT_PERMISSIONS;
```

On Linux, `osIsAdministrator()` resolves to `NV_IS_SUSER()`, which checks the
kernel admin capability path.

I added a read-only privilege-status query to:

```text
tools/rm_sm_issue_rate_probe.c
```

The current user is not privileged from RM's point of view:

```text
runs/20260520-rm-regops-privilege/rm-probe-gpu0-user.txt
runs/20260520-rm-regops-privilege/rm-probe-gpu4-user.txt

privileged_status status=0x00000000 flags=0x00 priv_user=0 kernel_handle=0 priv_handle=0
exec_reg_ops_read GLOBAL 0x00409660 regStatus=0x04
exec_reg_ops_read GLOBAL 0x00409664 regStatus=0x04
```

Under `sudo -n`, RM reports privileged user and privileged handle:

```text
runs/20260520-rm-regops-privilege/rm-probe-gpu0-sudo.txt
runs/20260520-rm-regops-privilege/rm-probe-gpu4-sudo.txt
runs/20260520-rm-regops-privilege/rm-probe-all-sudo-summary.txt

privileged_status status=0x00000000 flags=0x05 priv_user=1 kernel_handle=0 priv_handle=1
```

With that privilege, read-only `GPU_EXEC_REG_OPS` succeeds for the GV100 FECS
speed-select registers:

```text
CMP-labelled cards:
  0x00409660 FEATURE_READOUT = 0x007000f3
  0x00409664 SM_SPEED_SELECT = 0x00000999

V100-personality cards:
  0x00409660 FEATURE_READOUT = 0x00700100
  0x00409664 SM_SPEED_SELECT = 0x00000999
```

All local GV100 cards still return unsupported for the public issue-rate API:

```text
get_sm_issue_rate_modifier status=0x00000056
```

This exactly matches the source split:

```text
RM regops path:
  privileged readback can access the raw FECS registers.

RM issue-rate API:
  GET_SM_ISSUE_RATE_MODIFIER remains stubbed for pre_TURING/GV100.
```

Other candidate register-access paths are not better than privileged
`GPU_EXEC_REG_OPS`:

```text
NV2080_CTRL_CMD_GR_REG_ACCESS
  MODS/MIG/legacy-shaped path; returns NV_ERR_NOT_SUPPORTED locally.

NV2080_CTRL_CMD_INTERNAL_GR_REG_ACCESS
  internal/MODS forwarding path; returns NV_ERR_NOT_SUPPORTED locally.

NV83DE_CTRL_CMD_DEBUG_EXEC_REG_OPS
  debugger path validates with gpuValidateRegOps or proxies to GPU_EXEC_REG_OPS.

NVB0CC_CTRL_CMD_EXEC_REG_OPS
  profiler/HWPM path, restricted to PM/perfmux-style register operations.

NVA082_CTRL_CMD_HOST_VGPU_DEVICE_EXEC_VF_REG_OPS
  SR-IOV/VF maintenance path for MMU fault/TLB/access-counter aliases, not the
  GV100 FECS speed-select register space.

Diag SetUserRegisterAccessPermissions
  real access-map mutation hook, but compiled only under NV_VERIF_FEATURES or
  VERIF_ONLY_CONTROLS.
```

Interpretation: the closest confirmed path is now **privileged read-only RM
regops**, not an exploit. It gives live FECS state on this host and proves the
register values directly through RM. It does not change the previous blocker:
no supported production GV100 write/override path has been established, and
the public issue-rate API remains deliberately unsupported on GV100.

### 61. CVE / Exploitability Lens For Each Surface

Going forward, each new surface should be checked against public CVE classes
before treating it as a possible path. The check is useful, but it must stay
separate from exploit construction.

Current host versions:

```text
NVIDIA driver: 580.142
Kernel module: /lib/modules/7.0.0-15-generic/kernel/nvidia-580/nvidia.ko
Container toolkit: nvidia-container-toolkit 1.19.0-1
libnvidia-container: 1.19.0-1
```

Official NVIDIA advisories checked:

```text
May 2026 GPU Display Driver bulletin:
https://nvidia.custhelp.com/app/answers/detail/a_id/5821

January 2026 GPU Display Driver bulletin:
https://nvidia.custhelp.com/app/answers/detail/a_id/5747

November 2021 GPU and Tegra Hardware notice:
https://nvidia.custhelp.com/app/answers/detail/a_id/5263

GPU Operator / Container Toolkit CVE table:
https://docs.nvidia.com/datacenter/cloud-native/gpu-operator/25.3.3/security.html
```

Host applicability:

```text
January 2026 R580 Linux driver CVEs:
  Fixed at 580.126.09 for R580 Linux.
  Host is 580.142, so this host appears past that fixed version.

May 2026 R580 Linux Tesla driver CVEs:
  NVIDIA lists R580 Linux Tesla fixed at 580.159.03.
  Host is 580.142, so this host is in the affected-version range by branch and
  version.

Container Toolkit / GPU Operator CVEs:
  Local nvidia-container-toolkit is 1.19.0.
  This is newer than the fixed versions listed for the 2024 container-toolkit
  CVEs in the GPU Operator security table.
```

Surface-to-CVE map:

| Surface | Relevant CVE class | Current assessment |
|---|---|---|
| `NV2080_CTRL_CMD_GPU_EXEC_REG_OPS` / RM regops | Access-control and kernel-mode resource authorization, especially `CVE-2026-24190` / `CWE-862` | Highly relevant as a class. Local evidence shows RM intentionally allows privileged readback and blocks unprivileged access through the access map. No CVE tie to `0x409664` write/control has been found. |
| RM memory/PTE/UVM/HMM controls | `CVE-2026-24190`, `CVE-2026-24194`, `CVE-2026-24195`, `CVE-2026-24196`, plus older integer/overflow classes | Relevant to host risk and GPU-resource authorization, but the strongest local candidates are VA/PTE/UVM resource paths, not FECS speed-select registers. |
| `GR_REG_ACCESS` / `INTERNAL_GR_REG_ACCESS` | Legacy privileged API exposure class, historically closer to `CVE-2021-1052` than `CVE-2021-1051` | Checked. Local source and runtime show this path is MODS/MIG/internal-shaped and returns unsupported locally. |
| SM debugger regops (`NV83DE`) | Debug-resource access-control class | Checked. Source validates through `gpuValidateRegOps` or proxies to the same regops machinery; it is not a wider FECS bypass. |
| Profiler regops (`NVB0CC`) | Profiling/HWPM access-control CVEs | Checked. Source is PM/perfmux/HWPM-reservation oriented; not a general FECS speed-select path. |
| vGPU VF regops / host vGPU APIs | vGPU Manager CVEs such as `CVE-2025-33220`, `CVE-2026-24200`, `CVE-2026-24201` | Relevant only if using vGPU/SR-IOV manager paths. Local bare-metal GV100 FECS path is not this surface. |
| Falcon / FECS / SEC2 / ACR microcontrollers | November 2021 hardware notice: `CVE-2021-1088`, `CVE-2021-1105`, `CVE-2021-1125`, `CVE-2021-23219`, `CVE-2021-34399`, `CVE-2021-34400` | Relevant mostly for observability, debug-register and protected-information questions. NVIDIA notes these require elevated/admin/kernel rights. No local controlled GV100 FECS microcode-loading path was found. |
| VBIOS / flash tooling | NVFlash and firmware update tool CVEs, plus firmware-signing bypass questions | Relevant to provenance and flashing history. No public CVE found here bypasses GV100 VBIOS signing or converts a post-boot host process into a safe limiter-clear path. |
| Container boundary / Vast.ai | Container Toolkit escapes such as `CVE-2024-0132`, `CVE-2025-23266` | Relevant to host/container security. Local toolkit is newer than listed fixed versions. These are not a direct FECS speed-select path. |

Current priority ordering:

```text
1. May 2026 driver/RM access-control CVEs on R580 Linux, because host 580.142
   is below the Tesla R580 fixed version 580.159.03.
2. Falcon/internal-microcontroller CVEs, but only for read-only observability
   and source boundary checks.
3. vGPU/container CVEs only when a future step touches vGPU, SR-IOV, Vast.ai,
   container runtime, or host escape boundaries.
4. Flashing/nvflash CVEs only when a future step touches VBIOS provenance or
   prior-owner flashing history.
```

Operational boundary: CVE research can tell us which code surfaces deserve
extra review. It does not by itself justify exploit construction or live
register writes. The verified productive path remains read-only: privileged RM
regops for FECS state, ROM/init-script comparison for producer attribution, and
source diffing for access-control semantics.

### 62. Tegra / Jetson / DGX Firmware CVEs Are Mostly Analogy, Not Local GV100 Paths

The added CVE list is useful for classifying early-boot and platform-firmware
attack patterns, but most entries are not directly portable to this host or to
PCIe GV100 dGPUs.

Official references checked:

```text
CVE-2018-6242 / Tegra RCM:
https://nvidia.custhelp.com/app/answers/detail/a_id/4660

CVE-2019-5680 / Jetson TX1 nvtboot:
https://nvidia.custhelp.com/app/answers/detail/a_id/4835

CVE-2019-5699 / CVE-2019-5700 / Jetson + Shield bootloader:
https://nvidia.custhelp.com/app/answers/detail/a_id/4910

CVE-2021-34380 family / Jetson MB2:
https://nvidia.custhelp.com/app/answers/detail/a_id/5205

CVE-2023-25518 / Jetson CBoot PCIe without IOMMU:
https://nvidia.custhelp.com/app/answers/detail/a_id/5466

CVE-2022-42275 / DGX A100 BMC SPI flash:
https://nvidia.custhelp.com/app/answers/detail/a_id/5435
```

Local platform check:

```text
hostnamectl:
  Hardware Vendor: Hewlett-Packard
  Hardware Model: HP Z840 Workstation
  Firmware Version: M60 v02.31

dmidecode:
  Product: HP Z840 Workstation
  Baseboard: 2129

lspci:
  no NVIDIA DGX A100 BMC/SBIOS platform
  no local Tegra SoC / Jetson boot chain
  PCIe GPUs are GV100 dGPUs behind PCIe switches
```

Mapping:

| CVE family | Affected surface | Transferability to local GV100 limiter investigation |
|---|---|---|
| `CVE-2018-6242` | Tegra BootROM RCM USB recovery buffer overflow | Conceptual only. NVIDIA's notice explicitly frames this as older Tegra RCM with physical USB recovery; it is not a PCIe GV100 dGPU path. |
| `CVE-2019-5680`, `CVE-2019-5679`, `CVE-2019-5699`, `CVE-2019-5700` | Tegra/Shield `nvtboot` and bootloader image validation | Conceptual only. The pattern is pre-OS bootloader image validation, but local cards use PCIe dGPU VBIOS + signed Falcon firmware, not Jetson `nvtboot`. |
| `CVE-2021-34380` / `34383` / `34384` / `34388` / `34396` / `34397` | Jetson MB2/TegraBoot/bootloader heap or permission bugs | Conceptual only unless a GV100 dGPU Falcon/ACR loader bug with similar input validation is found. No such local producer path has been found. |
| `CVE-2021-1111` | Jetson NV3P USB server bounds check | Physical Jetson provisioning path; not local PCIe GV100. |
| `CVE-2023-25518` | Jetson CBoot initializes PCIe without IOMMU, enabling physical DMA | Useful analogy for early-boot DMA/IOMMU risk. Local host is HP Z840 with PCIe dGPUs already behind OS IOMMU groups; this does not provide a GV100 FECS control path. |
| `CVE-2022-42275`, `CVE-2023-0209` | DGX A100 BMC/SBIOS/platform firmware | Not directly applicable to this host. The machine is not DGX A100 and no DGX BMC/SBIOS path was found locally. |

Local source search for these component names found mostly unrelated or
platform-specific material:

```text
nvtboot / CBoot / MB2 / TegraBoot:
  no local GV100 dGPU producer path found

NVHost:
  MODS platform detection and Tegra-platform support references only

BMC / IPMI / DGX:
  DGX/MODS/fabric-manager/platform test references, not a local HP Z840
  GV100 limiter path
```

Interpretation: these CVEs strengthen the general lesson that NVIDIA early-boot
and platform-firmware bugs can matter when the vulnerable boot chain is present.
For this investigation, they do not supersede the current closest evidence:

```text
local PCIe GV100 dGPU
  -> VBIOS init script writes 0x409664 = 0x999
  -> privileged RM regops can read that state
  -> public GV100 issue-rate API remains stubbed
  -> producer/mapping still points to missing chipfuse/POR/IFF or signed/private
     firmware policy
```

### 63. Legacy Physical-Memory And Sensitive-Register CVEs Map To The Same Access-Control Boundary

The 2016/2017 CVEs are more relevant to this investigation than the Jetson
bootloader family because they describe the exact class of mistake we are
probing: CPU-side access to memory or registers that should be mediated by RM,
firmware, PLMs, or access maps.

Official/public references checked:

```text
CVE-2017-0352 / GPU firmware sensitive control registers:
https://nvidia.custhelp.com/app/answers/detail/a_id/4462
https://nvd.nist.gov/vuln/detail/CVE-2017-0352

CVE-2016-7382 / arbitrary physical memory via missing permissions check:
https://nvd.nist.gov/vuln/detail/CVE-2016-7382

CVE-2016-7389 / Linux nvidia.ko mmap improper validation:
https://nvd.nist.gov/vuln/detail/CVE-2016-7389
```

Relevance:

| CVE | Class | Local mapping |
|---|---|---|
| `CVE-2016-7382` | Missing permissions check allowed arbitrary physical memory access | Historical match for the risk class behind RM memory mapping and BAR/physical mapping APIs. The local host driver is R580.142, far beyond the old R304/R340/R367/R370-era fixed branches, so this is not a direct version match. |
| `CVE-2016-7389` | Linux `nvidia.ko` `mmap()` improper validation allowed arbitrary physical memory access | Historical match for mmap-context validation. The current open kernel module has explicit mmap-context, offset, size, write-protection, and safe-to-mmap checks in `/usr/src/nvidia-580.142/nvidia/nv-mmap.c`. |
| `CVE-2017-0352` | GPU firmware incorrect access control allowed CPU software to access sensitive GPU control registers | Strong conceptual match for FECS/PRI/PLM. NVIDIA's bulletin lists this as GPU firmware, high severity, local attacker with high privileges, and fixed long before current R580. |

Current host `nv-mmap.c` has the expected hardening shape:

```text
/usr/src/nvidia-580.142/nvidia/nv-mmap.c

nvidia_vma_access()
  rejects invalid mmap context
  rejects writes when NV_PROTECT_WRITEABLE is absent
  bounds-checks page index

nvidia_fault()
  rejects non-zero vm_pgoff
  rejects control-device mappings
  respects safe_to_mmap and GPU wakeup state

nvidia_mmap_helper()
  rejects invalid mmap context
  rejects non-zero vm_pgoff
  checks mapped size against memArea size
  separates register, framebuffer, peer I/O, and system-memory paths
  clears VM_WRITE / VM_MAYWRITE when the context is read-only
```

That does not prove the absence of any current mmap bug, but it makes the old
2016 CVEs a historical class reference rather than a directly reusable path on
this host.

For `CVE-2017-0352`, the local source tree contains a much stronger conceptual
anchor in the SBR/PUB safety docs:

```text
integ/gpu_drv/stage_rel/safety/SBR-TU10A/SWE-SBR-007-SWADS.md
integ/gpu_drv/stage_rel/safety/SBR-TU10A/Unit_Design/SWE-SBR-009-SWUD-LITE.md

PUB = PLM Update Binary
PUB runs on SEC2 Falcon
PMU and CTXSW/FECS/GPCCS microcodes run at NS privilege level
PUB lowers selected PLMs so those microcodes can access protected registers
PUB sets decode traps for selected devtools-visible FBPA access
PUB does not accept external input arguments
production PUB offers no testing/debugging capability
debug signed PUB runs only on debug boards
```

Interpretation: the relevant code mechanism is not "find a missing user-space
flag and flip it." NVIDIA has an explicit firmware-mediated PLM architecture
for allowing CPU/Falcon access to sensitive GPU control registers. Our local
privileged RM readback succeeds because RM grants an authorized privileged
read path. The missing unlock path would require a valid producer or policy
artifact that changes the secure PLM/FECS/VBIOS state, not just a stale CVE name.

The best value from these CVEs is therefore triage:

```text
1. Keep auditing mmap/context and regops authorization surfaces.
2. Treat 0x409664 as a sensitive control-register surface protected by access
   maps, PLM policy, and firmware state.
3. Treat SBR/PUB/PLM docs as the strongest local explanation of how NVIDIA
   would intentionally grant internal access without exposing a production
   user-space switch.
```

### 64. Using The Old Vulnerable Drivers Is Probably Not A GV100 Path

The obvious question is whether installing a driver branch from before the
2016/2017 fixes would reopen the sensitive-register or arbitrary-physical-memory
behavior.

This is unlikely to help with local GV100/CMP cards:

```text
CVE-2017-0352 fixed Linux branches listed by NVIDIA:
  R375 -> 375.66
  R381 -> 381.22
  GRID R367 -> 367.106

Public release-note/search evidence:
  GV100 / Tesla V100 support appears later than those fixed vulnerable branches.
  R390.48 is described publicly as adding GV100 and Tesla V100 support.
```

Implication:

```text
pre-fix R367/R375/R381 driver
  -> likely does not bind cleanly to GV100 / Tesla V100 / CMP 100-210
  -> lacks matching GV100 resource-manager HAL, hwref, and signed firmware path
  -> may not build or load on the current kernel/userspace stack
  -> would create host compromise risk without proving a GV100 FECS path
```

Even if a PCI ID were forced, that would not imply a useful state. The limiter
surface here is not only a kernel ioctl. It is a GV100 FECS/CTXSW/PLM/VBIOS
surface that needs the correct chip support and signed firmware state. An older
pre-GV100 vulnerable branch is more likely to fail initialization than to expose
`0x409664`.

The safe research use for those old packages is offline comparison:

```text
1. Download vulnerable and fixed driver packages.
2. Diff strings/symbols/source-visible open pieces around mmap, regops, and
   privilege checks.
3. Compare firmware/blob inventories for register-access/PLM vocabulary.
4. Use the diff to refine current-R580 source review, not as a live downgrade
   plan on this host.
```

### 65. Pre-Release GV100 Driver Hypothesis

A more precise variant is whether there were pre-release/internal GV100 drivers
from before the `CVE-2017-0352` fix.

Date/order evidence:

```text
CVE-2017-0352:
  Ubuntu publication date: 2017-05-09
  Ubuntu fixed nvidia-graphics-drivers-375 package: 375.66
  NVIDIA fixed public Linux branches include R375/R381-era releases

Tesla V100 / GV100 public timeline:
  NVIDIA announced Volta/Tesla V100 in 2017.
  Public server partner V100 systems were announced 2017-09-27.
  NVIDIA developer forum post for Linux 384.98 on 2017-11-02 lists added
  support for Tesla V100-SXM2-16GB and Tesla V100-PCIE-16GB.
  Public reports also describe 390.48 as adding GV100/Tesla V100 support.
```

Interpretation:

```text
There almost certainly were NVIDIA internal pre-release GV100 enablement
drivers before broad public V100 availability.

But there is no evidence that such a branch was:
  1. public,
  2. pre-fix for CVE-2017-0352,
  3. compatible with production GV100/CMP 100-210 cards,
  4. capable of bypassing the later GV100 FECS/PLM/VBIOS speed-select policy.
```

The date ordering actually cuts against the idea. The CVE was publicly fixed
around May 2017 in the R375/R381 era; broad public V100 support appears later
in R384/R390-era material. An internal bring-up driver could have existed, but
that would be an unavailable NVIDIA artifact, not something established by the
public vulnerable-driver trail.

Research value:

```text
Worth searching for:
  R384 beta / early datacenter release notes
  CUDA 9.0 RC / Volta early-access notes
  driver package strings naming GV100 before 384.98
  local nvdrv branch/version breadcrumbs around GV100 bring-up

Not established:
  a public pre-CVE GV100 driver
  a production-card-compatible pre-fix driver
  a usable route to change 0x409664 or the binary reduced-state mapping
```

### 66. Fact Check: The 375.82 / Falcon CVE Claims Are Misattributed

The proposed `375.82` path and the three CVE descriptions do not match the
local changelog or public CVE records.

Local changelog evidence:

```text
integ/gpu_drv/stage_rel/docs/unix/changelog/changelog-375.txt
  2017-07-24 version 375.82
  added support only for:
    GeForce GTX 1080 with Max-Q Design
    GeForce GTX 1070 with Max-Q Design
    GeForce GTX 1060 with Max-Q Design

integ/gpu_drv/stage_rel/docs/unix/changelog/changelog-384.txt
  2017-11-02 version 384.98
  added support for:
    P104-101
    P106-090
    Tesla V100-SXM2-16GB
    Tesla V100-PCIE-16GB

integ/gpu_drv/stage_rel/docs/unix/changelog/changelog-390.txt
  2018-03-28 version 390.48
  added support for:
    Quadro GV100
    Tesla V100-SXM2-32GB
    Tesla V100-PCIE-32GB
    Tesla V100-DGXS-32GB
```

So `375.82` is not a local/public V100 support candidate.

CVE record check:

| Pasted claim | Public record found | Relevance to this investigation |
|---|---|---|
| `CVE-2019-5674` = Falcon microcontroller RCE through malformed Falcon firmware interface | NVD describes NVIDIA GeForce Experience before 3.18, with ShadowPlay/GameStream enabled. Public references frame it as a GeForce Experience local-code-execution / file-write class, not a Linux Tesla driver Falcon RCE. | Not relevant to GV100 FECS limiter. |
| `CVE-2018-6249` = Volta Falcon firmware cryptographic bypass / unauthenticated microcode write | NVD/advisory descriptions identify a GPU Display Driver kernel-mode NULL pointer dereference that may cause DoS or potential EoP. | Kernel-driver bug class only; not evidence of a Volta firmware signature bypass. |
| `CVE-2019-5684` = firmware/NVRM context-register info leak on 375/384 | NVD describes NVIDIA Windows GPU Display Driver DirectX driver bug where a crafted shader can cause out-of-bounds access of an input texture array, leading to DoS or code execution. | Windows DirectX/shader path; not a Linux V100 firmware or FECS register leak. |

Interpretation: these three CVE IDs should not be used as evidence for a
`375.82` V100 firmware/Falcon unlock path. They are either not V100/Linux
driver surfaces or are materially different vulnerability classes than the
pasted descriptions.

The corrected branch remains:

```text
375.82:
  no V100 support in local NVIDIA changelog

384.98:
  first local changelog hit for Tesla V100-SXM2-16GB / V100-PCIE-16GB

390.48:
  local changelog hit for Quadro GV100 and 32GB V100 variants

known FECS limiter evidence:
  VBIOS init writes 0x409664 = 0x999
  privileged RM can read it
  public GV100 issue-rate API remains stubbed
  no CVE-backed write/control path established
```

### 67. Fact Check: Revised RM/vGPU CVE List

The revised list is closer to real NVIDIA driver and vGPU attack surfaces, but
several details still matter for this limiter investigation.

Official bulletin / local-source checks:

| CVE | Corrected public description | Applicability to this host / GV100 limiter |
|---|---|---|
| `CVE-2022-34669` | NVIDIA's November 2022 bulletin describes a Windows user-mode-layer issue where an unprivileged user can access or modify system/application-critical files. | Not a Linux NVRM, vGPU Manager, or FECS path. |
| `CVE-2022-31606` | NVIDIA's August 2022 bulletin describes a Windows `nvlddmkm.sys` `DxgkDdiEscape` validation failure causing kernel-mode out-of-bounds access. | Not a Linux host or GV100 FECS path. The same bulletin has Linux kernel-mode CVEs, but not this ID. |
| `CVE-2021-1056` | Linux `nvidia.ko` does not completely honor OS file-system permissions for GPU device-level isolation. | Real Linux isolation bug class, but the public record is device-file permission/isolation, not a context-switch state-scrub or FECS control-register primitive. |
| `CVE-2025-23282` | NVIDIA's October 2025 bulletin describes a Linux race-condition privilege-escalation vulnerability, and also lists it under vGPU Manager affected components. | Relevant host security surface. Local source shows `NV_ESC_ATTACH_GPUS_TO_FD` exists in old and current paths; current `580.142` wraps the check/allocation path in `nvl->ldata_lock`, while the older nvdrv snippet does not show that lock. This is a useful hardening signal, not proof of an unlock path. |
| `CVE-2024-0126` | NVIDIA's October 2024 bulletin describes a Windows/Linux issue allowing a privileged attacker to escalate permissions; CWE is improper input validation. It is also listed for vGPU Manager. | Real high-severity host/vGPU security class, but it already requires high privilege in the public vector and does not identify FECS speed-select or `0x409664`. |

Local `NV_ESC_ATTACH_GPUS_TO_FD` source comparison:

```text
current host source:
  /usr/src/nvidia-580.142/nvidia/nv.c

  case NV_ESC_ATTACH_GPUS_TO_FD:
      ...
      /* atomically check and alloc attached_gpus */
      down(&nvl->ldata_lock);
      if (nvlfp->num_attached_gpus != 0) ...
      NV_KMALLOC(nvlfp->attached_gpus, arg_size);
      ...
      nvlfp->num_attached_gpus = num_arg_gpus;
      up(&nvl->ldata_lock);

older nvdrv source:
  integ/gpu_drv/stage_rel/drivers/resman/arch/nvalloc/unix/Linux/nv.c

  case NV_ESC_ATTACH_GPUS_TO_FD:
      if (num_arg_gpus == 0 || nvlfp->num_attached_gpus != 0 ||
          arg_size % sizeof(NvU32) != 0)
          return EINVAL-style status
      NV_KMALLOC(nvlfp->attached_gpus, arg_size)
      memcpy(...)
      nvlfp->num_attached_gpus = num_arg_gpus
```

Interpretation:

```text
Old drivers are absolutely not safe as a general host-security strategy.
They carry real kernel/vGPU attack surface, and several later bulletins fix
classes that did not exist in the 2017/2018 codebase.

But host compromise and FECS speed-select control are different boundaries.
The limiter evidence still points to:
  VBIOS selected init script -> FECS 0x409664 = 0x999
  GV100 binary FMLA/IMLA/DP reduced state
  privileged readback possible
  public production write/override path not found
  exact reduced-state producer still behind missing chipfuse/POR/IFF or
  signed/private Falcon/RM policy
```

Practical conclusion for the investigation: CVEs remain useful as a search
lens for access-control and resource-routing code, especially around NVRM
ioctls, vGPU VF regops, legacy privileged APIs, mmap, and PTE/VA controls. They
do not, by themselves, establish a path to clear or reprogram GV100 FECS
speed-select state.

### 68. nvflashk / OMGVflash Are PROM Tools, Not General RM Regops Tools

Public `nvflashk` documentation describes it as a patched `nvflash` variant
for flashing signed NVIDIA VBIOS images across board-ID / subsystem-ID mismatch
checks. It explicitly frames the bypassed checks as image/device metadata:

```text
GPU PCI Device ID
PCI Subsystem ID
Board ID
Hierarchy
other software-defined metadata
```

The same public README says it allows flashing "nearly any signed BIOS" and
lists unsigned/modified BIOSes as not tested. That matches the local
architecture finding: these tools are about the PROM/VBIOS update path, not an
arbitrary FECS/PGRAPH register-operation interface.

Local source also distinguishes the two paths:

```text
PROM / VBIOS path:
  RM/devinit reads/writes PROM/VBIOS media.
  Some SKUs use constrained Nvflash Falcon ucode.
  Turing source reads secure scratch bits that decide whether NVFLASH will
  write to EEPROM.

Privileged RM regops path:
  NV2080_CTRL_CMD_GPU_EXEC_REG_OPS / debugger regops validate register offsets
  against access maps and privilege state.
  GV100 user maps omit FECS speed-select offsets.
```

Relevant local files:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/devinit/pascal/vbios_flcnucodegp104.c
  vbiosHandleConstrainedNvflashUcode_GP104(...)
  vbiosLoadFalconUcode(... VBIOS_FLCN_UCODE_CONSTRAIN_NVFLASH ...)

dev/gpu_drv/stage_rel/drivers/resman/kernel/devinit/nv/vbios_flcnucode.c
  vbiosGetConstrainedNvflashUcodeImage(...)

dev/gpu_drv/stage_rel/drivers/resman/kernel/devinit/turing/vbiostu102.c
  secure scratch bit "that decides if NVFLASH will write to EEPROM"
```

Interpretation:

```text
nvflash / nvflashk / OMGVflash may be able to write a signed image to EEPROM
when their checks and the GPU's own acceptance rules allow it.

They do not appear to provide:
  arbitrary NV2080_CTRL_CMD_GPU_EXEC_REG_OPS access
  a way to bypass GV100 user-register access maps
  a direct live write to FECS 0x409664
  a way to run unsigned FECS/SEC2/GPCCS firmware on a production fused GPU
```

For this investigation, that means patched flash tools are relevant only to
the boot/PROM branch: if a valid signed donor image exists whose selected init
script does not assert `0x409664 = 0x999`, a flash tool is the class of tool
that would write that image. They are not evidence of a privileged RM register
path for live limiter control.

### 69. VBIOS Chain-Of-Trust Code And CVE/Exploit Surface Review

The local GV100 VBIOS chain-of-trust path is mostly NVIDIA custom code and
Falcon/FWSECLIC-assisted verification, not a linked OpenSSL/mbedTLS-style
library with a simple version-to-CVE mapping.

Relevant local files:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/devinit/volta/vbiossecuritygv100.c
dev/gpu_drv/stage_rel/drivers/resman/kernel/devinit/maxwell/vbiossecuritygm200.c
dev/gpu_drv/stage_rel/drivers/resman/kernel/devinit/nv/vbioscrypto.c
dev/gpu_drv/stage_rel/drivers/resman/kernel/devinit/nv/vbiosnvsha256.c
dev/gpu_drv/stage_rel/drivers/resman/kernel/devinit/nv/vbiosx509derdecode.c
dev/gpu_drv/stage_rel/drivers/resman/kernel/devinit/maxwell/vbioscert20rootpubkey.h
```

GV100-specific flow:

```text
vbiosProcessSecurity_GV100(...)
  if DEVINIT_BY_SECURE_BOOT:
      handle FWSEC secure boot
      read VBIOS_SECURITY_INFO from GSP EMEM
  else:
      force FWSEC VV command down PMU path on GV100
      vbiosProcessFwseclicCommand_HAL(... CMD_VV ...)
      vbiosCheckFwseclicStatus_HAL(...)
      _vbiosGetSecurityInfoFromPmuVV(...)
```

Notable source comment:

```text
GV100 VBIOS VV command has bugs when executed on non-PMU path.
On GV100, FWSEC_ON_SEC2 is only set for HULK purposes.
So just use PMU path for VV.
```

There is also a Volta note that production GV100 security firmware does not
write the newer GFW_BOOT status register used for Turing development:

```text
production GV100 (core88) security firmware (FWSECLIC) does not write this register
This HAL was wired to Volta to support Turing development (core89)
```

Host-side BCRT20 / certificate logic:

```text
_vbiosExtractSignatureFromImageAndVerify(...)
  validates signature header version, digest algorithm, signature/hash sizes
  decrypts RSA signature with vendor public key
  compares decrypted hash to computed VBIOS image hash
  separately handles preservation-table signature

_vbiosCert20VerifyChainOfTrust(...)
  parses embedded root certificate from vbioscert20rootpubkey.h
  verifies root certificate against itself
  walks fixed chain levels
  verifies each child cert against the selected parent

_vbiosVerifyCert20Signature(...)
  hash: DM-over-AES128 or SHA-256
  crypto: RSA-1024
```

Core crypto/parser components:

| Component | Local implementation | CVE/exploit relevance found |
|---|---|---|
| SHA-256 | `vbiosnvsha256.c`, derived from Olivier Gay FIPS 180-2 SHA-256 code, locally modified by NVIDIA/Atmel | No named CVE surfaced for this embedded SHA-256 implementation in this context. |
| SHA-1 | `nvSha1.h` / `sha1Generate(...)` wrapper for older paths | SHA-1 is deprecated cryptographically, but the GV100/BCRT20 path of interest uses SHA-256 or DM-over-AES for certificate/image verification. |
| RSA | custom `vbiosRSADecrypt(...)` + big-number/Montgomery helpers | Custom PKCS#1-v1.5-like verification surface. No public CVE found that turns this local implementation into a GV100 VBIOS signing bypass. |
| X.509 / DER | custom `vbiosx509derdecode.c` | Parser is narrow and assert-heavy. It is an audit target for malformed-cert robustness, but no public CVE found tying it to GV100 VBIOS bypass. |
| BCRT20 certificate chain | custom NVIDIA BCRT20 parser/verifier in `vbiossecuritygm200.c` and GV100 FWSECLIC metadata path | Strongest local trust boundary. No public CVE found for production GV100 BCRT20 chain bypass. |
| FWSECLIC / HULK / secure metadata | Falcon-assisted path, with security info returned through PMU/GSP/FWSEC scratch/DMEM/EMEM | Conceptually closest to Falcon/security-firmware CVE classes, but no local controlled unsigned-code or cert-bypass path found. |

Potential audit notes from source, without claiming exploitability:

```text
1. RSA-1024 is old and deprecated, but no practical public factorization or
   NVIDIA VBIOS signing-key recovery was found.

2. The custom X.509/DER decoder does substantial pointer arithmetic and relies
   on asserted tags/lengths. That is a malformed-input robustness surface, not
   by itself a signature bypass.

3. vbiosRSADecrypt validates PKCS#1 padding and then returns the decrypted
   payload for exact hash comparison. This is verification-only, not a signing
   primitive.

4. MODS/build-time paths sometimes relax handling of preservation table
   signature failures for bringup/MODS conditions. Production image validation
   still requires the main image signature to verify.

5. GV100 routes verification through FWSECLIC VV on PMU because of GV100
   path-specific bugs. This is a local implementation wrinkle worth tracking,
   but it is not evidence of a bypass.
```

Public CVE/exploit mapping:

```text
No public CVE found:
  - bypassing GV100 BCRT20/RSA VBIOS signature verification
  - loading unsigned GV100 FECS/SEC2/GPCCS firmware on production fused GPUs
  - exploiting the local Olivier Gay-derived SHA-256 implementation
  - exploiting this local X.509/BCRT parser to produce a trusted modified VBIOS

Related but not directly portable:
  - 2022 leaked NVIDIA Windows code-signing certificates: software signing
    trust domain, not GPU VBIOS/Falcon production signing.
  - 2025 HGX/DGX VBIOS unsafe debug-access CVE-2025-23301: real VBIOS
    misconfiguration class, but Hopper HGX/DGX/Blackwell platform scope, not
    GV100 PCIe VBIOS signature bypass.
  - 2024/2025 nvflash/flash-tool and vGPU/driver CVEs: host or platform
    software trust boundaries, not a known GV100 cert-chain bypass.
```

Interpretation:

```text
The chain-of-trust code gives us excellent vocabulary and boundary locations:
  BCRT20 certificates
  RSA-1024 signatures
  SHA-256 / DM-over-AES hashes
  root public cert
  FWSECLIC VV/SB/HULK commands
  PMU/GSP/SEC2 execution routing
  preservation table handling

It does not currently give us:
  a private key
  a public production cert bypass
  a known vulnerable third-party crypto library version
  a path to make a modified GV100 VBIOS trusted
```

### 70. Manual Review Notes On The VBIOS Trust Implementation

This pass reviewed the local VBIOS trust code itself, plus local changelog
breadcrumbs and public NVIDIA bulletins, for implementation flaws or later
discoveries that might change the conclusion in section 69.

Findings, ordered by practical relevance:

1. The custom X.509/DER parser is assert-heavy and has weak end-bound tracking.

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/devinit/nv/vbiosx509derdecode.c

vbiosPEMToBinary:            20-82
derDecodeLength:             84-107
DecodeAlgorithmIdentifier:   141-164
NameDecode:                  168-249
RsaPublicKeyDecode:          265-290
SubjectPublicKeyInfoDecode:  293-323
TBSCertificateDecode:        404-466
X509CertificateDecode:       507-532
```

Notable issues:

```text
  - PEM decode assumes a fixed header offset and scans until '-' with no
    explicit end pointer.
  - DER length decoding advances pointers without carrying an input-buffer end.
  - Several tag/algorithm checks use RM_ASSERT rather than normal error returns.
  - OID handling deliberately keeps only the last byte or last 8 bytes.
  - Some decoded lengths are NvU64 but later cast down to NvU32/NvU16 for
    allocation, copy, or metadata.
```

Interpretation: this is a real malformed-input robustness surface. It does not
by itself show a certificate bypass because the signed hash comparison still has
to pass, but it is exactly the style of parser code NVIDIA would likely harden
in a later implementation.

2. RSA decrypt/padding handling has brittle length and bounds assumptions.

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/devinit/nv/vbioscrypto.c

vbiosRSADecrypt: 240-286
```

Notable issues:

```text
  - len <= modulus.size is RM_ASSERT-only.
  - PKCS#1-style padding walk decrements offset while reading 0xff padding.
  - Caller _vbiosVerifyImageSignature asserts decryptLen == expected size, but
    then compares a fixed NV_HASH_SIGNATURE_SIZE_BYTE anyway.
```

Caller:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/devinit/maxwell/vbiossecuritygm200.c
  _vbiosVerifyImageSignature: 850-878
```

Interpretation: this looks like a malformed-signature crash/robustness risk
before it looks like a forgery path. The final comparison is still against the
computed image hash.

3. BCRT20 chain handling is intentionally narrow.

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/devinit/maxwell/vbiossecuritygm200.c
  _vbiosCert20VerifyChainOfTrust: 1044-1105
```

The source comment says the logic only works when there is one certificate at
all chain levels except the last, because there is no way to choose among
multiple parent certificates. The code also relies on `RM_ASSERT(pNextCertificate)`
after each chain-level scan.

Interpretation: this is a design limitation and an audit target for weird
certificate-chain layouts. It is not evidence that a modified VBIOS can be made
trusted; every certificate encountered at a level is still verified against the
selected parent.

4. The signature header has a few permissive fields.

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/devinit/maxwell/vbiossecuritygm200.c
  _vbiosExtractSignatureFromImageAndVerify: 724-848
```

Notable issues:

```text
  - digest_algo is tested as a bitmask of allowed values, not exact equality.
  - sig_format is explicitly not checked because the field was reserved for
    later reuse.
  - MODS builds can skip preservation-table signature checking when the
    preservation table is updated.
```

Interpretation: none of these skips the main image signature check. The
preservation table is documented as bringup/floorsweeping data, with zero length
on production systems.

5. GV100 has known FWSECLIC routing wrinkles.

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/devinit/volta/vbiossecuritygv100.c
  vbiosProcessSecurity_GV100: 632-760
```

Notable source comments:

```text
  - Non-silicon skips BIOS certificate verification.
  - GV100 VBIOS VV command has bugs when executed on the non-PMU path.
  - Turing and Volta VBIOSes have a bug where certificateType is reported as
    vbiosType, and RM patches the returned type.
```

Interpretation: these are real implementation quirks. The non-silicon skip is
not a production-card path, and the PMU routing workaround does not imply an
unsigned VBIOS path.

6. Changelogs and public bulletins contain related security classes, not a
GV100 VBIOS trust bypass.

Local changelog breadcrumbs:

```text
diag/mods/docs/engnotes_willow.txt
  200329521: [RM-Security] Execute LS Devinit by sending SECURE_BOOT command to FWSECLIC
  200356219: fix the vbios certificate error
  HULK cert cleanup
  Enabling FWSECLIC in simulation
```

Public NVIDIA bulletins:

```text
2021 GPU/Tegra internal microcontroller notice:
  Volta is affected by debug/register/microcode vulnerability classes, but all
  listed attacks require OS administrative/kernel rights.

2025 HGX/DGX VBIOS/LS10 notice:
  CVE-2025-23301 is a real unsafe-debug-access VBIOS misconfiguration class,
  but the affected products are Hopper HGX/DGX and Blackwell HGX/DGX.

2026 display-driver notice:
  CVE-2026-24190 is a real improper-access-to-GPU-resources class, but it is a
  driver/kernel-mode issue, not a documented GV100 VBIOS signer bypass.
```

Interpretation:

```text
The most noteworthy flaws are parser/length/assert robustness concerns and
FWSECLIC routing quirks. They make the code look old and hand-rolled, but this
review still did not find a direct way to make a modified GV100 VBIOS pass
production trust checks or to derive the hidden FMLA_REDUCED -> 1/16 policy.
```

### 71. Offline Harness Confirms The RSA Padding Walk Is Crashable

I built a standalone, non-hardware harness that models only the padding-walk
part of `vbiosRSADecrypt()`. It does not link RM, does not access PCI/BAR0, and
does not load or flash a VBIOS.

Harness:

```text
projects/gpu-falcon-research/tools/vbios_rsa_padding_walk_harness.c
```

Run output:

```text
projects/gpu-falcon-research/runs/20260520-vbios-rsa-padding-harness/run.log
```

The modeled logic is:

```text
offset = res_size - 1
check trailing 0x00
offset--
check 0x01 PKCS padding marker
offset--
while res_value[offset] == 0xff:
    offset--
check delimiter 0x00
```

ASAN/UBSAN result:

```text
case=valid-shaped rc=0 out_len=4
summary case=valid-shaped exit=0

case=unterminated-ff-padding size=16
SUMMARY: AddressSanitizer: SEGV ... vbios_rsa_padding_walk_harness.c:45
summary case=unterminated-ff-padding signal=6

case=too-small-size size=1
SUMMARY: AddressSanitizer: SEGV ... vbios_rsa_padding_walk_harness.c:41
summary case=too-small-size signal=6
```

Interpretation:

```text
The padding walk is crashable in an offline model when:
  - the 0xff padding run is unterminated by a 0x00 delimiter, or
  - res_size is too small for the assumed trailing 0x00 + 0x01 structure.

The crash is a read from a wrapped/out-of-range offset. This confirms the code
review finding as a robustness issue. It still does not establish a VBIOS trust
bypass, because the normal success path must return a decrypted payload matching
the computed image hash.
```

### 72. Device-Side VBIOS RSA Equivalent Found, But It Is Legacy MSDEC/MSDEC4

There is a device-side VBIOS RSA implementation in the tree, but it is not the
GV100 FWSECLIC source. It is an older MSDEC/MSDEC4 secure VBIOS app:

```text
dev/gpu_drv/stage_rel/drivers/common/msdec3/sec/common/sec_vbios_insecure_loader.c
dev/gpu_drv/stage_rel/drivers/common/msdec3/sec/common/vbiosapp/sec_vbios_app0.c
dev/gpu_drv/stage_rel/drivers/common/msdec3/sec/common/vbiosapp/sec_vbios_bn_rsa.c
dev/gpu_drv/stage_rel/drivers/common/msdec3/sec/common/vbiosapp/sec_vbios_bn_lib.h
dev/gpu_drv/stage_rel/drivers/common/msdec4/makefile.nvmk
```

The Falcon loader dispatches app ID `SEC_VBIOS_APPINTERFACE_HEADER_APPID_RSA`
to `sec_vbios_app0()`:

```text
sec_vbios_insecure_loader.c:59-68
```

The MSDEC4 makefile compiles this as Falcon-side code:

```text
msdec4/makefile.nvmk:605-610
  sec_vbios_insecure_loader.c
  vbiosapp/sec_vbios_app0.c
  vbiosapp/sec_vbios_bn_rsa.c
```

The device-side padding checker is:

```text
sec_vbios_app0.c:76-117
RSA_padding_check_PKCS1_type_1(...)
```

Notable review observations:

```text
1. It is bounded differently than host vbiosRSADecrypt().
   The scan loop is for (i = j; i >= 0; i--), so it does not have the same
   obvious unsigned-offset wraparound shape.

2. It still contains suspicious old C:
   if ((num != (flen+1)) || (p[flen]=0 && p[flen-1]!= 1))

   That assigns p[flen] = 0 inside the condition instead of comparing. It is
   also writing one byte past the buffer described by from + flen if from has
   exactly flen bytes.

3. Failure paths halt the Falcon-side app:
   falc_halt() under MSDEC4, or while(1) otherwise.
```

Interpretation:

```text
This is a genuine local device-side analog for VBIOS RSA padding code, and it is
also old/fragile. However, it is legacy MSDEC/MSDEC4 secure VBIOS app code, not
the GV100 FWSECLIC VV implementation that production GV100 uses.
```

For GV100 FWSECLIC, the local tree exposes interfaces and error codes rather
than the implementation body:

```text
dev/gpu_drv/stage_rel/uproc/fbflcn/inc/vbios/ucode_interface.h:795-912
  IN_CMD_FWSECLIC_VV / OUT_CMD_FWSECLIC_VV

dev/gpu_drv/stage_rel/uproc/fbflcn/inc/vbios/ucode_postcodes.h:541-574
  VBIOS_VERIFY_CERT_PARSE_FAIL
  VBIOS_VERIFY_CERT_VERIFY_FAIL
  VBIOS_VERIFY_BIOS_SIG_FAIL
  HULK / UGPU / cert extension errors
```

Interpretation:

```text
The codebase confirms that device-side FWSECLIC performs BIOS certificate
parsing, certificate verification, BIOS signature verification, HULK checks, and
metadata return. The actual GV100 FWSECLIC verifier source was not found in this
tree; it is represented by headers/interfaces/error tables, not a C
implementation equivalent to sec_vbios_app0.c.
```

### 73. Newer Versions Visible Locally

Yes: the local `integ` tree has newer host-side VBIOS security code than the
older `dev` tree.

Newer local host-side files include:

```text
integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/devinit/vbioscrypto.c
integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/devinit/arch/volta/vbiossecuritygv100.c
integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/devinit/arch/ampere/vbiossecurityga10x.c
integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/devinit/arch/hopper/vbiossecuritygh100.c
integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/devinit/arch/ada/vbiossecurityad102.c
```

The host-side `vbiosRSADecrypt()` padding walk is still present in the newer
`integ` code:

```text
integ/.../devinit/vbioscrypto.c:239-286
  NV_ASSERT(len <= rsaPublicKey.modulus.size)
  offset = res.size - 1
  while (*(res.value + offset) == 0xFF) offset--
```

A diff against the older `dev` file shows infrastructure/refactor churn
around includes, memory helpers, and AES plumbing, but not a hardening of this
padding-walk logic.

The newer GV100 host orchestration is more informative. `integ` routes
production GV100 VBIOS verification through FWSECLIC on the PMU path:

```text
integ/.../arch/volta/vbiossecuritygv100.c:637-735
  vbiosProcessSecurity_GV100(...)
  FALCON_APPLICATION_INTERFACE_DMEM_MAPPER_V3_CMD_VV
  This codepath is taken for GV100 (no DEVINIT_BY_SECURE_BOOT)
  GV100 VBIOS VV command has bugs when executed on non-PMU path
  just use PMU path for VV
```

It also says the production GV100/core88 FWSECLIC does not write the later
GFW boot-status register:

```text
integ/.../arch/volta/vbiossecuritygv100.c:873-875
```

Newer generations move further away from the old host path:

```text
integ/.../arch/hopper/vbiossecuritygh100.c:62-95
  vbiosHandleSecureBoot_GH100(...)
  FSP boot command / secure boot status

integ/.../arch/hopper/vbiossecuritygh100.c:103-153
  HULK license payload is sent to FSP
```

The newer firmware interface headers are richer than the older GV100 headers,
but still not the FWSECLIC verifier source body:

```text
integ/gpu_drv/stage_rel/uproc/libs/vbios/inc/ucode_interface.h
  IN_CMD_FWSECLIC_VV
  OUT_CMD_FWSECLIC_VV
  FWSECLIC_READ_VBIOS_DESC
  FWSECLIC_FRTS_CMD
  OUT_CMD_FWSECLIC_SB

integ/gpu_drv/stage_rel/uproc/libs/vbios/inc/ucode_postcodes.h
  VBIOS_VERIFY_CERT_PARSE_FAIL
  VBIOS_VERIFY_CERT_VERIFY_FAIL
  VBIOS_VERIFY_BIOS_SIG_FAIL
  CERT20_VDPA_*
  CERT21_*
  CERT22_*
  BCRT2X_*
  FUSEREWORK_*
  FUB_*
```

Interpretation:

```text
The newer local code confirms the transition:

1. GV100 production VBIOS verification is FWSECLIC-mediated, with RM mostly
   orchestrating commands and reading metadata/status.
2. The visible host-side RSA helper remained fragile through the newer local
   host tree, but it is not the production GV100 verifier body.
3. Later Hopper/Ada-style code pushes more trust-chain work into secure
   firmware/FSP paths.
4. The exact GV100 FWSECLIC verifier source still is not present locally; the
   tree exposes command ABI, metadata structures, and error codes.
```

Public check:

```text
NVIDIA/open-gpu-kernel-modules main currently reports NVIDIA_VERSION 595.71.05.
A recursive GitHub tree query did not expose vbiossecurity*, vbioscrypto*,
fwseclic*, vbioscert*, or ucode_postcodes* paths in that public repo.
```

So the local `integ` tree is still more useful for this investigation than the
public open kernel module release.

### 74. Focused OSS-Style Static/Dynamic Scan Pass

The common OSS tools suggested for this kind of review were checked on this
host:

```text
cppcheck      not installed
flawfinder    not installed
clang-tidy    not installed
clang         not installed
valgrind      not installed
afl-fuzz      not installed
binwalk       not installed
gcc           installed: Ubuntu 15.2.0
```

So the first pass used:

```text
1. GCC AddressSanitizer/UBSan on the existing offline vbiosRSADecrypt padding
   harness.
2. A focused local pattern scanner for the exact C review classes we care
   about in VBIOS security / FWSECLIC-adjacent source.
```

Artifacts:

```text
projects/gpu-falcon-research/tools/focused_c_security_scan.py
projects/gpu-falcon-research/runs/20260520-static-security-scan/focused-files.txt
projects/gpu-falcon-research/runs/20260520-static-security-scan/focused-c-security-scan.json
projects/gpu-falcon-research/runs/20260520-static-security-scan/focused-c-security-scan.md
projects/gpu-falcon-research/runs/20260520-static-security-scan/vbios-rsa-padding-harness-asan.log
```

Scan scope:

```text
integ/.../devinit/vbioscrypto.c
integ/.../devinit/vbiosx509derdecode.c
integ/.../devinit/vbiossecurity.c
integ/.../devinit/arch/volta/vbiossecuritygv100.c
integ/.../devinit/arch/maxwell/vbiossecuritygm200.c
dev/.../msdec3/sec/common/vbiosapp/sec_vbios_app0.c
dev/.../msdec3/sec/common/vbiosapp/sec_vbios_bn_rsa.c
```

Focused pattern counts:

```text
allocation-from-parser-value: 13
assert-only-bound: 9
assignment-in-condition: 4
firmware-hard-halt: 5
raw-copy: 31
unguarded-decrement: 20
unsigned-size-minus-one: 1
```

The highest-signal confirmed item remains the host-side RSA padding walk:

```text
integ/.../devinit/vbioscrypto.c:250-283
  NV_ASSERT(len <= rsaPublicKey.modulus.size)
  offset = res.size - 1
  offset--
  while (*(res.value + offset) == 0xFF) offset--
  portMemAllocNonPaged(offset)
  reverseMemCopy(..., offset)
```

The ASan harness reproduced:

```text
valid-shaped: rc=0
unterminated-ff-padding: ASan SEGV at modeled line 45
too-small-size: ASan SEGV at modeled line 41
```

This supports a robustness finding for the host-modeled post-RSA-decrypt
padding walk. It still does not prove a trust bypass because the successful
path must return bytes matching the computed VBIOS hash.

The highest-signal legacy device-side item remains:

```text
dev/.../msdec3/sec/common/vbiosapp/sec_vbios_app0.c:81
if ((num != (flen+1)) || (p[flen]=0 && p[flen-1]!= 1))
```

This is both assignment-in-conditional and a one-past write shape relative to a
`from + flen` pointer model. Failure paths in the same legacy MSDEC app halt:

```text
sec_vbios_app0.c:213/244  falc_halt()
sec_vbios_app0.c:215/246  while(1)
sec_vbios_bn_rsa.c:51     falc_halt()
```

Additional triage-worthy parser shapes:

```text
integ/.../devinit/vbiosx509derdecode.c:48-58
  allocates len * 3 / 4 for PEM decode, then loops until '-' in input

integ/.../devinit/vbiosx509derdecode.c:519-530
  assert-style bit-string checks precede (length - 1) allocation/copy
```

Interpretation:

```text
The scan strengthens the source-review case that old NVIDIA VBIOS security
parsers contain fragile C patterns around variable lengths, reverse walks,
assert-only validation, and firmware halt failure modes.

It does not change the main limiter conclusion: these findings are in host
helpers or legacy MSDEC/MSDEC4 device code, while the production GV100
FWSECLIC verifier body remains absent from the visible tree.
```

### 75. Z3 SMT Model Corroborates The Padding-Walk Counterexamples

SMT/symbolic tool inventory:

```text
z3      not installed initially; installed as project-local z3-solver 4.16.0
klee    not installed
angr    not installed
semgrep not installed
cvc5    not installed
stp     not installed
symcc   not installed
```

A bounded Z3 model was added for the post-RSA-decrypt padding walk only:

```text
projects/gpu-falcon-research/tools/vbios_rsa_padding_smt.py
projects/gpu-falcon-research/runs/20260520-smt-padding-model/vbios-rsa-padding-smt.json
projects/gpu-falcon-research/runs/20260520-smt-padding-model/vbios-rsa-padding-smt.md
```

The modeled algorithm is:

```text
offset = res.size - 1
require res[offset] == 0
offset--
require res[offset] == 1
offset--
while res[offset] == 0xff:
    offset--
require res[offset] == 0
```

Z3 found these satisfiable cases:

```text
valid-shaped:
  size=16
  bytes=ff ff ff ff 00 ff ff ff ff ff ff ff ff ff 01 00
  reverse walk stops at delimiter offset 4

too-small-underflow:
  size=1
  bytes=00
  first check passes, then offset-- wraps before the second byte read

unterminated-ff-padding:
  size=16
  bytes=ff ff ff ff ff ff ff ff ff ff ff ff ff ff 01 00
  trailing 00/01 checks pass, every lower byte is 0xff, and the loop
  decrements below zero; unsigned offset becomes 0xffffffff before the next read
```

Interpretation:

```text
The SMT model independently corroborates the ASan harness result: the reviewed
padding walk has satisfiable malformed input shapes that reach out-of-bounds
reads under the modeled assumptions.

This is still a robustness/crashability finding for the modeled host helper,
not a VBIOS signature bypass or a production GV100 FWSECLIC exploit.
```

### 76. Can SMT/Symbolic Tools Recover The Register-To-1/16 Map?

Answer:

```text
Only if the implementation body is present.
```

SMT/symbolic tools can prove, search, or generate counterexamples over code or
binary semantics. They cannot infer a hidden hardware/Falcon scheduler mapping
from the register dictionary alone.

What the tools can recover from visible code:

```text
Turing/Ampere/later:
  source contains explicit 3-bit speed-select fields
  source contains explicit divisor names
  MODS/RM code maps selector fields to 1/2, 1/4, 1/8, 1/16, 1/32, 1/64

Examples:
  integ/.../diag/mods/gpu/turinggpu.cpp
  integ/.../diag/mods/gpu/amperegpu.cpp
  integ/.../drivers/resman/src/physical/gpu/gr/arch/ampere/gr_ga100.c

Concrete visible path:
  PGRAPH/FUSE FEATURE_READOUT or FEATURE_OVERRIDE field
  -> SM_SPEED_SELECT_FMLA16
  -> FMLA16_REDUCED_SPEED_1_16
  -> public RM/MODS issue-rate value
```

The latest targeted source pass again found the explicit later-generation
mapping:

```text
amperegpu.cpp:
  GetIssueRate(...)
  MODS_FUSE_FEATURE_OVERRIDE_SM_SPEED_SELECT_FMLA16_REDUCED_SPEED_1_16

turinggpu.cpp:
  GetIssueRate(...)
  MODS_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT_FMLA16_REDUCED_SPEED_1_16

gr_ga100.c:
  grGetSmIssueRateModifier_GA100(...)
  ct_assert(NV2080_CTRL_GR_GET_SM_ISSUE_RATE_MODIFIER_FMLA16_REDUCED_SPEED_1_16 ==
            NV_FUSE_FEATURE_OVERRIDE_SM_SPEED_SELECT_FMLA16_REDUCED_SPEED_1_16)
```

What the tools cannot recover from the visible GV100 corpus:

```text
GV100:
  register dictionary exposes FMLA_FULL_SPEED vs FMLA_REDUCED_SPEED
  VBIOS init writes FMLA_REDUCED + FMLA_OVERRIDE
  live FECS readback stays at 0x00000999
  benchmarks show effective 1/16 HMMA/Tensor issue-rate
  public RM issue-rate readback is stubbed for pre-Turing
  MODS GV100/Volta classes do not implement issue-rate read/override
  public GV100 FECS/SEC2 disassembly does not show 0x409664 / 0x409660 /
  0x19900 / 0x999 / named FMLA speed-select producer
```

Targeted disassembly search result:

```text
projects/gpu-falcon-research/runs/20260520-public-gv100-firmware/*.envydis*.txt

No direct semantic hit for:
  0x409664
  0x409660
  0x00019900
  0x00000999
  SM_SPEED_SELECT
  FMLA_REDUCED_SPEED
  FMLA16_REDUCED_SPEED_1_16
```

The only `0x999` matches in the public disassembly were instruction addresses,
not the register value:

```text
acr_ucode_unload.envydis.fuc5.txt:00000999
sec2_image.envydis.fuc5.txt:00000999
```

Interpretation:

```text
The map we can prove from visible artifacts is:

VBIOS selected init script
  -> writes 0x409664 with 0x999
  -> sets GV100 FECS FMLA_REDUCED + FMLA_OVERRIDE
  -> local HMMA/Tensor throughput measures as 1/16
  -> later NVIDIA RM/MODS code names the same issue-rate class as
     FMLA16_REDUCED_SPEED_1_16

The map we cannot yet prove from visible artifacts is:

GV100 FECS FMLA_REDUCED
  -> exact internal scheduler/divider implementation
  -> 1/16
```

For SMT/KLEE/angr to identify that missing edge, one of these inputs is needed:

```text
1. The production GV100 FECS/GR scheduler firmware body containing the divisor
   decision.
2. A fuller disassembly/decompilation that includes the state machine reading
   the binary speed-select bit.
3. Generated chipfuse/POR/IFF metadata that documents the reduced-state policy.
4. Differential live traces from a full-speed V100 and a capped card at the
   internal scheduler/FECS state level, not just the public register/readout
   level.
```

Without one of those, SMT can formalize our constraints but not invent the
missing implementation:

```text
FMLA_REDUCED = 1
measured_issue_rate = 1/16
later_generations(FMLA16_REDUCED_SPEED_1_16) = explicit selector

This proves consistency, not causality inside the hidden GV100 implementation.
```

### 77. Likely Physical Meaning Of The 1/16 Fraction

Inference, not directly proven:

```text
The cheapest implementation is probably an SM scheduler issue-rate gate, not
physical Tensor Core disablement.
```

Why:

```text
1. Later NVIDIA code calls the feature an "SM issue-rate modifier".
2. Later Turing/Ampere fields expose power-of-two divisors:
   1/2, 1/4, 1/8, 1/16, 1/32, 1/64.
3. MODS GetIssueRate() returns fractional issue rates from those fields.
4. GV100 local HMMA timing shows the instructions still execute, but issue
   spacing expands by roughly 14-16x.
5. Nsight reports Tensor pipe activity at exactly 6.25%, not zero active Tensor
   capability.
6. The VBIOS/FECS bit is named speed-select/reduced, not floorsweep/disable.
```

Cheap hardware shape:

```text
per-instruction-family scheduler gate:

  if family == FMLA/HMMA and speed_select == REDUCED:
      allow_issue only when low counter bits match a selected phase

For a 1/16 divisor:

  allow_issue = (counter[3:0] == phase)
```

Equivalent implementations could be:

```text
1. A modulo-N issue counter.
2. A token bucket refilled every 16 scheduler opportunities.
3. A duty-cycle mask in the warp scheduler / tensor dispatch path.
4. A scoreboard/ready gate that withholds HMMA issue eligibility for 15 of 16
   slots.
```

This is much cheaper than physically changing Tensor Core topology:

```text
No need to disable individual Tensor Cores.
No need to rewrite CUDA-visible SM count.
No need to alter instruction decoding.
No need to modify per-kernel code generation.
Same binary can execute; it just issues less often.
```

How later cards support this inference:

```text
Turing/Ampere:
  3-bit speed-select fields select explicit issue-rate divisors.

GV100:
  1-bit FMLA_REDUCED state likely selects one fixed reduced preset.

Observed local preset:
  FMLA_REDUCED -> approximately 1/16 HMMA/Tensor issue rate.
```

The `RMSchMicroSched` / `FUSE_SLOWDOWN_JITTER_HMMA16` path is related but not
identical evidence. It proves NVIDIA also has scheduler-level HMMA slowdown
bits in later chips. It does not prove that GV100 uses that exact register or
bit layout.

Practical consequence:

```text
If the 1/16 divisor is a scheduler gate, then changing visible PCI identity,
SM count, CUDA clocks, or power limit will not remove it. The state must be
changed before or during the protected GR/FECS/scheduler initialization path,
or by the internal secure mechanism that owns the speed-select state.
```

### 78. Profiling For The Scheduler Gate Signature

Yes, but only indirectly with the tools currently available on this host.

Directly visible:

```text
HMMA instructions execute.
Aggregate cycles per HMMA are much higher than a full-speed V100 control.
Nsight Compute showed Tensor pipe throughput at the 6.25% class.
```

Not directly visible yet:

```text
The internal scheduler condition itself.
A per-16-slot modulo phase.
The hardware counter/token bucket controlling issue eligibility.
```

Current compiler/tool status on this host:

```text
nvcc not found
ncu  not found
```

That means we could not build a new trace-mode CUDA kernel in this shell and
could not run a fresh Nsight Compute counter pass here. The existing
`hmma_issue_probe` binary was reused for a loop-count proxy sweep:

```text
projects/gpu-falcon-research/runs/20260520-hmma-cadence-proxy/
  summary.csv
  analysis.md
```

The proxy sweep used 512 blocks and varied loop counts on:

```text
GPU0  CMP 100-210
GPU14 Tesla V100-PCIE-12GB personality
```

Summary for loops >= 16:

```text
GPU0 dependent_accumulator:        mean ~812 cycles/HMMA
GPU0 independent_4_accumulators:   mean ~845 cycles/HMMA
GPU14 dependent_accumulator:       mean ~889 cycles/HMMA
GPU14 independent_4_accumulators:  mean ~941 cycles/HMMA
```

Interpretation:

```text
The aggregate slope stabilizes after startup overhead is amortized. That
supports a persistent issue-rate/issue-spacing gate rather than a one-time
startup effect.
```

Important limitation:

```text
This does not prove a modulo-16 cadence.

The existing binary records only one total clock64() delta per block. It does
not record per-iteration or per-HMMA timestamps. Block count also affects
residency/occupancy and per-block contention, which is why the 512-block proxy
numbers differ from the earlier 4096-block control run.
```

What would directly test the loop/condition:

```text
1. Rebuild a trace-mode HMMA probe with a small clock64() ring buffer.
2. Run one warp or one block per SM to reduce residency noise.
3. Record deltas around each wmma::mma_sync() or around each group of four HMMA
   SASS steps.
4. Analyze delta histograms and delta modulo 16 / modulo issue-slot cadence.
5. Compare capped local card against a full-speed V100 control with the same
   trace kernel.
```

Expected signatures:

```text
Simple issue divider:
  periodic long gaps or a stable allowed-issue cadence near 16x the full-speed
  control.

Token bucket:
  bursts of HMMA issue followed by regular refill gaps.

Active-lane mask instead of issue gate:
  similar issue timing to full-speed but lower work completed per issue. The
  existing HMMA issue-spacing result weighs against this being the only
  mechanism.
```

### 79. Trace-Mode HMMA Profiling

A trace-mode CUDA probe was added and compiled with the local Docker CUDA
toolchain:

```text
scripts/hmma_trace_probe.cu
runs/20260520-hmma-trace-mode/hmma_trace_probe
```

The binary contains real Volta HMMA SASS:

```text
HMMA.884.F32.F32.STEP0
HMMA.884.F32.F32.STEP1
HMMA.884.F32.F32.STEP2
HMMA.884.F32.F32.STEP3
```

Important correction: CUDA's default runtime device ordering did not match
`nvidia-smi` index order. The first trace files are still useful as raw
artifacts, but the PCI-aligned rerun used:

```text
CUDA_DEVICE_ORDER=PCI_BUS_ID
```

PCI-aligned saturated artifacts:

```text
runs/20260520-hmma-trace-mode/pci-gpu0-group1-blocks4096.json
runs/20260520-hmma-trace-mode/pci-gpu0-group16-blocks4096.json
runs/20260520-hmma-trace-mode/pci-gpu14-group1-blocks4096.json
runs/20260520-hmma-trace-mode/pci-gpu14-group16-blocks4096.json
runs/20260520-hmma-trace-mode/pci-saturated-summary.csv
```

Summary:

| GPU | Device | Group WMMA ops | Blocks | Avg cycles / WMMA |
| --- | --- | ---: | ---: | ---: |
| 0 | NVIDIA CMP 100-210 | 1 | 4096 | 3757.3 |
| 0 | NVIDIA CMP 100-210 | 16 | 4096 | 3788.8 |
| 14 | Tesla V100-PCIE-12GB personality | 1 | 4096 | 3365.8 |
| 14 | Tesla V100-PCIE-12GB personality | 16 | 4096 | 3443.9 |

This matches the older aggregate issue-spacing result:

```text
Local V100-personality: ~3416-3536 cycles / WMMA
Local CMP:              ~3675-3715 cycles / WMMA
```

Short, unsaturated traces behaved differently. With 1 block or 80 blocks, the
same trace kernel reported roughly `560-570 cycles / WMMA`. With 4096 blocks,
block-level medians moved to the `~4000 cycles / WMMA` class. That split is
useful rather than contradictory:

```text
Low-residency trace:
  captures short per-warp HMMA bursts.

Saturated trace:
  many resident warps contend for the same per-SM HMMA issue budget.
  average per-warp cadence moves into the known capped regime.
```

Interpretation:

```text
The trace-mode data strengthens the scheduler/token-budget model.

It does not expose the hidden modulo condition directly, but it shows that
once enough warps are resident to contend for Tensor issue slots, the measured
per-warp HMMA cadence returns to the same 15-16x capped class as the original
aggregate benchmark.
```

### 80. Burst Window Can Be Shaped, But It Does Not Unlock Throughput

The low-residency burst behavior was tested with a CUDA launch-shape sweep:

```text
runs/20260520-hmma-burst-window/summary.csv
runs/20260520-hmma-burst-window/burst-window-analysis.md
```

The sweep varied:

```text
GPU:    0 CMP 100-210, 14 Tesla V100-PCIE-12GB personality
Blocks: 1, 4, 16, 32, 68, 80, 128, 160, 256, 512, 1024, 2048, 4096
Loops:  1, 4, 16, 64, 256, 1024
Cases:  dependent accumulator, independent 4 accumulators
```

Key finding:

```text
The burst is real for resident CTAs, but it is not a full-throughput bypass.
```

Low block counts can keep the measured resident-CTA cadence near:

```text
~512 cycles / WMMA
```

But those cases underfill the GPU:

```text
1 block:    ~0.018 TFLOPS
4 blocks:   ~0.07 TFLOPS
16 blocks:  ~0.29 TFLOPS
32 blocks:  ~0.58 TFLOPS
```

At larger but still moderate launch sizes, the resident CTA clock window can
still look bursty while useful throughput approaches the local capped plateau:

```text
GPU0 CMP, independent, 256 blocks, 1024 loops:
  ~512 cycles / WMMA resident-CTA timing
  ~4.66 TFLOPS wall-clock

GPU14 V100-personality, independent, 256 blocks, 1024 loops:
  ~512 cycles / WMMA resident-CTA timing
  ~4.65 TFLOPS wall-clock
```

The best useful throughput in the sweep remained in the known capped range:

```text
GPU0 CMP:
  best independent_4_accumulators ~4.69 TFLOPS
  best dependent_accumulator      ~4.69 TFLOPS

GPU14 V100-personality:
  best independent_4_accumulators ~5.36 TFLOPS
  best dependent_accumulator      ~5.35 TFLOPS
```

Interpretation:

```text
The fast burst can be influenced by launch shape, especially for small or
latency-shaped custom kernels.

It does not recover full V100 Tensor throughput. Once enough work is queued to
make throughput matter, the global HMMA/Tensor issue budget dominates. The
missing time is not necessarily inside a resident CTA's clock64() window; it
can appear as CTA scheduling, queuing, and contention for the capped per-SM
Tensor issue budget.
```

Practical user-space consequence:

```text
For custom kernels on these cards, tune block count and loop granularity.
The local sweet spot is workload-dependent, but a few hundred to a few thousand
CTAs with long inner loops is better than trying to preserve tiny one-block
bursts.

For cuBLAS-sized GEMM, this does not avoid the 1/16-class limiter.
```

### 81. Z3 Consistency Check For Candidate Mechanisms

A small Z3 model was added to test mechanism classes against the observed
timing facts:

```text
tools/gv100_hmma_mechanism_z3.py
runs/20260520-z3-hmma-mechanism/hmma-mechanism-z3-results.md
runs/20260520-z3-hmma-mechanism/hmma-mechanism-z3-results.json
```

Encoded observations:

```text
Full-speed V100 control:          236.4 to 242.8 cycles / WMMA
Low-residency local burst:        512.1 to 515.9 cycles / WMMA
Saturated local resident timing:  2838.0 to 3708.4 cycles / WMMA
Best local burst-sweep throughput: 5.3610 TFLOPS
```

Candidate results:

| Candidate | Z3 result | Meaning |
| --- | --- | --- |
| shared token/budget | `sat` | Fits the data with `n = 16`. |
| strict per-warp modulo | `unsat` | Would slow the low-residency burst too. |
| active-lane mask only | `unsat` | Cannot explain saturated per-WMMA `clock64()` expansion. |
| global clock throttle | `unsat` | Would slow non-HMMA paths by the same 14-16x class. |
| pure CTA queueing only | `unsat` | Saturated resident block timing also rises, not just event time. |

Interpretation:

```text
The best surviving mechanism class is not a strict per-warp modulo gate.

It is a per-SM or per-scheduler shared HMMA/FMLA issue budget with enough
token depth or arbitration slack to allow short low-residency bursts, and a
sustained refill/eligibility rate equivalent to 1/16.
```

This is not a proof of the hidden hardware implementation, but it narrows the
mechanism shape:

```text
More likely:
  shared token bucket / shared Tensor issue budget / scheduler arbitration gate

Less likely:
  physical Tensor Core disablement
  active-lane-only mask
  global clock throttle
  pure host/CTA queueing artifact
  strict every-HMMA per-warp modulo gate
```

### 82. Concurrent Stream Test Shows Budget Conservation

A concurrent-stream probe was added to test whether two HMMA kernels receive
independent Tensor issue budgets or share the same capped budget:

```text
scripts/hmma_concurrent_stream_probe.cu
runs/20260520-hmma-concurrent-budget/hmma_concurrent_stream_probe
runs/20260520-hmma-concurrent-budget/summary.csv
runs/20260520-hmma-concurrent-budget/concurrent-budget-analysis.md
```

The first probe version used CUDA events in the default stream and was invalid
for nonblocking streams. The corrected version uses host wall-clock around the
launches plus `cudaDeviceSynchronize()`.

Test design:

```text
case A: launch one independent-4-accumulator HMMA kernel
case B: launch two identical HMMA kernels in separate nonblocking streams

vary blocks per kernel:
  1, 4, 16, 32, 68, 80, 128, 256, 512, 1024, 2048, 4096

loops:
  1024
```

The result has a clean two-regime shape.

Underfilled regime:

```text
<= 128 blocks/kernel
two streams scale almost exactly 2x
```

Examples:

| GPU | Blocks/kernel | Single TFLOPS | Two-kernel combined TFLOPS | Ratio |
| --- | ---: | ---: | ---: | ---: |
| 0 CMP | 32 | 0.581 | 1.159 | 1.994x |
| 0 CMP | 128 | 2.325 | 4.634 | 1.993x |
| 14 V100-personality | 32 | 0.579 | 1.159 | 2.000x |
| 14 V100-personality | 128 | 2.324 | 4.593 | 1.976x |

This proves the streams can overlap when the workload does not already fill
the device.

Saturated regime:

```text
>= 256 blocks/kernel
two streams no longer double total throughput
combined throughput stays in the local capped class
each kernel receives roughly half of the shared budget
```

Examples:

| GPU | Blocks/kernel | Single TFLOPS | Two-kernel combined TFLOPS | Ratio |
| --- | ---: | ---: | ---: | ---: |
| 0 CMP | 256 | 4.646 | 4.673 | 1.006x |
| 0 CMP | 1024 | 4.684 | 4.689 | 1.001x |
| 0 CMP | 2048 | 4.690 | 4.694 | 1.001x |
| 14 V100-personality | 256 | 4.644 | 4.672 | 1.006x |
| 14 V100-personality | 512 | 4.674 | 4.687 | 1.003x |

GPU14 has some run-to-run variation at larger launches, but it remains in the
same capped class rather than approaching 2x.

Interpretation:

```text
This is the strongest behavioral evidence so far for a shared HMMA/Tensor
issue budget.

The underfilled regime rules out simple stream serialization.
The saturated regime rules out independent per-kernel budgets.
The two-kernel case conserves the same local Tensor throughput plateau.
```

The exact hardware scope is still not visible, but the behavior matches:

```text
per-SM or per-scheduler shared HMMA/FMLA issue budget
all SMs initialized with the same reduced speed-select state
device-level plateau emerges from the sum of those per-SM caps
```

### 83. Firmware Dumps Show The Register Surfaces, Not The Hidden Budget Counter

A read-only firmware visibility scanner was added:

```text
tools/gv100_firmware_visibility_scan.py
runs/20260520-firmware-visibility/gv100-firmware-visibility-scan.md
runs/20260520-firmware-visibility/gv100-firmware-visibility-scan.json
```

The scan searched the public GV100 firmware dump artifacts for literal
little-endian dwords corresponding to:

```text
0x00409660  FECS feature readout
0x00409664  FECS feature override SM speed select
0x00000999  local reduced+override mask seen in VBIOS/live readback
0xfffff666  complement of that mask
0x00047a20  SM_SCH_MICRO_SCHED default
0x00419b50  GPCS_TPCS SM_SCH_MICRO_SCHED broadcast address
```

Direct useful binary hits:

```text
lib-firmware-gv100/gr/sw_ctx.bin
  0x192c: 0x00419b50  GPCS_TPCS_SM_SCH_MICRO_SCHED broadcast address
  0x1934: 0x00047a20  SM_SCH_MICRO_SCHED default value
```

This confirms that the public GR context-state firmware blob carries the
per-TPC/SM scheduler register surface and default. It does not expose the
hidden reduced-rate algorithm.

The only direct `0x00000999` hit was in:

```text
lib-firmware-gv100/gr/sw_bundle_init.bin@0x0af8
```

But nearby dwords are a sequential bundle table:

```text
0x00000995, 0x00000996, 0x00000997, 0x00000998,
0x00000999, 0x0000099a, ...
```

So that is not evidence of the FECS `SM_SPEED_SELECT` mask.

The generated GV100 ctxsw headers do expose both relevant surfaces:

```text
NETD_dev_graphics_nobundle.h
  NET24_NV_PGRAPH_PRI_FECS_FEATURE_READOUT                  0x00409660
  NET24_NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT 0x00409664

NETD_ctx_state_store.h
  NET24_NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
  NET24_NV_PGRAPH_PRI_GPCS_TPCS_SM_SCH_MICRO_SCHED default 0x00047a20
  NV_PTPC_PRI_SM_SCH_MICRO_SCHED offset 0x00000350
```

Interpretation:

```text
The firmware dumps can show the register/control surfaces around the limiter:
  FECS speed-select readout/override
  per-TPC scheduler micro-scheduler state
  context-state default for SM_SCH_MICRO_SCHED

They do not show a symbolic or literal "1/16 token budget" implementation.
That mechanism is likely in scheduler hardware or signed/internal FECS/GR
microcode without public symbols, triggered by the binary GV100 reduced state.
```

### 84. Fresh Debugdump Is Different From Firmware Dumps, But Still Not A Raw Speed-Select Dump

A fresh root debugdump pass was collected after reboot for one CMP-labelled
card and one V100-personality card:

```text
runs/20260520-debugdump-refresh/gpu0-debugdump.zip
runs/20260520-debugdump-refresh/gpu4-debugdump.zip
runs/20260520-debugdump-refresh/decrypted-gpu0/
runs/20260520-debugdump-refresh/decrypted-gpu4/
tools/gv100_debugdump_compare.py
runs/20260520-debugdump-refresh/nvdebugdump-decode-scan.md
runs/20260520-debugdump-refresh/nvdebugdump-decode-scan.json
runs/20260520-debugdump-refresh/debugdump-refresh-compare.md
```

Both captures succeeded with local root:

```text
gpu0 CMP:
  rm_00.pb              35702 bytes
  system_info.pb       282762 bytes
  nvlog.gpu000.log     527548 bytes

gpu4 V100-personality:
  rm_04.pb              28923 bytes
  system_info.pb       282762 bytes
  nvlog.gpu004.log     527548 bytes
```

This is a different artifact class from the public firmware dump:

```text
Firmware dump:
  static signed FECS/GPCCS/SEC2 blobs and GR context/init blobs

Debugdump:
  runtime RM diagnostic protobuf/log payloads after VBIOS and driver init
```

The useful debugdump signal is runtime board-management residue. String scans
of the fresh `rm_*.pb` payloads show InfoROM-style object names and CMP strings
on both the CMP-labelled card and the V100-personality card:

```text
rm_00.pb: CMP, OBD, OEM, IMG, ROM
rm_04.pb: CMP, OBD, OEM, IMG, ROM
```

That matches the earlier finding: the V100-personality flash changes visible
PCI/NVML/CUDA identity, but the board-management/InfoROM residue still looks
CMP-derived locally.

The fresh debugdump scan also found literal FECS speed-select addresses in the
binary `nvlog` payloads:

```text
0x00409660  FECS feature readout
0x00409664  FECS feature override SM speed select
```

They appear at repeated offsets in `nvlog.log` / `nvlog.gpu*.log`, not as
decoded fields in `rm_*.pb`. Treat these as diagnostic/log artifacts unless a
protobuf/log decoder maps them to a selected register block and value.

A protobuf decode pass was added with:

```text
tools/nvdebugdump_decode_scan.py
```

It successfully decodes local `system_info.pb`, `rm_*.pb`, and `error_data.pb`
payloads with the local NVIDIA protobuf schemas. On the local post-reboot
captures it confirms:

```text
gpu0 CMP:
  decoded InfoROM/OBD/OEM records contain "CMP"
  decoded RM fields include GPC clock-domain state and priv_level 136

gpu4 V100-personality:
  decoded InfoROM/OBD/OEM records also contain "CMP"
  decoded RM fields include GPC clock-domain state and priv_level 0
```

The decode pass still does not expose a decoded `0x409660` / `0x409664` value;
the address literals remain in the `nvlog` binary payload class.

Interpretation:

```text
Debugdump can help compare:
  InfoROM/OBD/OEM board-management residue
  selected RM diagnostic state
  nvlog history around register probes

Debugdump has not yet exposed:
  raw PFUSE rows
  decoded FECS 0x409660/0x409664 values
  the hidden binary-reduced-to-1/16 implementation
```

So yes, debugdump is different and still useful. The most valuable missing
comparator remains a usable decrypted debugdump from a true full-speed V100 in
an environment that permits RM capture-buffer access.

### 85. Earlier Debugging References Exist, But Point To Internal Debug Boards

A follow-up source search for earlier/pre-RM debugging did find relevant
documentation and tooling, but not a production GV100 entry point.

The strongest early-debug documentation is in the SBR/PUB safety docs:

```text
integ/gpu_drv/stage_rel/safety/SBR-TU10A/SWE-SBR-016-SWVP.md
```

It says dynamic PUB testing is done with:

```text
TU104-510 debug boards
debug signed PUB
MODS platform
```

The SBR/PUB design docs describe the earlier secure path:

```text
integ/gpu_drv/stage_rel/safety/SBR-TU10A/SWE-SBR-007-SWADS.md
integ/gpu_drv/stage_rel/safety/SBR-TU10A/Unit_Design/SWE-SBR-009-SWUD-LITE.md
```

Relevant facts:

```text
PUB runs on SEC2 Falcon.
RM loads PUB.
PUB sets up PLMs so PMU and CTXSW/FECS/GPCCS can access protected registers.
PUB accepts no outside input except its signature/status mailbox path.
PUB production code is PKC signed and BootROM verified.
Debug-signed PUB is for internal testing and only runs on special debug boards.
Production HS code disables TRACEPC and scrubs/halts in heavy-secure mode.
```

There is also an internal debugger/tooling stack:

```text
integ/gpu_drv/stage_rel/apps/nvwatch/
dev/gpu_drv/stage_rel/apps/nvwatch/
```

Useful names found:

```text
falctrace
flcn
flcngdb
pmuimemrd / pmudmemrd / pmudmemwr
fecsimemrd / fecsdmemrd / fecsdmemwr
sec2gv100.c
pmugv100.c
```

Interpretation:

```text
The local tree does describe NVIDIA's earlier debug architecture.

It is not nvidia-debugdump.
It is not normal production RM.
It is an internal debug-board/debug-signed-microcode/MODS/nvwatch path.
```

For this limiter investigation, that means:

```text
Earlier debug path exists conceptually:
  SEC2/PUB/BootROM/auth/PLM stage

But the checked-in material says the usable dynamic route requires:
  special debug boards + debug-signed PUB + internal MODS/nvwatch setup

So it explains where NVIDIA would debug/disable/test this class of policy,
but it does not give a normal host-side attach point for the local production
GV100 cards.

### 86. Why The V100 Firmware Update Could Unlock Some Hidden State But Not This Limiter

The prior V100-personality flash was not cosmetic. It changed a real
driver-visible enablement boundary:

```text
CMP personality:
  PCI device      10de:1d84
  subsystem       10de:12b9
  product name    NVIDIA CMP 100-210
  CUDA SM count   68
  Nsight Compute  ERR_NVCMPGPU for tensor counters

V100 personality:
  PCI device      10de:1df4
  subsystem       10de:12b8
  product name    Tesla V100-PCIE-12GB
  CUDA SM count   80
  Nsight Compute  tensor-pipe counters allowed
```

So the V100 update did unlock or expose hidden resources: identity, SM count,
and profiling/tooling policy all changed.

The Tensor/FP64 limiter is a different gate. Local V100-personality images
still contain the selected-main-init tail write:

```text
NV_REG R[0x409664] &= 0xfffff666 |= 0x00000999
```

Full-speed Tesla V100 32GB and Quadro GV100 controls reach `DONE` at the same
point instead of inserting that write. Two Titan V controls touch the same
register with a no-op zero write.

This means the flash crossed one policy layer but not the other:

```text
Layer A: board/personality/SM exposure
  controlled by VBIOS-visible identity, straps, board config, init tables
  prior owner's flash changed this successfully

Layer B: FECS speed-select issue policy
  controlled by selected VBIOS init tail + FECS/CTXSW/firmware/scheduler state
  local V100-personality images still assert this as reduced/override
```

That is why the cards can look like V100s and expose 80 SMs while still
behaving like capped CMP-derived cards for HMMA/FMLA and DP throughput.

The most important implication:

```text
The unlock precedent is real, but it proves there are multiple independent
gates. It does not prove that the V100-personality image used locally was a
full-speed V100 policy image.
```

The exact ROM-level discriminator remains the selected init tail:

```text
bad/capped image:
  0x409664 &= 0xfffff666 |= 0x00000999 before DONE

known full-speed controls:
  no set-reduced opcode at that tail
```
```

### 87. SM Count Is Now Mapped To The Public GPC/TPC Floorsweep Path

The close SM-count pass found a cleaner path than the FECS speed-select work:
CUDA-visible SM count is normal GPC/TPC floorsweeping.

GV100 topology from local hwref:

```text
NV_SCAL_LITTER_NUM_GPCS        = 6
NV_SCAL_LITTER_NUM_TPC_PER_GPC = 7
NV_SCAL_LITTER_NUM_SM_PER_TPC  = 2
```

So full physical GV100 topology is:

```text
6 GPC * 7 TPC/GPC * 2 SM/TPC = 84 SM
```

The RM-visible count is:

```text
enabled TPC count * 2 SM/TPC
```

Source:

```text
integ/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/hwproject.h:77-84
integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/gr/graphics.c:4406-4412
```

The bus HAL reads the GPC/TPC floorsweep status registers:

```text
NV_FUSE_STATUS_OPT_GPC        = 0x00021c1c
NV_FUSE_STATUS_OPT_TPC_GPC(i) = 0x00021c38 + i*4
```

and returns masks where `1` means present. The code explicitly inverts the
fuse value because the manuals define `0` as present and `1` as disabled.

Source:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/bus/maxwell/busgm107.c:155-257
dev/gpu_drv/stage_rel/drivers/resman/config/haldefs/bus.def:1650-1670
dev/gpu_drv/stage_rel/drivers/common/inc/hwref/volta/gv100/dev_fuse.h:2997-3054
```

The old MODS floorsweep tool has the same GV100 shape:

```text
GV100FsInfo:
  MAX_GPC = 6
  MAX_TPC = 42
  ParseFuseInfo reads:
    OPT_GPC_DISABLE
    OPT_TPC_GPC0_DISABLE ... OPT_TPC_GPC5_DISABLE
  FsStr emits:
    gpc_enable
    gpc_tpc_enable
```

Source:

```text
dev/gpu_drv/stage_rel/diag/mods/tools/floorsweep/floorsweep_libs/FsInfo/VoltaFsInfo.py:14-90
```

A new read-only RM probe was added:

```text
projects/gpu-falcon-research/tools/rm_gr_floorsweep_probe.c
projects/gpu-falcon-research/tools/rm_gr_floorsweep_probe
```

It calls:

```text
NV2080_CTRL_CMD_GR_GET_GPC_MASK
NV2080_CTRL_CMD_GR_GET_TPC_MASK
NV2080_CTRL_CMD_GR_GET_GLOBAL_SM_ORDER
```

Results on this host:

```text
GPU0 CMP 100-210 / 10de:1d84:
  gpc_mask = 0x1f
  enabled TPCs = 34
  inferred SMs = 68

GPU4 V100-personality / 10de:1df4:
  gpc_mask = 0x3f
  enabled TPCs = 40
  inferred SMs = 80
```

The all-card sweep shows the same class split:

```text
CMP-identity cards:
  34 enabled TPCs -> 68 SMs

V100-personality cards:
  40 enabled TPCs -> 80 SMs
```

Interpretation:

```text
The prior V100-personality flash really did cross the SM exposure gate.
It did so by changing the GPC/TPC floorsweep/personality state visible to RM.

That gate is separate from the FECS speed-select gate:
  SM count changed from 68 to 80.
  0x409664 speed-select still remains 0x999 on local capped images.
```

The missed practical implication is that SM count can be mapped and compared
with normal read-only RM controls. We do not need FECS access to understand the
68-vs-80 split; the exact per-card TPC masks are now visible.

What remains missing for the SM-count producer side:

```text
The generated SKU/POR/chipfuse file that decided why a given CMP identity
selects 34 TPCs while the V100-personality image selects 40 TPCs.

Likely file class:
  gv100_f.json
  gv100 POR / chipfuse SKU config
  MODS Floorsweeping_options / fsSettings metadata
```

## 88. Public `nvidia-debugdump` Artifacts Are Decodable But Not Yet Volta-Class

An online search for public `nvidia-debugdump` artifacts found two actual debugdump zips:

```text
runs/20260520-public-debugdump-search/nvidia-forum-2013-gk107-debugdump.zip
runs/20260520-public-debugdump-search/nvidia-forum-2023-quadro-m2200-debugdump.zip
```

The 2013 GK107 dump contains:

```text
debug_buffers_00.pb
rm_00.pb
system_info.pb
```

The 2023 Quadro M2200 dump contains:

```text
system_info.pb
error_data.pb
nvlog.log
```

Both artifacts were initially searched offline for:

```text
FECS
SM_SPEED
PGRAPH
InfoROM
OBD
OEM
GV100
V100
409664
fuse
GPC
TPC
PLM
PRIV
FALCON
```

No useful plaintext hits were found before decryption.

Azure's public HPC diagnostics repository describes its collected
`Nvidia/nvidia-debugdump.zip` as:

```text
only Nvidia can read
```

Local inspection of `/usr/bin/nvidia-debugdump` matches that from the external
tool's point of view: the external tool can collect RM/NVML diagnostics, but
does not expose a decode path in its help.

NVIDIA's public Linux driver README says the same thing in plainer terms:
`nvidia-debugdump` collects internal GPU state, and its output is a binary blob
that requires internal NVIDIA engineering tools to interpret.

Correction: this workspace does have the relevant internal pieces in source and
a built local extractor:

```text
projects/gpu-falcon-research/tools/nvdebugdump_decrypt_extract.c
projects/gpu-falcon-research/runs/20260520-rm-speed-select/nvdebugdump_decrypt_extract

dev/gpu_drv/stage_rel/apps/nvDebugDump/nvdd_main.c
dev/gpu_drv/stage_rel/apps/nvDebugDump/nvdebugdump.c
dev/gpu_drv/stage_rel/apps/nvDebugDump/Mac/decodeProtoBufMac.c
dev/gpu_drv/stage_rel/drivers/resman/kernel/inc/protobuf/*.proto
```

The extractor uses the local `nvdzip` path and the debugdump crypt key to
decrypt external dumps offline. After decryption:

```text
runs/20260520-public-debugdump-search/decrypted-gk107/
  debug_buffers_00.pb  2 bytes
  rm_00.pb             13478 bytes
  system_info.pb       242 bytes

runs/20260520-public-debugdump-search/decrypted-m2200/
  system_info.pb       172 bytes
  error_data.pb        4352 bytes
  nvlog.log            513212 bytes
```

The local protobuf schemas can decode `system_info.pb` and structurally decode
`rm_00.pb` / `error_data.pb`, with some schema-version noise. The decoded
system metadata confirms these samples are not Volta-class:

```text
GK107 sample:
  driver version: 313.18
  branch: rel/gpu_drv/r313/r313_00-1555
  kernel: 3.7.0-030700-generic

Quadro M2200 sample:
  driver version: 525.105.17
  branch: rel/gpu_drv/r525/r528_79-332
  kernel: 6.1.0-10-amd64
```

A wider local public-harvest set also decrypts:

```text
runs/20260520-public-nvidia-debugdump-harvest/decrypted/7agAmtUnjhnzJweTgNriRQsTQ1u/
runs/20260520-public-nvidia-debugdump-harvest/decrypted/kbNLVfP11nzkApnq8B505H0RoWo/
runs/20260520-public-nvidia-debugdump-harvest/decrypted/p88gGndvlYoPhZ1WoGPBAy6F99i/
```

Decoded driver branches from those public samples include:

```text
510.47.03 / rel/gpu_drv/r510/r511_37-228
390.42    / rel/gpu_drv/r390/r390_00-156
525.125.06 / rel/gpu_drv/r525/r529_03-397
```

After decryption and protobuf decode, searches for:

```text
GV100
V100
CMP
FECS
SM_SPEED
409660
409664
PGRAPH
HMMA
TENSOR
PLM
```

still found no useful Volta/GV100 limiter signal in these public examples. The
decrypted data does include InfoROM object records such as `ROM`, `IMG`, `BRD`,
`OBD`, `OEM`, `BBO`, and `RPR`, proving debugdump can carry the kind of board
management residue we want if we obtain a Volta-class sample.

The repeatable decode/scan artifact is:

```text
tools/nvdebugdump_decode_scan.py
runs/20260520-public-debugdump-search/nvdebugdump-decode-scan.md
runs/20260520-public-debugdump-search/nvdebugdump-decode-scan.json
```

A follow-up public forum hit looked promising because the post text mentioned a
V100 setup:

```text
https://forums.developer.nvidia.com/t/encrypted-gpu-binary-output/181653
```

The attached `nvidia-bug-report.log` contained an embedded base64
`nvidia-debugdump -D` block, which was extracted to:

```text
runs/20260520-public-debugdump-search/v100-forum-encrypted-gpu-binary-output/nvidia-debugdump-D-block0.zip
```

That embedded debugdump decrypted and decoded successfully:

```text
runs/20260520-public-debugdump-search/v100-forum-encrypted-gpu-binary-output/decrypted/
runs/20260520-public-debugdump-search/v100-forum-encrypted-gpu-binary-output/nvdebugdump-decode-scan.md
```

However, decoded `system_info.pb` identifies the actual cards as Tesla T4:

```text
driver version: 460.80
branch: rel/gpu_drv/r460/r460_00-483
num_gpus: 4
device_id packed value: 0x1eb810de
```

So this is a useful multi-GPU decode validation case, but not a Volta/GV100
limiter comparator.

Two real public Volta debugdump comparators were then found in public NVIDIA
forum `nvidia-bug-report.log.gz` attachments. Both contain embedded base64
`nvidia-debugdump -D` zips:

```text
https://forums.developer.nvidia.com/t/dynamic-page-retirement-encounted-fatal-error-on-v100/291605
https://forums.developer.nvidia.com/t/occasionally-missing-v100-pcie-32gb-gpu/78418
```

Artifacts:

```text
runs/20260520-public-debugdump-search/v100-forum-bugreports/
runs/20260520-public-debugdump-search/v100-forum-bugreports/v100-public-debugdump-compare.md
runs/20260520-public-debugdump-search/v100-forum-bugreports/nvdebugdump-decode-scan.md
```

Decoded identities:

```text
Public V100 SXM2 32GB:
  device: 10de:1db5
  driver: 550.76
  VBIOS: 88.00.80.00.01

Public V100 PCIe 32GB:
  device: 10de:1db6
  driver: 418.66
  VBIOS: 88.00.48.00.02
```

The public V100 debugdumps were scanned for the FECS speed-select address and
local mask values:

```text
0x00409660
0x00409664
0x00000999
0xfffff666
0x00419b50
0x00047a20
```

Result:

```text
no tracked dword hits
```

But the decoded InfoROM `OBD` payload comparison is strong:

```text
local gpu0 CMP:
  OBD payload contains "CMP"

local gpu4 V100-personality:
  OBD payload contains "CMP"

public true V100 PCIe:
  OBD payload contains "Tesl"

public true V100 SXM2:
  OBD payload has no visible CMP string
```

Interpretation: this is the cleanest external comparator so far. The local
V100-personality card still carries CMP board-management residue in decoded
debugdump InfoROM `OBD`; public true V100 cards do not. That supports the model
that VBIOS/personality flashing changed public PCI/NVML/CUDA identity but did
not change the lower board-management identity consumed by policy layers.

Detailed search notes:

```text
runs/20260520-public-debugdump-search/public-debugdump-search.md
```

Interpretation: the earlier "opaque" conclusion was wrong. Public debugdump
zips can be decrypted and decoded locally. The limiting factor is sample class:
the public dumps found so far are not V100/GV100/CMP. A real Volta-class
debugdump is now a stronger target than plaintext bug-report logs, because it
can carry protobuf-structured InfoROM, RM, register, and Falcon/debug state.

### 21. OBD Code Path: Strong Identity Comparator, Not A Direct Limiter Input Yet

The `OBD` object in decoded debugdump payloads maps directly to the local
InfoROM struct:

```text
dev/gpu_drv/stage_rel/drivers/resman/arch/nvalloc/common/inc/inforom/ifrstruct.h

INFOROM_OBD_OBJECT_V1_XX_PACKED_SIZE = 128
INFOROM_OBD_OBJECT_V1_XX:
  header
  buildDate
  marketingName[24]
  serialNumber[16]
  memoryManufacturer
  memoryPartID[20]
  memoryDateCode[6]
  productPartNumber[20]
  boardRevision[3]
  boardType
  board699PartNumber[20]
```

The object header is 8 bytes:

```text
type[3], version, subversion, size, checksum
```

So a prefix like:

```text
OBD 01 01 80 00 00 15 05 18 20 43 4d 50 20
```

decodes as:

```text
type: OBD
version: 1.1
size: 128
buildDate: 2018-05-15
marketingName prefix: CMP
```

A repeatable decoder was added:

```text
projects/gpu-falcon-research/tools/obd_payload_decode.py
projects/gpu-falcon-research/runs/20260520-public-debugdump-search/v100-forum-bugreports/obd-payload-prefix-decode.md
```

Decoded comparator:

```text
local gpu0 CMP:
  buildDate: 2018-05-15
  marketingName prefix: CMP

local gpu4 V100-personality:
  buildDate: 2017-12-13
  marketingName prefix: CMP

public true V100 PCIe:
  buildDate examples: 2018-05-05, 2018-05-07, 2018-07-06, 2018-07-07
  marketingName prefix: Tesl
```

Source consumers found:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/inforom/kepler/ifrgk104.c
integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/inforom/arch/maxwell/inforom_gm107.c
integ/gpu_drv/stage_rel/pmu_sw/prod_app/smbpbi/nv/smbpbi.c
integ/gpu_drv/stage_rel/uproc/soe_riscv/src/syslib/smbpbi/nv/soe_smbpbi.c
```

Those paths read OBD into RM board identity and expose it through SMBPBI,
debugdump, MODS validation/printing, and serial-number logging. Targeted
searches did not find a visible consumer that feeds OBD `marketingName`,
`pGpu->boardInfo`, or OBD validity into GV100 GR/FECS speed-select,
scheduler slowdown, or issue-rate policy.

Interpretation: OBD is currently the best persistent proof that the local
V100-personality card still has CMP board-management identity underneath the
flash-level personality. It is useful for correlation and sample matching. It
does not yet look like the direct source of the `1/16` tensor limiter in the
visible source.

### 22. Identity Differences Beyond OBD Name

There are several identity-bearing surfaces beyond the displayed product name.

Same-version ROM comparisons show that per-card variation is small and local:

```text
runs/20260520-bootrom-dump/same-version-obd-diff-note.md

fresh local V100-personality 88.00.51.00.04
vs TechPowerUp 266855 extracted 88.00.51.00.04:
  compared bytes: 270336
  differing bytes: 5
  differences: OBD/checksum/board-serial-like bytes

fresh local CMP 88.00.9D.00.00
vs previous local CMP 88.00.9D.00.00:
  compared bytes: 1048576
  differing bytes: 8
  differences: OBD/checksum/board-serial-like bytes
```

Interpretation: for the same VBIOS version, the obvious mutable identity area
is InfoROM/OBD object metadata. The decoded main init scripts and current
`M`/`P` table candidates are unchanged by those per-card serial differences.

Between CMP-derived local images and full-speed controls, the differences are
larger and more policy-relevant:

```text
CMP native:
  PCI device: 10de:1d84
  subsystem: 10de:12b9
  VBIOS: 88.00.9D.00.00
  OBD marketingName: CMP
  InfoROM IMG: G001.0000.01.04 on local samples
  CUDA-visible SMs: 68
  live FECS override: 0x00000999

V100-personality local:
  PCI device: 10de:1df4
  subsystem: 10de:12b8
  VBIOS: 88.00.51.00.04
  OBD marketingName: CMP
  InfoROM IMG: G001.0000.01.04 on local samples
  CUDA-visible SMs: 80
  live FECS override: 0x00000999

Public/full-speed V100 comparators:
  PCI device examples: 10de:1db4, 10de:1db5, 10de:1db6
  VBIOS examples: 88.00.1A.00.03, 88.00.48.00.02, 88.00.80.00.01
  OBD marketingName: Tesl where payload exposes it
  InfoROM IMG examples: G500.0202.00.02, G503.0203.00.05
  selected main-init tail: no 0x409664 set-reduced opcode on Tesla V100/Quadro GV100 controls
```

The most direct firmware difference remains the selected VBIOS main-init tail:

```text
capped/CMP-derived images:
  NV_REG R[0x409664] &= 0xfffff666 |= 0x00000999

full-speed Tesla V100 / Quadro GV100 controls:
  corresponding location reaches DONE
```

The prior owner’s V100-personality flash changed real driver-visible identity:

```text
PCI device/subdevice
product name
SM/TPC floorsweep exposure
Nsight tensor-counter access
```

but it did not change:

```text
OBD marketingName: still CMP
InfoROM IMG profile: still G001 on local V100-personality cards
FECS speed-select override: still 0x00000999
tensor/FP64 issue-rate class: still reduced
```

Visible code consumers line up with this split:

```text
OBD / boardInfo:
  read into RM board identity and exported/logged
  no visible GR/FECS limiter consumer found

GV100 CMP SKU:
  gpuGetIsCmpSku_GV100()
  derived from HAL_FUSE_OPT_PCIE_BOOT_GEN3_DISABLE
  and HAL_FUSE_OPT_PCIE_BOOT_GEN23_DISABLE

SM count:
  exposed through GPC/TPC floorsweep masks/status
  changes with V100 personality

Tensor/DP speed-select:
  visible as VBIOS 0x409664 init opcode and live FECS override
  not changed by the V100-personality identity layer
```

Interpretation: the firmware/identity evidence supports at least three
separate gates:

1. PCI/product/SM-count personality, which prior flashing changed.
2. Board-management identity, where local V100-personality cards still look
   CMP-derived through OBD/IMG.
3. FECS speed-select policy, where both CMP and V100-personality local cards
   still carry the `0x999` reduced-speed override while full-speed controls do
   not.

### 23. Debug Path Audit

The closest normal-driver debug surface is the `GT200_DEBUGGER` / `NV83DE`
class:

```text
sdk/nvidia/inc/class/cl83de.h
drivers/resman/inc/kernel/gpu/gr/kernel_sm_debugger_session.h
drivers/resman/src/kernel/gpu/gr/kernel_sm_debugger_session_ctrl.c
```

Several controls are exported as `NON_PRIVILEGED`, including surface access,
debug memory access, register operations, and batch memory access:

```text
NV83DE_CTRL_CMD_READ_SURFACE
NV83DE_CTRL_CMD_WRITE_SURFACE
NV83DE_CTRL_CMD_GET_MAPPINGS
NV83DE_CTRL_CMD_DEBUG_READ_MEMORY
NV83DE_CTRL_CMD_DEBUG_WRITE_MEMORY
NV83DE_CTRL_CMD_DEBUG_EXEC_REG_OPS
NV83DE_CTRL_CMD_DEBUG_READ_BATCH_MEMORY
NV83DE_CTRL_CMD_DEBUG_WRITE_BATCH_MEMORY
```

The important constraints are in the implementations.

Surface read/write is limited to the debuggee channel VA space. It validates
the requested GPU VA range through MMU trace before mapping pages and copying.
This is useful for debugger-visible channel memory, not arbitrary FECS IMEM,
DMEM, or private RM buffers.

Debug memory read/write requires a memory object handle owned by the caller:

```text
kernel_sm_debugger_session_ctrl.c:491-496
serverutilGetResourceRef(hClient, hMemory, &pResourceRef)

kernel_sm_debugger_session_ctrl.c:518-522
length != 0
portSafeAddU64(offset, length, &totalLength)
totalLength <= pMemDesc->Size
```

Interpretation: this path can copy memory behind an RM memory handle the caller
already has rights to. It does not by itself create a handle for RM internal
FECS/GPCCS firmware allocations.

Register operations are routed through the normal register-operation validator:

```text
kernel_sm_debugger_session_ctrl.c:678-710
ksmdbgssnCtrlCmdDebugExecRegOps_IMPL(...)
  -> gpuValidateRegOps(...)

subdevice_ctrl_gpu_regops.c:649-702
gpuValidateRegOps(...)
  -> gpuValidateRegOffset(...)
  -> NV2080_CTRL_GPU_REG_OP_STATUS_INVALID_OFFSET
```

The user register access map is constructed from chip-specific compressed
allowlist data:

```text
gpu_gv100.c:27
#include "volta/gv100/user_access_map_gzip.h"

gpu_gv100.c:117-130
gpuGetUserRegisterAccessMap_GV100(...)

gpu_register_access_map.c:197-326
gpuConstructUserRegisterAccessMap_IMPL(...)

gpu_register_access_map.c:123-159
gpuGetUserRegisterAccessPermissions_IMPL(...)
```

The local GV100 shipped access-map binary denies the FECS feature readout and
speed-select override offsets:

```text
volta/gv100/user_access_map.bin

0x00409660 NV_PGRAPH_PRI_FECS_FEATURE_READOUT                  denied
0x00409664 NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT denied
```

The bundled `tools/regaccess/config/common.yml` contains Volta entries for:

```text
NV_PGRAPH_PRI_FECS_FEATURE_READOUT
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
```

but the generated/shipped GV100 access map on disk does not contain those
registers. In production RM, `DEBUG_EXEC_REG_OPS` therefore remains a useful
oracle for allowlist status, not a bypass of the FECS speed-select register.

There is a verification-only path that can replace or edit the access map:

```text
subdevice_diag_ctrl.c:406-447
diagapiCtrlCmdGpuSetUserRegisterAccessPermissions_IMPL(...)

#if defined(NV_VERIF_FEATURES) || RMCFG_FEATURE_ENABLED(VERIF_ONLY_CONTROLS)
```

Interpretation: NVIDIA/MODS verification builds have an internal route to
modify the access map and commit it to FECS if the ucode supports a priv access
map. That route is not present as a normal production control.

One code-quality finding from this audit:

```text
kernel_sm_debugger_session_ctrl.c:713-755
DEBUG_READ_BATCH_MEMORY checks dataOffset < dataLength

kernel_sm_debugger_session_ctrl.c:757-790
DEBUG_WRITE_BATCH_MEMORY checks dataOffset + length <= dataLength
```

The read-batch path has a weaker local `pData` bounds check than the write-batch
path. It still calls the same `_nv83deCtrlCmdDebugAccessMemory()` routine, so it
still requires a caller-owned `hMemory` and a memdesc-bounded memory range. This
does not look like a direct path to FECS microcode or the speed-select policy,
but it is the only noteworthy bounds asymmetry found in the debug access code.

Secure Falcon documentation explains why runtime debug is unlikely to expose
the protected microcode state:

```text
safety/SBR-TU10A/SWE-SBR-007-SWADS.md

PUB microcode uses stack smashing protection.
Unused DMEM/BSS are scrubbed.
SEC2 Falcon GPRs and SCP GPRs are zeroed.
Non-secure IMEM blocks are invalidated/scrubbed after HS entry.
CMEM aperture is disabled.
TRACEPC is disabled to avoid exposing control flow.
```

Current conclusion: the debug path gives mapped-surface, owned-memory, and
allowlisted-register access. It does not expose a production path to read FECS
IMEM/DMEM or to write `0x409664` on GV100/CMP. The closest internal bypass-style
mechanism in visible code is the verification-only access-map modification
control, not an overflow.
