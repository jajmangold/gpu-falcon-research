#!/usr/bin/env python3
"""Read-only CUDA tensor-path probe for one visible GPU."""

from __future__ import annotations

import argparse
import json
import os
import platform
import subprocess
import time
from pathlib import Path
from typing import Any

import torch


def run_cmd(args: list[str]) -> dict[str, Any]:
    try:
        proc = subprocess.run(args, check=False, capture_output=True, text=True)
        return {
            "cmd": args,
            "returncode": proc.returncode,
            "stdout": proc.stdout.strip(),
            "stderr": proc.stderr.strip(),
        }
    except FileNotFoundError as exc:
        return {"cmd": args, "error": str(exc)}


def device_info(device: torch.device) -> dict[str, Any]:
    props = torch.cuda.get_device_properties(device)
    capability = [getattr(props, "major", None), getattr(props, "minor", None)]
    attrs: dict[str, Any] = {
        "name": props.name,
        "capability": capability,
    }
    for key in [
        "multi_processor_count",
        "total_memory",
        "shared_memory_per_block",
        "regs_per_block",
        "warp_size",
        "max_threads_per_block",
        "max_threads_per_multi_processor",
        "is_multi_gpu_board",
        "is_integrated",
        "managed_memory",
        "concurrent_kernels",
        "async_engine_count",
        "memory_clock_rate",
        "memory_bus_width",
        "l2_cache_size",
    ]:
        if hasattr(props, key):
            attrs[key] = getattr(props, key)
    if "total_memory" in attrs:
        attrs["total_memory_bytes"] = attrs.pop("total_memory")
    return attrs


def bench_matmul(
    device: torch.device,
    dtype: torch.dtype,
    n: int,
    warmup: int,
    iters: int,
) -> dict[str, Any]:
    torch.cuda.empty_cache()
    a = torch.randn((n, n), device=device, dtype=dtype)
    b = torch.randn((n, n), device=device, dtype=dtype)

    # Force allocation and cuBLAS setup outside the timed region.
    c = a @ b
    torch.cuda.synchronize(device)
    del c

    for _ in range(warmup):
        c = a @ b
    torch.cuda.synchronize(device)

    start = torch.cuda.Event(enable_timing=True)
    end = torch.cuda.Event(enable_timing=True)
    start.record()
    for _ in range(iters):
        c = a @ b
    end.record()
    torch.cuda.synchronize(device)

    elapsed_ms = start.elapsed_time(end)
    avg_ms = elapsed_ms / iters
    tflops = (2.0 * n * n * n) / (avg_ms / 1000.0) / 1.0e12
    checksum = float(c.float().mean().detach().cpu())

    del a, b, c
    torch.cuda.empty_cache()
    return {
        "dtype": str(dtype).replace("torch.", ""),
        "n": n,
        "warmup": warmup,
        "iters": iters,
        "elapsed_ms": elapsed_ms,
        "avg_ms": avg_ms,
        "tflops": tflops,
        "checksum_mean": checksum,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--host-gpu-index", required=True)
    parser.add_argument("--host-bus-id", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--fp16-size", type=int, default=8192)
    parser.add_argument("--fp32-size", type=int, default=4096)
    parser.add_argument("--warmup", type=int, default=5)
    parser.add_argument("--iters", type=int, default=20)
    args = parser.parse_args()

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)

    result: dict[str, Any] = {
        "label": args.label,
        "host_gpu_index": args.host_gpu_index,
        "host_bus_id": args.host_bus_id,
        "timestamp_unix": time.time(),
        "platform": platform.platform(),
        "python": platform.python_version(),
        "env": {
            "CUDA_VISIBLE_DEVICES": os.environ.get("CUDA_VISIBLE_DEVICES"),
            "NVIDIA_VISIBLE_DEVICES": os.environ.get("NVIDIA_VISIBLE_DEVICES"),
        },
        "torch": {
            "version": torch.__version__,
            "cuda": torch.version.cuda,
            "cudnn": torch.backends.cudnn.version(),
            "cuda_available": torch.cuda.is_available(),
            "device_count": torch.cuda.device_count(),
            "allow_tf32_matmul": torch.backends.cuda.matmul.allow_tf32,
            "allow_tf32_cudnn": torch.backends.cudnn.allow_tf32,
        },
        "commands": {
            "nvidia_smi_query": run_cmd(
                [
                    "nvidia-smi",
                    "--query-gpu=index,pci.bus_id,name,uuid,memory.total,memory.used,utilization.gpu,clocks.sm,clocks.mem,power.draw,power.limit",
                    "--format=csv,noheader,nounits",
                ]
            ),
            "nvidia_smi_q": run_cmd(["nvidia-smi", "-q", "-i", "0"]),
        },
        "device": None,
        "benchmarks": [],
        "errors": [],
    }

    if not torch.cuda.is_available():
        result["errors"].append("CUDA is not available")
        out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
        return 2

    device = torch.device("cuda:0")
    torch.cuda.set_device(device)
    result["device"] = device_info(device)

    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False

    for dtype, size in [(torch.float16, args.fp16_size), (torch.float32, args.fp32_size)]:
        try:
            result["benchmarks"].append(
                bench_matmul(device, dtype, size, args.warmup, args.iters)
            )
        except Exception as exc:  # noqa: BLE001 - record benchmark failure verbatim
            result["errors"].append(f"{dtype}: {type(exc).__name__}: {exc}")

    out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    return 0 if not result["errors"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
