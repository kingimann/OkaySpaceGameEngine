#pragma once
// Developer hook (OKAY_DEV_FIELD_DEMO=1): registers a "Field Demo" component that has one property of every
// kind and no hand-written Inspector, and adds an object with it to the open scene. It shows the Inspector's
// registry-driven card (what a plugin's component gets): drag-to-scrub numbers, mixed values, colour picker,
// file and object reference fields. Call once per frame; it acts once.
#include "EditorState.hpp"

namespace oked {
void DevFieldDemo(okay::editor::EditorState& ed);
}
