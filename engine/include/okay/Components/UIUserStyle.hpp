#pragma once
// Shared UI styles: one object holds a named style (fill, text colour, border, corner radius, text size); widgets point
// to it by name (UIStyleVariant::userStyle). ApplyUserStyles copies the style onto every widget that points to it, so
// editing the style and applying again restyles all of them. Widgets with no userStyle are never touched, and a scene
// that does not use styles saves nothing extra. Written for this engine from its own UI components.
#include "okay/Scene/Scene.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIStyleVariant.hpp"
#include <string>

namespace okay {

class UIUserStyle final : public Behaviour {
public:
    std::string styleName;                 ///< what widgets point to ("Primary button", "Title")
    bool  applyFill = true, applyText = true, applyBorder = true;
    Color fill{0.26f, 0.59f, 0.98f, 1.0f};
    Color textColor{1.0f, 1.0f, 1.0f, 1.0f};
    Color borderColor{1.0f, 1.0f, 1.0f, 0.3f};
    float borderWidth = 0.0f;
    float cornerRadius = -1.0f;            ///< < 0 = leave the widget's own
    float textSize = 0.0f;                 ///< 0 = leave the widget's own (TextRenderer pixel size)
};

inline Color StyleMixColor(Color a, Color b, float t) { return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a); }

/// Put `st` on one widget. Returns true when the widget had anything to style.
inline bool ApplyUserStyle(GameObject* go, const UIUserStyle& st) {
    bool any = false;
    if (auto* b = go->GetComponent<UIButton>()) {
        any = true;
        if (st.applyFill) { b->color = st.fill; b->hoverColor = StyleMixColor(st.fill, Color(1, 1, 1, 1), 0.18f); b->pressedColor = StyleMixColor(st.fill, Color(0, 0, 0, 1), 0.2f); }
        if (st.applyText) { b->textColor = st.textColor; b->hoverTextColor = st.textColor; }
        if (st.applyBorder) { b->borderColor = st.borderColor; b->borderWidth = st.borderWidth; }
        if (st.cornerRadius >= 0.0f) b->cornerRadius = st.cornerRadius;
    }
    if (auto* p = go->GetComponent<UIPanel>()) {
        any = true;
        if (st.applyFill) p->color = st.fill;
        if (st.applyBorder) { p->borderColor = st.borderColor; p->borderWidth = st.borderWidth; }
        if (st.cornerRadius >= 0.0f) p->cornerRadius = st.cornerRadius;
    }
    if (auto* t = go->GetComponent<TextRenderer>()) {
        if (t->screenSpace) {
            any = true;
            if (st.applyText) t->color = st.textColor;
            if (st.textSize > 0.0f) t->pixelSize = st.textSize;
        }
    }
    return any;
}

/// Restyle every widget in the scene that names a style. Returns how many widgets were restyled.
inline int ApplyUserStyles(Scene& scene) {
    std::vector<const UIUserStyle*> styles;
    for (const auto& up : scene.Objects()) if (up) for (auto* s : up->GetComponents<UIUserStyle>()) styles.push_back(s);
    int n = 0;
    for (const auto& up : scene.Objects()) {
        if (!up) continue;
        auto* v = up->GetComponent<UIStyleVariant>();
        if (!v || v->userStyle.empty()) continue;
        for (const UIUserStyle* s : styles) if (s->styleName == v->userStyle) { if (ApplyUserStyle(up.get(), *s)) ++n; break; }
    }
    return n;
}

} // namespace okay
