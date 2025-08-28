#!/usr/bin/env python3
"""Plot memory overhead benchmark results from saved benchmark_output.txt."""

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


def parse_results(data: str, pattern: str) -> dict:
    results = {}
    for line in data.splitlines():
        match = re.match(pattern, line)
        if match:
            name, size, bpi = match.groups()
            results.setdefault(name, []).append((int(size), float(bpi)))
    return results


def plot(results: dict):
    plt.figure(figsize=(10, 6))

    styles = {
        "Skiplist": {"color": "tab:blue", "marker": "o"},
        "Fbtree": {"color": "tab:orange", "marker": "s"},
    }
    linestyles = {"Append": "-", "Random": "--"}

    for name, points in sorted(results.items()):
        points.sort()
        sizes, bpi = zip(*points)

        ds = "Skiplist" if "Skiplist" in name else "Fbtree"
        order = "Append" if "Append" in name else "Random"

        plt.plot(
            sizes,
            bpi,
            label=f"{ds} ({order})",
            color=styles[ds]["color"],
            marker=styles[ds]["marker"],
            linestyle=linestyles[order],
            markersize=5,
        )

    plt.xlabel("Item Size (bytes)")
    plt.ylabel("Overhead (bytes per item)")
    plt.title("Memory Overhead: Skiplist vs FBTree")
    plt.legend()
    plt.grid(True, alpha=0.3)
    plt.ylim(bottom=0)
    plt.tight_layout()

    out = SCRIPT_DIR / "memory_overhead.png"
    plt.savefig(out, dpi=150)
    print(f"Generated: {out}")


def plot_zset(results: dict):
    plt.figure(figsize=(10, 6))

    styles = {
        "Listpack": {"color": "tab:green", "marker": "^"},
        "Skiplist": {"color": "tab:blue", "marker": "o"},
        "Fbtree": {"color": "tab:orange", "marker": "s"},
    }

    for name, points in sorted(results.items()):
        points.sort()
        counts, bpi = zip(*points)

        if "Listpack" in name:
            ds = "Listpack"
        elif "Skiplist" in name:
            ds = "Skiplist"
        else:
            ds = "Fbtree"

        plt.plot(
            counts,
            bpi,
            label=ds,
            color=styles[ds]["color"],
            marker=styles[ds]["marker"],
            markersize=6,
        )

    plt.axvline(x=128, color="gray", linestyle="--", linewidth=1.5, label="Current threshold (128)")
    plt.xlabel("Item Count")
    plt.ylabel("Total Memory (bytes per item)")
    plt.title("Full ZSET Memory: Listpack vs Skiplist+HT vs FBTree+HT")
    plt.legend()
    plt.grid(True, alpha=0.3)
    plt.xscale("log", base=2)
    plt.ylim(bottom=0)
    plt.tight_layout()

    out = SCRIPT_DIR / "zset_memory.png"
    plt.savefig(out, dpi=150)
    print(f"Generated: {out}")


def fmt_count(x, _):
    if x >= 1_000_000:
        return f"{int(x/1_000_000)}M"
    if x >= 1_000:
        return f"{int(x/1_000)}K"
    return str(int(x))


def plot_standard(results: dict):
    plt.figure(figsize=(10, 6))

    styles = {
        "Skiplist": {"color": "#3498db", "marker": "o", "linestyle": "-"},
        "Fbtree (random)": {"color": "#2ecc71", "marker": "s", "linestyle": "-"},
        "Fbtree (append)": {"color": "#2ecc71", "marker": "^", "linestyle": "--"},
    }

    for name, points in sorted(results.items()):
        points.sort()
        counts, bpi = zip(*points)

        if "Skiplist" in name:
            ds = "Skiplist"
        elif "Append" in name:
            ds = "Fbtree (append)"
        else:
            ds = "Fbtree (random)"

        plt.plot(
            counts,
            bpi,
            label=ds,
            color=styles[ds]["color"],
            marker=styles[ds]["marker"],
            linestyle=styles[ds]["linestyle"],
            markersize=6,
        )

    plt.axvline(x=128, color="gray", linestyle="--", linewidth=1.5, label="Listpack threshold (128)")
    plt.xlabel("Item Count")
    plt.ylabel("Bytes per Item")
    plt.title("Memory Efficiency: Skiplist vs FBTree (24-byte items)")
    plt.legend()
    plt.grid(True, alpha=0.3)
    plt.xscale("log", base=2)
    ticks = [8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576, 2097152, 4194304]
    plt.xticks(ticks)
    plt.gca().xaxis.set_major_formatter(plt.FuncFormatter(fmt_count))
    plt.ylim(bottom=0)
    plt.tight_layout()

    out = SCRIPT_DIR / "memory_overhead.png"
    plt.savefig(out, dpi=150)
    print(f"Generated: {out}")


def main():
    output = load_results()

    # Parse Standard memory results (skip stddev/cv lines with BytesPerItem=0)
    standard_results = {}
    pattern = r"(BM_(?:Fbtree|Skiplist)_Mem_Standard(?:_Append)?)/(\d+)/.*BytesPerItem=([\d.]+)"
    for line in output.splitlines():
        if "_stddev" in line or "_cv" in line:
            continue
        match = re.match(pattern, line)
        if match:
            name, size, bpi = match.groups()
            if float(bpi) > 0:
                standard_results.setdefault(name, []).append((int(size), float(bpi)))
    if standard_results:
        plot_standard(standard_results)

    # Parse BySize results if available
    overhead_results = parse_results(output, r"(BM_(?:Fbtree|Skiplist)_Mem_BySize_\w+)/(\d+)/.*BytesPerItem=([\d.]+)")
    if overhead_results:
        plot(overhead_results)

    # Parse ZSET results (ByCount benchmarks)
    zset_results = parse_results(output, r"(BM_Zset_\w+_Mem_ByCount)/(\d+)/.*BytesPerItem=([\d.]+)")
    if zset_results:
        plot_zset(zset_results)

    if not standard_results and not overhead_results and not zset_results:
        print("No memory benchmark results found.", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
