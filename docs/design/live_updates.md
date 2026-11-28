# Live Updates: design note (foundations, 2026-11-28)

Internal record. Owner request 2026-11-28: a native Live Update System, integrated with OkayBackend, with a replaceable storage
provider. This round builds the foundations only. Feature map: 18.11 (system "Live Updates"); remote config: 49.4.

## Decision: grow out of content delivery, not a new system

Content delivery (`shared/Content*.hpp`, 18.11) already had most of the core: a signed manifest of data and scripts (never code),
a sync state machine over an abstract `Transport`, resume by byte range, SHA-256 checks before a file is visible, the previous version
kept, a commit record written last, a replay floor, `Rollback`, and an overlay that serves downloaded files first and keeps the last
good version when offline. A second system beside it would be a copy (rules 19c, 19d). So Live Updates IS content delivery, extended
in place, and gets the one name "Live Updates" (rule 19e; the old names are aliases).

What was extended:

| Existing | Extended with |
|---|---|
| `ContentManifest.hpp` | kind `config`; headers `min-game` (the exported game's own version) and `apply` (when to make it visible); headers in any order; `Diff` (added, changed, removed) |
| `ContentTransport.hpp` `Transport` | is now the storage provider: `List` and `Put` (default "not supported"), `GetWhole`; `FileTransport` lists and puts (temp file + rename) |
| `ContentSync.hpp` | `gameVersion`; `stageOnly` -> `Status::Staged`; `ApplyStaged` (no network; signature and every hash checked again); `rolled-back.txt`; start-up guard `BeginLaunch` / `MarkLaunchGood` |
| `tools/okay-content` | file-kind table and folder scan moved to `ContentPublish.hpp`; `publish <folder> <dest-folder>`; `--min-game`, `--apply` |
| `GameContentValidators.hpp` | `.okaylive` checked before it is visible |
| Cvars (`Core/Cvar`, 49.4) | the remote source they lacked: `LiveConfig::ApplyTo` |

What is new (no older code did these): `ContentPublish.hpp` (publish only what changed), `ContentPolicy.hpp` (`CheckCompat`,
`DecideApply`), `ContentLiveConfig.hpp` (`LiveConfig`), `ContentBackendStorage.hpp` (`StorageTransport` over the existing
`okay::backend::Storage` interface). Nothing in the core names a storage service; the Supabase provider stays behind
`okay::backend::Storage`.

## How the pieces fit

* **Content separate from programs.** The exported game ships its programs; content versions come from the signed manifest and are
  read through the overlay. A content version needs no rebuild. `min-app` (engine) and `min-game` (the game) say what it needs.
* **Storage provider.** `Get` (whole or from a byte offset), `List`, `Put`. Local folder: full (tests, offline, LAN). OkayBackend
  Storage: whole-object reads (no range, so a cut download restarts that file), listing through folders, upload with replace.
* **Publish.** Check everything first (manifest, signature against the game's key, every local file's size and hash, version goes up),
  send new and changed files, re-send unchanged files the server lost, then the signature, then the manifest last. Between the last
  two a client can see a new signature with the old manifest and refuses it for one run (safe, keeps its content). Removed files stay
  on the server (a client still finishing the previous version may need them).
* **Delta download.** Unchanged files are linked from the previous version; only changed files are fetched (`Sync`, unchanged).
* **Staged apply.** `stageOnly` downloads and verifies into `trees/<n>.tmp` and writes `staged.txt` last. `ApplyStaged` makes it
  visible when `DecideApply` says so: start-up always; `Immediately`; `AtLoadingScreen` when one is up; `BetweenMatches` when no match
  runs; `NextLaunch` only at start-up. The manifest's `apply` beats the game's default; neither means next launch.
* **Rollback.** `Rollback` (a failed hot-apply, or by hand) puts the kept previous version back and records the bad one in
  `rolled-back.txt`, so it is never downloaded again; only a newer version is taken. The start-up guard counts starts that never reach
  `MarkLaunchGood`; at 2 the version is rolled back. A version that started fine once is not blamed later.
* **Signing.** Only the existing code: `CheckSignature` / `MakeSignatureFile` (`okayac::ed25519`, libsodium where it is built in;
  rule 18). A live config is a file in the signed manifest, so its hash is signed; it needs no key or signature of its own.
* **Compatibility.** `CheckCompat(server, client, rules)`: protocol ranges must meet; game version exact, same major.minor, or any;
  content equal by default, a client may be N versions behind; a client ahead is refused unless allowed. Says who has to update.

## Not done yet (next rounds)

1. Editor Live Updates window: publish, schedule, monitor, pause, roll back (on top of `ScanFolder`, `PublishVersion`, `Diff`).
2. OkayBackend: tables for versions, schedules and the pause switch; publishing from the editor through `StorageTransport` with the
   signed-in account; server-side scheduling.
3. Store files under their SHA-256 on CDN storage, so a cache can never serve old bytes under a changed file's name (today the sync
   refuses such bytes and the update waits).
4. Background download in the running game (a job running `Sync` with `stageOnly`) and a progress display.
5. Hot-apply in a running game: scenes, Visual Script graphs, assets reloaded after `ApplyStaged`; a failed reload calls `Rollback`.
6. Player integration: `stageOnly` / `ApplyStaged` at the `DecideApply` moments, `BeginLaunch` before content loads and
   `MarkLaunchGood` after the first scene, `gameVersion` from the build, `LiveConfig` loaded from the overlay and `ApplyTo` the Cvar
   registry, `CheckCompat` in the multiplayer join step (NetworkManager), the loaders still listed as "not wired" in
   `docs/release/content_delivery.md`.
7. Cleaning removed files off the server after every client has moved on.

Unverified: everything on Windows (rename over an existing file, hard links, file locking) and against a real storage bucket; the
OkayBackend provider was tested with an in-memory Storage only.
