# Script protection (internal record)

Owner request: make it harder for players to read a shipped game's script source. Owner's
reason, in their own words: "not sure, just want it locked down generally." This is a
deterrence feature, not a security boundary: the ceiling is explained below and repeated in
the Build Settings UI text, because the game itself has to be able to run the content it
ships.

## Existing, before this work

- **Scripts ship as plain text.** `.okay` scripts (`engine/src/Script/*`: `Lexer.cpp`,
  `Parser.cpp`, `Checker.cpp`, `Interpreter.cpp`, `NextScriptVM.cpp`) and the older VM
  (`engine/src/Scripting/OkayScriptVM.cpp`) are both **tree-walking interpreters** — they
  read and run the source text (or a thin AST built straight from it) at load time. There is
  no bytecode form for either one. A shipped game's `.okay` files are byte-for-byte the
  author's source, comments and all, sitting in the data folder or inside the `.zip`/pak —
  anyone can open one in a text editor, or unzip the archive, and read it.
- **Our own Lua (`engine/src/Lua/*`, branch `claude/scripting-migration-1`) is not merged
  into `main` yet** as of this writing (checked by looking for `engine/src/Lua` on this
  checkout and in `git log --all`; the commits exist on that branch, HEAD here is
  `c203e6e1`). When it lands, the same problem applies: it is also a tree-walking
  interpreter with no bytecode-only export mode, so `.lua` files ship as text too.
- **`DataPack` (`engine/include/okay/Core/DataPack.hpp`) already obfuscates packed data**
  with a keystream XOR (`splitmix64` mixer) plus an FNV-1a checksum for tamper detection.
  Its own doc comment is already honest about this: *"this is obfuscation, not strong DRM...
  a determined attacker can still recover the data."* The key (`DefaultKey()`) is a constant
  baked into the engine source, so it is identical in every build and ships inside every
  player binary — it stops a casual look in a hex editor, nothing more. `DataPack` is not
  wired into the script path today; `BuildPipeline` copies scripts as plain files.
- **`BuildPipeline` (`engine/include/okay/Core/BuildPipeline.hpp` /
  `engine/src/Core/BuildPipeline.cpp`) has no script-specific step.** Scripts are just one
  more `FileKind::Script` asset the dependency walk (`WalkAssets`) finds and copies
  verbatim into the data folder. There is no Build Settings toggle for scripts today, and no
  minification, stripping, or renaming pass anywhere in the export path.
- **Crypto that is already approved and in the engine**: `okay::net::netcrypto`
  (`engine/include/okay/Net/NetCrypto.hpp`) wraps libsodium (`crypto_aead_xchacha20poly1305`
  for AEAD, `crypto_kx` for key exchange, `crypto_sign` for signatures), and
  `okay::net::SecureChannel` builds the UDP transport's authenticated encryption on top of
  it. Per rule 18, libsodium stays the default and `okay::crypto` (behind `OKAY_OWN_CRYPTO`,
  off by default, unreviewed) is an alternate backend selected by the same macro —
  `netcrypto` already hides that choice behind one set of calls, so anything built on
  `netcrypto::AeadSeal` / `AeadOpen` gets both backends for free and needs no new
  cryptographic code.
- **`shared/OwnSwitches.hpp`** is a *runtime* on/off table for engine backends chosen from
  Settings (own WAV reader, own HTTP client, ...), applied via environment variables at
  process start. It is not a fit for a *build-time, per-project* Build Settings toggle like
  this one (script protection is a property of one game's export, not of which engine
  backend the editor runs with), so this feature adds its own Build Settings toggle instead
  of a new `OwnSwitches` row — see Stage 2 below.
- No existing Build Game test harness runs the export headlessly end to end; `tests/` has
  unit tests for `BuildPipeline`'s pieces (layout planning, the asset walk, the zip writer,
  the manifest) but nothing that builds a full game folder and scans it. Stage 3 extends
  that unit-test style rather than inventing a headless Build Game harness, because building
  one was out of scope for the time available here (see "Not done").

## The gap

Nothing today stops a player from reading a shipped game's script source, because nothing
changes it between the editor project and the shipped game. `DataPack`'s obfuscation exists
but is not applied to scripts.

## The plan (what "protect script source" means here)

1. **This doc.** Done.
2. **Build-time script compaction** (`ScriptProtect`, Stage 2): strip comments and collapse
   whitespace losslessly — i.e. an exported game's `.okay` text keeps only what the
   interpreter needs to run, with no comments and no extra formatting. **Identifier
   renaming is not implemented** — see "Not done" below for why.
3. **Debug-info stripping** (Stage 3): opaque script ids in runtime error messages when the
   toggle is on, with the real id -> file map kept only in the editor project.
4. **Optional encrypted pak** (`ScriptPack`, Stage 4): a new pak mode that calls
   `okay::net::netcrypto::AeadSeal` / `AeadOpen` (the same already-approved libsodium AEAD
   `SecureChannel` uses) with a per-build random key baked into the binary. This is still
   "the key ships with the lock" — document that plainly, it is not an upgrade in the actual
   security ceiling, only in how much work it takes a casual reader to get past (a raw
   `strings`/hex-editor look at the pak finds nothing; pulling the key out of the shipped
   binary and writing an unpacker still works for a determined reverse engineer, same as
   `DataPack`).
5. **Honest docs and UI text** for all of the above. No "military-grade" language anywhere.

## What each stage actually buys, plainly

- Stage 2 defeats: opening the `.okay` file in Notepad and reading it, or skimming a comment
  that explains the author's logic. It does not defeat: a determined person who diffs
  outputs, or who simply runs the (still fully functional) stripped script through the same
  interpreter to see what it does — the logic is unchanged, only its text is smaller.
- Stage 4 defeats: unzipping the pak and reading the scripts directly, or finding them with
  `strings`. It does not defeat: dumping the key out of the running process's memory or the
  binary on disk (the key has to be there for the game to run), which a reverse-engineering
  tool can automate. This is the same ceiling `DataPack`'s own doc comment already states,
  now stated here for the encrypted path too.
- `OKAY_OWN_CRYPTO` users (rule 18, off by default) get the identical `ScriptPack` behavior
  through their own backend automatically, because `ScriptPack` is written only against
  `okay::net::netcrypto`, never against libsodium or `okay::crypto` directly — this feature
  does not change which crypto backend is the default, and does not touch
  `okay::crypto`'s own code at all.

## Rule-18-adjacent decisions (why these are safe)

- No new cipher, hash, MAC or key-exchange scheme is introduced anywhere in this work.
  `ScriptPack` calls `okay::net::netcrypto::AeadSeal`/`AeadOpen`/`Random` — functions that
  already exist and are already used by `SecureChannel` — with a fresh random nonce per
  pack (required for AEAD; reused only never) and a fresh random key per build (so a leaked
  key from one game build does not expose another game's packs). Nothing here is a primitive;
  it is a second caller of an existing one, the same relationship `SecureChannel` already has
  to `netcrypto`.
- The checksum used by Stage 2/3 tooling (for self-tests, not for tamper detection of the
  shipped pak) reuses `okay::buildpipe::HashText` (FNV-1a, already in `BuildPipeline.hpp`)
  rather than adding another hash.
- `ScriptPack`'s tamper detection is the AEAD authentication tag libsodium already computes
  (`crypto_aead_xchacha20poly1305_ietf_decrypt` fails closed on a bad tag) — no separate
  checksum was added for that purpose, unlike the older XOR-based `DataPack`, because AEAD
  already gives that for free.

## Not done / unverified (say this honestly)

- **Identifier renaming (part of Stage 2) is not implemented.** OkayScript calls many of its
  top-level functions by name at runtime from the engine side (event hooks such as
  `on_menu`, `on_gui`, `on_update`, `on_collision`, and similar — see
  `examples/editor-plugins/*.okay`), and globals/engine API calls are resolved by name
  through `EngineApi.cpp`. Renaming local variables and private functions only, while
  reliably detecting every name the engine dispatches to by string (including names built
  from `@window`-style script metadata comments, which this stage already strips), needs a
  full scope analysis keyed to the interpreter's own name-resolution rules. Getting this
  wrong silently breaks a shipped game (a hook the engine looks up by name stops firing).
  Given the time available, shipping a renamer that is only *probably* safe was judged worse
  than shipping comment/whitespace stripping alone, which already removes everything a
  casual text-editor read needs and cannot break behavior. This is flagged here, in the
  Build Settings UI text, and in code comments as a deliberate gap, not an oversight.
- **Stage 3's runtime wiring (error messages actually showing opaque ids at the call sites
  in the interpreter) is not wired into `Interpreter.cpp`/`NextScriptVM.cpp`.** The id-map
  type and round-trip (`ScriptIdMap`) are built and tested in isolation; connecting it to
  every place a script error is currently formatted with a file path was not done in the
  time available.
- **No Build Settings UI toggle was added to the editor** (`editor/` was not touched). The
  `BuildPipeline` toggle this doc describes (`ScriptProtectOptions`) exists as a struct and
  functions other code can call; wiring a checkbox into the editor's Build Settings window
  and into the actual `--build-game` copy step was not done.
- **No end-to-end Build Game run was done.** Everything below was checked with focused unit
  tests against the new headers directly (`tests/test_script_protect.cpp`,
  `tests/test_script_pack.cpp`), not by running the editor's export or `OkayEngine
  --build-game` and scanning a real output folder, because no headless Build Game harness
  existed to extend (see above) and building one was out of scope for the time available.
  Treat "a build with the toggle on strips comments from the shipped files" as **unverified
  beyond the unit tests** until that harness exists.
- Windows/MinGW win32-threads constraint (rule 21a) was kept in mind: `ScriptProtect.hpp`
  and `ScriptPack.hpp` use no `<mutex>`, `<thread>`, `condition_variable` or `call_once`.
  This was checked by reading the headers, not by actually cross-compiling for Windows in
  this task.
