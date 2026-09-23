# Visible Identity Diff - 2026-05-20

## Purpose

After confirming the V100-personality cards are uniformly capped at `6.25%` sustained SM throughput on a Tensor Core GEMM kernel, this pass checks whether a driver-visible field still identifies the cards as CMP or as a reduced tensor-throughput SKU.

Artifacts:

```text
runs/20260519-173454-tensor-probe-gpu2-gpu14/visible-identity-diff/
```

## Key Finding

The V100-personality cards have successfully changed the obvious driver-visible identity:

```text
PCI device:      10de:1df4
Subsystem:       10de:12b8
Product name:    Tesla V100-PCIE-12GB
VBIOS:           88.00.51.00.04
SMs:             80
```

The CMP card remains:

```text
PCI device:      10de:1d84
Subsystem:       10de:12b9
Product name:    NVIDIA CMP 100-210
VBIOS:           88.00.9D.00.00
SMs:             68
```

That means the retained `1/16` tensor cap is not explained by the obvious PCI device ID, subsystem ID, product name, VBIOS version, or CUDA SM count. Those fields are already in the V100-personality state.

## NVIDIA-SMI / NVML Summary

| Field | CMP GPU 2 | V100-Personality GPUs 4/7/9/11/14 |
| --- | --- | --- |
| Product name | `NVIDIA CMP 100-210` | `Tesla V100-PCIE-12GB` |
| Product brand | `GeForce` | `Quadro` |
| VBIOS | `88.00.9D.00.00` | `88.00.51.00.04` |
| PCI device ID | `1D8410DE` | `1DF410DE` |
| PCI subsystem ID | `12B910DE` | `12B810DE` |
| FB memory | `16384 MiB` | `16384 MiB` |
| BAR1 memory | `64 MiB` | `64 MiB` |
| InfoROM image | `G001.0000.01.04` | `G001.0000.01.04` |
| Current power limit | `120 W` | `120 W` |
| Default/max power limit | `250 W` / `250 W` | `250 W` / `250 W` |
| Max SM clock | `1380 MHz` | `1380 MHz` |
| Max memory clock | `810 MHz` | `810 MHz` |
| ECC mode | `N/A` | `N/A` |

The most suspicious visible carry-over is not a CMP string; it is the shared board-management profile:

```text
InfoROM image:        G001.0000.01.04
Current power limit:  120 W
Default/max limit:    250 W
ECC:                  N/A
BAR1:                 64 MiB
Memory clock:         810 MHz
```

However, the prior power sample showed the DeepBench Tensor Core run did not hit the `120 W` cap, so the current power limit is not enough by itself to explain the `1/16` tensor cap.

## PCI / Sysfs Summary

Representative sysfs identity:

```text
GPU 2 CMP:
vendor=0x10de
device=0x1d84
subsystem_vendor=0x10de
subsystem_device=0x12b9
class=0x030200
revision=0xa1

GPU 14 V100-personality:
vendor=0x10de
device=0x1df4
subsystem_vendor=0x10de
subsystem_device=0x12b8
class=0x030200
revision=0xa1
```

The kernel-visible PCI identity follows the VBIOS/personality change.

## CUDA Attribute Summary

CUDA/PyTorch-visible attributes also follow the expected personality split:

| Field | CMP | V100-Personality |
| --- | ---: | ---: |
| Compute capability | `7.0` | `7.0` |
| SM count | `68` | `80` |
| L2 cache | `6291456` | `6291456` |
| Memory bus width | `4096` | `4096` |
| Memory clock | `810000 kHz` | `810000 kHz` |
| Max threads / SM | `2048` | `2048` |
| Shared memory / SM | `98304` | `98304` |
| Registers / SM | `65536` | `65536` |

CUDA does not expose a Tensor Core count or Tensor Core enable mask here.

## Interpretation

The visible identity pass did **not** find a simple remaining field that says "this is still CMP" on the V100-personality cards.

What remains plausible:

1. **Hidden fuse state:** the driver or firmware reads SKU/fuse data that is not surfaced through normal PCI/NVML/CUDA attributes.
2. **Signed firmware policy:** GR/FECS/GPCCS/SEC2 firmware configures tensor issue rate or active tensor lanes from hidden SKU/fuse state.
3. **Board-management carry-over:** shared InfoROM/power-management state may be part of the SKU profile, but current evidence says power limit alone is not the throughput cap.

## Lower-Level Read-Only Route

Envytools has PFUSE definitions for read-only hardware disable masks:

```text
PFUSE base:              0x021000
GF100+ FUSES offset:     0x100
TPC_DISABLE_MASK:        PFUSE + 0x100 + 0x144 = 0x021244
PART_DISABLE_MASK:       PFUSE + 0x100 + 0x148 = 0x021248
TEMP_CAL_* / SPEEDO:     PFUSE + 0x100 + later offsets
```

Those registers are relevant because PFUSE is described by envytools as factory-set configuration that cannot be overridden later by software.

Attempted tools:

```text
tools/envytools/build/nva/nvalist
tools/envytools/build/nva/nvapeek
```

Current result:

```text
WARN: Can't probe 0000:xx:00.0
PCI init failure!
```

So the PFUSE read route is currently blocked from this running driver environment. I did not force MMIO access or unload drivers.

The best next discriminator is lower-level register/fuse observation, if available read-only in a maintenance environment, or collecting the same NVML/CUDA/profiler identity from a real full-speed V100 PCIe card and finding which fields differ despite matching public V100 behavior.
