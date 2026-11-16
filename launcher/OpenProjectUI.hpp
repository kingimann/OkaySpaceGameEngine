#pragma once
// The window the launcher shows BEFORE it starts the editor on a project made with another version ("MyGame was made with v2.850.0.
// This editor is v2.875.0." with Upgrade project / Open with v2.850.x / Cancel). What it says and which buttons it has is decided in
// OpenProjectGate.hpp (header-only, tested); this file only draws one okayopen::Prompt in the launcher's style and reports the button.
// It reaches into nothing else of the launcher, so a small harness can draw it for a screenshot (tools/launcher_open_shot.cpp).
//
// main.cpp opens the popup from the top level of the frame (ImGui::OpenPopup(PopupName())) and calls Draw() every frame while the
// question is pending. See docs/project_upgrade.md, "The launcher's side".
#include <optional>

#include "imgui.h"
#include "OpenProjectGate.hpp"

namespace okayopenui {

/// The launcher's accent colour (its primary buttons); main.cpp passes kAccent and kAccentDim.
struct Look {
    ImVec4 accent{0.26f, 0.59f, 0.98f, 1.0f};
    ImVec4 accentDim{0.19f, 0.43f, 0.72f, 1.0f};
};

/// The popup's name: open it with ImGui::OpenPopup(PopupName()) from the top level of the frame, so the two names meet.
const char* PopupName();

/// Draw the question. Call every frame while it is pending, after OpenPopup. Returns the answer when a button was pressed this frame
/// (Escape counts as Cancel); the popup is closed then. std::nullopt otherwise, also while the popup is not open.
std::optional<okayopen::Answer> Draw(const okayopen::Prompt& prompt, const Look& look = Look());

}  // namespace okayopenui
