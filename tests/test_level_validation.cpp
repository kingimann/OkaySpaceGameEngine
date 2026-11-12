// Level-design rules in the scene validator and the Problems list: degenerate geometry, overlapping duplicates,
// light leaks, unreachable navigation, camera / light sanity, moving colliders without a body. Every rule has a case
// that fires and a case that stays quiet, the one-click fixes are applied, and a 10,000-object scene is timed.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Light.hpp"
#include "okay/Components/Mover.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/NavMeshSurface.hpp"
#include "okay/Debug/Diagnostics.hpp"
#include "okay/Debug/ProjectDiagnostics.hpp"
#include "okay/Testing/SceneValidator.hpp"

#include <chrono>
#include <cmath>
#include <limits>

using namespace okay;
using namespace okay::testing;

namespace {

double Seconds(std::chrono::steady_clock::time_point a) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
}

GameObject* StaticBox(Scene& s, const char* name, Vec3 pos, Vec3 size) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = pos;
    g->AddComponent<BoxCollider3D>()->size = size;
    g->isStatic = true;
    return g;
}

GameObject* NavBox(Scene& s, const char* name, Vec3 pos, Vec3 scale) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = pos;
    g->transform->localScale = scale;
    g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    return g;
}

GameObject* Cube(Scene& s, const char* name, Vec3 pos) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = pos;
    g->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
    return g;
}

int Count(const ValidationReport& r, const char* code) { return r.CountCode(code); }
const ValidationIssue* First(const ValidationReport& r, const char* code) {
    for (const auto& i : r.issues) if (i.code == code) return &i;
    return nullptr;
}
int CountD(const std::vector<Diagnostic>& v, const char* code) {
    int n = 0;
    for (const auto& d : v) if (d.code == code) ++n;
    return n;
}
const Diagnostic* FindD(const std::vector<Diagnostic>& v, const char* code) {
    for (const auto& d : v) if (d.code == code) return &d;
    return nullptr;
}
ValidationReport Run(const Scene& s) { return SceneValidator::Validate(s); }

} // namespace

int main() {
    RUN_SUITE("level_validation");

    // ---- (a) degenerate geometry -------------------------------------------------------------------------------
    {
        Scene s("zero"); s.physicsEnabled = false;
        GameObject* bad = s.CreateGameObject("Flat");
        bad->AddComponent<BoxCollider3D>()->size = {2, 0, 2};
        GameObject* ball = s.CreateGameObject("Ball");
        ball->AddComponent<SphereCollider3D>()->radius = 0.0f;
        GameObject* good = s.CreateGameObject("Fine");
        good->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        GameObject* fitted = s.CreateGameObject("Fitted");
        auto* fb = fitted->AddComponent<BoxCollider3D>(); fb->size = {0, 0, 0}; fb->autoFit = true;
        ValidationReport r = Run(s);
        CHECK(Count(r, "geometry.zero_collider") == 2);
        const ValidationIssue* i = First(r, "geometry.zero_collider");
        CHECK(i && i->object == "Flat" && i->severity == Severity::Warning && !i->guid.empty());
        // The fix gives the collider a size; the rule goes quiet.
        auto items = CollectProjectDiagnostics(s).items;
        const Diagnostic* d = nullptr;
        for (const auto& x : items) if (x.code == "geometry.zero_collider" && x.source.objectPath == "Flat") d = &x;
        CHECK(d && d->fix.kind == DiagFixKind::ResetColliderSize && !d->hint.empty());
        CHECK(d && ApplyDiagnosticFix(s, *d));
        CHECK(Count(Run(s), "geometry.zero_collider") == 1);   // the sphere is left
    }
    {   // transform.zero_scale and non_finite already exist: their rows now carry a one-click fix
        Scene s("scale"); s.physicsEnabled = false;
        GameObject* g = Cube(s, "Squashed", {0, 0, 0});
        g->transform->localScale = {1, 0, 1};
        GameObject* n = Cube(s, "Broken", {1, 0, 0});
        n->transform->localPosition.x = std::numeric_limits<float>::quiet_NaN();
        auto items = CollectProjectDiagnostics(s).items;
        const Diagnostic* z = FindD(items, "transform.zero_scale");
        const Diagnostic* f = FindD(items, "transform.non_finite");
        CHECK(z && z->fix.kind == DiagFixKind::ResetScale);
        CHECK(f && f->fix.kind == DiagFixKind::ResetTransform);
        CHECK(z && ApplyDiagnosticFix(s, *z) && g->transform->localScale.y == 1.0f);
        CHECK(f && ApplyDiagnosticFix(s, *f) && std::isfinite(n->transform->localPosition.x));
        CHECK(Count(Run(s), "transform.zero_scale") == 0 && Count(Run(s), "transform.non_finite") == 0);
    }
    {
        Scene s("empty"); s.physicsEnabled = false;
        s.CreateGameObject("NoMesh")->AddComponent<MeshRenderer>()->mesh = Mesh();
        Cube(s, "Cube", {0, 0, 0});
        GameObject* ter = s.CreateGameObject("Generated");
        ter->AddComponent<MeshRenderer>()->mesh = Mesh();
        ter->AddComponent<Terrain>();
        ValidationReport r = Run(s);
        CHECK(Count(r, "geometry.empty_mesh") == 1);
        const ValidationIssue* i = First(r, "geometry.empty_mesh");
        CHECK(i && i->object == "NoMesh");
    }

    // ---- (b) overlapping duplicates ----------------------------------------------------------------------------
    {
        Scene s("dup"); s.physicsEnabled = false;
        Cube(s, "Wall", {3, 0, 0});
        GameObject* copy = Cube(s, "Wall", {3.002f, 0, 0});      // near-identical
        Cube(s, "Other", {3, 0, 5});                              // same mesh, elsewhere
        GameObject* moved = Cube(s, "Moved", {3.0f, 0.5f, 0});    // same mesh, half a unit up
        GameObject* rot = Cube(s, "Rotated", {3, 0, 0});
        rot->transform->SetEulerAngles({0, 45, 0});               // same place, turned
        GameObject* scaled = Cube(s, "Scaled", {3, 0, 0});
        scaled->transform->localScale = {2, 2, 2};
        (void)moved;
        ValidationReport r = Run(s);
        CHECK(Count(r, "object.duplicate_overlap") == 1);
        const ValidationIssue* i = First(r, "object.duplicate_overlap");
        CHECK(i && i->guid == copy->Guid() && i->related.size() == 1 && i->relatedGuids.size() == 1);
        auto items = CollectProjectDiagnostics(s).items;
        const Diagnostic* d = FindD(items, "object.duplicate_overlap");
        CHECK(d && d->fix.kind == DiagFixKind::DeleteObject && d->relatedGuids.size() == 1);
        const int before = (int)s.Objects().size();
        CHECK(d && ApplyDiagnosticFix(s, *d));
        CHECK((int)s.Objects().size() == before - 1);
        CHECK(Count(Run(s), "object.duplicate_overlap") == 0);
    }
    {   // a different mesh file at the same place is not a duplicate; a copy with an extra component has no auto-delete
        Scene s("dup2"); s.physicsEnabled = false;
        GameObject* a = Cube(s, "A", {0, 0, 0}); a->GetComponent<MeshRenderer>()->meshPath = "a.obj";
        GameObject* b = Cube(s, "B", {0, 0, 0}); b->GetComponent<MeshRenderer>()->meshPath = "b.obj";
        CHECK(Count(Run(s), "object.duplicate_overlap") == 0);
        b->GetComponent<MeshRenderer>()->meshPath = "a.obj";
        b->AddComponent<BoxCollider3D>();
        CHECK(Count(Run(s), "object.duplicate_overlap") == 1);
        auto items = CollectProjectDiagnostics(s).items;
        const Diagnostic* d = FindD(items, "object.duplicate_overlap");
        CHECK(d && d->fix.kind == DiagFixKind::None);
    }

    // ---- (d) light leaks ---------------------------------------------------------------------------------------
    {
        Scene s("leak"); s.physicsEnabled = false;
        StaticBox(s, "WallA", {0, 1, 0}, {1, 2, 4});          // x in [-0.5, 0.5]
        StaticBox(s, "WallB", {1.02f, 1, 0}, {1, 2, 4});      // x in [0.52, 1.52]: a 2 cm seam
        ValidationReport r = Run(s);
        CHECK(Count(r, "lighting.light_leak") == 1);
        const ValidationIssue* i = First(r, "lighting.light_leak");
        CHECK(i && i->severity == Severity::Warning && i->object == "WallA" && i->related.size() == 1 && i->related[0] == "WallB");
        auto items = CollectProjectDiagnostics(s).items;
        const Diagnostic* d = FindD(items, "lighting.light_leak");
        CHECK(d && d->fix.kind == DiagFixKind::None && d->relatedGuids.size() == 1 && !d->hint.empty());
    }
    {   // negatives: touching, overlapping, wide gap, a filler box, not static, a corner, a rotated wall
        Scene s("noleak"); s.physicsEnabled = false;
        StaticBox(s, "A", {0, 1, 0}, {1, 2, 4});
        StaticBox(s, "Touch", {1.0f, 1, 0}, {1, 2, 4});          // flush
        StaticBox(s, "Overlap", {-0.9f, 1, 0}, {1, 2, 4});       // overlapping
        StaticBox(s, "Wide", {0, 1, 8}, {1, 2, 4});              // far
        StaticBox(s, "Corner", {1.52f, 1, 4.02f}, {1, 2, 0.04f});   // diagonal: apart on two axes
        CHECK(Count(Run(s), "lighting.light_leak") == 0);

        Scene f("filler"); f.physicsEnabled = false;
        StaticBox(f, "L", {0, 1, 0}, {1, 2, 4});
        StaticBox(f, "R", {1.02f, 1, 0}, {1, 2, 4});
        StaticBox(f, "Filler", {0.51f, 1, 0}, {0.1f, 2, 4});     // closes the seam
        CHECK(Count(Run(f), "lighting.light_leak") == 0);

        Scene m("movable"); m.physicsEnabled = false;
        StaticBox(m, "L", {0, 1, 0}, {1, 2, 4});
        GameObject* r2 = StaticBox(m, "R", {1.02f, 1, 0}, {1, 2, 4});
        r2->isStatic = false;                                    // movable geometry is not baked
        CHECK(Count(Run(m), "lighting.light_leak") == 0);

        Scene q("rot"); q.physicsEnabled = false;
        StaticBox(q, "L", {0, 1, 0}, {1, 2, 4});
        GameObject* rr = StaticBox(q, "R", {1.02f, 1, 0}, {1, 2, 4});
        rr->transform->SetEulerAngles({0, 30, 0});               // not a box-equals-bounds wall: not judged
        CHECK(Count(Run(q), "lighting.light_leak") == 0);
    }
    {   // a box resting a sliver above the floor is an Info, not a Warning
        Scene s("sliver"); s.physicsEnabled = false;
        StaticBox(s, "Floor", {0, -0.5f, 0}, {10, 1, 10});
        StaticBox(s, "Crate", {0, 0.53f, 0}, {1, 1, 1});         // 3 cm above the floor top (y = 0)
        ValidationReport r = Run(s);
        const ValidationIssue* i = First(r, "lighting.light_leak");
        CHECK(Count(r, "lighting.light_leak") == 1 && i && i->severity == Severity::Info);
    }

    // ---- (e) navigation ----------------------------------------------------------------------------------------
    {
        Scene s("nav"); s.physicsEnabled = false;
        NavBox(s, "FloorA", {0, -0.5f, 0}, {10, 1, 10});
        NavBox(s, "FloorB", {30, -0.5f, 0}, {10, 1, 10});        // an island 20 units from the first
        GameObject* agent = s.CreateGameObject("Agent");
        agent->transform->localPosition = {0, 0.9f, 0};
        agent->AddComponent<NavMeshAgent>()->baseOffset = 0.9f;
        GameObject* ok = s.CreateGameObject("Goal OK"); ok->tag = "Goal"; ok->transform->localPosition = {2, 0, 2};
        GameObject* island = s.CreateGameObject("Goal Island"); island->tag = "Goal"; island->transform->localPosition = {30, 0, 0};
        GameObject* off = s.CreateGameObject("Pickup Off"); off->tag = "pickup"; off->transform->localPosition = {100, 0, 0};
        GameObject* spawn = s.CreateGameObject("Spawn Island"); spawn->tag = "SpawnPoint"; spawn->transform->localPosition = {31, 0, 1};
        // Not baked: nothing from the new rules (navmesh.no_bake is the existing row).
        ValidationReport pre = Run(s);
        CHECK(Count(pre, "nav.off_mesh") == 0 && Count(pre, "nav.unreachable") == 0);
        CHECK(CountD(CollectProjectDiagnostics(s).items, "navmesh.no_bake") == 1);
        NavMeshSurface* ns = s.CreateGameObject("Nav")->AddComponent<NavMeshSurface>();
        ns->settings.cellSize = 0.5f;
        CHECK(ns->Bake(s));
        ValidationReport r = Run(s);
        CHECK(Count(r, "nav.off_mesh") == 1);
        CHECK(Count(r, "nav.unreachable") == 2);
        bool offOk = false, islandOk = false, spawnOk = false, okQuiet = true;
        for (const auto& i : r.issues) {
            if (i.code == "nav.off_mesh" && i.object == "Pickup Off") offOk = true;
            if (i.code == "nav.unreachable" && i.object == "Goal Island" && i.related.size() == 1 && i.related[0] == "Agent") islandOk = true;
            if (i.code == "nav.unreachable" && i.object == "Spawn Island") spawnOk = true;
            if (i.object == "Goal OK" || i.object == "Agent") okQuiet = false;
        }
        CHECK(offOk && islandOk && spawnOk && okQuiet);
        // An agent off the mesh is reported too.
        agent->transform->localPosition = {0, 0.9f, 60};
        CHECK(Count(Run(s), "nav.off_mesh") == 2);
        // Stable: the same scene reports the same text twice.
        CHECK(Run(s).ToJson() == Run(s).ToJson());
    }

    // ---- (g) cameras and lights --------------------------------------------------------------------------------
    {
        Scene s("cams"); s.physicsEnabled = false;
        s.CreateGameObject("Cam1")->AddComponent<Camera>();
        GameObject* c2 = s.CreateGameObject("Cam2");
        c2->AddComponent<Camera>();
        Cube(s, "Block", {0, 0, 0});
        ValidationReport r = Run(s);
        CHECK(Count(r, "scene.multiple_main_cameras") == 1);
        const ValidationIssue* i = First(r, "scene.multiple_main_cameras");
        CHECK(i && i->object == "Cam2" && i->related.size() == 1 && i->related[0] == "Cam1");
        CHECK(Count(r, "scene.no_light") == 1);
        auto items = CollectProjectDiagnostics(s).items;
        const Diagnostic* dm = FindD(items, "scene.multiple_main_cameras");
        CHECK(dm && dm->fix.kind == DiagFixKind::ClearMainCamera && ApplyDiagnosticFix(s, *dm));
        const Diagnostic* dl = FindD(items, "scene.no_light");
        CHECK(dl && dl->fix.kind == DiagFixKind::AddLight && ApplyDiagnosticFix(s, *dl));
        r = Run(s);
        CHECK(Count(r, "scene.multiple_main_cameras") == 0 && Count(r, "scene.no_light") == 0);
        // A disabled light does not count; an unlit scene needs none.
        s.FindObjectsOfType<Light>()[0]->enabled = false;
        CHECK(Count(Run(s), "scene.no_light") == 1);
        Scene u("unlit"); u.physicsEnabled = false;
        Cube(u, "Glow", {0, 0, 0})->GetComponent<MeshRenderer>()->shader = MeshRenderer::Shader::Unlit;
        CHECK(Count(Run(u), "scene.no_light") == 0);
        Scene e("nomesh"); e.physicsEnabled = false;
        CHECK(Count(Run(e), "scene.no_light") == 0);
    }

    // ---- (h) colliders on moving objects, bodies without colliders ---------------------------------------------
    {
        Scene s("move"); s.physicsEnabled = false;
        GameObject* plat = s.CreateGameObject("Platform");
        plat->AddComponent<BoxCollider3D>();
        plat->AddComponent<Mover>()->velocity = {1, 0, 0};
        GameObject* okp = s.CreateGameObject("Platform OK");
        okp->AddComponent<BoxCollider3D>();
        okp->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Kinematic;
        okp->AddComponent<Mover>()->velocity = {1, 0, 0};
        GameObject* still = s.CreateGameObject("Still");
        still->AddComponent<BoxCollider3D>();
        still->AddComponent<Mover>();                                  // zero velocity
        GameObject* parent = s.CreateGameObject("Rig");
        parent->AddComponent<Rigidbody3D>();
        GameObject* child = s.CreateGameObject("Rig Child");
        child->transform->SetParent(parent->transform);
        child->AddComponent<BoxCollider3D>();
        child->AddComponent<Mover>()->velocity = {0, 1, 0};            // the body above owns it
        GameObject* ghost = s.CreateGameObject("Ghost");
        ghost->AddComponent<BoxCollider3D>()->isTrigger = true;
        ghost->AddComponent<Mover>()->velocity = {1, 0, 0};            // a trigger does not push anything
        ValidationReport r = Run(s);
        CHECK(Count(r, "physics.moving_collider_no_rigidbody") == 1);
        const ValidationIssue* i = First(r, "physics.moving_collider_no_rigidbody");
        CHECK(i && i->object == "Platform");
        auto items = CollectProjectDiagnostics(s).items;
        const Diagnostic* d = FindD(items, "physics.moving_collider_no_rigidbody");
        CHECK(d && d->fix.kind == DiagFixKind::AddKinematicBody && ApplyDiagnosticFix(s, *d));
        CHECK(plat->GetComponent<Rigidbody3D>() && plat->GetComponent<Rigidbody3D>()->bodyType == Rigidbody3D::BodyType::Kinematic);
        CHECK(Count(Run(s), "physics.moving_collider_no_rigidbody") == 0);
        // Rigidbody with no collider is the existing rule: still there.
        GameObject* lonely = s.CreateGameObject("Lonely");
        lonely->AddComponent<Rigidbody3D>();
        CHECK(Count(Run(s), "physics.rigidbody_no_collider") >= 1);
    }

    // ---- (f) references ----------------------------------------------------------------------------------------
    {
        Scene s("refs"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Spawner");
        g->AddComponent<Spawner>()->prefabPath = "prefabs/missing.okayprefab";
        GameObject* n = s.CreateGameObject("Fine");
        n->AddComponent<Spawner>();
        ValidatorOptions o;
        o.fileExists = [](const std::string&) { return false; };
        ValidationReport r = SceneValidator::Validate(s, o);
        CHECK(Count(r, "asset.missing") == 1);
        const ValidationIssue* i = First(r, "asset.missing");
        CHECK(i && i->object == "Spawner" && i->message.find("prefab") != std::string::npos);
    }

    // ---- budget and stability: 10,000 objects ------------------------------------------------------------------
    {
        Scene s("big"); s.physicsEnabled = false;
        s.CreateGameObject("Cam")->AddComponent<Camera>();
        s.CreateGameObject("Sun")->AddComponent<Light>();
        for (int i = 0; i < 100; ++i)
            for (int j = 0; j < 100; ++j) {
                GameObject* g = StaticBox(s, "Tile", {i * 1.5f, 0, j * 1.5f}, {1, 1, 1});   // 0.5 m apart: no leak
                g->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
            }
        // A few real problems so the issue lists are not empty.
        for (int k = 0; k < 20; ++k) Cube(s, "Dup", {3000.0f + k * 3.0f, 0, 0});
        for (int k = 0; k < 20; ++k) Cube(s, "Dup", {3000.0f + k * 3.0f, 0, 0});
        StaticBox(s, "SeamA", {-50, 1, 0}, {1, 2, 4});
        StaticBox(s, "SeamB", {-48.98f, 1, 0}, {1, 2, 4});
        CHECK((int)s.Objects().size() >= 10000);
        auto t0 = std::chrono::steady_clock::now();
        ValidationReport r1 = SceneValidator::Validate(s);
        const double tv = Seconds(t0);
        t0 = std::chrono::steady_clock::now();
        ProjectDiagResult pd = CollectProjectDiagnostics(s);
        const double tp = Seconds(t0);
        std::cout << "  10k objects: Validate " << tv << " s, CollectProjectDiagnostics " << tp << " s\n";
        CHECK(tv < 2.0);
        CHECK(tp < 2.0);
        CHECK(Count(r1, "object.duplicate_overlap") == 20);
        CHECK(Count(r1, "lighting.light_leak") == 1);
        CHECK(CountD(pd.items, "object.duplicate_overlap") == 20);
        // Stable: byte-identical reports, rows in the same order.
        ValidationReport r2 = SceneValidator::Validate(s);
        CHECK(r1.ToJson() == r2.ToJson());
        ProjectDiagResult pd2 = CollectProjectDiagnostics(s);
        bool same = pd.items.size() == pd2.items.size();
        for (std::size_t k = 0; same && k < pd.items.size(); ++k)
            same = pd.items[k].code == pd2.items[k].code && pd.items[k].message == pd2.items[k].message &&
                   pd.items[k].source.objectGuid == pd2.items[k].source.objectGuid;
        CHECK(same);
    }

    TEST_MAIN_RESULT();
}
