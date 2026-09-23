# NVIDIA Driver Tree Falcon/Fuse Notes - 2026-05-20

## Scope

Source tree inspected:

```text
/home/josh/containers/temp/nvdrv
```

The goal was to identify useful read-only evidence for the GV100/CMP 100-210 `1/16` Tensor Core issue-rate cap, especially older V100 driver code, Falcon boot policy, and fuse/SKU handling.

## High-Level Result

This tree supports the existing model:

```text
The Tensor Core cap is likely keyed from hidden SKU/fuse/policy state below normal VBIOS/personality identity.
```

I did not find a documented `tensor disable` or `tensor throttle` fuse in the inspected GV100 public-style headers. I did find several concrete mechanisms NVIDIA uses for SKU/fuse policy:

1. SEC2/PUB/SBR Falcon code establishes protected runtime conditions and lowers PLMs for PMU and CTXSW microcodes.
2. GV100 RM has fuse register read paths, but fuse access is mediated by RM/HAL and privilege/JTAG clock handling.
3. PMU VFE has a `SINGLE_SENSED_FUSE` variable class that reads GPU/board fuse values from BAR0 registers and feeds perf/voltage/frequency equations.
4. NVAPI/RM raw fuse controls exist in headers, but the RM implementation in this tree says raw fuse data was only implemented for pre-Fermi chips and returns no data on newer chips.
5. MODS fuse tooling contains SKU/floorsweep logic, including GPC/TPC/FBP/FBPA/L2 enable checks and IFF records.

## Falcon / SBR Evidence

Relevant docs:

```text
integ/gpu_drv/stage_rel/safety/SBR-TU10A/SWE-SBR-007-SWADS.md
integ/gpu_drv/stage_rel/safety/SBR-TU10A/SWE-SBR-015-IFACE.md
```

Key points:

```text
PUB/SBR microcode runs on the SEC2 Falcon.
PUB does not accept normal input arguments from nvgpu-rm.
PUB reports status through NV_PSEC_FALCON_MAILBOX0.
PUB lowers PLMs so PMU and CTXSW FECS/GPCCS microcodes can run non-secure and access protected registers.
PUB resets PMU and leaves SEC2 halted in heavy-secure mode.
SEC2 Falcon BootROM verifies the HS signature and grants HS privilege.
Production PUB does not offer normal debugging.
```

Interpretation:

```text
The source matches the idea that policy affecting GR/FECS/GPCCS/PMU behavior can be established below ordinary driver-visible identity. The host driver can load signed microcode and observe status, but production HS behavior is not freely patchable.
```

## GV100 Fuse HAL

Relevant files:

```text
dev/gpu_drv/stage_rel/drivers/resman/kernel/fuse/volta/fusegv100.c
integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/fuse/arch/volta/fuse_gv100.c
```

The GV100 fuse HAL provides `fuseRead_GV100` and `fuseWrite_GV100`. Fuse access is not a plain user BAR0 read path. The HAL:

```text
Ungates the JTAG clock around fuse register access.
Mentions privilege-level handling for fuse read/write access.
Reads fuse registers through GPU_REG_RD32 after RM/HAL setup.
Falls back to GP10x/GP100/common fuse options for many fields.
```

Notable GV100-handled options include:

```text
HAL_FUSE_OPT_DEVID_SW_OVERRIDE_DISABLE
HAL_FUSE_OPT_NOTEBOOK
HAL_FUSE_OPT_PRIV_PDI_0 / PRIV_PDI_1
HAL_FUSE_STATUS_OPT_GPC
HAL_FUSE_STATUS_OPT_FBP
HAL_FUSE_STATUS_OPT_FBIO
HAL_FUSE_OPT_ECC_EN
HAL_FUSE_OPT_SAMPLE
HAL_FUSE_OPT_DISABLE_GEN3_SPEED
HAL_FUSE_OPT_PCIE_BOOT_GEN3_DISABLE
HAL_FUSE_OPT_NVENC_THROTTLE
```

I did not find a named GV100 fuse option for `TENSOR`, `HMMA`, or an explicit Tensor Core throttle in this HAL.

## Raw Fuse RM/NVAPI Route

Relevant headers:

```text
dev/gpu_drv/stage_rel/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080fuse.h
```

The header defines:

```text
NV2080_CTRL_CMD_FUSE_GET_RAW_DATA_SIZE
NV2080_CTRL_CMD_FUSE_GET_RAW_DATA
```

NVAPI exposes:

```text
NvAPI_GPU_GetRawFuseData
NVAPI_MAX_RAW_FUSE_DATA_SIZE = 64
```

Relevant implementation:

```text
dev/gpu_drv/stage_rel/apps/common/src/nvapiEscape.cpp
dev/gpu_drv/stage_rel/drivers/resman/kernel/fuse/nv/objfuse.c
```

`nvapiEscape.cpp` forwards `NvAPI_GPU_GetRawFuseData` to:

```text
NV2080_CTRL_CMD_FUSE_GET_RAW_DATA
```

But `objfuse.c` says:

```text
This function was only implemented for pre-Fermi chips.
```

and:

```text
NV2080_CTRL_CMD_FUSE_GET_RAW_DATA_SIZE -> fuseDataSize = 0
NV2080_CTRL_CMD_FUSE_GET_RAW_DATA      -> returns NV_OK without filling data
```

Interpretation:

```text
NVAPI raw fuse is not likely to help for GV100 in this tree. The safer read-only path is RM/MODS-style per-option fuse access or internal instrumentation, not NvAPI_GPU_GetRawFuseData.
```

## PMU VFE Sensed Fuse Path

Relevant files:

```text
dev/gpu_drv/stage_rel/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080vfe.h
dev/gpu_drv/stage_rel/pmu_sw/prod_app/perf/nv/3x/vfe_var_single_sensed_fuse.c
```

The VFE system has:

```text
NV2080_CTRL_PERF_VFE_VAR_TYPE_SINGLE_SENSED_FUSE
NV2080_CTRL_PERF_VFE_VAR_SINGLE_SENSED_FUSE_OVERRIDE_INFO
fuseValOverride
bFuseRegkeyOverride
```

The PMU implementation:

```text
Reads fuse and fuse version during construction.
Can use a regkey override when permitted by the descriptor.
Uses `REG_RD32(BAR0, regAddr)` and optional indexed register reads.
Applies version checks, default fallback, scale, and offset.
Returns the sensed fuse value to VFE equations.
```

VFIELD IDs include:

```text
STRAP_SPEEDO
STRAP_IDDQ
STRAP_BOARD_BINNING
STRAP_SRAM_VMIN
STRAP_BOOT_VMIN_NVVDD
KAPPA
```

Interpretation:

```text
This is strong evidence that fuse/strap-derived values are actively fed into PMU performance policy. It does not identify the Tensor Core divider, but it gives concrete places to inspect for SKU/perf-bin policy.
```

## MODS / SKU Fuse Logic

Relevant files:

```text
dev/gpu_drv/stage_rel/diag/mods/gpu/fuse/gv100_fuse.cpp
dev/gpu_drv/stage_rel/diag/mods/gpu/fuse/skuhandling/basicskuhandler.cpp
dev/gpu_drv/stage_rel/diag/mods/gpu/fuse/gpufuse.cpp
dev/gpu_drv/stage_rel/diag/mods/tools/floorsweep/floorsweep_libs/FsInfo/VoltaFsInfo.py
```

GV100 MODS fuse code:

```text
Parses fuse XML.
Uses direct fuse reads.
Tracks fuse rows, fuse columns, fuseless ranges, and KFuse words.
Checks fuse privilege security.
```

SKU handling:

```text
Checks SKU matches from fuse definitions.
Uses OPT_SKU_ID.
Checks GPC/TPC/FBP/FBPA/L2 floorsweep constraints.
Handles TPC disable and reconfig fuses specially.
Reads IFF rows specified by SKU.
```

`GpuFuse::GetFuseFilename()` picks the chip fuse definition from the device ID:

```text
<device>_f.json, if present
<device>_f.xml, otherwise
```

The MODS makefile says `CHIP_XML_DIR` defaults to:

```text
//sw/mods/chipfuse
```

and `diag/mods/tools/git_p4_paths` explicitly includes:

```text
//sw/mods/chipfuse/...
```

A local `fdfind` search did not find the GV100 chipfuse XML/JSON inputs in `/home/josh/containers/temp/nvdrv` or this workspace. The checked-out tree has the parser and SKU logic, but not the proprietary chipfuse database that names the actual GV100 SKU fields.

Follow-up local search on 2026-05-20 broadened this to:

```text
/home
/srv
/mnt
/opt
/var/tmp
/tmp
```

using `fdfind -HI` for:

```text
gv100_f.json / gv100_f.xml
gvlit1_f.json / gvlit1_f.xml
volta*_f.json / volta*_f.xml
chipfuse paths
MODS/fielddiag archives
GV100 fuse/SKU/nvspec paths
```

Result:

```text
No GV100 chipfuse definition file was found.
No `chipfuse` directory was found.
No MODS/fielddiag archive with chipfuse-like naming was found.
```

The only local GV100/SKU-adjacent files found were MODS source and board/test specs, for example:

```text
diag/mods/gpu/fuse/gv100_fuse.cpp
diag/mods/gpu/fuse/gv100_fuse.h
diag/mods/contrib/tesla/pg506sku*.json
diag/mods/contrib/tesla/pg509sku200_nvspec.json
```

The `pg506`/`pg509` nvspec JSONs are not fuse definitions. They contain board-level product properties such as:

```text
NVSPECS_BOARD_PROJECT
NVSPECS_BOARD_SKU
NVSPECS_DEVICE_ID
NVSPECS_ECC
NVSPECS_MEMORY_FB_SIZE / NVSPECS_MEMORY_FB_SIZE_MB
NVSPECS_TPC_COUNT
NVSPECS_FB_BAR_SIZE
NVSPECS_PCI_CLASS_CODE
```

Examples:

```text
pg506sku200_nvspec.json: PG506 SKU 200, device 0x20B0, ECC true, TPC_COUNT 54, memory 40940 MB
pg506sku202_nvspec.json: PG506 SKU 202, device 0x20B0, ECC true, TPC_COUNT 54, memory 40940 MB
pg506sku230_nvspecs.json: G506 SKU 230, device 0x20B6, ECC true, TPC_COUNT 62, memory 98280 MB
pg506sku242_nvspecs.json: G506 SKU 242, device 0x20B3, ECC true, TPC_COUNT 62, memory 65520 MB
pg509sku200_nvspec.json: PG509 SKU 200, device 0x20B0, ECC true, TPC_COUNT 54, memory 40940 MB
```

These are Ampere-class board/spec files (`0x20B*` device IDs), not local GV100/CMP fuse definitions. They are not enough to recover `OPT_SKU_ID` or IFF rows for GV100.

An archive listing scan across 398 local `.zip`, `.tar`, `.tgz`, `.tar.gz`, `.xz`, `.bz2`, and `.7z` packages under the local `nvdrv` copies also found no `chipfuse`, `gv100_f`, or GV100 fuse-definition payload.

Web search for exact public strings such as `gv100_f.xml`, `gv100_f.json`, `//sw/mods/chipfuse`, and `chipfuse gv100` did not find a public copy.

GV100 floorsweep geometry in MODS:

```text
MAX_GPC    = 6
MAX_TPC    = 42
MAX_FBP    = 8
MAX_FBIO   = 16
MAX_ROP    = 16
MAX_NVLINK = 6
```

The floorsweep helper parses:

```text
OPT_FBP_DISABLE
OPT_FBIO_DISABLE
OPT_ROP_L2_DISABLE
OPT_GPC_DISABLE
OPT_PES_DISABLE
OPT_TPC_GPC{0..5}_DISABLE
OPT_NVLINK_DISABLE
```

Interpretation:

```text
The MODS path is probably the best source-side lead for identifying hidden SKU state. It may reveal whether CMP and V100-personality cards differ in SKU/IFF/floorsweep definitions even when the runtime PCI/VBIOS identity has been changed.
```

The blocker is now specific: locate the matching `//sw/mods/chipfuse` GV100 definition, likely named like `gv100_f.json` or `gv100_f.xml`, then inspect `OPT_SKU_ID`, IFF rows, floorsweep masks, and any perf/SKU policy fields not surfaced through normal NVML/CUDA.

Current status after the broadened search:

```text
The chipfuse file is not present in the local trees searched and does not appear to be trivially public by exact-name search.
```

## MODS Tesla HMMA Performance Baseline

Relevant file:

```text
dev/gpu_drv/stage_rel/diag/mods/contrib/tesla/tesla_boards.spc
```

This file defines expected GV100 linpack lower bounds for production tests. Examples:

```text
PG500SKU200  CudaLinpackHMMAgemm:           71346 GFLOPS
PG500SKU201  CudaLinpackHMMAgemm:           76000 GFLOPS
PG503SKU201  CudaLinpackHMMAgemm:           79600 GFLOPS
PG503SKU250  CudaLinpackHMMAgemm:           60466 GFLOPS
PG503SKU250  CudaLinpackHMMAgemm.h884_fp16: 63929 GFLOPS
```

`PG503SKU250` uses:

```text
-test_devid 0x1df1
memSizeGb = 12
```

Interpretation:

```text
NVIDIA's own MODS production-style GV100 HMMA expectations are in the same range as the full-speed Vast V100 controls, not the local 5-7 TFLOPS results. This independently reinforces that the local cards are running a policy-limited Tensor Core path.
```

I did not find a useful MODS entry for local runtime device ID `0x1df4` in this tree. The exact local V100-personality device ID may be newer than this MODS snapshot, or the relevant board/spec files may be outside the checked-out subset.

## GV100 Generated Register Reference Pass

Relevant files:

```text
dev/gpu_drv/stage_rel/diag/mods/mdiag/generated/gv100/gv100_ref.txt.gz
dev/gpu_drv/stage_rel/apps/nvwatch/gpu/gr/grgv100.c
```

Searches for `TENSOR`, `HMMA`, `MMA`, scheduler issue, throttle, and divider terms did not expose a named Tensor Core issue-rate register.

The generated reference does show several nearby classes of control/status:

```text
NV_PBUS_FUSE_FMT_IFF_RECORD_* command formats
NV_PCHIPLET_PWR_GPC{0..5}_PE_* slowdown/speedup dividend tables
NV_PGRAPH_PRI_GPC*_TPC*_SM_POWER_THROTTLE_CTL
NV_PGRAPH_PRI_GPC*_TPC*_SM_POWER_VST_CTL / DATA
```

`nvwatch` dumps `SM_POWER_THROTTLE_CTL` and related SM power registers for GV100, but the inspected names point to power/thermal throttling visibility rather than an explicit Tensor/HMMA segmentation bit.

Interpretation:

```text
The generated register reference reinforces the IFF path as important: IFF records can patch registers in several address spaces. It does not provide a self-documenting Tensor Core limiter field. If the 1/16 divider is register-programmed, it is likely hidden behind a generic/power/privileged field name, an IFF row from chipfuse data, or signed firmware policy.
```

## What This Changes

The next practical discriminator is not Boot ROM glitching.

The better next steps are:

1. Identify whether the local driver stack exposes any usable RM control path for `HAL_FUSE_*` option reads.
2. Locate the missing `//sw/mods/chipfuse` GV100 fuse definition, especially `OPT_SKU_ID`, IFF records, and fields related to board binning or perf policy.
3. Compare accessible fuse-derived values between CMP-identity and V100-personality cards.
4. If a maintenance environment is available, use read-only RM/MODS-style fuse reads instead of blind BAR0/PSTRAPS writes.

## Current Assessment

```text
PSTRAPS is still worth understanding, but the stronger lead from this nvdrv tree is RM/MODS fuse/SKU policy and PMU VFE sensed-fuse inputs.
```

No evidence found yet that a documented PSTRAPS bit controls Tensor Core issue rate directly.
