#!/usr/bin/env bash
# Run the okay::crypto fuzz drivers (tests/fuzz_crypto.cpp, tests/fuzz_net_crypto.cpp) under AddressSanitizer and
# UndefinedBehaviorSanitizer with the built-in mutating driver (no libFuzzer needed; GCC or Clang with the
# sanitizer runtimes). The library sources are compiled WITH the sanitizers too (no prebuilt library).
# Usage: scripts/crypto_fuzz_sanitized.sh [iterations=200000] [seed=1] [compiler=g++]
# Fails (non-zero) on any sanitizer report or violated property. This is a deterministic mutation fuzz: it is
# weaker than coverage-guided libFuzzer (-DOKAY_CRYPTO_LIBFUZZER=ON with clang), which is the better tool.
set -eu
cd "$(dirname "$0")/.."
iters=${1:-200000}; seed=${2:-1}; cxx=${3:-g++}
out=$(mktemp -d)
srcs=""
for f in Util Random ChaCha20 Poly1305 Aead Blake2b Sha256 Sha512 X25519 Ed25519; do srcs="$srcs engine/src/Crypto/$f.cpp"; done
san="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
common="-std=c++17 -O1 -g $san -Iengine/include"
"$cxx" $common tests/fuzz_crypto.cpp $srcs -o "$out/fuzz_crypto"
"$cxx" $common -DOKAY_OWN_CRYPTO -DOKAY_NET_CRYPTO tests/fuzz_net_crypto.cpp engine/src/Net/SessionTicket.cpp $srcs -o "$out/fuzz_net_crypto"
"$out/fuzz_crypto" "$iters" "$seed"
# the net target does a key exchange per iteration, so it gets a tenth of the count
"$out/fuzz_net_crypto" "$((iters / 10))" "$seed"
rm -rf "$out"
