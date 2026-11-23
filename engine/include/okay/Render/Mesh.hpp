#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Render/TexturePath.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <functional>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace okay {

/// A simple indexed triangle mesh (positions + triangle indices). Enough to
/// describe 3D geometry; the editor renders these as wireframes and a future
/// GPU backend can upload them directly. `name` tags built-in primitives so they
/// can be serialized compactly.
/// A subdivision crease: the edge between vertices a and b stays sharp for `w` levels of subdivision (1 = one level).
struct EdgeCrease { int a = 0, b = 0; float w = 0.0f; };

struct Mesh {
    std::string       name;       // "Cube"/"Pyramid"/"Quad" for primitives
    std::vector<Vec3> vertices;
    std::vector<int>  triangles;  // 3 indices per triangle
    std::vector<Vec2> uvs;        // optional per-vertex UVs (parallel to vertices)
    std::vector<Color> triColors; // optional per-triangle colors (parallel to faces);
                                  // used by the renderer only when fully populated.
    std::vector<int>  polyFace;   // optional per-triangle polygon id (parallel to faces):
                                  // triangles with the same id make one quad / n-gon, so
                                  // modeling::PolyMesh gets the exact polygons back. Ignored
                                  // when its size doesn't match the triangle count.
    std::vector<Vec2> cornerUVs;  // optional UV per triangle corner (parallel to triangles).
                                  // Mesh edit mode keeps a mesh welded (one vertex per
                                  // position) with its exact UVs here, so seams survive;
                                  // SplitCornerUVs() turns them back into per-vertex uvs
                                  // for drawing. Ignored when its size doesn't match.
    std::vector<std::pair<int, int>> seams;  // UV seam edges (vertex index pairs): unwrapping
                                  // cuts the surface open along them. A pair that isn't an
                                  // edge of the mesh is ignored.
    std::vector<EdgeCrease> bevelWeights; // per-edge bevel weights (0..1, scaling the Bevel modifier's width when it is set to use weights)
    std::vector<EdgeCrease> creases; // subdivision creases (vertex pairs + sharpness); the Subdivision modifier keeps those edges sharp.
                                  // A pair that isn't an edge of the mesh is ignored.
    std::vector<Color> vertexColors; // optional per-vertex colours (parallel to vertices); the renderers blend them smoothly
                                  // across each face and multiply them with the face colour and material colour.
                                  // Ignored when its size doesn't match the vertex count.
    std::vector<int>  triSlot;    // optional per-triangle material slot (parallel to faces).
                                  // Ignored when its size doesn't match the triangle count.
    std::vector<Color> slotColors;// the colour of each material slot (slot i -> slotColors[i]).
                                  // ApplySlotColors() paints triColors from the two lists.
    // Hidden geometry (Hide / Isolate in mesh edit mode): triangles taken out of the mesh and kept here by
    // value (corner positions, not vertex numbers, so later edits that renumber the vertices cannot break
    // them). Renderers, colliders and tools never see them; RevealHidden() puts them back.
    std::vector<Vec3>  hiddenPos;    // 3 per hidden triangle
    std::vector<Vec2>  hiddenUV;     // 3 per hidden triangle, or empty when the mesh had no UVs
    std::vector<int>   hiddenSlot;   // 1 per hidden triangle (-1 = none)
    std::vector<int>   hiddenPoly;   // 1 per hidden triangle: its polygon id (-1 = none)
    std::vector<Color> hiddenColor;  // 1 per hidden triangle, or empty when the mesh had no face colours
    std::vector<Vec3> normals;    // optional per-vertex normals (parallel to vertices);
                                  // when present the renderer smooth (Gouraud) shades.
    float autoSmoothAngle = 0.0f; // >0: normals came from ComputeAutoSmoothNormals(angle)
                                  // (serialized so the shading survives save/load)

    int TriangleCount() const { return static_cast<int>(triangles.size() / 3); }
    bool HasFaceColors() const { return (int)triColors.size() == TriangleCount() && !triColors.empty(); }
    /// Set (w > 0, at most 1) or clear (w <= 0) the bevel weight of the edge between vertices a and b.
    void SetBevelWeight(int a, int b, float w) {
        if (a == b || a < 0 || b < 0 || !std::isfinite(w)) return;
        if (a > b) std::swap(a, b);
        for (std::size_t i = 0; i < bevelWeights.size(); ++i)
            if (bevelWeights[i].a == a && bevelWeights[i].b == b) { if (w > 0.0f) bevelWeights[i].w = std::min(w, 1.0f); else bevelWeights.erase(bevelWeights.begin() + i); return; }
        if (w > 0.0f) bevelWeights.push_back({a, b, std::min(w, 1.0f)});
    }
    /// Set (w > 0) or clear (w <= 0) the subdivision crease on the edge between vertices a and b.
    void SetCrease(int a, int b, float w) {
        if (a == b || a < 0 || b < 0 || !std::isfinite(w)) return;
        if (a > b) std::swap(a, b);
        for (std::size_t i = 0; i < creases.size(); ++i)
            if (creases[i].a == a && creases[i].b == b) { if (w > 0.0f) creases[i].w = std::min(w, 10.0f); else creases.erase(creases.begin() + i); return; }
        if (w > 0.0f) creases.push_back({a, b, std::min(w, 10.0f)});
    }
    bool HasVertexColors() const { return !vertexColors.empty() && vertexColors.size() == vertices.size(); }
    /// Colour of corner vertex `v` (white without vertex colours).
    Color VertexColorOf(int v) const {
        return (HasVertexColors() && v >= 0 && v < (int)vertexColors.size()) ? vertexColors[v] : Color{1, 1, 1, 1};
    }
    /// Paint vertex colours: every vertex within `radius` of `centre` blends toward `c` by `strength` (0..1)
    /// times a smooth falloff (1 at the centre, 0 at the edge; `hardness` 0..1 keeps the centre solid longer).
    /// Creates the colour list (white) on first use. Returns how many vertices changed.
    int PaintVertices(const Vec3& centre, float radius, const Color& c, float strength, float hardness = 0.0f) {
        if (!(radius > 0.0f) || !std::isfinite(radius + centre.x + centre.y + centre.z) || !(strength > 0.0f) || vertices.empty()) return 0;
        strength = std::min(strength, 1.0f); hardness = std::clamp(hardness, 0.0f, 0.99f);
        if (!HasVertexColors()) vertexColors.assign(vertices.size(), Color{1, 1, 1, 1});
        int n = 0;
        const float r2 = radius * radius;
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const float d2 = (vertices[i] - centre).SqrMagnitude();
            if (d2 > r2) continue;
            float t = 1.0f - std::sqrt(d2) / radius;                       // 1 at the centre
            t = hardness > 0.0f ? std::min(1.0f, t / (1.0f - hardness)) : t;
            const float w = strength * t * t * (3.0f - 2.0f * t);          // smoothstep
            Color& o = vertexColors[i];
            o.r += (c.r - o.r) * w; o.g += (c.g - o.g) * w; o.b += (c.b - o.b) * w; o.a += (c.a - o.a) * w;
            ++n;
        }
        return n;
    }
    /// After a tool added vertices (extrude, inset ...) the colour list is shorter than the vertex list: pad it with white so the
    /// paint is not thrown away. A list that is LONGER (vertices were removed or renumbered) cannot be matched up and is dropped.
    /// Returns true if the list was changed.
    bool SyncVertexColors() {
        if (vertexColors.empty() || vertexColors.size() == vertices.size()) return false;
        if (vertexColors.size() < vertices.size()) vertexColors.resize(vertices.size(), Color{1, 1, 1, 1});
        else vertexColors.clear();
        return true;
    }
    /// Set the given vertices to exactly `c`. Returns how many were set.
    int FillVertexColors(const std::vector<int>& verts, const Color& c) {
        if (vertices.empty()) return 0;
        if (!HasVertexColors()) vertexColors.assign(vertices.size(), Color{1, 1, 1, 1});
        int n = 0;
        for (int v : verts) if (v >= 0 && v < (int)vertices.size()) { vertexColors[v] = c; ++n; }
        return n;
    }
    /// Blend every vertex colour toward the average of the vertices it shares a triangle with, `passes` times.
    void SmoothVertexColors(int passes = 1) {
        if (!HasVertexColors()) return;
        for (int p = 0; p < std::clamp(passes, 0, 20); ++p) {
            std::vector<Vec3> sum(vertices.size(), Vec3{0, 0, 0}); std::vector<float> asum(vertices.size(), 0.0f); std::vector<int> cnt(vertices.size(), 0);
            for (int t = 0; t < TriangleCount(); ++t) {
                const int a = triangles[t * 3], b = triangles[t * 3 + 1], c = triangles[t * 3 + 2];
                if (a < 0 || b < 0 || c < 0 || a >= (int)vertices.size() || b >= (int)vertices.size() || c >= (int)vertices.size()) continue;
                const int idx[3] = {a, b, c};
                for (int k = 0; k < 3; ++k) for (int j = 0; j < 3; ++j) if (j != k) {
                    const Color& o = vertexColors[idx[j]];
                    sum[idx[k]] += Vec3{o.r, o.g, o.b}; asum[idx[k]] += o.a; ++cnt[idx[k]];
                }
            }
            for (std::size_t i = 0; i < vertices.size(); ++i) if (cnt[i]) {
                Color& o = vertexColors[i];
                o.r = (o.r + sum[i].x / (float)cnt[i]) * 0.5f; o.g = (o.g + sum[i].y / (float)cnt[i]) * 0.5f;
                o.b = (o.b + sum[i].z / (float)cnt[i]) * 0.5f; o.a = (o.a + asum[i] / (float)cnt[i]) * 0.5f;
            }
        }
    }
    int HiddenTriangleCount() const { return (int)(hiddenPos.size() / 3); }
    bool HasHidden() const { return !hiddenPos.empty(); }
    /// True when the hidden lists are consistent with each other (a damaged file could break that).
    bool HiddenConsistent() const {
        const std::size_t n = hiddenPos.size() / 3;
        return hiddenPos.size() % 3 == 0 && hiddenSlot.size() == n && hiddenPoly.size() == n &&
               (hiddenUV.empty() || hiddenUV.size() == n * 3) && (hiddenColor.empty() || hiddenColor.size() == n);
    }
    /// Take the given triangles out of the mesh and keep them hidden. Returns how many were hidden.
    /// Vertices only they used stay in the vertex list (nothing is renumbered), unused.
    int HideTriangles(const std::vector<int>& tris) {
        AttrGuard attrGuard_(*this);
        if (!HiddenConsistent()) { hiddenPos.clear(); hiddenUV.clear(); hiddenSlot.clear(); hiddenPoly.clear(); hiddenColor.clear(); }
        const int nt = TriangleCount();
        std::vector<char> hide((std::size_t)nt, 0);
        int count = 0;
        for (int t : tris) {
            if (t < 0 || t >= nt || hide[t]) continue;
            bool ok = true;
            for (int k = 0; k < 3; ++k) { const int v = triangles[t * 3 + k]; if (v < 0 || v >= (int)vertices.size()) ok = false; }
            if (!ok) continue;
            hide[t] = 1; ++count;
        }
        if (!count) return 0;
        const bool uvv = HasUVs(), cuv = HasCornerUVs(), fc = HasFaceColors(), sl = HasSlots();
        const bool pf = (int)polyFace.size() == nt && !polyFace.empty();
        // Keep the UV list in step: it is either complete (every hidden triangle has it) or empty.
        const bool keepUV = uvv || cuv;
        if (keepUV && hiddenUV.empty() && !hiddenPos.empty()) hiddenUV.assign(hiddenPos.size(), Vec2{0, 0});
        if (!keepUV && !hiddenUV.empty()) hiddenUV.insert(hiddenUV.end(), (std::size_t)count * 3, Vec2{0, 0});
        if (fc && hiddenColor.empty() && !hiddenPos.empty()) hiddenColor.assign(hiddenPos.size() / 3, Color{1, 1, 1, 1});
        std::vector<int> tri2, slot2, poly2; std::vector<Color> col2; std::vector<Vec2> cuv2;
        for (int t = 0; t < nt; ++t) {
            if (hide[t]) {
                for (int k = 0; k < 3; ++k) {
                    const int v = triangles[t * 3 + k];
                    hiddenPos.push_back(vertices[v]);
                    if (keepUV) hiddenUV.push_back(cuv ? cornerUVs[t * 3 + k] : uvs[v]);
                }
                hiddenSlot.push_back(sl ? triSlot[t] : -1);
                hiddenPoly.push_back(pf ? polyFace[t] : -1);
                if (fc || !hiddenColor.empty()) hiddenColor.push_back(fc ? triColors[t] : Color{1, 1, 1, 1});
                continue;
            }
            tri2.insert(tri2.end(), {triangles[t * 3], triangles[t * 3 + 1], triangles[t * 3 + 2]});
            if (sl) slot2.push_back(triSlot[t]);
            if (pf) poly2.push_back(polyFace[t]);
            if (fc) col2.push_back(triColors[t]);
            if (cuv) cuv2.insert(cuv2.end(), {cornerUVs[t * 3], cornerUVs[t * 3 + 1], cornerUVs[t * 3 + 2]});
        }
        triangles = std::move(tri2);
        if (sl) triSlot = std::move(slot2);
        if (pf) polyFace = std::move(poly2);
        if (fc) triColors = std::move(col2);
        if (cuv) cornerUVs = std::move(cuv2);
        name = "";
        return count;
    }
    /// Hide every triangle except the given ones (local view). Returns how many were hidden.
    int IsolateTriangles(const std::vector<int>& keep) {
        AttrGuard attrGuard_(*this);
        const int nt = TriangleCount();
        std::vector<char> k((std::size_t)nt, 0);
        for (int t : keep) if (t >= 0 && t < nt) k[t] = 1;
        std::vector<int> rest;
        for (int t = 0; t < nt; ++t) if (!k[t]) rest.push_back(t);
        return HideTriangles(rest);
    }
    /// Put every hidden triangle back (appended after the visible ones). Corners that match an existing
    /// vertex position reuse it, so the revealed faces join up with what is there. Returns the numbers of the
    /// triangles it added (so the editor can select them).
    std::vector<int> RevealHidden() {
        AttrGuard attrGuard_(*this);
        std::vector<int> added;
        if (!HiddenConsistent() || hiddenPos.empty()) { hiddenPos.clear(); hiddenUV.clear(); hiddenSlot.clear(); hiddenPoly.clear(); hiddenColor.clear(); return added; }
        const int n = HiddenTriangleCount();
        std::map<std::tuple<long, long, long>, int> at;
        auto key = [](const Vec3& p) { return std::make_tuple((long)std::lround(p.x * 1e5f), (long)std::lround(p.y * 1e5f), (long)std::lround(p.z * 1e5f)); };
        for (int v = 0; v < (int)vertices.size(); ++v) at.emplace(key(vertices[v]), v);
        const bool uvv = HasUVs(), cuvWanted = HasCornerUVs(), sl = HasSlots(), fc = HasFaceColors();
        const bool hadPoly = (int)polyFace.size() == TriangleCount() && !polyFace.empty();
        int polyBase = 0;
        if (hadPoly) for (int id : polyFace) polyBase = std::max(polyBase, id + 1);
        const bool hasHidUV = !hiddenUV.empty();
        if (!uvv && !uvs.empty()) uvs.clear();
        const bool padUV = uvv;                               // new vertices need a UV entry too
        const bool padNrm = HasNormals();
        const bool padVC = HasVertexColors();
        const int firstNew = TriangleCount();
        std::map<int, int> polyMap;                            // hidden polygon id -> id in this mesh
        const bool anyHiddenPoly = std::any_of(hiddenPoly.begin(), hiddenPoly.end(), [](int p) { return p >= 0; });
        for (int t = 0; t < n; ++t) {
            for (int k = 0; k < 3; ++k) {
                const Vec3& p = hiddenPos[t * 3 + k];
                auto it = at.find(key(p));
                int v;
                if (it == at.end()) {
                    v = (int)vertices.size();
                    vertices.push_back(p);
                    if (padUV) uvs.push_back(hasHidUV ? hiddenUV[t * 3 + k] : Vec2{0, 0});
                    if (padNrm) normals.push_back(Vec3{0, 1, 0});
                    if (padVC) vertexColors.push_back(Color{1, 1, 1, 1});
                    at.emplace(key(p), v);
                } else v = it->second;
                triangles.push_back(v);
                if (cuvWanted) cornerUVs.push_back(hasHidUV ? hiddenUV[t * 3 + k] : Vec2{0, 0});
            }
            if (sl) triSlot.push_back(hiddenSlot[t] >= 0 ? hiddenSlot[t] : 0);
            if (fc) triColors.push_back(hiddenColor.empty() ? Color{1, 1, 1, 1} : hiddenColor[t]);
            if (hadPoly || anyHiddenPoly) {
                int id = hiddenPoly[t];
                if (id >= 0) { auto it = polyMap.find(id); if (it == polyMap.end()) it = polyMap.emplace(id, polyBase + (int)polyMap.size()).first; id = it->second; }
                else id = polyBase + 100000 + t;
                if ((int)polyFace.size() == TriangleCount() - 1) polyFace.push_back(id);
            }
            added.push_back(firstNew + t);
        }
        // A mesh whose polygon list could not be kept in step falls back to recovering quads from the triangles.
        if (!polyFace.empty() && (int)polyFace.size() != TriangleCount()) polyFace.clear();
        hiddenPos.clear(); hiddenUV.clear(); hiddenSlot.clear(); hiddenPoly.clear(); hiddenColor.clear();
        name = "";
        RefreshNormals();
        return added;
    }
    bool HasSlots() const { return !triSlot.empty() && (int)triSlot.size() == TriangleCount(); }
    /// Number of material slots in use: the table size, or one past the highest slot assigned.
    int SlotCount() const {
        int n = (int)slotColors.size();
        if (HasSlots()) for (int s : triSlot) n = std::max(n, s + 1);
        return n;
    }
    /// Give the chosen triangles to `slot` (creating the triangle list, all in slot 0,
    /// on first use) and grow the colour table (white) to hold it. Returns how many
    /// triangles changed slot. Out-of-range triangle numbers and negative slots are ignored.
    int AssignSlot(const std::vector<int>& tris, int slot) {
        if (slot < 0 || slot > 255) return 0;
        if (!HasSlots()) triSlot.assign((std::size_t)TriangleCount(), 0);
        while ((int)slotColors.size() <= slot) slotColors.push_back(Color{1, 1, 1, 1});
        int n = 0;
        for (int t : tris)
            if (t >= 0 && t < TriangleCount() && triSlot[t] != slot) { triSlot[t] = slot; ++n; }
        return n;
    }
    /// The triangles grouped by material slot (stable: within a slot they keep their order): `order` lists the triangle
    /// numbers slot by slot, `groups` has {slot, first position in `order`, count} for each slot present, smallest slot
    /// first. Without slots there is one group (slot 0) holding every triangle. The OpenGL renderer draws one range per
    /// group so each slot can have its own picture.
    struct SlotGroups { std::vector<int> order; std::vector<std::array<int, 3>> groups; };
    SlotGroups GroupBySlot() const {
        SlotGroups g;
        const int nt = TriangleCount();
        g.order.resize((std::size_t)nt);
        for (int t = 0; t < nt; ++t) g.order[t] = t;
        if (HasSlots()) std::stable_sort(g.order.begin(), g.order.end(), [&](int x, int y) { return triSlot[x] < triSlot[y]; });
        for (int i = 0; i < nt; ++i) {
            const int slot = HasSlots() ? triSlot[g.order[i]] : 0;
            if (g.groups.empty() || g.groups.back()[0] != slot) g.groups.push_back({slot, i, 0});
            ++g.groups.back()[2];
        }
        return g;
    }
    /// Triangles in `slot`.
    std::vector<int> TrianglesInSlot(int slot) const {
        std::vector<int> r;
        if (HasSlots()) for (int t = 0; t < TriangleCount(); ++t) if (triSlot[t] == slot) r.push_back(t);
        return r;
    }
    /// Paint every triangle with its slot's colour (triColors). Slot colours beyond the table are white.
    /// Does nothing without slots. This is how the renderers (which all draw triColors) show slots.
    void ApplySlotColors() {
        if (!HasSlots()) return;
        triColors.resize(triSlot.size());
        for (std::size_t t = 0; t < triSlot.size(); ++t) {
            const int s = triSlot[t];
            triColors[t] = (s >= 0 && s < (int)slotColors.size()) ? slotColors[s] : Color{1, 1, 1, 1};
        }
    }
    /// Remove all slot data (the triangle list and the colour table).
    void ClearSlots() { triSlot.clear(); slotColors.clear(); }
    /// Remove one material slot: its faces fall back to slot 0 (or slot 1, which becomes 0, when slot 0 is the one removed),
    /// later slots move down by one and the colour list loses that entry. The last slot left cannot be removed (use
    /// ClearSlots). Returns false when `slot` is not a slot or is the only one.
    bool RemoveSlot(int slot) {
        const int n = std::max(SlotCount(), (int)slotColors.size());
        if (slot < 0 || slot >= n || n <= 1) return false;
        for (int& t : triSlot) { if (t == slot) t = 0; else if (t > slot) --t; }
        if (slot < (int)slotColors.size()) slotColors.erase(slotColors.begin() + slot);
        return true;
    }
    /// Move slot `from` to position `to` (the slots in between shift by one); faces keep their slot, so what they look like
    /// does not change. False when either number is not a slot.
    bool MoveSlot(int from, int to) {
        const int n = std::max(SlotCount(), (int)slotColors.size());
        if (from < 0 || to < 0 || from >= n || to >= n) return false;
        if (from == to) return true;
        std::vector<int> map((size_t)n);                      // old slot -> new slot
        std::vector<int> order; for (int i = 0; i < n; ++i) if (i != from) order.push_back(i);
        order.insert(order.begin() + to, from);               // order[newSlot] = oldSlot
        for (int ns = 0; ns < n; ++ns) map[(size_t)order[(size_t)ns]] = ns;
        for (int& t : triSlot) if (t >= 0 && t < n) t = map[(size_t)t];
        if ((int)slotColors.size() == n) { std::vector<Color> c((size_t)n); for (int ns = 0; ns < n; ++ns) c[(size_t)ns] = slotColors[(size_t)order[(size_t)ns]]; slotColors = std::move(c); }
        return true;
    }
    bool HasNormals() const { return normals.size() == vertices.size() && !vertices.empty(); }
    bool HasCornerUVs() const { return !cornerUVs.empty() && cornerUVs.size() == triangles.size(); }
    bool HasUVs() const { return uvs.size() == vertices.size() && !uvs.empty(); }

    /// UV seams are vertex pairs, so an operator that renumbers or splits vertices breaks them. Rebuild them from the old
    /// mesh by position: a seam whose two ends are still vertices at the same places stays a seam, and when an end moved
    /// to the middle of the old seam (the edge was split) both halves are seams.
    void RemapSeams(const Mesh& old) {
        if (old.seams.empty()) return;
        std::map<std::tuple<float, float, float>, int> at;
        for (int i = 0; i < (int)vertices.size(); ++i) at.emplace(std::make_tuple(vertices[(size_t)i].x, vertices[(size_t)i].y, vertices[(size_t)i].z), i);
        auto find = [&](int hint, const Vec3& p) {   // the same index when that vertex did not move, else a vertex at the same place
            if (hint >= 0 && hint < (int)vertices.size() && vertices[(size_t)hint] == p) return hint;
            auto it = at.find(std::make_tuple(p.x, p.y, p.z)); return it == at.end() ? -1 : it->second; };
        std::set<std::pair<int, int>> out;
        auto add = [&](int a, int b) { if (a >= 0 && b >= 0 && a != b) out.insert({std::min(a, b), std::max(a, b)}); };
        for (const auto& sm : old.seams) {
            if (sm.first < 0 || sm.second < 0 || sm.first >= (int)old.vertices.size() || sm.second >= (int)old.vertices.size()) continue;
            const Vec3 pa = old.vertices[(size_t)sm.first], pb = old.vertices[(size_t)sm.second];
            const int a = find(sm.first, pa), b = find(sm.second, pb), m = find(-1, (pa + pb) * 0.5f);
            if (a >= 0 && b >= 0) add(a, b);
            else if (a >= 0 && b < 0 && m >= 0) add(a, m);
            else if (b >= 0 && a < 0 && m >= 0) add(m, b);
            if (a >= 0 && b >= 0 && m >= 0 && m != a && m != b) { out.erase({std::min(a, b), std::max(a, b)}); add(a, m); add(m, b); }
        }
        seams.assign(out.begin(), out.end());
    }
    /// After an operator that added, removed or reordered vertices or faces: give back what it left at the wrong
    /// size. `old` is the mesh before the operator. A new vertex takes the UV and vertex colour of the nearest old
    /// vertex, a new face the material slot and face colour of the old face whose middle is nearest (so extruded,
    /// inset and subdivided faces keep the look of the face they came from). Vertices and faces that did not move
    /// keep their own values exactly. Polygon ids and per-corner UVs cannot be carried over this way and are
    /// dropped (the editor rebuilds polygons from the triangles). Lists that are already the right size are not touched.
    void InheritAttributes(const Mesh& old) {
        const int nv = (int)vertices.size(), nt = TriangleCount();
        if (!polyFace.empty() && (int)polyFace.size() != nt) polyFace.clear();
        const bool needCU = old.HasCornerUVs() && !HasCornerUVs();
        if (!needCU && !cornerUVs.empty() && cornerUVs.size() != triangles.size()) cornerUVs.clear();
        RemapSeams(old);
        const bool needUV = old.HasUVs() && !HasUVs(), needVC = old.HasVertexColors() && !HasVertexColors();
        const bool needSlot = old.HasSlots() && !HasSlots(), needFC = old.HasFaceColors() && !HasFaceColors();
        if (nv == 0 || (!needUV && !needVC && !needSlot && !needFC && !needCU)) return;
        struct Grid {
            float cell = 1.0f; std::unordered_map<long long, std::vector<int>> cells; const std::vector<Vec3>* pts = nullptr;
            long long Key(int x, int y, int z) const { return ((long long)(x & 0x1FFFFF) << 42) ^ ((long long)(y & 0x1FFFFF) << 21) ^ (long long)(z & 0x1FFFFF); }
            void Build(const std::vector<Vec3>& p) {
                pts = &p; Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
                for (const Vec3& v : p) { lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)}; hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)}; }
                const float ext = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-3f});
                cell = std::max(ext / std::max(1.0f, std::cbrt((float)p.size())), 1e-4f);
                for (int i = 0; i < (int)p.size(); ++i) cells[Key((int)std::floor(p[i].x / cell), (int)std::floor(p[i].y / cell), (int)std::floor(p[i].z / cell))].push_back(i);
            }
            std::vector<int> NearestK(const Vec3& q, int K) const {   // the K points nearest to q (fewer when there are fewer)
                std::vector<std::pair<float, int>> got;
                if (!pts || pts->empty()) return {};
                const int cx = (int)std::floor(q.x / cell), cy = (int)std::floor(q.y / cell), cz = (int)std::floor(q.z / cell);
                for (int r = 0; r <= 8; ++r) {
                    for (int x = cx - r; x <= cx + r; ++x) for (int y = cy - r; y <= cy + r; ++y) for (int z = cz - r; z <= cz + r; ++z) {
                        if (std::max({std::abs(x - cx), std::abs(y - cy), std::abs(z - cz)}) != r) continue;
                        auto it = cells.find(Key(x, y, z)); if (it == cells.end()) continue;
                        for (int i : it->second) got.push_back({((*pts)[i] - q).SqrMagnitude(), i});
                    }
                    if ((int)got.size() >= K) { std::nth_element(got.begin(), got.begin() + (K - 1), got.end()); if (std::sqrt(got[(size_t)K - 1].first) <= (float)r * cell) break; }
                }
                std::sort(got.begin(), got.end());
                std::vector<int> out; for (size_t i = 0; i < got.size() && (int)i < K; ++i) out.push_back(got[i].second);
                return out;
            }
            int Nearest(const Vec3& q) const {
                if (!pts || pts->empty()) return -1;
                const int cx = (int)std::floor(q.x / cell), cy = (int)std::floor(q.y / cell), cz = (int)std::floor(q.z / cell);
                int best = -1; float bd = 1e30f;
                for (int r = 0; r <= 6; ++r) {
                    for (int x = cx - r; x <= cx + r; ++x) for (int y = cy - r; y <= cy + r; ++y) for (int z = cz - r; z <= cz + r; ++z) {
                        if (std::max({std::abs(x - cx), std::abs(y - cy), std::abs(z - cz)}) != r) continue;   // only the new shell
                        auto it = cells.find(Key(x, y, z)); if (it == cells.end()) continue;
                        for (int i : it->second) { const float d = ((*pts)[i] - q).SqrMagnitude(); if (d < bd) { bd = d; best = i; } }
                    }
                    if (best >= 0 && std::sqrt(bd) <= (float)r * cell) break;
                }
                return best;
            }
        };
        auto centroids = [](const Mesh& m) {
            std::vector<Vec3> c((size_t)m.TriangleCount());
            for (int t = 0; t < m.TriangleCount(); ++t) {
                Vec3 sum{0, 0, 0};
                for (int k = 0; k < 3; ++k) { const int i = m.triangles[t * 3 + k]; if (i >= 0 && i < (int)m.vertices.size()) sum = sum + m.vertices[i]; }
                c[(size_t)t] = sum * (1.0f / 3.0f);
            }
            return c;
        };
        if (needUV || needVC) {
            Grid g; g.Build(old.vertices);
            std::vector<Vec2> nu; std::vector<Color> nc;
            for (int i = 0; i < nv; ++i) {
                int src = (i < (int)old.vertices.size() && old.vertices[(size_t)i] == vertices[(size_t)i]) ? i : g.Nearest(vertices[(size_t)i]);
                if (src < 0) src = 0;
                if (needUV) nu.push_back(old.uvs[(size_t)src]);
                if (needVC) nc.push_back(old.vertexColors[(size_t)src]);
            }
            if (needUV) uvs = std::move(nu);
            if (needVC) vertexColors = std::move(nc);
        }
        if (needSlot || needFC || needCU) {
            const std::vector<Vec3> oc = centroids(old), nc = centroids(*this);
            Grid g; g.Build(oc);
            // The old face a new face grew from: of the few old faces whose middles are nearest, the one whose surface
            // is closest to the new face's middle (a face on a big flat triangle is not nearest by its middle alone).
            auto distToOld = [&](int ot, const Vec3& p) {
                const int i0 = old.triangles[(size_t)ot * 3], i1 = old.triangles[(size_t)ot * 3 + 1], i2 = old.triangles[(size_t)ot * 3 + 2];
                if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= (int)old.vertices.size() || i1 >= (int)old.vertices.size() || i2 >= (int)old.vertices.size()) return 1e30f;
                const Vec3 a = old.vertices[(size_t)i0], b = old.vertices[(size_t)i1], c = old.vertices[(size_t)i2];
                const Vec3 e0 = b - a, e1 = c - a, d = p - a;
                const float d00 = Vec3::Dot(e0, e0), d01 = Vec3::Dot(e0, e1), d11 = Vec3::Dot(e1, e1), d20 = Vec3::Dot(d, e0), d21 = Vec3::Dot(d, e1);
                const float den = d00 * d11 - d01 * d01;
                float v = 0, w = 0;
                if (std::fabs(den) > 1e-20f) { v = (d11 * d20 - d01 * d21) / den; w = (d00 * d21 - d01 * d20) / den; }
                float u = std::max(1.0f - v - w, 0.0f); v = std::max(v, 0.0f); w = std::max(w, 0.0f);
                const float sum = u + v + w; if (sum > 1e-12f) { u /= sum; v /= sum; w /= sum; } else { u = 1; v = w = 0; }
                return (a * u + b * v + c * w - p).SqrMagnitude();
            };
            auto parent = [&](const Vec3& p) {
                int best = -1; float bd = 1e30f;
                for (int ot : g.NearestK(p, 12)) { const float d = distToOld(ot, p); if (d < bd) { bd = d; best = ot; } }
                return best;
            };
            std::vector<int> ns; std::vector<Color> nf; std::vector<Vec2> ncu;
            for (int t = 0; t < nt; ++t) {
                int src = (t < (int)oc.size() && oc[(size_t)t] == nc[(size_t)t]) ? t : parent(nc[(size_t)t]);
                if (src < 0) src = 0;
                if (needCU) {
                    // Per-corner UVs: a corner that sits where an old corner was keeps its UV; any other point takes the UV
                    // found by walking the old triangle it grew from (barycentric, clamped to the triangle), so UV islands survive.
                    const int oi[3] = {old.triangles[(size_t)src * 3], old.triangles[(size_t)src * 3 + 1], old.triangles[(size_t)src * 3 + 2]};
                    const bool okOld = oi[0] >= 0 && oi[1] >= 0 && oi[2] >= 0 && oi[0] < (int)old.vertices.size() && oi[1] < (int)old.vertices.size() && oi[2] < (int)old.vertices.size();
                    for (int k = 0; k < 3; ++k) {
                        const int ni = triangles[(size_t)t * 3 + k];
                        const Vec3 p = (ni >= 0 && ni < nv) ? vertices[(size_t)ni] : Vec3{0, 0, 0};
                        Vec2 uv = old.cornerUVs[(size_t)src * 3];
                        if (okOld) {
                            const Vec3 a = old.vertices[(size_t)oi[0]], b = old.vertices[(size_t)oi[1]], c = old.vertices[(size_t)oi[2]];
                            const Vec3 e0 = b - a, e1 = c - a, d = p - a;
                            const float d00 = Vec3::Dot(e0, e0), d01 = Vec3::Dot(e0, e1), d11 = Vec3::Dot(e1, e1), d20 = Vec3::Dot(d, e0), d21 = Vec3::Dot(d, e1);
                            const float den = d00 * d11 - d01 * d01;
                            float v = 0, w = 0;
                            if (std::fabs(den) > 1e-20f) { v = (d11 * d20 - d01 * d21) / den; w = (d00 * d21 - d01 * d20) / den; }
                            float u = 1.0f - v - w;
                            u = std::max(u, 0.0f); v = std::max(v, 0.0f); w = std::max(w, 0.0f);
                            const float sum = u + v + w; if (sum > 1e-12f) { u /= sum; v /= sum; w /= sum; } else { u = 1; v = w = 0; }
                            const Vec2 &ua = old.cornerUVs[(size_t)src * 3], &ub = old.cornerUVs[(size_t)src * 3 + 1], &uc = old.cornerUVs[(size_t)src * 3 + 2];
                            uv = Vec2{ua.x * u + ub.x * v + uc.x * w, ua.y * u + ub.y * v + uc.y * w};
                        }
                        ncu.push_back(uv);
                    }
                }
                if (needSlot) ns.push_back(old.triSlot[(size_t)src]);
                if (needFC) nf.push_back(old.triColors[(size_t)src]);
            }
            if (needSlot) { triSlot = std::move(ns); if (slotColors.empty()) slotColors = old.slotColors; }
            if (needFC) triColors = std::move(nf);
            if (needCU) cornerUVs = std::move(ncu);
        }
    }
    /// Put at the top of an operator: when the operator returns, whatever it left at the wrong size is rebuilt from
    /// the mesh as it was (see InheritAttributes). Costs one copy of the mesh, and only when it carries such lists.
    struct AttrGuard {
        Mesh& m; std::unique_ptr<Mesh> before;
        explicit AttrGuard(Mesh& mesh) : m(mesh) { if (m.HasUVs() || m.HasVertexColors() || m.HasSlots() || m.HasFaceColors() || m.HasCornerUVs() || !m.seams.empty()) before.reset(new Mesh(m)); }
        ~AttrGuard() { if (before) m.InheritAttributes(*before); }
    };

    /// Copy the per-vertex UVs onto the triangle corners (before a weld would
    /// merge vertices that sit on a UV seam). Keeps existing corner UVs.
    void CaptureCornerUVs() {
        if (HasCornerUVs() || !HasUVs()) return;
        cornerUVs.resize(triangles.size());
        for (std::size_t i = 0; i < triangles.size(); ++i) {
            const int v = triangles[i];
            cornerUVs[i] = (v >= 0 && v < (int)uvs.size()) ? uvs[v] : Vec2{0, 0};
        }
    }

    /// The same surface with one vertex per (vertex, corner UV), so per-vertex
    /// uvs are exact again: what renderers and exporters need. Vertex i stays
    /// vertex i (with the UV of its first corner) and copies split off at seams
    /// come after, so seams and selections keep their numbers and a later
    /// weld folds the copies back. Without corner UVs it's a plain copy.
    Mesh SplitCornerUVs() const {
        Mesh m = *this;
        if (!HasCornerUVs()) return m;
        m.cornerUVs.clear();
        const int nv = (int)vertices.size();
        const bool nrm = HasNormals();
        std::vector<char> seen(nv, 0);
        m.uvs.assign(nv, Vec2{0, 0});
        for (std::size_t i = 0; i < triangles.size(); ++i) {
            const int v = triangles[i];
            if (v >= 0 && v < nv && !seen[v]) { seen[v] = 1; m.uvs[v] = cornerUVs[i]; }
        }
        std::map<std::tuple<int, long, long>, int> copy;
        for (std::size_t i = 0; i < triangles.size(); ++i) {
            const int v = triangles[i];
            if (v < 0 || v >= nv) continue;
            const Vec2 t = cornerUVs[i];
            if (std::fabs(t.x - m.uvs[v].x) <= 1e-6f && std::fabs(t.y - m.uvs[v].y) <= 1e-6f) continue;
            const auto key = std::make_tuple(v, std::lround(t.x * 1e6f), std::lround(t.y * 1e6f));
            auto it = copy.find(key);
            if (it == copy.end()) {
                it = copy.emplace(key, (int)m.vertices.size()).first;
                m.vertices.push_back(vertices[v]);
                m.uvs.push_back(t);
                if (nrm) m.normals.push_back(normals[v]);
                if (HasVertexColors()) m.vertexColors.push_back(vertexColors[v]);
            }
            m.triangles[i] = it->second;
        }
        return m;
    }

    /// Compute per-vertex smooth normals for Gouraud shading. Vertices sharing a
    /// position (even across separately-built parts) are grouped so the normal is
    /// averaged across the seam — this both rounds curved surfaces and hides the
    /// joins between assembled body parts. Area-weighted (uses un-normalized face
    /// normals) for a natural result.
    void ComputeSmoothNormals() {
        autoSmoothAngle = 0.0f;               // fully smooth supersedes auto-smooth
        normals.assign(vertices.size(), Vec3{0, 0, 0});
        std::map<std::tuple<int, int, int>, int> rep;
        std::vector<int> grp(vertices.size());
        auto key = [](const Vec3& p) {
            return std::make_tuple((int)std::lround(p.x * 2048.0f),
                                   (int)std::lround(p.y * 2048.0f),
                                   (int)std::lround(p.z * 2048.0f));
        };
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            auto k = key(vertices[i]);
            auto it = rep.find(k);
            grp[i] = (it == rep.end()) ? (rep[k] = (int)i) : it->second;
        }
        for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
            int a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            Vec3 fn = Vec3::Cross(vertices[b] - vertices[a], vertices[c] - vertices[a]);
            normals[grp[a]] += fn; normals[grp[b]] += fn; normals[grp[c]] += fn;
        }
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            Vec3 n = normals[grp[i]];
            float m = n.Magnitude();
            normals[i] = m > 1e-8f ? n * (1.0f / m) : Vec3{0, 1, 0};
        }
    }

    /// Angle-based shading (Blender's Auto Smooth): smooth normals across edges
    /// where adjacent faces meet at less than `angleDeg`, hard splits where they
    /// meet sharper — a cylinder gets a smooth barrel with crisp cap rims in one
    /// click, no manual smooth/flat juggling. Rebuilds the vertex list (corners in
    /// different smoothing groups get their own vertex copies); keeps UVs (split
    /// per seam) and per-face colors. Remembered in `autoSmoothAngle` so the scene
    /// serializer can restore the same shading on load.
    void ComputeAutoSmoothNormals(float angleDeg = 30.0f) {
        const int nt = (int)triangles.size() / 3;
        if (nt == 0 || vertices.empty()) return;
        const float cosT = std::cos(angleDeg * 0.01745329252f);
        const bool hasUV = uvs.size() == vertices.size();
        // Area-weighted face normals (+ unit copies for the angle tests). Zero-area
        // triangles carry no orientation: they join whatever group is available
        // without contributing to (or being tested against) its normal.
        std::vector<Vec3> fn(nt), fu(nt);
        std::vector<char> fdeg(nt, 0);
        for (int t = 0; t < nt; ++t) {
            const Vec3& a = vertices[triangles[t * 3]];
            fn[t] = Vec3::Cross(vertices[triangles[t * 3 + 1]] - a,
                                vertices[triangles[t * 3 + 2]] - a);
            float m = fn[t].Magnitude();
            if (m > 1e-12f) fu[t] = fn[t] * (1.0f / m);
            else { fu[t] = Vec3{0, 1, 0}; fdeg[t] = 1; }
        }
        // Corners around each coincident-position vertex.
        std::vector<int> rep = CoincidentReps();
        // Indexed by representative vertex (visited in ascending order, as an ordered map would): no allocation per corner group.
        std::vector<std::vector<std::pair<int, int>>> corners(vertices.size());   // rep -> (tri, slot)
        for (int t = 0; t < nt; ++t)
            for (int k = 0; k < 3; ++k)
                corners[rep[triangles[t * 3 + k]]].push_back({t, k});
        Mesh out;
        out.triangles.assign(triangles.size(), 0);
        out.vertices.reserve(vertices.size() + vertices.size() / 4);
        out.normals.reserve(vertices.size() + vertices.size() / 4);
        for (auto& cvv : corners) {
            if (cvv.empty()) continue;
            struct { std::vector<std::pair<int, int>>& second; } cv{cvv};
            // Greedy smoothing groups at this vertex: a corner joins the first group
            // whose average normal is within the angle of its face's normal.
            std::vector<Vec3> gSum;
            std::vector<std::vector<std::pair<int, int>>> gMembers;
            for (auto& tk : cv.second) {
                int t = tk.first, gi = -1;
                float sign = 1.0f;
                if (fdeg[t]) {
                    // No orientation of its own: ride along with the first group.
                    if (gSum.empty()) { gSum.push_back({0, 0, 0}); gMembers.emplace_back(); }
                    gMembers[0].push_back(tk);
                    continue;
                }
                for (int g = 0; g < (int)gSum.size(); ++g) {
                    Vec3 gn = gSum[g]; float m = gn.Magnitude();
                    if (m < 1e-12f) continue;
                    // Sign-tolerant: several built-in generators (and hand edits)
                    // carry mixed winding, so a flipped-but-parallel neighbor still
                    // belongs to the group — accumulate it sign-aligned instead of
                    // letting it cancel the average to zero.
                    float d = Vec3::Dot(gn * (1.0f / m), fu[t]);
                    if (d >= cosT)       { gi = g; sign =  1.0f; break; }
                    else if (-d >= cosT) { gi = g; sign = -1.0f; break; }
                }
                if (gi < 0) { gSum.push_back({0, 0, 0}); gMembers.emplace_back(); gi = (int)gSum.size() - 1; }
                gSum[gi] += fn[t] * sign;
                gMembers[gi].push_back(tk);
            }
            for (int g = 0; g < (int)gSum.size(); ++g) {
                Vec3 n = gSum[g]; float m = n.Magnitude();
                n = m > 1e-12f ? n * (1.0f / m) : Vec3{0, 1, 0};
                // One output vertex per (group, uv) so texture seams keep their UVs.
                // (Corners are matched by exact position and exact UV: vertices a few thousandths apart share a smoothing group,
                // but each keeps its own position, so thin features such as wiper blades are not squeezed flat.)
                struct Made { float ux, uy; Vec3 p; int vi; };
                std::vector<Made> made;
                for (auto& tk : gMembers[g]) {
                    int src = triangles[tk.first * 3 + tk.second];
                    float ux = 0, uy = 0;
                    if (hasUV) { ux = uvs[src].x; uy = uvs[src].y; }   // exact: a corner keeps the UV it had
                    const Vec3& sp = vertices[src];
                    int vi = -1;
                    for (const Made& m : made)
                        if (m.ux == ux && m.uy == uy && m.p.x == sp.x && m.p.y == sp.y && m.p.z == sp.z) { vi = m.vi; break; }
                    if (vi < 0) {
                        vi = (int)out.vertices.size();
                        out.vertices.push_back(sp);
                        out.normals.push_back(n);
                        if (hasUV) out.uvs.push_back(uvs[src]);
                        made.push_back({ux, uy, sp, vi});
                    }
                    out.triangles[tk.first * 3 + tk.second] = vi;
                }
            }
        }
        vertices = std::move(out.vertices);
        normals  = std::move(out.normals);
        triangles = std::move(out.triangles);
        if (hasUV) uvs = std::move(out.uvs); else uvs.clear();
        autoSmoothAngle = angleDeg;   // triColors are per-face and index-stable: keep
        name = "";
    }

    /// Keep normals valid after an edit WITHOUT changing the mesh's shading mode:
    /// if it was smooth-shaded (had per-vertex normals) recompute them at the new
    /// resolution; if it had none, leave it flat (the renderer face-shades). This
    /// fixes lighting after edits that add/move vertices (Subdivide, deformers) —
    /// otherwise a stale, wrong-sized normals array corrupts the shading.
    void RefreshNormals() {
        if (normals.empty()) return;                 // flat-shaded: stay flat (face normals)
        ComputeSmoothNormals();                       // was smooth: rebuild at the new resolution
    }

    // ---- Primitive generators -----------------------------------------
    static Mesh Quad(float size = 1.0f);

    static Mesh Cube(float size = 1.0f);

    /// A wedge / ramp: a right-triangular prism (a cube sliced corner-to-corner)
    /// rising along +Z toward +Y. Great for ramps, rooftops, and level blockouts.
    static Mesh Wedge(float size = 1.0f);

    static Mesh Pyramid(float size = 1.0f);

    /// A subdivided horizontal grid in the XZ plane: `cols`×`rows` cells over
    /// `width`×`depth`, centered at the origin. The dense vertex grid is the
    /// base for terrain/water (displace the Y of each vertex afterwards).
    static Mesh Grid(float width = 10.0f, float depth = 10.0f, int cols = 10, int rows = 10);

    /// A flat horizontal ground plane in the XZ plane (good for 3D floors).
    static Mesh Plane(float size = 10.0f);

    /// A UV sphere of the given radius.
    static Mesh Sphere(float radius = 0.5f, int rings = 8, int sectors = 12);

    /// A rounded box (superellipsoid): flat faces with softly rounded edges and
    /// corners — the go-to shape for UI-ish props, buttons, dice, soft crates.
    /// `roundness` 0.05 = nearly a sharp cube .. 1.0 = a full sphere.
    static Mesh RoundedBox(float size = 1.0f, float roundness = 0.3f, int rings = 14, int sectors = 20);

    /// A capped cylinder along Y.
    static Mesh Cylinder(float radius = 0.5f, float height = 1.0f, int sectors = 12);

    /// A cone with a circular base on the XZ plane and apex on +Y.
    static Mesh Cone(float radius = 0.5f, float height = 1.0f, int sectors = 16);

    /// A torus (donut) around the Y axis. `radius` is the ring radius, `tube`
    /// the tube thickness.
    static Mesh Torus(float radius = 0.5f, float tube = 0.2f, int rings = 16, int sides = 10);

    /// A (p,q) torus knot: a circular tube swept along the knot curve
    ///   r = cos(q·t) + 2,  (x,z) = r·(cos,sin)(p·t),  y = -sin(q·t)
    /// The cross-section frame is built by parallel transport of an initial normal
    /// (no per-sample curvature needed), which keeps the tube from flipping. A
    /// Blender staple; nice for stress-testing shading and wireframes.
    static Mesh TorusKnot(int p = 2, int q = 3, float scale = 0.32f, float tube = 0.10f,
                          int rings = 160, int sides = 12);

    /// A hollow tube / pipe / ring along Y: an outer and inner wall joined by top
    /// and bottom annulus caps. `inner` < `outer`. Good for rings, portals, pipes.
    static Mesh Tube(float outer = 0.5f, float inner = 0.3f, float height = 1.0f, int sectors = 24);

    /// Append an axis-aligned box spanning [mn, mx] (8 verts, 12 tris, outward
    /// winding) to an existing mesh — a building block for compound primitives.
    static void AppendBox(Mesh& m, const Vec3& mn, const Vec3& mx);

    /// A dome: the top half of a sphere with a flat base cap on the XZ plane.
    static Mesh Hemisphere(float radius = 0.5f, int rings = 6, int sectors = 16);

    /// A staircase of `steps` boxes climbing +Y and marching +Z (great for level
    /// blockout). Sits with its base at y=0, front at z=0.
    static Mesh Stairs(int steps = 5, float width = 1.0f, float totalHeight = 1.0f, float totalDepth = 1.0f);

    /// A cog/gear: a short cylinder with `teeth` square teeth around the rim,
    /// extruded `thickness` along Y. Spin it with a script for machinery.
    static Mesh Gear(int teeth = 10, float radius = 0.5f, float toothDepth = 0.12f, float thickness = 0.25f);

    /// An n-sided prism (flat-capped): a regular polygon extruded along Y. `sides`
    /// picks the shape — 3 = triangular prism, 6 = hexagonal column, etc. Distinct
    /// from Cylinder (which is high-poly/round) — this stays crisply faceted.
    static Mesh Prism(int sides = 6, float radius = 0.5f, float height = 1.0f);

    /// A flat filled circle on the XZ plane (Blender's Circle, filled) — a round
    /// disc facing +Y. Handy for tabletops, coins, platforms, decals.
    static Mesh Disc(float radius = 0.5f, int sectors = 24);

    /// An octahedron — a faceted gem/diamond (6 vertices, 8 triangular faces).
    static Mesh Octahedron(float radius = 0.5f);

    /// A regular tetrahedron — the simplest solid (4 vertices, 4 triangular faces).
    static Mesh Tetrahedron(float radius = 0.5f);

    /// A bipyramid — a faceted crystal/gem: an n-gon equator with an apex above and
    /// below. `sides` picks the cut (6 = a classic hex crystal).
    static Mesh Bipyramid(int sides = 6, float radius = 0.5f, float height = 1.0f);

    /// A geodesic sphere: an icosahedron subdivided `subdivisions` times and
    /// projected to the radius. Triangles are near-uniform (no pinching at the
    /// poles like the UV Sphere), so it shades and tessellates evenly.
    static Mesh Icosphere(float radius = 0.5f, int subdivisions = 2);

    /// A broadleaf tree: a tapered trunk under a rounded leafy canopy. Sits on
    /// the ground (base at y=0), ~2 units tall. Per-face colored + smooth-shaded
    /// so it drops onto terrain and looks right immediately.
    // Deterministic per-(variant,key) value in [lo,hi] for procedural prop
    // variation. variant 0 always returns the midpoint, so the "base" model is
    // unchanged and backward-compatible; any other variant perturbs it.
    static float PropRand(int variant, int key, float lo, float hi);

    static Mesh Tree(int variant = 0);
    /// A conifer / pine: tapered trunk under overlapping shaded skirts. ~2.4 tall.
    static Mesh Pine(int variant = 0);
    /// A boulder: faceted low-poly lump, mottled stone, flat-shaded. ~0.8 tall.
    static Mesh Rock(int variant = 0);
    /// A small shrub: a two-tone cluster of jittered leafy blobs, ~0.7 tall.
    static Mesh Bush(int variant = 0);

    // ---- Prop library (compound models built from the primitives + modeling ops).
    // Every prop bakes per-face colors, rests on y=0, and keeps its name so it
    // serializes compactly and regenerates on load.

    /// A leaning palm: curved trunk, drooping fronds, coconuts. ~2.6 tall.
    static Mesh PalmTree(int variant = 0);
    /// A bare, weathered dead tree: gnarled trunk + reaching branches. ~2 tall.
    static Mesh DeadTree(int variant = 0);
    /// A toadstool: pale stem, red spotted cap. ~0.55 tall.
    static Mesh Mushroom(int variant = 0);
    /// A saguaro cactus: ribbed body + two elbow arms. ~1.6 tall.
    static Mesh Cactus(int variant = 0);
    /// A wooden barrel with iron bands. ~0.9 tall.
    static Mesh Barrel();
    /// A slatted shipping crate with corner braces. 0.8 cube.
    static Mesh Crate();
    /// A two-rail wooden fence segment, ~2 wide, ~1 tall. Array X to extend.
    static Mesh Fence();
    /// A stone well: mottled ring, posts, and a little pitched roof. ~1.5 tall.
    static Mesh Well();
    /// A street lamp: curved post with a warm glowing head. ~2.6 tall.
    static Mesh StreetLamp();
    /// A crystal cluster: tilted shards in two tones, flat-shaded. ~0.9 tall.
    static Mesh Crystal(int variant = 0);
    /// A cottage: walls, hipped roof, door, windows, chimney. ~2.2 tall, 3 wide.
    static Mesh House();
    /// A stone watchtower with a conical roof. ~3.4 tall.
    static Mesh Tower();
    /// A windmill: tapered stone body, conical cap, four angled sail blades. ~3.4 tall.
    static Mesh Windmill();
    /// An arched plank footbridge with side rails. ~3 long, ~1 tall.
    static Mesh Bridge();
    /// A natural stone arch: two tapered legs joined by a swept span. ~2.4 tall.
    static Mesh RockArch();
    /// A campfire: ring of stones, crossed logs, an emissive flame. ~0.6 tall.
    static Mesh Campfire();
    /// A hanging lantern: metal cage, warm glass core, top ring. ~0.6 tall.
    static Mesh Lantern();
    /// A treasure chest: wooden box, curved lid, iron bands + lock. ~0.7 tall.
    static Mesh Chest();
    /// A signpost: post with an angled board. ~1.5 tall.
    static Mesh Signpost();
    /// A wooden hand-cart: bed, side rails, two wheels. ~1 long.
    static Mesh Cart();
    /// A camping tent: ridged canvas with a dark door flap. ~1 tall.
    static Mesh Tent();
    /// A park bench: slatted seat + back on two legs. ~0.8 tall, ~1.6 wide.
    static Mesh Bench();

    /// True if this named prop supports procedural variants (FromNameSeeded).
    static bool NameHasVariants(const std::string& n);

    /// Like FromName, but for the nature props that support it, `variant` picks a
    /// procedurally-varied version (0 = the canonical base model). Used so a
    /// scattered forest isn't hundreds of identical clones; the variant is stored
    /// per object and regenerates the exact same shape on load.
    static Mesh FromNameSeeded(const std::string& n, int variant);

    /// Recreate a primitive mesh from its name.
    static Mesh FromName(const std::string& n);

    /// Axis-aligned bounding box of the vertices. `outMin`/`outMax` receive the
    /// corners; safe on an empty mesh (both set to origin). Useful for placing,
    /// centering, framing the camera, or simple culling.
    void Bounds(Vec3& outMin, Vec3& outMax) const {
        if (vertices.empty()) { outMin = outMax = Vec3{0, 0, 0}; return; }
        outMin = outMax = vertices[0];
        for (const Vec3& v : vertices) {   // plain compares: std::fmin/fmax are library calls
            if (v.x < outMin.x) outMin.x = v.x; if (v.x > outMax.x) outMax.x = v.x;
            if (v.y < outMin.y) outMin.y = v.y; if (v.y > outMax.y) outMax.y = v.y;
            if (v.z < outMin.z) outMin.z = v.z; if (v.z > outMax.z) outMax.z = v.z;
        }
    }
    /// Edges worth drawing in a wireframe: every real edge EXCEPT the internal
    /// triangulation diagonals of flat quads. Two coplanar triangles that form a
    /// convex quad are treated as one face and their shared (diagonal) edge is
    /// hidden — so a cube reads as 6 quads and a subdivided face as a clean grid,
    /// instead of a mess of triangles. Boundary edges and creases (angled faces,
    /// e.g. a sphere) are always kept. Returned as pairs of vertex indices.
    /// `angleDeg` is the crease threshold; larger keeps fewer diagonals.
    std::vector<std::pair<int, int>> VisibleEdges(float angleDeg = 12.0f) const {
        std::vector<std::pair<int, int>> out;
        const int nTris = TriangleCount();
        if (nTris == 0) return out;
        // Weld positions to canonical ids so shared edges match across duplicates.
        std::map<std::tuple<int, int, int>, int> rep;
        std::vector<int> cid(vertices.size());
        std::vector<int> repVert;                            // canonical id -> a vertex index
        auto key = [](const Vec3& v) {
            return std::make_tuple((int)std::lround(v.x * 1024.0f),
                                   (int)std::lround(v.y * 1024.0f),
                                   (int)std::lround(v.z * 1024.0f));
        };
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            auto it = rep.find(key(vertices[i]));
            if (it == rep.end()) { int id = (int)repVert.size(); rep[key(vertices[i])] = id; repVert.push_back((int)i); cid[i] = id; }
            else cid[i] = it->second;
        }
        // Per-triangle normal + the canonical ids of its corners.
        struct Tri { int c[3]; Vec3 n; };
        std::vector<Tri> tri(nTris);
        for (int f = 0; f < nTris; ++f) {
            int a = triangles[f * 3], b = triangles[f * 3 + 1], c = triangles[f * 3 + 2];
            tri[f].c[0] = cid[a]; tri[f].c[1] = cid[b]; tri[f].c[2] = cid[c];
            Vec3 fn = Vec3::Cross(vertices[b] - vertices[a], vertices[c] - vertices[a]);
            float m = fn.Magnitude(); tri[f].n = m > 1e-8f ? fn * (1.0f / m) : Vec3{0, 0, 0};
        }
        // Edge -> the (up to two) triangles sharing it.
        std::map<std::pair<int, int>, std::vector<int>> edgeTris;
        auto ek = [](int a, int b) { return std::make_pair(std::min(a, b), std::max(a, b)); };
        for (int f = 0; f < nTris; ++f)
            for (int e = 0; e < 3; ++e)
                edgeTris[ek(tri[f].c[e], tri[f].c[(e + 1) % 3])].push_back(f);
        // Candidate diagonals: internal, coplanar, convex-quad edges — sorted by
        // descending length so a quad's true diagonal (longer) is hidden before its
        // grid-edge legs (shorter), and each triangle is consumed at most once.
        const float cosT = std::cos(angleDeg * 3.14159265358979323846f / 180.0f);
        auto pos = [&](int c) { return vertices[repVert[c]]; };
        struct Cand { std::pair<int, int> e; int t0, t1; float len; };
        std::vector<Cand> cands;
        for (auto& kv : edgeTris) {
            if (kv.second.size() != 2) continue;
            int t0 = kv.second[0], t1 = kv.second[1];
            float d = tri[t0].n.x * tri[t1].n.x + tri[t0].n.y * tri[t1].n.y + tri[t0].n.z * tri[t1].n.z;
            if (d < cosT) continue;                          // a crease — keep this edge
            float len = (pos(kv.first.first) - pos(kv.first.second)).Magnitude();
            cands.push_back({kv.first, t0, t1, len});
        }
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.len > b.len; });
        std::vector<char> used(nTris, 0);
        std::set<std::pair<int, int>> hidden;
        for (const Cand& c : cands)
            if (!used[c.t0] && !used[c.t1]) { used[c.t0] = used[c.t1] = 1; hidden.insert(c.e); }
        for (auto& kv : edgeTris)
            if (hidden.find(kv.first) == hidden.end())
                out.push_back({repVert[kv.first.first], repVert[kv.first.second]});
        return out;
    }

    /// Center of the bounding box.
    Vec3 Center() const {
        Vec3 lo, hi; Bounds(lo, hi);
        return {(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
    }
    /// Size (extent) of the bounding box.
    Vec3 Size() const {
        Vec3 lo, hi; Bounds(lo, hi);
        return {hi.x - lo.x, hi.y - lo.y, hi.z - lo.z};
    }

    /// Translate so the bounding-box center sits at the origin — fixes imported
    /// OBJs modeled off to one side so they rotate/scale about their middle.
    /// Like every other edit, a change marks the mesh custom (clears `name`) so the
    /// moved geometry is what a scene saves, not a primitive rebuilt from its name.
    void RecenterToOrigin() {
        if (vertices.empty()) return;
        Vec3 c = Center();
        if (!std::isfinite(c.x + c.y + c.z) || (c.x == 0.0f && c.y == 0.0f && c.z == 0.0f)) return;
        for (Vec3& v : vertices) v -= c;
        name = "";
    }

    /// Move the pivot to the base: centered in X/Z with the lowest point at
    /// Y = 0, so the model rests on the ground when placed at y = 0.
    void GroundPivot() {
        if (vertices.empty()) return;
        Vec3 lo, hi; Bounds(lo, hi);
        Vec3 c{(lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f};
        if (!std::isfinite(c.x + c.y + c.z) || (c.x == 0.0f && c.y == 0.0f && c.z == 0.0f)) return;
        for (Vec3& v : vertices) v -= c;
        name = "";   // saved as real geometry (a primitive's name would rebuild the old pivot on load)
    }

    /// Uniformly scale (about the origin) so the largest bounding-box dimension
    /// equals `maxExtent` — normalize wildly-sized imported models to a usable
    /// scale. No-op on a flat/empty mesh.
    void ScaleToFit(float maxExtent = 1.0f) {
        if (!std::isfinite(maxExtent) || vertices.empty()) return;   // a typed NaN/inf would turn every vertex into NaN
        Vec3 s = Size();
        float biggest = std::fmax(s.x, std::fmax(s.y, s.z));
        if (!(biggest > 1e-6f) || !std::isfinite(biggest)) return;
        float k = maxExtent / biggest;
        if (!std::isfinite(k) || k == 1.0f) return;
        for (Vec3& v : vertices) v = v * k;
        name = "";   // saved as real geometry (a primitive's name would rebuild the old size on load)
    }

    /// Normalize an imported mesh's size in place. Exporters disagree on units —
    /// Meshy AI / FBX / CAD tools often emit centimeters or millimeters, making a
    /// character 180 "meters" tall. If the largest dimension is implausibly big
    /// (> 20 units) or microscopic (< 0.02), scale the VERTICES so it lands at
    /// ~2 units. Deterministic per file, so re-loading a scene can't compound it.
    static void NormalizeImportScale(Mesh& m) {
        if (m.vertices.empty()) return;
        Vec3 lo, hi; m.Bounds(lo, hi);
        Vec3 sz = hi - lo;
        float d = sz.x > sz.y ? (sz.x > sz.z ? sz.x : sz.z) : (sz.y > sz.z ? sz.y : sz.z);
        if (d <= 0.0f || (d <= 20.0f && d >= 0.02f)) return;
        float s = 2.0f / d;
        for (Vec3& v : m.vertices) v = v * s;
    }

    // ---- Modeling: import/export and mesh operations -------------------

    /// Load a Wavefront .OBJ (v positions + f faces; polygons are fan-triangulated,
    /// v/vt/vn and negative indices handled). Returns an empty mesh on failure;
    /// `ok` (if given) reports success.
    /// Load a Wavefront .OBJ. Parses positions + texture coords (UVs), splitting
    /// vertices per unique position/UV pair so textures map correctly. If
    /// `outTexture` is given, the referenced .mtl is read and its diffuse map
    /// (map_Kd) is returned (resolved next to the .obj) so an imported, textured
    /// model (e.g. a MakeHuman/Mixamo export) renders with its skin/clothing.
    static Mesh LoadOBJ(const std::string& path, bool* ok = nullptr, std::string* outTexture = nullptr);

    /// Write this mesh to a Wavefront .OBJ (and, when it has face or slot colours, a .mtl next to it with the same
    /// stem). Written from the OBJ/MTL format description; LoadOBJ reads everything back:
    ///  - `v x y z` with 9 significant digits (a float survives the round trip), plus `r g b` (0..1) when the mesh has
    ///    vertex colours (the common vertex-colour extension);
    ///  - `vt` per vertex, or per triangle corner when the mesh keeps corner UVs (edit mode);
    ///  - `vn` and `s 1` for smooth shading, `s off` for flat;
    ///  - polygons (quads, n-gons) as one `f` line each when `polyFace` groups triangles into a simple outline;
    ///  - face colours as one material per distinct colour (`usemtl`, with `Kd` and `d` in the .mtl).
    /// Returns false on an I/O error.
    bool SaveOBJ(const std::string& path) const;

private:
    /// The outline of a polygon made of triangles `group`: the edges no other triangle of the group runs the other
    /// way, chained into one loop. False (write the triangles instead) when the triangles differ in material or
    /// corner UV at a shared vertex, or the outline is not one simple loop through every corner vertex.
    bool PolygonOutline(const std::vector<int>& group, const std::vector<int>& matOf,
                        std::vector<std::pair<int, int>>& loop) const {
        const bool cuv = HasCornerUVs();
        std::map<int, int> cornerOf;                                     // vertex -> a triangle corner that uses it
        std::map<std::pair<int, int>, int> edges;                        // directed edge -> uses
        for (int t : group) {
            if (!matOf.empty() && matOf[t] != matOf[group[0]]) return false;
            for (int k = 0; k < 3; ++k) {
                const int v = triangles[t * 3 + k];
                auto it = cornerOf.find(v);
                if (it == cornerOf.end()) cornerOf[v] = t * 3 + k;
                else if (cuv && !(cornerUVs[it->second].x == cornerUVs[t * 3 + k].x && cornerUVs[it->second].y == cornerUVs[t * 3 + k].y)) return false;
                ++edges[{v, triangles[t * 3 + (k + 1) % 3]}];
            }
        }
        std::map<int, int> next;
        for (const auto& e : edges) {
            if (e.second != 1) return false;
            if (edges.count({e.first.second, e.first.first})) continue;   // inside the polygon
            if (next.count(e.first.first)) return false;                   // two ways out: not a simple loop
            next[e.first.first] = e.first.second;
        }
        if (next.size() < 3 || next.size() != cornerOf.size()) return false;
        const int start = triangles[group[0] * 3];
        if (!next.count(start)) return false;
        int v = start;
        do {
            loop.push_back({v, cornerOf[v]});
            auto it = next.find(v);
            if (it == next.end() || loop.size() > next.size()) { loop.clear(); return false; }
            v = it->second;
        } while (v != start);
        if (loop.size() != next.size()) { loop.clear(); return false; }
        return true;
    }

public:

    // ---- UV projection (quick unwraps for texturing edited meshes) -----------
    // All of these fill the per-vertex `uvs` array and mark the mesh custom
    // (clear `name`) so the projected UVs are what gets saved, not a primitive's
    // regenerated defaults. `tile` = how many texture repeats across the bounds.

    /// Planar projection along an axis (0 = X, 1 = Y, 2 = Z): floors, terrain, tabletops.
    /// Like every projection here it fills one UV per triangle corner
    /// (cornerUVs) and per-vertex uvs from each vertex's first corner;
    /// SplitCornerUVs() makes the per-vertex ones exact where corners differ.
    /// UVs follow the engine's convention: v = 0 is the top of the image, and a
    /// face reads the right way round seen from outside (from + along the axis).
    void ProjectUVPlanar(int axis, float tile = 1.0f);

    /// Box (tri-planar) projection: each face projects along its dominant
    /// normal axis, turned so the texture reads the right way round from
    /// outside on all six sides. The go-to for buildings and hand-edited geometry.
    void ProjectUVBox(float tile = 1.0f);

    /// Cylindrical projection around Y: u wraps around the side, v runs bottom
    /// to top. For columns, cans, tree trunks, lathe/screw results. Faces that
    /// straddle the wrap-around keep their corners on one side (no smeared band).
    void ProjectUVCylinder(float tile = 1.0f);

    /// Spherical projection from the bounds centre — planets, rocks, heads.
    void ProjectUVSphere(float tile = 1.0f);

    /// Set one UV per triangle corner; per-vertex uvs take each vertex's first corner.
    void SetCornerUVs(const std::vector<Vec2>& corner);

private:
    // A triangle whose u spans more than half a turn crosses the wrap: lift its low corners by 1.
    void UnwrapAroundU(std::vector<Vec2>& corner) const;

public:
    /// A copy with each vertex scaled (per-axis) then offset — the basic modeling
    /// transform for placing/sizing a part before combining.
    Mesh Transformed(const Vec3& scale, const Vec3& offset) const {
        Mesh m = *this; m.name = "";
        for (Vec3& v : m.vertices)
            v = {v.x * scale.x + offset.x, v.y * scale.y + offset.y, v.z * scale.z + offset.z};
        return m;
    }

    /// Bake an Euler rotation (degrees) into the vertices — orient a part (a cone
    /// roof, an arm) before Combine. Returns a rotated copy; RotateVerts does it
    /// in place.
    void RotateVerts(const Vec3& eulerDegrees) {
        Quat q = Quat::Euler(eulerDegrees);
        for (Vec3& v : vertices) v = q * v;
        name = "";
        RefreshNormals();
    }
    Mesh Rotated(const Vec3& eulerDegrees) const { Mesh m = *this; m.RotateVerts(eulerDegrees); return m; }

    /// Reverse triangle winding so faces point the other way — fix an
    /// inside-out imported mesh, or make an inward-facing skybox shell.
    void FlipWinding() {
        const bool cuv = HasCornerUVs();
        for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
            std::swap(triangles[i + 1], triangles[i + 2]);
            if (cuv) std::swap(cornerUVs[i + 1], cornerUVs[i + 2]);
        }
    }

    /// Append another mesh into this one (re-indexing its triangles) — build a
    /// compound model (e.g. a snowman) from primitive parts.
    void Combine(const Mesh& other) {
        const int base = (int)vertices.size();
        const int tBase = TriangleCount();
        const bool uvBoth = HasUVs() || other.HasUVs();
        const bool fcBoth = HasFaceColors() || other.HasFaceColors();
        const bool slBoth = HasSlots() || other.HasSlots();
        const bool pfThis = (int)polyFace.size() == tBase && !polyFace.empty();
        const bool pfOther = (int)other.polyFace.size() == other.TriangleCount() && !other.polyFace.empty();
        // What each side carries per triangle is filled in for the other so the lists stay in step.
        std::vector<Color> col;
        if (fcBoth) {
            col.reserve((std::size_t)tBase + other.TriangleCount());
            for (int t = 0; t < tBase; ++t) col.push_back(HasFaceColors() ? triColors[t] : Color{1, 1, 1, 1});
            for (int t = 0; t < other.TriangleCount(); ++t) col.push_back(other.HasFaceColors() ? other.triColors[t] : Color{1, 1, 1, 1});
        }
        std::vector<int> slot;
        int slotOffset = 0;
        if (slBoth) {
            slotOffset = (int)slotColors.size();
            if (!HasSlots()) slotOffset = std::max(1, (int)slotColors.size());   // this mesh's faces are all slot 0
            for (int t = 0; t < tBase; ++t) slot.push_back(HasSlots() ? triSlot[t] : 0);
            for (int t = 0; t < other.TriangleCount(); ++t) slot.push_back(slotOffset + (other.HasSlots() ? other.triSlot[t] : 0));
        }
        std::vector<int> poly;
        if (pfThis && pfOther) {
            poly = polyFace;
            int hi = 0; for (int id : polyFace) hi = std::max(hi, id + 1);
            for (int id : other.polyFace) poly.push_back(id + hi);
        }
        std::vector<Vec2> uv2;
        if (uvBoth) {
            uv2 = HasUVs() ? uvs : std::vector<Vec2>(vertices.size(), Vec2{0, 0});
            if (other.HasUVs()) uv2.insert(uv2.end(), other.uvs.begin(), other.uvs.end());
            else uv2.insert(uv2.end(), other.vertices.size(), Vec2{0, 0});
        }
        std::vector<Color> tbl = slotColors;
        if (slBoth) {
            if (tbl.empty()) tbl.push_back(Color{1, 1, 1, 1});
            while ((int)tbl.size() < slotOffset) tbl.push_back(Color{1, 1, 1, 1});
            tbl.insert(tbl.end(), other.slotColors.begin(), other.slotColors.end());
            const int need = slotOffset + std::max(1, other.SlotCount());
            while ((int)tbl.size() < need) tbl.push_back(Color{1, 1, 1, 1});
        }
        std::vector<Color> vc;
        if (HasVertexColors() || other.HasVertexColors()) {
            vc = HasVertexColors() ? vertexColors : std::vector<Color>(vertices.size(), Color{1, 1, 1, 1});
            if (other.HasVertexColors()) vc.insert(vc.end(), other.vertexColors.begin(), other.vertexColors.end());
            else vc.insert(vc.end(), other.vertices.size(), Color{1, 1, 1, 1});
        }
        name = "";
        vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
        for (int t : other.triangles) triangles.push_back(t + base);
        vertexColors = std::move(vc);
        triColors = std::move(col);
        triSlot = std::move(slot);
        slotColors = std::move(tbl);
        polyFace = std::move(poly);
        uvs = std::move(uv2);
        cornerUVs.clear();
        normals.clear();
        RefreshNormals();
    }
    static Mesh Combined(const Mesh& a, const Mesh& b) { Mesh m = a; m.Combine(b); return m; }

    /// How sound the surface is, by edge: an edge used by one triangle is a
    /// boundary (a hole), by more than two is non-manifold (overlapping or
    /// fanned geometry). Edges are matched by vertex position, so split vertices
    /// (UV/normal seams) still count as one edge.
    struct TopologyReport {
        int edges = 0, boundaryEdges = 0, nonManifoldEdges = 0, degenerateTris = 0;
        bool Closed() const { return boundaryEdges == 0 && nonManifoldEdges == 0; }
    };
    TopologyReport Topology() const {
        TopologyReport r;
        // Weld by position (quantised) so seams don't read as holes.
        std::map<std::tuple<long, long, long>, int> ids;
        std::vector<int> id(vertices.size());
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const Vec3& v = vertices[i];
            auto key = std::make_tuple(std::lround(v.x * 1e5f), std::lround(v.y * 1e5f), std::lround(v.z * 1e5f));
            auto it = ids.emplace(key, (int)ids.size()).first;
            id[i] = it->second;
        }
        std::map<std::pair<int, int>, int> uses;
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int a = triangles[t], b = triangles[t + 1], c = triangles[t + 2];
            if (a < 0 || b < 0 || c < 0 || a >= (int)id.size() || b >= (int)id.size() || c >= (int)id.size()) { ++r.degenerateTris; continue; }
            int k[3] = {id[a], id[b], id[c]};
            if (k[0] == k[1] || k[1] == k[2] || k[0] == k[2]) { ++r.degenerateTris; continue; }
            for (int e = 0; e < 3; ++e) {
                int p = k[e], q = k[(e + 1) % 3];
                ++uses[{std::min(p, q), std::max(p, q)}];
            }
        }
        r.edges = (int)uses.size();
        for (const auto& u : uses) {
            if (u.second == 1) ++r.boundaryEdges;
            else if (u.second > 2) ++r.nonManifoldEdges;
        }
        return r;
    }

    /// Mirror the mesh across an axis plane through the origin (0 = X / yz-plane,
    /// 1 = Y, 2 = Z), appending a flipped copy so the model becomes symmetric —
    /// model one half, mirror the rest. When `weld` is on, vertices on the mirror
    /// plane are merged so there's no seam down the middle.
    /// A mesh that crosses the plane is cut there first, keeping the + side, so
    /// the two halves never overlap (a centred cube stays one closed cube).
    void Mirror(int axis = 0, bool weld = true) {
        if (axis < 0 || axis > 2 || vertices.empty()) return;
        {
            const float eps = 1e-5f;
            bool pos = false, neg = false;
            for (const Vec3& v : vertices) {
                const float c = (&v.x)[axis];
                if (c > eps) pos = true;
                else if (c < -eps) neg = true;
            }
            if (pos && neg) {
                Vec3 n{axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f};
                Bisect({0, 0, 0}, n, /*cap=*/false, /*keepPositive=*/true);
                if (vertices.empty()) return;
            } else if (pos || neg) {
                // A closed half model with a face lying on the plane: after the
                // mirror that face and its copy would sit back to back inside the
                // model, so drop them (a mesh that is entirely flat on the plane
                // has no "inside" and keeps its faces).
                std::vector<int> onPlane;
                for (int f = 0; f * 3 + 2 < (int)triangles.size(); ++f) {
                    bool all = true;
                    for (int k = 0; k < 3 && all; ++k) {
                        const int vi = triangles[f * 3 + k];
                        all = vi >= 0 && vi < (int)vertices.size() && std::fabs((&vertices[vi].x)[axis]) <= eps;
                    }
                    if (all) onPlane.push_back(f);
                }
                if (!onPlane.empty()) DeleteFaces(onPlane);
            }
        }
        const bool faceCols = HasFaceColors();
        const bool hadNormals = HasNormals();
        Mesh copy = *this;
        for (Vec3& v : copy.vertices) (&v.x)[axis] = -(&v.x)[axis];   // reflect
        copy.FlipWinding();                                          // keep faces outward
        int base = (int)vertices.size();
        vertices.insert(vertices.end(), copy.vertices.begin(), copy.vertices.end());
        for (int t : copy.triangles) triangles.push_back(t + base);
        if (faceCols && copy.HasFaceColors())
            triColors.insert(triColors.end(), copy.triColors.begin(), copy.triColors.end());
        // Everything else that runs parallel to the vertices or faces is copied along, so the
        // mirrored half keeps its UVs, vertex colours, material slots, polygons and edge weights.
        if (copy.HasUVs() && uvs.size() == (std::size_t)base) uvs.insert(uvs.end(), copy.uvs.begin(), copy.uvs.end());
        else if (!uvs.empty()) uvs.assign(vertices.size(), Vec2{0, 0});
        if (copy.HasVertexColors() && vertexColors.size() == (std::size_t)base) vertexColors.insert(vertexColors.end(), copy.vertexColors.begin(), copy.vertexColors.end());
        else if (!vertexColors.empty()) vertexColors.assign(vertices.size(), Color{1, 1, 1, 1});
        if (copy.HasSlots()) triSlot.insert(triSlot.end(), copy.triSlot.begin(), copy.triSlot.end());
        if (!copy.polyFace.empty() && copy.polyFace.size() == copy.triangles.size() / 3) {
            int hi = 0; for (int id : polyFace) hi = std::max(hi, id + 1);
            for (int id : copy.polyFace) polyFace.push_back(id + hi);
        } else polyFace.clear();
        if (copy.HasCornerUVs()) cornerUVs.insert(cornerUVs.end(), copy.cornerUVs.begin(), copy.cornerUVs.end());
        else cornerUVs.clear();
        for (const auto& sm : copy.seams) seams.push_back({sm.first + base, sm.second + base});
        for (const EdgeCrease& c : copy.creases) creases.push_back({c.a + base, c.b + base, c.w});
        for (const EdgeCrease& c : copy.bevelWeights) bevelWeights.push_back({c.a + base, c.b + base, c.w});
        normals.clear();
        name = "";
        if (weld) WeldVertices();
        if (hadNormals) ComputeSmoothNormals();      // preserve smooth shading through the mirror
    }
    Mesh Mirrored(int axis = 0, bool weld = true) const { Mesh m = *this; m.Mirror(axis, weld); return m; }

    /// Symmetrize: keep the positive half across the `axis` plane through the pivot (0=X 1=Y 2=Z), then mirror it
    /// over the other side and weld the seam, so the mesh is exactly symmetric. The one implementation behind the
    /// Modeling window's Symmetrize buttons (Mirror X/Y/Z, by contrast, adds a mirrored copy of the whole mesh).
    void Symmetrize(int axis) {
        if (axis < 0 || axis > 2 || vertices.empty()) return;
        const Vec3 n{axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f};
        Bisect({0, 0, 0}, n, false, true);   // keep + half, seam open
        Mirror(axis);                        // reflect + weld the seam
        name = "";
    }

    /// Push every vertex toward a sphere centred on the mesh, by `amount` in [0,1]
    /// (0 = unchanged, 1 = fully spherical) — round off a cube into a ball, or just
    /// soften a blocky shape. The sphere radius is the average vertex distance.
    void Spherify(float amount = 1.0f) {
        if (!std::isfinite(amount)) return;   // a NaN or infinite amount would turn every vertex into NaN
        if (vertices.empty()) return;
        Vec3 lo, hi; Bounds(lo, hi);
        Vec3 c{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
        float rsum = 0.0f;
        for (const Vec3& v : vertices) {
            Vec3 d{v.x - c.x, v.y - c.y, v.z - c.z};
            rsum += std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        }
        float r = rsum / (float)vertices.size();
        for (Vec3& v : vertices) {
            Vec3 d{v.x - c.x, v.y - c.y, v.z - c.z};
            float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (len < 1e-6f) continue;
            Vec3 onSphere{c.x + d.x / len * r, c.y + d.y / len * r, c.z + d.z / len * r};
            v.x += (onSphere.x - v.x) * amount;
            v.y += (onSphere.y - v.y) * amount;
            v.z += (onSphere.z - v.z) * amount;
        }
        name = "";
        RefreshNormals();                            // rounding a shape re-smooths its lighting
    }

    /// Twist the mesh around an axis: each vertex rotates about that axis by
    /// `degreesPerUnit` × (its distance along the axis from the mesh centre) — the
    /// classic "wring the model" deformer. axis: 0 = X, 1 = Y, 2 = Z.
    void Twist(float degreesPerUnit, int axis = 1) {
        if (!std::isfinite(degreesPerUnit)) return;   // a NaN or infinite amount would turn every vertex into NaN
        if (vertices.empty() || axis < 0 || axis > 2) return;
        Vec3 lo, hi; Bounds(lo, hi);
        float mid = ((&lo.x)[axis] + (&hi.x)[axis]) * 0.5f;
        const float deg2rad = 3.14159265358979323846f / 180.0f;
        int a = (axis + 1) % 3, b = (axis + 2) % 3;   // the two perpendicular axes
        for (Vec3& v : vertices) {
            float along = (&v.x)[axis] - mid;
            float ang = degreesPerUnit * along * deg2rad;
            float ca = std::cos(ang), sa = std::sin(ang);
            float pa = (&v.x)[a], pb = (&v.x)[b];
            (&v.x)[a] = pa * ca - pb * sa;
            (&v.x)[b] = pa * sa + pb * ca;
        }
        name = "";
        RefreshNormals();
    }

    /// Taper along an axis: scale the two perpendicular axes from full size at the
    /// bottom (min of `axis`) to `endScale` at the top — turn a cylinder into a cone
    /// or a box into a frustum/wedge. axis: 0 = X, 1 = Y, 2 = Z.
    void Taper(int axis = 1, float endScale = 0.5f) {
        if (!std::isfinite(endScale)) return;   // a NaN or infinite amount would turn every vertex into NaN
        if (vertices.empty() || axis < 0 || axis > 2) return;
        Vec3 lo, hi; Bounds(lo, hi);
        float a0 = (&lo.x)[axis], a1 = (&hi.x)[axis], range = a1 - a0;
        if (range < 1e-6f) return;
        Vec3 c{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
        int b = (axis + 1) % 3, d = (axis + 2) % 3;
        for (Vec3& v : vertices) {
            float t = ((&v.x)[axis] - a0) / range;       // 0 at bottom .. 1 at top
            float s = 1.0f + (endScale - 1.0f) * t;
            (&v.x)[b] = (&c.x)[b] + ((&v.x)[b] - (&c.x)[b]) * s;
            (&v.x)[d] = (&c.x)[d] + ((&v.x)[d] - (&c.x)[d]) * s;
        }
        name = "";
        RefreshNormals();
    }

    /// Bend the mesh into an arc: sweep it around a circle so a straight bar curves by
    /// `degrees` total over its length along `axis` (0 = X, 1 = Y, 2 = Z). The bend
    /// happens in the plane of `axis` and the next axis. 0 leaves it straight.
    void Bend(float degrees, int axis = 0) {
        if (!std::isfinite(degrees)) return;   // a NaN or infinite amount would turn every vertex into NaN
        if (vertices.empty() || axis < 0 || axis > 2 || std::fabs(degrees) < 1e-4f) return;
        Vec3 lo, hi; Bounds(lo, hi);
        float a0 = (&lo.x)[axis], a1 = (&hi.x)[axis], len = a1 - a0;
        if (len < 1e-6f) return;
        const float kPi = 3.14159265358979323846f;
        float total = degrees * kPi / 180.0f;            // total bend angle
        float radius = len / total;                       // arc radius so the ends meet the angle
        float mid = (a0 + a1) * 0.5f;
        int up = (axis + 1) % 3;                          // the axis we curl toward
        for (Vec3& v : vertices) {
            float s = (&v.x)[axis] - mid;                 // distance along the bar from centre
            float h = (&v.x)[up];                         // offset from the neutral fibre
            float ang = s / radius;
            float r = radius + h;                         // outer fibres travel a larger arc
            (&v.x)[axis] = std::sin(ang) * r;
            (&v.x)[up]   = radius - std::cos(ang) * r;
        }
        normals.clear();
        name = "";
    }

    /// Give a thin surface real thickness: duplicate it, push the copy inward along
    /// the (smooth) vertex normals by `thickness`, and flip it so the shell reads
    /// solid from both sides — turn a plane/curved sheet into a slab/bowl wall.
    void Solidify(float thickness = 0.1f) {
        if (!std::isfinite(thickness)) return;   // a NaN or infinite amount would turn every vertex into NaN
        if (vertices.empty() || triangles.empty()) return;
        Mesh src = *this;
        if (!src.HasNormals()) src.ComputeSmoothNormals();
        Mesh inner = src;
        for (std::size_t i = 0; i < inner.vertices.size(); ++i) {
            const Vec3& n = src.normals[i];
            inner.vertices[i].x -= n.x * thickness;
            inner.vertices[i].y -= n.y * thickness;
            inner.vertices[i].z -= n.z * thickness;
        }
        inner.FlipWinding();
        std::vector<Color> innerCols = HasFaceColors() ? triColors : std::vector<Color>{};   // copy before growing
        int base = (int)vertices.size();
        vertices.insert(vertices.end(), inner.vertices.begin(), inner.vertices.end());
        for (int t : inner.triangles) triangles.push_back(t + base);
        if (!innerCols.empty()) triColors.insert(triColors.end(), innerCols.begin(), innerCols.end());
        // The inner shell is a copy of the surface, so it keeps the same UVs, vertex colours, material slots and polygons.
        if (src.HasUVs()) uvs.insert(uvs.end(), src.uvs.begin(), src.uvs.end()); else uvs.clear();
        if (src.HasVertexColors()) vertexColors.insert(vertexColors.end(), src.vertexColors.begin(), src.vertexColors.end()); else vertexColors.clear();
        if (src.HasSlots()) triSlot.insert(triSlot.end(), src.triSlot.begin(), src.triSlot.end()); else triSlot.clear();
        if (!src.polyFace.empty() && src.polyFace.size() == src.triangles.size() / 3) {
            int hi = 0; for (int id : src.polyFace) hi = std::max(hi, id + 1);
            for (int id : src.polyFace) polyFace.push_back(id + hi);
        } else polyFace.clear();
        if (src.HasCornerUVs()) cornerUVs.insert(cornerUVs.end(), inner.cornerUVs.begin(), inner.cornerUVs.end()); else cornerUVs.clear();
        AddSolidifyRim(src, base);
        // A negative thickness puts the new shell outside: turn every face round so the solid still faces outward.
        if (thickness < 0.0f) FlipWinding();
        normals.clear();
        name = "";
    }

private:
    /// Solidify's rim: close the gap between a surface and its offset copy (vertex i <-> i + base) with a wall of
    /// quads along every open border, so a sheet becomes a closed slab. An edge is open when no other triangle uses it
    /// in the opposite direction; vertices are matched by position so a UV seam (two vertices at one point) is not a
    /// border. The wall keeps the border face's colour and slot, and is wound to face away from the sheet.
    void AddSolidifyRim(const Mesh& src, int base) {
        const int nTri = src.TriangleCount();
        if (nTri <= 0) return;
        std::map<std::tuple<float, float, float>, int> idOf;            // one id per distinct position
        std::vector<int> canon(src.vertices.size());
        for (std::size_t i = 0; i < src.vertices.size(); ++i) {
            const Vec3& v = src.vertices[i];
            canon[i] = idOf.emplace(std::make_tuple(v.x, v.y, v.z), (int)idOf.size()).first->second;
        }
        std::map<std::pair<int, int>, int> directed;                   // (from, to) position ids -> uses
        for (int t = 0; t < nTri; ++t)
            for (int k = 0; k < 3; ++k) {
                const int a = src.triangles[t * 3 + k], b = src.triangles[t * 3 + (k + 1) % 3];
                if (a < 0 || b < 0 || a >= (int)canon.size() || b >= (int)canon.size()) continue;
                ++directed[{canon[a], canon[b]}];
            }
        const bool fc = HasFaceColors(), sl = HasSlots(), cuv = HasCornerUVs();
        const bool pf = !polyFace.empty() && polyFace.size() * 3 == triangles.size();
        int nextPoly = 0; if (pf) for (int id : polyFace) nextPoly = std::max(nextPoly, id + 1);
        for (int t = 0; t < nTri; ++t)
            for (int k = 0; k < 3; ++k) {
                const int a = src.triangles[t * 3 + k], b = src.triangles[t * 3 + (k + 1) % 3];
                if (a < 0 || b < 0 || a >= (int)canon.size() || b >= (int)canon.size() || canon[a] == canon[b]) continue;
                if (directed.count({canon[b], canon[a]}) || directed[{canon[a], canon[b]}] != 1) continue;   // shared (or non-manifold) edge
                // The face runs a -> b, so its neighbour across a border would run b -> a: the wall does too, then
                // drops to the copy (a' = a + base, b' = b + base).
                triangles.insert(triangles.end(), {b, a, a + base});
                triangles.insert(triangles.end(), {b, a + base, b + base});
                if (fc) { triColors.push_back(triColors[t]); triColors.push_back(triColors[t]); }
                if (sl) { triSlot.push_back(triSlot[t]); triSlot.push_back(triSlot[t]); }
                if (pf) { polyFace.push_back(nextPoly); polyFace.push_back(nextPoly); ++nextPoly; }
                if (cuv) {
                    const Vec2 ua = cornerUVs[t * 3 + k], ub = cornerUVs[t * 3 + (k + 1) % 3];
                    cornerUVs.insert(cornerUVs.end(), {ub, ua, ua, ub, ua, ub});
                }
            }
    }

public:

    /// Extrude a 2D outline (XY, convex, counter-clockwise) into a 3D prism of
    /// the given depth along Z — custom signs, logos, blocky props. Builds a
    /// front and back cap (fan-triangulated) plus side walls.
    static Mesh Extrude(const std::vector<Vec2>& outline, float depth = 1.0f);

    /// Revolve a 2D profile around the Y axis to make a surface of revolution —
    /// vases, bottles, goblets, columns. Each profile point is (radius, height);
    /// `segments` sets the angular resolution. Faces wind outward whichever way
    /// the profile runs (bottom-to-top or top-to-bottom).
    static Mesh Lathe(const std::vector<Vec2>& profile, int segments = 16);

    /// Merge vertices closer than `epsilon` into one and re-index triangles,
    /// then drop any triangle that collapsed to a line/point. Cleans up imported
    /// OBJs and Combine()d meshes so they're watertight. Returns vertices removed.
    int WeldVertices(float epsilon = 1e-5f) {
        std::vector<Vec3> unique;
        std::vector<int>  remap(vertices.size());
        // Preserve the parallel per-vertex UVs so welding a textured mesh keeps its
        // texture coordinates (previously the stale uvs array was silently dropped,
        // un-texturing the mesh). The kept vertex's UV wins at a merged seam.
        const bool hadUV = uvs.size() == vertices.size();
        const bool hadVC = HasVertexColors();
        std::vector<Vec2> uniqueUV; std::vector<Color> uniqueVC;
        if (!(epsilon > 0.0f)) epsilon = 0.0f;   // negative or NaN distance: merge exact repeats only
        float e2 = epsilon * epsilon;
        // A grid of cells as wide as the distance finds the near vertices without comparing
        // every pair (the all-pairs search took about 6 s on a 240k-triangle mesh). The first
        // unique vertex within the distance still wins, as before.
        const float cellW = std::max(epsilon, 1e-6f);
        auto cellOf = [&](const Vec3& p) {
            return std::make_tuple((long long)std::floor(p.x / cellW), (long long)std::floor(p.y / cellW), (long long)std::floor(p.z / cellW));
        };
        std::map<std::tuple<long long, long long, long long>, std::vector<int>> grid;
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            int found = -1;
            const auto c = cellOf(vertices[i]);
            for (long long dx = -1; dx <= 1; ++dx)
                for (long long dy = -1; dy <= 1; ++dy)
                    for (long long dz = -1; dz <= 1; ++dz) {
                        auto it = grid.find(std::make_tuple(std::get<0>(c) + dx, std::get<1>(c) + dy, std::get<2>(c) + dz));
                        if (it == grid.end()) continue;
                        for (int j : it->second)
                            if ((unique[j] - vertices[i]).SqrMagnitude() <= e2 && (found < 0 || j < found)) found = j;
                    }
            if (found < 0) {
                found = (int)unique.size(); unique.push_back(vertices[i]);
                grid[c].push_back(found);
                if (hadUV) uniqueUV.push_back(uvs[i]);
                if (hadVC) uniqueVC.push_back(vertexColors[i]);
            }
            remap[i] = found;
        }
        int removed = (int)vertices.size() - (int)unique.size();
        // Rebuild triangles, dropping any face that collapsed, and keep the parallel
        // per-triangle face colors aligned to the surviving faces.
        const bool hadFC = HasFaceColors();
        const bool hadPF = (int)polyFace.size() == TriangleCount() && !polyFace.empty();
        const bool hadCUV = HasCornerUVs();
        const bool hadSlots = HasSlots();
        std::vector<int>   tris, pf, sl;
        std::vector<Color> fc;
        std::vector<Vec2>  cuv;
        for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
            int a = remap[triangles[i]], b = remap[triangles[i + 1]], c = remap[triangles[i + 2]];
            if (a != b && b != c && a != c) {
                tris.insert(tris.end(), {a, b, c}); // skip degenerate
                if (hadFC) fc.push_back(triColors[i / 3]);
                if (hadPF) pf.push_back(polyFace[i / 3]);
                if (hadSlots) sl.push_back(triSlot[i / 3]);
                if (hadCUV) cuv.insert(cuv.end(), {cornerUVs[i], cornerUVs[i + 1], cornerUVs[i + 2]});
            }
        }
        std::vector<std::pair<int, int>> sm;
        for (const auto& e : seams) {
            if (e.first < 0 || e.second < 0 || e.first >= (int)remap.size() || e.second >= (int)remap.size()) continue;
            int a = remap[e.first], b = remap[e.second];
            if (a == b) continue;
            if (a > b) std::swap(a, b);
            if (std::find(sm.begin(), sm.end(), std::make_pair(a, b)) == sm.end()) sm.push_back({a, b});
        }
        seams = std::move(sm);
        for (std::vector<EdgeCrease>* list : {&creases, &bevelWeights}) {   // creases and bevel weights follow their vertices the same way
            std::vector<EdgeCrease> cr;
            for (const EdgeCrease& c : *list) {
                if (c.a < 0 || c.b < 0 || c.a >= (int)remap.size() || c.b >= (int)remap.size()) continue;
                int a = remap[c.a], b = remap[c.b];
                if (a == b) continue;
                if (a > b) std::swap(a, b);
                bool merged = false;
                for (EdgeCrease& e : cr) if (e.a == a && e.b == b) { e.w = std::max(e.w, c.w); merged = true; break; }
                if (!merged) cr.push_back({a, b, c.w});
            }
            *list = std::move(cr);
        }
        vertices = std::move(unique);
        triangles = std::move(tris);
        if (hadUV) uvs = std::move(uniqueUV); else uvs.clear();
        if (hadVC) vertexColors = std::move(uniqueVC); else vertexColors.clear();
        if (hadFC) triColors = std::move(fc); else triColors.clear();
        if (hadPF) polyFace = std::move(pf); else polyFace.clear();
        if (hadSlots) triSlot = std::move(sl); else triSlot.clear();
        if (hadCUV) cornerUVs = std::move(cuv); else cornerUVs.clear();
        if (removed > 0) name = "";
        RefreshNormals();   // rebuild smooth normals at the welded resolution (or stay flat)
        return removed;
    }

    /// Subdivide every triangle into 4 by splitting each edge at its midpoint —
    /// adds detail (and, after re-projecting, smooths primitives). Midpoints are
    /// shared between adjacent triangles so the mesh stays welded.
    void Subdivide() {
        AttrGuard attrGuard_(*this);
        std::vector<int> out;
        std::map<std::pair<int, int>, int> mids;     // edge -> new midpoint index
        auto midpoint = [&](int a, int b) {
            auto key = std::minmax(a, b);
            auto it = mids.find({key.first, key.second});
            if (it != mids.end()) return it->second;
            Vec3 m = (vertices[a] + vertices[b]) * 0.5f;
            int idx = (int)vertices.size();
            vertices.push_back(m);
            mids[{key.first, key.second}] = idx;
            return idx;
        };
        const bool faceCols = HasFaceColors();
        std::vector<Color> outCols;
        for (std::size_t i = 0, face = 0; i + 2 < triangles.size(); i += 3, ++face) {
            int a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            int ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
            out.insert(out.end(), {a, ab, ca,  ab, b, bc,  ca, bc, c,  ab, bc, ca});
            if (faceCols) { Color fc = triColors[face]; outCols.insert(outCols.end(), {fc, fc, fc, fc}); }
        }
        triangles = std::move(out);
        if (faceCols) triColors = std::move(outCols);
        name = "";
        RefreshNormals();                            // keep lighting valid at the new resolution
    }

    /// Push every vertex onto a sphere of the given radius about the origin —
    /// pair with Subdivide() to turn a cube/icosphere into a smooth ball.
    void ProjectToSphere(float radius = 0.5f) {
        if (!std::isfinite(radius)) return;   // a typed NaN or infinite value would turn every vertex into NaN
        for (Vec3& v : vertices) {
            float m = v.Magnitude();
            if (m > 1e-6f) v = v * (radius / m);
        }
        name = "";
        RefreshNormals();
    }

    /// Per-vertex normals, area-weighted from the adjacent faces (for lighting /
    /// export). Size matches `vertices`; degenerate meshes give zero vectors.
    std::vector<Vec3> Normals() const {
        std::vector<Vec3> n(vertices.size(), Vec3{0, 0, 0});
        for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
            int a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            Vec3 face = Vec3::Cross(vertices[b] - vertices[a], vertices[c] - vertices[a]);
            n[a] += face; n[b] += face; n[c] += face;   // area-weighted accumulation
        }
        for (Vec3& v : n) if (v.SqrMagnitude() > 1e-12f) v = v.Normalized();
        return n;
    }

    /// A capsule (cylinder body with hemispherical caps) along Y.
    static Mesh Capsule(float radius = 0.5f, float height = 1.0f, int sectors = 12, int rings = 6);

    /// Append another mesh's geometry, scaled (per-axis) then translated.
    /// Triangle indices are rebased; this is how compound meshes are built. If
    /// `col` is given, every appended face takes that color (per-part coloring).
    void Add(const Mesh& src, Vec3 offset, Vec3 scale = {1, 1, 1}, const Color* col = nullptr) {
        int base = (int)vertices.size();
        for (const Vec3& v : src.vertices)
            vertices.push_back({v.x * scale.x + offset.x,
                                v.y * scale.y + offset.y,
                                v.z * scale.z + offset.z});
        for (int t : src.triangles) triangles.push_back(t + base);
        if (col) for (int i = 0, n = (int)src.triangles.size() / 3; i < n; ++i) triColors.push_back(*col);
    }

    /// As Add(), but rotate (Euler degrees) about `pivot` after scaling — used to
    /// pose limbs (e.g. spread arms into an A-pose).
    void AddPosed(const Mesh& src, Vec3 offset, Vec3 scale, Vec3 eulerDeg, Vec3 pivot,
                  const Color* col = nullptr) {
        int base = (int)vertices.size();
        Quat q = Quat::Euler(eulerDeg);
        for (const Vec3& v : src.vertices) {
            Vec3 s{v.x * scale.x + offset.x - pivot.x,
                   v.y * scale.y + offset.y - pivot.y,
                   v.z * scale.z + offset.z - pivot.z};
            Vec3 r = q * s;
            vertices.push_back({r.x + pivot.x, r.y + pivot.y, r.z + pivot.z});
        }
        for (int t : src.triangles) triangles.push_back(t + base);
        if (col) for (int i = 0, n = (int)src.triangles.size() / 3; i < n; ++i) triColors.push_back(*col);
    }

    /// Concatenate another mesh wholesale (vertices, rebased triangles, face
    /// colors) — used to merge pre-built parts into one mesh.
    void Append(const Mesh& src) {
        int base = (int)vertices.size();
        vertices.insert(vertices.end(), src.vertices.begin(), src.vertices.end());
        for (int t : src.triangles) triangles.push_back(t + base);
        triColors.insert(triColors.end(), src.triColors.begin(), src.triColors.end());
    }

    /// Laplacian relaxation: move each vertex toward (positive `amount`) or away
    /// from (negative) the average of its edge-connected neighbours. Coincident
    /// vertices are treated as one so seams between welded parts/faces round too.
    void Smooth(float amount = 0.5f) {
        if (!std::isfinite(amount)) return;   // a NaN or infinite amount would turn every vertex into NaN
        if (amount == 0.0f || vertices.empty()) return;
        const std::size_t n = vertices.size();
        // Group coincident vertices (cube faces, joined parts) so smoothing
        // propagates across seams instead of leaving hard creases.
        std::map<std::tuple<int, int, int>, int> grid;
        std::vector<int> rep(n);
        auto key = [](const Vec3& v) {
            return std::make_tuple((int)std::lround(v.x * 2048.0f),
                                   (int)std::lround(v.y * 2048.0f),
                                   (int)std::lround(v.z * 2048.0f));
        };
        for (std::size_t i = 0; i < n; ++i) {
            auto k = key(vertices[i]);
            auto it = grid.find(k);
            if (it == grid.end()) { grid[k] = (int)i; rep[i] = (int)i; }
            else rep[i] = it->second;
        }
        std::vector<Vec3> sum(n, Vec3{0, 0, 0});
        std::vector<int>  cnt(n, 0);
        auto link = [&](int a, int b) { int ra = rep[a], rb = rep[b];
            sum[ra] = sum[ra] + vertices[rb]; ++cnt[ra]; };
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int i0 = triangles[t], i1 = triangles[t + 1], i2 = triangles[t + 2];
            link(i0, i1); link(i1, i0); link(i1, i2); link(i2, i1); link(i2, i0); link(i0, i2);
        }
        std::vector<Vec3> moved(n);
        for (std::size_t i = 0; i < n; ++i) {
            int r = rep[i];
            moved[i] = (cnt[r] > 0)
                ? vertices[i] * (1.0f - amount) + sum[r] * (amount / cnt[r])
                : vertices[i];
        }
        vertices = std::move(moved);
        RefreshNormals();
    }

    /// Take low-poly geometry up to smooth high-poly: `iterations` rounds of
    /// Subdivide() + Taubin smoothing (a shrink pass then an inflate pass) so the
    /// result rounds out WITHOUT collapsing/shrinking like plain Laplacian.
    void SubdivideSmooth(int iterations = 1, float amount = 0.5f) {
        for (int i = 0; i < iterations && !triangles.empty(); ++i) {
            Subdivide();
            Smooth(amount);                       // shrink toward neighbours
            Smooth(-(amount * 1.05f + 0.02f));    // inflate back (Taubin) — keeps volume
        }
    }

    // ---- Interactive editing (Blender-like mesh ops) -------------------
    // These operate on the triangle-indexed mesh, mark it custom (clear `name`)
    // so edited geometry is serialized verbatim rather than regenerated from a
    // primitive name, and recompute normals where it matters.

    /// Normalized geometric normal of triangle `f` (cross of its two edges).
    /// Returns the zero vector for a degenerate (collinear) triangle.
    Vec3 FaceNormal(int f) const {
        int i = f * 3;
        if (i < 0 || i + 2 >= (int)triangles.size()) return Vec3{0, 0, 0};
        int a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
        Vec3 n = Vec3::Cross(vertices[b] - vertices[a], vertices[c] - vertices[a]);
        float m = n.Magnitude();
        return m > 1e-8f ? n * (1.0f / m) : Vec3{0, 0, 0};
    }

    /// Centroid (average of the three corners) of triangle `f`.
    Vec3 FaceCenter(int f) const {
        int i = f * 3;
        if (i < 0 || i + 2 >= (int)triangles.size()) return Vec3{0, 0, 0};
        return (vertices[triangles[i]] + vertices[triangles[i + 1]] + vertices[triangles[i + 2]])
               * (1.0f / 3.0f);
    }

    /// Add `delta` to each listed vertex position (the move/translate gizmo).
    /// The caller resolves welded selections into the index list it passes.
    void MoveVertices(const std::vector<int>& verts, const Vec3& delta) {
        for (int v : verts)
            if (v >= 0 && v < (int)vertices.size()) vertices[v] += delta;
        name = "";
        RefreshNormals();
    }

    /// Move a vertex set with a proportional (soft-selection) falloff: every
    /// vertex within `radius` of the selection also moves, scaled by a smoothstep
    /// of its distance to the nearest selected vertex — Blender's proportional
    /// editing. radius <= 0 behaves exactly like MoveVertices.
    void MoveVerticesSoft(const std::vector<int>& verts, const Vec3& delta, float radius) {
        if (!std::isfinite(radius + delta.x + delta.y + delta.z)) return;   // a NaN or infinite amount would turn every vertex into NaN
        if (radius <= 1e-6f) { MoveVertices(verts, delta); return; }
        std::vector<Vec3> anchors;
        anchors.reserve(verts.size());
        for (int v : verts)
            if (v >= 0 && v < (int)vertices.size()) anchors.push_back(vertices[v]);
        if (anchors.empty()) return;
        for (Vec3& p : vertices) {
            float d2min = 1e30f;
            for (const Vec3& a : anchors) {
                Vec3 d = p - a;
                float d2 = d.x*d.x + d.y*d.y + d.z*d.z;
                if (d2 < d2min) d2min = d2;
            }
            float dist = std::sqrt(d2min);
            if (dist >= radius) continue;
            float t = 1.0f - dist / radius;
            p += delta * (t * t * (3.0f - 2.0f * t));   // smoothstep falloff
        }
        name = "";
        RefreshNormals();
    }

    /// Laplacian-relax ONLY the listed vertices (the rest stay put) — evens out
    /// a lumpy patch without melting the whole mesh like Smooth() would.
    void SmoothVertices(const std::vector<int>& verts, float amount = 0.5f) {
        if (!std::isfinite(amount)) return;   // a NaN or infinite amount would turn every vertex into NaN
        if (verts.empty() || amount == 0.0f || vertices.empty()) return;
        std::vector<Vec3> sum(vertices.size(), Vec3{0, 0, 0});
        std::vector<int>  cnt(vertices.size(), 0);
        auto link = [&](int a, int b) { sum[a] += vertices[b]; ++cnt[a]; };
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int i0 = triangles[t], i1 = triangles[t + 1], i2 = triangles[t + 2];
            link(i0, i1); link(i1, i0); link(i1, i2); link(i2, i1); link(i2, i0); link(i0, i2);
        }
        std::vector<Vec3> moved = vertices;
        for (int v : verts)
            if (v >= 0 && v < (int)vertices.size() && cnt[v] > 0)
                moved[v] = vertices[v] * (1.0f - amount) + sum[v] * (amount / cnt[v]);
        vertices = std::move(moved);
        name = "";
        RefreshNormals();
    }

    /// Move the listed vertices along their own normals by `dist` — Blender's
    /// Shrink/Fatten (Alt+S): inflate or carve a selected patch without dragging
    /// an axis. Uses smooth per-vertex normals so the offset follows curvature.
    void ShrinkFattenVertices(const std::vector<int>& verts, float dist) {
        if (verts.empty() || !std::isfinite(dist) || std::fabs(dist) < 1e-8f || vertices.empty()) return;
        std::vector<Vec3> n = Normals();
        for (int v : verts)
            if (v >= 0 && v < (int)vertices.size()) vertices[v] += n[v] * dist;
        name = "";
        RefreshNormals();
    }

    /// Reshape the listed vertices toward a sphere around their centroid by
    /// `amount` in [0,1] (0 = unchanged, 1 = perfectly spherical) — Blender's
    /// To Sphere (Shift+Alt+S). The sphere radius is the selection's mean distance
    /// from its centre, so the patch rounds without ballooning.
    void SphereizeVertices(const std::vector<int>& verts, float amount) {
        if (verts.empty() || !(amount > 0.0f) || vertices.empty()) return;   // (NaN fails the test)
        float t = amount > 1.0f ? 1.0f : amount;
        Vec3 c{0, 0, 0}; int n = 0;
        for (int v : verts) if (v >= 0 && v < (int)vertices.size()) { c += vertices[v]; ++n; }
        if (n == 0) return;
        c = c * (1.0f / n);
        float r = 0.0f;
        for (int v : verts) if (v >= 0 && v < (int)vertices.size()) r += (vertices[v] - c).Magnitude();
        r /= (float)n;
        for (int v : verts) {
            if (v < 0 || v >= (int)vertices.size()) continue;
            Vec3 d = vertices[v] - c;
            float m = d.Magnitude();
            if (m < 1e-6f) continue;
            Vec3 onSphere = c + d * (r / m);
            vertices[v] = vertices[v] + (onSphere - vertices[v]) * t;
        }
        name = "";
        RefreshNormals();
    }

    /// Snap the listed vertices to their shared average coordinate on one axis
    /// (0=X 1=Y 2=Z) — Blender's "flatten" (S+axis+0): level a rim, square a wall.
    void FlattenVertices(const std::vector<int>& verts, int axis) {
        if (verts.empty() || axis < 0 || axis > 2) return;
        float avg = 0.0f; int n = 0;
        for (int v : verts)
            if (v >= 0 && v < (int)vertices.size()) { avg += (&vertices[v].x)[axis]; ++n; }
        if (n == 0) return;
        avg /= (float)n;
        for (int v : verts)
            if (v >= 0 && v < (int)vertices.size()) (&vertices[v].x)[axis] = avg;
        name = "";
        RefreshNormals();
    }

    /// Split the listed triangles off into a NEW mesh (returned) and delete them
    /// from this one — Blender's "separate selection". The new mesh gets compacted
    /// vertices with their UVs and face colors carried over.
    Mesh SeparateFaces(const std::vector<int>& faces) {
        Mesh out;
        if (faces.empty()) return out;
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        const bool hadColors = HasFaceColors();
        std::map<int, int> remap;
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            for (int k = 0; k < 3; ++k) {
                int v = triangles[i + k];
                auto it = remap.find(v);
                int nv;
                if (it == remap.end()) {
                    nv = (int)out.vertices.size(); remap[v] = nv;
                    out.vertices.push_back(vertices[v]);
                    if (hadUV) out.uvs.push_back(uvs[v]);
                    if (HasVertexColors()) out.vertexColors.push_back(vertexColors[v]);
                } else nv = it->second;
                out.triangles.push_back(nv);
            }
            if (hadColors && f < (int)triColors.size()) out.triColors.push_back(triColors[f]);
            if (HasSlots()) out.triSlot.push_back(triSlot[f]);
        }
        if (HasSlots() && (int)out.triSlot.size() == out.TriangleCount()) out.slotColors = slotColors; else out.triSlot.clear();
        out.RefreshNormals();
        DeleteFaces(faces);
        return out;
    }

    /// Region-extrude the selected set of triangles along their averaged normal:
    /// the selected faces are detached and pushed out by `dist`, and the boundary
    /// of the region is bridged with side walls so the cap stays connected. The
    /// passed `faces` indices stay valid (those slots are re-pointed, not removed),
    /// so the caller's selection still refers to the extruded cap.
    void ExtrudeFaces(const std::vector<int>& faces, float dist) {
        AttrGuard attrGuard_(*this);
        if (faces.empty() || !std::isfinite(dist)) return;
        // Averaged (area-weighted) normal of the selected faces.
        Vec3 N{0, 0, 0};
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            int a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            N += Vec3::Cross(vertices[b] - vertices[a], vertices[c] - vertices[a]); // area-weighted
        }
        float nm = N.Magnitude();
        N = nm > 1e-8f ? N * (1.0f / nm) : Vec3{0, 0, 0};
        // Boundary edges: undirected edges used by exactly ONE selected triangle.
        std::map<std::pair<int, int>, int> edgeCount;
        auto ekey = [](int a, int b) { return std::make_pair(std::min(a, b), std::max(a, b)); };
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            int a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            auto k1 = ekey(a, b); ++edgeCount[{k1.first, k1.second}];
            auto k2 = ekey(b, c); ++edgeCount[{k2.first, k2.second}];
            auto k3 = ekey(c, a); ++edgeCount[{k3.first, k3.second}];
        }
        // Duplicate every vertex used by the selected region, pushed out by N*dist.
        std::map<int, int> newIndex;
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            for (int k = 0; k < 3; ++k) {
                int v = triangles[i + k];
                if (newIndex.find(v) == newIndex.end()) {
                    newIndex[v] = (int)vertices.size();
                    vertices.push_back(vertices[v] + N * dist);
                }
            }
        }
        // Re-point the selected triangles to the duplicated (moved-out) vertices,
        // and bridge each boundary edge — oriented as it appears in its triangle so
        // the side-wall winding stays outward-consistent.
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            int v0 = triangles[i], v1 = triangles[i + 1], v2 = triangles[i + 2];
            int e[3][2] = {{v0, v1}, {v1, v2}, {v2, v0}};
            for (auto& pr : e) {
                auto k = ekey(pr[0], pr[1]);
                if (edgeCount[{k.first, k.second}] == 1) {       // boundary edge a->b
                    int a = pr[0], b = pr[1];
                    int na = newIndex[a], nb = newIndex[b];
                    triangles.insert(triangles.end(), {a, b, nb}); // side wall (two tris)
                    triangles.insert(triangles.end(), {a, nb, na});
                }
            }
            triangles[i] = newIndex[v0]; triangles[i + 1] = newIndex[v1]; triangles[i + 2] = newIndex[v2];
        }
        name = "";
        RefreshNormals();     // keep the mesh's shading mode (flat stays flat → hard extrude edges)
    }

    /// Inset the selected region: duplicate its vertices, pulled toward the region
    /// centroid by `amount` (a [0,1] fraction of each vertex's distance to centre),
    /// re-point the selected faces to the inner ring, and bridge the boundary so a
    /// smaller inner cap sits inside the original outline.
    void InsetFaces(const std::vector<int>& faces, float amount) {
        AttrGuard attrGuard_(*this);
        if (faces.empty() || !std::isfinite(amount)) return;
        float t = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount);
        // Boundary edges (used by exactly one selected triangle).
        std::map<std::pair<int, int>, int> edgeCount;
        auto ekey = [](int a, int b) { return std::make_pair(std::min(a, b), std::max(a, b)); };
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            int a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            auto k1 = ekey(a, b); ++edgeCount[{k1.first, k1.second}];
            auto k2 = ekey(b, c); ++edgeCount[{k2.first, k2.second}];
            auto k3 = ekey(c, a); ++edgeCount[{k3.first, k3.second}];
        }
        // Centroid of all vertices used by the selected faces.
        std::map<int, int> newIndex;
        Vec3 C{0, 0, 0}; int count = 0;
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            for (int k = 0; k < 3; ++k) {
                int v = triangles[i + k];
                if (newIndex.find(v) == newIndex.end()) { newIndex[v] = -1; C += vertices[v]; ++count; }
            }
        }
        if (count > 0) C = C * (1.0f / count);
        // Duplicate each region vertex, lerped toward the centroid by `t`.
        for (auto& kv : newIndex) {
            int v = kv.first;
            kv.second = (int)vertices.size();
            vertices.push_back(vertices[v] + (C - vertices[v]) * t);
        }
        // Re-point selected faces to the inner ring and bridge boundary edges.
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            int v0 = triangles[i], v1 = triangles[i + 1], v2 = triangles[i + 2];
            int e[3][2] = {{v0, v1}, {v1, v2}, {v2, v0}};
            for (auto& pr : e) {
                auto k = ekey(pr[0], pr[1]);
                if (edgeCount[{k.first, k.second}] == 1) {
                    int a = pr[0], b = pr[1];
                    int na = newIndex[a], nb = newIndex[b];
                    triangles.insert(triangles.end(), {a, b, nb});
                    triangles.insert(triangles.end(), {a, nb, na});
                }
            }
            triangles[i] = newIndex[v0]; triangles[i + 1] = newIndex[v1]; triangles[i + 2] = newIndex[v2];
        }
        name = "";
        RefreshNormals();     // keep the mesh's shading mode (flat stays flat → hard extrude edges)
    }

    /// 1->4 midpoint subdivision of ONLY the listed triangles. Midpoints are
    /// shared between the selected faces (via an edge->midpoint map) so the region
    /// stays welded; unselected triangles are left untouched.
    void SubdivideFaces(const std::vector<int>& faces) {
        AttrGuard attrGuard_(*this);
        if (faces.empty()) return;
        std::map<int, bool> sel;
        for (int f : faces) sel[f] = true;
        std::map<std::pair<int, int>, int> mids;       // edge -> new midpoint index
        auto midpoint = [&](int a, int b) {
            auto key = std::minmax(a, b);
            auto it = mids.find({key.first, key.second});
            if (it != mids.end()) return it->second;
            int idx = (int)vertices.size();
            vertices.push_back((vertices[a] + vertices[b]) * 0.5f);
            mids[{key.first, key.second}] = idx;
            return idx;
        };
        std::vector<int> out;
        for (int face = 0, n = TriangleCount(); face < n; ++face) {
            int i = face * 3;
            int a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            if (sel.find(face) == sel.end()) {          // untouched
                out.insert(out.end(), {a, b, c});
            } else {                                    // split into 4
                int ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
                out.insert(out.end(), {a, ab, ca,  ab, b, bc,  ca, bc, c,  ab, bc, ca});
            }
        }
        triangles = std::move(out);
        name = "";
        RefreshNormals();
    }

    /// Poke: fan each selected triangle from a new centre vertex (1 -> 3 tris).
    /// `height` raises the centre along the face normal — 0 keeps it flat (adds an
    /// editable centre point); positive spikes it, negative dimples it. Blender's
    /// Poke Faces. Face colors of the poked triangle carry to its three children.
    void PokeFaces(const std::vector<int>& faces, float height = 0.0f) {
        AttrGuard attrGuard_(*this);
        if (!std::isfinite(height)) return;   // a NaN or infinite amount would turn every vertex into NaN
        if (faces.empty()) return;
        std::set<int> sel(faces.begin(), faces.end());
        const bool hadColors = HasFaceColors();
        const bool hadSlots = HasSlots();
        std::vector<int> out; out.reserve(triangles.size() * 2);
        std::vector<int> outS;
        std::vector<Color> outC; if (hadColors) outC.reserve(triColors.size() * 2);
        for (int f = 0, n = TriangleCount(); f < n; ++f) {
            int i = f * 3, a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            Color fc = (hadColors && f < (int)triColors.size()) ? triColors[f] : Color{1,1,1,1};
            if (!sel.count(f)) { out.insert(out.end(), {a, b, c}); if (hadColors) outC.push_back(fc); if (hadSlots) outS.push_back(triSlot[f]); continue; }
            Vec3 ctr = (vertices[a] + vertices[b] + vertices[c]) * (1.0f / 3.0f);
            if (height != 0.0f) ctr += FaceNormal(f) * height;
            int m = (int)vertices.size(); vertices.push_back(ctr);
            out.insert(out.end(), {a, b, m,  b, c, m,  c, a, m});
            if (hadColors) { outC.push_back(fc); outC.push_back(fc); outC.push_back(fc); }
            if (hadSlots) outS.insert(outS.end(), 3, triSlot[f]);
        }
        triangles = std::move(out);
        if (hadSlots) triSlot = std::move(outS);
        if (hadColors) triColors = std::move(outC);
        name = "";
        RefreshNormals();
    }

    /// Extrude each selected face on its OWN normal as a separate protrusion
    /// (studs, greebles, spikes) — Blender's "Extrude Individual Faces". Unlike
    /// ExtrudeFaces (one connected cap along the averaged normal), each face gets
    /// its own detached cap + side walls. Vertices are duplicated per face.
    void ExtrudeFacesIndividual(const std::vector<int>& faces, float dist) {
        AttrGuard attrGuard_(*this);
        if (faces.empty() || !std::isfinite(dist) || std::fabs(dist) < 1e-8f) return;
        std::set<int> sel(faces.begin(), faces.end());
        const bool hadColors = HasFaceColors();
        std::vector<int> out; out.reserve(triangles.size() * 3);
        std::vector<Color> outC; if (hadColors) outC.reserve(triColors.size() * 3);
        for (int f = 0, n = TriangleCount(); f < n; ++f) {
            int i = f * 3, a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            Color fc = (hadColors && f < (int)triColors.size()) ? triColors[f] : Color{1,1,1,1};
            if (!sel.count(f)) { out.insert(out.end(), {a, b, c}); if (hadColors) outC.push_back(fc); continue; }
            Vec3 nrm = FaceNormal(f) * dist;
            int a2 = (int)vertices.size(); vertices.push_back(vertices[a] + nrm);
            int b2 = (int)vertices.size(); vertices.push_back(vertices[b] + nrm);
            int c2 = (int)vertices.size(); vertices.push_back(vertices[c] + nrm);
            out.insert(out.end(), {a2, b2, c2});                     // cap
            out.insert(out.end(), {a, b, b2,  a, b2, a2});           // side walls (outward)
            out.insert(out.end(), {b, c, c2,  b, c2, b2});
            out.insert(out.end(), {c, a, a2,  c, a2, c2});
            if (hadColors) for (int k = 0; k < 7; ++k) outC.push_back(fc);
        }
        triangles = std::move(out);
        if (hadColors) triColors = std::move(outC);
        name = "";
        RefreshNormals();
    }

    /// Inset each selected face independently (a smaller face inside + a border
    /// ring), per face rather than as one shared region — Blender's "Inset
    /// Individual". Pairs with ExtrudeFacesIndividual for panel/greeble looks.
    void InsetFacesIndividual(const std::vector<int>& faces, float amount) {
        AttrGuard attrGuard_(*this);
        if (faces.empty() || !std::isfinite(amount)) return;
        float t = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount);
        std::set<int> sel(faces.begin(), faces.end());
        const bool hadColors = HasFaceColors();
        std::vector<int> out; out.reserve(triangles.size() * 3);
        std::vector<Color> outC; if (hadColors) outC.reserve(triColors.size() * 3);
        for (int f = 0, n = TriangleCount(); f < n; ++f) {
            int i = f * 3, a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            Color fc = (hadColors && f < (int)triColors.size()) ? triColors[f] : Color{1,1,1,1};
            if (!sel.count(f)) { out.insert(out.end(), {a, b, c}); if (hadColors) outC.push_back(fc); continue; }
            Vec3 ctr = (vertices[a] + vertices[b] + vertices[c]) * (1.0f / 3.0f);
            int a2 = (int)vertices.size(); vertices.push_back(vertices[a] + (ctr - vertices[a]) * t);
            int b2 = (int)vertices.size(); vertices.push_back(vertices[b] + (ctr - vertices[b]) * t);
            int c2 = (int)vertices.size(); vertices.push_back(vertices[c] + (ctr - vertices[c]) * t);
            out.insert(out.end(), {a2, b2, c2});                     // inner face
            out.insert(out.end(), {a, b, b2,  a, b2, a2});           // border ring
            out.insert(out.end(), {b, c, c2,  b, c2, b2});
            out.insert(out.end(), {c, a, a2,  c, a2, c2});
            if (hadColors) for (int k = 0; k < 7; ++k) outC.push_back(fc);
        }
        triangles = std::move(out);
        if (hadColors) triColors = std::move(outC);
        name = "";
        RefreshNormals();
    }

    /// Delete the listed triangles (erasing their 3 indices each). Sorted
    /// descending so earlier erases don't shift later indices. Orphan vertices are
    /// left in place (harmless).
    void DeleteFaces(std::vector<int> faces) {
        AttrGuard attrGuard_(*this);
        const bool hadColors = HasFaceColors();
        const bool hadSlots = HasSlots();
        std::sort(faces.begin(), faces.end(), std::greater<int>());
        faces.erase(std::unique(faces.begin(), faces.end()), faces.end());   // a repeated number is one face
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            triangles.erase(triangles.begin() + i, triangles.begin() + i + 3);
            if (hadSlots) triSlot.erase(triSlot.begin() + f);
            if (hadColors && f < (int)triColors.size())   // keep per-face colors aligned
                triColors.erase(triColors.begin() + f);
        }
        name = "";
    }

    /// Reverse the winding of every triangle (swap the 2nd and 3rd index) so all
    /// faces point the other way. The shading is kept: a flat mesh (no normals, as
    /// in Edit Mesh) stays flat, and stored normals (smooth or auto smooth) turn
    /// round with the faces. (It used to always compute smooth normals, so Flip
    /// Normals in Edit Mesh rounded off a box.)
    void FlipNormals() {
        AttrGuard attrGuard_(*this);
        const bool cuv = HasCornerUVs();
        for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
            std::swap(triangles[i + 1], triangles[i + 2]);
            if (cuv) std::swap(cornerUVs[i + 1], cornerUVs[i + 2]);
        }
        name = "";
        for (Vec3& n : normals) n = n * -1.0f;
    }

    /// Reverse the winding of just the listed triangles (repeats and bad numbers are
    /// ignored). Corner UVs follow their corners; stored normals are recomputed
    /// (a flat mesh stays flat). Returns how many triangles were flipped.
    int FlipFaces(const std::vector<int>& tris) {
        AttrGuard attrGuard_(*this);
        const int nt = (int)triangles.size() / 3;
        const bool cuv = HasCornerUVs();
        std::vector<char> done((std::size_t)std::max(nt, 0), 0);
        int n = 0;
        for (int t : tris) {
            if (t < 0 || t >= nt || done[(std::size_t)t]) continue;
            done[(std::size_t)t] = 1;
            std::swap(triangles[(std::size_t)t * 3 + 1], triangles[(std::size_t)t * 3 + 2]);
            if (cuv) std::swap(cornerUVs[(std::size_t)t * 3 + 1], cornerUVs[(std::size_t)t * 3 + 2]);
            ++n;
        }
        if (n == 0) return 0;
        name = "";
        if (n == nt) for (Vec3& v : normals) v = v * -1.0f;
        else RefreshNormals();
        return n;
    }

    /// Make every face's winding coherent with its neighbors and point each closed
    /// shell OUTWARD (Blender's "Recalculate Normals Outside"). Welds, bridges,
    /// booleans, mirrored halves and imports routinely leave a few faces inside-out
    /// — they shade dark and leak shadows. This walks each connected shell flipping
    /// inconsistent triangles (two coherent neighbors traverse their shared edge in
    /// OPPOSITE directions), then flips whole shells whose signed volume is negative.
    /// Returns how many triangles were flipped. Coincident-vertex aware, so seams
    /// from Separate/Weld/hard-shading still count as connected.
    int OrientFacesOutward() {
        AttrGuard attrGuard_(*this);
        const int nt = (int)triangles.size() / 3;
        if (nt == 0) return 0;
        std::vector<int> rep = CoincidentReps();
        // Zero-area triangles (collapsed pole fans etc.) have no orientation of
        // their own and corrupt the winding walk — leave them out entirely.
        std::vector<char> degen(nt, 0);
        for (int t = 0; t < nt; ++t) {
            const Vec3& a = vertices[triangles[t * 3]];
            Vec3 n = Vec3::Cross(vertices[triangles[t * 3 + 1]] - a,
                                 vertices[triangles[t * 3 + 2]] - a);
            if (n.SqrMagnitude() < 1e-16f) degen[t] = 1;
        }
        // Directed edge -> owning tri list (a tri owns edges a->b, b->c, c->a).
        std::map<std::pair<int, int>, std::vector<int>> edgeTris;   // undirected key
        for (int t = 0; t < nt; ++t) {
            if (degen[t]) continue;
            for (int k = 0; k < 3; ++k) {
                int a = rep[triangles[t * 3 + k]], b = rep[triangles[t * 3 + (k + 1) % 3]];
                if (a == b) continue;
                edgeTris[{a < b ? a : b, a < b ? b : a}].push_back(t);
            }
        }
        // Directed edge of tri t at slot k AFTER any flip recorded in `flip`.
        auto dirEdge = [&](int t, int k, bool flipped, int& a, int& b) {
            int i0 = rep[triangles[t * 3 + k]], i1 = rep[triangles[t * 3 + (k + 1) % 3]];
            if (flipped) { a = i1; b = i0; } else { a = i0; b = i1; }
        };
        std::vector<char> flip(nt, 0), seen(nt, 0);
        std::vector<int> shell; shell.reserve(64);
        int flipped = 0;
        for (int seed = 0; seed < nt; ++seed) {
            if (seen[seed] || degen[seed]) continue;
            shell.clear();
            std::vector<int> stack{seed};
            seen[seed] = 1;
            while (!stack.empty()) {                    // BFS: propagate coherent winding
                int t = stack.back(); stack.pop_back();
                shell.push_back(t);
                for (int k = 0; k < 3; ++k) {
                    int a, b; dirEdge(t, k, flip[t] != 0, a, b);
                    if (a == b) continue;
                    auto it = edgeTris.find({a < b ? a : b, a < b ? b : a});
                    if (it == edgeTris.end()) continue;
                    for (int n : it->second) {
                        if (n == t || seen[n]) continue;
                        // Find the shared edge's direction in the neighbor: coherent
                        // neighbors traverse it OPPOSITE to (a -> b).
                        for (int j = 0; j < 3; ++j) {
                            int na, nb; dirEdge(n, j, false, na, nb);
                            if (na == a && nb == b) { flip[n] = 1; break; }   // same dir = wrong
                            if (na == b && nb == a) { flip[n] = 0; break; }
                        }
                        seen[n] = 1;
                        stack.push_back(n);
                    }
                }
            }
            // Outward test: the shell's signed volume (divergence theorem). Negative
            // = the coherent winding faces inward, so flip the whole shell.
            double vol = 0.0;
            for (int t : shell) {
                Vec3 va = vertices[triangles[t * 3]], vb = vertices[triangles[t * 3 + 1]],
                     vc = vertices[triangles[t * 3 + 2]];
                if (flip[t]) std::swap(vb, vc);
                vol += (double)Vec3::Dot(va, Vec3::Cross(vb, vc));
            }
            if (vol < 0.0) for (int t : shell) flip[t] = !flip[t];
        }
        const bool cuv = HasCornerUVs();
        for (int t = 0; t < nt; ++t)
            if (flip[t]) {
                std::swap(triangles[t * 3 + 1], triangles[t * 3 + 2]);
                if (cuv) std::swap(cornerUVs[t * 3 + 1], cornerUVs[t * 3 + 2]);
                ++flipped;
            }
        if (flipped) { name = ""; if (HasNormals()) ComputeSmoothNormals(); }
        return flipped;
    }


    /// The sculpt and paint brush falloff: 1 at the centre, 0 at `radius` and beyond,
    /// smoothstep of 1 - dist / radius in between (s(x) = x * x * (3 - 2x)).
    static float BrushFalloff(float dist, float radius) {
        if (!(radius > 0.0f) || !(dist < radius)) return 0.0f;
        const float w = 1.0f - dist / radius;
        return w * w * (3.0f - 2.0f * w);
    }

    /// A sculpting brush in LOCAL mesh space. Vertices within `radius` of `center`
    /// are displaced with the BrushFalloff weight `w`. mode: 0 = GRAB (push along
    /// `dir` by strength * w), 1 = INFLATE (push along each vertex's own normal),
    /// 2 = SMOOTH (pull each vertex toward the average of the vertices it shares an
    /// edge with, a Laplacian step), 3 = FLATTEN (press the region onto its own
    /// average plane), 4 = PINCH (draw vertices toward the brush centre, sharpening
    /// creases). SMOOTH, FLATTEN and PINCH move at most all the way (no overshoot).
    /// Every mode reads one snapshot, so the stroke doesn't feed back on itself.
    /// Vertices at the same position (UV seam and pole copies) move as one.
    /// `symmetryX` adds the mirrored stroke across x = 0 (centre and dir mirrored):
    /// each vertex takes the stroke on its own side of the plane and vertices on the
    /// plane take the average of the two, so a mirror-symmetric mesh stays
    /// mirror-symmetric and nothing is moved twice.
    void SculptBrush(const Vec3& center, const Vec3& dir, float radius, float strength, int mode, bool symmetryX = false) {
        if (!(radius > 1e-6f) || vertices.empty()) return;
        if (!std::isfinite(radius + strength + center.x + center.y + center.z + dir.x + dir.y + dir.z)) return;   // a typed NaN or infinite value would turn every vertex into NaN
        const std::size_t n = vertices.size();
        const std::vector<Vec3> snap = vertices;
        // Copies of one point (split for UVs or at a pole) are one group and move together.
        // Copies made with sin / cos can differ in the last bits, so points closer than a
        // tiny tolerance (1e-5 of the mesh size) count as one: a grid of cells that size,
        // each point checked against the groups in its own and the 26 cells around it.
        std::vector<int> group(n);
        std::vector<Vec3> gpos;
        int groups = 0;
        {
            Vec3 lo = snap[0], hi = snap[0];
            for (const Vec3& p : snap) {
                lo.x = std::fmin(lo.x, p.x); lo.y = std::fmin(lo.y, p.y); lo.z = std::fmin(lo.z, p.z);
                hi.x = std::fmax(hi.x, p.x); hi.y = std::fmax(hi.y, p.y); hi.z = std::fmax(hi.z, p.z);
            }
            const float extent = std::fmax(std::fmax(hi.x - lo.x, hi.y - lo.y), std::fmax(hi.z - lo.z, 1e-3f));
            const float tol = extent * 1e-5f, inv = 1.0f / tol;
            if (!std::isfinite(extent)) return;   // a NaN or infinite vertex: leave the mesh alone
            using Cell = std::tuple<long long, long long, long long>;
            std::map<Cell, std::vector<int>> cells;   // cell -> groups whose first point is in it
            for (std::size_t i = 0; i < n; ++i) {
                const Vec3 p = snap[i];
                const long long cx = (long long)std::floor((p.x - lo.x) * inv), cy = (long long)std::floor((p.y - lo.y) * inv),
                                cz = (long long)std::floor((p.z - lo.z) * inv);
                int found = -1;
                for (long long dx = -1; dx <= 1 && found < 0; ++dx)
                    for (long long dy = -1; dy <= 1 && found < 0; ++dy)
                        for (long long dz = -1; dz <= 1 && found < 0; ++dz) {
                            auto it = cells.find(Cell{cx + dx, cy + dy, cz + dz});
                            if (it == cells.end()) continue;
                            for (int g : it->second)
                                if ((gpos[(std::size_t)g] - p).Magnitude() <= tol) { found = g; break; }
                        }
                if (found < 0) { found = groups++; gpos.push_back(p); cells[Cell{cx, cy, cz}].push_back(found); }
                group[i] = found;
            }
        }
        // Area-weighted normal of each group (INFLATE, FLATTEN).
        std::vector<Vec3> gnorm;
        if (mode == 1 || mode == 3) {
            gnorm.assign((std::size_t)groups, Vec3{0, 0, 0});
            for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
                const int a = triangles[t], b = triangles[t + 1], c = triangles[t + 2];
                if (a < 0 || b < 0 || c < 0 || a >= (int)n || b >= (int)n || c >= (int)n) continue;
                const Vec3 face = Vec3::Cross(snap[b] - snap[a], snap[c] - snap[a]);
                gnorm[(std::size_t)group[a]] += face; gnorm[(std::size_t)group[b]] += face; gnorm[(std::size_t)group[c]] += face;
            }
            for (Vec3& v : gnorm) if (v.SqrMagnitude() > 1e-20f) v = v.Normalized();
        }
        // Average neighbour position of each group (SMOOTH).
        std::vector<Vec3> gavg;
        if (mode == 2) {
            std::vector<std::vector<int>> nb((std::size_t)groups);
            for (std::size_t t = 0; t + 2 < triangles.size(); t += 3)
                for (int k = 0; k < 3; ++k) {
                    const int a = triangles[t + k], b = triangles[t + (k + 1) % 3];
                    if (a < 0 || b < 0 || a >= (int)n || b >= (int)n) continue;
                    const int ga = group[a], gb = group[b];
                    if (ga == gb) continue;
                    nb[(std::size_t)ga].push_back(gb); nb[(std::size_t)gb].push_back(ga);
                }
            gavg = gpos;
            for (int g = 0; g < groups; ++g) {
                std::vector<int>& l = nb[(std::size_t)g];
                std::sort(l.begin(), l.end());
                l.erase(std::unique(l.begin(), l.end()), l.end());
                if (l.empty()) continue;
                Vec3 sum{0, 0, 0};
                for (int h : l) sum += gpos[(std::size_t)h];
                gavg[(std::size_t)g] = sum * (1.0f / (float)l.size());
            }
        }
        // One stroke's displacement of every group.
        auto stroke = [&](const Vec3& c, const Vec3& d, std::vector<Vec3>& disp) {
            disp.assign((std::size_t)groups, Vec3{0, 0, 0});
            Vec3 planeC{0, 0, 0}, planeN{0, 1, 0};
            if (mode == 3) {   // the best-fit plane of the region: its centroid and averaged normal
                Vec3 nsum{0, 0, 0}; int cnt = 0;
                for (int g = 0; g < groups; ++g)
                    if ((gpos[(std::size_t)g] - c).Magnitude() < radius) { planeC += gpos[(std::size_t)g]; nsum += gnorm[(std::size_t)g]; ++cnt; }
                if (cnt == 0) return;
                planeC = planeC * (1.0f / (float)cnt);
                const float m = nsum.Magnitude();
                if (m > 1e-6f) planeN = nsum * (1.0f / m);
            }
            for (int g = 0; g < groups; ++g) {
                const Vec3 p = gpos[(std::size_t)g];
                const float w = BrushFalloff((p - c).Magnitude(), radius);
                if (w <= 0.0f) continue;
                const float k = std::fmin(strength * w, 1.0f);
                Vec3& out = disp[(std::size_t)g];
                if (mode == 1) out = gnorm[(std::size_t)g] * (strength * w);                    // INFLATE
                else if (mode == 2) out = (gavg[(std::size_t)g] - p) * k;                       // SMOOTH
                else if (mode == 3) out = planeN * (-Vec3::Dot(p - planeC, planeN) * k);       // FLATTEN
                else if (mode == 4) out = (c - p) * k;                                          // PINCH
                else out = d * (strength * w);                                                  // GRAB
            }
        };
        std::vector<Vec3> dispA;
        stroke(center, dir, dispA);
        if (!symmetryX) {
            for (std::size_t i = 0; i < n; ++i) vertices[i] = snap[i] + dispA[(std::size_t)group[i]];
        } else {
            std::vector<Vec3> dispB;
            stroke(Vec3{-center.x, center.y, center.z}, Vec3{-dir.x, dir.y, dir.z}, dispB);
            const bool aIsPlus = center.x >= 0.0f;   // stroke A works on the side its centre is on
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t g = (std::size_t)group[i];
                const float x = snap[i].x;
                Vec3 dv;
                if (x == 0.0f) dv = (dispA[g] + dispB[g]) * 0.5f;
                else dv = ((x > 0.0f) == aIsPlus) ? dispA[g] : dispB[g];
                vertices[i] = snap[i] + dv;
            }
        }
        name = "";
        ComputeSmoothNormals();
    }

    // ---- Modifiers (Blender-style) ------------------------------------------

    /// Array modifier: keep the original and append `count-1` more copies, each
    /// shifted a further `offset` along. Great for fences, columns, stairs, gears.
    void Array(int count, const Vec3& offset) {
        AttrGuard attrGuard_(*this);
        if (count <= 1 || vertices.empty() || !std::isfinite(offset.x + offset.y + offset.z)) return;   // a NaN or infinite offset would turn the copies into NaN
        Mesh base = *this;
        for (int i = 1; i < count; ++i)
            Add(base, {offset.x * (float)i, offset.y * (float)i, offset.z * (float)i});
        name = "";
        ComputeSmoothNormals();
    }

    /// Decimate modifier (vertex clustering): snap vertices to a grid of `cellSize`
    /// and collapse each cell to one averaged vertex, dropping faces that degenerate.
    /// A fast, robust way to cut triangle count on dense/imported meshes. Returns the
    /// new triangle count.
    int Decimate(float cellSize) {
        AttrGuard attrGuard_(*this);
        if (!(cellSize > 1e-6f) || vertices.empty()) return TriangleCount();
        std::map<std::tuple<int, int, int>, int> cell;
        std::vector<Vec3> sum; std::vector<int> cnt; std::vector<int> remap(vertices.size());
        auto key = [&](const Vec3& v) {
            return std::make_tuple((int)std::floor(v.x / cellSize),
                                   (int)std::floor(v.y / cellSize),
                                   (int)std::floor(v.z / cellSize));
        };
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            auto k = key(vertices[i]);
            auto it = cell.find(k);
            int id;
            if (it == cell.end()) { id = (int)sum.size(); cell[k] = id; sum.push_back(vertices[i]); cnt.push_back(1); }
            else { id = it->second; sum[id] += vertices[i]; ++cnt[id]; }
            remap[i] = id;
        }
        std::vector<Vec3> nv(sum.size());
        for (std::size_t i = 0; i < sum.size(); ++i) nv[i] = sum[i] * (1.0f / (float)cnt[i]);
        std::vector<int> nt; nt.reserve(triangles.size());
        std::vector<Color> nc; const bool hadColors = HasFaceColors();
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int a = remap[triangles[t]], b = remap[triangles[t + 1]], c = remap[triangles[t + 2]];
            if (a == b || b == c || a == c) continue;            // collapsed → drop
            nt.push_back(a); nt.push_back(b); nt.push_back(c);
            if (hadColors) nc.push_back(triColors[t / 3]);
        }
        vertices = std::move(nv); triangles = std::move(nt);
        triColors = std::move(nc); uvs.clear();
        name = "";
        ComputeSmoothNormals();
        return TriangleCount();
    }

    /// True if `p` is inside this CLOSED mesh — parity of how many faces a ray from
    /// `p` crosses (odd = inside). The basis for the boolean/remesh ops. The ray is
    /// slightly skewed off the axes so it never threads an exact shared edge/vertex
    /// (which would double-count and flip the answer).
    bool PointInside(const Vec3& p) const {
        const Vec3 d{1.0f, 0.0009124f, 0.0007215f};              // near +X, off every edge
        auto dot = [](const Vec3& u, const Vec3& w) { return u.x * w.x + u.y * w.y + u.z * w.z; };
        int crossings = 0;
        for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
            const Vec3& a = vertices[triangles[i]];
            const Vec3& b = vertices[triangles[i + 1]];
            const Vec3& c = vertices[triangles[i + 2]];
            Vec3 e1 = b - a, e2 = c - a;
            Vec3 h = Vec3::Cross(d, e2);
            float det = dot(e1, h);
            if (det > -1e-9f && det < 1e-9f) continue;           // ray parallel to face
            float invDet = 1.0f / det;
            Vec3 s = p - a;
            float u = dot(s, h) * invDet;
            if (u < 0.0f || u > 1.0f) continue;
            Vec3 q = Vec3::Cross(s, e1);
            float v = dot(d, q) * invDet;
            if (v < 0.0f || u + v > 1.0f) continue;
            float t = dot(e2, q) * invDet;
            if (t > 1e-7f) ++crossings;                          // crossing ahead along the ray
        }
        return (crossings & 1) != 0;
    }

    enum class BoolOp { Union, Difference, Intersect };

    /// Remesh modifier ("Blocks" mode): sample the solid on a voxel grid and emit a
    /// watertight blocky surface between solid and empty cells. Follow with Smooth()
    /// or SubdivideSmooth() to round it. `voxel` is the cell size; the grid is capped
    /// at `maxDim` cells per axis so a tiny voxel on a big mesh can't explode.
    static Mesh VoxelRemesh(const Mesh& src, float voxel, int maxDim = 64) {
        if (src.vertices.empty() || voxel <= 1e-5f) return Mesh{};
        Vec3 lo, hi; src.Bounds(lo, hi);
        lo = lo - Vec3{voxel, voxel, voxel};
        hi = hi + Vec3{voxel, voxel, voxel};
        int nx, ny, nz; float v;
        GridDims(lo, hi, voxel, maxDim, nx, ny, nz, v);
        std::vector<unsigned char> occ((std::size_t)nx * ny * nz, 0);
        SampleInto(src, occ, nx, ny, nz, lo, v);
        return EmitOccupancy(occ, nx, ny, nz, lo, v);
    }
    /// Replace this mesh with its voxel remesh in place.
    void Remesh(float voxel, int maxDim = 64) { *this = VoxelRemesh(*this, voxel, maxDim); }

    /// Boolean modifier: combine two closed meshes with union (A∪B), difference
    /// (A−B), or intersect (A∩B), via a shared voxel grid. Robust on any closed
    /// input (no fragile coplanar-triangle clipping); the result is blocky at the
    /// voxel scale, so pick `voxel` small for crisp seams and Smooth() to taste.
    static Mesh Boolean(const Mesh& a, const Mesh& b, BoolOp op, float voxel, int maxDim = 96) {
        if (a.vertices.empty() || b.vertices.empty() || voxel <= 1e-5f) return Mesh{};
        Vec3 alo, ahi, blo, bhi; a.Bounds(alo, ahi); b.Bounds(blo, bhi);
        // Union of both AABBs (covers every cell either solid could occupy), padded.
        Vec3 lo{std::min(alo.x, blo.x), std::min(alo.y, blo.y), std::min(alo.z, blo.z)};
        Vec3 hi{std::max(ahi.x, bhi.x), std::max(ahi.y, bhi.y), std::max(ahi.z, bhi.z)};
        lo = lo - Vec3{voxel, voxel, voxel};
        hi = hi + Vec3{voxel, voxel, voxel};
        int nx, ny, nz; float v;
        GridDims(lo, hi, voxel, maxDim, nx, ny, nz, v);
        const std::size_t n = (std::size_t)nx * ny * nz;
        std::vector<unsigned char> oa(n, 0), ob(n, 0);
        SampleInto(a, oa, nx, ny, nz, lo, v);
        SampleInto(b, ob, nx, ny, nz, lo, v);
        std::vector<unsigned char> occ(n, 0);
        for (std::size_t i = 0; i < n; ++i) {
            bool ia = oa[i] != 0, ib = ob[i] != 0;
            occ[i] = (op == BoolOp::Union ? (ia || ib)
                    : op == BoolOp::Intersect ? (ia && ib)
                    : (ia && !ib)) ? 1 : 0;                      // Difference
        }
        return EmitOccupancy(occ, nx, ny, nz, lo, v);
    }

    /// Convex Hull: the tight convex shell around a set of points (incremental
    /// algorithm). Great for turning a scan/blockout into a clean collider or a
    /// crystal/rock silhouette. Returns an empty mesh if the points are degenerate
    /// (fewer than 4, or all coplanar).
    static Mesh ConvexHull(const std::vector<Vec3>& ptsIn) {
        Mesh out;
        // De-duplicate coincident points.
        std::vector<Vec3> p;
        p.reserve(ptsIn.size());
        for (const Vec3& q : ptsIn) {
            bool dup = false;
            for (const Vec3& e : p) if ((e - q).Magnitude() < 1e-6f) { dup = true; break; }
            if (!dup) p.push_back(q);
        }
        if (p.size() < 4) return out;
        auto dot = [](const Vec3& u, const Vec3& w) { return u.x * w.x + u.y * w.y + u.z * w.z; };

        // Seed tetrahedron: spread-out, non-degenerate 4 points.
        int i0 = 0, i1 = 1;
        float best = -1.0f;
        for (std::size_t i = 0; i < p.size(); ++i)
            for (std::size_t j = i + 1; j < p.size(); ++j) {
                float d = (p[i] - p[j]).Magnitude();
                if (d > best) { best = d; i0 = (int)i; i1 = (int)j; }
            }
        int i2 = -1; best = -1.0f;
        for (std::size_t i = 0; i < p.size(); ++i) {
            Vec3 c = Vec3::Cross(p[i1] - p[i0], p[i] - p[i0]);
            float d = c.Magnitude();
            if (d > best) { best = d; i2 = (int)i; }
        }
        if (i2 < 0 || best < 1e-9f) return out;                 // all collinear
        Vec3 nrm = Vec3::Cross(p[i1] - p[i0], p[i2] - p[i0]);
        int i3 = -1; best = 1e-9f;
        for (std::size_t i = 0; i < p.size(); ++i) {
            float d = std::fabs(dot(nrm, p[i] - p[i0]));
            if (d > best) { best = d; i3 = (int)i; }
        }
        if (i3 < 0) return out;                                  // all coplanar

        Vec3 interior = (p[i0] + p[i1] + p[i2] + p[i3]) * 0.25f; // always inside the hull
        struct Face { int a, b, c; };
        std::vector<Face> faces;
        auto faceNormal = [&](const Face& f) { return Vec3::Cross(p[f.b] - p[f.a], p[f.c] - p[f.a]); };
        auto addFace = [&](int a, int b, int c) {
            Face f{a, b, c};
            // Wind so the normal points away from the interior point.
            if (dot(faceNormal(f), p[a] - interior) < 0.0f) std::swap(f.b, f.c);
            faces.push_back(f);
        };
        addFace(i0, i1, i2); addFace(i0, i1, i3); addFace(i0, i2, i3); addFace(i1, i2, i3);

        for (int q = 0; q < (int)p.size(); ++q) {
            if (q == i0 || q == i1 || q == i2 || q == i3) continue;
            // Faces the new point can "see" (it's outside their plane).
            std::vector<char> vis(faces.size(), 0);
            bool any = false;
            for (std::size_t f = 0; f < faces.size(); ++f) {
                Vec3 n = faceNormal(faces[f]);
                if (dot(n, p[q] - p[faces[f].a]) > 1e-9f) { vis[f] = 1; any = true; }
            }
            if (!any) continue;                                 // inside the current hull
            // Horizon = directed edges of visible faces whose reverse isn't also visible.
            std::map<std::pair<int, int>, int> dir;             // directed edge -> count among visible
            auto add = [&](int a, int b) { dir[{a, b}]++; };
            for (std::size_t f = 0; f < faces.size(); ++f)
                if (vis[f]) { add(faces[f].a, faces[f].b); add(faces[f].b, faces[f].c); add(faces[f].c, faces[f].a); }
            std::vector<std::pair<int, int>> horizon;
            for (auto& kv : dir)
                if (dir.find({kv.first.second, kv.first.first}) == dir.end())
                    horizon.push_back(kv.first);
            // Drop visible faces, add a fan from the horizon to q.
            std::vector<Face> keep;
            for (std::size_t f = 0; f < faces.size(); ++f) if (!vis[f]) keep.push_back(faces[f]);
            faces.swap(keep);
            for (auto& e : horizon) addFace(e.first, e.second, q);
        }

        // Emit unique vertices used by the hull faces.
        std::map<int, int> remap;
        for (const Face& f : faces)
            for (int idx : {f.a, f.b, f.c})
                if (remap.find(idx) == remap.end()) { remap[idx] = (int)out.vertices.size(); out.vertices.push_back(p[idx]); }
        for (const Face& f : faces) {
            out.triangles.push_back(remap[f.a]); out.triangles.push_back(remap[f.b]); out.triangles.push_back(remap[f.c]);
        }
        out.ComputeSmoothNormals();
        return out;
    }
    /// Replace this mesh with the convex hull of its own vertices.
    void MakeConvexHull() { *this = ConvexHull(vertices); }

    /// Bisect: cut the mesh with a plane (point `planeP`, normal `planeN`) and keep
    /// the half on the normal's side (flip `keepPositive` for the other). With `cap`,
    /// the exposed cross-section is filled so the result stays a closed solid. This is
    /// Blender's Bisect tool — slice a model cleanly in half, lop off a top, etc.
    void Bisect(const Vec3& planeP, const Vec3& planeN, bool cap = true, bool keepPositive = true) {
        Vec3 N = planeN; float nl = N.Magnitude();
        if (nl < 1e-9f) return;
        N = N * (1.0f / nl);
        const float sign = keepPositive ? 1.0f : -1.0f;
        auto sd = [&](const Vec3& v) { return sign * ((v.x - planeP.x) * N.x + (v.y - planeP.y) * N.y + (v.z - planeP.z) * N.z); };
        std::vector<Vec3> nv; std::vector<int> nt;
        std::vector<Vec3> cut;                                   // intersection points (for the cap)
        auto emit = [&](const Vec3& a, const Vec3& b, const Vec3& c) {
            int base = (int)nv.size(); nv.push_back(a); nv.push_back(b); nv.push_back(c);
            nt.push_back(base); nt.push_back(base + 1); nt.push_back(base + 2);
        };
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            Vec3 v[3] = {vertices[triangles[t]], vertices[triangles[t + 1]], vertices[triangles[t + 2]]};
            float d[3] = {sd(v[0]), sd(v[1]), sd(v[2])};
            // Clip the triangle to the kept half-space (Sutherland–Hodgman).
            std::vector<Vec3> poly;
            for (int i = 0; i < 3; ++i) {
                int j = (i + 1) % 3;
                if (d[i] >= 0.0f) poly.push_back(v[i]);
                if ((d[i] >= 0.0f) != (d[j] >= 0.0f)) {          // edge crosses the plane
                    float tt = d[i] / (d[i] - d[j]);
                    Vec3 x = v[i] + (v[j] - v[i]) * tt;
                    poly.push_back(x);
                    cut.push_back(x);
                }
            }
            for (std::size_t k = 1; k + 1 < poly.size(); ++k) emit(poly[0], poly[k], poly[k + 1]);
        }
        // Cap the cross-section: fan-triangulate the intersection ring in the plane.
        if (cap && cut.size() >= 3) {
            Vec3 ctr{0, 0, 0}; for (const Vec3& c : cut) ctr += c; ctr = ctr * (1.0f / (float)cut.size());
            // Plane basis for angular sort.
            Vec3 up = std::fabs(N.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
            Vec3 e0 = Vec3::Cross(up, N); e0 = e0 * (1.0f / std::max(1e-6f, e0.Magnitude()));
            Vec3 e1 = Vec3::Cross(N, e0);
            auto ang = [&](const Vec3& c) { Vec3 dd = c - ctr; return std::atan2(dd.x * e1.x + dd.y * e1.y + dd.z * e1.z,
                                                                                 dd.x * e0.x + dd.y * e0.y + dd.z * e0.z); };
            std::sort(cut.begin(), cut.end(), [&](const Vec3& a, const Vec3& b) { return ang(a) < ang(b); });
            for (std::size_t k = 0; k < cut.size(); ++k) {
                const Vec3& a = cut[k]; const Vec3& b = cut[(k + 1) % cut.size()];
                // Wind the cap so its normal faces out of the kept half (-N side is the new surface).
                Vec3 fn = Vec3::Cross(a - ctr, b - ctr);
                int base = (int)nv.size();
                if ((fn.x * N.x + fn.y * N.y + fn.z * N.z) * sign > 0.0f) { nv.push_back(ctr); nv.push_back(b); nv.push_back(a); }
                else                                                       { nv.push_back(ctr); nv.push_back(a); nv.push_back(b); }
                nt.push_back(base); nt.push_back(base + 1); nt.push_back(base + 2);
            }
        }
        vertices = std::move(nv); triangles = std::move(nt);
        triColors.clear(); uvs.clear();
        name = "";
        WeldVertices();
        ComputeSmoothNormals();
    }

    /// Shrink/Fatten (Blender's Alt-S): push every vertex along its surface normal by
    /// `dist` — positive inflates, negative deflates. Normals are oriented outward
    /// (away from the mesh centre) so it inflates regardless of triangle winding.
    void ShrinkFatten(float dist) {
        if (vertices.empty() || !std::isfinite(dist)) return;   // a typed NaN or infinite value would turn every vertex into NaN
        ComputeSmoothNormals();
        Vec3 ctr{0, 0, 0};
        for (const Vec3& v : vertices) ctr += v;
        ctr = ctr * (1.0f / (float)vertices.size());
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            Vec3 n = normals[i];
            Vec3 out = vertices[i] - ctr;
            if (n.x * out.x + n.y * out.y + n.z * out.z < 0.0f) n = n * -1.0f;  // face outward
            vertices[i] += n * dist;
        }
        name = "";
        ComputeSmoothNormals();
    }

    /// Wireframe modifier: replace every edge with a solid beam of square cross-
    /// section (`thickness` across), turning the mesh into its wire lattice — handy
    /// for cages, scaffolds, and stylised low-poly props.
    void Wireframe(float thickness) {
        if (triangles.empty() || !(thickness > 1e-6f) || !std::isfinite(thickness)) return;   // a typed NaN or infinite value would turn every vertex into NaN
        Mesh w = *this; w.WeldVertices();
        std::set<std::pair<int, int>> edges;
        for (std::size_t t = 0; t + 2 < w.triangles.size(); t += 3) {
            int a = w.triangles[t], b = w.triangles[t + 1], c = w.triangles[t + 2];
            auto add = [&](int i, int j) { edges.insert({std::min(i, j), std::max(i, j)}); };
            add(a, b); add(b, c); add(c, a);
        }
        Mesh out;
        float r = thickness * 0.5f;
        for (const auto& e : edges) out.AddBeam(w.vertices[e.first], w.vertices[e.second], r);
        *this = std::move(out);
        name = "";
        ComputeSmoothNormals();
    }

    /// Displace modifier: perturb every vertex along its outward normal by value
    /// noise sampled at its position — instant organic bumpiness (rocks, terrain,
    /// bark). `frequency` sets the noise scale, `seed` picks the pattern; both are
    /// deterministic. Subdivide first for finer detail.
    void Displace(float amount, float frequency = 1.0f, int seed = 0) {
        if (!std::isfinite(amount + frequency)) return;   // a NaN or infinite amount would turn every vertex into NaN
        amount = std::clamp(amount, -1e6f, 1e6f); frequency = std::clamp(frequency, -1e4f, 1e4f);   // (and absurd ones overflow the noise)
        if (vertices.empty()) return;
        ComputeSmoothNormals();
        Vec3 ctr{0, 0, 0};
        for (const Vec3& v : vertices) ctr += v;
        ctr = ctr * (1.0f / (float)vertices.size());
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            Vec3 n = normals[i], out = vertices[i] - ctr;
            if (n.x * out.x + n.y * out.y + n.z * out.z < 0.0f) n = n * -1.0f;
            float d = ValueNoise(vertices[i] * frequency, seed);
            vertices[i] += n * (d * amount);
        }
        name = "";
        ComputeSmoothNormals();
    }

    /// Cast modifier (to a cylinder): pull each vertex's radius (perpendicular to
    /// `axis`: 0=X,1=Y,2=Z) toward `radius`, by `amount` (0 = none, 1 = full). Rounds
    /// a boxy shape into a barrel/tube. Cast-to-sphere already exists as Spherify().
    void CastToCylinder(float radius, int axis = 1, float amount = 1.0f) {
        if (vertices.empty() || !std::isfinite(radius) || !std::isfinite(amount)) return;   // a typed NaN or infinite value would turn every vertex into NaN
        for (Vec3& v : vertices) {
            float a, b;
            if (axis == 0)      { a = v.y; b = v.z; }
            else if (axis == 2) { a = v.x; b = v.y; }
            else                { a = v.x; b = v.z; }
            float r = std::sqrt(a * a + b * b);
            if (r < 1e-6f) continue;
            float f = 1.0f + (radius / r - 1.0f) * amount;   // lerp(1, radius/r, amount)
            a *= f; b *= f;
            if (axis == 0)      { v.y = a; v.z = b; }
            else if (axis == 2) { v.x = a; v.y = b; }
            else                { v.x = a; v.z = b; }
        }
        name = "";
        ComputeSmoothNormals();
    }

    /// Stretch/squash along one axis (0=X,1=Y,2=Z) about the mesh centre. factor > 1
    /// stretches, < 1 squashes, < 0 mirrors. A cheap Simple-Deform stretch.
    void Stretch(int axis, float factor) {
        if (!std::isfinite(factor)) return;   // a NaN or infinite amount would turn every vertex into NaN
        if (vertices.empty() || axis < 0 || axis > 2) return;
        float c = 0.0f;
        for (const Vec3& v : vertices) c += (&v.x)[axis];
        c /= (float)vertices.size();
        for (Vec3& v : vertices) (&v.x)[axis] = c + ((&v.x)[axis] - c) * factor;
        name = "";
        ComputeSmoothNormals();
    }

    /// Screw modifier: revolve a profile (x = radius, y = height) around Y, sweeping
    /// `turns` full turns over `segments` steps and rising `pitch` per turn — a helix
    /// (springs, screw threads, spiral ramps). With turns = 1 and pitch = 0 it's a
    /// closed lathe.
    static Mesh Screw(const std::vector<Vec2>& profile, float turns = 1.0f, float pitch = 0.0f,
                      int segments = 32);

    /// Sweep a circular cross-section along a 3D polyline — pipes, rails, cables,
    /// tree branches. Frames are parallel-transported so the tube doesn't twist at
    /// bends. `caps` closes the ends with fans. `radiusEnd` >= 0 tapers the tube
    /// linearly from `radius` at the start to `radiusEnd` at the end (trunks, tails).
    static Mesh SweepPath(const std::vector<Vec3>& path, float radius = 0.25f,
                          int segments = 12, bool caps = true, float radiusEnd = -1.0f);

    /// Cap every hole: boundary edges (edges used by only one triangle) are chained
    /// into loops and each loop is fan-filled from its centroid. Coincident vertices
    /// are treated as one so unwelded seams don't read as boundaries. Returns the
    /// number of holes closed.
    int FillHoles() {
        AttrGuard attrGuard_(*this);
        if (triangles.size() < 3 || vertices.empty()) return 0;
        // Representative index per coincident-vertex group.
        std::map<std::tuple<int, int, int>, int> grid;
        std::vector<int> rep(vertices.size());
        auto key = [](const Vec3& v) {
            return std::make_tuple((int)std::lround(v.x * 4096.0f),
                                   (int)std::lround(v.y * 4096.0f),
                                   (int)std::lround(v.z * 4096.0f));
        };
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            auto k = key(vertices[i]);
            auto it = grid.find(k);
            if (it == grid.end()) { grid[k] = (int)i; rep[i] = (int)i; }
            else rep[i] = it->second;
        }
        std::set<std::pair<int, int>> edges;            // directed, on representatives
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int a = rep[triangles[t]], b = rep[triangles[t + 1]], c = rep[triangles[t + 2]];
            if (a == b || b == c || a == c) continue;
            edges.insert({a, b}); edges.insert({b, c}); edges.insert({c, a});
        }
        // Boundary edges: no reverse partner. A hole rim chains head-to-tail.
        std::map<int, int> next;
        for (const auto& e : edges)
            if (!edges.count({e.second, e.first}) && !next.count(e.first))
                next[e.first] = e.second;
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        const bool hadFC = HasFaceColors(), hadSlots = HasSlots();
        int holes = 0;
        std::set<int> used;
        for (const auto& start : next) {
            if (used.count(start.first)) continue;
            std::vector<int> loop;                      // walk the rim
            int v = start.first;
            while (next.count(v) && !used.count(v)) {
                used.insert(v); loop.push_back(v);
                v = next[v];
            }
            if (loop.size() < 3 || v != start.first) continue;   // open chain — not a loop
            Vec3 c{0, 0, 0};
            for (int i : loop) c += vertices[i];
            c = c * (1.0f / (float)loop.size());
            int ci = (int)vertices.size();
            vertices.push_back(c);
            if (hadUV) uvs.push_back({0.5f, 0.5f});
            // Rim edges wind with the surrounding faces; (b, a, centre) faces outward.
            for (std::size_t i = 0; i < loop.size(); ++i) {
                int a = loop[i], b = loop[(i + 1) % loop.size()];
                triangles.insert(triangles.end(), {b, a, ci});
            }
            ++holes;
        }
        if (holes > 0) {
            // The caps are new faces: they get white and slot 0, and every old face keeps its
            // paint and slot (audit: filling a hole used to wipe the whole mesh's face colours).
            if (hadFC) triColors.resize((std::size_t)TriangleCount(), Color{1, 1, 1, 1}); else triColors.clear();
            if (hadSlots) triSlot.resize((std::size_t)TriangleCount(), 0);
            name = "";
            RefreshNormals();
        }
        return holes;
    }

    /// What Repair() found and fixed.
    struct RepairReport {
        int mergedVerts = 0, holesFilled = 0, flippedFaces = 0;
        int openEdgesBefore = 0, openEdgesAfter = 0;
        int nonManifoldBefore = 0, nonManifoldAfter = 0;
        bool ClosedAfter() const { return openEdgesAfter == 0 && nonManifoldAfter == 0; }
    };
    /// One-click clean-up for a broken or imported mesh: merge vertices closer than `weldDist`
    /// (which also drops collapsed triangles), cap open holes (optional), then make every face
    /// point the same way, outward on closed shells. Painted colours and material slots stay.
    RepairReport Repair(float weldDist = 1e-5f, bool fillHoles = true) {
        AttrGuard attrGuard_(*this);
        RepairReport r;
        const TopologyReport before = Topology();
        r.openEdgesBefore = before.boundaryEdges; r.nonManifoldBefore = before.nonManifoldEdges;
        r.mergedVerts = WeldVertices(weldDist);
        r.flippedFaces = OrientFacesOutward();          // consistent first, so hole rims chain up
        if (fillHoles) {
            r.holesFilled = FillHoles();
            if (r.holesFilled > 0) r.flippedFaces += OrientFacesOutward();   // the caps too
        }
        const TopologyReport after = Topology();
        r.openEdgesAfter = after.boundaryEdges; r.nonManifoldAfter = after.nonManifoldEdges;
        return r;
    }

    /// Bridge: if the mesh has EXACTLY two open boundary loops (e.g. two facing
    /// faces deleted), connect them with a wall of triangles — tunnels, necks,
    /// tube joins. Loops of different sizes are paired proportionally. Returns
    /// true when a bridge was built.
    bool BridgeLoops() {
        if (triangles.size() < 3 || vertices.empty()) return false;
        // Boundary loops, coincident-vertex aware (same approach as FillHoles).
        std::map<std::tuple<int, int, int>, int> grid;
        std::vector<int> rep(vertices.size());
        auto key = [](const Vec3& v) {
            return std::make_tuple((int)std::lround(v.x * 4096.0f),
                                   (int)std::lround(v.y * 4096.0f),
                                   (int)std::lround(v.z * 4096.0f));
        };
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            auto k = key(vertices[i]);
            auto it = grid.find(k);
            if (it == grid.end()) { grid[k] = (int)i; rep[i] = (int)i; }
            else rep[i] = it->second;
        }
        std::set<std::pair<int, int>> edges;
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int a = rep[triangles[t]], b = rep[triangles[t + 1]], c = rep[triangles[t + 2]];
            if (a == b || b == c || a == c) continue;
            edges.insert({a, b}); edges.insert({b, c}); edges.insert({c, a});
        }
        std::map<int, int> next;
        for (const auto& e : edges)
            if (!edges.count({e.second, e.first}) && !next.count(e.first))
                next[e.first] = e.second;
        std::vector<std::vector<int>> loops;
        std::set<int> used;
        for (const auto& start : next) {
            if (used.count(start.first)) continue;
            std::vector<int> loop;
            int v = start.first;
            while (next.count(v) && !used.count(v)) { used.insert(v); loop.push_back(v); v = next[v]; }
            if (loop.size() >= 3 && v == start.first) loops.push_back(std::move(loop));
        }
        if (loops.size() != 2) return false;
        std::vector<int>& A = loops[0];
        std::vector<int>  B = loops[1];
        // Two rims of one surface wind opposite each other when facing; walking A
        // forward pairs with walking B REVERSED. Then rotate B so its start is the
        // vertex nearest A's start.
        std::reverse(B.begin(), B.end());
        {
            float best = 1e30f; std::size_t bi = 0;
            for (std::size_t i = 0; i < B.size(); ++i) {
                float d = (vertices[B[i]] - vertices[A[0]]).SqrMagnitude();
                if (d < best) { best = d; bi = i; }
            }
            std::rotate(B.begin(), B.begin() + bi, B.end());
        }
        const int nA = (int)A.size(), nB = (int)B.size();
        const int steps = std::max(nA, nB);
        auto ai = [&](int i) { return A[((long long)i * nA / steps) % nA]; };
        auto bi = [&](int i) { return B[((long long)i * nB / steps) % nB]; };
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        (void)hadUV;                     // bridge reuses existing vertices only
        for (int i = 0; i < steps; ++i) {
            int a0 = ai(i), a1 = ai(i + 1);
            int b0 = bi(i), b1 = bi(i + 1);
            // The surface already contains the rim's directed edges, so the wall
            // must supply their REVERSES: (a1->a0) and, on the reversed-B rim,
            // (b0->b1). Quad (a1, a0, b0, b1) split into two triangles does both.
            if (a0 != a1) triangles.insert(triangles.end(), {a1, a0, b0});
            if (b0 != b1) triangles.insert(triangles.end(), {a1, b0, b1});
        }
        triColors.clear();
        name = "";
        RefreshNormals();
        return true;
    }

    /// Collapse each listed edge to its midpoint (both endpoints merge into one
    /// vertex) — remove edge loops, simplify dense areas. Degenerate triangles
    /// are dropped by the weld pass.
    void CollapseEdges(const std::vector<std::pair<int, int>>& es) {
        AttrGuard attrGuard_(*this);
        if (es.empty()) return;
        for (const auto& e : es) {
            if (e.first < 0 || e.first >= (int)vertices.size() ||
                e.second < 0 || e.second >= (int)vertices.size()) continue;
            Vec3 mid = (vertices[e.first] + vertices[e.second]) * 0.5f;
            vertices[e.first] = mid;
            vertices[e.second] = mid;
        }
        WeldVertices();                  // merges the pairs + drops collapsed tris
        name = "";
        RefreshNormals();
    }

    /// Split each listed edge at its midpoint: a new vertex is inserted and every
    /// triangle sharing that edge is cut in two — add local detail exactly where
    /// you need it without subdividing whole faces.
    void SplitEdges(const std::vector<std::pair<int, int>>& es) {
        AttrGuard attrGuard_(*this);
        if (es.empty()) return;
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        for (const auto& e : es) {
            int a = e.first, b = e.second;
            if (a < 0 || a >= (int)vertices.size() || b < 0 || b >= (int)vertices.size() || a == b)
                continue;
            int mid = (int)vertices.size();
            vertices.push_back((vertices[a] + vertices[b]) * 0.5f);
            if (hadUV) uvs.push_back({(uvs[a].x + uvs[b].x) * 0.5f, (uvs[a].y + uvs[b].y) * 0.5f});
            std::vector<int> out;
            out.reserve(triangles.size() + 6);
            for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
                int v[3] = {triangles[t], triangles[t + 1], triangles[t + 2]};
                int hit = -1;                            // which cyclic edge is (a,b)?
                for (int k = 0; k < 3; ++k) {
                    int p = v[k], q = v[(k + 1) % 3];
                    if ((p == a && q == b) || (p == b && q == a)) { hit = k; break; }
                }
                if (hit < 0) { out.insert(out.end(), {v[0], v[1], v[2]}); continue; }
                int p = v[hit], q = v[(hit + 1) % 3], r = v[(hit + 2) % 3];
                out.insert(out.end(), {p, mid, r,  mid, q, r});   // same orientation
            }
            triangles = std::move(out);
        }
        triColors.clear();
        name = "";
        RefreshNormals();
    }

    /// Append a detached copy of the listed triangles (new vertices, UVs carried)
    /// and return the indices of the NEW faces — duplicate-and-drag workflows.
    std::vector<int> DuplicateFaces(const std::vector<int>& faces) {
        AttrGuard attrGuard_(*this);
        std::vector<int> newFaces;
        if (faces.empty()) return newFaces;
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        const bool hadColors = HasFaceColors();
        std::map<int, int> remap;
        for (int f : faces) {
            int i = f * 3;
            if (i < 0 || i + 2 >= (int)triangles.size()) continue;
            newFaces.push_back((int)(triangles.size() / 3));
            for (int k = 0; k < 3; ++k) {
                int v = triangles[i + k];
                auto it = remap.find(v);
                int nv;
                if (it == remap.end()) {
                    nv = (int)vertices.size(); remap[v] = nv;
                    vertices.push_back(vertices[v]);
                    if (hadUV) uvs.push_back(uvs[v]);
                } else nv = it->second;
                triangles.push_back(nv);
            }
            if (hadColors && f < (int)triColors.size()) triColors.push_back(triColors[f]);
        }
        name = "";
        RefreshNormals();
        return newFaces;
    }

    // ---- Selection helpers (index sets in/out; the editor owns the selection) ----

    /// Representative index per coincident-vertex group, so selection ops flow
    /// across unwelded seams the same way Smooth() does.
    std::vector<int> CoincidentReps() const {
        // Hashed (not an ordered map): this runs on every smooth-shading pass and on every guided regenerate, on up to a few
        // hundred thousand vertices. The representative is still the first vertex in each 1/4096 cell.
        struct Cell { int x, y, z; bool operator==(const Cell& o) const { return x == o.x && y == o.y && z == o.z; } };
        struct CellHash { std::size_t operator()(const Cell& c) const { return (std::size_t)((unsigned)c.x * 73856093u) ^ (std::size_t)((unsigned)c.y * 19349663u) ^ (std::size_t)((unsigned)c.z * 83492791u); } };
        std::unordered_map<Cell, int, CellHash> grid;
        grid.reserve(vertices.size() * 2 + 1);
        std::vector<int> rep(vertices.size());
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const Vec3& v = vertices[i];
            const Cell k{(int)std::lround(v.x * 4096.0f), (int)std::lround(v.y * 4096.0f), (int)std::lround(v.z * 4096.0f)};
            auto it = grid.find(k);
            if (it == grid.end()) { grid.emplace(k, (int)i); rep[i] = (int)i; }
            else rep[i] = it->second;
        }
        return rep;
    }

    /// One step of selection GROW: every vertex sharing an edge with the selection
    /// joins it (coincident-vertex aware).
    std::vector<int> GrownVertexSelection(const std::vector<int>& sel) const {
        if (sel.empty() || vertices.empty()) return sel;
        std::vector<int> rep = CoincidentReps();
        std::set<int> inSel;                              // rep space
        for (int v : sel) if (v >= 0 && v < (int)vertices.size()) inSel.insert(rep[v]);
        std::set<int> grown = inSel;
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int r[3] = {rep[triangles[t]], rep[triangles[t + 1]], rep[triangles[t + 2]]};
            for (int k = 0; k < 3; ++k)
                for (int j = 0; j < 3; ++j)
                    if (j != k && inSel.count(r[k])) grown.insert(r[j]);
        }
        std::vector<int> out;
        for (std::size_t i = 0; i < vertices.size(); ++i)
            if (grown.count(rep[i])) out.push_back((int)i);
        return out;
    }

    /// One step of selection SHRINK: only vertices whose every edge-neighbour is
    /// also selected stay — peels the selection's rim off.
    std::vector<int> ShrunkVertexSelection(const std::vector<int>& sel) const {
        if (sel.empty() || vertices.empty()) return sel;
        std::vector<int> rep = CoincidentReps();
        std::set<int> inSel;
        for (int v : sel) if (v >= 0 && v < (int)vertices.size()) inSel.insert(rep[v]);
        std::set<int> rim;                                // selected verts touching outside
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int r[3] = {rep[triangles[t]], rep[triangles[t + 1]], rep[triangles[t + 2]]};
            for (int k = 0; k < 3; ++k)
                for (int j = 0; j < 3; ++j)
                    if (j != k && inSel.count(r[k]) && !inSel.count(r[j])) rim.insert(r[k]);
        }
        std::vector<int> out;
        for (std::size_t i = 0; i < vertices.size(); ++i)
            if (inSel.count(rep[i]) && !rim.count(rep[i])) out.push_back((int)i);
        return out;
    }

    /// Everything connected to `seed` through shared edges (coincident-vertex
    /// aware) — Select Linked: grab one island of a combined mesh.
    std::vector<int> LinkedVertices(int seed) const {
        std::vector<int> out;
        if (seed < 0 || seed >= (int)vertices.size()) return out;
        std::vector<int> rep = CoincidentReps();
        std::map<int, std::vector<int>> adj;              // rep-space adjacency
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int r[3] = {rep[triangles[t]], rep[triangles[t + 1]], rep[triangles[t + 2]]};
            for (int k = 0; k < 3; ++k)
                for (int j = 0; j < 3; ++j)
                    if (j != k) adj[r[k]].push_back(r[j]);
        }
        std::set<int> seen;
        std::vector<int> stack = {rep[seed]};
        seen.insert(rep[seed]);
        while (!stack.empty()) {
            int v = stack.back(); stack.pop_back();
            auto it = adj.find(v);
            if (it == adj.end()) continue;
            for (int n : it->second)
                if (!seen.count(n)) { seen.insert(n); stack.push_back(n); }
        }
        for (std::size_t i = 0; i < vertices.size(); ++i)
            if (seen.count(rep[i])) out.push_back((int)i);
        return out;
    }

    /// Extend one edge into its LOOP: at each endpoint keep stepping onto the
    /// straightest continuation (quad-aware via VisibleEdges) until the loop
    /// closes or turns sharper than `minDot`. Returns vertex-index pairs
    /// including the seed edge — Blender's Alt+Click edge loop.
    std::vector<std::pair<int, int>> EdgeLoop(int va, int vb, float minDot = 0.5f) const {
        std::vector<std::pair<int, int>> out;
        if (va < 0 || vb < 0 || va >= (int)vertices.size() || vb >= (int)vertices.size())
            return out;
        std::vector<int> rep = CoincidentReps();
        std::map<int, std::vector<int>> adj;              // quad-wireframe adjacency
        for (const auto& e : VisibleEdges()) {
            int a = rep[e.first], b = rep[e.second];
            if (a == b) continue;
            adj[a].push_back(b);
            adj[b].push_back(a);
        }
        auto dir = [&](int a, int b) { return (vertices[b] - vertices[a]).Normalized(); };
        std::set<std::pair<int, int>> seen;
        auto addEdge = [&](int a, int b) {
            auto k = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
            if (seen.count(k)) return false;
            seen.insert(k);
            out.push_back({a, b});
            return true;
        };
        int A = rep[va], B = rep[vb];
        if (!addEdge(A, B)) return out;
        for (int pass = 0; pass < 2; ++pass) {            // walk both directions
            int prev = pass == 0 ? A : B;
            int cur  = pass == 0 ? B : A;
            for (int guard = 0; guard < 4096; ++guard) {
                Vec3 d = dir(prev, cur);
                float best = minDot; int nxt = -1;
                auto it = adj.find(cur);
                if (it == adj.end()) break;
                for (int n : it->second) {
                    if (n == prev) continue;
                    float dot = Vec3::Dot(d, dir(cur, n));
                    if (dot > best) { best = dot; nxt = n; }
                }
                if (nxt < 0) break;
                if (!addEdge(cur, nxt)) break;            // loop closed
                prev = cur; cur = nxt;
            }
        }
        return out;
    }

    // ---- Face painting (per-face vertex colors, saved via the meshcolors record) ----

    /// Make sure the per-face color array exists (one entry per triangle),
    /// seeding it with `base` — call before painting.
    void EnsureFaceColors(const Color& base = {1, 1, 1, 1}) {
        if (!HasFaceColors()) triColors.assign(TriangleCount(), base);
    }

    /// Paint brush: blend the faces whose centre lies within `radius` of `center`
    /// toward color `c`, by BrushFalloff times `blend` (at most all the way).
    /// When the mesh knows its polygons (polyFace), a quad or n-gon is one face:
    /// its centre is the average of its corners and all its triangles get the same
    /// colour. Painting keeps the primitive name — a painted cube is still a Cube.
    void PaintFaces(const Vec3& center, float radius, const Color& c, float blend = 1.0f) {
        if (!(radius > 1e-6f) || triangles.empty() || !(blend > 0.0f)) return;
        if (!std::isfinite(radius + blend + center.x + center.y + center.z + c.r + c.g + c.b + c.a)) return;   // NaN would spread into the colours
        EnsureFaceColors();
        const int tc = TriangleCount();
        const bool poly = (int)polyFace.size() == tc;
        std::map<int, std::vector<int>> polyVerts;   // polygon id -> its corners
        if (poly)
            for (int f = 0; f < tc; ++f)
                for (int k = 0; k < 3; ++k) polyVerts[polyFace[(std::size_t)f]].push_back(triangles[(std::size_t)f * 3 + k]);
        std::map<int, Vec3> polyCentre;
        for (auto& pv : polyVerts) {
            std::vector<int>& l = pv.second;
            std::sort(l.begin(), l.end());
            l.erase(std::unique(l.begin(), l.end()), l.end());
            Vec3 sum{0, 0, 0}; int cnt = 0;
            for (int v : l) if (v >= 0 && v < (int)vertices.size()) { sum += vertices[(std::size_t)v]; ++cnt; }
            polyCentre[pv.first] = cnt ? sum * (1.0f / (float)cnt) : Vec3{0, 0, 0};
        }
        for (int f = 0; f < tc; ++f) {
            const Vec3 fc = poly ? polyCentre[polyFace[(std::size_t)f]] : FaceCenter(f);
            float w = BrushFalloff((fc - center).Magnitude(), radius) * blend;
            if (w <= 0.0f) continue;
            if (w > 1.0f) w = 1.0f;
            Color& o = triColors[(std::size_t)f];
            o.r += (c.r - o.r) * w;
            o.g += (c.g - o.g) * w;
            o.b += (c.b - o.b) * w;
            o.a += (c.a - o.a) * w;
        }
    }

    /// Vary each face color's brightness by up to ±`amt` (deterministic per seed)
    /// — breaks up flat prop colors so wood/stone/foliage reads as a material
    /// instead of plastic. Keeps the primitive name (used by the prop builders).
    void MottleFaceColors(float amt, int seed = 0) {
        if (!HasFaceColors() || amt <= 0.0f) return;
        for (int f = 0; f < (int)triColors.size(); ++f) {
            unsigned x = ((unsigned)f * 73856093u) ^ ((unsigned)seed * 19349663u);
            x = (x ^ (x >> 13)) * 1274126177u;
            float k = 1.0f + amt * (((float)((x ^ (x >> 16)) & 0xffffu) / 65535.0f) * 2.0f - 1.0f);
            Color& c = triColors[f];
            c.r = std::fmin(c.r * k, 1.0f);
            c.g = std::fmin(c.g * k, 1.0f);
            c.b = std::fmin(c.b * k, 1.0f);
        }
    }

    /// Set the listed faces to exactly `c` (bucket fill on a selection).
    void FillFacesColor(const std::vector<int>& faces, const Color& c) {
        if (faces.empty()) return;
        EnsureFaceColors();
        for (int f : faces)
            if (f >= 0 && f < (int)triColors.size()) triColors[f] = c;
    }

    /// Extrude the listed edges: each gets a wall quad pushed `dist` along its
    /// adjacent face's normal — pull walls up from a floor plate's rim, extend a
    /// ribbon strip. Returns the NEW outer edges so the caller can keep extruding
    /// or move them. Winding is chosen from how the edge appears in its face, so
    /// boundary-edge walls face outward.
    std::vector<std::pair<int, int>> ExtrudeEdges(const std::vector<std::pair<int, int>>& es,
                                                  float dist) {
        std::vector<std::pair<int, int>> newEdges;
        if (es.empty() || std::fabs(dist) < 1e-8f) return newEdges;
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        for (const auto& e : es) {
            int a = e.first, b = e.second;
            if (a < 0 || a >= (int)vertices.size() || b < 0 || b >= (int)vertices.size() || a == b)
                continue;
            // Find a triangle owning this edge to get its direction + normal.
            int p = -1, q = -1; Vec3 n{0, 1, 0}; bool found = false;
            for (std::size_t t = 0; t + 2 < triangles.size() && !found; t += 3) {
                int v[3] = {triangles[t], triangles[t + 1], triangles[t + 2]};
                for (int k = 0; k < 3; ++k) {
                    int i0 = v[k], i1 = v[(k + 1) % 3];
                    if ((i0 == a && i1 == b) || (i0 == b && i1 == a)) {
                        p = i0; q = i1; n = FaceNormal((int)(t / 3)); found = true; break;
                    }
                }
            }
            if (!found) continue;
            int p2 = (int)vertices.size();
            vertices.push_back(vertices[p] + n * dist);
            if (hadUV) uvs.push_back(uvs[p]);
            int q2 = (int)vertices.size();
            vertices.push_back(vertices[q] + n * dist);
            if (hadUV) uvs.push_back(uvs[q]);
            // The face already has directed edge p->q; the wall supplies q->p.
            triangles.insert(triangles.end(), {q, p, p2,  q, p2, q2});
            newEdges.push_back({p2, q2});
        }
        if (!newEdges.empty()) {
            triColors.clear();
            name = "";
            RefreshNormals();
        }
        return newEdges;
    }

    /// Quantize vertices to a grid of `step` (empty `verts` = whole mesh) — clean
    /// up hand-moved points, make modular kit pieces line up exactly.
    void SnapToGrid(float step, const std::vector<int>& verts = {}) {
        if (!std::isfinite(step)) return;   // a NaN or infinite amount would turn every vertex into NaN
        if (step <= 1e-6f || vertices.empty()) return;
        auto snap = [&](int v) {
            vertices[v] = {std::lround(vertices[v].x / step) * step,
                           std::lround(vertices[v].y / step) * step,
                           std::lround(vertices[v].z / step) * step};
        };
        if (verts.empty())
            for (int i = 0; i < (int)vertices.size(); ++i) snap(i);
        else
            for (int v : verts)
                if (v >= 0 && v < (int)vertices.size()) snap(v);
        name = "";
        RefreshNormals();
    }

    /// Split every triangle that straddles the plane `axis == coord` (0=X 1=Y
    /// 2=Z), inserting vertices on the crossing edges and keeping BOTH sides — an
    /// edge loop across the mesh. The surface stays watertight and its volume is
    /// unchanged (this only adds cuts). Face colors carry to the split pieces.
    void SplitByPlane(int axis, float coord, float eps = 1e-5f) {
        if (axis < 0 || axis > 2 || triangles.size() < 3) return;
        const bool hadColors = HasFaceColors();
        std::vector<int> out; out.reserve(triangles.size() * 2);
        std::vector<Color> outC; if (hadColors) outC.reserve(triColors.size() * 2);
        auto sd = [&](int v) { return (&vertices[v].x)[axis] - coord; };
        auto lerpV = [&](int a, int b) {
            float sa = sd(a), sb = sd(b), t = sa / (sa - sb);
            int idx = (int)vertices.size();
            vertices.push_back(vertices[a] + (vertices[b] - vertices[a]) * t);
            if (uvs.size() == vertices.size() - 1)
                uvs.clear();   // (keep it simple: slicing drops per-vertex UVs)
            return idx;
        };
        for (int f = 0, n = TriangleCount(); f < n; ++f) {
            int i = f * 3, a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            Color fc = (hadColors && f < (int)triColors.size()) ? triColors[f] : Color{1,1,1,1};
            float s[3] = {sd(a), sd(b), sd(c)};
            float mn = std::fmin(s[0], std::fmin(s[1], s[2]));
            float mx = std::fmax(s[0], std::fmax(s[1], s[2]));
            if (!(mn < -eps && mx > eps)) {                     // no genuine crossing
                out.insert(out.end(), {a, b, c}); if (hadColors) outC.push_back(fc); continue;
            }
            int v[3] = {a, b, c};
            int lone = (s[0] * s[1] >= 0.0f) ? 2 : ((s[0] * s[2] >= 0.0f) ? 1 : 0);
            int L = v[lone], M = v[(lone + 1) % 3], N = v[(lone + 2) % 3];
            int iLM = lerpV(L, M), iNL = lerpV(N, L);
            out.insert(out.end(), {L, iLM, iNL});               // lone-side triangle
            out.insert(out.end(), {M, N, iNL,  M, iNL, iLM});   // other-side quad
            if (hadColors) { outC.push_back(fc); outC.push_back(fc); outC.push_back(fc); }
        }
        triangles = std::move(out);
        if (hadColors) triColors = std::move(outC);
        name = "";
        RefreshNormals();
    }

    /// Slice: cut the whole mesh with `count` evenly spaced planes across `axis`
    /// (0=X 1=Y 2=Z) and weld the cut vertices into rings — adds editable divisions
    /// to a wall/box/extruded shape on any topology. (This was called "Loop Cut";
    /// the real edge-ring Loop Cut is modeling::LoopCut, Ctrl+R in Edit Mesh.)
    void SliceAxis(int axis, int count) {
        if (axis < 0 || axis > 2 || count < 1 || vertices.empty()) return;
        Vec3 lo, hi; Bounds(lo, hi);
        float a0 = (&lo.x)[axis], a1 = (&hi.x)[axis];
        if (a1 - a0 < 1e-5f) return;
        for (int k = 1; k <= count; ++k)
            SplitByPlane(axis, a0 + (a1 - a0) * (float)k / (float)(count + 1));
        WeldVertices();                                         // merge coincident cut verts into loops
        name = "";
        RefreshNormals();
    }

    /// Chamfer the listed vertices: each selected corner is cut back by `amount`
    /// along every edge that meets it and the opening is capped with a flat facet —
    /// Blender's vertex bevel. Great for knocking the sharp corners off boxes.
    void BevelVertices(const std::vector<int>& sel, float amount) {
        AttrGuard attrGuard_(*this);
        if (!std::isfinite(amount)) return;   // a NaN or infinite amount would turn every vertex into NaN
        if (sel.empty() || amount <= 1e-6f || triangles.empty()) return;
        std::set<int> S;
        for (int v : sel) if (v >= 0 && v < (int)vertices.size()) S.insert(v);
        if (S.empty()) return;
        const bool hadUV = uvs.size() == vertices.size() && !uvs.empty();
        std::vector<Vec3> vn = Normals();                // cap orientation
        std::map<std::pair<int, int>, int> splitIdx;     // (corner, neighbour) -> new vert
        std::map<int, std::vector<int>> ringOf;          // corner -> its new ring verts
        auto splitPoint = [&](int v, int o) {
            auto k = std::make_pair(v, o);
            auto it = splitIdx.find(k);
            if (it != splitIdx.end()) return it->second;
            Vec3 d = vertices[o] - vertices[v];
            float len = d.Magnitude();
            float t = len > 1e-8f ? std::min(amount, len * 0.45f) / len : 0.0f;
            int idx = (int)vertices.size();
            vertices.push_back(vertices[v] + d * t);
            if (hadUV) uvs.push_back(uvs[v]);
            splitIdx[k] = idx;
            ringOf[v].push_back(idx);
            return idx;
        };
        std::vector<int> out;
        out.reserve(triangles.size() * 2);
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            int a = triangles[t], b = triangles[t + 1], c = triangles[t + 2];
            int poly[6]; int pn = 0;
            // Walk the perimeter a->b->c; a chamfered corner contributes its two
            // edge split points in walk order (arrive-edge first, leave-edge second).
            auto corner = [&](int v, int prev, int nxt) {
                if (!S.count(v)) { poly[pn++] = v; return; }
                poly[pn++] = splitPoint(v, prev);
                poly[pn++] = splitPoint(v, nxt);
            };
            corner(a, c, b); corner(b, a, c); corner(c, b, a);
            for (int i = 1; i + 1 < pn; ++i)
                out.insert(out.end(), {poly[0], poly[i], poly[i + 1]});
        }
        triangles = std::move(out);
        // Cap each chamfered corner with a facet over its ring, ordered by angle
        // around the old vertex normal so the fan faces outward.
        for (auto& kv : ringOf) {
            std::vector<int>& ring = kv.second;
            if (ring.size() < 3) continue;
            Vec3 nrm = vn[kv.first];
            if (nrm.Magnitude() < 1e-6f) nrm = {0, 1, 0};
            Vec3 u = Vec3::Cross(nrm, std::fabs(nrm.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0});
            { float mu = u.Magnitude(); u = mu > 1e-8f ? u * (1.0f / mu) : Vec3{1, 0, 0}; }
            Vec3 w = Vec3::Cross(nrm, u);
            Vec3 c{0, 0, 0};
            for (int i : ring) c += vertices[i];
            c = c * (1.0f / (float)ring.size());
            std::sort(ring.begin(), ring.end(), [&](int i, int j) {
                Vec3 di = vertices[i] - c, dj = vertices[j] - c;
                return std::atan2(Vec3::Dot(di, w), Vec3::Dot(di, u))
                     < std::atan2(Vec3::Dot(dj, w), Vec3::Dot(dj, u));
            });
            for (std::size_t i = 1; i + 1 < ring.size(); ++i)
                triangles.insert(triangles.end(), {ring[0], (int)ring[i], (int)ring[i + 1]});
        }
        triColors.clear();                               // face records changed shape
        name = "";
        RefreshNormals();
    }

    /// Displace vertices by a deterministic random offset up to `amount` — quick
    /// organic roughness (rocks, rubble, hand-made look). Empty `verts` = whole mesh.
    void JitterVertices(const std::vector<int>& verts, float amount, int seed = 0) {
        if (amount == 0.0f || !std::isfinite(amount) || vertices.empty()) return;   // a typed NaN or infinite value would turn every vertex into NaN
        auto h = [&](int i, int c) {
            unsigned x = ((unsigned)i * 73856093u) ^ ((unsigned)c * 19349663u) ^ ((unsigned)seed * 83492791u);
            x = (x ^ (x >> 13)) * 1274126177u;
            return ((float)((x ^ (x >> 16)) & 0xffffu) / 65535.0f) * 2.0f - 1.0f;
        };
        auto apply = [&](int v) {
            vertices[v] += Vec3{h(v, 1), h(v, 2), h(v, 3)} * amount;
        };
        if (verts.empty())
            for (int i = 0; i < (int)vertices.size(); ++i) apply(i);
        else
            for (int v : verts)
                if (v >= 0 && v < (int)vertices.size()) apply(v);
        name = "";
        RefreshNormals();
    }

    /// Shear: slide the mesh along `axis` proportionally to the coordinate on
    /// `along` (about the centre) — slanted walls, italic text blocks, leaning towers.
    void Shear(int axis, int along, float amount) {
        if (axis < 0 || axis > 2 || along < 0 || along > 2 || axis == along
            || amount == 0.0f || !std::isfinite(amount) || vertices.empty()) return;   // a typed NaN or infinite value would turn every vertex into NaN
        Vec3 lo, hi; Bounds(lo, hi);
        float c = ((&lo.x)[along] + (&hi.x)[along]) * 0.5f;
        for (Vec3& v : vertices)
            (&v.x)[axis] += ((&v.x)[along] - c) * amount;
        name = "";
        RefreshNormals();
    }

private:
    /// Deterministic hash of an integer lattice point → [-1,1] (for value noise).
    static float Hash3(int x, int y, int z, int seed) {
        unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + (unsigned)z * 2147483647u + (unsigned)seed * 971u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return ((float)((h ^ (h >> 16)) & 0xffffu) / 65535.0f) * 2.0f - 1.0f;
    }
    /// Trilinear-interpolated value noise at `p` (smoothstep fade), range ~[-1,1].
    static float ValueNoise(const Vec3& p, int seed) {
        int xi = (int)std::floor(p.x), yi = (int)std::floor(p.y), zi = (int)std::floor(p.z);
        float fx = p.x - xi, fy = p.y - yi, fz = p.z - zi;
        auto sm = [](float t) { return t * t * (3.0f - 2.0f * t); };
        fx = sm(fx); fy = sm(fy); fz = sm(fz);
        auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
        float c00 = lerp(Hash3(xi, yi, zi, seed),       Hash3(xi + 1, yi, zi, seed), fx);
        float c10 = lerp(Hash3(xi, yi + 1, zi, seed),   Hash3(xi + 1, yi + 1, zi, seed), fx);
        float c01 = lerp(Hash3(xi, yi, zi + 1, seed),   Hash3(xi + 1, yi, zi + 1, seed), fx);
        float c11 = lerp(Hash3(xi, yi + 1, zi + 1, seed), Hash3(xi + 1, yi + 1, zi + 1, seed), fx);
        return lerp(lerp(c00, c10, fy), lerp(c01, c11, fy), fz);
    }
    /// Append a square-section beam between two points (used by Wireframe()).
    void AddBeam(const Vec3& p0, const Vec3& p1, float r) {
        Vec3 d = p1 - p0; float len = d.Magnitude();
        if (len < 1e-6f) return;
        d = d * (1.0f / len);
        Vec3 up = std::fabs(d.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        Vec3 u = Vec3::Cross(d, up); u = u * (1.0f / std::max(1e-6f, u.Magnitude()));
        Vec3 w = Vec3::Cross(d, u);
        auto corner = [&](const Vec3& base, float su, float sw) { return base + u * (su * r) + w * (sw * r); };
        int b = (int)vertices.size();
        // 0..3 around p0, 4..7 around p1 (same order).
        vertices.push_back(corner(p0, +1, +1)); vertices.push_back(corner(p0, +1, -1));
        vertices.push_back(corner(p0, -1, -1)); vertices.push_back(corner(p0, -1, +1));
        vertices.push_back(corner(p1, +1, +1)); vertices.push_back(corner(p1, +1, -1));
        vertices.push_back(corner(p1, -1, -1)); vertices.push_back(corner(p1, -1, +1));
        auto quad = [&](int a, int b2, int c, int dd) {
            triangles.insert(triangles.end(), {b + a, b + b2, b + c, b + a, b + c, b + dd});
        };
        quad(0, 1, 5, 4); quad(1, 2, 6, 5); quad(2, 3, 7, 6); quad(3, 0, 4, 7);  // sides
        quad(3, 2, 1, 0); quad(4, 5, 6, 7);                                       // caps
    }

    /// Choose a grid that covers [lo,hi] at ~`voxel`, capped to `maxDim` per axis
    /// (enlarging the effective cell size `outV` if needed so it always fits).
    static void GridDims(const Vec3& lo, const Vec3& hi, float voxel, int maxDim,
                         int& nx, int& ny, int& nz, float& outV) {
        outV = voxel;
        auto dim = [&](float span) {
            int d = (int)std::ceil(span / outV);
            return d < 1 ? 1 : d;
        };
        nx = dim(hi.x - lo.x); ny = dim(hi.y - lo.y); nz = dim(hi.z - lo.z);
        int m = std::max(nx, std::max(ny, nz));
        if (maxDim > 0 && m > maxDim) {
            outV *= (float)m / (float)maxDim;                   // grow cells to fit the cap
            nx = dim(hi.x - lo.x); ny = dim(hi.y - lo.y); nz = dim(hi.z - lo.z);
        }
    }
    /// Mark every grid cell whose centre is inside `src` as solid.
    static void SampleInto(const Mesh& src, std::vector<unsigned char>& occ,
                           int nx, int ny, int nz, const Vec3& origin, float v) {
        for (int z = 0; z < nz; ++z)
            for (int y = 0; y < ny; ++y)
                for (int x = 0; x < nx; ++x) {
                    Vec3 c{origin.x + (x + 0.5f) * v, origin.y + (y + 0.5f) * v, origin.z + (z + 0.5f) * v};
                    if (src.PointInside(c)) occ[(std::size_t)(z * ny + y) * nx + x] = 1;
                }
    }
    /// Build a surface from a solid/empty grid: a quad on every face between a solid
    /// cell and an empty neighbour (or the grid edge), wound to face outward.
    static Mesh EmitOccupancy(const std::vector<unsigned char>& occ,
                              int nx, int ny, int nz, const Vec3& origin, float v) {
        Mesh out;
        auto solid = [&](int x, int y, int z) {
            if (x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz) return false;
            return occ[(std::size_t)(z * ny + y) * nx + x] != 0;
        };
        auto face = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 outward) {
            Vec3 nrm = Vec3::Cross(b - a, c - a);
            float dot = nrm.x * outward.x + nrm.y * outward.y + nrm.z * outward.z;
            int base = (int)out.vertices.size();
            if (dot >= 0.0f) { out.vertices.push_back(a); out.vertices.push_back(b);
                               out.vertices.push_back(c); out.vertices.push_back(d); }
            else             { out.vertices.push_back(a); out.vertices.push_back(d);
                               out.vertices.push_back(c); out.vertices.push_back(b); }
            out.triangles.insert(out.triangles.end(),
                                 {base, base + 1, base + 2, base, base + 2, base + 3});
        };
        for (int z = 0; z < nz; ++z)
            for (int y = 0; y < ny; ++y)
                for (int x = 0; x < nx; ++x) {
                    if (!solid(x, y, z)) continue;
                    float x0 = origin.x + x * v, y0 = origin.y + y * v, z0 = origin.z + z * v;
                    float x1 = x0 + v, y1 = y0 + v, z1 = z0 + v;
                    if (!solid(x + 1, y, z)) face({x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, {1, 0, 0});
                    if (!solid(x - 1, y, z)) face({x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {-1, 0, 0});
                    if (!solid(x, y + 1, z)) face({x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}, {0, 1, 0});
                    if (!solid(x, y - 1, z)) face({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {0, -1, 0});
                    if (!solid(x, y, z + 1)) face({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {0, 0, 1});
                    if (!solid(x, y, z - 1)) face({x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}, {0, 0, -1});
                }
        out.WeldVertices();
        out.ComputeSmoothNormals();
        return out;
    }

public:
};

} // namespace okay
