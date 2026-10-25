#pragma once
// Non-destructive mesh modifiers. A stack of these turns an object's base mesh
// (MeshRenderer::mesh, which every editing tool works on) into the mesh that is
// drawn and collided with (MeshRenderer::modifiedMesh). Change a setting, toggle
// a modifier or edit the base, and the result is rebuilt; Apply bakes one into
// the base for good. The MeshModifiers component runs the stack.
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Render/Mesh.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

class Scene;
class GameObject;

struct MeshModifier {
    // Append-only: the number is what scenes store.
    enum class Type { Mirror, Array, Subdivision, Solidify, Bevel, Boolean, Decimate, Displace, Count };
    Type type = Type::Mirror;
    bool enabled = true;
    bool showInEditMode = true;   ///< off = hidden while the object is in mesh edit mode

    // Mirror: reflect across the object's own axis planes; a mesh crossing a
    // plane is cut there first (the + side is kept) so the halves never overlap.
    bool mirrorX = true, mirrorY = false, mirrorZ = false;
    bool mirrorMerge = true;      ///< weld vertices on the mirror plane (no seam)
    // Array: `count` copies, each shifted by `offset` — times the mesh's size
    // when `relative` (1.1 = a 10% gap), else in units.
    int   count = 3;
    bool  relative = true;
    Vec3  offset{1.1f, 0.0f, 0.0f};
    // Subdivision surface (Catmull-Clark).
    int   levels = 1;
    bool  smoothShading = true;
    // Solidify: give a surface thickness.
    float thickness = 0.1f;
    // Bevel: round every edge sharper than `angle` degrees.
    float width = 0.05f;
    int   segments = 2;
    float angle = 30.0f;
    // Boolean against another object's mesh (by name), voxel based.
    std::string target;
    int   boolOp = 1;             ///< 0 union, 1 subtract, 2 intersect
    float voxel = 0.04f;
    // Decimate: merge vertices within cells of this size.
    float cellSize = 0.1f;
    // Displace: roughen along the normals with value noise.
    float strength = 0.08f;
    float frequency = 3.0f;
    int   seed = 1;

    static const char* TypeName(Type t);
    static MeshModifier Make(Type t);   ///< sensible settings for a new modifier
};

/// True when two stacks have the same modifiers with the same settings.
bool SameModifiers(const std::vector<MeshModifier>& a, const std::vector<MeshModifier>& b);

/// What a modifier may need besides the mesh.
struct ModifierContext {
    const Scene* scene = nullptr;         ///< for Boolean targets
    const GameObject* owner = nullptr;    ///< the object being modified
    bool editMode = false;                ///< skip modifiers with showInEditMode off
};

/// Run one modifier on `in`. False (with the reason in `error`) leaves `out` = `in`.
bool ApplyModifier(const Mesh& in, const MeshModifier& mod, const ModifierContext& ctx,
                   Mesh& out, std::string* error = nullptr);
/// Run every enabled modifier in order. `errors` gets one entry per modifier
/// ("" = fine). The result is `base` when nothing is enabled.
Mesh EvaluateModifiers(const Mesh& base, const std::vector<MeshModifier>& stack, const ModifierContext& ctx,
                       std::vector<std::string>* errors = nullptr);
/// A fingerprint of everything that decides a stack's result: the base mesh, the
/// settings, and each Boolean target's mesh and placement relative to the owner.
std::uint64_t ModifierInputHash(const Mesh& base, const std::vector<MeshModifier>& stack, const ModifierContext& ctx);

} // namespace okay
