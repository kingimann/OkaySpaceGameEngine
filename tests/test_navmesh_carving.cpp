// Navigation obstacle carving: a NavMeshObstacle with Carve on cuts a hole in the baked mesh so the path
// search goes around it. Covers the carving geometry (footprint strips, polygon subtraction, rebuilt
// portals, queries on the carved mesh), the obstacle's stationary / follow timing, agents that are already
// walking replanning, headless simulations (an agent walks around an obstacle that parks in its way), the
// scene file, the property registry and the validator rules.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Testing/SceneValidator.hpp"
#include <cmath>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::testing;

static GameObject* Box(Scene& s, const char* name, Vec3 pos, Vec3 scale) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = pos; g->transform->localScale = scale;
    g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    return g;
}
static NavMeshSurface* Surface(Scene& s) {
    auto* ns = s.CreateGameObject("Nav Mesh")->AddComponent<NavMeshSurface>();
    ns->settings.agentRadius = 0.4f; ns->settings.cellSize = 0.25f;
    return ns;
}
static NavMeshObstacle* Cart(Scene& s, Vec3 pos, Vec3 size, float timeToStationary = 0.2f) {
    GameObject* g = s.CreateGameObject("Cart");
    g->transform->localPosition = pos;
    auto* o = g->AddComponent<NavMeshObstacle>();
    o->size = size; o->carve = true; o->timeToStationary = timeToStationary;
    return o;
}
static float Dist2D(const Vec3& a, const Vec3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }
static float PathLength(const std::vector<Vec3>& p) {
    float l = 0.0f;
    for (std::size_t i = 1; i < p.size(); ++i) l += Dist2D(p[i], p[i - 1]);
    return l;
}
static bool Crosses(const std::vector<Vec3>& p, float x0, float x1, float z0, float z1) {
    for (std::size_t i = 1; i < p.size(); ++i)
        for (int k = 0; k <= 40; ++k) {
            const float t = k / 40.0f;
            const float x = p[i - 1].x + (p[i].x - p[i - 1].x) * t, z = p[i - 1].z + (p[i].z - p[i - 1].z) * t;
            if (x > x0 && x < x1 && z > z0 && z < z1) return true;
        }
    return false;
}
static void Tick(Scene& s, int n) { for (int i = 0; i < n; ++i) s.Update(1.0f / 60.0f); }
static bool Inside(const NavMesh& m, float x, float z) {
    return m.FindNearestPoly(Vec3{x, 0, z}, Vec3{0.01f, 5.0f, 0.01f}) >= 0;
}
static NavCarveVolume BoxVolume(float x0, float x1, float z0, float z1, float y0 = 0.0f, float y1 = 2.0f) {
    const float xs[4] = {x0, x1, x1, x0}, zs[4] = {z0, z0, z1, z1};
    NavCarveVolume v; v.SetHull(xs, zs, 4); v.minY = y0; v.maxY = y1;
    return v;
}

int main() {
    RUN_SUITE("navmesh carving");

    // ---- Footprint strips -----------------------------------------------------------------------------------
    {
        std::vector<NavCarveRect> r;
        CHECK(NavCarveRects(BoxVolume(-1, 1, -2, 2), 0.4f, 0.25f, r) == 1);   // axis-aligned: one rect, grown by the radius
        CHECK_NEAR(r[0].minX, -1.4f, 1e-5f); CHECK_NEAR(r[0].maxX, 1.4f, 1e-5f);
        CHECK_NEAR(r[0].minZ, -2.4f, 1e-5f); CHECK_NEAR(r[0].maxZ, 2.4f, 1e-5f);
        CHECK_NEAR(r[0].minY, 0.0f, 1e-5f); CHECK_NEAR(r[0].maxY, 2.0f, 1e-5f);

        // A 45 degree square (diamond, half-diagonal 1.5) is covered by several strips that follow its edges.
        const float xs[4] = {1.5f, 0, -1.5f, 0}, zs[4] = {0, 1.5f, 0, -1.5f};
        NavCarveVolume dia; dia.SetHull(xs, zs, 4); dia.minY = 0; dia.maxY = 1;
        CHECK(dia.count == 4);
        r.clear();
        const int n = NavCarveRects(dia, 0.0f, 0.25f, r);
        CHECK(n == 12);                                   // 3 wide, 0.25 per strip
        bool covered = true;
        for (float x = -1.45f; x <= 1.45f; x += 0.05f)
            for (float z = -1.45f; z <= 1.45f; z += 0.05f) {
                if (std::fabs(x) + std::fabs(z) > 1.5f) continue;
                bool in = false;
                for (const NavCarveRect& q : r) if (x >= q.minX - 1e-4f && x <= q.maxX + 1e-4f && z >= q.minZ - 1e-4f && z <= q.maxZ + 1e-4f) in = true;
                covered = covered && in;
            }
        CHECK(covered);                                   // every point of the diamond is inside a strip
        float area = 0.0f;
        for (const NavCarveRect& q : r) area += (q.maxX - q.minX) * (q.maxZ - q.minZ);
        CHECK(area > 4.5f && area < 4.5f + 3.0f * 0.25f * 1.5f * 2.0f + 0.5f);   // diamond area 4.5, plus a sliver per strip
        // A circle becomes a polygon just outside it, and a hull drops interior points.
        NavCarveVolume circ; circ.SetCircle(1, 2, 0.5f); circ.minY = 0; circ.maxY = 1;
        CHECK(circ.count == NavCarveVolume::kMaxPoints);
        for (int i = 0; i < circ.count; ++i) CHECK(Dist2D(Vec3{circ.x[i], 0, circ.z[i]}, Vec3{1, 0, 2}) >= 0.5f - 1e-4f);
        const float hx[5] = {0, 2, 2, 0, 1}, hz[5] = {0, 0, 2, 2, 1};
        NavCarveVolume hull; hull.SetHull(hx, hz, 5);
        CHECK(hull.count == 4);
        CHECK(hull.Delta(hull) == 0.0f);
        NavCarveVolume moved = hull; moved.x[0] += 0.25f;
        CHECK_NEAR(hull.Delta(moved), 0.25f, 1e-5f);
        CHECK(hull.Delta(circ) > 1e6f);                   // different point counts: never "the same"
        // Degenerate footprints give nothing.
        NavCarveVolume none; none.minY = 0; none.maxY = 1;
        r.clear();
        CHECK(NavCarveRects(none, 0.4f, 0.25f, r) == 0 && r.empty());
    }

    // ---- Carving a baked mesh: area, holes, portals, paths -----------------------------------------------------
    {
        Scene s("carve"); s.physicsEnabled = false;
        Box(s, "Ground", {0, -0.5f, 0}, {20, 1, 20});
        NavMeshSurface* ns = Surface(s);
        CHECK(ns->Bake(s));
        const NavMesh& base = *ns->Mesh();
        const std::string baked = base.Serialize();
        const float baseArea = base.Area();
        const std::size_t basePolys = base.polys.size();

        NavMesh out;
        CHECK(base.Carve({BoxVolume(-1, 1, -1, 1)}, out) >= 1);
        CHECK(base.Serialize() == baked);                         // the baked mesh is not touched
        CHECK(out.polys.size() > basePolys);                      // a polygon was split into pieces
        CHECK_NEAR(baseArea - out.Area(), 2.8f * 2.8f, 0.1f);    // the 2x2 box, grown by the 0.4 agent radius
        CHECK(!Inside(out, 0, 0) && !Inside(out, 1.3f, 1.3f) && !Inside(out, -1.3f, 0.5f));
        CHECK(Inside(base, 0, 0));
        CHECK(Inside(out, 1.6f, 0) && Inside(out, 0, -1.6f) && Inside(out, -5, 5));
        // Portals: valid, on both polygons' edges, and each has its mirror.
        bool linksOk = !out.links.empty();
        for (const NavMesh::Link& l : out.links) {
            if (l.from < 0 || l.to < 0 || l.from >= (int)out.polys.size() || l.to >= (int)out.polys.size() || l.from == l.to) { linksOk = false; continue; }
            for (const NavMesh::Poly* p : {&out.polys[(std::size_t)l.from], &out.polys[(std::size_t)l.to]})
                for (const Vec3* q : {&l.a, &l.b})
                    if (q->x < p->minX - 2e-3f || q->x > p->maxX + 2e-3f || q->z < p->minZ - 2e-3f || q->z > p->maxZ + 2e-3f) linksOk = false;
            bool mirror = false;
            for (const NavMesh::Link& m : out.links) if (m.from == l.to && m.to == l.from) { mirror = true; break; }
            if (!mirror) linksOk = false;
        }
        CHECK(linksOk);
        // Heights come from the original floor.
        CHECK_NEAR(out.SampleHeight({3, 0, 3}, -9), 0.0f, 1e-3f);
        // Paths and raycasts on the carved mesh go around the hole; on the baked mesh they go straight.
        std::vector<Vec3> straight, around;
        CHECK(base.FindPath({-5, 0, 0}, {5, 0, 0}, straight));
        CHECK(out.FindPath({-5, 0, 0}, {5, 0, 0}, around));
        CHECK(PathLength(straight) < 10.1f);
        CHECK(PathLength(around) > 10.4f && PathLength(around) < 12.5f);
        CHECK(Crosses(straight, -1.3f, 1.3f, -1.3f, 1.3f));
        CHECK(!Crosses(around, -1.39f, 1.39f, -1.39f, 1.39f));
        Vec3 hit;
        CHECK(!base.Raycast({-5, 0, 0}, {5, 0, 0}, hit));
        CHECK(out.Raycast({-5, 0, 0}, {5, 0, 0}, hit) && hit.x < -1.3f);
        // A goal inside the hole snaps to the rim, not into it.
        std::vector<Vec3> intoHole;
        if (out.FindPath({-5, 0, 0}, {0, 0, 0}, intoHole))
            CHECK(std::fabs(intoHole.back().x) >= 1.39f || std::fabs(intoHole.back().z) >= 1.39f);

        // A volume that does not stand on this floor carves nothing: floating above head room, or buried below it.
        NavMesh same;
        CHECK(base.Carve({BoxVolume(-1, 1, -1, 1, 3.0f, 5.0f)}, same) == 0);
        CHECK(same.polys.size() == basePolys && std::fabs(same.Area() - baseArea) < 1e-3f);
        CHECK(base.Carve({BoxVolume(-1, 1, -1, 1, -4.0f, -1.0f)}, same) == 0);
        CHECK(base.Carve({}, same) == 0 && same.Serialize() == baked);   // no volumes: an exact copy
        // Two volumes, one off the edge of the mesh: only the one on it cuts.
        CHECK(base.Carve({BoxVolume(-1, 1, -1, 1), BoxVolume(40, 42, 40, 42)}, same) >= 1);
        CHECK_NEAR(baseArea - same.Area(), 2.8f * 2.8f, 0.1f);
        // Fully covering the mesh leaves nothing (and nothing crashes).
        CHECK(base.Carve({BoxVolume(-30, 30, -30, 30)}, same) >= 1 && same.Empty());
        // Overlapping volumes cut the union once.
        CHECK(base.Carve({BoxVolume(-1, 1, -1, 1), BoxVolume(0, 2, 0, 2)}, same) >= 1);
        CHECK(baseArea - same.Area() > 2.8f * 2.8f && baseArea - same.Area() < 2.0f * 2.8f * 2.8f);
        CHECK(!Inside(same, 2.2f, 2.2f) && Inside(same, 2.6f, 0.0f) && Inside(same, -1.7f, -1.7f));
    }

    // ---- A rotated box carves its diamond, a capsule its circle ----------------------------------------------------
    {
        Scene s("shapes"); s.physicsEnabled = false;
        Box(s, "Ground", {0, -0.5f, 0}, {20, 1, 20});
        NavMeshSurface* ns = Surface(s);
        CHECK(ns->Bake(s));
        auto* rot = Cart(s, {0, 1, 0}, {2, 2, 2});
        rot->transform->localRotation = Quat::Euler(0, 45, 0);
        const NavCarveVolume v = rot->LiveVolume();
        CHECK(v.count == 4);
        CHECK_NEAR(v.minY, 0.0f, 1e-4f); CHECK_NEAR(v.maxY, 2.0f, 1e-4f);
        NavMesh out;
        CHECK(ns->Mesh()->Carve({v}, out) >= 1);
        CHECK(!Inside(out, 0, 0) && !Inside(out, 1.5f, 0.0f));          // diamond reach 1.414 + 0.4
        CHECK(Inside(out, 1.5f, 1.5f));                                  // the square's corner region is open: it is a diamond
        CHECK(Inside(out, 2.0f, 0.0f));

        auto* cap = Cart(s, {5, 1, 5}, {1, 1, 1});
        cap->shape = NavMeshObstacle::Shape::Capsule; cap->capsuleRadius = 0.5f; cap->capsuleHeight = 2.0f;
        const NavCarveVolume cv = cap->LiveVolume();
        CHECK(cv.count == NavCarveVolume::kMaxPoints);
        CHECK_NEAR(cv.minY, 0.0f, 1e-4f); CHECK_NEAR(cv.maxY, 2.0f, 1e-4f);
        CHECK_NEAR(cap->Radius(), 0.5f, 1e-5f);
        CHECK(ns->Mesh()->Carve({cv}, out) >= 1);
        CHECK(!Inside(out, 5, 5) && !Inside(out, 5.8f, 5.0f));          // radius 0.5 + 0.4
        CHECK(Inside(out, 5.95f, 5.0f) && Inside(out, 5, 6.0f));
        cap->transform->localScale = {2, 1, 2};                          // the capsule scales with the object
        CHECK_NEAR(cap->Radius(), 1.0f, 1e-5f);
    }

    // ---- Corridor: an obstacle across the only way leaves no path -------------------------------------------------
    {
        Scene s("corridor"); s.physicsEnabled = false;
        Box(s, "Floor", {0, -0.5f, 0}, {20, 1, 2.4f});
        NavMeshSurface* ns = Surface(s);
        CHECK(ns->Bake(s));
        NavMesh out; std::vector<Vec3> p;
        CHECK(ns->Mesh()->FindPath({-8, 0, 0}, {8, 0, 0}, p));
        CHECK(ns->Mesh()->Carve({BoxVolume(-0.5f, 0.5f, -1.2f, 1.2f)}, out) >= 1);
        CHECK(!out.FindPath({-8, 0, 0}, {8, 0, 0}, p));                    // cut in two
        CHECK(out.FindPath({-8, 0, 0}, {-3, 0, 0}, p) && out.FindPath({3, 0, 0}, {8, 0, 0}, p));   // each side is still whole
    }

    // ---- Stationary timing: only a parked obstacle carves; moving fills the hole again --------------------------------
    {
        Scene s("timing"); s.physicsEnabled = false;
        Box(s, "Ground", {0, -0.5f, 0}, {20, 1, 20});
        NavMeshSurface* ns = Surface(s);
        CHECK(ns->Bake(s));
        NavMeshObstacle* o = Cart(s, {0, 1, 8}, {2, 2, 2}, 0.5f);
        s.Start();
        const NavMesh* baked = ns->Mesh();
        CHECK(Navigation::Mesh(s) == baked && Navigation::BakedMesh(s) == baked);   // nothing carved yet
        CHECK(!o->Carving());
        o->transform->localPosition = {0, 1, 0};
        Tick(s, 20);                                                    // 0.33 s still: not yet
        CHECK(!o->Carving() && Navigation::Mesh(s) == baked);
        CHECK(o->StationaryTime() > 0.2f && o->StationaryTime() < 0.5f);
        Tick(s, 20);                                                    // past 0.5 s
        CHECK(o->Carving());
        const NavMesh* carved = Navigation::Mesh(s);
        CHECK(carved != baked && !Inside(*carved, 0, 0));
        CHECK(Navigation::BakedMesh(s) == baked && Inside(*baked, 0, 0));   // the baked mesh keeps its floor
        const std::uint32_t gen = Navigation::CarveGeneration(s);
        Tick(s, 30);
        CHECK(Navigation::CarveGeneration(s) == gen && Navigation::Mesh(s) == carved);   // no rebuild while it sits
        // It starts to move: the hole closes at once and it is only dodged locally.
        o->transform->localPosition = {0.5f, 1, 0};
        Tick(s, 1);
        CHECK(!o->Carving() && Navigation::Mesh(s) == baked && Navigation::CarveGeneration(s) != gen);
        for (int i = 0; i < 30; ++i) { o->transform->localPosition = {0.5f + 0.1f * (float)(i + 1), 1, 0}; Tick(s, 1); }
        CHECK(!o->Carving() && Navigation::Mesh(s) == baked);           // keeps moving: never carves
        const std::uint32_t moving = Navigation::CarveGeneration(s);
        Tick(s, 40);                                                    // parks at x = 3.5
        CHECK(o->Carving() && Navigation::Mesh(s) != baked && Navigation::CarveGeneration(s) != moving);
        CHECK(!Inside(*Navigation::Mesh(s), 3.5f, 0) && Inside(*Navigation::Mesh(s), 0, 0));
        // Switched off or deactivated: no hole, and the generation says so.
        const std::uint32_t before = Navigation::CarveGeneration(s);
        o->enabled = false;
        CHECK(Navigation::Mesh(s) == baked && Navigation::CarveGeneration(s) != before);
        o->enabled = true;
        CHECK(Navigation::Mesh(s) != baked);
        o->gameObject->active = false;
        CHECK(Navigation::Mesh(s) == baked);
        o->gameObject->active = true;
        // Carve off: never carves.
        o->carve = false;
        Tick(s, 3);
        CHECK(!o->Carving() && Navigation::Mesh(s) == baked);
        // timeToStationary 0: carves on the first still frame.
        o->carve = true; o->timeToStationary = 0.0f;
        Tick(s, 2);
        CHECK(o->Carving());
        // Baking again drops the old carve and the obstacle cuts the new mesh.
        CHECK(ns->Bake(s));
        CHECK(Navigation::Mesh(s) != nullptr && !Inside(*Navigation::Mesh(s), 3.5f, 0) && Inside(*Navigation::BakedMesh(s), 3.5f, 0));
    }

    // ---- Follow mode: the hole follows the obstacle, rebuilt after it has moved moveThreshold ----------------------------
    {
        Scene s("follow"); s.physicsEnabled = false;
        Box(s, "Ground", {0, -0.5f, 0}, {20, 1, 20});
        Surface(s)->Bake(s);
        NavMeshObstacle* o = Cart(s, {0, 1, 0}, {2, 2, 2});
        o->carveOnlyWhenStationary = false; o->moveThreshold = 0.5f;
        s.Start();
        Tick(s, 2);
        CHECK(o->Carving());                                            // carved at once, not waiting for it to stop
        const std::uint32_t g0 = Navigation::CarveGeneration(s);
        o->transform->localPosition = {0.2f, 1, 0}; Tick(s, 1);
        o->transform->localPosition = {0.4f, 1, 0}; Tick(s, 1);
        CHECK(Navigation::CarveGeneration(s) == g0);                    // inside the threshold: the old hole stays
        CHECK(!Inside(*Navigation::Mesh(s), 1.3f, 0) && Inside(*Navigation::Mesh(s), 1.6f, 0));
        o->transform->localPosition = {0.7f, 1, 0}; Tick(s, 1);
        CHECK(Navigation::CarveGeneration(s) != g0);                    // moved past the threshold: the hole moved
        CHECK(Inside(*Navigation::Mesh(s), -1.0f, 0));                  // ...and the old hole's far side is open again
        CHECK(!Inside(*Navigation::Mesh(s), 2.0f, 0));
    }

    // ---- Agents: a parked obstacle is planned around (a moving one is not) --------------------------------------------
    {
        Scene s("plan"); s.physicsEnabled = false;
        Box(s, "Ground", {0, -0.5f, 0}, {20, 1, 20});
        Surface(s)->Bake(s);
        NavMeshObstacle* wall = Cart(s, {0, 1, 0}, {1, 2, 8});
        GameObject* a = s.CreateGameObject("A"); a->transform->localPosition = {-6, 0, 0};
        auto* agent = a->AddComponent<NavMeshAgent>(); agent->speed = 4.0f; agent->avoidanceRange = 0.0f;
        s.Start();
        Tick(s, 20);                                                    // the wall has been still for 0.33 s: carving
        CHECK(wall->Carving());
        agent->SetDestination({6, 0, 0});
        Tick(s, 2);
        CHECK(agent->Path().size() >= 3);
        CHECK(!Crosses(agent->Path(), -0.5f, 0.5f, -4.0f, 4.0f));       // the planned route avoids the wall
        CHECK(PathLength(agent->Path()) > 14.5f);                // and is a real detour around its end (z = 4)
        float traveled = 0.0f; Vec3 last = a->transform->Position(); bool entered = false; int frames = 0;
        while (agent->HasPath() && frames < 1200) {
            Tick(s, 1); ++frames;
            const Vec3 p = a->transform->Position();
            traveled += Dist2D(p, last); last = p;
            if (p.x > -0.5f && p.x < 0.5f && p.z > -4.0f && p.z < 4.0f) entered = true;
        }
        CHECK(agent->Arrived() && !entered && traveled > 14.5f);
        CHECK(Dist2D(a->transform->Position(), {6, 0, 0}) < 0.5f);

        // The same wall with Carve off: the path search goes straight through it (agents only dodge it locally).
        wall->carve = false;
        a->transform->localPosition = {-6, 0, 0};
        agent->Warp({-6, 0, 0});
        Tick(s, 2);
        CHECK(Navigation::Mesh(s) == Navigation::BakedMesh(s));
        agent->SetDestination({6, 0, 0});
        Tick(s, 2);
        CHECK(Crosses(agent->Path(), -0.5f, 0.5f, -4.0f, 4.0f));
    }

    // ---- An agent already on its way replans when an obstacle parks in front of it -------------------------------------
    {
        Scene s("repath"); s.physicsEnabled = false;
        Box(s, "Ground", {0, -0.5f, 0}, {20, 1, 20});
        Surface(s)->Bake(s);
        NavMeshObstacle* wall = Cart(s, {0, 1, 30}, {1, 2, 8}, 0.3f);      // starts far off the mesh
        GameObject* a = s.CreateGameObject("A"); a->transform->localPosition = {-8, 0, 0};
        auto* agent = a->AddComponent<NavMeshAgent>(); agent->speed = 3.0f; agent->avoidanceRange = 0.0f; agent->autoRepath = false;   // only the carve may trigger a replan
        s.Start();
        agent->SetDestination({8, 0, 0});
        Tick(s, 60);                                                    // one second: walking along z = 0
        CHECK(agent->HasPath() && a->transform->Position().x > -6.0f && a->transform->Position().x < -3.0f);
        CHECK(Crosses(agent->Path(), -0.5f, 0.5f, -4.0f, 4.0f));        // the straight route, through where the wall will be
        const std::size_t cornersBefore = agent->Path().size();
        wall->transform->localPosition = {0, 1, 0};                      // it parks across the route
        Tick(s, 5);
        CHECK(!wall->Carving() && Crosses(agent->Path(), -0.5f, 0.5f, -4.0f, 4.0f));   // 0.08 s: not stationary yet
        Tick(s, 20);                                                    // past 0.3 s
        CHECK(wall->Carving());
        Tick(s, 1);
        CHECK(!Crosses(agent->Path(), -0.5f, 0.5f, -4.0f, 4.0f) && agent->Path().size() > cornersBefore - 1);   // replanned at once, no timer needed
        bool entered = false; int frames = 0;
        while (agent->HasPath() && frames < 1500) {
            Tick(s, 1); ++frames;
            const Vec3 p = a->transform->Position();
            if (p.x > -0.5f && p.x < 0.5f && p.z > -4.0f && p.z < 4.0f) entered = true;
        }
        CHECK(agent->Arrived() && !entered && Dist2D(a->transform->Position(), {8, 0, 0}) < 0.5f);

        // Nothing left to walk: the goal is cut off, the agent gives up instead of walking into the obstacle.
        Scene c("blocked"); c.physicsEnabled = false;
        Box(c, "Floor", {0, -0.5f, 0}, {20, 1, 2.4f});
        Surface(c)->Bake(c);
        NavMeshObstacle* gate = Cart(c, {0, 1, 0}, {1, 2, 4}, 0.1f);
        GameObject* b = c.CreateGameObject("B"); b->transform->localPosition = {-8, 0, 0};
        auto* ab = b->AddComponent<NavMeshAgent>(); ab->avoidanceRange = 0.0f;
        c.Start();
        Tick(c, 20);
        CHECK(gate->Carving());
        ab->SetDestination({8, 0, 0});
        Tick(c, 5);
        CHECK(!ab->HasPath() && !ab->Arrived());
        gate->carve = false;                                            // the gate opens: a new order works
        Tick(c, 2);
        ab->SetDestination({8, 0, 0});
        Tick(c, 5);
        CHECK(ab->HasPath());
    }

    // ---- NPC and click-to-move controllers path around a carved obstacle through the same mesh -------------------------
    {
        Scene s("npc"); s.physicsEnabled = false;
        Box(s, "Ground", {0, -0.5f, 0}, {20, 1, 20});
        Surface(s)->Bake(s);
        Cart(s, {0, 1, 0}, {1, 2, 8});
        GameObject* g = s.CreateGameObject("Guard"); g->transform->localPosition = {-6, 0, 0};
        auto* npc = g->AddComponent<NPCController>();
        npc->behavior = (int)NPCController::Behavior::Idle; npc->usePathfinding = true; npc->targetName = "nobody";
        s.Start();
        Tick(s, 20);
        npc->CommandGoTo({6, 0, 0});
        Tick(s, 5);
        CHECK(npc->CurrentPath().size() >= 3);
        CHECK(!Crosses(npc->CurrentPath(), -0.5f, 0.5f, -4.0f, 4.0f));
    }

    // ---- Scene file ---------------------------------------------------------------------------------------------------
    {
        Scene s("records"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Pillar");
        auto* o = g->AddComponent<NavMeshObstacle>();
        o->shape = NavMeshObstacle::Shape::Capsule; o->size = {2, 3, 4}; o->center = {0, 1, 0};
        o->capsuleRadius = 0.75f; o->capsuleHeight = 3.5f; o->carve = true; o->carveOnlyWhenStationary = false;
        o->timeToStationary = 1.5f; o->moveThreshold = 0.35f;
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("navobstacle") != std::string::npos && text.find("shape=1") != std::string::npos && text.find("carve=1") != std::string::npos);
        Scene s2("records2"); std::string err;
        CHECK(SceneSerializer::Deserialize(s2, text, &err));
        auto* o2 = s2.Find("Pillar") ? s2.Find("Pillar")->GetComponent<NavMeshObstacle>() : nullptr;
        CHECK(o2 != nullptr);
        if (o2) {
            CHECK(o2->shape == NavMeshObstacle::Shape::Capsule);
            CHECK_NEAR(o2->size.z, 4.0f, 1e-5f); CHECK_NEAR(o2->center.y, 1.0f, 1e-5f);
            CHECK_NEAR(o2->capsuleRadius, 0.75f, 1e-5f); CHECK_NEAR(o2->capsuleHeight, 3.5f, 1e-5f);
            CHECK(o2->carve && !o2->carveOnlyWhenStationary);
            CHECK_NEAR(o2->timeToStationary, 1.5f, 1e-5f); CHECK_NEAR(o2->moveThreshold, 0.35f, 1e-5f);
        }
        CHECK(SceneSerializer::Serialize(s2) == text);
        // A scene from before carving (no new keys) loads with the old behaviour: a box that does not carve.
        const std::size_t at = text.find(" shape=");
        const std::size_t eol = text.find('\n', at);
        CHECK(at != std::string::npos && eol != std::string::npos);
        const std::string old = text.substr(0, at) + text.substr(eol);
        Scene s3("old");
        CHECK(SceneSerializer::Deserialize(s3, old, &err));
        auto* o3 = s3.Find("Pillar") ? s3.Find("Pillar")->GetComponent<NavMeshObstacle>() : nullptr;
        CHECK(o3 != nullptr);
        if (o3) {
            CHECK(o3->shape == NavMeshObstacle::Shape::Box && !o3->carve && o3->carveOnlyWhenStationary);
            CHECK_NEAR(o3->timeToStationary, 0.5f, 1e-5f); CHECK_NEAR(o3->moveThreshold, 0.1f, 1e-5f);
            CHECK_NEAR(o3->size.z, 4.0f, 1e-5f);
        }
        // Hostile values from a hand-edited file cannot break it.
        std::string bad = text;
        const std::size_t sh = bad.find("shape=1");
        bad.replace(sh, 7, "shape=99");
        Scene s4("bad");
        CHECK(SceneSerializer::Deserialize(s4, bad, &err));
        auto* o4 = s4.Find("Pillar") ? s4.Find("Pillar")->GetComponent<NavMeshObstacle>() : nullptr;
        CHECK(o4 && (o4->shape == NavMeshObstacle::Shape::Box || o4->shape == NavMeshObstacle::Shape::Capsule));
    }

    // ---- Validator -------------------------------------------------------------------------------------------------------
    {
        ValidatorOptions opt; opt.checkAssets = false;
        {   // carving with no surface at all, and a flat box
            Scene s("v1"); s.physicsEnabled = false;
            s.CreateGameObject("Camera")->AddComponent<Camera>();
            Cart(s, {0, 0, 0}, {1, 1, 1});
            NavMeshObstacle* flat = Cart(s, {3, 0, 0}, {1, 0, 1});
            flat->carve = false;
            ValidationReport r = SceneValidator::Validate(s, opt);
            CHECK(r.CountCode("navobstacle.carve_no_surface") == 1);
            CHECK(r.CountCode("navobstacle.zero_size") == 1);
            CHECK(r.CountCode("navobstacle.carve_no_mesh") == 0);
        }
        {   // a surface that was never baked
            Scene s("v2"); s.physicsEnabled = false;
            s.CreateGameObject("Camera")->AddComponent<Camera>();
            Surface(s);
            NavMeshObstacle* c = Cart(s, {0, 0, 0}, {1, 1, 1});
            c->carveOnlyWhenStationary = false; c->moveThreshold = 0.0f;
            c->shape = NavMeshObstacle::Shape::Capsule; c->capsuleRadius = 0.0f;
            ValidationReport r = SceneValidator::Validate(s, opt);
            CHECK(r.CountCode("navobstacle.carve_no_mesh") == 1);
            CHECK(r.CountCode("navobstacle.carve_every_frame") == 1);
            CHECK(r.CountCode("navobstacle.zero_size") == 1);        // a capsule with no radius
            CHECK(r.CountCode("navobstacle.carve_no_surface") == 0);
        }
        {   // a baked surface and a sound obstacle: nothing to report
            Scene s("v3"); s.physicsEnabled = false;
            s.CreateGameObject("Camera")->AddComponent<Camera>();
            Box(s, "Ground", {0, -0.5f, 0}, {20, 1, 20});
            CHECK(Surface(s)->Bake(s));
            Cart(s, {0, 1, 0}, {1, 2, 2});
            ValidationReport r = SceneValidator::Validate(s, opt);
            for (const auto& i : r.issues) CHECK(i.code.rfind("navobstacle.", 0) != 0);
        }
    }

    TEST_MAIN_RESULT();
}
