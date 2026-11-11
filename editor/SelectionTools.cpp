// See SelectionTools.hpp for what these tools do and the rules they follow.
#include "SelectionTools.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/MeshModifiers.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include "okay/Components/Spline.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ostream>
#include <map>
#include <set>

namespace okay::editor::seltools {

namespace {
Settings g_settings;

inline float Comp(const Vec3& v, int a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); }
inline void SetComp(Vec3& v, int a, float f) { (a == 0 ? v.x : (a == 1 ? v.y : v.z)) = f; }
inline Vec3 Unit(int a) { return a == 0 ? Vec3{1, 0, 0} : (a == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1}); }

/// The selection the actions use: the whole multi-selection, or the one selected object.
std::vector<GameObject*> Selection(const EditorState& ed) {
    if (!ed.MultiSelection().empty()) return ed.MultiSelection();
    if (ed.selected()) return {ed.selected()};
    return {};
}

bool HasSelectedAncestor(const GameObject* g, const std::set<const GameObject*>& sel) {
    for (const Transform* t = g->transform ? g->transform->Parent() : nullptr; t; t = t->Parent())
        if (t->gameObject && sel.count(t->gameObject)) return true;
    return false;
}

std::string Num(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.3g", (double)v);
    return b;
}

/// The target that holds the active object (the active object itself unless it sits below another selected object).
GameObject* ActiveTarget(const std::vector<GameObject*>& targets, const GameObject* active) {
    if (!active) return nullptr;
    for (GameObject* t : targets)
        if (active->IsSelfOrDescendantOf(t)) return t;
    return nullptr;
}

void MoveWorld(GameObject* g, const Vec3& delta) {
    if (delta.x == 0.0f && delta.y == 0.0f && delta.z == 0.0f) return;
    g->transform->SetPosition(g->transform->Position() + delta);
}

// ---- Mirror ---------------------------------------------------------------------------------------------------------
void Rot3(const Quat& q, float r[3][3]) {
    for (int j = 0; j < 3; ++j) {
        const Vec3 c = q * Unit(j);
        r[0][j] = c.x; r[1][j] = c.y; r[2][j] = c.z;
    }
}
Quat FromRot3(float r[3][3]) {
    return Quat::FromRotationMatrix(r[0][0], r[0][1], r[0][2], r[1][0], r[1][1], r[1][2], r[2][0], r[2][1], r[2][2]);
}

/// The local axis to flip: the object's own axis most aligned with the plane normal (the first of those within a hair of
/// the best, so mirroring a mirrored object picks the same axis again). Z is left out when the subtree has a direction
/// that must stay a direction (lights, cameras): flipping their forward axis would point them backwards.
int ChooseFlipAxis(const Quat& worldRot, int planeAxis, bool avoidZ) {
    float c[3];
    for (int j = 0; j < 3; ++j) c[j] = std::fabs(Comp(worldRot * Unit(j), planeAxis));
    float best = -1.0f;
    for (int j = 0; j < 3; ++j) if (!(avoidZ && j == 2)) best = std::max(best, c[j]);
    for (int j = 0; j < 3; ++j) if (!(avoidZ && j == 2) && c[j] >= best - 1e-4f) return j;
    return 0;
}

using PlaneKey = std::array<long long, 4>;   // unit normal and distance from the origin, in thousandths

/// The surface of a mesh as {oriented plane -> area on it} plus its set of corner positions: what a viewer can see,
/// independent of how the faces are cut into triangles (a quad's diagonal runs the other way after a flip).
struct Surface { std::map<PlaneKey, double> planes; std::set<std::array<long long, 3>> corners; };

Surface SurfaceOf(const Mesh& m, int flipAxis) {
    Surface s;
    auto q = [](float f) { return (long long)std::llround((double)f * 1e3); };
    const int nt = m.TriangleCount();
    for (int t = 0; t < nt; ++t) {
        Vec3 p[3] = {m.vertices[m.triangles[t * 3]], m.vertices[m.triangles[t * 3 + 1]], m.vertices[m.triangles[t * 3 + 2]]};
        if (flipAxis >= 0) {                         // reflected across local axis `flipAxis`, winding reversed
            for (Vec3& v : p) SetComp(v, flipAxis, -Comp(v, flipAxis));
            std::swap(p[1], p[2]);
        }
        for (const Vec3& v : p) s.corners.insert({q(v.x), q(v.y), q(v.z)});
        const Vec3 c = Vec3::Cross(p[1] - p[0], p[2] - p[0]);
        const float len = c.Magnitude();
        if (len < 1e-9f) continue;
        const Vec3 n = c / len;
        s.planes[{q(n.x), q(n.y), q(n.z), q(Vec3::Dot(n, p[0]))}] += 0.5 * (double)len;
    }
    return s;
}

/// True when flipping local axis `k` (and reversing the winding) gives the same surface: a cube, a plane, a sphere.
/// Such a mesh is left alone. A mesh with UVs is never counted symmetric (its texture would not mirror).
bool SymmetricAcross(const Mesh& m, int k) {
    if (m.HasUVs() || m.HasCornerUVs() || m.vertices.empty() || m.TriangleCount() > 20000) return false;
    for (int idx : m.triangles) if (idx < 0 || idx >= (int)m.vertices.size()) return false;
    const Surface a = SurfaceOf(m, -1), b = SurfaceOf(m, k);
    if (a.corners != b.corners || a.planes.size() != b.planes.size()) return false;
    for (auto ia = a.planes.begin(), ib = b.planes.begin(); ia != a.planes.end(); ++ia, ++ib)
        if (ia->first != ib->first || std::fabs(ia->second - ib->second) > 1e-3 * std::max(1.0, ia->second)) return false;
    return true;
}

void FlipMesh(Mesh& m, int k) {
    for (Vec3& v : m.vertices) SetComp(v, k, -Comp(v, k));
    for (Vec3& n : m.normals) SetComp(n, k, -Comp(n, k));
    const bool cuv = m.HasCornerUVs();
    for (std::size_t i = 0; i + 2 < m.triangles.size(); i += 3) {
        std::swap(m.triangles[i + 1], m.triangles[i + 2]);
        if (cuv) std::swap(m.cornerUVs[i + 1], m.cornerUVs[i + 2]);
    }
    m.name = "";
}

void CollectSubtree(GameObject* root, std::vector<GameObject*>& out) {
    std::vector<GameObject*> stack{root};
    while (!stack.empty()) {
        GameObject* g = stack.back(); stack.pop_back();
        if (!g || g->IsDestroyed() || !g->transform) continue;
        out.push_back(g);
        for (Transform* c : g->transform->Children()) if (c && c->gameObject) stack.push_back(c->gameObject);
    }
}

bool ParentNeedsShear(const Transform* parent) {
    if (!parent) return false;
    const Vec3 s = parent->LossyScale();
    const float lo = std::min({std::fabs(s.x), std::fabs(s.y), std::fabs(s.z)});
    const float hi = std::max({std::fabs(s.x), std::fabs(s.y), std::fabs(s.z)});
    if (hi - lo <= 1e-3f * std::max(1.0f, hi)) return false;      // an even scale never shears
    float r[3][3];
    Rot3(parent->Rotation(), r);
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
        const float a = std::fabs(r[i][j]);
        if (a > 1e-3f && a < 1.0f - 1e-3f) return true;           // turned by something other than a quarter turn
    }
    return false;
}
}  // namespace

Settings& Cfg() { return g_settings; }

void AppendSettings(std::ostream& o, const Settings& s) {
    o << "alignto " << (int)s.alignTo << "\n"
      << "spreadmode " << (int)s.spread << "\n"
      << "mirrorabout " << (int)s.mirrorAbout << "\n"
      << "mirrordup " << (s.mirrorDuplicate ? 1 : 0) << "\n"
      << "gizmopivot " << (int)s.gizmoPivot << "\n";
}
bool ApplySetting(const std::string& k, int v, Settings& s) {
    if (k == "alignto") s.alignTo = v == 1 ? AlignTo::Active : AlignTo::Selection;
    else if (k == "spreadmode") s.spread = v == 1 ? Spread::Centres : Spread::Gaps;
    else if (k == "mirrorabout") s.mirrorAbout = v == 1 ? MirrorAbout::ActivePivot : (v == 2 ? MirrorAbout::WorldOrigin : MirrorAbout::SelectionCentre);
    else if (k == "mirrordup") s.mirrorDuplicate = v != 0;
    else if (k == "gizmopivot") s.gizmoPivot = v == 1 ? GizmoPivot::Center : GizmoPivot::Active;
    else return false;
    return true;
}

const char* AxisName(Axis a) { return a == Axis::X ? "X" : (a == Axis::Y ? "Y" : "Z"); }

// ---- Targets and bounds ---------------------------------------------------------------------------------------------
std::vector<GameObject*> Targets(const std::vector<GameObject*>& selection) {
    std::set<const GameObject*> all;
    for (GameObject* g : selection) if (g && !g->IsDestroyed() && g->transform) all.insert(g);
    std::vector<GameObject*> out;
    std::set<const GameObject*> seen;
    for (GameObject* g : selection) {
        if (!g || !all.count(g) || !seen.insert(g).second) continue;
        if (HasSelectedAncestor(g, all)) continue;
        out.push_back(g);
    }
    return out;
}

Bounds ObjectBounds(const GameObject& go) {
    Bounds b = Scene::WorldBounds(go, true);
    if (!b.IsEmpty()) return b;
    std::vector<const GameObject*> stack{&go};
    while (!stack.empty()) {
        const GameObject* g = stack.back(); stack.pop_back();
        if (g->IsDestroyed() || !g->active || !g->transform) continue;
        if (const Collider3D* c = g->GetComponent<Collider3D>()) {
            Vec3 lo, hi;
            c->WorldAABB(lo, hi);
            b.Encapsulate(lo);
            b.Encapsulate(hi);
        }
        for (const Transform* c : g->transform->Children()) if (c && c->gameObject) stack.push_back(c->gameObject);
    }
    if (b.IsEmpty()) b.Encapsulate(go.transform ? go.transform->Position() : Vec3{});
    return b;
}

Vec3 SelectionCentre(const std::vector<GameObject*>& targets) {
    Bounds u;
    for (const GameObject* g : targets) u.Encapsulate(ObjectBounds(*g));
    return u.IsEmpty() ? Vec3{} : u.center;
}

// ---- Availability -----------------------------------------------------------------------------------------------------
static Availability Need(const EditorState& ed, std::size_t n) {
    const auto sel = Selection(ed);
    const auto tg = Targets(sel);
    Availability a;
    if (tg.size() >= n) { a.ok = true; return a; }
    const std::string need = n == 1 ? "1 object" : (std::to_string(n) + " or more objects");
    if (sel.size() >= n)
        a.reason = "Needs " + need + " that are not inside each other (a child moves with its parent): " + std::to_string(tg.size()) + " usable";
    else
        a.reason = "Select " + need + " first (" + std::to_string(sel.size()) + " selected)";
    return a;
}
Availability CanAlign(const EditorState& ed) { return Need(ed, 2); }
Availability CanDistribute(const EditorState& ed) { return Need(ed, 3); }
Availability CanMirror(const EditorState& ed) { return Need(ed, 1); }

// ---- Align / Distribute -----------------------------------------------------------------------------------------------
namespace {
float EdgeOf(const Bounds& b, int axis, Edge e) {
    return e == Edge::Min ? Comp(b.Min(), axis) : (e == Edge::Max ? Comp(b.Max(), axis) : Comp(b.center, axis));
}
const char* EdgeName(Edge e) { return e == Edge::Min ? "min" : (e == Edge::Max ? "max" : "centre"); }

Result Apply(EditorState& ed, const std::vector<GameObject*>& targets, const std::vector<float>& delta, int axis, const std::string& what) {
    Result r;
    int n = 0;
    for (float d : delta) if (std::fabs(d) > 1e-6f) ++n;
    r.ok = true;
    if (n == 0) { r.message = what + ": already in place"; return r; }
    ed.PushUndo();
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (std::fabs(delta[i]) <= 1e-6f) continue;
        MoveWorld(targets[i], Unit(axis) * delta[i]);
    }
    ed.dirty = true;
    r.changed = n;
    r.message = what + ": moved " + std::to_string(n) + " object" + (n == 1 ? "" : "s");
    return r;
}
}  // namespace

Result Align(EditorState& ed, Axis axis, Edge edge, AlignTo to) {
    const Availability av = CanAlign(ed);
    if (!av.ok) { Result r; r.message = av.reason; return r; }
    const auto targets = Targets(Selection(ed));
    const int a = (int)axis;
    std::vector<Bounds> bs;
    Bounds all;
    for (const GameObject* g : targets) { bs.push_back(ObjectBounds(*g)); all.Encapsulate(bs.back()); }
    Bounds ref = all;
    std::string refName = "the selection";
    if (to == AlignTo::Active) {
        GameObject* at = ActiveTarget(targets, ed.selected());
        if (!at) { Result r; r.message = "Align to the active object: there is no active object in the selection"; return r; }
        for (std::size_t i = 0; i < targets.size(); ++i) if (targets[i] == at) ref = bs[i];
        refName = "'" + at->name + "'";
    }
    const float want = EdgeOf(ref, a, edge);
    std::vector<float> delta(targets.size());
    for (std::size_t i = 0; i < targets.size(); ++i) delta[i] = want - EdgeOf(bs[i], a, edge);
    return Apply(ed, targets, delta, a, std::string("Align ") + AxisName(axis) + " " + EdgeName(edge) + " to " + refName);
}

Result Distribute(EditorState& ed, Axis axis, Spread how) {
    const Availability av = CanDistribute(ed);
    if (!av.ok) { Result r; r.message = av.reason; return r; }
    const auto targets = Targets(Selection(ed));
    const int a = (int)axis;
    const std::size_t n = targets.size();
    std::vector<Bounds> bs;
    for (const GameObject* g : targets) bs.push_back(ObjectBounds(*g));
    std::vector<std::size_t> order(n);
    for (std::size_t i = 0; i < n; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return Comp(bs[x].center, a) < Comp(bs[y].center, a); });
    std::vector<float> delta(n, 0.0f);
    if (how == Spread::Centres) {
        const float c0 = Comp(bs[order.front()].center, a), c1 = Comp(bs[order.back()].center, a);
        for (std::size_t k = 0; k < n; ++k) {
            const float want = c0 + (c1 - c0) * (float)k / (float)(n - 1);
            delta[order[k]] = want - Comp(bs[order[k]].center, a);
        }
    } else {
        const float lo = Comp(bs[order.front()].Min(), a), hi = Comp(bs[order.back()].Max(), a);
        float sizes = 0.0f;
        for (const Bounds& b : bs) sizes += Comp(b.Max(), a) - Comp(b.Min(), a);
        const float gap = (hi - lo - sizes) / (float)(n - 1);
        float cursor = lo;
        for (std::size_t k = 0; k < n; ++k) {
            const Bounds& b = bs[order[k]];
            delta[order[k]] = cursor - Comp(b.Min(), a);
            cursor += (Comp(b.Max(), a) - Comp(b.Min(), a)) + gap;
        }
    }
    return Apply(ed, targets, delta, a, std::string("Distribute ") + AxisName(axis) + (how == Spread::Gaps ? " (equal gaps)" : " (equal spacing)"));
}

// ---- Mirror -----------------------------------------------------------------------------------------------------------
MirrorStats MirrorObject(GameObject& root, Axis plane, float planeValue) {
    MirrorStats st;
    const int axis = (int)plane;
    std::vector<GameObject*> all;
    CollectSubtree(&root, all);
    if (all.empty()) return st;
    Transform* t = root.transform;
    bool avoidZ = false;
    for (GameObject* g : all) if (g->GetComponent<Light>() || g->GetComponent<Camera>()) avoidZ = true;
    const int k = ChooseFlipAxis(t->Rotation(), axis, avoidZ);
    if (ParentNeedsShear(t->Parent())) st.approximate = 1;

    // Everything below the root keeps its place in the root's frame, flipped on axis k: local pose G * L * G.
    struct Local { Transform* t; Vec3 p; Quat q; };
    std::vector<Local> kids;
    for (GameObject* g : all) if (g != &root) kids.push_back({g->transform, g->transform->localPosition, g->transform->localRotation});

    Vec3 p = t->Position();
    SetComp(p, axis, 2.0f * planeValue - Comp(p, axis));
    float R[3][3];
    Rot3(t->Rotation(), R);
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
        R[i][j] *= (i == axis ? -1.0f : 1.0f) * (j == k ? -1.0f : 1.0f);   // F * R * G
    t->SetRotation(FromRot3(R));
    t->SetPosition(p);
    for (const Local& l : kids) {
        Vec3 lp = l.p;
        SetComp(lp, k, -Comp(lp, k));
        float M[3][3];
        Rot3(l.q, M);
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) M[i][j] *= (i == k ? -1.0f : 1.0f) * (j == k ? -1.0f : 1.0f);   // G * R * G
        l.t->localPosition = lp;
        l.t->localRotation = FromRot3(M);
    }

    // What is stored in each object's own frame, or as a world vector.
    for (GameObject* g : all) {
        if (auto* mr = g->GetComponent<MeshRenderer>()) {
            if (g->GetComponent<Terrain>() || mr->deformed || g->GetComponent<SkinnedMesh>()) {
                ++st.skipped;                                  // rebuilt every frame / a height field: the shape stays
            } else if (!mr->mesh.vertices.empty()) {
                if (SymmetricAcross(mr->mesh, k)) {
                    ++st.meshesLeftAsIs;
                } else {
                    FlipMesh(mr->mesh, k);
                    ++st.meshesFlipped;
                    if (auto* mm = g->GetComponent<MeshModifiers>()) mm->Refresh(true);
                    else RefreshSeamSplit(*mr);
                }
            }
        }
        if (auto* sp = g->GetComponent<SpriteRenderer>()) {
            if (k == 0) sp->flipX = !sp->flipX;
            else if (k == 1) sp->flipY = !sp->flipY;
        }
        if (auto* sl = g->GetComponent<Spline>()) {
            for (SplinePoint& pt : sl->points) {
                SetComp(pt.position, k, -Comp(pt.position, k));
                SetComp(pt.tangentIn, k, -Comp(pt.tangentIn, k));
                SetComp(pt.tangentOut, k, -Comp(pt.tangentOut, k));
                pt.roll = -pt.roll;
            }
        }
        for (Collider3D* c : g->GetComponents<Collider3D>()) SetComp(c->offset, axis, -Comp(c->offset, axis));   // a world vector
    }
    return st;
}

Result Mirror(EditorState& ed, Axis plane, MirrorAbout about, bool duplicate) {
    const Availability av = CanMirror(ed);
    if (!av.ok) { Result r; r.message = av.reason; return r; }
    const auto targets = Targets(Selection(ed));
    const int axis = (int)plane;
    float planeValue = 0.0f;
    std::string aboutName = "the world origin";
    if (about == MirrorAbout::SelectionCentre) { planeValue = Comp(SelectionCentre(targets), axis); aboutName = "the selection centre"; }
    else if (about == MirrorAbout::ActivePivot) {
        GameObject* at = ed.selected();
        if (!at || !at->transform) { Result r; r.message = "Mirror about the active object: there is no active object"; return r; }
        planeValue = Comp(at->transform->Position(), axis);
        aboutName = "'" + at->name + "'";
    }
    ed.PushUndo();
    std::vector<GameObject*> work;
    Result r;
    if (duplicate) {
        for (GameObject* g : targets) if (GameObject* c = ed.scene().Duplicate(*g)) work.push_back(c);
    } else {
        work = targets;
    }
    MirrorStats total;
    for (GameObject* g : work) {
        const MirrorStats s = MirrorObject(*g, plane, planeValue);
        total.skipped += s.skipped; total.approximate += s.approximate;
        total.meshesFlipped += s.meshesFlipped; total.meshesLeftAsIs += s.meshesLeftAsIs;
    }
    if (duplicate && !work.empty()) {
        ed.Select(work.front());
        for (std::size_t i = 1; i < work.size(); ++i) ed.ToggleSelect(work[i]);
        r.made = work;
    }
    ed.dirty = true;
    r.ok = true;
    r.changed = (int)work.size();
    r.skipped = total.skipped;
    r.approximate = total.approximate;
    r.message = std::string(duplicate ? "Mirrored copies of " : "Mirrored ") + std::to_string(work.size()) + " object" + (work.size() == 1 ? "" : "s") +
                " across the " + AxisName(plane) + " plane at " + AxisName(plane) + " = " + Num(planeValue) + " (" + aboutName + ")";
    if (total.skipped) r.message += "; " + std::to_string(total.skipped) + " mesh" + (total.skipped == 1 ? "" : "es") + " rebuilt every frame (or terrain) kept their shape";
    if (total.approximate) r.message += "; " + std::to_string(total.approximate) + " under a turned, unevenly scaled parent: approximate";
    return r;
}

// ---- Multi-select gizmo drags -------------------------------------------------------------------------------------------
Vec3 PivotPoint(const std::vector<GameObject*>& targets, const GameObject* active, GizmoPivot mode) {
    if (mode == GizmoPivot::Center && targets.size() > 1) {
        Vec3 sum{0, 0, 0};
        int n = 0;
        for (const GameObject* g : targets) if (g && g->transform) { sum += g->transform->Position(); ++n; }
        if (n > 0) return sum / (float)n;
    }
    if (active && active->transform) return active->transform->Position();
    return targets.empty() || !targets[0]->transform ? Vec3{} : targets[0]->transform->Position();
}

std::vector<GameObject*> Followers(const std::vector<GameObject*>& selection, const GameObject* primary) {
    std::vector<GameObject*> out;
    if (!primary) return out;
    for (GameObject* g : Targets(selection)) {
        if (g == primary || primary->IsSelfOrDescendantOf(g) || g->IsSelfOrDescendantOf(primary)) continue;
        out.push_back(g);
    }
    return out;
}

void MoveFollowers(const std::vector<GameObject*>& followers, const Vec3& worldDelta) {
    for (GameObject* g : followers) if (g && g->transform) MoveWorld(g, worldDelta);
}

void RotateAbout(const std::vector<GameObject*>& objects, const Vec3& pivot, const Vec3& axis, float degrees) {
    for (GameObject* g : objects) if (g && g->transform) g->transform->RotateAround(pivot, axis, degrees);
}

void ScaleFollowers(const std::vector<GameObject*>& followers, const Vec3& pivot, const Vec3 axes[3], const Vec3& ratio) {
    const float r[3] = {ratio.x, ratio.y, ratio.z};
    for (GameObject* g : followers) {
        if (!g || !g->transform) continue;
        const Vec3 off = g->transform->Position() - pivot;
        Vec3 stretched{0, 0, 0};
        for (int i = 0; i < 3; ++i) stretched += axes[i] * (Vec3::Dot(off, axes[i]) * r[i]);
        g->transform->SetPosition(pivot + stretched);
        if (g->GetComponent<Camera>()) continue;      // a camera is never scaled
        Vec3& s = g->transform->localScale;
        s.x = std::max(0.01f, s.x * ratio.x);
        s.y = std::max(0.01f, s.y * ratio.y);
        s.z = std::max(0.01f, s.z * ratio.z);
    }
}

}  // namespace okay::editor::seltools
