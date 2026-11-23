#!/usr/bin/env bash
# How many compile jobs to run at once: one per core, but never more than the memory allows.
# Usage: scripts/ci_jobs.sh [GB per job, default 4]   -> prints a number (at least 1).
# Why: a bare `-j` (ninja's default is cores + 2) let several of the biggest files (editor and player main.cpp,
# SceneSerializer.cpp, OkayScriptVM.cpp; 4-6 GB each) compile at once on a 16 GB CI runner. The runner ran out of
# memory and GitHub lost it ("The hosted runner lost communication with the server", #1120, #1140, #1143 on 2026-11-22).
# JOBS=N in the environment still wins. Used by ci.yml's build steps and scripts/build-windows-dist.sh.
per_job_gb="${1:-4}"
if [ -n "${JOBS:-}" ]; then echo "$JOBS"; exit 0; fi
cores="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
mem_kb="$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo 2>/dev/null || echo 0)"
if [ "${mem_kb:-0}" -gt 0 ]; then
    by_mem=$(( mem_kb / (per_job_gb * 1024 * 1024) ))
    [ "$by_mem" -lt 1 ] && by_mem=1
    jobs=$(( cores < by_mem ? cores : by_mem ))
else
    jobs="$cores"
fi
echo "compile jobs: $jobs (cores $cores, available memory $(( mem_kb / 1024 )) MB, ${per_job_gb} GB per job)" >&2
echo "$jobs"
