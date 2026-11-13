// Flat box and capsule casts, their overlap twins, and a 2D explosion.
// Each check builds a real scene and asserts the hit, the normal, the distance,
// or the blast's falloff. The demo scene at examples/physics2d is the same idea
// a person can press Play on.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
#include <cmath>
#include <string>

using namespace okay;

namespace {

GameObject* Wall(Scene& s, const char* name, const Vec2& pos, const Vec2& size, int layer = 0, bool trigger = false) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = {pos.x, pos.y, 0.0f};
    auto* b = g->AddComponent<BoxCollider2D>();
    b->size = size; b->layer = layer; b->isTrigger = trigger;
    return g;
}

GameObject* Body(Scene& s, const char* name, const Vec2& pos, float mass = 1.0f) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = {pos.x, pos.y, 0.0f};
    auto* rb = g->AddComponent<Rigidbody2D>();
    rb->mass = mass;
    rb->gravityScale = 0.0f;
    return g;
}

} // namespace

int main() {
    RUN_SUITE("physics2d_casts");

    // ---- BoxCast: hit, normal, distance, a miss, a thick hit a ray misses ----
    {
        Scene s("Box");
        s.physicsEnabled = false;
        GameObject* wall = Wall(s, "Wall", {5, 0}, {1, 1});   // face at x = 4.5
        Wall(s, "High", {5, 3}, {1, 1});
        s.Start();
        Physics2D& p = s.physics();

        RaycastHit2D h = p.BoxCast(s, {0, 0}, {0.5f, 0.5f}, {1, 0});
        CHECK(h.hit && h.gameObject == wall);
        CHECK_NEAR(h.distance, 4.0f, 0.02f);          // centre stops 0.5 before the face
        CHECK(h.normal.x < -0.9f);                    // normal points back at the caster
        CHECK_NEAR(h.point.x, 4.5f, 0.05f);

        CHECK(!p.BoxCast(s, {0, 8}, {0.5f, 0.5f}, {1, 0}).hit);

        // A thin ray at y = 0.9 misses the wall (top at 0.5). A box 0.5 tall clips it.
        CHECK(!p.Raycast(s, {0, 0.9f}, {1, 0}, 100.0f, nullptr, Physics2D::kAllLayers, false).hit);
        CHECK(p.BoxCast(s, {0, 0.9f}, {0.2f, 0.5f}, {1, 0}).hit);

        // Turned 45 degrees, the corner sticks out farther than the flat face.
        const float corner = 0.5f * std::sqrt(2.0f);
        RaycastHit2D turned = p.BoxCast(s, {0, 0}, {0.5f, 0.5f}, {1, 0}, Mathf::Deg2Rad * 45.0f);
        CHECK(turned.hit && turned.gameObject == wall);
        CHECK_NEAR(turned.distance, 4.5f - corner, 0.05f);
    }

    // ---- BoxCast: triggers, layers, ignore ----
    {
        Scene s("Filter");
        s.physicsEnabled = false;
        GameObject* trig = Wall(s, "Trig", {2, 0}, {1, 1}, 0, true);
        GameObject* solid = Wall(s, "Solid", {5, 0}, {1, 1}, 3);
        s.Start();
        Physics2D& p = s.physics();
        CHECK(p.BoxCast(s, {0, 0}, {0.4f, 0.4f}, {1, 0}).gameObject == solid);   // triggers skipped
        CHECK(p.BoxCast(s, {0, 0}, {0.4f, 0.4f}, {1, 0}, 0.0f, 100.0f, nullptr,
                       Physics2D::kAllLayers, true).gameObject == trig);
        CHECK(p.BoxCast(s, {0, 0}, {0.4f, 0.4f}, {1, 0}, 0.0f, 100.0f, solid).gameObject == nullptr);
        CHECK(!p.BoxCast(s, {0, 0}, {0.4f, 0.4f}, {1, 0}, 0.0f, 100.0f, nullptr, 1u << 0).hit);
    }

    // ---- CapsuleCast: distance, normal, miss ----
    {
        Scene s("Cap");
        s.physicsEnabled = false;
        GameObject* wall = Wall(s, "Wall", {5, 0}, {1, 2});
        s.Start();
        Physics2D& p = s.physics();
        RaycastHit2D h = p.CapsuleCast(s, {0, -0.5f}, {0, 0.5f}, 0.25f, {1, 0});
        CHECK(h.hit && h.gameObject == wall);
        CHECK_NEAR(h.distance, 4.25f, 0.02f);         // radius 0.25 stops short of the face at 4.5
        CHECK(h.normal.x < -0.9f);
        CHECK(!p.CapsuleCast(s, {0, 4}, {0, 5}, 0.25f, {1, 0}).hit);
        // A short wall the tall capsule still reaches.
        CHECK(p.CapsuleCast(s, {0, 0.8f}, {0, 1.6f}, 0.2f, {1, 0}).hit);
    }

    // ---- OverlapCapsule and a turned OverlapBox ----
    {
        Scene s("Over");
        s.physicsEnabled = false;
        GameObject* a = Wall(s, "A", {2, 0}, {1, 1});
        GameObject* b = Wall(s, "B", {5, 0}, {1, 1});
        s.Start();
        Physics2D& p = s.physics();
        auto cap = p.OverlapCapsule(s, {2, 0}, {5, 0}, 0.2f);
        CHECK(cap.size() == 2);
        CHECK(p.OverlapCapsule(s, {2, 3}, {5, 3}, 0.2f).empty());

        // A thin upright box at the origin misses both. Turned 90 degrees it lies
        // along X and reaches A (at x=2) but not B (at x=5).
        CHECK(p.OverlapBox(s, {0, 0}, {0.1f, 3.0f}).empty());
        auto turned = p.OverlapBox(s, {0, 0}, {0.1f, 3.0f}, Physics2D::kAllLayers, true, nullptr,
                                   Mathf::Deg2Rad * 90.0f);
        CHECK(turned.size() == 1 && turned[0]->gameObject == a);
        CHECK(p.OverlapBox(s, {2, 0}, {0.2f, 0.2f}, Physics2D::kAllLayers, true, a).empty());
        (void)b;
    }

    // ---- Explosion: falloff, outside the radius, upward bias, immovable bodies ----
    {
        Scene s("Boom");
        s.physicsEnabled = false;
        GameObject* near = Body(s, "Near", {2, 0});
        auto* rb = near->GetComponent<Rigidbody2D>();
        rb->AddExplosionForce(10.0f, {0, 0}, 5.0f);    // dist 2, falloff 0.6 -> impulse 6
        CHECK_NEAR(rb->velocity.x, 6.0f, 1e-3f);
        CHECK(std::fabs(rb->velocity.y) < 1e-5f);

        GameObject* heavy = Body(s, "Heavy", {2, 1}, 2.0f);
        heavy->GetComponent<Rigidbody2D>()->AddExplosionForce(10.0f, {0, 1}, 5.0f);
        CHECK_NEAR(heavy->GetComponent<Rigidbody2D>()->velocity.x, 3.0f, 1e-3f);

        GameObject* far = Body(s, "Far", {100, 0});
        far->GetComponent<Rigidbody2D>()->AddExplosionForce(10.0f, {0, 0}, 5.0f);
        CHECK(far->GetComponent<Rigidbody2D>()->velocity.x == 0.0f);

        GameObject* up = Body(s, "Up", {1, 0});
        up->GetComponent<Rigidbody2D>()->AddExplosionForce(10.0f, {0, 0}, 5.0f, 1.0f);
        // dir (1, 0) plus an upward bias of 1, times force 10 * falloff 0.8
        CHECK_NEAR(up->GetComponent<Rigidbody2D>()->velocity.x, 8.0f, 1e-3f);
        CHECK_NEAR(up->GetComponent<Rigidbody2D>()->velocity.y, 8.0f, 1e-3f);

        GameObject* still = Body(s, "Still", {2, 2});
        still->GetComponent<Rigidbody2D>()->bodyType = Rigidbody2D::BodyType::Kinematic;
        still->GetComponent<Rigidbody2D>()->AddExplosionForce(10.0f, {0, 2}, 5.0f);
        CHECK(still->GetComponent<Rigidbody2D>()->velocity.x == 0.0f);
    }

    // ---- Text script: each builtin, in a scene, with the effect you can read ----
    {
        Scene s("Script");
        s.physics().gravity = {0, 0};
        Wall(s, "Wall", {5, 0}, {1, 1});
        GameObject* probe = s.CreateGameObject("Probe");
        auto* sc = probe->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() {\n"
            "  var hit = box_cast(0, 0, 0.5, 0.5, 1, 0, 20);\n"
            "  if (hit == \"Wall\") { set_x(ray_dist()); }\n"
            "  if (ray_nx() < -0.5) { set_y(1); }\n"
            "  var pill = capsule_cast(0, -0.4, 0, 0.4, 0.25, 1, 0, 20);\n"
            "  if (pill == \"Wall\" && ray_dist() > 4) { set_z(ray_dist()); }\n"
            "  var names = overlap_capsule(4, 0, 6, 0, 0.2);\n"
            "  if (count(names) == 1) { set_scale(2); }\n"
            "}\n"));
        s.Start();
        CHECK_NEAR(probe->transform->localPosition.x, 4.0f, 0.05f);
        CHECK_NEAR(probe->transform->localPosition.y, 1.0f, 0.001f);
        CHECK(probe->transform->localPosition.z > 4.0f);
        CHECK_NEAR(probe->transform->localScale.x, 2.0f, 0.001f);
    }
    {
        Scene s("BlastScript");
        s.physics().gravity = {0, 0};
        GameObject* crate = Body(s, "Crate", {2, 0});
        auto* sc = crate->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() {\n"
            "  add_explosion_force(0, 0, 10, 5, 0);\n"
            "}\n"));
        s.Start();
        CHECK_NEAR(crate->GetComponent<Rigidbody2D>()->velocity.x, 6.0f, 1e-3f);
    }

    // ---- Visual script nodes: a cast's distance, an overlap count, a blast ----
    {
        Scene s("Nodes");
        s.physics().gravity = {0, 0};
        s.physicsEnabled = false;
        Wall(s, "Wall", {5, 0}, {1, 1});
        GameObject* look = s.CreateGameObject("Look");
        auto* vs = look->AddComponent<VisualScriptComponent>();
        CHECK(vs->LoadFromText(
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 BoxCast2D\n"
            "node 3 SetVar dist\n"
            "node 4 CapsuleCast2D\n"
            "node 5 SetVar pill\n"
            "node 6 OverlapCapsule2D\n"
            "node 7 SetVar n\n"
            "exec 1 0 3\nexec 3 0 5\nexec 5 0 7\n"
            "data 3 0 2 3\ndata 5 0 4 0\ndata 7 0 6 0\n"));
        // The overlap capsule stands on this object, so it misses the wall at x = 5.
        s.Start();
        s.Update(1.0f / 60.0f);
        CHECK_NEAR(vs->GetVariable("dist").AsFloat(), 4.0f, 0.05f);
        CHECK(vs->GetVariable("pill").AsBool());
        CHECK_NEAR(vs->GetVariable("n").AsFloat(), 0.0f, 0.001f);
    }
    {
        Scene s("NodeBlast");
        s.physics().gravity = {0, 0};
        GameObject* crate = Body(s, "Crate", {2, 0});
        auto* vs = crate->AddComponent<VisualScriptComponent>();
        CHECK(vs->LoadFromText(
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 AddExplosionForce\ndefault 2 1 10\ndefault 2 2 5\ndefault 2 3 0\n"
            "node 3 MakeVec3\ndefault 3 0 0\ndefault 3 1 0\ndefault 3 2 0\n"
            "exec 1 0 2\ndata 2 0 3 0\n"));
        s.Start();
        s.Update(1.0f / 60.0f);
        CHECK_NEAR(crate->GetComponent<Rigidbody2D>()->velocity.x, 6.0f, 1e-3f);
    }

    // ---- The demo scene: near crate flies, far crate does not, the lookout hits the wall ----
    {
        Scene s("Demo");
        std::string err;
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/physics2d/casts_and_blast.okayscene";
        CHECK(SceneSerializer::LoadFromFile(s, path, &err));
        if (!err.empty()) std::printf("  demo: %s\n", err.c_str());
        s.Start();
        s.Update(1.0f / 60.0f);
        GameObject* near = s.Find("Near Crate");
        GameObject* far = s.Find("Far Crate");
        GameObject* look = s.Find("Lookout");
        CHECK(near && far && look);
        if (near && far && look) {
            auto* vs = look->GetComponent<VisualScriptComponent>();
            CHECK(near->GetComponent<Rigidbody2D>()->velocity.x > 10.0f);
            CHECK(near->GetComponent<Rigidbody2D>()->velocity.y > 1.0f);   // upward bias
            CHECK(std::fabs(far->GetComponent<Rigidbody2D>()->velocity.x) < 1e-4f);
            CHECK(vs && vs->GetVariable("saw").AsBool());
            CHECK_NEAR(vs->GetVariable("dist").AsFloat(), 5.0f, 0.05f);
        }
    }

    TEST_MAIN_RESULT();
}
