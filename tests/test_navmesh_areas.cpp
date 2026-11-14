// Navigation areas and off-mesh links: area types baked from NavMeshModifier, per-area traversal costs and
// per-agent area masks honoured by the path search, NavMeshLinks (jump / drop / ladder) used as extra edges
// and crossed by the agent, and the scene file round trip.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/SceneSerializer.hpp"
#include <cmath>
#include <string>
#include <vector>

using namespace okay;

static GameObject* Box(Scene& s, const char* name, Vec3 pos, Vec3 scale) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = pos; g->transform->localScale = scale;
    g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    return g;
}
// A slab whose top is at `top`, covering [x0,x1] x [z0,z1].
static GameObject* Slab(Scene& s, const char* name, float x0, float x1, float z0, float z1, float top = 0.0f, float thick = 1.0f) {
    return Box(s, name, {(x0 + x1) * 0.5f, top - thick * 0.5f, (z0 + z1) * 0.5f}, {x1 - x0, thick, z1 - z0});
}
static GameObject* Area(GameObject* g, int area) { g->AddComponent<NavMeshModifier>()->area = area; return g; }
static NavMeshSurface* Surface(Scene& s) {
    auto* ns = s.CreateGameObject("Nav Mesh")->AddComponent<NavMeshSurface>();
    ns->settings.agentRadius = 0.4f; ns->settings.cellSize = 0.25f;
    return ns;
}
static float PathLength(const std::vector<Vec3>& p) {
    float l = 0.0f;
    for (std::size_t i = 1; i < p.size(); ++i) l += std::sqrt((p[i].x - p[i-1].x) * (p[i].x - p[i-1].x) + (p[i].z - p[i-1].z) * (p[i].z - p[i-1].z));
    return l;
}
static float Dist2D(const Vec3& a, const Vec3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }
static bool Crosses(const std::vector<Vec3>& p, float x0, float x1, float z0, float z1) {
    for (std::size_t i = 1; i < p.size(); ++i)
        for (int k = 0; k <= 60; ++k) {
            float t = k / 60.0f;
            float x = p[i-1].x + (p[i].x - p[i-1].x) * t, z = p[i-1].z + (p[i].z - p[i-1].z) * t;
            if (x > x0 && x < x1 && z > z0 && z < z1) return true;
        }
    return false;
}
static void Tick(Scene& s, int n) { for (int i = 0; i < n; ++i) s.Update(1.0f / 60.0f); }

// Two platforms with a 2 m gap, joined the long way round by a walkway. Link points (-2,0,0) -> (2,0,0).
static NavMeshLink* GapScene(Scene& s, bool walkway) {
    Slab(s, "Left", -10, -1, -5, 5);
    Slab(s, "Right", 1, 10, -5, 5);
    if (walkway) {
        Slab(s, "WL", -10, -8, -12, -5);
        Slab(s, "WB", -10, 10, -12, -8);
        Slab(s, "WR", 8, 10, -12, -5);
    }
    Surface(s)->Bake(s);
    GameObject* g = s.CreateGameObject("Link");
    auto* l = g->AddComponent<NavMeshLink>();
    l->startPoint = {-2, 0, 0}; l->endPoint = {2, 0, 0};
    return l;
}

int main() {
    RUN_SUITE("navmesh_areas");

    // ---- Area names and defaults.
    CHECK(std::string(NavAreaDefaultName(NavArea::Water)) == "Water");
    CHECK(std::string(NavAreaDefaultName(NavArea::Road)) == "Road");
    CHECK(NavAreaDefaultCost(NavArea::Road) < 1.0f && NavAreaDefaultCost(NavArea::Water) > 1.0f);
    CHECK((kNavDefaultAreaMask & (1u << NavArea::NotWalkable)) == 0 && (kNavDefaultAreaMask & (1u << NavArea::Water)) != 0);

    // ---- The bake stores each collider's modifier area on its polygons; a NotWalkable collider is a hole.
    {
        Scene s("bake"); s.physicsEnabled = false;
        Slab(s, "Floor", -10, 10, -10, 10);
        Area(Slab(s, "Pond", -3, 3, -3, 3, 0.0f, 0.1f), NavArea::Water);
        Area(Slab(s, "Pit", 5, 8, 5, 8, 0.0f, 0.1f), NavArea::NotWalkable);
        GameObject* parent = s.CreateGameObject("Meadow");
        Area(parent, 4);
        GameObject* kid = Slab(s, "MeadowPatch", -9, -6, 6, 9, 0.0f, 0.1f);
        kid->transform->SetParent(parent->transform);
        NavMeshSurface* ns = Surface(s);
        CHECK(ns->Bake(s));
        const NavMesh* m = ns->Mesh();
        CHECK(m != nullptr);
        int water = 0, walk = 0, custom = 0, bad = 0;
        for (const NavMesh::Poly& p : m->polys) {
            if (p.area == NavArea::Water) ++water; else if (p.area == NavArea::Walkable) ++walk;
            else if (p.area == 4) ++custom; else ++bad;
        }
        CHECK(water > 0 && walk > 0 && custom > 0 && bad == 0);
        CHECK(m->polys[(std::size_t)m->FindNearestPoly({0, 0, 0}, {0.5f, 1, 0.5f})].area == NavArea::Water);
        CHECK(m->polys[(std::size_t)m->FindNearestPoly({-8, 0, 8}, {0.5f, 1, 0.5f})].area == 4);   // inherited from the parent
        CHECK(m->FindNearestPoly({6.5f, 0, 6.5f}, {0.5f, 1, 0.5f}) < 0);                           // the pit has no mesh
        // The area survives the .okaynav text, and a file without the area number still loads.
        NavMesh back; CHECK(back.Deserialize(m->Serialize()));
        bool same = back.polys.size() == m->polys.size();
        for (std::size_t i = 0; same && i < back.polys.size(); ++i) same = back.polys[i].area == m->polys[i].area;
        CHECK(same);
        std::string text = m->Serialize(), old;
        std::size_t pos = 0;
        while (pos < text.size()) {
            std::size_t e = text.find('\n', pos); if (e == std::string::npos) e = text.size();
            std::string line = text.substr(pos, e - pos);
            if (line.rfind("p ", 0) == 0) line = line.substr(0, line.rfind(' '));
            old += line + "\n"; pos = e + 1;
        }
        NavMesh legacy; CHECK(legacy.Deserialize(old));
        CHECK(legacy.polys.size() == m->polys.size());
        bool allZero = true; for (auto& p : legacy.polys) allZero = allZero && p.area == 0;
        CHECK(allZero);
    }

    // ---- Costs: a swamp in the way is walked around, until it is made cheap; a road beats the shorter swamp.
    {
        Scene s("swamp"); s.physicsEnabled = false;
        Slab(s, "Floor", -15, 15, -15, 15);
        Area(Slab(s, "Swamp", -2, 2, -4, 4, 0.0f, 0.1f), NavArea::Water);
        NavMeshSurface* ns = Surface(s);
        CHECK(ns->Bake(s));
        const NavMesh* m = ns->Mesh();
        std::vector<Vec3> p;
        (void)m;
        ns->areaCost[NavArea::Water] = 10.0f;                           // a deep swamp: walk round it (the 16 m line would cost 52)
        CHECK(Navigation::FindPath(s, {-8, 0, 0}, {8, 0, 0}, p));
        CHECK(PathLength(p) > 18.0f);
        CHECK(!Crosses(p, -1.9f, 1.9f, -3.9f, 3.9f));
        ns->areaCost[NavArea::Water] = 1.0f;                            // no extra cost: the straight line
        CHECK(Navigation::FindPath(s, {-8, 0, 0}, {8, 0, 0}, p));
        CHECK(PathLength(p) < 17.0f);
        CHECK(Crosses(p, -1.9f, 1.9f, -3.9f, 3.9f));
    }
    {
        Scene s("road"); s.physicsEnabled = false;
        Area(Slab(s, "Marsh", -15, 15, -15, 15), NavArea::Water);                       // everything is swamp ...
        Area(Slab(s, "RoadN", -9, 9, 4, 6, 0.0f, 0.1f), NavArea::Road);                  // ... except a longer road around
        Area(Slab(s, "RoadW", -9, -7, 0, 6, 0.0f, 0.1f), NavArea::Road);
        Area(Slab(s, "RoadE", 7, 9, 0, 6, 0.0f, 0.1f), NavArea::Road);
        NavMeshSurface* ns = Surface(s);
        CHECK(ns->Bake(s));
        std::vector<Vec3> p;
        CHECK(Navigation::FindPath(s, {-8, 0, 0.5f}, {8, 0, 0.5f}, p));
        CHECK(PathLength(p) > 19.0f);                                   // the road is longer than the 16 m swamp crossing
        { float maxZ = 0.0f; for (const Vec3& q : p) maxZ = std::fmax(maxZ, q.z); CHECK(maxZ > 3.9f); }   // it went up to the road
        CHECK(!Crosses(p, -6.0f, 6.0f, -3.0f, 3.0f));
        // Equal costs: the shortest route wins instead.
        ns->areaCost[NavArea::Water] = 1.0f; ns->areaCost[NavArea::Road] = 1.0f;
        CHECK(Navigation::FindPath(s, {-8, 0, 0.5f}, {8, 0, 0.5f}, p));
        CHECK(PathLength(p) < 17.5f);
    }

    // ---- Blocked areas and per-agent masks.
    {
        Scene s("mask"); s.physicsEnabled = false;
        Slab(s, "Floor", -15, 15, -15, 15);
        Area(Slab(s, "Wall of mud", -1, 1, -15, 15, 0.0f, 0.1f), NavArea::Water);       // a river across the whole level
        Area(Slab(s, "Trap", 4, 7, -15, -2, 0.0f, 0.1f), NavArea::NotWalkable);
        NavMeshSurface* ns = Surface(s);
        CHECK(ns->Bake(s));
        std::vector<Vec3> p; std::vector<NavMeshLink*> legs;
        const std::uint32_t noWater = kNavDefaultAreaMask & ~(1u << NavArea::Water);
        CHECK(Navigation::FindPathForAgent(s, {-8, 0, -8}, {8, 0, -8}, p, legs, kNavDefaultAreaMask));
        CHECK(!Crosses(p, 4.0f, 7.0f, -15.0f, -2.0f));                  // the not-walkable trap is never entered
        CHECK(!Navigation::FindPathForAgent(s, {-8, 0, -8}, {8, 0, -8}, p, legs, noWater));   // cannot cross the river
        CHECK(!Navigation::FindPathForAgent(s, {-8, 0, -8}, {0, 0, -8}, p, legs, noWater));   // goal inside a forbidden area
        CHECK(Navigation::FindPathForAgent(s, {-8, 0, -8}, {-3, 0, 4}, p, legs, noWater));    // same bank: fine
        // Two agents, two masks.
        GameObject* ga = s.CreateGameObject("Wader"); ga->transform->localPosition = {-8, 0, -8};
        GameObject* gb = s.CreateGameObject("Dry"); gb->transform->localPosition = {-8, 0, -10};
        auto* wader = ga->AddComponent<NavMeshAgent>(); auto* dry = gb->AddComponent<NavMeshAgent>();
        wader->speed = dry->speed = 6.0f; wader->avoidanceRange = dry->avoidanceRange = 0.0f;
        dry->areaMask = noWater;
        s.Start();
        wader->SetDestination({-8, 0, -8}); dry->SetDestination({-8, 0, -10});   // settle
        Tick(s, 2);
        wader->SetDestination({8, 0, -8}); dry->SetDestination({8, 0, -10});
        Tick(s, 600);
        CHECK(wader->Arrived());
        CHECK(!dry->Arrived() && !dry->HasPath());                      // no way for it: gave up
        CHECK(gb->transform->Position().x < -1.0f);                     // never crossed the river
    }

    // ---- Off-mesh links in the path search.
    {
        Scene s("gap"); s.physicsEnabled = false;
        NavMeshLink* link = GapScene(s, false);
        std::vector<Vec3> p; std::vector<NavMeshLink*> legs;
        const std::uint32_t all = kNavDefaultAreaMask;
        CHECK(Navigation::FindPathForAgent(s, {-6, 0, 0}, {6, 0, 0}, p, legs, all));
        CHECK(legs.size() == p.size());
        int jumps = 0; std::size_t at = 0;
        for (std::size_t i = 0; i < legs.size(); ++i) if (legs[i]) { ++jumps; at = i; }
        CHECK(jumps == 1 && legs[at] == link && at >= 1);
        if (at >= 1) { CHECK_NEAR(p[at - 1].x, -2.0f, 0.05f); CHECK_NEAR(p[at].x, 2.0f, 0.05f); }
        CHECK(Navigation::FindPath(s, {-6, 0, 0}, {6, 0, 0}, p) == false || p.empty());   // plain walkers cannot cross the gap
        link->enabled = false;
        CHECK(!Navigation::FindPathForAgent(s, {-6, 0, 0}, {6, 0, 0}, p, legs, all));
        link->enabled = true;
        link->gameObject->active = false;
        CHECK(!Navigation::FindPathForAgent(s, {-6, 0, 0}, {6, 0, 0}, p, legs, all));
        link->gameObject->active = true;
        link->area = 5;                                                 // a custom area an agent can opt out of
        CHECK(Navigation::FindPathForAgent(s, {-6, 0, 0}, {6, 0, 0}, p, legs, all));
        CHECK(!Navigation::FindPathForAgent(s, {-6, 0, 0}, {6, 0, 0}, p, legs, all & ~(1u << 5)));
        link->area = 0;
        // One way: start -> end only.
        link->bidirectional = false;
        CHECK(Navigation::FindPathForAgent(s, {-6, 0, 0}, {6, 0, 0}, p, legs, all));
        CHECK(!Navigation::FindPathForAgent(s, {6, 0, 0}, {-6, 0, 0}, p, legs, all));
        link->bidirectional = true;
        CHECK(Navigation::FindPathForAgent(s, {6, 0, 0}, {-6, 0, 0}, p, legs, all));
        bool viaLink = false; for (auto* l : legs) viaLink = viaLink || l == link;
        CHECK(viaLink);
    }
    {
        Scene s("gap2"); s.physicsEnabled = false;
        NavMeshLink* link = GapScene(s, true);
        std::vector<Vec3> p; std::vector<NavMeshLink*> legs;
        const std::uint32_t all = kNavDefaultAreaMask;
        auto uses = [&]() { for (auto* l : legs) if (l) return true; return false; };
        CHECK(Navigation::FindPathForAgent(s, {-6, 0, 0}, {6, 0, 0}, p, legs, all));
        CHECK(uses());                                                  // shortens the route: used
        CHECK(PathLength(p) < 20.0f);
        link->enabled = false;
        CHECK(Navigation::FindPathForAgent(s, {-6, 0, 0}, {6, 0, 0}, p, legs, all));
        CHECK(!uses() && PathLength(p) > 30.0f);                        // disabled: the long way round
        link->enabled = true; link->area = 5;
        CHECK(Navigation::FindPathForAgent(s, {-6, 0, 0}, {6, 0, 0}, p, legs, all & ~(1u << 5)));
        CHECK(!uses() && PathLength(p) > 30.0f);                        // mask forbids it
        link->area = 0; link->cost = 50.0f;
        CHECK(Navigation::FindPathForAgent(s, {-6, 0, 0}, {6, 0, 0}, p, legs, all));
        CHECK(!uses());                                                 // too expensive to be worth it
        link->cost = 1.0f; link->bidirectional = false;
        CHECK(Navigation::FindPathForAgent(s, {-6, 0, 0}, {6, 0, 0}, p, legs, all));
        CHECK(uses() && PathLength(p) < 20.0f);
        CHECK(Navigation::FindPathForAgent(s, {6, 0, 0}, {-6, 0, 0}, p, legs, all));
        CHECK(!uses() && PathLength(p) > 30.0f);                        // one way: back along the walkway
        link->bidirectional = true;
        CHECK(Navigation::FindPathForAgent(s, {6, 0, 0}, {-6, 0, 0}, p, legs, all));
        CHECK(uses() && PathLength(p) < 20.0f);
    }

    // ---- Crossing: the agent walks to the start, jumps over time, lands on the end, then walks on; events in order.
    {
        Scene s("cross"); s.physicsEnabled = false;
        NavMeshLink* link = GapScene(s, false);
        link->type = NavMeshLink::Type::Jump; link->arcHeight = 1.0f;
        GameObject* go = s.CreateGameObject("Jumper");
        go->transform->localPosition = {-6, 0, 0};
        auto* agent = go->AddComponent<NavMeshAgent>();
        agent->speed = 4.0f; agent->avoidanceRange = 0.0f; agent->stoppingDistance = 0.3f;
        std::vector<std::string> log; Vec3 atStart, atEnd; int startFrame = -1, endFrame = -1, frame = 0;
        agent->onLinkStart = [&](NavMeshAgent& a, NavMeshLink& l) {
            log.push_back("start"); atStart = a.gameObject->transform->Position();
            CHECK(&l == link); CHECK(a.IsTraversingLink()); CHECK(a.CurrentLink() == link); startFrame = frame;
        };
        agent->onLinkEnd = [&](NavMeshAgent& a, NavMeshLink&) {
            log.push_back("end"); atEnd = a.gameObject->transform->Position();
            CHECK(!a.IsTraversingLink()); endFrame = frame;
        };
        s.Start();
        agent->SetDestination({6, 0, 0});
        float maxY = 0.0f;
        for (frame = 0; frame < 1200 && !agent->Arrived(); ++frame) {
            s.Update(1.0f / 60.0f);
            maxY = std::fmax(maxY, go->transform->Position().y);
        }
        CHECK(agent->Arrived());
        CHECK(log.size() == 2 && log[0] == "start" && log[1] == "end");
        CHECK(Dist2D(atStart, Vec3{-2, 0, 0}) < 0.4f);
        CHECK_NEAR(atEnd.x, 2.0f, 0.01f); CHECK_NEAR(atEnd.y, 0.0f, 0.01f); CHECK_NEAR(atEnd.z, 0.0f, 0.01f);   // exactly the end point
        CHECK(maxY > 0.7f);                                              // an arc, not a slide
        CHECK(endFrame - startFrame >= 55 && endFrame - startFrame <= 70);   // ~4 m at 4 m/s
        CHECK(Dist2D(go->transform->Position(), Vec3{6, 0, 0}) < 0.5f);
        CHECK(!agent->IsTraversingLink());
        // A slower link takes longer; progress reports mid-crossing.
        link->speed = 1.0f;
        agent->Warp({-6, 0, 0});
        log.clear();
        agent->SetDestination({6, 0, 0});
        float midProgress = -1.0f;
        for (frame = 0; frame < 2400 && !agent->Arrived(); ++frame) {
            s.Update(1.0f / 60.0f);
            if (agent->IsTraversingLink() && midProgress < 0.0f && agent->LinkProgress() > 0.4f) midProgress = agent->LinkProgress();
        }
        CHECK(agent->Arrived() && log.size() == 2);
        CHECK(midProgress > 0.4f && midProgress < 0.6f);
        CHECK(endFrame - startFrame >= 230 && endFrame - startFrame <= 270);
    }
    {   // A drop off a ledge: ends on the floor below at exactly the end point.
        Scene s("drop"); s.physicsEnabled = false;
        Slab(s, "Ledge", -10, -1, -5, 5, 3.0f, 3.0f);
        Slab(s, "Floor", 1, 10, -5, 5);
        Surface(s)->Bake(s);
        GameObject* lg = s.CreateGameObject("Drop");
        auto* link = lg->AddComponent<NavMeshLink>();
        link->type = NavMeshLink::Type::Drop; link->bidirectional = false;
        link->startPoint = {-2, 3, 0}; link->endPoint = {2, 0, 0};
        GameObject* go = s.CreateGameObject("Faller");
        go->transform->localPosition = {-6, 3, 0};
        auto* agent = go->AddComponent<NavMeshAgent>();
        agent->speed = 4.0f; agent->avoidanceRange = 0.0f;
        int starts = 0, ends = 0; float yAtHalf = -1.0f;
        agent->onLinkStart = [&](NavMeshAgent&, NavMeshLink&) { ++starts; };
        agent->onLinkEnd = [&](NavMeshAgent& a, NavMeshLink&) { ++ends; CHECK_NEAR(a.gameObject->transform->Position().y, 0.0f, 0.01f); };
        s.Start();
        agent->SetDestination({6, 0, 0});
        for (int i = 0; i < 1200 && !agent->Arrived(); ++i) {
            s.Update(1.0f / 60.0f);
            if (agent->IsTraversingLink() && yAtHalf < 0.0f && agent->LinkProgress() >= 0.5f) yAtHalf = go->transform->Position().y;
        }
        CHECK(agent->Arrived() && starts == 1 && ends == 1);
        CHECK(yAtHalf > 1.5f);                                           // a fall speeds up: still high half way through
        CHECK_NEAR(go->transform->Position().y, 0.0f, 0.1f);
        // Back up is impossible: the drop is one way.
        std::vector<Vec3> p; std::vector<NavMeshLink*> legs;
        CHECK(!Navigation::FindPathForAgent(s, {6, 0, 0}, {-6, 3, 0}, p, legs, kNavDefaultAreaMask));
    }
    {   // Crossing shapes.
        Scene s("shapes");
        auto* l = s.CreateGameObject("L")->AddComponent<NavMeshLink>();
        const Vec3 a{0, 0, 0}, b{4, 2, 0};
        l->type = NavMeshLink::Type::Ladder;
        CHECK_NEAR(l->PointAt(a, b, 0.5f).y, 1.0f, 1e-4); CHECK_NEAR(l->PointAt(a, b, 0.5f).x, 2.0f, 1e-4);
        l->type = NavMeshLink::Type::Jump; l->arcHeight = 2.0f;
        CHECK_NEAR(l->PointAt(a, b, 0.5f).y, 1.0f + 2.0f, 1e-4);
        l->type = NavMeshLink::Type::Drop;
        CHECK_NEAR(l->PointAt(a, b, 0.5f).y, 0.5f, 1e-4);
        for (auto t : {NavMeshLink::Type::Jump, NavMeshLink::Type::Drop, NavMeshLink::Type::Ladder}) {
            l->type = t;
            const Vec3 e = l->PointAt(a, b, 1.0f), st = l->PointAt(a, b, 0.0f);
            CHECK_NEAR(e.x, 4.0f, 1e-5); CHECK_NEAR(e.y, 2.0f, 1e-5); CHECK_NEAR(st.y, 0.0f, 1e-5);
        }
        l->speed = 0.0f; CHECK_NEAR(l->Duration(a, Vec3{4, 0, 0}, 2.0f), 2.0f, 1e-4);
        l->speed = 8.0f; CHECK_NEAR(l->Duration(a, Vec3{4, 0, 0}, 2.0f), 0.5f, 1e-4);
    }

    // ---- Scene file round trip.
    {
        Scene a("rt");
        GameObject* m = a.CreateGameObject("Mud");
        auto* mod = m->AddComponent<NavMeshModifier>(); mod->area = 7; mod->applyToChildren = false;
        GameObject* lg = a.CreateGameObject("Ladder");
        auto* l = lg->AddComponent<NavMeshLink>();
        l->type = NavMeshLink::Type::Ladder; l->startPoint = {1, 2, 3}; l->endPoint = {4, 5, 6}; l->bidirectional = false;
        l->cost = 2.5f; l->area = 9; l->speed = 1.5f; l->arcHeight = 0.25f; l->enabled = false;
        auto* ns = a.CreateGameObject("Nav")->AddComponent<NavMeshSurface>();
        ns->areaCost[NavArea::Water] = 6.0f; ns->areaCost[9] = 0.25f; ns->areaName[9] = "Ice";
        auto* ag = a.CreateGameObject("Agent")->AddComponent<NavMeshAgent>();
        ag->areaMask = 0x0000F0F1u;
        const std::string text = SceneSerializer::Serialize(a);
        Scene b("rt2"); std::string err;
        CHECK(SceneSerializer::Deserialize(b, text, &err, nullptr));
        auto* mod2 = b.Find("Mud") ? b.Find("Mud")->GetComponent<NavMeshModifier>() : nullptr;
        CHECK(mod2 && mod2->area == 7 && !mod2->applyToChildren);
        auto* l2 = b.Find("Ladder") ? b.Find("Ladder")->GetComponent<NavMeshLink>() : nullptr;
        CHECK(l2 != nullptr);
        if (l2) {
            CHECK(l2->type == NavMeshLink::Type::Ladder && !l2->bidirectional && !l2->enabled);
            CHECK(l2->startPoint.x == 1 && l2->startPoint.y == 2 && l2->startPoint.z == 3);
            CHECK(l2->endPoint.x == 4 && l2->endPoint.y == 5 && l2->endPoint.z == 6);
            CHECK(l2->cost == 2.5f && l2->area == 9 && l2->speed == 1.5f && l2->arcHeight == 0.25f);
        }
        auto* ns2 = b.Find("Nav") ? b.Find("Nav")->GetComponent<NavMeshSurface>() : nullptr;
        CHECK(ns2 != nullptr);
        if (ns2) {
            CHECK(ns2->areaCost[NavArea::Water] == 6.0f && ns2->areaCost[9] == 0.25f);
            CHECK(ns2->areaCost[NavArea::Road] == NavAreaDefaultCost(NavArea::Road));
            CHECK(std::string(ns2->AreaName(9)) == "Ice" && std::string(ns2->AreaName(2)) == "Water");
        }
        auto* ag2 = b.Find("Agent") ? b.Find("Agent")->GetComponent<NavMeshAgent>() : nullptr;
        CHECK(ag2 && ag2->areaMask == 0x0000F0F1u);
        CHECK(SceneSerializer::Serialize(b) == text);
    }

    TEST_MAIN_RESULT();
}
