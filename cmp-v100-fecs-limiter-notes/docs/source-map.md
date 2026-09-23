# Source Map

This page gives the exact source paths and line ranges behind the main claims.
The snippets are intentionally small and scoped. They are here so another
researcher can verify the logic without wading through the entire source tree.

The local paths were from a source tree understood to originate from the 2022
Lapsus$ NVIDIA leak. Do not publish the source tree itself.

## 1. GV100 FECS Speed-Select Register

Path:

```text
drivers/common/inc/hwref/volta/gv100/dev_graphics_nobundle.h
```

Line range checked:

```text
5436-5472
```

Scoped excerpt:

```c
#define NV_PGRAPH_PRI_FECS_FEATURE_READOUT_SM_SPEED_SELECT_DP    20:20
#define NV_PGRAPH_PRI_FECS_FEATURE_READOUT_SM_SPEED_SELECT_IMLA  21:21
#define NV_PGRAPH_PRI_FECS_FEATURE_READOUT_SM_SPEED_SELECT_FMLA  22:22

#define NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT      0x00409664
#define NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT_IMLA 0:0
#define NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT_FMLA 4:4
#define NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT_DP   8:8
```

Why it matters:

```text
This names the exact FECS register and shows GV100 exposes binary speed-select
state for DP, IMLA, and FMLA.
```

## 2. GV100 CTXSW Firmware Mirror

Path:

```text
drivers/common/inc/hwref/volta/gv100/dev_ctxsw_firmware.h
```

Line range checked:

```text
3190-3209
```

Scoped excerpt:

```c
#define NV_CTXSW_FIRMWARE_FEATURE_OVERRIDE_SM_SPEED_SELECT      0x00019900
#define NV_CTXSW_FIRMWARE_FEATURE_OVERRIDE_SM_SPEED_SELECT_IMLA 0:0
#define NV_CTXSW_FIRMWARE_FEATURE_OVERRIDE_SM_SPEED_SELECT_FMLA 4:4
#define NV_CTXSW_FIRMWARE_FEATURE_OVERRIDE_SM_SPEED_SELECT_DP   8:8
```

Why it matters:

```text
The speed-select register is not only a BAR0 manual artifact. It also exists
in the generated context-switch firmware surface.
```

## 3. RM Public Issue-Rate Readback Is Stubbed For GV100

Path:

```text
drivers/resman/config/haldefs/gr.def
```

Line range checked:

```text
7500-7509
```

Scoped excerpt:

```perl
GET_SM_ISSUE_RATE_MODIFIER => [
    STUB_RETURNS  => NV_ERR_NOT_SUPPORTED,
    _TU102        => [ TURING, ],
    _GA100        => [ AMPERE_and_later, ],
    _STUB         => [ pre_TURING, ],
],
```

Why it matters:

```text
GV100 is pre-Turing. The normal public RM readback for issue-rate state is
deliberately unavailable on GV100.
```

## 4. Turing Reads FECS Feature Readout Directly

Path:

```text
drivers/resman/kernel/gr/turing/grtu102.c
```

Line range checked:

```text
1011-1028
```

Scoped excerpt:

```c
NvU32 regVal = GPU_REG_RD32(pGpu, NV_PGRAPH_PRI_FECS_FEATURE_READOUT_1);

pParams->imla0  = REF_VAL(..._SM_SPEED_SELECT_IMLA0, regVal);
pParams->fmla16 = REF_VAL(..._SM_SPEED_SELECT_FMLA16, regVal);
pParams->dp     = REF_VAL(..._SM_SPEED_SELECT_DP, regVal);
```

Why it matters:

```text
The later public readback mechanism is exactly the family we expected:
read feature state, return issue-rate selectors.
```

## 5. Ampere Reads FUSE Feature Readout

Path:

```text
drivers/resman/kernel/gr/ampere/grga100.c
```

Line range checked:

```text
1523-1545
1589-1598
```

Scoped excerpt:

```c
fuseRead_HAL(pFuse, pGpu, ENG_REG_FUSE(pFuse, _FUSE, _FEATURE_READOUT_1), &regVal);

pParams->fmla16 = REF_VAL(NV_FUSE_FEATURE_READOUT_1_SM_SPEED_SELECT_FMLA16, regVal);

ct_assert(NV2080_CTRL_GR_GET_SM_ISSUE_RATE_MODIFIER_FMLA32_REDUCED_SPEED_1_16 ==
          NV_FUSE_FEATURE_OVERRIDE_SM_SPEED_SELECT_FMLA32_REDUCED_SPEED_1_16);
```

Why it matters:

```text
Ampere exposes explicit multi-rate selectors, including 1/16-class values, and
asserts that RM API values match hardware override values.
```

## 6. GV100 Access Map Source

Path:

```text
drivers/resman/src/physical/gpu/arch/volta/gpu_gv100.c
```

Line range checked:

```text
117-130
```

Scoped excerpt:

```c
gpuGetUserRegisterAccessMap_GV100(...)
{
    *ppComprData = UserAccessMapCompr;
    *pSize = UserAccessMapComprSize;
}
```

Why it matters:

```text
GV100 user-originated regops are controlled by the generated GV100 access map.
Local lookup against user_access_map.bin denied 0x00409660 and 0x00409664.
```

## 7. User Register Access Permission Check

Path:

```text
drivers/resman/src/kernel/gpu/gpu_register_access_map.c
```

Line range checked:

```text
123-159
```

Scoped excerpt:

```c
if (bitOffset >= (pGpu->userRegisterAccessMapSize * 8))
    return NV_FALSE;

if ((offset % 4) != 0)
    return NV_FALSE;

return nvBitFieldTest((NvU32*) pGpu->pUserRegisterAccessMap,
                      pGpu->userRegisterAccessMapSize / sizeof(NvU32),
                      bitOffset);
```

Why it matters:

```text
This is the bitmap gate behind user-accessible register operations.
```

## 8. Regops Invalid Offset Path

Path:

```text
drivers/resman/src/kernel/gpu/subdevice/subdevice_ctrl_gpu_regops.c
```

Line range checked:

```text
649-702
```

Scoped excerpt:

```c
status = gpuValidateRegOffset(pGpu, pRegOps[i].regOffset);
if (status != NV_OK)
{
    regStatus = NV2080_CTRL_GPU_REG_OP_STATUS_INVALID_OFFSET;
}
```

Why it matters:

```text
This explains the observed invalid-offset behavior. The request is rejected
before raw FECS MMIO is touched.
```

## 9. NV83DE Debug Regops Still Validate

Path:

```text
drivers/resman/src/kernel/gpu/gr/kernel_sm_debugger_session_ctrl.c
```

Line range checked:

```text
678-710
```

Scoped excerpt:

```c
NV_CHECK_OK_OR_RETURN(LEVEL_INFO,
    gpuValidateRegOps(pGpu, pParams->regOps, pParams->regOpCount,
                      pParams->bNonTransactional, isClientGspPlugin));
```

Why it matters:

```text
The debugger object is not a wider FECS register bypass. It uses the same
regops validation path.
```

## 10. NV83DE Debug Memory Requires Caller-Owned hMemory

Path:

```text
drivers/resman/src/kernel/gpu/gr/kernel_sm_debugger_session_ctrl.c
```

Line range checked:

```text
491-522
```

Scoped excerpt:

```c
if (serverutilGetResourceRef(hClient, hMemory, &pResourceRef) != NV_OK)
    return NV_ERR_INSUFFICIENT_PERMISSIONS;

if ((length == 0) || (!portSafeAddU64(offset, length, &totalLength)))
    return NV_ERR_INVALID_ARGUMENT;
if (totalLength > pMemDesc->Size)
    return NV_ERR_INVALID_ARGUMENT;
```

Why it matters:

```text
Debug memory access can copy memory behind a handle the caller already owns.
It does not manufacture a handle for private FECS/GPCCS firmware allocations.
```

## 11. Batch Debug Memory Bounds Asymmetry

Path:

```text
drivers/resman/src/kernel/gpu/gr/kernel_sm_debugger_session_ctrl.c
```

Line range checked:

```text
713-790
```

Scoped excerpt:

```c
// read batch
pParams->entries[i].dataOffset < pParams->dataLength

// write batch
(pParams->entries[i].dataOffset + pParams->entries[i].length) <= pParams->dataLength
```

Why it matters:

```text
This is the only notable debug-path bounds asymmetry found. It is still behind
caller-owned hMemory and memdesc bounds, so it does not directly solve FECS
speed-select access.
```

## 12. Verification-Only Access Map Editor

Path:

```text
drivers/resman/src/physical/gpu/subdevice/subdevice_diag_ctrl.c
```

Line range checked:

```text
406-447
```

Scoped excerpt:

```c
#if defined(NV_VERIF_FEATURES) || RMCFG_FEATURE_ENABLED(VERIF_ONLY_CONTROLS)

pGpu->pUserRegisterAccessMap = NvP64_VALUE(pParams->pNewAccessMap);
...
gpuSetUserRegisterAccessPermissions(pGpu, pParams->offset,
                                     pParams->size, pParams->bAllow);
```

Why it matters:

```text
This is the clearest internal-style route to alter register permissions, but
it is verification-only and not a production GV100 path.
```

## 13. GV100 CMP SKU Detection Exists, But Is Not The Whole Limiter

Path:

```text
drivers/resman/src/physical/gpu/arch/volta/gpu_gv100.c
```

Line range checked:

```text
855-880
```

Scoped excerpt:

```c
gpuGetIsCmpSku_GV100(...)
{
    fuseGetStateByOption_HAL(... HAL_FUSE_OPT_PCIE_BOOT_GEN3_DISABLE ...);
    fuseGetStateByOption_HAL(... HAL_FUSE_OPT_PCIE_BOOT_GEN23_DISABLE ...);
}
```

Why it matters:

```text
CMP identity is source-visible and fuse-derived. The direct Tensor limiter
evidence still points more strongly to FECS speed-select state than to this
reporting flag alone.
```

