#pragma once
// The editor's windows for easy anti-cheat setup (logic: editor/AntiCheatSetup.hpp, tested in
// tests/test_anticheat_setup.cpp). Called from a few places in main.cpp: Add Component, the Network
// Manager inspector, the Anti-Cheat Config inspector and Build Settings.
#include "EditorState.hpp"

namespace okay { class AntiCheatConfig; class NetworkManager; class GameObject; }

using okay::editor::EditorState;

namespace oked {

/// Add Component menu entry: "Anti-Cheat (one click, safe preset)". Returns true when something was added.
/// Call it from inside an open menu; it draws one MenuItem.
bool AddAntiCheatMenuItem(EditorState& ed, okay::GameObject* go);

/// Network Manager inspector: a one-click "Add Anti-Cheat" button while the scene has none.
void DrawNetworkManagerAntiCheat(EditorState& ed, okay::NetworkManager* nm);

/// Anti-Cheat Config inspector: the preset picker ("Casual co-op" / "Competitive" / "Economy game").
void DrawAntiCheatPresetPicker(EditorState& ed, okay::AntiCheatConfig* ac);

/// The checklist for the open scene: what is on, what is missing, a fix button where a safe one exists.
/// `manifestMode` is Build Settings' Anti-cheat combo (0 Off, 1 Report, 2 Enforce); a fix may raise it to Report.
void DrawAntiCheatChecklist(EditorState& ed, int& manifestMode, const char* idSuffix = "");

} // namespace oked
