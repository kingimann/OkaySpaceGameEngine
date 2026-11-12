#pragma once
// Which UI toolkit draws the launcher's and the editor's ported screens: Dear ImGui (the default) or OkayUI (preview).
// Pure logic, std only, so the choice can be parsed, saved and tested without a window (tests/test_ui_backend.cpp).
//
// The order of precedence, highest first:
//   1. the environment variable OKAY_UI_BACKEND ("okayui" or "imgui"; anything else is ignored),
//   2. the person's setting (Settings > Appearance in the launcher, Edit > Preferences in the editor),
//   3. the build's default: Dear ImGui, unless the build was configured with -DOKAY_UI_DEFAULT_BACKEND=okayui.
// A build without OkayUI compiled in (no SDL2, or the web player) always answers Dear ImGui.
#include <cctype>
#include <string>

#ifndef OKAY_UI_DEFAULT_BACKEND_OKAYUI
#define OKAY_UI_DEFAULT_BACKEND_OKAYUI 0
#endif

namespace okayuibackend {

enum class Backend { ImGui = 0, OkayUI = 1 };

/// True when this binary can host OkayUI at all (the okay_ui library is linked in).
inline bool CompiledIn() {
#ifdef OKAY_HAVE_OKAYUI
    return true;
#else
    return false;
#endif
}

/// The build's default (the CMake option OKAY_UI_DEFAULT_BACKEND).
inline Backend BuildDefault() { return OKAY_UI_DEFAULT_BACKEND_OKAYUI ? Backend::OkayUI : Backend::ImGui; }

/// The short name stored in settings files and read from the environment: "imgui" or "okayui".
inline const char* Name(Backend b) { return b == Backend::OkayUI ? "okayui" : "imgui"; }
/// The words the settings screens show.
inline const char* Label(Backend b) { return b == Backend::OkayUI ? "OkayUI (preview)" : "Dear ImGui (default)"; }

/// Read a backend name: "imgui", "dear imgui", "okayui", "okay ui" (case and surrounding spaces ignored), or the numbers
/// 0 and 1. Returns false and leaves `out` alone for anything else, including an empty string.
inline bool Parse(const std::string& text, Backend& out) {
    std::string s;
    for (char c : text) if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '_' && c != '-')
        s += (char)std::tolower((unsigned char)c);
    if (s == "imgui" || s == "dearimgui" || s == "0") { out = Backend::ImGui; return true; }
    if (s == "okayui" || s == "okay" || s == "1") { out = Backend::OkayUI; return true; }
    return false;
}

/// What a settings file stores: the canonical name of `text`, or an empty string for anything unreadable (= "never chosen").
/// Used on load (launcher.cfg `ui_backend=okayui`) and the other way on save, so a hand-edited or damaged line cannot pick a backend.
inline std::string Normalize(const std::string& text) {
    Backend b = Backend::ImGui;
    return Parse(text, b) ? std::string(Name(b)) : std::string();
}
/// The editor's settings file holds integers ("uibackend 1"): 1 is OkayUI, any other number Dear ImGui.
inline std::string FromSettingInt(int v) { return Name(v == 1 ? Backend::OkayUI : Backend::ImGui); }
/// The integer to save for a setting (never called for an empty one: nothing is saved then, so "never chosen" survives a save).
inline int ToSettingInt(const std::string& setting) { Backend b = Backend::ImGui; return Parse(setting, b) && b == Backend::OkayUI ? 1 : 0; }

/// The backend to use: the environment value (null or unreadable = none) over the setting, and Dear ImGui when OkayUI is
/// not compiled in. `settingValue` is what the settings file held (an empty string = the person never chose).
inline Backend Resolve(const char* envValue, const std::string& settingValue, bool compiledIn = CompiledIn()) {
    if (!compiledIn) return Backend::ImGui;
    Backend b = BuildDefault();
    Parse(settingValue, b);
    if (envValue) Parse(envValue, b);
    return b;
}

/// Which source decided: 0 build default, 1 the setting, 2 the environment (for the Settings screen's note).
inline int Source(const char* envValue, const std::string& settingValue) {
    Backend scratch = Backend::ImGui;
    if (envValue && Parse(envValue, scratch)) return 2;
    if (Parse(settingValue, scratch)) return 1;
    return 0;
}

} // namespace okayuibackend
