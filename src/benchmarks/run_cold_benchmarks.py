#!/usr/bin/env python3
"""Build and run cold-cache benchmarks, saving output to cold_benchmark_output.txt."""

from pathlib import Path
from bench_utils import build_microbench, run_benchmarks

OUTPUT_FILE = Path(__file__).parent / "cold_benchmark_output.txt"
FILTER = "^(Fbtree_Cold|Fbtree_Cold_Insert|Fbtree_Cold_Delete|Fbtree_Cold_PopHead|Fbtree_Cold_PopTail|Skiplist_Cold|Skiplist_Cold_Insert|Skiplist_Cold_Delete|Skiplist_Cold_PopHead|Skiplist_Cold_PopTail)/"

if __name__ == "__main__":
    build_microbench()
    run_benchmarks(FILTER, OUTPUT_FILE)
