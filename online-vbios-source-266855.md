# Online VBIOS Source - TechPowerUp 266855

Research date: 2026-05-19

## Exact Match Found

TechPowerUp has an exact metadata match for the V100-personality ROM we need:

```text
URL: https://www.techpowerup.com/vgabios/266855/266855
Filename: 266855.rom
VBIOS Version: 88.00.51.00.04
GPU Device Id: 0x10DE 0x1DF4
Subsystem Id: 10DE 12B8
BIOS internals: GV100 PG500 SKU 111 VGA VBIOS
Reported model labels: NVIDIA CMP 100-210 / NVIDIA Tesla V100-PCIE-16GB
Memory: 16384 MB HBM2
Core/Mem/Boost: 1147 / 808 / 1147 MHz
BIOS build date: 2020-09-06
Date added: 2024-04-07
Reported VBIOS size: 266 KB
Reported MD5: 0696ad4cec325f8b235ee25abef98765
Reported SHA1: 03b4d838ea6cac4fe430bb756fdea8ed763ada32
```

This matches the local V100-personality card identity:

```text
GPU 14
PCI device: 0x1DF410DE
PCI subdevice: 0x12B810DE
VBIOS: 88.00.51.00.04
Reported name: Tesla V100-PCIE-12GB
```

## Local ROM Status

The user supplied a local copy:

```text
/home/josh/containers/temp/266855.rom
```

It was copied into the run archive:

```text
runs/20260519-173454-tensor-probe-gpu2-gpu14/online-vbios/266855.rom
```

Hashes of the supplied file:

```text
MD5    0696ad4cec325f8b235ee25abef98765
SHA1   03b4d838ea6cac4fe430bb756fdea8ed763ada32
SHA256 6c9fef2b08316c12c176bc04d91af028b45827de21a5d97b09e427c47b359100
```

The MD5 and SHA1 match the TechPowerUp metadata above.

The supplied file is an NVIDIA `NVGI` container rather than a raw PCI ROM image. The first PCI ROM signature (`55 aa`) is at offset `0x0a00`. The extracted raw PCI ROM is:

```text
runs/20260519-173454-tensor-probe-gpu2-gpu14/online-vbios/266855-extracted-pcirom.rom
```

Extracted raw PCI ROM hashes:

```text
MD5    57ffc17a0daaf8086bcfb1f1449abb31
SHA1   4ca75c9e4f2278f394a349e465e077e5d01f772f
SHA256 c1de84fb844887ee11ad7c797dd14fc180ef2baae52c66f0a10fa083b81c14cd
```

Strings from the extracted raw PCI ROM:

```text
GV100 PG500 SKU 111 VGA VBIOS
Version 88.00.51.00.04
8931G5000111
```

## Parse Status

Feeding the full `NVGI` container directly to `nvbios` caused a parser crash after partial output. Parsing the extracted raw PCI ROM works and produced:

```text
runs/20260519-173454-tensor-probe-gpu2-gpu14/online-vbios/266855-extracted-nvbios-vbu.txt
runs/20260519-173454-tensor-probe-gpu2-gpu14/online-vbios/266855-extracted-nvbios-selected.txt
```

Key parsed identity:

```text
PCI device: 0x10de:0x1df4
BIOS version: 88.00.51.00.04 from 03/27/18
INFO unk30: 8931G5000111
```

Important decoded pointers:

```text
BIT table 'P'
0x00: 0x1f255 => PERFORMANCE TABLE
0x80: 0x1fce2 => LOW POWER GR TABLE
0x9c: 0x1fd90 => UNKNOWN TABLE

BIT table 'M'
0x01: 0x4d2e => RESTRICT TABLE
0x03: 0x4de4 => TYPE TABLE
0x0d: 0x52a1 => UNK0D TABLE

BIT table 'p'
0x00: 0xe4f4 => FALCON UCODE TABLE
```

`nvbios` labels the `p[0x00]` pointer as a Falcon ucode table and cannot decode it:

```text
Unknown FALCON UCODE table version 0xa8
Failed to parse FALCON UCODE table at 0xe4f4, version a8
```

Offline reassessment shows `0xe4f4` is inside the EFI option-ROM part that starts at `0xe400`, immediately after `NPDE` metadata and high-entropy compressed payload bytes. Treat the `FALCON UCODE` label here as stale parser output for GV100, not as a proven decoded Falcon firmware table.

## Diff Status

The focused CMP-vs-266855 diff is archived here:

```text
runs/20260519-173454-tensor-probe-gpu2-gpu14/online-vbios/cmp-vs-266855-focused-diff.txt
```

Most relevant deltas:

```text
M_RESTRICT   CMP=0x04d09 V100=0x04d2e, 3 byte diffs in first 256 bytes
M_TYPE       CMP=0x04dbf V100=0x04de4, 1 byte diff in first 256 bytes
M_UNK0D      CMP=0x0527c V100=0x052a1, 1 byte diff in first 256 bytes
BIT p[0x00] CMP=0x0e4f4 V100=0x0e4f4, inside EFI payload; apparent a4/a8 version bytes are not reliable Falcon table versions
```

The V100-personality image is now a verified comparator for offline analysis. It is still not a flashing recommendation.
