# SHA-256: three copies compared, one kept

Owner decision 2026-11-15: "Yes merge duplicates, but make sure to compare and see which one is better" (found by Grok, issue #888).
This page is the comparison, the choice and how the result was checked. Branch `claude/one-sha256`.

All code here is ours. The algorithm is FIPS 180-4, HMAC is RFC 2104 (vectors RFC 4231), PBKDF2 is RFC 8018 (vectors RFC 7914). Nothing was copied from another project.

## The three copies (before)

| # | Where | Used for |
|---|---|---|
| 1 | `okay::crypto::Sha256` / `Sha256Hash`, `engine/src/Crypto/Sha256.cpp` (compiled in `okay_crypto`) | session-ticket key binding (under `OKAY_OWN_CRYPTO`), crypto tests, libsodium cross-check |
| 2 | `okay::account::detail::Sha256` + `Pbkdf2Sha256` in `AccountService.hpp` (header-only), and the hand-written `HmacSha256` in `AntiCheatChecks.hpp` on top of it | local account passwords, **every file checksum** (launcher updater `versions.json`, official-game updater, anti-cheat manifest through `FileSha256`), the dedicated-server token key, the anti-cheat clock and hardware tags |
| 3 | `okay::voice::Sha256` in `launcher/VoiceChat.hpp` | the voice room key, once per room |

## Comparison

Speed: Release (`-O3`), g++, x86-64, best of 9 runs, run twice on a shared machine (other builds were running, so absolute numbers move about 10 percent; the order did not change in either run). `tests/sha256_compare bench`.

| | 1 crypto | 2 account | 3 voice | **new** (`Crypto/Sha256.hpp`) |
|---|---|---|---|---|
| Correct (FIPS/RFC vectors, lengths 0, 55, 56, 63, 64, 65, 119, 120, 127, 128, 1000, million-'a', multi-block) | yes | yes | yes | yes |
| Equal to Python hashlib, 2,000 random messages 0 to 70,000 bytes (plus all the edge lengths) | yes | yes | yes | yes |
| Streaming with random split points (also empty updates) | yes | yes (byte at a time) | no streaming API | yes |
| One-shot 1 MB (MB/s) | 297 to 300 | 263 to 269 | 231 to 244 | **363 to 369** |
| One-shot 50 MB (MB/s) | 293 to 299 | 267 to 268 | 208 to 213 | **364 to 366** |
| Streaming 50 MB in 64 KB pieces (what `FileSha256` does) (MB/s) | 290 to 300 | 220 to 229 | n/a | **362 to 371** |
| Streaming 8 MB in 16-byte pieces (MB/s) | 289 to 299 | 249 to 261 | n/a | **349 to 355** |
| 1,000,000 messages of 64 bytes (s) | 0.48 to 0.50 | 0.53 to 0.54 | 0.51 to 0.54 | **0.43** |
| Heap used hashing 1 MB | 0 | 0 | about 4 MB (copies the message into a vector, which grows by doubling) | 0 |
| Object size | 112 B | 112 B | no object | 112 B |
| API | `Update`, `Final`, one-shot; no hex helper | `Update(bytes/string)`, `Digest()`, hex helper | one-shot over `std::string` only | `Update`, `Final`, one-shot, `FinalWith32`, `Wipe`; HMAC (one-shot and streaming) and PBKDF2 (any output length) next to it |
| >4 GB message (length field past 2^32 bytes) | correct | correct | not tried (needs 4 GB in a vector; same 64-bit length logic) | correct |
| Wipes state | state and buffer in `Final` | no (it only resets the words; the buffer keeps the last bytes) | no (the message copy and schedule are left on the heap and stack) | state and buffer in `Final`, `Wipe()` for an abandoned object; PBKDF2 and HMAC wipe pads, keys and intermediates |
| Secret-dependent branch or table index | none | none | none | none (only the message length, which is public, steers the control flow) |
| Mutable static state / threads | none | none (a function-local `const` table) | none | none (`constexpr` tables only), so any number of threads may hash at once |
| Header-only or compiled | compiled (`okay_crypto`); the account, updater and anti-cheat code would have had to link it | header-only | header-only | **header-only**: no link change for any program, the compiler inlines the block function |
| `<mutex>` / `<thread>` | none | none | none | none |

How the new one is faster: whole 64-byte blocks are hashed straight from the caller's buffer instead of being copied into the block buffer first (copy 2 copied byte by byte, copy 1 copied then hashed), the message schedule is a 16-word ring instead of a 64-word array, and the 64 rounds are unrolled eight at a time. It uses no processor-specific instructions and no assembly.

Length counter: the byte count is 64 bits and the bit count is that times 8, so the field wraps after 2^61 bytes. FIPS 180-4 allows messages below 2^64 bits, so nothing smaller can be wrong. Checked with a real stream of 4,299,173,945 bytes (past 2^32): new, copy 1 and copy 2 all give `4dc3e8d0...3ec6`, which equals Python hashlib on the same bytes (`tests/sha256_compare big`, 45 seconds).

### PBKDF2 and HMAC

| | old PBKDF2 (copy 2) | old HMAC (`AntiCheatChecks.hpp`) | new `Pbkdf2HmacSha256` / `HmacSha256` |
|---|---|---|---|
| Equal to Python `hashlib.pbkdf2_hmac` / `hmac`, random passwords and keys 0 to 150 bytes (covers the key longer than 64 bytes path) | yes, 400 cases | yes, 1,000 cases | yes, same cases |
| RFC 4231 HMAC test cases 1 to 7 | not tested before | yes (checked in `test_sha256_unify`) | yes |
| RFC 7914 section 11 PBKDF2-HMAC-SHA256 vectors (64-byte outputs) | only the first 32 bytes were possible (one block only) | n/a | yes, both |
| Output length | exactly 32 bytes (one block) | 32 | any length (several blocks) |
| Cost per iteration | 2 compressions (pad states computed once) | 4 compressions plus byte-at-a-time hashing per call | 2 compressions, with the state kept as words so there is no byte shuffling |
| 600,000 rounds (the account service's cost), seconds | 0.258 to 0.265 | n/a | **0.213 to 0.235** |
| Wipes key and intermediates | no | the padded key only | yes, all |

Which is better: the algorithm in copy 2's PBKDF2 (pad states computed once, two compressions per round) is the right one and nothing in `okay::crypto` had a PBKDF2 or an HMAC at all, so it was **moved** into `okay::crypto` (not deleted) and generalised (any output length, streaming HMAC, wiping), with the RFC vectors added. The account service's cost is `kPasswordRounds = 600000` (a stored record may claim 1,000 to 20,000,000; older records are the 50,000-step SHA-256 chain, `HashPassword`, and are upgraded on the next sign-in). The anti-cheat HMAC is called once per tagged message (clock challenge, hardware tag, relay).

## Choice

Copy 1 was the base as expected, and it was verified (it is correct and it is the one with the KATs and libsodium cross-checks), then made faster and header-only, and extended with HMAC and PBKDF2 from copy 2's ideas. Copy 2 and copy 3 became thin wrappers:

| File | Now |
|---|---|
| `engine/include/okay/Crypto/Sha256.hpp` (new, header-only) | `Sha256`, `Sha256Hash`, `HmacSha256`, `HmacSha256Mac`, `Pbkdf2HmacSha256` |
| `engine/src/Crypto/Sha256.cpp` | deleted (the build lists, the fe16 test, the valgrind audit and the review-bundle script were updated) |
| `engine/include/okay/Crypto/Crypto.hpp` | includes the new header; the old declarations are gone; every caller of `okay::crypto::Sha256` is unchanged |
| `AccountService.hpp` | `detail::Sha256` keeps `Update(bytes/string)` and `Digest()` (and the reset after `Digest()`), over `okay::crypto::Sha256`; `Pbkdf2Sha256` calls `Pbkdf2HmacSha256`. `ToHex`, `Sha256Hex`, `HashPassword`, `HashPasswordV2`, `kPasswordRounds` are unchanged, so `FileSha256`, `DedicatedServer.cpp` and the tests need no change |
| `AntiCheatChecks.hpp` | `detail::HmacSha256(string, string)` calls `HmacSha256Mac` |
| `launcher/VoiceChat.hpp` | `voice::Sha256(string)` calls `Sha256Hash` |

## How it was checked

1. `tests/test_sha256_unify.cpp` (ctest `sha256_unify`, runs in about 2 seconds): the three old copies, the old PBKDF2 and the old HMAC are **kept verbatim as references** in `tests/sha256_old_refs.hpp` and compared with the new code on every length 0 to 300 and 300 random lengths up to 20,000, with random split points; FIPS vectors; RFC 4231 cases 1 to 7 (and the old HMAC against them); RFC 7914 vectors; old PBKDF2 against the account wrapper on 120 random inputs; and **password records made by the old code**: a SHA-256-chain record, a 600,000-round record, a long-key record and a non-ASCII record, written to an `accounts.db` that `AccountService::Login` then reads (v1 signs in and is upgraded, v2 signs in, a wrong password fails). Honest note: the old copies were taken verbatim from `origin/main` into the reference file and the stored records were generated from them (and cross-checked with Python); the test itself was written after the new header, not before it.
2. `scripts/sha256_python_diff.py` with `tests/sha256_compare check`: 2,000 digests (all four implementations plus streaming), 1,000 HMACs and 400 PBKDF2 outputs against Python `hashlib` / `hmac` / `hashlib.pbkdf2_hmac`: 0 mismatches (run for three seeds on the way, final run with the Release build).
3. The existing suites that use these functions pass on the new code: `account`, `official_game`, `official_publish`, `launcher_versions`, `self_update`, `update_signature`, `anticheat_integrity`, `anticheat_golden`, `anticheat_checks`, `anticheat_backend`, `crypto`, `crypto_fe16`, `embedded_font`, `net_crypto_own`, `voice_races`, and the engine library (which holds `DedicatedServer.cpp` and `RelayServer.cpp`) builds and links.

## How to try it

Nothing looks different on screen; the point is that nothing breaks and updates hash faster.

1. **Sign in with an existing local account** (launcher, Account tab). An account made by an older version must sign in as before; a record from before the PBKDF2 change is upgraded on that sign-in. Exercised by tests only (`sha256_unify`, `account`) with records made by the old code. Not tried in the real launcher.
2. **Launcher update check** (launcher > Settings or the startup check). Downloaded files are checked against the SHA-256 in `versions.json`; a wrong checksum is refused. Tests only (`launcher_versions`, `self_update`, `official_game`).
3. **Anti-cheat checked server**: join one; the file manifest and the clock tag must still be accepted. Tests only (`anticheat_integrity`, `anticheat_checks`, `anticheat_golden`, `anticheat_backend`).
4. **Voice room**: join the same room from two launchers; they must hear each other (the room key is `SHA-256("okayspace-voice-v1|" + key)`, so both sides must agree). `voice_races` compiles it; the key itself has no direct test. Not tried.
5. **The checks themselves** (a developer machine): build `test_sha256_unify` and `sha256_compare` (`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, then `ninja test_sha256_unify sha256_compare`), run `ctest -R sha256_unify` (expect "0 failures"), `python3 scripts/sha256_python_diff.py build/tests/sha256_compare` (expect "0 mismatches"), `build/tests/sha256_compare bench` (speed table) and `build/tests/sha256_compare big` (a 4.3 GB stream, 45 s).

## Guards added after review

- `Pbkdf2HmacSha256` returns false and writes nothing if `dkLen` needs more than 2^32-1 blocks (the block counter is 32 bits); tested.
- `FinalWith32Words` / `FinalWith32` write the full 64-bit bit count and fall back to the ordinary path when a partial block is pending, so they are correct for any state; tested with prefixes of 0, 1, 33, 64, 70, 128 bytes.
- The valgrind constant-time target and the sanitised fuzz script no longer list the deleted `Sha256.cpp`; their driver programs (`tests/crypto_ct_valgrind.cpp`, `tests/fuzz_crypto.cpp`) include `Crypto.hpp` and call the header's `Sha256`, so the header is still exercised. Run: `scripts/crypto_fuzz_sanitized.sh 20000 1` (no property violated, ASan+UBSan) and the valgrind harness for g++ and clang++ at -O0 and -O2 ("all cases as expected"). The full matrix script was not run.

## Not verified

- Windows / MinGW: not built here. The header uses only standard C++17, no intrinsics, no `<mutex>`/`<thread>`. A first CI run on Windows is the check.
- Real account files from real users: tested with records made by the old code, not with anyone's actual file.
- `okayspace-launcher`, the editor and the player were not rebuilt here (only the headers they include changed; `test_voice_races` compiles `VoiceChat.hpp`).
- Speed on other compilers and CPUs: measured with g++ and clang++ (-O2, -O3) on one x86-64 machine; clang gave the same order. MinGW and ARM were not measured.
- Not independently reviewed (rule 18). SHA-256, HMAC and PBKDF2 are not behind `OKAY_OWN_CRYPTO` (they never were), and no release guard or default changed.
