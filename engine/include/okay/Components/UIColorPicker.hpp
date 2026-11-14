#pragma once
// grok/ui-color — a row of swatches and a current-color chip, made of ordinary widgets.
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include <algorithm>
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

    int Count() const { return (int)swatches.size(); }
    int Value() const { return value; }
    Color Current() const {
        if (value >= 0 && value < Count()) return swatches[(std::size_t)value];
        return Color::White;
    }
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
    }

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
        const bool changed = i != value;
        value = i;
        Place();
        if (!gameObject) return;
        auto* sc = gameObject->GetComponent<ScriptComponent>();
        if (!sc || !sc->VM()) return;
        if (fromClick) sc->VM()->CallEvent("on_click");
        if (changed) sc->VM()->CallEvent("on_change");
    }
};

} // namespace okay
