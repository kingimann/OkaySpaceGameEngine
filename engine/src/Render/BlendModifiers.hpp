// Mesh modifiers for our own Blender reader: Mirror and Subdivision Surface (Catmull-Clark or simple), applied to a
// polygon mesh in Blender's own space. Internal to the engine (not an installed header): BlendReader.cpp reads the
// modifier settings from the file and calls these; tests/test_blend_modifiers.cpp checks them on hand-built meshes.
//
// Written for this project from general descriptions, not from any other program's code:
//  - Catmull-Clark: E. Catmull and J. Clark, "Recursively generated B-spline surfaces on arbitrary topological
//    meshes" (1978): face points, edge points, vertex points (Q + 2R + (n - 3) S) / n, every n-gon becomes n quads.
//    Boundary edges use the midpoint and boundary vertices the cubic B-spline curve rule (a + 6 S + b) / 8.
//  - Creases: the semi-sharp rules of T. DeRose, M. Kass and T. Truong, "Subdivision Surfaces in Character
//    Animation" (1998): an edge of sharpness s >= 1 uses the sharp rules, 0 < s < 1 blends, children get s - 1.
//  - Mirror: the public description of Blender's Mirror modifier (mirror axes in the object's space or in the space
//    of a mirror object, merge of vertices that lie within the merge distance of their mirror image, U / V flips of
//    the mirrored copy's texture coordinates, vertex group names swapped between left and right).
#pragma once
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {
namespace blendmod {

// One crease: the edge between two vertices and its sharpness (DeRose's s: >= 1 is fully sharp for one level).
struct Crease { int v0 = 0, v1 = 0; float sharpness = 0.0f; };

// A polygon mesh as the Blender reader holds it: faces are runs of loops (corners), each loop names a vertex.
struct PolyMesh {
    std::vector<Vec3> pos;
    std::vector<int> faceStart, faceCount, faceMat;
    std::vector<char> faceSmooth;
    std::vector<int> loopVert;
    std::vector<Vec2> loopUv;                 // empty or one per loop
    std::vector<std::uint32_t> dvStart;       // empty or pos.size() + 1: vertex v's weights are [dvStart[v], dvStart[v+1])
    std::vector<int> dvGroup;
    std::vector<float> dvWeight;
    std::vector<Crease> creases;              // only edges with a crease
};

struct MirrorSettings {
    bool axis[3] = {true, false, false};
    bool merge = true;
    float tolerance = 0.001f;
    bool flipU = false, flipV = false;
    float flipOffset[2] = {0.0f, 0.0f};       // added to a flipped coordinate (Blender 2.8+ "Offset U / V")
    float copyOffset[2] = {0.0f, 0.0f};       // added to the mirrored copy's UVs (Blender 2.8+ "Offset U / V" of the copy)
    bool useSpace = false;                    // mirror in another space (a mirror object)
    Mat4 toSpace, fromSpace;                  // mesh -> mirror space, and back
    std::vector<int> groupFlip;               // vertex group g of a mirrored vertex becomes groupFlip[g] (empty: unchanged)
};

// Mirrors the mesh across each selected axis in turn (X, then Y, then Z). Returns the number of merged vertices.
int Mirror(PolyMesh& m, const MirrorSettings& s);

struct SubsurfSettings {
    int levels = 1;
    bool simple = false;                      // Blender's "Simple": split faces, no smoothing
    bool smoothUv = true;                     // UVs subdivided with the same rules on their own (seam-aware) topology
    bool useCreases = true;
    bool joinCoincident = true;               // corners at bit-identical positions are one vertex (see Subdivide)
};

// Subdivides `levels` times. Stops before a level whose face count would pass maxFaces; returns the levels done.
int Subdivide(PolyMesh& m, const SubsurfSettings& s, std::size_t maxFaces);

// "Arm.L" <-> "Arm.R", "Left" <-> "Right", "L_hand" <-> "R_hand" (Blender's left / right naming); "" when no flip.
std::string FlipSideName(const std::string& name);

}  // namespace blendmod
}  // namespace okay
