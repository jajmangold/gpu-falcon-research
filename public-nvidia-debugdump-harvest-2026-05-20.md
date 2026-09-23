# Public NVIDIA Debugdump Harvest - 2026-05-20

## Purpose

Search for public `nvidia-debugdump` archives that could provide a full-speed V100 control dump comparable to local GPU14's decrypted `rm_14.pb` and InfoROM payload.

Artifacts:

```text
runs/20260520-public-nvidia-debugdump-harvest/
```

Harvester:

```text
tools/harvest_public_nvidia_debugdumps.py
```

## Sources Checked

The harvester queried NVIDIA Developer Forum topic JSON and downloaded public attachment URLs from posts mentioning `nvidia-debugdump`, `dump.zip`, or V100 debug logs.

It downloaded 12 public artifacts:

```text
8 gzip log attachments
3 direct debugdump zip attachments
1 public tar.zst sequence containing many debugdump outputs
```

The direct debugdump zip attachments successfully decrypted/extracted with the local `nvdebugdump_decrypt_extract` tool:

| Public file | Decrypted files | Likely source GPU |
| --- | --- | --- |
| `7agAmtUnjhnzJweTgNriRQsTQ1u.zip` | `rm_00.pb`, `system_info.pb`, `error_data.pb`, `debug_buffers_00.pb`, `nvlog*` | GeForce RTX 3060 Ti forum case |
| `kbNLVfP11nzkApnq8B505H0RoWo.zip` | `rm_00.pb`, `system_info.pb`, `error_data.pb`, `debug_buffers_00.pb`, `nvlog.log` | GTX 750 Ti forum case |
| `p88gGndvlYoPhZ1WoGPBAy6F99i.zip` | `system_info.pb`, `error_data.pb`, `nvlog*` | GeForce RTX 3090 forum case |

The public `system-hang-seq.tar.zst` sequence extracted into many debugdump-like directories, but the GPU strings observed were RTX/GTX/4090 display-hang cases, not V100.

## V100-Specific Result

The search did find a public V100 forum attachment, but it was a `nvidia-bug-report.log.gz`, not a `nvidia-debugdump dump.zip`.

Downloaded artifact:

```text
downloads/9Or5Z1nZ3K6QPuzJ0St3tXIZf8v.gz
```

This log is still useful as a full-speed V100 NVML/board-management baseline. It reports four `Tesla V100-SXM2-16GB` cards:

```text
PCI device:        10de:1db1
Subsystem:         10de:1212
Product name:      Tesla V100-SXM2-16GB
Product brand:     Tesla
VBIOS:             88.00.13.00.02
GPU part number:   900-2G503-0100-000
InfoROM image:     G503.0201.00.03
OEM object:        1.1
ECC object:        5.0
BAR1 total:        16384 MiB
Power limit:       300 W
Max SM clock:      1530 MHz
Max memory clock:  877 MHz
```

This matches the Vast.ai full-speed V100-SXM2 control at the important board-management layer:

```text
InfoROM G503.0201.00.03
ECC object 5.0
BAR1 16384 MiB
300 W power profile
Tesla product brand
```

It differs strongly from local GPU14 V100-personality:

```text
InfoROM G001.0000.01.04
ECC object N/A
BAR1 64 MiB
120 W current power limit
Quadro product brand
```

## Interpretation

Public `nvidia-debugdump` archives do exist, and the local decryptor works on them. The useful full-speed V100 debugdump is still missing from this public harvest.

The public V100 `nvidia-bug-report.log.gz` nevertheless reinforces the Vast.ai control finding: normal full-speed V100-SXM2 cards expose a `G503` InfoROM/board-management profile with ECC object `5.0`, 16 GiB BAR1, Tesla brand, and 300 W power profile. Local V100-personality CMP cards keep a different board-management profile even after the visible PCI/VBIOS personality change.

The next public-data step is to keep expanding forum/GitHub search for actual V100 `dump.zip` attachments. The next decisive data step remains a full-speed V100 host where `nvidia-debugdump --dumpall` can collect `rm_*.pb`.
