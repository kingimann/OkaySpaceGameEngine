# Own replacements: what can become the default, in what order, and what blocks deleting each library

Date: 2026-11-15. Branch `claude/own-flip-proposal`. **This is a proposal.** Nothing is flipped and nothing is deleted
by this page. A flip needs the owner's written approval (`CLAUDE.md` rule 17). Our own cryptography is never flipped on our
word (rule 18).

How this page was made. It is docs only. Nothing was built or run for it (disk is tight). The numbers are copied from the
docs listed in section 7. A few facts I checked myself, in the code and in the logs of GitHub's CI runs on main. Those are
marked "checked". Where I did not check, I say "unverified".

Short words used below:

* **Flip**: change a default from off to on. **Rollback**: the switch that goes back.
* **Fallback**: the third-party code (Assimp, ufbx) that stays compiled in. It reads a file by itself when ours refuses it.
* **Corpus**: the real files we test with. Today that is one set: the `test/models` folders of an Assimp 5.4.3 checkout.
* **ASan and UBSan**: tools that stop a program when it touches bad memory or does something the language leaves undefined.
* **Wine**: a program that runs Windows programs on Linux. It is not Windows. **MinGW**: the compiler that builds our
  Windows programs on Linux.
* **Hostile-input test**: feeds a reader broken or malicious files and checks that it refuses them without crashing. **Fuzz test**:
  changes bytes of a good file thousands of times to look for crashes. **Standalone**: built from the reader's source only, without the
  whole engine.
* **Startup line** (or stack line): the one line each program writes at start, such as `[okay] stack: ... readers=own(...)`. The editor's
  Help > About OkaySpace shows it. It is how you see which parts are ours.
* **Step 0**: prep work in section 3.1. It changes no default.

## 1. Summary for the owner

### 1.1 The short answer

* **Nothing is ready to flip today.** The best candidates are one prep step away (Step 0, section 3.1). That step changes no
  default. It makes the flips safe and makes them visible to you.
* **Closest to flip** (by evidence; the order I recommend is in section 2): STL and PLY, the old model formats (OFF, SMD, MD5,
  MS3D, ASE, then MD2, MD3, IRRMESH, B3D, X), then BVH, 3DS and COLLADA. Then WAV and FBX.
* **WAV is the most useful flip.** The default WAV loader plays A-law and mu-law files as loud noise, plays 64-bit float as
  silence and refuses ADPCM (Grok finding 19 on issue #888; I read the loader code and it is true). Ours reads all of them right.
* **Not soon:** OkayUI for Dear ImGui, our platform layer for SDL2, our HTTP client on Windows (Windows uses WinHTTP, the
  system's own client). **Never on our word:** our crypto for libsodium (rule 18, your decision of 2026-11-14).
* **Three things hold back every model-reader flip.**
  1. No reader has ever run on real Windows. Wine only. CI does have real Windows jobs, but they do not run the readers.
  2. The real-file corpus is not run in CI. Agents ran it by hand. A regression would not be seen.
  3. The 16 newest readers (AC3D, OGRE, XGL, IRR, NFF, Terragen, MDC, HMP, CSM, Q3D, RAW, IQM, COB, NDO, PMX, LWS) and WAV cannot be
     seen switched on. They are not in the startup line, the CI jobs on main never build
     them with the option on, there is no sample file, and no log line says which reader ran (checked).
* **A ready-made flip exists but is out of date.** Branch `claude/flip-model-readers` flips FBX, COLLADA, 3DS and BVH in one
  change. It is 68 commits behind main and conflicts in four files (checked). Redo it in small pieces (section 3).
* **What I need from you:** say yes to Step 0, and say which flips you want, in the order of section 2.

### 1.2 The table

**How to read the rating.** "Ready after X" means the work X is small and known. The percent is how far along I think the item
is toward a flip I would recommend (100 means I would recommend it). It is my judgement, not a measurement. "Not ready" means a
real piece is missing. No row says "Ready to flip now": every row needs at least Step 0, and none has run on real Windows.

| Replacement | Replaces | Default now | Evidence | NOT verified | Risk if flipped (what a user would notice) | Rollback | My rating | You say |
|---|---|---|---|---|---|---|---|---|
| **Batch A readers**: OFF, SMD, MD5, MS3D, ASE | Assimp's importers for these five formats | off | Real files of Assimp's test folder: OFF 2 of 2 valid, SMD 3 of 3, MD5 3 meshes and 1 clip, MS3D 4 of 4, ASE 14 of 14 UTF-8. Triangles, area, box and clip length equal Assimp's. 10 test programs with hostile-input and fuzz tests (clean under ASan and UBSan, standalone). CI job "Own stack (OKAY_OWN_STACK=ON, no libsodium)" is green on main with all five on (checked) | Real Windows. SMD and MS3D rigs (no real file has joints, so only made-up files). ASE from other 3ds Max versions. Sanitizers on the engine-linked tests in CI | A model in one of these old formats loads too big, turned, inside out or with a wrong texture, or a rig plays wrong. Few users own such files | `OKAY_OWN_<FMT>=0` at run time (for example `OKAY_OWN_MD5=0`). Assimp stays compiled in for one release | **Ready after Step 0, 85%** | flip OKAY_OWN_OFF, OKAY_OWN_SMD, OKAY_OWN_MD5, OKAY_OWN_MS3D and OKAY_OWN_ASE to on |
| **Batch C readers**: MD2, MD3, IRRMESH, B3D, DirectX X | Assimp's importers for these five | off | All 27 real files read. MD2 5 of 5, MD3 3 of 3, IRRMESH 5 of 5, B3D 3 of 3, X 11 of 11 equal Assimp on meshes, area and box. B3D and the skinned X files also equal Assimp on every node transform, key, inverse bind and weight sum. Hostile-input and fuzz tests clean (standalone). CI own-stack green with all five on (checked) | Real Windows. The engine-linked tests under sanitizers (never run). A real MD3 with tags. A multi-block MSZIP `.x`. A bzip or 64-bit binary `.x` | Same as batch A. MD2 and MD3 play only the first frame (Assimp does the same) | `OKAY_OWN_<FMT>=0` | **Ready after Step 0, 80%** | flip OKAY_OWN_MD2, OKAY_OWN_MD3, OKAY_OWN_IRRMESH, OKAY_OWN_B3D and OKAY_OWN_X to on |
| **Small-format readers**: Terragen (TER), DXF, NFF, XGL | Assimp's importers for these four | off | TER 2 of 2 real files equal Assimp. DXF 5 of 5 read, 4 equal and the 5th has 48 fewer zero-area triangles. NFF 12 of 12 equal on triangles, area and box. XGL 6 of 7 equal (the 7th places its objects as the file says, Assimp leaves them at the origin). Per-file budgets and fuzz tests. CI own-stack has DXF on; the CI jobs on main never build TER, NFF or XGL with the option on (checked) | Real Windows. TER from other writers (both real files are flat). XGL with textures. `.enff`. DXF from other CAD tools | A terrain or CAD file imports with a different triangle count or placement | `OKAY_OWN_<FMT>=0` | **Ready after Step 0, 80%** | flip OKAY_OWN_TER, OKAY_OWN_DXF, OKAY_OWN_NFF and OKAY_OWN_XGL to on |
| **STL and PLY first** (`OKAY_PREFER_OWN_READERS`) | Assimp's STL and PLY importers. The Windows download reads these two through Assimp first today (checked) | off for the download. Our readers always exist and are the only ones in builds without Assimp | Real files: STL 8 of 8 equal Assimp. PLY 9 of 9 that both read are equal (3 more: one truncated file ours refuses, two both refuse). Engine-linked tests clean under ASan, UBSan and valgrind. Seven bugs the corpus found are fixed. Wine run gives the same result as Linux | Real Windows. STL and PLY from CAD and slicer programs beyond the corpus. **There is no run-time switch**: it is a build option only (checked), so you cannot try it in the download until Step 0.8 | A model has slightly different normals or shared vertices, so shading looks different. A file ours refuses is read by Assimp | Build option off. After Step 0.8: `OKAY_OWN_STL=0`, `OKAY_OWN_PLY=0` | **Ready after Step 0, 85%** | flip OKAY_PREFER_OWN_READERS to on |
| **BVH and 3DS readers** | Assimp's BVH and 3DS importers | off | 3DS: 15 of 16 real files read (the 16th is empty, Assimp refuses it too). Meshes, triangles, box and area equal Assimp. The rotation convention was settled on two real files. BVH: 3 of 3, first and last keys equal to four decimals. Engine-linked tests clean under ASan, UBSan and valgrind. MinGW build passes under wine with identical results. CI own-stack green (checked) | Real Windows. Files from other exporters (all real files come from Assimp's test folder). 3DS frame rate (30 is assumed). BVH axes (assumed) | A 3DS prop at the wrong size, or an animation at the wrong speed. BVH gives a skeleton and a clip but no stick-figure mesh (Assimp makes one) | `OKAY_OWN_3DS=0`, `OKAY_OWN_BVH=0` | **Ready after Step 0, 80%** | flip OKAY_OWN_3DS and OKAY_OWN_BVH to on |
| **COLLADA reader** | Assimp's COLLADA importer | off | 26 of 26 real files read (six bugs found and fixed). Meshes, triangles, box, area and clips equal Assimp except explained differences. Clean under ASan and UBSan (also in CI job "Scripting and robustness tests (ASan + UBSan)", checked) and valgrind. Wine passes. CI own-stack green | Real Windows. Files from current Blender, Maya, 3ds Max or SketchUp exports. No morph targets (blend shapes). Curve interpolation is flattened to linear | A model or animation from an exporter we never saw looks wrong. Blend shapes in a `.dae` are lost | `OKAY_OWN_COLLADA=0` | **Ready after Step 0, 75%** | flip OKAY_OWN_COLLADA to on |
| **Own WAV decoder** (`OKAY_OWN_WAV`) | `SDL_LoadWAV` in the launcher, and the engine's narrower default loader `AudioClip::LoadWAV` | off | Hand-computed tests. 21,000 mutated files. A comparison with ffmpeg on 11 files ffmpeg encoded: MS ADPCM, A-law, mu-law, 8-bit, 24-bit and float32 equal bit for bit, IMA within 29 of 32,768. These tests run in CI job "Build & test (Ubuntu)" (734 tests green on main, checked). The default loader is shown wrong by Grok finding 19 | Real Windows. No CI job builds with the option on. WAV from a second encoder (only ffmpeg so far). Streamed WAV (music): `AudioStream` still uses the old parser, the fix is on branch `claude/own-wav-stream`, not merged. A long fuzz run | Game sounds play wrong, silent or too loud. This would be heard at once. For 16-bit PCM, the common case, ours equals the old loader in the tests | `OKAY_OWN_WAV=0` at run time | **Ready after Step 0, 75%** | flip OKAY_OWN_WAV to on |
| **FBX reader** (`OKAY_OWN_FBX`) | ufbx (vendored, always compiled). Assimp is ufbx's last fallback | off | 43 real files: 42 equal ufbx on counts, names, box, area, skin weights, inverse binds, clip samples and material values. The 43rd is damaged and every reader refuses it. Eight bugs found and fixed. Engine-linked tests clean under ASan, UBSan and valgrind. Wine gives the same result. CI own-stack green. CI sanitizer job runs `fbx_hostile` (checked) | Real Windows. Mixamo, Maya, Blender 4.x, Unity or Unreal exports. **Speed and memory on big files: never measured.** Left-handed axes. Maya Arnold and ShaderFX materials (not read, they come out plain). FBX older than 7.1 (ours refuses, ufbx reads it). The inflate code is being replaced (draft PR #913) | The most visible risk of all rows, and the most users: a character or prop from Blender, Maya or Mixamo looks wrong (size, turned, grey materials, stretched skin, an animation that jumps). If our reader refuses a file, ufbx reads it by itself. If it reads it wrongly, nothing catches that | `OKAY_OWN_FBX=0` at run time | **Ready after Step 0 and the FBX extras (section 3.3), 70%** | flip OKAY_OWN_FBX to on |
| **Readers with documented differences**: AC3D, LWO, AMF, X3D, 3MF | Assimp's importers for these five | off | LWO 53 of 54 read, 29 equal Assimp and 9 differ in triangle counts. AMF 11 read, 5 equal and 6 differ where Assimp reads less. X3D 3 of 4 read, all three differ from Assimp (each checked against the file). AC3D 8 of 11 read, 5 equal. 3MF one real file, equal. Budgets, hostile-input and fuzz tests. CI own-stack has LWO, AMF, X3D and 3MF on, not AC3D | Real Windows. 3MF with colours, textures or extensions (no real file exists). LWO texture coordinates against Assimp. AC3D `rot` and texture-repeat rules | The import differs from today's Assimp import. Ours often reads more (second volumes, `USE`d instances), but it is a change. A 3D-print 3MF may lose colours | `OKAY_OWN_<FMT>=0` | **Ready after Step 0 and your OK on the differences, 60%** | flip OKAY_OWN_AC3D, OKAY_OWN_LWO, OKAY_OWN_AMF, OKAY_OWN_X3D and OKAY_OWN_3MF to on |
| **Blender reader** (`OKAY_OWN_BLEND`) | Assimp's Blender importer | off | 28 of 28 real `.blend` files (Blender 2.41 to 2.93) read and equal Assimp on geometry (Assimp reads 27). Hostile-input and fuzz tests clean (standalone). CI own-stack green | **Any real Blender 3.x or 4.x file** (the file layout changed in 3.6 and 4.x, only made-up files so far). zstd-compressed files. Blender 5.0. Animated real files. Windows | Today's `.blend` files fail or import with parts missing. If ours refuses, Assimp reads it | `OKAY_OWN_BLEND=0` | **Not ready, 50%**: needs real 3.x and 4.x files | flip OKAY_OWN_BLEND to on (not yet) |
| **IQM, COB, MDC, HMP, CSM readers** | Assimp's importers for these five | off | Each read on its real files (IQM 1, COB 8, MDC 1, HMP 2, CSM 1) and checked against numbers taken from the raw bytes. Hostile-input and fuzz tests clean (standalone). **Never compared with Assimp** | No comparison with Assimp. IQM texture flip and COB handedness are inferred, not checked in a renderer. MDC compressed first frame has no real example. HMP is HMP7 only and its layout is my reading of two files. COB has no animation, bones or lights. No CI job has built them on. Windows | Wrong geometry (upside down, mirrored, flipped texture) on files of these rare formats | `OKAY_OWN_<FMT>=0` | **Not ready, 45%**: needs an Assimp comparison, run as a program | flip OKAY_OWN_IQM, OKAY_OWN_COB, OKAY_OWN_MDC, OKAY_OWN_HMP and OKAY_OWN_CSM to on (not yet) |
| **OGRE XML and Irrlicht scene readers** | Assimp's OGRE and IRR importers | off | OGRE: 4 of 4 `.mesh.xml` read, meshes equal Assimp. IRR: 21 of 21 read, 9 equal, 12 differ in cube and sphere sizes (ours follows Irrlicht's documented sizes) | **Ours reads less than Assimp**: no skeleton and no animation for OGRE, no animators for IRR. Our reader "succeeds", so the fallback does not run | A skinned Ogre model becomes rigid. An animated Irrlicht scene loses its clips | `OKAY_OWN_OGRE=0`, `OKAY_OWN_IRR=0` | **Not ready, 30%**: make our readers refuse files with a skeleton or animators (so Assimp reads them), or write the missing parts | do not flip these yet |
| **Q3D, RAW, PMX, LWS readers** | Assimp's Q3D, RAW, PMX and LWS importers | off | Hand-built files only, with hostile-input and fuzz tests (clean, standalone) | **No real file exists for any of the four**, so nothing was compared. LWS assumes radians and bank-first rotation. PMX reads SDEF as a linear blend. Q3D byte order is an open point. Windows | Wrong geometry or wrong animation, and no way to know before it happens | `OKAY_OWN_<FMT>=0` | **Not ready, 20%**: needs real files | do not flip these yet |
| **No reader yet, or seam only**: Nendo (NDO), SIB, OpenGEX, MDL, Q3BSP, glTF 1.0, OGRE binary | Assimp's importers for these | n/a | NDO is a seam that recognises the header and refuses every file. OpenGEX and MDL (Quake 1 and Half-Life 1 only) are on branch `claude/readers-e4` (7 commits, not merged, no PR). The rest are not written | Everything | Nothing to flip. These formats stop importing when Assimp is removed | n/a | **Nothing to rate, 0%** | nothing |
| **Own HTTP client** (`OKAY_OWN_HTTP`, Linux only) | curl (a program we start) on Linux. Windows uses WinHTTP, the system's own client | off | HTTP parser, redirects, proxies, TLS 1.2+ through the machine's OpenSSL, ranges, gzip, keep-alive, cookies. 4,054 + 323 + 54 + 31 + 7 checks. 200,000 fuzz iterations clean. CI jobs "Own HTTP client (OKAY_OWN_HTTP=ON, TLS tests required)" and own-stack are green on main since #852 (checked) | **Any real server** (GitHub Releases, Supabase, PlayFab, a CDN). A soak run. Keep-alive over TLS. Proxy passwords, SOCKS, PAC. A look at the parser by someone else. The call sites in the launcher and editor are compiled but not run on a desktop. Windows and macOS | On Linux: a download fails or hangs, then curl runs. **On the Windows download nothing changes**: you cannot try it there (rule 4a gap) | `OKAY_OWN_HTTP=0` or `OKAY_HTTP_CURL=1` | **Not ready, 40%** (the Linux code is about 85% done; the real-world evidence has not started) | flip OKAY_OWN_HTTP to on for Linux builds (not yet) |
| **Own platform layer** (`OKAY_PLATFORM_BACKEND=own`) | SDL2 for window, input, audio device, controllers | off (`sdl2` is the default) | Linux backend (X11, GLX, ALSA, evdev) tested under Xvfb. The player runs on it with our software painter in scripted sessions. 12 tests. CI own-stack (Linux) green | **Everything on real Windows**: the Win32 backend ran under wine only, its audio data path never ran. Real GPU, window manager, input method, file manager. The editor and launcher still open their windows through SDL and cannot leave it. macOS not written. The web build has no own backend | The player has no window, wrong input, no sound, or runs slowly | `OKAY_PLATFORM_BACKEND=sdl2` | **Not ready, 10%** | do not flip |
| **OkayUI** (`OKAY_UI_BACKEND=okayui`) | Dear ImGui in the launcher and editor | off (`imgui` is the default) | Headless: golden images, behaviour tests and fuzz (the sanitizer CI job runs the OkayUI fuzz tests, checked). 5 screens ported, under 1% of about 26,900 ImGui uses in the editor and launcher (checked). Render backends tested on a software GPU and under wine | Any real window, real GPU or Windows. The other 99% of the screens. Gaps listed in section 4 | A ported screen looks or behaves differently. Everything else is unchanged | `OKAY_UI_BACKEND=imgui` | **Not ready, 10%** | do not flip |
| **CodeEdit** (`OkayUI::CodeEdit`) | ImGuiColorTextEdit (the script editor's code view) | off. Used only with the developer-only OkayUI switch | 407 behaviour checks, fuzz, a parity table with the old widget. Adapter tested headless. The rebuilt text model is on branch `claude/own-code-editor-2` (not merged) | Never run in a real window. Windows. No monospaced code font, no IME, no folding. No real-use corpus, no soak | The script editor mis-draws or loses an edit | the developer-only switch | **Not ready, 15%** | do not flip |
| **okay::crypto** (`OKAY_OWN_CRYPTO`) | libsodium (network encryption, session tickets, host identity) | off. libsodium in every release | RFC vectors, 1,927 Wycheproof vectors, 32.6 million differential comparisons with libsodium, a valgrind constant-time audit of 24 builds, fuzz. CI own-stack runs it as `crypto=own(test)`. Two tests of our Ed25519 and SHA-512 run on real Windows (MSVC) in job "Windows updater restart" (checked) | **No outside review.** You decided not to hire one. Almost everything else ran on one Linux machine. `BCryptGenRandom` never ran | A security hole nobody sees | not applicable: the release guard refuses own crypto | **Never on our word (rule 18)** | nothing. Not a flip candidate |

Notes on the table.

* "Ready after Step 0" assumes the per-flip extras of section 3.3 are done too. They are small for the first rows and large for
  FBX.
* The rating of every reader row would be lower if the corpus were judged by breadth. All real files come from one library's
  test folder (a few exporters, many tiny test cases). The roadmap's criterion 1 is only met for that one corpus. Step 0.10
  (the compare tool) is how you give us a wider corpus from your own files.
* The always-on readers (OBJ, glTF 2.0) are already ours in every build and are not rows here.

## 2. Recommended order: one flip per release, smallest risk first

My reading of "one flip per release": one **group** per release. A group has one to five switches that share the same kind of
evidence. Every switch keeps its own rollback. If you want strictly one switch per release, every group splits cleanly, because
each reader has its own option. The project releases several times a day, so ten releases are days, not months.

Rule 17 applies to every row: announce in the release before, flip in one release, keep the fallback compiled in for that
release, delete one release later. The announcement text is in section 3.3.

| Flip | What | Why in this place | Needs first | You say |
|---|---|---|---|---|
| **Step 0** | Prep work, no default changes (section 3.1) | Makes every later flip safe, visible and testable by you | Nothing | "do Step 0" |
| **1** | Batch A: OFF, SMD, MD5, MS3D, ASE | Rare formats. Clean evidence. Rehearses the whole process (announce, flip, rollback, delete) on the least risky files | Step 0 | flip OKAY_OWN_OFF, OKAY_OWN_SMD, OKAY_OWN_MD5, OKAY_OWN_MS3D and OKAY_OWN_ASE to on |
| **2** | Batch C: MD2, MD3, IRRMESH, B3D, X | Rare. The best skinned-file evidence among the small formats | Flip 1 a release old, no bug report | flip OKAY_OWN_MD2, OKAY_OWN_MD3, OKAY_OWN_IRRMESH, OKAY_OWN_B3D and OKAY_OWN_X to on |
| **3** | TER, DXF, NFF, XGL | Rare. Only small differences from Assimp | Flip 2 a release old | flip OKAY_OWN_TER, OKAY_OWN_DXF, OKAY_OWN_NFF and OKAY_OWN_XGL to on |
| **4** | STL and PLY first | More users (3D printing), but the strongest evidence of all, and these readers already serve every build without Assimp | Step 0.8 (run-time switches) | flip OKAY_PREFER_OWN_READERS to on |
| **5** | BVH and 3DS | Low use. Strong evidence | Step 0 | flip OKAY_OWN_3DS and OKAY_OWN_BVH to on |
| **6** | COLLADA | More users. Strong evidence. Breadth gap (other exporters) | Your own `.dae` files tried with the compare tool | flip OKAY_OWN_COLLADA to on |
| **7** | WAV | I put it before FBX on purpose. The old default is wrong today (finding 19). The common case (16-bit PCM) is equal in the tests. Fewer file types than FBX | Streaming decision (`claude/own-wav-stream`), a real-Windows run, an ears check by you | flip OKAY_OWN_WAV to on |
| **8** | FBX | Last of the main formats. Most users, highest damage if wrong | The FBX extras of section 3.3, draft PR #913 merged first | flip OKAY_OWN_FBX to on |
| **9** | AC3D, LWO, AMF, X3D, 3MF | Rare, but they differ from Assimp on purpose. You decide if the differences are fine | Your OK on the differences in the table | flip OKAY_OWN_AC3D, OKAY_OWN_LWO, OKAY_OWN_AMF, OKAY_OWN_X3D and OKAY_OWN_3MF to on |
| **10** | Blender | Wide use, thin evidence for modern files | A real Blender 3.x and 4.x file read and compared; a zstd decision | flip OKAY_OWN_BLEND to on |
| later | IQM, COB, MDC, HMP, CSM | Not compared with Assimp yet | An Assimp comparison run as a program | one sentence per group, once the table says "Ready" |
| later | OGRE XML, IRR | They read less than Assimp | They refuse files with skeletons or animators, or they read them | the same |
| later | Q3D, RAW, PMX, LWS | No real files | Real files | the same |
| later | Own HTTP on Linux | Linux builds are parked (rule 14). Nothing changes on Windows | A real-server run (cheap on a GitHub runner), a soak, a review of the parser | flip OKAY_OWN_HTTP to on for Linux builds |
| no slot | Platform layer, OkayUI, CodeEdit | Blockers in section 4 | | |
| never | libsodium to okay::crypto | Rule 18 | | |

**Deletion, one release after each flip (rule 17).** For a model format, delete that format's `ASSIMP_BUILD_<FMT>_IMPORTER`
line in `engine/CMakeLists.txt` (the block at about line 522). Then Assimp no longer builds that importer and the Windows
programs get smaller. One catch: Assimp's ASE importer is only built together with its 3DS importer (`docs/model_corpus.md`), so
delete the 3DS line only after ASE and 3DS are both a release old. Assimp itself goes only when every format is ours, or an
announced drop is approved (section 4). For FBX, delete ufbx. For WAV, delete the `SDL_LoadWAV` call in
`shared/WavPlayback.hpp` and the old parser in `engine/src/Audio/AudioClip.cpp`.

## 3. What an agent does when the owner says yes

Who runs this: PR #915 (open, not merged) moves third-party work to Claude 2 (`claude2/*`, issue #422) and keeps the coordinator on
releases and merges. The steps are written so either can run them. Whoever runs a flip pushes only their own branch and opens no PR (rule 10);
the coordinator opens and merges it.

### 3.1 Step 0: prep work (changes no default)

Do these in small PRs, each with its "How to try it" (rule 4a). They are in the order I would do them.

| # | Work | Where | Done when |
|---|---|---|---|
| 0.0 | Let the in-flight own-code branches land first, so the flips happen on final code: draft PR #913 (one inflate decoder, used by FBX, Blender, X, XGL, 3MF, AMF), `claude/own-wav-stream` (streamed WAV), `claude/readers-e4` (OpenGEX, MDL) | PRs and branches | They are merged, and the corpus and sanitizer runs below are repeated on them |
| 0.1 | **A real-Windows run of the reader and WAV tests.** Add `test_model_readers`, `test_collada_reader`, `test_3ds_reader`, `test_bvh_reader`, `test_fbx_reader`, the batch tests, `test_model_corpus`, `test_own_wav` and `test_wav_ffmpeg_diff` to the MinGW build in job `windows-cross` and to the run list of job `windows-export` (`runs-on: windows-latest`). Fetch the corpus on the runner with a sparse clone of Assimp v5.4.3 (`test/models`, `test/models-nonbsd`). Watch out: many tests find their data by an absolute path baked in at build time, which will not exist on the Windows runner. Copy the data and use a relative path or an environment override | `.github/workflows/ci.yml` | The Windows job prints every one of those tests as passed, and `model_corpus` reads all manifest rows as on Linux |
| 0.2 | **A CI build equal to the release after a flip**: `-DOKAY_USE_ASSIMP=ON` with the own options on. Add a new test `own_reader_fallback`: when our reader refuses a file, the fallback (Assimp, or ufbx for FBX) reads it. I found no test of it (the fallback code is in `ImportModelData` and the FBX paths of `ModelImport.cpp`; the existing dispatch tests only check that an off reader is not used) | job `assimp-models`, `tests/` | The test passes, and fails when the fallback line is removed (mutation check) |
| 0.3 | **Make the 16 newest readers and WAV visible.** Add AC3D, OGRE, XGL, IRR, NFF, TER, MDC, HMP, CSM, Q3D, RAW, IQM, COB, PMX, LWS and WAV to `OKAY_OWN_STACK`: the `foreach` in the root `CMakeLists.txt`, `Readers()` and `RuntimeSettings()` in `shared/OwnStack.hpp`, `tests/test_own_stack.cpp`, the greps of job `own-stack`. Leave NDO out (it refuses everything). Today the stack line stops at `x` (checked in a green log) | `CMakeLists.txt`, `shared/OwnStack.hpp`, `tests/` | The job `own-stack` builds them all on and the startup line lists them |
| 0.4 | **One log line per import** saying which reader ran: `model import 'file': read by our own X reader` (or "by Assimp", "by ufbx"). The same for WAV in `AudioClip::LoadWAV`. Today the Console says nothing (`docs/try_it.md` calls this an honest gap) | `ModelImport.cpp`, `AudioClip.cpp` | A test reads the line. `docs/try_it.md` says what to look for |
| 0.5 | **Run the corpus in CI.** Job `assimp-models` already holds an Assimp checkout (cached `~/assimp-src`, so the models are there). Add `test_model_corpus` to its build and `model_corpus` to its test list, and set `OKAY_MODEL_CORPUS=$HOME/assimp-src/test`. `fbx_reader` then reads the 42 real FBX files too. Run it locally first: some manifest rows may have drifted | `.github/workflows/ci.yml` | The job is green and prints the corpus row count |
| 0.6 | **A sample file for every format** in `examples/own_readers` (folder already shipped; the script copies top-level files only). Copy our own committed fixtures (`tests/data/models`: `arm.smd`, `skel.md5mesh`, `skel.md5anim`, `rig.ms3d`, `scene.ase`, `walk.bvh`, `blend_attr_41.blend`; `tests/assets/fbx`: `rig_bin75.fbx`, `cube_zup_cm_bin.fbx`) and write tiny files for the rest with a script from the in-memory test fixtures. Our own files only (rule 19a). Update `docs/try_it.md` | `examples/own_readers`, a generator script | Each switch in section 5 has a file that imports |
| 0.7 | **Sanitizers for the engine-linked reader tests in CI.** Today the sanitizer job builds only `fbx_hostile`, `model_readers`, `model_readers_fuzz`, `collada_reader` and `glb_container` (checked in the green log). Add `3ds_reader`, `bvh_reader`, the batch tests, `own_dispatch*` and `model_corpus` (with 0.5) | job `sanitizers` | The job runs them green |
| 0.8 | **Run-time switches for STL and PLY**: `OKAY_OWN_STL` and `OKAY_OWN_PLY`. Today the choice is a build option only | `ModelImport.cpp` | You can set them in the download and the stack line shows them |
| 0.9 | **Own WAV on in a CI build.** Covered by 0.3 | `ci.yml` | The audio tests run with the decoder on |
| 0.10 | **A reader compare tool for your own files.** A small program (or an editor menu item) that imports one file with ours and with the fallback, and prints both reports (meshes, triangles, box, area, materials, clips) and one plain verdict: "same" or "different: ours 3 meshes, Assimp 5". You run it on real files from Mixamo, Blender, Maya, 3ds Max and tell us the verdicts. That widens the corpus without putting files in the repository | `Tools\` in the download | You can drop a file on it and read the verdict |
| 0.11 | Optional: a way back for people who are not developers. An environment variable is enough for you but not for players. A Settings row "Use the built-in model readers" would do. I recommend it before Flip 7 and 8 | editor settings | The row exists and is tested |

### 3.2 Steps for every flip

Use these for every flip. Names in `<...>` change per flip. Rule 20: run `df -h /` first and stop under 5 GB free, build
with `ninja -j2` and only the targets you need, and delete the build trees you made when you are done.

**C1. Announcement.** The previous release's `dist/CHANGELOG.txt` must already say "next version: ...". If it does not, ship that
note first. The flip comes one release later.

**C2. Branch.** `claude/flip-<name>` (or `claude2/flip-<name>`) off a fresh `origin/main`. Do not reuse `claude/flip-model-readers` (see 1.1). Plain push, no force.

**C3. The change.** In the root `CMakeLists.txt` change `option(OKAY_OWN_<FMT> "..." OFF)` to `ON` and fix the option text. Fix
the error text in `ModelImport.cpp` that says "our own reader is off by default". Find every test and doc that says "off by default"
for this format (`grep -rn "off by default"` and `OKAY_EXPECT_OWN_`). The option-ON tests
(`own_dispatch_*`, `own_*_default`, `own_reader_cmake_defaults`, `own_reader_wiring`) switch to the ON expectation by themselves when
the option is on. Update `docs/own_<fmt>_reader.md`, `docs/model_import.md`, `docs/own_code_roadmap.md`, `docs/THIRD_PARTY.md`,
`docs/try_it.md`, `docs/own_flip_proposal.md`. Then `python3 scripts/gen_feature_map.py` and `--check`.

**C4. Corpus run (Linux).**

```
git clone --depth 1 --branch v5.4.3 https://github.com/assimp/assimp "$SCRATCH/assimp"      # only test/ is used
OKAY_MODEL_CORPUS="$SCRATCH/assimp/test" ctest --test-dir build -R "model_corpus|<format tests>" --output-on-failure
```

Every manifest row of the format must read as recorded. A row that changed must be explained in the PR. Paste the counts.

**C5. Assimp comparison, only if the reader changed since its last comparison** (`git log -- engine/src/Render/<Reader>.cpp`).
Build Assimp with only that format's importer, run `scripts/assimp_corpus_dump.cpp` and `scripts/compare_model_corpus.py ours.tsv
assimp.tsv`. Use Assimp as a program only. Do not read its source (rule 19a).

**C6. Sanitizer and valgrind run.**

```
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS_DEBUG=-g1 \
  -DOKAY_SANITIZE=address,undefined,float-cast-overflow -DOKAY_BUILD_TESTS=ON -DOKAY_BUILD_SANDBOX=OFF \
  -DOKAY_BUILD_LAUNCHER=OFF -DOKAY_USE_SODIUM=OFF
ninja -C build-asan -j2 <the format's test targets> test_model_readers test_model_readers_fuzz test_model_corpus
```

Run them with and without `OKAY_MODEL_CORPUS`. If ASan dies at start with DEADLYSIGNAL, run `sudo sysctl -w vm.mmap_rnd_bits=28`.
Run the standalone hostile test with more iterations (for example `OKAY_BATCHB_FUZZ_ITERS=150000`). Then build the same targets
without sanitizers (`-O1 -g`) and run `valgrind --leak-check=full --error-exitcode=1` on them. No finding allowed.

**C7. MinGW and wine run.**

```
cmake -S . -B build-win-tests -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release -DOKAY_BUILD_TESTS=ON -DOKAY_BUILD_SANDBOX=OFF -DOKAY_BUILD_LAUNCHER=OFF \
  -DOKAY_BUILD_PLAYER=OFF -DOKAY_BUILD_EDITOR=OFF -DOKAY_USE_SODIUM=OFF \
  -DCMAKE_PREFIX_PATH="$SDL2_MINGW_PREFIX" -DCMAKE_FIND_ROOT_PATH="/usr/x86_64-w64-mingw32;$SDL2_MINGW_PREFIX"
ninja -C build-win-tests -j2 <targets>
cp "$SDL2_MINGW_PREFIX/bin/SDL2.dll" build-win-tests/tests/
(cd build-win-tests/tests && wine64 ./test_model_readers.exe)
```

`SDL2_MINGW_PREFIX` comes from `scripts/fetch-tarball.sh` as in job `windows-cross`. Wine is not Windows. If wine is not
installed, write "wine not run" in the PR. After Step 0.1, the real-Windows job must be green too.

**C8. Release configuration.** Configure with `-DOKAY_USE_ASSIMP=ON` and the option on (Step 0.2) and run the format's tests and
`own_reader_fallback`.

**C9. The stack line.** `OKAY_PRINT_STACK_AND_EXIT=1 <build>/bin/okay-editor` prints the line. It must list the format in
`readers=own(...)` (and, for FBX, `ufbx` must disappear). The `own_stack` test checks this. If the format is missing from
`Readers()` in `shared/OwnStack.hpp`, Step 0.3 was not finished.

**C10. Rollback proof.** In the PR, import the sample file twice: once normally, once with `OKAY_OWN_<FMT>=0`. The Console line
(Step 0.4) must say "our own reader" the first time and "Assimp" (or "ufbx") the second time.

**C11. No open bugs.** No `confirmed` finding without a fix for this component on issue #888, no row for it in
`docs/UNFINISHED.md` that says "Not started". If the evidence is thin, ask the owner to run the Grok brief on this reader.

**C12. Release.** The coordinator opens and merges the PR (rule 10). Bump `CMakeLists.txt`, add a `ROADMAP.md` row and a `dist/CHANGELOG.txt` entry (text in 3.3). Merge only with every
CI job green, including "Build & test (Ubuntu)" (rule 6). Squash-merge with the exact head sha. Dispatch `release.yml` once (ref
`main`, channel `latest`). Never re-run it for the same version (rule 12). Check the Supabase publish step, the "make latest" step,
and that `versions.json` has a SHA-256 for `OkaySpace.exe`, `OkayEngine.exe` and `OkaySpacePlayer.exe` (rule 13).

**C13. Deletion plan, one release later (rule 17).** Prepare a branch `claude/delete-<fmt>-from-assimp` with the change from section 2
(push it, open no PR). It is merged only when the flip release is a release old with no regression report and the owner agrees. Do not
delete what the flip itself needs as its fallback.

**C14. The PR's "How to try it" (rule 4a).** Install the new release. Open the editor, Help > About OkaySpace: the line says
`readers=own(<fmt>,...)`. File > Import Model... and pick `examples\own_readers\<sample>`. Then start with `set OKAY_OWN_<FMT>=0`
and import the same file: the old reader is used. Say what you should see and what is unverified.

### 3.3 Per-flip details

For each flip: the switches, the tests, the corpus rows, the extra checks, the release-note text and the deletion line. Test names
are `ctest` names. Corpus rows are the reader names in `tests/data/model_corpus_manifest.tsv` (433 lines, checked).

**Announcement template** (one release ahead): "Coming in the next version: <FORMATS> files will be read by our own code first. Nothing
changes in this version."

**Release-note template** (the flip release, `dist/CHANGELOG.txt`, section "Our own code, now on by default"): "<FORMATS> files are
now read by our own code first. Before, a third-party library read them. If a file loads but looks wrong, set `<VAR>=0`
(for example `OKAY_OWN_MD5=0`) and start the program again: the old reader is used. If our code cannot read a file, the old reader
is tried by itself. Please tell us which file went wrong."

| Flip | Tests | Corpus rows | Extra checks | Deletion line (one release later) |
|---|---|---|---|---|
| 1 Batch A | `off_reader off_hostile smd_reader smd_hostile md5_reader md5_hostile ms3d_reader ms3d_hostile ase_reader ase_hostile own_dispatch model_corpus own_reader_wiring` | `off` 5, `smd` 4, `md5` 4, `md5anim` 1, `ms3d` 4, `ase` 17 | Ask the owner for one real rigged `.smd` and one `.ms3d` (the skeleton paths were only checked on made-up files). ASE needs Assimp's 3DS importer: do not delete 3DS from Assimp before ASE and 3DS are both a release old | `ASSIMP_BUILD_OFF_IMPORTER`, `SMD`, `MD5`, `MS3D`, `ASE` |
| 2 Batch C | `md2_reader md3_reader irrmesh_reader b3d_reader x_reader batchc_hostile own_dispatch_c model_corpus` | `md2` 5, `md3` 3, `irrmesh` 5, `b3d` 3, `x` 12 | Run `own_dispatch_c` and `model_corpus` under sanitizers (never done). A real MD3 with tags and a real multi-block MSZIP `.x` if the owner has them | `MD2`, `MD3`, `IRRMESH`, `B3D`, `X` |
| 3 TER, DXF, NFF, XGL | `terragen_reader terragen_hostile own_ter_default dxf_reader batchb_hostile nff_reader xgl_reader batchd_hostile batchd_budget own_dispatch_d model_corpus` | `ter` 2, `dxf` 5, `nff` 13, `xgl` 7 | Build once with the four options on (the CI jobs on main never build TER, NFF or XGL with the option on). Check the XGL 40,000-face timing (0.1 s) still holds | `TERRAGEN`, `DXF`, `NFF`, `XGL` |
| 4 STL and PLY | `model_readers model_readers_fuzz model_corpus gltf_import loaders_robust` | `stl` 8, `ply` 12 | Step 0.8 first. Check that the truncated `ant-half.ply` still falls back to Assimp. Try a 3D-print STL from a slicer | `ASSIMP_BUILD_STL_IMPORTER`, `PLY`. Then the `OKAY_PREFER_OWN_READERS` option and its code |
| 5 BVH and 3DS | `3ds_reader bvh_reader 3ds_bvh_hostile own_dispatch model_corpus` | `3ds` 16, `bvh` 3 | The reader docs say: files from more than one exporter. Ask the owner for one real 3DS asset-pack file and one real mocap file, and use the compare tool (0.10) | `3DS` (only after ASE is a release old), `BVH` |
| 6 COLLADA | `collada_reader model_readers model_import_rig model_corpus` | `dae` 26 | Compare tool on `.dae` files from Blender, SketchUp or Maya | `COLLADA` |
| 7 WAV | `own_wav wav_ffmpeg_diff wav_playback wav_seam_wiring` and the whole audio ctest set in a build with `-DOKAY_OWN_WAV=ON` | none | Turn the rows of Grok finding 19 (A-law, mu-law, 64-bit float, MS ADPCM, 4-channel extensible, NaN samples, rate 0) into tests of the default path. Decide streamed WAV (`claude/own-wav-stream`). Real-Windows run of `own_wav` and `wav_ffmpeg_diff`. You play `examples\own_wav\own_wav_test.okayscene` both ways (old: buzzy; own: a clean 440 Hz tone) | the `SDL_LoadWAV` call in `shared/WavPlayback.hpp`; `ParseWav` and `ConvertWavFrames` in `AudioClip.cpp` once streaming is switched too |
| 8 FBX | `fbx_reader fbx_import_limits fbx_hostile model_import_rig model_corpus` | `fbx-own` 43 | See the FBX extras below | the ufbx list of section 4 |
| 9 AC3D, LWO, AMF, X3D, 3MF | `ac3d_reader lwo_reader amf_reader x3d_reader 3mf_reader batchb_hostile batchd_hostile batchd_budget model_corpus` | `ac` 11, `lwo` 54, `amf` 11, `x3d` 4, `3mf` 1 | Put the documented differences (table in 1.2) in the announcement. The owner says they are fine | `AC`, `LWO`, `AMF`, `X3D`, `3MF` |
| 10 Blender | `blend_reader blend_hostile model_corpus` | `blend` 28 | A real Blender 3.x and 4.x file read and compared (the reader doc names this the largest gap). Decide zstd: support it or announce it as a limit | `BLEND` |

**The FBX extras (Flip 8).** These are in addition to C1 to C14.

1. Merge draft PR #913 first, then repeat C4 to C7 on the final code.
2. **Speed and memory.** The FBX reader doc says "Not benchmarked". Time and measure our reader against ufbx on the largest corpus
   file and on a 50 to 100 MB FBX (a Mixamo character with animations is typical). Put both numbers in the PR and let the owner decide if
   they are acceptable.
3. **Files from other exporters.** Use the compare tool (0.10) on FBX files from Mixamo, Blender 4.x, Maya, and Unity or Unreal
   exports, each imported both ways. The owner sends back the verdicts. The corpus has no file known to come from Mixamo.
4. **Turn silent damage into a fallback.** Make our reader refuse a file that has `Shader` or `Implementation` objects (Maya Arnold,
   ShaderFX), so ufbx reads it. Today ours reads such a file and ignores those shader objects, so the colours and textures come out plain
   or missing, and nothing falls back. (Files older than 7.1 are already refused, so ufbx already reads them.)
5. **Test the fallback** (Step 0.2). The branch `claude/flip-model-readers` has a test hook for this
   (`ForceOwnReaderFailureForTest`). Take the idea, not the diff.
6. A real-Windows run (Step 0.1) and your try of your own FBX files.

**Ready-to-paste lines for the flip release** (`dist/CHANGELOG.txt`, section "Our own code, now on by default"). Each ends with the
rollback. The announcement one release earlier is "Coming in the next version: <the same formats> will be read by our own code first.
Nothing changes in this version."

1. "Old model formats (`.off`, `.smd`, `.md5mesh` and `.md5anim`, `.ms3d`, `.ase`) are now read by our own code first. If a file looks
   wrong, set `OKAY_OWN_OFF=0` (or `OKAY_OWN_SMD`, `OKAY_OWN_MD5`, `OKAY_OWN_MS3D`, `OKAY_OWN_ASE`) and start the program again. If our code
   cannot read a file, the old reader is tried by itself."
2. "`.md2`, `.md3`, `.irrmesh`, `.b3d` and DirectX `.x` files are now read by our own code first. MD2 and MD3 play only their first frame,
   as before. Rollback: `OKAY_OWN_MD2=0`, `_MD3`, `_IRRMESH`, `_B3D`, `_X`."
3. "`.ter`, `.dxf`, `.nff` and `.xgl` / `.zgl` files are now read by our own code first. Rollback: `OKAY_OWN_TER=0`, `_DXF`, `_NFF`, `_XGL`."
4. "STL and PLY files are now read by our own readers first. They used to go to a third-party library first. Rollback: `OKAY_OWN_STL=0`,
   `OKAY_OWN_PLY=0`."
5. "`.bvh` and `.3ds` files are now read by our own code first. A `.bvh` file gives a skeleton and a motion clip but no stick-figure mesh.
   Rollback: `OKAY_OWN_BVH=0`, `OKAY_OWN_3DS=0`."
6. "`.dae` (COLLADA) files are now read by our own code first. Blend shapes in a `.dae` are not read. Rollback: `OKAY_OWN_COLLADA=0`."
7. "WAV sound files are now decoded by our own code. This fixes A-law, mu-law, 64-bit float and ADPCM files, which used to play as loud noise
   or as silence. If a sound plays wrong, set `OKAY_OWN_WAV=0` and tell us which file."
8. "`.fbx` files are now read by our own code first. Maya Arnold and ShaderFX materials are not read by our code, so such files use the
   old reader. If a model looks wrong (size, rotation, materials, skin, animation), set `OKAY_OWN_FBX=0` and start the program again."
9. "`.ac`, `.lwo`, `.amf`, `.x3d` and `.3mf` files are now read by our own code first. They can differ from the old import: <list the
   differences from the table in section 1.2>. Rollback: `OKAY_OWN_AC3D=0`, `_LWO`, `_AMF`, `_X3D`, `_3MF`."
10. "`.blend` files are now read by our own code first. Not read: modifiers, constraints, shape keys, vertex colours, zstd-compressed files.
    Rollback: `OKAY_OWN_BLEND=0`."


## 4. What blocks deleting the third-party code

The order for every library is rule 17: seam, own code off, criteria, flip with the owner's OK, delete one release later. The
file-by-file deletion lists are in `docs/third_party_removal_checklist.md` on branch `claude/thirdparty-followups2` (not merged).
Its TweetNaCl section is out of date: that port is already gone (#849).

### Assimp (BSD-3, v5.4.3, fetched at configure time, code in `engine/src/Render/ModelImportAssimp.cpp`)

1. **Six importers the Windows build switches on still have no reader of ours:** OpenGEX, MDL, Q3BSP, SIB, Nendo, glTF 1.0. Plus the
   binary half of OGRE (`.mesh`, `.skeleton`, `.skeleton.xml`). 39 of the 45 importers are covered (hand-derived in
   `docs/own_readers_remaining.md`, and I got the same count from the import table). OpenGEX and MDL (Quake 1 and Half-Life 1 only)
   are on `claude/readers-e4`. The 3D GameStudio MDL variants (3, 4, 5, 7) need a description we may follow. SIB: no description or
   sample. Nendo: seam only. Q3BSP and glTF 1.0: not started.
2. **34 of our readers are off by default.** Each needs its flip and one release before its Assimp importer can go (section 2).
3. **Readers that read less than Assimp:** OGRE XML (no skeleton), Irrlicht scenes (no animators), COB (no animation), ASE skin
   data, `.vta`, `.md5camera`. A flip would make these worse than today.
4. **Real-file gaps:** Q3D, RAW, PMX and LWS have no real file. MDC compressed frames have none. HMP is HMP7 only. Blender 3.x,
   4.x, zstd and 5.0 are untested. 3MF colours and textures are untested. COLLADA has no morph targets.
5. **The announcement.** `docs/release_note_dropped_model_formats.md` is a draft. It must be one release ahead and the owner must
   decide every format that will still be dropped.
6. **Process.** No reader has run on real Windows. The corpus is not in CI. The old comparison tool needs Assimp built outside the
   engine, so after Assimp is gone the manifest is the only record of the comparison.
7. **Then delete:** the FetchContent block, `ModelImportAssimp.cpp`, `ImportBackend::Assimp`, the options `OKAY_USE_ASSIMP` and
   `OKAY_PREFER_OWN_READERS`, CI job `assimp-models`, `-DOKAY_USE_ASSIMP` in `scripts/build-windows-dist.sh` and `scripts/build-web.sh`,
   the Assimp half of the reader tests. This also removes the zlib Assimp builds.
8. Owner approval.

### ufbx (MIT or public domain, vendored in `engine/third_party/ufbx`)

1. The FBX flip (Flip 8) and one release after it.
2. **Missing before it is safe:** files from Mixamo, Maya, Blender 4.x, Unity and Unreal. Maya Arnold and ShaderFX materials.
   UV set names. Left-handed axes. `InheritType`. **FBX older than 7.1:** ufbx reads old files, ours refuses them, so after ufbx is
   gone these stop importing unless we announce a drop or write support. Damaged-file repair (ufbx recovers from many broken files,
   ours rejects). Speed and memory (never measured). A real-Windows run.
3. **Then delete:** `engine/third_party/ufbx` (`ufbx.c`, `ufbx.h`, `LICENSE`), its lines in `engine/CMakeLists.txt` (source, include
   directory, compile options), the `ufbx_*` code in `ModelImport.cpp` and `FbxScene.cpp`, the option `OKAY_OWN_FBX` and its define,
   the ufbx side of `fbx_reader` and `fbx_import_limits`, and `C` in the `project()` line if no other C file remains.

### SDL2 (zlib licence; `SDL2.dll` ships in every Windows download)

1. **The Windows backend of our platform layer has never run on real Windows.** It is compiled with MinGW and run under wine. Its audio
   data path never ran. No touch, no exclusive full screen, no proof of DPI and multi-monitor on Windows. Windows is the only
   platform we ship.
2. **The editor and launcher still call SDL directly** for windows, events and the GL context: about 500 to 600 SDL words in the
   editor and 940 in the launcher.
3. **Every app draws its 2D through `SDL_Renderer`** (about 330 calls). Our software painter exists for the player only and is "off,
   not ready". The editor and launcher need a painter or an OkayUI render backend. Those backends ran only on a software GPU and
   under wine.
4. **Dear ImGui's SDL backends** (`imgui_impl_sdl2`, `imgui_impl_sdlrenderer2`) read SDL types. They go away only with Dear ImGui.
5. **The launcher's 705 thread and mutex words** (`SDL_CreateThread`, `SDL_Mutex`). The platform docs say "`std::thread` would
   replace it". That is wrong for our Windows build: MinGW's thread model cannot compile `<thread>` or `<mutex>` (`CLAUDE.md` rule 21a). We
   need our own thread, mutex and condition wrapper (Win32 calls on Windows, pthreads on Linux).
6. **Smaller pieces:** `SDL_LoadWAV` (the WAV flip), the tray-icon hook, text-input composition (IME), multi-monitor and DPI, a real
   file manager for drops.
7. **macOS** is not written (parked). **The web build** uses Emscripten's SDL2 port and has no own backend. SDL2 stays for the
   browser unless we write one.
8. Owner approval. Then the 13-step deletion list in the checklist branch (CMake, `SDL2.dll`, CI and release downloads, the installer).

### Dear ImGui (MIT, fetched at a pinned commit)

1. **Size.** `editor/main.cpp` is 49,836 lines with 11,619 `ImGui::` uses. `launcher/main.cpp` is 26,829 lines with 6,423. About 26,900
   uses in the editor and launcher together (checked with `git grep`). Five screens are ported, under 1%.
2. **OkayUI gaps** (`docs/own_ui_toolkit.md` sections 4 and 6): multi-viewports, host-side IME, arbitrary fonts and a code font, table
   column reordering and hiding, password and callback text input, `GetStyle` (85 uses of a struct OkayUI does not mirror), tooltip
   scopes, the editor's `DockBuilder*` layout code (it must be rewritten, not ported), per-draw fonts.
3. **No host without SDL work:** the editor and launcher cannot leave SDL until the platform and painter work is done.
4. **Never run in a real window, on a real GPU, or on Windows.**
5. Dear ImGui is also used by the OkayUI tests and by two legacy font headers (`shared/RobotoFont.h`, `shared/JetBrainsMonoFont.h`).
6. Owner approval.

### ImGuiColorTextEdit (MIT, vendored in `editor/vendor/TextEditor.*`)

1. Our `OkayUI::CodeEdit` exists and is wired behind `ScriptEditorWidget`, but only with the developer-only OkayUI switch.
2. **Missing:** a run in a real window (Linux, then Windows); a monospaced code font; IME and folding; a real-use corpus (open and edit every
   example script through the adapter); a soak. The draw order of the autocomplete popup needs the real window.
3. **It can go before Dear ImGui does.** The script editor is one widget inside an ImGui window. It needs its own switch, because today
   it is tied to the whole OkayUI developer switch (unverified: I read the docs, not the code).
4. Then delete `editor/vendor/TextEditor.*`, its licence file and the fallback in `ScriptEditorWidget`.

### libsodium (ISC)

1. **Rule 18 blocks it.** An outside review is required. You decided not to hire one (2026-11-14). AI reviews are extra assurance only.
   So libsodium stays the default in every release, and the release guard refuses own crypto.
2. **If you ever reverse that, the work is:** consolidate the extra SHA-256 and HMAC copies (draft PR #912), fix `SecureChannel` key
   wiping, add the CI jobs a reviewer would ask for (valgrind, sanitizers, libFuzzer; they exist only on branch
   `claude/thirdparty-followups2`), runs on Windows, macOS and WebAssembly and on other CPUs, a decision on Ed25519 signing speed.
3. **What is already ours:** Ed25519 and SHA-512 when a build has no libsodium; SHA-512 always, inside the anti-cheat wrapper
   (`shared/AntiCheatCrypto.hpp` says so). The startup word `crypto=libsodium` only describes Ed25519. That is by your approval of
   the TweetNaCl replacement. Say so plainly to anyone who reads "libsodium is the only crypto in a release".

### curl (a program we start; not linked)

1. **Linux:** our client as the default needs a real-server run, a soak, a look at the parser by someone else, keep-alive over TLS
   against a real server, and answers for proxy passwords, SOCKS and PAC (people behind them would lose their connection). The launcher
   and editor call sites are compiled, not run on a desktop.
2. **macOS** has no TLS path, so curl stays there (parked).
3. **Windows** already uses WinHTTP (the system's own client). curl and PowerShell are only fallbacks there. Deleting those fallback
   lines is a code change plus your decision. A Windows client of our own would need TLS (Schannel): that is crypto work, not planned.
4. **`scripts/*.sh`, the workflows and `deploy/voice-server/install.sh`** use curl as build tooling. They are not shipped and stay.

## 5. How to try each own replacement (rule 4a)

All switches are environment variables set before starting a program. How (Windows): open a Command Prompt, `cd %LOCALAPPDATA%\OkaySpace`,
`set OKAY_OWN_FBX=1`, then `Tools\OkayEngine.exe`. The variable lives only in that window. The sure way to see the stack line is
**Help > About OkaySpace** in the editor. The samples are in `examples\own_readers` and `examples\own_wav` in the download (the script
`scripts/build-windows-dist.sh` copies `own_readers` and `own_wav`; both exist on main, checked).

| Item | The switch | A sample that ships in the download | How you can tell it is on | Gap |
|---|---|---|---|---|
| FBX | `OKAY_OWN_FBX=1` | **none.** Use any `.fbx` you own. (Fixtures exist in `tests/assets/fbx`, not shipped) | About popup: `readers=own(fbx,...)` and the word `ufbx` goes away | **no sample** |
| COLLADA | `OKAY_OWN_COLLADA=1` | `examples\own_readers\skinned_strip.dae` | `readers=own(dae)` | none |
| 3DS | `OKAY_OWN_3DS=1` | `examples\own_readers\cube.3ds` | `readers=own(3ds)` | none |
| OFF | `OKAY_OWN_OFF=1` | `examples\own_readers\cube.off` | `readers=own(off)` | none |
| BVH, SMD, MD5, MS3D, ASE, Blender | `OKAY_OWN_<FMT>=1` | **none** (fixtures exist in `tests/data/models`, not shipped) | `readers=own(<fmt>)` | **no sample** |
| 3MF, AMF, X3D, DXF, LWO, MD2, MD3, IRRMESH, B3D, X | `OKAY_OWN_<FMT>=1` | **none** (their fixtures are built in memory by the tests) | `readers=own(<fmt>)` | **no sample** |
| AC3D, OGRE, XGL, IRR, NFF, TER, MDC, HMP, CSM, Q3D, RAW, IQM, COB, NDO, PMX, LWS | `OKAY_OWN_<FMT>=1` | **none** | **nothing.** They are not in the stack line (checked in a green CI log: it ends at `x`) and there is no import log line | **no sample and no way to tell** |
| STL and PLY first | `-DOKAY_PREFER_OWN_READERS=ON` at build time only | none | nothing | **no run-time switch, no sample, no sign** |
| WAV | `OKAY_OWN_WAV=1` (`0` to force the old one) | `examples\own_wav\own_wav_test.okayscene` with `mulaw_tone.wav` | You hear it: old = buzzy, own = a clean 440 Hz tone. No startup word and no log line | no sign in the stack line |
| Own HTTP | Linux only: `-DOKAY_OWN_HTTP=ON` then `OKAY_OWN_HTTP=1` | none | `http=own` (Linux builds) | **cannot be tried on the Windows download** |
| Own platform layer | `OKAY_PLATFORM_BACKEND=own`, then `Tools\OkaySpacePlayer.exe` on an exported game | any exported game | `platform=own` | unverified on Windows. The editor and launcher still use SDL windows |
| OkayUI | `OKAY_UI_BACKEND=okayui` | none needed | About popup `ui=okayui`. Ported screens: editor Console, Crash Log, Audio Mixer; launcher What's New, Tips | unverified in the Windows build; no real-window run anywhere |
| CodeEdit | `OKAY_UI_BACKEND=okayui` plus `OKAY_DEV=1`, then open a script in the editor | any script | none | no sign, never run in a real window |
| okay::crypto | none, on purpose | none | the line must say `crypto=libsodium` | nothing to try |

**Gaps to close (all in Step 0):** no sample for 32 of the 35 reader switches (only OFF, 3DS and COLLADA have one); no way to tell that the 16
newest readers or WAV are on; no run-time switch for STL and PLY; no import log line for any reader. The same table in short form is in
`docs/try_it.md`, section 14.

## 6. Claims I found wrong or out of date

I fixed the plainly outdated wording in these docs in this change. "Left" means I did not change it and say why.

| # | Where | What it said | What is true | Action |
|---|---|---|---|---|
| 1 | `docs/THIRD_PARTY.md`, Assimp row | "24 now have an own reader", "21 are still to write" | 39 of the 45 are covered (checked against the import table), 6 importers plus OGRE's binary half are not | fixed |
| 2 | `docs/THIRD_PARTY.md`, ufbx row | "five bugs fixed" | eight (`docs/own_code_roadmap.md` lists them) | fixed |
| 3 | `docs/THIRD_PARTY.md`, ImGuiColorTextEdit row | "OkayUI code-editor widget; not started" | `OkayUI::CodeEdit` exists, is wired behind the developer-only OkayUI switch, never run in a real window | fixed |
| 4 | `docs/own_code_roadmap.md`, top table, Assimp row | "3DS, Blender and about fifteen other formats still need Assimp" | 3DS, BVH, Blender and 30 more formats have readers of ours (39 of 45 covered); 6 importers do not | fixed |
| 5 | same, ufbx row | "only tested on fixtures our own script wrote, not on real Mixamo or Blender files" | tested on 43 real files from Assimp's folder. None is known to be from Mixamo | fixed |
| 6 | same, inventory, Assimp row | "29 ... 16 are still to write" | 39 and 6 (checked) | fixed |
| 7 | same, HTTP row | "the later changes (call sites, keep-alive, cookies) have not run there yet" | job "Own HTTP client" is green on main since #852 (run 43655440539 and later, checked) | fixed |
| 8 | same, libsodium row | "nothing on Windows" | the two tests `anticheat_golden` and `anticheat_integrity_fallback` run our Ed25519 and SHA-512 on `windows-latest` with MSVC and pass (job "Windows updater restart", checked in the log). Only those two. `BCryptGenRandom` never ran | fixed |
| 9 | same, Dear ImGui row | "about 19,000 lines in `editor/main.cpp`" | 49,836 lines (checked) | fixed |
| 10 | `docs/own_http_readiness.md` | "PR #852 (open draft, not merged)", "until it merges ...", "the CI job has still never been seen running on GitHub" | #852 merged as `bb5a977e`. The job ran green on main (checked) | fixed |
| 11 | `docs/own_http_client.md` | the NOT DONE list says the launcher downloads, size probes, link preview, editor update download and PlayFab "still spawn curl" | since #852 they try our client first | fixed |
| 12 | `docs/own_stack_rollout.md` | "we cover 24 of those 45", "branch" status of the readiness page, the release numbers 2.872.0 to 2.880.0, "the own-stack job has not been seen running", "the own-http job has not been seen", "no connection reuse", curl "rows 2, 4, 5, 6, 7, 9" | 39 of 45; the page is on main; the numbers slipped (2.872.0 and 2.874.0 shipped without these steps); both jobs ran green on main; keep-alive exists; all nine runtime places try our client first | fixed (status box at the top, sentences fixed, the old release table kept and marked superseded) |
| 13 | `docs/UNFINISHED.md` | own-stack "not seen running on GitHub"; own HTTP "a run on GitHub CI"; crypto "CI jobs ... `OKAY_OWN_CRYPTO=ON` ... Not started" | the jobs ran green; own-stack builds with `OKAY_OWN_CRYPTO=ON`; two crypto tests ran on real Windows | fixed |
| 14 | `docs/model_corpus.md`, "Flip readiness" | "cover 30", "15 importers and the binary half of OGRE would be dropped" and the list | 39 covered; 6 would be dropped plus OGRE's binary half | fixed |
| 15 | `docs/release_note_dropped_model_formats.md` | lists Q3D, MDC, HMP, RAW, COB, CSM, PMX, IQM and LWS as "will stop importing" | they have readers (off by default). It is a draft, so I only fixed the lists and kept it a draft | fixed |
| 16 | `docs/platform_layer.md` | "`std::thread` would replace" `SDL_CreateThread` and the mutexes | not on our Windows build (rule 21a). It needs our own wrapper | fixed |
| 17 | `docs/own_ui_code_editor.md`, status line | "not used by the editor. Nothing was wired in" | step 27 of the same page wired it behind `ScriptEditorWidget` (developer-only) | fixed |
| 18 | `docs/crypto.md`, `docs/crypto_review_package.md` | "Not in CI yet (... no `OKAY_OWN_CRYPTO=ON` job)", "No Windows ... run at all" | the own-stack job has it on; two tests ran on real Windows (see 8) | fixed |
| 19 | `docs/own_wav_decoder.md` | "nothing was run on Windows or on GitHub CI" | the tests run in job "Build & test (Ubuntu)" with the option off (they call the decoder directly). Nothing runs with the option on in CI, nothing on Windows | fixed |
| 20 | `ROADMAP.md`, line about the own-stack rollout | proposes own readers to default on in 2.872.0, own HTTP in 2.874.0 | those releases shipped without them | fixed |
| 21 | `docs/animation.md` Tips | "Mixamo FBX: animations import via Assimp" | FBX is read by ufbx (ours with `OKAY_OWN_FBX=1`). Assimp is only the last fallback | fixed, and the feature-map note that flagged it |
| 22 | `docs/feature_map/41-build-tools-infra.md` 41.11, `03-modeling-import.md` 3.6 | the CI job "not seen running"; "12", "16" and "29" for the same count | seen green; 39 of 45 | fixed |
| 23 | **Release PR #914**, `dist/CHANGELOG.txt` for 2.875.0 | "Twelve more of our own model readers ... Each has an environment variable OKAY_OWN_<FORMAT>=1 **and a sample file in examples/own_readers**" | `examples/own_readers` has three files: `cube.3ds`, `cube.off`, `skinned_strip.dae`. None of the twelve has a sample (checked) | **left**: it is not my branch. The coordinator should fix the text before the release |
| 24 | `docs/provenance_audit.md` | two rows are both numbered 12 | cosmetic | left |

Also noted, not a claim in a doc: branch `claude/flip-model-readers` is out of date (see 1.1).

## 7. Sources, and what I did and did not check

**Read:** `docs/own_code_roadmap.md`, `own_stack_rollout.md`, `model_corpus.md`, `own_fbx_reader.md`, `own_3ds_reader.md`, `own_bvh_reader.md`,
`own_blend_reader.md`, the other reader pages (their "Before it can become the default" lines), `model_import.md`, `own_readers_remaining.md`,
`release_note_dropped_model_formats.md`, `own_wav_decoder.md`, `own_http_client.md`, `own_http_readiness.md`, `platform_layer.md`,
`own_ui_toolkit.md`, `own_ui_code_editor.md`, `own_code_editor.md` (on branch `origin/claude/own-code-editor-2`, read with `git show`),
`crypto.md`, `crypto_review_package.md`, `provenance_audit.md`, `THIRD_PARTY.md`, `UNFINISHED.md`, `try_it.md`,
`third_party_removal_checklist.md` and `own_platform_status.md` (both on branches), the CMake options, `tests/CMakeLists.txt`,
`.github/workflows/ci.yml` and `release.yml`, `scripts/build-windows-dist.sh`, issue #888 and open PRs #910 to #915.

**Checked by me (code or GitHub):**

* The import table in `engine/src/Render/ModelImport.cpp`: 35 reader options plus FBX. NDO is a seam. 39 of 45 importers covered.
* `shared/OwnStack.hpp` and the root `CMakeLists.txt`: `OKAY_OWN_STACK` and the stack line cover only 20 readers (FBX and 19 formats).
* STL and PLY preference is compile-time only (`OKAY_PREFER_OWN_READERS_DEFAULT`).
* Fallback code exists in `ImportModelData` and the FBX paths. No test of it found.
* `examples/own_readers` holds three files. `examples/own_wav` holds two. Both folders are in `EXAMPLE_DIRS`.
* The default WAV parser ignores the format tag (`AudioClip.cpp`), as Grok finding 19 says.
* Line counts of the editor and launcher, and the `ImGui::` count.
* GitHub CI on main, run 43754132084 (head `24efceb0`): all 16 jobs green, including own-stack (131 tests, stack line printed with
  `crypto=own(test)` and readers ending at `x`), own-http (6 tests), "Model readers with Assimp" (7 tests), "Build & test (Ubuntu)" (734 tests),
  "Windows updater restart" (real Windows: `anticheat_golden` and `anticheat_integrity_fallback` passed). Runs 43655440539 (own-http first
  green after #852) and 43656201342 (own-stack first green on main).
* The sanitizer job's test list (from its log).
* `git merge-tree` shows `claude/flip-model-readers` conflicts with main in four files.

**Not checked, not run:** every test count, corpus number and percentage in the table is copied from the docs. I built nothing and ran
no test, no corpus, no sanitizer, no wine, no Windows. All "equal Assimp" statements are the docs' statements. My ratings are judgement.

**Open questions for the owner**

1. Do you approve Step 0? (It changes no default.)
2. Is "one group per release" fine for "one flip per release"?
3. Do you want a Settings row for the way back (Step 0.11) before the WAV and FBX flips, or is the environment variable enough?
4. For OGRE XML and Irrlicht scenes: make them refuse files they cannot read fully (so the old reader runs), or keep them off?
5. Provenance audit question 4 is still open: is it fine to compare our readers with Assimp by running it (never by reading its source)? The
   batch A to D evidence rests on that.
6. Will you run the compare tool (Step 0.10) on your own real files and send back the verdicts?
7. Linux is parked (rule 14). Should the real-server run of our HTTP client go ahead anyway? It is cheap on a GitHub runner.
