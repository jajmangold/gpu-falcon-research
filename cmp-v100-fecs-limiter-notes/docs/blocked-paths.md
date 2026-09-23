# Blocked Paths

This page lists the paths checked so future work does not repeat the same
dead ends.

## CUDA / User-Space Runtime

No CUDA API, PTX mode, kernel launch option, persistence mode, or
application-level setting was found that changes the FECS speed-select state.

Why:

```text
The relevant state is established below CUDA.
The VBIOS/init script can assert it before the driver finishes normal setup.
The RM public issue-rate readback path is stubbed for GV100.
```

## Public RM Issue-Rate Controls

`NV2080_CTRL_CMD_GR_GET_SM_ISSUE_RATE_MODIFIER` is implemented for Turing and
newer paths, but GV100 falls under the pre-Turing stub.

Result:

```text
GV100 returns unsupported for the public issue-rate readback path.
```

## RM Regops

RM register operations validate offsets before execution.

For the relevant FECS speed-select offsets:

```text
0x00409660
0x00409664
```

the GV100 access map denies access, and regops report invalid/denied status.

## NV83DE Debugger

NV83DE exposes useful debugger controls but not unrestricted FECS access.

Blocked by:

```text
validated debuggee VA mappings
caller-owned hMemory checks
memdesc bounds checks
gpuValidateRegOps()
GV100 user register access map
```

## MODS-Style Issue-Rate Override

MODS and RM contain issue-rate override/readback logic for Turing and Ampere.
The visible GV100/Volta classes do not implement the same production path.

Related names:

```text
Volta-953
RMOverrideSmSpeedSelect
RMOverrideSmSpeedSelect1
RMSchMicroSched
```

These are strong evidence that NVIDIA has internal policy machinery for this
class of limiter, but not evidence of a production GV100 user path.

## V100-Personality Flash

Some local cards had previously been flashed to a V100-like personality. That
changed visible identity and SM exposure, but did not clear the speed-select
state.

Changed:

```text
PCI/product identity
visible SM/TPC exposure
some tooling behavior
```

Not changed:

```text
OBD/InfoROM CMP identity on local samples
0x00409664 = 0x00000999
Tensor/HMMA reduced issue class
```

## Direct Runtime BAR0 Write

On full-speed V100-style devices, direct access experiments showed the FECS
region can be observable/writable in some states. On CMP-class cards, the
relevant FECS feature-override range appeared blocked or disconnected in live
probing.

Interpretation:

```text
If CMP returns 0xffffffff across the FECS feature-override range,
the problem is deeper than a single software permission bit.
```

## VBIOS Patch Without Signing

The apparent clean firmware-level fix would be to remove or neutralize the
init-script write:

```text
NV_REG R[0x409664] &= 0xfffff666 |= 0x00000999
```

But modern NVIDIA firmware signing and board security make unsigned or
improperly signed firmware changes a separate trust-chain problem.

## Known Remaining Categories

The remaining plausible categories are not normal production software paths:

```text
NVIDIA internal verification RM or MODS builds
signed VBIOS / signed firmware with a changed init policy
private chipfuse / POR / IFF data explaining the SKU row
physical invasive or fault-injection research
a real vulnerability that grants the missing privileged access
```

