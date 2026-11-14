# Review request: our own cryptography library (for an outside AI reviewer)

**For the reviewer:** read the files linked in section 3 (all pinned to one commit, so they will not change
under you), then answer in the format in section 4. This page, the linked files and the single-file copy
(`review/okay_crypto_source.txt` on this branch) contain no keys, passwords or personal details.

Snapshot: commit `0a85739583bf206e6d255d18a48a25253f3fabdf` of `kingimann/OkaySpaceGameEngine` (main, 2026-11-14). Raw links below start with
`https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/`.

## 1. Who we are and what this is

We write a game engine in C++17. It currently uses **libsodium** for encryption and signatures. To remove that
dependency we wrote our own implementation of exactly the pieces we use, in the namespace `okay::crypto`:

- XChaCha20-Poly1305 and ChaCha20-Poly1305 (authenticated encryption)
- X25519 (key exchange), with the same key derivation libsodium uses
- Ed25519 (signatures), with strict verification
- BLAKE2b, SHA-256, SHA-512
- an operating-system random wrapper, a wipe helper and a constant-time compare

No assembly, no SIMD, no dependencies. Two arithmetic back ends for the field maths: 51-bit limbs with
`__int128`, and a portable 16-bit-limb version.

**Status: nobody independent has reviewed it.** The people (and AI agents) who wrote it also wrote its tests.
It is off by default; libsodium stays the default in released builds. We want to know where it is wrong.
**Do not assume it is correct. Try to break it.**

What we already did (so you do not repeat it): known-answer tests from the RFCs, the Project Wycheproof
vectors, differential fuzzing against libsodium, ASan/UBSan runs, one valgrind secret-dependence check and one
statistical timing test on a single x86-64 Linux machine with GCC and Clang. All of that agreed. Agreement
with libsodium on the cases we tried does not prove safety against cases we did not try.

## 2. What we want you to do (in this order)

### A. Correctness of the arithmetic
1. `Fe25519_64.hpp` (51-bit limbs): are the stated limb bounds right? Can any intermediate overflow a
   `uint64_t`/`__int128` for inputs the callers can produce? Check add, sub, mul, square, the carry chains,
   inversion (`Fe25519Chain.inc`), and the freeze/reduce-to-canonical step.
2. `Fe25519_16.hpp` (16-bit limbs in `int64_t`, arithmetic right shifts): same questions. Also: is relying on
   arithmetic right shift of a negative `int64_t` acceptable on every common compiler (GCC, Clang, MSVC)?
3. `Scalar25519.hpp`: is the Barrett reduction mod the group order L correct for every 512-bit input, and is it
   free of secret-dependent branches or table indexes?
4. `X25519.cpp`: the Montgomery ladder, the conditional swap, clamping, and the all-zero output check.
5. `Poly1305.cpp`: the 130-bit arithmetic and final reduction; any input length where it goes wrong
   (0, 1, 15, 16, 17 bytes, very large).
6. `Sha256.cpp`, `Sha512.cpp`, `Blake2b.cpp`: padding and length handling at the block edges (for SHA-512:
   0, 111, 112, 119, 120, 127, 128, 129 bytes and multi-block messages), integer overflow in the length
   counter, and `ChaCha20.cpp` counter wrap (we abort at 2^32 blocks like libsodium).

### B. Ed25519 verification rules (`Ed25519.cpp`)
We claim verification is strict: S < L, canonical encodings only, small-order points rejected (keys and R),
re-encode the computed R and compare bytes (no cofactor), identity key plus R = identity plus S = 0 rejected.
1. Does the code really do all of that on every path? Quote the lines.
2. Name any input where we accept something RFC 8032 section 5.1.7 or libsodium (`crypto_sign_verify_detached`)
   rejects, or reject something libsodium accepts. Give the exact bytes if you can build them.
3. Is signing deterministic and correct (nonce derivation from the seed and message, clamping)?
4. Any malformed input (wrong length, zero length, null) that can crash, read out of bounds or hang?

### C. Constant time: secret-dependent behaviour
Look for, in anything that touches a secret key, seed, nonce derivation, shared secret or MAC key:
- branches on secret data, secret-dependent array indexing, early-exit comparisons, variable-time division or
  shifts, loops whose count depends on a secret;
- places where the compiler could turn a masked select into a branch. We use a `ct::Barrier` trick
  (`CtAnnotate.hpp`) for this; is it enough?
- secrets that are not wiped (stack buffers, key copies).
Be explicit about what you checked by reading and what you could not check without a compiler and a CPU.
Please do not claim "constant time" as a verdict from source alone.

### D. How the primitives are used (`NetCrypto.hpp`, `SecureChannel.hpp`, `AntiCheatCrypto.hpp`)
1. Key exchange: the key is derived from `q || client_pk || server_pk` only (no identity binding). Is that
   acceptable here, given the host key is pinned and the join ticket is signed?
2. Nonces: random 24-byte XChaCha nonce per message, one key per direction. Safe?
3. Replay protection: a sequence number inside the ciphertext and a 64-entry window. Any way to replay,
   reorder or desynchronise?
4. `Seal` is `const` but changes counters. Thread-safety problem?
5. The anti-cheat wrapper: Ed25519 comes from libsodium when built in, else from `okay::crypto`; SHA-512 always
   from `okay::crypto`. Any way the two can disagree and cause a wrong accept?
6. The hand-written HMAC-SHA256 (last file in the paste): is it correct, and is the comparison of the result
   done in constant time where it must be?

### E. Known issues we already have (confirm or reject, do not just repeat)
1. `SecureChannel` never wipes its keys.
2. A plaintext join fallback after four unanswered sealed hellos, when no host key is pinned.
3. Several separate SHA-256 copies exist in the wider code base (not in this paste).
4. The compiler may undo a masked select (see C).
5. Two intended variable-time results: the tag comparison result and "is the X25519 output all zero".
6. The 16-bit field back end is untuned.
7. Verification is variable time on purpose (public inputs only); the first call builds a table once.

## 3. The files to read (pinned links)

Read these, in this order (links open the file; the raw link is the same file as plain text):

- [engine/include/okay/Crypto/Crypto.hpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/include/okay/Crypto/Crypto.hpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/include/okay/Crypto/Crypto.hpp))
- [engine/src/Crypto/Util.cpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Util.cpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Util.cpp))
- [engine/src/Crypto/CtAnnotate.hpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/CtAnnotate.hpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/CtAnnotate.hpp))
- [engine/src/Crypto/Sha256.cpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Sha256.cpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Sha256.cpp))
- [engine/src/Crypto/Sha512.cpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Sha512.cpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Sha512.cpp))
- [engine/src/Crypto/Blake2b.cpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Blake2b.cpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Blake2b.cpp))
- [engine/src/Crypto/ChaCha20.cpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/ChaCha20.cpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/ChaCha20.cpp))
- [engine/src/Crypto/Poly1305.cpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Poly1305.cpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Poly1305.cpp))
- [engine/src/Crypto/Aead.cpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Aead.cpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Aead.cpp))
- [engine/src/Crypto/Random.cpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Random.cpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Random.cpp))
- [engine/src/Crypto/Fe25519.hpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Fe25519.hpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Fe25519.hpp))
- [engine/src/Crypto/Fe25519_64.hpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Fe25519_64.hpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Fe25519_64.hpp))
- [engine/src/Crypto/Fe25519_16.hpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Fe25519_16.hpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Fe25519_16.hpp))
- [engine/src/Crypto/Fe25519Chain.inc](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Fe25519Chain.inc)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Fe25519Chain.inc))
- [engine/src/Crypto/Scalar25519.hpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Scalar25519.hpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Scalar25519.hpp))
- [engine/src/Crypto/X25519.cpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/X25519.cpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/X25519.cpp))
- [engine/src/Crypto/Ed25519.cpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Ed25519.cpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/src/Crypto/Ed25519.cpp))
- [shared/AntiCheatCrypto.hpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/shared/AntiCheatCrypto.hpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/shared/AntiCheatCrypto.hpp))
- [engine/include/okay/Net/NetCrypto.hpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/include/okay/Net/NetCrypto.hpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/include/okay/Net/NetCrypto.hpp))
- [engine/include/okay/Net/SecureChannel.hpp](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/include/okay/Net/SecureChannel.hpp)  ([raw](https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/0a85739583bf206e6d255d18a48a25253f3fabdf/engine/include/okay/Net/SecureChannel.hpp))
- The HMAC-SHA256 used by the relay: [shared/AntiCheatChecks.hpp lines 170-195](https://github.com/kingimann/OkaySpaceGameEngine/blob/0a85739583bf206e6d255d18a48a25253f3fabdf/shared/AntiCheatChecks.hpp#L170-L195)

All of them in one text file: `review/okay_crypto_source.txt` on this branch (about 2,650 lines).

Everything outside this list (tests, the rest of the game) is out of scope. Also out of scope: libsodium
itself, the operating system's random source, the compiler, denial of service by volume, an attacker who
already runs code inside the game process, power or fault attacks.

## 4. How to answer (please follow this format)

1. **Findings**, one per item, sorted worst first. For each: severity (critical, high, medium, low, info),
   file and line (or function name), what goes wrong, a concrete input or sequence that triggers it when you
   can build one, and a suggested fix.
2. **Confidence** for each finding: "I traced this by hand and am sure", "I think so but did not trace it",
   or "I am guessing". Do not present a guess as a fact. Findings we cannot reproduce cost us time.
3. **What you checked and found fine**, by area A to D, in one or two lines each.
4. **What you could not check** (timing on real hardware, compiled code, anything you only skimmed).
5. **A plain verdict**: is there any reason this must not be used instead of libsodium for new builds? Say
   which conditions would change your answer. Do not give a general "looks secure"; we need specifics.

We will test every finding with a real test before we change anything, so exact reproduction details help
more than long explanations.
