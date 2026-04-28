#!/usr/bin/env python3
"""Generate benchmark report from saved benchmark_output.txt."""

import re
import sys
from dataclasses import dataclass
from pathlib import Path

import matplotlib
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

matplotlib.use("Agg")

SCRIPT_DIR = Path(__file__).parent
OUTPUT_FILE = SCRIPT_DIR / "benchmark_output.txt"

# LLC size in items (24B each, ~3x overhead) - approx where data exceeds cache
LLC_BYTES = 46 * 1024 * 1024  # 46MB typical
LLC_ITEMS = LLC_BYTES // (24 * 3)  # ~640K items


@dataclass
class BenchmarkResult:
    structure: str      # "Fbtree" or "Skiplist"
    build: str          # "SeqBuild" or "RandBuild"
    operation: str      # "RankLookup", "Insert_Append", etc.
    item_count: int
    item_size: int
    time_ns: float
    items_per_sec: float = 0.0


def load_results() -> str:
    if not OUTPUT_FILE.exists():
        print(f"Error: {OUTPUT_FILE} not found. Run run_benchmarks.py first.", file=sys.stderr)
        sys.exit(1)
    return OUTPUT_FILE.read_text()


def parse_benchmark_output(text: str) -> list[BenchmarkResult]:
    results = []

    # Pattern for fixture benchmarks with items_per_second (including iterate now)
    fixture_pattern = re.compile(
        r"^(Fbtree|Skiplist)_(SeqBuild|RandBuild)/(\w+)/(\d+)/(\d+)(?:/\S+)?\s+"
        r"([\d.e+]+)\s+ns\s+.*?items_per_second=([\d.]+)([kMG])?/s",
        re.MULTILINE
    )

    for m in fixture_pattern.finditer(text):
        struct, build, op, count, size, time_ns, ips, ips_unit = m.groups()
        ips = float(ips)
        if ips_unit == "k":
            ips *= 1e3
        elif ips_unit == "M":
            ips *= 1e6
        elif ips_unit == "G":
            ips *= 1e9
        results.append(BenchmarkResult(
            structure=struct, build=build, operation=op,
            item_count=int(count), item_size=int(size),
            time_ns=float(time_ns), items_per_sec=ips,
        ))

    # Pattern for insert benchmarks
    insert_pattern = re.compile(
        r"^BM_(Fbtree|Skiplist)_Insert_(\w+)/(\d+)/(\d+)(?:/\S+)?\s+([\d.e+]+)\s+ns",
        re.MULTILINE
    )

    for m in insert_pattern.finditer(text):
        struct, order, count, size, time_ns = m.groups()
        results.append(BenchmarkResult(
            structure=struct, build="Insert", operation=f"Insert_{order}",
            item_count=int(count), item_size=int(size),
            time_ns=float(time_ns),
            items_per_sec=int(count) / (float(time_ns) / 1e9),
        ))

    # Pattern for delete benchmarks
    delete_pattern = re.compile(
        r"^BM_(Fbtree|Skiplist)_Delete_(\w+)/(\d+)/(\d+)(?:/\S+)?\s+([\d.e+]+)\s+ns\s+.*?items_per_second=([\d.]+)([kMG])?/s",
        re.MULTILINE
    )

    for m in delete_pattern.finditer(text):
        struct, order, count, size, time_ns, ips, ips_unit = m.groups()
        ips = float(ips)
        if ips_unit == "k":
            ips *= 1e3
        elif ips_unit == "M":
            ips *= 1e6
        elif ips_unit == "G":
            ips *= 1e9
        results.append(BenchmarkResult(
            structure=struct, build="Delete", operation=f"Delete_{order}",
            item_count=int(count), item_size=int(size),
            time_ns=float(time_ns), items_per_sec=ips,
        ))

    # Pattern for range delete benchmarks: BM_Fbtree_RangeDeleteByRank/1024/24
    range_delete_pattern = re.compile(
        r"^BM_(Fbtree|Skiplist)_RangeDeleteBy(Rank|Score)/(\d+)/(\d+)(?:/\S+)?\s+([\d.e+]+)\s+ns\s+.*?items_per_second=([\d.]+)([kMG])?/s",
        re.MULTILINE
    )

    for m in range_delete_pattern.finditer(text):
        struct, variant, count, size, time_ns, ips, ips_unit = m.groups()
        ips = float(ips)
        if ips_unit == "k":
            ips *= 1e3
        elif ips_unit == "M":
            ips *= 1e6
        elif ips_unit == "G":
            ips *= 1e9
        results.append(BenchmarkResult(
            structure=struct, build="RangeDelete", operation=f"RangeDeleteBy{variant}",
            item_count=int(count), item_size=int(size),
            time_ns=float(time_ns), items_per_sec=ips,
        ))

    # Pattern for partial range scan benchmarks: BM_Fbtree_PartialRangeScan/1024/24
    partial_scan_pattern = re.compile(
        r"^BM_(Fbtree|Skiplist)_PartialRangeScan/(\d+)/(\d+)(?:/\S+)?\s+([\d.e+]+)\s+ns\s+.*?items_per_second=([\d.]+)([kMG])?/s",
        re.MULTILINE
    )

    for m in partial_scan_pattern.finditer(text):
        struct, count, size, time_ns, ips, ips_unit = m.groups()
        ips = float(ips)
        if ips_unit == "k":
            ips *= 1e3
        elif ips_unit == "M":
            ips *= 1e6
        elif ips_unit == "G":
            ips *= 1e9
        results.append(BenchmarkResult(
            structure=struct, build="PartialScan", operation="PartialRangeScan",
            item_count=int(count), item_size=int(size),
            time_ns=float(time_ns), items_per_sec=ips,
        ))

    # Pattern for mixed workload benchmarks: BM_Fbtree_MixedWorkload/1024/24/50
    mixed_pattern = re.compile(
        r"^BM_(Fbtree|Skiplist)_MixedWorkload/(\d+)/(\d+)/(\d+)(?:/\S+)?\s+([\d.e+]+)\s+ns\s+.*?items_per_second=([\d.]+)([kMG])?/s",
        re.MULTILINE
    )

    for m in mixed_pattern.finditer(text):
        struct, count, size, read_pct, time_ns, ips, ips_unit = m.groups()
        ips = float(ips)
        if ips_unit == "k":
            ips *= 1e3
        elif ips_unit == "M":
            ips *= 1e6
        elif ips_unit == "G":
            ips *= 1e9
        results.append(BenchmarkResult(
            structure=struct, build="Mixed", operation=f"MixedWorkload_R{read_pct}",
            item_count=int(count), item_size=int(size),
            time_ns=float(time_ns), items_per_sec=ips,
        ))

    # Pattern for score update benchmarks: BM_Fbtree_ScoreUpdate/1024/24
    score_update_pattern = re.compile(
        r"^BM_(Fbtree|Skiplist)_ScoreUpdate/(\d+)/(\d+)(?:/\S+)?\s+([\d.e+]+)\s+ns\s+.*?items_per_second=([\d.]+)([kMG])?/s",
        re.MULTILINE
    )

    for m in score_update_pattern.finditer(text):
        struct, count, size, time_ns, ips, ips_unit = m.groups()
        ips = float(ips)
        if ips_unit == "k":
            ips *= 1e3
        elif ips_unit == "M":
            ips *= 1e6
        elif ips_unit == "G":
            ips *= 1e9
        results.append(BenchmarkResult(
            structure=struct, build="ScoreUpdate", operation="ScoreUpdate",
            item_count=int(count), item_size=int(size),
            time_ns=float(time_ns), items_per_sec=ips,
        ))

    return results


def format_rate(ips: float) -> str:
    if ips >= 1e9:
        return f"{ips/1e9:.1f}G"
    if ips >= 1e6:
        return f"{ips/1e6:.1f}M"
    if ips >= 1e3:
        return f"{ips/1e3:.1f}k"
    return f"{ips:.0f}"


from plot_config import STYLES

# Minor ticks at every doubling (2x intervals) from 8 to 4M
MINOR_TICKS = [8 * 2**i for i in range(20) if 8 * 2**i <= 4194304]


def setup_log_axis(ax, sizes, size_labels, set_xlim=True):
    """Configure log x-axis with major ticks at data points and minor ticks at doublings."""
    ax.set_xscale("log", base=2)
    ax.set_xticks(sizes)
    ax.set_xticklabels(size_labels)
    ax.set_xticks(MINOR_TICKS, minor=True)
    ax.tick_params(axis='x', which='minor', length=4)
    ax.xaxis.set_minor_formatter(ticker.NullFormatter())
    if set_xlim:
        ax.set_xlim(sizes[0] * 0.7, sizes[-1] * 1.4)


def plot_lookup_by_size(results: list[BenchmarkResult], build: str, filename: str):
    """Line chart comparing fbtree vs skiplist across sizes."""
    operations = ["RankLookup", "SeekToScore", "GetRankOfItem"]
    sizes = [1024, 8192, 65536, 524288, 4194304]
    size_labels = ["1K", "8K", "64K", "512K", "4M"]

    fig, axes = plt.subplots(1, len(operations), figsize=(5 * len(operations), 5))

    for ax, op in zip(axes, operations):
        for struct in ["Fbtree", "Skiplist"]:
            points = [(r.item_count, r.items_per_sec / 1e6) for r in results
                      if r.structure == struct and r.operation == op
                      and r.build == build and r.items_per_sec > 0]
            if points:
                points.sort()
                counts, rates = zip(*points)
                ax.plot(counts, rates, label=struct, **STYLES[struct], markersize=6, linewidth=2)

        ax.axvline(x=LLC_ITEMS, color="gray", linestyle="--", linewidth=1, alpha=0.7, label="LLC boundary")
        ax.set_xlabel("Set Size (items)")
        ax.set_ylabel("Throughput (M ops/sec)")
        ax.set_title(op)
        setup_log_axis(ax, sizes, size_labels)
        ax.set_ylim(bottom=0)
        ax.legend(fontsize=9)
        ax.grid(True, alpha=0.3)

    fig.suptitle(f"Lookup Performance - Hot Cache", fontsize=14, fontweight="bold")
    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def plot_insert_performance(results: list[BenchmarkResult], filename: str):
    """Plot insert performance - Random and Append, 24B items."""
    sizes = [1024, 8192, 65536, 524288, 4194304]
    size_labels = ["1K", "8K", "64K", "512K", "4M"]

    fig, axes = plt.subplots(1, 2, figsize=(14, 6))

    for ax, (op, title) in zip(axes, [("Insert_Random", "Random Insert"), ("Insert_Append", "Sequential Append")]):
        for struct in ["Fbtree", "Skiplist"]:
            points = [(r.item_count, r.items_per_sec / 1e6) for r in results
                      if r.structure == struct and r.operation == op
                      and r.item_size == 24]
            if points:
                points.sort()
                counts, rates = zip(*points)
                ax.plot(counts, rates, label=struct, **STYLES[struct], markersize=6, linewidth=2)

        ax.axvline(x=LLC_ITEMS, color="gray", linestyle="--", linewidth=1, alpha=0.7, label="LLC boundary")
        ax.set_xlabel("Set Size (items)")
        ax.set_ylabel("Throughput (M items/sec)")
        ax.set_title(title)
        setup_log_axis(ax, sizes, size_labels)
        ax.set_ylim(bottom=0)
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)

    fig.suptitle("Insert Performance - Hot Cache (24B items)", fontsize=14, fontweight="bold")
    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def plot_delete_performance(results: list[BenchmarkResult], filename: str):
    """Plot delete performance - Random, PopHead, PopTail, 24B items."""
    sizes = [1024, 8192, 65536, 524288, 4194304]
    size_labels = ["1K", "8K", "64K", "512K", "4M"]

    fig, axes = plt.subplots(1, 3, figsize=(15, 5))

    for ax, (op, title) in zip(axes, [
        ("Delete_Random", "Random Delete"),
        ("Delete_PopHead", "Pop Head"),
        ("Delete_PopTail", "Pop Tail")
    ]):
        for struct in ["Fbtree", "Skiplist"]:
            points = [(r.item_count, r.items_per_sec / 1e6) for r in results
                      if r.structure == struct and r.operation == op
                      and r.item_size == 24]
            if points:
                points.sort()
                counts, rates = zip(*points)
                ax.plot(counts, rates, label=struct, **STYLES[struct], markersize=6, linewidth=2)

        ax.axvline(x=LLC_ITEMS, color="gray", linestyle="--", linewidth=1, alpha=0.7, label="LLC boundary")
        ax.set_xlabel("Set Size (items)")
        ax.set_ylabel("Throughput (M items/sec)")
        ax.set_title(title)
        setup_log_axis(ax, sizes, size_labels)
        ax.set_ylim(bottom=0)
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)

    fig.suptitle("Delete Performance - Hot Cache (24B items)", fontsize=14, fontweight="bold")
    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def plot_iterate_performance(results: list[BenchmarkResult], filename: str):
    """Plot iteration throughput vs size."""
    sizes = [1024, 8192, 65536, 524288, 4194304]
    size_labels = ["1K", "8K", "64K", "512K", "4M"]

    fig, ax = plt.subplots(figsize=(10, 6))

    for struct in ["Fbtree", "Skiplist"]:
        points = [(r.item_count, r.items_per_sec / 1e6) for r in results
                  if r.structure == struct and r.operation == "IterateForward"
                  and r.build == "RandBuild" and r.items_per_sec > 0]
        if points:
            points.sort()
            counts, rates = zip(*points)
            ax.plot(counts, rates, label=struct, **STYLES[struct], markersize=6, linewidth=2)

    ax.axvline(x=LLC_ITEMS, color="gray", linestyle="--", linewidth=1, alpha=0.7, label="LLC boundary")
    ax.set_xlabel("Set Size (items)", fontsize=12)
    ax.set_ylabel("Throughput (M items/sec)", fontsize=12)
    ax.set_title("Iteration Performance - Hot Cache", fontsize=14, fontweight="bold")
    setup_log_axis(ax, sizes, size_labels)
    ax.set_ylim(bottom=0)
    ax.legend(fontsize=11)
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def format_size(n: int) -> str:
    if n >= 1048576:
        return f"{n//1048576}M"
    if n >= 1024:
        return f"{n//1024}K"
    return str(n)


def print_summary_table(results: list[BenchmarkResult], target_count: int = 4194304):
    """Print text tables comparing fbtree vs skiplist."""
    operations = ["RankLookup", "SeekToScore", "GetRankOfItem", "IterateForward",
                  "Insert_Random", "Insert_Append", "Delete_Random", "Delete_PopHead",
                  "Delete_PopTail", "RangeDeleteByRank", "RangeDeleteByScore",
                  "PartialRangeScan", "MixedWorkload_R20", "MixedWorkload_R50",
                  "MixedWorkload_R80", "ScoreUpdate"]
    sizes = [1024, 8192, 65536, 524288, 4194304]
    size_labels = [format_size(s) for s in sizes]

    # Filter to operations that have results
    ops_found = [op for op in operations if any(r.operation == op for r in results)]

    if not ops_found:
        print("No results found.")
        return

    col_width = 12
    header = f"{'Operation':<18} {'Struct':<10}" + "".join(f"{l:>{col_width}}" for l in size_labels)
    print("\n" + "=" * len(header))
    print("Hot-Cache Benchmark Results (ops/sec)")
    print("=" * len(header))
    print(header)
    print("-" * len(header))

    for op in ops_found:
        for struct in ["Fbtree", "Skiplist"]:
            row = f"{op:<18} {struct:<10}"
            for size in sizes:
                # Try RandBuild first, then Insert/Delete builds
                r = next((x for x in results if x.structure == struct
                         and x.operation == op and x.item_count == size
                         and x.build in ["RandBuild", "Insert", "Delete",
                                         "RangeDelete", "PartialScan", "Mixed",
                                         "ScoreUpdate"]), None)
                if r and r.items_per_sec > 0:
                    row += f"{format_rate(r.items_per_sec) + '/s':>{col_width}}"
                else:
                    row += f"{'--':>{col_width}}"
            print(row)
        print()

    # Speedup summary
    print("=" * len(header))
    print("Fbtree Speedup vs Skiplist (>1 = Fbtree faster)")
    print("=" * len(header))
    header2 = f"{'Operation':<18}" + "".join(f"{l:>{col_width}}" for l in size_labels)
    print(header2)
    print("-" * len(header2))

    for op in ops_found:
        row = f"{op:<18}"
        for size in sizes:
            fbt = next((x.items_per_sec for x in results if x.structure == "Fbtree"
                       and x.operation == op and x.item_count == size
                       and x.build in ["RandBuild", "Insert", "Delete",
                                       "RangeDelete", "PartialScan", "Mixed",
                                       "ScoreUpdate"]), 0)
            sl = next((x.items_per_sec for x in results if x.structure == "Skiplist"
                      and x.operation == op and x.item_count == size
                      and x.build in ["RandBuild", "Insert", "Delete",
                                      "RangeDelete", "PartialScan", "Mixed",
                                      "ScoreUpdate"]), 0)
            if fbt > 0 and sl > 0:
                speedup = fbt / sl
                row += f"{speedup:>{col_width-1}.2f}x"
            else:
                row += f"{'--':>{col_width}}"
        print(row)
    print()


def plot_range_delete_performance(results: list[BenchmarkResult], filename: str):
    """Plot range delete throughput vs size - ByRank and ByScore."""
    sizes = [1024, 8192, 65536, 524288, 4194304]
    size_labels = ["1K", "8K", "64K", "512K", "4M"]

    fig, axes = plt.subplots(1, 2, figsize=(14, 6))

    for ax, (op, title) in zip(axes, [
        ("RangeDeleteByRank", "Range Delete by Rank"),
        ("RangeDeleteByScore", "Range Delete by Score")
    ]):
        for struct in ["Fbtree", "Skiplist"]:
            points = [(r.item_count, r.items_per_sec / 1e6) for r in results
                      if r.structure == struct and r.operation == op
                      and r.item_size == 24]
            if points:
                points.sort()
                counts, rates = zip(*points)
                ax.plot(counts, rates, label=struct, **STYLES[struct], markersize=6, linewidth=2)

        ax.axvline(x=LLC_ITEMS, color="gray", linestyle="--", linewidth=1, alpha=0.7, label="LLC boundary")
        ax.set_xlabel("Set Size (items)")
        ax.set_ylabel("Throughput (M items/sec)")
        ax.set_title(title)
        setup_log_axis(ax, sizes, size_labels)
        ax.set_ylim(bottom=0)
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)

    fig.suptitle("Range Delete Performance - Hot Cache (24B items)", fontsize=14, fontweight="bold")
    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def plot_partial_scan_performance(results: list[BenchmarkResult], filename: str):
    """Plot partial range scan throughput vs size."""
    sizes = [1024, 8192, 65536, 524288, 4194304]
    size_labels = ["1K", "8K", "64K", "512K", "4M"]

    fig, ax = plt.subplots(figsize=(10, 6))

    for struct in ["Fbtree", "Skiplist"]:
        points = [(r.item_count, r.items_per_sec / 1e6) for r in results
                  if r.structure == struct and r.operation == "PartialRangeScan"
                  and r.item_size == 24]
        if points:
            points.sort()
            counts, rates = zip(*points)
            ax.plot(counts, rates, label=struct, **STYLES[struct], markersize=6, linewidth=2)

    ax.axvline(x=LLC_ITEMS, color="gray", linestyle="--", linewidth=1, alpha=0.7, label="LLC boundary")
    ax.set_xlabel("Set Size (items)", fontsize=12)
    ax.set_ylabel("Throughput (M items/sec)", fontsize=12)
    ax.set_title("Partial Range Scan Performance - Hot Cache (24B items)", fontsize=14, fontweight="bold")
    setup_log_axis(ax, sizes, size_labels)
    ax.set_ylim(bottom=0)
    ax.legend(fontsize=11)
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def plot_mixed_workload_performance(results: list[BenchmarkResult], filename: str):
    """Plot mixed workload throughput vs size at different read percentages."""
    sizes = [1024, 8192, 65536, 524288, 4194304]
    size_labels = ["1K", "8K", "64K", "512K", "4M"]
    read_pcts = ["20", "50", "80"]

    fig, axes = plt.subplots(1, 3, figsize=(15, 5))

    for ax, pct in zip(axes, read_pcts):
        op = f"MixedWorkload_R{pct}"
        for struct in ["Fbtree", "Skiplist"]:
            points = [(r.item_count, r.items_per_sec / 1e6) for r in results
                      if r.structure == struct and r.operation == op
                      and r.item_size == 24]
            if points:
                points.sort()
                counts, rates = zip(*points)
                ax.plot(counts, rates, label=struct, **STYLES[struct], markersize=6, linewidth=2)

        ax.axvline(x=LLC_ITEMS, color="gray", linestyle="--", linewidth=1, alpha=0.7, label="LLC boundary")
        ax.set_xlabel("Set Size (items)")
        ax.set_ylabel("Throughput (M ops/sec)")
        ax.set_title(f"{pct}% Reads / {100 - int(pct)}% Writes")
        setup_log_axis(ax, sizes, size_labels)
        ax.set_ylim(bottom=0)
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)

    fig.suptitle("Mixed Workload Performance - Hot Cache (24B items)", fontsize=14, fontweight="bold")
    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def plot_score_update_performance(results: list[BenchmarkResult], filename: str):
    """Plot score update throughput vs size."""
    sizes = [1024, 8192, 65536, 524288, 4194304]
    size_labels = ["1K", "8K", "64K", "512K", "4M"]

    fig, ax = plt.subplots(figsize=(10, 6))

    for struct in ["Fbtree", "Skiplist"]:
        points = [(r.item_count, r.items_per_sec / 1e6) for r in results
                  if r.structure == struct and r.operation == "ScoreUpdate"
                  and r.item_size == 24]
        if points:
            points.sort()
            counts, rates = zip(*points)
            ax.plot(counts, rates, label=struct, **STYLES[struct], markersize=6, linewidth=2)

    ax.axvline(x=LLC_ITEMS, color="gray", linestyle="--", linewidth=1, alpha=0.7, label="LLC boundary")
    ax.set_xlabel("Set Size (items)", fontsize=12)
    ax.set_ylabel("Throughput (M ops/sec)", fontsize=12)
    ax.set_title("Score Update Performance - Hot Cache (24B items)", fontsize=14, fontweight="bold")
    setup_log_axis(ax, sizes, size_labels)
    ax.set_ylim(bottom=0)
    ax.legend(fontsize=11)
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def generate_report(results: list[BenchmarkResult]):
    print("# Fbtree vs Skiplist Performance Report")
    print("\n## Methodology")
    print("""
Benchmarks measure data structure performance with random access patterns.
Small sets (< LLC) benefit from cache; large sets simulate busy server conditions.

- **Build order**: Random insertion (simulates typical runtime behavior)
- **Metric**: Items per second (higher = better)
- **LLC boundary**: ~640K items at 24B with 3x overhead
""")

    print("\n## Results Summary")
    print_summary_table(results)

    print("\n## Visualizations")
    plot_lookup_by_size(results, "RandBuild", "perf_lookup_by_size.png")
    plot_insert_performance(results, "perf_insert.png")
    plot_delete_performance(results, "perf_delete.png")
    plot_iterate_performance(results, "perf_iterate.png")
    plot_range_delete_performance(results, "perf_range_delete.png")
    plot_partial_scan_performance(results, "perf_partial_scan.png")
    plot_mixed_workload_performance(results, "perf_mixed_workload.png")
    plot_score_update_performance(results, "perf_score_update.png")


def main():
    output = load_results()
    results = parse_benchmark_output(output)

    if not results:
        print("No benchmark results found.", file=sys.stderr)
        sys.exit(1)

    print(f"Parsed {len(results)} benchmark results", file=sys.stderr)
    generate_report(results)


if __name__ == "__main__":
    main()
