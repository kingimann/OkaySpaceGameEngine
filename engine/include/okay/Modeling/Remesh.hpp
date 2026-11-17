#pragma once
// Quad remesh: turn a triangle mesh into a mesh made mostly (or only) of quads, with fewer faces, that still follows the
// original surface. Our own method, written from the general idea of pairing triangles into quads and a subdivide-and-snap step:
//   1. cut the triangle count with our edge-collapse simplifier (about target / 2 triangles),
//   2. pair neighbouring triangles into quads, best-shaped first (square corners, flat, convex),
//   3. optionally split every face into quads (a triangle gives 3, a quad gives 4), so the result has no triangles,
//   4. relax the points (smooth them out) and snap them back onto the original surface so the shape is kept.
// Limits (plain words): it does not follow flow lines such as the loops around an eye or a finger; the quads come from the
// triangle layout, not from the shape. Odd corners (poles) appear where the triangles did not pair up. Sharp edges are softened a
// little by the relax step. Open meshes work; holes and borders stay where they are. Slots, colours and UVs are not kept.
#include "okay/Modeling/PolyMesh.hpp"
#include "okay/Render/Mesh.hpp"
#include <string>

namespace okay::modeling {

/// Pair triangles into quads. Two triangles that share an edge become one quad when the four corners are all convex, the two
/// triangles are within `maxAngleDegrees` of flat against each other and the quad's worst corner is within `minQuality`
/// (0..1, 1 = four right angles). Best pairs are taken first. `leftover` (optional) gets the number of triangles left single.
/// The vertices keep their numbers. Bad triangle indices are skipped.
PolyMesh TrisToQuads(const Mesh& m, float maxAngleDegrees = 40.0f, float minQuality = 0.3f, int* leftover = nullptr);

struct QuadRemeshOptions {
    int  targetQuads = 400;       ///< about how many faces to end with (kept between 8 and 100000)
    bool allQuads = true;         ///< false: quads where triangles paired and triangles elsewhere; true: split everything into quads
    int  relaxIterations = 6;     ///< smooth + snap rounds (0..50)
    bool snapToSource = true;     ///< pull the points back onto the original surface after each smooth
};
struct QuadRemeshReport {
    bool ok = false;
    std::string error;
    int inputTriangles = 0, simplifiedTriangles = 0, pairedQuads = 0, leftoverTriangles = 0, outputQuads = 0, outputTriangles = 0;
};
/// Build a new PolyMesh from the surface of `src`. An empty mesh, or one with no valid triangle, gives ok = false and an empty result.
PolyMesh QuadRemesh(const Mesh& src, const QuadRemeshOptions& opt = QuadRemeshOptions(), QuadRemeshReport* report = nullptr);

} // namespace okay::modeling
