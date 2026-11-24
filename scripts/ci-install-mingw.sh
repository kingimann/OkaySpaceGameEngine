#!/usr/bin/env bash
# CI helper: install the MinGW-w64 cross compiler (x86_64, win32 thread model) quickly and reliably.
#
# Usage: scripts/ci-install-mingw.sh [extra apt packages...]
# Env:   OKAY_APT_ARCHIVES  folder for downloaded .deb files (default ~/apt-cache). CI keeps it in the
#                           Actions cache, so most runs install from it with no download at all.
#
# Why (2026-11-24): the runner's Ubuntu mirror sometimes served about 150 KB/s. A cold install (161 MB: cmake plus BOTH
# MinGW thread models) then outlasted the 25-minute step limit and failed the PR check ("apt attempt 1 failed or timed
# out"). The old cache key also named the runner image version, so a runner on a new image started with nothing.
# What this does instead:
#   1. Only the win32 thread model is installed (about 70 MB less). That is the model the release is built with, and the
#      one CLAUDE.md rule 21a is about. The posix one was pulled in only by the g++-mingw-w64-x86-64 metapackage.
#   2. cmake and ninja come from the runner image when it has them (no 17 MB cmake download).
#   3. First try: install from the cached .deb files with no network ("--no-download"). A full cache hit takes seconds.
#   4. Otherwise refresh the package index (scripts/ci-apt-mirror.sh), download the missing .deb files several at a
#      time with curl (resumable, slow transfers are dropped and retried, another mirror is tried), then let apt install.
#      apt checks every file's hash and fetches anything still missing itself.
#   5. Check the result: the compiler must exist and say "Thread model: win32" (rule 21a), and every extra package asked for
#      must be installed, or the script fails.
set -uo pipefail
cd "$(dirname "$0")/.."

ARCHIVES="${OKAY_APT_ARCHIVES:-$HOME/apt-cache}"
mkdir -p "$ARCHIVES/partial"
EXTRA=("$@")
PKGS=(gcc-mingw-w64-x86-64-win32 g++-mingw-w64-x86-64-win32 binutils-mingw-w64-x86-64 mingw-w64-x86-64-dev ccache "$@")
command -v cmake >/dev/null 2>&1 || PKGS+=(cmake)
command -v ninja >/dev/null 2>&1 || PKGS+=(ninja-build)
APT=(-o "Dir::Cache::archives=$ARCHIVES" -o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30
     -o DPkg::Lock::Timeout=120)
echo "packages: ${PKGS[*]}"
echo "cached .deb files: $(ls "$ARCHIVES"/*.deb 2>/dev/null | wc -l)"

apt_install() { local t="$1"; shift; timeout "$t" sudo apt-get "${APT[@]}" install -y --no-install-recommends "$@" "${PKGS[@]}"; }   # apt_install SECONDS [apt options]

check() {
    command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1 || return 1
    x86_64-w64-mingw32-g++ -v 2>&1 | grep -q '^Thread model: win32$' || return 1
    command -v x86_64-w64-mingw32-windres >/dev/null 2>&1 || return 1
    command -v cmake >/dev/null 2>&1 && command -v ninja >/dev/null 2>&1 && command -v ccache >/dev/null 2>&1 || return 1
    local p; for p in "${EXTRA[@]}"; do dpkg -s "$p" 2>/dev/null | grep -q '^Status: install ok installed' || return 1; done   # every extra package asked for
}

done_ok() {
    if ! check; then
        echo "::error title=MinGW toolchain check failed::x86_64-w64-mingw32-g++ is missing or is not the win32 thread model (CLAUDE.md rule 21a)"
        x86_64-w64-mingw32-g++ -v 2>&1 | tail -3 || true
        exit 1
    fi
    echo "compiler: $(readlink -f "$(command -v x86_64-w64-mingw32-g++)") ($(x86_64-w64-mingw32-g++ -dumpfullversion), $(x86_64-w64-mingw32-g++ -v 2>&1 | grep '^Thread model'))"
    echo "cmake: $(command -v cmake) $(cmake --version | head -1)   ninja: $(command -v ninja) $(ninja --version)   ccache: $(ccache --version | head -1)"
    echo "toolchain installed in $(( $(date +%s) - START ))s ($1)"
    exit 0
}
START=$(date +%s)

# 1. From the cache only, with the runner image's package index. No network.
if apt_install 300 --no-download -qq >/dev/null 2>&1 && check; then done_ok "all from the cache"; fi
echo "not everything is in the cache (or the image's package index differs); refreshing the index"

# 2. Fresh package index (switches mirror if the default one is down; retries an empty index).
bash scripts/ci-apt-mirror.sh
if apt_install 300 --no-download -qq >/dev/null 2>&1 && check; then done_ok "from the cache after an index refresh"; fi

# 3. Download what is missing, several files at once. Each line of --print-uris: 'URI' file size hash.
MIRROR="$(grep -v '^#' /etc/apt/apt-mirrors.txt 2>/dev/null | awk 'NF {print $1; exit}')"
[ -n "$MIRROR" ] && MIRROR="${MIRROR%/}/"
uris="$(sudo apt-get "${APT[@]}" install -y --no-install-recommends --print-uris -qq "${PKGS[@]}" 2>/dev/null | grep "^'" || true)"
fetch() {   # fetch URI FILE SIZE MIRROR
    local uri="$1" file="$2" size="$3" mirror="$4" dest="$ARCHIVES/$2" part="$ARCHIVES/partial/$2"
    [ -f "$dest" ] && [ "$(stat -c %s "$dest")" = "$size" ] && return 0
    case "$uri" in mirror+file:*) uri="${mirror}${uri#mirror+file:*/apt-mirrors.txt/}" ;; esac
    local alt="http://archive.ubuntu.com/ubuntu/pool/${uri#*/pool/}"
    for i in 1 2 3 4 5 6; do
        local u="$uri"; [ $((i % 2)) -eq 0 ] && u="$alt"
        # -C -: continue a partial file. --speed-limit/-time: drop a transfer slower than 50 KB/s for 30 s and retry.
        curl -fsSL -C - --connect-timeout 20 --speed-limit 50000 --speed-time 30 --max-time 600 -o "$part" "$u" 2>/dev/null || true
        if [ -f "$part" ] && [ "$(stat -c %s "$part")" = "$size" ]; then mv -f "$part" "$dest"; return 0; fi
        # A server that ignores the range (or a wrong file) leaves a file bigger than expected: start again.
        [ -f "$part" ] && [ "$(stat -c %s "$part")" -gt "$size" ] && rm -f "$part"
        sleep $((i * 3))
    done
    echo "could not fetch $file (apt will try)"
    return 1
}
export -f fetch
export ARCHIVES
if [ -n "$uris" ]; then
    sudo chown -R "$(id -u)" "$ARCHIVES"
    n=$(printf '%s\n' "$uris" | wc -l)
    mb=$(printf '%s\n' "$uris" | awk '{s+=$3} END {printf "%d", s/1048576}')
    echo "downloading $n files ($mb MB) from ${MIRROR:-the sources list}, 6 at a time"
    t0=$(date +%s)
    printf '%s\n' "$uris" | tr -d "'" | awk -v m="$MIRROR" '{print $1, $2, $3, m}' |
        timeout 900 xargs -P 6 -n 4 bash -c 'fetch "$@"' _ || echo "some downloads failed or timed out; apt will fetch the rest"
    echo "download took $(( $(date +%s) - t0 ))s"
fi

# 4. Install. apt verifies each file's hash and downloads anything still missing (resuming from the cache folder).
for i in 1 2 3; do
    if apt_install 600 && check; then done_ok "downloaded"; fi
    echo "apt install attempt $i failed or timed out; retrying"
    sudo pkill -x apt-get || true
    sleep 10
    timeout 240 sudo apt-get "${APT[@]}" update -qq || true
done
echo "::error title=MinGW toolchain install failed::apt could not install ${PKGS[*]} after 3 attempts"
exit 1
