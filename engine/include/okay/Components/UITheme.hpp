#pragma once
// A saved skin for the runtime UI kit (grok/ui-theme).
//
// Four built-in looks (Dark, Light, Sci-fi, Fantasy) plus a custom palette.
// Apply() bakes colors, a gradient, per-corner radius, border, soft shadow,
// outer glow, opacity, and text outline onto the widgets under this object.
// It does not move them, and it does not run unless you ask (applyOnStart is
// off, or you press Apply / call the script). Existing scenes stay as they are.
//
// Nine-slice frames stay on UIImage. A real blur of the scene behind a panel
// is not available: the UI painter cannot read the framebuffer back. The glow
// and the soft shadow are the depth the renderer already draws.
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
#include "okay/Components/UIImage.hpp"
#include "okay/Components/UITooltip.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/Minimap.hpp"
#include "okay/Components/Crosshair.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/UI/UIShape.hpp"
#include <string>

namespace okay {

class UITheme final : public Behaviour {
public:
    enum class Preset { Custom = 0, Dark = 1, Light = 2, SciFi = 3, Fantasy = 4 };

    std::string themeName = "Dark";
    /// Bake this skin when play starts. Off by default so a theme dropped into
    /// an old scene does not restyle it.
    bool applyOnStart = false;
    int  preset = (int)Preset::Dark;

    Color panel         = Color::FromBytes(22, 26, 38, 250);
    Color panelBottom   = Color::FromBytes(10, 12, 20, 250);
    Color accent        = Color::FromBytes(90, 170, 255);
    Color text          = Color::FromBytes(240, 244, 250);
    Color muted         = Color::FromBytes(150, 162, 184);
    Color button        = Color::FromBytes(64, 132, 240);
    Color buttonHover   = Color::FromBytes(110, 170, 255);
    Color buttonPressed = Color::FromBytes(36, 96, 200);
    Color buttonText    = Color::White;
    Color disabled      = Color::FromBytes(48, 54, 70);
    Color danger        = Color::FromBytes(226, 84, 84);
    Color success       = Color::FromBytes(64, 196, 140);
    Color border        = Color::FromBytes(255, 255, 255, 48);
    Color shadowCol     = Color::FromBytes(0, 0, 0, 160);
    Color glowColor     = Color::FromBytes(90, 170, 255, 0);

    float cornerRadius = 16.0f;
    int   cornerMask = UICornerAll;
    float borderWidth = 1.0f;
    bool  gradient = true;
    int   gradientDir = (int)UIPanel::GradientDir::Vertical;
    bool  shadow = true;
    float shadowSoftness = 18.0f;
    Vec2  shadowOffset{0.0f, 10.0f};
    float glow = 0.0f;
    bool  sheen = true;
    float opacity = 1.0f;
    float padding = 16.0f;
    float fontScale = 2.0f;
    float textPixelSize = 2.0f;
    float transitionSpeed = 12.0f;
    float hoverScale = 1.04f;

    void Start() override {
        if (applyOnStart) Apply();
    }

    /// Fill the palette from a built-in look. Custom leaves the fields alone.
    void ApplyPreset(int which) {
        preset = which;
        themeName = PresetName(which);
        if (which == (int)Preset::Light)    LightPalette();
        else if (which == (int)Preset::SciFi)   SciFiPalette();
        else if (which == (int)Preset::Fantasy) FantasyPalette();
        else if (which == (int)Preset::Dark)    DarkPalette();
    }

    static const char* PresetName(int which) {
        switch (which) {
            case (int)Preset::Light:   return "Light";
            case (int)Preset::SciFi:   return "Sci-fi";
            case (int)Preset::Fantasy: return "Fantasy";
            case (int)Preset::Custom:  return "Custom";
            default:                   return "Dark";
        }
    }

    void Apply() {
        if (gameObject) ApplyTo(gameObject);
    }

    /// Bake onto `root` and its descendants. Objects named "Swatch…" keep the
    /// fill they were given (the hover / pressed / disabled chips).
    void ApplyTo(GameObject* root) const {
        if (!root) return;
        ApplyOne(root);
        if (!root->transform) return;
        for (Transform* c : root->transform->Children())
            if (c && c->gameObject) ApplyTo(c->gameObject);
    }

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
    static Color WithAlpha(Color c, float a) {
        c.a *= a;
        return c;
    }
    static bool KeepFill(const GameObject* go) {
        return go && go->name.rfind("Swatch", 0) == 0;
    }
    static bool SwatchChild(const GameObject* go) {
        if (!go || !go->transform || !go->transform->Parent() || !go->transform->Parent()->gameObject)
            return false;
        return go->transform->Parent()->gameObject->name.rfind("Swatch", 0) == 0;
    }

    void DarkPalette() {
        panel = Color::FromBytes(22, 26, 38, 250);
        panelBottom = Color::FromBytes(8, 10, 18, 250);
        accent = Color::FromBytes(90, 170, 255);
        text = Color::FromBytes(240, 244, 250);
        muted = Color::FromBytes(150, 162, 184);
        button = Color::FromBytes(64, 132, 240);
        buttonHover = Color::FromBytes(120, 176, 255);
        buttonPressed = Color::FromBytes(32, 90, 196);
        buttonText = Color::White;
        disabled = Color::FromBytes(46, 52, 68);
        danger = Color::FromBytes(226, 84, 84);
        success = Color::FromBytes(64, 196, 140);
        border = Color::FromBytes(255, 255, 255, 46);
        shadowCol = Color::FromBytes(0, 0, 0, 170);
        glowColor = Color::FromBytes(80, 150, 255, 0);
        cornerRadius = 16.0f; cornerMask = UICornerAll; borderWidth = 1.0f;
        gradient = true; gradientDir = (int)UIPanel::GradientDir::Vertical;
        shadow = true; shadowSoftness = 18.0f; shadowOffset = {0.0f, 10.0f};
        glow = 0.0f; sheen = true; opacity = 1.0f;
        transitionSpeed = 12.0f; hoverScale = 1.04f; fontScale = 2.0f;
    }
    void LightPalette() {
        panel = Color::FromBytes(250, 246, 238, 252);
        panelBottom = Color::FromBytes(226, 216, 200, 252);
        accent = Color::FromBytes(28, 74, 158);
        text = Color::FromBytes(42, 34, 28);
        muted = Color::FromBytes(112, 98, 86);
        button = Color::FromBytes(28, 74, 158);
        buttonHover = Color::FromBytes(46, 102, 196);
        buttonPressed = Color::FromBytes(16, 48, 112);
        buttonText = Color::White;
        disabled = Color::FromBytes(176, 168, 156);
        danger = Color::FromBytes(168, 42, 42);
        success = Color::FromBytes(28, 128, 78);
        border = Color::FromBytes(90, 64, 36, 70);
        shadowCol = Color::FromBytes(90, 60, 30, 70);
        glowColor = Color::FromBytes(255, 255, 255, 0);
        cornerRadius = 12.0f; cornerMask = UICornerAll; borderWidth = 1.0f;
        gradient = true; gradientDir = (int)UIPanel::GradientDir::Vertical;
        shadow = true; shadowSoftness = 16.0f; shadowOffset = {0.0f, 8.0f};
        glow = 0.0f; sheen = false; opacity = 1.0f;
        transitionSpeed = 12.0f; hoverScale = 1.03f; fontScale = 2.0f;
    }
    void SciFiPalette() {
        panel = Color::FromBytes(6, 12, 20, 236);
        panelBottom = Color::FromBytes(4, 36, 52, 236);
        accent = Color::FromBytes(0, 229, 255);
        text = Color::FromBytes(214, 255, 248);
        muted = Color::FromBytes(86, 168, 176);
        button = Color::FromBytes(4, 20, 32);
        buttonHover = Color::FromBytes(0, 78, 96);
        buttonPressed = Color::FromBytes(0, 36, 48);
        buttonText = Color::FromBytes(0, 240, 255);
        disabled = Color::FromBytes(18, 28, 34);
        danger = Color::FromBytes(255, 64, 128);
        success = Color::FromBytes(150, 255, 80);
        border = Color::FromBytes(0, 220, 240, 200);
        shadowCol = Color::FromBytes(0, 0, 0, 200);
        glowColor = Color::FromBytes(0, 229, 255, 170);
        cornerRadius = 4.0f; cornerMask = UICornerTL | UICornerTR; borderWidth = 1.0f;
        gradient = true; gradientDir = (int)UIPanel::GradientDir::Vertical;
        shadow = true; shadowSoftness = 10.0f; shadowOffset = {0.0f, 4.0f};
        glow = 2.0f; sheen = true; opacity = 1.0f;
        transitionSpeed = 14.0f; hoverScale = 1.05f; fontScale = 2.0f;
    }
    void FantasyPalette() {
        panel = Color::FromBytes(236, 198, 142, 252);
        panelBottom = Color::FromBytes(168, 104, 52, 252);
        accent = Color::FromBytes(196, 142, 42);
        text = Color::FromBytes(58, 30, 14);
        muted = Color::FromBytes(110, 68, 36);
        button = Color::FromBytes(122, 28, 32);
        buttonHover = Color::FromBytes(158, 44, 46);
        buttonPressed = Color::FromBytes(84, 16, 20);
        buttonText = Color::FromBytes(255, 236, 210);
        disabled = Color::FromBytes(120, 84, 52);
        danger = Color::FromBytes(140, 24, 24);
        success = Color::FromBytes(36, 100, 48);
        border = Color::FromBytes(112, 64, 18, 220);
        shadowCol = Color::FromBytes(60, 28, 8, 130);
        glowColor = Color::FromBytes(255, 210, 110, 150);
        cornerRadius = 18.0f; cornerMask = UICornerAll; borderWidth = 2.0f;
        gradient = true; gradientDir = (int)UIPanel::GradientDir::Vertical;
        shadow = true; shadowSoftness = 14.0f; shadowOffset = {0.0f, 8.0f};
        glow = 1.5f; sheen = true; opacity = 1.0f;
        transitionSpeed = 10.0f; hoverScale = 1.04f; fontScale = 2.0f;
    }

    void ApplyOne(GameObject* go) const {
        if (!go || KeepFill(go)) {
            if (go) StyleText(go);
            return;
        }
        const float a = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
        if (auto* p = go->GetComponent<UIPanel>()) {
            const bool hairline = p->size.y > 0.0f && p->size.y <= 3.0f;
            if (!hairline && p->shape != UIShape::Circle) {
                p->color = WithAlpha(panel, a);
                p->colorBottom = WithAlpha(panelBottom, a);
                p->useGradient = gradient;
                p->gradientDir = (UIPanel::GradientDir)gradientDir;
                p->gradientHorizontal = p->gradientDir == UIPanel::GradientDir::Horizontal;
                p->shape = UIShape::Rounded;
                p->cornerRadius = cornerRadius;
                p->cornerMask = cornerMask;
                p->borderWidth = borderWidth;
                p->borderColor = border;
                p->shadow = shadow;
                p->shadowColor = shadowCol;
                p->shadowOffset = shadowOffset;
                p->shadowSoftness = shadowSoftness;
                p->outlineWidth = glow;
                p->outlineColor = glowColor;
                p->topHighlight = sheen;
                p->highlightColor = Color(1.0f, 1.0f, 1.0f, sheen ? 0.16f : 0.0f);
            }
        }
        if (auto* b = go->GetComponent<UIButton>()) {
            b->color = WithAlpha(button, a);
            b->hoverColor = buttonHover;
            b->pressedColor = buttonPressed;
            b->disabledColor = disabled;
            b->textColor = buttonText;
            b->hoverTextColor = buttonText;
            b->shape = UIShape::Rounded;
            b->cornerRadius = cornerRadius * 0.75f;
            b->borderWidth = borderWidth;
            b->borderColor = border;
            b->shadow = shadow;
            b->shadowColor = shadowCol;
            b->shadowOffset = Vec2{0.0f, shadowOffset.y * 0.45f};
            b->shadowSoftness = shadowSoftness * 0.5f;
            b->fontScale = fontScale;
            b->transitionSpeed = transitionSpeed;
            b->hoverScale = hoverScale;
        }
        if (auto* sl = go->GetComponent<UISlider>()) {
            sl->background = Mul(panel, 0.55f); sl->background.a = 1.0f;
            sl->fill = accent;
            sl->knob = buttonText;
            sl->textColor = text;
            sl->trackShape = UIShape::Pill;
            sl->cornerRadius = cornerRadius;
            sl->roundKnob = true;
            sl->showValue = true;
        }
        if (auto* tg = go->GetComponent<UIToggle>()) {
            tg->boxColor = Mul(panel, 0.62f); tg->boxColor.a = 1.0f;
            tg->checkColor = accent;
            tg->knobColor = buttonText;
            tg->textColor = text;
            if (tg->cornerRadius < 8.0f) tg->cornerRadius = 8.0f;
        }
        if (auto* bar = go->GetComponent<UIProgressBar>()) {
            bar->background = Mul(panel, 0.55f); bar->background.a = 1.0f;
            bar->fill = success;
            bar->fillEnd = accent;
            bar->gradientFill = true;
            bar->textColor = text;
            bar->shape = UIShape::Pill;
            bar->cornerRadius = cornerRadius;
            bar->showPercent = true;
        }
        if (auto* rad = go->GetComponent<UIRadialProgress>()) {
            rad->background = Mul(panel, 0.5f); rad->background.a = 1.0f;
            rad->fill = accent;
            rad->textColor = text;
        }
        if (auto* dd = go->GetComponent<UIDropdown>()) {
            dd->color = Mul(panel, 0.72f); dd->color.a = 1.0f;
            dd->hoverColor = buttonHover;
            dd->listColor = panel;
            dd->textColor = text;
            dd->borderColor = border;
            dd->shape = UIShape::Rounded;
            dd->cornerRadius = cornerRadius * 0.6f;
        }
        if (auto* field = go->GetComponent<UIInputField>()) {
            field->color = Mul(panel, 0.6f); field->color.a = 1.0f;
            field->focusColor = Mul(panel, 0.85f); field->focusColor.a = 1.0f;
            field->textColor = text;
            field->placeholderColor = muted;
            field->shape = UIShape::Rounded;
            field->cornerRadius = cornerRadius * 0.55f;
            field->borderWidth = borderWidth > 0.0f ? borderWidth : 1.0f;
            field->borderColor = accent;
        }
        if (auto* tabs = go->GetComponent<UITabs>()) {
            tabs->background = Mul(panel, 0.78f);
            tabs->selected = accent;
            tabs->textColor = muted;
            tabs->selectedTextColor = buttonText;
            tabs->hoverColor = Color(1.0f, 1.0f, 1.0f, 0.14f);
            tabs->cornerRadius = cornerRadius * 0.55f;
        }
        if (auto* sv = go->GetComponent<UIScrollView>()) {
            sv->background = WithAlpha(panel, a * 0.92f);
            Color bar = border; bar.a = 0.7f;
            sv->barColor = bar;
        }
        if (auto* im = go->GetComponent<UIImage>()) {
            if (go->name == "Slice") im->color = accent;
        }
        if (auto* tip = go->GetComponent<UITooltip>()) {
            tip->background = panel;
            tip->textColor = text;
            tip->borderColor = border;
        }
        if (auto* map = go->GetComponent<Minimap>()) {
            map->background = WithAlpha(panel, 0.92f);
            map->border = accent;
            map->targetColor = accent;
        }
        if (auto* ch = go->GetComponent<Crosshair>()) {
            ch->color = accent;
            ch->dotColor = buttonText;
            ch->circleColor = accent;
        }
        StyleText(go);
    }

    void StyleText(GameObject* go) const {
        auto* tr = go->GetComponent<TextRenderer>();
        if (!tr || !tr->screenSpace) return;
        const bool sw = SwatchChild(go);
        if (!sw) tr->color = text;
        const bool lightInk = (sw ? tr->color : text).r + (sw ? tr->color : text).g + (sw ? tr->color : text).b > 1.7f;
        tr->shadow = true;
        tr->outline = true;
        tr->outlineColor = lightInk ? Color(0.0f, 0.0f, 0.0f, 0.90f) : Color(1.0f, 0.98f, 0.92f, 0.80f);
        tr->shadowColor = tr->outlineColor;
        tr->shadowOffset = {1.0f, 1.0f};
        if (tr->pixelSize > 0.0f && tr->pixelSize < 0.5f) tr->pixelSize = textPixelSize;
    }
};

} // namespace okay
