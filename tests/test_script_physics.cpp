#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("script_physics");

    // A wall at the origin with a 2x2 box collider.
    auto makeWall = [](Scene& s) {
        GameObject* wall = s.CreateGameObject("Wall");
        wall->transform->localPosition = {0, 0, 0};
        auto* bc = wall->AddComponent<BoxCollider2D>();
        bc->size = {2, 2};
    };

    // --- overlap(x, y): inside the wall is a hit, far away is not.
    // The probe encodes results in its transform: x=1 if inside, y=1 if "outside" hit.
    {
        Scene scene("Ov");
        makeWall(scene);
        GameObject* probe = scene.CreateGameObject("Probe");
        auto* sc = probe->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() {\n"
            "  if (overlap(0, 0))   { set_x(1); }\n"
            "  if (overlap(50, 50)) { set_y(1); }\n"
            "}\n"));
        scene.Start();
        CHECK_NEAR(probe->transform->localPosition.x, 1.0f, 0.001f); // inside -> hit
        CHECK_NEAR(probe->transform->localPosition.y, 0.0f, 0.001f); // outside -> miss
    }

    // --- raycast_hit(): a ray toward the wall hits, away from it misses ---
    {
        Scene scene("Rc");
        makeWall(scene);
        GameObject* probe = scene.CreateGameObject("Probe");
        auto* sc = probe->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() {\n"
            "  if (raycast_hit(-10, 0, 1, 0, 100))  { set_x(1); }\n"  // toward +X
            "  if (raycast_hit(-10, 0, -1, 0, 100)) { set_y(1); }\n"  // away -X
            "}\n"));
        scene.Start();
        CHECK_NEAR(probe->transform->localPosition.x, 1.0f, 0.001f); // toward -> hit
        CHECK_NEAR(probe->transform->localPosition.y, 0.0f, 0.001f); // away -> miss
    }

    // --- raycast(): returns the hit object's name + detail accessors -----
    {
        Scene scene("RcDetail");
        makeWall(scene);                                     // 2x2 box, spans [-1,1]
        GameObject* probe = scene.CreateGameObject("Probe");
        auto* sc = probe->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() {\n"
            "  raycast(-10, 0, 1, 0, 100);\n"               // hits the wall at x=-1
            "  if (ray_hit()) { set_x(ray_dist()); }\n"     // distance 9
            "  if (ray_object() == \"Wall\") { set_y(1); }\n"
            "}\n"));
        scene.Start();
        CHECK_NEAR(probe->transform->localPosition.x, 9.0f, 0.05f);  // distance
        CHECK_NEAR(probe->transform->localPosition.y, 1.0f, 0.001f); // named the hit
    }

    // --- raycast() miss: returns empty name, ray_hit() is false ----------
    {
        Scene scene("RcMiss");
        makeWall(scene);
        GameObject* probe = scene.CreateGameObject("Probe");
        auto* sc = probe->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() {\n"
            "  raycast(-10, 0, -1, 0, 100);\n"              // away from the wall
            "  if (ray_hit()) { set_x(1); }\n"
            "  if (ray_object() == \"\") { set_y(1); }\n"
            "}\n"));
        scene.Start();
        CHECK_NEAR(probe->transform->localPosition.x, 0.0f, 0.001f); // no hit
        CHECK_NEAR(probe->transform->localPosition.y, 1.0f, 0.001f); // empty name
    }

    // --- set_velocity / set_vx / set_vy / velocity_x|y on a sibling body ---
    {
        Scene scene("Vel");
        GameObject* go = scene.CreateGameObject("Body");
        auto* rb = go->AddComponent<Rigidbody2D>();
        rb->bodyType = Rigidbody2D::BodyType::Kinematic; // moves by velocity only
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() { set_velocity(3, 4); }\n"
            "function update(dt) {\n"
            "  if (velocity_x() > 0) { set_vx(velocity_x() + 1); }\n"
            "}\n"));
        scene.Start();
        CHECK_NEAR(rb->velocity.x, 3.0f, 0.001f);
        CHECK_NEAR(rb->velocity.y, 4.0f, 0.001f);
        scene.Update(0.016f);
        CHECK_NEAR(rb->velocity.x, 4.0f, 0.001f); // update bumped vx by 1
    }

    // --- add_impulse changes velocity immediately (scaled by 1/mass) ---
    {
        Scene scene("Imp");
        GameObject* go = scene.CreateGameObject("Jumper");
        auto* rb = go->AddComponent<Rigidbody2D>();
        rb->bodyType = Rigidbody2D::BodyType::Dynamic;
        rb->mass = 2.0f;                                  // impulse halved
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function start() { add_impulse(0, 10); }"));
        scene.Start();
        CHECK_NEAR(rb->velocity.y, 5.0f, 0.001f);        // 10 * (1/2)
    }

    // --- set_image swaps a sibling UIImage's texture ---
    {
        Scene scene("Img");
        GameObject* go = scene.CreateGameObject("Icon");
        auto* im = go->AddComponent<UIImage>();
        im->texture = "off.png";
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function start() { set_image(\"on.png\"); }"));
        scene.Start();
        CHECK(im->texture == "on.png");
    }

    TEST_MAIN_RESULT();
}
