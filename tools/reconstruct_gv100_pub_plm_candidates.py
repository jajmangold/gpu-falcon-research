#!/usr/bin/env python3
"""
Build a read-only candidate PLM table from GV100 hwref headers.

This does not recover NVIDIA's internal PUB PLM list. It reconstructs the
local, source-visible side: registers that declare a __PRIV_LEVEL_MASK pointer
and the PLM register names that share the pointed-to address.
"""

from __future__ import annotations

import csv
import re
import sys
from collections import defaultdict
from pathlib import Path


DEFINE_RE = re.compile(r"^#define\s+([A-Za-z0-9_()]+)\s+(.+?)\s*(?:/\*.*)?$")
HEX_RE = re.compile(r"0x[0-9a-fA-F]+")


def parse_value(raw: str) -> int | None:
    match = HEX_RE.search(raw)
    if not match:
        return None
    return int(match.group(0), 16)


def clean_name(name: str) -> str:
    return name.replace("(i)", "").replace("(n)", "")


def priority_for(name: str, plm_names: list[str]) -> str:
    haystack = " ".join([name, *plm_names])
    if "FEATURE_OVERRIDE_SM_SPEED_SELECT" in haystack:
        return "critical-speed-select"
    if "FEATURE_OVERRIDE" in haystack and "FECS" in haystack:
        return "high-fecs-feature-override"
    if "PGRAPH_PRI_FECS" in haystack or "CTXSW" in haystack or "GPCCS" in haystack:
        return "high-ctxsw-fecs-gpccs"
    if "CPWR" in haystack or "PMU" in haystack or "PWR" in haystack:
        return "medium-pmu"
    return "normal"


def main() -> int:
    if len(sys.argv) != 3:
        print(
            "usage: reconstruct_gv100_pub_plm_candidates.py <gv100-hwref-dir> <out.csv>",
            file=sys.stderr,
        )
        return 2

    root = Path(sys.argv[1])
    out = Path(sys.argv[2])
    if not root.is_dir():
        print(f"not a directory: {root}", file=sys.stderr)
        return 2

    constants: dict[str, tuple[int, str, int]] = {}
    priv_links: list[tuple[str, int, str, int]] = []

    for path in sorted(root.glob("*.h")):
        try:
            lines = path.read_text(errors="ignore").splitlines()
        except OSError:
            continue
        for lineno, line in enumerate(lines, 1):
            match = DEFINE_RE.match(line.strip())
            if not match:
                continue
            name = clean_name(match.group(1))
            value = parse_value(match.group(2))
            if value is None:
                continue
            constants[name] = (value, str(path), lineno)
            if name.endswith("__PRIV_LEVEL_MASK"):
                reg_name = name[: -len("__PRIV_LEVEL_MASK")]
                priv_links.append((reg_name, value, str(path), lineno))

    plm_names_by_addr: dict[int, list[str]] = defaultdict(list)
    plm_names_by_low_offset: dict[int, list[str]] = defaultdict(list)
    for name, (value, _path, _lineno) in constants.items():
        if name.endswith("PRIV_LEVEL_MASK") and not name.endswith("__PRIV_LEVEL_MASK"):
            plm_names_by_addr[value].append(name)
            plm_names_by_low_offset[value & 0xFFF].append(name)

    rows = []
    for reg_name, plm_addr, source, line in priv_links:
        reg_value = constants.get(reg_name, (None, "", 0))[0]
        resolved_plm_addr = plm_addr
        if reg_value is not None and plm_addr < 0x1000 and reg_value >= 0x1000:
            resolved_plm_addr = (reg_value & ~0xFFF) + plm_addr
        plm_names = sorted(set(plm_names_by_addr.get(resolved_plm_addr, [])))
        if not plm_names:
            plm_names = sorted(set(plm_names_by_low_offset.get(plm_addr & 0xFFF, [])))
        rows.append(
            {
                "priority": priority_for(reg_name, plm_names),
                "register": reg_name,
                "register_addr": "" if reg_value is None else f"0x{reg_value:08x}",
                "plm_offset": f"0x{plm_addr:08x}",
                "resolved_plm_addr": f"0x{resolved_plm_addr:08x}",
                "plm_names": ";".join(plm_names),
                "source": source,
                "line": line,
            }
        )

    order = {
        "critical-speed-select": 0,
        "high-fecs-feature-override": 1,
        "high-ctxsw-fecs-gpccs": 2,
        "medium-pmu": 3,
        "normal": 4,
    }
    rows.sort(
        key=lambda r: (
            order.get(r["priority"], 99),
            r["resolved_plm_addr"],
            r["plm_offset"],
            r["register"],
        )
    )

    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=[
                "priority",
                "register",
                "register_addr",
                "plm_offset",
                "resolved_plm_addr",
                "plm_names",
                "source",
                "line",
            ],
        )
        writer.writeheader()
        writer.writerows(rows)

    print(f"wrote {len(rows)} rows to {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
