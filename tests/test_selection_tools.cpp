// Align / Distribute / Mirror and the multi-select gizmo helpers (editor/SelectionTools, ImGui-free):
// align to min / centre / max / the active object on every axis, equal-spacing and equal-gap distribute (selection order
// does not matter), mirror across a world plane as a duplicate or in place (positions, rotations, scales, winding checked
// on the world-space triangles, a mirrored mirror returns to the original), nested children and parents, prefab instances,
// lights / splines / colliders / sprites, the settings file, availability reasons, one undo step per action, and
// what a Move / Rotate / Scale gizmo drag does to the rest of the selection.
#include "test_framework.hpp"
#include "EditorState.hpp"
#include "SelectionTools.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Spline.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace okay;
using namespace okay::editor;
using namespace okay::editor::seltools;

static bool Near(const Vec3& a, const Vec3& b, float e = 1e-3f) { return (a - b).Magnitude() <= e; }
static float C(const Vec3& v, int a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); }

static GameObject* Cube(Scene& s, const std::string& name, Vec3 pos, Vec3 size, GameObject* parent = nullptr) {
    GameObject* g = s.CreateGameObject(name);
    if (parent) g->transform->SetParent(parent->transform, false);
    g->transform->SetPosition(pos);
    auto* mr = g->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube();
    g->transform->localScale = size;
    return g;
}

/// A mesh with no symmetry in any axis (so mirroring has to change its data).
static Mesh Lopsided() {
    Mesh m;
    m.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 2, 0}, {0, 0, 3}};
    m.triangles = {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};
    return m;
}
static GameObject* Odd(Scene& s, const std::string& name, Vec3 pos, Vec3 euler, Vec3 scale, GameObject* parent = nullptr) {
    GameObject* g = s.CreateGameObject(name);
    if (parent) g->transform->SetParent(parent->transform, false);
    g->transform->localPosition = pos;
    g->transform->localRotation = Quat::Euler(euler);
    g->transform->localScale = scale;
    g->AddComponent<MeshRenderer>()->mesh = Lopsided();
    return g;
}

static void SelectAll(EditorState& ed, std::vector<GameObject*> v) {
    ed.Select(v[0]);
    for (std::size_t i = 1; i < v.size(); ++i) ed.ToggleSelect(v[i]);   // the last one is the active object
}

static Bounds B(const GameObject* g) { return ObjectBounds(*g); }

// ---- world-space triangles: the geometry as drawn -------------------------------------------------------------------
struct Tri { Vec3 p[3]; };
static std::vector<Tri> WorldTris(const GameObject& g) {
    std::vector<Tri> out;
    const auto* mr = g.GetComponent<MeshRenderer>();
    if (!mr) return out;
    const Mat4 m = g.transform->LocalToWorldMatrix();
    const Mesh& me = mr->mesh;
    for (int t = 0; t < me.TriangleCount(); ++t) {
        Tri tr;
        for (int k = 0; k < 3; ++k) tr.p[k] = m.MultiplyPoint(me.vertices[me.triangles[t * 3 + k]]);
        out.push_back(tr);
    }
    return out;
}
static Vec3 Reflect(Vec3 p, int axis, float plane) {
    float* c = axis == 0 ? &p.x : (axis == 1 ? &p.y : &p.z);
    *c = 2.0f * plane - *c;
    return p;
}
/// Is `got` the mirror image of `before` (same triangles, reflected, facing the other way)? Matched as sets.
static bool IsMirrorImage(const std::vector<Tri>& before, const std::vector<Tri>& got, int axis, float plane) {
    if (before.size() != got.size()) return false;
    for (const Tri& b : before) {
        const Vec3 e[3] = {Reflect(b.p[0], axis, plane), Reflect(b.p[2], axis, plane), Reflect(b.p[1], axis, plane)};   // winding reversed
        bool found = false;
        for (const Tri& g : got) {
            for (int s = 0; s < 3 && !found; ++s)
                if (Near(g.p[0], e[s]) && Near(g.p[1], e[(s + 1) % 3]) && Near(g.p[2], e[(s + 2) % 3])) found = true;
            if (found) break;
        }
        if (!found) return false;
    }
    return true;
}
static std::vector<std::vector<Tri>> SubtreeTris(GameObject* root) {
    std::vector<std::vector<Tri>> out;
    std::vector<GameObject*> st{root};
    while (!st.empty()) {
        GameObject* g = st.back(); st.pop_back();
        out.push_back(WorldTris(*g));
        for (Transform* c : g->transform->Children()) st.push_back(c->gameObject);
    }
    return out;
}
static bool AllSubtreesMirrored(const std::vector<std::vector<Tri>>& before, GameObject* root, int axis, float plane) {
    const auto after = SubtreeTris(root);
    if (before.size() != after.size()) return false;
    for (std::size_t i = 0; i < before.size(); ++i) if (!IsMirrorImage(before[i], after[i], axis, plane)) return false;
    return true;
}

// ---- tests ------------------------------------------------------------------------------------------------------------
static void TestTargetsAndBounds() {
    EditorState ed;
    Scene& s = ed.scene();
    GameObject* a = Cube(s, "A", {1, 2, 3}, {2, 2, 2});
    GameObject* kid = Cube(s, "Kid", {1, 5, 3}, {1, 1, 1}, a);
    GameObject* b = Cube(s, "B", {9, 0, 0}, {1, 1, 1});
    auto t = Targets({a, kid, b, a, nullptr});
    CHECK(t.size() == 2 && t[0] == a && t[1] == b);                       // the child moves with its parent; no repeats, no nulls
    Bounds ba = B(a);                                                      // a cube 2 wide, with the kid (scaled by the parent to 2 wide, at y 5) above it
    CHECK(Near(ba.Min(), {0, 1, 2}) && Near(ba.Max(), {2, 6, 4}));
    GameObject* col = s.CreateGameObject("Col");                           // collider only
    col->transform->SetPosition({0, 0, 0});
    auto* bc = col->AddComponent<BoxCollider3D>(); bc->size = {2, 4, 6}; bc->offset = {1, 0, 0};
    Bounds bcb = B(col);
    CHECK(Near(bcb.Min(), {0, -2, -3}) && Near(bcb.Max(), {2, 2, 3}));
    GameObject* e = s.CreateGameObject("Empty");                           // nothing: a point at its position
    e->transform->SetPosition({4, 5, 6});
    CHECK(Near(B(e).center, {4, 5, 6}) && Near(B(e).Size(), {0, 0, 0}));
    CHECK(Near(SelectionCentre({a, b}), {4.75f, 2.75f, 1.75f}));           // centre of the union of both boxes
    // Availability and its reasons.
    ed.Select(a);
    CHECK(!CanAlign(ed).ok && CanAlign(ed).reason.find("2 or more") != std::string::npos);
    CHECK(CanMirror(ed).ok);
    SelectAll(ed, {a, b});
    CHECK(CanAlign(ed).ok && !CanDistribute(ed).ok && CanDistribute(ed).reason.find("3 or more") != std::string::npos);
    SelectAll(ed, {a, kid});
    CHECK(!CanAlign(ed).ok && CanAlign(ed).reason.find("inside each other") != std::string::npos);
    ed.Select(nullptr);
    CHECK(!CanMirror(ed).ok);
}

static void TestAlign() {
    for (int axis = 0; axis < 3; ++axis) {
        EditorState ed;
        Scene& s = ed.scene();
        GameObject* a = Cube(s, "A", {0, 0, 0}, {2, 2, 2});
        GameObject* b = Cube(s, "B", {5, 7, -3}, {4, 1, 6});
        GameObject* c = Cube(s, "C", {-6, 2, 9}, {1, 3, 1});
        SelectAll(ed, {a, b, c});                 // C is the active object
        const Axis ax = (Axis)axis;
        Bounds all; all.Encapsulate(B(a)); all.Encapsulate(B(b)); all.Encapsulate(B(c));
        const Vec3 posA = a->transform->Position();
        const int d0 = ed.UndoDepth();
        Result r = Align(ed, ax, Edge::Min, AlignTo::Selection);
        CHECK(r.ok && r.changed >= 1 && ed.UndoDepth() == d0 + 1);
        for (GameObject* g : {a, b, c}) CHECK_NEAR(C(B(g).Min(), axis), C(all.Min(), axis), 1e-4);
        // The other two axes did not move.
        for (int o = 0; o < 3; ++o) if (o != axis) CHECK_NEAR(C(a->transform->Position(), o), C(posA, o), 1e-5);
        // Centre of the original selection box.
        EditorState ed2; Scene& s2 = ed2.scene();
        GameObject* a2 = Cube(s2, "A", {0, 0, 0}, {2, 2, 2});
        GameObject* b2 = Cube(s2, "B", {5, 7, -3}, {4, 1, 6});
        GameObject* c2 = Cube(s2, "C", {-6, 2, 9}, {1, 3, 1});
        SelectAll(ed2, {a2, b2, c2});
        CHECK(Align(ed2, ax, Edge::Center, AlignTo::Selection).ok);
        for (GameObject* g : {a2, b2, c2}) CHECK_NEAR(C(B(g).center, axis), C(all.center, axis), 1e-4);
        EditorState ed3; Scene& s3 = ed3.scene();
        GameObject* a3 = Cube(s3, "A", {0, 0, 0}, {2, 2, 2});
        GameObject* b3 = Cube(s3, "B", {5, 7, -3}, {4, 1, 6});
        GameObject* c3 = Cube(s3, "C", {-6, 2, 9}, {1, 3, 1});
        SelectAll(ed3, {a3, b3, c3});
        CHECK(Align(ed3, ax, Edge::Max, AlignTo::Selection).ok);
        for (GameObject* g : {a3, b3, c3}) CHECK_NEAR(C(B(g).Max(), axis), C(all.Max(), axis), 1e-4);
        // To the active object (C): C stays, the others line up with C's edge.
        EditorState ed4; Scene& s4 = ed4.scene();
        GameObject* a4 = Cube(s4, "A", {0, 0, 0}, {2, 2, 2});
        GameObject* b4 = Cube(s4, "B", {5, 7, -3}, {4, 1, 6});
        GameObject* c4 = Cube(s4, "C", {-6, 2, 9}, {1, 3, 1});
        SelectAll(ed4, {a4, b4, c4});
        const Vec3 cBefore = c4->transform->Position();
        const float cMax = C(B(c4).Max(), axis), cMin = C(B(c4).Min(), axis), cMid = C(B(c4).center, axis);
        CHECK(Align(ed4, ax, Edge::Max, AlignTo::Active).ok);
        CHECK(Near(c4->transform->Position(), cBefore));
        CHECK_NEAR(C(B(a4).Max(), axis), cMax, 1e-4); CHECK_NEAR(C(B(b4).Max(), axis), cMax, 1e-4);
        CHECK(Align(ed4, ax, Edge::Min, AlignTo::Active).ok);
        CHECK_NEAR(C(B(a4).Min(), axis), cMin, 1e-4); CHECK_NEAR(C(B(b4).Min(), axis), cMin, 1e-4);
        CHECK(Align(ed4, ax, Edge::Center, AlignTo::Active).ok);
        CHECK_NEAR(C(B(a4).center, axis), cMid, 1e-4); CHECK_NEAR(C(B(b4).center, axis), cMid, 1e-4);
    }
    // Already aligned: ok, nothing changes and no undo step.
    EditorState ed;
    GameObject* a = Cube(ed.scene(), "A", {0, 1, 0}, {2, 2, 2});
    GameObject* b = Cube(ed.scene(), "B", {5, 1, 0}, {2, 2, 2});
    SelectAll(ed, {a, b});
    const int d0 = ed.UndoDepth();
    Result r = Align(ed, Axis::Y, Edge::Center, AlignTo::Selection);
    CHECK(r.ok && r.changed == 0 && ed.UndoDepth() == d0);
    // Refused: one object.
    ed.Select(a);
    r = Align(ed, Axis::X, Edge::Min, AlignTo::Selection);
    CHECK(!r.ok && ed.UndoDepth() == d0 && !r.message.empty());
}

static void TestAlignRotatedAndParented() {
    EditorState ed;
    Scene& s = ed.scene();
    // A turned box: its world box is the box around the turned corners, not the local one.
    GameObject* a = Cube(s, "A", {0, 0, 0}, {2, 2, 2});
    a->transform->SetRotation(Quat::Euler(0, 45, 0));
    GameObject* b = Cube(s, "B", {10, 0, 0}, {2, 2, 2});
    SelectAll(ed, {a, b});
    CHECK(Align(ed, Axis::X, Edge::Min, AlignTo::Selection).ok);
    CHECK_NEAR(C(B(b).Min(), 0), C(B(a).Min(), 0), 1e-4);
    CHECK(C(B(a).Min(), 0) < -1.2f);                                       // sqrt(2) wide half-extent of the turned box
    // Objects under scaled, turned parents move in world space.
    GameObject* p = s.CreateGameObject("P");
    p->transform->SetPosition({0, 0, 20});
    p->transform->localRotation = Quat::Euler(0, 90, 0);
    p->transform->localScale = {2, 2, 2};
    GameObject* k = Cube(s, "K", {1, 0, 0}, {1, 1, 1}, p);
    GameObject* m = Cube(s, "M", {3, 0, 25}, {1, 1, 1});
    SelectAll(ed, {k, m});
    CHECK(Align(ed, Axis::Z, Edge::Center, AlignTo::Selection).ok);
    CHECK_NEAR(C(B(k).center, 2), C(B(m).center, 2), 1e-3);
    CHECK_NEAR(k->transform->LossyScale().x, 2.0f, 1e-4);                  // scale untouched
}

static void TestDistribute() {
    for (int axis = 0; axis < 3; ++axis) {
        // Equal centre spacing, selected out of order.
        EditorState ed;
        Scene& s = ed.scene();
        GameObject* g[5];
        const float xs[5] = {0, 1, 7, 8, 20};
        const float sz[5] = {2, 4, 1, 6, 2};
        for (int i = 0; i < 5; ++i) {
            Vec3 pos{0, 0, 0}, size{1, 1, 1};
            if (axis == 0) { pos.x = xs[i]; size.x = sz[i]; } else if (axis == 1) { pos.y = xs[i]; size.y = sz[i]; } else { pos.z = xs[i]; size.z = sz[i]; }
            g[i] = Cube(s, "G" + std::to_string(i), pos, size);
        }
        SelectAll(ed, {g[3], g[0], g[4], g[2], g[1]});
        const int d0 = ed.UndoDepth();
        Result r = Distribute(ed, (Axis)axis, Spread::Centres);
        CHECK(r.ok && ed.UndoDepth() == d0 + 1);
        CHECK_NEAR(C(B(g[0]).center, axis), 0.0f, 1e-4);                   // the ends stay
        CHECK_NEAR(C(B(g[4]).center, axis), 20.0f, 1e-4);
        for (int i = 1; i < 5; ++i) CHECK_NEAR(C(B(g[i]).center, axis) - C(B(g[i - 1]).center, axis), 5.0f, 1e-3);
    }
    for (int axis = 0; axis < 3; ++axis) {
        // Equal gaps between bounds.
        EditorState ed;
        Scene& s = ed.scene();
        GameObject* g[4];
        const float xs[4] = {0, 3, 4, 30};
        const float sz[4] = {2, 6, 1, 4};
        for (int i = 0; i < 4; ++i) {
            Vec3 pos{0, 0, 0}, size{1, 1, 1};
            if (axis == 0) { pos.x = xs[i]; size.x = sz[i]; } else if (axis == 1) { pos.y = xs[i]; size.y = sz[i]; } else { pos.z = xs[i]; size.z = sz[i]; }
            g[i] = Cube(s, "G" + std::to_string(i), pos, size);
        }
        SelectAll(ed, {g[2], g[3], g[0], g[1]});
        const float lo = C(B(g[0]).Min(), axis), hi = C(B(g[3]).Max(), axis);
        CHECK(Distribute(ed, (Axis)axis, Spread::Gaps).ok);
        CHECK_NEAR(C(B(g[0]).Min(), axis), lo, 1e-4);                      // the outer edges stay
        CHECK_NEAR(C(B(g[3]).Max(), axis), hi, 1e-4);
        const float gap01 = C(B(g[1]).Min(), axis) - C(B(g[0]).Max(), axis);
        const float gap12 = C(B(g[2]).Min(), axis) - C(B(g[1]).Max(), axis);
        const float gap23 = C(B(g[3]).Min(), axis) - C(B(g[2]).Max(), axis);
        CHECK_NEAR(gap01, gap12, 1e-3); CHECK_NEAR(gap12, gap23, 1e-3);
        CHECK_NEAR(gap01, (hi - lo - (2 + 6 + 1 + 4)) / 3.0f, 1e-3);
    }
    // Too few objects: refused, with the reason, and no undo step.
    EditorState ed;
    GameObject* a = Cube(ed.scene(), "A", {0, 0, 0}, {1, 1, 1});
    GameObject* b = Cube(ed.scene(), "B", {4, 0, 0}, {1, 1, 1});
    SelectAll(ed, {a, b});
    const int d0 = ed.UndoDepth();
    Result r = Distribute(ed, Axis::X, Spread::Gaps);
    CHECK(!r.ok && ed.UndoDepth() == d0 && r.message.find("3 or more") != std::string::npos);
    // Three already evenly placed: nothing changes.
    GameObject* c = Cube(ed.scene(), "C", {8, 0, 0}, {1, 1, 1});
    SelectAll(ed, {a, b, c});
    r = Distribute(ed, Axis::X, Spread::Centres);
    CHECK(r.ok && r.changed == 0 && ed.UndoDepth() == d0);
    // Collider-only objects and bare points are distributed by what they have.
    GameObject* e1 = ed.scene().CreateGameObject("E1"); e1->transform->SetPosition({0, 0, 0});
    GameObject* e2 = ed.scene().CreateGameObject("E2"); e2->transform->SetPosition({1, 0, 0});
    GameObject* e3 = ed.scene().CreateGameObject("E3"); e3->transform->SetPosition({10, 0, 0});
    SelectAll(ed, {e1, e2, e3});
    CHECK(Distribute(ed, Axis::X, Spread::Gaps).ok);
    CHECK_NEAR(e2->transform->Position().x, 5.0f, 1e-4);
}

static void TestMirrorGeometry() {
    // Every axis x every pivot x duplicate / in place, on a lopsided mesh that is turned and unevenly scaled.
    for (int axis = 0; axis < 3; ++axis)
        for (int about = 0; about < 3; ++about)
            for (int dup = 0; dup < 2; ++dup) {
                EditorState ed;
                Scene& s = ed.scene();
                GameObject* a = Odd(s, "A", {1, 2, 3}, {20, 35, 50}, {1, 2, 3});
                GameObject* b = Odd(s, "B", {-4, 1, 5}, {0, 0, 0}, {1, 1, 1});
                SelectAll(ed, {a, b});
                const auto ta = WorldTris(*a), tb = WorldTris(*b);
                float plane = 0.0f;
                if (about == 0) plane = C(SelectionCentre({a, b}), axis);
                else if (about == 1) plane = C(b->transform->Position(), axis);   // b is the active object
                const Vec3 sa = a->transform->localScale;
                Result r = Mirror(ed, (Axis)axis, (MirrorAbout)about, dup != 0);
                CHECK(r.ok && r.changed == 2);
                GameObject *ma = a, *mb = b;
                if (dup) {
                    CHECK(r.made.size() == 2 && ed.MultiSelection().size() == 2);
                    ma = r.made[0]; mb = r.made[1];
                    CHECK(ma != a && mb != b);
                    // The originals did not change.
                    CHECK(Near(a->transform->Position(), {1, 2, 3}));
                    const auto now = WorldTris(*a);
                    for (std::size_t t = 0; t < ta.size(); ++t) for (int k = 0; k < 3; ++k) CHECK(Near(now[t].p[k], ta[t].p[k], 1e-4f));
                    CHECK(Near(a->transform->localScale, sa));
                }
                CHECK(IsMirrorImage(ta, WorldTris(*ma), axis, plane));
                CHECK(IsMirrorImage(tb, WorldTris(*mb), axis, plane));
                CHECK(Near(ma->transform->localScale, sa, 1e-5f));                // scale is never negative or changed
                CHECK(ma->transform->localScale.x > 0 && ma->transform->localScale.y > 0 && ma->transform->localScale.z > 0);
                const float n = ma->transform->Rotation().x * ma->transform->Rotation().x + ma->transform->Rotation().y * ma->transform->Rotation().y +
                                ma->transform->Rotation().z * ma->transform->Rotation().z + ma->transform->Rotation().w * ma->transform->Rotation().w;
                CHECK_NEAR(n, 1.0f, 1e-4);
                // The position is reflected.
                Vec3 want = Reflect({1, 2, 3}, axis, plane);
                CHECK(Near(ma->transform->Position(), want));
            }
}

static void TestMirrorTwiceIsIdentity() {
    for (int axis = 0; axis < 3; ++axis)
        for (int about = 0; about < 3; ++about) {
            EditorState ed;
            Scene& s = ed.scene();
            GameObject* a = Odd(s, "A", {1, 2, 3}, {20, 35, 50}, {1, 2, 3});
            GameObject* kid = Odd(s, "Kid", {0.5f, 1, -2}, {10, 80, -30}, {2, 1, 0.5f}, a);
            GameObject* b = Odd(s, "B", {-4, 1, 5}, {90, 0, 45}, {1, 1, 1});
            SelectAll(ed, {a, b});
            const Vec3 pa = a->transform->Position(), pk = kid->transform->localPosition, pb = b->transform->Position();
            const Quat qa = a->transform->Rotation(), qk = kid->transform->localRotation, qb = b->transform->Rotation();
            const auto triA = SubtreeTris(a);
            const auto triB = WorldTris(*b);
            const Mesh meshA = a->GetComponent<MeshRenderer>()->mesh;
            const Mesh meshK = kid->GetComponent<MeshRenderer>()->mesh;
            CHECK(Mirror(ed, (Axis)axis, (MirrorAbout)about, false).ok);
            CHECK(Mirror(ed, (Axis)axis, (MirrorAbout)about, false).ok);
            CHECK(Near(a->transform->Position(), pa, 1e-3f) && Near(kid->transform->localPosition, pk, 1e-3f) && Near(b->transform->Position(), pb, 1e-3f));
            auto sameRot = [](const Quat& x, const Quat& y) { return std::fabs(x.x * y.x + x.y * y.y + x.z * y.z + x.w * y.w) > 1.0f - 1e-4f; };
            CHECK(sameRot(a->transform->Rotation(), qa) && sameRot(kid->transform->localRotation, qk) && sameRot(b->transform->Rotation(), qb));
            CHECK(Near(a->transform->localScale, {1, 2, 3}, 1e-5f) && Near(kid->transform->localScale, {2, 1, 0.5f}, 1e-5f));
            const Mesh& m2 = a->GetComponent<MeshRenderer>()->mesh;
            CHECK(m2.vertices.size() == meshA.vertices.size() && m2.triangles == meshA.triangles);
            for (std::size_t i = 0; i < meshA.vertices.size(); ++i) CHECK(Near(m2.vertices[i], meshA.vertices[i], 1e-5f));
            CHECK(kid->GetComponent<MeshRenderer>()->mesh.triangles == meshK.triangles);
            const auto triA2 = SubtreeTris(a);
            for (std::size_t i = 0; i < triA.size(); ++i)
                for (std::size_t t = 0; t < triA[i].size(); ++t)
                    for (int k = 0; k < 3; ++k) CHECK(Near(triA[i][t].p[k], triA2[i][t].p[k], 2e-3f));
            const auto triB2 = WorldTris(*b);
            for (std::size_t t = 0; t < triB.size(); ++t)
                for (int k = 0; k < 3; ++k) CHECK(Near(triB[t].p[k], triB2[t].p[k], 2e-3f));
        }
}

static void TestMirrorHierarchy() {
    EditorState ed;
    Scene& s = ed.scene();
    // A parent holding a turned, scaled child holding a grandchild; mirrored as one.
    GameObject* p = Odd(s, "P", {2, 1, 0}, {0, 30, 0}, {1, 1, 1});
    GameObject* c = Odd(s, "C", {1, 2, 1}, {15, 40, 70}, {2, 1, 3}, p);
    GameObject* gc = Odd(s, "GC", {-1, 0.5f, 2}, {0, 90, 0}, {1, 2, 1}, c);
    ed.Select(p);
    const auto before = SubtreeTris(p);
    CHECK(Mirror(ed, Axis::X, MirrorAbout::WorldOrigin, false).ok);
    CHECK(AllSubtreesMirrored(before, p, 0, 0.0f));
    (void)gc;
    // A selected child of a selected parent is not mirrored a second time.
    EditorState ed2;
    Scene& s2 = ed2.scene();
    GameObject* p2 = Odd(s2, "P", {2, 1, 0}, {0, 30, 0}, {1, 1, 1});
    GameObject* c2 = Odd(s2, "C", {1, 2, 1}, {15, 40, 70}, {2, 1, 3}, p2);
    SelectAll(ed2, {p2, c2});
    const auto before2 = SubtreeTris(p2);
    Result r = Mirror(ed2, Axis::Z, MirrorAbout::WorldOrigin, false);
    CHECK(r.ok && r.changed == 1);
    CHECK(AllSubtreesMirrored(before2, p2, 2, 0.0f));
    // A selected object under an unselected, scaled and turned (by quarter turns) parent.
    EditorState ed3;
    Scene& s3 = ed3.scene();
    GameObject* root = s3.CreateGameObject("Root");
    root->transform->SetPosition({5, 0, 5});
    root->transform->localRotation = Quat::Euler(0, 90, 0);
    root->transform->localScale = {2, 3, 4};
    GameObject* k = Odd(s3, "K", {1, 1, 1}, {0, 0, 0}, {1, 1, 1}, root);
    ed3.Select(k);
    const auto beforeK = WorldTris(*k);
    r = Mirror(ed3, Axis::X, MirrorAbout::WorldOrigin, false);
    CHECK(r.ok && r.approximate == 0);
    CHECK(IsMirrorImage(beforeK, WorldTris(*k), 0, 0.0f));
    // The same under a parent that is turned by 30 degrees and scaled unevenly: a shear, flagged as approximate.
    EditorState ed4;
    Scene& s4 = ed4.scene();
    GameObject* root4 = s4.CreateGameObject("Root");
    root4->transform->localRotation = Quat::Euler(0, 30, 0);
    root4->transform->localScale = {2, 3, 4};
    GameObject* k4 = Odd(s4, "K", {1, 1, 1}, {0, 0, 0}, {1, 1, 1}, root4);
    ed4.Select(k4);
    r = Mirror(ed4, Axis::X, MirrorAbout::WorldOrigin, false);
    CHECK(r.ok && r.approximate == 1 && r.message.find("approximate") != std::string::npos);
}

static void TestMirrorDuplicateAndUndo() {
    EditorState ed;
    Scene& s = ed.scene();
    GameObject* a = Odd(s, "A", {3, 1, 0}, {0, 0, 0}, {1, 1, 1});
    GameObject* kid = Odd(s, "Kid", {0, 1, 0}, {0, 0, 0}, {1, 1, 1}, a);
    (void)kid;
    ed.Select(a);
    const std::size_t n0 = s.Objects().size();
    const int d0 = ed.UndoDepth();
    Result r = Mirror(ed, Axis::X, MirrorAbout::WorldOrigin, true);
    CHECK(r.ok && r.made.size() == 1 && ed.UndoDepth() == d0 + 1 && s.Objects().size() == n0 + 2);
    CHECK(ed.selected() == r.made[0] && ed.MultiSelection().size() == 1);
    CHECK(Near(r.made[0]->transform->Position(), {-3, 1, 0}));
    CHECK(Near(a->transform->Position(), {3, 1, 0}));                       // the original stays
    CHECK(r.made[0]->name != "A");                                           // a free name
    CHECK(ed.Undo());                                                         // ONE step takes the copies away again
    CHECK(s.Objects().size() == n0);
    CHECK(ed.scene().Find("A") && Near(ed.scene().Find("A")->transform->Position(), {3, 1, 0}));
    CHECK(ed.Redo());
    CHECK(ed.scene().Objects().size() == n0 + 2);
    // In place: one step, restores the mesh and the transform.
    EditorState e2;
    GameObject* b = Odd(e2.scene(), "B", {3, 1, 0}, {10, 20, 30}, {1, 2, 3});
    e2.Select(b);
    const Mesh mesh0 = b->GetComponent<MeshRenderer>()->mesh;
    const Quat q0 = b->transform->Rotation();
    const int dd = e2.UndoDepth();
    CHECK(Mirror(e2, Axis::Y, MirrorAbout::WorldOrigin, false).ok);
    CHECK(e2.UndoDepth() == dd + 1);
    CHECK(e2.Undo());
    GameObject* back = e2.scene().Find("B");
    CHECK(back && Near(back->transform->Position(), {3, 1, 0}) && Near(back->GetComponent<MeshRenderer>()->mesh.vertices[3], mesh0.vertices[3]));
    CHECK(std::fabs(back->transform->Rotation().w - q0.w) < 1e-4f);
    CHECK(e2.Redo());
    CHECK(Near(e2.scene().Find("B")->transform->Position(), {3, -1, 0}));
    // Undo of Align and Distribute: one step each.
    EditorState e3;
    GameObject* x = Cube(e3.scene(), "X", {0, 0, 0}, {1, 1, 1});
    GameObject* y = Cube(e3.scene(), "Y", {3, 4, 0}, {1, 1, 1});
    GameObject* z = Cube(e3.scene(), "Z", {10, 1, 0}, {1, 1, 1});
    SelectAll(e3, {x, y, z});
    const int d3 = e3.UndoDepth();
    CHECK(Align(e3, Axis::Y, Edge::Center, AlignTo::Selection).ok);
    CHECK(Distribute(e3, Axis::X, Spread::Centres).ok);
    CHECK(e3.UndoDepth() == d3 + 2);
    CHECK(e3.Undo());
    CHECK(e3.Undo());
    CHECK(Near(e3.scene().Find("Y")->transform->Position(), {3, 4, 0}) && Near(e3.scene().Find("Z")->transform->Position(), {10, 1, 0}));
    CHECK(e3.Redo() && e3.Redo());
    CHECK_NEAR(e3.scene().Find("Y")->transform->Position().y, e3.scene().Find("X")->transform->Position().y, 1e-4);
}

static void TestMirrorSymmetricMeshAndSpecials() {
    EditorState ed;
    Scene& s = ed.scene();
    // A cube is already symmetric: the mesh data stays (and keeps its name); the object still ends up in the right place.
    GameObject* cube = Cube(s, "Cube", {2, 0, 0}, {1, 2, 3});
    ed.Select(cube);
    CHECK(Mirror(ed, Axis::X, MirrorAbout::WorldOrigin, false).ok);
    CHECK(Near(cube->transform->Position(), {-2, 0, 0}) && cube->GetComponent<MeshRenderer>()->mesh.name == "Cube");
    CHECK(Near(cube->transform->localScale, {1, 2, 3}));
    // A turned, unevenly scaled cube: the eight corners in the world are the mirror image of the eight before.
    GameObject* tc = Cube(s, "TurnedCube", {1, 2, 3}, {1, 2, 3});
    tc->transform->localRotation = Quat::Euler(20, 35, 50);
    auto corners = [&](GameObject* g) {
        std::vector<Vec3> out;
        const Mat4 m = g->transform->LocalToWorldMatrix();
        for (const Vec3& v : g->GetComponent<MeshRenderer>()->mesh.vertices) out.push_back(m.MultiplyPoint(v));
        return out;
    };
    const auto c0 = corners(tc);
    ed.Select(tc);
    for (int axis = 0; axis < 3; ++axis) {
        const auto cb = corners(tc);
        CHECK(Mirror(ed, (Axis)axis, MirrorAbout::WorldOrigin, false).ok);
        const auto ca = corners(tc);
        for (const Vec3& e : cb) {
            const Vec3 want = Reflect(e, axis, 0.0f);
            bool found = false;
            for (const Vec3& g : ca) if (Near(g, want)) found = true;
            CHECK(found);
        }
        CHECK(tc->GetComponent<MeshRenderer>()->mesh.name == "Cube");
    }
    (void)c0;
    // Colliders: the offset is a world vector, so it is reflected across the plane.
    GameObject* col = s.CreateGameObject("Col");
    col->transform->SetPosition({1, 0, 0});
    auto* bc = col->AddComponent<BoxCollider3D>(); bc->offset = {0.5f, 2, 0};
    ed.Select(col);
    CHECK(Mirror(ed, Axis::X, MirrorAbout::WorldOrigin, false).ok);
    CHECK(Near(bc->WorldCenter(), {-1.5f, 2, 0}));                           // reflect(1 + 0.5) = -1.5
    // A directional light keeps pointing along the mirror image of its direction.
    GameObject* lg = s.CreateGameObject("Sun");
    lg->AddComponent<Light>();
    lg->transform->localRotation = Quat::Euler(40, 25, 0);
    lg->transform->SetPosition({4, 5, 6});
    const Vec3 fwd = lg->transform->Forward(), up = lg->transform->Up();
    ed.Select(lg);
    for (int axis = 0; axis < 3; ++axis) {
        CHECK(Mirror(ed, (Axis)axis, MirrorAbout::WorldOrigin, false).ok);
        Vec3 f = Reflect(fwd, axis, 0.0f) - Reflect({0, 0, 0}, axis, 0.0f);
        (void)f;
        CHECK(Mirror(ed, (Axis)axis, MirrorAbout::WorldOrigin, false).ok);   // and back
        CHECK(Near(lg->transform->Forward(), fwd, 1e-3f) && Near(lg->transform->Up(), up, 1e-3f));
    }
    CHECK(Mirror(ed, Axis::Z, MirrorAbout::WorldOrigin, false).ok);
    CHECK(Near(lg->transform->Forward(), {fwd.x, fwd.y, -fwd.z}, 1e-3f));    // the direction is the true mirror image
    // A spline: its points are in the object's frame.
    GameObject* sp = s.CreateGameObject("Path");
    auto* spl = sp->AddComponent<Spline>();
    spl->AddPoint({1, 0, 0}); spl->AddPoint({2, 0, 5});
    ed.Select(sp);
    CHECK(Mirror(ed, Axis::X, MirrorAbout::WorldOrigin, false).ok);
    // Whatever axis was flipped, the curve in the world is the mirror image: its first point reads -1 along x.
    CHECK(sp->transform->LocalToWorldMatrix().MultiplyPoint(spl->points[0].position).x < -0.99f);
    CHECK(sp->transform->LocalToWorldMatrix().MultiplyPoint(spl->points[1].position).z > 4.99f);
    // A sprite flips.
    GameObject* spr = s.CreateGameObject("Spr");
    auto* sr = spr->AddComponent<SpriteRenderer>();
    sr->flipX = false;
    spr->transform->SetPosition({3, 0, 0});
    ed.Select(spr);
    CHECK(Mirror(ed, Axis::X, MirrorAbout::WorldOrigin, false).ok);
    CHECK(sr->flipX && Near(spr->transform->Position(), {-3, 0, 0}));
    // A mesh that is rebuilt every frame is counted, not mangled.
    GameObject* def = Odd(s, "Def", {1, 0, 0}, {0, 0, 0}, {1, 1, 1});
    def->GetComponent<MeshRenderer>()->deformed = true;
    const Mesh dm = def->GetComponent<MeshRenderer>()->mesh;
    ed.Select(def);
    Result r = Mirror(ed, Axis::X, MirrorAbout::WorldOrigin, false);
    CHECK(r.ok && r.skipped == 1 && def->GetComponent<MeshRenderer>()->mesh.triangles == dm.triangles && r.message.find("rebuilt every frame") != std::string::npos);
    // Refused without a selection or when the active object is needed and missing.
    ed.Select(nullptr);
    CHECK(!Mirror(ed, Axis::X, MirrorAbout::ActivePivot, true).ok);
}

static void TestPrefabInstance() {
    EditorState ed;
    Scene& s = ed.scene();
    GameObject* src = Odd(s, "Crate", {0, 0, 0}, {0, 0, 0}, {1, 1, 1});
    GameObject* part = Odd(s, "Lid", {0, 1, 0}, {0, 0, 0}, {1, 1, 1}, src);
    (void)part;
    const std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/okay_seltools_test.okayprefab";
    { std::ofstream f(path); f << SceneSerializer::SerializeObject(*src); }
    std::string err;
    GameObject* inst = SceneSerializer::InstantiateFromFile(s, path, &err);
    CHECK(inst != nullptr);
    if (!inst) { std::remove(path.c_str()); return; }
    inst->transform->SetPosition({6, 0, 2});
    CHECK(!inst->prefabSource.empty());
    ed.Select(inst);
    // Duplicate: the copy is still an instance of the same prefab. In place: the link stays.
    Result r = Mirror(ed, Axis::X, MirrorAbout::WorldOrigin, true);
    CHECK(r.ok && r.made.size() == 1);
    CHECK(r.made[0]->prefabSource == inst->prefabSource);
    CHECK(Near(r.made[0]->transform->Position(), {-6, 0, 2}) && Near(inst->transform->Position(), {6, 0, 2}));
    GameObject* made = r.made[0];
    ed.Select(inst);
    const auto before = SubtreeTris(inst);
    CHECK(Mirror(ed, Axis::X, MirrorAbout::WorldOrigin, false).ok);
    CHECK(!inst->prefabSource.empty() && AllSubtreesMirrored(before, inst, 0, 0.0f));
    CHECK(made->prefabSource == inst->prefabSource && Near(inst->transform->Position(), {-6, 0, 2}));
    std::remove(path.c_str());
}

static void TestSettings() {
    Settings s;
    s.alignTo = AlignTo::Active; s.spread = Spread::Centres; s.mirrorAbout = MirrorAbout::WorldOrigin; s.mirrorDuplicate = false; s.gizmoPivot = GizmoPivot::Center;
    std::ostringstream o;
    AppendSettings(o, s);
    Settings t;
    std::istringstream in(o.str());
    std::string k; int v; int n = 0;
    while (in >> k >> v) { CHECK(ApplySetting(k, v, t)); ++n; }
    CHECK(n == 5 && t.alignTo == AlignTo::Active && t.spread == Spread::Centres && t.mirrorAbout == MirrorAbout::WorldOrigin && !t.mirrorDuplicate && t.gizmoPivot == GizmoPivot::Center);
    CHECK(!ApplySetting("snapon", 1, t));
    CHECK(ApplySetting("mirrorabout", 99, t) && t.mirrorAbout == MirrorAbout::SelectionCentre);   // junk falls back to the default
}

static void TestGizmoMultiSelect() {
    EditorState ed;
    Scene& s = ed.scene();
    GameObject* a = Cube(s, "A", {0, 0, 0}, {1, 1, 1});
    GameObject* b = Cube(s, "B", {4, 0, 0}, {1, 1, 1});
    GameObject* c = Cube(s, "C", {0, 0, 4}, {1, 1, 1});
    GameObject* kid = Cube(s, "Kid", {0, 3, 0}, {1, 1, 1}, a);
    SelectAll(ed, {b, c, a, kid});                  // the kid is selected with its parent
    auto sel = ed.MultiSelection();
    // Followers: everything but the primary, and nothing above or below it.
    auto f = Followers(sel, a);
    CHECK(f.size() == 2 && f[0] == b && f[1] == c);
    f = Followers(sel, kid);                        // the kid's parent is its ancestor: it moves with it, not as a follower
    CHECK(f.size() == 2);
    CHECK(Followers(sel, nullptr).empty());
    // Pivot modes.
    auto tg = Targets(sel);
    CHECK(Near(PivotPoint(tg, a, GizmoPivot::Active), {0, 0, 0}));
    CHECK(Near(PivotPoint(tg, b, GizmoPivot::Active), {4, 0, 0}));
    CHECK(Near(PivotPoint(tg, a, GizmoPivot::Center), {4.0f / 3.0f, 0, 4.0f / 3.0f}));
    CHECK(Near(PivotPoint({a}, a, GizmoPivot::Center), {0, 0, 0}));        // one object: its own pivot
    // A drag, as the gizmo does it: one undo step at the grab, then many frames.
    const int d0 = ed.UndoDepth();
    ed.PushUndo();
    f = Followers(sel, a);
    for (int i = 0; i < 10; ++i) { a->transform->Translate({0.5f, 0, 0}); MoveFollowers(f, {0.5f, 0, 0}); }
    CHECK(ed.UndoDepth() == d0 + 1);
    CHECK(Near(a->transform->Position(), {5, 0, 0}) && Near(b->transform->Position(), {9, 0, 0}) && Near(c->transform->Position(), {5, 0, 4}));
    CHECK(Near(kid->transform->Position(), {5, 3, 0}));                     // followed its parent, once
    CHECK(ed.Undo());
    CHECK(Near(ed.scene().Find("B")->transform->Position(), {4, 0, 0}) && Near(ed.scene().Find("C")->transform->Position(), {0, 0, 4}) &&
          Near(ed.scene().Find("A")->transform->Position(), {0, 0, 0}));
    CHECK(ed.Redo());
    CHECK(Near(ed.scene().Find("B")->transform->Position(), {9, 0, 0}));
    // Rotate about the centre of the selection: 90 degrees about Y.
    EditorState e2;
    GameObject* p = Cube(e2.scene(), "P", {2, 0, 0}, {1, 1, 1});
    GameObject* q = Cube(e2.scene(), "Q", {-2, 0, 0}, {1, 1, 1});
    SelectAll(e2, {p, q});
    const Vec3 pivot = PivotPoint(Targets(e2.MultiSelection()), q, GizmoPivot::Center);
    CHECK(Near(pivot, {0, 0, 0}));
    RotateAbout(Targets(e2.MultiSelection()), pivot, {0, 1, 0}, 90.0f);
    CHECK(Near(p->transform->Position(), {0, 0, -2}) && Near(q->transform->Position(), {0, 0, 2}));
    CHECK(Near(p->transform->Forward(), {1, 0, 0}, 1e-3f));                  // each object turned with the group
    CHECK(Near(PivotPoint(Targets(e2.MultiSelection()), q, GizmoPivot::Center), pivot));   // the centre does not drift during a rotate drag
    // Rotating about the active object's own pivot turns it in place and orbits the rest.
    RotateAbout(Targets(e2.MultiSelection()), q->transform->Position(), {0, 1, 0}, -90.0f);
    CHECK(Near(q->transform->Position(), {0, 0, 2}) && Near(p->transform->Position(), {4, 0, 2}));
    // Scale: the ratio applies to each object's own scale and to its offset from the pivot.
    EditorState e3;
    GameObject* m = Cube(e3.scene(), "M", {0, 0, 0}, {1, 1, 1});
    GameObject* n = Cube(e3.scene(), "N", {2, 3, 0}, {1, 2, 1});
    SelectAll(e3, {n, m});
    const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    ScaleFollowers(Followers(e3.MultiSelection(), m), m->transform->Position(), axes, {2, 1, 1});
    CHECK(Near(n->transform->localScale, {2, 2, 1}) && Near(n->transform->Position(), {4, 3, 0}));
    ScaleFollowers({n}, {0, 0, 0}, axes, {0.001f, 1, 1});
    CHECK(n->transform->localScale.x >= 0.01f);                                // never collapses
    GameObject* cam = e3.scene().CreateGameObject("Cam"); cam->AddComponent<Camera>();
    cam->transform->SetPosition({1, 0, 0});
    ScaleFollowers({cam}, {0, 0, 0}, axes, {3, 3, 3});
    CHECK(Near(cam->transform->localScale, {1, 1, 1}) && Near(cam->transform->Position(), {3, 0, 0}));   // moved, never scaled
}

int main() {
    RUN_SUITE("selection_tools");
    TestTargetsAndBounds();
    TestAlign();
    TestAlignRotatedAndParented();
    TestDistribute();
    TestMirrorGeometry();
    TestMirrorTwiceIsIdentity();
    TestMirrorHierarchy();
    TestMirrorDuplicateAndUndo();
    TestMirrorSymmetricMeshAndSpecials();
    TestPrefabInstance();
    TestSettings();
    TestGizmoMultiSelect();
    TEST_MAIN_RESULT();
}
