#!/usr/bin/env python3
"""Compare two bench_frame command lines fairly: run them one after the other, several times, and print the median.

    python3 scripts/bench_ab.py LABEL RUNS "COMMAND_A" "COMMAND_B"

Example (what a change is worth: the old path against the new path, same program):

    B=build/tests/bench_frame
    python3 scripts/bench_ab.py "shadow depth" 5 "$B --scene cubes2000 --frames 8 --warmup 3 --no-span" \
                                                 "$B --scene cubes2000 --frames 8 --warmup 3"

Why it exists (docs/engine/perf_log.md): on a shared machine one run is not a measurement. The two commands are run in turns
(A, B, A, B, ...), so a slow minute hits both. For every phase row of the bench table the script takes the median
column of each run, then the median of those over the runs, and prints A, B and A/B (above 1 means B is faster). The
two hash lines at the end must be equal when B is only meant to be faster.

Rows under 0.02 ms in both commands are left out. Needs only Python 3; no other package.
Written from scratch for this repository.
"""
import collections
import re
import shlex
import statistics
import subprocess
import sys

ROW = re.compile(r"^\s{3,}(\S.*?)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)$")   # "   phase name   median  p95  max"


def one_run(command):
    """Run one command line; return ({phase: median ms}, hash line)."""
    out = subprocess.run(shlex.split(command), capture_output=True, text=True).stdout
    rows, hashes = {}, ""
    for line in out.split("\n"):
        m = ROW.match(line)
        if m and not line.strip().startswith("phase"):
            rows[m.group(1)] = float(m.group(2))
        if "frame_hash" in line:
            hashes = line.strip()
    return rows, hashes


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        return 2
    label, runs, cmd_a, cmd_b = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
    samples = {cmd_a: collections.defaultdict(list), cmd_b: collections.defaultdict(list)}
    hashes = {}
    for _ in range(runs):
        for cmd in (cmd_a, cmd_b):
            rows, hashes[cmd] = one_run(cmd)
            for phase, ms in rows.items():
                samples[cmd][phase].append(ms)
    print(f"## {label}   ({runs} runs each, alternating; median of the per-run medians, ms)")
    print(f"A = {cmd_a}")
    print(f"B = {cmd_b}")
    print(f"{'phase':36s} {'A':>10s} {'B':>10s} {'A/B':>7s}")
    for phase in sorted(set(samples[cmd_a]) | set(samples[cmd_b])):
        a = statistics.median(samples[cmd_a][phase]) if samples[cmd_a][phase] else 0.0
        b = statistics.median(samples[cmd_b][phase]) if samples[cmd_b][phase] else 0.0
        if max(a, b) < 0.02:
            continue
        print(f"{phase:36s} {a:10.3f} {b:10.3f} {(a / b if b > 0 else 0.0):7.2f}x")
    print("hash A:", hashes.get(cmd_a))
    print("hash B:", hashes.get(cmd_b))
    print("hashes equal" if hashes.get(cmd_a) == hashes.get(cmd_b) else "HASHES DIFFER: the two commands did not produce the same picture and motion")
    return 0


if __name__ == "__main__":
    sys.exit(main())
