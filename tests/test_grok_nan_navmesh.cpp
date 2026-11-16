// Grok findings 23 and 29 (issue #888): navigation must never write NaN into an object's
// position.
//
// Finding 23: a NavMeshAgent given a destination that is not finite (with no baked mesh), or
// a NaN speed / stopping distance (with or without a mesh), moved its object to (nan, 0, nan).
// A negative speed ran away forever and a negative stopping distance never arrived. The
// behaviour-tree Patrol (a waypoint at NaN) and Wander (radius = inf) tasks reached the same.
//
// Finding 29: a Jump NavMeshLink whose arcHeight is NaN or infinite (the scene file reads
// `arc=1e999` as inf) left every agent that jumped it at a NaN height for good.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/AI/BehaviorTree.hpp"
#include "okay/AI/Navigation.hpp"
#include "okay/Components/BehaviorTreeRunner.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/NavMeshLink.hpp"
#include "okay/Components/NavMeshSurface.hpp"
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace okay;

namespace {
const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

bool Fin(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool Fin(const Quat& q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); }
float Dist2D(const Vec3& a, const Vec3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }

GameObject* Slab(Scene& s, const char* name, float x0, float x1, float z0, float z1) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = {(x0 + x1) * 0.5f, -0.5f, (z0 + z1) * 0.5f};
    g->transform->localScale = {x1 - x0, 1.0f, z1 - z0};
    g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    return g;
}
void Bake(Scene& s) {
    auto* ns = s.CreateGameObject("Nav Mesh")->AddComponent<NavMeshSurface>();
    ns->settings.agentRadius = 0.4f; ns->settings.cellSize = 0.25f;
    ns->Bake(s);
}

struct Run { bool finite = true; bool arrived = false; bool traversed = false; Vec3 end{0, 0, 0}; };

// One agent at the origin, 600 frames at 60 fps. `setup` changes the agent before the goal is set.
template <class Setup>
Run RunAgent(bool mesh, const Vec3& dest, Setup setup) {
    Scene s("agent"); s.physicsEnabled = false;
    if (mesh) { Slab(s, "Floor", -10, 10, -10, 10); Bake(s); }
    GameObject* go = s.CreateGameObject("Walker");
    auto* a = go->AddComponent<NavMeshAgent>();
    a->avoidanceRange = 0.0f;
    setup(*a);
    s.Start();
    a->SetDestination(dest);
    Run r;
    for (int i = 0; i < 600; ++i) {
        s.Update(1.0f / 60.0f);
        r.finite = r.finite && Fin(go->transform->Position()) && Fin(go->transform->localRotation) && Fin(a->Velocity());
        r.arrived = r.arrived || a->Arrived();
    }
    r.end = go->transform->Position();
    return r;
}

// The link scene of tests/test_navmesh_areas.cpp: two platforms 2 m apart and a Jump link (-2,0,0) -> (2,0,0).
Run RunGap(float arc) {
    Scene s("gap"); s.physicsEnabled = false;
    Slab(s, "Left", -10, -1, -5, 5);
    Slab(s, "Right", 1, 10, -5, 5);
    Bake(s);
    auto* link = s.CreateGameObject("Link")->AddComponent<NavMeshLink>();
    link->startPoint = {-2, 0, 0}; link->endPoint = {2, 0, 0};
    link->type = NavMeshLink::Type::Jump; link->arcHeight = arc;
    GameObject* go = s.CreateGameObject("Jumper");
    go->transform->localPosition = {-6, 0, 0};
    auto* a = go->AddComponent<NavMeshAgent>();
    a->speed = 4.0f; a->avoidanceRange = 0.0f;
    s.Start();
    a->SetDestination({6, 0, 0});
    Run r;
    for (int i = 0; i < 600; ++i) {
        s.Update(1.0f / 60.0f);
        r.finite = r.finite && Fin(go->transform->Position());
        r.traversed = r.traversed || a->IsTraversingLink();
        r.arrived = r.arrived || a->Arrived();
    }
    r.end = go->transform->Position();
    return r;
}

// A behaviour tree with one task on an object at the origin; 60 frames.
bool RunTree(bool agent, bool mesh, BtNodeType type, const char* key, const char* value, Vec3 waypoint) {
    Scene s("bt"); s.physicsEnabled = false;
    if (mesh) { Slab(s, "Floor", -10, 10, -10, 10); Bake(s); }
    s.CreateGameObject("wpn")->transform->localPosition = waypoint;
    GameObject* npc = s.CreateGameObject("Npc");
    if (agent) npc->AddComponent<NavMeshAgent>();
    auto* r = npc->AddComponent<BehaviorTreeRunner>();
    auto t = std::make_shared<BehaviorTree>();
    BtNode* n = t->Find(t->AddNode(type));
    n->params[key] = value;
    r->SetTree(t);
    s.Start();
    bool fin = true;
    for (int i = 0; i < 60; ++i) {
        s.Update(1.0f / 60.0f);
        fin = fin && Fin(npc->transform->Position());
    }
    return fin;
}
} // namespace

int main() {
    RUN_SUITE("grok_nan_navmesh");

    // ======================= Finding 23: the agent =======================
    auto none = [](NavMeshAgent&) {};
    for (int mesh = 0; mesh < 2; ++mesh) {
        // Control: a normal trip arrives.
        Run ok = RunAgent(mesh, {5, 0, 5}, none);
        CHECK(ok.finite && ok.arrived && Dist2D(ok.end, {5, 0, 5}) < 0.5f);

        // A destination that is not finite: the agent stays put and has no path.
        for (Vec3 bad : {Vec3{kNaN, 0, 5}, Vec3{kInf, 0, 5}, Vec3{0, kNaN, 0}}) {
            Scene s("dest"); s.physicsEnabled = false;
            if (mesh) { Slab(s, "Floor", -10, 10, -10, 10); Bake(s); }
            GameObject* go = s.CreateGameObject("Walker");
            auto* a = go->AddComponent<NavMeshAgent>();
            s.Start();
            a->SetDestination({3, 0, 0});           // an earlier, good goal is dropped too
            s.Update(1.0f / 60.0f);
            a->SetDestination(bad);
            bool fin = true;
            for (int i = 0; i < 600; ++i) { s.Update(1.0f / 60.0f); fin = fin && Fin(go->transform->Position()); }
            CHECK(fin);
            CHECK(!a->HasPath());
            CHECK(!a->Arrived());
            CHECK(Fin(go->transform->Position()) && Dist2D(go->transform->Position(), {0, 0, 0}) < 0.5f);
        }

        // NaN speed: does not move (counts as 0), stays finite.
        Run r = RunAgent(mesh, {5, 0, 5}, [](NavMeshAgent& a) { a.speed = kNaN; });
        CHECK(r.finite);
        CHECK(Dist2D(r.end, {0, 0, 0}) < 0.01f);
        // Negative speed: counts as 0 (it used to run the wrong way forever).
        r = RunAgent(mesh, {5, 0, 5}, [](NavMeshAgent& a) { a.speed = -3.0f; });
        CHECK(r.finite);
        CHECK(Dist2D(r.end, {0, 0, 0}) < 0.01f);
        // NaN stopping distance: counts as 0, the agent still arrives.
        r = RunAgent(mesh, {5, 0, 5}, [](NavMeshAgent& a) { a.stoppingDistance = kNaN; });
        CHECK(r.finite && r.arrived);
        CHECK(Dist2D(r.end, {5, 0, 5}) < 0.05f);
        // Negative stopping distance: counts as 0, the agent arrives (it used to sit on the goal forever).
        r = RunAgent(mesh, {5, 0, 5}, [](NavMeshAgent& a) { a.stoppingDistance = -1.0f; });
        CHECK(r.finite && r.arrived);
        // NaN turn rate: snaps to face the way it goes; the rotation stays finite.
        r = RunAgent(mesh, {5, 0, 5}, [](NavMeshAgent& a) { a.angularSpeed = kNaN; });
        CHECK(r.finite && r.arrived);
        // NaN base offset: counts as 0.
        r = RunAgent(mesh, {5, 0, 5}, [](NavMeshAgent& a) { a.baseOffset = kNaN; });
        CHECK(r.finite && r.arrived);
    }

    // The planner itself refuses a goal or a start that is not finite (no straight-line fallback).
    {
        Scene s("plan"); s.physicsEnabled = false;
        std::vector<Vec3> p;
        CHECK(!Navigation::FindPath(s, {0, 0, 0}, {kNaN, 0, 5}, p) && p.empty());
        CHECK(!Navigation::FindPath(s, {kInf, 0, 0}, {1, 0, 5}, p) && p.empty());
        CHECK(Navigation::FindPath(s, {0, 0, 0}, {1, 0, 5}, p) && !p.empty());   // the finite twin
    }

    // Behaviour trees: Patrol to a NaN waypoint, Wander with an infinite radius (with and without an agent).
    for (int mesh = 0; mesh < 2; ++mesh)
        for (int agent = 0; agent < 2; ++agent) {
            CHECK(RunTree(agent, mesh, BtNodeType::Patrol, "waypoints", "wpn", {kNaN, 0, 0}));
            CHECK(RunTree(agent, mesh, BtNodeType::Wander, "radius", "inf", {0, 0, 0}));
            CHECK(RunTree(agent, mesh, BtNodeType::Wander, "radius", "1e999", {0, 0, 0}));
            CHECK(RunTree(agent, mesh, BtNodeType::Wander, "radius", "nan", {0, 0, 0}));
        }

    // ======================= Finding 29: the Jump link's arc =======================
    {
        NavMeshLink l;
        l.type = NavMeshLink::Type::Jump;
        for (float arc : {kNaN, kInf, -kInf, 3e38f}) {
            l.arcHeight = arc;
            bool fin = true;
            for (int k = 0; k <= 10; ++k) fin = fin && Fin(l.PointAt({-2, 0, 0}, {2, 0, 0}, k / 10.0f));
            CHECK(fin);
            CHECK_NEAR(l.PointAt({-2, 0, 0}, {2, 0, 0}, 1.0f).y, 0.0f, 1e-6);   // lands on the end point
        }
        l.arcHeight = 1.0f;                                                       // a finite arc is unchanged
        CHECK_NEAR(l.PointAt({-2, 0, 0}, {2, 0, 0}, 0.5f).y, 1.0f, 1e-6);
        CHECK(std::isfinite(l.Duration({-2, 0, 0}, {2, 0, 0}, kNaN)));       // a NaN agent speed: still a finite crossing
    }
    {
        Run ctl = RunGap(1.0f);
        CHECK(ctl.traversed && ctl.arrived && ctl.finite);
        for (float arc : {kNaN, kInf, std::strtof("1e999", nullptr), 3e38f}) {
            Run r = RunGap(arc);
            CHECK(r.traversed);
            CHECK(r.finite);
            CHECK(r.arrived);
            CHECK(Fin(r.end) && Dist2D(r.end, {6, 0, 0}) < 0.5f && std::fabs(r.end.y) < 0.1f);
        }
    }

    TEST_MAIN_RESULT();
}
