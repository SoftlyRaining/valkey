#!/usr/bin/env python3
"""Generate memory overhead report from saved memory_benchmark_output.txt."""

import re
import sys
from dataclasses import dataclass
from pathlib import Path

import matplotlib
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

matplotlib.use("Agg")

SCRIPT_DIR = Path(__file__).parent
OUTPUT_FILE = SCRIPT_DIR / "memory_benchmark_output.txt"


@dataclass
class MemoryResult:
    structure: str      # "Fbtree", "Skiplist", "Dict", "Hashtable", "Zset_*"
    scenario: str       # "BySize", "ByCount"
    variation: str      # "Append", "Prepend", "Random", or ""
    param: int          # item_size or item_count depending on scenario
    total_bytes: float
    bytes_per_item: float


def load_results() -> str:
    if not OUTPUT_FILE.exists():
        print(f"Error: {OUTPUT_FILE} not found. Run run_memory_benchmarks.py first.", file=sys.stderr)
        sys.exit(1)
    return OUTPUT_FILE.read_text()


def parse_benchmark_output(text: str) -> list[MemoryResult]:
    results = []

    # Pattern: BM_Skiplist_Mem_ByCount_Random/1024/iterations:1 ... BytesPerItem=48 TotalBytes=48000
    pattern = re.compile(
        r"^BM_(\w+)_Mem_(\w+?)(?:_(Append|Prepend|Random))?/(\d+)/iterations:\d+\s+"
        r".*?BytesPerItem=([\d.e+]+)\s+TotalBytes=([\d.e+kKmM]+)",
        re.MULTILINE
    )

    for m in pattern.finditer(text):
        struct, scenario, variation, param, bpi, total = m.groups()
        results.append(MemoryResult(
            structure=struct,
            scenario=scenario,
            variation=variation or "",
            param=int(param),
            total_bytes=float(total.replace('k', 'e3').replace('K', 'e3').replace('M', 'e6').replace('m', 'e6')),
            bytes_per_item=float(bpi),
        ))

    # Pattern for MixedWorkload: BM_Fbtree_Mem_MixedWorkload/1024/iterations:1 ...
    mixed_pattern = re.compile(
        r"^BM_(\w+)_Mem_MixedWorkload/(\d+)/iterations:\d+\s+"
        r".*?BytesPerItem=([\d.e+]+)\s+.*?TotalBytes=([\d.e+kKmM]+)",
        re.MULTILINE
    )

    for m in mixed_pattern.finditer(text):
        struct, param, bpi, total = m.groups()
        results.append(MemoryResult(
            structure=struct,
            scenario="ByCount",
            variation="MixedWorkload",
            param=int(param),
            total_bytes=float(total.replace('k', 'e3').replace('K', 'e3').replace('M', 'e6').replace('m', 'e6')),
            bytes_per_item=float(bpi),
        ))

    return results


def format_size(n: int) -> str:
    if n >= 1048576:
        return f"{n // 1048576}M"
    if n >= 1024:
        return f"{n // 1024}K"
    return str(n)


from plot_config import STYLES


def setup_log_axis(ax, ticks, labels):
    """Configure log x-axis with specified ticks and labels."""
    ax.set_xscale("log", base=2)
    ax.set_xticks(ticks)
    ax.set_xticklabels(labels)
    ax.xaxis.set_minor_formatter(ticker.NullFormatter())
    ax.set_xlim(ticks[0] * 0.7, ticks[-1] * 1.4)


def plot_overhead_by_count(results: list[MemoryResult], filename: str):
    """Plot memory overhead (bytes per item) vs item count."""
    fig, ax = plt.subplots(figsize=(10, 6))

    series = [
        ("Fbtree", "ByCount", "Random", "Fbtree (random)", STYLES["Fbtree"]),
        ("Fbtree", "ByCount", "Append", "Fbtree (append)", STYLES["Fbtree_Append"]),
        ("Fbtree", "ByCount", "MixedWorkload", "Fbtree (mixed ins/del)", STYLES["Fbtree_Mixed"]),
        ("Skiplist", "ByCount", "Random", "Skiplist (random)", STYLES["Skiplist"]),
        ("Skiplist", "ByCount", "Append", "Skiplist (append)", STYLES["Skiplist_Append"]),
    ]

    for struct, scenario, variation, label, style in series:
        points = [(r.param, r.bytes_per_item) for r in results
                  if r.structure == struct and r.scenario == scenario and r.variation == variation]
        if points:
            points.sort()
            counts, bpi = zip(*points)
            ax.plot(counts, bpi, label=label, **style, markersize=6, linewidth=2)

    ax.axvline(x=128, color="gray", linestyle="--", linewidth=1, alpha=0.7, label="Listpack threshold")

    sizes = [8, 64, 512, 4096, 32768, 262144, 2097152]
    labels = ["8", "64", "512", "4K", "32K", "256K", "2M"]
    setup_log_axis(ax, sizes, labels)

    ax.set_xlabel("Item Count", fontsize=12)
    ax.set_ylabel("Overhead Bytes per Item", fontsize=12)
    ax.set_title("Memory Overhead vs Set Size (24B items)", fontsize=14, fontweight="bold")
    ax.set_ylim(bottom=0)
    ax.legend(fontsize=10)
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")



def plot_zset_comparison(results: list[MemoryResult], filename: str):
    """Plot full ZSET memory comparison (listpack vs skiplist+ht vs fbtree+ht)."""
    fig, ax = plt.subplots(figsize=(10, 6))

    series = [
        ("Zset_Listpack", "ByCount", "", "Listpack", {"color": "green", "marker": "s"}),
        ("Zset_Skiplist", "ByCount", "", "Skiplist + Hashtable", STYLES["Skiplist"]),
        ("Zset_Fbtree", "ByCount", "", "Fbtree + Hashtable", STYLES["Fbtree"]),
    ]

    for struct, scenario, variation, label, style in series:
        points = [(r.param, r.bytes_per_item) for r in results
                  if r.structure == struct and r.scenario == scenario]
        if points:
            points.sort()
            counts, bpi = zip(*points)
            ax.plot(counts, bpi, label=label, **style, markersize=6, linewidth=2)

    ax.axvline(x=128, color="gray", linestyle="--", linewidth=1, alpha=0.7, label="Listpack threshold")

    ax.set_xlabel("Item Count", fontsize=12)
    ax.set_ylabel("Bytes per Item (total)", fontsize=12)
    ax.set_title("Full ZSET Memory Usage (32B items)", fontsize=14, fontweight="bold")
    ax.set_ylim(bottom=0)
    ax.legend(fontsize=10)
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    out = SCRIPT_DIR / filename
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Generated: {out}")


def print_summary_table(results: list[MemoryResult]):
    """Print text summary of memory results."""
    # Group by structure and scenario
    by_count = [r for r in results if r.scenario == "ByCount"]
    by_size = [r for r in results if r.scenario == "BySize"]

    if by_count:
        # Find common counts
        counts = sorted(set(r.param for r in by_count))
        count_labels = [format_size(c) for c in counts[:10]]
        counts = counts[:10]

        col_width = 10
        header = f"{'Structure':<25}" + "".join(f"{l:>{col_width}}" for l in count_labels)
        print("\n" + "=" * len(header))
        print("Memory Overhead by Item Count (bytes per item)")
        print("=" * len(header))
        print(header)
        print("-" * len(header))

        seen = set()
        for r in by_count:
            key = (r.structure, r.variation)
            if key in seen:
                continue
            seen.add(key)

            label = f"{r.structure}" + (f" ({r.variation})" if r.variation else "")
            row = f"{label:<25}"
            for count in counts:
                match = next((x for x in by_count if x.structure == r.structure
                             and x.variation == r.variation and x.param == count), None)
                if match:
                    row += f"{match.bytes_per_item:>{col_width}.1f}"
                else:
                    row += f"{'--':>{col_width}}"
            print(row)
        print()

    if by_size:
        sizes = sorted(set(r.param for r in by_size))[:8]
        size_labels = [str(s) for s in sizes]

        col_width = 8
        header = f"{'Structure':<25}" + "".join(f"{l:>{col_width}}" for l in size_labels)
        print("=" * len(header))
        print("Memory Overhead by Item Size (bytes per item)")
        print("=" * len(header))
        print(header)
        print("-" * len(header))

        seen = set()
        for r in by_size:
            key = (r.structure, r.variation)
            if key in seen:
                continue
            seen.add(key)

            label = f"{r.structure}" + (f" ({r.variation})" if r.variation else "")
            row = f"{label:<25}"
            for size in sizes:
                match = next((x for x in by_size if x.structure == r.structure
                             and x.variation == r.variation and x.param == size), None)
                if match:
                    row += f"{match.bytes_per_item:>{col_width}.1f}"
                else:
                    row += f"{'--':>{col_width}}"
            print(row)
        print()


def main():
    output = load_results()
    results = parse_benchmark_output(output)

    if not results:
        print("No memory benchmark results found.", file=sys.stderr)
        sys.exit(1)

    print(f"Parsed {len(results)} memory benchmark results", file=sys.stderr)

    print("# Memory Overhead Report")
    print("\n## Summary")
    print_summary_table(results)

    print("\n## Visualizations")
    plot_overhead_by_count(results, "mem_overhead_by_count.png")
    plot_zset_comparison(results, "mem_zset_comparison.png")


if __name__ == "__main__":
    main()
