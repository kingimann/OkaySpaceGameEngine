// OkaySpace "Slate" look, shared by the launcher and the editor.
//
// It grew out of the chat window: blue-tinted dark surfaces in three depths
// (sunken rails/toolbars, the base panel, raised cards/popups), soft white
// hairlines instead of black borders, rounded controls, light text, and the
// accent colour reserved for interactive states (selection bars, checks,
// sliders, the active tab, primary buttons). Keep both apps on these values so
// they read as one product.
#pragma once
#include "imgui.h"

namespace okaytheme {

// Surfaces, darkest to lightest.
inline const ImVec4 kDeep    (0.082f, 0.090f, 0.114f, 1.0f);   // title bars, empty dock space
inline const ImVec4 kSunken  (0.094f, 0.102f, 0.129f, 1.0f);   // side rails, menu/tool bars, tab strips
inline const ImVec4 kBase    (0.118f, 0.129f, 0.161f, 1.0f);   // panels / window background
inline const ImVec4 kRaised  (0.150f, 0.163f, 0.200f, 1.0f);   // cards, popups
inline const ImVec4 kField   (0.160f, 0.180f, 0.230f, 1.0f);   // inputs
inline const ImVec4 kControl (0.200f, 0.220f, 0.280f, 1.0f);   // buttons, foldout headers
// Text.
inline const ImVec4 kText    (0.900f, 0.920f, 0.960f, 1.0f);
inline const ImVec4 kTextDim (0.550f, 0.590f, 0.670f, 1.0f);

inline ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}
inline ImVec4 WithAlpha(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }

// Rounding + borders (spacing/padding stay per-app). Call before ScaleAllSizes.
inline void ApplySlateMetrics(ImGuiStyle& s) {
    s.WindowRounding = 8.0f;  s.ChildRounding = 8.0f;  s.PopupRounding = 8.0f;
    s.FrameRounding  = 6.0f;  s.GrabRounding  = 6.0f;  s.TabRounding   = 6.0f;
    s.ScrollbarRounding = 8.0f;
    s.WindowBorderSize = 1.0f; s.ChildBorderSize = 1.0f; s.PopupBorderSize = 1.0f;
    s.FrameBorderSize  = 0.0f; s.TabBorderSize   = 0.0f;
}

// The full colour palette for a given accent.
inline void ApplySlateColors(ImGuiStyle& s, const ImVec4& accentIn) {
    const ImVec4 accent(accentIn.x, accentIn.y, accentIn.z, 1.0f);
    const ImVec4 accentLit = Mix(accent, ImVec4(1, 1, 1, 1), 0.28f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                  = kText;
    c[ImGuiCol_TextDisabled]          = kTextDim;
    c[ImGuiCol_WindowBg]              = kBase;
    c[ImGuiCol_ChildBg]               = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]               = WithAlpha(Mix(kRaised, kBase, 0.35f), 0.985f);
    c[ImGuiCol_Border]                = ImVec4(1, 1, 1, 0.075f);
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = kField;
    c[ImGuiCol_FrameBgHovered]        = Mix(kField, ImVec4(1, 1, 1, 1), 0.05f);
    c[ImGuiCol_FrameBgActive]         = Mix(kField, ImVec4(1, 1, 1, 1), 0.09f);
    c[ImGuiCol_TitleBg]               = kDeep;
    c[ImGuiCol_TitleBgActive]         = Mix(kDeep, kSunken, 0.6f);
    c[ImGuiCol_TitleBgCollapsed]      = kDeep;
    c[ImGuiCol_MenuBarBg]             = kSunken;
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(1, 1, 1, 0.12f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(1, 1, 1, 0.24f);
    c[ImGuiCol_ScrollbarGrabActive]   = accent;
    c[ImGuiCol_CheckMark]             = accentLit;
    c[ImGuiCol_SliderGrab]            = accent;
    c[ImGuiCol_SliderGrabActive]      = accentLit;
    c[ImGuiCol_Button]                = kControl;
    c[ImGuiCol_ButtonHovered]         = Mix(kControl, ImVec4(1, 1, 1, 1), 0.08f);
    c[ImGuiCol_ButtonActive]          = accent;
    c[ImGuiCol_Header]                = Mix(kControl, accent, 0.18f);
    c[ImGuiCol_HeaderHovered]         = Mix(kControl, ImVec4(1, 1, 1, 1), 0.07f);
    c[ImGuiCol_HeaderActive]          = Mix(kControl, accent, 0.45f);
    c[ImGuiCol_Separator]             = ImVec4(1, 1, 1, 0.08f);
    c[ImGuiCol_SeparatorHovered]      = WithAlpha(accent, 0.70f);
    c[ImGuiCol_SeparatorActive]       = accent;
    c[ImGuiCol_ResizeGrip]            = ImVec4(1, 1, 1, 0.06f);
    c[ImGuiCol_ResizeGripHovered]     = WithAlpha(accent, 0.60f);
    c[ImGuiCol_ResizeGripActive]      = accent;
    c[ImGuiCol_InputTextCursor]       = kText;
    c[ImGuiCol_Tab]                   = kSunken;
    c[ImGuiCol_TabHovered]            = Mix(kSunken, kControl, 0.7f);
    c[ImGuiCol_TabSelected]           = kBase;
    c[ImGuiCol_TabSelectedOverline]   = accent;
    c[ImGuiCol_TabDimmed]             = kSunken;
    c[ImGuiCol_TabDimmedSelected]     = Mix(kSunken, kBase, 0.6f);
    c[ImGuiCol_TabDimmedSelectedOverline] = WithAlpha(accent, 0.45f);
    c[ImGuiCol_DockingPreview]        = WithAlpha(accent, 0.55f);
    c[ImGuiCol_DockingEmptyBg]        = kDeep;
    c[ImGuiCol_TableHeaderBg]         = Mix(kSunken, kBase, 0.5f);
    c[ImGuiCol_TableBorderStrong]     = ImVec4(1, 1, 1, 0.07f);
    c[ImGuiCol_TableBorderLight]      = ImVec4(1, 1, 1, 0.04f);
    c[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(1, 1, 1, 0.022f);
    c[ImGuiCol_TextLink]              = accentLit;
    c[ImGuiCol_TextSelectedBg]        = WithAlpha(accent, 0.40f);
    c[ImGuiCol_DragDropTarget]        = accentLit;
    c[ImGuiCol_NavCursor]             = accent;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1, 1, 1, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0, 0, 0, 0.45f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, 0.55f);
}

}  // namespace okaytheme
