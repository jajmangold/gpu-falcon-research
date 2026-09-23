#!/usr/bin/env python3
"""Compare decoded GV100 VBIOS main-init script tails.

This is an offline parser for `nvbios -v -b -u` text output. It extracts the
selected main init script pointer and the lines around the FECS speed-select
write or the corresponding DONE point in full-speed controls.
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path


PROJECT = Path("/srv/nvme-data/containers/projects/gpu-falcon-research")

TARGETS = {
    "cmp-card0": PROJECT / "runs/20260520-bootrom-dump/card0-cmp-05-nvbios-vbu.txt",
    "v100-personality-card4": PROJECT / "runs/20260520-bootrom-dump/card4-v100-0b-nvbios-vbu.txt",
    "v100-personality-266855": PROJECT
    / "runs/20260519-173454-tensor-probe-gpu2-gpu14/online-vbios/266855-extracted-nvbios-vbu.txt",
    "tesla-v100-16gb-control": PROJECT
    / "runs/20260519-173454-tensor-probe-gpu2-gpu14/full-speed-control/v100-control-nvbios-vbu.txt",
    "tesla-v100-32gb-control": PROJECT
    / "runs/20260519-173454-tensor-probe-gpu2-gpu14/temp-rom-inventory/nvbios-selected/NVIDIA.TeslaV100.32768.180223-extracted-pcirom.txt",
    "quadro-gv100-control": PROJECT
    / "runs/20260519-173454-tensor-probe-gpu2-gpu14/temp-rom-inventory/nvbios-selected/NVIDIA.QuadroGV100.32768.180214-extracted-pcirom.txt",
    "titan-v-12gb-control": PROJECT
    / "runs/20260519-173454-tensor-probe-gpu2-gpu14/temp-rom-inventory/nvbios-selected/NVIDIA.TitanV.12288.180904-extracted-pcirom.txt",
}

ADDR_RE = re.compile(r"^0x([0-9a-fA-F]{8}):")
TABLE_RE = re.compile(r"Init script table at 0x([0-9a-fA-F]+): (\d+) main scripts")
SCRIPT_RE = re.compile(r"Init script 0 at 0x([0-9a-fA-F]+):")


@dataclass(frozen=True)
class Tail:
    label: str
    path: Path
    table_offset: str
    main_count: str
    script0_offset: str
    anchor_offset: str
    anchor_kind: str
    lines: list[str]


def extract(label: str, path: Path) -> Tail:
    text = path.read_text(errors="replace").splitlines()
    table_offset = "?"
    main_count = "?"
    script0_offset = "?"
    for line in text:
        if m := TABLE_RE.search(line):
            table_offset = f"0x{int(m.group(1), 16):04x}"
            main_count = m.group(2)
        if m := SCRIPT_RE.search(line):
            script0_offset = f"0x{int(m.group(1), 16):04x}"
            break

    anchor_idx: int | None = None
    anchor_kind = "missing"
    for i, line in enumerate(text):
        if "R[0x409664]" in line:
            anchor_idx = i
            anchor_kind = "fecs-write"
            break

    if anchor_idx is None:
        # For controls, use the DONE immediately after the common GPC setup tail.
        for i, line in enumerate(text):
            if "R[0x137300] = 0x20000000" not in line:
                continue
            for j in range(i + 1, min(i + 4, len(text))):
                if "DONE" in text[j]:
                    anchor_idx = j
                    anchor_kind = "done-after-common-tail"
                    break
            if anchor_idx is not None:
                break

    if anchor_idx is None:
        anchor_idx = 0

    m = ADDR_RE.match(text[anchor_idx])
    anchor_offset = f"0x{int(m.group(1), 16):08x}" if m else "?"
    start = max(0, anchor_idx - 4)
    end = min(len(text), anchor_idx + 3)
    return Tail(
        label,
        path,
        table_offset,
        main_count,
        script0_offset,
        anchor_offset,
        anchor_kind,
        text[start:end],
    )


def write_report(out: Path, tails: list[Tail]) -> None:
    lines = [
        "# GV100 VBIOS Main Init Tail Compare - 2026-05-20",
        "",
        "Read-only comparison of decoded `nvbios` output around the FECS speed-select write or the corresponding full-speed-control `DONE` point.",
        "",
        "## Summary",
        "",
        "| Image | Init table | Main scripts | Script 0 | Anchor | Kind |",
        "| --- | ---: | ---: | ---: | ---: | --- |",
    ]
    for tail in tails:
        lines.append(
            f"| {tail.label} | `{tail.table_offset}` | `{tail.main_count}` | `{tail.script0_offset}` | `{tail.anchor_offset}` | `{tail.anchor_kind}` |"
        )

    lines.extend(["", "## Extracts", ""])
    for tail in tails:
        rel = tail.path.relative_to(PROJECT)
        lines.extend([f"### {tail.label}", "", f"`{rel}`", "", "```text"])
        lines.extend(tail.lines)
        lines.extend(["```", ""])

    lines.extend(
        [
            "## Interpretation",
            "",
            "The capped/CMP-derived images have one selected main init script. In the common tail, they perform the same `0x137330`, `0x137310`, and `0x137300` writes seen in full-speed GV100 controls, then execute the FECS `0x409664` set-reduced opcode before `DONE`.",
            "",
            "Full-speed Tesla V100 32GB and Quadro GV100 controls reach `DONE` at the corresponding point instead. This supports the narrower provenance model: the visible limiter assertion is an unconditional main-init-script opcode in the capped/CMP-derived VBIOS images.",
            "",
        ]
    )
    out.write_text("\n".join(lines))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--out",
        type=Path,
        default=PROJECT / "runs/20260520-evidence-dag/gv100-vbios-main-init-tail-compare.md",
    )
    args = parser.parse_args()

    tails = [extract(label, path) for label, path in TARGETS.items()]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    write_report(args.out, tails)


if __name__ == "__main__":
    main()
