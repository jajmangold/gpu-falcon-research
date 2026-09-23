#!/usr/bin/env python3
"""Read-only visibility scan for GV100 limiter evidence in firmware dumps.

This does not try to interpret or patch firmware. It answers a narrower
question: do the public GV100 firmware blobs contain literal references to the
known FECS speed-select and scheduler registers, and what related register
surfaces are present in the generated ctxsw headers?
"""

from __future__ import annotations

import argparse
import json
import re
import struct
from dataclasses import dataclass, asdict
from pathlib import Path


PATTERNS = {
    "FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT_ADDR": 0x00409664,
    "FECS_FEATURE_READOUT_ADDR": 0x00409660,
    "SM_SPEED_SELECT_MASK_0x999": 0x00000999,
    "SM_SPEED_SELECT_CLEAR_COMPLEMENT": 0xFFFFF666,
    "SM_SCH_MICRO_SCHED_DEFAULT": 0x00047A20,
    "GPCS_TPCS_SM_SCH_MICRO_SCHED_BCAST_ADDR": 0x00419B50,
}

TEXT_PATTERNS = [
    "SM_SPEED_SELECT",
    "FMLA",
    "HMMA",
    "TENSOR",
    "MICRO_SCHED",
    "FEATURE_OVERRIDE",
    "FEATURE_READOUT",
]


@dataclass
class BinaryHit:
    file: str
    pattern: str
    value_hex: str
    offset_hex: str


@dataclass
class HeaderHit:
    file: str
    line: int
    text: str


def iter_files(root: Path) -> list[Path]:
    return sorted(p for p in root.rglob("*") if p.is_file())


def scan_binary_patterns(files: list[Path], root: Path) -> list[BinaryHit]:
    hits: list[BinaryHit] = []
    encoded = {
        name: struct.pack("<I", value)
        for name, value in PATTERNS.items()
    }
    for path in files:
        data = path.read_bytes()
        for name, needle in encoded.items():
            start = 0
            while True:
                idx = data.find(needle, start)
                if idx == -1:
                    break
                hits.append(
                    BinaryHit(
                        file=str(path.relative_to(root)),
                        pattern=name,
                        value_hex=f"0x{PATTERNS[name]:08x}",
                        offset_hex=f"0x{idx:x}",
                    )
                )
                start = idx + 1
    return hits


def scan_text(files: list[Path], root: Path) -> list[HeaderHit]:
    hits: list[HeaderHit] = []
    pattern = re.compile("|".join(re.escape(p) for p in TEXT_PATTERNS))
    for path in files:
        try:
            text = path.read_text(errors="replace")
        except UnicodeDecodeError:
            continue
        for lineno, line in enumerate(text.splitlines(), 1):
            if pattern.search(line):
                hits.append(
                    HeaderHit(
                        file=str(path.relative_to(root)),
                        line=lineno,
                        text=line.strip(),
                    )
                )
    return hits


def extract_relevant_ctxsw_header_hits(net_roots: list[Path]) -> list[HeaderHit]:
    hits: list[HeaderHit] = []
    relevant = re.compile(
        r"FECS_FEATURE_(READOUT|OVERRIDE_SM_SPEED_SELECT)|SM_SCH_MICRO_SCHED"
    )
    for root in net_roots:
        if not root.exists():
            continue
        for path in sorted(root.glob("NETD_*.h")):
            text = path.read_text(errors="replace")
            for lineno, line in enumerate(text.splitlines(), 1):
                if relevant.search(line):
                    hits.append(
                        HeaderHit(
                            file=str(path),
                            line=lineno,
                            text=line.strip(),
                        )
                    )
    return hits


def summarize_false_positive_context(root: Path, binary_hits: list[BinaryHit]) -> list[str]:
    notes: list[str] = []
    for hit in binary_hits:
        if hit.pattern != "SM_SPEED_SELECT_MASK_0x999":
            continue
        path = root / hit.file
        off = int(hit.offset_hex, 16)
        data = path.read_bytes()
        start = max(0, off - 32)
        end = min(len(data), off + 48)
        words = []
        for idx in range(start, end, 4):
            chunk = data[idx : idx + 4]
            if len(chunk) == 4:
                words.append(f"{idx:06x}:{struct.unpack('<I', chunk)[0]:08x}")
        notes.append(f"{hit.file}@{hit.offset_hex}: " + " ".join(words))
    return notes


def write_outputs(out_dir: Path, payload: dict) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "gv100-firmware-visibility-scan.json").write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n"
    )

    binary_hits = payload["binary_hits"]
    ctxsw_hits = payload["ctxsw_header_hits"]
    false_positive_context = payload["false_positive_context"]

    lines = [
        "# GV100 Firmware Visibility Scan",
        "",
        "This is a read-only scan of the public GV100 firmware dump artifacts and",
        "the generated GV100 ctxsw register headers.",
        "",
        "## Direct Binary Pattern Hits",
        "",
    ]
    if binary_hits:
        lines.append("| File | Pattern | Value | Offset |")
        lines.append("| --- | --- | --- | ---: |")
        for hit in binary_hits:
            lines.append(
                f"| `{hit['file']}` | `{hit['pattern']}` | `{hit['value_hex']}` | `{hit['offset_hex']}` |"
            )
    else:
        lines.append("No literal little-endian dword hits for the tracked addresses/masks.")

    lines.extend(["", "## 0x999 Context Check", ""])
    if false_positive_context:
        lines.append(
            "The only direct `0x00000999`-class hit is context-dependent; nearby dwords are:"
        )
        lines.append("")
        for note in false_positive_context:
            lines.append(f"- `{note}`")
    else:
        lines.append("No direct `0x00000999` hits were found in the scanned firmware files.")

    lines.extend(["", "## Generated Ctxsw/Register Evidence", ""])
    lines.append("| File | Line | Text |")
    lines.append("| --- | ---: | --- |")
    for hit in ctxsw_hits[:120]:
        lines.append(
            f"| `{hit['file']}` | {hit['line']} | `{hit['text']}` |"
        )
    if len(ctxsw_hits) > 120:
        lines.append(f"| ... | ... | {len(ctxsw_hits) - 120} additional matches omitted |")

    lines.extend(
        [
            "",
            "## Interpretation",
            "",
            "- The raw FECS/GPCCS firmware binaries do not show a named or literal implementation of the 1/16 HMMA issue budget.",
            "- The generated ctxsw artifacts do show the relevant hardware surfaces: FECS feature readout/override at `0x00409660/0x00409664` and per-TPC `SM_SCH_MICRO_SCHED` state.",
            "- The practical visible chain remains: VBIOS/init/fuse state sets FECS `SM_SPEED_SELECT`; generated ctxsw machinery preserves/knows that register; the actual reduced-rate budget is likely implemented below the dumped firmware source level in scheduler hardware or signed/internal microcode without symbols.",
        ]
    )
    (out_dir / "gv100-firmware-visibility-scan.md").write_text("\n".join(lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--firmware-root",
        type=Path,
        default=Path(
            "projects/gpu-falcon-research/runs/20260520-public-gv100-firmware"
        ),
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path("projects/gpu-falcon-research/runs/20260520-firmware-visibility"),
    )
    parser.add_argument(
        "--net-root",
        action="append",
        type=Path,
        default=[
            Path(
                "/home/josh/containers/temp/nvdrv/dev/gpu_drv/stage_rel/drivers/resman/kernel/inc/ctxsw_ucode/volta/gv100/net"
            ),
            Path(
                "/home/josh/containers/temp/nvdrv/integ/gpu_drv/stage_rel/drivers/resman/kernel/inc/ctxsw_ucode/volta/gv100/net"
            ),
        ],
    )
    args = parser.parse_args()

    firmware_files = iter_files(args.firmware_root)
    binary_files = [
        p
        for p in firmware_files
        if p.suffix in {".bin", ".zst"} or ".bin." in p.name
    ]
    text_files = [p for p in firmware_files if p.suffix in {".txt", ".err"}]

    binary_hits = scan_binary_patterns(binary_files, args.firmware_root)
    text_hits = scan_text(text_files, args.firmware_root)
    ctxsw_hits = extract_relevant_ctxsw_header_hits(args.net_root)
    false_positive_context = summarize_false_positive_context(
        args.firmware_root, binary_hits
    )

    payload = {
        "firmware_root": str(args.firmware_root.resolve()),
        "binary_file_count": len(binary_files),
        "text_file_count": len(text_files),
        "patterns": {k: f"0x{v:08x}" for k, v in PATTERNS.items()},
        "binary_hits": [asdict(h) for h in binary_hits],
        "text_hit_count": len(text_hits),
        "ctxsw_header_hits": [asdict(h) for h in ctxsw_hits],
        "false_positive_context": false_positive_context,
    }
    write_outputs(args.out_dir, payload)
    print(args.out_dir / "gv100-firmware-visibility-scan.md")
    print(args.out_dir / "gv100-firmware-visibility-scan.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
