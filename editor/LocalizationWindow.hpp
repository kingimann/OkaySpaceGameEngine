#pragma once
// The Localization window (LocalizationWindow.cpp): the project's string table as
// keys x languages with inline editing, the languages and how complete each is,
// search and filters, CSV import / export, a pseudo-localized preview and Find in
// Scene; the toolbar's preview-language menu; the Inspector's Localized options;
// and the string-table files Build Game ships.
#include "EditorState.hpp"
#include <string>

namespace okay { class Component; class GameObject; struct LocalizedText; }

namespace oked {

// Window visibility flag (defined in LocalizationWindow.cpp; the View menu toggles it).
extern bool g_showLocalization;

// Draw the Localization window (no-op while hidden).
void DrawLocalizationWindow(okay::editor::EditorState& ed);

// The toolbar's preview-language menu (nothing while the project has no languages).
void DrawLanguageToolbar(okay::editor::EditorState& ed);

// Once a frame: keep the scene's localized text in the preview language (the
// scene isn't ticked outside Play).
void LocalizationTick(okay::editor::EditorState& ed);

// True while the Localization window has keyboard focus, so Ctrl+Z / Ctrl+Y act
// on the string table, not the scene; LocalizationUndo does that (false when
// there is nothing to undo).
bool LocalizationFocused();
bool LocalizationUndo(bool redo);

// The Inspector's "Localized" option of one text field: a checkbox, then a key
// picker with the default language's text. Turning it on reuses a key whose text
// is the literal, else adds one made from the object's name. True while the field
// is localized (the caller then hides its literal text box).
bool LocalizedTextField(okay::editor::EditorState& ed, const char* id, okay::LocalizedText& loc,
                        std::string& literal, okay::GameObject* go);
// A string-table key field: the key, a picker listing the table's keys with their
// text, and the default language's text under it.
bool LocKeyInput(okay::editor::EditorState& ed, const char* label, std::string& key);
// The Localized switch of a Pause Menu, Hold Prompt or Loading Screen (a no-op
// for other components). Turning it on adds the component's keys to the table
// with its current words. Returns the switch's state.
bool LocalizedSwitch(okay::editor::EditorState& ed, okay::Component* c);

// Font setup: merge an installed font with CJK glyphs into the editor's UI font,
// so the string table shows every language.
void MergeLocalizationFonts(float sizePx);

// Build Game: copy the string table and the fonts its languages name into the
// build's Data folder (keeping their paths relative to the table).
void CopyLocalizationFiles(okay::editor::EditorState& ed, const std::string& dataDir);

// Dev screenshot hooks: OKAY_UI_LOC=1 opens the window, OKAY_UI_LANG=<code>
// previews that language (qps-ploc = pseudo-localized), OKAY_UI_LOCKEY=<key>
// selects a key (its Find in Scene list shows).
void LocalizationDevHooks(okay::editor::EditorState& ed);

} // namespace oked
