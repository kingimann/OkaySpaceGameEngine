# Review log

What has been reviewed, by whom, what was found and where each fix stands. Kept short and honest: a review
that found nothing is not proof of safety, and a review by an AI is not an independent human review.

Reviewer kinds used so far:

- **Internal agent:** a fresh agent of ours that did not write the code. Same kind of mind as the author.
- **Second AI (another account's Claude):** read-only review of branches and PRs. Different session and context,
  still an AI. Its findings are checked by us (reading and tests) before anything is changed.
- **Outside human reviewer:** none. The owner decided on 2026-11-14 not to hire one (`CLAUDE.md` rule 18).

Dates are 2026-11-14. "Fixed on a branch" means the fix is pushed but not on main yet; the status is checked
when the branch merges.

## 1. `okay::crypto` (internal agent)

Scope: Ed25519, SHA-512, X25519, ChaCha20-Poly1305, Poly1305, BLAKE2b, arithmetic and secret-dependent behaviour.
Not read: the network layer (`SecureChannel`, tickets), `Sha256.cpp`, the repo's own tests.

- 3.2 million Ed25519 verdicts compared with libsodium 1.0.18: 0 mismatches. Signing, X25519, BLAKE2b,
  XChaCha20-Poly1305 and SHA-512 (lengths 0 to 600) byte-identical.
- ASan, UBSan and a valgrind secret-dependence check (g++ 13 and clang 18): no secret-dependent branch or index found.
- No critical, high or medium findings. Low items: stack temporaries not wiped, `Ed25519SignTrustedPk` unsafe on an
  untrusted key, signature buffer aliasing the message gives a wrong signature, SHA-512 final step called twice.
- Not verified: timing on real hardware, MinGW, MSVC, Emscripten, ARM, the protocol layer.
- **Result: "internally reviewed, not independently reviewed". libsodium stays the default.** A request for a second
  AI review is on branch `claude/crypto-review-grok` (`review/CRYPTO_REVIEW_REQUEST.md`).

## 2. PR #850, own HTTP client readiness (second AI)

| Finding | Status |
|---|---|
| Windows per-request timeout added although the docs said Windows was untouched; a timeout could re-send a POST through curl | Confirmed. Per-request timeout removed; a POST that may have reached the server is no longer re-sent (PR #853) |
| PR title promised keep-alive, HEAD, conditional requests and cookies; they are in PR #852, not #850 | Confirmed. The merged title cannot change; the readiness doc says which PR added what (PR #853) |
| gzip decoder closely followed a well-known public reference decoder (rule 19a) | Confirmed by reading. Rewritten from RFC 1950, 1951 and 1952 (PR #854); see section 3 |
| Redirect-cap proof named a test with no such case | Confirmed. Real test added (PR #853) |
| Docs said the own-http CI job never ran | Confirmed stale. It ran green on `1b455cfa` (PR #853) |
| CRC table setup not thread-safe | Confirmed. Constexpr table (PR #854) |
| Zip-bomb test only checked output size; Range checks strict for one request shape only | Confirmed. Tests tightened (PR #854); strict Range checks (PR #853) |

## 3. PR #854, gzip decoder rewrite (second AI)

No RFC errors found. 3,000 Python-zlib streams decoded byte for byte; 9,000 mutated streams gave the same
accept or refuse decision as the old decoder. Findings: the two-level table design is a published technique also
used elsewhere (identifiers renamed for distance); the peak memory is about three times the cap, not two (docs
corrected); the PR text was stale (rewritten); the decoder accepts a single-symbol incomplete code of any length
(zlib only at length 1; harmless, output stays capped and checked). "No decoder source was consulted" cannot be
verified by anyone.

## 4. Five branches, PR #852 and the crypto wrapper (second AI)

| Finding | Where | Status |
|---|---|---|
| A release without libsodium silently uses `okay::crypto` for signatures; the release guard only looks for names | main, `claude/own-stack-rollout` | Fix in progress: `-DOKAY_REQUIRE_SODIUM=ON` for releases, guard checks the built result, honest startup line, pinned libsodium download |
| Normalized path used to open files breaks Windows network-share paths; watcher stuck "Quiet"; fast rescans; case-insensitive sharing on Linux | `claude/core-assets` | Fix in progress |
| Small hostile model files can exhaust memory (XGL, Irrlicht scene) or take hours (AC3D subdivision); OGRE strips and fans test cannot fail; `long` overflow on Windows; scene files may read paths outside their folder | `claude/readers-d` | Fix in progress |
| Windows: POST can still be re-sent after a reset or bad reply; cut-off body on Windows still returns 200 | `claude/ownhttp-review-fixes` (PR #853) | Fixed on the branch; WinHTTP changes syntax-checked only, never run |
| PlayFab can send the same POST twice after the own client fails | PR #852 | Open; fix after #853 merges (uses `maybeSent`) |
| Keep-alive retry looser than the docs say | PR #852 | Open |
| Wrong-signature accept through the crypto wrapper | `shared/AntiCheatCrypto.hpp` | No path found. The risk is the silent backend switch above |

Nothing copied-looking was found in these branches. That comes from reading and cannot prove the code is original.

## What no review has covered

- Anything on real Windows or macOS; real servers, real network shares, real PlayFab.
- The launcher and editor code of PR #852 (compiled with syntax checks only).
- The full test suite on most branches (CI runs it on PRs).
- An independent human review of any of it, including all cryptography.

## How to add a review

Add a section: reviewer kind, what was read and run, findings with file and line, status of each, and what was not
checked. Findings are checked by reading and by a test before any code changes. Keep the status column true when
the fix merges.
