#pragma once
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Input/Input.hpp"
#include <algorithm>
#include <vector>

namespace okay {

/// Keyboard / gamepad menu navigation over a scene's UIButtons. Call once per
/// frame (the player does). Up/Down (W/S or arrows, D-pad up/down) move focus
/// between focusable, interactable buttons in top-to-bottom, left-to-right
/// order; Enter/Space or gamepad A activates the focused one (fires on_click).
/// Focus is highlighted via UIButton::IsFocused(). Mouse hover still works
/// independently. Returns the button activated this frame, or nullptr.
inline GameObject* NavigateUI(Scene& scene) {
    std::vector<UIButton*> btns;
    for (const auto& go : scene.Objects())
        if (go->active)
            if (auto* b = go->GetComponent<UIButton>())
                if (b->focusable && b->interactable) btns.push_back(b);

    if (btns.empty()) return nullptr;
    std::sort(btns.begin(), btns.end(), [](UIButton* a, UIButton* b) {
        if (a->position.y != b->position.y) return a->position.y < b->position.y;
        return a->position.x < b->position.x;
    });

    int focused = -1;
    for (std::size_t i = 0; i < btns.size(); ++i) if (btns[i]->IsFocused()) focused = (int)i;

    const int n = (int)btns.size();
    bool down = Input::GetKeyDown('s') || Input::GetGamepadButtonDown(9);
    bool up   = Input::GetKeyDown('w') || Input::GetGamepadButtonDown(8);
    if (down) focused = (focused < 0) ? 0 : (focused + 1) % n;
    else if (up) focused = (focused < 0) ? n - 1 : (focused - 1 + n) % n;
    else if (focused < 0) focused = 0;          // first nav-less frame: focus the first

    for (int i = 0; i < n; ++i) btns[i]->SetFocused(i == focused);

    bool activate = Input::GetKeyDown(' ') || Input::GetKeyDown('\r') ||
                    Input::GetKeyDown('\n') || Input::GetGamepadButtonDown(0);
    if (activate && focused >= 0) {
        btns[focused]->Press();
        return btns[focused]->gameObject;
    }
    return nullptr;
}

} // namespace okay
