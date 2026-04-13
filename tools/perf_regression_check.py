#!/usr/bin/env python3
"""perf_regression_check.py — KPI regression checker for RBS scheduler/OMS metrics.

Usage examples:

  # Compare two JSON dumps:
  python3 tools/perf_regression_check.py \\
    --baseline artifacts/baseline.json \\
    --current  artifacts/current.json

  # Fetch current counters live from OMS Prometheus endpoint:
  python3 tools/perf_regression_check.py \\
    --baseline artifacts/baseline.json \\
    --pm-url   http://127.0.0.1:9090/metrics

  # Save current run as new baseline (after a clean pass):
  python3 tools/perf_regression_check.py \\
    --baseline artifacts/baseline.json \\
    --current  artifacts/current.json \\
    --save-baseline artifacts/baseline.json

  # Override regression threshold (default 10%):
  python3 tools/perf_regression_check.py ... --threshold 0.15

JSON format for baseline / current:
  {
    "perf.scheduler.mean_us":        150.3,
    "perf.scheduler.p95_us":         420.1,
    "perf.scheduler.throughput_fps": 18500.0,
    "rbs.lte.s1.rx_errors":          0.0,
    ...
  }

Counter direction convention:
  - Counters with suffix "_us" or "_errors" or "_latency":
      LOWER is better → regression if current > baseline * (1 + threshold)
  - Counters with suffix "_fps" or "_throughput" or "_rate" or "_count":
      HIGHER is better → regression if current < baseline * (1 - threshold)
  - All other counters: treated as LOWER-is-better.

Exit codes:
  0  All KPIs within budget.
  1  One or more KPI regressions detected.
  2  Usage / IO error.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import urllib.request
from pathlib import Path
from typing import Optional


# ---------------------------------------------------------------------------
# Direction heuristic
# ---------------------------------------------------------------------------
_HIGHER_IS_BETTER_RE = re.compile(
    r"(_fps|_throughput|_rate|throughput|_success|success_rate)$", re.I
)
_LOWER_IS_BETTER_RE  = re.compile(
    r"(_us|_ms|_latency|_errors|_error|_fail|_nack|_retx)$", re.I
)


def _direction(name: str) -> str:
    """Return 'higher' or 'lower' (is better) for a counter name."""
    if _HIGHER_IS_BETTER_RE.search(name):
        return "higher"
    return "lower"


# ---------------------------------------------------------------------------
# Prometheus text format → dict
# ---------------------------------------------------------------------------
def parse_prometheus(text: str) -> dict[str, float]:
    """Parse Prometheus text exposition format into {metric_name: value}."""
    result: dict[str, float] = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        # Split on last whitespace before value (ignoring optional timestamp)
        parts = line.rsplit(None, 2)
        if len(parts) < 2:
            continue
        metric_part, value_str = parts[0], parts[1]
        # Strip labels for plain counter name
        metric_name = metric_part.split("{")[0]
        try:
            result[metric_name] = float(value_str)
        except ValueError:
            pass
    return result


def fetch_prometheus(url: str) -> dict[str, float]:
    """Fetch /metrics from a running Prometheus endpoint."""
    try:
        with urllib.request.urlopen(url, timeout=5) as resp:  # noqa: S310 – local URL
            return parse_prometheus(resp.read().decode())
    except Exception as exc:
        print(f"ERROR: cannot fetch {url}: {exc}", file=sys.stderr)
        sys.exit(2)


# ---------------------------------------------------------------------------
# Regression check
# ---------------------------------------------------------------------------
def check_regressions(
    baseline: dict[str, float],
    current:  dict[str, float],
    threshold: float,
) -> list[dict]:
    """Return list of regression dicts (empty = all OK)."""
    regressions = []
    for name, base_val in sorted(baseline.items()):
        cur_val = current.get(name)
        if cur_val is None:
            regressions.append({
                "name":     name,
                "reason":   "counter missing in current run",
                "baseline": base_val,
                "current":  None,
                "delta_pct": None,
            })
            continue

        if base_val == 0.0:
            # Can't compute percentage for zero baseline; skip
            continue

        delta_pct = (cur_val - base_val) / abs(base_val) * 100.0
        direction = _direction(name)

        regressed = False
        if direction == "lower" and cur_val > base_val * (1.0 + threshold):
            regressed = True
        elif direction == "higher" and cur_val < base_val * (1.0 - threshold):
            regressed = True

        if regressed:
            regressions.append({
                "name":      name,
                "direction": direction,
                "reason":    f"{'increased' if direction == 'lower' else 'decreased'} "
                             f"by {abs(delta_pct):.1f}% (threshold={threshold*100:.0f}%)",
                "baseline":  base_val,
                "current":   cur_val,
                "delta_pct": round(delta_pct, 2),
            })

    return regressions


# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
def print_report(
    baseline: dict[str, float],
    current:  dict[str, float],
    regressions: list[dict],
    threshold: float,
) -> None:
    col_names = sorted(set(baseline) | set(current))
    print(f"\n{'KPI':<50}  {'baseline':>12}  {'current':>12}  {'delta%':>8}  status")
    print("─" * 100)
    for name in col_names:
        bval = baseline.get(name)
        cval = current.get(name)
        if bval is None and cval is None:
            continue
        reg = next((r for r in regressions if r["name"] == name), None)
        status = "REGRESS" if reg else "ok"
        bstr = f"{bval:.2f}" if bval is not None else "—"
        cstr = f"{cval:.2f}" if cval is not None else "—"
        if bval and cval:
            dpct = (cval - bval) / abs(bval) * 100.0
            dstr = f"{dpct:+.1f}%"
        else:
            dstr = "—"
        print(f"{name:<50}  {bstr:>12}  {cstr:>12}  {dstr:>8}  {status}")
    print()


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="RBS KPI regression checker (п.32)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("--baseline",      required=True, metavar="FILE",
                   help="JSON file with KPI baseline values")
    p.add_argument("--current",       metavar="FILE",
                   help="JSON file with current KPI values")
    p.add_argument("--pm-url",        metavar="URL",
                   help="Prometheus /metrics URL (alternative to --current)")
    p.add_argument("--threshold",     type=float, default=0.10, metavar="FRAC",
                   help="Regression threshold as fraction (default: 0.10 = 10%%)")
    p.add_argument("--save-baseline", metavar="FILE",
                   help="Overwrite this file with current values after a PASS")
    p.add_argument("--report-json",   metavar="FILE",
                   help="Write regression report as JSON to this file")
    return p


def load_json(path: str) -> dict[str, float]:
    try:
        with open(path) as f:
            data = json.load(f)
        if not isinstance(data, dict):
            raise ValueError("top-level value must be a JSON object")
        return {k: float(v) for k, v in data.items()}
    except Exception as exc:
        print(f"ERROR: cannot read {path}: {exc}", file=sys.stderr)
        sys.exit(2)


def main() -> None:
    args = build_arg_parser().parse_args()

    if not args.current and not args.pm_url:
        print("ERROR: supply --current FILE or --pm-url URL", file=sys.stderr)
        sys.exit(2)

    baseline = load_json(args.baseline)

    if args.pm_url:
        current = fetch_prometheus(args.pm_url)
    else:
        current = load_json(args.current)

    regressions = check_regressions(baseline, current, args.threshold)

    print_report(baseline, current, regressions, args.threshold)

    if args.report_json:
        report = {
            "passed":       len(regressions) == 0,
            "threshold":    args.threshold,
            "regressions":  regressions,
        }
        try:
            Path(args.report_json).write_text(json.dumps(report, indent=2))
            print(f"Report written → {args.report_json}")
        except Exception as exc:
            print(f"WARN: cannot write report: {exc}", file=sys.stderr)

    if regressions:
        print(f"RESULT: FAIL — {len(regressions)} KPI regression(s) detected")
        for r in regressions:
            print(f"  [{r['name']}] {r['reason']}")
        sys.exit(1)

    print("RESULT: PASS — all KPIs within budget")

    if args.save_baseline:
        try:
            Path(args.save_baseline).write_text(json.dumps(current, indent=2))
            print(f"Baseline updated → {args.save_baseline}")
        except Exception as exc:
            print(f"WARN: cannot save baseline: {exc}", file=sys.stderr)


if __name__ == "__main__":
    main()
