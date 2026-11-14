# okay::crypto: package for an outside security review

> **Status: UNREVIEWED, off by default.** This is what a reviewer needs to work from. Nothing here is a claim that the code is secure; it says what exists, what was checked and how, what was not checked, and what we want answered. The decision to replace libsodium depends on this review (`docs/own_code_roadmap.md`, libsodium row). Background and test list: `docs/crypto.md`.

Every number below is quoted from the code, the tests or the docs in this repository, or from a measurement run on 2026-11-13 whose method and machine are given in section 4. Where something was not measured, it says so. **No outside review has happened.** The measurements in section 4 were made by the engine team on one machine; they are evidence for the reviewer to check, not a result of a review.

**Re-verified on 2026-11-14 against `main` at `0631dcca`** (Linux x86-64, GCC 13.3, libsodium 1.0.18, 4 vCPUs). Checked again: every line count in section 1 (four were stale and are corrected: `test_crypto.cpp` is 443 lines, `crypto_difftest.cpp` 464, the account `Sha256` class is lines 56 to 160 and 105 lines long, and the tests total about 3,050 lines, not 3,100); every ctest name and script named here exists; the 1,927 Wycheproof vectors and 2,753 checks; the vector-file counts. Re-run: all crypto ctests (`ctest -R '^crypto'`, `crypto_*`, `net_crypto_own`, `net_crypto_sodium`, `anticheat_integrity_ownlib` and `_portable`: pass; 9,529 checks in `crypto`, 96,485 in `crypto_field`, 2,753 in `crypto_wycheproof`, 68,856 in `crypto_sodium_xcheck` with 3,000 cases per primitive, 370 in each anticheat integrity build, `crypto_difftest` at N = 1,500: 49,328 comparisons, 0 mismatches), `scripts/crypto_ct_matrix.sh` (all 24 builds clean, both positive controls reported) and `scripts/crypto_fuzz_sanitized.sh 200000 1` (no sanitizer report, no violated property). Not re-run (too long or not offline): the 1,000,000-case differential test (about 15 minutes), the timing test, the 30,000-case cross-checks, a libFuzzer run, and `net_crypto` and `anticheat_integrity` (they link the whole engine, which was not built). The timing figures, speed tables and the 32.6 million comparisons in the sections below are quoted from the 2026-11-13 measurements and were not repeated. The reviewer archive is made by `scripts/make_crypto_review_bundle.sh`; the request to send is `docs/crypto_review_request.md`.

## 0. Start here: an hour with this package

If you have one hour, do this in order. Everything named is in this repository; section 6 has the exact commands.

| Minutes | Read or run | Why |
|---|---|---|
| 0 to 10 | Section 2 (threat model) and section 1 (scope table). Correct the threat model if it is wrong; it decides what counts as a finding | Sets what matters |
| 10 to 25 | `engine/src/Crypto/Fe25519_64.hpp` (193 lines; the limb-bound comment at the top is the contract), `Scalar25519.hpp` (113), `CtAnnotate.hpp` (34) | The arithmetic every secret goes through: the part where a bug is both likeliest and worst |
| 25 to 40 | `Ed25519.cpp` (476): `BaseMult` and `SelectBase` (the only secret-scalar path), then `Ed25519Verify` and `DoubleScalarMultPublic` (public data; variable time on purpose) | The signature code, and what changed in round 2 (section 4.2) |
| 40 to 50 | `X25519.cpp` (104), `Aead.cpp` (87), `ChaCha20.cpp` (86) | Short; the ladder, the tag-before-decrypt order, the counter guard |
| 50 to 60 | Run `ctest -R '^crypto'`, `crypto_difftest`, and `scripts/crypto_ct_matrix.sh` (sections 3 and 6); read section 9 (what only you can judge) and section 7 (known issues we want decided) | Reproduce the evidence, then decide where to spend the rest of the review |

**State in one paragraph.** The library implements only what the engine calls (XChaCha20-Poly1305 and ChaCha20-Poly1305, X25519, Ed25519, BLAKE2b, SHA-256, SHA-512, the Kx key derivation, OS randomness). It matches published vectors (RFCs and all 1,927 Project Wycheproof vectors in the relevant files), matches libsodium on every X25519 and Ed25519 decision including the vectors the suite calls "acceptable", and matches libsodium and an independent TweetNaCl-derived port bit for bit on millions of random and structured inputs (section 3). A valgrind secret-dependence audit over 24 compiler/flag/field builds and a statistical timing test found nothing (section 4.1) after one compiler-induced branch was fixed. It has been tested on one Linux x86-64 virtual machine only. **Verdict: not ready.** Nothing here replaces your review; the gate in `docs/own_code_roadmap.md` stays closed until it is done and its findings are fixed.

## 1. Scope

### Files under review

| File | Lines | What |
|---|---|---|
| `engine/include/okay/Crypto/Crypto.hpp` | 188 | The whole public interface (`okay::crypto`) |
| `engine/src/Crypto/Aead.cpp` | 87 | ChaCha20-Poly1305 and XChaCha20-Poly1305 (IETF) |
| `engine/src/Crypto/ChaCha20.cpp` | 86 | ChaCha20, HChaCha20 |
| `engine/src/Crypto/Poly1305.cpp` | 143 | Poly1305 (5 x 26-bit limbs, `uint32_t`) |
| `engine/src/Crypto/Blake2b.cpp` | 114 | BLAKE2b |
| `engine/src/Crypto/Sha256.cpp`, `Sha512.cpp` | 85, 99 | SHA-256, SHA-512 |
| `engine/src/Crypto/Fe25519.hpp` | 14 | Picks the field implementation at compile time |
| `engine/src/Crypto/Fe25519_64.hpp` | 193 | Field arithmetic mod 2^255-19, five 51-bit limbs in `uint64_t`, 128-bit product (needs `__int128`; GCC and Clang, 64-bit) |
| `engine/src/Crypto/Fe25519_16.hpp` | 149 | The same field as 16 limbs of 16 bits in `int64_t`: the portable fallback (MSVC, WebAssembly, 32-bit; `-DOKAY_CRYPTO_FE16` forces it) |
| `engine/src/Crypto/Fe25519Chain.inc` | 65 | The addition chains for inversion and the square-root exponent (included by both field files) |
| `engine/src/Crypto/Scalar25519.hpp` | 113 | Scalars modulo the Ed25519 group order: Barrett reduction on 32-bit limbs, multiply-add, canonical check |
| `engine/src/Crypto/CtAnnotate.hpp` | 34 | The value barrier `ct::Barrier` and the audit hook `OKAY_CT_PUBLIC` (a no-op unless built for the valgrind audit) |
| `engine/src/Crypto/X25519.cpp` | 104 | X25519 and the Kx session-key derivation |
| `engine/src/Crypto/Ed25519.cpp` | 476 | Ed25519 sign and verify, the base-point table, point arithmetic, the interleaved double-scalar multiplication used by verification |
| `engine/src/Crypto/Random.cpp` | 61 | OS random source |
| `engine/src/Crypto/Util.cpp` | 26 | `MemZero`, `ConstantTimeEqual` |
| `engine/include/okay/Net/NetCrypto.hpp` | 134 | The seam: the only file that picks between libsodium and `okay::crypto` |

The engine's Crypto sources total 1,849 lines (`engine/src/Crypto/`: 1,281 in the `.cpp` files, 568 in the headers and the `.inc`), plus the 188-line public header. The tests that carry the evidence are about 3,050 further lines (`tests/test_crypto.cpp` 443, `test_crypto_wycheproof.cpp` 523, `crypto_difftest.cpp` 464, `test_crypto_sodium_xcheck.cpp` 241, `test_crypto_field.cpp` 209, `crypto_ct_valgrind.cpp` 179, `crypto_timing.cpp` 268, `fuzz_crypto.cpp` 156, `fuzz_net_crypto.cpp` 260, `crypto_rfc_vectors.hpp` 169, and the Python driver) and 1.3 MB of vector files (`tests/wycheproof/`, third-party data, not code under review). The code is built as its own static library, `okay_crypto` (`okay::crypto` in CMake); `okay_engine` links it, and so can anything that must not link the engine.

### Also formally in scope: the other crypto code in the tree

These are not `okay::crypto`, but they are security-relevant code we wrote or ported and they are on the removal list too, so the review covers them.

| File | Lines | What | Why it is in scope |
|---|---|---|---|
| `shared/AntiCheatCrypto.hpp` | 523 | C++ port of public-domain TweetNaCl: Ed25519 and SHA-512, used when there is neither libsodium nor `OKAY_OWN_CRYPTO`. (The roadmap said the launcher does not link the engine; in the tree today it does, for its painter, and `launcher/CMakeLists.txt` now also links `okay_crypto` directly.) With `OKAY_OWN_CRYPTO` every function a caller uses (Ed25519 sign, verify, public key, `IsWeakKey`, SHA-512) goes to `okay::crypto`; the port stays compiled as the reference. Adds the checks libsodium 1.0.18 makes (S < L, R not small-order, public key canonical and not small-order); a test (`tests/test_anticheat_integrity.cpp`) pins the identity-key forgery plain TweetNaCl would accept. Claimed to match libsodium only on those checks, not on every malformed input | A second Ed25519 and SHA-512 that signs and verifies the integrity manifest and the join key message. To be replaced by `okay::crypto` after this review |
| `shared/AntiCheatChecks.hpp`, function `okayac::detail::HmacSha256` (lines 175 to 192 of 411) | 18 (file: 411) | HMAC-SHA256 written by hand (key padding, ipad and opad, wipes its key buffer), also used for the clock-challenge tag (`ClockTag`) | Used by the relay for its address cookie (`RelayServer::MakeCookie`, `engine/src/Net/RelayServer.cpp`; a cookie is the HMAC of address, port and minute under a random relay secret, compared in `CookieValid` with a XOR-accumulate loop) and by the anti-cheat clock challenge |
| `engine/include/okay/Platform/Account/AccountService.hpp`, class `okay::account::detail::Sha256` (lines 56 to 160 of that header) | 105 (class) | A third SHA-256 in the tree, "public domain, compact", which `HmacSha256` above is built on; it also hashes local-backend passwords | The relay cookie and the anti-cheat clock tag rest on it, so it is part of the HMAC's review. It should be replaced by `okay::crypto::Sha256` |
| `shared/OfficialGame.hpp`, `FileSha256` (around line 446) | n/a | SHA-256 of files for the official game's updater and the manifest | Review whether it too goes onto `okay::crypto::Sha256` |

### Primitives

XChaCha20-Poly1305 (IETF) and plain ChaCha20-Poly1305 (RFC 8439, draft-irtf-cfrg-xchacha), X25519 (RFC 7748), BLAKE2b (RFC 7693), SHA-256 and SHA-512 (FIPS 180-4), Ed25519 pure and detached (RFC 8032), the Kx session-key construction (same as libsodium's `crypto_kx`: X25519, then BLAKE2b-512 of `q || client_pk || server_pk`, split into rx and tx), `RandomBytes`, `MemZero`, `ConstantTimeEqual`. Only what the engine uses. No assembly, no SIMD, no custom random generator; `__int128` only in the 51-bit field where the compiler has it.

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
| Published vectors | RFC 8439 (A.1 to A.5 and the 2.x examples), the XChaCha draft (HChaCha20 and the AEAD vector), RFC 7748 (5.2, 6.1, iterated 1 and 1000; 1,000,000 only with `OKAY_CRYPTO_SLOW=1`), RFC 8032 section 7.1 (all five), RFC 7693 (`abc` and the appendix E self-test). Vectors in `tests/crypto_rfc_vectors.hpp` (169 lines) are extracted from the RFC text | `tests/test_crypto.cpp` (443 lines), ctest `crypto` | pass |
| Streaming equals one-shot | at every split point | same | pass |
| Tamper tests | every bit of the tag and ciphertext, and the AAD, nonce and key flipped must fail | same | pass |
| Ed25519 strictness | malleability (S >= L), small-order R and key, non-canonical key encodings | same | pass |
| libsodium cross-check | random cases per primitive, both directions (ours seals and theirs opens, theirs seals and ours opens), accept/reject agreement on malformed Ed25519 inputs and small-order X25519 points | `tests/test_crypto_sodium_xcheck.cpp` (241 lines), ctest `crypto_sodium_xcheck`; built only when libsodium is found | `OKAY_CRYPTO_XCHECK_N` cases per primitive, **default 3000**; the Ed25519 ones are one tenth of it (300), because each costs milliseconds. The test prints the libsodium version and the count it ran |
| Python cross-check | the same against an independent cryptography library and a standard hash library (through Python), both directions, inputs sizes biased to block edges, fixed seed unless `OKAY_CRYPTO_SEED` is set. that library has no XChaCha20, so XChaCha is checked against libsodium instead, and here through a construction of the HChaCha20 subkey | `tests/scripts/crypto_xcheck.py` (134 lines) driving `tests/crypto_xcheck_tool.cpp` (99 lines), ctest `crypto_python_xcheck`; skipped when the module is missing | ctest runs it with **1500** cases per primitive |
| Fuzz, primitives | decryption of arbitrary bytes never succeeds unless it is a valid seal and never writes plaintext on failure; Ed25519 verify of arbitrary signature, key and message never crashes and never accepts garbage and agrees with itself; hashes and MACs streamed at any split equal one-shot; X25519 and Kx accept any 32 bytes | `tests/fuzz_crypto.cpp` (156 lines); ctest `crypto_fuzz_smoke` runs **300** iterations of the built-in mutator; `-DOKAY_CRYPTO_LIBFUZZER=ON` (clang) builds the real libFuzzer binary with ASan and UBSan | the smoke test is small; `docs/crypto.md` suggests `fuzz_crypto_smoke 100000` under ASan/UBSan for more. We have no recorded long libFuzzer run to quote |
| Whole net layer on both backends | `test_net_crypto.cpp` (85 lines) compiled once per backend in one build (`net_crypto_own`, `net_crypto_sodium`); the `net*` family, `anticheat*` and `dedicated_server` run under whichever backend the build has; interoperability both directions | `scripts/test_crypto_backends.sh` | pass on Linux/GCC |
| Memory | `MemZero` uses volatile stores plus a compiler barrier; `Open` zeroes its output when the tag fails; intermediate secrets (otk, subkey, scalar, hash buffers, points) are zeroed after use | `Util.cpp`, `Aead.cpp`, `Ed25519.cpp`, `X25519.cpp` | by reading; not tested by inspecting memory |
| Field arithmetic | The 51-bit and the 16-bit field agree on every operation, on 40 x 40 edge-value pairs (0, 1, p-1, p, p+1, 2^255-1, powers of two, limb boundaries), on 3000 random pairs and on 200 chains of 300 random operations; the inversion and square-root chains equal the bit-by-bit power; Barrett reduction and multiply-add mod L equal a bit-by-bit reference (the first implementation) on 80 edge values (0, L-1, L, L+1, 2^512-1, multiples of L), 4000 random 64-byte values and 3000 random multiply-adds | `tests/test_crypto_field.cpp`, ctest `crypto_field` | pass |
| Portable field | `tests/test_crypto.cpp` compiled again with `-DOKAY_CRYPTO_FE16` | ctest `crypto_fe16` | pass |
| libsodium cross-check, larger run | `OKAY_CRYPTO_XCHECK_N=30000` (30,000 cases per primitive, 3,000 for the Ed25519 ones), after the speed work | `tests/test_crypto_sodium_xcheck.cpp` | 683,740 checks, 0 failures (2026-11-13) |
| Python cross-check, larger run | 10,000 cases per primitive (5,000 for XChaCha), Ed25519 300 | `tests/scripts/crypto_xcheck.py` | "ALL AGREE" (2026-11-13) |
| Strictness without the engine | `tests/test_anticheat_integrity.cpp` (RFC 8032 vectors, S < L, small-order R and key, canonical key, the identity-key forgery, SHA-512 vectors) compiled twice without the engine: against `okay::crypto` alone (`-DOKAY_OWN_CRYPTO`) and with neither it nor libsodium (the port); the same file also runs as before in the engine build | ctest `anticheat_integrity_ownlib`, `anticheat_integrity_portable`, `anticheat_integrity` | 370 checks each, 0 failures |
| Fuzz, parsing paths | `tests/fuzz_net_crypto.cpp`: key exchange with a peer key of any length and value (wrong length and every small-order point refused, client and server agree), sealed-message open on mutated and arbitrary bytes and the replay window (no second open, no plaintext left behind), `SessionTicket::Verify` on mutated tickets and arbitrary strings (accepted only if byte-identical to the signed ticket; base64url is canonical), Ed25519 verify and AEAD open of mutated valid messages (accepted only if nothing changed; includes the structured S + L mutation) | ctest `crypto_fuzz_net_smoke`; `scripts/crypto_fuzz_sanitized.sh` | see "Fuzz campaign" in section 4 |
| Project Wycheproof vectors (round 2) | EVERY vector of `x25519_test.json` (518), `ed25519_test.json` (210), `chacha20_poly1305_test.json` (325), `xchacha20_poly1305_test.json` (315), `hmac_sha256/512_test.json` (174 each, HMAC built in the test from our SHA-2), `hkdf_sha256/512_test.json` (86 and 83), `chacha20_counter_test.json` (42): 1,927 vectors from commit `65ca494727a5` of the public repository (Apache-2.0, copied unmodified into `tests/wycheproof/`). "valid" must pass, "invalid" must be refused, "acceptable" is recorded, pinned and compared with libsodium. No BLAKE2b or SHA-2 hash vectors exist in that project | `tests/test_crypto_wycheproof.cpp` (523 lines), ctest `crypto_wycheproof`; `OKAY_WYCHEPROOF_DIR` points it at another copy | 2,753 checks, 0 failures; one real finding (ChaCha20 counter wrap, fixed, below); no difference from libsodium on any X25519 or Ed25519 vector. Details and decisions in 3.1 |
| Large differential test (round 2) | okay::crypto against libsodium (X25519 scalar mult and base, Kx, Ed25519 public key, signature bytes and verdicts, ChaCha20-Poly1305 and XChaCha20-Poly1305, SHA-256, SHA-512, BLAKE2b with keys, Poly1305) and against the TweetNaCl-derived port (Ed25519 public key, signature, verdict, `IsWeakKey`; SHA-512) on random and structured inputs: 748 edge values around p, 2^255-19, the group order L, powers of two, runs of 0x00/0x7f/0x80/0xff, the 14 small-order encodings and their single-bit variants, non-canonical y; scalars with high bits set, clamped and unclamped; every single bit flipped in R, S and the key of valid signatures; S + L; every truncation, every bit of ciphertext and tag, and flips in AAD, nonce and key for both AEADs | `tests/crypto_difftest.cpp` (464 lines), ctest `crypto_difftest_smoke` (N = 1,500); `OKAY_DIFF_N=1000000 crypto_difftest` for the full run | **Full run (2026-11-13, N = 1,000,000, one core, about 13 minutes, GCC 13 `-O2`): 32,591,852 comparisons, 0 mismatches.** By category: X25519 1,000,000 (accept/refuse flag and output); X25519 base 100,000; Kx 50,000; Ed25519 public key, signature bytes (byte-identical, `Ed25519SignTrustedPk` too) and verdict 500,000; verification of mutated and structured inputs 1,000,008; Ed25519 against the port 140,000 plus 400,000 `IsWeakKey` classifications; AEAD 27,376,844 (seal bytes, open verdicts and plaintexts, no plaintext left behind by a failed open); SHA-256, SHA-512, keyed BLAKE2b 1,000,000 (SHA-512 also against the port, 25,000); Poly1305 1,000,000. **Under ASan and UBSan (`-DOKAY_SANITIZE=address,undefined`, N = 40,000): 1,326,550 comparisons, 0 mismatches, no sanitizer report.** The harness was checked for sensitivity by breaking the code on purpose: changing one byte of one small-order blocklist entry and corrupting one table index inside the verification loop were both reported within the 1,500-case smoke size. One entry change was NOT reported (the two non-canonical blocklist entries for y = p and y = p + 1): they are redundant by construction, because a non-canonical key is refused by the canonical check and a non-canonical R can never equal a re-encoded point, so removing them changes no verdict |


### 3.1 Where we differ from the standards or the vectors, and why (round 2)

Running every Wycheproof vector and the structured differential test produced exactly one defect and a list of strictness decisions. Every decision below is "match libsodium 1.0.18", because wire compatibility with libsodium builds is a requirement; a reviewer may disagree with a decision, but none is an accident.

| Case | Vector file says | We do | Same as libsodium | Why |
|---|---|---|---|---|
| ChaCha20 32-bit block counter wrap (a message needing more blocks than 2^32 minus the start counter) | "invalid" (the `ChaCha20-IETF` group of `chacha20_counter_test.json`: the keystream would repeat) | **DEFECT, fixed.** `ChaCha20Xor` used to wrap silently; it now returns false and writes nothing. `Open` returns false; `Seal` (no error channel) stops the process, as libsodium does. Unreachable in practice: needs a message of about 256 GiB | yes (it aborts) | keystream reuse is catastrophic; regression tests in `tests/test_crypto.cpp` and the vectors |
| X25519 with a low-order public key (all-zero shared secret) | "acceptable": either refuse or compute | refuse (return false; the output buffer holds the all-zero result, and `Kx` zeroes its keys) when the result is all zero: 31 of 31 vectors | yes | RFC 7748 section 6.1 allows the check; `Kx` must not derive a key from a zero secret |
| X25519 with a non-canonical u coordinate (u >= p) | "acceptable" | reduce mod p and compute (13 computed), refuse only when the result is zero (6 refused) | yes, on every vector | RFC 7748 section 5 says to reduce |
| X25519 twist and special points, unclamped private keys | "acceptable" or "valid" | compute (the result matches the listed secret on all of them) | yes | RFC 7748: the ladder is correct on the twist; the scalar is clamped inside |
| Ed25519 public key of small order (8 vectors, including the identity) | "acceptable" | reject | yes (blocklist) | the identity key "verifies" R = identity, S = 0 for any message |
| Ed25519 public key of mixed order (prime-order point plus a torsion point; 21 vectors) | "acceptable" | accept when the cofactorless equation [S]B = R + [h]A holds (all 21 accepted) | yes | RFC 8032 allows either the cofactorless or the cofactored equation; libsodium and the ref10 lineage use the cofactorless one, and signatures verifying under both is what matters for interoperability |
| Ed25519 signature malleability (S + L), non-canonical R, non-canonical S, non-canonical key, R or S overlapping, S = L | "invalid" (64 vectors) | all rejected: S must be < L, R is compared as bytes with the re-encoded point (so a non-canonical R can never match), the key must have y < p | yes | strictness the standard recommends |
| Ed25519 signatures that are not 64 bytes (truncated, with garbage appended) | "invalid" | not expressible: `Ed25519Verify` takes a fixed 64-byte array; the test counts them as rejected by construction and also feeds padded copies to check nothing crashes | n/a | API shape; callers (`SessionTicket`, the anti-cheat code) check lengths before calling |
| AEAD vectors with a nonce or key of the wrong size (9 per AEAD file) | "invalid" | not expressible: the nonce and key are fixed-size pointers; counted as skipped | n/a | API shape |
| 64-bit-counter ChaCha20 exhaustion ("invalid" vectors in the XChaCha20 group) | "invalid" | not expressible: we expose only the IETF 32-bit counter | n/a | nothing in the engine can produce 2^64 blocks |

The "valid" vectors of the XChaCha20 counter group (36) are run by driving `ChaCha20Xor` one block at a time with a test-side 64-bit carry, which checks the block function on every boundary but not a library-level carry (there is none).

All of the above has run on Linux (GCC; the audits in section 4 also with Clang) on one x86-64 machine only.

## 4. Measurements, and what is NOT verified

### 4.1 Constant time: how it was checked (2026-11-13)

Two methods, both in the tree, plus a reading of the code. The machine for everything in this section: a virtual machine with 4 vCPUs (Intel Xeon Processor at 2.10 GHz), Linux 6.18, GCC 13.3.0 and Clang 18.1.3, x86-64, CMake `Release`. One machine, one CPU model, one OS.

**Method 1, secret-dependent branches and addresses (`tests/crypto_ct_valgrind.cpp`, `scripts/crypto_ct_matrix.sh`, ctest `crypto_ct_valgrind`).** Every secret is marked as undefined memory under valgrind 3.22; valgrind then reports a conditional branch or a memory address that depends on it. Secrets marked: X25519 scalars, Kx secret keys, Ed25519 seeds and secret keys (and, in a second case, the message), ChaCha20 and Poly1305 keys and data, AEAD keys, data and AAD, BLAKE2b keys and messages, SHA-2 messages, both sides of `ConstantTimeEqual`. Outputs are marked public afterwards. Two values are public by design and are marked inside the library with `OKAY_CT_PUBLIC` (compiled in only for this audit): the accept/reject answer of a tag comparison, and whether an X25519 result is all zero (a small-order peer point; libsodium reports it the same way). Two **positive controls** (a branch on a secret byte, a table index from a secret) must be reported, otherwise the harness is blind; they are. The matrix: GCC and Clang x `-O0 -O1 -O2 -O3 -Os -Ofast` x both field implementations = 24 builds; **all 24 report nothing** after the fix below. Not run: `-march=native`, LTO, 32-bit, MinGW, MSVC, macOS, WebAssembly, another CPU.

**Findings of method 1 (fixed).** (a) Clang at `-O1`, `-O2`, `-O3` and `-Os` compiled the mask select `(a & mask) | (b & ~mask)` in the final reduction of the 16-bit field (`ToBytes`) into `test; js`, a real branch on whether the value was at least p. The same code is in the field on `main`: I built the unchanged sources of the commit this branch started from, with only the audit hook added, and the audit reported X25519, X25519Base, Kx, `Ed25519PublicFromSeed` and both `Ed25519Sign` cases under Clang `-O2` and `-O3` (none under GCC). The branch is taken for practically no value, so the information leaked is tiny, but a branch on a secret-derived value is what the audit is for. (b) The same pattern appeared in the Barrett correction step of the new scalar code written during this work (`CondSubL`), and was caught by the same audit before it was committed. Fix for both, and for every other mask select (`CSwap`, `Select`, the Poly1305 final select): the mask goes through `ct::Barrier` (an empty inline-asm statement, a volatile round trip on MSVC), which stops the compiler from seeing through it. That is a mitigation verified per compiler and flag set by this audit, **not a guarantee**: a compiler we did not run, or a newer one, can undo it, which is why the audit should be in CI.

**Method 2, a statistical timing test (`tests/crypto_timing.cpp`, ctest `crypto_timing_smoke` only checks it runs).** For each operation two classes of secret input are timed in random interleaving with `rdtsc` (serialised with `lfence`), pinned to one core with `taskset`: a fixed value and fresh random values (fixed random value, and a second run with an all-zero or all-ones fixed pattern), with 300,000 measurements per row and mode (about 150,000 per class). Welch's t-test is computed on the raw times, on the times cropped at the 99th, 95th, 90th, 75th and 50th percentile and on centred squares; the largest |t| is reported; |t| below 4.5 is "no leak detected", above 10 "leak". Rows: `ConstantTimeEqual` (equal against differing at a random byte; differing at the first against the last byte of a 16-byte tag), X25519 (two fixed scalars), X25519Base, Kx, Ed25519 public key from a seed, Ed25519 sign (two fixed keys), XChaCha20-Poly1305 seal (fixed key and data against random) and open with a forged tag (first against last tag byte), ChaCha20, Poly1305 (two patterns), BLAKE2b keyed, SHA-512, plus libsodium's SHA-512 and X25519 through the same harness as a reference. Two positive controls (an early-exit comparison and a multiply skipped for zero limbs) and an A/A control (both classes random: nothing to find, so any flag is a false alarm of the machine) run in the same binary.

Result of the final run: **all real rows |t| <= 3.11** (fixed random value against random: largest 2.94; fixed pattern against random: largest 3.11), the A/A control rows <= 3.08, libsodium's reference rows 0.80 to 2.12, the two positive controls 14,644 and 3,856 (flagged as they must be). Class medians agree to within 4 cycles on the roughly 70,000-cycle X25519 and 66,000-cycle Ed25519 signing runs. That is all it says: on this virtual machine, with these classes, at this sample size, no difference was detected. It says nothing about other CPUs, other compilers, other input classes, or microarchitectural attacks; a leak that depends on a value pattern we did not try is not excluded. Run it again on a quiet bare-metal machine with millions of measurements before relying on it.

**Re-run after the round-2 speed work (2026-11-13).** The valgrind matrix (`scripts/crypto_ct_matrix.sh`: GCC and Clang x `-O0 -O1 -O2 -O3 -Os -Ofast` x both fields = 24 builds) was run again on the changed code, with a new case for `Ed25519SignTrustedPk`: all 24 "all cases as expected" (nothing reported, both positive controls reported). The timing test was run again on the signing rows, pinned to one core, on a machine that was NOT quiet (other builds were running). First with 150,000 measurements per row and mode: all |t| <= 3.71 (the `Ed25519SignTrustedPk` row, the largest of about ten statistics per row); then again with 400,000 per row and mode (`taskset -c 3 crypto_timing 400000 Ed25519`): `Ed25519PublicFromSeed` |t| 1.97 and 1.02, `Ed25519Sign` with two fixed keys 0.96, 1.77, 0.62 and 1.25, `Ed25519SignTrustedPk` 2.49 and 1.08, A/A control 2.28; all under 4.5, "no leak detected", class medians within 2 cycles of each other on the roughly 62,000-cycle signing runs. The 3.71 did not repeat. Neither run is the long quiet-machine run the earlier paragraph asks for. Signing changed in only two ways: the two output points are encoded with one shared inversion (a Montgomery trick: field multiplications, constant time), and the public-key tail variant was added; `BaseMult` and its constant-time table lookup are unchanged.

**A measurement artefact worth knowing about.** The first full runs of this harness flagged rows with |t| up to 385: ChaCha20, XChaCha20, SHA-512 (also libsodium's own SHA-512 in the reference row) and, in the A/A control, BLAKE2b. The cause was the harness: input preparation branched on the class, which left different cache and predictor state in front of the timed call. After preparation was made branch-free (a mask instead of a branch) every row, the A/A control included, fell below 4.5. The lesson for the reviewer: a flag on one row proves little unless the A/A control for that row is clean, and a clean run proves little unless the positive controls were flagged. The harness prints the quantiles of both classes whenever it flags something.

**By reading** (the author's reading of the code, not an independent one): X25519 uses only `CSwap` on the secret scalar bits in a fixed 255-step ladder; Ed25519 signing and key generation use `BaseMult`, which recodes the scalar into 64 signed 4-bit digits and, per digit, reads all eight entries of that window's table and selects with masks (the lookup index is computed arithmetically); the nonce and challenge reductions and the multiply-add are fixed-length Barrett arithmetic with masked corrections; ChaCha20 and BLAKE2b are add, xor and rotate only; Poly1305 and the field code use fixed loops with masks; the tag comparison is an OR-accumulated difference and the AEAD decrypts only after the tag has verified (the accept/reject decision is public by design). Variable-time code exists on purpose and works on public data only: Ed25519 verification (point decoding, the interleaved width-5/width-7 non-adjacent-form double-scalar multiplication, the blocklist comparison for small-order encodings, the canonical checks; all inputs are the signature, the message and the public key) and the table construction at first use. The round-2 speedup made verification MORE variable-time (digit-dependent additions, doublings that skip a coordinate), which is acceptable only because nothing secret reaches it: please confirm that claim for every caller, in particular that no caller verifies a signature whose message or key is secret. Not checked: variable-latency instructions on other CPUs (the code has no division and no variable shift, but that was not machine-checked), and the compiled code of anything except through the two methods above.

### 4.2 Speed (2026-11-13, same machine, `bench_crypto`, microseconds per operation)

| Operation | Before | After | libsodium 1.0.18 |
|---|---|---|---|
| X25519, variable point | 358.05 | 34.93 | 39.71 |
| X25519, public key (base) | 370.41 | 34.48 | 34.02 |
| Ed25519 public key from seed | 894.43 | 15.23 | 15.73 |
| Ed25519 sign, 64-byte message | 1711.89 | 32.72 | 17.08 |
| Ed25519 verify | 1739.54 | 71.12 | 45.05 |
| Kx client session keys | 437.79 | 34.62 | 38.47 |
| XChaCha20-Poly1305 seal, 1 KiB | 3.48 | 3.09 | 0.99 |
| BLAKE2b-512 of 1 KiB | 2.42 | 2.06 | 1.17 |
| SHA-512 of 1 KiB | 4.91 | 4.03 | 2.51 |
| SHA-256 of 1 KiB | 4.44 | 4.26 | 3.41 |

"Before" is the unmodified code on `main` (0.5 s per row, not pinned); "after" is this branch (1 s per row, pinned to one core), so the two columns differ slightly in method. The speedup comes from: five 51-bit limbs with a 128-bit product instead of sixteen 16-bit limbs (the old field stays as the portable fallback), a dedicated squaring, fixed addition chains for inversion and the square-root exponent instead of a 255-step power loop, a dedicated point-doubling formula, a precomputed base-point table (32 windows x 8 multiples, built from the curve constants on the first Ed25519 call, about 1.1 ms once) read with a constant-time lookup, Barrett reduction modulo the group order instead of a bit-by-bit loop, and a 4-bit windowed multiplication for verification. X25519 is now as fast as libsodium's on this machine; signing takes about twice and verification about 1.6 times libsodium's time (signing recomputes the public key from the seed, as before, for the tamper-safety reason in the header). The 16-bit fallback was not tuned (it only gained the faster chains and the table). AEAD and hashes were not touched.

**Round 2 (same machine, `bench_crypto`, microseconds per operation; before = the commit this round started from, after = this branch; libsodium measured in the same run):**

| Operation | Round-2 before | Round-2 after | libsodium in the same run (before / after) |
|---|---|---|---|
| Ed25519 verify | 77.66 | 41.91 | 47.69 / 46.51 |
| Ed25519 sign, default (`Ed25519Sign`, recomputes the public key from the seed) | 33.31 | 31.66 | 18.24 / 17.04 |
| Ed25519 sign, stored public key (`Ed25519SignTrustedPk`, libsodium's behaviour) | n/a | 18.61 | n/a / 17.01 |
| Ed25519 public key from seed | 17.45 | 16.85 | 17.33 / 18.97 |
| X25519 (unchanged) | 36.19 | 35.36 | 38.55 / 41.05 |

What changed in verification: one interleaved (Straus/Shamir) double-scalar multiplication [S]B + [h](-A) over width-7 and width-5 non-adjacent forms instead of two separate multiplications, a precomputed table of 32 odd multiples of the base point (built on first use with the signing table; about 0.1 ms more once), doublings that skip the T coordinate when no addition follows, and the small-order checks done by comparing the encodings with the seven-entry blocklist instead of decoding R and the key a second and third time (each decode is a field square root). Verification is now about 10 percent faster than libsodium's on this machine. The signing speedup is small by design: signing is two fixed-base multiplications (the nonce point and the public key), libsodium does one because it trusts the public key stored in its 64-byte secret key. Trusting it has a known failure mode (a wrong tail lets an attacker who can make the signer sign twice recover the secret scalar), so `Ed25519Sign` keeps recomputing it; `Ed25519SignTrustedPk` is the libsodium-equivalent for callers that only ever pass a key made by `Ed25519SeedKeypair`, and it runs at libsodium's speed. No caller uses it yet; choosing that is a decision for the reviewer and the owner (section 7, item 8). The fixed-base table with signed 4-bit digits and the batch inversion (one inversion for the two encodings of a signature) were already in or are now in; neither is the bottleneck.

### 4.3 Fuzz campaign (2026-11-13)

`scripts/crypto_fuzz_sanitized.sh` compiles the library and both fuzz drivers with GCC 13 under AddressSanitizer and UndefinedBehaviorSanitizer (`-O1`, no recovery) and runs the built-in mutating driver: seed 1 with 200,000 iterations of `fuzz_crypto` and 20,000 of `fuzz_net_crypto`, seed 7 with 1,000,000 and 100,000. No sanitizer report, no violated property. The fuzzer was checked for sensitivity by breaking the code on purpose: a tag comparison that ignores the last byte was caught within 20,000 iterations; removing the S < L check was caught only after a structured "S + L" mutation was added to the driver (random edits do not find it, so the unit tests, not the fuzzer, are what cover that case). **libFuzzer itself was not run**: the Clang 18 installed here has no fuzzer or sanitizer runtime libraries, so `-DOKAY_CRYPTO_LIBFUZZER=ON` is configured but untested; there is no coverage-guided campaign, no corpus and no coverage report. A mutation driver that starts from nothing is weaker than a coverage-guided one.

### 4.4 What is NOT verified

- **No outside review.** Everything above was done by the team that wrote the code.
- **Constant time is evidence, not proof**: one VM, GCC 13 and Clang 18, x86-64 only (section 4.1). No Windows (MinGW or MSVC), macOS or WebAssembly build or timing run, no other CPU, no `-march=native` or LTO build, no bare-metal run, no inspection of the generated machine code beyond the two methods.
- **No Windows (MinGW or MSVC), macOS or WebAssembly run at all.** The code avoids platform extensions, and `Random.cpp` has Windows (`BCryptGenRandom`), macOS and BSD (`getentropy`) branches, but only the Linux branch (`getrandom`) has ever executed. The 16-bit field that MSVC and WebAssembly would use is tested on Linux only (`crypto_fe16`).
- **Not in CI:** no libFuzzer job, no sanitizer job for these tests, no valgrind audit job, no `OKAY_OWN_CRYPTO=ON` job. Runs were by hand.
- **No coverage-guided fuzz campaign and no coverage report** (section 4.3).
- **Speed:** verification is now at libsodium's speed, default signing still takes about 1.9 times libsodium's (section 4.2, a design choice); the 16-bit fallback field is untuned; a possible denial-of-service amplification point (verification costs about 42 microseconds, a handshake about 36) has not been analysed.
- **The round-2 tests are one machine, one libsodium version (1.0.18, the system package of the test machine), one TweetNaCl-derived port.** Agreement with libsodium 1.0.18 does not show agreement with other versions; the Wycheproof vectors are the version-independent part.
- **Structured input sets are our guess at the edge cases** (section 3, differential test). A property-based generator that knows the field representation, or a symbolic execution of the field code, would find different cases.
- **Memory hygiene of callers:** `SecureChannel` holds the Kx session keys in plain members (`m_sk`, `m_rx`, `m_tx`) and a read of the header shows no destructor and no `MemZero` or `sodium_memzero` for them. The same holds for the libsodium backend. A reviewer should say whether that matters.
- **The base-point table is built at run time on first use** from the curve constants (not pasted in); nothing independent checks the table against published values except that signatures and public keys match libsodium and the RFC vectors.
- **No formal verification, no differential test against a third implementation of XChaCha20-Poly1305 apart from libsodium.**
- No Argon2, secretbox, sign-open, Ed25519ph or batch verification: not implemented, nothing uses them.

## 5. Reviewer checklist

Please mark each item pass, fail or not assessed, with a pointer.

1. **Constant time.** Any branch, early exit, table index or variable-time instruction (division, variable shift, `memcmp`) on secret data in: `Fe25519_64.hpp` and `Fe25519_16.hpp` (the 16-bit one uses arithmetic right shifts of negative `int64_t` in carry and reduce; the 51-bit one is unsigned), `X25519.cpp` (ladder, conditional swap), `Ed25519.cpp` (`BaseMult` and its table lookup, `Scalar25519.hpp` Barrett reduction and `CondSubL`, `MulAdd`), `Poly1305.cpp`, `ChaCha20.cpp`, `Aead.cpp`. Does `ConstantTimeEqual` survive optimisation? Is the tag compare the only secret-dependent comparison? Are `ct::Barrier` on the masks and the audit of section 4.1 enough, or do you want the selects written differently?
2. **Key handling.** Secret keys, seeds, scalars, one-time keys (`otk`), HChaCha20 subkeys and the Kx hash: are all copies wiped on every path (including early returns)? Does `Ed25519Sign` really ignore the second half of `sk`, as documented? Are session keys held longer than needed in `SecureChannel`?
3. **Nonce reuse.** `SecureChannel` uses a random 24-byte nonce per message with one key per direction. Is that sound for the expected volume per session? Is any code path able to seal two messages with the same key and nonce (copying a channel object, the `m_nextSecure` to `m_secure` handover in `NetworkManager`, `Seal` being `const` with a mutable sequence counter)? Is the ChaCha20 32-bit block counter bounded for the maximum message size? (Round 2: `ChaCha20Xor` now refuses a wrap and the AEADs refuse or stop; please check the arithmetic in `ChaCha20.cpp` lines 44 to 51 and the callers in `Aead.cpp`.)
4. **Small-order and invalid points.** X25519 must report an all-zero result (we return false but still write `out`); Kx must reject it and zero the keys. Ed25519 must reject small-order R and A and non-canonical A (round 2: small order is now decided by comparing the encoding with a seven-entry blocklist, `HasSmallOrderEncoding`, instead of decoding and doubling; the equivalence argument is in the comment above it, and the port in `shared/AntiCheatCrypto.hpp` still decodes, so it is an independent check). Does the decode accept a non-canonical y (the comment says callers check it) in every caller?
5. **S < L and malleability.** `ScalarIsCanonical` before verification; verification computes [S]B + [h](-A) in one interleaved multiplication (`DoubleScalarMultPublic`: the non-adjacent-form recoding `Wnaf`, the table of odd multiples, the signed additions) and compares the 32 re-encoded bytes with R (no cofactor multiplication); check the recoding for a digit that overflows `int8_t` or a missed final carry, and the doubling that leaves T unset (`PointDoubleMaybeT`) for a use of the stale T; confirm there is no accept/reject divergence with libsodium or RFC 8032 that matters here.
6. **Memory zeroing.** `MemZero` (volatile stores and an empty `asm` barrier on GCC and Clang; no barrier on MSVC): is it enough on every compiler we support (GCC, MinGW, Clang, MSVC, Emscripten)? `Poly1305::Final` and `Blake2b::Final` wipe state; do destructors do the same on abandoned objects?
7. **Random source.** `RandomBytes` uses `getrandom` (Linux), `getentropy` (macOS, BSD, Emscripten), `BCryptGenRandom` with the system-preferred RNG (Windows), `/dev/urandom` as the last fallback, and aborts if the OS gives nothing. Check the 256-byte chunking, `EINTR` handling, early-boot behaviour, and the Emscripten branch (`getentropy` in a browser).
8. **API misuse.** Overlapping `in` and `out` (documented as allowed for Seal), null pointers with zero length, the 16-byte minimum in `Open`, `n + 16` buffer sizing by callers, integer overflow in length arithmetic (`aadLen`, `n` as `uint64_t` in the length block), the replay window arithmetic in `SecureChannel::AcceptSeq`, and the `SessionTicket` parser's handling of attacker-controlled lengths before `Verify`.
9. **Protocol construction.** The Kx derivation (transcript is `q || client_pk || server_pk` only), the join key message that Ed25519 signs for host pinning (`JoinKeyMessage` in `NetworkManager.cpp`), the ticket key binding (SHA-256 of the public key), and whether the sealed handshake is open to downgrade (there is a plaintext join fallback, `m_legacyJoin`).
10. **Hash correctness at edges.** SHA-256, SHA-512 and BLAKE2b at block boundaries, length counters above 2^32 bytes, keyed BLAKE2b.
11. **Build flags.** Behaviour under `-O2`, `-O3`, `-Ofast`/`-ffast-math` (should not matter), LTO, 32-bit targets, and signed shift and overflow assumptions in `Fe25519.hpp` (`int64_t` right shift of negatives is arithmetic in practice; undefined or implementation-defined depending on language standard).

## 6. Build and run

Requires CMake, Ninja (the script uses it), a C++17 compiler. libsodium (`libsodium-dev`) is needed for the libsodium side and for the cross-check; Python 3 with an independent cryptography module (see `tests/scripts/crypto_xcheck.py` for the exact import) is optional for the Python cross-check.

```
# The archive for a reviewer (outside the repository; never committed), with a manifest and a 15-minute README:
scripts/make_crypto_review_bundle.sh /some/scratch/okay-crypto-review.tar.gz

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
scripts/crypto_fuzz_sanitized.sh 200000 1       # both fuzz drivers under ASan+UBSan, no CMake build needed

# Round 2: every Wycheproof vector, and the large differential test (needs libsodium; the ctest is a 1,500-case smoke run)
ctest -R 'crypto_wycheproof|crypto_difftest' --output-on-failure
OKAY_WYCHEPROOF_DIR=/path/to/wycheproof/testvectors_v1 ./build/tests/test_crypto_wycheproof   # a fresh clone's files instead of the copy
OKAY_DIFF_N=1000000 ./build/tests/crypto_difftest        # about 15 minutes on one core (N=1,000,000: see section 3); OKAY_DIFF_SEED=... changes the seed
cmake -S . -B build-asan -DOKAY_SANITIZE=address,undefined && ninja -C build-asan crypto_difftest test_crypto_wycheproof && OKAY_DIFF_N=100000 build-asan/tests/crypto_difftest

# Speed and the two constant-time audits (section 4):
./build/tests/bench_crypto 1.0                  # microseconds per operation, next to libsodium when installed
scripts/crypto_ct_matrix.sh                     # valgrind audit: every compiler, -O0..-Ofast, both field versions
taskset -c 2 ./build/tests/crypto_timing 300000 # statistical timing test; quiet machine, one core; ~7 minutes
python3 scripts/gen_embedded_fonts.py --check   # (not crypto) the embedded fonts
```

Test names: `crypto`, `crypto_wycheproof`, `crypto_difftest_smoke`, `crypto_field`, `crypto_fe16`, `crypto_ct_valgrind` (needs valgrind), `crypto_sodium_xcheck`, `crypto_python_xcheck`, `crypto_fuzz_smoke`, `crypto_fuzz_net_smoke`, `crypto_timing_smoke`, `net_crypto`, `net_crypto_own`, `net_crypto_sodium`, `anticheat_integrity_ownlib`, `anticheat_integrity_portable`, plus the `net*` and `anticheat*` families. Switch on our code in an application build with `-DOKAY_OWN_CRYPTO=ON` (then libsodium is not linked at all). Binary paths above may differ with the generator; ctest names are stable.

## 7. Known issues we want fixed or decided

These are findings we already know about. They are not open questions to the reviewer about whether a problem exists; we want each one fixed, or a decision recorded that it is acceptable, before sign-off.

1. **`SecureChannel` never zeroes its key material.** `engine/include/okay/Net/SecureChannel.hpp` holds the ephemeral secret key and both session keys in plain members (`m_sk`, `m_rx`, `m_tx`, and `m_pk`), the class has no destructor, and nothing calls `MemZero` (or `sodium_memzero`) on them. The same is true under the libsodium backend. Fix or decide: add a destructor and a wipe of the secret key after the keys are derived, and make sure copies of the channel (the `m_nextSecure` to `m_secure` handover in `NetworkManager`) are wiped too.
2. **Plaintext join fallback (`m_legacyJoin`).** `NetworkManager` can fall back to an unsealed plaintext join: after `kSealedJoinTries = 4` unanswered sealed hellos the client sends the plaintext Join (unless a host key is pinned, which refuses to join without encryption). A network attacker who drops the first four hellos may be able to push a client that has not pinned a host key onto the plaintext path. Fix or decide: which cases must refuse instead of falling back, and whether the fallback stays at all.
3. **Three SHA-256 implementations and two Ed25519 implementations in the tree** (`okay::crypto`, the account `Sha256` under the HMAC, and the TweetNaCl port; `okay::crypto` and the port for Ed25519). Fix or decide: move the HMAC, the relay cookie, the clock tag and the manifest signing onto `okay::crypto` and delete the others, once the review accepts `okay::crypto`.
4. **`Seal` is `const` with mutable counters** (`m_txSeq`, `m_rxMax`, `m_window`): decide whether it is safe to call from more than one thread, and document it.
5. **The compiler can undo a masked select.** Found by the audit in section 4.1 (Clang turned two into branches, one of them in the code on `main`); mitigated with `ct::Barrier`, not proven. Decide whether that is acceptable, whether the selects should be written differently (for example per-limb arithmetic the compiler cannot fold), and what CI job keeps it caught.
6. **Two intended declassifications**: the tag-comparison result and "is the X25519 result all zero" (so `Kx` refuses a small-order peer key in variable time relative to a good key). Decide that both are acceptable (libsodium has the second).
7. **The 16-bit field fallback is untuned and runs only on Linux in tests**, yet it is what MSVC and WebAssembly builds would use. Decide whether those targets should use `okay::crypto` at all before it is measured there.

8. **`Ed25519Sign` against `Ed25519SignTrustedPk`.** The default recomputes the public key from the seed (twice libsodium's cost, immune to a corrupted or substituted key tail); the second uses the stored tail like libsodium (same speed, one more way to misuse). Decide which one the net layer and the host-identity code should call; today both `NetCrypto.hpp` and `shared/AntiCheatCrypto.hpp` call the safe one, and the anti-cheat wrapper derives a keypair on every signature (three fixed-base multiplications where one or two would do; signing is rare, so we left it).
9. **ChaCha20 counter guard and `Seal` stopping the process.** `Seal` has no error channel, so a message of 2^32 blocks or more makes it call `std::abort()`, as libsodium does. Decide whether a hard stop is the right failure for a network engine, or whether `Seal` should return a status (an API change touching `SecureChannel`).
10. **Verification is variable time and so is the new table build.** Decide that no caller verifies with secret inputs (section 4.1) and that the first-call table build (about 1.2 ms, one-time, not secret-dependent) is acceptable on a latency-sensitive thread.

## 8. Questions we want answered

1. Is the code constant time as compiled? Section 4.1 is what we measured; what further measurement would you accept, on which compilers, flags and CPUs, and what is the minimum matrix before we could ship (GCC and MinGW on x86-64 for Windows and Linux builds; Clang for macOS; WebAssembly)?
2. Are the two field implementations (51-bit limbs with `__int128`; 16-bit limbs in `int64_t` with arithmetic right shifts) acceptable on every compiler we support, are the limb bounds documented in `Fe25519_64.hpp` right, and is the Barrett scalar reduction (`Scalar25519.hpp`) constant time and correct?
3. Is a random 24-byte XChaCha nonce per message under a per-direction key acceptable for our traffic, or do you want a counter nonce?
4. Is the Ed25519 verification (strict, byte comparison of the re-encoded point, no cofactor, S < L, small-order rejection, canonical key) the right strictness for session tickets, host pinning and the integrity manifest? Any case where accepting libsodium's exact behaviour matters more than being strict?
5. Beyond known issues 1 and 4: anything else about how `SecureChannel` holds keys and counters you would change?
6. The relay MAC uses `okayac::detail::HmacSha256` from `shared/AntiCheatChecks.hpp`, which is separate code from `okay::crypto::Sha256`. Should it move onto the reviewed primitives, and is that HMAC itself sound?
7. Beyond known issue 2: is there another downgrade path we have missed?
8. What should we add before we ask you to sign off: more fuzz time, a third implementation to compare against, known-answer tests from another suite, formal methods on the field arithmetic?
9. What would you want in CI to keep it safe afterwards (sanitizers, a libFuzzer job, a timing check)?
10. Anything you would rather we did not implement ourselves at all, and instead kept using from a maintained library?

## 9. What only an outside reviewer can judge

These are the things this package cannot establish, however many tests we add, because the team that wrote the code cannot be independent of it, or because they need hardware, toolchains or expertise we have not used.

1. **Constant time as compiled, on the compilers and CPUs that ship.** We ran GCC 13 and Clang 18 on one x86-64 virtual machine, with valgrind and a statistical test. Not run: MinGW and MSVC (the Windows builds), Clang on macOS (arm64 included), Emscripten (the 16-bit field), `-march=native`, LTO, bare metal, other CPU models, 32-bit. Variable-latency instructions (an `imul` that is not constant time on some cores, 128-bit multiplication lowered to library calls on 32-bit targets) cannot be seen by our two methods. A reviewer with tooling for the generated machine code (binary-level analysis, cycle-accurate measurement) is the only way to close this.
2. **Side channels beyond timing:** cache-line and branch-predictor attacks across tenants (we read all eight table entries per window, but nobody has measured it), power and fault attacks (out of scope for a game, but say so), speculative execution.
3. **Protocol composition in `shared/` and the net layer.** The primitives can all be correct while the use is not: the Kx transcript (`q || client_pk || server_pk`, no identity binding), the sealed-join handshake and the plaintext fallback (known issue 2), random 24-byte nonces under one key per direction (birthday bound against traffic volume), the replay window of 64, the ticket format and its key binding, host-key pinning (`JoinKeyMessage`), relay cookies built on a hand-written HMAC, and the anti-cheat manifest signature. Our tests establish that the code does what we intended, not that what we intended is sound.
4. **Whether the strictness decisions in section 3.1 are the right ones** for signatures that other software might also produce or check (cofactorless verification, rejecting small-order keys, accepting mixed-order keys).
5. **The soundness of the field and scalar arithmetic beyond tests.** We compare two independent implementations of the field and a bit-by-bit reference for the scalar; a reviewer can check the limb-bound argument in `Fe25519_64.hpp`, the carry analysis in `Fe25519_16.hpp`, the Barrett constants and the recoding in `Wnaf`, which random testing can miss when the failing inputs are astronomically rare (for example a carry that occurs once in 2^60 inputs). Formal verification of the field code is the strongest option and is outside what we can do.
6. **Whether the test suite itself is sufficient**: that the structured input set of the differential test covers the dangerous cases, that the fuzzers reach the code they claim to (no coverage-guided run has been done), and that the Wycheproof "acceptable" pins are the right decisions.
7. **Key handling in the surrounding code** (`SecureChannel` never zeroes its keys; callers hold secrets in ordinary memory) and the operational practice around the signing keys.
8. **Whether any of this should be written by us at all** (question 10).

## 10. Request text (ready to send; no names, no secrets)

> Subject: Request for a cryptographic code review of a small, self-contained C++ library (about 1,850 lines)
>
> We maintain a game engine that currently depends on libsodium for network encryption and on a ported copy of TweetNaCl for Ed25519 signatures. To remove those dependencies we wrote our own implementation of exactly the primitives we use: XChaCha20-Poly1305 and ChaCha20-Poly1305, X25519 (with the libsodium-style key-exchange derivation), Ed25519 (strict verification), BLAKE2b, SHA-256, SHA-512 and an OS random wrapper. It is about 1,850 lines of C++17 plus a 190-line interface, no assembly, no SIMD, no external dependencies. It is off by default and will stay off until an outside review has passed.
>
> What we have already done: known-answer tests from the RFCs and every vector of the relevant Project Wycheproof files (1,927 vectors); differential tests against libsodium and against our old Ed25519 port on millions of random and structured edge-case inputs, both directions; a valgrind secret-dependence audit over 24 compiler, flag and field-representation combinations; a statistical timing test; sanitizer builds and fuzzing. All of it is reproducible with the commands in the package, and all of it was done by us on one Linux x86-64 machine.
>
> What we are asking for: (1) a review of the arithmetic and the constant-time properties of the secret-key paths (X25519, Ed25519 signing and key generation, the AEAD and the field, scalar and table code behind them), including the compiled output on the compilers we ship (GCC, MinGW, Clang, MSVC, Emscripten); (2) a review of the Ed25519 verification strictness choices and the new interleaved double-scalar multiplication; (3) a look at how the primitives are used in our transport and handshake; (4) your view on what else we should test before relying on it.
>
> The review package (scope with exact files and line counts, threat model, what is tested and how to reproduce each result, known gaps, known issues we want decided, specific questions) is `docs/crypto_review_package.md` in the repository. An hour with it should be enough to decide whether you can help, and what you would need from us. Please tell us your availability, how you would scope and price it, and whether you would prefer a fixed snapshot of the code (we will tag one).

## 11. Cost and scheduling

We do not know what an outside review costs or how long it takes to schedule: no reviewer has been approached and no quote has been requested. `docs/crypto_review_request.md` section 6 gives a rough effort estimate (an estimate by us, in working days, not a quote and not a price). The package is built so that a reviewer can state scope and price after about an hour (section 0); the first step is sending the request text (`docs/crypto_review_request.md` section 1) with the bundle and asking for that.
