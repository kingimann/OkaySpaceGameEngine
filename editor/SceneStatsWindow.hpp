#pragma once
// The Scene Stats window (View > Scene Stats window) and the Run order section of Project Settings
// (SceneStatsWindow.cpp). Both are plain readouts / switches over okay::Scene::Stats(), Scene::LastFrameTimings()
// and okay::ExecutionOrderSettings; see docs/ecs_guide.md.
#include "EditorState.hpp"

namespace oked {

/// Window visibility flag (defined in SceneStatsWindow.cpp).
extern bool g_showSceneStats;

/// Draw the Scene Stats window (no-op while hidden): object and component counts, components by type, and,
/// with "Measure time per type" on, how long each type took in Update last frame.
void DrawSceneStatsWindow(okay::editor::EditorState& ed);

/// The "Run order" section body of Project Settings: pick a component type and say "Run early", "Normal" or "Run late".
/// `save` is called after every change (the editor's SaveProjectSettings).
void DrawRunOrderSettings(okay::editor::EditorState& ed, void (*save)());

} // namespace oked
