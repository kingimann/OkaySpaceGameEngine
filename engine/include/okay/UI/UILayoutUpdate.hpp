#pragma once

namespace okay {

class Scene;

/// Per-frame UI layout, shared by the game and the editor (so the edit-time
/// preview matches play): settle each UIFrame against its widget's own fields,
/// fit each auto-content Scroll View's extent to its children, re-arrange flex
/// layouts, and rebuild / rebind the visible rows of every List View. Call once a
/// frame before the scene updates.
void UpdateUILayouts(Scene& scene);

/// Give every widget in the scene a UIFrame placing it where its anchor preset
/// does (ConvertToUIFrame). Nothing moves. Returns how many were converted.
int ConvertSceneToUIFrames(Scene& scene);

} // namespace okay
