#pragma once
// Faster modeling: selection helpers (grow, shrink, select similar, select by direction, size or slot),
// baking an object's scale / rotation into its mesh, and "how do I" recipes that run real tools
// (a window hole, rounded edges, a table). Triangles are numbered like the rest of mesh edit mode:
// a quad is two triangles, and selecting one selects both.
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Render/Mesh.hpp"
#include <string>
#include <utility>
#include <vector>

namespace okay::modeling {

/// The selection plus every triangle that shares a corner (a position, so welded or not) with it.
std::vector<int> GrowFaces(const Mesh& m, const std::vector<int>& tris);
/// The selection without its rim: triangles that touch no unselected triangle are kept.
std::vector<int> ShrinkFaces(const Mesh& m, const std::vector<int>& tris);

enum class SimilarKind { Normal, Area, Slot, Coplanar };
const char* SimilarKindName(SimilarKind k);
/// Every triangle like the selected ones. Normal: facing within `tolerance` degrees. Area: within
/// `tolerance` (a fraction, 0.1 = 10%) of a selected triangle's area. Slot: in the same material
/// slot. Coplanar: same facing and on the same plane (within `tolerance` units). With nothing
/// selected the result is empty.
std::vector<int> SelectSimilar(const Mesh& m, const std::vector<int>& tris, SimilarKind kind, float tolerance);
/// Triangles whose normal is within `angleDegrees` of `dir` (e.g. all that face up).
std::vector<int> SelectByNormal(const Mesh& m, const Vec3& dir, float angleDegrees);
/// Triangles with an area between `minArea` and `maxArea` (negative max = no limit).
std::vector<int> SelectByArea(const Mesh& m, float minArea, float maxArea);

/// The separate pieces of the mesh: triangles that touch (share a corner position) are one piece. Each piece is a
/// list of triangle numbers; pieces come biggest first (ties: lowest triangle number first).
std::vector<std::vector<int>> LooseParts(const Mesh& m);

/// Bake an object's scale and rotation into its mesh (Blender's Apply Scale / Rotation): the vertices
/// become scale * rotation * vertex and normals turn with them. A negative scale on an odd number of axes
/// flips the winding so faces still point out. Returns false (mesh unchanged) for a zero or non-finite scale.
bool BakeTransform(Mesh& m, const Vec3& scale, const Quat& rotation);

// ---- "How do I" recipes: each runs the same tools as the buttons ----------------------------------
/// Cut a window in the selected faces: inset them by `border` (0..1), then delete the inner faces so there is
/// an opening with a frame. Returns false with the reason in `error` if nothing is selected.
bool MakeWindowHole(Mesh& m, const std::vector<int>& tris, float border, std::string* error = nullptr);
/// Round the given edges (vertex pairs) with a bevel `width` wide and `segments` steps. Returns how many edges
/// were rounded, or 0 with the reason in `error`.
int RoundEdges(Mesh& m, const std::vector<std::pair<int, int>>& edges, float width, int segments, std::string* error = nullptr);
/// A four-legged table: a top slab and four legs, slot 0 for the top and slot 1 for the legs. Sizes are clamped to sensible values.
Mesh MakeTable(float width = 1.6f, float depth = 0.9f, float height = 0.75f, float legSize = 0.07f, float topThickness = 0.06f);

// ---- Shrinkwrap / snap to surface ------------------------------------------------------------------
enum class WrapMode { Nearest, Project };   ///< Nearest: the closest point on the target; Project: along `dir` (either way) to where it hits
struct WrapReport { int moved = 0, missed = 0; float maxMove = 0; };
/// Move vertices of `m` onto the surface of `target` (both in the same space; the editor converts). `verts` = which vertices (empty = all),
/// `offset` pushes the result away from the surface along its normal, `maxDistance` (> 0) leaves vertices farther than that where they are
/// (0 = no limit). Project mode casts along `dir` in both directions and takes the nearer hit. Vertices that find nothing are counted in `missed`.
WrapReport Shrinkwrap(Mesh& m, const std::vector<int>& verts, const Mesh& target, WrapMode mode, float offset = 0.0f,
                      float maxDistance = 0.0f, const Vec3& dir = Vec3{0, -1, 0});

} // namespace okay::modeling
