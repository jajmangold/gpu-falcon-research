# Key Observations

## Registers

```text
NV_PGRAPH_PRI_FECS_FEATURE_READOUT                  0x00409660
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT 0x00409664
```

## Observed Capped Value

```text
0x00409664 = 0x00000999
```

## VBIOS Init Opcode

```text
NV_REG R[0x409664] &= 0xfffff666 |= 0x00000999
```

## RM Public Readback Boundary

```text
GET_SM_ISSUE_RATE_MODIFIER:
  Turing: implemented
  Ampere and later: implemented
  pre-Turing / GV100: NV_ERR_NOT_SUPPORTED
```

## GV100 Access Map Result

```text
0x00409660 denied
0x00409664 denied
```

## Debug Path Result

```text
NV83DE memory access: caller-owned hMemory only
NV83DE surface access: validated channel VA only
NV83DE regops: register access map only
```

## Identity Split

```text
V100-personality flash can change visible SM/product identity.
It did not clear 0x00409664 = 0x00000999 on local cards.
It did not restore full-speed Tensor/HMMA throughput on local cards.
```

## Current Bottom Line

```text
No credible production software unlock path was found.
The strongest boundary is FECS speed-select policy plus GV100 access-map denial.
```

