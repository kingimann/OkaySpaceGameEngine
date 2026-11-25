#!/bin/sh
# Standalone build of the okay-build prototype and its tests. Not part of CMake or CI.
# The tests also drive our own compilers (tools/okaycc: okay-cc, okay-ld; tools/okayms: okay-cl, okay-link). They are built here
# into "$out/compilers", one file at a time, and only again when one of their sources is newer. OKAYBUILD_OKAY_COMPILERS=DIR uses programs
# that are already built; OKAYBUILD_OKAY_COMPILERS=none skips those checks (they are counted as skipped, not passed).
set -e
here=$(cd "$(dirname "$0")" && pwd)
tools=$(cd "$here/.." && pwd)
out=${1:-/tmp/okaybuild-bin}
mkdir -p "$out"
g++ -std=c++17 -O2 -Wall -Wextra -pthread "$here/okay-build.cpp" -o "$out/okay-build"
g++ -std=c++17 -O0 -Wall -Wextra "$here/test_okaybuild.cpp" -o "$out/test_okaybuild"

# build_compiler_pair DIR SRCDIR INCLUDE MAIN1:NAME1 MAIN2:NAME2 LIBSRC...: build two programs from SRCDIR's library sources
build_compiler_pair() {
    dir=$1; src=$2; inc=$3; m1=$4; m2=$5; shift 5
    newest=$(find "$src" -newer "$dir/${m2#*:}" -name '*.[ch]pp' 2>/dev/null | head -1)
    if [ -x "$dir/${m1#*:}" ] && [ -x "$dir/${m2#*:}" ] && [ -z "$newest" ]; then return 0; fi
    mkdir -p "$dir/obj"
    objs=""
    for f in "$@"; do
        o="$dir/obj/$(basename "$f" .cpp).o"
        g++ -std=c++17 -O1 -I "$inc" -c "$src/$f" -o "$o"
        objs="$objs $o"
    done
    # shellcheck disable=SC2086
    g++ -std=c++17 -O1 -I "$inc" "$src/${m1%%:*}" $objs -o "$dir/${m1#*:}"
    # shellcheck disable=SC2086
    g++ -std=c++17 -O1 -I "$inc" "$src/${m2%%:*}" $objs -o "$dir/${m2#*:}"
}
if [ -z "${OKAYBUILD_OKAY_COMPILERS:-}" ]; then
    cdir="$out/compilers"
    cc_srcs=$(cd "$tools/okaycc" && ls src/*.cpp | grep -v 'main\.cpp$')
    # shellcheck disable=SC2086
    build_compiler_pair "$cdir" "$tools/okaycc" "$tools/okaycc/src" src/main.cpp:okay-cc src/ldmain.cpp:okay-ld $cc_srcs
    ms_srcs=$(cd "$tools/okayms" && ls src/*.cpp)
    # shellcheck disable=SC2086
    build_compiler_pair "$cdir/ms" "$tools/okayms" "$tools/okayms/include" cmd/okay-cl.cpp:okay-cl cmd/okay-link.cpp:okay-link $ms_srcs
    cp "$cdir/ms/okay-cl" "$cdir/ms/okay-link" "$cdir/"
    OKAYBUILD_OKAY_COMPILERS=$cdir
fi
export OKAYBUILD_OKAY_COMPILERS
"$out/test_okaybuild" "$out/okay-build"
