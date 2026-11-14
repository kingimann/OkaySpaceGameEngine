# The updater: integrity, self-update, rollback

How the launcher (and the editor) replace their own files, what is checked before anything is replaced, what the person sees when it
does not work, and what is still open. The Versions page (installing other editors next to the running one) is described in
[launcher_versions.md](launcher_versions.md); this page is about the **in-place update** of `OkaySpace.exe`, `OkayEngine.exe` and
`OkaySpacePlayer.exe`.

Code: `launcher/UpdaterCore.hpp` (stage, verify, all-or-nothing swap), `shared/SelfUpdate.hpp` (state file, start-up verdict, retry
rule, self-check, rollback), `shared/AppRestart.hpp` (the detached helper), `launcher/main.cpp` (`RunUpdateCheck`,
`ApplyStartupUpdateVerdict`, `BeginUpdatedRestart`), `editor/main.cpp` (`updater::FetchVerified`, `InstallUpdate`, `EnsurePlayer`),
`scripts/versions_index.py` and `scripts/publish-supabase.sh` (the publish side).
Tests: `self_update`, `app_restart`, `launcher_versions`, `update_signature` (all in `tests/`; temp folders only, no network).
Signing: `shared/UpdateSignature.hpp`, `scripts/sign_index.py`, [Signed releases](#signed-releases).

## The rules

1. **No checksum, no install.** A file is downloaded only when `versions.json` lists a size and a 64-hex SHA-256 for it. A missing or
   unreadable `versions.json`, a version that is not in it, a file that is not in it, or a bad checksum is a refusal with the reason in
   plain words. There is no "install anyway" anywhere (the Versions page's old "Install without a checksum?" question was removed).
2. **Stage, verify, then swap.** Every file is downloaded to `<name>.new` and its size and SHA-256 are checked first. Nothing live is
   replaced until all of them passed.
3. **The swap is all or nothing.** The launcher's own file goes first (the swap most likely to be refused). If any swap fails, the ones
   already done are put back from their `.old` backups, and the message says so (`okayupdater::CommitAll`).
4. **The new launcher must prove itself before it is started.** After the old launcher exits, the helper runs
   `OkaySpace.exe --okay-selfcheck <published version>`. It must start and answer that it is exactly that version. If it cannot be
   started, or is another version, the helper puts the previous file back (`OkaySpace.exe.old` over `OkaySpace.exe`, the bad file kept as
   `OkaySpace.exe.failed`), records the failure, and starts the previous launcher.
5. **The new launcher confirms itself.** At start, the launcher compares its own version with `okay_selfupdate.state`: running the target
   version clears the state and the `.old` backup ("confirmed"). The helper waits up to 20 s for that and logs whether it happened.
6. **Failures are loud, logged and not a loop.** Every failure is shown in the update dialog and the sidebar note (with what to do), written to
   `okay_update.log`, and counted per target version in `okay_selfupdate.state`. The automatic check at launch stops trying a version after
   2 failures ("failed 2 times, so it is not tried again automatically. Use Retry"); a click on Retry always tries once more. A
   newer release is a new chance. After a rollback `VERSION.txt` is reset so the next check still sees the update.

## What the person sees

| Situation | Message (update dialog + sidebar note; same text in `okay_update.log`) |
|---|---|
| No `versions.json` | "Update refused. The release server has no checksum list (versions.json) for v9.0.0. Nothing was installed and your current version is unchanged. This release has no usable checksums, so it can't be checked: tell the publisher to publish it again (scripts/publish-supabase.sh), or download the full installer." |
| Checksum does not match | "The downloaded OkaySpace.exe is not the published file (its checksum does not match). Nothing was installed and your current version is unchanged." |
| Rename refused (antivirus lock, read-only folder) | "Could not replace OkaySpace.exe: couldn't move the existing program aside: ... The launcher's own file could not be replaced while it is running (security software often locks it); your current version is unchanged." |
| The helper could not be started | "Couldn't restart the launcher (...). The update is installed: close OkaySpace and open it again to finish." |
| New file does not start / is another version | At the next start: "The update to v2.856.0 was not installed: The new version did not pass its start-up check: the new file could not be started (...). The previous version was put back. Your current version still works. Close any security software that may be blocking the file, then use Retry in the update dialog. If it keeps failing, download the full installer from the release page." (dialog the first time, sidebar note afterwards) |
| Swapped, but the old version came up | "The update to v2.856.0 did not apply. After the restart the launcher is still v2.855.0, not v2.856.0. ..." |
| Failed twice | "The update to v2.856.0 failed 2 times, so it is not tried again automatically. Use Retry to try once more." |

## Audit: what was already there, what was missing

"HAVE" is what PR #650 and the restart work already did. "GAP" is what this pass found; the status column says what became of it.

| Area | HAVE (before this pass) | GAP found | Now |
|---|---|---|---|
| Checksum required for launcher / engine / player | `Stage` refuses a file with no 64-hex SHA-256 in the index (#650) | A fallback index (no `versions.json`) got the vague "does not list v..." message | Says "no checksum list ... for v..." |
| No index = refuse | `IndexProblem` refuses before any download | Versions page: "Install anyway" for any version without a checksum (size check only, after a click) | Removed: `PlanInstall` refuses, `Install` refuses a hand-built plan too. Tests: `launcher_versions`, `self_update` |
| Editor's own update | none | `editor/main.cpp` `InstallUpdate` downloaded `OkayEngine.exe` from GitHub with only a size and an "MZ" check; `EnsurePlayer` did the same for the player | Both go through the same index rules (`FetchVerified`). Not run here (see "Known gaps") |
| Verify before replace | all files staged and verified first (#650) | Swap of several files was not atomic: a failed launcher swap left the engine and player already replaced | `CommitAll` puts the earlier ones back. Test: `self_update` |
| Swap safety | `SwapFile`: old file aside, put back on failure | - | unchanged |
| Failure is shown | Reason in the dialog and `okay_update.log` (#650) | Not kept across a restart; a failure after the swap (new file does not start, wrong version) was invisible | `okay_selfupdate.state` + start-up verdict (dialog once, sidebar note afterwards) |
| Restart | Detached helper waits for exit, relaunches once (#584) | On Linux the restart used `/proc/self/exe`, which points at the renamed `OkaySpace.old` after the swap, so the OLD launcher was relaunched ("version after restart unchanged") | The path captured at start is used (`SelfProgramPath`). Windows behaviour not checked here |
| Version after restart | none | - | Helper runs `--okay-selfcheck <version>` before the real start; the launcher confirms itself at start |
| Rollback | only a failed swap | New file that cannot start was left in place | Helper rolls back (`RollBack`), keeps the bad file as `.failed`, starts the previous launcher. Test: `app_restart` (real processes) |
| No retry loop | - | A release that failed would be retried on every start | 2 automatic attempts per target version (`AutoRetryAllowed`) |
| Publish side | `publish-supabase.sh` fails without a checksum for each file, writes `versions.json` before `VERSION.txt`, reads it back (#650) | Nothing checks that the published `OkaySpace.exe` reports the published version | Open (see below) |
| Hostile index inputs | `ParseIndex` is tolerant and strict (schema, sizes, hex) | Not tested against an attacker's index for the updater's invariant | Test: any text, `PlanInstall` ok implies a checksum and size for every download |
| Windows CI | `windows-restart` job runs `app_restart` | New process-level cases are inside `app_restart`; the job also builds and runs `self_update` now | **Not run by the author** (no Windows runner here); CI will be the first run |
| Signing | none | `versions.json` is plain JSON from the same bucket | **Built** (ed25519, [Signed releases](#signed-releases)); off until the owner creates a key, so today's feed is still unsigned |
| Editor: server and cross-OS export runtimes | none | `okay-server-linux-x64` and `SDL2.dll` are fetched from GitHub's latest release with a size check only; they are not in `versions.json` | Open: they would need to be published with checksums first |
| Release on GitHub only | launcher says it cannot update in place | no `versions.json` there | Unchanged by design (refusal is the safe behaviour) |

## Known gaps

- A launcher that passes the self-check and then crashes later (after the helper started it) is **not** rolled back automatically. The
  state stays "swapped"; the next start of the same broken build will not confirm. The person must reinstall. A watchdog that
  notices the launched process dying before it confirms would close this (needs the launched process id from `LaunchOnce`).
- The self-check blocks until the new file exits; a new file that hangs on `--okay-selfcheck` would hang the helper (the old launcher is
  already closed). Not seen; a timeout is a small follow-up.
- On Windows, renaming a running `.exe` works, but a scanner holding the file can make the swap fail: that case is reported (row 3 of the
  table) but was not reproduced here.
- The editor's own update gets the same checksum rule, self-check and rollback, but has no start-up verdict: after a rollback the previous
  editor simply starts again and the reason is only in `okay_update.log`. Its download code compiles with the launcher's list reader
  (`launcher/VersionsManager.cpp`) but was not run against a fake server here.
- `versions.json` is trusted as far as its bucket is **until a signing key exists**: see [Signed releases](#signed-releases).
- The publish scripts cannot run the Windows binary, so "the published launcher reports the published version" is only checked on the
  user's machine by the self-check, after download.

## Signed releases

`versions.json` lists every file's size and SHA-256; the updater installs nothing a checksum does not cover. Without a signature, an
attacker who controls the release bucket can publish a bad file **and** a matching `versions.json`. A signature closes that.

**How it works.** The publish script (`scripts/publish-supabase.sh`, which calls `scripts/sign_index.py`) signs the exact bytes of
`versions.json` with an ed25519 key and uploads `versions.json.sig` next to it: one line, `okay-sig1 <128 hex digits>`, over the message
`okayspace-versions-json-v1\n` + the file (the prefix keeps a signature for this from being valid for another use of the same key).
The launcher and the editor (`launcher/VersionsManager.cpp` `LoadIndex`, so the Versions page, the in-place updater and the editor's own
update all go through it) are built with `-DOKAY_UPDATE_TRUSTED_KEYS=<hex>[,<hex>]` (public keys; up to four). With a key built in:

- an index with no signature, a damaged signature, or a signature no trusted key made is **refused**: *"Update refused: the release
  list (versions.json) was not signed by the publisher's key. Nothing was installed and your current version is unchanged..."* It never
  falls back to the unsigned `VERSION.txt` heads, which carry no checksums and so could not be installed anyway;
- a bad key list (malformed, weak key) refuses everything: it never becomes "no keys, so accept";
- the verify code is `shared/UpdateSignature.hpp` on the repository's own ed25519 (`shared/AntiCheatCrypto.hpp`, libsodium when present,
  its portable code otherwise), and tests/test_update_signature.cpp checks that the publish tool and the C++ verifier agree.

The channel heads (`VERSION.txt`, `LTS.txt`) and notes are not signed and are not trusted: a version can only be installed from the signed
index. There is no separate "manifest" to sign: `versions.json` is the manifest (the game's own signed `Data/manifest.json` is the
anti-cheat's, signed with its own key, see [anticheat.md](anticheat.md#keys-setup-for-the-owner)).

**The transition (what is true today).** No key exists, so no build has one and the feed is unsigned. A build **without**
`OKAY_UPDATE_TRUSTED_KEYS` accepts an unsigned feed exactly as before, and a signed feed too (it just doesn't check). So publishing a
signature first breaks nobody. Nothing is refused until you ship a launcher that has a key, and that is the step that makes unsigned
feeds fail for it. Older launchers in the wild never verify, so signing protects only users who have updated to a build with the key.

**Owner steps** (once):

1. On a machine you trust, not CI: `python3 scripts/sign_index.py genkey`. It prints two lines and writes nothing:
   `OKAY_UPDATE_SIGNING_SEED=<64 hex>` (the **private** key) and `OKAY_UPDATE_TRUSTED_KEYS=<64 hex>` (the public key).
2. Keep the seed in your password manager or hardware-backed store, with an offline backup. It goes in no file in the repository, no
   chat, no log.
3. Set the **public** key as the repository variable `OKAY_UPDATE_TRUSTED_KEYS` (Settings > Secrets and variables > Actions >
   Variables; `release.yml` passes it to the build, `scripts/build-windows-dist.sh` to CMake as `-DOKAY_UPDATE_TRUSTED_KEYS`). The seed
   is **not** a repository secret unless you decide to publish from CI; if you do, use a protected environment with required reviewers.
4. Publish a release that is signed but still accepted by every build: `export OKAY_UPDATE_SIGNING_SEED=<seed>` in the shell that runs
   `scripts/publish-supabase.sh` (or `scripts/release-local.sh`). It uploads the signature, downloads both files back and verifies the
   published pair; on failure VERSION.txt is not written.
5. Release a launcher built with the public key (step 3). From then on that launcher refuses an unsigned or wrongly signed feed. Once you
   are sure every publish is signed, set `REQUIRE_SIGNED=1` so a publish without the seed stops instead of uploading an unsigned index.

**Rotation** (two keys at a time, up to four). Make the next key pair; release a launcher built with
`OKAY_UPDATE_TRUSTED_KEYS=<current>,<next>` (still signed with the current key). When that launcher is what almost everyone runs, start
signing with the next seed; later builds can trust only `<next>`. A launcher that only knows the old key refuses an index signed with the
new one, so never switch the signing key before the two-key build has been out long enough. If a seed leaks: sign with a new key
the same way, but know that a launcher that only trusts the leaked key cannot be told to stop trusting it; it needs a new installer
that carries the new public key (the full installer is the recovery path). If the seed is lost, the same applies.

**What this does not do.**

- It does not stop a **replay**: an attacker who serves an older signed `versions.json` offers only versions that were really
  published and checked, but can hide a newer one (a "freeze") or offer an older one. The launcher still only installs a version newer
  than its own on the automatic path; there is no signed timestamp or expiry yet.
- It does not sign the executables (item 2 of the old design: code signing for the OS and scanners). That needs a purchased
  certificate and a release-workflow step.
- A mismatch while a publish is in flight (the new `versions.json` is up and `versions.json.sig` is not yet) makes a launcher that
  checks at that moment refuse once ("try again in a minute"); it retries on the next check.
- It protects the update path only; whoever can build and sign with the seed can ship anything, so protect the seed.

**Checks.** `tests/test_update_signature.cpp` (verdicts, rotation, domain separation, a refusal never falls back, the transition, the
Python tool against the C++ verifier), `tests/scripts/test_sign_index.sh` (RFC 8032 vector, tamper and wrong-key refusal, the seed is
read from the environment only and never printed, the publish script's guards). CI runs them in the `ticket-and-script-tests` and
`anticheat-crypto` jobs.

## Manual check

Under a virtual display with a local fake release server (`OKAY_UPDATE_SERVER=http://127.0.0.1:<port>`, plain http is accepted only
for 127.0.0.1): serve `<server>/storage/v1/object/public/releases/VERSION.txt`, `versions.json` and `v<ver>/<file>`; start the launcher
with `update_on_launch=1` in `launcher.cfg` (or click the version label). Refused cases: wrong SHA-256 in the index, no `versions.json`.
Applied case: a launcher built with a higher `OKAY_ENGINE_VERSION` as `OkaySpace.exe`. Rollback case: the same launcher binary served as
`OkaySpace.exe` under a higher version number (it reports its own version, so the self-check fails).
