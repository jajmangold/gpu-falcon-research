# Inventory Snapshot - 2026-05-19

Host timezone: America/Chicago  
Snapshot time: `2026-05-19T17:27:01-05:00`  
Driver: `580.142`  
CUDA: `13.0`

## Selection Rationale

No GV100 card was completely idle. A shared `python3` process held small contexts on most cards. The selected pair had 0% GPU utilization and the lowest observed framebuffer use among comparable CMP/V100-personality cards:

- CMP candidate: GPU 2, bus `00000000:07:00.0`, 289 MiB used, 0% GPU util.
- V100-personality candidate: GPU 14, bus `00000000:19:00.0`, 329 MiB used, 0% GPU util.

Avoided for now:

- GPU 0: 97% GPU utilization.
- GPUs 1, 9, 12, 13, 15: materially higher resident memory.
- Other low-util cards: viable alternates, but selected pair has the cleanest low-memory CMP/V100 contrast.

## Full GPU Summary

```text
0, 00000000:05:00.0, NVIDIA CMP 100-210, GPU-f0641c73-e42a-2ba3-278a-3b4ad86c24a9, 16384 MiB, 8971 MiB used, 97% GPU util
1, 00000000:06:00.0, NVIDIA CMP 100-210, GPU-299f91bd-13ff-f86f-9a35-959c8efe6af6, 16384 MiB, 4785 MiB used, 0% GPU util
2, 00000000:07:00.0, NVIDIA CMP 100-210, GPU-4f0356c3-2160-54bd-6464-8b62eaf5b23c, 16384 MiB, 289 MiB used, 0% GPU util
3, 00000000:08:00.0, NVIDIA CMP 100-210, GPU-b23f051c-5548-21d4-4ca6-f2b1c4d3237e, 16384 MiB, 637 MiB used, 0% GPU util
4, 00000000:0B:00.0, Tesla V100-PCIE-12GB, GPU-c1b56a89-5620-3520-9ebf-63834f715a2f, 16384 MiB, 453 MiB used, 0% GPU util
5, 00000000:0C:00.0, NVIDIA CMP 100-210, GPU-68309a3d-8ef1-8660-8634-6953b027b34a, 16384 MiB, 637 MiB used, 0% GPU util
6, 00000000:0D:00.0, NVIDIA CMP 100-210, GPU-44c4fe09-a616-cf30-6a47-b86bba89f923, 16384 MiB, 637 MiB used, 0% GPU util
7, 00000000:0E:00.0, Tesla V100-PCIE-12GB, GPU-c0e49412-4443-a727-f0fd-b20611796a3d, 16384 MiB, 727 MiB used, 0% GPU util
8, 00000000:11:00.0, NVIDIA CMP 100-210, GPU-534081fb-0507-a383-481e-c994ad231ae9, 16384 MiB, 637 MiB used, 0% GPU util
9, 00000000:12:00.0, Tesla V100-PCIE-12GB, GPU-7540b3cb-86d0-be61-e72f-525f7e051444, 16384 MiB, 3353 MiB used, 0% GPU util
10, 00000000:13:00.0, NVIDIA CMP 100-210, GPU-75f8a273-a6fb-971d-5f04-48cc73cccdcd, 16384 MiB, 689 MiB used, 0% GPU util
11, 00000000:14:00.0, Tesla V100-PCIE-12GB, GPU-1abdcc29-1477-c2d3-885b-8286abb29cd7, 16384 MiB, 817 MiB used, 0% GPU util
12, 00000000:17:00.0, NVIDIA CMP 100-210, GPU-61318af7-6bcc-297f-acd8-c141e00aa18f, 16384 MiB, 3069 MiB used, 0% GPU util
13, 00000000:18:00.0, NVIDIA CMP 100-210, GPU-367c3947-d69d-2047-d72a-6f9959dfe85b, 16384 MiB, 14097 MiB used, 0% GPU util
14, 00000000:19:00.0, Tesla V100-PCIE-12GB, GPU-71be175a-2ae4-735d-78e1-6eeeea51fc53, 16384 MiB, 329 MiB used, 0% GPU util
15, 00000000:1A:00.0, NVIDIA CMP 100-210, GPU-01085ed5-3a1a-d9d9-4f2f-fdf5e34f323f, 16384 MiB, 6377 MiB used, 0% GPU util
16, 00000000:1B:00.0, Quadro K620, GPU-96985cbe-d079-cbbd-e0e5-2770406b0d63, 2048 MiB, 9 MiB used, 0% GPU util
```

## PCI Identity Notes

```text
07:00.0 3D controller [0302]: NVIDIA Corporation GV100 [CMP 100-210] [10de:1d84] (rev a1)
19:00.0 3D controller [0302]: NVIDIA Corporation GV100 [CMP 100-210] [10de:1df4] (rev a1)
```

`nvidia-smi` reports GPU 14 as `Tesla V100-PCIE-12GB`, while `lspci` still labels the PCI class text as GV100 CMP 100-210 with device ID `10de:1df4`.

## Candidate Detail

### GPU 2 - CMP Baseline

- Product name: `NVIDIA CMP 100-210`
- Bus ID: `00000000:07:00.0`
- UUID: `GPU-4f0356c3-2160-54bd-6464-8b62eaf5b23c`
- Serial: `0321818053610`
- VBIOS: `88.00.9D.00.00`
- FB memory: 16384 MiB total, 289 MiB used, 15856 MiB free
- BAR1 memory: 64 MiB total, 7 MiB used, 57 MiB free
- Compute mode: Default
- Persistence mode: Enabled
- Current power limit: 120 W
- Default/max power limit: 250 W

Observed resident compute app:

```text
00000000:07:00.0, GPU-4f0356c3-2160-54bd-6464-8b62eaf5b23c, 2024418, python3, 270 MiB
```

### GPU 14 - V100-Personality Comparator

- Product name: `Tesla V100-PCIE-12GB`
- Bus ID: `00000000:19:00.0`
- UUID: `GPU-71be175a-2ae4-735d-78e1-6eeeea51fc53`
- Serial: `1324819049869`
- VBIOS: `88.00.51.00.04`
- FB memory: 16384 MiB total, 329 MiB used, 15816 MiB free
- BAR1 memory: 64 MiB total, 7 MiB used, 57 MiB free
- Compute mode: Default
- Persistence mode: Enabled
- Current power limit: 120 W
- Default/max power limit: 250 W

Observed resident compute app:

```text
00000000:19:00.0, GPU-71be175a-2ae4-735d-78e1-6eeeea51fc53, 2024418, python3, 310 MiB
```

