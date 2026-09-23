# Glossary

## FECS

Front End Control Subsystem / Front End Control Processor. In this research,
FECS is the important graphics-control microcontroller surface because the
speed-select register lives under the PGRAPH FECS register range.

## GPCCS

GPC Control Subsystem. Another NVIDIA graphics microcontroller. It is relevant
to context switching and graphics control, but the strongest limiter evidence
here points at FECS speed-select state.

## FMLA / HMMA / Tensor

Names around fused multiply-add and Tensor Core execution. GV100 public
headers expose `FMLA` speed-select state; workload probes observe the effect
through HMMA/Tensor throughput.

## IMLA

Integer multiply-add class. The same speed-select register family includes
IMLA fields, which suggests the policy is broader than only FP16 Tensor math.

## DP

Double precision. The speed-select register family includes DP fields too.

## SM Speed Select

A hardware/firmware feature-selection mechanism that can put selected SM math
classes into full-speed or reduced-speed modes.

GV100 uses an older 1-bit full/reduced model. Later architectures expose
explicit multi-rate selectors.

## RM

NVIDIA Resource Manager, the main kernel/driver management layer.

## MODS

NVIDIA's manufacturing/diagnostic test framework. MODS-style paths are useful
because they often reveal verification and factory-test concepts that normal
production RM does not expose.

## User Register Access Map

A bitmap/allowlist controlling which BAR0 registers user-originated regops may
access through RM-mediated paths.

In the local GV100 map:

```text
0x00409660 denied
0x00409664 denied
```

## OBD / InfoROM

Board-management identity objects stored outside the normal display name. Local
V100-personality cards still showed CMP-like OBD/InfoROM identity.

## IFF / POR / chipfuse

Internal configuration data classes that likely describe SKU and fuse-derived
policy. These are top candidates for the missing producer of the GV100
speed-select state.

