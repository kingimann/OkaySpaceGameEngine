#pragma once
// ModelingEasyOps: what each beginner tool of ModelingEasy.hpp does to a Mesh, using the engine's existing operations
// (Stretch, Smooth, Solidify, ExtrudeFaces, InsetFaces, DeleteFaces, BevelVertices, Repair ...). No second copy of any
// algorithm: this is only the mapping from a plain tool id to those calls, so it can be tested without a window.
// Tools that need the editor (add a shape, set the gizmo, collision, save) are not here; the window runs them.
#include "ModelingEasy.hpp"
#include "okay/Render/Mesh.hpp"
#include <string>
#include <vector>

namespace oked::easymodel {

struct ToolArgs {
    std::vector<int> faces;    // picked triangle numbers (face mode)
    std::vector<int> corners;  // picked vertex numbers (corner mode)
    float strength = 0.5f;     // the one slider of "Smooth it" and "Round it", 0.1 to 1
    float r = 0.8f, g = 0.3f, b = 0.3f;   // paint colour
};
struct ToolResult { bool handled = false; bool ok = false; std::string message; };

inline float ClampStrength(float s) { return !(s == s) ? 0.5f : (s < 0.1f ? 0.1f : (s > 1.0f ? 1.0f : s)); }

/// Run a mesh-only tool. `handled` is false for ids that belong to the window (adding shapes, move / turn / bigger, collision, save).
inline ToolResult RunMeshTool(okay::Mesh& m, const std::string& id, const ToolArgs& a) {
    ToolResult r; r.handled = true; r.ok = true;
    const float s = ClampStrength(a.strength);
    const int tris = m.TriangleCount();
    auto validFaces = [&] { for (int f : a.faces) if (f < 0 || f >= tris) return false; return !a.faces.empty(); };
    if (id == "squash")             m.Stretch(1, 1.0f - 0.5f * s);
    else if (id == "smooth")        m.Smooth(s);
    else if (id == "mirror")        m.Mirror(0, true);
    else if (id == "cut.half")      m.SliceAxis(1, 1);
    else if (id == "detail.add") {  if (validFaces()) m.SubdivideFaces(a.faces); else m.Subdivide(); }
    else if (id == "round.it")      m.SubdivideSmooth(1, 0.25f + 0.5f * s);
    else if (id == "hollow")        m.Solidify(0.1f);
    else if (id == "push.out")    { if (!validFaces()) { r.ok = false; r.message = "Select at least one face first."; } else m.ExtrudeFaces(a.faces, 0.3f); }
    else if (id == "make.hole")   { if (!validFaces()) { r.ok = false; r.message = "Select at least one face first."; } else { m.InsetFaces(a.faces, 0.3f); m.DeleteFaces(a.faces); } }
    else if (id == "delete.faces"){ if (!validFaces()) { r.ok = false; r.message = "Select at least one face first."; } else m.DeleteFaces(a.faces); }
    else if (id == "round.corners") {
        if (a.corners.empty()) { r.ok = false; r.message = "Select at least one corner first."; }
        else m.BevelVertices(a.corners, 0.05f + 0.25f * s);
    }
    else if (id == "paint.all" || id == "paint.faces") {
        const okay::Color c{a.r, a.g, a.b, 1.0f};
        if (id == "paint.faces" && !validFaces()) { r.ok = false; r.message = "Select at least one face first."; }
        else {
            if (!m.HasFaceColors()) m.triColors.assign((std::size_t)tris, okay::Color{1, 1, 1, 1});
            if (id == "paint.all") for (okay::Color& t : m.triColors) t = c;
            else for (int f : a.faces) m.triColors[(std::size_t)f] = c;
        }
    }
    else if (id == "fix.problems") {
        const auto before = m.Topology();
        const auto rep = m.Repair();
        r.message = FixReport(before.boundaryEdges, rep.openEdgesAfter, rep.mergedVerts);
        r.ok = rep.ClosedAfter();
    }
    else if (id == "on.ground")     m.GroundPivot();
    else r.handled = false;
    return r;
}

} // namespace oked::easymodel
