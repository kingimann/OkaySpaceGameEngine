#!/usr/bin/env bash
# Refuse to build or ship a RELEASE with our own unreviewed crypto or the own-stack dogfood switch on.
# (CLAUDE.md rule 18: cryptography cannot be replaced on our word; docs/own_stack_rollout.md.)
#
#   scripts/check_release_own_stack.sh                 check the environment and the cmake arguments after "--"
#   scripts/check_release_own_stack.sh --cache DIR     also read DIR/CMakeCache.txt (a configured build tree)
#   scripts/check_release_own_stack.sh --self-test     test this script (used by ctest: release_refuses_own_crypto)
#
# Refused: the environment variables OKAY_OWN_CRYPTO, OKAY_OWN_STACK_CRYPTO_TEST and OKAY_OWN_STACK set to anything
# that is not empty, 0, OFF or NO; and the same names set ON/1/TRUE/YES in a cmake cache or on a -D argument.
# Exit 0 = fine, 1 = refused (the message names what), 2 = bad use.
set -u

NAMES="OKAY_OWN_CRYPTO OKAY_OWN_STACK_CRYPTO_TEST OKAY_OWN_STACK"

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
    return $bad
}

check_args() {   # cmake -D arguments
    local bad=0 a n v
    for a in "$@"; do
        case "$a" in
            -D*)
                a="${a#-D}"; n="${a%%[:=]*}"; v="${a#*=}"
                case " $NAMES " in *" $n "*) if is_on "$v"; then echo "refused: -D$n=$v on the command line; a release must not use it" >&2; bad=1; fi ;; esac ;;
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
    expect 0 "OKAY_OWN_CRYPTO=0 is off"                env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST OKAY_OWN_CRYPTO=0 bash "$me"
    expect 0 "empty value is off"                      env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST OKAY_OWN_CRYPTO= bash "$me"
    expect 1 "-DOKAY_OWN_CRYPTO=ON argument"           env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST -u OKAY_OWN_CRYPTO bash "$me" -- -DCMAKE_BUILD_TYPE=Release -DOKAY_OWN_CRYPTO=ON
    expect 1 "-DOKAY_OWN_STACK:BOOL=ON argument"       env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST -u OKAY_OWN_CRYPTO bash "$me" -- -DOKAY_OWN_STACK:BOOL=ON
    expect 0 "-DOKAY_OWN_CRYPTO=OFF argument"          env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST -u OKAY_OWN_CRYPTO bash "$me" -- -DOKAY_OWN_CRYPTO=OFF -DOKAY_USE_ASSIMP=ON
    printf 'OKAY_OWN_CRYPTO:BOOL=OFF\nOKAY_OWN_STACK:BOOL=OFF\nOKAY_OWN_STACK_CRYPTO_TEST:BOOL=OFF\n' > "$d/off.txt"
    printf 'OKAY_USE_SODIUM:BOOL=ON\nOKAY_OWN_CRYPTO:BOOL=ON\n' > "$d/crypto.txt"
    printf 'OKAY_OWN_STACK:BOOL=ON\n' > "$d/stack.txt"
    printf 'OKAY_OWN_STACK_CRYPTO_TEST:BOOL=ON\n' > "$d/test.txt"
    mkdir "$d/a" "$d/b" "$d/c" "$d/e"; cp "$d/off.txt" "$d/a/CMakeCache.txt"; cp "$d/crypto.txt" "$d/b/CMakeCache.txt"
    cp "$d/stack.txt" "$d/c/CMakeCache.txt"; cp "$d/test.txt" "$d/e/CMakeCache.txt"
    local cl="env -u OKAY_OWN_STACK -u OKAY_OWN_STACK_CRYPTO_TEST -u OKAY_OWN_CRYPTO bash $me"
    expect 0 "cache with everything off"               $cl --cache "$d/a"
    expect 1 "cache with OKAY_OWN_CRYPTO ON"           $cl --cache "$d/b"
    expect 1 "cache with OKAY_OWN_STACK ON"            $cl --cache "$d/c"
    expect 1 "cache with OKAY_OWN_STACK_CRYPTO_TEST ON" $cl --cache "$d/e"
    expect 2 "missing cache is a usage error"          $cl --cache "$d/none"
    if [ "$fails" -ne 0 ]; then echo "$fails self-test failure(s)" >&2; exit 1; fi
    echo "check_release_own_stack: self-test passed"
}

case "${1:-}" in
    --self-test) self_test; exit $? ;;
esac

rc=0
cache=""
args=()
while [ $# -gt 0 ]; do
    case "$1" in
        --cache) cache="${2:-}"; [ -n "$cache" ] || { echo "usage: --cache DIR" >&2; exit 2; }; shift 2 ;;
        --) shift; args=("$@"); break ;;
        *) echo "usage: $0 [--cache BUILD_DIR] [-- cmake -D args...] | --self-test" >&2; exit 2 ;;
    esac
done
check_env || rc=1
[ ${#args[@]} -gt 0 ] && { check_args "${args[@]}" || rc=1; }
if [ -n "$cache" ]; then
    check_cache "$cache/CMakeCache.txt"; c=$?
    [ "$c" -eq 0 ] || rc=$c
fi
[ "$rc" -eq 0 ] && echo "check_release_own_stack: no own crypto and no own-stack switch in this release build"
exit $rc
