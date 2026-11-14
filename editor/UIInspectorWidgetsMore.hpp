#pragma once
// Inspectors and GameObject > UI entries for the extra widgets: search bar,
// date and time, file picker, accordion, breadcrumbs, pagination, skeleton
// and bottom sheet.
#include "UIInspectorLayout.hpp"

namespace oked {

void DrawMoreUIComponentInspector(okay::Component* comp, okay::editor::EditorState& ed,
                                  okay::Component** toRemove, const UIInspectorHooks& hooks);

void BuildUISearchBar(okay::GameObject* g);
void BuildUIDateTimePicker(okay::GameObject* g);
void BuildUIFilePicker(okay::GameObject* g);
void BuildUIAccordion(okay::GameObject* g);
void BuildUIBreadcrumbs(okay::GameObject* g);
void BuildUIPagination(okay::GameObject* g);
void BuildUISkeleton(okay::GameObject* g);
void BuildUIBottomSheet(okay::GameObject* g);

} // namespace oked
