#pragma once
// World streaming in the editor (StreamingTools.cpp, docs/engine/world_streaming.md): the World Partition window
// (View > Editors & Tools > World Partition) that splits the open scene into streaming cells, the Scene-view
// overlay (the partition grid while the window is open, a World Streamer's cells coloured by state, Level
// Streaming Volume boxes), the Add Component rows of the streaming components, and dev screenshot hooks.
#include "EditorState.hpp"
#include "imgui.h"
#include <functional>

namespace okay { class GameObject; struct Mat4; }

namespace oked {

extern bool g_showWorldPartition;   // the World Partition window

void DrawWorldPartitionWindow(okay::editor::EditorState& ed);

// Scene-view overlay, drawn above the meshes. Pure drawing (no handles).
void DrawStreamingOverlay(okay::editor::EditorState& ed, ImDrawList* dl, const okay::Mat4& vp, ImVec2 center, ImVec2 canvasSize);

// Add Component rows (World): Streaming Source, Level Streaming Volume, Voxel World (endless), Partition Layer.
void StreamingAddComponentItems(okay::editor::EditorState& ed, okay::GameObject* go,
                                const std::function<bool(bool, const char*)>& item);

// OKAY_UI_PARTITION=1: a demo town and the window with its grid preview. OKAY_UI_STREAMSCENE=<file>: open that scene
// (with OKAY_UI_PLAY=1 it plays; the World Streamer's overlay is switched on). OKAY_UI_STREAMCAM=x,y,z,yaw,pitch,dist.
void StreamingDevHooks(okay::editor::EditorState& ed);

} // namespace oked
