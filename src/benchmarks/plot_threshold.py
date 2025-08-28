#!/usr/bin/env python3
"""Plot ZSET threshold performance from saved benchmark_output.txt."""

import os
import re
import subprocess
import sys
from pathlib import Path

import matplotlib
import matplotlib.pyplot as plt
matplotlib.use("Agg")

SCRIPT_DIR = Path(__file__).parent
OUTPUT_FILE = SCRIPT_DIR / "benchmark_output.txt"


def load_results() -> str:
    if not OUTPUT_FILE.exists():
        print(f"Error: {OUTPUT_FILE} not found. Run run_benchmarks.py first.", file=sys.stderr)
        sys.exit(1)
    return OUTPUT_FILE.read_text()


def parse_results(data: str) -> dict:
    results = {}
    for line in data.splitlines():
        # Match: BM_Zset_Listpack_Cold_LookupByScore/128   1232 ns
        match = re.match(r"BM_Zset_(\w+)_Cold_(\w+)/(\d+)(?:/iterations:\d+)?\s+([\d.]+)\s+ns", line)
        if match:
            ds, op, count, time_ns = match.groups()
            key = (ds, op)
            results.setdefault(key, []).append((int(count), float(time_ns)))
    return results


def plot_operation(results: dict, operation: str, filename: str):
    plt.figure(figsize=(10, 6))

    styles = {
        "Listpack": {"color": "tab:green", "marker": "^"},
        "Skiplist": {"color": "tab:blue", "marker": "o"},
        "Fbtree": {"color": "tab:orange", "marker": "s"},
    }

    for (ds, op), points in sorted(results.items()):
        if op != operation:
            continue
        points.sort()
        counts, times = zip(*points)
        plt.plot(
            counts,
            times,
            label=ds,
            color=styles[ds]["color"],
            marker=styles[ds]["marker"],
            markersize=6,
        )

    plt.axvline(x=128, color="gray", linestyle="--", linewidth=1.5, label="Current threshold (128)")
    plt.xlabel("Item Count")
    plt.ylabel("Time per operation (ns)")
    plt.title(f"ZSET {operation} (Cold Memory)")
    plt.legend()
    plt.grid(True, alpha=0.3)
    plt.xscale("log", base=2)
    plt.yscale("log")
    plt.tight_layout()

    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150)
    print(f"Generated: {out}")


def main():
    output = load_results()
    results = parse_results(output)

    if not results:
        print("No benchmark results found.", file=sys.stderr)
        sys.exit(1)

    plot_operation(results, "SeekToScore", "zset_cold_lookup.png")
    plot_operation(results, "RankLookup", "zset_cold_rank.png")
    plot_operation(results, "Insert", "zset_cold_insert.png")
    plot_operation(results, "Iterate", "zset_cold_iterate.png")


if __name__ == "__main__":
    main()
