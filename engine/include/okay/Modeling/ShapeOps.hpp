#pragma once
// The detail / pivot buttons that appear both in the Modeling window (Edit page) and in the Mesh Renderer inspector:
// Subdivide, Subdiv + Smooth, Smooth (round), Weld, Recenter, Ground and Fit 1u. Both panels run them through this
// one function (and one undo step each, ModelOpUndo.hpp), so the two copies can no longer drift apart. Our own code.
#include "okay/Render/Mesh.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace okay::modeling {

enum class ShapeOp { Subdivide, SubdivSmooth, SmoothRound, Weld, Recenter, Ground, Fit1u, Count };

/// The button label (without an ImGui id) and the undo-history name of each operation.
inline const char* ShapeOpLabel(ShapeOp op) {
    switch (op) {
        case ShapeOp::Subdivide:    return "Subdivide";
        case ShapeOp::SubdivSmooth: return "Subdiv + Smooth";
        case ShapeOp::SmoothRound:  return "Smooth";
        case ShapeOp::Weld:         return "Weld";
        case ShapeOp::Recenter:     return "Recenter";
        case ShapeOp::Ground:       return "Ground";
        case ShapeOp::Fit1u:        return "Fit 1u";
        default:                    return "";
    }
}

/// The tooltip of each operation.
inline const char* ShapeOpTip(ShapeOp op) {
    switch (op) {
        case ShapeOp::Subdivide:    return "Split every face into 4. Select faces first (Edit Mesh) to subdivide just those.";
        case ShapeOp::SubdivSmooth: return "Quadruple the triangles and relax them: turns a low-poly blockout into smooth high-poly.";
        case ShapeOp::SmoothRound:  return "Subdivide and push every vertex onto a sphere: round a cube or icosphere into a ball.";
        case ShapeOp::Weld:         return "Merge vertices that sit at the same point (cleans up imports and combined meshes).";
        case ShapeOp::Recenter:     return "Move the mesh so the middle of its bounds is at the pivot.";
        case ShapeOp::Ground:       return "Move the pivot to the base: centred in X/Z, lowest point at Y = 0.";
        case ShapeOp::Fit1u:        return "Scale uniformly so the largest side is 1 unit.";
        default:                    return "";
    }
}

/// Run one operation on `m`. `selFaces` limits Subdivide to those faces (empty = whole mesh). Returns a line for
/// the console (empty when there is nothing to say).
inline std::string ApplyShapeOp(Mesh& m, ShapeOp op, const std::vector<int>& selFaces = {}) {
    switch (op) {
        case ShapeOp::Subdivide:
            if (!selFaces.empty()) m.SubdivideFaces(selFaces); else m.Subdivide();
            return "";
        case ShapeOp::SubdivSmooth:
            m.SubdivideSmooth(1, 0.5f);
            return "Subdivided + smoothed: " + std::to_string(m.TriangleCount()) + " triangles";
        case ShapeOp::SmoothRound: {
            const Vec3 sz = m.Size();
            m.Subdivide();
            m.ProjectToSphere(0.5f * std::fmax(sz.x, std::fmax(sz.y, sz.z)));
            return "";
        }
        case ShapeOp::Weld:
            return "Welded " + std::to_string(m.WeldVertices()) + " duplicate verts";
        case ShapeOp::Recenter: m.RecenterToOrigin(); return "";
        case ShapeOp::Ground:   m.GroundPivot();      return "";
        case ShapeOp::Fit1u:    m.ScaleToFit(1.0f);   return "";
        default: return "";
    }
}

} // namespace okay::modeling
