# GV100 Fuse Layout Source Reconstruction - 2026-05-20

## Purpose

The MODS `chipfuse` XML/JSON dictionary is not present locally or in the searched archives, so this pass reconstructs the relevant GV100 fuse and SM speed-select layout directly from the older `nvdrv` source tree:

```text
/srv/nvme-data/containers/temp/nvdrv/
```

Search was done with `fdfind` and `rg`. No live register writes or MMIO pokes were performed.

## Key Finding

The source does expose a real SM speed-select mechanism for GV100.

It does **not** expose a cleanly named `HMMA`, `TENSOR`, or `1/16` fuse for GV100. The closest source-side path is:

```text
NV_FUSE_OPT_SM_IMLA_SPEED_SELECT
NV_FUSE_OPT_SM_FMLA_SPEED_SELECT
NV_FUSE_OPT_DP_SPEED_SELECT
NV_CTXSW_FIRMWARE_FEATURE_READOUT_SM_SPEED_SELECT_*
NV_CTXSW_FIRMWARE_FEATURE_OVERRIDE_SM_SPEED_SELECT
```

Later Turing/Ampere RM code names the same class of mechanism as an SM issue-rate modifier and explicitly enumerates reduced speeds including `1/16`. That aligns strongly with the measured local behavior, even though the GV100 source names only a binary full/reduced speed select.

## Source Files

Important files:

```text
drivers/common/inc/hwref/volta/gv100/dev_fuse.h
drivers/common/inc/hwref/volta/gv100/dev_fuse_off.h
drivers/common/inc/hwref/volta/gv100/dev_ctxsw_firmware.h
drivers/resman/src/physical/gpu/fuse/arch/volta/fuse_gv100.c
drivers/resman/src/physical/gpu/gr/arch/volta/gr_gv100.c
drivers/resman/src/physical/gpu/gr/arch/turing/gr_tu102.c
drivers/resman/src/physical/gpu/gr/arch/ampere/gr_ga100.c
```

The GV100 addendum file exists but is not the missing chipfuse dictionary. It only adds ADC calibration field aliases:

```text
drivers/common/inc/swref/volta/gv100/dev_fuse_addendum.h
```

## Reconstructed GV100 Register Map

| Register | Address | Field | Meaning From Source |
| --- | ---: | --- | --- |
| `NV_FUSE_EN_SW_OVERRIDE` | `0x21040` | bit 0 | Enables software fuse override path. |
| `NV_FUSE_IFF_RECORD` | `0x21050` | start `7:0`, last `23:16` | IFF record pointer/status. |
| `NV_FUSE_OPT_FEATURE_FUSES_OVERRIDE_DISABLE` | `0x213f0` | bit 0 | Disables feature-fuse overrides. |
| `NV_FUSE_OPT_INTERNAL_SKU` | `0x213f4` | bit 0 | Internal SKU marker. |
| `NV_FUSE_OPT_DP_SPEED_SELECT` | `0x21224` | bit 0 | DP full/reduced speed select. |
| `NV_FUSE_OPT_SM_IMLA_SPEED_SELECT` | `0x21410` | bit 0 | SM IMLA full/reduced speed select. |
| `NV_FUSE_OPT_SM_FMLA_SPEED_SELECT` | `0x214e0` | bit 0 | SM FMLA full/reduced speed select. |
| `NV_FUSE_OPT_SHF_SPEED_SELECT` | `0x2159c` | bit 0 | SHF full/reduced speed select. |
| `NV_FUSE_OPT_PCIE_DEVIDA` | `0x214d8` | bits `15:0` | PCI device ID A fuse. |
| `NV_FUSE_OPT_PCIE_DEVIDB` | `0x2156c` | bits `15:0` | PCI device ID B fuse. |
| `NV_FUSE_OPT_DEVID_SW_OVERRIDE_DIS` | `0x21584` | bit 0 | Device-ID software override disable. |
| `NV_FUSE_STATUS_OPT_GPC` | `0x21c1c` | bits `31:0` | GPC disable status readout. |
| `NV_FUSE_STATUS_OPT_TPC_GPC(i)` | `0x21c38 + i*4` | bits `31:0` | TPC disable status readout per GPC. |
| `NV_FUSE_STATUS_OPT_FBP` | `0x21d38` | bits `15:0` | FBP disable status readout. |
| `NV_FUSE_SPARE_BIT_14` | `0x21e74` | bit 0 | Used by GV100 VPR/security code. |
| `NV_FUSE_SPARE_BIT_15` | `0x21e78` | bit 0 | Used by GV100 VPR/security code. |
| `NV_FUSE_SPARE_BIT_16` | `0x21e7c` | bit 0 | Used by GV100 VPR/security code. |

Raw fuse alias fields from `dev_fuse.h`:

| Fuse | Raw row alias | Data bit | Control bit |
| --- | ---: | ---: | ---: |
| `OPT_FEATURE_FUSES_OVERRIDE_DISABLE` | `0x10` | 9 | none found |
| `OPT_SM_IMLA_SPEED_SELECT` | `0x10` | 17 | 18 |
| `OPT_SM_FMLA_SPEED_SELECT` | `0x12` | 10 | 11 |
| `OPT_SHF_SPEED_SELECT` | `0x16` | 10 | 11 |
| `OPT_DEVID_SW_OVERRIDE_DIS` | `0x16` | 4 | none found |

## FECS / Context-Switch Firmware Fields

GV100 context-switch firmware has a feature readout register exposing SM speed select state:

```text
NV_CTXSW_FIRMWARE_FEATURE_READOUT_SM_SPEED_SELECT_DP    bit 20
NV_CTXSW_FIRMWARE_FEATURE_READOUT_SM_SPEED_SELECT_IMLA  bit 21
NV_CTXSW_FIRMWARE_FEATURE_READOUT_SM_SPEED_SELECT_FMLA  bit 22
```

Each has:

```text
0 = FULL_SPEED
1 = REDUCED_SPEED
```

GV100 also has an override register:

```text
NV_CTXSW_FIRMWARE_FEATURE_OVERRIDE_SM_SPEED_SELECT      0x00019900
```

With fields:

| Field | Bits |
| --- | --- |
| IMLA speed select | `0:0` |
| IMLA override enable | `3:3` |
| FMLA speed select | `4:4` |
| FMLA override enable | `7:7` |
| DP speed select | `8:8` |
| DP override enable | `11:11` |

This is important because the performance symptom is an issue-rate limiter, not a missing Tensor Core count.

## Later-Generation Naming Confirms The Mechanism

Turing and Ampere RM source exposes a control path named:

```text
NV2080_CTRL_CMD_GR_GET_SM_ISSUE_RATE_MODIFIER
grGetSmIssueRateModifier_TU102
grGetSmIssueRateModifier_GA100
```

Those implementations read `FEATURE_READOUT` values and assert enumerants for:

```text
FULL_SPEED
REDUCED_SPEED_1_2
REDUCED_SPEED_1_4
REDUCED_SPEED_1_8
REDUCED_SPEED_1_16
REDUCED_SPEED_1_32
REDUCED_SPEED_1_64
```

So the driver source does contain NVIDIA's official software concept of per-instruction-family SM issue-rate modifiers. GV100 appears to predate the richer public RM control and uses binary full/reduced naming in the available source.

## What This Means For The CMP 100-210

The exact local symptom is:

```text
Tensor Core GEMM NCU sustained SM throughput: 6.25% = 1/16
```

The source-side reconstruction now gives a plausible hardware/firmware route:

1. GV100 has fused SM speed-select bits.
2. FECS/ctxsw firmware exposes SM speed-select readout/override fields.
3. Later NVIDIA RM code explicitly calls this class of state an SM issue-rate modifier.
4. The later enumerants include `REDUCED_SPEED_1_16`.

The missing piece is the GV100-specific mapping from `REDUCED_SPEED` to the actual divisor for HMMA/Tensor instructions. The source tree does not show a clean HMMA/Tensor-specific GV100 fuse name, and the MODS chipfuse dictionary that would likely label the production row/bit is absent.

## Mock Decoder

An offline decoder was added:

```text
projects/gpu-falcon-research/tools/gv100_speed_select_decode.py
```

List known fields:

```bash
python3 projects/gpu-falcon-research/tools/gv100_speed_select_decode.py --list
```

Decode read values:

```bash
python3 projects/gpu-falcon-research/tools/gv100_speed_select_decode.py \
  nv_fuse_opt_sm_imla_speed_select=0x1 \
  nv_fuse_opt_sm_fmla_speed_select=0x0 \
  ctxsw_feature_readout=0x00600000
```

This is intentionally offline-only. It is for read-only dumps collected later from a maintenance environment or a driver API path, not for live writes.

## Next Discriminator

The best next read-only discriminator is to get these values from a local capped card and a full-speed V100:

```text
NV_FUSE_OPT_SM_IMLA_SPEED_SELECT        0x21410
NV_FUSE_OPT_SM_FMLA_SPEED_SELECT        0x214e0
NV_FUSE_OPT_DP_SPEED_SELECT             0x21224
NV_CTXSW_FIRMWARE_FEATURE_READOUT       FECS/ctxsw feature readout bits 20-22
NV_CTXSW_FIRMWARE_FEATURE_OVERRIDE_SM_SPEED_SELECT 0x00019900
```

If the capped CMP/V100-personality card has SM speed-select readout set to reduced while the full-speed V100 does not, that would tie the 1/16 Tensor result directly to the speed-select path rather than to generic SKU identity.

## Missing JSON Consumer Reconstruction

A later pass traced the missing `gv100_f.json` / `gv100_f.jsone` and `gv100_POR.json` consumers instead of searching for the absent producer files.

The key parser is:

```text
integ/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp
integ/gpu_drv/stage_rel/diag/mods/core/include/fusejsonparser.h
```

`GV100Fuse` calls `ParseXmlInt(...)`, but JSON files are handled through the generic fuse parser/cache path. The JSON consumer expects:

```text
<chip>_f.json
<chip>_f.jsone
<chip>_POR.json
<chip>_POR.jsone
```

`FindUnredactedFilename()` first looks for the restored/unredacted filename, then also accepts the same name with an added `e` suffix. `ParseJson()` reads via `Utility::ReadPossiblyEncryptedFile()`, then redacts strings before feeding RapidJSON. This means local `.jsone` support is built into MODS, but the actual GV100 encrypted/unencrypted producer files are not present in this checkout.

Required and optional JSON sections found from the parser:

| Section | Required | Consumer meaning |
| --- | --- | --- |
| `header` | yes for main `_f.json` | P4/source metadata. |
| `options.Chip_options` | yes | Defines each fuse name, bit count, visibility, type, and primary offset. |
| `options.Map_options` | yes | Maps fuse names to physical fuse rows/bits and redundant locations. |
| `options.Mkt_options` | yes | Defines SKU names and per-SKU fuse values. |
| `options.Fuse_encoding` | yes for main `_f.json` | Defines repair/fuseless ranges and config field mappings. |
| `options.IFF_patches` | optional | Defines per-SKU IFF record sequences. |
| `options.Bootrom_patches` | optional | Defines per-SKU bootrom patch revisions as address/value rows. |
| `options.Fuse_macro` | optional | Defines `num_fuse_rows`, `num_fuse_cols`, and `fuse_record_start`. |
| `options.Sku_ids` | optional | Maps SKU names to IDs. |
| `_POR.json` `Por_data` | optional external file for GV100+ | Defines per-SKU POR bin data. |

The important refinement is that the missing fuse JSON is not only a fuse dictionary. It can also carry SKU-specific bootrom patch rows:

```text
"Bootrom_patches": {
  "<SKU>": {
    "<revision-name>": {
      "sequence": <number>,
      "rows": [
        { "address": <number>, "value": "<hex>" }
      ]
    },
    "checksum": "<string>"
  }
}
```

That is now a better producer candidate for the exact local ROM delta than `Mkt_options` alone. The proven VBIOS artifact is a SKU-specific 13-byte init opcode:

```text
6e 64 96 40 00 66 f6 ff ff 99 09 00 00
```

which decodes to a write of:

```text
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT = 0x00409664
mask  = 0xfffff666
value = 0x00000999
```

A missing `Bootrom_patches` entry could plausibly be upstream data for this kind of per-SKU patch intent, but a follow-up source pass found an important caveat: the visible `BRRevs` / `BRPatches` runtime consumers in this checkout are Tegra-side MODS code, not a dGPU GV100 VBIOS builder. That means `Bootrom_patches` proves the schema can represent address/value patch rows, but it does not prove the exact dGPU producer for the observed 13-byte VBIOS delta.

The safest current wording is:

```text
Missing chipfuse/POR JSON family:
  can describe fuse, IFF, POR, and bootrom patch intent

Observed dGPU VBIOS delta:
  is a SKU-local FECS speed-select init opcode

Visible source gap:
  no dGPU GV100 consumer has been found that turns Bootrom_patches into this VBIOS opcode
```

So `IFF_patches` remains relevant for runtime fuse/IFF record state, while `Bootrom_patches` remains a schema-level candidate for byte-level patch intent. The concrete dGPU ROM producer is likely a VBIOS build-time generator or private producer artifact outside this checkout.

## Init-Script Decoder And Public Producer Search

The local `envytools` checkout confirms the byte-level interpretation of the capped delta:

```text
projects/gpu-falcon-research/tools/envytools/nvbios/print.c

case 0x6e:
  len = 13
  u32 address
  u32 and-mask
  u32 or-value
  prints MMIO_MASK <address> &= <and-mask> |= <or-value>
```

So this byte sequence:

```text
6e 64 96 40 00 66 f6 ff ff 99 09 00 00
```

is not an opaque blob. It is a standard 13-byte VBIOS init-script `MMIO_MASK` operation:

```text
address  0x00409664
mask     0xfffff666
value    0x00000999
```

That strengthens the producer model:

```text
proven:
  dGPU VBIOS init script contains a standard MMIO_MASK opcode that asserts FECS SM speed-select reduced+override

not yet found:
  the dGPU VBIOS build-time producer that chose to emit that opcode for CMP-derived images
```

A public web search for exact missing data names did not find `gv100_f.json`, `gv100_f.jsone`, `gv100_POR.json`, `gv100_POR.jsone`, or a public `FuseJsonParser` dataset carrying the GV100 entries. A related public GA100/CMP 170HX gist does independently reinforce the generation split: Ampere exposes explicit 3-bit SM speed-select fuse/readout values and reports CMP-line speed-select fuses as product-line restrictions, but it does not provide the missing GV100 producer data.

Source checked:

```text
https://gist.github.com/duggasco/a901279c3e0c9038f789f34badf8992d
```

## Local MODS Sim/Emu ROM Corpus Check

The direct filename sweep found many local GV100/GA100/etc. simulation and emulation ROM images under:

```text
/home/josh/containers/temp/nvdrv/integ/gpu_drv/stage_rel/diag/mods/sim/resources
/home/josh/containers/temp/nvdrv/dev/gpu_drv/stage_rel/diag/mods/sim/resources
```

Those ROMs were scanned with the same offline signature classifier:

```text
projects/gpu-falcon-research/tools/gv100_bootrom_patch_signature.py
```

Result:

```text
Total files scanned: 3168
Non-empty files: 3168
Unique non-empty SHA-256 values: 2367
Capped tail signatures: 0
Explicit full-speed zero-write signatures: 0
Full-speed direct-DONE signatures: 0
```

Output:

```text
projects/gpu-falcon-research/runs/20260520-evidence-dag/gv100-sim-rom-signature-scan.md
projects/gpu-falcon-research/runs/20260520-evidence-dag/gv100-sim-rom-signature-scan.json
```

Interpretation: the local MODS sim/emu ROM corpus does not contain the same production main-init tail signature family. It is useful as a negative control, but not as the missing producer for the CMP-derived production VBIOS opcode.

## Envytools Issues And Pull Requests

An envytools GitHub issue/PR pass was run against:

```text
https://github.com/envytools/envytools
```

Local checkout:

```text
origin https://github.com/envytools/envytools.git
HEAD   f102b82 Merge pull request #218 from saper/build-fixes
```

Search terms included:

```text
nvbios
init script
MMIO_MASK
0x6e
GV100
Volta
FECS
409664
Bootrom_patches
SM_SPEED_SELECT
```

The useful results were:

```text
PR #190: nvbios: Document / parse Volta & Turing devinit opcodes
https://github.com/envytools/envytools/pull/190
```

Key relevance:

```text
Post PMU taking over responsibility for interpreting devinit scripts,
these are helpful to debug VBIOSes.

However, nouveau currently has no need to interpret these directly nor
is a method currently known to modify devinit scripts in VBIOSes in a
way that allows modified versions to be loaded by the PMU.
```

Interpretation: this is directly aligned with the current boundary. Envytools can decode the production init-script operation, and PR #190 confirms Volta/Turing devinit is PMU-interpreted. The same PR also cautions that a working modified-load path was not known to the envytools/nouveau developers at that time.

Other relevant-but-secondary results:

```text
PR #181: nvbios: Add Pascal family devinit opcodes
https://github.com/envytools/envytools/pull/181

PR #127: Initial Volta family support
https://github.com/envytools/envytools/pull/127

PR #129: nvbios: Add HBM2 memory type, present on GV100 GPUs
https://github.com/envytools/envytools/pull/129

PR #75: nvbios: Properly handle unknown generic conditions
https://github.com/envytools/envytools/pull/75

Issue #200: Add Falcon security mode to nvbios
https://github.com/envytools/envytools/issues/200
```

Useful takeaways:

```text
PR #181:
  Confirms later devinit opcodes can carry MMIO/fuse address plus mask/value style operands.

PR #127 / #129:
  Establish that early envytools Volta/GV100 support was tested against real GV100 VBIOSes, but they do not discuss FECS speed-select policy.

PR #75:
  Warns that unknown generic conditions can affect script interpretation. This does not change the local limiter tail because the observed opcode is immediately before DONE in the selected main init script, but it is relevant for broader VBIOS script diffing.

Issue #200:
  Merely asks for Falcon security mode support in nvbios; no implementation detail or useful path is present.
```

Negative result:

```text
No envytools issue or PR found in this pass names:
  0x409664
  NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
  SM_SPEED_SELECT
  Bootrom_patches
  gv100_f.json / gv100_POR.json
```

Conclusion: the envytools tracker helps most by explaining why the decoded VBIOS init-script opcode is a real PMU/devinit artifact and why editing/loading modified Volta devinit is a separate, hard problem. It does not expose the missing GV100 chipfuse/POR producer or a known clean modified-load path.

## RM Control Readout Attempt - 2026-05-20

A read-only RM ioctl probe was added:

```text
projects/gpu-falcon-research/tools/rm_sm_issue_rate_probe.c
```

It uses the normal NVIDIA control device path:

```text
/dev/nvidiactl
NV_ESC_CARD_INFO
NV_ESC_RM_ALLOC
NV_ESC_RM_CONTROL
```

The probe successfully:

1. Enumerated the local GPU minor numbers and RM GPU IDs.
2. Allocated an RM root client.
3. Queried attached GPU IDs.
4. Allocated `NV01_DEVICE_0` for the target GPU.
5. Allocated `NV20_SUBDEVICE_0` for the target GPU.

Then it called:

```text
NV2080_CTRL_CMD_GR_GET_SM_ISSUE_RATE_MODIFIER = 0x20801230
```

Result across local V100-personality GPUs `4`, `7`, `9`, `11`, and `14`:

```text
get_sm_issue_rate_modifier status=0x00000056
```

On this driver, `0x56` decodes to:

```text
NV_ERR_NOT_SUPPORTED
```

This means the later-generation RM issue-rate control exists in the headers and matches the 1/16 issue-rate concept, but it is not an exposed GV100 readout path in the currently loaded driver.

Artifacts:

```text
runs/20260520-rm-speed-select/
```

## Debugdump Readout Attempt - 2026-05-20

Unprivileged `nvidia-debugdump --dumpall --device 14` failed with insufficient permissions. Running the same command with local sudo produced a valid diagnostic zip:

```text
runs/20260520-rm-speed-select/gpu14-debugdump-root.zip
```

The archive contains:

```text
system_info.pb
error_data.pb
nvlog.log
debug_buffers_14.pb
rm_14.pb
nvlog.gpu014.log
```

String scans did not expose readable `FUSE`, `FECS`, `HMMA`, `TENSOR`, `ISSUE`, or speed-select fields. The useful data appears to be protobuf-encoded, and the local `nvidia-debugdump` binary did not emit a decoded text form from the saved archive.

So the current state is:

```text
RM handle path: works
Named issue-rate RM control: GV100 returns NV_ERR_NOT_SUPPORTED
Debugdump path: captures protobufs, but no decoded speed-select/fuse field yet
Live PFUSE/MMIO path: still blocked by the loaded driver unless run in a maintenance environment
```

## Debugdump Decryption And Targeted Decode - 2026-05-20

The local `nvidia-debugdump` is an external build. Source inspection showed external builds always encrypt the inner dump files with `THE_CRYPT_KEY` in:

```text
temp/nvdrv/integ/gpu_drv/stage_rel/apps/nvDebugDump/nvdebugdump.c
```

A small extractor was added that links NVIDIA's own `nvdzip` and `nvdcrypt` code with that built-in key:

```text
projects/gpu-falcon-research/tools/nvdebugdump_decrypt_extract.c
```

After decrypting `gpu14-debugdump-root.zip`, the inner files are valid protobuf/log payloads:

```text
system_info.pb       282762 bytes
error_data.pb          5000 bytes
nvlog.log            527548 bytes
debug_buffers_14.pb       2 bytes
rm_14.pb              29185 bytes
nvlog.gpu014.log     527548 bytes
```

Artifacts:

```text
runs/20260520-rm-speed-select/decrypted/
```

Targeted protobuf walking of `rm_14.pb` decoded `836` `Regs.RegsAndMem` register blocks.

Important negative result:

```text
No decoded register block contains the known GV100 speed-select fuse offsets:
NV_FUSE_OPT_SM_IMLA_SPEED_SELECT 0x21410
NV_FUSE_OPT_SM_FMLA_SPEED_SELECT 0x214e0
NV_FUSE_OPT_DP_SPEED_SELECT      0x21224
NV_FUSE_OPT_SHF_SPEED_SELECT     0x2159c
```

The dump does include nearby or related register ranges, for example:

```text
0x21c00
0x22700
0x400100 ...
0x409800 ...
0x410134 stride 0x400
```

So debugdump is not a raw all-register/PFUSE dump. It captures selected RM diagnostic register groups, and the specific speed-select fuse words are not among them.

## Debugdump InfoROM Finding

The decrypted `rm_14.pb` contains `NvDebug.Eng.Inforom` data. Its filesystem file list is:

```text
ROM
IMG
OBD
OEM
BBO
```

The VBIOS engine reports V100-personality values:

```text
bios_rev:      0x88005100
bios_oem_rev:  0x4
chip_sku:      893
project:       G500-0111
vbios_status:  0
```

But the InfoROM/OBD payload still contains printable CMP strings:

```text
OBD ... " CMP "
```

Interpretation:

```text
Normal PCI/NVML/CUDA identity is in the V100-personality state, but the board's InfoROM still carries CMP-era object data. That is a real visible residue in the decrypted debugdump.
```

This does not prove InfoROM causes the Tensor Core cap. It does give us a concrete remaining identity/policy artifact to compare against a real full-speed V100 debugdump.

## RM GR Register Access Attempt

The probe was extended to try a read-only GR register access path:

```text
NV2080_CTRL_CMD_GR_REG_ACCESS          = 0x20801226
NV2080_CTRL_CMD_INTERNAL_GR_REG_ACCESS = 0x20800a15
flags = READ | LEGACY
```

It tested a known debugdump PGRAPH address plus candidate FECS speed-select addresses:

```text
0x00409800  known debugdump PGRAPH block
0x00409900  possible FECS speed-select override
0x00409904  possible FECS speed-select override_1
0x00019900  relative candidate
0x00019904  relative candidate
```

Result on GPUs `14` and `4`:

```text
status=0x00000056
```

That is again `NV_ERR_NOT_SUPPORTED`, even for the known PGRAPH address. The loaded public driver does not expose this MODS-only register-read path.

Artifacts:

```text
runs/20260520-rm-speed-select/gpu14-rm-sm-issue-rate-regread.txt
runs/20260520-rm-speed-select/gpu4-rm-sm-issue-rate-regread.txt
```

## Updated Assessment

The nvdrv/debugdump path did contain useful information, but not the exact hidden speed-select fuse value.

What it established:

1. External debugdump encryption is reversible using the key in the source tree.
2. The decrypted RM protobuf can be parsed without NVIDIA's internal decoder.
3. Debugdump selected register groups do not include the known GV100 speed-select fuse offsets.
4. InfoROM still contains CMP/OBD strings despite V100-personality PCI/NVML/CUDA identity.
5. Public-driver RM controls for issue-rate and GR register read return `NV_ERR_NOT_SUPPORTED` on GV100.

Best next discriminator:

```text
Capture the same decrypted debugdump from a known full-speed V100 and compare Eng.Inforom/VBios/Gpu/Perf fields against GPU14.
```

If the full-speed V100 lacks the CMP OBD residue while all other visible identity fields match, InfoROM/persistent board object data becomes a stronger suspect. If it matches too, the remaining lead is hidden fuse/firmware policy outside debugdump's selected register groups.
