#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <string>

using namespace okay;

// OkayScript navmesh queries: nav_path_exists, nav_path_length, nav_point_walkable,
// nav_nearest_point, run against a real baked navmesh.

static GameObject* Box(Scene& s, const char* name, Vec3 pos, Vec3 scale) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = pos; g->transform->localScale = scale;
    g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    return g;
}

static ScriptComponent* Load(GameObject* go, const std::string& src) {
    auto* sc = go->AddComponent<ScriptComponent>(); std::string e;
    if (!sc->LoadSource(src, &e)) { std::printf("  parse: %s\n", e.c_str()); return nullptr; }
    return sc;
}

int main() {
    RUN_SUITE("script_nav");

    // A 20x20 floor with a wall across the middle and a separate island: the wall
    // makes the route longer, the island has no route.
    {
        Scene s("nav"); s.physicsEnabled = false;
        Box(s, "Ground", {0, -0.5f, 0}, {20, 1, 20});
        Box(s, "Wall", {0, 1.0f, 0}, {1, 2, 6});
        Box(s, "Island", {16, -0.5f, 0}, {4, 1, 4});
        auto* ns = s.CreateGameObject("Nav Mesh")->AddComponent<NavMeshSurface>();
        ns->settings.agentRadius = 0.4f; ns->settings.cellSize = 0.25f;
        CHECK(ns->Bake(s));
        GameObject* hero = s.CreateGameObject("Hero");
        hero->transform->localPosition = {-5, 0, 0};
        auto* sc = Load(hero,
            "var near = 0\nvar far = 0\nvar island = 0\nvar lenAround = 0\nvar lenStraight = 0\nvar lenIsland = 0\n"
            "var ok = 0\nvar nope = 0\nvar nx = 0\nvar nz = 0\nvar keepX = 0\nvar missing = 0\n"
            "func start() {\n"
            "  near = nav_path_exists(-5, 0, 5)\n"
            "  far = nav_path_exists(5, 0, 0)\n"
            "  island = nav_path_exists(16, 0, 0)\n"
            "  lenAround = nav_path_length(5, 0, 0)\n"
            "  lenStraight = nav_path_length(-5, 0, 6, 5, 0, 6)\n"
            "  lenIsland = nav_path_length(16, 0, 0)\n"
            "  ok = nav_point_walkable(3, 0, 3)\n"
            "  nope = nav_point_walkable(40, 0, 0)\n"
            "  var p = nav_nearest_point(40, 0, 0)\n"
            "  nx = vec_x(p)\n  nz = vec_z(p)\n"
            "  var q = nav_nearest_point(40, 0, 0, 2)\n"
            "  keepX = vec_x(q)\n"
            "}\n");
        CHECK(sc != nullptr);
        if (sc) {
            s.Start();
            s.Update(1.0f / 60.0f);
            auto G = [&](const char* n) { return sc->VM()->GetGlobal(n).AsFloat(); };
            CHECK(G("near") == 1.0f && G("far") == 1.0f);
            CHECK(G("island") == 0.0f);
            CHECK(G("lenIsland") == -1.0f);
            CHECK(G("lenStraight") > 9.5f && G("lenStraight") < 10.5f);   // clear line, 10 units
            CHECK(G("lenAround") > 10.5f);                                // goes round the wall
            CHECK(G("ok") == 1.0f && G("nope") == 0.0f);
            CHECK(G("nx") > 15.0f && G("nx") < 20.0f);                    // the island's near edge, not the far side
            CHECK(G("keepX") == 40.0f);                                   // nothing within 2: the point itself
        }
    }

    // A scene with no baked navmesh: the grid answers path questions, and
    // point queries say "not walkable" instead of failing.
    {
        Scene s("nomesh"); s.physicsEnabled = false;
        Box(s, "Ground", {0, -0.5f, 0}, {20, 1, 20});
        GameObject* hero = s.CreateGameObject("Hero");
        auto* sc = Load(hero,
            "var w = 1\nvar ex = 0\n"
            "func start() {\n  w = nav_point_walkable(1, 0, 1)\n  ex = nav_path_exists(3, 0, 3)\n}\n");
        CHECK(sc != nullptr);
        if (sc) {
            s.Start();
            s.Update(1.0f / 60.0f);
            CHECK(sc->VM()->GetGlobal("w").AsFloat() == 0.0f);
            CHECK(sc->VM()->GetGlobal("ex").AsFloat() == 1.0f);
        }
    }

    TEST_MAIN_RESULT();
}
