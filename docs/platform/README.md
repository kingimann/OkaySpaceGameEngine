# platform/: Our own replacements for platform code and tools

> **What this is:** the list of pages in `docs/platform/`, one line each. Our own replacements for platform code and tools: the platform layer (SDL2), build tool, compilers, crypto, HTTP, Xbox and web exports.
>
> **Status:** index, made 2026-11-20 when the docs were sorted into folders. Back to the [start page](../README.md).
>
> **Last checked:** 2026-11-20.

- [Our own crypto (okay::crypto): stage 1, side by side, off by default](crypto.md): **UNREVIEWED. NOT THE DEFAULT.** This code was written from the RFCs by the engine team.
- [okay::crypto: package for an outside security review](crypto_review_package.md): **Status: UNREVIEWED, off by default.** This is what a reviewer needs to work from.
- [Asking for an outside review of our own cryptography](crypto_review_request.md): **Update (2026-11-14): the owner decided not to hire a reviewer.** libsodium stays the default and `okay::crypto` stays off by default.
- [Own build tool (okay-build): internal plan](own_build_plan.md): Internal note, not a guide. Owner, 2026-11-17: start, do not replace CMake tomorrow.
- [okay-cc](own_compiler_plan.md): Internal note. Not a user guide. It stays off by default: not in the engine CMake, not in CI, and not in releases.
- [Own HTTP client](own_http.md): Our own HTTP client and how ready it is to replace curl.
- [okay-ms](own_msvc_plan.md): Internal note for issue #992. Not a user guide.
- [The platform layer (`okay::platform`) and our own backend](platform_layer.md): Stage 1 of replacing SDL2 with our own code (see the roadmap: seam first, own backend side by side and non-default, SDL2 stays).
- [Getting the editor and launcher off SDL2: the plan](sdl_removal_plan.md): Goal (CLAUDE.md rules 17 and 19): write the replacement ourselves, keep it off by default, and flip a default only with the owner's written approval.
- [SHA-256: three copies compared, one kept](sha256_comparison.md): Owner decision 2026-11-15: "Yes merge duplicates, but make sure to compare and see which one is better" (found by Grok, issue #888).
- [Web & mobile builds](web_mobile.md): The OkaySpace **player** (`player/`) is plain C++17 + SDL2 with a frame-callback loop, so it ports to the web and mobile the same way any SDL2 app does.
- [Xbox One export (experimental)](xbox_uwp.md): The experimental Xbox One (UWP, Developer Mode) export, its audit, and where our own code is used in it.
