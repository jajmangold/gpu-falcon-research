# PSTRAPS Override Assessment - 2026-05-20

## Purpose

Assess whether the documented envytools `PSTRAPS` runtime override mechanism is a plausible explanation or bypass path for the local GV100 Tensor throughput limiter.

This pass was read-only. No MMIO writes were performed.

## What The Envytools Docs Confirm

Local source:

```text
tools/envytools/docs/hw/io/pstraps.rst
tools/envytools/rnndb/io/pstraps.xml
```

Confirmed points:

```text
PSTRAPS / PEXTDEV MMIO base on NV3+:
0x101000-0x101fff in BAR0

G80+ STRAPS0_PRIMARY:
0x101000

G80+ STRAPS1_PRIMARY:
0x10100c

Primary register bit 31:
OVERRIDE_ENABLE
```

The docs state that when writing the primary strap register:

```text
bit 31 = 0: override disabled, restore original reset strap value
bit 31 = 1: override enabled, use host-written strap value
```

They also state that changed effective strap values start being used immediately.

## Important Scope Caveat

The documented G80+ strap fields are mostly public board/config identity fields:

```text
STRAPS0:
- RAM config
- crystal type
- DEVICE_ID bits
- BAR1 size
- ROM type
- flat panel config

STRAPS1:
- PCI class
- BAR5 enable
- BAR0 size
- BAR1 size high bits
- BAR3 size
```

The documented fields do not include a Tensor Core enable, Tensor Core issue-rate divider, SKU throttle, or FP16 throughput policy bit.

There are unknown bits in the documented G80+ sets, so PSTRAPS cannot be ruled out completely. But the visible fields are already in the V100-personality state on the local V100-personality cards:

```text
PCI device:    10de:1df4
Subsystem:     10de:12b8
Product name:  Tesla V100-PCIE-12GB
VBIOS:         88.00.51.00.04
SM count:      80
```

The Tensor throughput cap survives those visible strap/personality changes.

## Live Read Attempt

Earlier:

```text
tools/envytools/build/nva/nvalist
tools/envytools/build/nva/nvapeek
```

failed in the current NVIDIA-driver-owned environment:

```text
WARN: Can't probe 0000:xx:00.0
PCI init failure!
```

A read-only sysfs BAR0 attempt from a privileged container also failed:

```text
/sys/bus/pci/devices/0000:07:00.0/resource0
dd skip 0x101000:
Input/output error
```

So live PSTRAPS values were not captured in the current environment.

## Interpretation

The PSTRAPS override mechanism is real, and it is worth keeping in the investigation tree. However, the current evidence does not support the strong claim that writing `0x101000 | bit31` would necessarily remove the Tensor throughput cap.

Reasons:

1. The documented G80+ strap fields do not identify a tensor-throughput throttle field.
2. The local V100-personality cards already expose the normal V100-visible identity fields that PSTRAPS is known to influence.
3. The cap persists after those identity fields change.
4. The 15-16x HMMA issue/cycle cost is more consistent with hidden SKU/fuse/firmware policy than a normal documented strap field.

Current best model:

```text
PSTRAPS likely participates in visible board/personality identity.
The Tensor issue divider is probably keyed from hidden fuse/SKU state or signed firmware policy not exposed in documented PSTRAPS fields.
```

## Safe Next Step

A proper PSTRAPS test requires a maintenance environment where BAR0 can be read without the NVIDIA driver owning the device. The first safe target is read-only:

```text
read 0x101000..0x101040 on CMP identity
read 0x101000..0x101040 on V100-personality
compare against full-speed V100 control if possible
```

Only after the read-only values are understood should any strap override experiment be considered, and only on a sacrificial card with an explicit rollback plan.
