# Downloadable game content (content delivery)

An exported game can fetch new **data and scripts** from a server after it is installed: 3D models (characters, vehicles,
buildings), textures and materials, animations, maps (a scene plus its assets), audio, dialogue, and Visual Script / OkayScript
files that use the systems already in the engine. It is **off by default**: with no content URL configured the player does
nothing new.

**What it can never do:** download or run engine code or native programs. There is no "program" kind, the manifest refuses
file names such as `.exe`, `.dll`, `.so`, `.dylib`, `.bat`, `.ps1`, `.wasm` (even when correctly signed), and nothing in the
sync loads a file as a library. This is why it is allowed on a UWP / Store app (the Xbox build): a Store app may update its
data and scripts, but must never download and run code. A downloaded script runs with exactly the powers a shipped one has (see
"Scripts and graphs").

Written from the plain description in this page; the only outside knowledge used is the SHA-256 and ed25519 standards, which
come from the existing code (`okay::crypto::Sha256`, `shared/UpdateSignature.hpp`). No new cryptography, no third-party library
or SDK. Try it: `docs/try_it/content-delivery.md`.

## What existed, and the gap

| Existing | What it does | Gap for game content |
|---|---|---|
| Launcher updater: `launcher/VersionsManager.*`, `UpdaterCore.hpp`, `scripts/publish-supabase.sh`, Supabase bucket `releases`, `versions.json` + `versions.json.sig` (`shared/UpdateSignature.hpp`, replay floor in `shared/UpdateGuard.hpp`) | Updates the three programs (launcher, editor, player), signed index, checksums | Programs only. Not for a game's own files. Its signature domain is fixed to the release index |
| Official game updater: `shared/OfficialGame.hpp` (`manifest.json` with `{path,size,sha256}`, staging, journalled swap), `scripts/publish-official-game.sh` | Installs and updates the one official game by replacing files in its install folder | Manifest is **not signed**; whole files only, no resume of partial files, no previous-version rollback, install folder (not writable on UWP) |
| HTTP: `okay/Platform/NativeHttp.hpp` (WinHTTP on Windows; our own HTTP/1.1 client on Linux with `OKAY_OWN_HTTP`), `RequestEx` with `resumeFrom` | GET with headers, streams to a file | Not used for game files. WinHTTP is not available to UWP apps |
| Paths: `Core/Paths`, `Core/PathSafety.hpp` (one place that decides if a name may be opened), `Core/ScriptPaths.hpp` (script sandbox), `Save::BaseDir`, `buildpipe::FindDataDir`, StreamingAssets | Where a game's files and saves live; safe-name rules | No second lookup folder. Loaders open `baseDir + name` directly |
| `Core/DataPack` (`OKP1`) | Obfuscates shipped data | Obfuscation, not signing; not a delivery mechanism |
| Xbox UWP (branch `claude/xbox-uwp-2`, `platform/xbox_uwp/XboxPaths.hpp`, `XboxManifest.hpp`) | Install folder read-only, `LocalFolder` writable; `internetClient` capability is a default of the generated manifest (tested there) | Not in `main` yet, so nothing here edits it. No network transport |
| Feature map: nothing about asset bundles, DLC or patching | | This is the first |

Reused, not copied: `pathsafety::Check` for every name, `okay::crypto::Sha256` for hashes, `okayupdate::ParseKeys` and
`ParseSignature` and `okayac::ed25519::Sign/Verify` for the signature, `okay::http::Request` for the network, `ProjectVersion`
for version comparison, `fake_storage.py` for the publish test. What is new is the manifest format, the sync state machine, the
overlay lookup, and the glue.

## How it works

```
publisher                                   server (Supabase bucket, any static host, or a folder)
  okay-content make-manifest <folder>  ->     <base>/content.manifest        (signed text list of files)
  scripts/publish-content.sh                  <base>/content.manifest.sig    (ed25519 signature line)
                                              <base>/<path>                  each file, same folder layout the game uses
game (player, at start-up, only when a content URL is set)
  1. GET content.manifest and .sig   -> check signature, parse strictly, check game id, version floor, app version
  2. plan: which files are new or changed (by SHA-256), which are reused from the previous version, budget check
  3. download only the changed files into staging (resumable), verify size and SHA-256, move into trees/<n>.tmp/
  4. script / graph validator on code-like files
  5. rename trees/<n>.tmp -> trees/<n>, write manifests/<n>.manifest, write current.txt LAST (the commit record)
  6. tidy: remove versions older than the previous one, and say so
  then: load the overlay, so each file is looked up in the downloaded content first, then in the installed files
```

### Manifest format (`content.manifest`)

UTF-8, `\n` line ends, TAB between fields, no blank lines or comments. Anything else is refused.

```
okay-content 1
game mygame
version 12
min-app 2.886.0
file<TAB><sha256><TAB><size><TAB><kind><TAB><group or -><TAB><min-app or -><TAB><path>
```

| Field | Rule |
|---|---|
| `game` | `[a-z0-9._-]`, 1 to 64. A client accepts only its own game id (a signed manifest of another game made with the same key is refused) |
| `version` | whole number 1 to 2^53, no leading zeros. Must go **up** with every publish. A client never goes to a lower one (replay floor) and refuses the same number with different bytes |
| `min-app` | optional; the whole manifest needs at least this engine version, else refused with "update the game first" |
| `kind` | `model texture animation scene audio dialogue script data`. No kind for code |
| `group` | optional tag (for example `hd`, `voice-fr`). A game may ask for only some groups; files with no group always come |
| per-file `min-app` | optional; older apps skip that file (counted as skipped) |
| `path` | relative, `/` only, passes `pathsafety` (no `..`, no absolute path, no drive letter or `:`, no device names such as `NUL`, `COM1`, no trailing dot or space, no control characters), no `//` or `.` parts, must not start with `.`, no executable extension; two paths that differ only by letter case are refused (Windows would merge them); a name cannot be both a file and a folder; files must be in strict byte order by path |

Limits (all checked before anything is downloaded): manifest at most 4 MiB, at most 20,000 files, one file at most 2 GiB, all
files together at most 8 GiB; a game can set a tighter budget (`budget-mb`). A manifest over the budget is **refused whole**
("nothing was downloaded and nothing was deleted"); nothing is evicted silently.

### Signature

`content.manifest.sig` is one line `okay-sig1 <128 hex>`, an ed25519 signature over `"okayspace-content-manifest-v1\n"` + the
manifest bytes. The domain prefix differs from the release index's, so a signature made for `versions.json` can never be valid
here even if the same key signs both (tested). The game trusts one or two public keys (two during a key rotation).

Unlike the release index there is **no transition mode**: with no key configured, content is refused. The only way to skip the
check is the explicit developer switch `OKAY_CONTENT_ALLOW_UNSIGNED=1`, which logs a warning every start.

Make a key pair with the existing tool: `python3 scripts/sign_index.py genkey` prints a seed (private: keep it, never commit
it, never paste it in chat) and the public key. The seed goes in `OKAY_CONTENT_SIGNING_SEED` when publishing; the public key
goes in the game's `content.okayconfig` as `keys=`.

### Folder layout on the player's machine

```
<save folder>/content/        (the game's per-user writable folder; on UWP: under LocalFolder, never the install folder)
  current.txt                 commit record: game, visible version, SHA-256 of its manifest, previous version, floor
  manifests/<n>.manifest(.sig)
  trees/<n>/<path>            the files of version n, same layout the game uses (a scene's relative references keep working)
  trees/<n>.tmp/              being built; verified files only; resumed by the next run
  staging/<sha256>.part       partial downloads; never visible to the game
```

Unchanged files are hard-linked (copied if the disk cannot link) from the previous tree, so an update downloads only what
changed. The previous version stays until the next update commits, so there is always one to go back to (`okay-content
rollback <dir>`). `floor` is the highest version ever committed and is never lowered, even by a rollback.

### Downloader rules

* Transport interface (`shared/ContentTransport.hpp`): `Get(name, rangeFrom, maxBytes, handlers)`; the state machine has no
  sockets, no threads. Real transports: `file://` folder, HTTP(S) (`shared/ContentHttpTransport.hpp`), Xbox stub.
* A file is accepted only when its size and SHA-256 equal the signed manifest's. More bytes than the manifest says: cut off
  at the size, partial thrown away (a hostile server cannot fill the disk). Wrong bytes: thrown away, retried, never visible.
* Resume: a partial file is continued with `Range: bytes=N-`. A `206` that starts anywhere but N is never spliced in; a `200` to
  a range request restarts the file; a `416` restarts it.
* Retry: up to 4 tries per file, wait 0.5 s, 1 s, 2 s (doubling, capped at 8 s). 404 and other 4xx (except 408, 429) stop
  at once with "the server does not have ...". 5xx, timeouts and cut connections retry.
* Cancel: the progress callback returns `false`; partial files stay for the next run.
* Same bytes under two names download once.
* The previous version stays usable until the commit record is written. A crash or cancel at any point leaves it in place.

### Overlay (`shared/ContentOverlay.hpp`)

`Overlay::Resolve(name, installRoot)` returns the downloaded file if the committed manifest lists `name`, else
`installRoot/name`, else "". Rules: the name goes through `pathsafety` first (so `..`, absolute paths, drive letters, device
names, ADS streams find nothing in **either** layer); a name not in the verified manifest is never found, even if a stray file
sits in the folder; lookup is case-insensitive on every platform; `\` is accepted as a separator; a symbolic link pointing out
of a folder is refused; the signature is checked again on load. `Overlay::Global()` is set once at start-up and is read-only
after, so there is no lock.

The player (`player/main.cpp`) uses it for: the scene list and start scene, `items.okayitems`, `weapons.okayweapon`,
`strings.okayloc`, 2D images loaded by the UI painter, and `.obj` meshes of mesh renderers. **Not wired yet** (still read from the
installed folder only): audio (`LoadSceneAudio`), `DataAsset`, `Character` clips, behavior trees, `.okayvs` graphs a component
opens by path, fonts, models other than `.obj` (glTF and FBX loaders) and prefabs. Each is a one-line change to call
`contentPath(name)`/`Overlay::Find`, but each needs its own check that nothing else assumes the install folder. A content file
the game never asks for through one of the wired paths has no effect.

### Scripts and graphs

Content of kind `script`, `dialogue` or `data` goes through the `validate` hook **after** it is verified and **before** it is
visible (`shared/GameContentValidators.hpp`, used by the player):

* `.okayvs`: `okay::vs::ValidateGraphText`, the checker the editor and Project diagnostics use. Any error (severity 0), parse
  failures included, refuses the file and the **whole update** (warnings are allowed).
* `.okayscript`: `okay::OkayScriptVM::Validate` (parse and syntax check; nothing runs).
* Empty, over 4 MB or unreadable: refused. A refused file is deleted with its half-built version; the old version stays.

What is **not** new: when the game later runs a downloaded graph or script it uses the ordinary loaders, so the existing runtime
safety applies unchanged: `ScriptPathOk` (script file access only inside the game and save folders, symlink checks, device
names), the node and builtin set, the graph's own limits. The validator does not prove a script is harmless or finishes; it
proves it parses and the checker finds no error. The signature is what says who wrote it.

### Security model

An attacker who controls the server (or the network, or a CDN), but not the publisher's signing key, **can**: stop updates (deny
service), serve an older *validly signed* manifest only up to what the player already had (the floor refuses lower versions;
a player that has never synced can be given any old signed version), make the game download wasted bytes (bounded by the
manifest size and per-file size), learn which files and versions a player fetches.

**Cannot**: put a file on the player's disk that is not in a manifest the publisher signed (every file is checked by SHA-256);
change or add a file name, size, kind, or group (the manifest is signed as a whole); make a path escape the content folder or
overwrite saves or program files (path rules plus own folder); deliver code or a library (no such kind, extension refused,
nothing loads them); make a manifest for another game take effect (game id); make a half-downloaded update visible (commit
record last, previous version kept); fill the disk with an over-long body (cut at the manifest size).

An attacker who has the **signing key** can deliver any data and script the engine can run. That is the whole risk: keep the
seed offline, rotate with the two-key list if it leaks. An attacker with **local access** to the player's machine can edit the
content folder (and the game); the local check on load (manifest hash against the commit record, signature) catches accidental
damage and casual edits, but local disk is not a trust boundary against the device owner.

**Signed:** the manifest (game id, version, min-app, every file's path, size, SHA-256, kind, group). **Not signed:** the file
bytes themselves (they are covered by the SHA-256 in the manifest), transport, file timestamps, HTTP headers. The channel does
not need TLS for integrity (hashes and signature carry it) but use `https` anyway for privacy.

**Weaknesses and honest limits:** the publisher uploads files before the manifest, but files overwrite older ones under the
same name, so a player downloading during a publish may get a checksum mismatch and retry (never a wrong file). There is no
revocation list besides publishing a newer version. No delta of single files (a changed file is downloaded whole). No
encryption of content (anyone who gets the manifest URL can download the files).

### Xbox / UWP notes

* The content folder must be under `ApplicationData.LocalFolder` (`XboxPaths`: the install folder is read-only).
* `internetClient` is in the default capability list of `platform/xbox_uwp/XboxManifest.hpp` (branch `claude/xbox-uwp-2`, tested
  there at `test_xbox_uwp.cpp`); it must stay in the generated manifest of any game that sets a content URL. Not in `main`, so
  nothing was changed here.
* `HttpTransport` uses WinHTTP, which UWP apps may not call. `MakeTransport` returns `UwpTransportStub` when `OKAY_XBOX_UWP` is
  defined; it only fails with "Downloading game content is not available in the Xbox app yet". It needs
  `Windows.Web.Http.HttpClient` (WinRT) from the UWP glue, or our own HTTP over Winsock, behind the same interface. **Nothing
  here has run on an Xbox, and the UWP transport does not work.**
* Content is data and scripts, never code; that is what keeps this inside Store rules. Check the current Store policy before
  shipping.

## In the game (configuration)

`content.okayconfig` beside the game files (written by the publisher of the game), or environment variables:

| `content.okayconfig` | Environment | Meaning |
|---|---|---|
| `url=` | `OKAY_CONTENT_URL` | where `content.manifest` is. `https://`, `http://`, or `file:///folder` for a local test. **No url = the whole feature is off** |
| `game=` | `OKAY_CONTENT_GAME` | the game id the manifest must carry |
| `keys=` | `OKAY_CONTENT_KEYS` | public key(s), hex, comma separated |
| `groups=` | `OKAY_CONTENT_GROUPS` | only these groups (empty = all) |
| `budget-mb=` | `OKAY_CONTENT_BUDGET_MB` | most MB this game keeps |
| | `OKAY_CONTENT_ALLOW_UNSIGNED=1` | developer only: skip the signature (logged) |

The environment wins over the file. A key compiled in with `-DOKAY_CONTENT_TRUSTED_KEYS=<hex>` wins over both, so a shipped
game cannot be pointed at another publisher by editing a text file. The sync runs at start-up and **blocks** until done or
failed (no progress screen yet); a failed or offline sync keeps serving the last good content. The log shows one line:
`content: updated, serving downloaded content version 3`.

The `min-app` check compares with the engine version the player was built with (`BuildInfo::Version()`).

## Tools

* `okay-content` (built by default; shipped in `Tools/` of the Windows folder): `make-manifest`, `verify`, `sync`, `status`,
  `rollback`, `pubkey`. Details at the top of `tools/okay-content/main.cpp`.
* `scripts/publish-content.sh <folder> --game <id> --version <n>`: builds and signs the manifest, verifies locally, uploads files
  then signature then manifest **last** to Supabase Storage (bucket `game-content`, folder `<game>/`) over the plain REST API
  with curl, reads the published pair back and verifies it. Environment: `OKAY_CONTENT_SIGNING_SEED`, `SUPABASE_SERVICE_KEY` or
  `SUPABASE_ACCESS_TOKEN`, as `publish-supabase.sh`. `DRY_RUN=1` uploads nothing. A PowerShell version was not written; use
  Git Bash or WSL on Windows.
* There is no `okay-content serve`: for a local test use a `file:///` URL (no server needed), or any static host. A loopback
  server with Range support is in `tests/scripts/content_range_server.py` (Python, test only).

## Tests

| Test | What it covers |
|---|---|
| `content_delivery` (`tests/test_content_delivery.cpp`, 250 checks) | manifest format and limits, hostile paths, signature (wrong key, tamper, domain separation, rotation, no key), URL helpers, sync (fresh, up to date, changed files only, dedup, resume, retry and backoff, cancel and resume, corrupt, over-long and lying servers, 404, replay floor, same-version change, game and app version, groups, budget), validator hook, rollback, overlay (precedence, fallback, escapes, case, links, tampering, stray files), start-up wiring (off by default, offline fallback) |
| `content_validators` | the real graph checker and script parser refuse bad files before they are visible |
| `content_cli` | `okay-content` end to end, including a real HTTP download from a loopback server that cuts the first response (when built with the own HTTP client) |
| `publish_content_script` | `scripts/publish-content.sh` against the fake storage server: upload order, no secret printed, refusals upload nothing |

## Verified, unverified, not done

Verified on Linux: everything in the Tests table except where noted. `test_content_delivery` was also run under AddressSanitizer
and UBSan, clean. The HTTP path was exercised with the own HTTP client over plain `http://` on loopback.

Verified under Wine (Linux, not a real Windows PC): `okay-content.exe` cross-compiled with the MinGW win32-thread-model compiler
(`-fsyntax-only` is also clean for all new headers, so the Windows build rule holds) ran `make-manifest`, `verify` and `sync` with
Windows paths from a `file:///Z:/...` folder (download, hard-link reuse of an unchanged file, a version 2 update that downloaded
only the changed file), and a `sync` over `http://` through **WinHTTP** from the loopback server that cuts the first response
(one retry, then complete). Wine is not Windows: treat this as a good sign, not as proof.

Known Windows limit: the WinHTTP wrapper in `NativeHttp.hpp` throws away the body of a response that was cut short, so on Windows an
interrupted download restarts that one file on the retry (finished files are kept). Resume with `Range` works for a file whose
earlier part was saved by a cancelled run or by the own client (Linux).

**Unverified:** anything on a real Windows PC (hard links on NTFS, `rename` over an existing file, Windows path spellings under
real file locking and antivirus); `https://` (TLS is the existing client's job, not exercised here); a real Supabase bucket (only
the fake storage server was used; the bucket name, size limit and object-name characters are assumptions to check once); the
player wiring on screen (`player/main.cpp` was only compiled as a syntax check, and not started); the Xbox transport (a stub);
behaviour with a very large file or a slow network; disk-full handling beyond a plain failure message.

**Not done:** a Visual Script quick block "Update Game Content" (the node would need a place to hold a running sync and a
progress display; no good small place was found, so it was left out rather than faked); a progress screen or background sync (the
player waits at start-up); the loaders listed as "Not wired"; a PowerShell publisher; `okay-content serve`; delta updates;
a revocation list; the editor's Build Game does not write `content.okayconfig` yet (copy it by hand beside the exported game);
unifying `OfficialGame.hpp` (unsigned manifest, no resume) with this system.
