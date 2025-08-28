#!/usr/bin/env python3
"""Build and run hot cache benchmarks, saving output to benchmark_output.txt."""

from pathlib import Path
from bench_utils import build_microbench, run_benchmarks

OUTPUT_FILE = Path(__file__).parent / "benchmark_output.txt"

FILTERS = [
    "(Fbtree|Skiplist)_RandBuild/",
    "BM_(Fbtree|Skiplist)_Insert_(Random|Append)",
    "BM_(Fbtree|Skiplist)_Delete_",
]

if __name__ == "__main__":
    build_microbench()
    run_benchmarks("|".join(FILTERS), OUTPUT_FILE)
