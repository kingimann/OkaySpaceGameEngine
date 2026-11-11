// Editor snapping (editor/SurfaceSnap, ImGui-free): rotation / scale / move step maths (negative angles, wrap),
// the drag accumulator, modifier-key rules, settings persistence, and snap-to-surface (hit, miss, ignores the dragged
// object and its children, hidden objects and objects under a hidden parent, normal alignment, offset, rest on the
// surface, meshes with and without colliders), and one undo step through EditorState::PushUndo.
#include "test_framework.hpp"
#include "EditorState.hpp"
#include "EditorVisibility.hpp"
#include "SurfaceSnap.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Physics/Collider3D.hpp"
#include <sstream>

using namespace okay;
using namespace okay::editor;
using namespace okay::editor::snap;

static GameObject* Box(Scene& s, const std::string& name, Vec3 pos, Vec3 size, bool collider = true, bool mesh = false, GameObject* parent = nullptr) {
    GameObject* g = s.CreateGameObject(name);
    if (parent) g->transform->SetParent(parent->transform, false);
    g->transform->SetPosition(pos);
    if (mesh) { auto* mr = g->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Cube(); g->transform->localScale = size; }
    if (collider) { auto* bc = g->AddComponent<BoxCollider3D>(); bc->size = mesh ? Vec3::One : size; }
    return g;
}
static SurfaceHit Down(Scene& s, float x, float z, const SurfaceQuery& q = {}) {
    s.Start(); s.Update(0.0f);
    return RaycastSurface(s, {x, 20, z}, {0, -1, 0}, q);
}

static void TestStepMath() {
    CHECK_NEAR(RoundStep(7.4f, 15), 0.0f, 1e-5);
    CHECK_NEAR(RoundStep(7.6f, 15), 15.0f, 1e-5);
    CHECK_NEAR(RoundStep(-7.6f, 15), -15.0f, 1e-5);
    CHECK_NEAR(RoundStep(-7.4f, 15), 0.0f, 1e-5);
    CHECK(!std::signbit(RoundStep(-1.0f, 15)));              // never -0
    CHECK_NEAR(RoundStep(7.5f, 15), 15.0f, 1e-5);            // halves go away from zero on both sides
    CHECK_NEAR(RoundStep(-7.5f, 15), -15.0f, 1e-5);
    CHECK_NEAR(RoundStep(3.3f, 0.0f), 3.3f, 1e-6);           // step 0 = no snap
    CHECK_NEAR(RoundStep(3.3f, -1.0f), 3.3f, 1e-6);
    CHECK_NEAR(RoundStep(0.26f, 0.25f), 0.25f, 1e-6);
    for (float step : {1.0f, 5.0f, 15.0f, 45.0f, 90.0f}) {
        CHECK_NEAR(RoundStep(step * 0.6f, step), step, 1e-4);
        CHECK(OnStep(RoundStep(37.3f, step), step));
        CHECK(OnStep(RoundStep(-37.3f, step), step));
    }
    // Wrap.
    CHECK_NEAR(WrapDegrees(190), -170, 1e-4);
    CHECK_NEAR(WrapDegrees(-190), 170, 1e-4);
    CHECK_NEAR(WrapDegrees(180), 180, 1e-4);
    CHECK_NEAR(WrapDegrees(-180), 180, 1e-4);
    CHECK_NEAR(WrapDegrees(360), 0, 1e-4);
    CHECK_NEAR(WrapDegrees(725), 5, 1e-4);
    CHECK_NEAR(WrapDegrees(-725), -5, 1e-4);
    CHECK_NEAR(SnapDegrees(359.0f, 15), 0.0f, 1e-4);          // 360 reads as 0
    CHECK_NEAR(SnapDegrees(-359.0f, 15), 0.0f, 1e-4);
    CHECK_NEAR(SnapDegrees(172.0f, 15), 165.0f, 1e-4);
    CHECK_NEAR(SnapDegrees(-100.0f, 45), -90.0f, 1e-4);
    CHECK_NEAR(SnapDegrees(100.0f, 90), 90.0f, 1e-4);
    CHECK_NEAR(SnapDegrees(46.0f, 1), 46.0f, 1e-4);
    CHECK_NEAR(SnapDegrees(46.4f, 1), 46.0f, 1e-4);
    // Scale never collapses.
    CHECK_NEAR(SnapScale(0.04f, 0.25f), 0.01f, 1e-6);
    CHECK_NEAR(SnapScale(1.13f, 0.25f), 1.25f, 1e-6);
    CHECK_NEAR(SnapScale(1.13f, 0.1f), 1.1f, 1e-5);
    CHECK_NEAR(SnapScale(-3.0f, 0.5f), 0.01f, 1e-6);
    // Presets.
    CHECK(PresetIndex(kRotationPresets, 5, 15.0f) == 2);
    CHECK(PresetIndex(kRotationPresets, 5, 22.5f) == -1);
    CHECK(PresetIndex(kScalePresets, 6, 0.1f) == 2);
}

static void TestDragAccumulator() {
    // A slow drag in 1-unit mouse steps with a 15 degree detent: the value must reach 15, not stay stuck at 0.
    DragSnap d; d.Begin(0.0f);
    float shown = 0.0f;
    for (int i = 0; i < 20; ++i) shown = d.Feed(shown + 1.0f, 15.0f);
    CHECK_NEAR(shown, 15.0f, 1e-4);                           // 20 units of raw motion -> one detent at 15
    for (int i = 0; i < 40; ++i) shown = d.Feed(shown - 1.0f, 15.0f);
    CHECK_NEAR(shown, -15.0f, 1e-4);                          // raw is now -20 -> -15
    DragSnap e; e.Begin(37.0f);                               // a value that starts off-grid snaps on the first move
    CHECK_NEAR(e.Feed(38.0f, 15.0f), 45.0f, 1e-4);
}

static void TestModifiers() {
    SnapSettings s;
    auto r = Resolve(s, {});                                  CHECK(!r.steps && !r.surface);
    r = Resolve(s, {true, false, false});                     CHECK(r.steps && !r.surface);     // Ctrl: steps
    r = Resolve(s, {true, true, false});                      CHECK(r.steps && r.surface);      // Ctrl+Shift: surface
    r = Resolve(s, {false, true, false});                     CHECK(!r.steps && !r.surface);    // Shift alone does nothing
    s.enabled = true; s.surface = true;
    r = Resolve(s, {});                                       CHECK(r.steps && r.surface);
    r = Resolve(s, {false, false, true});                     CHECK(!r.steps && !r.surface);    // Alt cancels both
    r = Resolve(s, {true, true, true});                       CHECK(!r.steps && !r.surface);
}

static void TestPersistence() {
    SnapSettings a;
    a.enabled = true; a.moveStep = 0.25f; a.rotStep = 22.5f; a.scaleStep = 0.05f; a.surface = true;
    a.alignNormal = true; a.surfaceOffset = -0.125f; a.restOnSurface = false; a.placeOnSurface = true;
    std::ostringstream out; AppendSettings(out, a);
    out << "theme 1\n";                                       // another setting in the same file is left alone
    std::istringstream in(out.str());
    SnapSettings b; std::string k; int v; int others = 0;
    while (in >> k >> v) if (!ApplySetting(k, v, b)) ++others;
    CHECK(others == 1);
    CHECK(b.enabled && b.surface && b.alignNormal && !b.restOnSurface && b.placeOnSurface);
    CHECK_NEAR(b.moveStep, 0.25f, 1e-5); CHECK_NEAR(b.rotStep, 22.5f, 1e-5);
    CHECK_NEAR(b.scaleStep, 0.05f, 1e-5); CHECK_NEAR(b.surfaceOffset, -0.125f, 1e-5);
    // No file / old file: the defaults stand.
    SnapSettings d; CHECK(!d.enabled && d.rotStep == 15.0f && d.scaleStep == 0.1f && d.moveStep == 1.0f && !d.surface && d.restOnSurface && !d.placeOnSurface);
    // Hand-edited nonsense is clamped, never zero or negative steps.
    SnapSettings c;
    ApplySetting("snaprotx100", 0, c);        CHECK(c.rotStep >= kMinRotStep);
    ApplySetting("snaprotx100", 999999, c);   CHECK(c.rotStep <= kMaxRotStep);
    ApplySetting("snapmovex1000", -5, c);     CHECK(c.moveStep >= kMinStep);
    ApplySetting("snapscalex1000", 0, c);     CHECK(c.scaleStep >= kMinStep);
    ApplySetting("snapoffsetx1000", 99999999, c); CHECK(c.surfaceOffset <= 100.0f);
    CHECK(!ApplySetting("uismartsnap", 1, c));
}

static void TestRaycast() {
    Scene s("surf");
    GameObject* floor = Box(s, "Floor", {0, 0, 0}, {20, 1, 20});     // top face at y = 0.5
    SurfaceHit h = Down(s, 0, 0);
    CHECK(h.hit && h.object == floor && h.source == HitSource::Collider);
    CHECK_NEAR(h.point.y, 0.5f, 1e-3); CHECK_NEAR(h.normal.y, 1.0f, 1e-3); CHECK_NEAR(h.distance, 19.5f, 1e-2);
    // Miss: beside the floor.
    CHECK(!RaycastSurface(s, {50, 20, 0}, {0, -1, 0}, {}).hit);
    // Ray pointing away.
    CHECK(!RaycastSurface(s, {0, 20, 0}, {0, 1, 0}, {}).hit);
    // Max distance.
    SurfaceQuery near_; near_.maxDistance = 5.0f;
    CHECK(!RaycastSurface(s, {0, 20, 0}, {0, -1, 0}, near_).hit);
    // A zero direction is a miss, not a crash.
    CHECK(!RaycastSurface(s, {0, 20, 0}, {0, 0, 0}, {}).hit);

    // The dragged object (and its children) sit between the cursor and the floor: they are skipped.
    GameObject* drag = Box(s, "Dragged", {0, 3, 0}, {2, 2, 2});
    GameObject* kid = Box(s, "Kid", {0, 5, 0}, {2, 2, 2}, true, false, drag);
    h = Down(s, 0, 0);
    CHECK(h.hit && h.object == kid);                                  // nothing ignored: the child is on top
    SurfaceQuery q; q.ignore = {drag};
    h = Down(s, 0, 0, q);
    CHECK(h.hit && h.object == floor);                                // dragged object and child both skipped
    // Two ignored roots (a multi selection).
    GameObject* other = Box(s, "Other", {0, 8, 0}, {2, 2, 2});
    q.ignore = {drag, other};
    h = Down(s, 0, 0, q);
    CHECK(h.hit && h.object == floor);

    // Hidden objects are skipped; so is everything under a hidden parent.
    other->editorHidden = true;
    q.ignore = {drag};
    h = Down(s, 0, 0, q);
    CHECK(h.hit && h.object == floor);
    drag->editorHidden = true; q.ignore.clear();
    h = Down(s, 0, 0, q);
    CHECK(h.hit && h.object == floor);                                // drag, its child kid and other are all hidden
    drag->editorHidden = false; other->editorHidden = false;
    // Inactive objects are skipped.
    kid->active = false; drag->active = false;
    h = Down(s, 0, 0);
    CHECK(h.hit && h.object != kid && h.object != drag);
    drag->active = true; kid->active = true;
    // Trigger colliders are not surfaces.
    for (auto* c : drag->GetComponents<Collider3D>()) c->isTrigger = true;
    for (auto* c : kid->GetComponents<Collider3D>()) c->isTrigger = true;
    for (auto* c : other->GetComponents<Collider3D>()) c->isTrigger = true;
    h = Down(s, 0, 0);
    CHECK(h.hit && h.object == floor);
    // Isolate keeps other objects out.
    for (auto* c : drag->GetComponents<Collider3D>()) c->isTrigger = false;
    q = {}; q.isolate = drag;
    h = Down(s, 0, 0, q);
    CHECK(h.hit && h.object == drag);
}

static void TestMeshes() {
    // A mesh with no collider hits its triangles.
    {
        Scene s("meshes");
        GameObject* m = Box(s, "Crate", {0, 1, 0}, {2, 2, 2}, /*collider*/ false, /*mesh*/ true);
        SurfaceHit h = Down(s, 0.3f, 0.2f);
        CHECK(h.hit && h.object == m && h.source == HitSource::Triangles);
        CHECK_NEAR(h.point.y, 2.0f, 1e-3); CHECK_NEAR(h.normal.y, 1.0f, 1e-3);
        CHECK(!RaycastSurface(s, {1.5f, 20, 0}, {0, -1, 0}, {}).hit);   // outside the cube
        // Too many triangles for the budget: the render bounds stand in.
        SurfaceQuery q; q.maxTriangles = 1;
        h = Down(s, 0.3f, 0.2f, q);
        CHECK(h.hit && h.source == HitSource::Bounds);
        CHECK_NEAR(h.point.y, 2.0f, 1e-3); CHECK_NEAR(h.normal.y, 1.0f, 1e-3);
        // From the side the normal faces the viewer.
        h = RaycastSurface(s, {-10, 1, 0.1f}, {1, 0, 0}, {});
        CHECK(h.hit); CHECK_NEAR(h.normal.x, -1.0f, 1e-3); CHECK_NEAR(h.point.x, -1.0f, 1e-3);
    }
    // A MeshCollider is only a box: the triangles of the mesh are used, so a pyramid's slanted face is found.
    {
        Scene s("pyr");
        GameObject* p = s.CreateGameObject("Pyramid");
        p->AddComponent<MeshRenderer>()->mesh = Mesh::Pyramid();
        p->AddComponent<MeshCollider3D>();
        p->transform->SetPosition({0, 0, 0});
        SurfaceHit h = Down(s, 0.0f, 0.0f);
        CHECK(h.hit && h.object == p && h.source == HitSource::Triangles);
        Vec3 lo, hi; Mesh::Pyramid().Bounds(lo, hi);
        CHECK(h.point.y < hi.y + 1e-3f);                                // not the box top unless the apex is there
        // Near a corner of the base the true surface is far below the box's top face.
        SurfaceHit edge = Down(s, 0.45f * (hi.x - lo.x), 0.45f * (hi.z - lo.z));
        if (edge.hit) CHECK(edge.point.y < hi.y - 0.2f * (hi.y - lo.y));
    }
    // Sphere collider: exact, not its box.
    {
        Scene s("sph");
        GameObject* g = s.CreateGameObject("Ball");
        g->AddComponent<SphereCollider3D>()->radius = 1.0f;
        SurfaceHit h = Down(s, 0, 0);
        CHECK(h.hit && h.source == HitSource::Collider); CHECK_NEAR(h.point.y, 1.0f, 1e-3);
        h = Down(s, 0.9f, 0.0f);
        CHECK(h.hit); CHECK(h.point.y < 0.5f); CHECK(h.normal.x > 0.5f);   // sloped at the rim
    }
}

static void TestPose() {
    Scene s("pose");
    GameObject* floor = Box(s, "Floor", {0, 0, 0}, {20, 1, 20});
    GameObject* crate = Box(s, "Crate", {3, 9, 3}, {2, 2, 2}, false, true);  // 2 units tall, pivot at its centre
    SurfaceQuery q; q.ignore = {crate};
    SurfaceHit h = Down(s, 1, 1, q);
    CHECK(h.hit && h.object == floor);
    SnapSettings cfg;
    // Rest on the surface (default): the pivot is lifted by half the height.
    SurfacePose p = ComputeSurfacePose(crate, h, cfg);
    CHECK_NEAR(p.position.y, 0.5f + 1.0f, 1e-3); CHECK_NEAR(p.position.x, 1.0f, 1e-4); CHECK(!p.rotated);
    // Offset along the normal.
    cfg.surfaceOffset = 0.25f;
    p = ComputeSurfacePose(crate, h, cfg);
    CHECK_NEAR(p.position.y, 1.75f, 1e-3);
    cfg.surfaceOffset = -0.5f;
    p = ComputeSurfacePose(crate, h, cfg);
    CHECK_NEAR(p.position.y, 1.0f, 1e-3);
    // No rest: the pivot is on the surface plus the offset.
    cfg.restOnSurface = false; cfg.surfaceOffset = 0.0f;
    p = ComputeSurfacePose(crate, h, cfg);
    CHECK_NEAR(p.position.y, 0.5f, 1e-3);
    ApplySurfacePose(crate, p);
    CHECK_NEAR(crate->transform->Position().y, 0.5f, 1e-3);
    crate->transform->SetPosition({3, 9, 3});

    // Normal alignment on a 30-degree slope (a tilted slab).
    GameObject* ramp = Box(s, "Ramp", {5, 3, 0}, {4, 0.2f, 4}, true);
    ramp->transform->SetRotation(Quat::AngleAxis(30.0f, Vec3::Forward));   // tilt about Z
    q.ignore = {crate};
    SurfaceHit rh = Down(s, 5, 0, q);
    CHECK(rh.hit && rh.object == ramp);
    const Vec3 want = ramp->transform->Up();
    CHECK(Vec3::Dot(rh.normal, want) > 0.999f);
    cfg.alignNormal = true; cfg.restOnSurface = true;
    p = ComputeSurfacePose(crate, rh, cfg);
    CHECK(p.rotated);
    CHECK(Vec3::Dot(p.rotation * Vec3::Up, rh.normal) > 0.9999f);       // up now matches the surface normal
    // Heading is kept: turning only about the axis between old up and the normal, so a yawed object keeps its yaw.
    crate->transform->SetRotation(Quat::AngleAxis(40.0f, Vec3::Up));
    p = ComputeSurfacePose(crate, rh, cfg);
    CHECK(Vec3::Dot(p.rotation * Vec3::Up, rh.normal) > 0.9999f);
    const Vec3 fwdBefore = crate->transform->Forward(), fwdAfter = p.rotation * Vec3::Forward;
    CHECK(Vec3::Dot(fwdBefore, fwdAfter) > 0.7f);                        // turned by the slope only (30 degrees max)
    // Resting height along the normal: the crate's lowest point touches the slope.
    const float reach = ReachBelow(crate, rh.normal, p.rotation * crate->transform->Rotation().Inverse());
    CHECK_NEAR(reach, 1.0f, 1e-3);                                       // a cube turned flat on the slope reaches 1 below
    CHECK_NEAR(Vec3::Dot(p.position - rh.point, rh.normal), 1.0f, 1e-3);
    // Without Align: rotation is unchanged.
    cfg.alignNormal = false;
    p = ComputeSurfacePose(crate, rh, cfg);
    CHECK(!p.rotated);
    // An empty object has no reach.
    GameObject* empty = s.CreateGameObject("Empty");
    CHECK_NEAR(ReachBelow(empty, {0, 1, 0}, Quat{0, 0, 0, 1}), 0.0f, 1e-6);
}

static void TestUndo() {
    EditorState ed;
    GameObject* floor = Box(ed.scene(), "Floor", {0, 0, 0}, {20, 1, 20});
    GameObject* c = Box(ed.scene(), "Crate", {2, 9, 2}, {1, 1, 1}, false, true);
    (void)floor;
    ed.scene().Start(); ed.scene().Update(0.0f);
    SurfaceQuery q; q.ignore = {c};
    SurfaceHit h = RaycastSurface(ed.scene(), {2, 20, 2}, {0, -1, 0}, q);
    CHECK(h.hit);
    const Vec3 before = c->transform->Position();
    const int depth0 = ed.UndoDepth();
    ed.PushUndo();                                                       // the gizmo grab pushes this once for the whole drag
    SnapSettings cfg;
    for (int i = 0; i < 5; ++i) ApplySurfacePose(c, ComputeSurfacePose(c, h, cfg));   // many frames of the same drag
    CHECK(ed.UndoDepth() == depth0 + 1);
    CHECK_NEAR(c->transform->Position().y, 1.0f, 1e-3);
    CHECK(ed.Undo());
    GameObject* back = ed.scene().Find("Crate");
    CHECK(back && (back->transform->Position() - before).Magnitude() < 1e-3f);
    CHECK(ed.Redo());
    back = ed.scene().Find("Crate");
    CHECK(back && std::fabs(back->transform->Position().y - 1.0f) < 1e-3f);
}

int main() {
    RUN_SUITE("surface_snap");
    TestStepMath();
    TestDragAccumulator();
    TestModifiers();
    TestPersistence();
    TestRaycast();
    TestMeshes();
    TestPose();
    TestUndo();
    TEST_MAIN_RESULT();
}
