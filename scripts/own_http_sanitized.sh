#!/usr/bin/env bash
# Build and run the own-HTTP-client tests (tests/test_own_http.cpp, tests/test_own_http_net.cpp) standalone under
# AddressSanitizer and UndefinedBehaviorSanitizer; the client is header-only, so nothing else is compiled.
# Usage: scripts/own_http_sanitized.sh [fuzz iterations=200000] [compiler=g++]
# Needs the openssl program and libssl for the TLS half (those cases print SKIP otherwise).
set -eu
cd "$(dirname "$0")/.."
iters=${1:-200000}; cxx=${2:-g++}
out=$(mktemp -d)
san="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
common="-std=c++17 -O1 -g $san -Wall -Wextra -Iengine/include"
"$cxx" $common tests/test_own_http.cpp -o "$out/test_own_http"
"$cxx" $common tests/test_own_http_net.cpp -o "$out/test_own_http_net" -ldl -lpthread
"$cxx" $common -DOKAY_OWN_HTTP=1 -Ilauncher tests/test_native_fetch.cpp -o "$out/test_native_fetch" -ldl -lpthread
"$out/test_own_http" "$iters"
# the sanitizer's leak check would flag the SSL contexts kept for the life of the process (by design)
ASAN_OPTIONS=detect_leaks=0 "$out/test_own_http_net" tests/scripts/gen_http_test_certs.sh .
ASAN_OPTIONS=detect_leaks=0 "$out/test_native_fetch"
rm -rf "$out"
