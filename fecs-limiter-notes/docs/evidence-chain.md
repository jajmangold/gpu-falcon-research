# Evidence Chain

This is the shortest path from symptom to current conclusion.

## 1. Symptom

Some local GV100-based CMP 100-210 cards, including cards flashed to a
V100-like personality, show much lower FP16 Tensor/HMMA throughput than a
full-speed V100 control.

The slowdown is not explained by simple CUDA identity alone:

```text
CMP-native cards: fewer visible SMs and capped Tensor throughput
V100-personality local cards: more visible SMs, still capped Tensor throughput
full-speed controls: expected Tensor throughput class
```

## 2. Register-Level Clue

The recurring live value on capped local cards is:

```text
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
BAR0 offset: 0x00409664
value:      0x00000999
```

The bit layout is consistent with reduced-speed plus override fields for
integer multiply-add, FMLA/Tensor-related math, and DP-related math in the
older GV100 speed-select model.

## 3. VBIOS Init Script Clue

Decoded capped/CMP-derived VBIOS init scripts contain:

```text
NV_REG R[0x409664] &= 0xfffff666 |= 0x00000999
```

This is the strongest visible assertion point found. It shows the speed-select
state is not merely a late CUDA or application policy. It is established by
early board/firmware initialization.

Full-speed Tesla V100 / Quadro GV100 control images examined locally do not
contain the same set-reduced opcode at the corresponding main-init location.

## 4. Driver Source Clue

The source tree exposes the mechanism family:

```text
NV_PGRAPH_PRI_FECS_FEATURE_READOUT
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
SM_SPEED_SELECT
FEATURE_READOUT
FEATURE_OVERRIDE
RMOverrideSmSpeedSelect
RMSchMicroSched
GET_SM_ISSUE_RATE_MODIFIER
```

For GV100, the public-style visible state is coarse:

```text
FULL_SPEED
REDUCED_SPEED
```

For later chips, the visible state becomes a multi-rate selector with explicit
divisors such as:

```text
FMLA16_REDUCED_SPEED_1_2
FMLA16_REDUCED_SPEED_1_4
FMLA16_REDUCED_SPEED_1_8
FMLA16_REDUCED_SPEED_1_16
```

Internal engineering notes found in the tree explicitly describe a transition
from older 1-bit SM speed-select fuses to newer 3-bit SM speed-select fuses.

## 5. Public RM Boundary

The RM HAL dispatch table maps `GET_SM_ISSUE_RATE_MODIFIER` like this:

```text
Turing: implemented
Ampere and later: implemented
pre-Turing: stubbed with NV_ERR_NOT_SUPPORTED
```

GV100 is pre-Turing, so the normal public readback path is intentionally not
available even though the hardware register definitions exist.

## 6. Debug Boundary

The NV83DE debugger object exposes non-privileged debug controls, but they are
constrained:

```text
surface access -> validated channel VA mappings
memory access  -> caller-owned RM memory handles
regops         -> user register access map
```

The GV100 shipped user access map denies:

```text
0x00409660 NV_PGRAPH_PRI_FECS_FEATURE_READOUT
0x00409664 NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
```

So debug regops are an allowlist oracle, not a bypass.

## 7. Current Conclusion

The limiter appears to be a FECS/GR speed-select policy established before or
during early GPU initialization. The visible production software surfaces do
not provide a normal way to clear it on GV100/CMP.

