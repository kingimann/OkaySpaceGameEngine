#include "okay/UI/UIFocus.hpp"
#include "okay/Core/AccessibilitySettings.hpp"
#include "okay/Components/UITheme.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace okay {

namespace {

struct FocusState {
    UIFocus::Source source = UIFocus::Source::None;
    bool manualPulse = false;
    float pulseTime = 0.0f;
};
FocusState& F() {
    static FocusState s;
    return s;
}

float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

UITheme* ThemeFor(GameObject* go) {
    for (GameObject* g = go; g;) {
        if (auto* t = g->GetComponent<UITheme>()) return t;
        Transform* p = g->transform ? g->transform->Parent() : nullptr;
        g = (p && p->gameObject) ? p->gameObject : nullptr;
    }
    Scene* scene = go ? go->scene() : nullptr;
    if (!scene) return nullptr;
    UITheme* first = nullptr;
    for (const auto& o : scene->Objects()) {
        auto* t = o->GetComponent<UITheme>();
        if (!t) continue;
        if (t->global) return t;
        if (!first) first = t;
    }
    return first;
}

} // namespace

UIFocus::Source UIFocus::Current() { return F().source; }
bool UIFocus::RingVisible() {
    return F().source == Source::Keyboard || F().source == Source::Script;
}
void UIFocus::NoteKeyboard() { F().source = Source::Keyboard; }
void UIFocus::NotePointer() { F().source = Source::Pointer; }
void UIFocus::NoteScript() { F().source = Source::Script; }
void UIFocus::Reset() {
    F().source = Source::None;
    F().manualPulse = false;
    F().pulseTime = 0.0f;
}
void UIFocus::SetPulseTime(float seconds) {
    F().manualPulse = true;
    F().pulseTime = seconds;
}
void UIFocus::ClearManualPulse() { F().manualPulse = false; }
float UIFocus::PulseTime() {
    if (F().manualPulse) return F().pulseTime;
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    return std::chrono::duration<float>(clock::now() - start).count();
}

UIFocusStyle ResolveUIFocusStyle(GameObject* widget) {
    UIFocusStyle s;
    if (UITheme* theme = ThemeFor(widget)) {
        s.color = theme->focusColor;
        s.thickness = theme->focusThickness;
        s.padding = theme->focusPadding;
        s.pulse = theme->focusPulse;
    }
    const AccessibilitySettings& a = Accessibility::Get();
    s.thickness *= a.focusThicknessScale;
    if (a.reduceMotion || a.reduceFlashing) s.pulse = false;
    if (a.focusHighVisibility) {
        s.highVisibility = true;
        s.color = Color::FromBytes(255, 220, 0, 255);
        s.thickness = std::max(s.thickness * 2.0f, 6.0f);
        s.padding = std::max(s.padding + 2.0f, 5.0f);
        s.pulse = false;
    }
    s.thickness = Clamp(s.thickness, 1.0f, 16.0f);
    s.padding = Clamp(s.padding, 0.0f, 24.0f);
    return s;
}

float UIFocusPulseAlpha(const UIFocusStyle& style) {
    if (!style.pulse) return 1.0f;
    const float wave = std::sin(UIFocus::PulseTime() * 6.2831853f);
    return 0.55f + 0.45f * (wave * 0.5f + 0.5f);
}

} // namespace okay
