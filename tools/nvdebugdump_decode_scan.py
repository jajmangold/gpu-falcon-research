#!/usr/bin/env python3
"""Decode and scan decrypted NVIDIA debugdump payloads.

This is an offline artifact tool. It expects debugdump zips to have already
been decrypted with the local nvdzip extractor.
"""

from __future__ import annotations

import argparse
import json
import subprocess
from dataclasses import asdict, dataclass
from pathlib import Path


DEFAULT_PROTOC = Path(
    "/srv/nvme-data/containers/acestep/venv/lib/python3.12/site-packages/torch/bin/protoc"
)
DEFAULT_PROTO_DIR = Path(
    "/srv/nvme-data/containers/projects/gpu-falcon-research/runs/"
    "20260520-public-debugdump-search/proto-work"
)

TERMS = [
    "GV100",
    "V100",
    "Tesla",
    "CMP",
    "FECS",
    "SM_SPEED",
    "409660",
    "409664",
    "PGRAPH",
    "InfoROM",
    "Inforom",
    "OBD",
    "OEM",
    "FUSE",
    "HMMA",
    "TENSOR",
    "SPEED",
    "GPC",
    "TPC",
    "PLM",
    "PRIV",
    "FALCON",
]


@dataclass
class DecodeResult:
    source: str
    message: str
    output: str
    returncode: int
    stderr_tail: str


@dataclass
class Hit:
    file: str
    term: str
    line: int
    text: str


def decode_file(protoc: Path, proto_dir: Path, source: Path, message: str) -> DecodeResult:
    out = source.with_suffix(source.suffix + f".{message.replace('.', '_')}.txt")
    proc = subprocess.run(
        [
            str(protoc),
            f"-I{proto_dir}",
            f"--decode={message}",
            str(proto_dir / "nvdebug.proto"),
        ],
        input=source.read_bytes(),
        capture_output=True,
    )
    out.write_bytes(proc.stdout)
    err = proc.stderr.decode("utf-8", "replace")
    if err:
        out.with_suffix(out.suffix + ".err").write_text(err)
    return DecodeResult(
        source=str(source),
        message=message,
        output=str(out),
        returncode=proc.returncode,
        stderr_tail=err[-2000:],
    )


def printable_text(data: bytes) -> str:
    return "".join(chr(b) if 32 <= b < 127 or b in (9, 10, 13) else "\n" for b in data)


def scan_text_file(path: Path, root: Path) -> list[Hit]:
    try:
        text = path.read_text("utf-8", "replace")
    except UnicodeDecodeError:
        text = printable_text(path.read_bytes())
    hits: list[Hit] = []
    for idx, line in enumerate(text.splitlines(), 1):
        for term in TERMS:
            if term.lower() in line.lower():
                hits.append(
                    Hit(
                        file=str(path.relative_to(root)),
                        term=term,
                        line=idx,
                        text=line[:240],
                    )
                )
    return hits


def decode_tree(root: Path, protoc: Path, proto_dir: Path) -> dict:
    results: list[DecodeResult] = []
    for source in sorted(root.rglob("*.pb")):
        name = source.name
        if name.startswith("debug_buffers_"):
            continue
        if name == "system_info.pb":
            messages = ["NvDebug.SystemInfo"]
        elif name.startswith("rm_") or name == "error_data.pb":
            messages = ["NvDebug.NvDump", "NvDebug.GpuInfo"]
        else:
            messages = ["NvDebug.NvDump"]
        for message in messages:
            results.append(decode_file(protoc, proto_dir, source, message))

    hit_files = [
        p
        for p in root.rglob("*")
        if p.is_file() and (p.suffix in {".txt", ".log"} or p.name.endswith(".pb"))
    ]
    hits = []
    for path in sorted(hit_files):
        hits.extend(scan_text_file(path, root))

    return {
        "root": str(root),
        "decodes": [asdict(item) for item in results],
        "hits": [asdict(hit) for hit in hits],
    }


def write_markdown(report: dict, out_path: Path) -> None:
    lines = [
        "# NvDebugDump Decode Scan",
        "",
        "Offline scan of decrypted `nvidia-debugdump` payloads using local NVIDIA",
        "protobuf schemas.",
        "",
    ]
    for tree in report["trees"]:
        lines.append(f"## {tree['root']}")
        lines.append("")
        lines.append("| Decoded file | Message | Return code |")
        lines.append("| --- | --- | ---: |")
        for item in tree["decodes"]:
            lines.append(f"| `{item['output']}` | `{item['message']}` | {item['returncode']} |")
        lines.append("")
        if not tree["hits"]:
            lines.append("No tracked terms found.")
            lines.append("")
            continue
        lines.append("| File | Term | Line | Text |")
        lines.append("| --- | --- | ---: | --- |")
        for hit in tree["hits"][:300]:
            text = hit["text"].replace("|", "\\|")
            lines.append(f"| `{hit['file']}` | `{hit['term']}` | {hit['line']} | {text} |")
        if len(tree["hits"]) > 300:
            lines.append(f"| ... | ... | ... | {len(tree['hits']) - 300} more hits omitted |")
        lines.append("")
    out_path.write_text("\n".join(lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("roots", nargs="+", type=Path)
    parser.add_argument("--protoc", type=Path, default=DEFAULT_PROTOC)
    parser.add_argument("--proto-dir", type=Path, default=DEFAULT_PROTO_DIR)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    report = {
        "terms": TERMS,
        "protoc": str(args.protoc),
        "proto_dir": str(args.proto_dir),
        "trees": [decode_tree(root, args.protoc, args.proto_dir) for root in args.roots],
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    write_markdown(report, args.out.with_suffix(".md"))
    print(args.out)
    print(args.out.with_suffix(".md"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
