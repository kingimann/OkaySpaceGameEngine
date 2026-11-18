#pragma once
// grok/ui-color — a row of swatches and a current-color chip, made of ordinary widgets.
// UI library slice 3 adds an advanced mode: a hue / saturation square (UIColorSquare) with a value slider and an alpha slider
// beside it, made of the same kind of child objects. It is off by default, so a picker made before keeps its look.
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIAppParts.hpp"
#include "okay/Render/ColorHSV.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace okay {

/// A swatch strip. Each swatch is a child button and the current color is a child
/// panel, so this adds no draw kind. Clicking a swatch selects it and fires
/// on_click; a changed selection also fires on_change.
class UIColorPicker final : public Behaviour {
public:
    Vec2 position{32.0f, 32.0f};
    UIAnchor anchor = UIAnchor::TopLeft;
    float swatch = 40.0f;
    float gap = 8.0f;
    int value = 0;
    std::vector<Color> swatches{
        Color::FromBytes(232, 235, 240),
        Color::FromBytes(46, 51, 64),
        Color::FromBytes(226, 82, 82),
        Color::FromBytes(240, 180, 70),
        Color::FromBytes(72, 196, 128),
        Color::FromBytes(72, 196, 196),
        Color::FromBytes(66, 150, 250),
        Color::FromBytes(180, 120, 230),
    };

    // ---- advanced mode (slice 3) ----
    /// Show the hue / saturation square and the value and alpha sliders under the swatch strip.
    bool advanced = false;
    float hue = 210.0f;               ///< degrees, 0 to 360
    float sat = 0.8f;                 ///< 0 to 1
    float val = 0.95f;                ///< 0 to 1 (brightness)
    float alpha = 1.0f;               ///< 0 to 1
    /// True once the square or a slider (or SetHSVA) made a colour of its own; a click on a swatch goes back to the swatches.
    bool useCustom = false;
    float squareSize = 140.0f;        ///< side of the hue / saturation square, in pixels

    int Count() const { return (int)swatches.size(); }
    int Value() const { return value; }
    Color Current() const {
        if (useCustom) return colorhsv::ToColor(hue, sat, val, alpha);
        if (value >= 0 && value < Count()) return swatches[(std::size_t)value];
        return Color::White;
    }
    /// "#RRGGBB" ("#RRGGBBAA" when see-through) of the colour now.
    std::string Hex() const { return colorhsv::Hex(Current()); }
    /// Make a colour of your own from hue (degrees), saturation, value and alpha (0 to 1). Runs on_change.
    void SetHSVA(float h, float s, float v, float a) {
        hue = std::fmod(std::fmod(h, 360.0f) + 360.0f, 360.0f);
        sat = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
        val = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        alpha = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
        useCustom = true;
        Place();
        FireChange();
    }
    /// Make the colour of your own from a colour (the same as SetHSVA with its parts).
    void SetColor(const Color& c) { const colorhsv::HSV h = colorhsv::FromRGB(c.r, c.g, c.b); SetHSVA(h.h, h.s, h.v, c.a); }
    void SetAdvanced(bool on) { advanced = on; Ensure(); Place(); }
    void SetSwatchSize(float px) { swatch = px < 16.0f ? 16.0f : px; }
    void SetGap(float g) { gap = g < 0.0f ? 0.0f : g; }
    void SetSwatch(int i, const Color& c) {
        if (i < 0) return;
        if ((int)swatches.size() <= i) swatches.resize((std::size_t)i + 1, Color::White);
        swatches[(std::size_t)i] = c;
    }
    void SetValue(int i) { Select(i, false); }

    void Rebuild() { Ensure(); Place(); }
    int ExecutionOrder() const override { return 50; }

    void Update(float) override {
        Ensure();
        if (advanced) SyncAdvanced();
        int hit = -1;
        for (int i = 0; i < Count(); ++i) {
            GameObject* g = Named(SwatchName(i));
            auto* b = g ? g->GetComponent<UIButton>() : nullptr;
            if (b && b->WasClicked()) hit = i;
        }
        if (hit >= 0) Select(hit, true);
        else Place();
    }

private:
    static std::string SwatchName(int i) { return "Swatch" + std::to_string(i); }

    GameObject* Named(const std::string& name) const {
        if (!gameObject || !gameObject->transform) return nullptr;
        for (Transform* c : gameObject->transform->Children())
            if (c && c->gameObject && c->gameObject->name == name) return c->gameObject;
        return nullptr;
    }

    void Ensure() {
        if (!gameObject || !gameObject->scene()) return;
        Scene* sc = gameObject->scene();
        const int n = Count();
        std::vector<GameObject*> extra;
        for (Transform* c : gameObject->transform->Children()) {
            if (!c || !c->gameObject) continue;
            const std::string& nm = c->gameObject->name;
            if (nm.rfind("Swatch", 0) != 0 || nm.size() <= 6) continue;
            int idx = 0;
            bool digits = true;
            for (char ch : nm.substr(6)) {
                if (ch < '0' || ch > '9') { digits = false; break; }
                idx = idx * 10 + (ch - '0');
            }
            if (digits && idx >= n) extra.push_back(c->gameObject);
        }
        for (GameObject* g : extra) sc->Destroy(g);
        for (int i = 0; i < n; ++i) {
            if (Named(SwatchName(i))) continue;
            GameObject* g = sc->CreateGameObject(SwatchName(i));
            g->transform->SetParent(gameObject->transform, false);
            g->AddComponent<UIButton>();
        }
        if (!Named("Preview")) {
            GameObject* g = sc->CreateGameObject("Preview");
            g->transform->SetParent(gameObject->transform, false);
            g->AddComponent<UIPanel>();
        }
        static const char* const kAdv[] = {"ColorSquare", "ValueSlider", "AlphaSlider"};
        for (const char* nm : kAdv) {
            GameObject* g = Named(nm);
            if (!advanced) { if (g) sc->Destroy(g); continue; }
            if (g) continue;
            g = sc->CreateGameObject(nm);
            g->transform->SetParent(gameObject->transform, false);
            if (nm[0] == 'C') { g->AddComponent<UIColorSquare>(); }
            else {
                auto* sl = g->AddComponent<UISlider>();
                sl->minValue = 0.0f; sl->maxValue = 1.0f; sl->vertical = true; sl->showValue = false;
                sl->fill = nm[0] == 'V' ? Color::FromBytes(235, 235, 240) : Color::FromBytes(120, 190, 255);
            }
            m_synced = false;   // a new child starts from the picker's numbers
        }
    }

    void FireChange() {
        if (!gameObject) return;
        auto* sc = gameObject->GetComponent<ScriptComponent>();
        if (sc && sc->VM()) sc->VM()->CallEvent("on_change");
    }

    // Advanced mode: the picker's hue, sat, val and alpha are the master. A child that moved since the last frame (the player
    // dragged it) hands its number to the picker; then the picker's numbers are written back to all three children.
    void SyncAdvanced() {
        auto* sq = Named("ColorSquare") ? Named("ColorSquare")->GetComponent<UIColorSquare>() : nullptr;
        auto* vs = Named("ValueSlider") ? Named("ValueSlider")->GetComponent<UISlider>() : nullptr;
        auto* as = Named("AlphaSlider") ? Named("AlphaSlider")->GetComponent<UISlider>() : nullptr;
        bool changed = false;
        if (m_synced) {
            if (sq && (sq->hue != m_pushH || sq->sat != m_pushS)) { hue = sq->hue; sat = sq->sat; changed = true; }
            if (vs && vs->value != m_pushV) { val = vs->value; changed = true; }
            if (as && as->value != m_pushA) { alpha = as->value; changed = true; }
        }
        if (changed) useCustom = true;
        if (sq) { sq->hue = hue; sq->sat = sat; sq->brightness = val; sq->size = {squareSize, squareSize}; }
        if (vs) vs->value = val;
        if (as) as->value = alpha;
        m_pushH = hue; m_pushS = sat; m_pushV = val; m_pushA = alpha;
        m_synced = true;
        if (changed) FireChange();
    }
    bool m_synced = false;
    float m_pushH = 0.0f, m_pushS = 0.0f, m_pushV = 0.0f, m_pushA = 0.0f;

    static Color Lift(Color c) {
        return Color(std::min(1.0f, c.r + 0.08f), std::min(1.0f, c.g + 0.08f),
                     std::min(1.0f, c.b + 0.08f), c.a);
    }

    void Place() {
        const float chip = swatch * 1.6f;
        const float rowW = Count() * swatch + std::max(0, Count() - 1) * gap;
        const Vec2 origin = ResolveAnchor(anchor, position, Vec2{rowW + 16.0f + chip, chip});
        for (int i = 0; i < Count(); ++i) {
            GameObject* g = Named(SwatchName(i));
            auto* b = g ? g->GetComponent<UIButton>() : nullptr;
            if (!b) continue;
            const Color c = swatches[(std::size_t)i];
            b->anchor = UIAnchor::TopLeft;
            b->position = {origin.x + i * (swatch + gap), origin.y + (chip - swatch) * 0.5f};
            b->size = {swatch, swatch};
            b->shape = UIShape::Rounded;
            b->cornerRadius = 8.0f;
            b->fontScale = 1.0f;
            b->focusable = false;
            b->label.clear();
            b->color = c;
            b->hoverColor = Lift(c);
            b->pressedColor = c;
            b->textColor = Color::White;
            b->borderWidth = (i == value) ? 3.0f : 0.0f;
            b->borderColor = Color::White;
        }
        if (advanced) {
            const float y0 = origin.y + chip + 12.0f;
            if (GameObject* g = Named("ColorSquare")) {
                if (auto* q = g->GetComponent<UIColorSquare>()) { q->anchor = UIAnchor::TopLeft; q->position = {origin.x, y0}; q->size = {squareSize, squareSize}; }
            }
            if (GameObject* g = Named("ValueSlider")) {
                if (auto* sl = g->GetComponent<UISlider>()) { sl->anchor = UIAnchor::TopLeft; sl->position = {origin.x + squareSize + 14.0f, y0}; sl->size = {22.0f, squareSize}; }
            }
            if (GameObject* g = Named("AlphaSlider")) {
                if (auto* sl = g->GetComponent<UISlider>()) { sl->anchor = UIAnchor::TopLeft; sl->position = {origin.x + squareSize + 48.0f, y0}; sl->size = {22.0f, squareSize}; }
            }
        }
        if (GameObject* g = Named("Preview")) {
            if (auto* p = g->GetComponent<UIPanel>()) {
                p->anchor = UIAnchor::TopLeft;
                p->position = {origin.x + rowW + 16.0f, origin.y};
                p->size = {chip, chip};
                p->shape = UIShape::Rounded;
                p->cornerRadius = 10.0f;
                p->color = Current();
                p->borderWidth = 1.0f;
                p->borderColor = Color::FromBytes(255, 255, 255, 40);
            }
        }
    }

    void Select(int i, bool fromClick) {
        if (i < 0 || i >= Count()) return;
        const bool changed = i != value || useCustom;
        value = i;
        useCustom = false;
        { const Color c = swatches[(std::size_t)i]; const colorhsv::HSV h = colorhsv::FromRGB(c.r, c.g, c.b); hue = h.h; sat = h.s; val = h.v; alpha = c.a; }
        Place();
        if (!gameObject) return;
        auto* sc = gameObject->GetComponent<ScriptComponent>();
        if (!sc || !sc->VM()) return;
        if (fromClick) sc->VM()->CallEvent("on_click");
        if (changed) sc->VM()->CallEvent("on_change");
    }
};

} // namespace okay
