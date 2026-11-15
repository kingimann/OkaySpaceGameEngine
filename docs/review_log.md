# Review log

What has been reviewed, by whom, what was found and where each fix stands. Kept short and honest: a review
that found nothing is not proof of safety, and a review by an AI is not an independent human review.

Reviewer kinds used so far:

- **Internal agent:** a fresh agent of ours that did not write the code. Same kind of mind as the author.
- **Second AI (another account's Claude):** read-only review of branches and PRs. Different session and context,
  still an AI. Its findings are checked by us (reading and tests) before anything is changed.
- **Outside human reviewer:** none. The owner decided on 2026-11-14 not to hire one (`CLAUDE.md` rule 18).

Dates are 2026-11-14 (sections 9 to 13: 2026-11-15). "Fixed on a branch" means the fix is pushed but not on main yet; the status is checked
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

## 5. Second Claude's branches: reverb, LWO UVs, Terragen (internal agent re-check)

Read-only re-check of the fixes the second Claude made after its first review. Nothing was built or run by the
reviewer; test counts are the author's claims.

| Finding | Where | Status |
|---|---|---|
| Reverb: no problems found; level held to 0.05 dB against the old reverb; not listened to on speakers | `claude2/reverb-own-tuning` (PR #860) | Ready; unverified by ear |
| LWO UVs: cylinder V lost the `WRPH` scale, so "placement kept identical" was not true; one code comment still said "derived from geometry" | `claude2/lwo-uv-rewrite` | Fixed on `claude/lwo-fix` (owner accepted keeping the old placement for now, 2026-11-14); test being added |
| Terragen: `-DOKAY_OWN_TER=ON` did nothing (a merge dropped TER from the CMake loop) | `claude2/own-reader-terragen` | Fixed on `claude/terragen-fix`; proof being added |

Not checked: Windows, the full test suite, any listening test.

## 6. Launcher branches: startup update (#864) and login gate (internal agents, two rounds)

Round 1 and round 2 were read-only reviews by fresh agents; the authors fixed what was real after each round.

| Finding | Where | Status |
|---|---|---|
| "Restarting" panel could hide the launcher forever (restart failed or editor open) | startup update | Fixed (12 s give-up), round 2 verified |
| Launch race: programs started during the check, then replaced by the install | startup update | Mostly fixed; residual window closed in round 3 (flag set in `SpawnTracked`) |
| Stalled download kept a full-screen panel with no way out; "programs running" refusal counted as a failed update | startup update | Fixed on the branch (60 s no-growth and hard cap; refusal not counted) |
| 403 from a proxy or captive portal counted as a refused sign-in and deleted the saved session | login gate | Fixed (only an auth-server body about the refresh token counts) |
| Race with the editor's token rotation; clock set ahead locks out after 7 days; opaque tokens | login gate | Fixed (re-read the file, grace renewed on any server confirmation) |
| Half-written session file or an old worker acting on a new account | login gate | Fixed in round 3 (guards and atomic write) |

Not covered by any review: running either branch, Windows, a real server, a real hung download.

## 7. Own readers by agents (second reviews of e1, e2, e3, e5)

| Finding | Where | Status |
|---|---|---|
| Tiny file makes large work or memory: RAW tokeniser on one huge line (about 4 GB), COB quadratic ShBx loop, IQM meshes reusing one range (about 300 MB from 250 KB), PMX and LWS bone copies | readers e2, e3, e5 | Fixed on the branches with counting-new tests |
| No test that the CMake option makes the reader the default (Terragen lost its option in a merge once) | readers e1 to e5 | Added per branch |
| MDC used another project's struct names and "published structures"; HMP and COB spec claims vaguer than the evidence | readers e1, e3 | Reworded; constants "from memory, unverified" stated; owner decision flagged |
| Sample files from a non-BSD part of a test tree | readers e1, e3 | Recorded in THIRD_PARTY.md; licence unchecked; not committed |

Silo (SIB) is not written (no public description). Nendo (NDO) is a seam that refuses every file.

## 8. Closed PRs #849 to #857 (merged with a red Ubuntu job)

Every red was one test: `player` (arm pixel check broken by shadows on by default, #842; fixed by #859). #832, #839, #840 and #793 had the `ui_render_bench` speed test red. All other jobs on all nine PRs were green. They merged under the old rule 6; the owner then changed it (merge only with every job green).

## 9. Vehicle seats and the vehicle look (internal agents, three rounds)

Dates are 2026-11-15. Read-only reviews by fresh agents; the authors fixed what was real after each round.

| Finding | Where | Status |
|---|---|---|
| NPCs could board and a first-person player could not (the seat looked for a `Character` component) | `VehicleSeat` | Fixed (#876): only a player controller can board |
| Saving, copying or undo while seated wrote a frozen player; a scene torn down while seated left a stale pointer | `SceneSerializer`, `VehicleSeat` | Fixed (#876): state handed back during the write, seat removed from the occupied list on destroy |
| One E press swapped seats; a dead occupant kept driving; removing the driver seat left the car braking forever | `VehicleSeat`, `VehicleController` | Fixed (#876) |
| Exit-placement test could not prove a blocked exit | `tests/test_vehicle_seat.cpp` | Rewritten (#876); a deliberate break of `Blocked()` now fails the test |
| Owner: the driver stood instead of sitting and the car did not look like a car | sample scene | Reworked on `claude/vehicle-looks`: sit pose, car shape, wheels, chase camera; owner saw headless screenshots and said "much better" |
| Sit-clip height was 0.5 m too high, D steered left, chase camera looked away | `HumanoidActions`, `VehicleController`, camera | Fixed on `claude/vehicle-looks` |
| Review of `claude/vehicle-looks`: a committed `.bak` file, a dead driver could be pulled into stand-up, the height fix was not pinned by a test, stale doc numbers and left/right wording | `claude/vehicle-looks` | Fixed on the branch (`08c53c39`); PR not opened yet |

Not covered: any real window or editor, Windows, the editor Inspector block for the seat (first compiled by CI), multiplayer seat state.

## 10. Small fixes and own readers by agents (read-only reviews)

| Finding | Where | Status |
|---|---|---|
| Play start: a flac test still read the old clip field; the nav bake skip would go stale once Mesh Collider baking landed; the new clip cache used `<mutex>` (breaks the MinGW win32 build) | `claude/play-start-hitch` (#882) | Fixed: test updated, bake key now hashes Mesh Collider triangles, cache uses `debugutil::SpinLock` |
| Navmesh from Mesh Collider triangles: no work or memory budget, non-finite corners, float gap on column borders, closed-mesh test ran before the triangle cap | `claude2/navmesh-mesh-collider` (#879) | Fixed on the branch before the PR; limits, skipped counts and a plain warning added |
| OBJ texture paths: Windows device names, a UNC test that could not fail, home-folder check read only `$HOME`, `<mutex>` in a header | `claude2/obj-texture-roots` (#881) | Fixed (#881); Windows behaviour (junctions, `\\?\` paths) unverified |
| Own-reader tests named `.iqm` as a format with no own reader and broke CI when IQM was added | `tests/test_model_readers.cpp` (#883) | Fixed: `.sib` (Silo, reserved, unwritten) is the permanent "no reader" example |
| Reader e5 (PMX, LWS) and e3 (IQM, COB) merges with main: slot numbers, wiring list, option-ON tests | `claude/readers-e3`, `claude/readers-e5` | Merged (e3, #883) or ready (e5, #889); no real PMX or LWS file exists to test against |

## 11. Second Claude's later branches (independent read-only review, three rounds)

Claude 2 became active again on 2026-11-15 (feature work, issue #422). Nothing below was built by the reviewer.

| Finding | Branch | Status |
|---|---|---|
| Earlier code findings (nameplate nits, world-text size and length caps, prone doc wording, software DoF threading and early-out) | nameplate, world text, prone, software DoF | Fixed by the author, re-read and confirmed |
| Software scene SSAO now goes through `ApplyPowered`, which caps strength at 1.0, but the editor slider goes to 2.0 (a regression for the process-wide path) | `claude2/software-scene-ssao` | Open: sent back |
| No branch had "How to try it", a sample scene or screenshots (owner rule 4a came after) | all visual branches | Open: sent back; screenshots rendered by the coordinator |
| `pausemenu` record in `SceneSerializer.cpp` does not save five PauseMenu fields (hint, showRestart, showVolume, restartScene, localized): real data loss | `claude2/property-registry-gameplay` | Open: Claude 2 reported it; fix and a registry-versus-record audit requested |
| Registry batch is very large (about 166 types, 2,239 properties) and nothing detects a field that is saved but not registered | same | Open: asked to stop adding types, fix the bug first, add the audit |
| Durability: an item with stored durability 0 and no definition maximum is treated as broken; wear and repair are not networked | `claude2/item-durability` | Open: sent back |
| Net node tests: join loops bounded by frame count not wall clock; encrypted half skips when no crypto backend; unrelated `test_vs_api_silent` in the same branch | `claude2/vs-net-nodes`, `vs-net-link-pin` | Open: sent back |
| Two copies of the vehicle seat were built at once (Claude 2 and the coordinator) | `claude2/vehicle-seat` | Closed: Claude 2 told to stop; the coordinator's version merged (#876) |

## 12. The try-it branch (packaging and docs, independent read-only review)

`claude/try-it` (#887) ships a curated `examples/` folder in the Windows zip and installer (it did not before, so the vehicle scene was unreachable from a release), adds a "Vehicle (get in and drive)" template and `docs/try_it.md`. Verdict: ready. Low items: sample provenance needs lines in `docs/THIRD_PARTY.md` or `docs/provenance_audit.md` (follow-up); the uninstaller removes `$INSTDIR\examples`, which would delete a user's own `examples` folder if they installed into one (same exposure as the existing Assets and Games lines). Not covered: anything on Windows, `makensis`, the editor build.

## 13. Startup update safety read before it ships on by default (#864)

A third read-only safety pass on 2026-11-15 after the owner approved on-by-default: off switch (Settings toggle and `OKAY_UPDATE_ON_LAUNCH=0`), 8 s offline step-aside, no install while the editor or a game runs, signed index and checksums checked first, one attempt per launch. No gap needed a fix. Known gap left on purpose: no Cancel button during download and install. Most existing installs have `update_on_launch=0` saved, so default-on mostly reaches fresh installs. Not covered: a real launch, a real server, real Windows.

## What no review has covered

- Anything on real Windows or macOS; real servers, real network shares, real PlayFab.
- The launcher and editor code of PR #852 (compiled with syntax checks only).
- The full test suite on most branches (CI runs it on PRs).
- An independent human review of any of it, including all cryptography.

## Grok (second AI) findings, issue #888

Source: issue #888 comments, sanitizer builds of main by the second AI. Each finding is reproduced by a test first.

| Finding | What | Status |
|---|---|---|
| 2 | OBJ reader: face index 2147483648 narrowed `long` to `int` (signed overflow, UB). Fixed in `ReadObjBuffer`: an out-of-range index is not narrowed, the file is still refused with "face index out of range". Test: `tests/test_model_readers_fuzz.cpp` (runs in the sanitizer CI job). Same pattern checked in the other own readers: none found (the `p`/`l` element path was already range-checked). | Fixed on `claude/grok-fix-obj-okayui` |
| 7 | OkayUI float-to-int casts on NaN / inf / 1e30: `BeginDragDropSource` (drag id now hashes the float bits) and `CodeEdit::handleMouse` (gutter line is range-checked). The four OkayUI fuzz tests now run in the sanitizer CI job. | Fixed on `claude/grok-fix-obj-okayui` (also hidden behind the abort: `CodeEdit::ScreenToPos` and the draw loop's scroll-to-line / scroll-to-column casts, now through a clamping helper). All 25 OkayUI test binaries pass under ASan + UBSan + float-cast-overflow |

## How to add a review

Add a section: reviewer kind, what was read and run, findings with file and line, status of each, and what was not
checked. Findings are checked by reading and by a test before any code changes. Keep the status column true when
the fix merges.
