#pragma once
// Splines in the editor (SplineTools.cpp): the Scene-view overlay that draws every
// spline and edits the selected one (points, handles, insert, delete, snap to the
// ground), the inspectors and Add Component rows of the spline, follower, spline
// mesh, instancer, line and trail components, GameObject > Spline, the edit-mode
// upkeep (meshes and instancers rebuilt live, trails previewed) and the lines and
// trails drawn over the 3D view.
#include "EditorState.hpp"
#include "imgui.h"
#include <functional>

namespace okay { class Component; class GameObject; struct Mat4; struct Vec3; }

namespace oked {

// Scene-view overlay, above the meshes: every spline's curve, and for the selected
// one its points, handles and the picked point's move gizmo. `clickable` says the
// view is hovered and nothing else took this frame's click.
void DrawSplineOverlay(okay::editor::EditorState& ed, ImDrawList* dl, const okay::Mat4& vp, const okay::Vec3& eye,
                       ImVec2 center, ImVec2 canvasSize, ImVec2 canvasPos, bool clickable, ImGuiIO& io);

// True while a point of the selected object's spline is picked: the Scene view
// shows that point's gizmo instead of the object's.
bool SplinePointPicked(okay::editor::EditorState& ed);

// The Delete key: removes the picked spline point (true) instead of the object.
bool SplineDeletePickedPoint(okay::editor::EditorState& ed);

// Line and Trail Renderers over the 3D image of a view (`viewMin`..`viewMax`),
// alpha-blended like the particles. `cullingMask` and `occluded` are the 3D
// render's (RenderCullingMask, ParticleOccluded).
void DrawLinesAndTrails(okay::editor::EditorState& ed, ImDrawList* dl, const okay::Mat4& vp, const okay::Vec3& eye,
                        ImVec2 viewMin, ImVec2 viewMax, const okay::GameObject* ignore, int cullingMask,
                        bool (*occluded)(float, float, float));

// The Inspector of one spline / follower / spline mesh / instancer / line / trail
// component (a no-op for other components). `header` is the Inspector's CompHeader.
using CompHeaderFn = bool (*)(const char*, okay::Component*, okay::Component**, bool);
void DrawSplineInspector(okay::editor::EditorState& ed, okay::GameObject* go, okay::Component* comp,
                         okay::Component*& toRemove, CompHeaderFn header);

// The Add Component rows: `rendering` = the Rendering category (Line, Trail, Spline
// Mesh), else Gameplay (Spline, Spline Follower, Spline Instancer).
void SplineAddComponentItems(okay::editor::EditorState& ed, okay::GameObject* go,
                             const std::function<bool(bool, const char*)>& item, bool rendering);

// GameObject > Spline: a spline of three points in front of the camera, on the ground.
void CreateSplineFromMenu(okay::editor::EditorState& ed);

// Edit mode, every frame: spline meshes and instancers follow their splines, and
// trails preview behind objects you move.
void SplineEditTick(okay::editor::EditorState& ed, float dt);

// Dev screenshot hooks: OKAY_UI_SPLINE=1 builds a demo on the open scene (a road with
// lamp posts, a fence, a platform on a rail, a laser and a comet with a trail);
// OKAY_UI_SPLINESEL=Object:point selects that object and picks the point;
// OKAY_UI_SPLINECAM=x,y,z,yaw,pitch,dist aims the Scene camera.
void SplineDevHooks(okay::editor::EditorState& ed);

} // namespace oked
