#pragma once
// The Weapons window (WeaponEditor.cpp): the project's weapons.okayweapon, the
// list the Melee Attacker and the Ranged Weapon pick from. View > Editors & Tools
// and the window list open it. A dropped .okayweapon file opens it too.
#include "EditorState.hpp"
#include <string>

namespace oked {

extern bool g_showWeapons;

void DrawWeaponEditor(okay::editor::EditorState& ed);
bool WeaponEditorFocused();
bool WeaponEditorUndo(bool redo);
void OpenWeaponFile(const std::string& path);

// Inspector: a dropdown of weapons plus a slot that accepts a dropped .okayweapon file.
// `guns` lists guns; otherwise it lists melee weapons. True when the reference changes.
bool WeaponReferenceField(const char* label, std::string& weaponId, std::string& weaponFile, bool guns);

// OKAY_UI_WEAPONS=1 opens the window. OKAY_UI_WEAPONS_FILE is a sample file to load.
void WeaponEditorDevHooks(okay::editor::EditorState& ed);

} // namespace oked
