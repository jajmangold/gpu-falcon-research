#!/usr/bin/env python3
"""Z3 model for the vbiosRSADecrypt-style PKCS#1 padding walk.

This models only the post-RSA-decrypt byte walk:

    offset = res.size - 1
    require res[offset] == 0
    offset--
    require res[offset] == 1
    offset--
    while res[offset] == 0xff:
        offset--
    require res[offset] == 0

The goal is to prove whether simple buffer shapes exist that make the walk
read outside the modeled byte array. It does not model RSA, RM, VBIOS flashing,
or GPU hardware.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import asdict, dataclass
from pathlib import Path

from z3 import BitVec, BitVecVal, Solver, sat, simplify


@dataclass(frozen=True)
class CaseResult:
    name: str
    result: str
    size: int
    explanation: str
    bytes_hex: list[str]


def solve_valid(size: int = 16, delimiter: int = 4) -> CaseResult:
    """Find a valid-shaped buffer returning out_len == delimiter."""
    b = [BitVec(f"valid_b{i}", 8) for i in range(size)]
    s = Solver()
    s.add(b[size - 1] == 0x00)
    s.add(b[size - 2] == 0x01)
    for i in range(delimiter + 1, size - 2):
        s.add(b[i] == 0xFF)
    s.add(b[delimiter] == 0x00)
    for i in range(delimiter):
        s.add(b[i] != 0x00)

    assert s.check() == sat
    model = s.model()
    vals = [model.eval(x, model_completion=True).as_long() for x in b]
    return CaseResult(
        "valid-shaped",
        "sat",
        size,
        f"Valid PKCS#1-like shape exists; reverse walk stops at delimiter offset {delimiter}.",
        [f"{x:02x}" for x in vals],
    )


def solve_too_small() -> CaseResult:
    """A size-1 buffer satisfies the first check, then offset underflows."""
    size = 1
    b0 = BitVec("small_b0", 8)
    s = Solver()
    s.add(b0 == 0x00)
    assert s.check() == sat
    return CaseResult(
        "too-small-underflow",
        "sat",
        size,
        "With res.size == 1 and b[0] == 0, the first check passes, then offset-- wraps before the second byte read.",
        ["00"],
    )


def solve_unterminated_ff(size: int = 16) -> CaseResult:
    """A buffer with no 0x00 delimiter below padding makes the loop underflow."""
    b = [BitVec(f"unterminated_b{i}", 8) for i in range(size)]
    s = Solver()
    s.add(b[size - 1] == 0x00)
    s.add(b[size - 2] == 0x01)
    for i in range(size - 2):
        s.add(b[i] == 0xFF)

    assert s.check() == sat
    model = s.model()
    vals = [model.eval(x, model_completion=True).as_long() for x in b]

    unsigned_after_loop = simplify(BitVecVal(0, 32) - BitVecVal(1, 32))
    return CaseResult(
        "unterminated-ff-padding",
        "sat",
        size,
        "Trailing 00/01 checks pass, every lower byte is 0xff, and the loop decrements below zero; unsigned offset becomes "
        f"0x{unsigned_after_loop.as_long():08x} before the next read.",
        [f"{x:02x}" for x in vals],
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--md-out", type=Path)
    args = parser.parse_args()

    cases = [solve_valid(), solve_too_small(), solve_unterminated_ff()]
    payload = [asdict(case) for case in cases]

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2) + "\n")

    if args.md_out:
        args.md_out.parent.mkdir(parents=True, exist_ok=True)
        lines = [
            "# VBIOS RSA Padding Walk SMT Model",
            "",
            "This is a bounded Z3 model of the post-RSA-decrypt padding walk only.",
            "",
            "| Case | Result | Size | Bytes | Explanation |",
            "| --- | --- | --- | --- | --- |",
        ]
        for case in cases:
            lines.append(
                f"| `{case.name}` | `{case.result}` | {case.size} | `{ ' '.join(case.bytes_hex) }` | {case.explanation} |"
            )
        lines.append("")
        args.md_out.write_text("\n".join(lines))

    for case in cases:
        print(f"{case.name}: {case.result}: {case.explanation}")


if __name__ == "__main__":
    main()
