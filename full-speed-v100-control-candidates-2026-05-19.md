# Full-Speed V100 Control Candidates - 2026-05-19

## Why A Third Comparator Is Needed

The CMP-vs-`266855` comparison shows what changed between:

- CMP identity: `10de:1d84`, subdevice `10de:12b9`, SKU 110, 68 CUDA-visible SMs.
- V100-personality identity: `10de:1df4`, subdevice `10de:12b8`, SKU 111, 80 CUDA-visible SMs.

That pair is not enough to isolate the full-speed FP16 tensor gate because the local V100-personality card still benchmarks near FP32-class throughput. A known full-speed V100 PCIe ROM plus a matching benchmark result is required as a third point.

## Best Public Candidates

TechPowerUp lists a reference Tesla V100 PCIe 16GB VBIOS that matches the supplied local filename:

```text
URL: https://www.techpowerup.com/vgabios/199146/nvidia-teslav100-16384-170728
Filename: NVIDIA.TeslaV100.16384.170728.rom
VBIOS Version: 88.00.1A.00.03
GPU Device Id: 0x10DE 0x1DB4
Subsystem Id: 10DE 1214
BIOS internals: GV100 PG500 SKU 200 VGA VBIOS
BIOS build date: 2017-07-28
Date added: 2018-02-18
Reported VBIOS size: 262 KB
Reported MD5: bffff80a49a3af3b9bc8ecabc47c71c9
Reported SHA1: 650495724a5c1370833ec23708af8cd5aeb34aee
Board power limit: 250 W
Clock fields: 1245 / 876 / 1380 MHz
```

TechPowerUp also lists a similar `_1` entry:

```text
URL: https://www.techpowerup.com/vgabios/201027/nvidia-teslav100-16384-170728-1
Filename: NVIDIA.TeslaV100.16384.170728_1.rom
VBIOS Version: 88.00.1A.00.03
GPU Device Id: 0x10DE 0x1DB4
Subsystem Id: 10DE 1214
BIOS internals: GV100 PG500 SKU 200 VGA VBIOS
BIOS build date: 2017-07-28
Reported VBIOS size: 318 KB
Reported MD5: fbe0f1b2d20fe7f30a42576df577142c
Reported SHA1: d7c86318d616f8dbdfc3908ea4ae0dea1d0ab1ce
Board power limit: 250 W
Clock fields: 1245 / 876 / 1380 MHz
```

The supplied local control file matches entry `199146`, not `201027`.

The expanded temp-folder set also includes a second compute-class Tesla control:

```text
Filename: NVIDIA.TeslaV100.32768.180223.rom
VBIOS Version: 88.00.48.00.02
GPU Device Id: 0x10DE 0x1DB6
BIOS internals: GV100 PG500 SKU 202 VGA VBIOS
Parsed INFO unk30: 8970G5000202
Memory class: 32 GB HBM2 Tesla V100
```

This identity matches NVIDIA's Tesla V100 PCIe 16GB product identity better than `266855`:

```text
NVIDIA product brief:
Device ID: 0x1DB4
Vendor ID: 0x10DE
Sub-Vendor ID: 0x10DE
Sub-System ID: 0x1214
Product SKU: 699-2G500-0200-XXX
GPU SKU: GV100-893-A1
Total board power: 250 W
Base / boost: 1245 / 1380 MHz
Memory: 16 GB HBM2, up to 900 GB/s
```

## Validation Criteria

Treat this as a control only if all of these are true:

- local bytes match the TechPowerUp MD5/SHA1 above;
- extracted PCI ROM parses as `10de:1db4` / `10de:1214`;
- internals show `GV100 PG500 SKU 200`;
- a real V100 PCIe 16GB using this identity shows FP16 tensor throughput far above FP32-class throughput on the same cuBLAS/WMMA probe stack.

## Comparison Targets If Obtained

Compare against both local images:

```text
CMP:              gpu2-cmp.rom, 10de:1d84, SKU 110, VBIOS 88.00.9D.00.00
V100 personality: 266855-extracted-pcirom.rom, 10de:1df4, SKU 111, VBIOS 88.00.51.00.04
V100 reference:   201027 candidate, 10de:1db4, SKU 200, VBIOS 88.00.1A.00.03
```

Priority fields:

- PCIR device/subdevice identity.
- INFO `unk07`, `unk30`, and nearby board identity fields.
- `M_RESTRICT`, `M_TYPE`, `M_UNK0D`.
- `P` table power/base-clock/low-power entries, but only after the small `M` and identity fields are exhausted.
- Any BIT table pointer that resolves inside the legacy VBIOS region, not inside compressed EFI payload.

## Sources

- TechPowerUp VBIOS listing 199146: https://www.techpowerup.com/vgabios/199146/nvidia-teslav100-16384-170728
- TechPowerUp VBIOS listing: https://www.techpowerup.com/vgabios/201027/nvidia-teslav100-16384-170728-1
- NVIDIA Tesla V100 PCIe Product Brief: https://images.nvidia.com/content/tesla/pdf/Tesla-V100-PCIe-Product-Brief.pdf
