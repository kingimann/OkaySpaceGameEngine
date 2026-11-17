#pragma once
// The one master switch back to the third-party code (owner decision 2026-11-17: our own replacements are the default, each with
// a way back; docs/own_code_roadmap.md, docs/try_it.md "Testing our own replacements").
//
//   OKAY_THIRD_PARTY=1   every own replacement that has a third-party twin goes back to it for this run: the platform layer and
//                        renderers (SDL), the WAV reader (SDL_LoadWAV / the old engine reader), the font rasteriser (Dear ImGui's),
//                        the script code editor (ImGuiColorTextEdit) and the ported screens (Dear ImGui).
//
// Each feature's own variable (OKAY_PLATFORM_BACKEND, OKAY_OWN_WAV, OKAY_OWN_FONT_RASTER, OKAY_CODE_EDITOR, OKAY_UI_BACKEND,
// OKAY_LAUNCHER_RENDERER, OKAY_EDITOR_PLATFORM, OKAY_EDITOR_RENDERER) still wins when it is set, so one part can be tried alone
// on either side. Crypto is not here: libsodium stays the only crypto in a release (CLAUDE.md rule 18).
//
// Header-only and std-only: the engine, the launcher, the editor, the player and the tests all read the same rule.
#include <cstdlib>
#include <cstring>

namespace okay {

/// True when OKAY_THIRD_PARTY is set to "1", "on", "yes" or "true" (any case): use the old third-party paths. Read each call
/// (cheap; the tests change it at run time).
inline bool ThirdPartyForced() {
    const char* e = std::getenv("OKAY_THIRD_PARTY");
    if (!e || !*e) return false;
    char b[8] = {};
    std::size_t n = 0;
    for (; e[n] && n + 1 < sizeof b; ++n) b[n] = (e[n] >= 'A' && e[n] <= 'Z') ? (char)(e[n] + 32) : e[n];
    if (e[n]) return false;   // longer than any accepted word
    return !std::strcmp(b, "1") || !std::strcmp(b, "on") || !std::strcmp(b, "yes") || !std::strcmp(b, "true");
}

/// Test hook for the automatic fallback: OKAY_TEST_OWN_FAIL makes our own start of `part` fail on purpose, so a test can see the
/// program log it and start on SDL instead. Its value is "1" or "all" (every part), or a comma list of parts: "window" (the own
/// platform window / GL context), "renderer" (the own painter and ImGui renderer), "platform" (the own platform backend's start).
inline bool OwnStartForcedToFail(const char* part) {
    const char* e = std::getenv("OKAY_TEST_OWN_FAIL");
    if (!e || !*e || !part) return false;
    if (!std::strcmp(e, "1") || !std::strcmp(e, "all")) return true;
    const std::size_t n = std::strlen(part);
    for (const char* p = e; *p;) {
        const char* c = std::strchr(p, ',');
        const std::size_t len = c ? (std::size_t)(c - p) : std::strlen(p);
        if (len == n && !std::strncmp(p, part, n)) return true;
        if (!c) break;
        p = c + 1;
    }
    return false;
}

} // namespace okay
