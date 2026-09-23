# GV100 Flasher Tool Survey - 2026-05-20

This note records a high-level, read-only survey of `OMGVflash` and `nvflashk` for the CMP 100-210 / GV100 FECS speed-select investigation.

It is not a flashing procedure.

## Local Workspace

Direct local search found NVIDIA `nvflash` binaries in the leaked/local driver tree, but no local `omgvflash` or `nvflashk` checkout:

```text
fdfind -HI 'omgvflash|nvflashk|nvflash' /srv/nvme-data/containers /home/josh
```

The local hits are mostly Tegra `nvflash` binaries, Tegra manufacturing tooling, and old bundled NVIDIA binaries. They do not expose the public `omgvflash` / `nvflashk` logic or the missing GV100 VBIOS producer.

## nvflashk

Public repo:

```text
https://github.com/notfromstatefarm/nvflashk
```

Relevant README claims:

```text
nvflashk is a patched version of NVIDIA nvflash.
It is intended to bypass nvflash-side GPU/board/subsystem ID checks.
The repo describes flashing nearly any signed BIOS to a GPU.
Unsigned/modified BIOSes are listed as not yet tested in the README.
```

This distinction matters for the GV100 limiter:

```text
Our local artifact:
  a modified production VBIOS/devinit byte sequence would be needed to remove the
  FECS speed-select MMIO_MASK opcode from the capped image.

nvflashk's visible scope:
  mostly signed-ROM crossflash metadata mismatch bypass.
```

So `nvflashk` may be relevant for signed VBIOS crossflash experiments in general, but it does not directly solve the harder problem: loading a modified GV100 devinit script that PMU/Falcon accepts.

## nvflashk Issues

GitHub issue search terms:

```text
GV100
Volta
Tesla
Quadro
modified BIOS
unsigned
signature
certificate
invalid firmware
board id
```

Results:

```text
GV100: 0
Volta: 0
unsigned: 0
signature: 0
certificate: 0
```

Useful issue:

```text
https://github.com/notfromstatefarm/nvflashk/issues/26
```

That issue is a Quadro M6000 -> Tesla M40 attempt. The log shows mismatch bypass prompts, then the update still ends with:

```text
ERROR: Invalid firmware image detected.
Nvflash CPU side error Code:2
Error Message: Falcon In HALT or STOP state, abort uCode command issuing process.
```

Interpretation: even when nvflash-side ID mismatch checks are bypassed, the card/firmware path can still reject the image. This is consistent with our GV100 boundary: software-side mismatch bypass is not the same thing as a guaranteed PMU/Falcon-accepted modified devinit image.

Useful issue:

```text
https://github.com/notfromstatefarm/nvflashk/issues/29
```

This is positive feedback for a P102-100 / GP102 mining card crossflash. It shows `nvflashk` can help on some Pascal mining-card crossflash cases, but it is not evidence for GV100, Volta, FECS speed-select, or modified devinit acceptance.

## OMGVflash

I did not find a public GitHub source repository for `OMGVflash` in this pass. Public coverage points to TechPowerUp forum distribution and secondary summaries.

Useful public summary:

```text
https://www.tomshardware.com/news/new-nvidia-bios-modding-tools
```

The public summaries say:

```text
OMGVflash is associated with bypassing NVIDIA vBIOS signature/vendor checks.
Coverage describes support around Turing and older cards.
```

The same public coverage distinguishes `nvflashk` as a patched `nvflash` for crossflashing and notes uncertainty around uncertified/modified BIOSes.

## CMP 100-210 Public Thread

Directly relevant thread:

```text
https://forums.developer.nvidia.com/t/how-to-test-if-tensor-cores-are-working-cmp-100-210/288865
```

The poster is investigating a CMP 100-210 / V100-style conversion. Their summary includes:

```text
Hacked flashers not working (omgvflash and nvflashk)
Still there is the Falcon protection in the GPU...
```

Interpretation: this is anecdotal, but it is the most directly relevant public report found for this exact card family. It points away from `omgvflash` / `nvflashk` as a clean route for CMP 100-210.

## Current Interpretation

The flasher survey changes the evidence map like this:

```text
Proven local cause:
  capped/CMP-derived VBIOS init script contains a standard 13-byte MMIO_MASK
  opcode that sets FECS SM speed-select reduced+override at 0x00409664.

Useful flasher distinction:
  nvflashk targets nvflash-side identity/mismatch checks for mostly signed ROMs.
  OMGVflash is reported to bypass signature/vendor checks on Turing-and-older
  style cards, but no public source or GV100-specific successful case was found.

Public CMP 100-210 datapoint:
  one CMP 100-210 investigator says omgvflash and nvflashk did not work.

Remaining blocker:
  the hard part is still producing or loading a PMU/Falcon-accepted GV100 VBIOS
  image with the devinit speed-select opcode absent or different.
```

So these tools are useful context, but they do not currently remove the main block. They may help with signed crossflash cases, but our evidence points to a modified-devinit / production-builder / missing-chipfuse-data problem rather than a simple board-ID mismatch problem.

## How The Previous Owner Could Have Made V100-Personality Cards

The local dumps explain the apparent contradiction:

```text
Card 4 V100-personality:
  PCI device: 0x10de:0x1df4
  BIOS version: 88.00.51.00.04
  String: GV100 PG500 SKU 111 VGA VBIOS
  String: 8931G5000111

But the same ROM also contains:
  NV_REG R[0x409664] &= 0xfffff666 |= 0x00000999
```

So the previous owner did not necessarily flash a stock, full-speed V100 ROM. They appear to have flashed a V100-identity / V100-personality image that is still in the capped/CMP-derived init-script family.

The bootrom signature scanner classifies the successful local V100-personality dump as:

```text
card4-v100-0b.rom -> capped_or_cmp_derived
anchor            -> 0x008222 set_reduced_999
```

The most likely routes are:

```text
1. External SPI/EEPROM programmer.
   This bypasses nvflash/Falcon update acceptance because the EEPROM is written
   directly. The public CMP 100-210 thread explicitly mentions external
   CH341A-style reflash as a known route while hacked flashers did not work.

2. A signed V100-personality / service image.
   The image may have been valid enough for the card's boot path while still
   retaining the CMP-derived FECS speed-select limiter opcode.

3. Board strap / identity changes plus a compatible image.
   The board can present V100-like PCI/NVML/CUDA identity and 80 SMs while
   still carrying lower-level CMP/board-policy residue.

4. Factory/recycler tooling.
   A prior owner or refurbisher may have had access to service tooling or
   prebuilt ROMs not present in the public toolchain.
```

This also explains why `nvflashk` / `OMGVflash` do not have to be the answer. A direct EEPROM write can create a V100-personality card even if software flashers cannot produce a PMU/Falcon-accepted modified full-speed image.

Current interpretation:

```text
They probably flashed identity/personality, not the limiter state.

The V100-personality image changed:
  device ID / product name / VBIOS version / visible SM count / profiler access

It did not change:
  the selected main-init tail that asserts FECS SM speed-select reduced+override
```

## Public Miner / Reseller Trail

A public search for how miners and resellers handle CMP 100-210 cards found that the `V100 VBIOS` variant is common enough to be sold and discussed as a product class.

### Marketplace Evidence

ReSpec.IO sells a listing titled:

```text
Nvidia CMP 100-210 16GB Mining GPU (V100 VBIOS)
```

Source:

```text
https://respec.io/product/nvidia-cmp-100-210-16gb-mining-gpu-v100-vbios-1yr-warranty-fast-ship/
```

The important claims are:

```text
Units are sorted between two VBIOS revisions.
The V100 VBIOS units are guaranteed to have that VBIOS.
Users report slightly higher performance than the CMP BIOS revision.
Both revisions have the same underlying chipset.
The card is still described as firmware locked and physically modified for constrained PCIe bandwidth.
```

Interpretation: resellers treat `V100 VBIOS` as an identity/performance variant, not as a confirmed full V100 unlock.

### Mining OS / Rig Evidence

HiveOS forum users also report two CMP 100-210 variants:

```text
Tesla V100-PCIE-12GB  (10DE 1DF4)
CMP 100-210           (10DE 1D84)
```

Source:

```text
https://hiveon.com/forum/t/nvidia-gpu-naming-in-hiveos/87631
```

Interpretation: mixed mining rigs encounter both identities in the wild. That matches the local card population:

```text
CMP identity:
  10de:1d84
  VBIOS 88.00.9D.00.00

V100 personality:
  10de:1df4
  VBIOS 88.00.51.00.04
```

### User Modder Evidence

The NVIDIA Developer Forum CMP 100-210 thread is the most directly relevant public modder report:

```text
https://forums.developer.nvidia.com/t/how-to-test-if-tensor-cores-are-working-cmp-100-210/288865
```

The user reports:

```text
Board straps can be set toward V100 configuration.
The BIOS can be reflashed with an external CH341A-style flasher.
Hacked flashers such as omgvflash and nvflashk did not work.
Falcon protection remains a concern.
```

The same thread later says nearby SMD strap components control board ID, while a GPU ID / deeper identity issue remains.

Interpretation: this is exactly the pattern that explains the local cards. Miners/refurbishers can create or buy a V100-personality ROM state using direct EEPROM methods and/or strap identity changes, but that does not imply the FECS speed-select limiter was removed.

### Public Device-ID Evidence

Public PCI ID databases list both `1D84` and `1DF4` as GV100/CMP 100-210-family identities rather than treating `1DF4` as a clean proof of a normal retail Tesla V100:

```text
https://devicehunt.com/view/type/pci/vendor/10DE/device/1DF4
https://linux-hardware.org/?id=pci:10de-1df4-10de-1365
```

Interpretation: a card presenting `10de:1df4` can still be in the CMP 100-210 family. This matches the local evidence that `1DF4` / V100-personality is not sufficient to prove full-speed V100 policy.

## What Miners Probably Did

The common route appears to be:

```text
1. Acquire CMP 100-210 cards from mining channels.
2. Sort or rewrite VBIOS revision / identity so some cards present as Tesla V100-PCIE.
3. Optionally alter board straps / missing SMD strap population for identity or PCIe behavior experiments.
4. Use the cards for mining/LLM workloads where PCIe bandwidth and Tensor/FP64 issue limits are less visible than memory capacity/bandwidth.
```

For the local cards, the most likely explanation remains:

```text
previous owner wrote or bought a V100-personality image
  -> card reports Tesla V100-PCIE-12GB / 10de:1df4 / 80 SM
  -> but ROM still contains capped FECS speed-select init opcode
  -> so Tensor/FP64 limiter remains
```

This aligns with the marketplace wording: `V100 VBIOS` is a saleable variant, but not necessarily a full V100 functional conversion.
