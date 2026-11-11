// Editor tools that work on a multi-selection of 3D scene objects (ImGui-free, so they run headless):
//
//   Align       X / Y / Z to the min, centre or max of the selection's bounds, or to the active (last-picked) object.
//   Distribute  3 or more objects with equal gaps between their bounds, or equal spacing between their centres.
//   Mirror      the selection across a world plane (X / Y / Z) through its centre, the active object's pivot or the
//               world origin, as a duplicate or in place.
//   Gizmo       the Move / Rotate / Scale gizmo drags apply to every selected object, about the Active object's pivot
//               or the Centre of the selection (the Local / Global switch already existed).
//
// What the tools work on ("targets"): the selected objects, minus any object whose parent (at any depth) is also
// selected (it moves with its parent), minus destroyed ones. Everything below a target follows it.
//
// Bounds: the world-space box of the target's renderers and everything below it (Scene::WorldBounds); with no
// renderer, the world boxes of its 3D colliders; with neither, the transform position (a point).
//
// Every action takes ONE undo step through EditorState::PushUndo (and none when nothing would change).
//
// How Mirror treats rotation, scale and winding. A reflection cannot be written as rotation + positive scale, and the
// engine does not render or light a negative world scale correctly (the software renderer culls back faces from the
// world-space winding and shades with a cofactor normal matrix, which both assume a positive determinant), so Mirror never
// writes a negative scale. Instead, for each mirrored object (and everything below it) it
//   * reflects the position, and turns the rotation into the proper rotation R' = F * R * G (F = the reflection across
//     the plane, G = a flip of one local axis k, chosen as the object's own axis most aligned with the plane normal
//     so an unrotated object keeps a zero rotation);
//   * flips the same local axis k in the mesh data (vertices, normals) and reverses every triangle's winding, so the
//     drawn geometry is the true mirror image; a mesh that is already symmetric across that flip (a cube, a plane) is
//     left as it is, with its name; children keep their local scale and get the local pose G * L * G;
//   * mirrors what is stored in local space: spline points and tangents, collider offsets (those are world vectors,
//     so they are reflected across the plane), and a sprite's flip flag.
// Mirroring twice across the same plane gives back the original transform, scale and mesh (within float error).
// Not mirrored (copied as they are): other component data in local space (waypoints, joint anchors, particle
// directions), and meshes rebuilt every frame (skinned or deformed meshes) and terrain: their transform is mirrored
// and the result says how many such objects were skipped. A target under a parent that is both rotated and scaled
// unevenly cannot be mirrored exactly (the result would need a shear); it is mirrored as closely as a rotation and
// scale allow and counted as "approximate".
#pragma once
#include "EditorState.hpp"
#include "okay/Math/Bounds.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Scene/GameObject.hpp"
#include <iosfwd>
#include <string>
#include <vector>

namespace okay::editor::seltools {

enum class Axis : int { X = 0, Y = 1, Z = 2 };
enum class AlignTo { Selection, Active };          ///< what Align lines the objects up with
enum class Edge { Min, Center, Max };              ///< which edge of each object's box is lined up
enum class Spread { Gaps, Centres };               ///< Distribute: equal gaps between bounds, or equal centre spacing
enum class MirrorAbout { SelectionCentre, ActivePivot, WorldOrigin };
enum class GizmoPivot { Active, Center };          ///< where the Scene gizmo sits for a multi-selection

/// The options the Align / Distribute / Mirror menus edit; saved in okay_settings.txt with the other editor settings.
struct Settings {
    AlignTo     alignTo = AlignTo::Selection;
    Spread      spread = Spread::Gaps;
    MirrorAbout mirrorAbout = MirrorAbout::SelectionCentre;
    bool        mirrorDuplicate = true;            ///< Mirror makes copies (true) or mirrors the selection itself
    GizmoPivot  gizmoPivot = GizmoPivot::Active;
};
Settings& Cfg();
void AppendSettings(std::ostream& out, const Settings& s);
/// One "key integer" pair read from the settings file. True when the key was one of ours (the value is range-checked).
bool ApplySetting(const std::string& key, int value, Settings& s);

const char* AxisName(Axis a);                      ///< "X", "Y", "Z"

// ---- What an action works on ----------------------------------------------------------------------------------------
/// The objects an action moves: `selection` without nulls, duplicates, destroyed objects, and objects below another
/// selected object. Order is the selection order.
std::vector<GameObject*> Targets(const std::vector<GameObject*>& selection);
/// World-space box of an object and everything below it (see the top of this file for the fallbacks). Never empty.
Bounds ObjectBounds(const GameObject& go);
/// The centre of the union of the targets' boxes.
Vec3 SelectionCentre(const std::vector<GameObject*>& targets);

struct Availability {
    bool ok = false;
    std::string reason;                            ///< when !ok: what is missing, for a tooltip beside the greyed-out item
};
Availability CanAlign(const EditorState& ed);      ///< 2 or more targets
Availability CanDistribute(const EditorState& ed); ///< 3 or more targets
Availability CanMirror(const EditorState& ed);     ///< 1 or more targets

struct Result {
    bool ok = false;                               ///< false: nothing was done (see `message`)
    int changed = 0;                               ///< objects that moved (Align / Distribute) or were mirrored
    int approximate = 0;                           ///< Mirror: targets that could only be mirrored approximately
    int skipped = 0;                               ///< Mirror: objects whose shape could not be mirrored (terrain, deformed meshes)
    std::string message;                           ///< one line for the console / status bar
    std::vector<GameObject*> made;                 ///< Mirror as duplicate: the new objects (now the selection)
};

// ---- The actions (selection is ed.MultiSelection(); the active object is ed.selected()) ----------------------------
Result Align(EditorState& ed, Axis axis, Edge edge, AlignTo to);
Result Distribute(EditorState& ed, Axis axis, Spread how);
Result Mirror(EditorState& ed, Axis plane, MirrorAbout about, bool duplicate);

/// Mirror one object (and everything below it) across the world plane `axis = planeValue`, with no undo step and no
/// selection change. The building block of Mirror(); exposed for tests and scripts.
struct MirrorStats { int meshesFlipped = 0, meshesLeftAsIs = 0, skipped = 0, approximate = 0; };
MirrorStats MirrorObject(GameObject& root, Axis plane, float planeValue);

// ---- Multi-select gizmo drags ------------------------------------------------------------------------------------------
/// Where the gizmo sits: the active object's position, or (Center) the average of the targets' positions.
Vec3 PivotPoint(const std::vector<GameObject*>& targets, const GameObject* active, GizmoPivot mode);
/// The targets that follow `primary` (the object the gizmo math drives): every target except `primary` and any object
/// above or below it (those move with it already).
std::vector<GameObject*> Followers(const std::vector<GameObject*>& selection, const GameObject* primary);
/// Move every follower by the world-space step the primary moved.
void MoveFollowers(const std::vector<GameObject*>& followers, const Vec3& worldDelta);
/// Turn objects about the world axis `axis` through `pivot` (positions orbit the pivot, orientations turn with them).
void RotateAbout(const std::vector<GameObject*>& objects, const Vec3& pivot, const Vec3& axis, float degrees);
/// Scale followers by `ratio` along the gizmo axes (`axes` = the three world-space gizmo directions): each object's own
/// local scale is multiplied per axis (never below 0.01, cameras are left alone) and its offset from `pivot` is
/// stretched along the same directions.
void ScaleFollowers(const std::vector<GameObject*>& followers, const Vec3& pivot, const Vec3 axes[3], const Vec3& ratio);

}  // namespace okay::editor::seltools
