#pragma once
// The UI Gallery window (GameObject > UI > Browse All UI...): one searchable list of every UI component, style,
// theme, icon and ready-made screen, with a preview and one-click add. The list and the search rules are in
// engine/include/okay/Scene/UIGalleryIndex.hpp (tested without a window); this file only draws them.
#include "EditorState.hpp"
#include "okay/Scene/UIStyleLib.hpp"
#include <functional>
#include <string>

namespace okay::editor {

/// Open (or focus) the window.
void OpenUIGalleryWindow();
/// The open/closed flag, for the View > Editors & Tools > UI Gallery check mark.
bool& UIGalleryOpenFlag();
/// Draw it when open (called once per frame from the editor's main loop).
void DrawUIGalleryWindow(EditorState& ed);
/// The look and style picked in the Gallery for new screens ("" = none). GameObject > UI screens use them too; there is no scene-wide theme.
void CurrentScreenLook(std::string& look, std::string& style);
/// How the window adds a component by its GameObject > UI id ("ui.button"). The editor sets it; the window never needs the menu code.
void SetUIGalleryAdder(std::function<bool(EditorState&, const std::string&)> adder);

/// A small picture of a button in a style, under a palette (the Inspector's live preview of the Style choice).
void DrawStylePreviewWidget(const okay::UIStyleLib::StyleSpec& spec, const okay::UIStyleLib::Palette& palette, float width);

} // namespace okay::editor
