#!/usr/bin/env python3
"""Build and run memory overhead benchmarks, saving output to memory_benchmark_output.txt."""

from pathlib import Path
from bench_utils import build_microbench, run_benchmarks

OUTPUT_FILE = Path(__file__).parent / "memory_benchmark_output.txt"

FILTERS = [
    "BM_(Fbtree|Skiplist)_Mem_",
    "BM_Zset_.*_Mem_",
]

if __name__ == "__main__":
    build_microbench()
    run_benchmarks("|".join(FILTERS), OUTPUT_FILE)
