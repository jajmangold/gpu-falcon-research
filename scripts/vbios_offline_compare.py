#!/usr/bin/env python3
"""Offline comparator for the local GV100 CMP and V100-personality VBIOS images."""

from __future__ import annotations

import argparse
import hashlib
import math
import struct
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class RomPart:
    offset: int
    size: int
    code_type: int
    indicator: int
    device_id: int


@dataclass(frozen=True)
class BitEntry:
    kind: str
    version: int
    length: int
    table_offset: int
    entry_offset: int


M_NAMES = {
    0x01: "RESTRICT",
    0x03: "TYPE",
    0x05: "TRAIN",
    0x09: "TRAIN_PATTERN",
    0x0D: "UNK0D",
    0x11: "UNKNOWN_11",
    0x15: "UNKNOWN_15",
}


def u8(data: bytes, off: int) -> int:
    return data[off]


def u16(data: bytes, off: int) -> int:
    return struct.unpack_from("<H", data, off)[0]


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def entropy(data: bytes) -> float:
    if not data:
        return 0.0
    counts = [0] * 256
    for b in data:
        counts[b] += 1
    total = len(data)
    return -sum((c / total) * math.log2(c / total) for c in counts if c)


def parse_rom_parts(data: bytes) -> list[RomPart]:
    parts: list[RomPart] = []
    off = 0
    while off + 0x40 < len(data) and data[off : off + 2] == b"\x55\xaa":
        blocks = u8(data, off + 2)
        size = blocks * 512
        pcir_rel = u16(data, off + 0x18)
        pcir = off + pcir_rel
        if data[pcir : pcir + 4] != b"PCIR":
            break
        device_id = u16(data, pcir + 0x06)
        code_type = u8(data, pcir + 0x14)
        indicator = u8(data, pcir + 0x15)
        parts.append(RomPart(off, size, code_type, indicator, device_id))
        if indicator & 0x80:
            break
        off += size
    return parts


def find_bit(data: bytes) -> int:
    # These images have the BIT pointer in the legacy VBIOS header area, and the
    # table itself starts at 0x1b0. The on-disk signature is two bytes into the
    # structure: [checksum?][?]["BIT\0"].
    for off in range(0x100, 0x300):
        if data[off + 2 : off + 6] != b"BIT\x00":
            continue
        if off + 12 > len(data):
            continue
        hlen = u8(data, off + 8)
        rlen = u8(data, off + 9)
        count = u8(data, off + 10)
        version = u16(data, off + 6)
        if version == 0x0100 and hlen >= 12 and rlen >= 6 and 1 <= count <= 64:
            return off
    raise ValueError("BIT table not found")


def parse_bit(data: bytes) -> tuple[int, list[BitEntry]]:
    bit = find_bit(data)
    hlen = u8(data, bit + 8)
    rlen = u8(data, bit + 9)
    count = u8(data, bit + 10)
    entries: list[BitEntry] = []
    for i in range(count):
        off = bit + hlen + rlen * i
        kind = chr(u8(data, off))
        entries.append(
            BitEntry(
                kind=kind,
                version=u8(data, off + 1),
                length=u16(data, off + 2),
                table_offset=u16(data, off + 4),
                entry_offset=off,
            )
        )
    return bit, entries


def entry_by_kind(entries: list[BitEntry], kind: str) -> BitEntry:
    for entry in entries:
        if entry.kind == kind:
            return entry
    raise ValueError(f"BIT entry {kind!r} not found")


def parse_m_table(data: bytes, entry: BitEntry) -> dict[int, int]:
    result: dict[int, int] = {}
    base = entry.table_offset
    # Version 2 M table stores ram_restrict_group_count at byte 0, then 16-bit
    # table pointers at odd offsets.
    for rel in range(1, entry.length, 2):
        if base + rel + 2 <= len(data):
            ptr = u16(data, base + rel)
            if ptr:
                result[rel] = ptr
    return result


def diff_positions(a: bytes, b: bytes, limit: int | None = None) -> list[tuple[int, int, int]]:
    n = min(len(a), len(b), limit if limit is not None else min(len(a), len(b)))
    return [(i, a[i], b[i]) for i in range(n) if a[i] != b[i]]


def hexdump_line(data: bytes, off: int, size: int) -> str:
    chunk = data[off : off + size]
    return " ".join(f"{b:02x}" for b in chunk)


def section_for_table(data: bytes, off: int, parts: list[RomPart]) -> str:
    for part in parts:
        if part.offset <= off < part.offset + part.size:
            label = "legacy-x86" if part.code_type == 0 else "efi" if part.code_type == 3 else f"type-{part.code_type}"
            return f"{label} part @ 0x{part.offset:05x}"
    return "outside parsed ROM parts"


def mem_type_summary(data: bytes, off: int) -> dict[str, int]:
    return {
        "version": u8(data, off),
        "hlen": u8(data, off + 1),
        "rlen": u8(data, off + 2),
        "entries": u8(data, off + 3),
    }


def unk0d_summary(data: bytes, off: int) -> dict[str, int]:
    return mem_type_summary(data, off)


def report(cmp_path: Path, v100_path: Path) -> str:
    cmp_data = cmp_path.read_bytes()
    v100_data = v100_path.read_bytes()
    cmp_parts = parse_rom_parts(cmp_data)
    v100_parts = parse_rom_parts(v100_data)
    cmp_bit, cmp_entries = parse_bit(cmp_data)
    v100_bit, v100_entries = parse_bit(v100_data)
    cmp_m = entry_by_kind(cmp_entries, "M")
    v100_m = entry_by_kind(v100_entries, "M")
    cmp_p = entry_by_kind(cmp_entries, "p")
    v100_p = entry_by_kind(v100_entries, "p")
    cmp_m_ptrs = parse_m_table(cmp_data, cmp_m)
    v100_m_ptrs = parse_m_table(v100_data, v100_m)
    lines: list[str] = []

    lines.append("# GV100 VBIOS Offline Compare")
    lines.append("")
    lines.append("## Inputs")
    lines.append("")
    lines.append(f"- CMP ROM: `{cmp_path}`")
    lines.append(f"  - size: `{len(cmp_data)}`")
    lines.append(f"  - sha256: `{digest(cmp_data)}`")
    lines.append(f"- V100-personality ROM: `{v100_path}`")
    lines.append(f"  - size: `{len(v100_data)}`")
    lines.append(f"  - sha256: `{digest(v100_data)}`")
    lines.append("")

    lines.append("## ROM Parts")
    lines.append("")
    lines.append("| Image | Part | Offset | Size | Code Type | Device | Indicator |")
    lines.append("| --- | ---: | ---: | ---: | ---: | ---: | ---: |")
    for label, parts in (("CMP", cmp_parts), ("V100", v100_parts)):
        for idx, part in enumerate(parts):
            lines.append(
                f"| {label} | {idx} | `0x{part.offset:05x}` | `0x{part.size:x}` | "
                f"`0x{part.code_type:02x}` | `0x{part.device_id:04x}` | `0x{part.indicator:02x}` |"
            )
    lines.append("")

    lines.append("## BIT Table")
    lines.append("")
    lines.append(f"- CMP BIT offset: `0x{cmp_bit:04x}`")
    lines.append(f"- V100 BIT offset: `0x{v100_bit:04x}`")
    lines.append("- BIT entries are byte-identical through the top-level directory; most useful differences are in the pointed-to tables.")
    lines.append("")

    lines.append("## BIT p / Falcon Pointer Reassessment")
    lines.append("")
    cmp_p_ptr = u32(cmp_data, cmp_p.table_offset)
    v100_p_ptr = u32(v100_data, v100_p.table_offset)
    lines.append(f"- CMP `p[0x00]` pointer: `0x{cmp_p_ptr:05x}` ({section_for_table(cmp_data, cmp_p_ptr, cmp_parts)})")
    lines.append(f"- V100 `p[0x00]` pointer: `0x{v100_p_ptr:05x}` ({section_for_table(v100_data, v100_p_ptr, v100_parts)})")
    lines.append(f"- CMP bytes at pointer: `{hexdump_line(cmp_data, cmp_p_ptr, 16)}`")
    lines.append(f"- V100 bytes at pointer: `{hexdump_line(v100_data, v100_p_ptr, 16)}`")
    lines.append(
        f"- Entropy at pointer, first 4096 bytes: CMP `{entropy(cmp_data[cmp_p_ptr:cmp_p_ptr+4096]):.3f}` bits/byte, "
        f"V100 `{entropy(v100_data[v100_p_ptr:v100_p_ptr+4096]):.3f}` bits/byte."
    )
    lines.append("")
    lines.append("Interpretation: `0xe4f4` is inside the EFI option-ROM part that begins at `0xe400`, immediately after `NPDE` metadata and compressed/high-entropy payload bytes. The `a4`/`a8` bytes are not reliable Falcon table version values. Treat envytools' `FALCON UCODE` label here as stale parser output for GV100, not as a decoded firmware-policy table.")
    lines.append("")

    lines.append("## M Table Directory")
    lines.append("")
    lines.append("| Rel | Name | CMP Ptr | V100 Ptr | Delta |")
    lines.append("| ---: | --- | ---: | ---: | ---: |")
    for rel in sorted(set(cmp_m_ptrs) | set(v100_m_ptrs)):
        c = cmp_m_ptrs.get(rel, 0)
        v = v100_m_ptrs.get(rel, 0)
        lines.append(f"| `0x{rel:02x}` | {M_NAMES.get(rel, 'UNKNOWN')} | `0x{c:04x}` | `0x{v:04x}` | `{v - c:+d}` |")
    lines.append("")

    lines.append("## M Table Candidate Deltas")
    lines.append("")
    for rel, name in ((0x01, "RESTRICT"), (0x03, "TYPE"), (0x0D, "UNK0D")):
        cptr = cmp_m_ptrs[rel]
        vptr = v100_m_ptrs[rel]
        cblk = cmp_data[cptr : cptr + 256]
        vblk = v100_data[vptr : vptr + 256]
        diffs = diff_positions(cblk, vblk, 256)
        lines.append(f"### {name}")
        lines.append("")
        lines.append(f"- CMP offset: `0x{cptr:04x}`")
        lines.append(f"- V100 offset: `0x{vptr:04x}`")
        lines.append(f"- first-256-byte diffs: `{len(diffs)}`")
        if rel == 0x03:
            lines.append(f"- CMP header: `{mem_type_summary(cmp_data, cptr)}`")
            lines.append(f"- V100 header: `{mem_type_summary(v100_data, vptr)}`")
        if rel == 0x0D:
            lines.append(f"- CMP header: `{unk0d_summary(cmp_data, cptr)}`")
            lines.append(f"- V100 header: `{unk0d_summary(v100_data, vptr)}`")
        for pos, ca, va in diffs[:16]:
            lines.append(f"- `+0x{pos:03x}`: CMP `0x{ca:02x}` -> V100 `0x{va:02x}`")
        lines.append("")

    lines.append("## Current Conclusion")
    lines.append("")
    lines.append("- The `p`/`FALCON_UCODE` lead is probably a false positive caused by a stale parser mapping into the EFI payload.")
    lines.append("- The small VBIOS-visible board/personality deltas are in `M_RESTRICT`, `M_TYPE`, and `M_UNK0D`, plus INFO/PCIR identity fields.")
    lines.append("- These deltas explain memory/board/personality metadata more directly than tensor-throughput policy; the full-speed FP16 gate is still more likely in signed GR/SEC2 firmware, driver policy keyed by identity/fuses, or hardware fuses.")
    lines.append("- The next offline control needed is a known full-speed V100 ROM plus matching benchmark result. Without that third comparator, CMP-vs-V100-personality only shows which fields changed for the 68-SM to 80-SM/profiler identity transition, not what enables full tensor throughput.")
    lines.append("")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmp", required=True, type=Path)
    parser.add_argument("--v100", required=True, type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    text = report(args.cmp, args.v100)
    if args.out:
        args.out.write_text(text)
    else:
        print(text)


if __name__ == "__main__":
    main()
