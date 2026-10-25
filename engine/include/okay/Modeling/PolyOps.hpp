#pragma once
// Edge-loop modeling tools on a PolyMesh: loop and ring selection, loop cut,
// edge slide, dissolve, bridge, bevel and knife (split edges / connect corners).
//
// Every tool keeps the numbering of existing vertices (new ones are appended),
// so a selection made by vertex index survives the operation. New corners get
// UVs interpolated from the face they sit in.
#include "okay/Modeling/PolyMesh.hpp"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace okay::modeling {

using EdgeList = std::vector<std::pair<int, int>>;

/// Edges and the faces around them, for walking the topology.
struct PolyTopology {
    explicit PolyTopology(const PolyMesh& pm);
    /// Faces that use the edge a-b (either direction).
    const std::vector<int>& FacesOf(int a, int b) const;
    /// Vertices joined to v by an edge.
    const std::vector<int>& Neighbours(int v) const;
    bool IsEdge(int a, int b) const;
    bool IsBoundary(int a, int b) const { return FacesOf(a, b).size() == 1; }
    /// Corner index of v in face f, or -1.
    int Corner(int f, int v) const;
private:
    const PolyMesh& m_pm;
    std::vector<std::vector<int>> m_nbr;
    std::map<std::pair<int, int>, std::vector<int>> m_edgeFaces;
    std::vector<int> m_none;
};

/// The edge loop through a-b: across 4-way vertices it continues on the edge
/// that shares no face with the one it came in on; along an open border it
/// follows the border. Stops at other vertices or where the loop closes.
/// Includes a-b. Empty if a-b isn't an edge.
EdgeList EdgeLoop(const PolyMesh& pm, int a, int b);
/// The edge ring through a-b: the opposite edges of the quads on either side,
/// face after face, until a non-quad, a border or the start. Includes a-b.
EdgeList EdgeRing(const PolyMesh& pm, int a, int b);

/// Split connected edges into chains of vertices (in order along each chain).
struct EdgeChain { std::vector<int> verts; bool closed = false; };
std::vector<EdgeChain> OrderChains(const EdgeList& edges);

/// Loop cut: cut the ring through a-b with `cuts` new edge loops, evenly
/// spaced. With one cut, `position` (0..1) places it along the ring's edges
/// (0.5 = middle). Returns the new loops' edges (empty if a-b isn't an edge).
EdgeList LoopCut(PolyMesh& pm, int a, int b, int cuts = 1, float position = 0.5f);

/// Edge slide: move each vertex of the edge loop(s) along the edge leaving it
/// to one side of the loop. amount in -1..1 (0 = stay, 1 = all the way to the
/// neighbour on the left of the loop's direction, -1 = to the right).
/// `from`, if given, holds the positions to slide from (for interactive use);
/// otherwise the current positions are used. Returns how many vertices moved.
int EdgeSlide(PolyMesh& pm, const EdgeList& loop, float amount, const std::vector<Vec3>* from = nullptr);

/// Dissolve edges: merge the two faces on each edge into one (border edges are
/// skipped); vertices left with only two edges inside a face are removed too
/// when `cleanVerts` is set. Returns how many edges were dissolved.
int DissolveEdges(PolyMesh& pm, const EdgeList& edges, bool cleanVerts = true);
/// Dissolve vertices: merge the faces around each vertex into one, without it.
int DissolveVerts(PolyMesh& pm, const std::vector<int>& verts);

/// Bridge two open borders (edge loops on the mesh's edge, e.g. the ends of two
/// tubes) with a band of quads. The loops need the same number of edges.
/// Returns the number of faces made, or 0 with the reason in `error`.
int BridgeEdgeLoops(PolyMesh& pm, const EdgeList& edges, std::string* error = nullptr);

/// Bevel edges to the given width: each edge becomes a strip of `segments`
/// faces (1 = a flat chamfer). `profile` shapes a multi-segment bevel: 0.5 is
/// round, lower is flatter, 1 is pulled out toward the old sharp corner.
/// Corners where three or more bevels meet get one patch face. Edges must have
/// a face on both sides and run through vertices surrounded by faces. Returns
/// the number of edges beveled, or 0 with the reason in `error`.
int BevelEdges(PolyMesh& pm, const EdgeList& edges, float width, std::string* error = nullptr,
               int segments = 1, float profile = 0.5f);

/// Split the edge a-b at t (0 = a, 1 = b): a new vertex is added to every face
/// using it. Returns the new vertex, or -1.
int SplitEdge(PolyMesh& pm, int a, int b, float t);
/// Split face f along a new edge between its corners u and v (which must not
/// already be neighbours). Returns the new face, or -1.
int SplitFace(PolyMesh& pm, int f, int u, int v);
/// Knife: a cut through a sequence of points, each on an edge (a, b, t) or a
/// vertex (b < 0). Consecutive points must share a face, which is split
/// between them. Returns the new edges, or empty with the reason in `error`.
struct KnifePoint { int a = -1, b = -1; float t = 0; };
EdgeList KnifeCut(PolyMesh& pm, const std::vector<KnifePoint>& points, std::string* error = nullptr);

/// Fill: make a face from each selected loop of border edges (a hole's rim), or
/// from a single open chain of 3+ vertices. Faces it makes run the opposite way
/// to their neighbours (consistent normals). Returns how many faces were made.
int FillFaces(PolyMesh& pm, const EdgeList& edges, std::string* error = nullptr);
/// Grid fill: close a hole whose rim is one loop of an even number of edges
/// with a grid of quads (shaped to blend the four sides). `span` is the number
/// of quads along the first side (0 = about a quarter of the rim). Returns the
/// number of faces made, or 0 with the reason in `error`.
int GridFill(PolyMesh& pm, const EdgeList& edges, int span = 0, std::string* error = nullptr);

/// Merge vertices into one at `at` (faces that collapse are removed). The first
/// vertex in the list is the one kept. Returns how many vertices were merged away.
int MergeVerts(PolyMesh& pm, const std::vector<int>& verts, const Vec3& at);

/// Drop vertices no face uses. Returns old index -> new index (-1 = removed),
/// for remapping a selection.
std::vector<int> CompactVerts(PolyMesh& pm);

} // namespace okay::modeling
