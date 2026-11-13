# Our own crypto (okay::crypto): stage 1, side by side, off by default

> **UNREVIEWED. NOT THE DEFAULT.** This code was written from the RFCs by the engine team. No outside
> cryptographer has reviewed it. It must **not** become the default, and libsodium must **not** be removed,
> until an outside expert has reviewed it (see `docs/own_code_roadmap.md`, libsodium row). Do not ship a build
> with `OKAY_OWN_CRYPTO=ON` to players.

## What it is

`okay::crypto` (`engine/include/okay/Crypto/Crypto.hpp`, one `.cpp` per primitive in `engine/src/Crypto/`) holds
exactly the primitives the network layer uses today, plus their direct building blocks:

| Primitive | Spec | Used by |
|---|---|---|
| XChaCha20-Poly1305 (IETF) with ChaCha20, HChaCha20, Poly1305 (also plain ChaCha20-Poly1305) | RFC 8439, draft-irtf-cfrg-xchacha | `SecureChannel::Seal/Open` |
| X25519 | RFC 7748 | key pairs |
| Kx session keys (X25519, then BLAKE2b-512 of `q || client_pk || server_pk`, split into rx and tx) | same construction as libsodium's `crypto_kx` | `SecureChannel::Establish` |
| BLAKE2b | RFC 7693 | Kx |
| Ed25519 (detached, strict like libsodium: S < L, no small-order R or key, canonical key) | RFC 8032 | session tickets, host identity (`shared/AntiCheatCrypto.hpp`) |
| SHA-256, SHA-512 | FIPS 180-4 | ticket key binding, Ed25519 |
| `RandomBytes` (getrandom, getentropy, BCryptGenRandom; aborts if the OS has none), `MemZero`, `ConstantTimeEqual` | | everywhere |

No custom random generator. No assembly. Field arithmetic mod 2^255-19 comes in two interchangeable versions with the same
interface (`engine/src/Crypto/Fe25519.hpp` picks one): five 51-bit limbs with a 128-bit product (`Fe25519_64.hpp`, used when the
compiler has `__int128`: GCC and Clang on 64-bit) and the original sixteen 16-bit limbs in `int64_t` (`Fe25519_16.hpp`, the
portable fallback for MSVC, WebAssembly and 32-bit; `-DOKAY_CRYPTO_FE16` forces it, and a ctest runs the whole primitive test
on it so it cannot rot). Scalars modulo the group order use a Barrett reduction on 32-bit limbs (`Scalar25519.hpp`); Ed25519
signing and key generation use a precomputed table of base-point multiples read with a constant-time lookup (all eight entries
of a window are read, masks pick one); verification works on public data only and may branch. Constant time by construction:
fixed loops, masks, no secret-dependent branch or table index, fixed-length ladder for X25519. Two measurements back the
design (valgrind, and a statistical timing test; see the review package, section 4), and they found something: clang compiled
two mask selects into branches, now guarded by a value barrier (`ct::Barrier`). That is still evidence from the compilers,
flags and CPU we ran, not a guarantee.

`okay::crypto` is its own static library (`okay_crypto`, `okay::crypto` in CMake), so code that must not link the engine can use it:
`shared/AntiCheatCrypto.hpp` (Ed25519, SHA-512, key vetting) goes to it under `-DOKAY_OWN_CRYPTO`, and the crypto tests, fuzz
targets and benchmark link only it.

## Switching it on

```
cmake -DOKAY_OWN_CRYPTO=ON ...     # the net layer, session tickets and the host identity use okay::crypto
```

Default is `OFF` and the engine then uses libsodium exactly as before. With `ON`, libsodium is not linked at all
(`OKAY_HAVE_SODIUM` is not defined); the primitives themselves are compiled and tested in every build. The seam is
`engine/include/okay/Net/NetCrypto.hpp`; `SecureChannel`, `SessionTicket` and the relay call only that. The wire
format is identical, so a client built one way talks to a server built the other (tested: both directions).

## How it is tested

| Test (ctest name) | What it proves |
|---|---|
| `crypto` | every published vector: RFC 8439 (A.1 to A.5 and 2.x), the XChaCha draft (HChaCha20 and the AEAD vector), RFC 7748 (5.2, 6.1, iterated 1 and 1000; 1,000,000 with `OKAY_CRYPTO_SLOW=1`), RFC 8032 section 7.1 (all five), RFC 7693 (`abc` and the appendix E self-test); streaming equals one-shot at every split; every bit of the tag and ciphertext, the AAD, the nonce and the key flipped must fail; Ed25519 malleability, small-order and non-canonical cases |
| `crypto_sodium_xcheck` | against the system libsodium, thousands of random cases per primitive, both directions (ours seals and theirs opens, theirs seals and ours opens), including accept/reject agreement on malformed Ed25519 inputs and small-order X25519 points. `OKAY_CRYPTO_XCHECK_N` sets the count |
| `crypto_python_xcheck` | the same against Python `cryptography` (OpenSSL) and `hashlib`; skipped when that module is missing |
| `net_crypto_own`, `net_crypto_sodium` | the existing `test_net_crypto.cpp` compiled once per backend in the same build |
| `crypto_fuzz_smoke`, `crypto_fuzz_net_smoke` | the fuzz targets (`tests/fuzz_crypto.cpp`: the primitives; `tests/fuzz_net_crypto.cpp`: key exchange with any peer key, sealed-message open and the replay window, session tickets, mutated valid signatures and AEAD messages) for a few hundred iterations; `-DOKAY_CRYPTO_LIBFUZZER=ON` builds the real libFuzzer binaries (clang). `scripts/crypto_fuzz_sanitized.sh` runs both under ASan and UBSan with the built-in mutator |
| `crypto_field` | the 51-bit and the 16-bit field against each other (edge values, 3000 random pairs, 200 long random operation chains), the inversion and square-root chains against bit-by-bit powers, the Barrett scalar reduction and multiply-add against a bit-by-bit reference |
| `crypto_fe16` | `tests/test_crypto.cpp` again, compiled with the portable field forced on |
| `crypto_ct_valgrind` | the constant-time audit, method 1: every secret marked undefined memory under valgrind, two deliberately leaky controls that must be reported (`scripts/crypto_ct_matrix.sh` runs it for every installed compiler, -O0 to -Ofast, both fields) |
| `crypto_timing_smoke` | the constant-time audit, method 2 (`tests/crypto_timing.cpp`) is a statistical timing test that needs a quiet machine and millions of measurements; the ctest only checks that it runs |
| `anticheat_integrity_ownlib`, `anticheat_integrity_portable` | `tests/test_anticheat_integrity.cpp` compiled without the engine: once against `okay::crypto` alone (`-DOKAY_OWN_CRYPTO`) and once with neither it nor libsodium (the TweetNaCl-derived port); the RFC 8032 vectors, S < L, small-order R and key, canonical key |
| `bench_crypto` (not a test) | microseconds per operation next to libsodium when it is installed |
| the whole `net*` family, `anticheat*`, `dedicated_server` | run under whichever backend the build was configured with; `scripts/test_crypto_backends.sh` builds and runs them under both |

## Not done (stage 1)

- No outside review. This is the gate for ever making it the default.
- Timing: measured with valgrind (secret-dependent branches and addresses) and a statistical timing test, on one machine (Linux, one Intel Xeon core, GCC and Clang, x86-64). Not measured on Windows (MinGW, MSVC), macOS or WebAssembly, on other CPUs, or with other flags than the matrix in the review package; the generated machine code was inspected only through those tools.
- Not run on Windows (MinGW), macOS or WebAssembly; the code avoids platform extensions, but only Linux (GCC and Clang) has run.
- Speed: after the 51-bit field and the base-point table X25519 and Ed25519 are close to libsodium's (numbers in the review package), not equal; verification is about 1.4 times slower than libsodium's; the 16-bit fallback is still slow.
- Not in CI yet (no libFuzzer job, no sanitizer job for these tests, no valgrind job, no `OKAY_OWN_CRYPTO=ON` job). libFuzzer itself could not be run where this was written (the clang here has no fuzzer runtime); only the built-in mutating driver under ASan and UBSan ran.
- No SIMD, no batch verification, no Ed25519ph/ctx, no Argon2, no secretbox, no sign-open (combined) API: nothing uses them.
- libsodium's other primitives the engine does not call today are not reimplemented (by design, only what is used).
