#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/AI/NavGrid3D.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace okay;

// ---- Scene helpers: real colliders on static bodies, like the shipped templates ----
static GameObject* Box(Scene& s, const char* name, Vec3 pos, Vec3 scale, Quat rot = Quat::Identity) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = pos; g->transform->localScale = scale; g->transform->localRotation = rot;
    g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    return g;
}
static GameObject* Floor(Scene& s, float size = 20.0f) { return Box(s, "Ground", {0, -0.5f, 0}, {size, 1, size}); }
static NavMeshSurface* Surface(Scene& s, float radius = 0.4f, float cell = 0.25f) {
    auto* ns = s.CreateGameObject("Nav Mesh")->AddComponent<NavMeshSurface>();
    ns->settings.agentRadius = radius; ns->settings.cellSize = cell;
    return ns;
}
static float PathLength(const std::vector<Vec3>& p) {
    float l = 0.0f;
    for (std::size_t i = 1; i < p.size(); ++i) l += std::sqrt((p[i].x - p[i-1].x) * (p[i].x - p[i-1].x) + (p[i].z - p[i-1].z) * (p[i].z - p[i-1].z));
    return l;
}
static float Dist2D(const Vec3& a, const Vec3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }
// Does the polyline enter the XZ rectangle [x0,x1]x[z0,z1] (sampled along each segment)?
static bool Crosses(const std::vector<Vec3>& p, float x0, float x1, float z0, float z1) {
    for (std::size_t i = 1; i < p.size(); ++i)
        for (int k = 0; k <= 40; ++k) {
            float t = k / 40.0f;
            float x = p[i-1].x + (p[i].x - p[i-1].x) * t, z = p[i-1].z + (p[i].z - p[i-1].z) * t;
            if (x > x0 && x < x1 && z > z0 && z < z1) return true;
        }
    return false;
}
static void Tick(Scene& s, int n) { for (int i = 0; i < n; ++i) s.Update(1.0f / 60.0f); }

int main() {
    RUN_SUITE("navmesh");

    // ---- A 20x20 floor with a 1x6 wall: the path goes around the wall, and is no
    // longer than the grid's route.
    {
        Scene s("wall"); s.physicsEnabled = false;
        Floor(s);
        Box(s, "Wall", {0, 1.0f, 0}, {1, 2, 6});
        NavMeshSurface* ns = Surface(s);
        NavBakeStats st;
        CHECK(ns->Bake(s, &st));
        CHECK(st.polys > 0);
        const NavMesh* mesh = ns->Mesh();
        CHECK(mesh != nullptr);
        std::vector<Vec3> path;
        CHECK(mesh && mesh->FindPath({-5, 0, 0}, {5, 0, 0}, path));
        CHECK(path.size() >= 3);                                  // a corner at each end of the wall
        CHECK(!Crosses(path, -0.5f - 0.35f, 0.5f + 0.35f, -3.0f - 0.35f, 3.0f + 0.35f));   // keeps the agent radius clear
        if (!path.empty()) { CHECK_NEAR(path.front().x, -5.0f, 0.3f); CHECK_NEAR(path.back().x, 5.0f, 0.3f); }
        std::vector<Vec3> grid = NavGrid3D::FindPath(s, {-5, 0, 0}, {5, 0, 0});
        CHECK(!grid.empty());
        CHECK(PathLength(path) <= PathLength(grid) + 0.05f);
        // String pulling: fewer corners than walking polygon centre to centre.
        std::vector<Vec3> centres;
        CHECK(mesh && mesh->FindPath({-5, 0, 0}, {5, 0, 0}, centres, false));
        CHECK(path.size() < centres.size());
        // The facade uses the mesh when the scene has one.
        std::vector<Vec3> viaScene = Navigation::FindPath(s, {-5, 0, 0}, {5, 0, 0});
        CHECK(viaScene.size() == path.size());
        CHECK(Navigation::Mesh(s) == mesh);
        // A raycast along the mesh stops at the wall; a clear line does not.
        Vec3 hit;
        CHECK(mesh && mesh->Raycast({-5, 0, 0}, {5, 0, 0}, hit));
        CHECK(hit.x < -0.5f && hit.x > -2.0f);
        CHECK(mesh && !mesh->Raycast({-5, 0, 5}, {5, 0, 5}, hit));
        // Random points land on the mesh, never inside the wall's clearance.
        std::uint32_t seed = 7u;
        for (int i = 0; i < 50; ++i) {
            Vec3 p;
            CHECK(mesh && mesh->RandomPoint(seed, p));
            CHECK(!(std::fabs(p.x) < 0.85f && std::fabs(p.z) < 3.35f));
            CHECK(std::fabs(p.y) < 0.05f);
        }
        // Nearest point and height sampling.
        Vec3 near;
        CHECK(mesh && mesh->SamplePosition({0, 0, 0}, 2.0f, near));   // inside the wall: snaps out of it
        CHECK(std::fabs(near.x) > 0.8f);
        CHECK_NEAR(mesh ? mesh->SampleHeight({3, 0.5f, 3}, 99.0f) : 99.0f, 0.0f, 0.05f);
    }

    // ---- A wall with a 1.2 m doorway: a 0.4 m agent walks through it; a 0.7 m one can't.
    {
        for (float radius : {0.4f, 0.7f}) {
            Scene s("door"); s.physicsEnabled = false;
            Floor(s);
            Box(s, "WallL", {-5.3f, 1.0f, 0}, {9.4f, 2, 1});   // x in [-10, -0.6]: wall to wall
            Box(s, "WallR", { 5.3f, 1.0f, 0}, {9.4f, 2, 1});   // x in [0.6, 10]
            NavMeshSurface* ns = Surface(s, radius, 0.2f);
            CHECK(ns->Bake(s));
            std::vector<Vec3> path;
            const bool found = ns->Mesh() && ns->Mesh()->FindPath({0, 0, -4}, {0, 0, 4}, path);
            if (radius < 0.5f) {
                CHECK(found);
                CHECK(Crosses(path, -0.6f, 0.6f, -0.6f, 0.6f));       // through the door
                CHECK(!Crosses(path, -10.0f, -0.6f, -0.5f, 0.5f));   // not through a wall
                CHECK(!Crosses(path, 0.6f, 10.0f, -0.5f, 0.5f));
            } else {
                CHECK(!found);                                       // the doorway closed
                CHECK(path.empty());
            }
        }
    }

    // ---- A 40 degree ramp is walkable at max slope 45 and not at 30.
    {
        for (float maxSlope : {45.0f, 30.0f}) {
            Scene s("ramp"); s.physicsEnabled = false;
            Floor(s, 24.0f);
            const float angle = 40.0f, len = 3.3f;
            const float rise = len * std::sin(angle * Mathf::Deg2Rad), run = len * std::cos(angle * Mathf::Deg2Rad);
            Quat q = Quat::Euler(-angle, 0, 0);
            if ((q * Vec3{0, 0, 1}).y < 0.0f) q = Quat::Euler(angle, 0, 0);   // tilt so +z rises
            const Vec3 normal = q * Vec3{0, 1, 0};
            Vec3 top{0, rise * 0.5f, -2.0f + run * 0.5f};
            Box(s, "Ramp", top - normal * 0.1f, {3, 0.2f, len}, q);
            Box(s, "Platform", {0, rise - 0.5f, -2.0f + run + 2.5f}, {6, 1, 5});   // top at `rise`
            NavMeshSurface* ns = Surface(s);
            ns->settings.maxSlope = maxSlope;
            CHECK(ns->Bake(s));
            std::vector<Vec3> path;
            const bool found = ns->Mesh() && ns->Mesh()->FindPath({0, 0, -6}, {0, rise, -2.0f + run + 2.5f}, path);
            if (maxSlope > 40.0f) {
                CHECK(found);
                if (found) CHECK_NEAR(path.back().y, rise, 0.15f);
            } else {
                CHECK(!found);
            }
        }
    }

    // ---- A 0.3 m step is walkable at max step 0.4 and not at 0.2.
    {
        for (float maxStep : {0.4f, 0.2f}) {
            Scene s("step"); s.physicsEnabled = false;
            Floor(s);
            Box(s, "Step", {0, 0.15f, 3.5f}, {20, 0.3f, 5});   // top at 0.3, z in [1, 6]
            NavMeshSurface* ns = Surface(s);
            ns->settings.maxStep = maxStep;
            CHECK(ns->Bake(s));
            std::vector<Vec3> path;
            const bool found = ns->Mesh() && ns->Mesh()->FindPath({0, 0, -5}, {0, 0.3f, 4}, path);
            CHECK(found == (maxStep > 0.3f));
        }
    }

    // ---- A bridge over the floor: two layers; the deck and the floor under it are
    // both walkable, and an agent on the bridge never drops to the floor.
    {
        Scene s("bridge"); s.physicsEnabled = false;
        Floor(s);
        Box(s, "Deck", {0, 2.4f, 0}, {12, 0.2f, 2});   // top at 2.5, 2.3 of head room underneath
        NavMeshSurface* ns = Surface(s);
        CHECK(ns->Bake(s));
        const NavMesh* mesh = ns->Mesh();
        CHECK(mesh != nullptr);
        if (mesh) {
            Vec3 n;
            int deck = mesh->FindNearestPoly({0, 2.5f, 0}, {1, 1, 1}, &n);
            CHECK(deck >= 0); CHECK_NEAR(n.y, 2.5f, 0.05f);
            int floor = mesh->FindNearestPoly({0, 0, 0}, {1, 1, 1}, &n);
            CHECK(floor >= 0); CHECK_NEAR(n.y, 0.0f, 0.05f);
            CHECK(deck != floor);
            std::vector<Vec3> onDeck, onFloor;
            CHECK(mesh->FindPath({-5, 2.5f, 0}, {5, 2.5f, 0}, onDeck));
            for (const Vec3& p : onDeck) CHECK(p.y > 2.3f);
            CHECK(mesh->FindPath({-5, 0, 0}, {5, 0, 0}, onFloor));
            for (const Vec3& p : onFloor) CHECK(p.y < 0.2f);
            // No ramp joins them: the deck is its own island.
            std::vector<Vec3> across;
            CHECK(!mesh->FindPath({-5, 2.5f, 0}, {5, 0, 5}, across));
        }
        auto* agent = s.CreateGameObject("Walker")->AddComponent<NavMeshAgent>();
        agent->gameObject->transform->localPosition = {-5, 2.5f, 0};
        agent->speed = 4.0f;
        s.Start();
        agent->SetDestination({5, 2.5f, 0});
        float minY = 10.0f;
        for (int i = 0; i < 300 && agent->HasPath(); ++i) { s.Update(1.0f / 60.0f); minY = std::fmin(minY, agent->gameObject->transform->Position().y); }
        CHECK(!agent->HasPath());
        CHECK(agent->Arrived());
        CHECK(minY > 2.3f);
    }

    // ---- An unreachable island returns no path.
    {
        Scene s("island"); s.physicsEnabled = false;
        Floor(s);
        Box(s, "Island", {14, -0.5f, 0}, {4, 1, 4});
        NavMeshSurface* ns = Surface(s);
        CHECK(ns->Bake(s));
        std::vector<Vec3> path;
        CHECK(ns->Mesh() && !ns->Mesh()->FindPath({0, 0, 0}, {14, 0, 0}, path));
        CHECK(path.empty());
        CHECK(Navigation::FindPath(s, {0, 0, 0}, {14, 0, 0}).empty());
    }

    // ---- An agent reaches its destination within a few seconds and stops at the
    // stopping distance; Stop / Resume pause and continue it.
    {
        Scene s("agent"); s.physicsEnabled = false;
        Floor(s);
        Box(s, "Wall", {0, 1.0f, 0}, {1, 2, 6});
        Surface(s)->Bake(s);
        GameObject* go = s.CreateGameObject("Walker");
        go->transform->localPosition = {-5, 0, 0};
        auto* agent = go->AddComponent<NavMeshAgent>();
        agent->speed = 4.0f; agent->stoppingDistance = 0.3f;
        s.Start();
        CHECK(!agent->HasPath());
        agent->SetDestination({5, 0, 0});
        CHECK(agent->HasPath());
        Tick(s, 30);
        CHECK(agent->RemainingDistance() > 5.0f);
        CHECK(agent->Velocity().Magnitude() > 1.0f);
        agent->Stop();
        CHECK(agent->IsStopped());
        Vec3 held = go->transform->Position();
        Tick(s, 30);
        CHECK(Dist2D(go->transform->Position(), held) < 1.0f);   // coasted to a halt
        agent->Resume();
        int frames = 0;
        while (agent->HasPath() && frames < 600) { s.Update(1.0f / 60.0f); ++frames; }
        CHECK(frames < 600);
        CHECK(agent->Arrived());
        const float d = Dist2D(go->transform->Position(), {5, 0, 0});
        CHECK(d <= 0.3f + 0.05f);
        CHECK(std::fabs(go->transform->Position().z) < 0.3f);
        Tick(s, 10);
        CHECK(agent->Velocity().Magnitude() < 0.01f);
        // It walked around the wall, not through it.
        CHECK(agent->Path().empty());
    }

    // ---- Two agents crossing paths steer around each other and both arrive.
    {
        Scene s("crossing"); s.physicsEnabled = false;
        Floor(s);
        Surface(s)->Bake(s);
        GameObject* a = s.CreateGameObject("A"); a->transform->localPosition = {-4, 0, 0};
        GameObject* b = s.CreateGameObject("B"); b->transform->localPosition = {4, 0, 0.05f};
        auto* aa = a->AddComponent<NavMeshAgent>(); auto* ab = b->AddComponent<NavMeshAgent>();
        aa->speed = ab->speed = 3.0f;
        s.Start();
        aa->SetDestination({4, 0, 0}); ab->SetDestination({-4, 0, 0.05f});
        float minDist = 1e9f; int frames = 0;
        while ((aa->HasPath() || ab->HasPath()) && frames < 900) {
            s.Update(1.0f / 60.0f); ++frames;
            minDist = std::fmin(minDist, Dist2D(a->transform->Position(), b->transform->Position()));
        }
        CHECK(frames < 900);
        CHECK(aa->Arrived() && ab->Arrived());
        CHECK(minDist >= (aa->radius + ab->radius) * 0.8f);
        CHECK(Dist2D(a->transform->Position(), {4, 0, 0}) < 0.4f);
        CHECK(Dist2D(b->transform->Position(), {-4, 0, 0.05f}) < 0.4f);
    }

    // ---- An agent steers around a NavMeshObstacle in its way.
    {
        Scene s("obstacle"); s.physicsEnabled = false;
        Floor(s);
        Surface(s)->Bake(s);
        GameObject* o = s.CreateGameObject("Cart"); o->transform->localPosition = {0, 0.5f, 0};
        o->AddComponent<NavMeshObstacle>()->size = {1, 1, 1};
        GameObject* a = s.CreateGameObject("A"); a->transform->localPosition = {-4, 0, 0};
        auto* agent = a->AddComponent<NavMeshAgent>(); agent->speed = 3.0f;
        s.Start();
        agent->SetDestination({4, 0, 0});
        float minDist = 1e9f; int frames = 0;
        while (agent->HasPath() && frames < 900) { s.Update(1.0f / 60.0f); ++frames; minDist = std::fmin(minDist, Dist2D(a->transform->Position(), o->transform->Position())); }
        CHECK(agent->Arrived());
        CHECK(minDist > 0.6f);
    }

    // ---- .okaynav write / read round trip: same mesh, same paths.
    {
        Scene s("file"); s.physicsEnabled = false;
        Floor(s);
        Box(s, "Wall", {0, 1.0f, 0}, {1, 2, 6});
        Box(s, "Crate", {4, 0.5f, 4}, {1, 1, 1});
        NavMeshSurface* ns = Surface(s);
        CHECK(ns->Bake(s));
        const std::string dir = (std::filesystem::temp_directory_path() / "okay_navmesh_test").string();
        std::filesystem::create_directories(dir);
        ns->navPath = dir + "/file.okaynav";
        CHECK(ns->Save());
        NavMesh loaded;
        std::string err;
        CHECK(loaded.LoadFromFile(ns->navPath, &err));
        CHECK(loaded.polys.size() == ns->Mesh()->polys.size());
        CHECK(loaded.vertices.size() == ns->Mesh()->vertices.size());
        CHECK(loaded.links.size() == ns->Mesh()->links.size());
        CHECK_NEAR(loaded.settings.agentRadius, 0.4f, 1e-5f);
        std::vector<Vec3> a, b;
        CHECK(ns->Mesh()->FindPath({-5, 0, -5}, {5, 0, 5}, a));
        CHECK(loaded.FindPath({-5, 0, -5}, {5, 0, 5}, b));
        CHECK(a.size() == b.size());
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) { CHECK_NEAR(a[i].x, b[i].x, 1e-3f); CHECK_NEAR(a[i].z, b[i].z, 1e-3f); }
        CHECK(loaded.Serialize() == ns->Mesh()->Serialize());
        // A fresh surface loads the file when the scene starts.
        Scene s2("file2"); s2.physicsEnabled = false;
        auto* ns2 = s2.CreateGameObject("Nav Mesh")->AddComponent<NavMeshSurface>();
        ns2->navPath = ns->navPath;
        s2.Start();
        CHECK(ns2->HasMesh());
        CHECK(Navigation::Mesh(s2) != nullptr);
        std::filesystem::remove_all(dir);
    }

    // ---- Component records round trip through the scene file; a scene without
    // them loads as before.
    {
        Scene s("records"); s.physicsEnabled = false;
        GameObject* n = s.CreateGameObject("Nav");
        auto* ns = n->AddComponent<NavMeshSurface>();
        ns->settings.agentRadius = 0.55f; ns->settings.agentHeight = 2.1f; ns->settings.maxSlope = 35.0f;
        ns->settings.maxStep = 0.33f; ns->settings.cellSize = 0.3f; ns->settings.cellHeight = 0.15f;
        ns->navPath = "Assets/Scenes/Level.okaynav"; ns->autoBounds = false;
        ns->boundsCenter = {1, 2, 3}; ns->boundsSize = {30, 10, 40}; ns->includeDynamic = true; ns->layerMask = 0xF0F0u; ns->bakeBeforePlay = true;
        GameObject* w = s.CreateGameObject("Walker");
        auto* na = w->AddComponent<NavMeshAgent>();
        na->radius = 0.6f; na->height = 2.0f; na->speed = 5.5f; na->acceleration = 12.0f; na->angularSpeed = 180.0f;
        na->stoppingDistance = 0.75f; na->autoRepath = false; na->repathInterval = 1.5f; na->avoidancePriority = 10;
        na->avoidanceRange = 4.5f; na->autoBraking = false; na->faceMovement = false; na->driveAnimation = false; na->baseOffset = 0.5f;
        GameObject* c = s.CreateGameObject("Cart");
        auto* no = c->AddComponent<NavMeshObstacle>();
        no->size = {2, 1, 3}; no->center = {0, 0.5f, 0};
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("navsurface") != std::string::npos);
        CHECK(text.find("navagent") != std::string::npos);
        CHECK(text.find("navobstacle") != std::string::npos);
        Scene s2("records2");
        std::string err;
        CHECK(SceneSerializer::Deserialize(s2, text, &err));
        auto* ns2 = s2.Find("Nav") ? s2.Find("Nav")->GetComponent<NavMeshSurface>() : nullptr;
        CHECK(ns2 != nullptr);
        if (ns2) {
            CHECK_NEAR(ns2->settings.agentRadius, 0.55f, 1e-5f); CHECK_NEAR(ns2->settings.agentHeight, 2.1f, 1e-5f);
            CHECK_NEAR(ns2->settings.maxSlope, 35.0f, 1e-5f); CHECK_NEAR(ns2->settings.maxStep, 0.33f, 1e-5f);
            CHECK_NEAR(ns2->settings.cellSize, 0.3f, 1e-5f); CHECK_NEAR(ns2->settings.cellHeight, 0.15f, 1e-5f);
            CHECK(ns2->navPath == "Assets/Scenes/Level.okaynav");
            CHECK(!ns2->autoBounds); CHECK_NEAR(ns2->boundsCenter.z, 3.0f, 1e-5f); CHECK_NEAR(ns2->boundsSize.z, 40.0f, 1e-5f);
            CHECK(ns2->includeDynamic); CHECK(ns2->layerMask == 0xF0F0u); CHECK(ns2->bakeBeforePlay);
        }
        auto* na2 = s2.Find("Walker") ? s2.Find("Walker")->GetComponent<NavMeshAgent>() : nullptr;
        CHECK(na2 != nullptr);
        if (na2) {
            CHECK_NEAR(na2->radius, 0.6f, 1e-5f); CHECK_NEAR(na2->height, 2.0f, 1e-5f); CHECK_NEAR(na2->speed, 5.5f, 1e-5f);
            CHECK_NEAR(na2->acceleration, 12.0f, 1e-5f); CHECK_NEAR(na2->angularSpeed, 180.0f, 1e-5f);
            CHECK_NEAR(na2->stoppingDistance, 0.75f, 1e-5f); CHECK(!na2->autoRepath); CHECK_NEAR(na2->repathInterval, 1.5f, 1e-5f);
            CHECK(na2->avoidancePriority == 10); CHECK_NEAR(na2->avoidanceRange, 4.5f, 1e-5f);
            CHECK(!na2->autoBraking); CHECK(!na2->faceMovement); CHECK(!na2->driveAnimation); CHECK_NEAR(na2->baseOffset, 0.5f, 1e-5f);
        }
        auto* no2 = s2.Find("Cart") ? s2.Find("Cart")->GetComponent<NavMeshObstacle>() : nullptr;
        CHECK(no2 != nullptr);
        if (no2) { CHECK_NEAR(no2->size.z, 3.0f, 1e-5f); CHECK_NEAR(no2->center.y, 0.5f, 1e-5f); }
        // Older scene text (no navigation records) still loads.
        Scene s3("plain");
        CHECK(SceneSerializer::Deserialize(s3, "okayscene 3\nname \"old\"\n", &err));
    }

    // ---- A scene without a navmesh still paths with the grid (around the wall).
    {
        Scene s("grid"); s.physicsEnabled = false;
        Floor(s);
        Box(s, "Wall", {0, 1.0f, 0}, {1, 2, 6});
        CHECK(Navigation::Mesh(s) == nullptr);
        std::vector<Vec3> path = Navigation::FindPath(s, {-5, 0, 0}, {5, 0, 0});
        CHECK(path.size() >= 3);
        CHECK(!Crosses(path, -0.5f, 0.5f, -3.0f, 3.0f));
        // An NPC in such a scene keeps its grid route; with a baked mesh it uses that.
        GameObject* g = s.CreateGameObject("Guard");
        g->transform->localPosition = {-5, 0, 0};
        auto* npc = g->AddComponent<NPCController>();
        npc->behavior = (int)NPCController::Behavior::Idle; npc->usePathfinding = true; npc->targetName = "nobody";
        s.Start();
        npc->CommandGoTo({5, 0, 0});
        Tick(s, 5);
        CHECK(npc->CurrentPath().size() >= 3);
        Surface(s)->Bake(s);
        CHECK(Navigation::Mesh(s) != nullptr);
        npc->CommandGoTo({5, 0, 2});
        Tick(s, 60);
        CHECK(npc->CurrentPath().size() >= 2);
        CHECK(!Crosses(npc->CurrentPath(), -0.5f, 0.5f, -3.0f, 3.0f));
        // Through a Nav Mesh Agent on the same object the NPC does the walking.
        auto* agent = g->AddComponent<NavMeshAgent>();
        agent->SetDestination({-5, 0, 2});
        CHECK(npc->Commanded());
        CHECK(agent->HasPath());
        int frames = 0;
        while (agent->HasPath() && frames < 900) { s.Update(1.0f / 60.0f); ++frames; }
        CHECK(agent->Arrived());
        CHECK(Dist2D(g->transform->Position(), {-5, 0, 2}) < 1.0f);
    }

    TEST_MAIN_RESULT();
}
