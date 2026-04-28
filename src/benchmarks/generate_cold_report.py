#!/usr/bin/env python3
"""Generate cold-cache benchmark report from cold_benchmark_output.txt."""

import re
import sys
from dataclasses import dataclass
from pathlib import Path

import matplotlib
import matplotlib.pyplot as plt
import numpy as np

matplotlib.use("Agg")

SCRIPT_DIR = Path(__file__).parent
OUTPUT_FILE = SCRIPT_DIR / "cold_benchmark_output.txt"


@dataclass
class BenchmarkResult:
    structure: str      # "Fbtree" or "Skiplist"
    operation: str      # "RankLookup", "SeekToScore", "GetRankOfItem"
    item_count: int
    time_ns: float
    items_per_sec: float


def load_results() -> str:
    if not OUTPUT_FILE.exists():
        print(f"Error: {OUTPUT_FILE} not found. Run run_cold_benchmarks.py first.", file=sys.stderr)
        sys.exit(1)
    return OUTPUT_FILE.read_text()


def parse_benchmark_output(text: str) -> list[BenchmarkResult]:
    results = []

    # Pattern: Fbtree_Cold/RankLookup/128/24  207 ns ... items_per_second=4.82706M/s
    pattern = re.compile(
        r"^(Fbtree|Skiplist)_Cold/(\w+)/(\d+)/\d+\s+"
        r"([\d.]+)\s+ns\s+.*?items_per_second=([\d.]+)([kMG])?/s",
        re.MULTILINE
    )

    for m in pattern.finditer(text):
        struct, op, count, time_ns, ips, ips_unit = m.groups()
        ips = float(ips)
        if ips_unit == "k":
            ips *= 1e3
        elif ips_unit == "M":
            ips *= 1e6
        elif ips_unit == "G":
            ips *= 1e9
        results.append(BenchmarkResult(
            structure=struct, operation=op,
            item_count=int(count), time_ns=float(time_ns),
            items_per_sec=ips,
        ))

    # Pattern for insert/delete/pop: Fbtree_Cold_Insert/Random/128/24 ...
    mut_pattern = re.compile(
        r"^(Fbtree|Skiplist)_Cold_(Insert|Delete|PopHead|PopTail)/(\w+)/(\d+)/\d+\s+"
        r"([\d.]+)\s+ns\s+.*?items_per_second=([\d.]+)([kMG])?/s",
        re.MULTILINE
    )

    for m in mut_pattern.finditer(text):
        struct, category, variation, count, time_ns, ips, ips_unit = m.groups()
        ips = float(ips)
        if ips_unit == "k":
            ips *= 1e3
        elif ips_unit == "M":
            ips *= 1e6
        elif ips_unit == "G":
            ips *= 1e9
        # Map to operation names expected by plot functions:
        # Insert/Random -> InsertRandom, Delete/Random -> DeleteRandom,
        # PopHead/Pop -> PopHead, PopTail/Pop -> PopTail
        if category in ("PopHead", "PopTail"):
            op = category
        else:
            op = category + variation  # e.g. "InsertRandom", "DeleteRandom"
        results.append(BenchmarkResult(
            structure=struct, operation=op,
            item_count=int(count), time_ns=float(time_ns),
            items_per_sec=ips,
        ))

    # Pattern for range delete cold benchmarks:
    # Fbtree_Cold_RangeDeleteRank/Op/128/24, Skiplist_Cold_RangeDeleteScore/Op/128/24
    range_del_pattern = re.compile(
        r"^(Fbtree|Skiplist)_Cold_(RangeDeleteRank|RangeDeleteScore)/\w+/(\d+)/\d+\s+"
        r"([\d.]+)\s+ns\s+.*?items_per_second=([\d.]+)([kMG])?/s",
        re.MULTILINE
    )

    for m in range_del_pattern.finditer(text):
        struct, op, count, time_ns, ips, ips_unit = m.groups()
        ips = float(ips)
        if ips_unit == "k":
            ips *= 1e3
        elif ips_unit == "M":
            ips *= 1e6
        elif ips_unit == "G":
            ips *= 1e9
        results.append(BenchmarkResult(
            structure=struct, operation=op,
            item_count=int(count), time_ns=float(time_ns),
            items_per_sec=ips,
        ))

    # Pattern for mixed cold benchmarks: Fbtree_Cold_Mixed/Op/128/24
    mixed_cold_pattern = re.compile(
        r"^(Fbtree|Skiplist)_Cold_Mixed/\w+/(\d+)/\d+\s+"
        r"([\d.]+)\s+ns\s+.*?items_per_second=([\d.]+)([kMG])?/s",
        re.MULTILINE
    )

    for m in mixed_cold_pattern.finditer(text):
        struct, count, time_ns, ips, ips_unit = m.groups()
        ips = float(ips)
        if ips_unit == "k":
            ips *= 1e3
        elif ips_unit == "M":
            ips *= 1e6
        elif ips_unit == "G":
            ips *= 1e9
        results.append(BenchmarkResult(
            structure=struct, operation="Mixed",
            item_count=int(count), time_ns=float(time_ns),
            items_per_sec=ips,
        ))

    # Pattern for score update cold benchmarks: Fbtree_Cold_ScoreUpdate/Op/128/24
    score_update_cold_pattern = re.compile(
        r"^(Fbtree|Skiplist)_Cold_ScoreUpdate/\w+/(\d+)/\d+\s+"
        r"([\d.]+)\s+ns\s+.*?items_per_second=([\d.]+)([kMG])?/s",
        re.MULTILINE
    )

    for m in score_update_cold_pattern.finditer(text):
        struct, count, time_ns, ips, ips_unit = m.groups()
        ips = float(ips)
        if ips_unit == "k":
            ips *= 1e3
        elif ips_unit == "M":
            ips *= 1e6
        elif ips_unit == "G":
            ips *= 1e9
        results.append(BenchmarkResult(
            structure=struct, operation="ScoreUpdate",
            item_count=int(count), time_ns=float(time_ns),
            items_per_sec=ips,
        ))

    return results


from plot_config import STYLES


def format_rate(ips: float) -> str:
    if ips >= 1e6:
        return f"{ips/1e6:.2f}M"
    if ips >= 1e3:
        return f"{ips/1e3:.1f}k"
    return f"{ips:.0f}"


def plot_cold_lookup(results: list[BenchmarkResult], filename: str):
    """Line chart comparing fbtree vs skiplist cold-cache performance."""
    operations = ["RankLookup", "SeekToScore", "GetRankOfItem"]
    sizes = [128, 1024, 8192, 65536]
    size_labels = ["128", "1K", "8K", "64K"]

    fig, axes = plt.subplots(1, 3, figsize=(15, 5))

    for ax, op in zip(axes, operations):
        for struct in ["Fbtree", "Skiplist"]:
            points = [(r.item_count, r.items_per_sec / 1e6) for r in results
                      if r.structure == struct and r.operation == op]
            if points:
                points.sort()
                counts, rates = zip(*points)
                ax.plot(counts, rates, label=struct, **STYLES[struct], markersize=8, linewidth=2)

        ax.set_xlabel("Set Size (items)")
        ax.set_ylabel("Throughput (M ops/sec)")
        ax.set_title(op)
        ax.set_xscale("log", base=2)
        ax.set_xticks(sizes)
        ax.set_xticklabels(size_labels)
        ax.set_ylim(bottom=0)
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)

    fig.suptitle("Lookup Performance - Cold Cache", fontsize=14, fontweight="bold")
    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def plot_cold_latency(results: list[BenchmarkResult], filename: str):
    """Bar chart showing latency (ns) at each size."""
    operations = ["RankLookup", "SeekToScore", "GetRankOfItem"]
    sizes = [128, 1024, 8192, 65536]
    size_labels = ["128", "1K", "8K", "64K"]

    fig, axes = plt.subplots(1, len(operations), figsize=(5 * len(operations), 5))
    x = np.arange(len(sizes))
    width = 0.35

    for ax, op in zip(axes, operations):
        fbt_times = [next((r.time_ns for r in results
                          if r.structure == "Fbtree" and r.operation == op
                          and r.item_count == s), 0) for s in sizes]
        sl_times = [next((r.time_ns for r in results
                         if r.structure == "Skiplist" and r.operation == op
                         and r.item_count == s), 0) for s in sizes]

        ax.bar(x - width/2, fbt_times, width, label="Fbtree", color=STYLES["Fbtree"]["color"])
        ax.bar(x + width/2, sl_times, width, label="Skiplist", color=STYLES["Skiplist"]["color"])

        ax.set_xlabel("Set Size")
        ax.set_ylabel("Latency (ns)")
        ax.set_title(op)
        ax.set_xticks(x)
        ax.set_xticklabels(size_labels)
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3, axis="y")

    fig.suptitle("Latency - Cold Cache", fontsize=14, fontweight="bold")
    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def plot_cold_insert(results: list[BenchmarkResult], filename: str):
    """Plot insert throughput vs size - Random and Append."""
    sizes = [128, 1024, 8192, 65536]
    size_labels = ["128", "1K", "8K", "64K"]

    fig, axes = plt.subplots(1, 2, figsize=(14, 6))

    for ax, (op, title) in zip(axes, [("InsertRandom", "Random Insert"), ("InsertAppend", "Sequential Append")]):
        for struct in ["Fbtree", "Skiplist"]:
            points = [(r.item_count, r.items_per_sec / 1e6) for r in results
                      if r.structure == struct and r.operation == op]
            if points:
                points.sort()
                counts, rates = zip(*points)
                ax.plot(counts, rates, label=struct, **STYLES[struct], markersize=8, linewidth=2)

        ax.set_xlabel("Set Size (items)")
        ax.set_ylabel("Throughput (M ops/sec)")
        ax.set_title(title)
        ax.set_xscale("log", base=2)
        ax.set_xticks(sizes)
        ax.set_xticklabels(size_labels)
        ax.set_ylim(bottom=0)
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)

    fig.suptitle("Insert Performance - Cold Cache", fontsize=14, fontweight="bold")
    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def plot_cold_delete(results: list[BenchmarkResult], filename: str):
    """Plot delete throughput vs size - Random, PopHead, PopTail."""
    sizes = [128, 1024, 8192, 65536]
    size_labels = ["128", "1K", "8K", "64K"]

    fig, axes = plt.subplots(1, 3, figsize=(15, 5))

    for ax, (op, title) in zip(axes, [
        ("DeleteRandom", "Random Delete"),
        ("PopHead", "Pop Head"),
        ("PopTail", "Pop Tail")
    ]):
        for struct in ["Fbtree", "Skiplist"]:
            points = [(r.item_count, r.items_per_sec / 1e6) for r in results
                      if r.structure == struct and r.operation == op]
            if points:
                points.sort()
                counts, rates = zip(*points)
                ax.plot(counts, rates, label=struct, **STYLES[struct], markersize=8, linewidth=2)

        ax.set_xlabel("Set Size (items)")
        ax.set_ylabel("Throughput (M ops/sec)")
        ax.set_title(title)
        ax.set_xscale("log", base=2)
        ax.set_xticks(sizes)
        ax.set_xticklabels(size_labels)
        ax.set_ylim(bottom=0)
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)

    fig.suptitle("Delete Performance - Cold Cache", fontsize=14, fontweight="bold")
    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def plot_cold_iterate(results: list[BenchmarkResult], filename: str):
    """Plot iteration throughput vs size."""
    sizes = [128, 1024, 8192, 65536]
    size_labels = ["128", "1K", "8K", "64K"]

    fig, ax = plt.subplots(figsize=(10, 6))

    for struct in ["Fbtree", "Skiplist"]:
        points = [(r.item_count, r.items_per_sec / 1e6) for r in results
                  if r.structure == struct and r.operation == "IterateForward"]
        if points:
            points.sort()
            counts, rates = zip(*points)
            ax.plot(counts, rates, label=struct, **STYLES[struct], markersize=8, linewidth=2)

    ax.set_xlabel("Set Size (items)", fontsize=12)
    ax.set_ylabel("Throughput (M items/sec)", fontsize=12)
    ax.set_title("Iteration Performance - Cold Cache", fontsize=14, fontweight="bold")
    ax.set_xscale("log", base=2)
    ax.set_xticks(sizes)
    ax.set_xticklabels(size_labels)
    ax.set_ylim(bottom=0)
    ax.legend(fontsize=11)
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def plot_speedup(results: list[BenchmarkResult], filename: str):
    """Bar chart showing fbtree speedup over skiplist."""
    operations = ["RankLookup", "SeekToScore", "GetRankOfItem", "IterateForward",
                  "InsertRandom", "InsertAppend", "DeleteRandom", "PopHead", "PopTail",
                  "RangeDeleteRank", "RangeDeleteScore", "PartialScan",
                  "Mixed", "ScoreUpdate"]
    sizes = [128, 1024, 8192, 65536]
    size_labels = ["128", "1K", "8K", "64K"]
    max_display = 5.0  # Truncate bars above this value

    fig, ax = plt.subplots(figsize=(10, 6))
    x = np.arange(len(sizes))
    n_ops = len(operations)
    width = 0.8 / n_ops

    for i, op in enumerate(operations):
        speedups = []
        for s in sizes:
            fbt = next((r.items_per_sec for r in results
                       if r.structure == "Fbtree" and r.operation == op
                       and r.item_count == s), 0)
            sl = next((r.items_per_sec for r in results
                      if r.structure == "Skiplist" and r.operation == op
                      and r.item_count == s), 0)
            speedups.append(fbt / sl if sl > 0 else 0)

        offset = (i - n_ops / 2 + 0.5) * width
        display_vals = [min(v, max_display) for v in speedups]
        bars = ax.bar(x + offset, display_vals, width, label=op)

        # Annotate truncated bars with actual value
        for j, (bar, actual) in enumerate(zip(bars, speedups)):
            if actual > max_display:
                ax.annotate(f"{actual:.0f}x", xy=(bar.get_x() + bar.get_width()/2, max_display),
                           ha="center", va="bottom", fontsize=7, fontweight="bold")

    ax.axhline(y=1, color="gray", linestyle="--", linewidth=1, alpha=0.7)
    ax.set_xlabel("Set Size")
    ax.set_ylabel("Fbtree Speedup (x)")
    ax.set_title("Fbtree vs Skiplist Speedup - Cold Cache", fontsize=14, fontweight="bold")
    ax.set_xticks(x)
    ax.set_xticklabels(size_labels)
    ax.set_ylim(0, max_display * 1.15)  # Room for annotations
    ax.legend(fontsize=9, loc="center left", bbox_to_anchor=(1.02, 0.5))
    ax.grid(True, alpha=0.3, axis="y")

    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def print_summary_table(results: list[BenchmarkResult]):
    """Print text tables comparing fbtree vs skiplist."""
    operations = ["RankLookup", "SeekToScore", "GetRankOfItem", "IterateForward",
                  "InsertRandom", "InsertAppend", "DeleteRandom", "PopHead", "PopTail",
                  "RangeDeleteRank", "RangeDeleteScore", "PartialScan",
                  "Mixed", "ScoreUpdate"]
    sizes = [128, 1024, 8192, 65536]
    size_labels = ["128", "1K", "8K", "64K"]

    # Filter to operations that have results
    ops_found = [op for op in operations if any(r.operation == op for r in results)]

    if not ops_found:
        print("No results found.")
        return

    # Header
    header = f"{'Operation':<18} {'Struct':<10}" + "".join(f"{l:>12}" for l in size_labels)
    print("\n" + "=" * len(header))
    print("Cold-Cache Benchmark Results (ops/sec)")
    print("=" * len(header))
    print(header)
    print("-" * len(header))

    for op in ops_found:
        for struct in ["Fbtree", "Skiplist"]:
            row = f"{op:<18} {struct:<10}"
            for size in sizes:
                r = next((x for x in results if x.structure == struct
                         and x.operation == op and x.item_count == size), None)
                row += f"{format_rate(r.items_per_sec) + '/s':>12}" if r else f"{'--':>12}"
            print(row)
        print()

    # Speedup summary
    print("=" * len(header))
    print("Fbtree Speedup vs Skiplist (>1 = Fbtree faster)")
    print("=" * len(header))
    header2 = f"{'Operation':<18}" + "".join(f"{l:>12}" for l in size_labels)
    print(header2)
    print("-" * len(header2))

    for op in ops_found:
        row = f"{op:<18}"
        for size in sizes:
            fbt = next((x.items_per_sec for x in results if x.structure == "Fbtree"
                       and x.operation == op and x.item_count == size), 0)
            sl = next((x.items_per_sec for x in results if x.structure == "Skiplist"
                      and x.operation == op and x.item_count == size), 0)
            if fbt > 0 and sl > 0:
                speedup = fbt / sl
                row += f"{speedup:>11.2f}x"
            else:
                row += f"{'--':>12}"
        print(row)
    print()


def plot_cold_range_delete(results: list[BenchmarkResult], filename: str):
    """Plot cold-cache range delete throughput vs size."""
    sizes = [128, 1024, 8192, 65536]
    size_labels = ["128", "1K", "8K", "64K"]

    fig, axes = plt.subplots(1, 2, figsize=(14, 6))

    for ax, (op, title) in zip(axes, [
        ("RangeDeleteRank", "Range Delete by Rank"),
        ("RangeDeleteScore", "Range Delete by Score")
    ]):
        for struct in ["Fbtree", "Skiplist"]:
            points = [(r.item_count, r.items_per_sec / 1e6) for r in results
                      if r.structure == struct and r.operation == op]
            if points:
                points.sort()
                counts, rates = zip(*points)
                ax.plot(counts, rates, label=struct, **STYLES[struct], markersize=8, linewidth=2)

        ax.set_xlabel("Set Size (items)")
        ax.set_ylabel("Throughput (M ops/sec)")
        ax.set_title(title)
        ax.set_xscale("log", base=2)
        ax.set_xticks(sizes)
        ax.set_xticklabels(size_labels)
        ax.set_ylim(bottom=0)
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)

    fig.suptitle("Range Delete Performance - Cold Cache", fontsize=14, fontweight="bold")
    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def plot_cold_partial_scan(results: list[BenchmarkResult], filename: str):
    """Plot cold-cache partial range scan throughput vs size."""
    sizes = [128, 1024, 8192, 65536]
    size_labels = ["128", "1K", "8K", "64K"]

    fig, ax = plt.subplots(figsize=(10, 6))

    for struct in ["Fbtree", "Skiplist"]:
        points = [(r.item_count, r.items_per_sec / 1e6) for r in results
                  if r.structure == struct and r.operation == "PartialScan"]
        if points:
            points.sort()
            counts, rates = zip(*points)
            ax.plot(counts, rates, label=struct, **STYLES[struct], markersize=8, linewidth=2)

    ax.set_xlabel("Set Size (items)", fontsize=12)
    ax.set_ylabel("Throughput (M items/sec)", fontsize=12)
    ax.set_title("Partial Range Scan Performance - Cold Cache", fontsize=14, fontweight="bold")
    ax.set_xscale("log", base=2)
    ax.set_xticks(sizes)
    ax.set_xticklabels(size_labels)
    ax.set_ylim(bottom=0)
    ax.legend(fontsize=11)
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def plot_cold_mixed_workload(results: list[BenchmarkResult], filename: str):
    """Plot cold-cache mixed workload throughput vs size."""
    sizes = [128, 1024, 8192, 65536]
    size_labels = ["128", "1K", "8K", "64K"]

    fig, ax = plt.subplots(figsize=(10, 6))

    for struct in ["Fbtree", "Skiplist"]:
        points = [(r.item_count, r.items_per_sec / 1e6) for r in results
                  if r.structure == struct and r.operation == "Mixed"]
        if points:
            points.sort()
            counts, rates = zip(*points)
            ax.plot(counts, rates, label=struct, **STYLES[struct], markersize=8, linewidth=2)

    ax.set_xlabel("Set Size (items)", fontsize=12)
    ax.set_ylabel("Throughput (M ops/sec)", fontsize=12)
    ax.set_title("Mixed Workload Performance - Cold Cache", fontsize=14, fontweight="bold")
    ax.set_xscale("log", base=2)
    ax.set_xticks(sizes)
    ax.set_xticklabels(size_labels)
    ax.set_ylim(bottom=0)
    ax.legend(fontsize=11)
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def plot_cold_score_update(results: list[BenchmarkResult], filename: str):
    """Plot cold-cache score update throughput vs size."""
    sizes = [128, 1024, 8192, 65536]
    size_labels = ["128", "1K", "8K", "64K"]

    fig, ax = plt.subplots(figsize=(10, 6))

    for struct in ["Fbtree", "Skiplist"]:
        points = [(r.item_count, r.items_per_sec / 1e6) for r in results
                  if r.structure == struct and r.operation == "ScoreUpdate"]
        if points:
            points.sort()
            counts, rates = zip(*points)
            ax.plot(counts, rates, label=struct, **STYLES[struct], markersize=8, linewidth=2)

    ax.set_xlabel("Set Size (items)", fontsize=12)
    ax.set_ylabel("Throughput (M ops/sec)", fontsize=12)
    ax.set_title("Score Update Performance - Cold Cache", fontsize=14, fontweight="bold")
    ax.set_xscale("log", base=2)
    ax.set_xticks(sizes)
    ax.set_xticklabels(size_labels)
    ax.set_ylim(bottom=0)
    ax.legend(fontsize=11)
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def main():
    output = load_results()
    results = parse_benchmark_output(output)

    if not results:
        print("No benchmark results found.", file=sys.stderr)
        sys.exit(1)

    print(f"Parsed {len(results)} benchmark results", file=sys.stderr)

    print("# Tier 2: Cold-Cache Benchmark Report")
    print("\n## Methodology")
    print("""
Simulates realistic Valkey workload with many small sorted sets.
Each operation accesses a different structure, ensuring cold-cache conditions.

- **Structure count**: Dynamically sized to exceed 2x LLC
- **Access pattern**: Shuffled to defeat prefetcher
- **Metric**: Operations per second (higher = better)
""")

    print("\n## Results")
    print_summary_table(results)

    print("\n## Visualizations")
    plot_cold_lookup(results, "cold_perf_lookup.png")
    plot_cold_insert(results, "cold_perf_insert.png")
    plot_cold_delete(results, "cold_perf_delete.png")
    plot_cold_iterate(results, "cold_perf_iterate.png")
    plot_cold_latency(results, "cold_perf_latency.png")
    plot_cold_range_delete(results, "cold_perf_range_delete.png")
    plot_cold_partial_scan(results, "cold_perf_partial_scan.png")
    plot_cold_mixed_workload(results, "cold_perf_mixed_workload.png")
    plot_cold_score_update(results, "cold_perf_score_update.png")
    plot_speedup(results, "cold_perf_speedup.png")


if __name__ == "__main__":
    main()
