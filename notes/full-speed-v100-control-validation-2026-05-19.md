# Full-Speed V100 Control Validation - 2026-05-19

## Supplied File

```text
/home/josh/containers/temp/NVIDIA.TeslaV100.16384.170728.rom
```

Archived copy:

```text
runs/20260519-173454-tensor-probe-gpu2-gpu14/full-speed-control/NVIDIA.TeslaV100.16384.170728.rom
```

The file is an NVIDIA `NVGI` container. The raw PCI ROM starts at offset `0x0a00`.

Extracted raw PCI ROM:

```text
runs/20260519-173454-tensor-probe-gpu2-gpu14/full-speed-control/NVIDIA.TeslaV100.16384.170728-extracted-pcirom.rom
```

## Hashes

Container:

```text
MD5    bffff80a49a3af3b9bc8ecabc47c71c9
SHA1   650495724a5c1370833ec23708af8cd5aeb34aee
SHA256 9fd6f2f2d851ad9e767695b3986a43840e60f608db5201eb1ed998b275c98586
```

The container MD5/SHA1 match TechPowerUp entry `199146`:

```text
https://www.techpowerup.com/vgabios/199146/nvidia-teslav100-16384-170728
```

Extracted PCI ROM:

```text
MD5    b8525465497054bf95553bf7e61b4bd7
SHA1   23c39993d90a0add22a3d40277102280a547d015
SHA256 901187b4040dfdf1a1e4be9fc4a39a43486d999a54c2530eeabfcacec2f28886
```

## Parsed Identity

`nvbios` parse output:

```text
PCI device: 0x10de:0x1db4, class 0x030200
BIOS version 0x88.00.1a.00.03 from 07/21/17 for NV140 [GV100]
INFO unk30 8930G5000200
```

Strings:

```text
GV100 PG500 SKU 200 VGA VBIOS
Version 88.00.1A.00.03
8930G5000200
```

This matches the NVIDIA Tesla V100 PCIe 16GB reference identity:

```text
Device ID: 0x1DB4
Subsystem ID: 10DE 1214
VBIOS: 88.00.1A.00.03
SKU: GV100 PG500 SKU 200
Board power: 250 W
Clocks: 1245 / 876 / 1380 MHz
```

## Generated Artifacts

```text
runs/20260519-173454-tensor-probe-gpu2-gpu14/full-speed-control/v100-control-nvbios-vbu.txt
runs/20260519-173454-tensor-probe-gpu2-gpu14/full-speed-control/v100-control-nvbios-selected.txt
runs/20260519-173454-tensor-probe-gpu2-gpu14/full-speed-control/cmp-vs-v100-control-offline-compare.md
runs/20260519-173454-tensor-probe-gpu2-gpu14/full-speed-control/266855-vs-v100-control-offline-compare.md
```

## Key Deltas

Compared with CMP SKU 110:

```text
CMP device:     10de:1d84
Control device: 10de:1db4
CMP INFO unk30:     1100G5000110
Control INFO unk30: 8930G5000200
M_RESTRICT: 3 diffs in first 256 bytes
M_TYPE:     2 diffs in first 256 bytes
M_UNK0D:    1 diff in first 256 bytes
```

Compared with `266855` V100-personality SKU 111:

```text
266855 device:  10de:1df4
Control device: 10de:1db4
266855 INFO unk30:  8931G5000111
Control INFO unk30: 8930G5000200
M_RESTRICT: 4 diffs in first 256 bytes
M_TYPE:     2 diffs in first 256 bytes
M_UNK0D:    1 diff in first 256 bytes
```

The BIT `p[0x00]` pointer is still `0xe4f4`, and it still resolves inside the EFI option-ROM payload. The apparent byte at that offset is `e7` for this control ROM, reinforcing that the prior `a4`/`a8` parser interpretation was a false lead.

## Current Status

This ROM is now validated as the correct full-speed V100 PCIe 16GB control image by identity and source hash.

Additional temp-folder inventory found a second compute-class control:

```text
NVIDIA.TeslaV100.32768.180223.rom
Device: 10de:1db6
VBIOS: 88.00.48.00.02
SKU: GV100 PG500 SKU 202
INFO unk30: 8970G5000202
```

The 32 GB Tesla V100 control has byte-identical `M_RESTRICT`, `M_TYPE`, and `M_UNK0D` regions to `266855`, while the 16 GB Tesla V100 control differs in those fields. That makes the narrow `M` table deltas look more like board or memory-capacity metadata than a standalone FP16 tensor-throughput gate.

Inventory and comparison artifacts:

```text
runs/20260519-173454-tensor-probe-gpu2-gpu14/temp-rom-inventory/temp-vbios-inventory-2026-05-19.md
runs/20260519-173454-tensor-probe-gpu2-gpu14/temp-rom-inventory/cmp-vs-v100-32gb-offline-compare.md
runs/20260519-173454-tensor-probe-gpu2-gpu14/temp-rom-inventory/266855-vs-v100-32gb-offline-compare.md
runs/20260519-173454-tensor-probe-gpu2-gpu14/temp-rom-inventory/v100-16gb-vs-v100-32gb-offline-compare.md
```

What is still missing:

- a benchmark result from a real card using this reference identity on the same cuBLAS/WMMA probe stack;
- a direct proof that any specific VBIOS field controls FP16 tensor throughput.

The next offline analysis should treat the Tesla V100 16 GB and 32 GB ROMs as compute-class controls and prioritize fields that are shared by both full-speed control identities but differ from the local CMP/V100-personality pair.

## Sources

- TechPowerUp VBIOS 199146: https://www.techpowerup.com/vgabios/199146/nvidia-teslav100-16384-170728
- NVIDIA Tesla V100 PCIe Product Brief: https://images.nvidia.com/content/tesla/pdf/Tesla-V100-PCIe-Product-Brief.pdf
