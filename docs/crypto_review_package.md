# okay::crypto: package for an outside security review

> **Status: UNREVIEWED, off by default.** This is what a reviewer needs to work from. Nothing here is a claim that the code is secure; it says what exists, what was checked and how, what was not checked, and what we want answered. The decision to replace libsodium depends on this review (`docs/own_code_roadmap.md`, libsodium row). Background and test list: `docs/crypto.md`.

Every number below is quoted from the code, the tests or the docs in this repository. Where something was not measured, it says so.

## 1. Scope

### Files under review

| File | Lines | What |
|---|---|---|
| `engine/include/okay/Crypto/Crypto.hpp` | 177 | The whole public interface (`okay::crypto`) |
| `engine/src/Crypto/Aead.cpp` | 83 | ChaCha20-Poly1305 and XChaCha20-Poly1305 (IETF) |
| `engine/src/Crypto/ChaCha20.cpp` | 80 | ChaCha20, HChaCha20 |
| `engine/src/Crypto/Poly1305.cpp` | 142 | Poly1305 (5 x 26-bit limbs, `uint32_t`) |
| `engine/src/Crypto/Blake2b.cpp` | 114 | BLAKE2b |
| `engine/src/Crypto/Sha256.cpp`, `Sha512.cpp` | 85, 99 | SHA-256, SHA-512 |
| `engine/src/Crypto/Fe25519.hpp` | 152 | Field arithmetic mod 2^255-19: 16 limbs of 16 bits in `int64_t` |
| `engine/src/Crypto/X25519.cpp` | 102 | X25519 and the Kx session-key derivation |
| `engine/src/Crypto/Ed25519.cpp` | 291 | Ed25519 sign and verify, scalar arithmetic mod L |
| `engine/src/Crypto/Random.cpp` | 61 | OS random source |
| `engine/src/Crypto/Util.cpp` | 23 | `MemZero`, `ConstantTimeEqual` |
| `engine/include/okay/Net/NetCrypto.hpp` | 134 | The seam: the only file that picks between libsodium and `okay::crypto` |

The engine's Crypto sources total 1,232 lines (`engine/src/Crypto/`), plus the 177-line header.

Also in scope, because it is a second, older Ed25519 and SHA-512 in the tree: `shared/AntiCheatCrypto.hpp` (496 lines), a C++ port of public-domain TweetNaCl used when there is no libsodium and by the launcher, which does not link the engine. It adds the checks libsodium 1.0.18 makes (S < L, R not small-order, public key canonical and not small-order); a test pins the identity-key forgery plain TweetNaCl would accept. It is claimed to match libsodium only on those checks, not on every malformed input. We intend to replace it with `okay::crypto` (roadmap), so a reviewer may prefer to review that one only briefly.

### Primitives

XChaCha20-Poly1305 (IETF) and plain ChaCha20-Poly1305 (RFC 8439, draft-irtf-cfrg-xchacha), X25519 (RFC 7748), BLAKE2b (RFC 7693), SHA-256 and SHA-512 (FIPS 180-4), Ed25519 pure and detached (RFC 8032), the Kx session-key construction (same as libsodium's `crypto_kx`: X25519, then BLAKE2b-512 of `q || client_pk || server_pk`, split into rx and tx), `RandomBytes`, `MemZero`, `ConstantTimeEqual`. Only what the engine uses. No assembly, no SIMD, no `__int128`, no custom random generator.

### Where it is used (through `NetCrypto.hpp`)

| User | File | Uses |
|---|---|---|
| `SecureChannel` (the UDP transport's encryption) | `engine/include/okay/Net/SecureChannel.hpp` | Kx key pair and session keys, XChaCha20-Poly1305 seal and open with a random 24-byte nonce put in front of the ciphertext, optional sequence number inside the ciphertext (replay window of 64), optional associated data |
| `SessionTicket` (signed join tickets, key binding) | `engine/src/Net/SessionTicket.cpp` (146 lines) | Ed25519 verify, sign and key generation; SHA-256 of a public key for the ticket's key binding |
| Relay | `engine/src/Net/RelayServer.cpp` | `netcrypto::Random` for slot tokens; its MAC is `okayac::detail::HmacSha256` from `shared/AntiCheatChecks.hpp` (own SHA-256 code, not `okay::crypto`; see questions) |
| Anti-cheat and host identity | `shared/AntiCheatCrypto.hpp`, `engine/src/Net/NetworkManager.cpp` | Ed25519 sign and verify of the join key message (host identity pinning) and of the integrity manifest; with `OKAY_OWN_CRYPTO` the `okayac` calls go to `okay::crypto` |

Wire format is identical to libsodium's for all of the above; a client built one way talks to a server built the other (tested in both directions).

## 2. Threat model

What the engine uses this for, and what an attacker is assumed able to do. This is our working model; a reviewer should correct it.

- **Network attacker** (on path, can read, drop, replay, reorder, modify and inject UDP packets, can run a client or a server of their own). Goal: read or change game traffic, forge a join, replay a packet, impersonate a host that a client has pinned, forge a session ticket.
- **Malicious peer** sending any bytes as a public key, signature, ciphertext or ticket (small-order points, non-canonical encodings, S >= L, truncated and oversized inputs).
- **Local observer on the same machine** (another process, or a cloud neighbour) who may measure timing of operations on secrets. Out of scope for us only to the extent that timing was not measured (section 4).
- **Out of scope:** an attacker with code execution in the game process (keys are in its memory), a compromised OS random source, the owner's key-management practice for ticket and manifest signing keys (`docs/anticheat.md`), denial of service by volume, side channels beyond timing (power, cache attacks on shared hardware are not analysed).
- **Not provided by design:** forward secrecy beyond the per-session ephemeral X25519 keys, anonymity, protection of metadata (packet sizes and timing are visible). Anti-cheat and the game's trust model are separate from this package.

## 3. What is verified, and how

| Check | What it covers | Where | Numbers from the code |
|---|---|---|---|
| Published vectors | RFC 8439 (A.1 to A.5 and the 2.x examples), the XChaCha draft (HChaCha20 and the AEAD vector), RFC 7748 (5.2, 6.1, iterated 1 and 1000; 1,000,000 only with `OKAY_CRYPTO_SLOW=1`), RFC 8032 section 7.1 (all five), RFC 7693 (`abc` and the appendix E self-test). Vectors in `tests/crypto_rfc_vectors.hpp` (169 lines) are extracted from the RFC text | `tests/test_crypto.cpp` (392 lines), ctest `crypto` | pass |
| Streaming equals one-shot | at every split point | same | pass |
| Tamper tests | every bit of the tag and ciphertext, and the AAD, nonce and key flipped must fail | same | pass |
| Ed25519 strictness | malleability (S >= L), small-order R and key, non-canonical key encodings | same | pass |
| libsodium cross-check | random cases per primitive, both directions (ours seals and theirs opens, theirs seals and ours opens), accept/reject agreement on malformed Ed25519 inputs and small-order X25519 points | `tests/test_crypto_sodium_xcheck.cpp` (241 lines), ctest `crypto_sodium_xcheck`; built only when libsodium is found | `OKAY_CRYPTO_XCHECK_N` cases per primitive, **default 3000**; the Ed25519 ones are one tenth of it (300), because each costs milliseconds. The test prints the libsodium version and the count it ran |
| Python cross-check | the same against Python `cryptography` (OpenSSL) and `hashlib`, both directions, inputs sizes biased to block edges, fixed seed unless `OKAY_CRYPTO_SEED` is set. OpenSSL has no XChaCha20, so XChaCha is checked against libsodium instead, and here through a construction of the HChaCha20 subkey | `tests/scripts/crypto_xcheck.py` (134 lines) driving `tests/crypto_xcheck_tool.cpp` (99 lines), ctest `crypto_python_xcheck`; skipped when the module is missing | ctest runs it with **1500** cases per primitive |
| Fuzz | decryption of arbitrary bytes never succeeds unless it is a valid seal and never writes plaintext on failure; Ed25519 verify of arbitrary signature, key and message never crashes and never accepts garbage and agrees with itself; hashes and MACs streamed at any split equal one-shot; X25519 and Kx accept any 32 bytes | `tests/fuzz_crypto.cpp` (156 lines); ctest `crypto_fuzz_smoke` runs **300** iterations of the built-in mutator; `-DOKAY_CRYPTO_LIBFUZZER=ON` (clang) builds the real libFuzzer binary with ASan and UBSan | the smoke test is small; `docs/crypto.md` suggests `fuzz_crypto_smoke 100000` under ASan/UBSan for more. We have no recorded long libFuzzer run to quote |
| Whole net layer on both backends | `test_net_crypto.cpp` (85 lines) compiled once per backend in one build (`net_crypto_own`, `net_crypto_sodium`); the `net*` family, `anticheat*` and `dedicated_server` run under whichever backend the build has; interoperability both directions | `scripts/test_crypto_backends.sh` | pass on Linux/GCC |
| Memory | `MemZero` uses volatile stores plus a compiler barrier; `Open` zeroes its output when the tag fails; intermediate secrets (otk, subkey, scalar, hash buffers, points) are zeroed after use | `Util.cpp`, `Aead.cpp`, `Ed25519.cpp`, `X25519.cpp` | by reading; not tested by inspecting memory |

All of the above has run on Linux with GCC only.

## 4. What is NOT verified

- **Constant time is by design only.** The code uses fixed loops, masks and fixed-length ladders (255 steps for X25519, fixed-length Ed25519 scalar multiplication), and has no secret-dependent branch or table index. **No timing measurement** (dudect or similar) has been done on any compiler, optimisation level or CPU, and nobody has read the generated machine code.
- **No Windows (MinGW or MSVC), macOS or WebAssembly run.** The code avoids platform extensions and `__int128`, and `Random.cpp` has Windows (`BCryptGenRandom`), macOS and BSD (`getentropy`) branches, but only the Linux branch (`getrandom`) has ever executed.
- **Not in CI:** no libFuzzer job, no sanitizer job for these tests, no `OKAY_OWN_CRYPTO=ON` job. Runs were by hand.
- **No long fuzz campaign is recorded**, and no coverage report.
- **Speed:** X25519 and Ed25519 take a few milliseconds per operation, far slower than libsodium. Fine for a handshake; a possible denial-of-service amplification point has not been analysed.
- **Memory hygiene of callers:** `SecureChannel` holds the Kx session keys in plain members (`m_sk`, `m_rx`, `m_tx`) and a read of the header shows no destructor and no `MemZero` or `sodium_memzero` for them. The same holds for the libsodium backend. A reviewer should say whether that matters.
- **No formal verification, no differential test against a third implementation of XChaCha20-Poly1305 apart from libsodium.**
- No Argon2, secretbox, sign-open, Ed25519ph or batch verification: not implemented, nothing uses them.

## 5. Reviewer checklist

Please mark each item pass, fail or not assessed, with a pointer.

1. **Constant time.** Any branch, early exit, table index or variable-time instruction (division, variable shift, `memcmp`) on secret data in: `Fe25519.hpp` (arithmetic right shifts of negative `int64_t` in carry and reduce), `X25519.cpp` (ladder, conditional swap), `Ed25519.cpp` (scalar multiplication, `ReduceL` bit-by-bit reduction, `CondSubL`, `MulAddL`), `Poly1305.cpp`, `ChaCha20.cpp`, `Aead.cpp`. Does `ConstantTimeEqual` survive optimisation? Is the tag compare the only secret-dependent comparison?
2. **Key handling.** Secret keys, seeds, scalars, one-time keys (`otk`), HChaCha20 subkeys and the Kx hash: are all copies wiped on every path (including early returns)? Does `Ed25519Sign` really ignore the second half of `sk`, as documented? Are session keys held longer than needed in `SecureChannel`?
3. **Nonce reuse.** `SecureChannel` uses a random 24-byte nonce per message with one key per direction. Is that sound for the expected volume per session? Is any code path able to seal two messages with the same key and nonce (copying a channel object, the `m_nextSecure` to `m_secure` handover in `NetworkManager`, `Seal` being `const` with a mutable sequence counter)? Is the ChaCha20 32-bit block counter bounded for the maximum message size?
4. **Small-order and invalid points.** X25519 must report an all-zero result (we return false but still write `out`); Kx must reject it and zero the keys. Ed25519 must reject small-order R and A and non-canonical A. Does the decode accept a non-canonical y (the comment says callers check it) in every caller?
5. **S < L and malleability.** `ScalarIsCanonical` before verification; verification re-encodes sB - hA and compares the 32 bytes with R (no cofactor multiplication); confirm there is no accept/reject divergence with libsodium or RFC 8032 that matters here.
6. **Memory zeroing.** `MemZero` (volatile stores and an empty `asm` barrier on GCC and Clang; no barrier on MSVC): is it enough on every compiler we support (GCC, MinGW, Clang, MSVC, Emscripten)? `Poly1305::Final` and `Blake2b::Final` wipe state; do destructors do the same on abandoned objects?
7. **Random source.** `RandomBytes` uses `getrandom` (Linux), `getentropy` (macOS, BSD, Emscripten), `BCryptGenRandom` with the system-preferred RNG (Windows), `/dev/urandom` as the last fallback, and aborts if the OS gives nothing. Check the 256-byte chunking, `EINTR` handling, early-boot behaviour, and the Emscripten branch (`getentropy` in a browser).
8. **API misuse.** Overlapping `in` and `out` (documented as allowed for Seal), null pointers with zero length, the 16-byte minimum in `Open`, `n + 16` buffer sizing by callers, integer overflow in length arithmetic (`aadLen`, `n` as `uint64_t` in the length block), the replay window arithmetic in `SecureChannel::AcceptSeq`, and the `SessionTicket` parser's handling of attacker-controlled lengths before `Verify`.
9. **Protocol construction.** The Kx derivation (transcript is `q || client_pk || server_pk` only), the join key message that Ed25519 signs for host pinning (`JoinKeyMessage` in `NetworkManager.cpp`), the ticket key binding (SHA-256 of the public key), and whether the sealed handshake is open to downgrade (there is a plaintext join fallback, `m_legacyJoin`).
10. **Hash correctness at edges.** SHA-256, SHA-512 and BLAKE2b at block boundaries, length counters above 2^32 bytes, keyed BLAKE2b.
11. **Build flags.** Behaviour under `-O2`, `-O3`, `-Ofast`/`-ffast-math` (should not matter), LTO, 32-bit targets, and signed shift and overflow assumptions in `Fe25519.hpp` (`int64_t` right shift of negatives is arithmetic in practice; undefined or implementation-defined depending on language standard).

## 6. Build and run

Requires CMake, Ninja (the script uses it), a C++17 compiler. libsodium (`libsodium-dev`) is needed for the libsodium side and for the cross-check; Python 3 with the `cryptography` module is optional for the Python cross-check.

```
# Both backends, then the whole encryption-related test set on each:
scripts/test_crypto_backends.sh [build-root] [jobs]
#   runs ctest -R '^(crypto|crypto_.*|net|net_.*|dedicated_server|anticheat.*)$' under
#   -DOKAY_OWN_CRYPTO=OFF (libsodium) and =ON (okay::crypto); prints "both backends passed"

# Just the primitives:
cmake -S . -B build -DOKAY_BUILD_SANDBOX=OFF -DOKAY_BUILD_LAUNCHER=OFF
cmake --build build -j
cd build && ctest -R '^crypto' --output-on-failure

# More cases, sanitizers, slow vector, real libFuzzer (clang):
OKAY_CRYPTO_XCHECK_N=20000 ./tests/test_crypto_sodium_xcheck
OKAY_CRYPTO_SLOW=1 ./tests/test_crypto
cmake -S . -B build-asan -DOKAY_SANITIZE=address,undefined
cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DOKAY_CRYPTO_LIBFUZZER=ON
./build-fuzz/tests/fuzz_crypto -max_total_time=300 corpus/
./build/tests/fuzz_crypto_smoke 100000          # without libFuzzer; best under ASan/UBSan
```

Test names: `crypto`, `crypto_sodium_xcheck`, `crypto_python_xcheck`, `crypto_fuzz_smoke`, `net_crypto`, `net_crypto_own`, `net_crypto_sodium`, plus the `net*` and `anticheat*` families. Switch on our code in an application build with `-DOKAY_OWN_CRYPTO=ON` (then libsodium is not linked at all). Binary paths above may differ with the generator; ctest names are stable.

## 7. Questions we want answered

1. Is the code constant time as compiled? What measurement would you accept, on which compilers, flags and CPUs, and what is the minimum matrix before we could ship (GCC and MinGW on x86-64 for Windows and Linux builds; Clang for macOS; WebAssembly)?
2. Is the 16-limb, 16-bit, `int64_t` field arithmetic with arithmetic right shifts acceptable on every compiler we support, and is the `ReduceL` bit-by-bit scalar reduction constant time and correct?
3. Is a random 24-byte XChaCha nonce per message under a per-direction key acceptable for our traffic, or do you want a counter nonce?
4. Is the Ed25519 verification (strict, byte comparison of the re-encoded point, no cofactor, S < L, small-order rejection, canonical key) the right strictness for session tickets, host pinning and the integrity manifest? Any case where accepting libsodium's exact behaviour matters more than being strict?
5. `SecureChannel` keeps session keys unwiped, and `Seal` is `const` with a mutable sequence counter: change anything?
6. The relay MAC uses `okayac::detail::HmacSha256` from `shared/AntiCheatChecks.hpp`, which is separate code from `okay::crypto::Sha256`. Should it move onto the reviewed primitives, and is that HMAC itself sound?
7. The plaintext join fallback (`m_legacyJoin`) and the sealed-join retry count (`kSealedJoinTries = 4`): is there a downgrade we have missed?
8. What should we add before we ask you to sign off: more fuzz time, a third implementation to compare against, known-answer tests from another suite, formal methods on the field arithmetic?
9. What would you want in CI to keep it safe afterwards (sanitizers, a libFuzzer job, a timing check)?
10. Anything you would rather we did not implement ourselves at all, and instead kept using from a maintained library?
