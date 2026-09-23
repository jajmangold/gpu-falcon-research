#!/usr/bin/env python3
"""Build a read-only evidence DAG for the GV100 tensor limiter investigation.

The graph is deliberately descriptive. It records observations, source facts,
blocked paths, and remaining missing artifacts. It does not modify hardware,
firmware, or driver state.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass, asdict
from pathlib import Path


PROJECT = Path("/srv/nvme-data/containers/projects/gpu-falcon-research")


SET_REDUCED_PATTERN = bytes.fromhex(
    "6e 64 96 40 00 66 f6 ff ff 99 09 00 00"
)
NOOP_ZERO_PATTERN = bytes.fromhex(
    "6e 64 96 40 00 ff ff ff ff 00 00 00 00"
)


DEFAULT_ROMS = [
    PROJECT / "runs/20260520-bootrom-dump/card0-cmp-05.rom",
    PROJECT / "runs/20260520-bootrom-dump/card4-v100-0b.rom",
    PROJECT
    / "runs/20260519-173454-tensor-probe-gpu2-gpu14/online-vbios/266855-extracted-pcirom.rom",
    PROJECT
    / "runs/20260519-173454-tensor-probe-gpu2-gpu14/full-speed-control/NVIDIA.TeslaV100.16384.170728-extracted-pcirom.rom",
    PROJECT
    / "runs/20260519-173454-tensor-probe-gpu2-gpu14/temp-rom-inventory/extracted-pcirom/NVIDIA.TeslaV100.32768.180223-extracted-pcirom.rom",
    PROJECT
    / "runs/20260519-173454-tensor-probe-gpu2-gpu14/temp-rom-inventory/extracted-pcirom/NVIDIA.QuadroGV100.32768.180214-extracted-pcirom.rom",
    PROJECT
    / "runs/20260519-173454-tensor-probe-gpu2-gpu14/temp-rom-inventory/extracted-pcirom/NVIDIA.TitanV.12288.180904-extracted-pcirom.rom",
    PROJECT
    / "runs/20260519-173454-tensor-probe-gpu2-gpu14/temp-rom-inventory/extracted-pcirom/NVIDIA.TitanV.32768.180602-extracted-pcirom.rom",
]


@dataclass(frozen=True)
class Node:
    id: str
    label: str
    kind: str
    status: str
    detail: str
    citations: list[str]


@dataclass(frozen=True)
class Edge:
    src: str
    dst: str
    relation: str


def scan_pattern(data: bytes, pattern: bytes) -> list[int]:
    hits: list[int] = []
    off = data.find(pattern)
    while off != -1:
        hits.append(off)
        off = data.find(pattern, off + 1)
    return hits


def scan_roms(paths: list[Path]) -> dict[str, dict[str, list[str]]]:
    result: dict[str, dict[str, list[str]]] = {}
    for path in paths:
        if not path.exists():
            result[str(path)] = {"missing": ["true"]}
            continue
        data = path.read_bytes()
        reduced = [f"0x{x:06x}" for x in scan_pattern(data, SET_REDUCED_PATTERN)]
        noop = [f"0x{x:06x}" for x in scan_pattern(data, NOOP_ZERO_PATTERN)]
        result[str(path)] = {
            "set_reduced_999": reduced,
            "noop_zero": noop,
        }
    return result


def build_graph(scan: dict[str, dict[str, list[str]]]) -> tuple[list[Node], list[Edge]]:
    scan_lines = []
    for path, hits in scan.items():
        for name, offsets in hits.items():
            if offsets:
                scan_lines.append(f"{Path(path).name}: {name} at {', '.join(offsets)}")
    scan_detail = "; ".join(scan_lines) if scan_lines else "no pattern hits"

    nodes = [
        Node(
            "symptom_1_16",
            "Observed tensor throughput cap",
            "observation",
            "proven",
            "Local V100-personality GV100 cards execute HMMA/Tensor kernels but sustain exactly 6.25% of peak in Nsight Compute and roughly 1/13 to 1/16 of full-speed V100 GEMM throughput.",
            [
                "tensor-throughput-slowdown-diagnosis-2026-05-19.md",
                "hmma-issue-spacing-2026-05-20.md",
                "vastai-v100-control-2026-05-20.md",
            ],
        ),
        Node(
            "vbios_init_409664",
            "VBIOS init writes FECS speed-select override",
            "artifact",
            "proven",
            "Decoded init scripts write NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT at 0x00409664 with mask 0xfffff666 and value 0x00000999 on local capped/CMP-derived images.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "runs/20260520-bootrom-dump/card0-cmp-05-nvbios-vbu.txt",
                "runs/20260520-bootrom-dump/card4-v100-0b-nvbios-vbu.txt",
            ],
        ),
        Node(
            "main_init_tail",
            "Selected main init script tail contains the write",
            "artifact",
            "proven",
            "Decoded VBIOS output shows one selected main init script. Capped/CMP-derived images execute the FECS 0x409664 set-reduced opcode immediately before DONE; full-speed Tesla V100/Quadro GV100 controls reach DONE at the corresponding point.",
            [
                "runs/20260520-evidence-dag/gv100-vbios-main-init-tail-compare.md",
                "tools/gv100_vbios_init_tail_compare.py",
            ],
        ),
        Node(
            "raw_rom_pattern",
            "Raw ROM byte pattern location",
            "artifact",
            "proven",
            f"The opcode bytes for the set-reduced write are 6e 64 96 40 00 66 f6 ff ff 99 09 00 00. Scan result: {scan_detail}.",
            [
                "runs/20260520-bootrom-dump/card0-cmp-05.rom",
                "runs/20260520-bootrom-dump/card4-v100-0b.rom",
                "runs/20260519-173454-tensor-probe-gpu2-gpu14/online-vbios/266855-extracted-pcirom.rom",
            ],
        ),
        Node(
            "fullspeed_controls_no_set",
            "Full-speed controls do not set reduced here",
            "artifact",
            "proven",
            "Full-speed Tesla V100 32GB and Quadro GV100 decoded main init scripts reach DONE at 0x8222 where local V100-personality / 266855 images instead contain the 13-byte FECS set-reduced opcode followed by DONE at 0x822f. Full-speed Tesla V100 16GB does not contain the set-reduced pattern either.",
            [
                "runs/20260519-173454-tensor-probe-gpu2-gpu14/temp-rom-inventory/nvbios-selected/NVIDIA.TeslaV100.32768.180223-extracted-pcirom.txt",
                "runs/20260519-173454-tensor-probe-gpu2-gpu14/temp-rom-inventory/nvbios-selected/NVIDIA.QuadroGV100.32768.180214-extracted-pcirom.txt",
                "runs/20260519-173454-tensor-probe-gpu2-gpu14/full-speed-control/v100-control-nvbios-vbu.txt",
            ],
        ),
        Node(
            "field_decode_999",
            "0x999 decodes to reduced plus override",
            "decode",
            "proven",
            "On GV100, value 0x999 sets IMLA reduced+override, FMLA reduced+override, and DP reduced+override. FMLA is the tensor/HMMA-relevant bit family.",
            [
                "gv100-fuse-layout-source-reconstruction-2026-05-20.md",
                "tools/gv100_speed_select_decode.py",
            ],
        ),
        Node(
            "fmla_downstream_uniform",
            "FMLA/HMMA cap is uniform per SM locally",
            "observation",
            "proven",
            "A local cuBLAS Tensor/SGEMM sweep across 3 V100-personality and 6 CMP-labelled containers shows nearly identical FP16 tensor throughput per SM: about 0.087 TFLOP/s/SM for both groups. V100 personality changes total throughput via SM count, not the FMLA/HMMA issue class.",
            [
                "local-tensor-sgemm-sweep-2026-05-20.md",
                "runs/20260520-tensor-sgemm-sweep/",
            ],
        ),
        Node(
            "dp_downstream_uniform",
            "DP/FP64 cap is uniform locally",
            "observation",
            "proven",
            "A local SGEMM/DGEMM ratio sweep across the same card set shows FP64 throughput is only about 6.8-7.3% of the FP32-implied full-speed V100 FP64 rate. This is a downstream signature for DP_REDUCED plus DP_OVERRIDE.",
            [
                "sgemm-dgemm-ratio-probe-2026-05-20.md",
                "runs/20260520-sgemm-dgemm-ratio/",
            ],
        ),
        Node(
            "imla_dp4a_unresolved",
            "DP4A does not show IMLA-class collapse",
            "observation",
            "partial",
            "Later MODS maps IDP4A_S32_S8 to IMLA3, so a new DP4A probe is a closer IMLA-family test than generic IMAD. It emits real IDP.4A.S8.S8 SASS but does not show an HMMA/DP-class collapse locally; GV100 IMLA remains unresolved.",
            [
                "dp4a-imla-downstream-probe-2026-05-20.md",
                "runs/20260520-dp4a-imla-probe/",
            ],
        ),
        Node(
            "gv100_binary_state",
            "GV100 exposes binary speed-select state",
            "source",
            "proven",
            "GV100 hwref/ctxsw exposes FULL_SPEED versus REDUCED_SPEED for FMLA/IMLA/DP, not an explicit 3-bit divisor field.",
            [
                "gv100-fuse-layout-source-reconstruction-2026-05-20.md",
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
            ],
        ),
        Node(
            "later_explicit_divisor",
            "Later chips expose explicit 1/16 selector",
            "source",
            "proven",
            "Turing/Ampere/later RM and fuse definitions expose SM issue-rate modifiers including FMLA16_REDUCED_SPEED_1_16.",
            ["nvdrv-backtrack-from-v100-baseline-2026-05-20.md"],
        ),
        Node(
            "runtime_read_blocked",
            "Unprivileged production user-space readback blocked",
            "blocked-path",
            "proven",
            "NV2080_CTRL_CMD_GPU_EXEC_REG_OPS rejects 0x409660 and 0x409664 as INVALID_OFFSET for an unprivileged caller because the GV100 generated user access map omits FECS speed-select registers.",
            [
                "tools/gv100_fecs_speed_select_local_regaccess_optin.md",
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
            ],
        ),
        Node(
            "privileged_rm_regops_readback",
            "Privileged RM regops can read FECS speed-select",
            "observation",
            "proven",
            "After adding a read-only NV0000_CTRL_CMD_SYSTEM_GET_PRIVILEGED_STATUS query, normal user runs show priv_user=0 and regStatus=0x04 for 0x409660/0x409664. sudo runs show priv_user=1/priv_handle=1 and read live FECS values: CMP-labelled cards report FEATURE_READOUT=0x007000f3 and SM_SPEED_SELECT=0x00000999; V100-personality cards report FEATURE_READOUT=0x00700100 and SM_SPEED_SELECT=0x00000999.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "tools/rm_sm_issue_rate_probe.c",
                "runs/20260520-rm-regops-privilege/rm-probe-all-sudo-summary.txt",
            ],
        ),
        Node(
            "cve_surface_lens",
            "CVE lens added per surface",
            "security-context",
            "active",
            "Each register/API/firmware surface is now checked against relevant NVIDIA CVE classes before it is treated as a possible path. The highest-priority current class is May 2026 R580 Linux driver access-control/resource CVEs because the host runs driver 580.142, below NVIDIA's listed Tesla R580 Linux fixed version 580.159.03. This is a triage lens, not an exploit chain.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "https://nvidia.custhelp.com/app/answers/detail/a_id/5821",
                "https://nvidia.custhelp.com/app/answers/detail/a_id/5263",
                "https://docs.nvidia.com/datacenter/cloud-native/gpu-operator/25.3.3/security.html",
            ],
        ),
        Node(
            "legacy_mmap_physmem_cves",
            "Legacy arbitrary physical-memory CVEs map to mmap class",
            "security-context",
            "triage",
            "CVE-2016-7382 and CVE-2016-7389 are historical matches for missing-permission or mmap-validation bugs that allowed arbitrary physical memory access. The current host R580.142 open kernel module has explicit mmap-context, offset, size, write-protection, and safe-to-mmap checks, so these CVEs are class references rather than direct version matches.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "/usr/src/nvidia-580.142/nvidia/nv-mmap.c",
                "https://nvd.nist.gov/vuln/detail/CVE-2016-7382",
                "https://nvd.nist.gov/vuln/detail/CVE-2016-7389",
            ],
        ),
        Node(
            "cve_2017_0352_sensitive_regs",
            "CVE-2017-0352 matches sensitive-register access-control class",
            "security-context",
            "triage",
            "CVE-2017-0352 describes GPU firmware incorrect access control allowing CPU software to access sensitive GPU control registers. This is conceptually close to FECS/PRI/PLM, but NVIDIA fixed it in old R375/R381-era branches and no direct R580/GV100 limiter exploit path has been found.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "https://nvidia.custhelp.com/app/answers/detail/a_id/4462",
                "https://nvd.nist.gov/vuln/detail/CVE-2017-0352",
            ],
        ),
        Node(
            "rm_vgpu_cve_factcheck",
            "Revised RM/vGPU CVE list fact-checked",
            "security-context",
            "triage",
            "CVE-2022-34669 and CVE-2022-31606 are Windows-side in NVIDIA's bulletins, not Linux vGPU escape evidence for this host. CVE-2021-1056 is a real Linux device-file isolation bug class, not a context-state scrub claim. CVE-2025-23282 is a real Linux race-escalation/vGPU-affected class; local source shows the current NV_ESC_ATTACH_GPUS_TO_FD path is lock-hardened compared with the older nvdrv snippet. CVE-2024-0126 is a real privileged attacker input-validation / vGPU Manager class. None establishes FECS 0x409664 control.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "/usr/src/nvidia-580.142/nvidia/nv.c",
                "integ/gpu_drv/stage_rel/drivers/resman/arch/nvalloc/unix/Linux/nv.c",
                "https://nvidia.custhelp.com/app/answers/detail/a_id/5703",
                "https://nvidia.custhelp.com/app/answers/detail/a_id/5415",
                "https://nvidia.custhelp.com/app/answers/detail/a_id/5383",
                "https://nvidia.custhelp.com/app/answers/detail/a_id/5586",
            ],
        ),
        Node(
            "sbr_pub_plm_access_model",
            "SBR/PUB docs explain firmware-mediated PLM access",
            "source",
            "proven",
            "SBR/PUB safety docs describe a PLM Update Binary running on SEC2 Falcon that lowers selected PLMs so PMU and CTXSW/FECS/GPCCS microcodes can access protected registers, sets decode traps for selected devtools visibility, accepts no external arguments, and only supports debug signed PUB on debug boards.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "integ/gpu_drv/stage_rel/safety/SBR-TU10A/SWE-SBR-007-SWADS.md",
                "integ/gpu_drv/stage_rel/safety/SBR-TU10A/Unit_Design/SWE-SBR-009-SWUD-LITE.md",
            ],
        ),
        Node(
            "missing_chipfuse_por",
            "Missing GV100 chipfuse/POR producer data",
            "missing-artifact",
            "blocked",
            "The checkout references and can parse gv100_f.json/gv100_f.jsone and gv100_POR.json, but those artifacts are absent locally. They are the highest-value visible-format place for SKU/IFF rows that could select the reduced state.",
            [
                "nvdrv-falcon-fuse-notes-2026-05-20.md",
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
            ],
        ),
        Node(
            "json_consumer_schema",
            "MODS fuse JSON consumer schema reconstructed",
            "source",
            "proven",
            "The generic FuseJsonParser consumes _f.json/_f.jsone plus GV100+ _POR.json/_POR.jsone files. The main JSON requires header/options/Chip_options/Map_options/Mkt_options/Fuse_encoding and optionally consumes IFF_patches, Bootrom_patches, Fuse_macro, Sku_ids, Slt_data, and Por_data. _POR.json contains a top-level Por_data section.",
            [
                "gv100-fuse-layout-source-reconstruction-2026-05-20.md",
                "integ/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp",
                "integ/gpu_drv/stage_rel/diag/mods/core/include/fusejsonparser.h",
            ],
        ),
        Node(
            "bootrom_patches_candidate",
            "Bootrom_patches schema can carry SKU byte patch rows",
            "source",
            "inferred",
            "FuseJsonParser parses optional Bootrom_patches by SKU and revision. Each revision has a sequence number and rows containing address/value pairs. This is the right visible schema shape for byte-local patch intent, but visible BRRevs/BRPatches runtime consumers found so far are Tegra-side. No dGPU GV100 consumer has been found that turns Bootrom_patches into the observed 13-byte VBIOS init-script delta.",
            [
                "gv100-fuse-layout-source-reconstruction-2026-05-20.md",
                "integ/gpu_drv/stage_rel/diag/mods/core/utility/fusejsonparser.cpp",
                "integ/gpu_drv/stage_rel/diag/mods/core/utility/fuseparser.cpp",
            ],
        ),
        Node(
            "init_opcode_decoder",
            "VBIOS opcode 0x6e is a standard MMIO_MASK write",
            "source",
            "proven",
            "The local envytools nvbios decoder treats opcode 0x6e as a 13-byte operation: 32-bit address, 32-bit AND mask, 32-bit OR value. The capped byte sequence therefore decodes cleanly as MMIO_MASK 0x00409664 &= 0xfffff666 |= 0x00000999, not as an opaque blob.",
            [
                "gv100-fuse-layout-source-reconstruction-2026-05-20.md",
                "projects/gpu-falcon-research/tools/envytools/nvbios/print.c",
            ],
        ),
        Node(
            "public_search_no_gv100_json",
            "Public search did not find GV100 chipfuse/POR JSON data",
            "missing-artifact",
            "blocked",
            "Exact public searches for gv100_f.json/gv100_f.jsone/gv100_POR.json/gv100_POR.jsone and FuseJsonParser datasets did not surface the missing GV100 producer files. A related public GA100/CMP 170HX gist reinforces the later-generation 3-bit SM speed-select model but does not provide the GV100 producer.",
            [
                "gv100-fuse-layout-source-reconstruction-2026-05-20.md",
                "https://gist.github.com/duggasco/a901279c3e0c9038f789f34badf8992d",
            ],
        ),
        Node(
            "sim_rom_negative_control",
            "MODS sim/emu ROM corpus lacks production tail signature",
            "evidence",
            "proven",
            "A scan of 3168 local MODS sim/emu ROM files, representing 2367 unique non-empty SHA-256 values, found zero capped tail signatures, zero explicit full-speed zero-write signatures, and zero full-speed direct-DONE production tail signatures. The sim ROM corpus is therefore a negative control rather than the missing CMP-derived production VBIOS producer.",
            [
                "gv100-fuse-layout-source-reconstruction-2026-05-20.md",
                "runs/20260520-evidence-dag/gv100-sim-rom-signature-scan.md",
            ],
        ),
        Node(
            "initial_scope_non_destructive",
            "Investigation scope started non-destructive",
            "source",
            "proven",
            "The initial GPU2/GPU14 pair was selected for read-only inventory, benchmark, and source comparison. This scope decision is important context for the DAG: evidence collection may include ROM/debugdump/register reads, but the graph is not an unlock or exploit recipe.",
            [
                "inventory-2026-05-19.md",
                "know_96419584e40948ca989de69953192d86",
            ],
        ),
        Node(
            "board_personality_split",
            "Board personality changes identity and SM count",
            "observation",
            "proven",
            "CMP baseline cards expose PCI 10de:1d84, 68 SMs, and CMP-labelled identity; V100-personality cards expose PCI 10de:1df4, 80 SMs, and allow Nsight tensor-pipe counters. The tensor limiter persists across both groups, so SM exposure/personality and tensor issue-rate are separate gates.",
            [
                "board-personality-findings-2026-05-19.md",
                "know_e5b522b669df4a52b5e7566eefbf34b1",
            ],
        ),
        Node(
            "sm_count_floorsweep_masks",
            "SM count is RM-visible GPC/TPC floorsweeping",
            "evidence",
            "proven",
            "A read-only RM probe over local GV100 cards shows the 68-vs-80 SM split directly in GPC/TPC floorsweep masks. CMP-identity cards expose 34 enabled TPCs, hence 68 SMs at 2 SM/TPC. V100-personality cards expose 40 enabled TPCs, hence 80 SMs. The relevant RM controls are GET_GPC_MASK and GET_TPC_MASK, backed by GV100 STATUS_OPT_GPC and STATUS_OPT_TPC_GPC(i) fuse/status masks.",
            [
                "runs/20260520-rm-gr-floorsweep/rm-gr-floorsweep-summary.md",
                "tools/rm_gr_floorsweep_probe.c",
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
            ],
        ),
        Node(
            "visible_identity_not_enough",
            "Visible V100 identity is not enough",
            "observation",
            "proven",
            "V100-personality cards report V100-visible PCI/device fields and 80 SMs, but still retain CMP-like board-management traits such as InfoROM G001, ECC N/A, BAR1 64 MiB, and low current power limit. Power sampling ruled out the 120 W cap as the primary 1/16 tensor gate.",
            [
                "visible-identity-diff-2026-05-20.md",
                "tensor-throughput-slowdown-diagnosis-2026-05-19.md",
                "know_dc61317b35ef4f2599394c9041f334f4",
            ],
        ),
        Node(
            "v100_flash_personality_not_speed_gate",
            "V100 flash crossed personality gate but not speed gate",
            "analysis",
            "proven",
            "The prior V100-personality flash changed real driver-visible state: PCI identity, product name, SM count, and Nsight tensor-counter access. It did not remove the FECS speed-select policy, because local V100-personality images still contain the selected-main-init 0x409664 set-reduced write while full-speed Tesla V100/Quadro GV100 controls do not.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "board-personality-findings-2026-05-19.md",
                "visible-identity-diff-2026-05-20.md",
                "know_c0d9ae23110a403a8fc6ad74c5ea069f",
            ],
        ),
        Node(
            "public_deepbench_control",
            "Public DeepBench V100 control is full speed",
            "observation",
            "proven",
            "Baidu DeepBench public V100 results on four exact FP16 mixed-math GEMM shapes are roughly 89-112 TFLOP/s, while local V100-personality and CMP cards are roughly 5-7 TFLOP/s on the same shapes.",
            [
                "deepbench-public-v100-compare-2026-05-19.md",
                "know_0557470d985a459f83af3e4552bc220f",
            ],
        ),
        Node(
            "vast_fullspeed_control",
            "Vast.ai V100 controls reached normal throughput",
            "observation",
            "proven",
            "Remote full-speed Tesla V100 controls reached about 80-94 TFLOP/s on the same probe, confirming that the benchmark path can reach normal V100 Tensor throughput when the limiter is absent.",
            [
                "vastai-v100-control-2026-05-20.md",
                "vastai-v100-debugdump-control-2026-05-20.md",
                "know_df6b582dceb1459983cb67b8b83032ac",
                "know_62d1e2afa6c34cacafdc072433f97bd5",
            ],
        ),
        Node(
            "hmma_issue_spacing_control",
            "HMMA microbench confirms issue-rate class slowdown",
            "observation",
            "proven",
            "The custom WMMA/HMMA issue-spacing microbenchmark emitted HMMA.884.F32.F32 and showed local cards at about 3400-3700 cycles per WMMA op versus about 236-243 cycles on a full-speed V100 control, matching a 14-16x issue-rate slowdown.",
            [
                "hmma-issue-spacing-2026-05-20.md",
                "runs/20260520-hmma-issue-spacing/",
                "know_ce835b78cd614814a597aec72cda611b",
            ],
        ),
        Node(
            "hmma_trace_mode_saturated",
            "Trace-mode HMMA profiling returns to capped cadence under saturation",
            "observation",
            "proven",
            "A trace-mode WMMA/HMMA probe emits real HMMA.884 SASS and records per-group clock64 deltas. Short low-residency runs show sub-1k-cycle bursts, but PCI-ordered 4096-block saturated runs return to the known capped regime: about 3366-3444 cycles/WMMA on local V100-personality and about 3757-3789 cycles/WMMA on CMP. This supports a per-SM scheduler/token-budget model rather than a one-time startup delay.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "runs/20260520-hmma-trace-mode/",
                "runs/20260520-hmma-trace-mode/pci-saturated-summary.csv",
                "know_3b92b1c3c6314ef6bf5ae3b7815e451d",
            ],
        ),
        Node(
            "hmma_burst_window_shapeable",
            "HMMA burst window is shapeable but not an unlock",
            "observation",
            "proven",
            "A CUDA launch-shape sweep across GPU0 CMP and GPU14 V100-personality shows low-residency resident CTAs can preserve about 512 cycles/WMMA, but those cases underfill the GPU. Useful throughput still plateaus in the known capped range: about 4.69 TFLOPS on CMP and about 5.36 TFLOPS on V100-personality. Launch shaping can improve small-kernel latency or find a local sweet spot, but it does not recover full V100 Tensor throughput.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "runs/20260520-hmma-burst-window/summary.csv",
                "runs/20260520-hmma-burst-window/burst-window-analysis.md",
                "know_e813d210a8634a559a1444c7b304b8e5",
            ],
        ),
        Node(
            "z3_mechanism_filter",
            "Z3 filters candidate HMMA limiter mechanisms",
            "analysis",
            "proven",
            "A Z3 consistency model encodes full-speed V100 cycles, local low-residency burst cycles, saturated local resident timing, and local throughput plateau. The shared token/budget model is satisfiable with n=16. Strict per-warp modulo, active-lane-only, global clock throttle, and pure CTA queueing-only models are unsatisfiable under these observations.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "tools/gv100_hmma_mechanism_z3.py",
                "runs/20260520-z3-hmma-mechanism/hmma-mechanism-z3-results.md",
                "runs/20260520-z3-hmma-mechanism/hmma-mechanism-z3-results.json",
                "know_1818d3dd2c4a4fdf9176ad2ba683f158",
            ],
        ),
        Node(
            "hmma_concurrent_budget_conserved",
            "Concurrent HMMA streams conserve one capped issue budget",
            "observation",
            "proven",
            "A concurrent-stream HMMA probe compares one kernel against two identical kernels in nonblocking streams. Underfilled launches up to 128 blocks/kernel scale almost exactly 2x, proving streams overlap. Saturated launches at 256+ blocks/kernel stop scaling: two kernels conserve the same capped total throughput, with each receiving roughly half the budget. This is the strongest behavioral evidence for a shared HMMA/Tensor issue budget rather than per-kernel quotas or stream serialization.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "scripts/hmma_concurrent_stream_probe.cu",
                "runs/20260520-hmma-concurrent-budget/summary.csv",
                "runs/20260520-hmma-concurrent-budget/concurrent-budget-analysis.md",
                "know_9721fe6f052d4cdca2bd8f13e4bc0bd2",
            ],
        ),
        Node(
            "fresh_rom_metadata_only",
            "Same-family ROM diffs are metadata-sized",
            "artifact",
            "proven",
            "Fresh local V100-vs-control and fresh-CMP-vs-old-CMP ROM comparisons reduced many earlier VBIOS differences to a handful of bytes in OBD/serial-like metadata. This weakens broad table-diff theories and makes the selected init-script write a more specific signal.",
            [
                "rom-dump-and-vbios-diff-status-2026-05-19.md",
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "know_ff5ee32aaf034a55bf243997b5f63660",
            ],
        ),
        Node(
            "bootrom_dump_succeeded",
            "PROM/VBIOS dump succeeded locally",
            "artifact",
            "proven",
            "Local PROM dumping produced 1 MiB card ROM images for CMP and V100-personality cards, enabling the raw pattern scan, selected init-script tail comparison, and offline VBIOS analysis in this DAG.",
            [
                "runs/20260520-bootrom-dump/",
                "rom-dump-and-vbios-diff-status-2026-05-19.md",
                "know_b8068a9d45954fb7abe57ade287dfab1",
            ],
        ),
        Node(
            "m_table_negative",
            "M-table and stale Falcon-table leads weakened",
            "evidence",
            "proven",
            "Expanded GV100 ROM controls made M_RESTRICT/M_TYPE/M_UNK0D byte-identical between a full-speed V100 32GB control and 266855, and the old FALCON_UCODE/BIT pointer lead landed inside EFI payload bytes. These are negative controls against earlier broad VBIOS-table hypotheses.",
            [
                "full-speed-v100-control-validation-2026-05-19.md",
                "runs/20260519-173454-tensor-probe-gpu2-gpu14/offline-vbios-compare.md",
                "know_6584f943b13b440fb5b7051383e9913e",
                "know_70812617973748dbb07c6e06c77ecba5",
            ],
        ),
        Node(
            "pstraps_negative",
            "PSTRAPS remains unproven for tensor limiter",
            "blocked-path",
            "partial",
            "PSTRAPS is a real identity/strap override mechanism, but documented fields cover board identity and memory/PCI properties rather than a Tensor issue-rate divider. Local evidence shows V100 identity can be visible while the tensor cap persists.",
            [
                "pstraps-override-assessment-2026-05-20.md",
                "know_c54aa091d90c48d0b18e3f874b414191",
            ],
        ),
        Node(
            "fecs_live_readbacks",
            "Bare-metal FECS readbacks show stable 0x999",
            "observation",
            "proven",
            "After reboot, all 16 local GV100-class cards returned live FECS values. Every initialized card read FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT 0x409664 as 0x00000999; CMP readout 0x409660 was 0x007000f3 and V100-personality readout was 0x00700100.",
            [
                "runs/20260520-fecs-plm-readonly/gv100-fecs-plm-readonly-after-reboot-sudo.txt",
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "know_11f07f934a6d4b0095cffa1a42526443",
            ],
        ),
        Node(
            "gpu14_transient_not_unlock",
            "GPU14 zero override was transient, not unlock",
            "observation",
            "proven",
            "A pre-reboot GPU14 state briefly showed 0x409664 cleared, but after clean reboot GPU14 again read 0x999 and measured about 7 TFLOP/s FP16 Tensor throughput. The transient state did not prove a persistent or effective unlock.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "runs/20260520-gpu14-current-state/gpu14-cublas-tensor-after-reboot-docker-8192.json",
                "know_11f07f934a6d4b0095cffa1a42526443",
            ],
        ),
        Node(
            "dynamic_fecs_static",
            "FECS state stays static during Tensor GEMM",
            "observation",
            "proven",
            "Dynamic read-only sampling during active cuBLAS Tensor GEMM showed FECS debug/status, readout, and 0x409664 values remain stable. The speed-select surface behaves like configuration/readout state, not a live performance counter.",
            [
                "runs/20260520-falcon-cve2021-readonly/dynamic/",
                "know_6fc2c5e31dd14174949b5595889c1fa1",
            ],
        ),
        Node(
            "rm_issue_rate_stub",
            "RM public issue-rate control stubs GV100",
            "source",
            "proven",
            "The named RM issue-rate control returns NV_ERR_NOT_SUPPORTED on local GV100 and the RM HAL dispatch maps GET_SM_ISSUE_RATE_MODIFIER to TU102/GA100 while stubbing pre_TURING. This blocks a normal GV100 production readback of the exact divisor.",
            [
                "tools/rm_sm_issue_rate_probe.c",
                "gv100-fuse-layout-source-reconstruction-2026-05-20.md",
                "know_dfc5747973ef4ce09de36e209c97c160",
                "know_82671cddf22f4d70b66ffd3b89daf6f5",
            ],
        ),
        Node(
            "mods_no_gv100_override",
            "MODS issue-rate override skips GV100",
            "source",
            "proven",
            "MODS implements issue-rate read/override for Turing and Ampere, but GV100/Volta classes do not override the base unsupported methods. The feature name appears as Volta-953 in later code but not as a normal GV100 path.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "know_82671cddf22f4d70b66ffd3b89daf6f5",
            ],
        ),
        Node(
            "rm_policy_hooks_internal",
            "RM has internal speed-select policy hooks",
            "source",
            "proven",
            "RMOverrideSmSpeedSelect/RMOverrideSmSpeedSelect1 and RMSchMicroSched show that NVIDIA's source has speed-select and HMMA scheduler slowdown policy hooks. The visible apply paths are verification/internal or Turing/Ampere/later, not a production GV100 switch.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "know_74b5b078871f42f2a83e3c8109502c9d",
            ],
        ),
        Node(
            "cmp_sku_detection",
            "GV100 CMP SKU flag is fuse-derived",
            "source",
            "proven",
            "gpuGetIsCmpSku_GV100 reads two PCIe boot-disable fuse options and exports CMP state to RM/CUDA tooling. Found consumers mainly report CMP state or disable debug/devtools; they did not directly set the Tensor issue-rate cap in visible source.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "know_74b5b078871f42f2a83e3c8109502c9d",
            ],
        ),
        Node(
            "ctxsw_generated_surface",
            "Generated CTXSW state-store includes speed-select",
            "source",
            "proven",
            "Generated GV100 NETD context-state files include NET24_NV_PGRAPH_PRI_FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT, proving 0x409664 is a generated GV100 FECS context-state surface, not just an isolated hwref manual define.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "know_d2c05a25ff944b00ad9fb09c3d4fa381",
                "know_66cb979ded1f44f688e3a54a948b5f25",
            ],
        ),
        Node(
            "local_access_map_optin",
            "Local access-map opt-in is a measurement route",
            "blocked-path",
            "inferred",
            "A locally patched/generated RM access map can expose read-only FECS speed-select registers for measurement, but checked-in production maps omit them and verification-only controls are not available in normal builds. This is a diagnostic route, not evidence of a production unlock.",
            [
                "tools/gv100_fecs_speed_select_local_regaccess_optin.md",
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "know_66cb979ded1f44f688e3a54a948b5f25",
            ],
        ),
        Node(
            "plm_uniform",
            "Feature override PLM is uniform across local cards",
            "observation",
            "proven",
            "Read-only PLM probes show FECS_FEATURE_OVERRIDE_PLM 0x409650 and related Falcon PLMs are uniform across CMP and V100-personality cards. Live PLM state does not explain a per-personality unlock difference.",
            [
                "runs/20260520-fecs-plm-readonly/gv100-fecs-plm-readonly-after-reboot-sudo.txt",
                "runs/20260520-falcon-cve2021-readonly/gv100-fecs-falcon-plm-after-reboot-sudo.txt",
                "know_ff519e1d593d420f89e53069862a57eb",
                "know_df00cdeaff4a419cac76421b6888a47a",
            ],
        ),
        Node(
            "pub_plm_reconstructed",
            "PUB PLM reconstruction did not expose feature PLM producer",
            "source",
            "partial",
            "CSV/access-map reconstruction links 0x409664 to the nearby 0x409650 PLM candidate, but did not find a checked-in source producer for the feature-override PLM. The likely source remains fuse/manual policy or generated/private metadata.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "runs/20260520-pub-plm-candidate-reconstruction/",
                "know_737df59c567a49aea49c84f61d0d50dc",
                "know_278002e478b94978b0c9462bfa2100b7",
            ],
        ),
        Node(
            "sbr_pub_secure_boundary",
            "SBR/PUB docs place debug below secure boundary",
            "source",
            "proven",
            "SBR/PUB safety docs describe SEC2/PUB BootROM verification, signed HS code, PLM lowering, and halt/reset behavior on signature failure. This supports a secure Falcon boundary around the missing producer, not a normal host-side control.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "know_070a95abcd12455e854599aabf271158",
                "know_1938c30f859a4435889a7a178e25dd74",
            ],
        ),
        Node(
            "falcon_cve_readonly",
            "Falcon CVE class helps observability, not unlock",
            "blocked-path",
            "partial",
            "The 2021 NVIDIA internal-microcontroller CVE cluster maps to Falcon debug/scrub/microcode-loading concepts. Local use so far is read-only observability of debug/status registers; no usable GV100 path to alter FECS speed-select was found.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "know_b5ad13aee487446abc301782a91e6a30",
                "know_ff519e1d593d420f89e53069862a57eb",
            ],
        ),
        Node(
            "debugdump_not_pfuse",
            "Debugdump is not a raw PFUSE dump",
            "evidence",
            "proven",
            "Decrypted local nvidia-debugdump data contains RM protobuf/register blocks and InfoROM payloads, but targeted parsing found no blocks covering the known GV100 speed-select PFUSE offsets. Debugdump did expose CMP strings in InfoROM/OBD, but not a direct limiter fuse table.",
            [
                "gv100-fuse-layout-source-reconstruction-2026-05-20.md",
                "runs/20260520-rm-speed-select/decrypted/",
                "know_7a4b587f25d348e2bf0430dc8b6312f4",
            ],
        ),
        Node(
            "fresh_debugdump_runtime_residue",
            "Fresh debugdump shows runtime InfoROM/CMP residue",
            "evidence",
            "proven",
            "Fresh root nvidia-debugdump captures after reboot succeeded for GPU0 CMP and GPU4 V100-personality. Decrypted RM protobufs expose InfoROM-style OBD/OEM/IMG/ROM and CMP strings on both cards, including the V100-personality card. Literal FECS addresses 0x00409660/0x00409664 appear in nvlog binary payloads, but not as decoded rm_*.pb register fields or values.",
            [
                "runs/20260520-debugdump-refresh/debugdump-refresh-compare.md",
                "tools/gv100_debugdump_compare.py",
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "know_42c9dc49dc204685b0f8b190cdc1fde2",
            ],
        ),
        Node(
            "fullspeed_debugdump_missing",
            "Full-speed V100 debugdump comparator remains missing",
            "missing-artifact",
            "blocked",
            "Public and Vast.ai attempts produced full-speed performance/NVML controls but not a usable full-speed V100 rm_*.pb debugdump. Provider/debugdump policy blocked the strongest direct comparison.",
            [
                "public-nvidia-debugdump-harvest-2026-05-20.md",
                "vastai-v100-debugdump-control-2026-05-20.md",
                "know_89f3f203a5464a4f9456f481490f14f4",
                "know_62d1e2afa6c34cacafdc072433f97bd5",
            ],
        ),
        Node(
            "nvspec_negative",
            "NVSPEC board JSONs are not chipfuse data",
            "evidence",
            "proven",
            "Local pg506/pg509 nvspec JSONs contain board properties such as project/SKU, device ID, ECC, memory, TPC count, BAR size, and PCI class, but not GV100 chipfuse IFF/SKU/perf policy rows.",
            [
                "nvdrv-falcon-fuse-notes-2026-05-20.md",
                "know_045e9b4f7bc348568d77b36ead074370",
            ],
        ),
        Node(
            "chipfuse_search_negative",
            "Broader local/archive/web chipfuse search was negative",
            "missing-artifact",
            "blocked",
            "Searches across local trees, archives, and exact web queries did not find gv100_f.json, gv100_f.xml, gv100_f.jsone, gv100_POR, or a public chipfuse dataset. Only parser/source references and board specs were found.",
            [
                "nvdrv-falcon-fuse-notes-2026-05-20.md",
                "know_20fbfb1f1c9e49ea8abc87db04bf8d6c",
                "know_f63b23e9078a4e828f8832f6541b3a01",
            ],
        ),
        Node(
            "public_firmware_no_producer",
            "Public GV100 firmware lacks direct producer hit",
            "evidence",
            "proven",
            "Nouveau/linux-firmware identify and provide signed GV100 FECS/GPCCS/SEC2 blobs. Offline decompression and envydis searches found nearby FECS references but no direct 0x409664, 0x409660, 0x00019900, or 0x999 producer.",
            [
                "runs/20260520-public-gv100-firmware/",
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "know_1375ef0720344d169776b9be7b04448b",
            ],
        ),
        Node(
            "firmware_ctxsw_surfaces_visible",
            "Firmware dumps expose ctxsw/scheduler surfaces",
            "evidence",
            "proven",
            "A read-only firmware visibility scan found gr/sw_ctx.bin contains the GPCS_TPCS_SM_SCH_MICRO_SCHED broadcast address 0x00419b50 at offset 0x192c and default 0x00047a20 at offset 0x1934. Generated GV100 ctxsw headers also list FECS_FEATURE_READOUT 0x00409660, FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT 0x00409664, and SM_SCH_MICRO_SCHED context state. The only 0x00000999 firmware hit is a sequential sw_bundle_init table false-positive, not the FECS speed-select mask.",
            [
                "runs/20260520-firmware-visibility/gv100-firmware-visibility-scan.md",
                "tools/gv100_firmware_visibility_scan.py",
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "know_df671224bf6947bfafdb89b404c9681d",
            ],
        ),
        Node(
            "newer_vbios_security_versions",
            "Newer local VBIOS security versions are visible",
            "source",
            "proven",
            "The local integ tree contains newer host-side VBIOS security files for Volta, Ampere, Hopper, and Ada plus updated FWSECLIC/FSP interface headers. The host vbiosRSADecrypt padding walk remains structurally unchanged, while production GV100 still routes VBIOS verification through FWSECLIC on the PMU path and later Hopper-style code moves secure-boot/HULK orchestration to FSP.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/devinit/vbioscrypto.c",
                "integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/devinit/arch/volta/vbiossecuritygv100.c",
                "integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/devinit/arch/hopper/vbiossecuritygh100.c",
                "integ/gpu_drv/stage_rel/uproc/libs/vbios/inc/ucode_interface.h",
                "integ/gpu_drv/stage_rel/uproc/libs/vbios/inc/ucode_postcodes.h",
            ],
        ),
        Node(
            "focused_static_dynamic_scan",
            "Focused OSS-style static/dynamic scan pass",
            "evidence",
            "proven",
            "Only GCC is installed locally from the requested OSS tool list, so the pass used GCC ASan/UBSan on the offline vbiosRSADecrypt padding-walk harness plus a focused pattern scanner over seven VBIOS/FWSECLIC-adjacent files. The scan produced 83 review hits, with the highest-signal confirmed items being the unchanged host vbiosRSADecrypt reverse padding walk and the legacy MSDEC p[flen]=0 conditional write plus halt failure paths.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "tools/focused_c_security_scan.py",
                "runs/20260520-static-security-scan/focused-c-security-scan.md",
                "runs/20260520-static-security-scan/vbios-rsa-padding-harness-asan.log",
            ],
        ),
        Node(
            "smt_padding_model",
            "Z3 model proves padding-walk counterexamples",
            "evidence",
            "proven",
            "A bounded Z3 model of the post-RSA-decrypt padding walk found satisfiable malformed inputs for the same two cases reproduced by ASan: res.size==1 with byte 00 underflows after the first check, and a size-16 buffer ending in 00 01 with all lower bytes ff walks below offset zero to unsigned 0xffffffff before the next read.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "tools/vbios_rsa_padding_smt.py",
                "runs/20260520-smt-padding-model/vbios-rsa-padding-smt.md",
                "runs/20260520-smt-padding-model/vbios-rsa-padding-smt.json",
            ],
        ),
        Node(
            "symbolic_tools_map_boundary",
            "Symbolic tools need implementation body for 1/16 map",
            "evidence",
            "proven",
            "SMT/symbolic tools can recover the explicit register-to-divisor map in Turing/Ampere code because the 3-bit selectors and divisor enums are present. On GV100, visible artifacts stop at binary FMLA_FULL_SPEED/FMLA_REDUCED_SPEED; targeted public GV100 FECS/SEC2 disassembly searches found no direct 0x409664, 0x409660, 0x19900, 0x999, SM_SPEED_SELECT, or FMLA_REDUCED_SPEED producer. The tools can formalize consistency but cannot invent the hidden scheduler/Falcon implementation.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "integ/gpu_drv/stage_rel/diag/mods/gpu/turinggpu.cpp",
                "integ/gpu_drv/stage_rel/diag/mods/gpu/amperegpu.cpp",
                "integ/gpu_drv/stage_rel/drivers/resman/src/physical/gpu/gr/arch/ampere/gr_ga100.c",
                "runs/20260520-public-gv100-firmware/",
            ],
        ),
        Node(
            "offline_falcon_tools_only",
            "Falcon reverse tools are offline-only here",
            "blocked-path",
            "partial",
            "falcon-tools and faucon help with Falcon vocabulary, disassembly, and secure-mode modeling, but their public payloads/targets are not a GV100 host entrypoint and do not provide signed FECS/SEC2 code or a production control path.",
            [
                "falcon-specs-switch-gv100.md",
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "know_b41c084bacd84f178f35b9682689d384",
            ],
        ),
        Node(
            "tegra_bootrom_analogy_not_path",
            "Tegra BootROM work is an analogy, not a dGPU path",
            "blocked-path",
            "partial",
            "Switch/tx2hax/fault-injection work reinforces the relevance of pre-lockout execution and debug fuse classes, but no GV100 PCIe equivalent of Tegra USB RCM or a production dGPU BootROM extraction/control mode was found.",
            [
                "falcon-specs-switch-gv100.md",
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "know_86c89df861044f518e3a84eaff0c8e46",
                "know_070a95abcd12455e854599aabf271158",
            ],
        ),
        Node(
            "older_board_history",
            "Older board history explains binary GV100 model",
            "source",
            "proven",
            "Pascal/GP100 exposes a binary DP-only FECS speed-select model; GV100 extends the binary full/reduced model to DP/IMLA/FMLA; Turing adds explicit 3-bit speed selectors. This makes GV100 the transition generation where FMLA can be reduced but the explicit divisor is hidden behind binary REDUCED.",
            [
                "nvdrv-backtrack-from-v100-baseline-2026-05-20.md",
                "know_c9298e3166584bcc9881b181e3b56a7b",
                "know_614b7428046f4d58ba96cdd1faadddf6",
            ],
        ),
        Node(
            "blocked_unlock",
            "Unlock path not established",
            "conclusion",
            "blocked",
            "Evidence identifies where the reduced state is asserted and how it propagates to measured throughput, but the producer and exact binary-reduced-to-1/16 mapping remain behind missing chipfuse/POR/IFF data, signed/private Falcon policy, or internal NVIDIA build paths. No supported production GV100 switch or verified host-side unlock path has been found.",
            ["nvdrv-backtrack-from-v100-baseline-2026-05-20.md"],
        ),
    ]

    edges = [
        Edge("main_init_tail", "vbios_init_409664", "localizes"),
        Edge("vbios_init_409664", "raw_rom_pattern", "has-byte-instance"),
        Edge("fullspeed_controls_no_set", "vbios_init_409664", "contrasts-with"),
        Edge("raw_rom_pattern", "field_decode_999", "decodes-as"),
        Edge("field_decode_999", "fmla_downstream_uniform", "predicts"),
        Edge("field_decode_999", "dp_downstream_uniform", "predicts"),
        Edge("field_decode_999", "imla_dp4a_unresolved", "does-not-cleanly-predict"),
        Edge("fmla_downstream_uniform", "symptom_1_16", "corroborates"),
        Edge("dp_downstream_uniform", "symptom_1_16", "broadens-beyond-tensor"),
        Edge("field_decode_999", "gv100_binary_state", "sets"),
        Edge("gv100_binary_state", "symptom_1_16", "explains-visible-state-for"),
        Edge("later_explicit_divisor", "symptom_1_16", "corroborates-exact-divisor-class"),
        Edge("runtime_read_blocked", "blocked_unlock", "blocks-live-user-space-validation"),
        Edge("runtime_read_blocked", "privileged_rm_regops_readback", "bypassed-for-readback-by-admin"),
        Edge("privileged_rm_regops_readback", "fecs_live_readbacks", "confirms-via-rm"),
        Edge("privileged_rm_regops_readback", "blocked_unlock", "readback-only-not-control"),
        Edge("cve_surface_lens", "runtime_read_blocked", "prioritizes-access-control-review"),
        Edge("cve_surface_lens", "privileged_rm_regops_readback", "frames-as-authorized-readback-not-exploit"),
        Edge("cve_surface_lens", "falcon_cve_readonly", "connects-hardware-cve-class"),
        Edge("cve_surface_lens", "blocked_unlock", "does-not-establish-unlock"),
        Edge("cve_surface_lens", "legacy_mmap_physmem_cves", "expands-to-physical-memory-class"),
        Edge("cve_surface_lens", "cve_2017_0352_sensitive_regs", "expands-to-sensitive-register-class"),
        Edge("cve_surface_lens", "rm_vgpu_cve_factcheck", "fact-checks-revised-host-vgpu-list"),
        Edge("rm_vgpu_cve_factcheck", "blocked_unlock", "host-cve-class-not-fecs-control"),
        Edge("legacy_mmap_physmem_cves", "blocked_unlock", "no-direct-current-version-path"),
        Edge("cve_2017_0352_sensitive_regs", "sbr_pub_plm_access_model", "matches-firmware-access-control-model"),
        Edge("sbr_pub_plm_access_model", "sbr_pub_secure_boundary", "reinforces"),
        Edge("sbr_pub_plm_access_model", "blocked_unlock", "places-control-below-host-switch"),
        Edge("missing_chipfuse_por", "blocked_unlock", "blocks-producer-attribution"),
        Edge("vbios_init_409664", "blocked_unlock", "identifies-location-not-safe-toggle"),
        Edge("json_consumer_schema", "missing_chipfuse_por", "defines-shape-of"),
        Edge("bootrom_patches_candidate", "vbios_init_409664", "schema-candidate-for"),
        Edge("missing_chipfuse_por", "bootrom_patches_candidate", "prevents-confirming"),
        Edge("init_opcode_decoder", "raw_rom_pattern", "decodes-byte-shape"),
        Edge("public_search_no_gv100_json", "missing_chipfuse_por", "does-not-resolve"),
        Edge("sim_rom_negative_control", "missing_chipfuse_por", "does-not-substitute-for"),
        Edge("initial_scope_non_destructive", "blocked_unlock", "keeps-graph-read-only"),
        Edge("board_personality_split", "visible_identity_not_enough", "supports"),
        Edge("board_personality_split", "v100_flash_personality_not_speed_gate", "crossed-one-gate"),
        Edge("board_personality_split", "sm_count_floorsweep_masks", "explained-by"),
        Edge("sm_count_floorsweep_masks", "v100_flash_personality_not_speed_gate", "supports-separate-gates"),
        Edge("visible_identity_not_enough", "symptom_1_16", "does-not-clear"),
        Edge("visible_identity_not_enough", "v100_flash_personality_not_speed_gate", "supports-separate-gates"),
        Edge("vbios_init_409664", "v100_flash_personality_not_speed_gate", "shows-speed-gate-persists"),
        Edge("fullspeed_controls_no_set", "v100_flash_personality_not_speed_gate", "defines-fullspeed-control"),
        Edge("v100_flash_personality_not_speed_gate", "blocked_unlock", "narrows-target-to-speed-gate"),
        Edge("public_deepbench_control", "symptom_1_16", "contrasts-with"),
        Edge("vast_fullspeed_control", "symptom_1_16", "contrasts-with"),
        Edge("hmma_issue_spacing_control", "symptom_1_16", "corroborates-issue-rate"),
        Edge("hmma_trace_mode_saturated", "hmma_issue_spacing_control", "reproduces-with-trace"),
        Edge("hmma_trace_mode_saturated", "symptom_1_16", "supports-scheduler-token-model"),
        Edge("hmma_burst_window_shapeable", "hmma_trace_mode_saturated", "explains-low-residency-bursts"),
        Edge("hmma_burst_window_shapeable", "blocked_unlock", "not-a-throughput-bypass"),
        Edge("z3_mechanism_filter", "hmma_burst_window_shapeable", "formalizes-observation-constraints"),
        Edge("z3_mechanism_filter", "symptom_1_16", "selects-shared-token-budget"),
        Edge("hmma_concurrent_budget_conserved", "z3_mechanism_filter", "confirms-surviving-model"),
        Edge("hmma_concurrent_budget_conserved", "symptom_1_16", "strongly-supports-shared-budget"),
        Edge("bootrom_dump_succeeded", "raw_rom_pattern", "enables"),
        Edge("fresh_rom_metadata_only", "vbios_init_409664", "sharpens-focus-on"),
        Edge("m_table_negative", "vbios_init_409664", "rules-out-broader-table-diff"),
        Edge("pstraps_negative", "visible_identity_not_enough", "supports-separate-gates"),
        Edge("fecs_live_readbacks", "field_decode_999", "confirms-live-state"),
        Edge("fecs_live_readbacks", "symptom_1_16", "aligns-with"),
        Edge("gpu14_transient_not_unlock", "blocked_unlock", "rules-out-transient-as-unlock"),
        Edge("dynamic_fecs_static", "fecs_live_readbacks", "shows-config-state"),
        Edge("rm_issue_rate_stub", "runtime_read_blocked", "corroborates"),
        Edge("mods_no_gv100_override", "blocked_unlock", "blocks-normal-diagnostic-path"),
        Edge("rm_policy_hooks_internal", "gv100_binary_state", "shows-policy-class"),
        Edge("rm_policy_hooks_internal", "blocked_unlock", "not-production-gv100"),
        Edge("cmp_sku_detection", "visible_identity_not_enough", "shows-separate-cmp-policy"),
        Edge("ctxsw_generated_surface", "vbios_init_409664", "validates-register-surface"),
        Edge("ctxsw_generated_surface", "gv100_binary_state", "matches"),
        Edge("local_access_map_optin", "runtime_read_blocked", "explains-possible-measurement-only-workaround"),
        Edge("plm_uniform", "blocked_unlock", "not-differentiator"),
        Edge("pub_plm_reconstructed", "plm_uniform", "contextualizes"),
        Edge("sbr_pub_secure_boundary", "public_firmware_no_producer", "sets-boundary-for"),
        Edge("falcon_cve_readonly", "dynamic_fecs_static", "limited-to-observability"),
        Edge("falcon_cve_readonly", "blocked_unlock", "no-control-path-found"),
        Edge("debugdump_not_pfuse", "missing_chipfuse_por", "does-not-resolve"),
        Edge("fresh_debugdump_runtime_residue", "debugdump_not_pfuse", "corroborates-selected-runtime-dump"),
        Edge("fresh_debugdump_runtime_residue", "visible_identity_not_enough", "shows-board-residue-persists"),
        Edge("fresh_debugdump_runtime_residue", "fullspeed_debugdump_missing", "defines-needed-comparator"),
        Edge("fresh_debugdump_runtime_residue", "blocked_unlock", "not-raw-speed-select-value"),
        Edge("fullspeed_debugdump_missing", "debugdump_not_pfuse", "prevents-comparator"),
        Edge("nvspec_negative", "missing_chipfuse_por", "does-not-substitute-for"),
        Edge("chipfuse_search_negative", "missing_chipfuse_por", "reinforces"),
        Edge("public_firmware_no_producer", "blocked_unlock", "leaves-producer-missing"),
        Edge("firmware_ctxsw_surfaces_visible", "public_firmware_no_producer", "narrows-negative-result"),
        Edge("firmware_ctxsw_surfaces_visible", "ctxsw_generated_surface", "corroborates"),
        Edge("firmware_ctxsw_surfaces_visible", "z3_mechanism_filter", "matches-scheduler-surface"),
        Edge("firmware_ctxsw_surfaces_visible", "hmma_concurrent_budget_conserved", "supports-shared-scheduler-scope"),
        Edge("firmware_ctxsw_surfaces_visible", "blocked_unlock", "surface-visible-counter-hidden"),
        Edge("newer_vbios_security_versions", "public_firmware_no_producer", "confirms-interface-not-body"),
        Edge("newer_vbios_security_versions", "blocked_unlock", "moves-boundary-to-secure-firmware"),
        Edge("focused_static_dynamic_scan", "newer_vbios_security_versions", "checks-host-and-legacy-device-code"),
        Edge("focused_static_dynamic_scan", "blocked_unlock", "does-not-reveal-production-fwseclic-body"),
        Edge("smt_padding_model", "focused_static_dynamic_scan", "corroborates-asan-counterexamples"),
        Edge("smt_padding_model", "blocked_unlock", "not-a-signature-bypass"),
        Edge("symbolic_tools_map_boundary", "later_explicit_divisor", "recovers-visible-later-map"),
        Edge("symbolic_tools_map_boundary", "public_firmware_no_producer", "confirms-no-gv100-producer-hit"),
        Edge("symbolic_tools_map_boundary", "blocked_unlock", "cannot-infer-hidden-implementation"),
        Edge("offline_falcon_tools_only", "public_firmware_no_producer", "can-annotate-but-not-produce"),
        Edge("tegra_bootrom_analogy_not_path", "blocked_unlock", "not-portable-as-host-path"),
        Edge("older_board_history", "gv100_binary_state", "explains-generation-position"),
        Edge("older_board_history", "later_explicit_divisor", "connects-to"),
    ]
    return nodes, edges


def dot_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def write_outputs(out_dir: Path, nodes: list[Node], edges: list[Edge], scan: dict[str, dict[str, list[str]]]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)

    payload = {
        "nodes": [asdict(node) for node in nodes],
        "edges": [asdict(edge) for edge in edges],
        "rom_scan": scan,
    }
    (out_dir / "gv100-limiter-evidence-dag.json").write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n"
    )

    dot_lines = ["digraph gv100_limiter {", "  rankdir=LR;"]
    for node in nodes:
        shape = {
            "observation": "ellipse",
            "artifact": "box",
            "decode": "box",
            "source": "component",
            "blocked-path": "octagon",
            "missing-artifact": "folder",
            "conclusion": "doubleoctagon",
        }.get(node.kind, "box")
        color = "red" if node.status == "blocked" else "black"
        dot_lines.append(
            f'  "{node.id}" [label="{dot_escape(node.label)}\\n{node.status}", shape={shape}, color={color}];'
        )
    for edge in edges:
        dot_lines.append(
            f'  "{edge.src}" -> "{edge.dst}" [label="{dot_escape(edge.relation)}"];'
        )
    dot_lines.append("}")
    (out_dir / "gv100-limiter-evidence-dag.dot").write_text("\n".join(dot_lines) + "\n")

    md_lines = [
        "# GV100 Limiter Evidence DAG - 2026-05-20",
        "",
        "This is a read-only logical map of the current evidence. It records what is proven, what is inferred, and where the work is blocked. It is not a hardware modification plan.",
        "",
        "## ROM Pattern Scan",
        "",
    ]
    if scan:
        md_lines.extend(["| ROM | set_reduced_999 | noop_zero |", "| --- | --- | --- |"])
        for path, hits in sorted(scan.items()):
            md_lines.append(
                f"| `{path}` | `{', '.join(hits.get('set_reduced_999', [])) or '-'}` | `{', '.join(hits.get('noop_zero', [])) or '-'}` |"
            )
    else:
        md_lines.append("No ROM pattern hits.")
    md_lines.extend(["", "## Nodes", ""])
    for node in nodes:
        md_lines.extend(
            [
                f"### {node.label}",
                "",
                f"- id: `{node.id}`",
                f"- kind: `{node.kind}`",
                f"- status: `{node.status}`",
                f"- detail: {node.detail}",
                f"- citations: {', '.join(f'`{c}`' for c in node.citations)}",
                "",
            ]
        )
    md_lines.extend(["## Edges", ""])
    for edge in edges:
        md_lines.append(f"- `{edge.src}` -> `{edge.dst}`: {edge.relation}")
    md_lines.append("")
    (out_dir / "gv100-limiter-evidence-dag.md").write_text("\n".join(md_lines))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=PROJECT / "runs/20260520-evidence-dag",
    )
    parser.add_argument(
        "roms",
        nargs="*",
        type=Path,
        help="Optional ROM paths. Defaults to the current curated GV100/Titan controls.",
    )
    args = parser.parse_args()

    roms = args.roms or DEFAULT_ROMS
    scan = scan_roms(roms)
    nodes, edges = build_graph(scan)
    write_outputs(args.out_dir, nodes, edges, scan)


if __name__ == "__main__":
    main()
