# GPU Falcon Research

Reverse-engineering the FECS speed-select limiter on NVIDIA CMP 100-210 (GV100) GPUs that refuse to deliver full Tesla V100 Tensor throughput after V100 firmware flashes.

## License

[MIT](LICENSE)

## The Story

The CMP 100-210 is a crypto-mining-era card built on GV100 silicon -- the same chip as Tesla V100. Second-hand cards can be flashed with V100 firmware, changing their visible identity and exposed resources. But something deep in the graphics initialization path keeps Tensor/HMMA workloads capped at roughly 1/16th V100 throughput.

The investigation traced this to a single FECS register:

```
NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT
BAR0 offset: 0x00409664
Value on capped cards: 0x00000999
```

Written during early VBIOS init, this register asserts reduced-speed policy for IMLA, FMLA, and DP math classes. The full narrative is in [`fecs-limiter-notes/`](fecs-limiter-notes/).

## Repository Structure

```
.
├── fecs-limiter-notes/    # Long-form research narrative with Mermaid diagrams
├── notes/                 # Dated research notes (probes, benchmarks, findings)
├── scripts/               # CUDA probe kernels (HMMA, DP4A, SGEMM, WMMA)
├── tools/                 # C/Python analysis tools (VBIOS decode, register probes)
└── runs/                  # Raw run data (gitignored)
```

## Key Findings

| Finding | Reference |
|---------|-----------|
| FECS register `0x409664` holds the speed-select state | [`fecs-limiter-notes/`](fecs-limiter-notes/) |
| VBIOS init scripts write `0x999` to this register on capped images | `notes/nvdrv-backtrack-from-v100-baseline-2026-05-20.md` |
| FP64 GEMM runs at 6.8-7.3% of V100 full-speed (consistent with DP_REDUCED) | `notes/sgemm-dgemm-ratio-probe-2026-05-20.md` |
| V100 personality changes SM count but not the FMLA/HMMA cap class | `notes/local-tensor-sgemm-sweep-2026-05-20.md` |
| RM readback path (`GET_SM_ISSUE_RATE_MODIFIER`) is stubbed for pre-Turing | `fecs-limiter-notes/docs/evidence-chain.md` |
| User access map explicitly denies `0x409660` and `0x409664` | `fecs-limiter-notes/docs/debug-paths.md` |

## Reading the Notes

Start with [`fecs-limiter-notes/README.md`](fecs-limiter-notes/README.md) for the full narrative, then drill into supporting docs:

| Document | Purpose |
|----------|---------|
| `docs/evidence-chain.md` | Compact proof chain from symptom to FECS speed-select |
| `docs/source-map.md` | Source paths, line ranges, scoped snippets |
| `docs/experiment-log.md` | What was tried, why, and what each branch ruled out |
| `docs/registers-and-identities.md` | Register constants and identity layers |
| `docs/throughput-model.md` | Working model for burst behavior and global Tensor budget |

## Running the Probes

CUDA probe scripts in `scripts/` require CUDA 12+ and a Volta-class GPU:

```bash
nvcc scripts/hmma_issue_probe.cu -o hmma_probe
./hmma_probe

nvcc scripts/sgemm_dgemm_ratio_probe.cu -o sgemm_probe
./sgemm_probe
```

Python analysis tools in `tools/` work with VBIOS dumps and `nvidia-smi` CSV output.

## Scope

- Non-destructive, read-only investigation of owned hardware
- No firmware flashing, EEPROM overwrites, or active GPU disruption
- Documentation-only: no NVIDIA source files, firmware blobs, or leaked archives
