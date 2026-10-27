#pragma once
// A saved, reusable skin for the runtime UI kit (grok/ui-theme).
//
// Colors, corner radius, border, drop shadow, padding, font size and the
// hover/press fade speed live on this component and round-trip through the
// scene file. Apply() bakes them onto the widgets already in the tree
// (panels, buttons, sliders, toggles, bars, dropdowns, fields, tabs, text).
// It does not move anything and it does not change widget defaults, so a
// scene that never calls Apply looks exactly as it did.
//
// applyOnStart is off unless you turn it on. Existing scenes are left alone.
// Nine-slice backgrounds stay on UIImage (already in the kit); this theme
// does not add a second drawing path.
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIRadialProgress.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/UITabs.hpp"
#include "okay/Components/UIScrollView.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/UI/UIShape.hpp"
#include <string>

namespace okay {

class UITheme final : public Behaviour {
public:
    std::string themeName = "Studio";
    /// Bake this skin when play starts. Off by default so dropping a theme
    /// into an old scene does not restyle it.
    bool applyOnStart = false;

    Color panel         = Color::FromBytes(34, 40, 56, 248);
    Color accent        = Color::FromBytes(96, 176, 255);
    Color text          = Color::FromBytes(246, 248, 252);
    Color muted         = Color::FromBytes(164, 174, 192);
    Color button        = Color::FromBytes(78, 148, 246);
    Color buttonHover   = Color::FromBytes(122, 182, 255);
    Color buttonPressed = Color::FromBytes(48, 108, 206);
    Color disabled      = Color::FromBytes(54, 60, 76);
    Color danger        = Color::FromBytes(230, 96, 96);
    Color success       = Color::FromBytes(78, 200, 146);
    Color border        = Color::FromBytes(255, 255, 255, 42);
    Color shadowCol     = Color::FromBytes(0, 0, 0, 150);

    float cornerRadius = 14.0f;
    float borderWidth = 1.0f;
    bool  shadow = true;
    float shadowSoftness = 14.0f;
    Vec2  shadowOffset{0.0f, 8.0f};
    /// Layout hint for screens built from this theme (pixels). Apply does not
    /// move widgets; builders and scripts read it.
    float padding = 20.0f;
    float fontScale = 2.0f;          // UIButton label multiplier
    float textPixelSize = 2.0f;      // TextRenderer size when a screen label is still at the world default
    float transitionSpeed = 12.0f;   // button color units/sec; 0 would be instant

    void Start() override {
        if (applyOnStart) Apply();
    }

    /// Bake onto this object and everything under it.
    void Apply() {
        if (gameObject) ApplyTo(gameObject);
    }

    /// Bake onto `root` and its descendants. Objects named "Swatch…" keep the
    /// fill they were given (demo chips for hover / pressed / disabled).
    void ApplyTo(GameObject* root) const {
        if (!root) return;
        ApplyOne(root);
        if (!root->transform) return;
        for (Transform* c : root->transform->Children())
            if (c && c->gameObject) ApplyTo(c->gameObject);
    }

    /// The theme called `name`, or the first theme in the scene when `name` is empty.
    static UITheme* Find(Scene* scene, const std::string& name) {
        if (!scene) return nullptr;
        UITheme* first = nullptr;
        for (const auto& go : scene->Objects()) {
            auto* t = go->GetComponent<UITheme>();
            if (!t) continue;
            if (!first) first = t;
            if (!name.empty() && (t->themeName == name || go->name == name)) return t;
        }
        return name.empty() ? first : nullptr;
    }

private:
    static Color Mul(Color c, float s) {
        return Color(c.r * s, c.g * s, c.b * s, c.a);
    }
    static bool KeepFill(const GameObject* go) {
        return go && go->name.rfind("Swatch", 0) == 0;
    }

    void ApplyOne(GameObject* go) const {
        if (!go || KeepFill(go)) {
            // A swatch still wants crisp text; the fill stays as authored.
            if (go) StyleText(go->GetComponent<TextRenderer>());
            return;
        }
        if (auto* p = go->GetComponent<UIPanel>()) {
            const bool hairline = p->size.y > 0.0f && p->size.y <= 3.0f;
            if (!hairline && p->shape != UIShape::Circle) {
                p->color = panel;
                p->shape = UIShape::Rounded;
                p->cornerRadius = cornerRadius;
                p->borderWidth = borderWidth;
                p->borderColor = border;
                p->shadow = shadow;
                p->shadowColor = shadowCol;
                p->shadowOffset = shadowOffset;
                p->shadowSoftness = shadowSoftness;
            }
        }
        if (auto* b = go->GetComponent<UIButton>()) {
            b->color = button;
            b->hoverColor = buttonHover;
            b->pressedColor = buttonPressed;
            b->disabledColor = disabled;
            b->textColor = text;
            b->hoverTextColor = text;
            b->shape = UIShape::Rounded;
            b->cornerRadius = cornerRadius;
            b->borderWidth = borderWidth;
            b->borderColor = border;
            b->shadow = shadow;
            b->shadowColor = shadowCol;
            b->shadowOffset = Vec2{0.0f, shadowOffset.y * 0.5f};
            b->shadowSoftness = shadowSoftness * 0.55f;
            b->fontScale = fontScale;
            b->transitionSpeed = transitionSpeed;
        }
        if (auto* sl = go->GetComponent<UISlider>()) {
            sl->background = Mul(panel, 0.72f);
            sl->background.a = 1.0f;
            sl->fill = accent;
            sl->knob = text;
            sl->textColor = text;
            sl->trackShape = UIShape::Pill;
            sl->cornerRadius = cornerRadius;
            sl->roundKnob = true;
        }
        if (auto* tg = go->GetComponent<UIToggle>()) {
            tg->boxColor = Mul(panel, 0.7f);
            tg->boxColor.a = 1.0f;
            tg->checkColor = accent;
            tg->knobColor = text;
            tg->textColor = text;
            if (tg->cornerRadius < 6.0f) tg->cornerRadius = 6.0f;
        }
        if (auto* bar = go->GetComponent<UIProgressBar>()) {
            bar->background = Mul(panel, 0.72f);
            bar->background.a = 1.0f;
            bar->fill = success;
            bar->textColor = text;
            bar->shape = UIShape::Pill;
            bar->cornerRadius = cornerRadius;
        }
        if (auto* rad = go->GetComponent<UIRadialProgress>()) {
            rad->background = Mul(panel, 0.65f);
            rad->background.a = 1.0f;
            rad->fill = accent;
            rad->textColor = text;
        }
        if (auto* dd = go->GetComponent<UIDropdown>()) {
            dd->color = Mul(panel, 0.78f);
            dd->color.a = 1.0f;
            dd->hoverColor = buttonHover;
            dd->listColor = panel;
            dd->textColor = text;
            dd->borderColor = border;
            dd->shape = UIShape::Rounded;
            dd->cornerRadius = cornerRadius * 0.7f;
        }
        if (auto* field = go->GetComponent<UIInputField>()) {
            field->color = Mul(panel, 0.7f);
            field->color.a = 1.0f;
            field->focusColor = Mul(panel, 0.9f);
            field->focusColor.a = 1.0f;
            field->textColor = text;
            field->placeholderColor = muted;
            field->shape = UIShape::Rounded;
            field->cornerRadius = cornerRadius * 0.7f;
            field->borderWidth = borderWidth > 0.0f ? borderWidth : 1.0f;
            field->borderColor = accent;
        }
        if (auto* tabs = go->GetComponent<UITabs>()) {
            tabs->background = Mul(panel, 0.85f);
            tabs->selected = accent;
            tabs->textColor = muted;
            tabs->selectedTextColor = text;
            tabs->hoverColor = Color(1.0f, 1.0f, 1.0f, 0.14f);
            tabs->cornerRadius = cornerRadius * 0.6f;
        }
        if (auto* sv = go->GetComponent<UIScrollView>()) {
            sv->background = panel;
            Color bar = border;
            bar.a = 0.55f;
            sv->barColor = bar;
        }
        StyleText(go->GetComponent<TextRenderer>());
    }

    void StyleText(TextRenderer* tr) const {
        if (!tr || !tr->screenSpace) return;
        tr->shadow = true;
        tr->shadowColor = Color::FromBytes(0, 0, 0, 210);
        tr->shadowOffset = {1.0f, 1.0f};
        tr->outline = true;
        tr->outlineColor = Color::FromBytes(0, 0, 0, 230);
        // A screen label left at the world-space default (0.1) is a speck.
        // Authored sizes (the usual 2+) stay put.
        if (tr->pixelSize > 0.0f && tr->pixelSize < 0.5f) tr->pixelSize = textPixelSize;
    }
};

} // namespace okay
