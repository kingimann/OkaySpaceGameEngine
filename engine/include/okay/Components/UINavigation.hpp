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
/// Call once per frame (the player does). Up/Down/Left/Right (WASD or arrows,
/// D-pad) move focus to the nearest focusable widget on screen in that direction,
/// so grids and side-by-side buttons navigate the way they look; Up/Down wrap
/// around at the ends (in reading order). Left/Right stay with a focused slider,
/// dropdown or tabs, which use them for their value. The focused widget is
/// highlighted (IsFocused()). Then, by widget type:
///   * Button   — Enter/Space or gamepad A fires on_click().
///   * Toggle   — Enter/Space/A flips it (on_toggle()).
///   * Dropdown — Enter/Space/A opens/closes; Left/Right change the selection.
///   * Slider   — Left/Right adjust the value by 5% (on_change()).
///   * Tabs     — Left/Right select the previous / next enabled tab (on_change()).
///   * Keybind  — Enter/Space/A starts listening for a key (focus holds while it listens).
/// While a modal is open only the widgets inside it take focus. Mouse interaction
/// still works independently. Returns the widget activated this frame
/// (button/toggle/dropdown/keybind), or nullptr.
inline GameObject* NavigateUI(Scene& scene) {
    // Collect focusable, interactable widgets and a quick type tag.
    enum Kind { Button, Toggle, Slider, Dropdown, Tabs, Keybind };
    struct Item { GameObject* go; Kind kind; Vec2 origin; Vec2 centre; };
    std::vector<Item> items;
    for (const auto& go : scene.Objects()) {
        if (auto* kb = go->GetComponent<UIKeybindField>(); kb && kb->IsListening())
            return nullptr;                                  // the keys are being captured
    }
    for (const auto& go : scene.Objects()) {
        if (!go->active || UIHidden(go.get())) continue;   // hidden pages/menus can't take focus
        if (UIModal::Blocks(go.get())) continue;            // behind an open modal
        if (auto* b = go->GetComponent<UIButton>())   { if (b->focusable && b->interactable) items.push_back({go.get(), Button, {}, {}}); continue; }
        if (auto* t = go->GetComponent<UIToggle>())   { if (t->focusable) items.push_back({go.get(), Toggle, {}, {}}); continue; }
        if (auto* s = go->GetComponent<UISlider>())   { if (s->focusable) items.push_back({go.get(), Slider, {}, {}}); continue; }
        if (auto* d = go->GetComponent<UIDropdown>()) { if (d->focusable) items.push_back({go.get(), Dropdown, {}, {}}); continue; }
        if (auto* tb = go->GetComponent<UITabs>())    { if (tb->focusable && tb->interactable && tb->Count() > 0) items.push_back({go.get(), Tabs, {}, {}}); continue; }
        if (auto* kb = go->GetComponent<UIKeybindField>()) { if (kb->focusable && kb->interactable) items.push_back({go.get(), Keybind, {}, {}}); continue; }
    }
    if (items.empty()) return nullptr;

    // Where each widget is on screen: the renderer's rect when it published one
    // (scaled canvases, world-space UI), else the resolved layout rect.
    const float sw = UIWorld().screenW > 0.0f ? UIWorld().screenW : 1280.0f;
    const float sh = UIWorld().screenH > 0.0f ? UIWorld().screenH : 720.0f;
    for (auto& it : items) {
        Vec2 o{0.0f, 0.0f}, sz{0.0f, 0.0f};
        if (!(UIWorld().rectOf && UIWorld().rectOf(it.go, o, sz))) GetUIScreenRect(it.go, sw, sh, o, sz);
        it.origin = o;
        it.centre = o + sz * 0.5f;
    }
    // Reading order (top-to-bottom, left-to-right): first focus and wrap-around.
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.origin.y != b.origin.y) return a.origin.y < b.origin.y;
        return a.origin.x < b.origin.x;
    });

    auto isFocused = [](const Item& it) -> bool {
        if (auto* b = it.go->GetComponent<UIButton>())   return b->IsFocused();
        if (auto* t = it.go->GetComponent<UIToggle>())   return t->IsFocused();
        if (auto* s = it.go->GetComponent<UISlider>())   return s->IsFocused();
        if (auto* d = it.go->GetComponent<UIDropdown>()) return d->IsFocused();
        if (auto* tb = it.go->GetComponent<UITabs>())    return tb->IsFocused();
        if (auto* kb = it.go->GetComponent<UIKeybindField>()) return kb->IsFocused();
        return false;
    };
    auto setFocused = [](const Item& it, bool f) {
        if (auto* b = it.go->GetComponent<UIButton>())   b->SetFocused(f);
        if (auto* t = it.go->GetComponent<UIToggle>())   t->SetFocused(f);
        if (auto* s = it.go->GetComponent<UISlider>())   s->SetFocused(f);
        if (auto* d = it.go->GetComponent<UIDropdown>()) d->SetFocused(f);
        if (auto* tb = it.go->GetComponent<UITabs>())    tb->SetFocused(f);
        if (auto* kb = it.go->GetComponent<UIKeybindField>()) kb->SetFocused(f);
    };

    const int n = (int)items.size();
    int focused = -1;
    for (int i = 0; i < n; ++i) if (isFocused(items[i])) focused = i;

    bool down = Input::GetKeyDown('s') || Input::GetGamepadButtonDown(9);
    bool up   = Input::GetKeyDown('w') || Input::GetGamepadButtonDown(8);
    bool left  = Input::GetKeyDown('a') || Input::GetGamepadButtonDown(10);
    bool right = Input::GetKeyDown('d') || Input::GetGamepadButtonDown(11);
    bool activate = Input::GetKeyDown(' ') || Input::GetKeyDown('\r') ||
                    Input::GetKeyDown('\n') || Input::GetGamepadButtonDown(0);

    // The nearest widget whose centre lies in direction (dx, dy) from the focused
    // one; sideways offset counts double, so a widget straight ahead beats a
    // closer one off to the side. -1 if there is none that way.
    auto nearest = [&](int from, float dx, float dy) {
        int best = -1; float bestScore = 0.0f;
        for (int i = 0; i < n; ++i) {
            if (i == from) continue;
            const Vec2 d = items[i].centre - items[from].centre;
            const float along = d.x * dx + d.y * dy;
            if (along < 1.0f) continue;
            const float side = d.x * dy - d.y * dx;
            const float score = along + 2.0f * (side < 0.0f ? -side : side);
            if (best < 0 || score < bestScore) { best = i; bestScore = score; }
        }
        return best;
    };

    if (focused < 0) {
        focused = up ? n - 1 : 0;
        left = right = false;
    } else {
        // Left/Right belong to the focused slider / dropdown / tabs; otherwise they move.
        const Kind fk = items[focused].kind;
        const bool valueKeys = fk == Slider || fk == Dropdown || fk == Tabs;
        int to = -1;
        if (down)      { to = nearest(focused, 0.0f, 1.0f);  if (to < 0) to = 0; }
        else if (up)   { to = nearest(focused, 0.0f, -1.0f); if (to < 0) to = n - 1; }
        else if (!valueKeys && left)  to = nearest(focused, -1.0f, 0.0f);
        else if (!valueKeys && right) to = nearest(focused, 1.0f, 0.0f);
        if (!valueKeys) left = right = false;
        if (to >= 0 && to != focused) { focused = to; left = right = false; }
    }

    for (int i = 0; i < n; ++i) setFocused(items[i], i == focused);

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
        case Tabs: {
            auto* tb = it.go->GetComponent<UITabs>();
            if (right) tb->SelectNext();
            else if (left) tb->SelectPrevious();
            break;
        }
        case Keybind:
            if (activate) { it.go->GetComponent<UIKeybindField>()->Listen(); return it.go; }
            break;
    }
    return nullptr;
}

} // namespace okay
