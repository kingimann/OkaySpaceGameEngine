#!/usr/bin/env bash
# Constant-time audit, method 1 (docs/crypto_review_package.md section 4): build the valgrind harness
# tests/crypto_ct_valgrind.cpp with every compiler and optimisation level that is installed, for both
# field implementations (51-bit and the portable 16-bit one), and run it under valgrind. A level fails
# when valgrind reports a branch or address that depends on a secret, or when a positive control
# (deliberately leaky code) is NOT reported.
# Usage: scripts/crypto_ct_matrix.sh [extra compiler flags, e.g. -march=native]
# Optional filters (default: everything): CT_COMPILERS="g++ clang++" CT_OPTS="-O0 -O1 -O2 -O3 -Os -Ofast".
# Needs valgrind and its headers (apt: valgrind). Linux only.
set -u
cd "$(dirname "$0")/.."
command -v valgrind >/dev/null || { echo "valgrind not installed"; exit 2; }
out=$(mktemp -d)
srcs=""
for f in Util Random ChaCha20 Poly1305 Aead Blake2b Sha256 Sha512 X25519 Ed25519; do srcs="$srcs engine/src/Crypto/$f.cpp"; done
bad=0
for cxx in ${CT_COMPILERS:-g++ clang++}; do
  command -v "$cxx" >/dev/null || continue
  for opt in ${CT_OPTS:--O0 -O1 -O2 -O3 -Os -Ofast}; do
    for fe in "" "-DOKAY_CRYPTO_FE16"; do
      if ! "$cxx" -std=c++17 -g $opt $fe "$@" -DOKAY_CRYPTO_CTGRIND -Iengine/include \
            tests/crypto_ct_valgrind.cpp $srcs -o "$out/t" 2>"$out/err"; then
        echo "$cxx $opt ${fe:-fe64}: BUILD FAILED"; head -3 "$out/err"; bad=1; continue
      fi
      res=$(valgrind -q "$out/t" 2>/dev/null | grep -E "FAIL|all cases" | tr '\n' ' ')
      echo "$cxx $opt ${fe:-fe64}: $res"
      case "$res" in *"all cases as expected"*) ;; *) bad=1 ;; esac
    done
  done
done
rm -rf "$out"
exit $bad
