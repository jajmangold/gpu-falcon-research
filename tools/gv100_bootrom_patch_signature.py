#!/usr/bin/env python3
"""Classify GV100 VBIOS images by the FECS speed-select init tail.

This is an offline evidence tool. It reads ROM files and reports whether the
known GV100 main-init tail reaches DONE directly, writes the reduced-speed
FECS override first, or writes an explicit zero/no-op first.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from dataclasses import asdict, dataclass
from pathlib import Path


PROJECT = Path("/srv/nvme-data/containers/projects/gpu-falcon-research")

COMMON_TAIL_PREFIX = bytes.fromhex("7a 00 73 13 00 00 00 00 20")
SET_REDUCED_OPCODE = bytes.fromhex("6e 64 96 40 00 66 f6 ff ff 99 09 00 00")
NOOP_ZERO_OPCODE = bytes.fromhex("6e 64 96 40 00 ff ff ff ff 00 00 00 00")
DONE = bytes.fromhex("71")
SHARED_AFTER_DONE = bytes.fromhex("7a 04 22 12 00 01 00 00 00")

DEFAULT_DIRS = [
    PROJECT / "runs/20260520-host-all-vbios",
    PROJECT / "runs/20260520-bootrom-dump",
    PROJECT / "runs/20260519-173454-tensor-probe-gpu2-gpu14/online-vbios",
    PROJECT / "runs/20260519-173454-tensor-probe-gpu2-gpu14/full-speed-control",
    PROJECT / "runs/20260519-173454-tensor-probe-gpu2-gpu14/temp-rom-inventory/extracted-pcirom",
]


@dataclass
class Hit:
    offset: str
    kind: str
    detail: str


@dataclass
class RomResult:
    path: str
    name: str
    size: int
    sha256: str
    class_name: str
    hits: list[Hit]


def find_all(data: bytes, pattern: bytes) -> list[int]:
    hits: list[int] = []
    pos = data.find(pattern)
    while pos != -1:
        hits.append(pos)
        pos = data.find(pattern, pos + 1)
    return hits


def classify_rom(path: Path) -> RomResult:
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    hits: list[Hit] = []

    if not data:
        return RomResult(
            str(path),
            path.name,
            0,
            digest,
            "empty_or_failed_dump",
            hits,
        )

    for off in find_all(data, COMMON_TAIL_PREFIX + SET_REDUCED_OPCODE + DONE):
        opcode_off = off + len(COMMON_TAIL_PREFIX)
        hits.append(Hit(
            f"0x{opcode_off:06x}",
            "set_reduced_999",
            "common tail followed by FECS speed-select reduced+override write, then DONE",
        ))

    for off in find_all(data, COMMON_TAIL_PREFIX + NOOP_ZERO_OPCODE + DONE):
        opcode_off = off + len(COMMON_TAIL_PREFIX)
        hits.append(Hit(
            f"0x{opcode_off:06x}",
            "noop_zero",
            "common tail followed by explicit FECS zero/no-op write, then DONE",
        ))

    for off in find_all(data, COMMON_TAIL_PREFIX + DONE + SHARED_AFTER_DONE):
        done_off = off + len(COMMON_TAIL_PREFIX)
        hits.append(Hit(
            f"0x{done_off:06x}",
            "direct_done",
            "common tail reaches DONE directly and resumes shared following bytes",
        ))

    if any(hit.kind == "set_reduced_999" for hit in hits):
        class_name = "capped_or_cmp_derived"
    elif any(hit.kind == "noop_zero" for hit in hits):
        class_name = "explicit_fullspeed_zero_write"
    elif any(hit.kind == "direct_done" for hit in hits):
        class_name = "fullspeed_direct_done"
    else:
        class_name = "no_known_tail_signature"

    return RomResult(str(path), path.name, len(data), digest, class_name, hits)


def gather_roms(paths: list[Path]) -> list[Path]:
    roms: list[Path] = []
    for path in paths:
        if path.is_dir():
            roms.extend(sorted(path.glob("*.rom")))
        elif path.is_file():
            roms.append(path)
    return sorted(set(roms))


def write_markdown(results: list[RomResult], out: Path) -> None:
    non_empty = [result for result in results if result.size > 0]
    unique_non_empty = {result.sha256: result for result in non_empty}
    lines = [
        "# GV100 Bootrom Patch Signature Scan - 2026-05-20",
        "",
        "Offline scan of dumped ROM images. This report classifies the already-observed main-init tail pattern; it is not a ROM modification plan.",
        "",
        "## Summary",
        "",
        f"Total files scanned: {len(results)}",
        "",
        f"Non-empty files: {len(non_empty)}",
        "",
        f"Unique non-empty SHA-256 values: {len(unique_non_empty)}",
        "",
        "| Class | Count |",
        "| --- | ---: |",
    ]
    counts: dict[str, int] = {}
    for result in results:
        counts[result.class_name] = counts.get(result.class_name, 0) + 1
    for name, count in sorted(counts.items()):
        lines.append(f"| `{name}` | {count} |")

    unique_counts: dict[str, int] = {}
    for result in unique_non_empty.values():
        unique_counts[result.class_name] = unique_counts.get(result.class_name, 0) + 1
    lines.extend([
        "",
        "## Unique Non-Empty Summary",
        "",
        "| Class | Unique SHA-256 Count |",
        "| --- | ---: |",
    ])
    for name, count in sorted(unique_counts.items()):
        lines.append(f"| `{name}` | {count} |")

    lines.extend([
        "",
        "## Results",
        "",
        "| ROM | Size | SHA-256 | Class | Hits |",
        "| --- | ---: | --- | --- | --- |",
    ])
    for result in results:
        hits = "<br>".join(
            f"`{hit.offset}` `{hit.kind}`" for hit in result.hits
        ) or "-"
        lines.append(
            f"| `{result.name}` | {result.size} | `{result.sha256[:12]}` | `{result.class_name}` | {hits} |"
        )

    lines.extend([
        "",
        "## Observed Producer Shape",
        "",
        "The missing MODS fuse JSON parser supports per-SKU `Bootrom_patches` entries with `sequence` and `rows` containing `address` and `value` fields. That schema is the right shape for byte-local patch intent, but the visible runtime `BRRevs` consumers found so far are Tegra-side; no dGPU GV100 `Bootrom_patches` application path has been found in this checkout. The observed capped signature is a local 13-byte opcode immediately before `DONE`:",
        "",
        "```text",
        "6e 64 96 40 00 66 f6 ff ff 99 09 00 00",
        "```",
        "",
        "Decoded from existing `nvbios` output, that opcode is:",
        "",
        "```text",
        "NV_REG R[0x409664] &= 0xfffff666 |= 0x00000999",
        "```",
        "",
        "This strengthens the evidence that the missing producer artifact is SKU-specific VBIOS/init-script patch data rather than a runtime CUDA or RM setting. The exact dGPU producer is still missing from the visible source tree.",
        "",
    ])
    out.write_text("\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="*", type=Path, default=DEFAULT_DIRS)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--md", type=Path)
    args = parser.parse_args()

    roms = gather_roms(args.paths)
    results = [classify_rom(path) for path in roms]

    if args.json:
        args.json.write_text(json.dumps([asdict(r) for r in results], indent=2))
    if args.md:
        write_markdown(results, args.md)
    if not args.json and not args.md:
        print(json.dumps([asdict(r) for r in results], indent=2))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
