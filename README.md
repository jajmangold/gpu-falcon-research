# GPU Falcon Research

Reverse-engineering the FECS speed-select limiter on NVIDIA CMP 100-210 (GV100) GPUs that refuse to deliver full Tesla V100 Tensor throughput after V100 firmware flashes.

## License

[MIT](LICENSE)

## The Cards

The CMP 100-210 is a crypto-mining-era card built on GV100 silicon -- the same chip as Tesla V100 and Titan V. Second-hand cards go for cheap. I bought 4 for ~$250, then a used Octominer from eBay with 12 more for $1500. ~$2k for 256GB of HBM2 VRAM with 13.28 TB/s aggregate bandwidth.

Some of the new batch came flashed with V100 firmware. They showed up as `Tesla V100-PCIE-12GB`, exposed 80 SMs instead of 68, and looked like real V100s in every way CUDA and `nvidia-smi` could see.

But they delivered roughly **1/16th** the Tensor/HMMA throughput of a real V100.

The question became: *what is holding them back, and can it be cleared?*

## The Answer (So Far)

The limiter lives in a single FECS register:

```
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
BAR0 offset: 0x00409664
Value on capped cards: 0x00000999
```

Written during early VBIOS init, this register asserts reduced-speed policy for IMLA, FMLA, and DP math classes. The production software surfaces we found do not provide a normal way to clear it on GV100.

## The Three-Layer Model

The confusion around "it shows up as a V100, why doesn't it act like one" comes from three independent layers:

```mermaid
flowchart TD
  pci["Layer 1: visible PCI/CUDA personality"] --> pci_items["name, PCI ID, visible SM count"]
  board["Layer 2: board management identity"] --> board_items["InfoROM, OBD, IMG/profile data"]
  policy["Layer 3: low-level GR/FECS policy"] --> policy_items["speed-select state at 0x409664"]
  pci_items --> flash["V100 flash can change this"]
  board_items --> partial["local cards still retained CMP clues"]
  policy_items --> limiter["Tensor limiter remained active"]
```

A VBIOS flash can change layers 1 and 2. Layer 3 -- the FECS speed-select policy -- stays asserted. That's why "it shows up like a V100" was never enough evidence.

## The Key Flow

```mermaid
flowchart TD
  vbios["VBIOS init script"] --> op["R[0x409664] &= 0xfffff666 |= 0x00000999"]
  op --> reg["FECS FEATURE_OVERRIDE_SM_SPEED_SELECT"]
  reg --> classes["IMLA, FMLA, DP reduced + override bits"]
  classes --> policy["GV100 reduced-speed policy"]
  policy --> symptom["Tensor/HMMA throughput stays capped"]
```

## What the Leaked Source Tree Revealed

The 2022 [Lapsus$ NVIDIA ransomware leak](https://www.deepwatch.com/labs/nvidia-confirms-data-was-stolen-lapsus-takes-credit) included a massive internal Volta-era source tree. It contained HAL dispatch tables, register definitions, generated headers, and verification-only controls that don't exist in NVIDIA's public GPU kernel modules.

This repo does not redistribute the tree. But the source gave us a map of what NVIDIA calls things -- and that map connected live hardware measurements to driver internals without guessing from raw offsets.

### The Register Definitions

The exact FECS speed-select register family, from `volta/gv100/dev_graphics_nobundle.h`:

```c
#define NV_PGRAPH_PRI_FECS_FEATURE_READOUT_SM_SPEED_SELECT_DP    20:20
#define NV_PGRAPH_PRI_FECS_FEATURE_READOUT_SM_SPEED_SELECT_IMLA  21:21
#define NV_PGRAPH_PRI_FECS_FEATURE_READOUT_SM_SPEED_SELECT_FMLA  22:22

#define NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT      0x00409664
#define NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT_IMLA 0:0
#define NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT_FMLA 4:4
#define NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT_DP   8:8
```

The same register also appears in the context-switch firmware surface (`dev_ctxsw_firmware.h`), proving it's not just a BAR0 artifact -- the speed-select state is part of the FECS/CTXSW lifecycle.

### The RM HAL Dispatch Table

The source shows exactly why the limiter can't be read through normal driver APIs. From `gr.def`:

```perl
GET_SM_ISSUE_RATE_MODIFIER => [
    STUB_RETURNS  => NV_ERR_NOT_SUPPORTED,
    _TU102        => [ TURING, ],
    _GA100        => [ AMPERE_and_later, ],
    _STUB         => [ pre_TURING, ],
],
```

GV100 is pre-Turing. The readback path is deliberately stubbed. Meanwhile, the Turing implementation reads the exact register family we expected:

```c
// grtu102.c
NvU32 regVal = GPU_REG_RD32(pGpu, NV_PGRAPH_PRI_FECS_FEATURE_READOUT_1);
pParams->imla0  = REF_VAL(..._SM_SPEED_SELECT_IMLA0, regVal);
pParams->fmla16 = REF_VAL(..._SM_SPEED_SELECT_FMLA16, regVal);
pParams->dp     = REF_VAL(..._SM_SPEED_SELECT_DP, regVal);
```

And Ampere exposes explicit multi-rate selectors including the 1/16 value we measured:

```c
// grga100.c
pParams->fmla16 = REF_VAL(NV_FUSE_FEATURE_READOUT_1_SM_SPEED_SELECT_FMLA16, regVal);
ct_assert(NV2080_CTRL_GR_GET_SM_ISSUE_RATE_MODIFIER_FMLA32_REDUCED_SPEED_1_16 ==
          NV_FUSE_FEATURE_OVERRIDE_SM_SPEED_SELECT_FMLA32_REDUCED_SPEED_1_16);
```

### The Access Map That Blocks Debuggers

The GV100 user register access map (`gpu_gv100.c`) controls what user-space regops can reach. Local lookup against the generated `user_access_map.bin` denied exactly the two registers that matter:

```
0x00409660  NV_PGRAPH_PRI_FECS_FEATURE_READOUT
0x00409664  NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
```

The verification-only access map editor (`subdevice_diag_ctrl.c`) can rewrite these permissions, but it's gated behind `NV_VERIF_FEATURES` -- a build-time flag that production drivers don't set.

### The Generation Transition

Internal engineering notes in the source describe a transition from older 1-bit SM speed-select fuses to newer 3-bit fuses. GV100's visible model is binary (`FULL_SPEED` / `REDUCED_SPEED`). Later chips expose explicit divisors: `1/2, 1/4, 1/8, 1/16, 1/32`.

The 1/16 we measure on GV100 is real. The exact divisor value is hidden behind the 1-bit abstraction layer that GV100 exposes.

### What the Source Did NOT Give Us

The source tree did not provide a turnkey unlock. It gave us names and boundaries, not bypasses:

- `RMOverrideSmSpeedSelect` and `RMSchMicroSched` exist but are verification-only or later-generation paths
- The speed-select override producer (the code that *writes* `0x999`) was not found in visible RM code
- CMP SKU detection exists (`gpuGetIsCmpSku_GV100`) but is a reporting flag, not the limiter itself

The source confirmed that NVIDIA knows about this mechanism. It also confirmed that the GV100 production path deliberately blocks user access to it.

## Why the Register Is a Dead End (For Now)

Every path to alter the register ends at a boundary:

| Path | What Happens |
|------|-------------|
| RM `GET_SM_ISSUE_RATE_MODIFIER` | Stubbed with `NV_ERR_NOT_SUPPORTED` on pre-Turing |
| Debug object (`NV83DE`) regops | Register denied by GV100 user access map |
| VBIOS flash | Changes identity, not FECS policy |
| Firmware/debugdump scans | Names appear, the speed-select producer does not |

The register tells the hardware to use reduced mode. The exact 1/16 implementation is likely in private fuse/POR/IFF data or signed FECS/GR policy below the visible source boundary.

## Where the 1/16 Probably Comes From

```mermaid
flowchart TD
  kernel["kernel issues HMMA work"] --> sm["SM schedulers request Tensor issue slots"]
  sm --> state["FECS/GR speed-select state says reduced"]
  state --> gate["hardware cadence or issue-token gate"]
  gate --> grants["only some Tensor issue opportunities are granted"]
  grants --> result["sustained throughput looks 1/16-class"]
```

## Repository Structure

```
.
├── fecs-limiter-notes/    # Long-form research narrative with Mermaid diagrams
├── notes/                 # Dated research notes (probes, benchmarks, findings)
├── scripts/               # CUDA probe kernels (HMMA, DP4A, SGEMM, WMMA)
├── tools/                 # C/Python analysis tools (VBIOS decode, register probes)
└── runs/                  # Raw run data (gitignored)
```

## What Was Tried

Every branch converged on FECS speed-select. Every ordinary production path to alter it ended at a privilege, generation, or signature boundary:

| Branch | Why It Was Plausible | What Happened |
|--------|---------------------|---------------|
| Benchmarks and clocks | Most "slow GPU" bugs are measurement bugs | Slowdown tracked Tensor/HMMA issue behavior, not clocks |
| V100-personality VBIOS | Maybe identity was the whole lock | Changed visible behavior, not the Tensor limiter |
| VBIOS init scripts | Early init can write privileged registers | Capped images write `0x999` to `0x409664` |
| BAR0/FECS register probing | A live register would connect intent to state | `0x409664` became the central observable |
| RM issue-rate controls | A supported driver path would be cleanest | GV100 path stubbed as unsupported |
| RM regops and debug object | Debug objects sometimes expose lower-level access | Denied by GV100 user access map |
| Later-card source comparison | Later chips might show how NVIDIA represents this | They expose explicit multi-rate selectors including 1/16 |

## Key Findings

| Finding | Reference |
|---------|-----------|
| FECS register `0x409664` holds the speed-select state | [`fecs-limiter-notes/`](fecs-limiter-notes/) |
| VBIOS init scripts write `0x999` to this register on capped images | [`notes/nvdrv-backtrack-from-v100-baseline-2026-05-20.md`](notes/nvdrv-backtrack-from-v100-baseline-2026-05-20.md) |
| FP64 GEMM runs at 6.8-7.3% of V100 full-speed (DP_REDUCED) | [`notes/sgemm-dgemm-ratio-probe-2026-05-20.md`](notes/sgemm-dgemm-ratio-probe-2026-05-20.md) |
| V100 personality changes SM count but not the FMLA/HMMA cap | [`notes/local-tensor-sgemm-sweep-2026-05-20.md`](notes/local-tensor-sgemm-sweep-2026-05-20.md) |
| RM readback stubbed for pre-Turing (`GET_SM_ISSUE_RATE_MODIFIER`) | [`fecs-limiter-notes/docs/evidence-chain.md`](fecs-limiter-notes/docs/evidence-chain.md) |
| User access map denies `0x409660` and `0x409664` | [`fecs-limiter-notes/docs/debug-paths.md`](fecs-limiter-notes/docs/debug-paths.md) |

## Running the Probes

CUDA probe scripts in `scripts/` require CUDA 12+ and a Volta-class GPU:

```bash
nvcc scripts/hmma_issue_probe.cu -o hmma_probe
./hmma_probe

nvcc scripts/sgemm_dgemm_ratio_probe.cu -o sgemm_probe
./sgemm_probe
```

Python analysis tools in `tools/` work with VBIOS dumps and `nvidia-smi` CSV output.

## Reading the Notes

Start with [`fecs-limiter-notes/`](fecs-limiter-notes/) for the full narrative, then drill into supporting docs:

| Document | Purpose |
|----------|---------|
| `docs/evidence-chain.md` | Compact proof chain from symptom to FECS speed-select |
| `docs/source-map.md` | Source paths, line ranges, scoped snippets |
| `docs/experiment-log.md` | What was tried, why, and what each branch ruled out |
| `docs/registers-and-identities.md` | Register constants and identity layers |
| `docs/throughput-model.md` | Working model for burst behavior and global Tensor budget |
| `docs/blocked-paths.md` | Dead ends and boundaries |
| `docs/next-research.md` | What would actually produce new information |

## Scope

- Non-destructive, read-only investigation of owned hardware
- No firmware flashing, EEPROM overwrites, or active GPU disruption
- Documentation-only: no NVIDIA source files, firmware blobs, or leaked archives
