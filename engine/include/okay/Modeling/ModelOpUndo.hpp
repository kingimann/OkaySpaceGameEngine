#pragma once
// One button in the Modeling window or the Mesh Renderer inspector = one undo step, on the stack that is active.
// In Edit Mesh on this object, Ctrl+Z walks the mesh-edit stack (MeshUndo.hpp), so a whole-mesh operation (Twist,
// Shear, Solidify, Mirror, Recenter, ...) must push a mesh step there; pushing a scene step instead folds the
// operation into the previous mesh step, and one Ctrl+Z then takes back both. Outside Edit Mesh the ordinary scene
// undo applies. Kept apart from the editor so the rule can be tested without a window. Our own code.
#include "okay/Modeling/MeshUndo.hpp"
#include "okay/Render/Mesh.hpp"
#include <string>
#include <utility>
#include <vector>

namespace okay::modeling {

/// True when two meshes hold the same geometry and the same per-vertex / per-face data that the modeling buttons change.
inline bool SameMesh(const Mesh& a, const Mesh& b) {
    auto eqV3 = [](const std::vector<Vec3>& x, const std::vector<Vec3>& y) {
        if (x.size() != y.size()) return false;
        for (std::size_t i = 0; i < x.size(); ++i)
            if (!(x[i].x == y[i].x && x[i].y == y[i].y && x[i].z == y[i].z)) return false;
        return true;
    };
    auto eqV2 = [](const std::vector<Vec2>& x, const std::vector<Vec2>& y) {
        if (x.size() != y.size()) return false;
        for (std::size_t i = 0; i < x.size(); ++i)
            if (!(x[i].x == y[i].x && x[i].y == y[i].y)) return false;
        return true;
    };
    auto eqC = [](const std::vector<Color>& x, const std::vector<Color>& y) {
        if (x.size() != y.size()) return false;
        for (std::size_t i = 0; i < x.size(); ++i)
            if (!(x[i].r == y[i].r && x[i].g == y[i].g && x[i].b == y[i].b && x[i].a == y[i].a)) return false;
        return true;
    };
    return a.name == b.name && a.triangles == b.triangles && a.polyFace == b.polyFace && a.triSlot == b.triSlot &&
           eqV3(a.vertices, b.vertices) && eqV3(a.normals, b.normals) && eqV2(a.uvs, b.uvs) &&
           eqV2(a.cornerUVs, b.cornerUVs) && eqC(a.triColors, b.triColors) && eqC(a.vertexColors, b.vertexColors) &&
           a.autoSmoothAngle == b.autoSmoothAngle;
}

/// Run `op` on the mesh in `target` as exactly one undo step:
///  - `meshEditActive` (Edit Mesh is on for this very mesh): a step on the mesh-edit stacks `undo`/`redo`;
///  - otherwise `sceneUndo()` is called once first (the editor's scene snapshot).
/// The step is labelled `label` (the Undo history shows it). When the operation changed nothing, the mesh step is
/// taken back again, so a no-op button (Recenter on a centred mesh) leaves no empty step. Returns whether the mesh
/// changed.
template <class SceneUndo, class Op>
bool RunUndoableMeshOp(bool meshEditActive, std::vector<MeshUndoStep>& undo, std::vector<MeshUndoStep>& redo,
                       const MeshUndoTarget& target, const std::string& label, SceneUndo&& sceneUndo, Op&& op) {
    if (!target.mesh) return false;
    const Mesh before = *target.mesh;
    if (meshEditActive) {
        std::vector<MeshUndoStep> savedRedo = redo;          // restored if the step turns out empty
        PushMeshUndo(undo, redo, target, label);
        op(*target.mesh);
        if (SameMesh(before, *target.mesh)) {
            undo.pop_back();
            redo = std::move(savedRedo);
            return false;
        }
        return true;
    }
    sceneUndo();
    op(*target.mesh);
    return !SameMesh(before, *target.mesh);
}

} // namespace okay::modeling
