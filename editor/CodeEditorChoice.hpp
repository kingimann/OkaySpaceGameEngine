#pragma once
// Which code widget the script editor uses: our own OkayUI::CodeEdit (the default since the owner's decision of 2026-11-17) or
// the vendored ImGuiColorTextEdit (the way back).
//
//   OKAY_CODE_EDITOR=own     our own editor (OkayUI::CodeEdit through editor/ScriptEditorCodeEdit.hpp)
//   OKAY_CODE_EDITOR=imgui   the ImGui editor
//   unset (or anything else) our own editor; with OKAY_THIRD_PARTY=1 the ImGui editor unless the OkayUI backend is on
//
// Pure: no ImGui, no SDL, no OkayUI, so it is tested headless (okayui/tests/test_okayui_script_adapter.cpp).
#include <cstring>

#include "okay/Core/ThirdPartySwitch.hpp"

namespace codeeditorchoice {

enum class Choice { FollowUiBackend, ImGui, Own };

/// Read OKAY_CODE_EDITOR's value (null: unset). Case-insensitive; "own", "okayui", "okay" and "1" mean our own editor,
/// "imgui", "0" and "vendored" the ImGui one.
inline Choice Parse(const char* v) {
    if (!v || !*v) return Choice::FollowUiBackend;
    char b[16] = {};
    std::size_t n = 0;
    for (; v[n] && n + 1 < sizeof b; ++n) b[n] = (v[n] >= 'A' && v[n] <= 'Z') ? (char)(v[n] + 32) : v[n];
    if (v[n]) return Choice::FollowUiBackend;   // longer than any accepted word
    if (!std::strcmp(b, "own") || !std::strcmp(b, "okayui") || !std::strcmp(b, "okay") || !std::strcmp(b, "1")) return Choice::Own;
    if (!std::strcmp(b, "imgui") || !std::strcmp(b, "0") || !std::strcmp(b, "vendored")) return Choice::ImGui;
    return Choice::FollowUiBackend;
}

/// True when the script editor should draw with our own editor. `okayUiBuilt`: the build has OkayUI at all (without it the
/// answer is always false).
inline bool UseOwn(Choice c, bool okayUiBackendOn, bool okayUiBuilt = true) {
    if (!okayUiBuilt) return false;
    if (c == Choice::Own) return true;
    if (c == Choice::ImGui) return false;
    return okay::ThirdPartyForced() ? okayUiBackendOn : true;
}

/// For the log line at start-up.
inline const char* Describe(Choice c, bool okayUiBackendOn, bool okayUiBuilt = true) {
    if (!okayUiBuilt) return c == Choice::Own ? "imgui (OKAY_CODE_EDITOR=own asked, but this build has no OkayUI)" : "imgui";
    if (c == Choice::Own) return "own (OkayUI::CodeEdit, OKAY_CODE_EDITOR=own)";
    if (c == Choice::ImGui) return "imgui (OKAY_CODE_EDITOR=imgui)";
    if (!okay::ThirdPartyForced()) return "own (default; OKAY_CODE_EDITOR=imgui for the old one)";
    return okayUiBackendOn ? "own (OKAY_THIRD_PARTY=1, but the OkayUI backend is on)" : "imgui (OKAY_THIRD_PARTY=1)";
}

} // namespace codeeditorchoice
