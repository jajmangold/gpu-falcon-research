# Vast.ai V100 Debugdump Control - 2026-05-20

## Purpose

Rent a known full-speed V100 control and try to collect the same debugdump/identity artifacts used on local GPU14. The goal was to compare full-speed V100 InfoROM/VBIOS/RM protobuf contents against the local V100-personality card that is capped at `1/16` Tensor throughput.

Artifacts:

```text
runs/20260520-vast-v100-debugdump-control/
```

The Vast API key remains in the workspace root `.env`. The short-lived instance API key returned by Vast was redacted from saved artifacts.

## Instance

| Contract | Offer | GPU | Power limit | VBIOS | Result |
| ---: | ---: | --- | ---: | --- | --- |
| `37111614` | `19788333` | Tesla V100-SXM2-16GB | 300 W | `88.00.4F.00.09` | Destroyed |

A post-destroy account check showed zero remaining Vast instances.

## Benchmark Control

The same DeepBench-shaped Tensor Core probe produced full-speed V100 results:

| Shape | Vast V100-SXM2 TFLOPS | Public V100 TFLOPS | Ratio |
| --- | ---: | ---: | ---: |
| DeepBench-V100-FP16-row9 | 80.975 | 89.415 | 0.906 |
| DeepBench-V100-FP16-row14 | 81.397 | 98.030 | 0.830 |
| DeepBench-V100-FP16-row19 | 87.714 | 103.790 | 0.845 |
| DeepBench-V100-FP16-row24 | 93.949 | 112.276 | 0.837 |

This confirms the rented card was a usable full-speed control.

## Debugdump Attempt

`nvidia-debugdump` was present on the rented instance, but the provider/driver policy blocked capture-buffer reads:

```text
ERROR: GetCaptureBuffer failed, Insufficient Permissions
ERROR: internal_getDumpBuffer failed, return code: 0x4
ERROR: internal_dumpGpuComponent() failed, return code: 0x4
```

The ioctl/nvlog-only retry also failed:

```text
ERROR: internal_dumpNvLogComponent() failed, return code: 0x3e7
```

So this run did not produce a usable full-speed V100 `rm_*.pb` or InfoROM payload. The debugdump comparison remains blocked on a provider that allows the required RM capture-buffer access.

## Visible Identity Comparison

The available `nvidia-smi -q -x` and extended query data still show useful differences:

| Field | Local GPU14 V100-personality | Vast full-speed V100-SXM2 |
| --- | --- | --- |
| Product name | `Tesla V100-PCIE-12GB` | `Tesla V100-SXM2-16GB` |
| Product brand | `Quadro` | `Tesla` |
| PCI device ID | `1DF410DE` | `1DB110DE` |
| PCI subsystem ID | `12B810DE` | `121210DE` |
| VBIOS | `88.00.51.00.04` | `88.00.4F.00.09` |
| InfoROM image | `G001.0000.01.04` | `G503.0201.00.03` |
| ECC object | `N/A` | `5.0` |
| ECC mode | `N/A` | `Enabled` |
| BAR1 total | `64 MiB` | `16384 MiB` |
| Current/default/max power | `120/250/250 W` | `300/300/300 W` |
| Max SM clock | `1380 MHz` | `1530 MHz` |
| Max memory clock | `810 MHz` | `877 MHz` |

The strongest visible discriminator from this control is the board-management/InfoROM profile:

```text
Local GPU14:       InfoROM G001.0000.01.04, ECC N/A, BAR1 64 MiB, brand Quadro
Full-speed V100:   InfoROM G503.0201.00.03, ECC 5.0, BAR1 16 GiB, brand Tesla
```

That does not prove the tensor limiter is InfoROM-driven. It does show that the local V100-personality flash did not turn the card into a normal full-speed V100 at the board-management/NVML layer.

## Interpretation

This pass confirms two things:

1. The live Vast control is full-speed on the exact same benchmark binary and problem set.
2. A usable full-speed V100 debugdump requires a provider/environment that allows NVIDIA RM capture-buffer access.

The next useful control is either a self-administered full-speed V100 host, or a Vast/bare-metal provider where `nvidia-debugdump --dumpall` can collect GPU components without `Insufficient Permissions`.
