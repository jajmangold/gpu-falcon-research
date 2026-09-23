#!/usr/bin/env python3
"""Decode visible InfoROM OBD payload prefixes from protobuf text dumps."""

from __future__ import annotations

import argparse
import ast
import re
from dataclasses import dataclass
from pathlib import Path


PAYLOAD_RE = re.compile(r'(?:payload|12):\s*("(?:[^"\\]|\\.)*")')


@dataclass(frozen=True)
class ObdHit:
    file: Path
    line: int
    size: int
    checksum: int
    build_date: str
    marketing_prefix: str
    raw_hex: str


def decode_escaped_string(token: str) -> bytes:
    value = ast.literal_eval(token)
    return value.encode("latin1")


def decode_build_date(raw: bytes) -> str:
    if len(raw) < 12:
        return "short"
    day = ((raw[8] >> 4) * 10) + (raw[8] & 0x0f)
    month = ((raw[9] >> 4) * 10) + (raw[9] & 0x0f)
    year = 2000 + ((raw[10] >> 4) * 10) + (raw[10] & 0x0f)
    return f"{year:04d}-{month:02d}-{day:02d}"


def printable_field(raw: bytes) -> str:
    text = raw.split(b"\x00", 1)[0].decode("ascii", "replace")
    return text.strip()


def scan_file(path: Path, root: Path) -> list[ObdHit]:
    hits: list[ObdHit] = []
    for line_no, line in enumerate(path.read_text("utf-8", "replace").splitlines(), 1):
        match = PAYLOAD_RE.search(line)
        if not match:
            continue
        raw = decode_escaped_string(match.group(1))
        if not raw.startswith(b"OBD\x01\x01"):
            continue
        size = int.from_bytes(raw[5:7], "little") if len(raw) >= 7 else 0
        checksum = raw[7] if len(raw) >= 8 else 0
        hits.append(
            ObdHit(
                file=path.relative_to(root),
                line=line_no,
                size=size,
                checksum=checksum,
                build_date=decode_build_date(raw),
                marketing_prefix=printable_field(raw[12:36]),
                raw_hex=raw.hex(),
            )
        )
    return hits


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("roots", nargs="+", type=Path)
    args = parser.parse_args()

    print("| Root | File | Line | OBD size | Checksum | Build date | Marketing prefix | Raw prefix |")
    print("| --- | --- | ---: | ---: | ---: | --- | --- | --- |")
    for root in args.roots:
        for path in sorted(root.rglob("*.txt")):
            for hit in scan_file(path, root):
                print(
                    f"| `{root}` | `{hit.file}` | {hit.line} | {hit.size} | "
                    f"0x{hit.checksum:02x} | {hit.build_date} | `{hit.marketing_prefix}` | "
                    f"`{hit.raw_hex}` |"
                )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
