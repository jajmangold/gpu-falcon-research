#!/usr/bin/env python3
"""Focused C source scan for the GV100 VBIOS/FWSECLIC investigation.

This is not a general-purpose static analyzer. It records narrow patterns that
matter for the current review: assert-only bounds checks, unsigned reverse
walks, assignment in conditionals, hard halts on parse failures, and suspicious
copy-size pairs around VBIOS security code.
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import asdict, dataclass
from pathlib import Path


DEFAULT_FILE_LIST = Path(
    "/srv/nvme-data/containers/projects/gpu-falcon-research/"
    "runs/20260520-static-security-scan/focused-files.txt"
)


@dataclass(frozen=True)
class Finding:
    rule: str
    severity: str
    path: str
    line: int
    text: str
    rationale: str


RULES: list[tuple[str, str, re.Pattern[str], str]] = [
    (
        "assert-only-bound",
        "medium",
        re.compile(r"\bNV_?ASSERT\s*\([^;]*(<=|<|>=|>)"),
        "Bounds or size condition is enforced through an assert-style macro; verify release behavior.",
    ),
    (
        "unsigned-size-minus-one",
        "high",
        re.compile(r"\b\w+\s*=\s*\w+(?:->|\.)?size\s*-\s*1\b"),
        "Index starts from size - 1; verify size is nonzero and later decrements are bounded.",
    ),
    (
        "unguarded-decrement",
        "high",
        re.compile(r"\b\w+\s*--\s*;"),
        "Manual decrement found; in reverse scans verify a lower-bound guard exists on all paths.",
    ),
    (
        "assignment-in-condition",
        "high",
        re.compile(r"\bif\s*\([^;\n]*(?<![=!<>])=(?!=)[^;\n]*\)"),
        "Assignment appears inside an if condition; confirm it is intentional and in-bounds.",
    ),
    (
        "firmware-hard-halt",
        "medium",
        re.compile(r"\bfalc_halt\s*\(|\bwhile\s*\(\s*1\s*\)"),
        "Firmware-side failure path halts; useful for crash/DoS risk triage.",
    ),
    (
        "raw-copy",
        "low",
        re.compile(r"\b(portMemCopy|memcpy|reverseMemCopy|osMemCopy)\s*\("),
        "Raw copy call; verify source, destination, and length are independently bounded.",
    ),
    (
        "allocation-from-parser-value",
        "medium",
        re.compile(r"\b(portMemAllocNonPaged|malloc|osAllocMem)\s*\([^;\n]*(offset|len|size|Length|length)"),
        "Allocation size appears derived from parsed/variable length; verify upper/lower bounds.",
    ),
]


def scan_file(path: Path) -> list[Finding]:
    findings: list[Finding] = []
    try:
        lines = path.read_text(errors="replace").splitlines()
    except OSError as exc:
        return [
            Finding(
                "file-read-error",
                "error",
                str(path),
                0,
                str(exc),
                "File could not be read.",
            )
        ]

    for idx, line in enumerate(lines, start=1):
        stripped = line.strip()
        for rule, severity, pattern, rationale in RULES:
            if pattern.search(stripped):
                findings.append(
                    Finding(rule, severity, str(path), idx, stripped, rationale)
                )
    return findings


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--file-list",
        type=Path,
        default=DEFAULT_FILE_LIST,
        help="Newline-separated source files to scan.",
    )
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--md-out", type=Path)
    args = parser.parse_args()

    files = [
        Path(line.strip())
        for line in args.file_list.read_text().splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    findings: list[Finding] = []
    for path in files:
        findings.extend(scan_file(path))

    payload = [asdict(finding) for finding in findings]
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2) + "\n")

    if args.md_out:
        args.md_out.parent.mkdir(parents=True, exist_ok=True)
        lines = [
            "# Focused C Security Scan",
            "",
            "This scan is pattern-based and review-oriented. It is not a proof of exploitability.",
            "",
            f"- files scanned: {len(files)}",
            f"- findings: {len(findings)}",
            "",
            "| Severity | Rule | File:Line | Text |",
            "| --- | --- | --- | --- |",
        ]
        for finding in findings:
            rel = finding.path
            text = finding.text.replace("|", "\\|")
            lines.append(
                f"| {finding.severity} | `{finding.rule}` | `{rel}:{finding.line}` | `{text}` |"
            )
        lines.append("")
        args.md_out.write_text("\n".join(lines))

    counts: dict[str, int] = {}
    for finding in findings:
        counts[finding.rule] = counts.get(finding.rule, 0) + 1
    for rule, count in sorted(counts.items()):
        print(f"{rule}: {count}")


if __name__ == "__main__":
    main()
