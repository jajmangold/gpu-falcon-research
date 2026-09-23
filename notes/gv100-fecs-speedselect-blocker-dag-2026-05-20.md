# GV100 FECS Speed-Select Blocker DAG

Date: 2026-05-20

This is a compact source/evidence map for the current GV100 Tensor/FP64 limiter investigation.

```text
local capped behavior
  -> HMMA/Tensor and FP64 around 6.25 percent of expected
  -> FP32/SGEMM healthy

VBIOS init script
  -> writes NV_REG mask op to 0x409664
  -> address: NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
  -> value mask: 0x00000999
  -> sets IMLA/FMLA/DP reduced + override bits

GV100 hwref
  -> 0x409660 NV_PGRAPH_PRI_FECS_FEATURE_READOUT
  -> 0x409664 NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
  -> binary full/reduced fields, not explicit 1/16 fields

later chips
  -> Turing/Ampere/later RM issue-rate readback implemented
  -> later fuse models expose 3-bit speed selectors
  -> public enum includes FMLA16_REDUCED_SPEED_1_16

GV100 RM public control
  -> GET_SM_ISSUE_RATE_MODIFIER
  -> gr.def maps pre_TURING to STUB
  -> returns NV_ERR_NOT_SUPPORTED

user-space EXEC_REG_OPS
  -> gpuValidateRegOps()
  -> gpuValidateRegOffset()
  -> denied access collapses to INVALID_OFFSET

production GV100 access maps
  -> user_access_map_gzip.h denies 0x409660 and 0x409664
  -> global_access_map_gzip.h denies 0x409660 and 0x409664
  -> public user-space cannot read target FECS state through normal regops

GV100 passthrough maps
  -> gv100_passthrough_spec.yml allows nearby FECS method/reset/mailbox registers
  -> denies 0x409618-0x4097ff
  -> denied span covers FEATURE_READOUT, FEATURE_OVERRIDE_SM_SPEED_SELECT, and PLM registers

current regaccess generator config
  -> common.yml includes FECS_FEATURE_READOUT for volta
  -> common.yml includes FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT for volta
  -> regenerated map allows 0x409660 and 0x409664
  -> checked-in production GV100 map is stricter than current config

RM access-map lifecycle
  -> pUserRegisterAccessMap is inflated from chip compressed map
  -> pUnrestrictedRegisterAccessMap is copied from pUserRegisterAccessMap before profiling removal
  -> unrestricted/admin means "base map plus profiling access", not "all registers"
  -> FECS ucode consumes committed priv-access-map buffers

verification-only diag path
  -> NV208F_CTRL_CMD_GPU_SET_USER_REGISTER_ACCESS_PERMISSIONS can swap/edit RM access map
  -> docs say it exposes sensitive functionality and belongs only in verification builds
  -> implementation is behind NV_VERIF_FEATURES or VERIF_ONLY_CONTROLS
  -> explains NVIDIA/MODS internal testing path, not production Linux availability

MODS internal/debug path
  -> Volta-953 issue-rate override vocabulary
  -> RMOverrideSmSpeedSelect / RMOverrideSmSpeedSelect1 are verification-build only
  -> Turing/Ampere implement override; GV100/Volta normal classes do not
  -> board may still hit FECS_FEATURE_OVERRIDE PRIV protection

FECS feature-override PLM
  -> 0x409664 has __PRIV_LEVEL_MASK 0x650
  -> controlling register is 0x409650 NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_PRIV_LEVEL_MASK
  -> MODS checks WRITE_PROTECTION_LEVEL0_ENABLE before issue-rate override
  -> if missing, returns PRIV_LEVEL_VIOLATION with "FECS_FEATURE_OVERRIDE is PRIV protected"

feature_override_quadro
  -> later Turing/Ampere/g00x manual and FUSE headers name this as the shared feature-override PLM fuse signal
  -> paired with feature_override_ecc for the ECC-specific FECS feature-override PLM
  -> likely source-provenance name for the 0x409650 lower gate

local SBR/PUB Falcon docs
  -> PUB is a SEC2 Falcon PLM Update Binary
  -> PUB-NS sets bootrom parameters; PUB-HS runs Heavy Secure HS Lib and PUB Core
  -> PUB Core lowers selected PLMs for PMU and CTXSW FECS/GPCCS ucodes
  -> production PUB has no debug interface
  -> debug HS-signed PUB runs only on debug boards
  -> HS ucode is encrypted/signed and verified by Falcon bootrom
  -> this explains NVIDIA's internal PLM path without exposing a production user-space path

missing producer inputs
  -> gv100_f.json / gv100_f.jsone absent locally
  -> gv100_POR.json / gv100_POR.jsone absent locally
  -> FECS policy source absent locally
  -> exact binary reduced -> 1/16 divisor mapping remains below visible GV100 source boundary
```

Current best blocker chain:

```text
VBIOS sets reduced/override at 0x409664
  -> FECS/GR honors reduced state
  -> production RM blocks user readback of 0x409660/0x409664
  -> root/admin uses unrestricted access map, but unrestricted still inherits generated base-map omissions
  -> public RM issue-rate API is stubbed on GV100
  -> internal/debug source hints exist, but require different build/map/board state
  -> even with map exposure, board-level PLM can still block writes below RM
  -> local SBR/PUB docs show PLM lowering is an HS-signed SEC2 Falcon function
```

Switch Fusee note:

```text
Tegra RCM bootROM exploit is conceptually relevant as "pre-lockout execution".
It is not a direct GV100 path because it targets Tegra USB RCM/BPMP, not a discrete PCIe GPU FECS/RM path.
```
