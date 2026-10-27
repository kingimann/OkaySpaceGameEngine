#pragma once
// grok/ui-hud — a grid of clickable slots made of ordinary buttons. No inventory.
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include <string>
#include <vector>

namespace okay {

/// A rows-by-columns grid of slots. Each slot is a child button (no new draw
/// kind, and nothing here talks to inventory). Clicking a slot selects it and
/// fires on_click; a changed selection also fires on_change.
class UISlotGrid final : public Behaviour {
public:
    Vec2 position{32.0f, 32.0f};
    UIAnchor anchor = UIAnchor::TopLeft;
    int columns = 4;
    int rows = 2;
    Vec2 cell{48.0f, 48.0f};
    float gap = 6.0f;
    int value = -1;   // selected slot, or -1
    Color color = Color::FromBytes(46, 51, 64);
    Color selectedColor = Color::FromBytes(66, 150, 250);
    Color hoverColor = Color::FromBytes(72, 80, 98);
    std::vector<std::string> labels;

    int Count() const { return (columns < 1 ? 1 : columns) * (rows < 1 ? 1 : rows); }
    float Width() const {
        const int c = columns < 1 ? 1 : columns;
        return c * cell.x + (c - 1) * gap;
    }
    float Height() const {
        const int r = rows < 1 ? 1 : rows;
        return r * cell.y + (r - 1) * gap;
    }
    int Value() const { return value; }

    void SetColumns(int n) { columns = n < 1 ? 1 : (n > 16 ? 16 : n); m_dirty = true; }
    void SetRows(int n) { rows = n < 1 ? 1 : (n > 16 ? 16 : n); m_dirty = true; }
    void SetGap(float g) { gap = g < 0.0f ? 0.0f : g; }
    void SetCell(float w, float h) { cell = {w < 8.0f ? 8.0f : w, h < 8.0f ? 8.0f : h}; }
    void SetLabel(int i, const std::string& text) {
        if (i < 0) return;
        if ((int)labels.size() <= i) labels.resize((std::size_t)i + 1);
        labels[(std::size_t)i] = text;
    }
    const std::string& Label(int i) const {
        static const std::string none;
        return (i >= 0 && i < (int)labels.size()) ? labels[(std::size_t)i] : none;
    }
    void SetValue(int i) {
        if (i < 0) { value = -1; Place(); return; }
        Select(i, false);
    }

    /// Build any missing slot buttons. Safe to call from a template before Play.
    void Rebuild() { m_dirty = true; Ensure(); Place(); }

    int ExecutionOrder() const override { return 50; }

    void Update(float) override {
        Ensure();
        int hit = -1;
        for (int i = 0; i < Count(); ++i) {
            GameObject* g = SlotObject(i);
            auto* b = g ? g->GetComponent<UIButton>() : nullptr;
            if (b && b->WasClicked()) hit = i;
        }
        if (hit >= 0) Select(hit, true);
        else Place();
    }

private:
    static std::string SlotName(int i) { return "Slot" + std::to_string(i); }

    GameObject* SlotObject(int i) const {
        if (!gameObject || !gameObject->transform) return nullptr;
        const std::string name = SlotName(i);
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
            if (nm.rfind("Slot", 0) != 0 || nm.size() <= 4) continue;
            int idx = 0;
            bool digits = true;
            for (char ch : nm.substr(4)) {
                if (ch < '0' || ch > '9') { digits = false; break; }
                idx = idx * 10 + (ch - '0');
            }
            if (digits && idx >= n) extra.push_back(c->gameObject);
        }
        for (GameObject* g : extra) sc->Destroy(g);
        for (int i = 0; i < n; ++i) {
            if (SlotObject(i)) continue;
            GameObject* g = sc->CreateGameObject(SlotName(i));
            g->transform->SetParent(gameObject->transform, false);
            g->AddComponent<UIButton>();
        }
        m_dirty = false;
    }

    void Place() {
        const Vec2 origin = ResolveAnchor(anchor, position, Vec2{Width(), Height()});
        const int cols = columns < 1 ? 1 : columns;
        for (int i = 0; i < Count(); ++i) {
            GameObject* g = SlotObject(i);
            auto* b = g ? g->GetComponent<UIButton>() : nullptr;
            if (!b) continue;
            const int col = i % cols, row = i / cols;
            b->anchor = UIAnchor::TopLeft;
            b->position = {origin.x + col * (cell.x + gap), origin.y + row * (cell.y + gap)};
            b->size = cell;
            b->shape = UIShape::Rounded;
            b->cornerRadius = 8.0f;
            b->fontScale = 1.5f;
            b->focusable = false;
            b->label = Label(i).empty() ? std::to_string(i + 1) : Label(i);
            b->color = (i == value) ? selectedColor : color;
            b->hoverColor = hoverColor;
            b->pressedColor = selectedColor;
            b->textColor = Color::White;
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

    bool m_dirty = true;
};

} // namespace okay
