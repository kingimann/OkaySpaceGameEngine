#!/usr/bin/env bash
# Build and run the encryption-related tests under BOTH crypto backends: libsodium (the default)
# and our own okay::crypto (-DOKAY_OWN_CRYPTO=ON, UNREVIEWED; see docs/crypto.md).
#   scripts/test_crypto_backends.sh [build-root] [jobs]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-$root/build-crypto-backends}"
jobs="${2:-2}"
tests='^(crypto|crypto_.*|net|net_.*|dedicated_server|anticheat.*)$'
for backend in sodium own; do
    dir="$out/$backend"
    flag=OFF; [ "$backend" = own ] && flag=ON
    echo "=== backend: $backend (OKAY_OWN_CRYPTO=$flag) ==="
    cmake -S "$root" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DOKAY_OWN_CRYPTO=$flag \
          -DOKAY_BUILD_SANDBOX=OFF -DOKAY_BUILD_LAUNCHER=OFF > "$dir.configure.log" 2>&1
    cmake --build "$dir" -j"$jobs" > "$dir.build.log" 2>&1
    (cd "$dir" && ctest -R "$tests" --output-on-failure)
done
echo "both backends passed"
