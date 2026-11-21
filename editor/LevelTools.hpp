#pragma once
// Level-design tools in the editor (LevelTools.cpp): the foliage brush (paint, erase and rescale plants on a
// terrain or any surface), terrain texture layers (splat painting), the decal projector, the cable, the
// measuring tool and the orthographic Top / Front / Right views of the Scene view.
#include "EditorState.hpp"
#include "imgui.h"
#include <functional>

namespace okay { class Component; class GameObject; class Terrain; struct Mat4; struct Vec3; }

namespace okaypanels { struct InspFieldList; }

namespace oked {

// ---- Scene view ---------------------------------------------------------------------------------

// Projection of the Scene view's 3D camera: perspective, or orthographic when a view preset (or the
// Orthographic switch) is on. `camDist` sets the zoom so both look alike at the same distance.
okay::Mat4 LevelEditorProjection(float fovDeg, float aspect, float nearZ, float farZ, float camDist);

// Overlay above the meshes: the brush ring and the strokes of the foliage and terrain-layer brushes, the
// measuring tool, the selected decal's box and a label naming an orthographic view. `clickable` says the
// view is hovered and nothing else took this frame's click.
void DrawLevelOverlay(okay::editor::EditorState& ed, ImDrawList* dl, const okay::Mat4& vp, ImVec2 center,
                      ImVec2 canvasSize, bool clickable, ImGuiIO& io);

// True while a brush of this module owns the left mouse button in the Scene view (the terrain sculpt brush stands down).
bool LevelBrushActive(okay::editor::EditorState& ed);

// The Scene view's View options: Perspective / Orthographic Top, Front, Right (and their opposites) and
// the measuring tool. Called inside the popup.
void DrawLevelViewMenu(okay::editor::EditorState& ed);

// Keys in the Scene view (numpad 7 / 1 / 3 views, numpad 5 projection, M measuring tool, Esc leaves a tool).
void HandleLevelKeys(okay::editor::EditorState& ed, bool viewHovered);

// ---- Inspector and menus ------------------------------------------------------------------------

// The Inspector of a Foliage, Decal or Cable component (a no-op for other components).
void DrawLevelInspector(okay::editor::EditorState& ed, okay::GameObject* go, okay::Component* comp,
                        okay::Component*& toRemove, bool (*header)(const char*, okay::Component*, okay::Component**, bool));

// The Terrain Inspector's texture layers section and its Scatter section (which plants a Foliage layer).
void DrawTerrainLayersUI(okay::editor::EditorState& ed, okay::Terrain& terrain);
void DrawTerrainScatterUI(okay::editor::EditorState& ed, okay::Terrain& terrain);

// The same two sections as OkayUI field rows (the Inspector, stage 4; docs/own_ui_toolkit.md section 17): AddTerrainToolRows appends
// them to the Terrain body's rows and keeps what it needs in `st`; after the rows are drawn RunTerrainToolRows does what the Dear ImGui
// sections do. RunTerrainToolRows returns false when it stopped early (every layer removed).
struct TerrainToolRows {
    int addLayers = -1, addLayer = -1, fill = -1, removeAll = -1, detail = -1, paint = -1;
    int firstLayer = -1, layerCount = 0;                 // per layer: radio, name, tint, tiling, [Remove], texture (see rowsPerLayer)
    int layerRow[4][6] = {};
    float tint[4][4] = {};
    int detailIndex = 1;
    float scale[2] = {}, band[2] = {};
    int scaleRow = -1, bandRow = -1, scatter = -1, clear = -1, addFoliage = -1;
    bool paintOn = false;
};
void AddTerrainToolRows(okay::editor::EditorState& ed, okay::Terrain& terrain, okaypanels::InspFieldList& L, TerrainToolRows& st);
void RunTerrainToolRows(okay::editor::EditorState& ed, okay::Terrain& terrain, okaypanels::InspFieldList& L, TerrainToolRows& st);

// Add Component rows: Foliage, Decal in Rendering; Cable in Rendering too.
void LevelAddComponentItems(okay::editor::EditorState& ed, okay::GameObject* go,
                            const std::function<bool(bool, const char*)>& item);

// GameObject menu rows: Foliage Layer, Decal Projector, Cable.
void LevelCreateMenuItems(okay::editor::EditorState& ed, bool& created);

// Edit mode, every frame: foliage chunks follow the instance lists, cables follow their ends, decals are
// projected when they are new or have been moved.
void LevelEditTick(okay::editor::EditorState& ed, float dt);

// Dev screenshot hooks (OKAY_UI_LEVEL=1 builds a demo scene; OKAY_UI_LEVELTOOL picks a tool; OKAY_UI_LEVELVIEW a view).
void LevelDevHooks(okay::editor::EditorState& ed);

} // namespace oked
