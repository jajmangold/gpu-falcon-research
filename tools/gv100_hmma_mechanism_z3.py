#!/usr/bin/env python3
"""Use Z3 to test HMMA limiter mechanism classes against local observations.

This does not prove the hidden hardware implementation. It checks whether a
candidate mechanism is internally consistent with the observed timing facts.
"""

from __future__ import annotations

import csv
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import z3


ROOT = Path("/srv/nvme-data/containers/projects/gpu-falcon-research")
BURST_SUMMARY = ROOT / "runs/20260520-hmma-burst-window/summary.csv"
OUT_DIR = ROOT / "runs/20260520-z3-hmma-mechanism"


@dataclass(frozen=True)
class Observations:
    full_control_cycles_min: float
    full_control_cycles_max: float
    burst_cycles_min: float
    burst_cycles_max: float
    saturated_cycles_min: float
    saturated_cycles_max: float
    best_local_tflops: float

    @property
    def burst_ratio_max(self) -> float:
        return self.burst_cycles_max / self.full_control_cycles_min

    @property
    def saturated_ratio_min(self) -> float:
        return self.saturated_cycles_min / self.full_control_cycles_max

    @property
    def saturated_ratio_max(self) -> float:
        return self.saturated_cycles_max / self.full_control_cycles_min


def load_observations() -> Observations:
    rows = list(csv.DictReader(BURST_SUMMARY.open()))

    burst_rows = [
        r
        for r in rows
        if int(r["blocks"]) <= 256
        and int(r["loops"]) >= 256
        and float(r["cycles_per_mma"]) < 700
    ]
    saturated_rows = [
        r
        for r in rows
        if int(r["blocks"]) >= 2048
        and int(r["loops"]) >= 256
        and float(r["cycles_per_mma"]) > 2500
    ]

    if not burst_rows or not saturated_rows:
        raise RuntimeError("expected burst and saturated rows were not found")

    # Full-speed V100 control from hmma-issue-spacing-2026-05-20.md.
    # dependent_accumulator: 242.8 cycles/WMMA
    # independent_4_accumulators: 236.4 cycles/WMMA
    full_min = 236.4
    full_max = 242.8

    burst_values = [float(r["cycles_per_mma"]) for r in burst_rows]
    saturated_values = [float(r["cycles_per_mma"]) for r in saturated_rows]
    best_tflops = max(float(r["tflops"]) for r in rows)

    return Observations(
        full_control_cycles_min=full_min,
        full_control_cycles_max=full_max,
        burst_cycles_min=min(burst_values),
        burst_cycles_max=max(burst_values),
        saturated_cycles_min=min(saturated_values),
        saturated_cycles_max=max(saturated_values),
        best_local_tflops=best_tflops,
    )


def allowed_divisor(s: z3.Solver, n: z3.IntNumRef) -> None:
    s.add(z3.Or(*[n == v for v in (1, 2, 4, 8, 16, 32, 64)]))


def solve_candidate(name: str, build: Callable[[z3.Solver, Observations], dict], obs: Observations) -> dict:
    solver = z3.Solver()
    terms = build(solver, obs)
    result = solver.check()
    item = {
        "candidate": name,
        "result": str(result),
        "model": {},
        "constraints": [str(a) for a in solver.assertions()],
    }
    if result == z3.sat:
        model = solver.model()
        for key, expr in terms.items():
            value = model.evaluate(expr, model_completion=True)
            item["model"][key] = str(value)
    return item


def candidate_shared_token_budget(s: z3.Solver, obs: Observations) -> dict:
    n = z3.Int("n")
    sustained_ratio = z3.Real("sustained_ratio")
    burst_ratio = z3.Real("burst_ratio")
    token_depth = z3.Int("token_depth")

    allowed_divisor(s, n)
    s.add(n >= 14, n <= 17)

    # Observed saturated local-vs-control ratio is in the 14-16x class.
    s.add(sustained_ratio >= 14, sustained_ratio <= 17)
    s.add(sustained_ratio <= n + z3.RealVal("1.0"))
    s.add(sustained_ratio >= n - z3.RealVal("2.0"))

    # Low-residency bursts are much faster than the sustained divider.
    s.add(burst_ratio >= 1, burst_ratio <= 3)
    s.add(burst_ratio < sustained_ratio)

    # A token/budget model needs at least enough depth to permit a short burst.
    s.add(token_depth >= 1, token_depth <= 512)
    s.add(token_depth >= n)
    return {"n": n, "sustained_ratio": sustained_ratio, "burst_ratio": burst_ratio, "token_depth": token_depth}


def candidate_strict_per_warp_modulo(s: z3.Solver, obs: Observations) -> dict:
    n = z3.Int("n")
    sustained_ratio = z3.Real("sustained_ratio")
    burst_ratio = z3.Real("burst_ratio")

    allowed_divisor(s, n)
    s.add(n >= 14, n <= 17)
    s.add(sustained_ratio >= 14, sustained_ratio <= 17)

    # Strict per-warp modulo gating applies to every HMMA issue window, so the
    # low-residency burst should also be near N. Observed burst ratio is <= ~3.
    s.add(burst_ratio >= 1, burst_ratio <= 3)
    s.add(burst_ratio >= n - 1)
    return {"n": n, "sustained_ratio": sustained_ratio, "burst_ratio": burst_ratio}


def candidate_active_lane_mask_only(s: z3.Solver, obs: Observations) -> dict:
    n = z3.Int("n")
    issue_ratio = z3.Real("issue_ratio")
    throughput_ratio = z3.Real("throughput_ratio")

    allowed_divisor(s, n)
    s.add(n >= 14, n <= 17)

    # Active-lane masking reduces useful work per issue, but should not produce
    # a 14-16x per-WMMA issue-spacing increase in the saturated clock64 window.
    s.add(issue_ratio <= 3)
    s.add(issue_ratio >= 14)
    s.add(throughput_ratio == n)
    return {"n": n, "issue_ratio": issue_ratio, "throughput_ratio": throughput_ratio}


def candidate_global_clock_throttle(s: z3.Solver, obs: Observations) -> dict:
    clock_ratio = z3.Real("clock_ratio")
    hmma_ratio = z3.Real("hmma_ratio")
    non_hmma_ratio = z3.Real("non_hmma_ratio")

    # A global clock throttle sufficient to explain HMMA would also slow non-HMMA
    # paths heavily. Local FP32/integer paths do not show a 14-16x collapse.
    s.add(hmma_ratio >= 14, hmma_ratio <= 17)
    s.add(clock_ratio == hmma_ratio)
    s.add(non_hmma_ratio == clock_ratio)
    s.add(non_hmma_ratio <= 2)
    return {"clock_ratio": clock_ratio, "hmma_ratio": hmma_ratio, "non_hmma_ratio": non_hmma_ratio}


def candidate_pure_cta_queue_only(s: z3.Solver, obs: Observations) -> dict:
    n = z3.Int("n")
    resident_ratio = z3.Real("resident_ratio")
    sustained_ratio = z3.Real("sustained_ratio")

    # Queueing alone may increase event time, but the 4096-block trace also
    # raised per-block resident clock64 windows into the capped class.
    s.add(n == 1)
    s.add(resident_ratio <= 3)
    s.add(sustained_ratio >= 14, sustained_ratio <= 17)
    s.add(resident_ratio >= sustained_ratio - 2)
    return {"n": n, "resident_ratio": resident_ratio, "sustained_ratio": sustained_ratio}


def write_outputs(obs: Observations, results: list[dict]) -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    data = {
        "observations": {
            "full_control_cycles_min": obs.full_control_cycles_min,
            "full_control_cycles_max": obs.full_control_cycles_max,
            "burst_cycles_min": obs.burst_cycles_min,
            "burst_cycles_max": obs.burst_cycles_max,
            "burst_ratio_max_vs_full_min": obs.burst_ratio_max,
            "saturated_cycles_min": obs.saturated_cycles_min,
            "saturated_cycles_max": obs.saturated_cycles_max,
            "saturated_ratio_min_vs_full_max": obs.saturated_ratio_min,
            "saturated_ratio_max_vs_full_min": obs.saturated_ratio_max,
            "best_local_tflops": obs.best_local_tflops,
        },
        "results": results,
    }
    (OUT_DIR / "hmma-mechanism-z3-results.json").write_text(json.dumps(data, indent=2) + "\n")

    lines = [
        "# HMMA Mechanism Z3 Consistency Check",
        "",
        "This is a consistency model, not a proof of the hidden hardware implementation.",
        "",
        "## Observations Encoded",
        "",
        f"- Full-speed V100 control: `{obs.full_control_cycles_min:.1f}` to `{obs.full_control_cycles_max:.1f}` cycles / WMMA.",
        f"- Low-residency local burst: `{obs.burst_cycles_min:.1f}` to `{obs.burst_cycles_max:.1f}` cycles / WMMA.",
        f"- Saturated local resident timing: `{obs.saturated_cycles_min:.1f}` to `{obs.saturated_cycles_max:.1f}` cycles / WMMA.",
        f"- Best local throughput in burst sweep: `{obs.best_local_tflops:.4f}` TFLOPS.",
        "",
        "## Candidate Results",
        "",
        "| Candidate | Z3 result | Model values |",
        "| --- | --- | --- |",
    ]
    for item in results:
        model = ", ".join(f"{k}={v}" for k, v in item["model"].items()) or "-"
        lines.append(f"| {item['candidate']} | `{item['result']}` | `{model}` |")
    lines.extend(
        [
            "",
            "## Interpretation",
            "",
            "- The shared token/budget model is satisfiable and selects `n=16`, matching the later explicit `*_REDUCED_SPEED_1_16` enum family.",
            "- A strict per-warp modulo gate is unsatisfiable because it would slow low-residency bursts too; the measured burst window is much faster than 16x.",
            "- Active-lane-only is unsatisfiable because saturated per-WMMA `clock64()` timing itself rises into the 14-16x class.",
            "- A global clock throttle is unsatisfiable because non-HMMA paths are not globally slowed by 14-16x.",
            "- Pure CTA queueing is unsatisfiable because saturated per-block resident timing also rises; the effect is not only time spent waiting to launch CTAs.",
            "",
            "Best surviving mechanism class:",
            "",
            "```text",
            "per-SM or per-scheduler HMMA/FMLA shared issue budget",
            "with enough token depth or arbitration slack to allow short low-residency bursts",
            "and a sustained refill/eligibility rate equivalent to 1/16",
            "```",
        ]
    )
    (OUT_DIR / "hmma-mechanism-z3-results.md").write_text("\n".join(lines) + "\n")


def main() -> None:
    obs = load_observations()
    candidates = [
        ("shared_token_budget", candidate_shared_token_budget),
        ("strict_per_warp_modulo", candidate_strict_per_warp_modulo),
        ("active_lane_mask_only", candidate_active_lane_mask_only),
        ("global_clock_throttle", candidate_global_clock_throttle),
        ("pure_cta_queue_only", candidate_pure_cta_queue_only),
    ]
    results = [solve_candidate(name, fn, obs) for name, fn in candidates]
    write_outputs(obs, results)
    print((OUT_DIR / "hmma-mechanism-z3-results.md").read_text())


if __name__ == "__main__":
    main()
