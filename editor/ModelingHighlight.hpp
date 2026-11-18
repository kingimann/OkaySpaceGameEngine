#pragma once
// Button wrappers for the Modeling window: each draws the ordinary ImGui widget, then calls one hook, which outlines the
// button when it matches the tool search and remembers its hint. main.cpp points the hook at its own function and, only around
// DrawModeling, maps ImGui::Button / SmallButton / Checkbox onto these with a #define. Global scope on purpose (ImGui's namespace).
#include "imgui.h"

inline void (*g_mdlMarkHook)(const char* label) = nullptr;   // set by the Modeling window; null = do nothing
namespace ImGui {
inline bool MdlHlButton(const char* l, const ImVec2& sz = ImVec2(0, 0)) { const bool r = Button(l, sz); if (g_mdlMarkHook) g_mdlMarkHook(l); return r; }
inline bool MdlHlSmallButton(const char* l) { const bool r = SmallButton(l); if (g_mdlMarkHook) g_mdlMarkHook(l); return r; }
inline bool MdlHlCheckbox(const char* l, bool* v) { const bool r = Checkbox(l, v); if (g_mdlMarkHook) g_mdlMarkHook(l); return r; }
}
