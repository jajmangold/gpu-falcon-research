# Experiment Log

This page is a narrative of what was tried. It is intentionally practical:
future researchers should be able to skip repeated dead ends and pick up at
the actual boundary.

## Phase 1: Establish The Symptom

Question:

```text
Are the cards merely slower because of clocks, power, or bad benchmarking?
```

Why this came first:

Before blaming firmware, we needed to prove there was a stable, repeatable
performance class difference. GPUs can look broken for mundane reasons:
thermal throttling, power caps, clock settings, container scheduling,
incorrect Tensor Core dispatch, or a benchmark that accidentally falls back to
non-Tensor instructions.

Work performed:

```text
WMMA/HMMA probes
cuBLAS Tensor GEMM probes
SGEMM/DGEMM ratio probes
DP/IMLA probes
block/loop burst-window sweeps
cross-card comparisons between CMP-native and V100-personality cards
```

Outcome:

```text
The slowdown tracks math issue class, not just product name or visible SM
count. The local V100-personality cards expose more SMs but retain the reduced
Tensor/HMMA issue class.
```

Interpretation:

This moved the investigation below ordinary application behavior. The
slowdown followed Tensor/HMMA-style math, not just the product string printed
by CUDA or the number of visible SMs.

Relevant local datasets:

```text
runs/20260519-173454-tensor-probe-gpu2-gpu14/
runs/20260520-dp-imla-issue/
runs/20260520-dp4a-imla-probe/
runs/20260520-hmma-burst-window/
runs/20260520-hmma-cadence-proxy/
```

## Phase 2: Compare VBIOS And Identity

Question:

```text
Did prior flashing change the actual limiter or just visible identity?
```

Why this branch mattered:

The prior owner had apparently flashed some cards into a V100-like
personality. If that flash fully changed the board policy, the limiter might
have been a VBIOS table problem. If it only changed the visible identity, then
the limiter had to live in a deeper layer.

Work performed:

```text
local CMP ROM dump comparison
local V100-personality ROM comparison
public V100 comparator checks
same-version OBD difference checks
decoded VBIOS main-init tail comparison
```

Outcome:

```text
The V100-personality flash changed visible identity and SM exposure, but local
cards still showed CMP-like OBD/InfoROM identity and still carried the FECS
speed-select reduced value.
```

Interpretation:

The V100 flash was real but incomplete. It affected the identity and visible
floorsweep surface, but not the speed-select policy that controls Tensor/HMMA
issue behavior.

Critical observation:

```text
capped/CMP-derived images:
  NV_REG R[0x409664] &= 0xfffff666 |= 0x00000999

full-speed Tesla V100 / Quadro GV100 controls:
  no matching set-reduced opcode in the corresponding location
```

Relevant local datasets:

```text
runs/20260520-bootrom-dump/
runs/20260520-evidence-dag/gv100-vbios-main-init-tail-compare.md
rom-dump-and-vbios-diff-status-2026-05-19.md
visible-identity-diff-2026-05-20.md
```

## Phase 3: Ask RM Directly

Question:

```text
Can RM report the selected issue-rate modifier on GV100?
```

Why this branch mattered:

If RM could report the selected issue-rate modifier, then the limiter might be
a supported policy state rather than a hidden one. A supported readback would
also give us clean confirmation of the divisor without raw register access.

Work performed:

```text
NV2080_CTRL_CMD_GR_GET_SM_ISSUE_RATE_MODIFIER probes
internal static GR/KGR issue-rate control searches
source backtracking through gr.def and GR HAL implementations
```

Outcome:

```text
GV100/pre-Turing is explicitly stubbed with NV_ERR_NOT_SUPPORTED.
Turing and Ampere implement the readback.
```

Meaning:

```text
The source explains the runtime unsupported result. This is not a probe bug.
```

Interpretation:

This closed the clean public-RM path. NVIDIA added public issue-rate readback
for later architectures, but GV100 falls on the stubbed side of the HAL split.

## Phase 4: Register Access And Debugger Paths

Question:

```text
Can regops, NV83DE, or debug memory reach the FECS register anyway?
```

Why this branch mattered:

Debugger paths are attractive because they are designed to inspect GPU state.
They often have access that ordinary CUDA code does not. If any production
path could legally reach `0x00409664`, this was one of the most plausible
places to find it.

Work performed:

```text
RM regops path audit
NV83DE debug object audit
GV100 user_access_map.bin lookup
verification-only access-map edit path search
batch memory bounds review
```

Outcome:

```text
0x00409660 denied by GV100 user access map
0x00409664 denied by GV100 user access map
NV83DE DEBUG_EXEC_REG_OPS calls gpuValidateRegOps first
debug memory access requires caller-owned hMemory
access-map editing exists only in verification/internal builds
```

One code-quality note:

```text
DEBUG_READ_BATCH_MEMORY checks only dataOffset < dataLength.
DEBUG_WRITE_BATCH_MEMORY checks dataOffset + length <= dataLength.
```

This is interesting for review, but it does not directly expose FECS state.

Interpretation:

The debug path is useful for understanding the permission model, not for
bypassing it. The same register access map that blocks normal regops also
blocks debugger regops, and debug memory still requires a memory object the
caller already owns.

## Phase 5: Firmware And Debugdump Visibility

Question:

```text
Can firmware dumps or debugdump outputs reveal the hidden 1/16 implementation?
```

Why this branch mattered:

Once the register and VBIOS write were known, the remaining mystery was the
meaning of GV100 `REDUCED_SPEED`. Firmware and debugdump artifacts were the
best chance of seeing the policy code, symbol names, or private tables that
translate the binary reduced bit into a concrete issue cadence.

Work performed:

```text
firmware dump scanning
debugdump refresh and compare
public debugdump harvesting
literal byte-pattern checks for 0x00000999
register-name/address scans
```

Outcome:

```text
The dumps reveal register surfaces, names, and addresses. They did not reveal
the private policy code that maps GV100 binary reduced mode to the observed
issue cadence.
```

Interpretation:

The dumps were good for correlation, not extraction. They confirmed that the
register names and addresses are part of the firmware/debug vocabulary, but
they did not expose the secure policy implementation.

Relevant local datasets:

```text
runs/20260520-firmware-visibility/
runs/20260520-debugdump-refresh/
public-nvidia-debugdump-harvest-2026-05-20.md
vastai-v100-debugdump-control-2026-05-20.md
```

## Phase 6: Mechanism Modeling

Question:

```text
Is the limiter per-SM, per-kernel, per-card, or a global issue budget?
```

Why this branch mattered:

Even without the private microcode, benchmark shape can distinguish possible
mechanisms. A per-SM disable, a per-kernel throttle, and a global issue budget
leave different scaling patterns when block count, loop depth, and concurrency
change.

Work performed:

```text
single-block and many-block HMMA loops
concurrent stream tests
burst-window tests
Z3 consistency check for candidate mechanisms
```

Outcome:

```text
The data is most consistent with a global Tensor/HMMA issue budget or cadence
gate, not a simple per-SM disable. More work would be needed for a definitive
microarchitectural proof.
```

Interpretation:

The cap behaves more like an issue budget or cadence gate than like a simple
SM-count reduction. That matches the fact that the register is a speed-select
policy surface, not a floorsweep mask.

Relevant local datasets:

```text
runs/20260520-hmma-burst-window/
runs/20260520-hmma-cadence-proxy/
hmma-issue-spacing-2026-05-20.md
```

## Phase 7: Final Boundary

Why the investigation stops here:

Every production path we checked either reports the state, mirrors the state,
or refuses to access the state. The remaining unknown is not likely to be
found by another CUDA benchmark or another ordinary RM control. It is likely
in the private producer data or signed policy code that decides what
`REDUCED_SPEED` means for this SKU.

The strongest remaining blockers:

```text
missing private chipfuse/POR/IFF producer data
signed firmware/VBIOS trust chain
GV100 production access map denial
pre-Turing RM issue-rate stub
FECS/GR secure microcode boundary
```

The strongest next leads:

```text
gv100_f.json / gv100_f.jsone
gv100_POR.json
IFF_patches
Mkt_options
engineering-board access maps
internal MODS verification behavior
additional full-speed GV100 debugdump and VBIOS controls
```
