# Provenance audit: code that may have been copied or ported

> **What this is:** The record of what in the repository may have been copied, ported or taken from others (code, data,
> assets, names, patents). Audits: 2026-11-26 (whole repository, branch `claude/ip-audit-1`) and 2026-11-14 (branch
> `claude/provenance-audit`, main at `ff89d98a`).
>
> **Status:** Internal record (audit, plan or status): read it as a dated record, newest facts in the code.
>
> **Last checked:** 2026-11-26 (whole-repository re-audit on main `d18e4027`, section "2026-11-26" below; the 2026-11-14
> text after it is kept as the earlier record).

## 2026-11-26: whole-repository re-audit (main at `d18e4027`)

Asked by the owner on 2026-11-26: "Implement systems independently using publicly documented standards, mathematical
principles and technical specifications. Do not copy proprietary source code or incorporate third-party code without
identifying its source and license. Flag potential intellectual property conflicts", "make sure to look engine wide".
Branch `claude/ip-audit-1`. Read only on source code: nothing outside this page, `docs/status/THIRD_PARTY.md` and
`scripts/provenance_scan.py` was changed. Every fix below is a recommendation for a later, separate change.

**Not legal advice.** Licences and patents below are what the files say plus what the auditor remembers; the network
proxy refused the licence pages tried (for example www.unicode.org), so **no licence or patent was checked online**.
"From memory" means exactly that.

**Short answer.** Three pieces of code or data clearly or probably follow another author's published code: the
marching-cubes tables (copied, with the source named), the simplex noise (follows Gustavson's public-domain reference
code line for line in structure) and the FXAA shader (follows the widely reposted FXAA 2 shader by Timothy Lottes /
NVIDIA). Our own Windows programs say "MIT License" in their version information while `LICENSE` is proprietary.
Several third-party runtimes are compiled into what we ship and are not listed or noticed (the MinGW C and C++
runtime and winpthreads, static libsodium, the whole Emscripten runtime in web builds). The new big systems
(JavaScript engine, browser, Lua, RHI, physics, okay-cc, okay-ms, okay-build, Xbox UWP, okay.js) show no sign of
copied code in this scan; see "Clean" and "Limits".

### Summary, most serious first

| # | Severity | Where (file:line) | Likely source and licence | How sure | Recommended fix |
|---|---|---|---|---|---|
| A1 | **Clear copy (data) + probable port (structure)** | `engine/src/Components/VoxelTerrain.cpp:59-334` (`kEdge[256]`, `kTri[256][16]`), `:336-338` (corner and edge order, "Bourke order"), the polygonise loop `:877-905` (`cubeindex`, `val[i] < iso`) | Paul Bourke, "Polygonising a scalar field" (1994), tables credited there to Cory Gene Bloyd. Licence: from memory, offered freely / public domain; **unverified**. The file names Bourke but records no licence | High: same 256 + 4096 values, same lower-case hex in rows of 16 with no spaces, same `cubeindex` loop | Owner decision (rule 19a: public-domain needs approval). If not approved: build the tables at start-up from our own case analysis of the 256 corner patterns (or switch to our own surface-nets / marching-tetrahedra mesher), with a watertightness test. Either way add the source line |
| A2 | **Probable close port of public-domain code** | `engine/include/okay/Math/Noise.hpp:86-126` (`Simplex2`, `Simplex3`) | Stefan Gustavson, "Simplex noise demystified" (2005), its Java reference code; public domain by the author's statement (from memory, unverified) | High on structure: same names (`x0 y0 i1 j1 i2 j2 k2 x1..x3 t0..t3`), the same six-way `if (x0 >= y0)` ordering, the same 0.5 / 0.6 radii and the 70 / 32 output scales. The hash and gradients are our own. No source is named in the file | Owner decision (rule 19a). If not approved: rewrite from the idea (skewed simplex lattice, rank the coordinates, sum the corner kernels) in our own structure, choose and test our own kernel radius and normalisation. See also P1 |
| A3 | **Probable close port** | `engine/src/Render/GLRendererShaders.cpp:602-623` (`kFxaaFrag`), `engine/include/okay/Render/D3DPostHLSL.hpp:69-88` (`PSFxaa`) | The FXAA 2 / "FXAA PC console" shader by Timothy Lottes (NVIDIA, 2009-2011) as reposted in many GLSL versions. NVIDIA's FXAA 3.11 header carries an NVIDIA copyright and its own licence; reposts carry various or no licence (from memory, **unverified**) | High: `rgbA` / `rgbB`, `dirReduce`, `rcpDirMin`, reduce-min 1/128, reduce-mul 1/8, span 8, taps at 1/3 - 0.5 and 2/3 - 0.5, the `lB < lMin \|\| lB > lMax` choice, and the 0.0312 / 0.125 edge thresholds of FXAA 3.11. The comment says "the classic 3.x formulation" | Rewrite an edge anti-aliasing pass from the idea (local luma contrast, edge direction from the luma gradient, blend along the edge) with our own taps and constants chosen by tests; or reuse the software renderer's own "FXAA-lite" idea (`SoftwareRenderer.hpp:1484`). Rename the UI label "FXAA" (an NVIDIA name) to "Edge smoothing" |
| A4 | **Our own licence stated wrongly in shipped files** | `cmake/okay_version.rc.in:23` (`LegalCopyright ... MIT License.`), used for every Windows `.exe` | `LICENSE` is the proprietary OkaySpace licence (README.md:410 and CHANGELOG say so) | Certain | Change the string to "Copyright (c) 2026 OkaySpaceGameEngine. All rights reserved. See LICENSE." (source change) |
| A5 | **Third-party code shipped, not listed, no notice** | `cmake/mingw-w64-x86_64.toolchain.cmake:45-54`: libgcc, libstdc++ and libwinpthread linked statically into every Windows program; `:20-43` removes winpthreads' own version resource | GCC runtime libraries: GPLv3 with the GCC Runtime Library Exception; winpthreads: MIT-style licence that asks for its notice in copies (both from memory, **unverified**) | Certain that they are linked; licence terms unverified | Add the notices to the release notice file (F6) once the owner confirms the terms. Now listed in `THIRD_PARTY.md` |
| A6 | **Third-party code shipped, not listed, no notice** | Web export: `player/CMakeLists.txt:8-23` (`-sUSE_SDL=2`), `.github/workflows/ci.yml:868-880` (Emscripten 3.1.51) | Emscripten's JavaScript runtime and system libraries (its C library, libc++ / libc++abi, compiler-rt, malloc) and its SDL2 port go into `okay-player.js` / `.wasm` (licences: Emscripten MIT / NCSA, musl MIT, LLVM Apache-2.0 with LLVM exception, SDL zlib; from memory, **unverified**) | Certain that they are built in; licences unverified | Same as A5; also a replacement plan line in `docs/status/own_code_roadmap.md`. Now listed in `THIRD_PARTY.md` |
| A7 | Notice gap (known, F6, still open) | libsodium is linked **statically** into release builds (`engine/CMakeLists.txt:660-668`, `SODIUM_STATIC`, `OKAY_REQUIRE_SODIUM`); Dear ImGui (MIT) in the editor and launcher; SDL2 (zlib) DLL bundled; `steam_api64.dll` bundled when `STEAMWORKS_SDK_PATH` is set (`scripts/build-windows-dist.sh:272-275`); Roboto Medium (Apache-2.0 per its name table) embedded in the engine (`shared/RobotoFontOwn.h`) and so in every exported game, still with no licence file | ISC, MIT, zlib, Valve SDK terms, Apache-2.0 | Certain about what is built in; no step writes a notice file. **Improved since 11-14:** `scripts/build-windows-dist.sh:180-183` copies `assets/fonts/*`, so the DejaVu licence and the JetBrains Mono OFL now travel with the Windows package | One `THIRD_PARTY_NOTICES.txt` in every release archive, installer, exported game and web build; Roboto licence text from the owner (question 7 of 11-14) |
| A8 | Trademark / look-alike | `engine/include/okay/Browser/Profile.hpp:24` (`kDefaultSearchUrl` = Google), `engine/src/Browser/Profile.cpp:67` and `okayui/screens/BrowserView.hpp:85` ("Search Google or type an address", close to Chrome's own "Search Google or type a URL"), `shared/BrowserApp.cpp:801` ("Use Google") | Google's name and Chrome's wording; Google's terms for a default search engine (unverified) | Certain that the name and the near-identical wording are there | Neutral placeholder ("Search or type an address"), let the person pick a search address on first run, or ask the owner whether to keep Google as the default. (Browser files are being changed in other worktrees today; this audits main) |
| A9 | Trademark (low; naming other products in UI text) | "Minecraft-style" in UI strings: `editor/main.cpp:30012, 30015, 30302, 30970, 35007, 35013, 37866, 38463, 38528, 41808`, `editor/UIMenuModel.hpp:283`, `engine/src/Scene/BuiltinPropertiesGameplay.cpp:160, 247`. "ACES" as a tone-map name (`editor/main.cpp:11334`, `shared/EditorPanelModels.hpp:676`; the curve is Narkowicz's fit, not the Academy's ACES transform). "FXAA" (NVIDIA's name) in many UI labels | Mojang / Microsoft, the Academy (A.M.P.A.S.), NVIDIA | Certain they are there; whether any is a problem is unverified | Use plain words: "block-game style", "Filmic (ACES fit)" or "Filmic A / B", "Edge smoothing" |
| A10 | Data needing a source note | Hable filmic curve A-F and W = 11.2: `GLRendererShaders.cpp:567-569`, `D3DPostHLSL.hpp:34-36` (named only in `Atmosphere.hpp:119`); ACES fit: `GLRendererShaders.cpp:91, 566`, `D3D11Hlsl.hpp:194` (named in `SoftwareRenderer.hpp:1339`); interleaved gradient noise 52.9829189 / 0.06711056 / 0.00583715: `GLRendererShaders.cpp:292`, `D3D11Hlsl.hpp:300` (no note); 9-tap gaussian in 5 linear taps 1.384615 / 3.230769 / 0.227027 / 0.316216 / 0.070270: `DofMath.hpp:92-94`, `SsaoMath.hpp:44-48`, `GLRendererShaders.cpp:655-657`, `D3DPostHLSL.hpp:29-31` (derived in a note only in `RHI/BloomPass.cpp:4-5`); IMA ADPCM step table `launcher/VoiceChat.hpp:89` (no note; `WavDecoder.cpp:22` has one) | John Hable (GDC 2010), Krzysztof Narkowicz (2016), Jorge Jimenez (SIGGRAPH 2014), Daniel Rákos (2010; derivable from binomial weights), IMA / DVI recommended practice | Certain they match those publications; each is a short published formula, low risk | One comment line naming the paper or talk at each place (and show the gaussian derivation once) |
| A11 | Asset or fixture with no recorded origin (likely ours or tool output) | `launcher/okayspace.ico` = `editor/okayspace.ico` (a planet icon, 7 sizes); `platform/xbox_uwp/app/Assets/*.png` (plain dark-blue squares, no artwork); `tests/data/audio/ogg`, `mp3`, `flac` (encoder strings in the files: ffmpeg `Lavf60.16.100`, LAME 3.100, libvorbis; the signal and the script that made them are not recorded); `examples/own_wav/mulaw_tone.wav`, `examples/spatial_audio/tone.wav`, `examples/ui_quality/ui_quality_icon.png`, `examples/obj_texture_paths/*`, `okayui/tests/golden/*`, `tests/golden/*`, `tests/export/*` | Unknown | The encoder facts are certain; who made the icon is unknown | Owner or author to record the origin (the icon especially: it ships). The test audio and its encoders are now in `THIRD_PARTY.md` |
| A12 | Possible, weak (names only) | `tools/okayms/src/gen.hpp:181-193` (`gen_expr`, `gen_stmt`, `gen_addr`, the names chibicc uses, Rui Ueyama, MIT); `tools/okaycc/include/__tree`, `__hash_table`, `__reverse_iterator` (libc++'s internal file names, LLVM licence); `okayui/include/okay/UI/OkayUIImGuiNames.hpp` (Dear ImGui API names such as `ImHashStr`, `ImGuiStorage` as aliases); `tools/okayms/stdinc/windows.h`, `winhttp.h`, `bcrypt.h`, `intrin.h` (Win32 declarations retyped from Microsoft's documentation) | as named | Low: the bodies differ (okay-ms emits machine bytes from C++ classes; `__tree` is an AVL tree, libc++'s is red-black; OkayUI's `HashStr` is FNV-1a, Dear ImGui's is CRC-32; the Windows headers say "own words") | None needed; keep the "written from" lines. An overlap run against chibicc and libc++ would settle it (not available here) |
| P1 | Patent risk (possible, unverified) | Simplex noise `Noise.hpp:86-126` | Ken Perlin, US 6,867,776 (simplex noise for texture synthesis in 3D and up); believed expired in 2022 (from memory) | Unverified | Owner / counsel to confirm expiry; the A2 rewrite does not change this |
| P2 | Patent risk (possible, unverified) | BC1-BC3 (S3TC / DXT) encoder and decoder `engine/include/okay/Texture/Bc.hpp`, `engine/src/Texture/`; MP3 decoder `engine/src/Audio/Mp3Decoder.cpp`; marching cubes (A1) | S3 Graphics S3TC patents, MPEG audio layer III patents, US 4,710,876: all believed expired (2017-2018, about 2017, 2005; from memory) | Unverified | Confirm once; no code change expected |
| P3 | Patent risk (possible, unverified) | Anti-cheat (`engine/src/Net/AntiCheat.cpp`, `engine/include/okay/Net/CheatDetect.hpp`, `shared/AntiCheat*.hpp`), server lag compensation and rollback netcode (`engine/src/Net/`), SSAO (`GLRendererShaders.cpp:432`) | Fields with many patents; **no specific patent was identified** | Unverified | Owner decision whether a patent search is wanted before shipping the anti-cheat and netcode commercially |
| N1 | Not IP, found on the way (rule 18 / 19c) | `launcher/VoiceChat.hpp:55-84`: a second, own ChaCha20 outside `okay::crypto` (which has `engine/src/Crypto/ChaCha20.cpp`), and a second IMA ADPCM table (`:89`, also `WavDecoder.cpp:22`) | ours | Certain | Use `okay::crypto` / libsodium and the one IMA table (Claude 2's lane: duplicates and crypto rules) |

### Fixes (branch `claude/ip-fixes-1`, 2026-11-26)

`python3 scripts/provenance_scan.py check` (run by `scripts/precheck.sh`) fails if a fixed finding comes back.

- **A1 fixed (tables now worked out by our code).** `engine/src/Components/VoxelTerrain.cpp` no longer holds the
  published 256-entry edge table and 256 x 16 triangle table, nor the corner order and loop that went with them.
  `CubeCases()` builds all 256 cases once at start-up from the idea of the method (Lorensen and Cline 1987): our own
  corner numbering (bit 0 = x, bit 1 = y, bit 2 = z), crossings joined by walking each cube face (air corners on a
  face diagonal are cut off one by one, so neighbouring cubes always agree), loops split into triangles with no
  inner edge lying in a cube face. Both mesh loops (`AppendCubes`, `AppendCubesReference`) read it. Checked: every one
  of the 256 single-cube patterns and six random fields mesh into closed, consistently wound surfaces
  (`tests/test_voxel.cpp`); a stand-alone run of both tables on the same random fields gave the same triangle count
  (10,788) and no open edge for either. The first version of our split (a plain fan) failed that test on 4 of the
  256 patterns (triangles lying in a shared face), which is why `SplitLoop` exists. The source line asked for by the
  audit is the comment above `CubeCases()`. Guard: `check`.
- **A2 fixed (rewritten from the idea).** `Simplex2` / `Simplex3` in `engine/include/okay/Math/Noise.hpp` are one
  D-dimensional routine of our own: skew to the simplex lattice, rank the fractional parts to find the simplex's
  corners, sum a radial kernel times a gradient per corner. Our numbers, worked out and tested: kernel (r^2 - d^2)^3
  with r^2 = 1/2 (the smallest distance^2 to a lattice corner outside the point's own simplex, measured as 0.5000 in
  2D and 3D, so the noise is continuous: the old 3D radius 0.6 jumped at simplex borders); 16 gradient directions
  in 2D, the 12 icosahedron directions in 3D; output scales 33 and 41 from a search for the largest possible corner
  sum (0.030176 and 0.024307). Values changed; nothing in the engine, the goldens or a saved scene reads
  `NoiseField` (only `tests/test_math_geometry.cpp`). Test: continuity (steepest slope at a 1e-4 step 33.7 before,
  5.8 after, vs 6.5 / 5.9 at 1e-3), range without the clamp, centring, per-seed determinism, no grid direction.
  P1 (the patent question) is unchanged. Guard: `check`.
- **A3 fixed (our own pass).** `kFxaaFrag` / `PSFxaa` are replaced by `kEdgeSmoothFrag` (GL) and `PSEdgeSmooth`
  (Direct3D, `D3DPostHLSL.hpp`; the shader bundle entry is renamed too). Written from the general idea only (find a
  brightness edge, blend along it): Rec. 709 brightness of the pixel and its four side neighbours, an edge when their
  spread passes 0.02 + 0.06 x the brightest, taps at 1 and 2.5 texels along the edge (across the brightness slope),
  weights 1 : 2 : 1, mixed in premultiplied alpha, blended in by 8 x the spread. Constants chosen on Mesa with the
  test below: a turned 64 x 64 cube, 160 pixels changed, part-covered silhouette pixels 71 -> 151, total variation
  -2.7%, nothing outside its 5 x 5 range (the old FXAA pass, measured the same way: 185, 71 -> 171, -2.4%). None of FXAA's names, taps or
  constants. UI text says "Anti-aliasing (edge smoothing)" / "Edge smoothing"; saved keys stay `fxaa` (scene post,
  project settings, game.okayconfig) and code names such as `FXAAEnabled()` stay (frozen). `tests/test_gl_post_alpha.cpp`
  renders a turned cube on Mesa with the switch off and on. The Direct3D text is only text-checked
  (`tests/test_d3d_post_hlsl.cpp`): UNVERIFIED on a GPU. Not changed: `editor/BuildSettingsModel.hpp` still labels the
  build setting "FXAA" (Claude 3's file; needs their Agree). Goldens changed: `okayui/tests/golden/editorpanels_post.png`
  and `editorpanels_post_presets.png` (only the "Anti-aliasing (edge smoothing)" label and A9's "Punchy filmic"
  tone-map entry differ, checked by eye side by side). Guard: `check`.
- **A4 fixed.** `cmake/okay_version.rc.in` now says "Copyright (c) 2026 OkaySpaceGameEngine. All rights reserved. See
  LICENSE." Guard: `check` refuses another licence name in `LegalCopyright`.
- **A5-A7 fixed (notices; texts partly unverified).** `dist/THIRD_PARTY_NOTICES.txt` lists the GCC runtime (with the
  GCC Runtime Library Exception text), the MinGW-w64 C runtime and winpthreads, libsodium 1.0.20, SDL2 2.30.9, Dear
  ImGui, `steam_api64.dll`, the Emscripten runtime, Roboto, JetBrains Mono and DejaVu, with the licence texts taken
  from the build machine's package copyright files, Dear ImGui's LICENSE.txt and the font licence files. The
  Emscripten and Steamworks lines and the LLVM exception text are from memory and say UNVERIFIED; the libsodium
  statement is Debian's for 1.0.18. Shipped by `scripts/build-windows-dist.sh` (root and `Tools\`, so the installer
  too; the uninstaller removes both), `editor/BuildGame.cpp` (every exported desktop game) and `scripts/web_package.py`
  (every web build and its zip; `tests/scripts/test_web_package.py`). `assets/fonts/Roboto-LICENSE.txt` (Apache 2.0)
  added. Not opened: a real release archive or exported game. Guard: `check`.
- **N1 fixed.** `launcher/VoiceChat.hpp` has no ChaCha20 or IMA table of its own. Its frames go through the network
  layer's crypto seam, `okay::net::netcrypto::StreamXor` (new in `engine/include/okay/Net/NetCrypto.hpp`): libsodium's
  `crypto_stream_chacha20_ietf_xor_ic` in every release build, `okay::crypto::ChaCha20Xor` only in a build that has
  no libsodium (as the rest of such a build); `okay::crypto` itself is unchanged and still off by default (rule 18).
  Same keystream (RFC 8439, block counter 1), so old and new launchers still understand each other: a stand-alone
  run compared libsodium 1.0.18 through `StreamXor` with `okay::crypto::ChaCha20Xor` (lengths 0 to 1000, counters
  0 / 1 / 7, and the counter-wrap refusal): identical. The IMA step and index tables are the WAV reader's
  (`okay::wav::ImaAdpcmSteps` / `ImaAdpcmIndexAdjust`), so A10's IMA row is covered by its existing source note.
  `tests/test_voice_races.cpp` checks the shared table, an ADPCM round trip and the cipher. Guard: `check`.
  Not changed: the voice cipher has no authentication tag (ChaCha20 alone); worth a separate look.
- **A8 fixed (wording).** The empty address bar says "Search the web or type an address" (was "Search Google or
  type an address", close to Chrome's own words). Google stays the default search, as the owner chose; the
  "Use Google" button in the browser settings stays (it names the service it picks). Guard: `check`.
- **A9 fixed (UI text).** "Minecraft-style" and "Minecraft mining" are gone from editor tooltips, the Inventory UI
  header and two property tooltips (plain words instead: "hotbar + backpack", "block-game style", "a modest dip").
  The tone-map list shows "Punchy filmic" (the Narkowicz ACES fit) and "Soft filmic" (Hable's curve) instead of
  "ACES" / "Filmic": "Filmic (ACES fit)" is not the published name of that fit (Narkowicz calls it "ACES Filmic Tone
  Mapping Curve"), so plain words were used. Saved values stay 0 / 1 / 2. Kept: code comments that say what a
  feature resembles, and the old-menu record in `editor/UIMenuModel.hpp` (never shown). Guard: `check`.
- **A10 fixed.** One source line at each place: the Narkowicz ACES fit and Hable's curve (GL, D3D, CPU), Jimenez's
  interleaved gradient noise (GL, D3D), and the 9-tap gaussian in 5 linear taps, now derived in `SsaoMath.hpp` from the
  binomial row n = 12 and checked against it by `tests/test_post_quality_math.cpp`; the GL / D3D / DoF / bloom blurs
  point there. Guard: `check` refuses one of those constants with no source note within three lines. (The IMA ADPCM
  table is handled with N1.)

Counts: **1 clear copy of data (A1)**, **2 probable ports (A2, A3)**, **1 own-licence error (A4)**, **3 third-party
runtime / notice gaps (A5-A7)**, **2 trademark groups (A8, A9)**, **1 data-note group (A10)**, **1 asset-record group
(A11)**, **1 weak naming group (A12)**, **3 patent groups (P1-P3)**, **1 non-IP note (N1)**.

### State of the 2026-11-14 findings

F1 (LWO UVs): done, reimplemented. F2 (TweetNaCl): closed, deleted. F3 (Freeverb): closed, rewritten. F4
(`engine/src/Crypto/Fe25519_16.hpp`): **still open**, unchanged. F5: moot (no vendored code is left: `git ls-files`
has no `third_party`, `vendor` or `external` folder). F6 (notices): **still open**, partly better (A7). F7 (behaviour
matched to Assimp): stands; Assimp is no longer built. F9 (`tests/wycheproof/*.json`, Apache-2.0 test data): still in
the repository, owner question 6 still open. F11 (inflate): stands.

### Data tables with a source note (checked, fine)

MP3 tables (ISO/IEC 11172-3, `Mp3Tables.hpp`, `Mp3Decoder.cpp`), JPEG (ITU T.81), SHA-2, BLAKE2b, Ed25519 (FIPS / RFC,
constants derived or cited), Unicode bidi class L ranges and Arabic joining forms (`engine/src/Core/Localization.cpp:1247`,
"worked out from the Unicode Character Database 14.0"; whether the Unicode licence asks for a notice for derived
tables is unverified), Machado et al. 2009 colour-vision matrices (`engine/src/Graphics/ColorVision.cpp:18`), the
evdev-to-HID key table (`engine/src/Platform/OwnLinuxX11.cpp:172`), keyboard layouts (`platform/xbox_uwp/XboxKeyLayout.hpp`,
layouts are facts), CSS named colours (CSS Color 4, `engine/src/Browser/Css.cpp`), the HTML named-reference subset
(`engine/src/Browser/Html.cpp:28`), the 5^q table (`engine/src/Render/Pow5Table.inc`, generated by
`scripts/gen_pow5_table.py` from exact integers; the idea is Lemire 2021, cited), the built-in 8x8 font
(`engine/src/Graphics/Font.cpp`, "hand-drawn"; a few glyphs compared from memory with the public-domain font8x8 set:
different), Mersenne Twister parameters (`tools/okaycc/include/random:55`, ISO C++), CRC polynomials, and the hash
mixing primes of xxHash / MurmurHash / splitmix used as constants in `Noise.hpp`, `WindField.hpp`, `VoxelEdit.cpp`,
`Terrain.cpp`, `Destructible.hpp:250`, `launcher/main.cpp:4404` (published constants of public algorithms; a note
would help, nothing to rewrite). `tests/ttf_refs.inc` holds FreeType's rendering of DejaVu Sans (program output,
stated in `tests/test_ttf_rasterizer.cpp:5`).

### Checked and clean (no sign of copied code in this scan)

- **JavaScript engine** (`engine/src/JavaScript/`, 13,334 lines): every file names its ECMA-262 sections; no QuickJS,
  Duktape, JerryScript or V8 identifiers (`JS_ATOM`, `JSContext`, `JSValue`, `duk_`...).
- **Lua** (`engine/src/Lua/`, 8,125 lines): written from the Lua 5.4 manual; no PUC-Rio identifiers (`luaV_`, `luaH_`,
  `TValue`, `lua_State`, `OP_` names); `%.14g` number printing is Lua's documented behaviour.
- **Browser** (`engine/src/Browser/`, 8,978 lines): WHATWG HTML / DOM / URL and CSS specs cited per file; own user agent
  "OkaySpace Browser"; `-webkit-` names are CSS property names pages use, not WebKit code.
- **RHI** (`engine/src/Render/RHI/`), **physics** (`engine/src/Physics/Core/`, Catto's papers cited; Box2D-like default
  iteration counts 8 / 3 are numbers, not code), **okay-build**, **okay-cc** runtime (`tools/okaycc/lib/`, Linux system
  calls and ISO C), **okay-ms** runtime, **Xbox UWP** (own APPX writer from Microsoft's documentation and ECMA-376; no
  Visual Studio template names such as `DeviceResources` / `StepTimer`), **okay.js** (`web/okayjs/`), the web shell,
  **Supabase functions** (no imported libraries), the **Vorbis, FLAC and WAV** decoders (no stb / dr_libs names), the
  **TrueType** rasteriser (no `stbtt` names), the **image decoders** (no `stbi` / libjpeg names or constants), **OkayUI**
  (no Dear ImGui internals such as `IM_NORMALIZE2F`, `FringeScale`, `stb_textedit`).
- No other project's copyright line in our code (only the JetBrains Mono font header). No .NET runtime or hostfxr
  code on main yet. No video or AAC codec, no Brotli dictionary, no public suffix list, no SDL controller database.
- Brand names used to describe a device or a service (DualShock, DualSense, Joy-Con, Xbox, Steam, PlayFab, Mixamo in
  import hints, Blender in import notes) are text only; no logo or controller button art was found (PlayStation
  buttons are the words "Cross", "Circle", ... in `engine/src/Input/GamepadInfo.cpp:225`).

### Method (2026-11-26)

1. `scripts/provenance_scan.py` was extended (one scanner, no second copy): it now reads every code folder
   (`okayscript`, `browser`, `platform`, `web`, `supabase`, `examples`, the extension-less okay-cc / okay-ms headers
   and more file types), knows many more project names (QuickJS, Duktape, WebKit, Lua's C files, LLVM, libc++,
   chibicc, CoreCLR, Jolt, Gustavson, Bourke, Lottes, ...), and has four new modes: `constants` (magic numbers of
   other projects and published shaders), `tables` (number tables of 64+ entries and whether a source note is near),
   `marks` (other brands inside string literals of shipped code) and `assets` (every font, picture, sound and model
   folder and whether `THIRD_PARTY.md` or a generator accounts for it). Results on this commit: 736 marker lines,
   82 constant hits, 45 tables, 423 brand literals (most are "Steam", "Xbox", "Edge" as in mesh edges), 31 asset
   folders (13 unaccounted, A11).
2. Hits were read by eye; every row above was confirmed by reading the code, not only by the match.
3. Headers of every file in the areas added since 11-14 were read for a stated source. `git grep` for other
   projects' identifiers, copyright lines, licence texts, controller-database strings and codec names.
4. Build and packaging files (`cmake/`, `scripts/build-windows-dist.sh`, `player/CMakeLists.txt`, the CI workflow)
   were read for what is linked or bundled into shipped programs.

### Limits (2026-11-26)

- **A text scan cannot prove that nothing was copied.** A port that renames and reorders passes every mode.
- No reference source tree was available and none could be fetched (the proxy refused outside hosts), so the
  `overlap` mode was **not** run against QuickJS, Lua, chibicc, libc++, Box2D, stb, FXAA 3.11, Gustavson's code or
  Bourke's page. A2 and A3 rest on reading our code against the remembered reference, not on a side-by-side diff.
- Licences, patents and trademark status are from memory and marked unverified; no built release archive, web build
  or exported game was opened to see what notices it carries.
- Not read in full: the large hand-written files (`editor/main.cpp`, `launcher/main.cpp`, the renderers, the okay-cc
  parser and checker, the okay-ms compiler, `okayui/src/`), and every shader string. Shaders were only searched for
  known constants.
- Files under `engine/src/Browser/**`, `shared/Browser*`, `okayui/screens/BrowserView.cpp` and `browser/main.cpp` are
  being changed in other worktrees today; A8 describes main at `d18e4027`.

### Questions for the owner (2026-11-26)

1. A1 and A2: approve keeping public-domain data and code (marching-cubes tables, Gustavson's simplex structure), or
   rewrite both? Recommendation: rewrite (both are small and testable).
2. A3: rewrite the FXAA pass and rename it "Edge smoothing"?
3. A4: fix the "MIT License" line in the Windows version information now?
4. A5-A7: one notice file in every release, installer, exported game and web build, covering the runtimes listed?
5. A8: keep Google as the browser's default search, or ship with no default / the person's choice?
6. A11: who made the OkaySpace planet icon, and may we record that?
7. P1-P3: is a professional patent check wanted for the anti-cheat and netcode before selling games commercially?

---

## 2026-11-14 audit (earlier record, kept as written)

Date: 2026-11-14. Branch `claude/provenance-audit`, cut from main at `ff89d98a`. Read-only on source code: nothing
in `engine/`, `shared/`, `editor/` and so on was changed. Only this page and `scripts/provenance_scan.py` were added.

The rule being checked is CLAUDE.md rule 19a: no copyrighted code, write everything ourselves; ideas, algorithms and
file formats are fine, code is not; public-domain code needs the owner's approval first.

**Short answer.** One clear case of a port was found (the LWO projected-UV code, from Assimp, BSD-3). One close port of
public-domain code was found that was already listed (the TweetNaCl port in `shared/AntiCheatCrypto.hpp`; **closed since: deleted, see F2**), and one
possible port of public-domain constants that is **not** listed (the reverb tuning numbers, which match Freeverb). Two
notice problems exist (Roboto has no licence file, and nothing ships the notice files). Everything else the scan
found is a standard table, standard test data, "behaves like Assimp" wording, or a false alarm. A text scan cannot prove
that nothing else was copied; see "Method and limits".

## Summary, most serious first

| # | Severity | Where | Possible source and licence | Action |
|---|---|---|---|---|
| 1 | Clear copy / close port: **reimplemented** (owner decision 2026-11-14) | `engine/src/Render/LwoReader.cpp` `ProjectUvs` | Assimp `ComputeUVMappingProcess.cpp`, BSD 3-clause | Reimplemented as new code; the placement convention is deliberately kept identical to the old output and the seam rule is new (F1) |
| 2 | ~~Close port of public-domain code (listed)~~ **CLOSED: deleted** | was `shared/AntiCheatCrypto.hpp` lines 1 to about 400 (SHA-512, Ed25519) | TweetNaCl, public domain | Done: the owner approved the replacement; the header is now a thin wrapper over `okay::crypto` and libsodium (F2). The replacement is `okay::crypto`, which has not had its outside review yet, and row 4 below still stands |
| 3 | ~~Probable copy of public-domain constants (not listed)~~ **CLOSED: replaced** | was `engine/src/Audio/SpatialFx.cpp` lines 12 to 15 | Freeverb by Jezar, public domain | Done: the owner said nothing may be copied; the whole reverb was rewritten as our own feedback delay network from the papers (F3) |
| 4 | Possible close re-derivation, unverified | `engine/src/Crypto/Fe25519_16.hpp` (16 limbs of 16 bits, fold by 38) | TweetNaCl's field code, public domain | Unverified; part of the outside crypto review (F4) |
| 5 | Vendored third-party code (allowed, listed) | none left: ImGuiColorTextEdit (`editor/vendor/`) was deleted 2026-11-23, replaced by `OkayUI::CodeEdit`; ufbx (`engine/third_party/ufbx/`) was deleted 2026-11-23 | - | Nothing to keep; F5 is moot |
| 6 | Notice or licence gap | Roboto Medium (no licence file); no packaging step copies any notice file | Roboto: Apache 2.0 (per the font's own name table) | Owner must supply the licence; add notices to release archives (F6) |
| 7 | Behaviour copied, code not (judgement call) | Own model readers, comments such as "as Assimp does", "imports like Assimp" | Assimp, BSD 3-clause | Rule 19a does not forbid matching behaviour; ask the owner to confirm (F7) |
| 8 | Standard table / standard algorithm | MP3 Huffman tables and window, SHA-2 constants, CRC-32, inflate tables, JPEG tables, Ed25519 constants | ISO, FIPS, RFC, ITU specs | Keep; say so in the file headers (F8) |
| 9 | Test data from elsewhere (licensed, notice present) | `tests/wycheproof/*.json` | Project Wycheproof, Apache 2.0, `LICENSE` and `README.md` present | Keep; owner to confirm that test data counts as allowed (F9) |
| 11 | **Rewritten 2026-11-14** (was: close following of a public reference decoder) | `engine/include/okay/Platform/OwnHttpInflate.hpp` | The earlier version (#850) was flagged by an outside review as closely following Mark Adler's puff.c (same count/symbol table builder, same bit-by-bit decode loop, near-identical extra-bit table names, same incomplete-code rule and an identical error text). Replaced by a different design written from RFC 1950, 1951 and 1952 only (table-driven canonical decoder, derived base and extra-bit tables, 64-bit bit buffer); no decoder source consulted | Done; the rewrite is covered by `tests/test_own_http_inflate.cpp` (differential against Python zlib) and still wants an outside look. **Since the one-inflate change the decoder lives in `engine/include/okay/Core/Inflate.hpp`** (the code moved there as it was, with options; `OwnHttpInflate.hpp` is a thin wrapper) and the two other copies (`ImageDecode.cpp`, `FbxReader.cpp`) were deleted, so this row now covers the only inflate in our own code. **What is left of the old copies:** nothing in the tree. They survive only in git history (they were in `main` before this change; one commit of this branch, `0ea3b26a`, also had them verbatim as `tests/inflate_reference_old.hpp`, used once to show that old and new agree on 257,014 inputs, then removed). The `ImageDecode.cpp` and `FbxReader.cpp` copies used the count / first-code / offset table shape (`left`, `offs`, `code - count < first`) that the earlier outside review compared to puff.c; where they came from was never recorded in this audit, so treat their origin as unknown, not as cleared. Deleting them from the tree closes that for the code we ship. `tests/test_inflate_differential.cpp` now compares the engine inflate with `tests/inflate_reference_slow.hpp`, a bit-at-a-time reference written fresh from RFC 1950, 1951 and 1952 for the test (no other decoder consulted) |
| 12 | Written with no importer source read (checked by the author, not by a scan) | `engine/src/Render/MdcReader.cpp`, `HmpReader.cpp`, `CsmReader.cpp` (batch E) | The formats: my own memory of the MDC layout (no struct names or code used), and the sample files `spider.mdc`, `terrain.hmp`, `terrain_withtexture.hmp`, `ThomasFechten.csm` read as data | None needed; see F12 |
| 12b | Clean entry (no finding) | `engine/src/Render/PmxReader.cpp`, `engine/src/Render/LwsReader.cpp` | Written from the PMX and LightWave scene format descriptions; no importer source read | None; see F12b |
| 12c | Clean entry (no finding), but the evidence is weak for OpenGEX and MDL (2026-11-22, brought onto main on branch `claude2/own-readers-last`) | `engine/src/Render/Gltf1Reader.cpp`, `OpenDdl.hpp`, `OgexReader.cpp`, `MdlReader.cpp`, `Q3BspReader.cpp`, `AffineInverse.hpp`, their tests and fixtures | glTF 1.0: the Khronos glTF 1.0 specification and the `KHR_binary_glTF` / `KHR_materials_common` extension texts. OpenDDL / OpenGEX, Quake 1 / Half-Life 1 MDL and Quake III BSP: the public format descriptions **recalled from memory** by the authoring agent (nothing consulted while writing; the OpenGEX site could not be reached on 2026-11-22 to check), then checked on the real files of Assimp's test folder and against Assimp's output used as a black box | No importer source (Assimp's or any other) and no web snippet was read, copied or ported (stated by the writers on `claude/readers-e4` and `claude/own-readers-6`; not checked by a scan). The SIB reader of that branch, worked out from one sample file with no public specification, was not brought over |
| 12e | Clean entry (no finding), 2026-11-23 (branch `claude/third-party-3b`) | `engine/src/Render/MdlReader.cpp` (the 3D GameStudio part), `GameStudioSkin.hpp`, `HmpReader.cpp` (HMP4 / HMP5), `SibReader.cpp`, `tests/gs_fixtures.hpp`, `tests/sib_fixtures.hpp` and their tests | MDL3 / MDL4 / MDL5 and HMP5: Conitec's published descriptions (the 2001 "3D Gamestudio Programmer's Manual", conitec.net/gstech.pdf, and the "Gamestudio file formats" page, conitec.net/beta/prog_mdlhmp.htm), read as prose and struct tables. MDL7 (not publicly documented; the Conitec SDK was not used) and SIB (no description exists): our own hex dumps and parsing scripts of the real sample files (three MDL7 files; `heffalump.sib`, public domain), taken from Assimp's `test/models*` data folders with a sparse checkout that had no code | No importer source, SDK or header of any project was read, copied or ported during this work; Assimp was not run. **Correction 2026-11-24:** some MDL7 header field names of the first version (bone count, entity and editor lumps, colour / frame-vertex / bone-transform record sizes) were not in the sample files and came from memory, most likely of the SDK header or another importer; they were removed and the reader now uses only what the files show (`docs/content/model_readers.md`, MDL7, "Where the names came from"). The sample files are not in the repository (read through `OKAY_MODEL_CORPUS`) |
| 12d | Sample files typed or generated by us; no outside file (no licence needed) | `examples/own_readers/box_v1.gltf`, `box_v1.glb` (`scripts/gen_gltf1_samples.py`), `cube_spin.ogex` (typed), `quake_pyramid.mdl`, `halflife_tri.mdl` (the fixtures of `tests/e4_fixtures.hpp` written out; `own_e4_samples` checks the bytes), `tiny_arena.bsp`, `tiny_arena.pk3` (`scripts/gen_q3bsp_samples.py`) | Numbers typed in our scripts and fixtures | The real corpus files (Assimp's test folder) are only named in `tests/data/model_corpus_manifest.tsv`, never copied or shipped |
| 10 | False alarm | "mirrors Box2D / PhysX", ImGui start-up calls, `0x9E3779B97F4A7C15`, `copy of`, "Blender-style" | none | None |
| 12 | **New code, 2026-11-15 (what was and was not read)** | `engine/src/Audio/WavDecoder.cpp`, `engine/include/okay/Audio/WavDecoder.hpp` | Own WAV decoder. Written from the public format descriptions only (Microsoft RIFF / WAVE format registration notes, ITU-T G.711, the Microsoft ADPCM and IMA ADPCM block descriptions). **Not read:** SDL, libsndfile, dr_wav, ffmpeg or any other decoder source; nothing copied or ported. The IMA step table (89 entries), the IMA index adjust table, the Microsoft adaptation table (16 entries) and the seven standard Microsoft coefficient pairs are standard published constants typed from memory; the IMA ones were checked at test time against Python's `audioop` output (not its source), the Microsoft ones by hand | Done; needs the same "constants are standard" note as row 8 |

Counts: **1 clear copy or close port (not approved)**, **1 close port of public-domain code (listed, approval pending)**,
**1 probable public-domain constants (not listed)**, **1 unverified**, **2 vendored libraries (listed)**, **2 notice
gaps**, **1 judgement call**, **3 standard-data or test-data groups**, **1 group of false alarms**.

## Known third-party pieces (step 1)

All of these are in `docs/status/THIRD_PARTY.md`. Notice files were checked by looking in the repository.

| Piece | Where | Licence status | Notice file present? |
|---|---|---|---|
| SDL2 | system package, `SDL2.dll` bundled in Windows builds | zlib | No file in the repository (not vendored). The Windows distribution ships the DLL; whether it ships `COPYING.txt` is **unverified** |
| Dear ImGui | downloaded at configure time (`editor/CMakeLists.txt`, `launcher/CMakeLists.txt`, `tests/CMakeLists.txt`) | MIT | Not vendored. A binary that contains ImGui needs its MIT notice; none is shipped by any script (F6) |
| ufbx | deleted 2026-11-23 (was `engine/third_party/ufbx/`) | MIT or public domain, both offered | No longer in the repository |
| Assimp 5.4.3 | deleted 2026-11-24 (was downloaded at configure time, `engine/CMakeLists.txt`, optional) | BSD 3-clause | No longer built or linked; its `test/` folder is still fetched by CI as corpus data only (not shipped) |
| libsodium | system package, optional | ISC | Not vendored |
| TweetNaCl port | was `shared/AntiCheatCrypto.hpp` | Public domain | Deleted (F2, closed) |
| stb `stb_decompress` | removed from the engine; the old base85 arrays remain in `shared/RobotoFont.h` and `shared/JetBrainsMonoFont.h` | Public domain | n/a. The arrays are font data compressed with that format; the decoder is no longer used by the engine |
| DejaVu Sans | `assets/fonts/DejaVuSans.ttf` | Free font licence | Yes: `assets/fonts/DejaVuSans-LICENSE.txt` (a Debian copyright file) |
| JetBrains Mono | embedded in `shared/JetBrainsMonoFont.h`, `shared/JetBrainsMonoFontOwn.h` | SIL OFL 1.1 | Yes: `assets/fonts/JetBrainsMono-OFL.txt` |
| Roboto Medium | embedded in `shared/RobotoFont.h`, `shared/RobotoFontOwn.h` | Apache 2.0 according to the font's own name table; **not confirmed** | **No** |
| Starter textures | `assets/textures/*.png` | Ours: made by `tools/gen_textures.cpp` (procedural, fixed seeds) | n/a |

Earlier third-party code that git history shows was already removed: stb_vorbis (#644), stb_image_write (#639),
tinyfiledialogs (#640), stb_decompress (engine side).

## Findings in detail

### F1. LWO planar, cylindrical and spherical UV projection (clear copy / close port)

**Reimplemented (2026-11-14, branch `claude2/lwo-uv-rewrite`); owner decision: keeping the old placement convention with the new seam rule is acceptable.** This is described honestly as a reimplementation, not as code derived only from geometry.
- **What is new code.** `ProjectUvs` was deleted and written again in a new structure. Neither the old function body nor Assimp's code was read for the rewrite; only the old function's first 14 lines were seen once by accident (the bounding-box idea).
- **What is deliberately the same.** The placement convention: the projection on the surface's bounding box, the pair of axes per main axis, and where the turn starts and which way it runs on each axis. These were fitted to the old reader's output so that textures stay where they were. A point on the axis (a sphere's pole) gets the turn's starting value and a flat box gives 0, both as before.
- **What is new behaviour.** The seam rule. A triangle crosses the seam only when the gap between its U values that runs through the seam is not the largest. Then the corners on one side of the largest gap move onto the seam, whichever side moves less. There are no thresholds or flags.
- **Measured against the old reader** on the corpus's projected-map files (the 15 `earth_*` cylindrical, spherical and planar files, `UglyVertexColors.lwo` and `bluewithcylindrictexz.lwo`): planar files are identical. On the cylindrical and spherical files all positions keep a UV they had before, and between 2 and 8 seam positions per file get a different set of UVs.
- **Tests.** `lwo_reader` has hand-computed UVs for a cylinder band (exact U at the four compass points and a seam split), a sphere with poles and a flat planar quad. A mutation check confirmed that changing the turn start, switching off the seam repair, forcing poles to 0 or flat boxes to 0.5 each makes the test fail.
- **Open owner question:** whether matching the old convention is acceptable under rule 19a. Answered for now: yes.

What follows is the original finding.


- **File and lines:** `engine/src/Render/LwoReader.cpp`, `ProjectUvs`, about lines 70 to 134; the call site near 495 to 505.
- **Evidence from our own text:** line 71, "This is Assimp's ComputeUVMappingProcess"; line 83, "the two axes after `ax` in Assimp's formulas"; line 498, "the same mapping as Assimp's GenUVCoords step"; `docs/content/model_readers.md` line 23 says the same. The file's first lines say "no code from any other importer", which is therefore wrong for this function.
- **Evidence from comparing with the source:** a copy of Assimp 5.4.3 was already on this machine (a scratch build from an earlier session) and was read, not copied. The structure matches in ways that a re-derivation from the idea would not give:
  the seam repair has the same flags (zero, one, round-to-zero, a "small" and a "large" corner), the same 0.1 / 0.9 thresholds and the same 0.01 / 0.99 epsilons, and the same three-way branch when both a zero and a one corner exist; the angle uses the same `atan2` argument pairs per axis; the spherical case uses the same `asin` form. The plain cylinder and sphere maths is a standard formula, but the seam-repair block and the flag logic are Assimp's, restructured and renamed.
- **Possible source licence:** BSD 3-clause (Assimp). It requires the notice to be kept when the code is redistributed.
- **Severity:** close port (verified by comparison, not just by the comment).
- **Action:** rewrite from scratch, from the idea only (project the vertex onto the map's axis, compute an angle around the axis, handle the wrap at U = 0 and U = 1 in our own way, for example by deciding per triangle from the shortest way round the circle instead of threshold flags). Then re-run the LWO corpus comparison. Correct the header comment and `docs/content/model_readers.md`. Note that the old threshold behaviour may differ from the new in rare files; that is acceptable and should be recorded.

### F2. TweetNaCl port in `shared/AntiCheatCrypto.hpp` (close port, public domain, listed) - CLOSED

- **Status (closed):** the owner told us to replace it and it is gone. The `gf[16]` field code, the TweetNaCl-named helpers and constants, the group-order array and the second SHA-512 are deleted from `shared/AntiCheatCrypto.hpp`, which is now a thin wrapper (libsodium for Ed25519 where built, otherwise `okay::crypto`; SHA-512 always `okay::crypto`). Before deleting it, a differential run (24,000 random comparisons) and a recorded transcript test (`tests/test_anticheat_golden.cpp`) showed `okay::crypto` answers byte for byte like the old port. Caveats that remain: `okay::crypto` is unreviewed (rule 18), and F4 (`Fe25519_16.hpp`, the portable 16-bit field inside `okay::crypto`) is still unverified. The text below is the original finding, kept as the record.

- **Evidence:** the header comment (lines 6 to 8) says "a C++ port of TweetNaCl's public-domain ed25519 (Bernstein et al.)". The code uses TweetNaCl's field representation (`typedef std::int64_t gf[16]` at line 189), TweetNaCl's helper names (`Carry`, `Sel`, `Pack`, `Unpack`, `Inv`, `Pow2523`, `ScalarMult`, `ScalarBase`, `UnpackNeg`), its curve constants named `D`, `D2`, `X`, `Y`, `I` and its group-order array `l`. It also holds the SHA-512 round-constant table (`K[80]`), which is standard FIPS 180-4 data.
- **Licence:** public domain. Rule 19a says public-domain code needs the owner's approval; `docs/status/THIRD_PARTY.md` lists it as allowed only until `okay::crypto` is reviewed. I did not find a recorded approval; I could not verify that one was given elsewhere.
- **Severity:** close port.
- **Action:** ask the owner (question 2). The planned path already exists: `okay::crypto` (`engine/src/Crypto/`) is written from RFC 8032, RFC 7748, FIPS 180-4 and RFC 8439 and its headers say so. Rule 18 says crypto needs an outside review before it replaces anything. Until then, keep the port and keep it listed.

### F3. Reverb delay lengths (probable copy of public-domain constants, not listed)

**Closed (2026-11-14, branch `claude2/reverb-own-tuning`).** The owner answered question 3: nothing copied, public domain included. Looking at the whole class, not only the numbers, more than the constants followed Freeverb (the room-to-feedback mapping `room * 0.28 + 0.7`, the stereo `wet1` / `wet2` width formula, the damped comb and the `-input + buffered` all-pass). So the reverb was not re-tuned but replaced: `RoomReverb` is a feedback delay network written from Stautner and Puckette (1982), Jot and Chaigne (1991) and Schroeder (1962), with every length, gain and mapping our own and checked by tests (`spatial_audio`). See `docs/audio/audio.md`, "Reverb zones". What follows is the original finding.


- **File and lines:** `engine/src/Audio/SpatialFx.cpp` lines 12 to 15: `kCombTuning`, `kAllpassTuning`, `kSpread` (23), `kInputGain` (0.015), `kAllpassFeedback` (0.5).
- **Evidence:** the eight comb lengths (1116 to 1617), four all-pass lengths (556 to 225), the stereo spread of 23, the input gain 0.015 and the all-pass feedback 0.5 are, from memory, the published constants of Freeverb by Jezar at Dreampoint (public domain). I could not check them against the original here (no copy on this machine, none fetched), so the match is **unverified**. The class is called `SchroederReverb`, which is the name of the general technique, and its `Comb` and `Allpass` functions are the textbook structure. No file says where the numbers came from. `docs/audio/audio.md` also mentions the reverb.
- **Severity:** probable copy of data; the code around it is the standard comb and all-pass network and was not compared.
- **Action:** ask the owner (question 3). If the answer is "not allowed", choose our own delay lengths (mutually prime, scaled to 44.1 kHz) and our own gains, and listen-test them.

### F4. `Fe25519_16.hpp` (possible close re-derivation, unverified)

- **File:** `engine/src/Crypto/Fe25519_16.hpp`, 149 lines.
- **Evidence:** sixteen 16-bit limbs in `int64_t`, carry that folds the top limb back times 38, and a 31-entry product array folded with `38 * t[i + 16]`. This is the same representation and the same shape as TweetNaCl's field code. The comment does not claim a source. The representation is a well-known technique, and the code is written with its own types and names (`Fe`, `Carry`, `Mul`, `MulSmall`, `Freeze`).
- **Severity:** unverified; could be an independent write-up of a standard technique, could be a close port. Not enough to say.
- **Action:** include in the outside crypto review package (`docs/platform/crypto_review_package.md`) with the question "was this written from the field arithmetic, or from TweetNaCl". If the owner wants certainty, rewrite on a different representation (the 51-bit version `Fe25519_64.hpp` already exists, so the portable fallback could use, for example, ten 25.5-bit limbs).

### F5. Vendored libraries (allowed, listed)

- ImGuiColorTextEdit (`editor/vendor/TextEditor.*`) was deleted on 2026-11-23; the script editor uses our own `OkayUI::CodeEdit`.
- (deleted 2026-11-23) `engine/third_party/ufbx/ufbx.c` and `.h` (MIT or public domain; the file offered a choice of two licences). To stay clear of rule 19a's "public-domain needs approval", the MIT option is the one to choose; the owner should record that (question 5).
- Both are gone. The own FBX reader (`FbxReader.cpp`, `FbxScene.cpp`) says "no third-party code". An overlap run of our sources against `ufbx` found no matching runs of 3 or more lines.

### F6. Notice gaps

- **Roboto Medium** has no licence file in the repository. `docs/status/THIRD_PARTY.md` already says so and lists the leads.
- **No script or workflow copies any notice file** into release archives or the installer. A search of `scripts/`, `.github/workflows/`, `dist/` and the CMake files for `OFL`, `LICENSE` and the notice file names found only documentation. The OFL (JetBrains Mono), Apache 2.0 (Roboto), the DejaVu licence, MIT (Dear ImGui, TextEditor, ufbx) and BSD (Assimp) all need their notice to travel with a binary or a font that contains them. I did not inspect a built release archive, so "not shipped" is **unverified**; "no step in the repository adds it" is what was found.
- **Action:** add one `THIRD_PARTY_NOTICES.txt` to every release archive (the text of each licence above), and add `assets/fonts/Roboto-LICENSE.txt` once the owner confirms the source. This is a docs and packaging task, not a source rewrite.

### F7. Behaviour matched to Assimp (judgement call)

The own readers were checked against Assimp on a real-file corpus (`docs/content/model_corpus.md`). Comments and tests say things like "imports like Assimp", "as Assimp reads it", "what Assimp's GenSmoothNormals does", "as Assimp's pre-transform step does". Counts of comments that name Assimp: `ModelImport.cpp` 35, `ModelImportAssimp.cpp` 16 (the backend itself, expected), `LwoReader.cpp` 5, `ModelReaders.cpp` 4, `ColladaReader.cpp` 1, `Gltf.hpp` 2, `DxfReader.hpp` 1.

- **Corpus:** the Assimp test files are not copied into the repository. `tests/data/model_corpus_manifest.tsv` lists paths and the counts our readers gave. No Assimp expected-result file is copied.
- **Code overlap:** a line-by-line comparison of all our own sources with Assimp's `code/` and `contrib/` found only one hit, three generic struct-member lines in `FbxReader.hpp` (a false alarm). See "Method and limits" for what that does and does not show.
- **Reading of rule 19a:** matching what another program outputs is behaviour, not code. It is allowed by the rule as written. The risk is that behaviour matched by reading the other source (as F1) turns into code. Recommend: keep behaviour matching, but compare against Assimp only by running it, never by reading its source (question 4).

### F8. Standard tables and algorithms (keep, say so)

These are numeric data or algorithms fixed by a specification. Typing the same numbers is unavoidable; copying them from a library's source file is not necessary. Where a file already says where its data comes from, that is noted.

| Data | Where | Source of truth | Note |
|---|---|---|---|
| MP3 Huffman tables 1 to 3, 5 to 13, 15, 16, 24, count1 table, 512-entry synthesis window | `engine/src/Audio/Mp3Tables.hpp` | ISO/IEC 11172-3 Table B.7 and the synthesis window | The header cites the standard. The layout (separate length and code arrays per table) and the window "first 257 of 512 in units of 1/65536" is our own choice, as far as I can see. I could not compare with minimp3, stb or other decoders (none on this machine), so a match with a library's table file is **unverified**. The numbers themselves are standard |
| MP3, Vorbis, FLAC decoders | `Mp3Decoder.cpp`, `VorbisDecoder.cpp`, `FlacDecoder.cpp` | ISO 11172-3 and 13818-3; Vorbis I spec; FLAC format spec | Headers say "written from the spec". Not compared line by line with any library |
| SHA-256, SHA-512 constants | `engine/include/okay/Crypto/Sha256.hpp`, `engine/src/Crypto/Sha512.cpp`, `AntiCheatCrypto.hpp` | FIPS 180-4 | Standard; own comments explain how they are defined |
| Ed25519 constants | `engine/src/Crypto/Ed25519.cpp` | RFC 8032 | The file says d, sqrt(-1) and the base point "are derived from their definitions on first use"; nothing is pasted in. Good practice |
| CRC-32 | `ZipLite.hpp`, `ImageWrite.cpp`, `OkfDecode.hpp` | zlib and PNG specs | One-line generator loop, not a pasted table |
| Inflate length and distance tables | `engine/include/okay/Core/Inflate.hpp` (the one inflate; the copies in `FbxReader.cpp` and `ImageDecode.cpp` were removed) | RFC 1951 | Standard |
| JPEG quantisation and Huffman tables | `ImageDecodeJpeg.cpp` | ITU T.81 Annex K | Standard (tables not compared with any library). The header says "libjpeg-style fancy upsampling", which is a named algorithm (triangle filter), not code |
| Mix constant `0x9E3779B97F4A7C15`, PCG and splitmix constants | many files | public algorithms | Standard constants |
| Perlin or SH constants | searched, none found as pasted tables | n/a | n/a |

### F9. Test data from other projects

- `tests/wycheproof/*.json`: copied unmodified from Project Wycheproof (Apache 2.0, commit `65ca4947...` per `tests/wycheproof/README.md`), with the repository's `LICENSE` beside them. Test data only; not linked or included in any program. This is the one place where third-party material is stored in the repository besides the vendored libraries. Whether that fits "no test files from other projects" in rule 19a is a question for the owner (question 6). The rule text lists test files, so by a strict reading it does not fit; the alternative is to download them at test time, as the model corpus is.
- `tests/data/` (images, audio, models): the fixture generators (`tests/data/images/gen_fixtures.py`, `scripts/gen_fbx_fixtures.py`) suggest these are generated by us. Not checked file by file; **unverified**.

### F12b. PMX and LWS / MOT readers (batch E5): clean entry, written from format descriptions

`engine/src/Render/PmxReader.cpp` and `engine/src/Render/LwsReader.cpp` (and their tests and `tests/pmxlws_fixtures.hpp`) were written under rule 19a: from the public descriptions of the PMX 2.0 / 2.1 file format and of the LightWave scene file format (the `LWSC` / `LWMO` text with items, `ParentItem` and `Key` envelopes), as the author knows those descriptions, plus reasoning about the structure. **No importer source (Assimp or any other project) was read, copied or ported, no snippet from the web was used, no sample file was read (none is in the tree) and Assimp was not run.** The test files were written by hand from the same structure. Nothing here is a finding; this entry records how the files were made so a later overlap run or review has something to check against. What this cannot show: the descriptions were used as remembered, not re-read, so a wrong detail is possible (and listed as unverified in `docs/content/model_readers.md` and `docs/content/model_readers.md`); a text scan cannot prove that nothing was copied (see "Method and limits").

### F10. False alarms

- "Mirrors Box2D's rest optimisation" and "Mirrors the PhysX rest optimisation" (`Physics2D.cpp` line 318, `Physics3D.cpp` line 507): names a well-known idea (sleep when at rest), not code.
- "Blender-style", "Blender's Auto Smooth" and similar in `Mesh.hpp`: names of editing tools whose behaviour is described; no code.
- ImGui start-up and shutdown calls in `editor/VsGraphShot.cpp` and `launcher/main.cpp`: the documented API sequence for using the library, not copied library code.
- `okayui/` is a Dear ImGui style API ("ImGui-style" auto layout). API shape is an idea. An overlap run against a Dear ImGui source tree found no matching runs in `okayui/`.
- "taken from", "copy of", "derived from" in comments, almost all about program data ("a copy of the prefab"), not about code origin.

### F12. Batch E readers (MDC, HMP, CSM): written from the format description, no importer source read

`MdcReader.cpp`, `HmpReader.cpp` and `CsmReader.cpp` were written to the rule that the formats are fine to implement and another project's code is not. **What was used:** (MDC) the layout of the format from my own memory, then checked and corrected field by field against the one real file `spider.mdc` (the offsets of every table add up to the byte). **The compressed-offset constants (0.05 scale, bias 127, 1/64) were typed from memory of the game's headers, are NOT verified on any real file (frame 0 of `spider.mdc` is not compressed), and the mutation test only shows the code agrees with itself. This is flagged for the owner (`docs/content/model_readers.md`).** The four sample files' licence is unchecked: see `docs/status/THIRD_PARTY.md`; (HMP) only the two sample files `terrain.hmp` and `terrain_withtexture.hmp` and what they share with the Quake / GameStudio MDL header: no written specification was found, so the layout is the author's reading of the data; (CSM) the one sample file `ThomasFechten.csm` and the Character Studio keyword names from memory. The four sample files come from the `test/` directory of an Assimp checkout and were opened **as data files only**; **no Assimp source file, header or comment was opened for these readers, no other project's code was looked at (not on the web either), and Assimp was not run on the samples** (the earlier batches used it as a black box; these did not). The headers of the three files say the same. The pre-existing shared helpers (`BinReader.hpp`, `ReaderCommon.hpp`, `TextReaderUtil.hpp`) are our own. A text scan cannot prove this; it is the author's statement, and a reviewer can check it by reading the three files (166 to 234 lines each) against the sample files.

## Method and limits

1. **Known pieces.** Read `CLAUDE.md` and `docs/status/THIRD_PARTY.md`; looked for each piece and its notice file in the repository.
2. **Marker scan.** `python3 scripts/provenance_scan.py markers` lists comment lines in `engine/ shared/ editor/ launcher/ player/ okayui/ relay/ tools/ scripts/ tests/` (skipping `engine/third_party`, `tests/wycheproof`, `tests/data`) that contain porting words ("ported from", "based on", "adapted from", "taken from", "translated from", "copied from", "à la", "the way X does") or names of other projects and libraries. It printed 527 lines. I read the ones not about Assimp, Dear ImGui, SDL or Blender by eye; the rest are counted above.
3. **Line overlap.** `python3 scripts/provenance_scan.py overlap <reference folders>` compares our own C and C++ files with a local source tree of another project. It ignores whitespace, comments and preprocessor lines and reports runs of consecutive matching lines. It was run against: the Assimp 5.4.3 source (`code/` and `contrib/`), a Dear ImGui source tree, and the vendored ufbx. Results: one false-alarm hit in `FbxReader.hpp`; start-up boilerplate hits in `editor/VsGraphShot.cpp` and `launcher/main.cpp`; nothing for ufbx. It prints file names and line numbers only, never the other code.
4. **History.** `git log -S` and message searches for "port", "ported from", "copied from", "stb", "Assimp", "vendored". They show the removed libraries listed above and the Assimp-comparison work; no message says that code was ported from a named library except the LWO change (#835, "the way Assimp's GenUVCoords step does").
5. **Reading.** The LWO function was compared with the Assimp source by eye (F1). Headers of every own model reader, every decoder, `okay::crypto`, the TTF rasteriser and image decoders were read for stated sources.

**Limits. A text scan cannot prove that nothing was copied.**

- The overlap check only finds code that was copied nearly line for line. A port that renames identifiers, reorders statements or changes the loop shape will pass. F1 would **not** have been found by the overlap run (it did not report `LwoReader.cpp`); it was found by the comment and confirmed by reading. So the overlap result is weak evidence.
- It can only compare with source trees that exist on this machine: Assimp, Dear ImGui, ufbx. **Not compared (no copy available, nothing fetched):** minimp3, stb_vorbis, stb_image, dr_libs, miniaudio, libsodium, TweetNaCl, meshoptimizer, Box2D, Bullet, PhysX, Recast and Detour, Godot, Unreal, Unity, Freeverb, FreeType, libjpeg, zlib or miniz, lodepng. Findings that depend on those (F3, F4, the MP3 tables) are from memory and marked unverified.
- Own style differences (long files by one author versus short ones) were looked at only by sampling headers; I did not read the 2,858 tracked files. Large hand-written files not read in full include `launcher/main.cpp`, `editor/main.cpp`, `OkayScriptVM.cpp`, `Mesh.hpp`, the renderers, the physics code, `TtfRaster.cpp`, `ImageDecode.cpp`, `VorbisDecoder.cpp`, `FlacDecoder.cpp`, `Mp3Decoder.cpp`.
- Comments and documents written by the author may be missing, wrong or untrue ("no code from any other importer" is false for one function).
- Shaders (GLSL and HLSL strings in the renderers) were not compared with any known shader source.
- Data tables inside the Dear ImGui-format font headers are font files, not code.
- Licence statements here are from the repository and from memory; they are not legal advice.

## Questions for the owner

1. **LWO UV projection (F1).** Approve a rewrite from scratch of `ProjectUvs` now? It is the only clear port found and the fix is small (about 70 lines plus doc wording).
2. **TweetNaCl port (F2). Answered and done: the owner approved replacing it.** (Original question: do you approve keeping it, as public-domain code, until `okay::crypto` has passed the outside review? If not, what should the launcher use in the meantime?)
3. **Freeverb numbers (F3).** Are public-domain reverb constants acceptable, or should the delay lengths and gains be re-chosen by us?
4. **Behaviour matching with Assimp (F7).** Confirm that matching Assimp's output (by running it) is acceptable, and that reading Assimp's source to do so is not.
5. **ufbx licence choice (F5).** Moot since ufbx was deleted on 2026-11-23. Record that we take ufbx under the MIT option, not the public-domain option?
6. **Wycheproof test vectors (F9).** Apache-2.0 test data stored in the repository: keep, or download at test time only?
7. **Roboto (F6).** Where did the font come from, and can you add its licence text?
8. **Notices in releases (F6).** Should I add a `THIRD_PARTY_NOTICES.txt` to the release archives and installer (docs and packaging only)?
9. **Order of work.** Should the audit be repeated with other reference trees (minimp3, stb, libsodium, Freeverb, meshoptimizer) fetched into a scratch folder? This needs network access and would make F3, F4 and the MP3 table check verified.

## Recommended order for rewriting

1. `ProjectUvs` in `LwoReader.cpp` (F1): clear port, small, testable against the existing LWO corpus.
2. The reverb constants in `SpatialFx.cpp` (F3), if the owner says not to keep them: a few numbers and a listening test.
3. `Fe25519_16.hpp` (F4): only after the owner's answer and the reviewer's view.
4. The TweetNaCl port (F2): done (deleted on the owner's say-so, replaced by `okay::crypto`; the outside review of `okay::crypto` is still open).
5. Packaging notices (F6): not a rewrite, but it is a licence obligation that exists today.

## Addendum: batch E readers (Q3D, RAW), branch `claude/readers-e2`

`engine/src/Render/Q3dReader.cpp` and `engine/src/Render/RawReader.cpp` were written from the format descriptions only: Paul Bourke's "Quick3D Object File" page (header, chunk types, field lists) and his "POV-Ray RAW triangle format" page (nine numbers per triangle, the colour and texture-name variants, group identifiers). **No importer source was read**: not Assimp's, not any other project's, and no code snippet from the web. Where a description was silent (the Q3D byte order, a few Q3D fields, RAW comments) the choice made is written down in `docs/content/model_readers.md` and `docs/content/model_readers.md`. No sample file was available for either, so no output was compared with another reader. The SIB reader (`docs/content/model_readers.md`) was **not written** because no public description or sample could be found; a reader written from memory of another importer would have broken rule 19a.

## Addendum (batch E3 readers: IQM, COB, NDO)

Date: 2026-11-15, branch `claude/readers-e3`. Checked by hand when written; **not** run through `scripts/provenance_scan.py`.

| Reader | Where each fact came from | Not used |
|---|---|---|
| `IqmReader.cpp` | **My memory** of the IQM specification text (`iqm.txt`): header, record layouts, vertex-array types and formats, the frame-channel rule. The spec was not fetched or re-read during the work. Every layout detail was then checked against a hex dump of the one real file `mrfixit.iqm` (record sizes, offsets, quaternion lengths) | Any importer or engine source |
| `CobReader.cpp` | **From memory**: the chunk names and the ASCII keywords. **Settled by a hex dump / text view of the 8 sample files**: all byte layouts (chunk header, object chunk, face record, `Mat1`, `ShBx` type codes 3, 6, 7, 10, the `file name` parameter, the 48-byte centre-and-axes block before the matrix, that matrices are world matrices). Guesses, not seen in any file: shader type code 4 as an integer, hole meaning of face flags. No citable document exists | Any importer source |
| `NdoReader.cpp` | Nothing but the header text `nendo `. It reads no geometry. A web search for a specification returned an importer-based summary whose passing facts (the `nendo 1.x` header, big endian, a count-width change at 1.2) were **seen** and are disclosed in `docs/content/model_readers.md`; only the header text is used | The importer source itself (never opened) |

**The Assimp test tree was open in the scratchpad while this work was done.** Only its data files (`test/models/COB/*.cob`, `test/models/IQM/mrfixit.iqm`) were copied to a private folder and read; none of its source folders was opened, listed for content or searched. The shared helpers the readers use (`BinReader.hpp`, `ReaderCommon.hpp`, `PolygonTriangulate.hpp`, `TextReaderUtil.hpp`, `SceneRefPath.hpp`) are the project's own earlier files. The test files are hand-built from the structure; no real file is committed.
