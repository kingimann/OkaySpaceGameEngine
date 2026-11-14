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
