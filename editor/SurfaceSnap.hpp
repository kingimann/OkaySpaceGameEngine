// Editor snapping: the step maths, the settings, and snap-to-surface (ImGui-free, so it runs headless).
//
//   One settings object (snap::Cfg()) holds everything the Scene view's Snap button and its settings popup edit:
//   the three step sizes (move, rotate, scale), the Surface options, and the "place new objects on the surface"
//   option. It is saved in okay_settings.txt with the other editor settings.
//
// Modifier keys while dragging a Scene gizmo (the same rules apply to dragging the Inspector Position / Rotation /
// Scale fields, except where noted):
//   Ctrl          snap to the steps for this drag (also when the Snap button is off)
//   Alt           no snapping at all for this drag (also when the Snap button is on)
//   Ctrl+Shift    Surface snap for this Move drag (also when the Surface option is off)
// A value typed into a field is never changed by snapping.
//
// Surface snap reuses the editor's own scene queries: Physics3D::RaycastAll for primitive colliders and terrain, the
// RayTriangle test (Render/Lightmap.hpp) for meshes with no solid collider (including MeshCollider objects, whose
// collider is only a box), and vis::SceneViewVisible for hidden / isolated objects.
#pragma once
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include <iosfwd>
#include <string>
#include <vector>

namespace okay::editor::snap {

struct SnapSettings {
    bool  enabled = false;       ///< the toolbar Snap button
    float moveStep = 1.0f;       ///< world units
    float rotStep = 15.0f;       ///< degrees
    float scaleStep = 0.1f;      ///< scale units
    bool  surface = false;       ///< Move drags snap to the surface under the cursor
    bool  alignNormal = false;   ///< ... and turn the object's up axis to the surface normal
    float surfaceOffset = 0.0f;  ///< distance kept above the surface along its normal (may be negative)
    bool  restOnSurface = true;  ///< lift the object so its lowest point (along the normal) touches the surface
    bool  placeOnSurface = false;///< new, duplicated and dropped objects land on the surface under the cursor
};

/// The one live settings object.
SnapSettings& Cfg();

inline constexpr float kRotationPresets[] = {1.0f, 5.0f, 15.0f, 45.0f, 90.0f};
inline constexpr float kMovePresets[]     = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f};
inline constexpr float kScalePresets[]    = {0.01f, 0.05f, 0.1f, 0.25f, 0.5f, 1.0f};
inline constexpr float kMinStep = 0.001f, kMaxMoveStep = 1000.0f, kMinRotStep = 0.1f, kMaxRotStep = 180.0f, kMaxScaleStep = 100.0f;

// ---- Step maths -------------------------------------------------------------------------------------------
/// Nearest multiple of `step` (halves round away from zero, so -7.5 and 7.5 snap to opposite sides). step <= 0 returns v.
float RoundStep(float v, float step);
/// Degrees folded into (-180, 180].
float WrapDegrees(float deg);
/// RoundStep for an angle, then wrapped, so 359 with a 15 degree step reads -1 -> 0, never 360.
float SnapDegrees(float deg, float step);
/// A scale value snapped to its step but never below `minValue` (a scale of 0 would flatten the object).
float SnapScale(float v, float step, float minValue = 0.01f);
/// Is `v` on a step (within a tiny tolerance)?
bool OnStep(float v, float step);
/// The preset index equal to `v` (within tolerance), or -1 for a custom value.
int PresetIndex(const float* presets, int count, float v);

/// A drag that keeps its true (unsnapped) value, so slow mouse motion is not rounded away on every frame.
/// Begin() when the drag starts, Feed() with the value the control now shows; it returns the snapped value to show.
struct DragSnap {
    float raw = 0.0f, shown = 0.0f;
    void Begin(float v) { raw = shown = v; }
    float Feed(float v, float step) {
        raw += v - shown;
        shown = RoundStep(raw, step);
        return shown;
    }
};

// ---- Modifier keys ----------------------------------------------------------------------------------------
struct Mods { bool ctrl = false, shift = false, alt = false; };
struct ActiveSnap { bool steps = false; bool surface = false; };
/// What snapping a drag uses, from the settings and the keys held (the rules at the top of this file).
ActiveSnap Resolve(const SnapSettings& s, Mods m);

// ---- Persistence (okay_settings.txt: one "key integer" pair per line) -----------------------------------------
void AppendSettings(std::ostream& out, const SnapSettings& s);
/// Takes one pair read from the file. True when the key was one of ours (the value is clamped to a sane range).
bool ApplySetting(const std::string& key, int value, SnapSettings& s);

// ---- Surface snap -----------------------------------------------------------------------------------------
enum class HitSource { None, Collider, Triangles, Bounds, Terrain };
const char* HitSourceName(HitSource s);

struct SurfaceHit {
    bool hit = false;
    Vec3 point{0, 0, 0};
    Vec3 normal{0, 1, 0};        ///< unit length, facing the ray origin
    float distance = 0.0f;
    GameObject* object = nullptr;
    HitSource source = HitSource::None;
};

struct SurfaceQuery {
    std::vector<const GameObject*> ignore;   ///< these objects and everything below them are never hit
    const GameObject* isolate = nullptr;     ///< Isolate target (hidden objects are always skipped)
    float maxDistance = 5000.0f;
    int maxTriangles = 200000;               ///< bigger meshes are tested against their bounds box instead
};

/// The nearest surface along the ray. Skips: ignored objects and their children, objects hidden in the Scene view
/// (or by a hidden parent), inactive objects, trigger colliders. Solid primitive colliders and terrain hit exactly;
/// a mesh without a solid primitive collider (no collider, a MeshCollider, or only triggers) hits its triangles, or
/// its bounds box when it has no triangles or more than maxTriangles.
SurfaceHit RaycastSurface(Scene& scene, const Vec3& origin, const Vec3& direction, const SurfaceQuery& q);

struct SurfacePose {
    Vec3 position{0, 0, 0};
    Quat rotation{0, 0, 0, 1};
    bool rotated = false;
};
/// Where `obj` goes for this hit: on the point, lifted along the normal by the offset (and by how far the object
/// reaches below its pivot when restOnSurface is on); its up axis turned to the normal when alignNormal is on
/// (keeping its heading: the smallest turn that does it).
SurfacePose ComputeSurfacePose(const GameObject* obj, const SurfaceHit& hit, const SnapSettings& s);

/// Distance from the pivot to the object's lowest point along -normal, if the object were turned by `delta`.
/// Looks at the mesh bounds of the object and everything below it; 0 when there is no mesh.
float ReachBelow(const GameObject* obj, const Vec3& normal, const Quat& delta);

/// Move (and optionally turn) `obj` onto the hit. Does not push undo: the caller's step does.
void ApplySurfacePose(GameObject* obj, const SurfacePose& p);

}  // namespace okay::editor::snap
