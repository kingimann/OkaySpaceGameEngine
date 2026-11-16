#pragma once
// Inspectors and GameObject > UI entries for the extra widgets: search bar,
// date and time, file picker, accordion, breadcrumbs, pagination, skeleton
// and bottom sheet, plus the range slider, aspect-ratio container and input validation.
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
void BuildUIRangeSlider(okay::GameObject* g);
void BuildUIAspectRatio(okay::GameObject* g);
void BuildUIInputValidation(okay::GameObject* g);
// Slice 2: game HUD parts and data glue (GameObject > UI > Game HUD / Data).
void BuildUIRadialMenu(okay::GameObject* g);
void BuildUIKillFeed(okay::GameObject* g);
void BuildUIAmmoCounter(okay::GameObject* g);
void BuildUIInteractionPrompt(okay::GameObject* g);
void BuildUIObjectiveTracker(okay::GameObject* g);
void BuildUICompass(okay::GameObject* g);
void BuildUIItemTooltipCard(okay::GameObject* g);
void BuildUIAbilityIcon(okay::GameObject* g);
void BuildUIBoundSlider(okay::GameObject* g);
void BuildUIBoundList(okay::GameObject* g);
void BuildUIHotReload(okay::GameObject* g);
void BuildUIVisibleIf(okay::GameObject* g);

} // namespace oked
