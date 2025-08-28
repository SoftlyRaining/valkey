"""Shared utilities for benchmark scripts."""

import os
import platform
import subprocess
import sys
from pathlib import Path

SRC_DIR = Path(__file__).parent.parent


def get_simd_cflags():
    """Return appropriate SIMD flags for the current platform."""
    machine = platform.machine().lower()
    if machine in ('x86_64', 'amd64'):
        return "-mavx2"
    return ""


def build_microbench():
    """Build valkey-microbench with appropriate SIMD flags."""
    print("Building valkey-microbench...", file=sys.stderr)
    
    # Check if cached SERVER_CFLAGS differs from what we need
    simd_flags = get_simd_cflags()
    settings_file = SRC_DIR / ".make-settings"
    if settings_file.exists():
        content = settings_file.read_text()
        for line in content.splitlines():
            if line.startswith("SERVER_CFLAGS="):
                cached = line.split("=", 1)[1]
                if cached != simd_flags:
                    print(f"SERVER_CFLAGS changed ({cached!r} -> {simd_flags!r}), cleaning...", file=sys.stderr)
                    subprocess.run(["make", "distclean"], cwd=SRC_DIR, check=True)
                break
    
    make_cmd = ["make", f"-j{os.cpu_count()*2}"]
    make_cmd.append(f"SERVER_CFLAGS={simd_flags}")
    make_cmd.append("valkey-microbench")
    subprocess.run(make_cmd, cwd=SRC_DIR, check=True)


def run_benchmarks(filter_pattern, output_file, append=False):
    """Run benchmarks matching filter and save output to file."""
    print(f"Running benchmarks with filter: {filter_pattern}", file=sys.stderr)

    mode = "a" if append else "w"
    with open(output_file, mode) as f:
        proc = subprocess.Popen(
            [str(SRC_DIR / "valkey-microbench"), f"--benchmark_filter={filter_pattern}"],
            cwd=SRC_DIR,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        for line in proc.stdout:
            print(line, end="", flush=True)
            f.write(line)
        stderr_output = proc.stderr.read()
        exit_code = proc.wait()

    if stderr_output:
        print(stderr_output, file=sys.stderr)
    if exit_code != 0:
        print(f"\nError: benchmark exited with code {exit_code}", file=sys.stderr)
        sys.exit(exit_code)

    print(f"\nResults saved to: {output_file}", file=sys.stderr)
