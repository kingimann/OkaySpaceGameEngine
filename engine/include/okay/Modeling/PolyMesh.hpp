#pragma once
// A polygon mesh for modeling: faces are quads and n-gons (ordered vertex
// loops), with a UV per face corner, an optional colour and a material slot
// per face. It is the authoring form of a model; okay::Mesh (triangles) stays
// what the renderer, physics and exporters use, and the two convert both ways:
//
//   PolyMesh::FromMesh(mesh)  welds shared corners and recovers quads from
//                             pairs of coplanar triangles (a triangulated cube
//                             comes back as 6 quads)
//   poly.ToMesh()             triangulates each face (ear clipping, so concave
//                             faces work) and splits vertices only where a
//                             corner's UV differs
//
// Saved as a .okaymesh text file (see Serialize) so a model can be reused
// across objects and scenes.
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Render/Mesh.hpp"
#include <string>
#include <vector>

namespace okay::modeling {

struct PolyFace {
    std::vector<int>  verts;   ///< corner vertex indices, counter-clockwise seen from outside
    std::vector<Vec2> uvs;     ///< one per corner, or empty for none
    int   material = 0;        ///< material slot
    bool  hasColor = false;
    Color color{1, 1, 1, 1};
    int Size() const { return (int)verts.size(); }
};

struct PolyFromMeshOptions {
    bool  weld = true;          ///< merge vertices at the same position
    bool  mergeQuads = true;    ///< recover quads from coplanar triangle pairs
    float planarDegrees = 1.0f; ///< how far from flat a pair may be and still merge
};

class PolyMesh {
public:
    std::vector<Vec3>     verts;
    std::vector<PolyFace> faces;

    /// An edge between two vertices and the faces on either side.
    struct Edge {
        int a = -1, b = -1;        ///< a < b
        int faceCount = 0;         ///< 1 = boundary, 2 = interior, more = non-manifold
        int f0 = -1, f1 = -1;      ///< the first two faces using it
    };
    std::vector<Edge> Edges() const;

    /// Soundness check. Closed = no boundary and no non-manifold edges.
    struct Report {
        int boundaryEdges = 0, nonManifoldEdges = 0, degenerateFaces = 0, badIndices = 0, unusedVerts = 0;
        bool Closed() const { return boundaryEdges == 0 && nonManifoldEdges == 0; }
        bool Valid() const { return degenerateFaces == 0 && badIndices == 0; }
    };
    Report Check() const;
    /// V - E + F (2 for anything shaped like a sphere, 0 for a torus).
    int EulerCharacteristic() const;
    int CountFaces(int corners) const;   ///< faces with exactly this many corners
    int CountNgons() const;              ///< faces with more than four corners

    using FromMeshOptions = PolyFromMeshOptions;
    static PolyMesh FromMesh(const Mesh& mesh, const FromMeshOptions& opt = FromMeshOptions());
    /// Triangles for rendering. `triFace`, if given, receives the face each triangle came from.
    Mesh ToMesh(std::vector<int>* triFace = nullptr) const;
    /// Face normal (Newell's method, so it works for any planar or near-planar polygon).
    Vec3 FaceNormal(int face) const;
    /// Triangulate one face into corner positions within that face (triples of 0..n-1).
    std::vector<int> TriangulateFace(int face) const;

    // ---- .okaymesh files -------------------------------------------------
    std::string Serialize() const;
    static bool Parse(const std::string& text, PolyMesh& out, std::string* error = nullptr);
    bool Save(const std::string& path, std::string* error = nullptr) const;
    static bool Load(const std::string& path, PolyMesh& out, std::string* error = nullptr);
};

/// True for paths ending in ".okaymesh" (any case).
bool IsOkayMeshPath(const std::string& path);

} // namespace okay::modeling
