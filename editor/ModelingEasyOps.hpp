#pragma once
// ModelingEasyOps: what each beginner tool of ModelingEasy.hpp does to a Mesh, using the engine's existing operations
// (Stretch, Smooth, Solidify, ExtrudeFaces, InsetFaces, DeleteFaces, BevelVertices, Repair ...). No second copy of any
// algorithm: this is only the mapping from a plain tool id to those calls, so it can be tested without a window.
// Tools that need the editor (add a shape, set the gizmo, collision, save) are not here; the window runs them.
#include "ModelingEasy.hpp"
#include "okay/Modeling/SelectOps.hpp"
#include "okay/Render/Mesh.hpp"
#include <set>
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

/// The pick tools ("Pick whole part", "Pick similar"): they change what is picked, never the mesh.
struct PickResult { bool handled = false, ok = false; std::vector<int> faces, corners; std::string message; };
inline PickResult RunPickTool(const okay::Mesh& m, const std::string& id, const ToolArgs& a) {
    PickResult r;
    if (id != "select.part" && id != "select.similar") return r;
    r.handled = true;
    const int tris = m.TriangleCount();
    const int nv = (int)m.vertices.size();
    if (id == "select.part") {
        int seed = -1;
        if (!a.faces.empty() && a.faces[0] >= 0 && a.faces[0] < tris) seed = m.triangles[(std::size_t)a.faces[0] * 3];
        else if (!a.corners.empty()) seed = a.corners[0];
        if (seed < 0 || seed >= nv) { r.message = "Pick something first."; return r; }
        const std::vector<int> link = m.LinkedVertices(seed);
        const std::set<int> in(link.begin(), link.end());
        if (!a.faces.empty()) {
            for (int t = 0; t < tris; ++t)
                if (in.count(m.triangles[(std::size_t)t * 3]) && in.count(m.triangles[(std::size_t)t * 3 + 1]) && in.count(m.triangles[(std::size_t)t * 3 + 2])) r.faces.push_back(t);
        } else r.corners = link;
        r.ok = true;
    } else {
        if (a.faces.empty()) { r.message = "Select at least one face first."; return r; }
        for (int f : a.faces) if (f < 0 || f >= tris) { r.message = "Select at least one face first."; return r; }
        r.faces = okay::modeling::SelectSimilar(m, a.faces, okay::modeling::SimilarKind::Normal, 5.0f);
        r.ok = true;
    }
    return r;
}

/// Tools whose result depends on the "How much" slider (they can be previewed while it is dragged).
inline bool UsesStrength(const std::string& id) { return id == "squash" || id == "smooth" || id == "round.it" || id == "round.corners"; }

/// Live preview for the "How much" slider. While the slider moves, the tool is run on a copy of the model as it was when the drag
/// started (so the preview never piles up); when the slider is let go, End() puts the model back exactly and the caller runs the tool
/// once for real (one undo step). Cost while dragging: one copy of the mesh plus one run of the tool per moved frame, nothing when idle.
struct StrengthPreview {
    bool active = false;
    okay::Mesh before;
    void Update(okay::Mesh& m, const std::string& id, const ToolArgs& a) {
        if (!active) { before = m; active = true; }
        m = before;
        RunMeshTool(m, id, a);
    }
    /// True when a preview was on screen (the mesh is back to what it was).
    bool End(okay::Mesh& m) { if (!active) return false; m = before; before = okay::Mesh(); active = false; return true; }
};

} // namespace oked::easymodel
