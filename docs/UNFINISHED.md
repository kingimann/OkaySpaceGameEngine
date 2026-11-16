# Unfinished features

What is not finished yet, so we can pick it off. Anything unfinished is hidden or labelled in the product (see "Hidden or limited today"); this page is the to-do list behind that.

Sources: the feature map (`docs/FEATURE_MAP.md`, rows marked Partial or Planned, with their Open notes) and the hand-written lists below.

The feature map currently has **124** rows that are Partial or Planned.

## Audits

- [Engine-wide audit](unfinished_engine.md): 93 findings across 14 subsystems (9 High, 58 Medium, 26 Low), each with the file and line, what is missing, impact and size, and a top 15 to tackle first. The High items: accessibility settings that nothing reads, SSAO on Direct3D 11 (written, unrun on a GPU), no post-processing on Direct3D 12 (labelled preview), the editor's Play differing from a built game on keys and gamepads, a mesh collider that is only a bounding box, navigation without off-mesh links or area costs, audio that is mono and fully decoded, editor undo that uses full-scene snapshots, and a save framework with no editor UI.

- [Launcher audit](unfinished_launcher.md): 38 findings in six areas (6 High open, 2 more High done in code and waiting on the owner to run SQL; 15 Medium, 15 Low), with file and line, impact and size, and a top 10 to tackle first. The High items: typing indicators and read receipts need database tables no SQL file creates, voice calls need a deployed voice server, blocking and privacy settings only filter on the device, there is no user report button, the forgot-password email has no landing page, no account deletion or data export, and updates are not signed (kick, ban and admin roles for servers and group chats are now built; the owner applies `docs/sql/member_moderation.sql`).

- [Install, uninstall and editor versions audit](install_audit.md): a file map of everything Setup, the launcher and the editor write, the installer scenarios (run under wine), the Versions page cases, 24 ranked findings and a 12-item plan (phase 1, audit only; nothing in the program changed). The worst: an old kept editor can update itself in place and reopen an old project without asking, "Remove version" can delete a project made inside the version folder, Setup says projects are in Documents (they are in the install folder), and uninstall leaves most of what the launcher made. Owner decisions are listed in its section 7. A comparison with three well-known launchers is [install_comparison.md](install_comparison.md).

## Hidden, removed or limited today

| What | State | What is missing |
|---|---|---|
| Natural walk and run (`docs/walk_vs_run.md`, `docs/try_it.md` row 7r) | Done in code and tests; Linux only | Not run on Windows or a GPU renderer, no real Mixamo model used (test rigs only), and whether 2 m/s walk and 5.5 m/s run feel right in a real game is the owner's call (one number in `NaturalGait.hpp`). Older projects keep their saved speeds until the Inspector's **Use natural walk and run speeds** is pressed: an automatic upgrade would need an `okayscene` format bump and an opt-in in the upgrade window (written down in the doc, not built). The 2.5D, top-down, click-to-move, NPC and `CharacterController3D` defaults were not changed (their W now shows the jog it really is). The walk's hips bob about 8 cm (a person's 3 to 5): that needs stance-knee flex and pelvic tilt. |
| One inflate (`engine/include/okay/Core/Inflate.hpp`) for PNG, .blend, .x, ZGL, 3MF/AMF, FBX arrays and the own HTTP client | Done in code (the three older copies are deleted from the tree; ImageDecode and FbxReader keep thin wrappers, see `docs/try_it.md` row 15) | Checked on Linux only: `tests/test_inflate_differential.cpp` against a slow reference written from the RFCs (about 67,000 inputs and 15,500 gzip inputs per run, ASan and UBSan clean) and, before the old copies were removed, against those copies (257,014 inputs, no disagreement; commit `0ea3b26a`); `png_hint`; not run on Windows or macOS, not run on real PNG, `.blend`, `.x`, FBX or gzip-download files from outside our own tests; `model_readers` and `own_http_net` not run for this change. Two behaviour notes: PNG decoding stops as soon as all expected pixel bytes are decoded (a damaged tail after them is no longer examined), and the first output allocation is now limited to what the input could produce (1032 times its size), so a PNG header claiming a huge size with a few bytes of data no longer zero-fills that much first. The shared `FixedCodes` table is a function-local static built on first use: C++11-safe, but not proven under the MinGW win32 thread model (no `<mutex>` is used) |
| Start-up update screen | Built, unverified in the real app | On for new installs and after "Reset to defaults" only (existing configs already hold a saved `update_on_launch`). Checked by unit tests (`startup_update`) and a syntax-only compile of the launcher on Linux. Not run: a real download or restart, Windows, a slow or offline network, the OkayUI backend (the panel is drawn with Dear ImGui after the OkayUI frame; the draw order there is unverified). `docs/updater.md` |
| Test builds (the dev channel: `.github/workflows/dev-build.yml`, `scripts/publish-supabase-dev.sh`, Settings > Behavior > Get test builds, Go back to the stable version) | Built, off by default, never run end to end | The workflow has never run on GitHub, and can only be started once it is on `main`. The publisher has only written to a fake storage server (never the real Supabase bucket; its upload, list and delete calls are the release publisher's). Nothing ran on Windows: no launcher or editor screen was seen (no screenshot, the launcher was not built here), and the restart, the self-check and the rollback were never watched for a four-number version or for the way back (a downgrade, the only one the updater does). Checked on Linux: `dev_channel`, `dev_index`, `dev_publish_script`, `dev_workflow_lint`, `self_update`, and a syntax-only compile of the launcher and editor with g++ and the MinGW win32 compiler. Not done: saying how far a branch is behind `main` (a test build has only what the branch has), delivering files other than the three programs (a new example scene is not in a test build), signing needs the seed as a CI secret once launchers trust a key, the OkayUI preview What's New page and a way to start a test build without GitHub. `docs/release_channels.md` "Test builds", `docs/try_it.md` 7v |
| Own stack dogfood switch (`OKAY_OWN_STACK`) | Developer and CI only | Seen working on Linux only: the `own-stack` CI job has run green on GitHub (main run 43754132084, first green on main in run 43656201342), nothing was run on real Windows, the job builds without Assimp so it does not cover the readers' automatic fallback to the old reader (I found no test of that fallback), and the switch covers only 20 readers (FBX and 19 formats), not the 16 newest readers or WAV. Own crypto is only ever in test builds (`OKAY_OWN_STACK_CRYPTO_TEST`) and releases refuse it. Plan and gates: `docs/own_stack_rollout.md`; which flips come next and in what order: `docs/own_flip_proposal.md` |
| Prep work before any own-code default can flip ("Step 0" of `docs/own_flip_proposal.md`) | Not started | A real-Windows CI run of the reader and WAV tests; a CI build equal to the release after a flip (Assimp on, own readers on) and a test that our reader's refusal falls back to Assimp or ufbx; the 16 newest readers and WAV added to `OKAY_OWN_STACK` and the startup line; one log line per import saying which reader ran; the real-file corpus run in CI (job `assimp-models` already has the files); a sample file for 32 of the 35 reader switches; sanitizers for the engine-linked reader tests in CI; run-time switches `OKAY_OWN_STL` and `OKAY_OWN_PLY`; a reader compare tool the owner can run on their own files; optionally a Settings row as a way back |
| Release libsodium guard (`OKAY_REQUIRE_SODIUM`, pinned download, `check_release_own_stack.sh --cache/--binary`, `crypto=` startup word) | Release builds | Compiled and tested on Linux only (guard self-test, pin test, a real Linux configure). The Windows release path (`build-windows-dist.sh`, `release.yml`) was edited but never run on GitHub or Windows; a Windows .exe cannot be asked for its startup line on the Linux runner, so only the cache and compiler-flag checks cover it. The libsodium SHA-256 pin is trust on first use (2026-11-14), the project's `.minisig` was fetched but not verified. Not covered: the macOS and web builds (parked / no libsodium by design). `docs/own_stack_rollout.md` section 3.3 |
| Direct3D 11 depth of field, ambient occlusion, Bloom Softness and AO Power | Compile-checked only | Never run on a GPU or compiled by `D3DCompile`; the shared maths is CPU tested (`dof_math`, `ssao_math`, `post_quality_math`). The software renderer and Direct3D 12 ignore Bloom Softness and AO Power |
| Texture Filter, Wrap and Mipmaps import controls | Hidden (`kShowTextureSamplerSettings`) | Work in the software and OpenGL renderers; the Direct3D 11 renderer has per-texture filter and wrap code that has not been run, Mipmaps do nothing there, Direct3D 12 reads none |
| Import settings sRGB, Compress | Removed from the UI | The renderers do no colour-space handling and no compressed GPU path exists. Audio loading and mix-down are applied (see `docs/audio.md`); quality is not offered because audio is not re-encoded |
| OBJ/MTL textures with an absolute path | Limited (#753), partly restored | In the editor, an absolute or `../` texture path that lies inside the open project (links followed) is kept again (`okay/Render/TexturePath.hpp`, the project folder is the allowed root); one outside the project is still dropped, so the material has no texture: copy the texture into the project. Without a project (the player, tools), only paths inside the model's own folder are kept, as since #753. Both OBJ code paths use the same rule; `Mesh::LoadOBJ` (the merged-mesh import) used to keep any path with a folder in it as written. File names with `: ? * " < > |`, Windows device names (`con.png`) and parts ending in a dot or space are refused on every platform. Windows behaviour (case-insensitive compare, junctions, `\\?\` paths) is unverified: never run on Windows. Other formats keep their own rules |
| Models already in a scene on import-settings Apply | Limited | Stored meshes are not re-imported; the setting applies the next time the model is added |
| Own OGRE reader: binary `.mesh`, `.skeleton`, `.skeleton.xml` | Not started | Only the XML mesh geometry and `.material` colours are read (`docs/own_ogre_reader.md`, batch D); the binary forms and every skeleton, bone weight and animation are missing, so OGRE characters import rigid with Assimp absent. Same for the Irrlicht scene's animators (`docs/own_irr_reader.md`) |
| Own batch-D readers: strips and fans on real files, budgets on real big files | Unverified | OGRE `triangle_strip` / `triangle_fan` are checked on synthetic faces only (no real exporter file in the test tree uses them), and the per-file budgets (4,000,000 triangles for XGL and Irrlicht scenes, 2,000,000 subdivided AC3D corners) are limits chosen for safety, not measured against real large assets; nothing was run on real Windows (`docs/own_ogre_reader.md`, `own_xgl_reader.md`, `own_irr_reader.md`, `own_ac3d_reader.md`) |
| Own batch-E readers (MDC, HMP, CSM) | Unverified | Each is read on the real files of Assimp's test tree (1 MDC, 2 HMP, 1 CSM) and checked against numbers taken from the raw bytes, **not compared with Assimp** (by choice: written without reading or running another importer). HMP is HMP7 only and its header layout, the row orientation and the texture-coordinate direction are my reading of two sample files (no written spec found); MDC's compressed first frame (scale 0.05, bias 127) and tag rotation have no real example; CSM has no real file with missing markers. Off by default, slots 28 to 30; nothing was run on real Windows (`docs/own_mdc_reader.md`, `own_hmp_reader.md`, `own_csm_reader.md`) |
| Own Terragen reader (`.ter`): other writers and curve mode 1 on real files | Unverified | Off by default (`OKAY_OWN_TER`, `docs/own_terragen_reader.md`). Checked on the 2 real files in Assimp's test tree (both flat, both from Terragen) and on synthetic files; files from other writers (World Machine and similar tools) and a real curved (curve mode 1) terrain were not available, and nothing ran on Windows |
| Own Q3D and RAW readers: real files | Unverified | Written from public format descriptions; no `.q3o` / `.q3s` / `.raw` file was available, so neither was compared with Assimp or any real file. Q3D has open points (byte order, texture fields when the header names none, normals; `docs/own_q3d_reader.md`); binary RAW is not read |
| Own SIB reader | Not started | No public description of the Silo `.sib` format and no sample file were available (`docs/own_sib_reader.md`); slot 32 is reserved |
| Own IQM and COB readers (batch E3): compared with Assimp, texture v flip, COB handedness and animation | Unverified / partly done | Both read their real sample files (`mrfixit.iqm`, 8 `.cob`) and pass hand-computed tests, but no side-by-side with Assimp was run, the v flip (both) and trueSpace's left-handed Z-up (COB) are inferences not checked in a renderer, COB hole flags are not interpreted, **COB animation (`Chan`), bones and lights are not read at all**, nothing ran on Windows (`docs/own_iqm_reader.md`, `docs/own_cob_reader.md`) |
| Own NDO (Nendo) reader | Seam only, no geometry | Header check and refusal only (`docs/own_ndo_reader.md`); the body needs a format description we may follow or real files, none exists here. Nendo files still need Assimp |
| Own PMX and LWS / MOT readers (batch E5) | Unverified, off by default | **No real `.pmx`, `.lws` or `.mot` file is in the repository**, so both are checked on hand-built files only and not compared with Assimp. LWS assumes angles in radians, bank then pitch then heading, key times in seconds (also for version 1 and 2 scenes) and object paths inside the scene's own folder (LightWave's content directory is not searched). PMX reads SDEF as a two-bone linear blend, checks morphs but does not import them (no morph type in the engine), ignores IK, inherit rules, sphere and toon textures, and never parses display frames, rigid bodies, joints or soft bodies. Nothing was run on real Windows (`docs/own_pmx_reader.md`, `docs/own_lws_reader.md`) |
| Ten visual-script nodes: `Fn.net_join_link`, `Fn.net_pin_host`, `Fn.pf_*` (delete, leaderboard_top, load, login, login_password, register, save, set_stat) | Hidden from the palette (`kHiddenUntilTested`) | No test covers them; they need a network or PlayFab service |
| 15 online nodes with only indirect coverage (net host/join/relay/queue/browser) | Visible | Never run as nodes in a test; the calls under them are covered by network suites |
| Six engine-API nodes: Input.DefineAction, Input.DefineDirection, Audio.SaveSettings, Rigidbody.AddTorque (covered by other suites); Input.Rumble and Fog.SetMode proven | Partly proven | Show nothing in a headless scene |
| Voxel edit speed on real hardware (`docs/voxel_edit_perf.md`) | Measured on a CPU only | The digging cost was measured headless (no GPU): the CPU work per edit frame fell about 6x at the Sandbox size and about 15x on a big world, and the bytes handed to the renderer about 9x. The GPU upload and driver time, the Direct3D 12 renderer (it re-expands every visible mesh every frame, with or without digging), the editor's click path and anything on Windows were not run. A mesh "revision" number the renderers could trust instead of hashing was left out on purpose (it would touch `Mesh.hpp` and all renderers) |
| Voxel world edge | Limitation | A body past the edge of the voxel grid has nothing to collide with and falls forever (measured: y = -106 m after 5 s); the sides of the field are not closed in the picture either |
| Voxel real-life digging (`docs/terrain_water.md`) | Built, headless only | Materials, volume-exact store, debris, collapse, slump, undo and presets are tested on Linux. Windows, a GPU and the Inspector click path were not run. Not done: a per-material dig sound and particle (the table has the fields, the digger still plays one sound and one puff), debris colliders are boxes (not the piece's shape), debris assumes the terrain sits at the origin, a body that settles may take up to its life (20 s) because bodies were seen not to sleep in the physics core (unconfirmed, reported to the physics lane) |
| Sandbox frame rate on a real machine (`docs/terrain_audit.md`, whole-frame hunt) | Cause found and fixed headless, real machine unverified | The dig-time fps drop was a hidden full software render for particle depth (now only for live particles, no shadows, capped size). Not measured: real GPU time, the per-frame read-back in the player, which renderer the owner's machine actually uses (F3 now says), and the software renderer's cost for water (about 40 ms of 125) |
| Voxel digging, still to do (`docs/terrain_water.md`) | Partly built | Done headless: cave-in, budgeted generation, water/lava flow, footprints, foliage fast path. Not done: a far-reaching avalanche beyond bounded slump passes, flat-sheet water and fluid that pushes or floats bodies, piece colliders that follow the piece's shape (one box each; waiting for compound children on main), dig edits over the network (the host would have to own the field; today each player digs their own copy), per-material sound output (loaded, not heard), editor undo of voxel edits (unverified) |
| Photon, PlayFab and Custom net providers | C++ extension points only | Need the vendor SDKs and accounts; nothing in the editor selects them |
| Play start hitch in the editor | `EditorState::Play()` still runs on the click frame: snapshot text, loading every AudioSource file, validating every visual script, and (in the editor) baking each NavMeshSurface with Bake Before Play. Done: sources on one file share one decoded clip (no per-source copy; the cache checks file size and write time and is trimmed at Stop); the bake is skipped when its input is exactly what the last bake used | `tests/test_play_start_bench.cpp` times each step; `tests/test_play_hitch.cpp` checks the sharing, the stale-file rule, the trim, threads and the skip rules. Not done: spread the remaining work (snapshot text, script validation, first-time decode and a changed-scene bake) over frames. The GPU side (first draw of the Game view) cannot be measured headless, and the skip was not timed in a real editor window |
| Own WAV decoder (`okay::wav`) instead of `SDL_LoadWAV` and the narrow engine reader | Off by default (`-DOKAY_OWN_WAV=ON` or `OKAY_OWN_WAV=1`); reads PCM 8 to 32 bit, float 32 and 64, A-law, mu-law, MS ADPCM, IMA ADPCM, extensible, with budgets; used by `AudioClip::LoadWAV` when on | A second encoder (only ffmpeg so far: MS ADPCM, A-law, mu-law, 8-bit, 24-bit and float equal ffmpeg's decode bit for bit, IMA within 29 of 32,768), a run on Windows, a CI build with the option on (the tests run on GitHub with it off), a long fuzz run, the launcher's voice-clip load now goes through the seam `shared/WavPlayback.hpp` (SDL_LoadWAV only while the option is off; not run in the launcher on Windows), streamed WAV (`AudioStream`) still the old parser, RF64, owner approval. `docs/own_wav_decoder.md` |
| Own HTTP client (`okay::ownhttp`) instead of curl | Off by default, Linux only (`-DOKAY_OWN_HTTP=ON` plus `OKAY_OWN_HTTP=1`); every Linux runtime curl use (launcher Versions, game and official downloads, size probes, link preview; editor update download; PlayFab; the account calls) tries it first, curl is the fallback; keep-alive, HEAD, conditional requests and per-call cookies exist | Real-server run, a run on Windows (WinHTTP stays there), a soak, a run of the launcher and editor call sites on a real desktop (they are compiled, not run), the Windows `maybeSent` and cut-body changes in `NativeHttp.hpp` (only syntax-checked with MinGW), macOS runs; the `own-http` CI job is green on GitHub (for PR #850 head `1b455cfa` with its ASan and UBSan step, and on main since #852), keep-alive over TLS against a real server, proxy credentials and SOCKS, outside review of the parser, owner approval. Table and flip criteria: `docs/own_http_readiness.md`. The inflate was rewritten from the RFCs on 2026-11-14 after an outside review found the first version close to a public decoder (`docs/provenance_audit.md` row 11); the rewrite has no outside review yet. Since the one-inflate change this same decoder (`engine/include/okay/Core/Inflate.hpp`) also serves PNG, the model readers and the FBX reader: there is one inflate in our own code, not three |
| Steamworks backend | Off by default | Needs Valve's proprietary SDK |
| Glass UI theme | Visible, no real blur | The shared draw path cannot read the framebuffer back |
| Rich text tags | Screen-space and world text (colour, bold, italic, `[size=N]`; a line is as tall as the largest size on it; `world_text_rich`) | The **width** of a line is not measured with `[size]` (wrapping and centring use the text's own size, so a centred line with a big run leans right); italic and bold are approximations; text over 2,000 bytes is not parsed and at most 16 tags may be open (limits against hostile text); world text is drawn by the flat (2D) player pass and the editor's 2D views only (a 3D scene does not draw it) and with the 8x8 bitmap font unless the Text has a font file (a new font system is not planned here); the player and editor world-text drawing was only syntax-checked on Linux, never run in a real window or on Windows |
| UI nameplate | Name, level and health bar bound to the character it is a child of (`owner.` keys in UI Text Bind / UI Bar Bind; `ui_nameplate_bound`); a 220 x 62 plate with everything inside the card at any size and with long names (Best Fit), constant size in the world; the Inspector rows list the keys and show the owner; sample scene `examples/ui_text` (try it: `docs/try_it.md` 7j) | Not networked beyond what the character's own data replicates; no mana bar or status icons; the name is the object name (no separate display-name field); the plate was checked by headless pictures and measurements, never in a real editor or player window or on Windows; in the world only Panels, Progress Bars and Text were made to scale with the projection: the other widget kinds (images, sliders, toggles and the rest) still draw at their design size there (not checked) |
| UI Dropdown | Scrolls with wheel, keys and by dragging the open list (mouse or finger; a tap still picks, a drag never does) | Scrolls whole rows only, no momentum; touch is tested through the pointer the finger drives, not on a real touch screen |
| Inventory screen | Shows the bag, drags and splits stacks, moves between chest, hotbar, backpack and grid bags | Networked and scripted slot-to-grid moves, durability wear and repair; own-code arbiter for wheel and use key (only lean and bag-key clashes are handled), item names cut by measured width and icon file re-check are not ported; see [inventory.md](inventory.md) |
| Mixamo Soldier and Xbot samples | Removed | |
| LedgeGrab hands-on-ledge IK | Honest tooltip: not wired | Animation and IK team |
| App icon | Placeholder OkaySpace logo | Branding decision |
| D3D11 / D3D12 renderers | Not buildable or runnable in our Linux environment | Only compile-checked with MinGW in CI |
| Software renderer: scene Ambient Occlusion and Depth of field | Not read / not drawn | The scene's Post-processing SSAO switch and depth of field do nothing in software (bloom, FXAA, exposure, tonemap, grading, vignette are read now). See [render_status.md](render_status.md) |
| Direct3D 12 post chain | Not started (plan written) | `docs/render_status.md` has the steps; the post HLSL text is shared in `D3DPostHLSL.hpp` but D3D12 does not use it yet. Nothing GPU-side has been run |

## Own cryptography: no hired review, libsodium stays

`okay::crypto` (off by default, `OKAY_OWN_CRYPTO`) cannot become the default on our word (`CLAUDE.md` rule 18). The package, the request text and the reviewer archive script are ready (`docs/crypto_review_package.md`, `docs/crypto_review_request.md`, `scripts/make_crypto_review_bundle.sh`). What remains, in order:

| Step | State | Who |
|---|---|---|
| Consolidate the extra SHA-256 copies and the hand-written HMAC onto `okay::crypto`: **done on branch `claude/one-sha256` (owner approved 2026-11-15, not merged yet)**. One header-only `engine/include/okay/Crypto/Sha256.hpp` now holds SHA-256, HMAC-SHA256 (RFC 4231 tests) and PBKDF2-HMAC-SHA256 (RFC 7914 tests); `AccountService.hpp`, `AntiCheatChecks.hpp` and `launcher/VoiceChat.hpp` are thin wrappers; every output is byte-identical to the old copies (differential test, stored password records made by the old code, Python hashlib), and it is as fast or faster (`docs/sha256_comparison.md`). Still open from this step: `SecureChannel` key wiping (recommended, not done) | Done (SHA-256 / HMAC); key wiping open | Team |
| Say in the outside review request that the non-libsodium path of the anti-cheat and launcher Ed25519 now relies on `okay::crypto` (done in `docs/crypto_review_request.md`); a real signed manifest and `versions.json` from the release pipeline still have to be checked with a no-libsodium build | Open | Team |
| Choose a reviewer, a budget and a date; send the request and the bundle | Decided: **no hired reviewer** (owner, 2026-11-14). libsodium stays the default; the package and request stay in the repo in case that changes | Owner |
| Extra assurance without a hired reviewer: an internal review by a fresh agent, and a review by a second AI (Grok) from `review/CRYPTO_REVIEW_REQUEST.md` on branch `claude/crypto-review-grok` (pinned to commit `0a857395`). Reported as "not independently reviewed"; every claimed bug is reproduced by a test before any change | Internal review running; Grok review waiting for the owner to run it | Team and owner |
| The outside review itself, its report and findings | Not planned (no reviewer hired) | n/a |
| Fix every finding from the AI reviews that a test reproduces, or record why it is acceptable, and re-run the evidence | Waiting on the reports | Team |
| CI jobs the review is likely to ask for (valgrind audit, sanitizer run, libFuzzer, `OKAY_OWN_CRYPTO=ON`); runs on Windows, macOS and WebAssembly | Partly: the `own-stack` job builds with `OKAY_OWN_CRYPTO=ON` (`crypto=own(test)`), and two crypto tests (`anticheat_golden`, `anticheat_integrity_fallback`: Ed25519 and SHA-512) run on real Windows (MSVC, job "Windows updater restart"); the valgrind, sanitizer and libFuzzer jobs exist only on branch `claude/thirdparty-followups2` (not merged); macOS and WebAssembly not started | Team |
| The flip decision: make it the default for new builds, then delete libsodium one release later (the TweetNaCl port is already deleted) | Owner decision, only after a reviewer's yes | Owner |
| Physics: Improved quality for 2D (`docs/physics_plan.md`) | Not started (3D done) | The 2D step has no iterated contact solver: 2D stacks still sink and tip, 2D sleeping does not wake on lost support, no 2D slider, no 2D joint passes or debug joints. The quality setting is saved for 2D but changes nothing there. Plan: run 2D bodies through `ContactSolver.hpp` as 3D bodies that turn about z only, with 2-point box contacts |
| Physics: a heavy weight on a light rope | Better, not fixed | A 50 kg ball on a rope of 1 kg balls hangs 13 m long instead of 8 m (Classic: 64 m); repeated joint passes are not enough, it needs the joints solved together with the contacts (one solver) |
| Physics: dense sleeping piles in the Improved step | Costs more than Classic | 500 touching crates asleep cost 2.5 ms a step (Classic 0.9) because every sleeping pair still runs the contact test to send `OnCollisionStay`; a cache needs the stay callbacks to stop for sleepers or to be answered from the cache |
| Physics: heightmap terrain ground-follow takes 18 percent of the sideways speed every step a body touches the ground | Found, not fixed (terrain lane) | `friction = 0.18f` in `Physics3D::Step`: slows by step rate, not by time |
| Physics: `DebrisPool` and `DebrisEmitter` | New, 3D only | The voxel terrain does not use it yet (the voxel agent's branch decides); chunks are boxes only, no merge of settled chunks back into terrain |

## Planned: visual-script core upgrade

Steps 1 and 2 turned out to be mostly shipped already (see "Declared variables, lists, maps and structs" in `docs/visual_scripting.md`); the audit and the remaining gaps are below. Steps 3 to 5 are not started. One at a time, each shipped only when finished, tested and usable in the editor:

1. DONE (earlier work, audited again here): a better variable system. Declared variables (`decl name Type scope default`) with Number, Bool, Text, Vector3, Color, Object, `List<T>`, `Map<T>` and struct types; object, scene and global scopes with defaults editable in the Inspector; typed Set / Add Var; old `var` lines still load (`tests/data/vs_core_legacy.okayvs` is a graph in the old format, run by `vs_variables`). Not done: a declared "local" scope (function locals via Set Local stay untyped), and a separate "graph" scope distinct from "object" (today `object` is the graph's own variables); typed lists of structs.
2. DONE for lists and maps: add, remove (by index and by value), insert, set, find, sort, reverse, clear, copy, for each, length, keys, values, count, plus List Append, List Slice, List Pop, Map Get Or and Map Merge (added now), with element types carried through the graph checker. Not done: lists and maps have no OkayScript export (Generate script lists them as not translated); no sort-by-key or sort-with-comparer; List Pop takes only the last item.
3. Custom events with typed parameters, and delegates (bind, unbind, call).
4. Graphs talking to each other: call a function, fire an event or read a variable on another object's graph, with the graph checker flagging broken links.
5. Custom function nodes: user-defined nodes with typed pins, reusable across graphs and listed in the palette.

The text-script nodes below depend on this.

## Planned: text-script nodes in visual script

Not started. These would let a visual-script graph run a short text script next to the normal nodes. All of it is our own code: no third-party Lua, Python or JavaScript runtime.

Scope: a script node is for what nodes are bad at. That is algorithms and loops, tables and dictionaries, string and data handling, compact rules, and reusing snippets. Engine features (movement, physics, UI, audio, animation, networking) stay as nodes. A script calls the existing node and never reimplements it.

Order, one step at a time, each shipped only when finished and tested in both directions (node to script and script to node):

1. A "Run OkayScript" and expression node: typed pins, exec in and out, errors with line numbers.
2. Lua-style syntax compiled to the same VM.
3. A JavaScript-style subset, then a Python-style subset.

These are "-style" subsets, not the real languages: code written for the real Lua, JavaScript or Python will often not run unchanged, and the standard libraries are not included. Differences (for example 1-based arrays in Lua) will be documented per language.

## Planned work from the to-do list

- Admin: create test servers without a game (not started: "test server" needs a definition from the owner; see unfinished_launcher.md)
- Improve the editor Plugins menu
- Editor menus, follow-ups from the 2026-11-15 clean-up ([editor_menus.md](editor_menus.md), section 5): Ctrl+X with several objects selected deletes them all but keeps only one on the clipboard (the menus refuse Cut for several, the key does not); Copy and the clipboard hold one object, not a selection; what a menu click does (`RunMenuAction`, `HierRowAction`) has no test, only the lists do; the Inspector and Project menus are separate branches
- Improve inventory systems (survival-game style): partly done (moves, stack and weight rules, slot-to-grid moves); see [inventory.md](inventory.md) for what is left
- Scene Manager window in the editor
- Full UI component library (essential, game, app, layout, data-driven)
- Blocky character's arms in first person: the owner's complaint ("fix the first person hand so it looks normal to others and to us") is addressed on `claude/fp-hand-fix` (presets, always-visible arm with forearm, click swing, example scene, see [fp_hand.md](fp_hand.md)); still open: a real two-arm stance (Both arms preset only changes two-handed items), no fingers, a finer arm pose than a block, the first-person pose is not sent over the network, owner sign-off on the default look
- Script language phases 2 to 5: classes, physics and events, visual-script shared layer, tooling and debugger, export, replace the old VM
- Per-face materials, the rest (renderer submeshes): material slots with a colour each are built (see [modeling.md](modeling.md), Material slots); slots can also carry a picture in the software renderer; per-slot pictures in OpenGL / Direct3D 11 / Direct3D 12, per-slot shaders, and separate draw calls per slot, are not built
- Per-vertex colours: built (software, OpenGL, Direct3D 11, Direct3D 12 vertex paths; the three GPU paths were written but never run on a GPU); still open: a GPU look at them, per-slot textures and submeshes in the GPU renderers, reference images / blueprint planes, curve objects (bezier / NURBS) and text-to-mesh, live-preview booleans and bevel weights, quad remesh, retopology brushes; only per-face colours and slot colours exist, and every renderer would need a vertex colour stream first
- Guided characters (7zk) gaps: no rig, skin weights or animation; clothes are shells not cloth; insect and spider legs are crude; Environment and Props not built
- Guided vehicles and Detail (7zj) gaps: no opening doors, interiors or vehicle physics setup; motorcycles now have real frame geometry (rake, swingarm, shocks, engines, bars, fairings) but are still blocky shapes; box-only designs have no Low reduction ([guided_detail.md](guided_detail.md))
- Guided speed (7zo, [guided_perf.md](guided_perf.md)) gaps: the panel's drag preview, time line and fingerprint throttle compile but were never run in a real window; a release at Ultra still spends 100 to 200 ms writing the .okaymesh file; models with hand edits get no drag preview and still do the full analysis on release; the Hero human's cape hangs a few centimetres off the back
- Guided architecture (7zi) gaps: no railings on landings; wall corners are butt-joined, not mitred; click-to-draw path has no preview line; per-face material slots (claude/modeling-more) not used yet beyond the stored numbers
- Guided modeling, phase 2 and the known gaps of phase 1 ([guided_modeling_design.md](guided_modeling_design.md)): categories Vehicles, Characters, Environment and more of Architecture (walls, stairs, doors, windows, roofs, buildings) have no designs yet and say so; the fence has no Windows-window verification; the path is a list of numbers (no click-to-draw path in the 3D view); the fence does not add a collider; a moved part is recognised only when it was moved (and, for one shell, turned) as a whole; geometry welded to a part at an exact shared corner counts as an edit of it (it asks, it never overwrites); after the editor's global Ctrl+Z the recipe file can be newer than the mesh (parts then show as edited); material slots are stored but drawn only when per-face materials land (colour per kind of part is drawn now)
- Script editor: classes, default templates, no random script on New Script
- Prone for first and third person
- Player customization: more options
- Humanoid animation quality, procedural object holding, 2.5D controller improvements, backpedal and strafe animations, character joint hinges and detail (animation team)
- Splitting the largest files: launcher `main.cpp`, the OkayScript VM, component serializer registry, editor `main.cpp` into panel files
- ~~Remove the hard-coded Supabase URL and key from CMake; stop committing dist binaries~~ DONE on `claude/hygiene-1`: address and publishable key now live only in `config/public-account.json` (no longer in CMake or the page); dist binaries removed from git, lint added. Optional owner step: rotate the old key in Supabase and edit that file (`docs/accounts.md`)
- Security plan SEC1: relay isolation, script file-path safety, updater signing, reactions privacy SQL; anti-cheat hardening
- README matching reality and a component audit document

## From the feature map (Partial and Planned)

### 1. Core & scene

| ID | Feature | Status | Open |
|---|---|---|---|
| [1.6](feature_map/01-core-scene.md#16-prefabs-and-templates) | Prefabs and templates | Partial | prefab mode (editing a prefab in its own scene; Open only isolates the instance); the old... |
| [1.11](feature_map/01-core-scene.md#111-asset-manager) | Asset manager | Partial | move existing loaders (`DataAsset::Cached`, `AudioClip::LoadCached`, the renderers' own loads) onto it; track GPU resources; an... |
| [1.12](feature_map/01-core-scene.md#112-asset-database) | Asset database | Partial | importers reading the remaining settings; GUID references inside scene files; build/export closure wiring; binary formats such as `.fbx` have no dependency edges; no file watcher (the Project panel polls the incremental scan every 2 s or more); an asset has a `.okaymeta` only after it is given a tag or import settings... |
| [1.13](feature_map/01-core-scene.md#113-object-pooling) | Object pooling | Partial | a script builtin and a no-code Spawner option; resetting Rigidbody velocities is each component's job |
| [1.16](feature_map/01-core-scene.md#116-localization-tools) | Localization tools | Partial | JSON import and export in the window, a confirm step before CSV import, the font coverage report, and right-to-left layout (the window shows a mirrored preview of the selected key)... |
| [1.19](feature_map/01-core-scene.md#119-property-reflection) | Property reflection | Partial | not registered yet: controllers, scripts, the other UI widgets, navigation, networking; animation and IK components are left to... |
| [1.20](feature_map/01-core-scene.md#120-asset-thumbnails) | Asset thumbnails | Partial | pictures are made on the main thread inside a per-frame time budget, not on a worker thread; one 128 px size scaled to the tile size; no pictures for fonts or data assets; a scene picture loads the whole scene... |

### 3. 3D modeling & import

| ID | Feature | Status | Open |
|---|---|---|---|
| [3.5](feature_map/03-modeling-import.md#35-mesh-assets-and-primitives) | Mesh assets and primitives | Partial | M5b per-face materials: PolyFace has a material slot but Mesh has no per-triangle slot; old map says Sphere, Cylinder, Cone... |
| [3.6](feature_map/03-modeling-import.md#36-model-import) | Model import | Partial | possible doc bug: docs/animation.md Tips says FBX animations import via Assimp, but FBX is read by the built-in ufbx path; packed... |
| [3.11](feature_map/03-modeling-import.md#311-guided-creation-templates-parts-safe-regeneration) | Guided creation | Partial | fence and the architecture templates only; no landing railings; editor page not opened in a real window |

### 5. Animation & IK

| ID | Feature | Status | Open |
|---|---|---|---|
| [5.9](feature_map/05-animation-ik.md#59-object-holding) | Object holding | Partial | old map: holding PRs 3-4: body adjust is done (#389), multiplayer sync (PR 4, branch `claude/holding-net`) and grip editor (PR 5... |

### 12. In-game UI

| ID | Feature | Status | Open |
|---|---|---|---|
| [12.7](feature_map/12-ingame-ui.md#127-themes-and-the-launcher-matching-ui-kit) | Themes and the launcher-matching UI kit | Partial | "full component library" (see the missing list in 12.3 and 12.4); stylesheet assets with selectors... |
| [12.10](feature_map/12-ingame-ui.md#1210-ui-editing-in-the-editor) | UI editing in the editor | Partial | UI plan P4: anchor preset picker and pivot handles, UI prefabs with variants, UIDocument-to-hierarchy round trip, a `--selftest`... |
| [12.13](feature_map/12-ingame-ui.md#1213-data-binding-ui-animation-and-script-control) | Data binding, UI animation and script control | Partial | UI plan P3 not found: `ui_create`, `ui_destroy`, `ui_find`, generic `ui_get/ui_set(prop)`, events `on_hover_enter`... |

### 13. Scripting

| ID | Feature | Status | Open |
|---|---|---|---|
| [13.2](feature_map/13-scripting.md#132-c-style-language-core) | C#-style language core | Partial | in flight (branches, no PR, 2026-10-26): claude/csharp-p1-events, -inheritance, -try, -coroutines, -getcomponent... |

### 15. Editor

| ID | Feature | Status | Open |
|---|---|---|---|
| [15.10](feature_map/15-editor.md#1510-splitting-editormaincpp-into-panel-files-45k-lines) | Splitting `editor/main.cpp` into panel files (~45k lines) | Partial | no PR for the next split found (#577 changed about 280 lines of `main.cpp` for the layouts, the Script Editor find and replace... |

### 16. Networking & multiplayer

| ID | Feature | Status | Open |
|---|---|---|---|
| [16.7](feature_map/16-networking.md#167-voice-chat-server) | Voice chat server | Partial | no tests for ADPCM, jitter buffer or room-key handling; launcher voice calls now send version-3 hellos with the relay cookie and... |

### 17. Launcher & platform

| ID | Feature | Status | Open |
|---|---|---|---|
| [17.11](feature_map/17-launcher-platform.md#1711-security-plan-sec1) | Security plan SEC1 | Partial | sign the launcher, editor and player downloads (ed25519 as the anti-cheat manifest does) and verify before swapping; the SEC1... |

### 18. Build, export & release

| ID | Feature | Status | Open |
|---|---|---|---|
| [18.2](feature_map/18-build-export-release.md#182-web-and-mobile-export) | Web and mobile export | Partial | the Build Settings tooltip says "Export an Android Studio project" but the code writes assets and a README only (possible... |

### 19. Docs, tests & code health

| ID | Feature | Status | Open |
|---|---|---|---|
| [19.4](feature_map/19-docs-tests-health.md#194-splitting-the-largest-files-editor-launcher-okayscript-vm-serializer) | Splitting the largest files (editor, launcher, OkayScript VM, serializer) | Planned | splitting `launcher/main.cpp` needs a plan per page (17.1 to 17.16 are the natural seams); `player/main.cpp` is 2,658 lines and... |
| [19.5](feature_map/19-docs-tests-health.md#195-roadmap-and-pr-template) | Roadmap and PR template | Partial | the anti-cheat hardening list still shows items 5, 6, 8, 9, 10, 12, 13, 14 as open, although `git log` has #350, #360, #363... |

### 20. Asset management (Critical)

| ID | Feature | Status | Open |
|---|---|---|---|
| [20.1](feature_map/20-35-planned.md#201-asset-browser) | Asset browser | Partial | Two sources of truth for labels (colour labels in `.okaylabels` vs tags in `.okaymeta`); no file watcher, outside changes show on the next poll (2 s or more); no automated UI test of the window... |
| [20.2](feature_map/20-35-planned.md#202-dependencies-and-references) | Dependencies and references | Partial | Build Game and the database each have their own dependency walk (Build Game uses `Core/BuildPipeline`, not `BuildClosurePaths`); references are found by reading text, so a path built at run time is never seen... |
| [20.3](feature_map/20-35-planned.md#203-guids) | GUIDs | Partial | meta files are matched by content hash (FNV-1a, not cryptographic); files over 512 MB hash only head and tail; a project already... |
| [20.4](feature_map/20-35-planned.md#204-import-settings) | Import settings | Partial | no audio options in the UI, no sRGB or compress effect; two places currently own texture options (the asset meta and the MeshRenderer fields), the plan must say which wins... |
| [20.5](feature_map/20-35-planned.md#205-thumbnails) | Thumbnails | Partial | pictures are made on the main thread inside a time budget, not on a worker thread; a scene thumbnail loads the whole scene; no thumbnail for fonts or data assets |

### 21. Prefab system (Critical)

| ID | Feature | Status | Open |
|---|---|---|---|
| [21.2](feature_map/20-35-planned.md#212-nested-prefabs) | Nested prefabs | Partial | objects are matched by depth-first position and name, so reordering many objects can pair the wrong ones; nesting errors are not listed in the Console on load; a second instance of the applied file inside the same outer instance is not updated by Apply... |
| [21.3](feature_map/20-35-planned.md#213-variants) | Variants | Partial | no variant badge on the Project panel tile; scene files never contain variant records (only prefab files do); objects added to or lost from a variant instance cannot be applied into the variant... |

### 22. Undo/redo & recovery (Critical)

| ID | Feature | Status | Open |
|---|---|---|---|
| [22.1](feature_map/20-35-planned.md#221-undo-history) | Undo history | Partial | each snapshot is a full scene text (memory grows with scene size); three separate stacks can confuse Ctrl+Z; behaviour of the... |
| [22.2](feature_map/20-35-planned.md#222-autosave) | Autosave | Partial | a leftover `.autosave` from an older editor can still sit beside the scene until it is recovered or discarded; a project under... |
| [22.3](feature_map/20-35-planned.md#223-crash-recovery) | Crash recovery | Partial | the player has no handler; the Windows handler has never run on real Windows (MinGW compile only); a stripped release gives offsets and mangled names, not file and line. |
| [22.4](feature_map/20-35-planned.md#224-backups) | Backups | Partial | a scene save still keeps one `.bak` beside the scene, and a leftover `.autosave` from an older editor may too; a project under... |

### 23. Profiling & debugging (Critical)

| ID | Feature | Status | Open |
|---|---|---|---|
| [23.1](feature_map/20-35-planned.md#231-cpugpu-profiler) | CPU/GPU profiler | Partial | the "GPU" number is CPU wall time around the render call; the ring is only 4 seconds at 60 fps; `FrameHistory` keeps 600 frames... |
| [23.2](feature_map/20-35-planned.md#232-memory-tracking) | Memory tracking | Partial | no platform memory numbers (process working set); heap hooks off by default. |
| [23.3](feature_map/20-35-planned.md#233-frame-debugger) | Frame debugger | Partial | each renderer (software, GL, D3D11, D3D12) draws differently, so four hooks are needed. |

### 24. Plugin & extension system (Critical)

| ID | Feature | Status | Open |
|---|---|---|---|
| [24.1](feature_map/20-35-planned.md#241-c-plugins) | C++ plugins | Planned | native plugins can crash the editor; ABI and ownership of memory need an explicit rule; use the property registry (on main, #578... |
| [24.3](feature_map/20-35-planned.md#243-custom-nodes) | Custom nodes | Planned | node text format must keep loading old graphs when a custom node type is missing (placeholder node). |
| [24.4](feature_map/20-35-planned.md#244-plugin-sdk) | Plugin SDK | Partial | versioning rule for the engine ABI is undecided (see 25.3). |

### 25. Project management (High)

| ID | Feature | Status | Open |
|---|---|---|---|
| [25.2](feature_map/20-35-planned.md#252-project-settings) | Project settings | Partial | `project.okayproj` is loaded from the current directory, so opening a project from elsewhere depends on the working directory... |
| [25.3](feature_map/20-35-planned.md#253-migration) | Migration | Partial | no policy yet on how long old formats stay readable; `MigrateThroughEngine` drops records this build does not know and gives... |
| [25.4](feature_map/20-35-planned.md#254-dependency-management) | Dependency management | Planned | where packages are hosted (Workshop, GitHub) is undecided. |

### 26. Shader & material editor (High)

| ID | Feature | Status | Open |
|---|---|---|---|
| [26.1](feature_map/20-35-planned.md#261-visual-shader-graphs) | Visual shader graphs | Planned | four renderers (software, GL, D3D11, D3D12) must stay consistent; D3D12 has no normal, gloss or AO maps today. |
| [26.2](feature_map/20-35-planned.md#262-material-editor) | Material editor | Partial | preview is not what the renderer draws; materials are copied into the renderer (`ApplyTo`), not referenced, so editing the file... |
| [26.3](feature_map/20-35-planned.md#263-custom-shaders) | Custom shaders | Planned | D3D12 is "v1" and opt-in; shader hot reload would need 34.3. |
| [26.4](feature_map/20-35-planned.md#264-pbr) | PBR | Partial | four renderers to keep equal; D3D12 lacks maps; the software renderer is the reference. |

### 27. Optimization tools (High)

| ID | Feature | Status | Open |
|---|---|---|---|
| [27.1](feature_map/20-35-planned.md#271-lod-generation) | LOD generation | Partial | simplifier ignores colour and material changes in its error; a mesh with `cornerUVs` is split into per-vertex UVs first; skinned... |
| [27.2](feature_map/20-35-planned.md#272-occlusion-culling) | Occlusion culling | Partial | CPU rasteriser only (no GPU queries); conservative, so it may draw more than needed; no components yet. |
| [27.3](feature_map/20-35-planned.md#273-batching) | Batching | Partial | `isStatic` is only "a hint for tools"; merged meshes need a draw path in each renderer; skinned and transparent objects are never... |
| [27.4](feature_map/20-35-planned.md#274-streaming) | Streaming | Partial | a loaded cell is created in one frame (hitch risk); saves do not know about unloaded cells (see 35.3); cells are on the XZ plane... |

### 28. Version control integration (High)

| ID | Feature | Status | Open |
|---|---|---|---|
| [28.1](feature_map/20-35-planned.md#281-git-integration) | Git integration | Partial | no automated test; no conflict handling after a pull (see 28.4); shelling out to git means errors come back as text only. |
| [28.2](feature_map/20-35-planned.md#282-diffs) | Diffs | Partial | no UI and no tool call these yet; a file without ids pairs objects by name path, so a rename there looks like a removal plus an... |
| [28.3](feature_map/20-35-planned.md#283-project-history) | Project history | Planned | depends on git being installed; projects without git only get local backups. |
| [28.4](feature_map/20-35-planned.md#284-merge-conflict-tools) | Merge conflict tools | Partial | the library only merges text of files at one format version; ours and theirs at different versions are not merged; possible... |

### 29. Terrain & world building tools (High)

| ID | Feature | Status | Open |
|---|---|---|---|
| [29.2](feature_map/20-35-planned.md#292-vegetation-painting) | Vegetation painting | Partial | object-per-instance scatter will not scale; no undo grouping for a scatter pass beyond the scene snapshot. |
| [29.3](feature_map/20-35-planned.md#293-procedural-generation) | Procedural generation | Partial | possible duplicate: `Components/Terrain.cpp` has its own noise code next to `Math/Noise`. |

### 30. Character creator (Medium)

| ID | Feature | Status | Open |
|---|---|---|---|
| [30.1](feature_map/20-35-planned.md#301-customization) | Customization | Partial | the part rig and the single-mesh rig are two build paths for one component (see 4.9). |
| [30.2](feature_map/20-35-planned.md#302-rig-generation) | Rig generation | Partial | rigid weights tear at joints; animation and IK code belong to Grok's sessions. |
| [30.3](feature_map/20-35-planned.md#303-clothing) | Clothing | Partial | no layered clothing, cloth physics (see 52). |
| [30.4](feature_map/20-35-planned.md#304-morph-targets) | Morph targets | Partial | shapes are stored sparse and applied to the bind pose; large faces with many shapes cost CPU each frame (not measured). |

### 31. Localization tools (Medium)

| ID | Feature | Status | Open |
|---|---|---|---|
| [31.1](feature_map/20-35-planned.md#311-translation-tables) | Translation tables | Partial | JSON import and export in the window, a confirm step before CSV import, and translation memory. The window uses `LocTable`, `LocCheck` and `LocScan`... |
| [31.2](feature_map/20-35-planned.md#312-font-fallback) | Font fallback | Partial | system fonts differ between machines, so a CJK game should ship its own font; RTL is drawn left to right. |
| [31.3](feature_map/20-35-planned.md#313-language-previews) | Language previews | Partial | the Scene view layout does not mirror for right-to-left languages. The Localization window shows a mirrored `LocPreview` of the selected key. |

### 32. Accessibility (High)

| ID | Feature | Status | Open |
|---|---|---|---|
| [32.1](feature_map/20-35-planned.md#321-controller-navigation) | Controller navigation | Partial | one shared ring is drawn from the theme and the accessibility settings; there is no focus sound; the graph is built from screen rectangles each time it is asked |
| [32.2](feature_map/20-35-planned.md#322-subtitles) | Subtitles | Partial | speaker colours need a colour-vision-safe palette (32.3); subtitle size, background and speaker-name rows, and SRT import. An Audio Source caption is shown when sound captions are on. |
| [32.3](feature_map/20-35-planned.md#323-colorblind-options) | Colorblind options | Partial | the GPU renderers do not read the settings; the speaker-colour palette in `CaptionManager` is the only consumer in the engine. |
| [32.4](feature_map/20-35-planned.md#324-remapping) | Remapping | Partial | the UI navigation keys are fixed in code; mouse-button and wheel rebinding coverage is not checked. |

### 33. Automated testing (Critical)

| ID | Feature | Status | Open |
|---|---|---|---|
| [33.1](feature_map/20-35-planned.md#331-play-mode-tests) | Play-mode tests | Partial | a script that reads the wall clock or uses threads makes a play-test non-deterministic (stated in `docs/automated_testing.md`)... |
| [33.3](feature_map/20-35-planned.md#333-regression-testing) | Regression testing | Partial | compilers round shading slightly differently, so goldens need a tolerance; the software renderer draws mesh renderers only (no... |

### 34. Live editing (High)

| ID | Feature | Status | Open |
|---|---|---|---|
| [34.1](feature_map/20-35-planned.md#341-properties-during-play) | Properties during Play | Partial | objects created or destroyed in Play have no edit-scene counterpart to keep changes on; the registry does not cover controllers... |
| [34.2](feature_map/20-35-planned.md#342-live-object-inspector) | Live object inspector | Partial | no per-object runtime counters; the live view relies on the same redraw as the edit view. |
| [34.3](feature_map/20-35-planned.md#343-hot-reload) | Hot reload | Partial | in-place replacement happens inside `Pump()`, so readers must be on that thread; a failed half-written file keeps the old data... |

### 35. Save game framework (High)

| ID | Feature | Status | Open |
|---|---|---|---|
| [35.2](feature_map/20-35-planned.md#352-checkpoints) | Checkpoints | Partial | AutoSave writes one slot, so a corrupt moment is overwritten; wait for the framework decision (35.1). |

### 36. Math & geometry

| ID | Feature | Status | Open |
|---|---|---|---|
| [36.6](feature_map/36-math-geometry.md#366-geometry-helpers-elsewhere) | Geometry helpers elsewhere | Partial | duplicated ray-box and ray-sphere code in `Physics2D`/`Physics3D`, `EditTransform::Ray`, `Lightmap::RayTriangle` and `Frustum`... |
| [36.7](feature_map/36-math-geometry.md#367-noise-curves-colour-spaces-and-random-streams) | Noise, curves, colour spaces and random streams | Partial | move terrain, textures, particles and meshes onto the shared noise so a seed means the same thing everywhere; `Mesh::PropRand`... |

### 37. Platform & windowing

| ID | Feature | Status | Open |
|---|---|---|---|
| [37.5](feature_map/37-platform-windowing.md#375-web-and-mobile-shims) | Web and mobile shims | Partial | Android and iOS projects are not in the repo; web has no networking; `docs/web_mobile.md` still says to read `SDL_FINGER*`... |

### 38. Render backends & GPU

| ID | Feature | Status | Open |
|---|---|---|---|
| [38.4](feature_map/38-render-backends.md#384-direct3d-12-backend) | Direct3D 12 backend | Partial | possible doc mismatch: the header says `samples` is accepted but MSAA is not applied, while the `.cpp` header comment and code... |
| [38.11](feature_map/38-render-backends.md#3811-gpu-timing-and-frame-debugging) | GPU timing and frame debugging | Partial | wiring real GPU timers into each backend; a frame debugger window (area 23) |

### 39. Security & anti-cheat

| ID | Feature | Status | Open |
|---|---|---|---|
| [39.3](feature_map/39-security-anticheat.md#393-session-tickets-and-the-ticket-service) | Session tickets and the ticket service | Partial | deploy the function and create the key pair (the owner's step, ROADMAP "Waiting on Iman"); wire the launcher or game to fetch a... |
| [39.4](feature_map/39-security-anticheat.md#394-client-integrity-signed-manifest-and-launch-check) | Client integrity: signed manifest and launch check | Partial | set `OKAY_AC_TRUSTED_KEYS` so the check becomes a failure (planned in `release.yml`); hashing every file costs time on big games... |
| [39.5](feature_map/39-security-anticheat.md#395-runtime-checks-and-the-clock-heartbeat) | Runtime checks and the clock heartbeat | Partial | no kernel driver, memory scanning or obfuscation (by design); older clients never answer the heartbeat, so turn `maxClockDrift`... |
| [39.11](feature_map/39-security-anticheat.md#3911-account-backend-and-updater-hardening) | Account, backend and updater hardening | Partial | possible weaknesses: the local password hash is a hand-made chain of SHA-256 rather than a standard KDF, and its salt comes from... |

### 40. OkayUI standalone library

| ID | Feature | Status | Open |
|---|---|---|---|
| [40.3](feature_map/40-okayui-library.md#403-backends-and-frame-loop) | Backends and frame loop | Partial | no test of the D3D11 path found; skipped in the web build (per `CMakeLists.txt` and `scripts/build-web.sh` comments) |

### 43. 2D tools

| ID | Feature | Status | Open |
|---|---|---|---|
| [43.3](feature_map/43-proposals.md#433-sprite-animation-tools) | Sprite animation tools | Partial | first step: a Sprite Animation window with a frame strip, per-frame duration and animation events (call a script function on... |
| [43.4](feature_map/43-proposals.md#434-2d-lighting) | 2D lighting | Planned | first step: an unlit-versus-lit flag and a simple 2D point light with falloff that tints sprites in the software renderer first... |

### 44. Level design and blockout tools

| ID | Feature | Status | Open |
|---|---|---|---|
| [44.3](feature_map/43-proposals.md#443-measuring-and-level-metrics) | Measuring and level metrics | Planned | first step: a Measure tool (click two points, show distance and slope) and a reference-figure toggle showing a 1.8 m person and... |
| [44.4](feature_map/43-proposals.md#444-volumes-and-zones) | Volumes and zones | Partial | first step: a GameObject > Effects > Volume menu (trigger, kill, music, spawn) drawn as a tinted box with its name in the Scene view; one... |

### 45. Open-world streaming and world partition

| ID | Feature | Status | Open |
|---|---|---|---|
| [45.1](feature_map/43-proposals.md#451-cell-authoring-and-baking) | Cell authoring and baking | Partial | first step: "Split scene into cells" (grid size, object assignment by position, writes cell prefabs and a manifest) and a Scene... |
| [45.2](feature_map/43-proposals.md#452-large-world-coordinates) | Large world coordinates | Planned | first step: measure where jitter starts in a test scene; then a floating-origin option that shifts the whole scene back when the... |
| [45.3](feature_map/43-proposals.md#453-streaming-aware-systems) | Streaming-aware systems | Planned | first step: per-cell navmesh bake stored with the cell and stitched at load; cell-level state records for saves. |
| [45.4](feature_map/43-proposals.md#454-distant-scenery) | Distant scenery | Planned | first step: merged low-detail meshes per cell ("hierarchical LOD") using the simplifier; billboard impostors for trees. |

### 46. Replay system

| ID | Feature | Status | Open |
|---|---|---|---|
| [46.1](feature_map/43-proposals.md#461-input-recording-and-playback) | Input recording and playback | Planned | first step: record `PlayerInput` per fixed tick plus the random seed and scene file, tie each tick to a `FrameRecorder` marker... |
| [46.2](feature_map/43-proposals.md#462-state-replay-and-killcam) | State replay and killcam | Planned | first step: write snapshots to a ring buffer and a free camera that reads them back. |
| [46.3](feature_map/43-proposals.md#463-replay-files-and-viewer) | Replay files and viewer | Planned | first step: define the file (header with version, scene, seed; per-tick records) with the same versioning rules as saves (35.4). |

### 47. Mod support

| ID | Feature | Status | Open |
|---|---|---|---|
| [47.1](feature_map/43-proposals.md#471-mod-loader-and-content-overrides) | Mod loader and content overrides | Planned | first step: asset lookup through an ordered list of folders (mods first), a `mod.txt` manifest (name, version, load order), and a... |
| [47.2](feature_map/43-proposals.md#472-mod-scripting-sandbox) | Mod scripting sandbox | Planned | first step: a capability flag per script host (files, network, process) defaulting to off for mod scripts. |
| [47.3](feature_map/43-proposals.md#473-mod-packaging-and-sharing) | Mod packaging and sharing | Planned | first step: add a `mod` kind, an installer that unpacks into the game's Mods folder, and an enable and disable list. |

### 48. Crash reporting and telemetry

| ID | Feature | Status | Open |
|---|---|---|---|
| [48.1](feature_map/43-proposals.md#481-crash-reports) | Crash reports | Partial | the editor writes detailed reports and the launcher sends them (scrubbed, default on, after a one-time notice) and makes GitHub issues once the owner runs the table SQL and sets the repository variable `CRASH_ISSUES_ENABLED`; next: reuse the handler in the player; file and line for a stripped release... |
| [48.2](feature_map/43-proposals.md#482-opt-in-analytics-events) | Opt-in analytics events | Planned | first step: an `Analytics::Event(name, fields)` API that does nothing unless consent is on, writes to a local file first, and a... |
| [48.3](feature_map/43-proposals.md#483-consent-and-privacy) | Consent and privacy | Partial | crash reports have a switch (on by default) with a disclosure, a one-time notice before the first send and a preview; no general consent dialog component, no switch for analytics events. |
| [48.4](feature_map/43-proposals.md#484-collection-backend-and-dashboards) | Collection backend and dashboards | Partial | the `crash_reports` table (insert-only) is written and tested locally, not yet on the real project; the `events` table and a web view are not built. | |

### 49. Cloud services

| ID | Feature | Status | Open |
|---|---|---|---|
| [49.1](feature_map/43-proposals.md#491-cloud-saves) | Cloud saves | Partial | first step: `ISaveSync` backends for Steam Cloud and PlayFab user data, with "newest wins" and a conflict prompt; size limits per... |
| [49.2](feature_map/43-proposals.md#492-leaderboards) | Leaderboards | Partial | first step: a small `Leaderboards` interface with the three as backends and a UI list component. |
| [49.3](feature_map/43-proposals.md#493-achievements-sync) | Achievements sync | Partial | first step: one `Unlock(id)` that writes local, calls Steam when present and queues an upload when signed in. |
| [49.4](feature_map/43-proposals.md#494-remote-configuration) | Remote configuration | Planned | first step: a signed JSON file fetched at start that sets cvars; falls back to the shipped defaults offline. |

### 50. Capture and media

| ID | Feature | Status | Open |
|---|---|---|---|
| [50.2](feature_map/43-proposals.md#502-photo-mode) | Photo mode | Planned | first step: a `PhotoMode` component: key toggles pause, hides the UI, switches to a free camera with sliders for field of view... |
| [50.3](feature_map/43-proposals.md#503-video-recording) | Video recording | Planned | first step: an image-sequence recorder at a fixed time step (so slow frames do not drop video frames), using... |
| [50.4](feature_map/43-proposals.md#504-video-playback) | Video playback | Planned | first step: choose a codec and a small decoder library (for example a simple open format) and a `VideoPlayer` component drawing... |

### 51. Onboarding, tutorials and samples

| ID | Feature | Status | Open |
|---|---|---|---|
| [51.1](feature_map/43-proposals.md#511-sample-gallery) | Sample gallery | Partial | first step: each template gets a short "What to look at" note shown when it opens (which objects, scripts and settings matter); a... |

### 52. Simulation extras (cloth, soft bodies, fluids, destruction, crowds)

| ID | Feature | Status | Open |
|---|---|---|---|
| [52.1](feature_map/43-proposals.md#521-cloth-and-hair) | Cloth and hair | Planned | first step: a rope and flag component on a particle grid with pin points and wind from `Environment/Wind`. |
| [52.2](feature_map/43-proposals.md#522-soft-bodies) | Soft bodies | Planned | first step: a spring-mass lattice on a mesh (same solver as 52.1). |
| [52.4](feature_map/43-proposals.md#524-destruction) | Destruction | Partial | first step: pre-fractured meshes (a mesh authored as pieces) with a break threshold and impulse. |

### 53. Day-night and weather hooks

| ID | Feature | Status | Open |
|---|---|---|---|
| [53.1](feature_map/43-proposals.md#531-time-of-day-events-and-gameplay) | Time-of-day events and gameplay | Partial | first step: publish events on the `EventBus` (HourChanged, Sunrise, Sunset) and a "run actions at hour X" component; possible... |
| [53.2](feature_map/43-proposals.md#532-weather-gameplay-effects) | Weather gameplay effects | Partial | first step: a `WeatherEffects` component mapping a preset to survival modifiers (wetness, temperature drop, friction scale... |

### 54. Debug draw, gizmos, dev console and cheats

| ID | Feature | Status | Open |
|---|---|---|---|
| [54.3](feature_map/43-proposals.md#543-in-game-dev-console) | In-game dev console | Partial | first step: a backtick-toggled console component using `Console::History/Lines/Complete`, and engine cvars (`r.shadows`... |
| [54.4](feature_map/43-proposals.md#544-cheats-and-debug-commands) | Cheats and debug commands | Planned | first step: register `god`, `give <item> [n]`, `tp <x> <y> <z>`, `time <hour>` against the survival, inventory and day-night... |

### 55. Script debugging and profiling

| ID | Feature | Status | Open |
|---|---|---|---|
| [55.1](feature_map/43-proposals.md#551-breakpoints-and-stepping-for-text-scripts) | Breakpoints and stepping for text scripts | Partial | first step: a line hook in the OkayScript VM and the typed-script interpreter (`engine/src/Script/Interpreter.cpp`) that calls a... |
| [55.2](feature_map/43-proposals.md#552-watch-and-call-stack) | Watch and call stack | Partial | first step: expose the interpreter's frame and locals read-only to the editor while paused. |

### 56. Platform targets and store packaging

| ID | Feature | Status | Open |
|---|---|---|---|
| [56.1](feature_map/43-proposals.md#561-desktop-packaging) | Desktop packaging | Partial | first step: Linux AppImage or tarball for exported games from the same Build dialog; macOS app bundle later (needs a macOS build... |
| [56.3](feature_map/43-proposals.md#563-web-export) | Web export | Partial | first step: package the web build as a zip ready for itch.io from the editor when emscripten is installed. |
| [56.4](feature_map/43-proposals.md#564-console-platforms) | Console platforms | Planned | first step: none until the owner decides; keeping platform code behind the `Platform/*` interfaces helps. |

### 57. Documentation website

| ID | Feature | Status | Open |
|---|---|---|---|
| [57.2](feature_map/43-proposals.md#572-generated-api-reference) | Generated API reference | Partial | first step: generate `scripting_reference.md` from the same builtin table the editor window uses so they cannot drift. |

### 58. Performance budgets and CI perf tests

| ID | Feature | Status | Open |
|---|---|---|---|
| [58.1](feature_map/43-proposals.md#581-performance-budgets) | Performance budgets | Planned | first step: a `budgets.okaybudget` text file (frame ms, draw calls, triangles, texture MB per scene) and a command that reports... |
| [58.3](feature_map/43-proposals.md#583-performance-regression-guard-in-ci) | Performance regression guard in CI | Planned | first step: store the benchmark output as a CI artifact and print a table in the job summary before gating anything. |
| [58.4](feature_map/43-proposals.md#584-build-size-and-load-time-report) | Build size and load-time report | Partial | first step: log scene load time and asset decode time (the async loader in #570 reports progress) in the Profiler. |

