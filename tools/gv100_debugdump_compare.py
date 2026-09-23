#!/usr/bin/env python3
"""Compare read-only NVIDIA debugdump extracts for GV100 limiter clues."""

from __future__ import annotations

import argparse
import json
import re
import struct
from dataclasses import asdict, dataclass
from pathlib import Path


DWORD_PATTERNS = {
    "FECS_FEATURE_READOUT_ADDR": 0x00409660,
    "FECS_FEATURE_OVERRIDE_SM_SPEED_SELECT_ADDR": 0x00409664,
    "SM_SPEED_SELECT_MASK_0x999": 0x00000999,
    "SM_SPEED_SELECT_CLEAR_COMPLEMENT": 0xFFFFF666,
    "SM_SCH_MICRO_SCHED_BCAST_ADDR": 0x00419B50,
    "SM_SCH_MICRO_SCHED_DEFAULT": 0x00047A20,
}

STRING_PATTERNS = [
    "CMP",
    "OBD",
    "OEM",
    "IMG",
    "ROM",
    "InfoROM",
    "Inforom",
    "FECS",
    "FUSE",
    "HMMA",
    "TENSOR",
    "SPEED",
    "SCHED",
    "409660",
    "409664",
]


@dataclass
class DwordHit:
    file: str
    pattern: str
    value: str
    offset: str


@dataclass
class StringHit:
    file: str
    offset: str
    text: str


def printable_strings(data: bytes, min_len: int = 3) -> list[tuple[int, str]]:
    out: list[tuple[int, str]] = []
    start = None
    buf = bytearray()
    for idx, b in enumerate(data):
        if 32 <= b < 127:
            if start is None:
                start = idx
            buf.append(b)
        else:
            if start is not None and len(buf) >= min_len:
                out.append((start, buf.decode("ascii", "replace")))
            start = None
            buf.clear()
    if start is not None and len(buf) >= min_len:
        out.append((start, buf.decode("ascii", "replace")))
    return out


def scan_dir(path: Path) -> dict:
    dword_hits: list[DwordHit] = []
    string_hits: list[StringHit] = []
    string_re = re.compile("|".join(re.escape(x) for x in STRING_PATTERNS))
    files = sorted(p for p in path.glob("*") if p.is_file())
    for file_path in files:
        data = file_path.read_bytes()
        for name, value in DWORD_PATTERNS.items():
            needle = struct.pack("<I", value)
            start = 0
            while True:
                idx = data.find(needle, start)
                if idx < 0:
                    break
                dword_hits.append(
                    DwordHit(
                        file=file_path.name,
                        pattern=name,
                        value=f"0x{value:08x}",
                        offset=f"0x{idx:x}",
                    )
                )
                start = idx + 1
        for off, text in printable_strings(data):
            if string_re.search(text):
                string_hits.append(
                    StringHit(file=file_path.name, offset=f"0x{off:x}", text=text)
                )
    return {
        "path": str(path),
        "files": {p.name: p.stat().st_size for p in files},
        "dword_hits": [asdict(h) for h in dword_hits],
        "string_hits": [asdict(h) for h in string_hits],
    }


def write_report(out_dir: Path, payload: dict) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "debugdump-refresh-compare.json").write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n"
    )

    lines = [
        "# Debugdump Refresh Compare",
        "",
        "Fresh root `nvidia-debugdump --dumpall` captures were collected for one",
        "CMP-labelled card and one V100-personality card, then decrypted with the",
        "local nvdebugdump extractor.",
        "",
        "## Files",
        "",
    ]
    for label, data in payload["devices"].items():
        lines.append(f"### {label}")
        lines.append("")
        lines.append("| File | Bytes |")
        lines.append("| --- | ---: |")
        for name, size in sorted(data["files"].items()):
            lines.append(f"| `{name}` | {size} |")
        lines.append("")

    lines.extend(["## Dword Pattern Hits", ""])
    for label, data in payload["devices"].items():
        lines.append(f"### {label}")
        lines.append("")
        hits = data["dword_hits"]
        if not hits:
            lines.append("No tracked dword patterns found.")
            lines.append("")
            continue
        lines.append("| File | Pattern | Value | Offset |")
        lines.append("| --- | --- | --- | ---: |")
        for hit in hits:
            lines.append(
                f"| `{hit['file']}` | `{hit['pattern']}` | `{hit['value']}` | `{hit['offset']}` |"
            )
        lines.append("")

    lines.extend(["## String Hits", ""])
    for label, data in payload["devices"].items():
        lines.append(f"### {label}")
        lines.append("")
        hits = data["string_hits"][:80]
        if not hits:
            lines.append("No tracked strings found.")
            lines.append("")
            continue
        lines.append("| File | Offset | Text |")
        lines.append("| --- | ---: | --- |")
        for hit in hits:
            escaped = hit["text"].replace("|", "\\|")
            lines.append(f"| `{hit['file']}` | `{hit['offset']}` | `{escaped}` |")
        if len(data["string_hits"]) > len(hits):
            lines.append(f"| ... | ... | {len(data['string_hits']) - len(hits)} more omitted |")
        lines.append("")

    lines.extend(
        [
            "## Interpretation",
            "",
            "- Debugdump is different from the static firmware dump: it captures runtime RM diagnostic protobuf/log payloads after VBIOS and driver initialization.",
            "- The RM protobufs expose InfoROM/OBD/CMP-era strings on both sampled local cards, including the V100-personality card.",
            "- The tracked FECS addresses appear in `nvlog` binary payloads, not as decoded `rm_*.pb` register fields. These are treated as diagnostic/log artifacts unless a protobuf decoder maps them to a selected register block.",
            "- This remains useful for comparing board-management residue against a true full-speed V100 debugdump, but it is not a raw all-register/PFUSE dump and does not directly reveal the hidden 1/16 budget implementation.",
        ]
    )
    (out_dir / "debugdump-refresh-compare.md").write_text("\n".join(lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--cmp-dir",
        type=Path,
        default=Path(
            "projects/gpu-falcon-research/runs/20260520-debugdump-refresh/decrypted-gpu0"
        ),
    )
    parser.add_argument(
        "--v100-dir",
        type=Path,
        default=Path(
            "projects/gpu-falcon-research/runs/20260520-debugdump-refresh/decrypted-gpu4"
        ),
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path("projects/gpu-falcon-research/runs/20260520-debugdump-refresh"),
    )
    args = parser.parse_args()
    payload = {
        "patterns": {k: f"0x{v:08x}" for k, v in DWORD_PATTERNS.items()},
        "devices": {
            "gpu0-cmp": scan_dir(args.cmp_dir),
            "gpu4-v100-personality": scan_dir(args.v100_dir),
        },
    }
    write_report(args.out_dir, payload)
    print(args.out_dir / "debugdump-refresh-compare.md")
    print(args.out_dir / "debugdump-refresh-compare.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
