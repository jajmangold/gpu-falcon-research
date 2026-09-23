# GPU Falcon Research

Side research workspace for comparing local GV100 cards that report as CMP 100-210 versus cards currently exposed as Tesla V100-PCIE-12GB.

## License

[MIT](LICENSE)

## Scope

- Keep all early work non-destructive and reversible.
- Start with inventory, reproducible observations, and workload benchmarks.
- Do not flash firmware, overwrite EEPROM contents, or unload/reset active production GPUs without a separate explicit plan.
- Treat cards as owned, end-of-life lab hardware, but still document legal, safety, and recovery assumptions before any bypass or firmware experiment.

## Initial Candidate Pair

Selected on 2026-05-19 from `nvidia-smi` and `lspci` because both were at 0% GPU utilization with only small resident Python contexts.

| Role | GPU index | PCI bus ID | Reported name | PCI device ID | UUID | VBIOS | Memory used |
| --- | ---: | --- | --- | --- | --- | --- | ---: |
| CMP baseline | 2 | `00000000:07:00.0` | `NVIDIA CMP 100-210` | `10de:1d84` | `GPU-4f0356c3-2160-54bd-6464-8b62eaf5b23c` | `88.00.9D.00.00` | 289 MiB |
| V100-firmware comparator | 14 | `00000000:19:00.0` | `Tesla V100-PCIE-12GB` | `10de:1df4` | `GPU-71be175a-2ae4-735d-78e1-6eeeea51fc53` | `88.00.51.00.04` | 329 MiB |

## Baseline Commands

Use these for repeatable read-only snapshots:

```bash
nvidia-smi --query-gpu=index,pci.bus_id,name,uuid,memory.total,memory.used,utilization.gpu,utilization.memory,temperature.gpu,power.draw --format=csv
nvidia-smi --query-compute-apps=gpu_bus_id,gpu_uuid,pid,process_name,used_memory --format=csv
lspci -nn | rg -i 'nvidia|3d controller|vga'
nvidia-smi -q -i 2,14
```

## Open Questions

- Does the `10de:1df4` V100 personality change the visible SM/tensor-core scheduling behavior versus `10de:1d84` CMP?
- Are the differences limited to PCI IDs, VBIOS strings, power defaults, and exposed product name, or do CUDA kernels show measurable capability changes?
- Which persistent contexts are using all GPUs, especially PID `2024418`, and can a controlled benchmark window be scheduled without disrupting services?
- What recovery path exists before any firmware experiment: known-good VBIOS dump, external programmer access, riser isolation, and host boot fallback?

## Runs

- `runs/20260519-173454-tensor-probe-gpu2-gpu14/summary.md`: first CUDA/PyTorch/cuBLAS/WMMA tensor-path probe. Key result: GPU 14 exposes 80 SMs versus GPU 2's 68, both cards accept and run HMMA instructions, but PyTorch/cuBLAS FP16 throughput remains around FP32-class performance.
- `nvdrv-backtrack-from-v100-baseline-2026-05-20.md`: current main report. Key result: decoded VBIOS init scripts on local capped/CMP-derived GV100 images write `NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT` at `0x00409664` with value `0x00000999`, setting IMLA/FMLA/DP reduced speed plus override before normal driver operation. The exact raw ROM byte anchors are `0x8212` on CMP and `0x8222` on local V100-personality / 266855 images.
- `runs/20260520-evidence-dag/gv100-limiter-evidence-dag.md`: read-only evidence DAG tying together the benchmark symptom, VBIOS init write, GV100 FECS/CTXSW field decode, blocked user-space readback path, and missing chipfuse/POR producer data.
- `runs/20260520-evidence-dag/gv100-vbios-main-init-tail-compare.md`: decoded VBIOS main-init tail comparison. Key result: capped/CMP-derived images execute the FECS `0x409664` set-reduced opcode in the selected main init script immediately before `DONE`; full-speed Tesla V100 / Quadro GV100 controls reach `DONE` at the corresponding point.
- `downstream-metrics-from-fecs-speed-select-2026-05-20.md`: downstream measurement matrix for the `0x999` FECS speed-select write. Key result: FMLA/HMMA impact is already measured; next useful read-only probes are DP/FP64 and IMLA/integer issue-rate tests plus targeted Nsight issue/stall metrics.
- `dp-imla-downstream-probe-2026-05-20.md`: read-only DP/IMLA follow-up. Key result: DP/FP64 also shows very low local throughput (`0.4368` TFLOP/s DGEMM on V100-personality, `0.3665` TFLOP/s on CMP) and scales with SM count; IMLA is inconclusive without a full-speed GV100 integer-control comparison.
- `sgemm-dgemm-ratio-probe-2026-05-20.md`: local SGEMM/DGEMM ratio sweep across 3 V100-personality and 6 CMP-labelled cards. Key result: FP32 GEMM is healthy while FP64 GEMM is only about `6.8-7.3%` of the FP32-implied full-speed V100 FP64 rate, consistent with the same `0x999` mask asserting `DP_REDUCED + DP_OVERRIDE`.
- `local-tensor-sgemm-sweep-2026-05-20.md`: local cuBLAS Tensor/SGEMM sweep across the same accessible card set. Key result: V100-personality and CMP-labelled cards have the same FP16 tensor throughput per SM (`~0.087` TFLOP/s/SM), so the V100 personality changes SM count but not the FMLA/HMMA cap class.
- `dp4a-imla-downstream-probe-2026-05-20.md`: targeted DP4A probe after source backtracking mapped later `IDP4A_S32_S8` to `IMLA3`. Key result: real `IDP.4A.S8.S8` instructions execute and do not show an HMMA/DP-class collapse, so IMLA remains unresolved rather than proven capped.
