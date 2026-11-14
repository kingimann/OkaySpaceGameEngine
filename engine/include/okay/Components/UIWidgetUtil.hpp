#pragma once
// Shared helpers for the app-style widgets (search, date, files, accordion,
// breadcrumbs, pages, skeleton, bottom sheet). Kept here so each widget header
// stays about the widget.
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Components/UIWorldContext.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Math/Vec2.hpp"
#include <string>

namespace okay {

/// Text is drawn at this many screen pixels per font pixel. Hit boxes use the
/// same 8x8 cell so a click lands on the letter it looks like it lands on.
inline constexpr float kUIWidgetTextPx = 2.0f;
inline float UIWidgetCharW() { return 8.0f * kUIWidgetTextPx; }

inline void FireUIEvent(GameObject* go, const char* name) {
    if (!go || !name) return;
    if (auto* sc = go->GetComponent<ScriptComponent>())
        if (sc->VM()) sc->VM()->CallEvent(name);
}

/// Mouse-down point inside the widget, in pixels from its top-left. False when
/// the button did not go down, or the cursor is outside the widget.
inline bool UIWidgetPointerLocal(GameObject* go, UIAnchor anchor, const Vec2& position,
                                 const Vec2& size, Vec2& local) {
    if (!Input::GetMouseButtonDown(0)) return false;
    Vec2 o, sz;
    Vec2 p = UIControlPoint(go, anchor, position, size, Input::MousePosition(), o, sz);
    local = {p.x - o.x, p.y - o.y};
    return local.x >= -0.5f && local.y >= -0.5f && local.x <= sz.x + 0.5f && local.y <= sz.y + 0.5f;
}

inline std::string UIWidgetLower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

inline std::string UIWidgetTrim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

} // namespace okay
