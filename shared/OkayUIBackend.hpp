#pragma once
// Which UI toolkit draws the launcher's and the editor's ported screens: Dear ImGui (the default) or OkayUI (preview).
// Pure logic, std only, so the choice can be parsed, saved and tested without a window (tests/test_ui_backend.cpp).
//
// This is a DEVELOPER switch, not a user feature: nothing is offered to users until the owner declares the OkayUI ports ready.
// In a normal run (no OKAY_DEV) the answer is the build default, Dear ImGui, whatever a saved settings file says, and the
// settings rows that change it are not shown. With the developer flag OKAY_DEV (the switch the editor already has) on:
//   1. the environment variable OKAY_UI_BACKEND ("okayui" or "imgui"; anything else is ignored),
//   2. the saved setting (the row in Settings > Appearance in the launcher, View > Rendering & Performance in the editor),
//   3. the build's default: Dear ImGui, unless the build was configured with -DOKAY_UI_DEFAULT_BACKEND=okayui.
// OKAY_UI_BACKEND alone (without OKAY_DEV) is honoured too: setting it is itself a deliberate developer act, and the tests use it.
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

/// The developer flag: OKAY_DEV set to anything but empty or "0" (the same rule as the editor's launcher gate).
inline bool DevMode(const char* okayDevEnv) { return okayDevEnv && okayDevEnv[0] && okayDevEnv[0] != '0'; }

/// The settings rows that change the backend are shown only with the developer flag, and only where OkayUI is compiled in.
inline bool SettingRowVisible(bool devMode, bool compiledIn = CompiledIn()) { return devMode && compiledIn; }

/// The backend to use: the environment value (null or unreadable = none) over the saved setting, and Dear ImGui when OkayUI is
/// not compiled in. The saved setting (`settingValue`, empty = never chosen) is ignored unless `devMode`, so a stale value in
/// a settings file cannot switch a normal run.
inline Backend Resolve(const char* envValue, const std::string& settingValue, bool devMode, bool compiledIn = CompiledIn()) {
    if (!compiledIn) return Backend::ImGui;
    Backend b = BuildDefault();
    if (devMode) Parse(settingValue, b);
    if (envValue) Parse(envValue, b);
    return b;
}

/// Which source decided: 0 build default, 1 the saved setting, 2 the environment (for the settings row's note).
inline int Source(const char* envValue, const std::string& settingValue, bool devMode) {
    Backend scratch = Backend::ImGui;
    if (envValue && Parse(envValue, scratch)) return 2;
    if (devMode && Parse(settingValue, scratch)) return 1;
    return 0;
}

} // namespace okayuibackend
