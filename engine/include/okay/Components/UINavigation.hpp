#pragma once
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Input/Input.hpp"
#include <algorithm>
#include <vector>

namespace okay {

/// Keyboard / gamepad menu navigation over a scene's interactive UI widgets.
/// Call once per frame (the player does). Up/Down (W/S or arrows, D-pad up/down)
/// move focus between focusable widgets in top-to-bottom, left-to-right order;
/// the focused widget is highlighted (IsFocused()). Then, by widget type:
///   * Button   — Enter/Space or gamepad A fires on_click().
///   * Toggle   — Enter/Space/A flips it (on_toggle()).
///   * Dropdown — Enter/Space/A opens/closes; Left/Right change the selection.
///   * Slider   — Left/Right adjust the value by 5% (on_change()).
/// Mouse interaction still works independently. Returns the widget activated
/// this frame (button/toggle/dropdown), or nullptr.
inline GameObject* NavigateUI(Scene& scene) {
    // Collect focusable, interactable widgets and a quick type tag.
    enum Kind { Button, Toggle, Slider, Dropdown };
    struct Item { GameObject* go; Kind kind; };
    std::vector<Item> items;
    for (const auto& go : scene.Objects()) {
        if (!go->active) continue;
        if (auto* b = go->GetComponent<UIButton>())   { if (b->focusable && b->interactable) items.push_back({go.get(), Button}); continue; }
        if (auto* t = go->GetComponent<UIToggle>())   { if (t->focusable) items.push_back({go.get(), Toggle}); continue; }
        if (auto* s = go->GetComponent<UISlider>())   { if (s->focusable) items.push_back({go.get(), Slider}); continue; }
        if (auto* d = go->GetComponent<UIDropdown>()) { if (d->focusable) items.push_back({go.get(), Dropdown}); continue; }
    }
    if (items.empty()) return nullptr;

    // Top-to-bottom, left-to-right by resolved rect.
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        UIRect ra = GetUIRect(a.go), rb = GetUIRect(b.go);
        float ay = ra.position ? ra.position->y : 0.0f, by = rb.position ? rb.position->y : 0.0f;
        float ax = ra.position ? ra.position->x : 0.0f, bx = rb.position ? rb.position->x : 0.0f;
        if (ay != by) return ay < by;
        return ax < bx;
    });

    auto isFocused = [](const Item& it) -> bool {
        if (auto* b = it.go->GetComponent<UIButton>())   return b->IsFocused();
        if (auto* t = it.go->GetComponent<UIToggle>())   return t->IsFocused();
        if (auto* s = it.go->GetComponent<UISlider>())   return s->IsFocused();
        if (auto* d = it.go->GetComponent<UIDropdown>()) return d->IsFocused();
        return false;
    };
    auto setFocused = [](const Item& it, bool f) {
        if (auto* b = it.go->GetComponent<UIButton>())   b->SetFocused(f);
        if (auto* t = it.go->GetComponent<UIToggle>())   t->SetFocused(f);
        if (auto* s = it.go->GetComponent<UISlider>())   s->SetFocused(f);
        if (auto* d = it.go->GetComponent<UIDropdown>()) d->SetFocused(f);
    };

    const int n = (int)items.size();
    int focused = -1;
    for (int i = 0; i < n; ++i) if (isFocused(items[i])) focused = i;

    bool down = Input::GetKeyDown('s') || Input::GetGamepadButtonDown(9);
    bool up   = Input::GetKeyDown('w') || Input::GetGamepadButtonDown(8);
    if (down) focused = (focused < 0) ? 0 : (focused + 1) % n;
    else if (up) focused = (focused < 0) ? n - 1 : (focused - 1 + n) % n;
    else if (focused < 0) focused = 0;

    for (int i = 0; i < n; ++i) setFocused(items[i], i == focused);

    bool activate = Input::GetKeyDown(' ') || Input::GetKeyDown('\r') ||
                    Input::GetKeyDown('\n') || Input::GetGamepadButtonDown(0);
    // Left/Right (A/D, arrows, D-pad) adjust value widgets.
    bool left  = Input::GetKeyDown('a') || Input::GetGamepadButtonDown(10);
    bool right = Input::GetKeyDown('d') || Input::GetGamepadButtonDown(11);

    if (focused < 0) return nullptr;
    Item& it = items[focused];
    switch (it.kind) {
        case Button:
            if (activate) { it.go->GetComponent<UIButton>()->Press(); return it.go; }
            break;
        case Toggle:
            if (activate) { it.go->GetComponent<UIToggle>()->Toggle(); return it.go; }
            break;
        case Dropdown: {
            auto* d = it.go->GetComponent<UIDropdown>();
            if (right) d->Cycle(+1);
            else if (left) d->Cycle(-1);
            else if (activate) { d->open = !d->open; return it.go; }
            break;
        }
        case Slider: {
            auto* s = it.go->GetComponent<UISlider>();
            float step = (s->maxValue - s->minValue) * 0.05f;
            if (right) s->SetValue(s->value + step);
            else if (left) s->SetValue(s->value - step);
            break;
        }
    }
    return nullptr;
}

} // namespace okay
