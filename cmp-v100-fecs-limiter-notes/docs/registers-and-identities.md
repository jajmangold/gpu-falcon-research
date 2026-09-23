# Registers, Identities, And Policy Layers

## Core Registers

```text
NV_PGRAPH_PRI_FECS_FEATURE_READOUT
BAR0 offset: 0x00409660
Purpose: FECS feature readout state

NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
BAR0 offset: 0x00409664
Purpose: FECS SM speed-select override
Observed capped value: 0x00000999
```

GV100 exposes speed-select as an older binary full/reduced model.

Later chips expose direct multi-rate fields in fuse/readout/override models.

## Interpreting 0x00000999

At the research-summary level:

```text
0x00000999 means reduced-speed bits plus override bits are asserted
for multiple math classes in the GV100 speed-select register.
```

This is consistent with reduced behavior for:

```text
FMLA / Tensor-like math
IMLA-like math
DP-like math
```

The exact internal place where GV100 binary `REDUCED_SPEED` becomes a
1/16-class issue policy was not found in visible source.

## Identity Layers

The evidence supports at least three separate identity/policy layers.

### Layer 1: PCI / Product / Visible SM Personality

This layer can change through VBIOS/personality flashing.

Observed effects:

```text
device/subdevice identity changes
product name changes
visible SM/TPC exposure can change
some tools behave as if the card is V100-like
```

### Layer 2: Board Management / InfoROM Identity

Local V100-personality samples still carried CMP-like board-management identity
in OBD/InfoROM-derived fields.

Observed:

```text
OBD marketingName: CMP
InfoROM IMG profile: G001-like on local samples
```

This is strong evidence of CMP origin, but no visible direct consumer was found
that maps OBD alone to the FECS speed-select limiter.

### Layer 3: FECS Speed-Select Policy

This layer remained capped even after visible V100 personality changes.

Observed:

```text
0x00409664 = 0x00000999
Tensor/HMMA issue class remains reduced
```

This appears to be the layer that matters for the limiter.

## CMP SKU Detection

The visible source contains `gpuGetIsCmpSku_GV100()`.

For GV100, CMP detection is based on fuse-derived PCIe boot-disable state:

```text
HAL_FUSE_OPT_PCIE_BOOT_GEN3_DISABLE
HAL_FUSE_OPT_PCIE_BOOT_GEN23_DISABLE
```

Visible consumers found so far mainly affect reporting and CUDA/devtool
restrictions. The stronger limiter evidence is the separate FECS speed-select
path.

