# Packaging & distribution layout

The Windows distribution is laid out so a user double-clicks one thing at the top
and everything else is tucked away. This is the **canonical layout** — builds
(and the `scripts/build-windows-dist.sh` script) follow it.

```
OkaySpace.exe        ← the launcher: start here (sign in, create/play games)
SDL2.dll             ← required next to OkaySpace.exe
README.txt           ← short "what do I run" note
Tools/
  OkayEngine.exe        the editor (make games)
  OkaySpacePlayer.exe   standalone runtime that plays an exported game
  okayspace-relay.exe   optional multiplayer NAT relay (see relay.md)
  SDL2.dll              copy (see below)
  VERSION.txt
  accounts.md
```

## Linux (x86-64)

Linux is a release target: every release carries a Linux tarball and the three programs the in-app updater downloads. The layout
mirrors the Windows zip (the launcher at the top, the rest in `Tools/`), with plain program names and no DLLs:

```
OkaySpace            <- the launcher: start here
README.txt  VERSION.txt
Assets/              starter textures and fonts, as in the Windows zip
Tools/
  OkayEngine            the editor
  OkaySpacePlayer       standalone runtime (also the dedicated server: --server)
  okayspace-relay       optional multiplayer relay (when it built)
  VERSION.txt  accounts.md
```

`scripts/build-linux-dist.sh` builds it natively (CMake + Ninja, `CMAKE_BUILD_TYPE=Release`, libsodium required: the script stops
rather than ship an unencrypted build) and writes `build-linux-dist/`, the tarball `OkaySpace-linux-x64-v<ver>.tar.gz` (one top folder
of the same name), its `.sha256` file and `build-linux-symbols/OkayEngine-linux-x64.debug` (the editor's debug info, never
downloaded by the launcher). The release workflow's `linux-client` job runs it on **Ubuntu 22.04**, so the programs need glibc 2.35 or
newer. They link SDL2 and libsodium dynamically: the machine needs `libsdl2-2.0-0`, `libsodium23` and `curl` (Debian / Ubuntu names);
`zenity` or `kdialog` are optional (file dialogs). The script prints each program's shared-library needs at the end.

**Release file names.** The updater downloads individual files by name (GitHub release asset, `v<ver>/<name>` in the Supabase bucket, key
in `versions.json`), so each platform has its own non-colliding set (`shared/ReleaseAssets.hpp`):

| | launcher | editor | player |
|---|---|---|---|
| Windows (unchanged) | `OkaySpace.exe` | `OkayEngine.exe` | `OkaySpacePlayer.exe` |
| Linux | `OkaySpace-linux-x64` | `OkayEngine-linux-x64` | `OkaySpacePlayer-linux-x64` |
| installed on disk (Linux) | `OkaySpace` | `Tools/OkayEngine` | `Tools/OkaySpacePlayer` |

macOS has no release files yet: its launcher says there is no macOS build instead of downloading a Windows program.

Checksums: the SHA-256 of the three Linux files go into `versions.json` exactly like the Windows ones (the updater refuses a file without
one). `scripts/publish-supabase.sh` with `PLATFORM=linux` adds them to the version's existing entry, after the Windows publish; see
[releasing.md](releasing.md). The tarball's own checksum is the `.sha256` asset (`sha256sum -c`); the launcher never downloads the
tarball. Linux programs are not code-signed (there is no equivalent of Authenticode here); integrity rests on the checksums, and on the
signed `versions.json` once a signing key exists ([updater.md](updater.md#signed-releases)).

Not covered: AppImage, `.deb`/`.rpm`, ARM64, a desktop entry or icon install, musl. (`okay-server-linux-x64`, the headless server runtime,
is a separate asset and unchanged.)

## Why SDL2.dll appears twice

Windows resolves a load-time-linked DLL from the **directory of the executable
that needs it**. Every OkaySpace `.exe` is dynamically linked against SDL2, so
each one needs `SDL2.dll` in its own folder. That's why one copy sits beside
`OkaySpace.exe` at the top and another sits in `Tools/` beside the editor /
player / relay. It is required, not redundant — **never separate an `.exe` from
an `SDL2.dll`.**

(If you'd rather ship a single DLL, build with `-DOKAY_STATIC_SDL=ON` to link
SDL2 statically; then no `SDL2.dll` ships at all. The default is dynamic linking,
— which keeps the exes small and the DLL swappable.)

## Building it

```bash
export SDL2_MINGW_PREFIX=/path/to/SDL2-<ver>/x86_64-w64-mingw32
./scripts/build-windows-dist.sh        # -> build-win-dist/ in the layout above
```

The script builds the launcher, editor, player, and relay, then arranges them as
shown and writes `README.txt`. Zip `build-win-dist/` for release.

## Code signing (Authenticode)

Unsigned programs make Windows SmartScreen show "unknown publisher" the first time someone runs them. The release
workflow can sign OkaySpace.exe, OkayEngine.exe, OkaySpacePlayer.exe and OkaySpace-Setup.exe (not the third-party
SDL2.dll). It uses `scripts/sign-windows.sh` (osslsigncode, runs on the Linux runner) and signs **before** the zip, the
installer and the checksums are made, so every file a user can download carries the same signed bytes and the checksums in
`versions.json` match them.

**Without a certificate nothing changes:** the step prints a warning and the release stays unsigned, as before. **With one,
any failure fails the release**: it never ships half-signed or silently unsigned.

What you need to do (once):

1. Get a **code-signing certificate** (OV or EV) from a certificate authority, as a `.pfx` / `.p12` file with its private
   key. An EV certificate builds SmartScreen reputation faster. Include the intermediate certificates in the file.
2. Base64-encode it: `base64 -w0 mycert.pfx` (on macOS: `base64 -i mycert.pfx`).
3. In the GitHub repository: Settings > Secrets and variables > Actions > New repository secret. Add:
   - `WINDOWS_SIGN_PFX_BASE64`: the base64 text from step 2;
   - `WINDOWS_SIGN_PFX_PASSWORD`: the certificate's password (the secret may be empty if the file has none).
4. Run the next release. The log of "Sign the Windows programs" lists each file as `signed ...`.

The certificate and password only travel through environment variables and private temp files that are deleted at the end;
they are never printed and never put on a command line. Signatures are timestamped (DigiCert, then Sectigo, then
GlobalSign as fallbacks; `WINDOWS_SIGN_TIMESTAMP_URLS` overrides the list), so they stay valid after the certificate
expires; signing without a timestamp is refused outside of tests. After signing, the script verifies each signature against
the certificate's own chain; a chain problem (a self-signed test certificate, or an intermediate missing from the `.pfx`)
is reported as a warning because Windows may not trust it.

`scripts/release-local.sh` runs the same script when `WINDOWS_SIGN_PFX_BASE64` is set in your environment. The test
`sign_windows` (tests/scripts/test_sign_windows.sh) signs a small program with a throwaway certificate and covers the
refusals; it is skipped when osslsigncode, openssl or MinGW is missing.

Not covered: kernel drivers (the anti-cheat does not use one), macOS notarization, and Azure Trusted Signing or other
cloud signing services (they need a different tool than a `.pfx` file).

## Shipping a single *game* (not the toolset)

When you export a game from the editor (**File → Build Settings…**), that output is its
own self-contained folder — `<YourGame>.exe` (a copy of the player runtime) +
`SDL2.dll` + `<YourGame>_Data/` (your scenes, only the assets they use, StreamingAssets and `build.json`).
See [build_pipeline.md](build_pipeline.md) for the layouts and options. For Steam auto-updates, upload that folder as your depot
(see [steam_release.md](steam_release.md)).
