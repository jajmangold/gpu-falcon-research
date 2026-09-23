# Next Research

This is what would actually move the investigation forward.

## 1. Find The Missing Producer Data

The strongest missing artifact is the data source that tells GV100 early init
or FECS policy to assert reduced speed.

Candidate names/classes:

```text
chipfuse
POR
IFF
Mkt_options
IFF_patches
gv100_f.json
gv100_f.jsone
gv100_POR.json
```

Goal:

```text
Find a row or policy entry that targets FECS speed-select state,
SM speed-select state, or the VBIOS init opcode that writes 0x409664.
```

## 2. Compare More Full-Speed GV100 Controls

Useful comparators:

```text
Tesla V100 PCI IDs 1db4 / 1db5 / 1db6
Quadro GV100
Titan V
known-good full-speed SXM and PCIe variants
```

For each comparator, capture:

```text
VBIOS version
decoded main init script
InfoROM/OBD/IMG metadata if available
live 0x00409664 if legally and safely observable
Tensor/HMMA throughput probe result
visible GPC/TPC floorsweep state
```

## 3. Keep Access-Map Work Read-Only

The access map is useful because it explains why production debug/regops paths
fail.

Useful questions:

```text
Which generated config would include 0x00409660 / 0x00409664?
Was that entry removed by signoff policy?
Do internal MODS/verification builds ship a different map?
Does the unrestricted map differ on engineering boards?
```

## 4. Keep Firmware Dump Expectations Realistic

Firmware/debug dumps found register names and addresses, but not the hidden
GV100 divisor implementation.

Likely reason:

```text
The visible dumps expose register surfaces and metadata,
not the secure FECS/GR policy code that maps reduced mode to issue cadence.
```

## 5. Watch For Similar Mechanisms On Neighbor Generations

Later generations are useful because they expose the explicit model:

```text
feature readout
feature override
3-bit SM speed-select fields
explicit 1/2, 1/4, 1/8, 1/16 selectors
```

The later model can help infer GV100 behavior, but GA100/Turing offsets should
not be treated as portable to GV100.

## 6. Do Not Re-Run Known Dead Ends First

Lower-value repeats:

```text
ordinary CUDA knobs
nvidia-smi settings
public RM issue-rate controls
NV83DE debug regops to 0x409664
OBD-only identity edits
V100 personality flash alone
```

Those paths have already been checked or have a clear source-level reason for
failure.

