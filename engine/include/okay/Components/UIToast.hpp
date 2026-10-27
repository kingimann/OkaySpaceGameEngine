#pragma once
// A stack of toast notifications. Each toast is an ordinary button card (plus a
// label and an icon), so this adds no new draw kind and does not change how
// existing widgets paint. Live toasts are not saved; only the host's settings are.
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace okay {

/// One live toast card. The UIToast host owns the clock, the slide and the stack.
class UIToastItem final : public Behaviour {
public:
    int kind = 0;          // 0 info, 1 success, 2 warning, 3 error
    float age = 0.0f;
    float life = 4.0f;
    int serial = 0;
};

/// Host for toast notifications. Push() slides a card in at `corner`, stacks up
/// to `maxStack`, and removes it when the time runs out or the player clicks it.
/// A click also fires this object's on_click(). Script: toast_push(title, body, kind).
class UIToast final : public Behaviour {
public:
    /// 0 top-right, 1 top-left, 2 bottom-right, 3 bottom-left, 4 top-center.
    int corner = 0;
    float duration = 4.0f;
    int maxStack = 4;
    float margin = 24.0f;
    float gap = 8.0f;
    float width = 320.0f;
    float height = 72.0f;
    bool showIcon = true;

    void SetDuration(float seconds) { duration = std::max(0.05f, seconds); }
    void SetCorner(int c) { corner = std::clamp(c, 0, 4); }
    void SetMaxStack(int n) { maxStack = std::max(1, n); }
    int Count() const;

    /// Show a toast. kind: 0 info, 1 success, 2 warning, 3 error.
    GameObject* Push(const std::string& title, const std::string& body, int kind);

    void Update(float dt) override;
    /// After ordinary widgets, so a click on a toast is visible the same frame.
    int ExecutionOrder() const override { return 50; }

private:
    void Place(UIButton* b, int index, float slide) const;
    void FireClick();
    int m_serial = 0;
};

inline int UIToast::Count() const {
    if (!gameObject || !gameObject->transform) return 0;
    int n = 0;
    for (Transform* ch : gameObject->transform->Children())
        if (ch && ch->gameObject && ch->gameObject->GetComponent<UIToastItem>()) ++n;
    return n;
}

inline void UIToast::Place(UIButton* b, int index, float slide) const {
    const float stack = margin + (float)index * (height + gap);
    const float off = slide * (width + margin);
    b->size = {width, height};
    switch (corner) {
        case 1: b->anchor = UIAnchor::TopLeft;     b->position = {margin - off, stack}; break;
        case 2: b->anchor = UIAnchor::BottomRight; b->position = {-margin + off, -stack}; break;
        case 3: b->anchor = UIAnchor::BottomLeft;  b->position = {margin - off, -stack}; break;
        case 4: b->anchor = UIAnchor::TopCenter;   b->position = {0.0f, stack - slide * (height + margin)}; break;
        default: b->anchor = UIAnchor::TopRight;   b->position = {-margin + off, stack}; break;
    }
}

inline void UIToast::FireClick() {
    if (!gameObject) return;
    if (auto* sc = gameObject->GetComponent<ScriptComponent>())
        if (sc->VM()) sc->VM()->CallEvent("on_click");
}

inline GameObject* UIToast::Push(const std::string& title, const std::string& body, int kind) {
    if (!gameObject || !gameObject->scene()) return nullptr;
    Scene* sc = gameObject->scene();
    kind = std::clamp(kind, 0, 3);
    SetMaxStack(maxStack);
    while (Count() >= maxStack) {
        UIToastItem* oldest = nullptr;
        GameObject* og = nullptr;
        for (Transform* ch : gameObject->transform->Children()) {
            if (!ch || !ch->gameObject) continue;
            auto* item = ch->gameObject->GetComponent<UIToastItem>();
            if (!item) continue;
            if (!oldest || item->serial < oldest->serial) { oldest = item; og = ch->gameObject; }
        }
        if (!og) break;
        sc->Destroy(og);
    }

    const Color accent = kind == 1 ? Color::FromBytes(72, 196, 128)
                       : kind == 2 ? Color::FromBytes(240, 180, 70)
                       : kind == 3 ? Color::FromBytes(226, 82, 82)
                                   : Color::FromBytes(66, 150, 250);
    const char* glyph = kind == 1 ? "+" : kind == 2 ? "!" : kind == 3 ? "x" : "i";

    GameObject* card = sc->CreateGameObject("Toast");
    card->transform->SetParent(gameObject->transform, false);
    auto* btn = card->AddComponent<UIButton>();
    btn->label.clear();
    btn->size = {width, height};
    btn->shape = UIShape::Rounded;
    btn->cornerRadius = 12.0f;
    btn->fontScale = 2.0f;
    btn->hoverScale = 1.0f;
    btn->focusable = false;
    btn->color = Color::FromBytes(28, 31, 38, 242);
    btn->hoverColor = Color::FromBytes(40, 44, 54, 248);
    btn->pressedColor = Color::FromBytes(20, 22, 28, 248);
    btn->textColor = Color::White;
    btn->borderWidth = 1.0f;
    btn->borderColor = Color::FromBytes(255, 255, 255, 28);
    btn->shadow = true;
    btn->shadowOffset = {0.0f, 4.0f};
    btn->shadowSoftness = 12.0f;
    btn->shadowColor = Color::FromBytes(0, 0, 0, 120);

    auto* item = card->AddComponent<UIToastItem>();
    item->kind = kind;
    item->life = std::max(0.05f, duration);
    item->serial = ++m_serial;

    GameObject* bar = sc->CreateGameObject("ToastBar");
    bar->transform->SetParent(card->transform, false);
    auto* bp = bar->AddComponent<UIPanel>();
    bp->anchor = UIAnchor::TopLeft;
    bp->position = {0.0f, 10.0f};
    bp->size = {5.0f, std::max(8.0f, height - 20.0f)};
    bp->shape = UIShape::Rectangle;
    bp->cornerRadius = 0.0f;
    bp->color = accent;

    const float textX = showIcon ? 60.0f : 18.0f;
    if (showIcon) {
        GameObject* icon = sc->CreateGameObject("ToastIcon");
        icon->transform->SetParent(card->transform, false);
        auto* ip = icon->AddComponent<UIPanel>();
        ip->anchor = UIAnchor::TopLeft;
        ip->position = {16.0f, (height - 36.0f) * 0.5f};
        ip->size = {36.0f, 36.0f};
        ip->shape = UIShape::Circle;
        ip->cornerRadius = 18.0f;
        ip->color = accent;
        GameObject* g = sc->CreateGameObject("ToastGlyph");
        g->transform->SetParent(icon->transform, false);
        auto* tr = g->AddComponent<TextRenderer>();
        tr->text = glyph;
        tr->screenSpace = true;
        tr->anchor = UIAnchor::TopLeft;
        tr->screenPos = {0.0f, 0.0f};
        tr->size = {36.0f, 36.0f};
        tr->pixelSize = 18.0f / 8.0f;
        tr->color = Color::White;
        tr->align = 1;
        tr->bold = true;
        tr->vcenter = true;
    }
    GameObject* titleGo = sc->CreateGameObject("ToastTitle");
    titleGo->transform->SetParent(card->transform, false);
    auto* tt = titleGo->AddComponent<TextRenderer>();
    tt->text = title;
    tt->screenSpace = true;
    tt->anchor = UIAnchor::TopLeft;
    tt->screenPos = {textX, 12.0f};
    tt->size = {std::max(40.0f, width - textX - 14.0f), 22.0f};
    tt->pixelSize = 16.0f / 8.0f;
    tt->color = Color::White;
    tt->bold = true;
    tt->vcenter = true;

    GameObject* bodyGo = sc->CreateGameObject("ToastBody");
    bodyGo->transform->SetParent(card->transform, false);
    auto* bt = bodyGo->AddComponent<TextRenderer>();
    bt->text = body;
    bt->screenSpace = true;
    bt->anchor = UIAnchor::TopLeft;
    bt->screenPos = {textX, 36.0f};
    bt->size = {std::max(40.0f, width - textX - 14.0f), 24.0f};
    bt->pixelSize = 13.0f / 8.0f;
    bt->color = Color::FromBytes(176, 182, 196);
    bt->vcenter = true;

    Place(btn, std::max(0, Count() - 1), 1.0f);
    return card;
}

inline void UIToast::Update(float dt) {
    if (!gameObject || !gameObject->transform || !gameObject->scene()) return;
    struct Row { UIToastItem* item; UIButton* btn; GameObject* go; bool dead; };
    std::vector<Row> rows;
    for (Transform* ch : gameObject->transform->Children()) {
        if (!ch || !ch->gameObject) continue;
        auto* item = ch->gameObject->GetComponent<UIToastItem>();
        auto* btn = ch->gameObject->GetComponent<UIButton>();
        if (!item || !btn) continue;
        rows.push_back({item, btn, ch->gameObject, false});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.item->serial > b.item->serial; });
    bool clicked = false;
    for (Row& r : rows) {
        r.item->age += dt;
        if (r.btn->WasClicked()) { clicked = true; r.dead = true; }
        else if (r.item->age >= r.item->life) r.dead = true;
    }
    Scene* sc = gameObject->scene();
    int index = 0;
    for (Row& r : rows) {
        if (r.dead) { sc->Destroy(r.go); continue; }
        float u = r.item->age / 0.18f;
        if (u > 1.0f) u = 1.0f;
        const float e = u * u * (3.0f - 2.0f * u);
        Place(r.btn, index, 1.0f - e);
        ++index;
    }
    if (clicked) FireClick();
}

} // namespace okay
