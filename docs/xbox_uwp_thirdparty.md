# Xbox UWP build and third-party code: where our own code is used

Owner request 2026-11-18: see if the experimental Xbox UWP build can run on our own replacement code. This page says, item by item, what the
UWP target uses today, which own replacement it has, how far that is wired, and what still blocks it. Everything here is
checked on Linux only (text scanner, syntax sweeps with MinGW, unit tests); **nothing was compiled with Microsoft's compiler or run on
Windows or a console: UNVERIFIED.** It follows CLAUDE.md rules 17, 18 and 19a. The rest of the Xbox story is in [xbox_uwp.md](xbox_uwp.md).

## The table

| Item | What the UWP target uses today | Our own replacement | Status in the UWP build | What blocks it on UWP |
|---|---|---|---|---|
| **SDL2** (window, input, audio, 2D) | Nothing. The UWP files (`UwpPlatform.cpp`, `XboxUwpBackend.hpp`, the shell) use `CoreWindow`, `Windows.Gaming.Input`, WASAPI and the swap chain | `okay::platform` seam with the UWP backend (reports kind "own") | **Done.** `okay-uwp-audit` reports no SDL include or `SDL_` word in the 309 compiled engine sources and their 949-file include closure (the scanner also lists `SDL.h` and `SDL_` as forbidden) | Nothing for SDL. The window, gamepad and audio backends themselves are unverified |
| **Dear ImGui / okayui toolkit** | Nothing. The bridge draws the 2D / UI layer with the software painter and blends it over the 3D frame | `XboxSoftPainter.hpp` (premultiplied RGBA) over the engine's `okay::platform::Painter` seam; D3D11 overlay shader `PSOverlay` | **Done** for the engine's own UI (`okay::ui2d`). The scanner forbids `imgui.h` | The editor and launcher still use ImGui, but they are not in the UWP target. Pixel match with the desktop painter is unverified |
| **Models: Assimp** | Not linked, `OKAY_HAVE_ASSIMP` never defined | Our own readers: OBJ, glTF/GLB, FBX, Collada, 3DS, BVH, STL, PLY, OFF, SMD, MD2/MD3/MD5, MS3D, ASE, Blender, 3MF, AMF, X3D, DXF, LWO/LXO/LWS, B3D, X, AC3D, OGRE, XGL, IRR, NFF, Terragen, Q3D, IQM, COB, MDC, HMP, CSM, PMX, ZAE, XML | **Done.** `engine/uwp_own_readers.cmake` reads the root `CMakeLists.txt` and turns every `OKAY_OWN_<FMT>` option that is ON into `OKAY_OWN_<FMT>_DEFAULT` for `ModelImport.cpp` (38 reader switches today, FBX is forced on separately; none edited by hand). A file our reader refuses fails with a plain log line and `ok=false` | Formats we have no reader for (`.ndo` is a seam that refuses every file; anything else Assimp read) cannot be loaded. Real-world files our readers reject (see `docs/model_corpus.md`) cannot fall back to Assimp |
| **Models: ufbx** | Not compiled: `third_party/ufbx/ufbx.c` is in `engine/uwp_exclude.txt`; the `ufbx.h` include and every ufbx call in `ModelImport.cpp` are compiled out under `OKAY_XBOX_UWP`; `okay::fbx::OwnReaderEnabled()` is always true there (the `OKAY_OWN_FBX=0` switch cannot select ufbx) | `okay::fbx` (`FbxReader.cpp`, `FbxScene.cpp`) | **Done.** If our FBX reader refuses a file the log says "the own FBX reader could not read this file (the Xbox build has no other FBX reader)" | Odd FBX files that only ufbx reads fail here |
| **Audio files** | `AudioClip::LoadWAV` goes through our WAV decoder (`OKAY_OWN_WAV_DEFAULT=1` for the UWP engine; PCM, float, A-law, mu-law, MS and IMA ADPCM). OGG Vorbis, MP3 and FLAC always go through `VorbisDecoder.cpp`, `Mp3Decoder.cpp`, `FlacDecoder.cpp` | `okay::wav`, Vorbis, MP3, FLAC decoders (ours) | **Done in code.** No `SDL_LoadWAV`, `SDL_mixer`, `stb_vorbis` or `dr_*` is reachable (scanner tokens `stb_vorbis.c`, `dr_wav.h`, `dr_mp3.h`). Output is `XboxWasapi.hpp` through the `AudioDevice` seam | Sound on a device is unverified; the own WAV decoder is OFF by default in desktop builds and only on here |
| **Crypto: libsodium** | Not linked. See the next section | `okay::crypto` (`engine/src/Crypto`) | **Packet encryption OFF by default; hashes, random bytes and Ed25519 checks use `okay::crypto`.** Not independently reviewed | Rule 18: own crypto cannot be trusted on our word and is never in a release |
| **HTTP: libcurl, WinHTTP** | No native HTTP client in `NativeHttp.hpp` under `OKAY_XBOX_UWP` (calls say "not reached"). The content-delivery transport is `Windows.Web.Http` (`app/xbox_winhttp.hpp`) | `okay::ownhttp` (HTTP/1.1, our code). Here: `XboxOwnHttp.hpp`, own client over Winsock | **Choice at build time:** `OKAY_XBOX_HTTP=windows-web-http` (default, https works) or `own` (plain `http://` only). Both UNVERIFIED | We have **no TLS of our own** (`okay::ownhttp` uses the system OpenSSL loaded at run time, which does not exist in a UWP app). So `own` refuses every `https://` URL; it is only for a LAN or local test server. A CDN needs `windows-web-http` |
| **Fonts** | Overlay and UI text use our TTF rasteriser (`TtfFont.cpp`, `TtfRaster.cpp`) and the embedded fonts in our OKF1 container (`EmbeddedFont.cpp`, `shared/*FontOwn.h`) plus the built-in bitmap font (`Font.cpp`) | Ours (code). The font files are assets with licences (`docs/THIRD_PARTY.md`) | **Done in code.** `stb_truetype.h` is a forbidden token and is not reachable | The Roboto licence text is still missing in the repository (asset record in `docs/THIRD_PARTY.md`, the owner's call) |
| **Images** | `ImageDecode.cpp`, `ImageDecodeJpeg.cpp`, `Image.cpp` (PNG, JPEG, writer) | Ours | **Done in code.** `stb_image.h` is a forbidden token and is not reachable | Nothing known; decoding on Windows is unverified |
| **Release folder `xbox-src`** | `build-windows-dist.sh` copies `platform/xbox_uwp`, `engine/include`, `engine/src`, `engine/CMakeLists.txt`, `shared/` and now the root `CMakeLists.txt` (the UWP engine reads it for the reader defaults) | | **Done.** `engine/third_party` is no longer shipped (nothing in it is compiled for UWP) and the script stops if it, the root `CMakeLists.txt` or `uwp_own_readers.cmake` is wrong | |

## Crypto, plainly (rule 18)

* **libsodium does not exist in the UWP build.** `NetCrypto.hpp` picks `okay::crypto` only with `OKAY_OWN_CRYPTO`, libsodium only with `OKAY_HAVE_SODIUM`, and neither
  means no packet encryption (`OKAY_NET_CRYPTO` undefined). The UWP target defines neither by default, so `sodium.h` is never included.
* What `okay_crypto_uwp` still contains and uses on UWP: SHA-256 and SHA-512, ChaCha20, X25519, Ed25519, AEAD and the random-bytes function (`BCryptGenRandom`
  with the system RNG, which UWP allows). They are always our own code: file checksums and update-signature checks call them directly
  (`shared/AntiCheatCrypto.hpp`, `shared/UpdateSignature.hpp`). **This is own crypto, not independently reviewed.**
* **The explicit choice:** `OKAY_XBOX_OWN_CRYPTO` (CMake option of `platform/xbox_uwp/engine`, default **OFF**; PowerShell `-OwnCrypto`). ON defines
  `OKAY_OWN_CRYPTO`, so packet encryption works through `okay::crypto`. CMake then prints a warning and the PowerShell script prints and logs
  "own crypto, not independently reviewed". It is for experiments only: do not ship.
* **Visible:** the export `README_XBOX.txt` says "own crypto, not independently reviewed; for experiments only; do not ship", names the switch and says libsodium is absent.
* **A release can never carry it.** `scripts/check_release_own_stack.sh` (run by `build-windows-dist.sh`, `release.yml`, `dev-build.yml`) now refuses
  `OKAY_XBOX_OWN_CRYPTO` set to anything but OFF in the environment, in a `-D` argument (also `-D NAME=ON`) or in a CMake cache, exactly like `OKAY_OWN_CRYPTO`.
  Its self-test (`ctest -R release_refuses_own_crypto`) has cases for each of the three. An own-crypto compile definition in a release tree was already refused.

## HTTP choice

| `OKAY_XBOX_HTTP` | Client | https | Where |
|---|---|---|---|
| `windows-web-http` (default) | `Windows.Web.Http` through `app/xbox_winhttp.hpp` | yes | the expected path |
| `own` | `okay::ownhttp` over a Winsock TCP stream (`ws2_32` is linked; `internetClient` is in the manifest) | **refused** with "this build has no TLS of its own" (never sent in the clear) | `XboxOwnHttp.hpp` (`PlainConnector`, `OwnHttpClient`, `WinsockDialer`) |

The pure parts (the choice, the connector, the `http::Client` adapter, the Range header, redirects, an https refusal) are tested on Linux with an in-memory TCP
in `tests/test_xbox_uwp_runtime.cpp` (`OwnHttpTests`). The Winsock dialer compiles only under `_WIN32` and has **never been compiled with Microsoft's compiler or run**.
Written from RFC 9112, the Microsoft Winsock pages and our own `okay::ownhttp`; no outside code.

## How this was checked, and how to repeat it

* Source scan: `okay-uwp-audit .` (ctest `xbox_uwp_audit`): 313 engine sources, 4 excluded (`GLRenderer`, `D3D12Renderer`, `PhotonTransport`, `ufbx.c`), 309 scanned,
  949 files in the include closure, 0 findings. New forbidden tokens: `sodium.h`, `imgui.h`, `assimp/`, `ufbx.h`, `curl/curl.h`, `openssl/`, `stb_*`, `dr_wav.h`, `dr_mp3.h`.
* Models: ctest `xbox_uwp_models` compiles `ModelImport.cpp` and `FbxScene.cpp` the way the UWP engine does (`OKAY_XBOX_UWP`, the 38 reader defaults, no `ufbx.h`,
  no Assimp) and loads 17 sample files (OBJ, STL, PLY, 3DS, OFF, SMD, MD5, MS3D, ASE, Collada, Blender, FBX binary and ascii, GLB) through them, checks that
  `OKAY_OWN_FBX=0` does not switch FBX away from our reader, and that garbage under six extensions fails cleanly.
* Syntax sweep: the 310 UWP sources (309 engine sources plus `Painter.cpp`) with MinGW `x86_64-w64-mingw32-g++-win32 -fsyntax-only -DWINAPI_FAMILY=2` (a proxy for the UWP headers, not Microsoft's compiler).
* Crypto guard: `scripts/check_release_own_stack.sh --self-test`.

## Written from

`platform/xbox_uwp/XboxOwnHttp.hpp`: RFC 9112 and Microsoft's Winsock documentation. `engine/uwp_own_readers.cmake`, the CMake parts of `engine/CMakeLists.txt`,
`tests/test_xbox_uwp_models.cpp`, the fences in `ModelImport.cpp` and `FbxScene.cpp`, the scanner tokens and the guard cases: our own repository code, from the
repository's own options. No code was copied from another project and no library was added.

## Still not done

* Everything above on a real Windows build and a console (UNVERIFIED).
* Our own TLS (needed before `own` HTTP can talk to a CDN) and an independent review of `okay::crypto` (rule 18, owner decided not to hire one).
* A fallback for model files only Assimp or ufbx read: by design none on UWP; the fix is a better own reader (`docs/own_code_roadmap.md`).
