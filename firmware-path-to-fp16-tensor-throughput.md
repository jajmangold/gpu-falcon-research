# Firmware Path To FP16 Tensor Throughput

Research date: 2026-05-19

This note maps the likely below-CUDA path for the CMP 100-210 / V100-personality GV100 tensor-throughput gate.

## Current Evidence

Observed on local hardware:

- CMP identity cards expose 68 CUDA-visible SMs.
- V100-personality cards expose 80 CUDA-visible SMs.
- V100 personality changes PCI device/subdevice identity and VBIOS:
  - CMP: `10de:1d84`, subdevice `10de:12b9`, VBIOS `88.00.9D.00.00`.
  - V100 personality: `10de:1df4`, subdevice `10de:12b8`, VBIOS `88.00.51.00.04`.
- Both cards accept and run `sm_70` HMMA instructions.
- V100-personality cards permit Nsight Compute tensor-pipe counters; CMP cards are blocked by `ERR_NVCMPGPU`.
- V100-personality cuBLAS kernels issue tensor-pipe instructions, but sustained FP16 tensor GEMM stays around FP32-class throughput.
- Power, clocks, and utilization do not look like the immediate limiter during sustained runs.

Conclusion: the V100 personality unlocks a real identity/SM/profiler gate, but not the full FP16 tensor-throughput gate.

## Relevant GV100 Firmware Chain

Public Nouveau source and local firmware layout show this shape:

1. **VBIOS / straps / devinit**
   - Board identity, device/subdevice IDs, straps, and early register initialization come from physical straps and VBIOS/devinit data.
   - This layer clearly affects visible SM count on these cards, because V100-personality cards expose 80 SMs while CMP identity cards expose 68.

2. **ACR**
   - GV100 Nouveau ACR code loads `nvidia/gv100/acr/bl.bin` and `nvidia/gv100/acr/ucode_load.bin`.
   - Nouveau’s `gv100_acr_load_fwif` boots ACR through `NVKM_ACR_HSF_SEC2`, meaning SEC2 is the high-secure Falcon used for ACR load on GV100.

3. **WPR / managed Falcon boot**
   - Nouveau’s ACR structures include WPR memory, low-secure Falcon firmware descriptors, signatures, fuse version, engine ID, ucode ID, and signature metadata.
   - Managed low-secure Falcon IDs include FECS, GPCCS, NVDEC, SEC2, PMU, etc.

4. **GR FECS / GPCCS**
   - Local GV100 firmware includes:
     - `gr/fecs_bl.bin`
     - `gr/fecs_inst.bin`
     - `gr/fecs_data.bin`
     - `gr/fecs_sig.bin`
     - `gr/gpccs_inst.bin`
     - `gr/gpccs_data.bin`
     - `gr/gpccs_sig.bin`
   - This is the most likely signed firmware layer for graphics/compute context setup and GPC/TPC/SM exposure policy.

5. **SEC2**
   - Local GV100 firmware includes `sec2/desc.bin`, `sec2/image.bin`, and `sec2/sig.bin`.
   - `sec2/desc.bin` is parseable metadata; `image.bin` is Falcon code; `sig.bin` contains signature material and metadata.

## Local Firmware Blob Facts

Decompressed local GV100 firmware sizes:

| Blob | Decompressed size |
| --- | ---: |
| `acr/bl.bin.zst` | 1,280 |
| `acr/ucode_load.bin.zst` | 18,688 |
| `acr/ucode_unload.bin.zst` | 6,400 |
| `acr/unload_bl.bin.zst` | 1,280 |
| `sec2/desc.bin.zst` | 656 |
| `sec2/image.bin.zst` | 91,136 |
| `sec2/sig.bin.zst` | 192 |
| `gr/fecs_bl.bin.zst` | 576 |
| `gr/fecs_data.bin.zst` | 4,788 |
| `gr/fecs_inst.bin.zst` | 25,632 |
| `gr/fecs_sig.bin.zst` | 192 |
| `gr/gpccs_bl.bin.zst` | 576 |
| `gr/gpccs_data.bin.zst` | 2,128 |
| `gr/gpccs_inst.bin.zst` | 12,643 |
| `gr/gpccs_sig.bin.zst` | 192 |

`sec2/desc.bin` includes a visible build timestamp:

```text
Tue Feb 13 11:42:34 2018
```

The signature blobs are small and separate from instruction/data blobs, consistent with signed firmware packaging.

## Most Likely Gates

### Gate A: VBIOS / PCI Identity / Strap Gate

Evidence:

- Changing to V100 personality changes visible SM count from 68 to 80.
- It changes PCI device/subdevice IDs and VBIOS version.
- It removes the CMP-specific Nsight Compute block.

What this gate controls:

- Product identity.
- Driver capability tables.
- SM exposure.
- Tooling/profiler policy.

What it does **not** control, based on current evidence:

- Full-speed FP16 tensor throughput.

### Gate B: Signed GR Firmware / Context Setup Gate

Evidence:

- FECS/GPCCS are signed and loaded through the secure ACR/WPR path.
- They are responsible for graphics/compute context control.
- Tensor instructions issue, but effective tensor throughput is capped.

This is the strongest current candidate for a throughput policy that is below CUDA but above literal physical fuses.

The practical problem: modifying FECS/GPCCS code or signed metadata should fail unless the ACR/SEC2 chain accepts the modified firmware. The local files include separate signatures, and Nouveau’s ACR path explicitly models signature metadata, fuse version, ucode ID, and engine ID.

### Gate C: SEC2 / ACR Secure Loader Gate

Evidence:

- GV100 ACR load path is through SEC2 high-secure firmware.
- SEC2 has `desc/image/sig`.
- ACR builds/checks WPR and bootstraps managed Falcons.

If FECS/GPCCS is the throughput policy layer, SEC2/ACR is the enforcement layer that makes patching it nontrivial.

### Gate D: Hardware Fuse / Harvest Gate

Evidence:

- CMP cards expose 68 SMs consistently.
- V100-personality cards expose 80 SMs, so at least some chips/cards have physically available 80-SM configuration.
- Full tensor throughput remains absent even on 80-SM V100-personality cards.

If tensor throughput is fuse-limited independently of SM visibility, firmware cannot restore it. The only way to distinguish this from Gate B/C is deeper counter/register evidence or a known full-speed control card.

## Path Forward

### Step 1: Preserve ROM/Personality Images

Goal: get exact VBIOS/personality images from:

- one CMP 68-SM card;
- one V100-personality 80-SM card;
- any known-good full-speed V100, if available.

Current state:

- CMP card GPU 2 has a valid local ROM dump.
- The local V100-personality card dump path hung through `nvagetbios`, so further live probing should wait for driver recovery or reboot.
- The supplied TechPowerUp `266855.rom` has been hash-verified, extracted from its `NVGI` container, and parsed as a V100-personality comparator matching `88.00.51.00.04` / `10de:1df4` / `10de:12b8`.

Next practical route:

- continue offline comparison between `gpu2-cmp.rom` and `266855-extracted-pcirom.rom`;
- defer additional live ROM reads until the NVIDIA driver state is healthy;
- if live reads resume, save ROMs only, hash and archive dumps, and parse with the same commands.

### Step 2: Decode VBIOS Tables And Straps

Compare:

- PCI device/subdevice identity tables;
- strap override data;
- devinit scripts;
- clock/power tables;
- feature/board tables;
- any table fields referencing GPC/TPC/SM masks.

Expected outcome:

- explain the 68-SM vs 80-SM delta;
- identify whether tensor policy appears in VBIOS-visible tables or not.

If tensor policy is absent from VBIOS tables, it strengthens the FECS/GPCCS/SEC2 path hypothesis.

The current focused diff gives two priority surfaces:

```text
M_RESTRICT / M_TYPE / M_UNK0D have very small byte deltas and likely encode board/personality policy.
```

The prior `FALCON_UCODE at 0x0e4f4` lead was reassessed. That pointer lands inside the EFI option-ROM payload beginning at `0xe400`, so the apparent `a4`/`a8` version bytes are not reliable Falcon table versions.

The `P` table regions are large and mostly different, but those changes may include clock, power, and board-tuning data rather than a specific tensor-throughput gate.

### Step 3: Parse Signed Firmware Descriptors

Build a local parser for:

- `sec2/desc.bin`;
- `sec2/sig.bin`;
- `gr/fecs_sig.bin`;
- `gr/gpccs_sig.bin`;
- ACR ucode headers.

Goal:

- identify version, fuse version, ucode ID, engine ID, signature count, offsets, and any chip/personality binding metadata.

This does not bypass signatures. It tells us whether signatures are generic GV100 blobs or bound to device/fuse/personality inputs.

### Step 4: Identify Parameter Injection Points

The signed firmware may not hard-code the throughput policy. It may read policy from:

- straps;
- VBIOS tables;
- driver-provided bootloader data;
- WPR descriptors;
- fuse registers;
- GR initialization bundles such as `sw_bundle_init`, `sw_ctx`, `sw_method_init`, `sw_nonctx`.

This is the most promising non-signature path: change or spoof unsigned inputs to signed firmware rather than modifying signed firmware itself.

Candidate input surfaces:

- VBIOS/devinit table values.
- Strap override registers.
- Driver RM policy keyed by PCI/subdevice ID.
- GR software bundle/context init blobs if they are not covered by the same secure signature path.

### Step 5: Prove Or Reject Runtime Patchability

Use a sacrificial idle card only.

Evidence to collect before attempting any write:

- BAR0 register snapshots around PSTRAPS and GR setup.
- Driver initialization traces if feasible.
- Nouveau source register names for PSTRAPS/GR/Falcon boot.
- Reboot/reset recovery plan.

Expected result:

- If strap-like inputs can be changed before driver initialization and affect SM/profiler identity, VBIOS/strap spoofing is viable.
- If tensor throughput remains capped after identity/strap spoofing, the remaining gate is signed firmware or hardware fuse.

## Practical Answer

To unlock full-speed FP16 tensor throughput, one of these must be true:

1. There is an unsigned VBIOS/strap/devinit parameter that signed GR firmware uses to configure tensor throughput.
2. The proprietary driver has a policy table keyed by device/subdevice/VBIOS that can be coerced without firmware patching.
3. FECS/GPCCS/SEC2 signed firmware can be made to accept modified policy or code.
4. The hardware was never physically/fuse-disabled.

Current evidence already disproves the simplest version of #2: V100 identity gets 80 SMs and profiler counters, but not tensor throughput.

So the highest-value path is:

```text
ROM dump -> VBIOS/strap/devinit diff -> signed firmware descriptor parse -> identify unsigned inputs to ACR/FECS/GPCCS -> test input spoofing on sacrificial card
```

If no unsigned input controls tensor throughput, then the gate is inside signed FECS/GPCCS/SEC2 policy or hardware fuses. At that point, a full unlock requires defeating the secure firmware acceptance chain or changing physical/fuse state, not just flashing a different board personality.

## Sources

- Nouveau GV100 ACR source: https://codebrowser.dev/linux/linux/drivers/gpu/drm/nouveau/nvkm/subdev/acr/gv100.c.html
- Nouveau ACR structures and Falcon IDs: https://codebrowser.dev/linux/linux/drivers/gpu/drm/nouveau/include/nvkm/subdev/acr.h.html
- Linux NOVA devinit overview: https://docs.kernel.org/gpu/nova/core/devinit.html
- envytools PSTRAPS documentation: https://envytools.readthedocs.io/en/latest/hw/io/pstraps.html
- envytools Falcon overview: https://envytools.readthedocs.io/en/latest/hw/falcon/intro.html
