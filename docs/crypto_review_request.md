# Asking for an outside review of our own cryptography

> **Status: a request, not a result.** Nobody has been approached yet. Nothing here says our code is secure; the whole point is to find out. Our own crypto (`okay::crypto`) stays **off by default** (`OKAY_OWN_CRYPTO`, off) until an outside reviewer has looked at it and the owner decides (`CLAUDE.md` rule 18, `docs/own_code_roadmap.md`).

This page is for the owner. It has the text to send to a reviewer or a firm, what we hand over, how to pick someone, what happens afterwards and a checklist of the owner's steps. The technical detail for the reviewer is in `docs/crypto_review_package.md` (scope, threat model, evidence, known issues, questions).

## 1. The request (copy and send; no names, no secrets)

> Subject: Request for a cryptographic code review of a small C++ library and how a game engine uses it
>
> We maintain a game engine that uses libsodium for network encryption and a ported copy of TweetNaCl for Ed25519 signatures. To remove those dependencies we wrote our own implementation of exactly the primitives we use: XChaCha20-Poly1305 and ChaCha20-Poly1305, X25519 with a libsodium-compatible key-exchange derivation, Ed25519 (strict verification), BLAKE2b, SHA-256, SHA-512 and an OS random wrapper. It is about 1,850 lines of C++17 plus a 190-line interface, with no assembly, no SIMD and no dependencies. It is off by default and will stay off until an independent review is done and its findings are fixed.
>
> We would like (1) a review of the arithmetic and the constant-time behaviour of everything that touches a secret, including the compiled code on the compilers we ship; (2) a review of the Ed25519 verification rules; (3) a review of how the primitives are used in our UDP transport and join handshake (nonce handling, key exchange, ticket signing, replay protection); (4) your view on whether it is safe to become the default for new builds; and (5) what else we should test.
>
> We will send one archive with the code, the tests, scripts that rebuild every result we quote, a manifest with a SHA-256 for every file, and a document with the scope, threat model, known issues and our questions. You can build and run the evidence in about 15 minutes on Linux. An hour with the document is enough to tell whether you can help.
>
> Please tell us: your availability, how you would scope and price the work, whether you want a different scope from the one we suggest, and how you want findings reported. We will tag a fixed snapshot of the code for you and will not change it during the review without telling you.

## 2. What we ask them to review

In scope (details and line counts in `docs/crypto_review_package.md` section 1):

- **`okay::crypto`** (the 1,849 source lines under `engine/src/Crypto/` and the public header): the AEADs, the Poly1305 and ChaCha20 code, BLAKE2b, SHA-256, SHA-512, the field and scalar arithmetic behind X25519 and Ed25519, the Ed25519 sign and verify code including the interleaved double-scalar multiplication, the random source and the wipe and compare helpers.
- **The constant-time properties of the secret paths**, as compiled by GCC, MinGW, Clang, MSVC and Emscripten. We ran only two audits on one x86-64 Linux machine with GCC and Clang (a valgrind secret-dependence audit and a statistical timing test); see section 4 of the package.
- **The use of the primitives in the net layer**: the seam (`NetCrypto.hpp`), `SecureChannel` (key exchange, random 24-byte nonces, sequence numbers and the 64-entry replay window), `SessionTicket` (Ed25519-signed join tickets and key binding), the join key message the host signs for host-key pinning, and the sealed join and its plaintext fallback.
- **The other crypto code in the tree** (section 4 below): the TweetNaCl-derived port, the hand-written HMAC-SHA256 and the extra SHA-256 copies.
- **The ten known issues** (package section 7) and **the ten questions** (package section 8): confirm, fix or accept each.

Out of scope, please do not spend time on it:

- libsodium itself, the operating system random source, the compiler.
- Anything that is not cryptography: rendering, editor, game code, the launcher's user interface.
- An attacker who already runs code inside the game process (the keys are in its memory), denial of service by volume, power and fault attacks, physical access.
- The owner's practice for storing the ticket and manifest signing keys (an operations question, not a code one).
- Third-party test data (`tests/wycheproof/` is the unmodified Project Wycheproof set).
- Argon2, secretbox, Ed25519ph, batch verification: not implemented and not used.

## 3. What we want back

1. **A written report** we may keep and, if the reviewer agrees, publish or summarise (we will ask, not assume).
2. **Findings sorted by severity** (for example critical, high, medium, low, informational), each with the file and line, what goes wrong, how an attacker would use it, a suggested fix, and a note saying whether they could reproduce it.
3. **A constant-time analysis**: which functions they checked, with which compilers, flags and CPUs, by which method, and what they did not check. Also a view on whether the `ct::Barrier` mask protection is enough (package section 7, item 5) and what CI check should keep it caught.
4. **A protocol verdict**: is the use of Ed25519, X25519 and XChaCha20-Poly1305 in our protocol sound? In particular the nonce handling (random 24-byte nonces, one key per direction), the key exchange (transcript is `q || client_pk || server_pk` only, no identity binding), ticket signing and key binding, host-key pinning, replay protection (sequence number inside the ciphertext, window of 64) and downgrade (the plaintext join fallback, package section 7, item 2).
5. **A plain yes or no on "safe to be the default for new builds"**, with the conditions attached (for example "yes, once findings 1 to 3 are fixed and a timing check runs in CI"). A "no" with reasons is a useful answer.
6. **A list of what else to test or change** before relying on it, and whether anything should stay with a maintained library instead of our own code (package question 10).
7. After our fixes, **a re-check** that each finding is resolved (see section 6).

## 4. The other crypto code: in scope, and what to do about it first

These are not `okay::crypto` but they are crypto we wrote or ported, and the review should cover them (package section 1, second table):

| Code | Where | What it is |
|---|---|---|
| TweetNaCl port (Ed25519 and SHA-512) | `shared/AntiCheatCrypto.hpp` (523 lines) | A second Ed25519. The fallback when there is neither libsodium nor `OKAY_OWN_CRYPTO`, and the independent reference our differential test compares `okay::crypto` against. It adds libsodium's strictness checks; a test pins the identity-key forgery that plain TweetNaCl would accept |
| Hand-written HMAC-SHA256 | `okayac::detail::HmacSha256`, `shared/AntiCheatChecks.hpp` lines 175 to 192 | Used for the relay's address cookie (`RelayServer::MakeCookie`), the anti-cheat clock challenge and the hardware tag (`shared/AntiCheatHardware.hpp`) |
| A third SHA-256 | class `okay::account::detail::Sha256`, `engine/include/okay/Platform/Account/AccountService.hpp` lines 56 to 160 | What the HMAC above is built on. Also hashes local-backend passwords and the dedicated server's token cache key |
| File SHA-256 | `FileSha256` in `shared/OfficialGame.hpp` (line 446) | Hashes files for the manifest and for the launcher's update check (`VerifyFile`), so a wrong answer here is an update-integrity bug |

**Recommendation (not done, and not part of this change):** consolidate before the review starts, so the reviewer looks at the shape we intend to ship rather than at code that is about to be deleted.

1. **Do it**: move the account `Sha256`, `FileSha256` and the `HmacSha256` onto `okay::crypto`. For SHA-256 this is a small change with ready known-answer vectors. For HMAC, add a small `okay::crypto` HMAC-SHA256 built on our SHA-256 (the Wycheproof HMAC vectors already run against such a construction in `tests/test_crypto_wycheproof.cpp`) and make the relay cookie, the clock tag and the hardware tag call it. That leaves one SHA-256 and one HMAC in the tree, both inside the reviewed library, and removes package question 6 and known issue 3 for the hash side.
2. **Do not do yet**: delete the TweetNaCl port. It is the second, independent Ed25519 our tests compare against, and the roadmap deletes it only after the review. Ask the reviewer to look at it as a reference and as the no-library fallback.
3. **Also worth doing first** (cheap, and the reviewer would report them anyway): known issue 1 (`SecureChannel` never wipes its keys) and issue 4 (document whether `Seal` may be called from more than one thread).
4. **Then freeze**: tag the commit the bundle is made from and do not change crypto code during the review without telling the reviewer.

The cost of consolidating first is a short delay. The cost of not doing it is that the reviewer reads three SHA-256 implementations and a hand-written HMAC and the review has to be repeated for whichever stays. Any such change is itself cryptographic code, so it needs the same care: tests first, no change to any vector, and it goes in the scope the reviewer sees.

## 5. What we give them

- **The bundle**: one `.tar.gz`, made by `scripts/make_crypto_review_bundle.sh <output.tar.gz>` from a tagged commit. It holds exactly the files in the package's scope tables, the tests and harnesses, the scripts, the docs, `MANIFEST.sha256` (SHA-256 of every file), `MANIFEST.txt` (the git commit) and `README.md` with the 15-minute build-and-run path (`reviewer/run_evidence.sh`, plain compiler, no CMake). The archive is reproducible: the same commit gives the same bytes, so the reviewer can check it against the SHA-256 we send in the email.
- **`docs/crypto_review_package.md`**, inside the bundle: scope with line counts, threat model, what was verified and how, what was not, the strictness decisions (section 3.1), the checklist (section 5), the known issues (section 7), our ten questions (section 8) and what only an outside reviewer can judge (section 9).
- **Access**: an answer within a working day to questions, and a person who can rebuild anything on request. Nothing secret is in the bundle: it has no keys, no signing material and no credentials.

## 6. Rough effort and how long it takes

**This is an estimate by us, not a quote.** Nobody has been asked, and a reviewer will know better. Our code is small (about 1,850 lines plus the 1,300 lines of other crypto code) but it is dense arithmetic, and the compiled-code constant-time work is the slow part.

| Part | Estimated reviewer effort |
|---|---|
| Source review of `okay::crypto` and the evidence (primitives, field and scalar arithmetic, Ed25519, strictness decisions) | about 6 to 12 working days |
| Protocol review of the net layer and the other crypto code in section 4 | about 3 to 6 working days |
| Constant-time analysis of compiled output on several toolchains (GCC, MinGW, Clang, MSVC, Emscripten; more than one optimisation level) | about 4 to 10 working days, strongly dependent on tooling and how many targets |
| Report, our questions, and a re-check of our fixes | about 3 to 5 working days |
| **Total** | **roughly 3 to 7 weeks of one reviewer's time, spread over a longer calendar time** |

A smaller first step is possible: source review plus the protocol (the first two rows) is roughly 2 to 4 weeks, and the reviewer can say what the compiled-code part would add. Calendar time before the work even starts is unknown (good reviewers are often booked weeks or months ahead). We do not know the price, and we do not put a money figure here.

## 7. How to pick a reviewer

Look for, in this order:

1. **Hands-on experience with Ed25519, X25519 and ChaCha20-Poly1305 implementations**, ideally having found or fixed bugs in one (small-order points, malleability, non-canonical encodings, limb-bound and carry errors in field arithmetic).
2. **Constant-time review experience**: someone who reads compiler output, knows `ctgrind` and `dudect`-style testing, and has dealt with the compiler turning a mask select into a branch (we hit that once; package section 4.1).
3. **Protocol and application experience**, ideally with game or UDP protocols: nonce and replay handling over lossy transports, handshake and downgrade, key pinning, tickets.
4. **Independence**: no connection to libsodium or to us, and willingness to write down disagreements with our decisions.
5. **Practical**: can start in a reasonable time, will review a fixed snapshot, will re-check fixes, will report privately first.

Where to look: firms that do cryptographic code audits, and individual cryptographers and engineers who have published audits or implementations of these primitives. Ask each for one or two example (public) reports of similar scope, and ask what their process would be for the compiled-code part. Compare two or three quotes if the owner wishes. Do not accept a reviewer who wants to "just run a scanner": this needs a person reading arithmetic code.

## 8. What happens after

1. The reviewer sends the report, privately first.
2. **We fix**: every finding is fixed, or a written decision says why it is acceptable (package section 7 asks for exactly this for the known issues). Fixes are small changes with a test that fails before and passes after. No test vector's expected value is changed to make a test pass.
3. **We re-verify**: re-run the whole evidence (`reviewer/run_evidence.sh --ct --fuzz --diff`, the CMake test suite on both backends via `scripts/test_crypto_backends.sh`, Windows and macOS runs where the review asked for them), and send the reviewer the changes for a re-check.
4. **The owner decides the flip.** Only with the reviewer's "yes" (or a "yes with conditions" whose conditions are met), the CI jobs the reviewer asked for in place, and the owner's written approval. Then, following `docs/own_code_roadmap.md`: flip the default in one release with a release note one release ahead, and delete libsodium, `OKAY_USE_SODIUM` and the TweetNaCl port one release later. Nothing is flipped on our word.

## 9. The owner's checklist

- [ ] Decide whether to consolidate the extra SHA-256 and HMAC code and fix known issues 1 and 4 first (section 4). Our recommendation: yes. This is engineering work for us; it needs no money.
- [ ] Choose a budget and a calendar window. Ask for a quote for the full scope and for the smaller first step (section 6).
- [ ] Pick two or three candidate reviewers (section 7) and send each the request text from section 1.
- [ ] Sign whatever the reviewer or firm needs (an NDA is not required for the code, which is open in the repository, but they may want a statement about reporting). Agree how findings are reported and whether the report may be published.
- [ ] Tag the commit to be reviewed. Make the bundle (`scripts/make_crypto_review_bundle.sh /some/scratch/okay-crypto-review.tar.gz`) and send it together with its SHA-256 (the script prints it). Do not commit the archive.
- [ ] Answer reviewer questions; the team can rebuild any result on request.
- [ ] Read the report. For each finding, approve the fix or the written decision to accept it.
- [ ] Ask the team to re-run the evidence and send the fixes for a re-check. Ask the reviewer for the yes or no on "safe to be the default for new builds".
- [ ] **Only then** decide the flip, in writing in the PR. Until then `OKAY_OWN_CRYPTO` stays off.
