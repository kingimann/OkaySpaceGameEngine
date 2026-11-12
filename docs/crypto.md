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

No custom random generator. No assembly. Field arithmetic is 16 limbs of 16 bits in `int64_t` (slow but simple and
portable: no `__int128`, so MSVC and WebAssembly build it too). Constant time by construction: fixed loops, masks,
no secret-dependent branch or table index, fixed-length ladders for X25519 and for Ed25519 scalar multiplication.
This is the *design*; nobody has audited the generated machine code or measured it.

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
| `crypto_fuzz_smoke` | the fuzz target (`tests/fuzz_crypto.cpp`) for a few hundred iterations; `-DOKAY_CRYPTO_LIBFUZZER=ON` builds the real libFuzzer binary (clang) |
| the whole `net*` family, `anticheat*`, `dedicated_server` | run under whichever backend the build was configured with; `scripts/test_crypto_backends.sh` builds and runs them under both |

## Not done (stage 1)

- No outside review. This is the gate for ever making it the default.
- No timing measurement of the compiled code (dudect or similar), on any compiler, optimisation level or CPU.
- Not run on Windows (MinGW), macOS or WebAssembly; the code avoids platform extensions, but only Linux/GCC has run.
- Speed: X25519 and Ed25519 are far slower than libsodium's (a few milliseconds per operation); fine for a handshake, not tuned.
- Not in CI yet (no libFuzzer job, no sanitizer job for these tests, no `OKAY_OWN_CRYPTO=ON` job).
- No SIMD, no batch verification, no Ed25519ph/ctx, no Argon2, no secretbox, no sign-open (combined) API: nothing uses them.
- libsodium's other primitives the engine does not call today are not reimplemented (by design, only what is used).
