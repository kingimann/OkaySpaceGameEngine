#pragma once
// A saved skin for the runtime UI kit (grok/ui-theme).
//
// Twelve built-in looks (Dark, Light, Sci-fi, Fantasy, Modern Flat, Cartoon, Retro Pixel,
// Minimal, Neon, Glass, Military, Horror) plus a custom palette.
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
#include "okay/Components/UISlotGrid.hpp"
#include "okay/Components/UIStepper.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIStyleVariant.hpp"
#include "okay/Components/Minimap.hpp"
#include "okay/Components/Crosshair.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/UI/UIShape.hpp"
#include <cmath>
#include <string>
#include <vector>

namespace okay {

class UITheme final : public Behaviour {
public:
    /// New presets are appended: the number saved in a scene never changes meaning.
    enum class Preset { Custom = 0, Dark = 1, Light = 2, SciFi = 3, Fantasy = 4,
                        ModernFlat = 5, Cartoon = 6, RetroPixel = 7, Minimal = 8,
                        Neon = 9, Glass = 10, Military = 11, Horror = 12 };
    static constexpr int kPresetCount = 13;   // including Custom

    /// How a widget stands out: the same theme, a different role.
    enum class Variant { Default = 0, Primary = 1, Secondary = 2, Danger = 3, Ghost = 4, Outline = 5, Pill = 6 };
    static constexpr int kVariantCount = 7;

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
    /// Optional font (a .ttf / .otf path) put on every button and text the theme touches. Empty = leave them.
    std::string fontPath;
    /// Apply to every widget in the scene instead of only the ones under this object.
    bool  global = false;

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
        else if (which == (int)Preset::ModernFlat) ModernFlatPalette();
        else if (which == (int)Preset::Cartoon)    CartoonPalette();
        else if (which == (int)Preset::RetroPixel) RetroPixelPalette();
        else if (which == (int)Preset::Minimal)    MinimalPalette();
        else if (which == (int)Preset::Neon)       NeonPalette();
        else if (which == (int)Preset::Glass)      GlassPalette();
        else if (which == (int)Preset::Military)   MilitaryPalette();
        else if (which == (int)Preset::Horror)     HorrorPalette();
    }

    /// Preset by its name ("Neon", "sci-fi", "Retro Pixel"...; case and spaces do not matter).
    /// Returns -1 when no preset has that name.
    static int PresetFromName(const std::string& name) {
        const std::string key = Squash(name);
        for (int i = 0; i < kPresetCount; ++i)
            if (Squash(PresetName(i)) == key) return i;
        return -1;
    }
    static const char* VariantName(int v) {
        switch (v) {
            case (int)Variant::Primary:   return "Primary";
            case (int)Variant::Secondary: return "Secondary";
            case (int)Variant::Danger:    return "Danger";
            case (int)Variant::Ghost:     return "Ghost";
            case (int)Variant::Outline:   return "Outline";
            case (int)Variant::Pill:      return "Pill";
            default:                      return "Default";
        }
    }
    static int VariantFromName(const std::string& name) {
        const std::string key = Squash(name);
        for (int i = 0; i < kVariantCount; ++i)
            if (Squash(VariantName(i)) == key) return i;
        return -1;
    }

    static const char* PresetName(int which) {
        switch (which) {
            case (int)Preset::Light:   return "Light";
            case (int)Preset::SciFi:   return "Sci-fi";
            case (int)Preset::Fantasy: return "Fantasy";
            case (int)Preset::Custom:  return "Custom";
            case (int)Preset::ModernFlat: return "Modern Flat";
            case (int)Preset::Cartoon:    return "Cartoon";
            case (int)Preset::RetroPixel: return "Retro Pixel";
            case (int)Preset::Minimal:    return "Minimal";
            case (int)Preset::Neon:       return "Neon";
            case (int)Preset::Glass:      return "Glass";
            case (int)Preset::Military:   return "Military";
            case (int)Preset::Horror:     return "Horror";
            default:                   return "Dark";
        }
    }

    void Apply() {
        if (!gameObject) return;
        if (global && gameObject->scene()) ApplyToScene(*gameObject->scene());
        else ApplyTo(gameObject);
    }

    /// Bake onto every object in the scene (UI widgets only change; nothing else is touched).
    void ApplyToScene(Scene& scene) const {
        for (const auto& go : scene.Objects()) ApplyOne(go.get());
    }

    /// Style one widget as `v`: a role (Primary, Danger, Ghost...) drawn from this theme's colours.
    /// Buttons, panels, progress bars, sliders, toggles and text understand it. Default does nothing.
    void ApplyVariant(GameObject* go, Variant v) const;

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

    // ---- Presets added with the 12-look set. Colours only; the shape of each look is in the numbers. ----
    void SetLook(Color pn, Color pb, Color ac, Color tx, Color mu, Color bt, Color bh, Color bp, Color btx,
                 Color dis, Color dg, Color ok, Color bd, Color sh, Color gl) {
        panel = pn; panelBottom = pb; accent = ac; text = tx; muted = mu; button = bt; buttonHover = bh;
        buttonPressed = bp; buttonText = btx; disabled = dis; danger = dg; success = ok; border = bd;
        shadowCol = sh; glowColor = gl;
    }
    void SetShape(float radius, int mask, float borderW, bool grad, bool sh, float soft, Vec2 off, float gl,
                  bool sheenOn, float trans, float hscale) {
        cornerRadius = radius; cornerMask = mask; borderWidth = borderW;
        gradient = grad; gradientDir = (int)UIPanel::GradientDir::Vertical;
        shadow = sh; shadowSoftness = soft; shadowOffset = off; glow = gl; sheen = sheenOn; opacity = 1.0f;
        transitionSpeed = trans; hoverScale = hscale; fontScale = 2.0f;
    }
    using C = Color;
    void ModernFlatPalette() {
        SetLook(C::FromBytes(36, 40, 52, 255), C::FromBytes(36, 40, 52, 255), C::FromBytes(79, 140, 255),
                C::FromBytes(236, 240, 246), C::FromBytes(145, 153, 170), C::FromBytes(79, 140, 255),
                C::FromBytes(108, 162, 255), C::FromBytes(56, 112, 224), C::White, C::FromBytes(60, 66, 82),
                C::FromBytes(239, 83, 80), C::FromBytes(76, 201, 140), C::FromBytes(255, 255, 255, 20),
                C::FromBytes(0, 0, 0, 0), C::FromBytes(79, 140, 255, 0));
        SetShape(8.0f, UICornerAll, 1.0f, false, false, 0.0f, {0.0f, 0.0f}, 0.0f, false, 14.0f, 1.0f);
    }
    void CartoonPalette() {
        SetLook(C::FromBytes(255, 244, 214, 255), C::FromBytes(255, 222, 150, 255), C::FromBytes(255, 107, 53),
                C::FromBytes(60, 36, 20), C::FromBytes(140, 100, 60), C::FromBytes(255, 107, 53),
                C::FromBytes(255, 142, 86), C::FromBytes(214, 80, 30), C::White, C::FromBytes(200, 184, 150),
                C::FromBytes(230, 50, 70), C::FromBytes(80, 190, 70), C::FromBytes(60, 36, 20, 255),
                C::FromBytes(60, 36, 20, 255), C::FromBytes(255, 255, 255, 0));
        SetShape(22.0f, UICornerAll, 3.0f, true, true, 0.0f, {0.0f, 6.0f}, 0.0f, true, 16.0f, 1.08f);
    }
    void RetroPixelPalette() {
        SetLook(C::FromBytes(24, 16, 40, 255), C::FromBytes(24, 16, 40, 255), C::FromBytes(255, 204, 0),
                C::FromBytes(240, 240, 200), C::FromBytes(150, 150, 120), C::FromBytes(60, 60, 140),
                C::FromBytes(90, 90, 200), C::FromBytes(30, 30, 90), C::White, C::FromBytes(50, 50, 50),
                C::FromBytes(255, 60, 60), C::FromBytes(60, 255, 100), C::FromBytes(255, 255, 255, 255),
                C::FromBytes(0, 0, 0, 255), C::FromBytes(255, 204, 0, 0));
        SetShape(0.0f, UICornerAll, 2.0f, false, true, 0.0f, {4.0f, 4.0f}, 0.0f, false, 0.0f, 1.0f);
    }
    void MinimalPalette() {
        SetLook(C::FromBytes(250, 250, 250, 255), C::FromBytes(250, 250, 250, 255), C::FromBytes(24, 24, 24),
                C::FromBytes(24, 24, 24), C::FromBytes(130, 130, 130), C::FromBytes(24, 24, 24),
                C::FromBytes(64, 64, 64), C::FromBytes(0, 0, 0), C::White, C::FromBytes(214, 214, 214),
                C::FromBytes(200, 40, 40), C::FromBytes(40, 150, 80), C::FromBytes(0, 0, 0, 40),
                C::FromBytes(0, 0, 0, 0), C::FromBytes(0, 0, 0, 0));
        SetShape(4.0f, UICornerAll, 1.0f, false, false, 0.0f, {0.0f, 0.0f}, 0.0f, false, 14.0f, 1.0f);
    }
    void NeonPalette() {
        SetLook(C::FromBytes(10, 6, 24, 240), C::FromBytes(22, 6, 42, 240), C::FromBytes(255, 40, 200),
                C::FromBytes(240, 235, 255), C::FromBytes(150, 120, 200), C::FromBytes(20, 10, 40),
                C::FromBytes(60, 20, 90), C::FromBytes(10, 4, 24), C::FromBytes(0, 255, 240),
                C::FromBytes(30, 20, 50), C::FromBytes(255, 60, 90), C::FromBytes(60, 255, 160),
                C::FromBytes(255, 40, 200, 230), C::FromBytes(255, 40, 200, 120), C::FromBytes(0, 255, 240, 200));
        SetShape(10.0f, UICornerAll, 2.0f, true, true, 22.0f, {0.0f, 0.0f}, 2.0f, false, 16.0f, 1.04f);
    }
    void GlassPalette() {
        // Glass is built from layers, not a blur: a see-through fill that fades from a bright top to almost
        // clear, a bright hairline edge, a second soft ring just outside it, a lit top band and a deep soft
        // shadow. Whatever is behind the card shows through it.
        SetLook(C::FromBytes(255, 255, 255, 44), C::FromBytes(255, 255, 255, 10), C::FromBytes(150, 210, 255),
                C::White, C::FromBytes(225, 232, 245, 210), C::FromBytes(255, 255, 255, 56),
                C::FromBytes(255, 255, 255, 96), C::FromBytes(255, 255, 255, 30), C::White,
                C::FromBytes(255, 255, 255, 20), C::FromBytes(255, 120, 120), C::FromBytes(120, 240, 190),
                C::FromBytes(255, 255, 255, 190), C::FromBytes(8, 16, 48, 120), C::FromBytes(255, 255, 255, 70));
        SetShape(22.0f, UICornerAll, 1.5f, true, true, 30.0f, {0.0f, 16.0f}, 1.5f, true, 14.0f, 1.02f);
    }
    void MilitaryPalette() {
        SetLook(C::FromBytes(52, 60, 42, 250), C::FromBytes(36, 42, 30, 250), C::FromBytes(200, 170, 60),
                C::FromBytes(222, 224, 200), C::FromBytes(150, 158, 126), C::FromBytes(84, 96, 64),
                C::FromBytes(110, 124, 82), C::FromBytes(60, 70, 44), C::FromBytes(236, 236, 210),
                C::FromBytes(60, 64, 52), C::FromBytes(176, 50, 40), C::FromBytes(110, 160, 60),
                C::FromBytes(20, 24, 16, 255), C::FromBytes(0, 0, 0, 140), C::FromBytes(200, 170, 60, 0));
        SetShape(2.0f, UICornerAll, 2.0f, true, true, 6.0f, {0.0f, 3.0f}, 0.0f, false, 12.0f, 1.0f);
    }
    void HorrorPalette() {
        SetLook(C::FromBytes(16, 8, 8, 245), C::FromBytes(6, 2, 2, 245), C::FromBytes(170, 20, 20),
                C::FromBytes(210, 196, 184), C::FromBytes(120, 96, 88), C::FromBytes(60, 12, 12),
                C::FromBytes(110, 20, 20), C::FromBytes(30, 6, 6), C::FromBytes(230, 210, 200),
                C::FromBytes(34, 24, 24), C::FromBytes(200, 20, 20), C::FromBytes(90, 130, 70),
                C::FromBytes(110, 20, 20, 220), C::FromBytes(140, 0, 0, 140), C::FromBytes(170, 20, 20, 120));
        SetShape(3.0f, UICornerAll, 1.0f, true, true, 20.0f, {0.0f, 0.0f}, 1.0f, false, 8.0f, 1.0f);
    }

    static std::string Squash(const std::string& s) {
        std::string out;
        for (char c : s)
            if (c != ' ' && c != '-' && c != '_') out.push_back((char)((c >= 'A' && c <= 'Z') ? c + 32 : c));
        return out;
    }
    static Color Mix(Color a, Color b, float t) {
        return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
    }
    /// A colour as seen on screen: see-through colours are laid over a mid-blue backdrop, so the contrast
    /// checks do not call a glass card white-on-white.
    static Color Flat(Color c) {
        const Color backdrop(0.23f, 0.32f, 0.55f, 1.0f);
        Color f = Mix(backdrop, c, c.a < 0.0f ? 0.0f : (c.a > 1.0f ? 1.0f : c.a));
        f.a = 1.0f;
        return f;
    }
    static float Luminance(Color c) {
        auto lin = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
        return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b);
    }
public:
    /// WCAG contrast ratio of two colours (1 = none, 21 = black on white); see-through colours are flattened first.
    static float Contrast(Color a, Color b) {
        const float la = Luminance(Flat(a)), lb = Luminance(Flat(b));
        const float hi = la > lb ? la : lb, lo = la > lb ? lb : la;
        return (hi + 0.05f) / (lo + 0.05f);
    }
    /// The smallest contrast the themes promise between a label and what it sits on.
    static constexpr float kMinContrast = 3.0f;
private:
    /// `fg` when it reads on `bg`, else the theme's text colour, else white or near-black: whichever reads best.
    Color Readable(Color fg, Color bg) const {
        if (Contrast(fg, bg) >= kMinContrast) return fg;
        Color best = text; float bestRatio = Contrast(text, bg);
        const Color ink = Ink(Flat(bg));
        if (Contrast(ink, bg) > bestRatio) { best = ink; bestRatio = Contrast(ink, bg); }
        return best;
    }
    /// White or near-black, whichever reads better on `bg`.
    static Color Ink(Color bg) {
        const Color dark = Color::FromBytes(20, 22, 28);
        return Contrast(Color::White, bg) >= Contrast(dark, bg) ? Color::White : dark;
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
            b->textColor = Readable(buttonText, Mix(Flat(panel), Flat(button), button.a));
            b->hoverTextColor = b->textColor;
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
            // The buttons of a slot grid sit on the grid's own (themed) slot colours, not the button colour.
            if (go->transform && go->transform->Parent() && go->transform->Parent()->gameObject &&
                go->transform->Parent()->gameObject->GetComponent<UISlotGrid>()) {
                b->textColor = text; b->hoverTextColor = text;
            }
        }
        if (auto* stp = go->GetComponent<UIStepper>()) {
            stp->background = Mix(panel, text, 0.10f); stp->background.a = 1.0f;
            stp->button = accent;
            stp->textColor = text;
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
            bar->showPercent = bar->size.y >= 14.0f;   // a thin bar has no room for text
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
            tabs->selectedTextColor = Ink(accent);   // readable on the accent, whatever the look
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
        if (auto* sg = go->GetComponent<UISlotGrid>()) {
            sg->color = Mix(panel, text, 0.12f); sg->color.a = 1.0f;
            sg->hoverColor = Mix(panel, text, 0.22f); sg->hoverColor.a = 1.0f;
            sg->selectedColor = accent;
            sg->textColor = text;
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
        if (auto* sv = go->GetComponent<UIStyleVariant>())
            if (sv->variant > 0 && sv->variant < kVariantCount) ApplyVariant(go, (Variant)sv->variant);
        if (!fontPath.empty()) {
            if (auto* b2 = go->GetComponent<UIButton>()) b2->fontPath = fontPath;
            if (auto* t2 = go->GetComponent<TextRenderer>()) if (t2->screenSpace) t2->fontPath = fontPath;
        }
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

/// Theme assets (.okaytheme): one look in its own small text file, shareable between scenes and
/// projects. Loading keeps any field the file does not mention, and skips fields it cannot read.
std::string UIThemeToText(const UITheme& theme);
bool UIThemeFromText(UITheme& theme, const std::string& text, std::string* error = nullptr);
bool SaveUIThemeAsset(const UITheme& theme, const std::string& path, std::string* error = nullptr);
bool LoadUIThemeAsset(UITheme& theme, const std::string& path, std::string* error = nullptr);

inline void UITheme::ApplyVariant(GameObject* go, Variant v) const {
    if (!go || v == Variant::Default) return;
    const Color clear(0.0f, 0.0f, 0.0f, 0.0f);
    // The role colour: what this variant is "about".
    Color role = accent;
    if (v == Variant::Danger) role = danger;
    else if (v == Variant::Secondary) role = Mix(Flat(panel), text, 0.16f);
    role.a = 1.0f;
    if (auto* b = go->GetComponent<UIButton>()) {
        switch (v) {
            case Variant::Primary: case Variant::Danger: case Variant::Secondary: {
                const Color ink = v == Variant::Secondary ? text : Ink(role);
                b->color = role;
                b->hoverColor = Mix(role, Color::White, 0.18f);
                b->pressedColor = Mix(role, Color(0, 0, 0, 1), 0.28f);
                b->textColor = ink; b->hoverTextColor = ink;
                b->borderWidth = v == Variant::Secondary ? 1.0f : 0.0f;
                break;
            }
            case Variant::Ghost: case Variant::Outline: {
                b->color = clear;
                b->hoverColor = WithAlpha(accent, 0.16f);
                b->pressedColor = WithAlpha(accent, 0.30f);
                b->textColor = Readable(accent, panel); b->hoverTextColor = b->textColor;   // gold on parchment would not read
                b->borderWidth = v == Variant::Outline ? (borderWidth > 2.0f ? borderWidth : 2.0f) : 0.0f;
                b->borderColor = accent;
                b->shadow = false;
                break;
            }
            case Variant::Pill:
                b->shape = UIShape::Pill;
                b->cornerRadius = b->size.y * 0.5f;
                break;
            default: break;
        }
    }
    if (auto* p = go->GetComponent<UIPanel>()) {
        switch (v) {
            case Variant::Primary: case Variant::Danger:
                p->color = role; p->useGradient = false; p->borderWidth = 0.0f; break;
            case Variant::Secondary:
                p->color = Mix(panel, text, 0.10f); p->useGradient = false; break;
            case Variant::Ghost:
                p->color = clear; p->useGradient = false; p->borderWidth = 0.0f; p->shadow = false; p->topHighlight = false; break;
            case Variant::Outline:
                p->color = clear; p->useGradient = false; p->shadow = false; p->topHighlight = false;
                p->borderWidth = borderWidth > 2.0f ? borderWidth : 2.0f; p->borderColor = accent; break;
            case Variant::Pill:
                p->shape = UIShape::Pill; p->cornerRadius = p->size.y * 0.5f; break;
            default: break;
        }
    }
    if (auto* bar = go->GetComponent<UIProgressBar>()) {
        if (v == Variant::Primary || v == Variant::Danger || v == Variant::Secondary) {
            bar->fill = role; bar->fillEnd = role; bar->gradientFill = false;
        } else if (v == Variant::Pill) {
            bar->shape = UIShape::Pill; bar->cornerRadius = bar->size.y * 0.5f;
        }
    }
    if (auto* sl = go->GetComponent<UISlider>())
        if (v == Variant::Primary || v == Variant::Danger || v == Variant::Secondary) sl->fill = role;
    if (auto* tg = go->GetComponent<UIToggle>())
        if (v == Variant::Primary || v == Variant::Danger || v == Variant::Secondary) tg->checkColor = role;
    if (auto* tr = go->GetComponent<TextRenderer>()) {
        if (tr->screenSpace) {
            if (v == Variant::Primary) tr->color = accent;
            else if (v == Variant::Danger) tr->color = danger;
            else if (v == Variant::Secondary) tr->color = muted;
        }
    }
}

} // namespace okay
