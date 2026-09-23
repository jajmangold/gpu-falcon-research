# ROM Dump And VBIOS Diff Status - 2026-05-19

## What Was Done

- Installed build dependencies for open-source envytools.
- Cloned and built envytools under `projects/gpu-falcon-research/tools/envytools`.
- Used `nvalist` with sudo to confirm card numbering:
  - card 2: `0000:07:00.0` CMP baseline.
  - card 14: `0000:19:00.0` V100-personality comparator.
- Dumped the CMP card VBIOS successfully with `nvagetbios`.
- Parsed the CMP VBIOS with `nvbios`.

## Tools Built

Important binaries:

```text
projects/gpu-falcon-research/tools/envytools/build/nva/nvalist
projects/gpu-falcon-research/tools/envytools/build/nva/nvagetbios
projects/gpu-falcon-research/tools/envytools/build/nvbios/nvbios
```

## CMP ROM Dump

Dump:

```text
runs/20260519-173454-tensor-probe-gpu2-gpu14/rom-dumps/gpu2-cmp.rom
```

SHA256:

```text
2a6d1e608073f7f33691e0466524289c334b248314d9ba713aa6935f133a6c3b
```

`nvagetbios` log:

```text
No extraction method specified (using -s extraction_method). Autodetecting.
Attempt to extract the vbios from card 2 (nv140) using PRAMIN.
Invalid signature(0x55aa). You may want to try another retrieval method.
Attempt to extract the vbios from card 2 (nv140) using PROM.
Card has second bios
Vbios extracted successfully!
```

Basic identity from `nvbios`:

```text
PCI device: 0x10de:0x1d84, class 0x030200
INFO: BIOS version 0x88.00.9d.00.00 from 12/26/19 for NV140 [GV100]
```

Strings include:

```text
GV100 PG500 SKU 110 VGA VBIOS
Version 88.00.9D.00.00
GV100 Board
1100G5000110
```

## CMP Tables Of Interest

Top-level BIT table starts at `0x01b0`.

Important decoded pointers:

```text
BIT table 'P' at 0x1e6, version 2
0x00: 0x21055 => PERFORMANCE TABLE
0x80: 0x21b4a => LOW POWER GR TABLE
0x9c: 0x21bf8 => UNKNOWN TABLE

BIT table 'M' at 0x1da, version 2
0x01: 0x4d09 => RESTRICT TABLE
0x03: 0x4dbf => TYPE TABLE
0x0d: 0x527c => UNK0D TABLE
0x11: 0xd1c0 => UNKNOWN TABLE
0x15: 0xd1cc => UNKNOWN TABLE

BIT table 'p' at 0x210, version 2
0x00: 0xe4f4 => FALCON UCODE TABLE
```

`nvbios` cannot parse the GV100 Falcon table:

```text
Unknown FALCON UCODE table version 0xa4
Failed to parse FALCON UCODE table at 0xe4f4, version a4
```

The offsets around `0xe4f4`, `0x21055`, and `0x21bf8` look high-entropy/opaque rather than ordinary simple tables. That may indicate compressed, signed, encrypted, or unsupported table formats in this old envytools parser.

## V100-Personality ROM Attempt

Target:

```text
card 14 / 0000:19:00.0 / Tesla V100-PCIE-12GB personality
```

Default extraction stuck after:

```text
No extraction method specified (using -s extraction_method). Autodetecting.
Attempt to extract the vbios from card 14 (nv140) using PRAMIN.
```

Explicit PROM extraction also hung and produced no ROM output. The process was eventually killed, but this left one blocked NVIDIA procfs read process from a later information query:

```text
cat /proc/driver/nvidia/gpus/0000:05:00.0/information
```

That process is in uninterruptible sleep (`D`) and will likely clear only when the NVIDIA driver path recovers or the host reboots. No further GPU MMIO/procfs probing should be attempted in this session until the driver state is healthy.

## Current Status

We have:

- a valid CMP ROM dump;
- a verified local copy of TechPowerUp `266855.rom`;
- an extracted raw PCI ROM from that `NVGI` container;
- parsed top-level CMP VBIOS table map;
- parsed top-level V100-personality VBIOS table map;
- a focused CMP-vs-V100-personality table diff;
- CMP Falcon table pointer;
- buildable open-source tooling for future dumps/parsing.

We do not yet have:

- a V100-personality ROM dump;
- a dump made directly from the local V100-personality card;
- a known-good full-speed V100 control ROM/result pair;
- a parsed GV100 Falcon ucode table format.

## Verified Online Comparator

The supplied local file:

```text
/home/josh/containers/temp/266855.rom
```

matches TechPowerUp metadata:

```text
MD5  0696ad4cec325f8b235ee25abef98765
SHA1 03b4d838ea6cac4fe430bb756fdea8ed763ada32
```

It is an NVIDIA `NVGI` container, not a raw PCI ROM. The raw PCI ROM starts at offset `0x0a00` and is archived as:

```text
runs/20260519-173454-tensor-probe-gpu2-gpu14/online-vbios/266855-extracted-pcirom.rom
```

The extracted ROM parses as:

```text
PCI device: 0x10de:0x1df4
BIOS version: 88.00.51.00.04 from 03/27/18
GV100 PG500 SKU 111 VGA VBIOS
INFO unk30: 8931G5000111
```

Its BIT `p[0x00]` pointer is the same offset as the CMP image:

```text
BIT p[0x00] at 0x0e4f4
CMP byte at pointer:  a4
V100 byte at pointer: a8
```

Reassessment: `0xe4f4` is inside the EFI option-ROM part that begins at `0xe400`, not in a clearly decoded legacy Falcon policy table. The high entropy around that offset is consistent with compressed EFI payload. Treat envytools' `FALCON UCODE` label as a stale parser mapping on these GV100 images.

The small decoded `M` table deltas are now the cleanest VBIOS-visible candidates for board/personality policy:

```text
M_RESTRICT: 3 byte diffs in first 256 bytes
M_TYPE:     1 byte diff in first 256 bytes
M_UNK0D:    1 byte diff in first 256 bytes
```

## Implication For The Unlock Path

The CMP VBIOS has explicit table surfaces that may carry policy:

- `RESTRICT TABLE`
- `TYPE TABLE`
- `PERFORMANCE TABLE`
- `LOW POWER GR TABLE`
- BIT `p[0x00]`, previously labeled `FALCON UCODE TABLE` by envytools

The BIT `p[0x00]` pointer is now likely a false lead because it resolves into the EFI payload. The immediate next analysis task is to focus on the small decoded `M` table deltas and to obtain a known full-speed V100 ROM/result pair as a third comparator before any additional live-card access.

## Safe Next Step

Do not continue MMIO/procfs probing in the current driver state.

After reboot or driver recovery:

1. Try to dump a V100-personality card with a different tool path, preferably `nvflash --save`, if a trusted copy can be staged.
2. If using envytools again, avoid card 14 first; try a less critical V100-personality card and set a hard external watchdog.
3. Parse both ROMs with the same `nvbios` command set.
4. Diff the raw ROMs and parsed table offsets, especially `M`, `P`, and `p` BIT entries.
