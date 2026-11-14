#!/usr/bin/env bash
# Build the self-contained archive that is handed to an outside cryptographic reviewer
# (docs/crypto_review_request.md). It holds exactly the files named in the scope tables of
# docs/crypto_review_package.md, the tests and scripts that carry the evidence, the docs, a MANIFEST with
# the SHA-256 of every file and the git commit it came from, and a README with a 15 minute path.
#
#   scripts/make_crypto_review_bundle.sh <output.tar.gz>
#
# The output path must be OUTSIDE the repository (the archive is never committed). The script reads the
# committed files (git show HEAD:path), so what goes in is exactly the commit named in the MANIFEST, even
# if the working tree has local edits. It changes nothing in the repository, builds nothing, and contains
# no secrets: only tracked source, tests, docs and the unmodified third-party Wycheproof vectors.
# The archive is reproducible: the same commit gives the same bytes (sorted names, fixed owner and times).
set -euo pipefail

if [ $# -ne 1 ]; then
    echo "usage: $0 <output.tar.gz>   (outside the repository)" >&2
    exit 2
fi
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
out_dir="$(cd "$(dirname "$1")" && pwd)"
out="$out_dir/$(basename "$1")"
case "$out" in
    "$root"/*) echo "refusing: output $out is inside the repository; give a scratch path" >&2; exit 2 ;;
esac
case "$out" in *.tar.gz) ;; *) echo "output name must end in .tar.gz" >&2; exit 2 ;; esac

commit="$(git rev-parse HEAD)"
commit_time="$(git show -s --format=%cI HEAD)"
commit_date="$(git show -s --format=%cs HEAD)"

# ---------------------------------------------------------------------------------------------
# The file list. Keep it in step with section 1 of docs/crypto_review_package.md.
# ---------------------------------------------------------------------------------------------
files=(
    # okay::crypto: the files under review (section 1, first table)
    engine/include/okay/Crypto/Crypto.hpp
    engine/src/Crypto/Aead.cpp
    engine/src/Crypto/ChaCha20.cpp
    engine/src/Crypto/Poly1305.cpp
    engine/src/Crypto/Blake2b.cpp
    engine/src/Crypto/Sha256.cpp
    engine/src/Crypto/Sha512.cpp
    engine/src/Crypto/Fe25519.hpp
    engine/src/Crypto/Fe25519_64.hpp
    engine/src/Crypto/Fe25519_16.hpp
    engine/src/Crypto/Fe25519Chain.inc
    engine/src/Crypto/Scalar25519.hpp
    engine/src/Crypto/CtAnnotate.hpp
    engine/src/Crypto/X25519.cpp
    engine/src/Crypto/Ed25519.cpp
    engine/src/Crypto/Random.cpp
    engine/src/Crypto/Util.cpp
    engine/include/okay/Net/NetCrypto.hpp
    # where it is used (section 1, "Where it is used"); SecureChannel.hpp and SessionTicket build standalone
    engine/include/okay/Net/SecureChannel.hpp
    engine/include/okay/Net/SessionTicket.hpp
    engine/src/Net/SessionTicket.cpp
    # read-only context for the protocol questions; these do not build outside the full repository
    engine/include/okay/Net/NetworkManager.hpp
    engine/src/Net/NetworkManager.cpp
    engine/src/Net/RelayServer.cpp
    # the other crypto code in the tree (section 1, second table); read-only, not built here
    # (shared/AntiCheatCrypto.hpp is a thin wrapper over okay::crypto now; the TweetNaCl-derived port is deleted)
    shared/AntiCheatCrypto.hpp
    shared/AntiCheatChecks.hpp
    engine/include/okay/Platform/Account/AccountService.hpp
    shared/OfficialGame.hpp
    # tests, harnesses and fuzzers
    tests/test_framework.hpp
    tests/crypto_rfc_vectors.hpp
    tests/test_crypto.cpp
    tests/test_crypto_field.cpp
    tests/test_crypto_wycheproof.cpp
    tests/test_crypto_sodium_xcheck.cpp
    tests/crypto_difftest.cpp
    tests/crypto_ct_valgrind.cpp
    tests/crypto_timing.cpp
    tests/crypto_xcheck_tool.cpp
    tests/scripts/crypto_xcheck.py
    tests/fuzz_crypto.cpp
    tests/fuzz_net_crypto.cpp
    tests/test_net_crypto.cpp
    tests/test_net_crypto_own.cpp
    tests/test_net_crypto_sodium.cpp
    tests/bench_crypto.cpp
    tests/test_anticheat_integrity.cpp
    tests/test_anticheat_golden.cpp
    # scripts
    scripts/crypto_ct_matrix.sh
    scripts/crypto_fuzz_sanitized.sh
    scripts/test_crypto_backends.sh
    scripts/make_crypto_review_bundle.sh
    # docs
    docs/crypto_review_package.md
    docs/crypto_review_request.md
    docs/crypto.md
    docs/own_code_roadmap.md
    docs/anticheat.md
)
# The third-party Wycheproof vector files (Apache-2.0, unmodified; data, not code under review).
while IFS= read -r f; do files+=("$f"); done < <(git ls-tree -r --name-only HEAD tests/wycheproof | LC_ALL=C sort)

for f in "${files[@]}"; do
    git cat-file -e "HEAD:$f" 2>/dev/null || { echo "missing from commit $commit: $f" >&2; exit 1; }
done

work="$(mktemp -d "$out_dir/.crypto-bundle.XXXXXX")"
trap 'rm -rf "$work"' EXIT
name="okay-crypto-review-${commit:0:12}"
stage="$work/$name"
mkdir -p "$stage"

for f in "${files[@]}"; do
    mkdir -p "$stage/$(dirname "$f")"
    git show "HEAD:$f" > "$stage/$f"
    if git ls-tree HEAD "$f" | grep -q '^100755'; then chmod 755 "$stage/$f"; fi
done

# ---------------------------------------------------------------------------------------------
# reviewer/run_evidence.sh: builds and runs the evidence with a plain compiler, no CMake needed.
# ---------------------------------------------------------------------------------------------
mkdir -p "$stage/reviewer"
cat > "$stage/reviewer/run_evidence.sh" <<'RUNNER'
#!/usr/bin/env bash
# Build and run the okay::crypto evidence from this extracted bundle with a plain C++17 compiler.
#   reviewer/run_evidence.sh            the quick set (a few minutes): known answers, Wycheproof, field and scalar
#                                       arithmetic, portable 16-bit field, libsodium and Python cross-checks,
#                                       differential smoke test, fuzz smoke tests, the net layer on our backend
#   reviewer/run_evidence.sh --ct       also the valgrind constant-time matrix (about 2 minutes per compiler)
#   reviewer/run_evidence.sh --fuzz     also both fuzz drivers under ASan and UBSan (about 1.5 minutes)
#   reviewer/run_evidence.sh --diff     also the large differential test, N = 1,000,000 (about 15 minutes, one core)
#   reviewer/run_evidence.sh --timing   also the statistical timing test (x86-64 only, about 7 minutes; best on a quiet machine)
# Needs: g++ or clang++ (CXX=...), a Linux shell. Optional: libsodium-dev and pkg-config (libsodium cross-checks),
# python3 with the `cryptography` module, valgrind (--ct). Missing optional parts are reported as SKIPPED, not hidden.
set -u
cd "$(dirname "$0")/.."
ROOT="$PWD"
CXX="${CXX:-g++}"
OUT="${OUT:-$ROOT/build-evidence}"
mkdir -p "$OUT"
want_ct=0; want_fuzz=0; want_diff=0; want_timing=0
for a in "$@"; do
    case "$a" in
        --ct) want_ct=1 ;; --fuzz) want_fuzz=1 ;; --diff) want_diff=1 ;; --timing) want_timing=1 ;;
        *) echo "unknown option $a"; exit 2 ;;
    esac
done
command -v "$CXX" >/dev/null || { echo "compiler $CXX not found"; exit 2; }

CORE=()
for f in Util Random ChaCha20 Poly1305 Aead Blake2b Sha256 Sha512 X25519 Ed25519; do CORE+=("engine/src/Crypto/$f.cpp"); done
BASE=(-std=c++17 -O2 -Iengine/include)
have_sodium=0
if command -v pkg-config >/dev/null && pkg-config --exists libsodium; then have_sodium=1; SODIUM_CF=($(pkg-config --cflags libsodium)); SODIUM_LD=($(pkg-config --libs libsodium)); fi
have_py=0; PY=""
for py in python3 /usr/bin/python3 python; do
    if "$py" -c "from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305; from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey" >/dev/null 2>&1; then have_py=1; PY="$py"; break; fi
done

results=()
note() { results+=("$1"); echo "$1"; }
# build NAME flags... -- sources...   (builds $OUT/NAME)
build() {
    local name="$1"; shift
    if "$CXX" "$@" -o "$OUT/$name" 2>"$OUT/$name.build.log"; then return 0; fi
    note "FAIL     $name (build failed, see $OUT/$name.build.log)"; return 1
}
run() {   # run NAME args...
    local name="$1"; shift
    local t0=$SECONDS
    if "$OUT/$name" "$@" >"$OUT/$name.log" 2>&1; then
        note "PASS     $name ($((SECONDS - t0)) s) $(tail -n1 "$OUT/$name.log")"
    else
        note "FAIL     $name (see $OUT/$name.log)"; tail -n5 "$OUT/$name.log"
    fi
}

echo "== bundle commit: $(sed -n 's/^commit: //p' MANIFEST.txt | head -1)   compiler: $("$CXX" --version | head -1)"

build test_crypto "${BASE[@]}" tests/test_crypto.cpp "${CORE[@]}" && run test_crypto
build test_crypto_fe16 "${BASE[@]}" -DOKAY_CRYPTO_FE16 tests/test_crypto.cpp "${CORE[@]}" && run test_crypto_fe16
build test_crypto_field "${BASE[@]}" tests/test_crypto_field.cpp && run test_crypto_field

WY=(-DOKAY_WYCHEPROOF_DEFAULT_DIR="\"$ROOT/tests/wycheproof\"")
if [ $have_sodium = 1 ]; then
    build test_crypto_wycheproof "${BASE[@]}" "${WY[@]}" -DOKAY_WY_SODIUM "${SODIUM_CF[@]}" tests/test_crypto_wycheproof.cpp "${CORE[@]}" "${SODIUM_LD[@]}" && run test_crypto_wycheproof
    build test_crypto_sodium_xcheck "${BASE[@]}" "${SODIUM_CF[@]}" tests/test_crypto_sodium_xcheck.cpp "${CORE[@]}" "${SODIUM_LD[@]}" && run test_crypto_sodium_xcheck
    build crypto_difftest "${BASE[@]}" "${SODIUM_CF[@]}" tests/crypto_difftest.cpp "${CORE[@]}" "${SODIUM_LD[@]}" && run crypto_difftest 1500
    build test_net_crypto_sodium "${BASE[@]}" -Itests "${SODIUM_CF[@]}" tests/test_net_crypto_sodium.cpp "${CORE[@]}" "${SODIUM_LD[@]}" && run test_net_crypto_sodium
else
    build test_crypto_wycheproof "${BASE[@]}" "${WY[@]}" tests/test_crypto_wycheproof.cpp "${CORE[@]}" && run test_crypto_wycheproof
    note "SKIPPED  libsodium cross-checks and the differential test (libsodium-dev / pkg-config not found)"
fi
build test_net_crypto_own "${BASE[@]}" -Itests tests/test_net_crypto_own.cpp "${CORE[@]}" && run test_net_crypto_own
# The anti-cheat Ed25519 and SHA-512 wrapper (shared/AntiCheatCrypto.hpp) against a transcript recorded from the old TweetNaCl-derived port
build test_anticheat_golden "${BASE[@]}" -Itests -Ishared tests/test_anticheat_golden.cpp "${CORE[@]}" && run test_anticheat_golden
build fuzz_crypto_smoke "${BASE[@]}" tests/fuzz_crypto.cpp "${CORE[@]}" && run fuzz_crypto_smoke 300
build fuzz_net_crypto_smoke "${BASE[@]}" -DOKAY_OWN_CRYPTO -DOKAY_NET_CRYPTO tests/fuzz_net_crypto.cpp engine/src/Net/SessionTicket.cpp "${CORE[@]}" && run fuzz_net_crypto_smoke 300
if [ $have_py = 1 ]; then
    build crypto_xcheck_tool "${BASE[@]}" tests/crypto_xcheck_tool.cpp "${CORE[@]}" && {
        t0=$SECONDS
        if "$PY" tests/scripts/crypto_xcheck.py "$OUT/crypto_xcheck_tool" 1500 >"$OUT/crypto_python_xcheck.log" 2>&1; then
            note "PASS     crypto_python_xcheck ($((SECONDS - t0)) s) $(tail -n1 "$OUT/crypto_python_xcheck.log")"
        else note "FAIL     crypto_python_xcheck (see $OUT/crypto_python_xcheck.log)"; fi
    }
else
    note "SKIPPED  crypto_python_xcheck (python3 module 'cryptography' not found)"
fi

if [ $want_ct = 1 ]; then
    if command -v valgrind >/dev/null && [ -e /usr/include/valgrind/memcheck.h -o -e /usr/local/include/valgrind/memcheck.h ]; then
        if scripts/crypto_ct_matrix.sh >"$OUT/ct_matrix.log" 2>&1; then note "PASS     valgrind constant-time matrix ($(grep -c 'all cases as expected' "$OUT/ct_matrix.log") builds clean)"
        else note "FAIL     valgrind constant-time matrix (see $OUT/ct_matrix.log)"; fi
    else note "SKIPPED  valgrind constant-time matrix (valgrind and its headers not installed)"; fi
fi
if [ $want_fuzz = 1 ]; then
    if scripts/crypto_fuzz_sanitized.sh 200000 1 "$CXX" >"$OUT/fuzz_sanitized.log" 2>&1; then note "PASS     fuzz under ASan+UBSan ($(tail -n2 "$OUT/fuzz_sanitized.log" | tr '\n' ' '))"
    else note "FAIL     fuzz under ASan+UBSan (see $OUT/fuzz_sanitized.log)"; fi
fi
if [ $want_diff = 1 ]; then
    if [ $have_sodium = 1 ]; then OKAY_DIFF_N=1000000 "$OUT/crypto_difftest" >"$OUT/crypto_difftest_full.log" 2>&1 && note "PASS     crypto_difftest N=1,000,000 ($(tail -n1 "$OUT/crypto_difftest_full.log"))" || note "FAIL     crypto_difftest N=1,000,000 (see $OUT/crypto_difftest_full.log)"
    else note "SKIPPED  full differential test (needs libsodium)"; fi
fi
if [ $want_timing = 1 ]; then
    if build crypto_timing "${BASE[@]}" tests/crypto_timing.cpp "${CORE[@]}"; then
        if command -v taskset >/dev/null; then taskset -c 0 "$OUT/crypto_timing" 300000 >"$OUT/crypto_timing.log" 2>&1; else "$OUT/crypto_timing" 300000 >"$OUT/crypto_timing.log" 2>&1; fi
        note "DONE     crypto_timing (read $OUT/crypto_timing.log: rows with |t| above 4.5 need a second look; the positive controls MUST be flagged)"
    fi
fi

echo; echo "== summary"; printf '%s\n' "${results[@]}"
if printf '%s\n' "${results[@]}" | grep -q '^FAIL'; then echo "SOME CHECKS FAILED"; exit 1; fi
echo "all checks that ran passed (a pass is evidence for the reviewer to check, not a finding of security)"
RUNNER
chmod 755 "$stage/reviewer/run_evidence.sh"

# ---------------------------------------------------------------------------------------------
# README for the reviewer
# ---------------------------------------------------------------------------------------------
cat > "$stage/README.md" <<README
# okay::crypto: reviewer bundle

Source commit: \`$commit\` ($commit_date). Every file's SHA-256 is in \`MANIFEST.sha256\`; the commit and the file list are in \`MANIFEST.txt\`.

**Status: UNREVIEWED. Nothing in this bundle says the code is secure.** It is what the team that wrote it checked and how, so that you can check it. Start with \`docs/crypto_review_request.md\` (what we ask, what we do not), then \`docs/crypto_review_package.md\` (scope, threat model, evidence, known issues, questions).

## Verify the bundle (1 minute)

\`\`\`
tar xzf okay-crypto-review-*.tar.gz && cd okay-crypto-review-*
sha256sum -c MANIFEST.sha256 --quiet && echo "all files match"
\`\`\`

## Build and run the evidence in 15 minutes (Linux)

Needs only a C++17 compiler (\`g++\` or \`clang++\`); no CMake. Optional: \`libsodium-dev\` and \`pkg-config\` (cross-checks against libsodium), \`python3\` with the \`cryptography\` module, \`valgrind\`.

\`\`\`
reviewer/run_evidence.sh            # about 3 to 5 minutes on a 4-core machine
reviewer/run_evidence.sh --ct       # + valgrind constant-time audit matrix (about 3 minutes per compiler installed)
reviewer/run_evidence.sh --fuzz     # + both fuzz drivers under ASan and UBSan (about 1.5 minutes)
reviewer/run_evidence.sh --diff     # + the large differential test, 1,000,000 cases (about 15 minutes, one core)
reviewer/run_evidence.sh --timing   # + the statistical timing test (x86-64; about 7 minutes; quiet machine)
CXX=clang++ reviewer/run_evidence.sh
\`\`\`

The script prints PASS, FAIL or SKIPPED for each check and keeps full logs in \`build-evidence/\`. Skipped means a tool was missing, not that the check passed.

## What is in the bundle

| Directory | Contents |
|---|---|
| \`engine/include/okay/Crypto/\`, \`engine/src/Crypto/\` | The library under review (\`docs/crypto_review_package.md\` section 1, first table) |
| \`engine/include/okay/Net/\`, \`engine/src/Net/\` | The seam (\`NetCrypto.hpp\`), \`SecureChannel\`, \`SessionTicket\` (these build standalone), and \`NetworkManager\` and \`RelayServer\` as read-only context for the protocol questions |
| \`shared/\`, \`engine/include/okay/Platform/Account/\` | The other crypto code in scope: the thin anti-cheat wrapper \`AntiCheatCrypto.hpp\` (the TweetNaCl-derived port is deleted), the hand-written HMAC, the extra SHA-256 copies. Read-only here; they need the rest of the engine to build |
| \`tests/\` | Known-answer, field, Wycheproof, differential, cross-check, constant-time and fuzz harnesses, and \`tests/wycheproof/\` (third-party vectors, Apache-2.0, unmodified, data not code under review). \`test_anticheat_integrity.cpp\` is included to read; it needs the engine to build. \`test_anticheat_golden.cpp\` builds standalone and checks the anti-cheat wrapper against a recorded transcript |
| \`scripts/\` | The constant-time matrix and sanitizer fuzz scripts (run from this root), the backend test script (needs the full repository), and the script that made this bundle |
| \`docs/\` | The review package and request, \`crypto.md\`, the roadmap and the anti-cheat guide |
| \`reviewer/run_evidence.sh\` | Builds and runs the evidence with a plain compiler |

The full repository also has a CMake build and ctest names (\`ctest -R '^crypto'\`); this bundle has no CMake files on purpose, so that nothing needs to be installed to start. The wire format and the call sites in the net layer are the same code as in the repository at the commit above.

## A one-hour reading order

Section 0 of \`docs/crypto_review_package.md\` gives it minute by minute. In short: threat model, then \`Fe25519_64.hpp\` and \`Scalar25519.hpp\`, then \`Ed25519.cpp\` (\`BaseMult\`, \`SelectBase\`, \`Ed25519Verify\`, \`DoubleScalarMultPublic\`), then \`X25519.cpp\`, \`Aead.cpp\`, \`ChaCha20.cpp\`.

## Please do not publish findings before we have had a chance to fix them

Report privately (the contact in the request that came with this bundle).
README

# ---------------------------------------------------------------------------------------------
# MANIFEST: commit, file list and SHA-256 of everything (MANIFEST.sha256 is the checkable form)
# ---------------------------------------------------------------------------------------------
( cd "$stage" && find . -type f ! -name 'MANIFEST*' | LC_ALL=C sort | sed 's|^\./||' | xargs sha256sum > MANIFEST.sha256 )
{
    echo "okay::crypto reviewer bundle"
    echo "commit: $commit"
    echo "commit date: $commit_time"
    echo "files: $(wc -l < "$stage/MANIFEST.sha256")"
    echo "check: sha256sum -c MANIFEST.sha256 (MANIFEST.sha256 itself and this file are not listed)"
    echo
    cat "$stage/MANIFEST.sha256"
} > "$stage/MANIFEST.txt"

# ---------------------------------------------------------------------------------------------
# Reproducible tarball
# ---------------------------------------------------------------------------------------------
( cd "$work" && tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="$commit_time" -cf - "$name" | gzip -n -9 > "$out" )
echo "wrote $out"
echo "commit $commit, $(wc -l < "$stage/MANIFEST.sha256") files, $(du -h "$out" | cut -f1)"
echo "archive sha256: $(sha256sum "$out" | cut -d' ' -f1)"
