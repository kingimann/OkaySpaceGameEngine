#pragma once
// The few Inspector rules the editor and the OkayUI screens share. Part of shared/EditorPanelModels.hpp (which includes it). No ImGui,
// no OkayUI, no engine types.
//
// Round 6 (2026-11-27): the library's own Inspector views (the object header card, the component bar, the Transform, the Mesh Renderer,
// Add Component and the "field rows" bodies, with their models here and in InspectorFieldsModel.hpp) were deleted: since round 5 the
// editor draws every component with its one body (editor/main.cpp DrawInspector), so they had no caller (rule 19d). What is left is
// used by the editor's component bar and combos and by the OkayUI screens' colour field.
//
// Written from the Inspector's own behaviour (editor/main.cpp DrawInspector, CompHeader); no code from any other project.
#include "HierarchyPanelModel.hpp"   // HierCol

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace okaypanels {

/// The component's name cut to `room` pixels with an ellipsis (whole UTF-8 characters), as the bar shows it. `measure(text)` is the
/// version's text width. Nothing is cut when the room is under 20 pixels (the bar is too narrow to matter).
template <class Measure>
std::string CompHeaderTitle(const std::string& label, float room, Measure measure) {
    std::string nm = label;
    if (room > 20.0f && measure(nm.c_str()) > room) {
        while (nm.size() > 1 && measure((nm + "\xE2\x80\xA6").c_str()) > room) {
            // one whole character off the end: its continuation bytes, then its first byte (the editor's bar once left a first byte alone)
            while (!nm.empty() && (static_cast<unsigned char>(nm.back()) & 0xC0) == 0x80) nm.pop_back();
            if (!nm.empty()) nm.pop_back();
        }
        nm += "\xE2\x80\xA6";
    }
    return nm;
}

/// A combo's closed text cut to fit `maxW` with "..." at its end (whole characters), as the editor's combos draw it ("Bool false" and
/// "CrouchWalk" ran under the arrow, stage 6).
template <class Measure>
std::string InspFitText(const char* text, float maxW, Measure measure) {
    std::string s(text ? text : "");
    if (s.empty() || measure(s.c_str()) <= maxW) return s;
    while (!s.empty()) {
        do { s.pop_back(); } while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80);   // a whole character
        if (!s.empty() && ((unsigned char)s.back() & 0x80)) s.pop_back();                    // the lead byte of a cut character
        const std::string t = s + "...";
        if (measure(t.c_str()) <= maxW) return t;
    }
    return "...";
}

/// A colour field's channels as Dear ImGui's ColorEdit shows them: whole numbers 0 to 255.
inline int ColorByte(float c) { return (int)std::lround(std::min(1.0f, std::max(0.0f, c)) * 255.0f); }

/// The colour markers Dear ImGui draws down the left of a colour field's R, G, B and A cells.
inline const std::uint32_t kColorMarker[4] = {HierCol(240, 20, 20), HierCol(20, 240, 20), HierCol(20, 20, 240), HierCol(140, 140, 140)};

} // namespace okaypanels
