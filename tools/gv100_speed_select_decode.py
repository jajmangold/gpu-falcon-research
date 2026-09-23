#!/usr/bin/env python3
"""Decode the GV100 SM speed-select fields found in nvdrv source.

This is an offline helper. It does not touch hardware. Pass register values as
hex strings when you have read-only dumps from a maintenance environment.
"""

from __future__ import annotations

import argparse


FIELDS = {
    "ctxsw_feature_readout": {
        "addr": 0x19900,
        "fields": {
            "dp_reduced": 20,
            "imla_reduced": 21,
            "fmla_reduced": 22,
        },
    },
    "ctxsw_feature_override_sm_speed_select": {
        "addr": 0x19900,
        "fields": {
            "imla_reduced": 0,
            "imla_override": 3,
            "fmla_reduced": 4,
            "fmla_override": 7,
            "dp_reduced": 8,
            "dp_override": 11,
        },
    },
    "nv_fuse_opt_dp_speed_select": {
        "addr": 0x21224,
        "fields": {"dp_reduced": 0},
    },
    "nv_fuse_opt_feature_fuses_override_disable": {
        "addr": 0x213F0,
        "fields": {"feature_fuse_overrides_disabled": 0},
    },
    "nv_fuse_opt_internal_sku": {
        "addr": 0x213F4,
        "fields": {"internal_sku": 0},
    },
    "nv_fuse_opt_sm_imla_speed_select": {
        "addr": 0x21410,
        "fields": {"imla_reduced": 0},
        "raw_alias": {"row": 0x10, "data_bit": 17, "control_bit": 18},
    },
    "nv_fuse_opt_sm_fmla_speed_select": {
        "addr": 0x214E0,
        "fields": {"fmla_reduced": 0},
        "raw_alias": {"row": 0x12, "data_bit": 10, "control_bit": 11},
    },
    "nv_fuse_opt_shf_speed_select": {
        "addr": 0x2159C,
        "fields": {"shf_reduced": 0},
        "raw_alias": {"row": 0x16, "data_bit": 10, "control_bit": 11},
    },
}


def parse_int(text: str) -> int:
    return int(text, 0)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "values",
        nargs="*",
        help="name=value pairs, for example nv_fuse_opt_sm_imla_speed_select=0x1",
    )
    parser.add_argument("--list", action="store_true", help="list known fields")
    args = parser.parse_args()

    if args.list:
        for name, spec in FIELDS.items():
            print(f"{name} @ 0x{spec['addr']:05x}")
            for field, bit in spec["fields"].items():
                print(f"  bit {bit:2d}: {field}")
            if "raw_alias" in spec:
                alias = spec["raw_alias"]
                print(
                    f"  raw alias row 0x{alias['row']:02x}: "
                    f"data bit {alias['data_bit']}, control bit {alias['control_bit']}"
                )
        return

    for pair in args.values:
        name, _, raw = pair.partition("=")
        if not raw or name not in FIELDS:
            raise SystemExit(f"expected known name=value, got {pair!r}")
        value = parse_int(raw)
        print(f"{name} = 0x{value:08x}")
        for field, bit in FIELDS[name]["fields"].items():
            print(f"  {field}: {(value >> bit) & 1}")


if __name__ == "__main__":
    main()
