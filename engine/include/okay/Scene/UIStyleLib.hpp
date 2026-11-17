#pragma once
// The UI style and theme library (UI library slice 4). Two registries, both read from data (assets/ui/ui_styles.okaydata and
// ui_themes.okaydata, embedded into the engine), so a new look is a few lines of data and not a new class.
//
//   Style  = a named mix of choices: SHAPE (square, rounded, pill, chamfered, bevelled, hexagon...), FILL (solid, gradient, glass, ghost,
//            tint, gloss...), BORDER (hairline to thick, double, glow), EFFECT (soft, hard or long shadow, glow, neon, emboss, lift),
//            STATE LOOKS (hover, pressed, disabled, focus), a SIZE (XS to XL) and a DENSITY (compact to spacious).
//            ApplyStyle() sets the matching fields of the buttons, panels, text fields, dropdowns, bars and so on under an object.
//   Look   = a whole look for ONE screen or ONE widget: colours, corner radius, spacing, animation speed and sound names (54 of them: the
//            twelve built-in looks and 42 more). There is no global theme: ApplyLook() puts a look on the screen or widget you choose
//            (script: ui_set_look("Pause Menu", "Neon")), so a retro menu, a sci-fi HUD and a cartoon popup can live in one scene. The
//            choice is stored on the object (UI Style Variant: themeName = the look, style, styleSize, styleDensity) and saved with it.
// Written for this engine from its own UI components; no other project's styles, themes or code were used.
#include "okay/Scene/UITemplates.hpp"
#include "okay/Components/UITheme.hpp"
#include "okay/Components/UILayoutGroup.hpp"
#include "okay/UI/UILibData.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <fstream>
#include <sstream>
#include <map>
#include <string>
#include <vector>

namespace okay { namespace UIStyleLib {

// ------------------------------------------------------------------ colours
inline Color Hex(const std::string& s, Color def = Color::White) {
    if (s.size() < 7 || s[0] != '#') return def;
    auto h = [&](std::size_t i) { return (int)std::strtol(s.substr(i, 2).c_str(), nullptr, 16); };
    return Color::FromBytes(h(1), h(3), h(5), s.size() >= 9 ? h(7) : 255);
}
inline Color Mix(Color a, Color b, float t) { return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t); }
inline Color Lighten(Color c, float t) { return t >= 0 ? Mix(c, Color(1, 1, 1, c.a), t) : Mix(c, Color(0, 0, 0, c.a), -t); }
inline Color Alpha(Color c, float a) { c.a *= a; return c; }
inline Color Ink(Color bg) {   // white or near-black, whichever reads better
    return UITheme::Contrast(Color::White, bg) >= UITheme::Contrast(Color::FromBytes(18, 20, 26), bg) ? Color::White : Color::FromBytes(18, 20, 26);
}

// ------------------------------------------------------------------ the style registry
struct StyleRegistry {
    std::map<std::string, std::vector<uilib::DataSection>> axes;   // axis -> members (in file order)
    std::vector<uilib::DataSection> styles;
};
inline const char* const kAxes[] = {"shape", "fill", "border", "effect", "size", "density", "hover", "press", "disabled", "focus"};
inline StyleRegistry ParseStyles(const std::string& text) {
    StyleRegistry r;
    for (uilib::DataSection& s : uilib::ParseData(text)) {
        if (s.kind == "style") { r.styles.push_back(std::move(s)); continue; }
        for (const char* a : kAxes) if (s.kind == a) { r.axes[a].push_back(std::move(s)); break; }
    }
    return r;
}
inline const StyleRegistry& Styles() { static const StyleRegistry r = ParseStyles(uilib::EmbeddedData("ui_styles")); return r; }
inline const uilib::DataSection* Member(const std::string& axis, const std::string& name) {
    auto it = Styles().axes.find(axis);
    if (it == Styles().axes.end()) return nullptr;
    for (const auto& m : it->second) if (m.name == name) return &m;
    return nullptr;
}
inline const uilib::DataSection* FindStyle(const std::string& name) {
    for (const auto& s : Styles().styles) if (s.name == name) return &s;
    return nullptr;
}
inline std::size_t AxisCount(const char* axis) { auto it = Styles().axes.find(axis); return it == Styles().axes.end() ? 0 : it->second.size(); }
/// How many looks the five main axes can make together (shape x fill x border x effect). A count of combinations, not of hand-made styles.
inline std::size_t CombinationCount() { return AxisCount("shape") * AxisCount("fill") * AxisCount("border") * AxisCount("effect"); }

/// One chosen look. `style` fills the first eight from the registry; size and density are separate choices.
struct StyleSpec {
    std::string style;
    std::string shape = "rounded", fill = "solid", border = "none", effect = "none", role = "accent";
    std::string hover = "lighten", press = "darken", disabled = "fade", focus = "ring";
    std::string size = "m", density = "cosy";
};
inline StyleSpec SpecOf(const std::string& styleName, const std::string& size = "m", const std::string& density = "cosy") {
    StyleSpec sp; sp.size = size; sp.density = density;
    if (const uilib::DataSection* s = FindStyle(styleName)) {
        sp.style = styleName;
        sp.shape = s->Str("shape", sp.shape); sp.fill = s->Str("fill", sp.fill); sp.border = s->Str("border", sp.border);
        sp.effect = s->Str("effect", sp.effect); sp.role = s->Str("role", sp.role); sp.hover = s->Str("hover", sp.hover);
        sp.press = s->Str("press", sp.press); sp.disabled = s->Str("disabled", sp.disabled); sp.focus = s->Str("focus", sp.focus);
    }
    return sp;
}

/// The colours a style draws from: a theme's, or the launcher's Slate palette when the scene has no theme.
struct Palette { Color accent, text, panel, danger, success, warning, muted; };
inline Palette PaletteOf(const UITheme* th) {
    namespace slate = UITemplates::slate;
    Palette p{slate::Accent(), slate::Text(), slate::Panel(), slate::Danger(), slate::Success(), slate::Warning(), slate::Dim()};
    if (th) { p.accent = th->accent; p.text = th->text; p.panel = th->panel; p.danger = th->danger; p.success = th->success; p.muted = th->muted; }
    return p;
}
inline Color RoleColor(const Palette& p, const std::string& role) {
    Color c = p.accent;
    if (role == "danger") c = p.danger; else if (role == "success") c = p.success; else if (role == "warning") c = p.warning;
    else if (role == "neutral") c = Mix(Color(p.panel.r, p.panel.g, p.panel.b, 1), p.text, 0.22f);
    c.a = 1.0f;
    return c;
}

inline int ShapeFromName(const std::string& n) {
    auto sq = [](const std::string& s) { std::string o; for (char c : s) if (c != ' ' && c != '-' && c != '(' && c != ')') o.push_back((char)std::tolower((unsigned char)c)); return o; };
    for (int i = 0; i < kUIShapeCount; ++i) if (sq(UIShapeName(i)) == sq(n)) return i;
    return (int)UIShape::Rounded;
}

namespace detail {
struct Look {
    UIShape shape = UIShape::Rounded; float radius = 8;
    Color fill, fillBottom, text, hover, pressed, disabled, border, outline;
    bool gradient = false, horizontal = false, highlight = false, shadow = false, glow = false;
    float borderW = 0, outlineW = 0, shadowSoft = 0; Vec2 shadowOff{0, 0}; Color shadowCol;
    float hoverScale = 1, pressOffset = 1, focusW = 2; Color focus;
    float height = 36, font = 2;
};
inline float N(const uilib::DataSection* s, const char* k, float d) { return s ? s->Num(k, d) : d; }
inline Look Compute(const StyleSpec& sp, const Palette& pal) {
    Look L;
    const uilib::DataSection *sh = Member("shape", sp.shape), *fl = Member("fill", sp.fill), *bd = Member("border", sp.border),
        *ef = Member("effect", sp.effect), *sz = Member("size", sp.size), *hv = Member("hover", sp.hover), *pr = Member("press", sp.press),
        *ds = Member("disabled", sp.disabled), *fc = Member("focus", sp.focus);
    const Color base = RoleColor(pal, sp.role);
    L.shape = (UIShape)ShapeFromName(sh ? sh->Str("shape", "Rounded") : "Rounded");
    L.radius = N(sh, "radius", 8) * N(sz, "radius_scale", 1);
    L.height = N(sz, "height", 36); L.font = N(sz, "font", 2);
    const std::string mode = fl ? fl->Str("mode", "solid") : "solid";
    const float g = N(fl, "gradient", 0), fa = N(fl, "alpha", 1);
    L.fill = base; L.fillBottom = base; L.text = Ink(base);
    const Color panelFlat(pal.panel.r, pal.panel.g, pal.panel.b, 1.0f);
    if (mode == "gradient" || mode == "gloss" || mode == "gradienth") {
        L.gradient = true; L.horizontal = mode == "gradienth"; L.highlight = mode == "gloss";
        L.fill = Lighten(base, g * 0.6f); L.fillBottom = Lighten(base, -g * 0.7f);
    } else if (mode == "glass") {
        L.fill = Alpha(Mix(base, Color(1, 1, 1, 1), 0.25f), fa); L.highlight = true; L.text = pal.text;
    } else if (mode == "ghost") {
        L.fill = Alpha(base, 0.0f); L.text = Mix(base, pal.text, 0.25f);
    } else if (mode == "tint") {
        L.fill = Alpha(base, fa); L.text = Mix(base, pal.text, 0.35f);
    } else if (mode == "inset") {
        L.fill = Mix(panelFlat, Color(0, 0, 0, 1), 0.35f); L.text = pal.text;
    } else if (mode == "dark") {
        L.fill = Lighten(base, -0.62f); L.text = Mix(base, pal.text, 0.55f);
    } else if (mode == "light") {
        L.fill = Lighten(base, 0.86f); L.text = Color::FromBytes(24, 26, 32);
    }
    const Color ref = L.fill.a < 0.15f ? panelFlat : L.fill;
    if (L.text.a >= 0.99f && UITheme::Contrast(L.text, ref) < 3.0f) L.text = Ink(ref);
    L.borderW = N(bd, "width", 0); L.outlineW = N(bd, "outer", 0);
    const float ba = N(bd, "alpha", 1);
    L.border = sp.border == "dark-line" ? Color(0, 0, 0, 0.85f) : sp.border == "hairline" ? Alpha(pal.text, 0.28f * 1.0f) : Alpha(base, ba);
    L.outline = sp.border == "glow" ? Alpha(base, 0.28f) : Lighten(base, 0.35f);
    if (sp.border == "glow") { L.outlineW = N(bd, "outer", 6); }
    if (N(ef, "shadow", 0) > 0) {
        L.shadow = true; L.shadowSoft = N(ef, "soft", 0); L.shadowOff = {N(ef, "dx", 0), N(ef, "dy", 0)};
        L.glow = N(ef, "glow", 0) > 0;
        L.shadowCol = L.glow ? Alpha(base, 0.65f) : Color(0, 0, 0, 0.45f);
    }
    if (N(ef, "highlight", 0) > 0) L.highlight = true;
    if (sp.effect == "neon") { L.borderW = std::max(L.borderW, 1.5f); L.border = base; }
    L.hoverScale = N(hv, "scale", 1) * N(ef, "hover_scale", 1);
    const float hl = N(hv, "lighten", 0.18f);
    L.hover = L.fill.a < 0.15f ? Alpha(base, 0.16f) : (mode == "tint" ? Alpha(base, std::min(1.0f, fa + 0.14f)) : Lighten(L.fill, hl));
    L.hover.a = std::max(L.hover.a, L.fill.a);
    const float dk = N(pr, "darken", 0.2f);
    L.pressed = L.fill.a < 0.15f ? Alpha(base, 0.3f) : Lighten(L.fill, -dk);
    L.pressOffset = N(pr, "offset", 1); L.hoverScale *= N(pr, "scale", 1) < 1 ? 1.0f : 1.0f;
    const float da = N(ds, "alpha", 0.4f);
    L.disabled = N(ds, "grey", 0) > 0 ? Mix(Color(0.5f, 0.5f, 0.5f, 1), panelFlat, 0.4f) : Alpha(Mix(L.fill.a < 0.15f ? panelFlat : L.fill, panelFlat, 0.55f), std::max(0.4f, da));
    L.focusW = N(fc, "width", 2);
    L.focus = Lighten(base, 0.3f);
    return L;
}
} // namespace detail

/// Apply a style to one object's widgets (not its children). Returns how many widgets it touched.
inline int ApplyStyleOne(GameObject* go, const StyleSpec& sp, const Palette& pal) {
    if (!go) return 0;
    const detail::Look L = detail::Compute(sp, pal);
    int n = 0;
    if (auto* b = go->GetComponent<UIButton>()) {
        b->shape = L.shape; b->cornerRadius = L.radius;
        b->color = L.fill; b->hoverColor = L.hover; b->pressedColor = L.pressed; b->disabledColor = L.disabled;
        b->textColor = L.text; b->hoverTextColor = L.fill.a < 0.15f ? Lighten(L.text, 0.2f) : L.text;
        b->borderWidth = L.borderW + (L.outlineW > 0 && sp.border == "double" ? 2.0f : 0.0f); b->borderColor = L.border;
        b->shadow = L.shadow; b->shadowColor = L.shadowCol; b->shadowOffset = L.shadowOff; b->shadowSoftness = L.shadowSoft;
        b->hoverScale = L.hoverScale; b->pressOffset = L.pressOffset;
        b->separateFocus = true; b->focusColor = L.focus;
        b->size.y = L.height; b->fontScale = L.font;
        ++n;
    }
    if (auto* p = go->GetComponent<UIPanel>()) {
        p->shape = L.shape; p->cornerRadius = L.radius;
        p->color = L.fill; p->useGradient = L.gradient; p->colorBottom = L.fillBottom;
        p->gradientDir = L.horizontal ? UIPanel::GradientDir::Horizontal : UIPanel::GradientDir::Vertical;
        p->borderWidth = L.borderW; p->borderColor = L.border;
        p->outlineWidth = L.outlineW; p->outlineColor = L.outline;
        p->topHighlight = L.highlight;
        p->shadow = L.shadow; p->shadowColor = L.shadowCol; p->shadowOffset = L.shadowOff; p->shadowSoftness = L.shadowSoft;
        ++n;
    }
    if (auto* in = go->GetComponent<UIInputField>()) {
        in->shape = L.shape; in->cornerRadius = L.radius; in->color = L.fill.a < 0.15f ? Alpha(pal.panel, 0.6f) : Lighten(L.fill, -0.35f);
        in->focusColor = Lighten(in->color, 0.1f); in->borderColor = L.focus; in->borderWidth = std::max(1.0f, L.borderW * 0.5f);
        in->size.y = L.height; ++n;
    }
    if (auto* d = go->GetComponent<UIDropdown>()) {
        d->shape = L.shape; d->cornerRadius = L.radius; d->color = L.fill.a < 0.15f ? Alpha(pal.panel, 0.6f) : Lighten(L.fill, -0.35f);
        d->borderColor = L.border; d->size.y = L.height; ++n;
    }
    if (auto* bar = go->GetComponent<UIProgressBar>()) {
        bar->shape = L.shape; bar->cornerRadius = std::min(L.radius, bar->size.y * 0.5f); bar->fill = RoleColor(pal, sp.role); ++n;
    }
    if (auto* t = go->GetComponent<UIToggle>()) { t->checkColor = RoleColor(pal, sp.role); ++n; }
    if (auto* s = go->GetComponent<UISlider>()) { s->fill = RoleColor(pal, sp.role); ++n; }
    if (auto* tb = go->GetComponent<UITabs>()) { tb->selected = RoleColor(pal, sp.role); tb->shape = L.shape; tb->cornerRadius = L.radius; ++n; }
    if (auto* lg = go->GetComponent<UILayoutGroup>()) {
        if (const uilib::DataSection* d = Member("density", sp.density)) { lg->spacing = d->Num("spacing", lg->spacing); lg->padding = d->Num("padding", lg->padding); ++n; }
    }
    return n;
}
/// Parts of a screen that a whole-screen style leaves alone, because giving them a card's shape and fill would spoil them: icon pictures (buttons that
/// cannot be clicked), round panels (avatars, the map), tiny panels (badges, dividers), see-through containers (tab pages), full-screen backdrops.
inline bool StyleSkips(GameObject* go) {
    if (auto* b = go->GetComponent<UIButton>()) if (!b->interactable) return true;
    if (auto* p = go->GetComponent<UIPanel>()) {
        if (AnchorIsStretch(p->anchor) || p->color.a < 0.02f) return true;
        if (p->shape == UIShape::Circle) return true;
        if (std::min(p->size.x, p->size.y) <= 24.0f) return true;
    }
    return false;
}
/// The same, for `root` and everything under it (the parts StyleSkips names are left as they are; `root` itself is always styled).
inline int ApplyStyle(GameObject* root, const StyleSpec& sp, const Palette& pal, bool isRoot = true) {
    if (!root) return 0;
    int n = (isRoot || !StyleSkips(root)) ? ApplyStyleOne(root, sp, pal) : 0;
    if (root->transform)
        for (Transform* c : root->transform->Children()) if (c && c->gameObject) n += ApplyStyle(c->gameObject, sp, pal, false);
    return n;
}

/// A tiny deterministic random number (so "randomise" is repeatable from a seed).
inline unsigned Next(unsigned& s) { s = s * 1664525u + 1013904223u; return s >> 8; }
/// Pick a random style from the registry for `root` (seeded), apply it under the theme's colours. Returns the style name.
inline std::string RandomiseStyle(GameObject* root, unsigned seed, const Palette& pal) {
    unsigned s = seed * 2654435761u + 12345u;
    if (Styles().styles.empty()) return std::string();
    const uilib::DataSection& st = Styles().styles[Next(s) % Styles().styles.size()];
    StyleSpec sp = SpecOf(st.name);
    ApplyStyle(root, sp, pal);
    return st.name;
}

// ------------------------------------------------------------------ the theme registry
struct ThemeInfo { std::string name, mode, tags; bool builtin = false; const uilib::DataSection* data = nullptr; };
inline const std::vector<uilib::DataSection>& ThemeSections() {
    static const std::vector<uilib::DataSection> v = [] {
        std::vector<uilib::DataSection> r;
        for (auto& s : uilib::ParseData(uilib::EmbeddedData("ui_themes"))) if (s.kind == "theme") r.push_back(std::move(s));
        return r;
    }();
    return v;
}
inline std::vector<ThemeInfo> Themes() {
    std::vector<ThemeInfo> v;
    for (const auto& s : ThemeSections()) {
        ThemeInfo t; t.name = s.name; t.tags = s.Str("tags"); t.data = &s;
        t.builtin = s.Get("preset") != nullptr;
        if (t.builtin) {
            UITheme tmp; tmp.ApplyPreset(UITheme::PresetFromName(s.Str("preset")));
            t.mode = (tmp.panel.r + tmp.panel.g + tmp.panel.b) / 3.0f > 0.5f ? "light" : "dark";
        } else t.mode = s.Str("mode", "dark");
        v.push_back(std::move(t));
    }
    return v;
}
inline const uilib::DataSection* FindTheme(const std::string& name) {
    const std::string key = [&] { std::string o; for (char c : name) if (c != ' ' && c != '-' && c != '_') o.push_back((char)std::tolower((unsigned char)c)); return o; }();
    for (const auto& s : ThemeSections()) {
        std::string o; for (char c : s.name) if (c != ' ' && c != '-' && c != '_') o.push_back((char)std::tolower((unsigned char)c));
        if (o == key) return &s;
    }
    return nullptr;
}

/// Fill a UITheme from a registry theme (a built-in preset or a data theme). false for an unknown name.
inline bool ApplyThemeData(UITheme& th, const std::string& name) {
    const uilib::DataSection* s = FindTheme(name);
    if (!s) return false;
    if (const std::string* p = s->Get("preset")) { th.ApplyPreset(UITheme::PresetFromName(*p)); th.themeName = s->name; return true; }
    th.preset = (int)UITheme::Preset::Custom; th.themeName = s->name;
    th.panel = Hex(s->Str("panel"), th.panel); th.panelBottom = Hex(s->Str("panelBottom"), th.panelBottom);
    th.accent = Hex(s->Str("accent"), th.accent); th.text = Hex(s->Str("text"), th.text); th.muted = Hex(s->Str("muted"), th.muted);
    th.danger = Hex(s->Str("danger"), th.danger); th.success = Hex(s->Str("success"), th.success);
    th.button = th.accent; th.buttonHover = Lighten(th.accent, 0.2f); th.buttonPressed = Lighten(th.accent, -0.22f);
    th.buttonText = Ink(th.accent);
    th.disabled = Mix(Color(th.panel.r, th.panel.g, th.panel.b, 1), th.text, 0.12f);
    th.border = Alpha(th.text, 0.18f);
    th.shadowCol = Color(0, 0, 0, 0.55f);
    th.glowColor = Alpha(th.accent, 0.7f);
    th.focusColor = Lighten(th.accent, 0.25f); th.focusThickness = 2.0f + s->Num("borderWidth", 1) * 0.0f; th.focusPulse = s->Num("glow", 0) > 0;
    th.cornerRadius = s->Num("radius", 12); th.cornerMask = UICornerAll;
    th.borderWidth = s->Num("borderWidth", 1); th.gradient = true;
    th.shadow = s->Num("shadow", 10) > 0; th.shadowSoftness = s->Num("shadow", 10); th.shadowOffset = {0.0f, th.shadow ? 6.0f : 0.0f};
    th.glow = s->Num("glow", 0); th.sheen = s->Num("glow", 0) > 0;
    th.padding = s->Num("padding", 16); th.fontScale = s->Num("fontScale", 2); th.hoverScale = s->Num("hoverScale", 1.03f);
    th.transitionSpeed = s->Num("speed", 12);
    const std::string font = s->Str("font", "dejavu");
    // `font` names which shipped font the theme wants ("pixel" = the built-in 8x8 font, "dejavu" = DejaVu Sans). It is not written into fontPath:
    // that is a project file path, and the shipped font is not a file of the player's project. The built-in font is used.
    th.fontPath.clear();
    return true;
}

/// The name of a sound a look uses: kind is "hover", "click" or "open". Empty when the look names none.
inline std::string ThemeSound(const std::string& themeName, const std::string& kind) {
    const uilib::DataSection* s = FindTheme(themeName);
    if (!s) return std::string();
    std::string k = kind; if (!k.empty()) k[0] = (char)std::toupper((unsigned char)k[0]);
    return s->Str("sound" + k);
}

/// The colours a widget's style draws from: its own look (UIStyleVariant.themeName), else the launcher's Slate palette. Never a scene-wide theme.
inline Palette PaletteFor(const UIStyleVariant* sv) {
    if (sv && !sv->themeName.empty() && FindTheme(sv->themeName)) { UITheme tmp; ApplyThemeData(tmp, sv->themeName); return PaletteOf(&tmp); }
    return PaletteOf(nullptr);
}

/// Put a look (one of the 54, by name; "" = keep the colours) and/or a named style on `root` and everything under it. The choice is stored on
/// `root` (UI Style Variant) so it is saved and can be re-applied. Nothing outside `root` changes. false for an unknown look or style.
inline bool ApplyLook(GameObject* root, const std::string& look, const std::string& style = std::string(), const std::string& size = std::string(),
                      const std::string& density = std::string()) {
    if (!root) return false;
    const uilib::DataSection* ls = look.empty() ? nullptr : FindTheme(look);
    if ((!look.empty() && !ls) || (!style.empty() && !FindStyle(style))) return false;
    auto* sv = root->GetComponent<UIStyleVariant>();
    if (!sv) sv = root->AddComponent<UIStyleVariant>();
    if (ls) {
        sv->themeName = ls->name;
        // The look recolours every panel and button under `root`. Icon pictures (buttons that cannot be clicked) and see-through containers (tab pages,
        // a crosshair frame) must keep what they had, so their fields are saved first and put back after.
        std::vector<std::function<void()>> restore;
        std::function<void(GameObject*)> keep = [&](GameObject* g) {
            if (auto* b = g->GetComponent<UIButton>(); b && !b->interactable) {
                const Color col = b->color, hov = b->hoverColor, prs = b->pressedColor, dis = b->disabledColor, tx = b->textColor, htx = b->hoverTextColor;
                const float bw = b->borderWidth, hs = b->hoverScale; const bool sh = b->shadow; const UIShape shp = b->shape;
                restore.push_back([=] { b->color = col; b->hoverColor = hov; b->pressedColor = prs; b->disabledColor = dis; b->textColor = tx; b->hoverTextColor = htx; b->borderWidth = bw; b->hoverScale = hs; b->shadow = sh; b->shape = shp; });
            }
            if (auto* p = g->GetComponent<UIPanel>(); p && p->color.a < 0.02f) {
                const Color col = p->color; const bool gr = p->useGradient, sh = p->shadow, hl = p->topHighlight; const float bw = p->borderWidth, ow = p->outlineWidth;
                restore.push_back([=] { p->color = col; p->useGradient = gr; p->shadow = sh; p->topHighlight = hl; p->borderWidth = bw; p->outlineWidth = ow; });
            }
            for (Transform* c : g->transform->Children()) if (c && c->gameObject) keep(c->gameObject);
        };
        keep(root);
        UITheme tmp; ApplyThemeData(tmp, ls->name); tmp.ApplyTo(root);
        for (auto& r : restore) r();
    }
    sv->style = style; sv->styleSize = size; sv->styleDensity = density;
    if (!style.empty()) ApplyStyle(root, SpecOf(style, size.empty() ? "m" : size, density.empty() ? "cosy" : density), PaletteFor(sv));
    return true;
}
/// Apply again what an object stores (after its look or style fields were edited).
inline bool ReapplyLook(GameObject* root) {
    auto* sv = root ? root->GetComponent<UIStyleVariant>() : nullptr;
    if (!sv || (sv->themeName.empty() && sv->style.empty())) return false;
    return ApplyLook(root, sv->themeName, sv->style, sv->styleSize, sv->styleDensity);
}

/// The topmost UI object of a screen: the ancestor just under the Canvas (or the object itself when it has no UI parent).
inline GameObject* ScreenRootOf(GameObject* go) {
    GameObject* cur = go;
    while (cur && cur->transform && cur->transform->Parent() && cur->transform->Parent()->gameObject && !cur->transform->Parent()->gameObject->GetComponent<Canvas>())
        cur = cur->transform->Parent()->gameObject;
    return cur;
}

/// The preset files in a folder: every .okaylook and .okaytheme path, sorted by file name (for the Inspector's preset list).
inline std::vector<std::string> ListPresetFiles(const std::string& dir) {
    std::vector<std::string> v;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        const std::string ext = e.path().extension().string();
        if (e.is_regular_file(ec) && (ext == ".okaylook" || ext == ".okaytheme")) v.push_back(e.path().string());
    }
    std::sort(v.begin(), v.end());
    return v;
}
/// A look and style saved to a small text file (.okaylook), so a favourite combination can be shared between projects.
struct LookPreset { std::string look, style, size, density; };
inline std::string PresetToText(const LookPreset& p) {
    return "okaylook 1\nlook=" + p.look + "\nstyle=" + p.style + "\nsize=" + p.size + "\ndensity=" + p.density + "\n";
}
inline bool PresetFromText(const std::string& text, LookPreset& out) {
    const auto lines = uilib::Split(text, '\n');
    if (lines.empty() || lines[0].rfind("okaylook", 0) != 0) return false;
    LookPreset p; bool any = false;
    for (const std::string& l : lines) {
        const std::size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = l.substr(0, eq), v = uilib::Trim(l.substr(eq + 1));
        if (k == "look") { p.look = v; any = true; } else if (k == "style") { p.style = v; any = true; } else if (k == "size") p.size = v; else if (k == "density") p.density = v;
    }
    if (!any) return false;
    out = p; return true;
}
inline bool SavePreset(const std::string& path, const LookPreset& p) {
    std::ofstream f(path, std::ios::binary); if (!f) return false;
    f << PresetToText(p); return (bool)f;
}
inline bool LoadPreset(const std::string& path, LookPreset& out) {
    std::ifstream f(path, std::ios::binary); if (!f) return false;
    std::stringstream ss; ss << f.rdbuf(); return PresetFromText(ss.str(), out);
}
/// An old .okaytheme file (UI Theme asset) used as a look for one screen or widget. false when the file cannot be read.
inline bool ApplyThemeFileAsLook(GameObject* root, const std::string& path) {
    UITheme tmp; if (!root || !LoadUIThemeAsset(tmp, path)) return false;
    tmp.ApplyTo(root); return true;
}

/// Apply a preset file (.okaylook or .okaytheme) from ListPresetFiles to one screen or widget. false when it cannot be read or names an unknown look.
inline bool ApplyPresetFile(GameObject* root, const std::string& path) {
    if (path.size() >= 9 && path.compare(path.size() - 9, 9, ".okaylook") == 0) {
        LookPreset p;
        return LoadPreset(path, p) && ApplyLook(root, p.look, p.style, p.size, p.density);
    }
    return ApplyThemeFileAsLook(root, path);
}

}} // namespace okay::UIStyleLib
