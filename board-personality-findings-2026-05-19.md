# Board Personality Findings - 2026-05-19

This note captures the non-destructive board/personality comparison after tensor-path probing.

## Key Result

The V100-personality card is not just a different display name. It exposes a different PCI device/subdevice ID pair and 80 CUDA-visible SMs, while CMP cards expose 68 SMs.

The V100-personality change does **not** restore expected V100 tensor GEMM throughput in the tested host stack. It does, however, remove the CMP-specific Nsight Compute block: the CMP card returns `ERR_NVCMPGPU`, while the V100-personality card permits privileged tensor-pipe counter collection.

## Selected Pair

| Field | GPU 2 CMP baseline | GPU 14 V100-personality |
| --- | --- | --- |
| Bus ID | `00000000:07:00.0` | `00000000:19:00.0` |
| `nvidia-smi` name | `NVIDIA CMP 100-210` | `Tesla V100-PCIE-12GB` |
| PCI device ID | `0x1D8410DE` | `0x1DF410DE` |
| PCI subdevice ID | `0x12B910DE` | `0x12B810DE` |
| `lspci` ID | `10de:1d84` | `10de:1df4` |
| `lspci` subsystem | `10de:12b9` | `10de:12b8` |
| VBIOS | `88.00.9D.00.00` | `88.00.51.00.04` |
| InfoROM image | `G001.0000.01.04` | `G001.0000.01.04` |
| CUDA CC | `7.0` | `7.0` |
| CUDA-visible SMs | 68 | 80 |
| Power limit | 120 W current, 250 W default/max | 120 W current, 250 W default/max |
| Max SM clock | 1380 MHz | 1380 MHz |
| Max memory clock | 810 MHz | 810 MHz |

## Fleet Pattern

The CUDA attribute sweep with `CUDA_DEVICE_ORDER=PCI_BUS_ID` shows this split across the host:

- CMP 100-210 cards expose 68 SMs.
- Tesla V100-PCIE-12GB personality cards expose 80 SMs.

This means the previous owner’s V100 firmware/personality likely changed a real driver-visible enablement boundary, not just the product string.

## Tensor Findings Connected To Personality

- Both selected cards accept and run `sm_70` WMMA/HMMA code.
- The V100-personality card can be profiled and shows tensor-pipe instruction execution in Nsight Compute.
- The CMP card runs the same HMMA program but Nsight Compute refuses counters with `ERR_NVCMPGPU`.
- Sustained cuBLAS FP16 tensor-op throughput stays near FP32 control throughput:
  - GPU 2 CMP: 6.01 TFLOPS FP16 vs 6.02 TFLOPS FP32.
  - GPU 14 V100-personality: 7.05 TFLOPS FP16 vs 7.07 TFLOPS FP32.

## Current Interpretation

There appear to be at least two separate gates:

1. **SM exposure/personality gate:** CMP identity exposes 68 SMs; V100 personality exposes 80 SMs.
2. **Tensor throughput gate:** both personalities can issue tensor instructions, but neither produces normal V100 tensor GEMM throughput in this test.

The first gate looks tied to VBIOS/device/subdevice identity. The second gate is still unresolved and may be below the obvious PCI identity layer, inside driver policy, board firmware tables, or the hardware configuration itself.

## Captured Artifacts

Under `runs/20260519-173454-tensor-probe-gpu2-gpu14/`:

- `gpu-identity-power-clocks.csv`
- `lspci-nnvvv-07-00-0.txt`
- `lspci-nnvvv-19-00-0.txt`
- `lspci-config-xxxx-07-00-0.txt`
- `lspci-config-xxxx-19-00-0.txt`
- `gv100-linux-firmware-sha256.txt`
- `all-gpu-cuda-attrs-pcibus.txt`
- `summary.md`

