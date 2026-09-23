#!/usr/bin/env python3
"""Build a compact inventory report for archived NVGI-wrapped VBIOS files."""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from vbios_offline_compare import (  # noqa: E402
    M_NAMES,
    diff_positions,
    entry_by_kind,
    parse_bit,
    parse_m_table,
    parse_rom_parts,
    u32,
)


@dataclass(frozen=True)
class ImageInfo:
    name: str
    container: Path
    pcirom: Path
    container_md5: str
    container_sha1: str
    container_sha256: str
    pcirom_sha256: str
    device_id: int
    code_class: str
    bios_version: str
    sku_text: str
    info_unk30: str
    m_ptrs: dict[int, int]
    p0: int
    p0_byte: int


def digest(data: bytes, algo: str) -> str:
    h = hashlib.new(algo)
    h.update(data)
    return h.hexdigest()


def ascii_strings(data: bytes, min_len: int = 8) -> list[str]:
    return [m.group(0).decode("ascii", "replace") for m in re.finditer(rb"[\x20-\x7e]{%d,}" % min_len, data)]


def first_match(strings: list[str], pattern: str, default: str = "") -> str:
    rx = re.compile(pattern)
    for s in strings:
        m = rx.search(s)
        if m:
            return m.group(0)
    return default


def image_info(container: Path, pcirom: Path) -> ImageInfo:
    cdata = container.read_bytes()
    rdata = pcirom.read_bytes()
    parts = parse_rom_parts(rdata)
    bit, entries = parse_bit(rdata)
    del bit
    m_entry = entry_by_kind(entries, "M")
    p_entry = entry_by_kind(entries, "p")
    m_ptrs = parse_m_table(rdata, m_entry)
    p0 = u32(rdata, p_entry.table_offset)
    strings = ascii_strings(rdata)
    sku_text = first_match(strings, r"(GV100|GP100) [A-Z0-9]+ SKU [0-9]+ [A-Z0-9 ]+VBIOS|GP100 [A-Z0-9]+ SKU [0-9]+ [A-Z0-9 ]+BIOS")
    bios_version = first_match(strings, r"Version [0-9A-Fa-f]{2}\.[0-9A-Fa-f]{2}\.[0-9A-Fa-f]{2}\.[0-9A-Fa-f]{2}\.[0-9A-Fa-f]{2}")
    info_unk30 = first_match(strings, r"[0-9]{4}G[0-9]{4}[0-9]{3}")
    device = parts[0].device_id if parts else 0
    code_class = "compute" if device in {0x1DB4, 0x1DB6, 0x1DF4} else "graphics"
    return ImageInfo(
        name=container.name,
        container=container,
        pcirom=pcirom,
        container_md5=digest(cdata, "md5"),
        container_sha1=digest(cdata, "sha1"),
        container_sha256=digest(cdata, "sha256"),
        pcirom_sha256=digest(rdata, "sha256"),
        device_id=device,
        code_class=code_class,
        bios_version=bios_version.removeprefix("Version "),
        sku_text=sku_text.strip(" (,"),
        info_unk30=info_unk30,
        m_ptrs=m_ptrs,
        p0=p0,
        p0_byte=rdata[p0] if p0 < len(rdata) else -1,
    )


def m_diff_counts(a: ImageInfo, b: ImageInfo) -> dict[int, int]:
    adata = a.pcirom.read_bytes()
    bdata = b.pcirom.read_bytes()
    counts: dict[int, int] = {}
    for rel in (0x01, 0x03, 0x0D):
        ap = a.m_ptrs.get(rel)
        bp = b.m_ptrs.get(rel)
        if ap is None or bp is None:
            continue
        counts[rel] = len(diff_positions(adata[ap : ap + 256], bdata[bp : bp + 256], 256))
    return counts


def report(containers: Path, pciroms: Path, cmp_rom: Path, out: Path) -> None:
    infos = []
    for container in sorted(containers.glob("*.rom")):
        pcirom = pciroms / f"{container.stem}-extracted-pcirom.rom"
        if pcirom.exists():
            infos.append(image_info(container, pcirom))

    cmp_container = cmp_rom
    cmp_info = image_info(cmp_container, pciroms / "266855-extracted-pcirom.rom") if cmp_container.name == "266855.rom" else None
    control = next((i for i in infos if i.device_id == 0x1DB4), None)
    personality = next((i for i in infos if i.device_id == 0x1DF4), None)

    lines: list[str] = []
    lines.append("# Temp VBIOS Inventory - 2026-05-19")
    lines.append("")
    lines.append("All supplied files are NVIDIA `NVGI` containers with raw PCI ROM payloads extracted at `0x0a00`.")
    lines.append("")
    lines.append("## Identity Summary")
    lines.append("")
    lines.append("| File | Device | Class | Version | SKU / Board Text | INFO unk30 | p[0] byte |")
    lines.append("| --- | ---: | --- | --- | --- | --- | ---: |")
    for info in infos:
        pbyte = f"`0x{info.p0_byte:02x}`" if info.p0_byte >= 0 else ""
        lines.append(
            f"| `{info.name}` | `10de:{info.device_id:04x}` | {info.code_class} | `{info.bios_version}` | "
            f"`{info.sku_text}` | `{info.info_unk30}` | {pbyte} |"
        )
    lines.append("")

    if personality and control:
        lines.append("## Candidate Priority")
        lines.append("")
        lines.append("- Best full-speed control: `NVIDIA.TeslaV100.16384.170728.rom` (`10de:1db4`, SKU 200).")
        lines.append("- Strong additional control: `NVIDIA.TeslaV100.32768.180223.rom` (`10de:1db6`, SKU 202, V100 32GB).")
        lines.append("- Useful workstation comparators: Quadro GV100 / Titan V images, but they are graphics-class identities.")
        lines.append("- `PNY.QuadroGP100.16384.171121.rom` is GP100, so keep it as an architecture outgroup only.")
        lines.append("")

    lines.append("## Container Hashes")
    lines.append("")
    lines.append("| File | MD5 | SHA1 |")
    lines.append("| --- | --- | --- |")
    for info in infos:
        lines.append(f"| `{info.name}` | `{info.container_md5}` | `{info.container_sha1}` |")
    lines.append("")

    lines.append("## M Table Delta Counts")
    lines.append("")
    lines.append("First-256-byte diff counts for the three narrow decoded M subtables.")
    lines.append("")
    refs = [i for i in (personality, control) if i is not None]
    for ref in refs:
        lines.append(f"### Relative to `{ref.name}`")
        lines.append("")
        lines.append("| File | M_RESTRICT | M_TYPE | M_UNK0D |")
        lines.append("| --- | ---: | ---: | ---: |")
        for info in infos:
            counts = m_diff_counts(ref, info)
            lines.append(
                f"| `{info.name}` | `{counts.get(0x01, -1)}` | `{counts.get(0x03, -1)}` | `{counts.get(0x0D, -1)}` |"
            )
        lines.append("")

    lines.append("## Current Read")
    lines.append("")
    lines.append("- The V100 16GB and V100 32GB ROMs are the most relevant full-speed controls because they are compute-class GV100 Tesla identities.")
    lines.append("- The BIT `p[0x00]` pointer remains `0x0e4f4` across these GV100 images and points into the EFI payload; the differing byte at that location reinforces the false-positive Falcon-table conclusion.")
    lines.append("- The next useful offline comparison is to intersect fields that are shared by both Tesla V100 controls but differ from the local CMP/V100-personality pair.")
    lines.append("")
    out.write_text("\n".join(lines))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--containers", required=True, type=Path)
    parser.add_argument("--pciroms", required=True, type=Path)
    parser.add_argument("--cmp-container", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    report(args.containers, args.pciroms, args.cmp_container, args.out)


if __name__ == "__main__":
    main()
