#!/usr/bin/env bash
# Refuse to build or ship a RELEASE with our own unreviewed crypto or the own-stack dogfood switch on.
# (CLAUDE.md rule 18: cryptography cannot be replaced on our word; docs/own_stack_rollout.md.)
#
#   scripts/check_release_own_stack.sh                 check the environment and the cmake arguments after "--"
#   scripts/check_release_own_stack.sh --cache DIR     also read DIR/CMakeCache.txt (a configured build tree)
#   scripts/check_release_own_stack.sh --binary PATH   run a built program with OKAY_PRINT_STACK_AND_EXIT=1; it must print
#                                                      a stack line with exactly `crypto=libsodium` (checks the BUILT result;
#                                                      only for a program that can run on this host: not a Windows exe here)
#   scripts/check_release_own_stack.sh --self-test     test this script (used by ctest: release_refuses_own_crypto)
#
# Refused also: a release that does not really have libsodium. With --cache DIR the tree's CMakeCache.txt must say
# OKAY_SODIUM_IN_BUILD=ON (set by engine/CMakeLists.txt only when libsodium was found and linked) and OKAY_USE_SODIUM not OFF,
# and, if present, build.ninja / compile_commands.json must carry OKAY_HAVE_SODIUM and must not carry OKAY_OWN_CRYPTO.
# Without libsodium the code silently falls back to our own unreviewed okay::crypto, so names alone are not enough.
# Also refused: -D forms with a space ("-D OKAY_OWN_CRYPTO=ON"), and OKAY_OWN_CRYPTO / OKAY_OWN_STACK_CRYPTO_TEST set to ON
# in the build.ninja or compile_commands.json (a toolchain file `set(OKAY_OWN_CRYPTO ON)` leaves no cache entry but does
# reach the compiler as -DOKAY_OWN_CRYPTO).
#
# Refused: the environment variables OKAY_OWN_CRYPTO, OKAY_OWN_STACK_CRYPTO_TEST, OKAY_OWN_STACK and OKAY_OWN_SIGNING (our own
# unreviewed test-key signing code, docs/own_signing_plan.md) set to anything
# that is not empty, 0, OFF or NO; and the same names set ON/1/TRUE/YES in a cmake cache or on a -D argument.
# Exit 0 = fine, 1 = refused (the message names what), 2 = bad use.
set -u

NAMES="OKAY_OWN_CRYPTO OKAY_OWN_STACK_CRYPTO_TEST OKAY_OWN_STACK OKAY_OWN_SIGNING"

is_on() {   # a value that means "on" for CMake
    case "$(printf '%s' "$1" | tr '[:lower:]' '[:upper:]')" in
        ""|0|OFF|NO|FALSE|N|IGNORE|NOTFOUND|*-NOTFOUND) return 1 ;;
        *) return 0 ;;
    esac
}

check_env() {
    local bad=0 n v
    for n in $NAMES; do
        v="${!n:-}"
        if is_on "$v"; then echo "refused: environment variable $n=$v is set; a release must not use it" >&2; bad=1; fi
    done
    return $bad
}

check_cache() {   # $1 = a CMakeCache.txt
    local bad=0 n line v
    [ -f "$1" ] || { echo "error: no such cache file: $1" >&2; return 2; }
    for n in $NAMES; do
        line="$(grep -E "^${n}(:[A-Z]+)?=" "$1" | head -n1)"
        [ -n "$line" ] || continue
        v="${line#*=}"
        if is_on "$v"; then echo "refused: $1 has $n=$v; a release must not use it" >&2; bad=1; fi
    done
    check_built "$(dirname "$1")" || bad=1
    return $bad
}

check_built() {   # $1 = a configured build directory: the BUILT result must have libsodium and not our own crypto
    local d="$1" bad=0 c="$1/CMakeCache.txt" line v f seen=0
    line="$(grep -E '^OKAY_SODIUM_IN_BUILD(:[A-Z]+)?=' "$c" | head -n1)"
    v="${line#*=}"
    if [ -z "$line" ]; then
        echo "refused: $c has no OKAY_SODIUM_IN_BUILD entry: cannot show that libsodium is in this build (configure with the repository's CMake and -DOKAY_REQUIRE_SODIUM=ON)" >&2; bad=1
    elif ! is_on "$v"; then
        echo "refused: $c says OKAY_SODIUM_IN_BUILD=$v: libsodium is NOT in this build, so Ed25519 would silently use our own unreviewed okay::crypto" >&2; bad=1
    fi
    line="$(grep -E '^OKAY_USE_SODIUM(:[A-Z]+)?=' "$c" | head -n1)"
    if [ -n "$line" ] && ! is_on "${line#*=}"; then echo "refused: $c has OKAY_USE_SODIUM=${line#*=}; a release must use libsodium" >&2; bad=1; fi
    line="$(grep -E '^OKAY_REQUIRE_SODIUM(:[A-Z]+)?=' "$c" | head -n1)"
    if [ -z "$line" ] || ! is_on "${line#*=}"; then echo "refused: $c does not have OKAY_REQUIRE_SODIUM=ON; a release is configured with -DOKAY_REQUIRE_SODIUM=ON" >&2; bad=1; fi
    # Only the compile lines of the engine and the programs count: test targets define OKAY_OWN_CRYPTO on purpose (a
    # release has OKAY_BUILD_TESTS=OFF, but the guard does not depend on that).
    local flagsmake txt; flagsmake="$(find "$d" -maxdepth 4 -path '*okay_engine.dir/flags.make' 2>/dev/null | head -n1)"
    for f in "$d/build.ninja" "$d/compile_commands.json" ${flagsmake:+"$flagsmake"}; do
        [ -f "$f" ] || continue
        seen=1
        case "$f" in
            *.ninja) txt="$(awk '/^build /{ok = ($0 ~ /(^|[ \/])(engine|player|launcher|editor)\/CMakeFiles\//)} ok && /^[ \t]+(DEFINES|FLAGS)/ {print}' "$f")" ;;
            *.json)  txt="$(grep '"command"' "$f" | grep -E '(engine|player|launcher|editor)/CMakeFiles/')" ;;
            *)       txt="$(cat "$f")" ;;
        esac
        if [ -z "$txt" ]; then echo "refused: $f has no compile flags for the engine or the programs: cannot show libsodium is in" >&2; bad=1; continue; fi
        if printf '%s\n' "$txt" | grep -q -E '(^|[^A-Za-z0-9_])-?D?OKAY_OWN_CRYPTO([^A-Za-z0-9_]|$)'; then echo "refused: $f defines OKAY_OWN_CRYPTO for the engine or a program (our own crypto reaches the compiler, whatever the cache says)" >&2; bad=1; fi
        if printf '%s\n' "$txt" | grep -q -E '(^|[^A-Za-z0-9_])-?D?OKAY_OWN_STACK_CRYPTO_TEST([^A-Za-z0-9_]|$)'; then echo "refused: $f defines OKAY_OWN_STACK_CRYPTO_TEST for the engine or a program" >&2; bad=1; fi
        if ! printf '%s\n' "$txt" | grep -q 'OKAY_HAVE_SODIUM'; then echo "refused: $f has no OKAY_HAVE_SODIUM for the engine or the programs: the compiler is not told libsodium is in" >&2; bad=1; fi
    done
    [ "$seen" -eq 1 ] || echo "note: no build.ninja / compile_commands.json (or Makefile flags) in $d: only the cache was checked" >&2
    return $bad
}

check_binary() {   # $1 = a built program that can run here
    local bin="$1" out line
    [ -x "$bin" ] || { echo "error: not an executable file: $bin" >&2; return 2; }
    out="$(OKAY_PRINT_STACK_AND_EXIT=1 timeout 60 "$bin" 2>/dev/null)"; local rc=$?
    line="$(printf '%s\n' "$out" | grep -m1 '^\[okay\] stack: ')"
    if [ "$rc" -ne 0 ] || [ -z "$line" ]; then
        echo "refused: $bin did not print a stack line with OKAY_PRINT_STACK_AND_EXIT=1 (exit $rc); cannot show it uses libsodium" >&2; return 1
    fi
    case " $line " in
        *" crypto=libsodium "*) echo "check_release_own_stack: $bin says: $line" ;;
        *) echo "refused: $bin says: $line  (a release must say crypto=libsodium)" >&2; return 1 ;;
    esac
}

check_args() {   # cmake -D arguments, also "-D NAME=VALUE" written as two words
    local bad=0 a n v prev=""
    for a in "$@"; do
        if [ "$prev" = "-D" ]; then a="-D$a"; fi
        prev=""
        case "$a" in
            -D) prev="-D"; continue ;;
            -D*)
                a="${a#-D}"; n="${a%%[:=]*}"; v="${a#*=}"
                case " $NAMES " in *" $n "*) if is_on "$v"; then echo "refused: -D$n=$v on the command line; a release must not use it" >&2; bad=1; fi ;; esac
                case "$n" in OKAY_USE_SODIUM|OKAY_REQUIRE_SODIUM) if [ "$n" = OKAY_USE_SODIUM ] && ! is_on "$v"; then echo "refused: -D$n=$v on the command line; a release must use libsodium" >&2; bad=1; fi
                              if [ "$n" = OKAY_REQUIRE_SODIUM ] && ! is_on "$v"; then echo "refused: -D$n=$v on the command line; a release is configured with -DOKAY_REQUIRE_SODIUM=ON" >&2; bad=1; fi ;; esac ;;
        esac
    done
    return $bad
}

self_test() {
    local d fails=0 rc
    d="$(mktemp -d)" || exit 2
    trap "rm -rf '$d'" EXIT
    expect() {   # expect <want-exit> <description> <command...>
        local want="$1" what="$2"; shift 2
        "$@" >/dev/null 2>&1; rc=$?
        if [ "$rc" -ne "$want" ]; then echo "SELF-TEST FAIL: $what (exit $rc, wanted $want)" >&2; fails=$((fails + 1)); fi
    }
    local me="$0"
    expect 0 "clean environment"                      env -u OKAY_OWN_CRYPTO -u OKAY_OWN_STACK_CRYPTO_TEST -u OKAY_OWN_STACK bash "$me"
    expect 1 "OKAY_OWN_CRYPTO=1 in the environment"    env OKAY_OWN_CRYPTO=1 bash "$me"
    expect 1 "OKAY_OWN_CRYPTO=ON in the environment"   env OKAY_OWN_CRYPTO=ON bash "$me"
    expect 1 "OKAY_OWN_STACK_CRYPTO_TEST=1"            env OKAY_OWN_STACK_CRYPTO_TEST=1 bash "$me"
    expect 1 "OKAY_OWN_STACK=1"                        env OKAY_OWN_STACK=1 bash "$me"
    expect 1 "OKAY_OWN_SIGNING=1"                      env OKAY_OWN_SIGNING=1 bash "$me"
    expect 1 "-DOKAY_OWN_SIGNING=ON argument"          env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST -u OKAY_OWN_CRYPTO bash "$me" -- -DOKAY_OWN_SIGNING=ON
    expect 0 "-DOKAY_OWN_SIGNING=OFF argument"         env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST -u OKAY_OWN_CRYPTO bash "$me" -- -DOKAY_OWN_SIGNING=OFF
    expect 0 "OKAY_OWN_CRYPTO=0 is off"                env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST OKAY_OWN_CRYPTO=0 bash "$me"
    expect 0 "empty value is off"                      env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST OKAY_OWN_CRYPTO= bash "$me"
    expect 1 "-DOKAY_OWN_CRYPTO=ON argument"           env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST -u OKAY_OWN_CRYPTO bash "$me" -- -DCMAKE_BUILD_TYPE=Release -DOKAY_OWN_CRYPTO=ON
    expect 1 "-DOKAY_OWN_STACK:BOOL=ON argument"       env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST -u OKAY_OWN_CRYPTO bash "$me" -- -DOKAY_OWN_STACK:BOOL=ON
    expect 0 "-DOKAY_OWN_CRYPTO=OFF argument"          env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST -u OKAY_OWN_CRYPTO bash "$me" -- -DOKAY_OWN_CRYPTO=OFF -DOKAY_USE_ASSIMP=ON
    # --- the -D forms, including a space after -D
    expect 1 "-D OKAY_OWN_CRYPTO=ON (space after -D)"  env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST -u OKAY_OWN_CRYPTO bash "$me" -- -D OKAY_OWN_CRYPTO=ON
    expect 1 "-D OKAY_OWN_STACK:BOOL=ON (space)"       env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST -u OKAY_OWN_CRYPTO bash "$me" -- -D OKAY_OWN_STACK:BOOL=ON
    expect 0 "-D OKAY_OWN_CRYPTO=OFF (space) is fine"  env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST -u OKAY_OWN_CRYPTO bash "$me" -- -D OKAY_OWN_CRYPTO=OFF
    expect 1 "-DOKAY_USE_SODIUM=OFF argument"          env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST -u OKAY_OWN_CRYPTO bash "$me" -- -DOKAY_USE_SODIUM=OFF
    expect 1 "-D OKAY_USE_SODIUM=OFF (space)"          env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST -u OKAY_OWN_CRYPTO bash "$me" -- -D OKAY_USE_SODIUM=OFF
    expect 1 "-DOKAY_REQUIRE_SODIUM=OFF argument"      env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST -u OKAY_OWN_CRYPTO bash "$me" -- -DOKAY_REQUIRE_SODIUM=OFF
    expect 0 "-DOKAY_REQUIRE_SODIUM=ON argument"       env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST -u OKAY_OWN_CRYPTO bash "$me" -- -DOKAY_REQUIRE_SODIUM=ON

    # --- configured trees. mk <name> <cache-lines...>: a tree with those cache lines; ninja <name> <text>: a build.ninja
    mk() { local n="$1"; shift; mkdir -p "$d/$n"; : > "$d/$n/CMakeCache.txt"; for l in "$@"; do echo "$l" >> "$d/$n/CMakeCache.txt"; done; }
    nin() { printf 'build engine/CMakeFiles/okay_engine.dir/a.cpp.o: CXX a.cpp\n  DEFINES = %s\nbuild player/CMakeFiles/okay-player.dir/main.cpp.o: CXX m.cpp\n  FLAGS = -O2\n' "$2" > "$1/build.ninja"; }
    local good="OKAY_SODIUM_IN_BUILD:INTERNAL=ON OKAY_REQUIRE_SODIUM:BOOL=ON OKAY_USE_SODIUM:BOOL=ON"
    mk good $good; nin "$d/good" '-DOKAY_HAVE_SODIUM -DOKAY_NET_CRYPTO -DSODIUM_STATIC=1'
    printf 'build tests/CMakeFiles/test_x.dir/x.cpp.o: CXX x.cpp\n  DEFINES = -DOKAY_OWN_CRYPTO\n' >> "$d/good/build.ninja"   # a TEST target may define it
    local cl="env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST -u OKAY_OWN_CRYPTO bash $me"
    expect 0 "a build with libsodium (cache + build.ninja)" $cl --cache "$d/good"
    expect 2 "missing cache is a usage error"          $cl --cache "$d/none"
    mk own1 OKAY_SODIUM_IN_BUILD:INTERNAL=ON OKAY_REQUIRE_SODIUM:BOOL=ON OKAY_OWN_CRYPTO:BOOL=ON;  nin "$d/own1" '-DOKAY_HAVE_SODIUM -DOKAY_OWN_CRYPTO' 
    expect 1 "cache with OKAY_OWN_CRYPTO ON"           $cl --cache "$d/own1"
    mk own2 $good OKAY_OWN_STACK:BOOL=ON;                          cp "$d/good/build.ninja" "$d/own2/"
    expect 1 "cache with OKAY_OWN_STACK ON"            $cl --cache "$d/own2"
    mk own3 $good OKAY_OWN_STACK_CRYPTO_TEST:BOOL=ON;              cp "$d/good/build.ninja" "$d/own3/"
    expect 1 "cache with OKAY_OWN_STACK_CRYPTO_TEST ON" $cl --cache "$d/own3"
    # a build WITHOUT libsodium: the cache says so
    mk nosod OKAY_SODIUM_IN_BUILD:INTERNAL=OFF OKAY_REQUIRE_SODIUM:BOOL=ON OKAY_USE_SODIUM:BOOL=ON; nin "$d/nosod" '-DOKAY_BUILD' 
    expect 1 "build without libsodium (OKAY_SODIUM_IN_BUILD=OFF)" $cl --cache "$d/nosod"
    # a cache that only has names and nothing about the built result (the old guard passed this)
    mk nameonly OKAY_OWN_CRYPTO:BOOL=OFF OKAY_OWN_STACK:BOOL=OFF OKAY_OWN_STACK_CRYPTO_TEST:BOOL=OFF
    expect 1 "cache with no OKAY_SODIUM_IN_BUILD entry"   $cl --cache "$d/nameonly"
    mk nosodopt OKAY_SODIUM_IN_BUILD:INTERNAL=ON OKAY_REQUIRE_SODIUM:BOOL=ON OKAY_USE_SODIUM:BOOL=OFF; cp "$d/good/build.ninja" "$d/nosodopt/"
    expect 1 "OKAY_USE_SODIUM=OFF in the cache"           $cl --cache "$d/nosodopt"
    mk noreq OKAY_SODIUM_IN_BUILD:INTERNAL=ON OKAY_USE_SODIUM:BOOL=ON;  cp "$d/good/build.ninja" "$d/noreq/"
    expect 1 "cache without OKAY_REQUIRE_SODIUM=ON"       $cl --cache "$d/noreq"
    # the toolchain-file bypass: no cache entry for OKAY_OWN_CRYPTO, but it reaches the compiler
    mk tool $good; nin "$d/tool" '-DOKAY_HAVE_SODIUM -DOKAY_OWN_CRYPTO -DOKAY_NET_CRYPTO' 
    expect 1 "set(OKAY_OWN_CRYPTO ON) in a toolchain file (only build.ninja shows it)" $cl --cache "$d/tool"
    mk toolj $good; echo '[{"command": "g++ -DOKAY_HAVE_SODIUM -DOKAY_OWN_STACK_CRYPTO_TEST -o player/CMakeFiles/okay-player.dir/m.o -c a.cpp"}]' > "$d/toolj/compile_commands.json"
    expect 1 "own-stack crypto test define in compile_commands.json" $cl --cache "$d/toolj"
    mk nodef $good; nin "$d/nodef" '-DOKAY_BUILD' 
    expect 1 "build.ninja without OKAY_HAVE_SODIUM although the cache says ON" $cl --cache "$d/nodef"
    mk okname $good; nin "$d/okname" '-DOKAY_HAVE_SODIUM -DOKAY_OWN_CRYPTO_NOTES_X' 
    expect 0 "a longer name that only starts with OKAY_OWN_CRYPTO is not the switch" $cl --cache "$d/okname"

    # --- a built program: --binary
    printf '#!/bin/sh\necho "[okay] stack: platform=sdl2 http=curl readers=ufbx crypto=libsodium ui=none"\n' > "$d/p_ok"
    printf '#!/bin/sh\necho "[okay] stack: platform=sdl2 http=curl readers=ufbx crypto=own(no libsodium) ui=none"\n' > "$d/p_nosod"
    printf '#!/bin/sh\necho "[okay] stack: platform=sdl2 http=curl readers=ufbx crypto=own(test) ui=none"\n' > "$d/p_own"
    printf '#!/bin/sh\necho "[okay] stack: platform=sdl2 http=curl readers=ufbx crypto=libsodium-not ui=none"\n' > "$d/p_fake"
    printf '#!/bin/sh\necho hello\n' > "$d/p_none"
    printf '#!/bin/sh\n[ "$OKAY_PRINT_STACK_AND_EXIT" = 1 ] || exit 3\necho "[okay] stack: crypto=libsodium"\nexit 5\n' > "$d/p_badrc"
    chmod +x "$d"/p_*
    expect 0 "--binary that says crypto=libsodium"        $cl --binary "$d/p_ok"
    expect 1 "--binary that says crypto=own(no libsodium)" $cl --binary "$d/p_nosod"
    expect 1 "--binary that says crypto=own(test)"        $cl --binary "$d/p_own"
    expect 1 "--binary with crypto=libsodium-not"         $cl --binary "$d/p_fake"
    expect 1 "--binary that prints no stack line"         $cl --binary "$d/p_none"
    expect 1 "--binary that exits non-zero"               $cl --binary "$d/p_badrc"
    expect 2 "--binary that is not a file"                $cl --binary "$d/nothing"
    if [ "$fails" -ne 0 ]; then echo "$fails self-test failure(s)" >&2; exit 1; fi
    echo "check_release_own_stack: self-test passed"
}

case "${1:-}" in
    --self-test) self_test; exit $? ;;
esac

rc=0
cache=""
binary=""
args=()
while [ $# -gt 0 ]; do
    case "$1" in
        --cache) cache="${2:-}"; [ -n "$cache" ] || { echo "usage: --cache DIR" >&2; exit 2; }; shift 2 ;;
        --binary) binary="${2:-}"; [ -n "$binary" ] || { echo "usage: --binary PATH" >&2; exit 2; }; shift 2 ;;
        --) shift; args=("$@"); break ;;
        *) echo "usage: $0 [--cache BUILD_DIR] [--binary PROGRAM] [-- cmake -D args...] | --self-test" >&2; exit 2 ;;
    esac
done
check_env || rc=1
[ ${#args[@]} -gt 0 ] && { check_args "${args[@]}" || rc=1; }
if [ -n "$cache" ]; then
    check_cache "$cache/CMakeCache.txt"; c=$?
    [ "$c" -eq 0 ] || rc=$c
fi
if [ -n "$binary" ]; then
    check_binary "$binary"; c=$?
    [ "$c" -eq 0 ] || rc=$c
fi
[ "$rc" -eq 0 ] && echo "check_release_own_stack: no own crypto and no own-stack switch in this release build${cache:+, and libsodium is in it}"
exit $rc
