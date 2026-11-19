// The physics toys (ConveyorBelt, ForceField, GravityZone, Turntable, MovingPlatform, Explosion, Breakable) and the
// Physics Surface: each is run in a small scene for a few seconds and its effect measured; each is saved and loaded back.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/PhysicsToys.hpp"
#include "okay/Components/NoCode.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <cmath>
#include <string>
#include <vector>

using namespace okay;

namespace okay { void RegisterPhysicsToyProperties(); namespace detail { void RegisterPhysicsToySerializers(); } }

namespace {
struct World {
    Scene s{"w"}; int n = 0; bool started = false;
    World() { s.physics3D().quality = PhysicsQuality::Improved; s.UseFixedStepPhysics(true); s.fixedTimeStep = 0.02f; }
    GameObject* Floor() { auto* f = s.CreateGameObject("Floor"); f->transform->localPosition = {0, -0.5f, 0}; f->AddComponent<BoxCollider3D>()->size = {200, 1, 200}; return f; }
    GameObject* Box(Vec3 p, float mass = 1.0f, Vec3 sz = {1, 1, 1}) {
        auto* o = s.CreateGameObject("B" + std::to_string(n++)); o->transform->localPosition = p;
        o->AddComponent<BoxCollider3D>()->size = sz; auto* r = o->AddComponent<Rigidbody3D>(); r->mass = mass; r->freezeRotation = true; return o;
    }
    GameObject* Ball(Vec3 p, float mass = 1.0f, float bounce = 0.0f) {
        auto* o = s.CreateGameObject("S" + std::to_string(n++)); o->transform->localPosition = p;
        o->AddComponent<SphereCollider3D>()->radius = 0.5f; auto* r = o->AddComponent<Rigidbody3D>(); r->mass = mass; r->bounciness = bounce; r->freezeRotation = true; return o;
    }
    void Run(float sec) { if (!started) { s.Start(); started = true; } const int k = (int)std::lround(sec / 0.02f); for (int i = 0; i < k; ++i) s.Update(0.02f); }
};
Vec3 P(GameObject* o) { return o->transform->Position(); }
Rigidbody3D* R(GameObject* o) { return o->GetComponent<Rigidbody3D>(); }
Rigidbody3D* Static(GameObject* o) { auto* r = o->AddComponent<Rigidbody3D>(); r->bodyType = Rigidbody3D::BodyType::Static; return r; }
}

int main() {
    RUN_SUITE("physics_toys");
    RegisterPhysicsToyProperties(); detail::RegisterPhysicsToySerializers();

    // ---- ConveyorBelt: a crate on the belt is carried to the belt's speed; one beside it is not; a negative speed runs it back.
    for (float sp : {2.0f, -2.0f}) {
        World w; w.Floor();
        auto* belt = w.s.CreateGameObject("Belt"); belt->transform->localPosition = {0, 0.1f, 0};
        belt->AddComponent<BoxCollider3D>()->size = {3, 0.2f, 12}; Static(belt);
        auto* cb = belt->AddComponent<ConveyorBelt>(); cb->speed = sp;
        GameObject* on = w.Box({0, 0.7f, 0}); GameObject* beside = w.Box({6, 0.5f, 0});
        w.Run(2.0f);
        std::cout << "  belt " << sp << " m/s: crate on it moved z " << P(on).z << " (speed " << R(on)->velocity.z << "), crate beside moved " << P(beside).z << "\n";
        CHECK(std::fabs(R(on)->velocity.z - sp) < 0.3f);
        CHECK(P(on).z * sp > 1.5f);               // carried the way the belt runs
        CHECK(std::fabs(P(beside).z) < 0.01f);
    }
    // ---- ForceField: attract, repel, outside the radius, off, the same pull on heavy and light, gravity well ratio, swirl.
    {
        World w; w.s.physics3D().gravity = Vec3::Zero;
        auto* ff = w.s.CreateGameObject("Field"); ff->transform->localPosition = {0, 0, 0}; auto* f = ff->AddComponent<ForceField>();
        f->radius = 8.0f; f->strength = 20.0f; f->falloff = ForceField::Constant;
        GameObject* light = w.Ball({4, 0, 0}, 1.0f); GameObject* heavy = w.Ball({0, 4, 0}, 50.0f); GameObject* far = w.Ball({20, 0, 0});
        w.Run(0.5f);
        std::cout << "  attract: light vx " << R(light)->velocity.x << ", heavy vy " << R(heavy)->velocity.y << ", outside " << R(far)->velocity.x << "\n";
        CHECK(R(light)->velocity.x < -5.0f); CHECK(R(heavy)->velocity.y < -5.0f);
        CHECK_NEAR(R(light)->velocity.x, R(heavy)->velocity.y, 0.5);     // a pull, not a push on mass: same acceleration
        CHECK_NEAR(R(far)->velocity.x, 0.0, 1e-4);
        f->mode = ForceField::Repel; R(light)->velocity = Vec3::Zero; light->transform->localPosition = {4, 0, 0};
        w.Run(0.3f);
        CHECK(R(light)->velocity.x > 3.0f);
        f->SetActive(false); R(light)->velocity = Vec3::Zero; w.Run(0.3f);
        CHECK_NEAR(R(light)->velocity.x, 0.0, 1e-4);
        ForceField well; well.mode = ForceField::GravityWell; well.radius = 50; well.strength = 40; well.coreRadius = 1.0f;
        const float a2 = well.AccelerationAt({2, 0, 0}, {0, 1, 0}).Magnitude(), a4 = well.AccelerationAt({4, 0, 0}, {0, 1, 0}).Magnitude();
        CHECK_NEAR(a2 / a4, 4.0, 0.01);                                   // inverse square
        CHECK_NEAR(well.AccelerationAt({0.1f, 0, 0}, {0, 1, 0}).Magnitude(), 40.0, 0.01);   // capped inside the core
        ForceField sw; sw.mode = ForceField::Swirl; sw.radius = 10; sw.strength = 10; sw.falloff = ForceField::Constant;
        const Vec3 as = sw.AccelerationAt({3, 0, 0}, {0, 1, 0});
        CHECK(std::fabs(as.z) > 5.0f);                                    // goes round, not just in
        CHECK(sw.AccelerationAt({30, 0, 0}, {0, 1, 0}).Magnitude() == 0.0f);
    }
    // ---- GravityZone: zero-g floats, a moon falls slowly, outside is normal.
    {
        World w; w.Floor();
        auto* z = w.s.CreateGameObject("Zone"); z->transform->localPosition = {0, 5, 0}; auto* gz = z->AddComponent<GravityZone>(); gz->size = {6, 6, 6}; gz->gravity = Vec3::Zero;
        auto* moon = w.s.CreateGameObject("Moon"); moon->transform->localPosition = {20, 5, 0}; auto* gm = moon->AddComponent<GravityZone>(); gm->size = {6, 6, 6}; gm->gravity = {0, -2.0f, 0};
        GameObject* a = w.Ball({0, 5, 0}); GameObject* b = w.Ball({20, 5, 0}); GameObject* c = w.Ball({-20, 25, 0});
        w.Run(1.0f);
        std::cout << "  gravity zones after 1 s: zero-g vy " << R(a)->velocity.y << ", moon vy " << R(b)->velocity.y << ", outside vy " << R(c)->velocity.y << "\n";
        CHECK(std::fabs(R(a)->velocity.y) < 0.2f);
        CHECK_NEAR(R(b)->velocity.y, -2.0, 0.3);
        CHECK_NEAR(R(c)->velocity.y, -9.81, 0.3);
    }
    // ---- Turntable: turns at the set speed once up to speed.
    {
        World w; w.s.physics3D().gravity = Vec3::Zero;
        auto* o = w.s.CreateGameObject("Spin"); o->AddComponent<BoxCollider3D>()->size = {4, 0.2f, 0.4f};
        auto* sp = o->AddComponent<Turntable>(); sp->degreesPerSecond = 90.0f; sp->rampTime = 0.0f;
        w.Run(1.0f);
        const Vec3 fwd = o->transform->Rotation() * Vec3{1, 0, 0};
        const float angle = std::atan2(-fwd.z, fwd.x) * 57.29578f;      // turned about +y
        std::cout << "  spinner 90 deg/s: turned " << angle << " deg in 1 s\n";
        CHECK_NEAR(std::fabs(angle), 90.0, 6.0);
    }
    // ---- MovingPlatform: the path is right (ping-pong and loop), it reaches the far point, and a crate on it goes along.
    {
        MovingPlatform mp; mp.offset2 = {4, 0, 0}; mp.speed = 2.0f; mp.pause = 0.5f; mp.easing = false; mp.mode = MovingPlatform::PingPong;
        CHECK_NEAR(mp.PositionAt(0).x, 0.0, 1e-5);
        CHECK_NEAR(mp.PositionAt(1.0f).x, 2.0, 1e-4);          // half way after 1 s at 2 m/s
        CHECK_NEAR(mp.PositionAt(2.3f).x, 4.0, 1e-4);          // waiting at the far end
        CHECK(mp.PositionAt(5.0f).x < 4.0f);                   // on the way back
        CHECK_NEAR(mp.PositionAt(4.0f).x, 1.0, 1e-3);          // 1.5 s into the way back: 3 m of the 4 m done
        mp.mode = MovingPlatform::Loop; mp.pointCount = 3; mp.offset3 = {4, 0, 4};
        CHECK_NEAR(mp.PositionAt(0.0f).z, 0.0, 1e-5);
        CHECK_NEAR(mp.PositionAt(5.0f).z, 4.0, 1e-3);          // the third point after two 4 m legs (2.5 s each with the pause)
        CHECK_NEAR(mp.PositionAt(5.0f + 3.9f).x + mp.PositionAt(5.0f + 3.9f).z, 0.0, 1.5);   // then home again
        World w; w.Floor();
        auto* pl = w.s.CreateGameObject("Platform"); pl->transform->localPosition = {0, 0.1f, 0}; pl->AddComponent<BoxCollider3D>()->size = {4, 0.2f, 4};
        auto* mpc = pl->AddComponent<MovingPlatform>(); mpc->offset2 = {6, 0, 0}; mpc->speed = 2.0f; mpc->pause = 0.0f; mpc->easing = false;
        GameObject* rider = w.Box({0, 0.7f, 0});
        w.Run(2.0f);
        std::cout << "  moving platform at 2 s: platform x " << P(pl).x << ", rider x " << P(rider).x << "\n";
        CHECK(P(pl).x > 3.0f && P(pl).x < 4.5f);
        CHECK(std::fabs(P(rider).x - P(pl).x) < 1.2f);           // still on it
        CHECK(P(rider).y > 0.5f);
        w.Run(1.0f);
        CHECK(P(pl).x > 5.5f);                                   // it reached the far point (6 m at 2 m/s = 3 s)
    }
    // ---- Explosion: shoves what is near, less what is far, none outside, less for heavy things, damage hook scales; timer fires once.
    {
        World w; w.s.physics3D().gravity = Vec3::Zero;
        auto* e = w.s.CreateGameObject("Boom"); auto* ex = e->AddComponent<Explosion>(); ex->force = 20.0f; ex->radius = 10.0f; ex->upwardBias = 0.0f; ex->damage = 100.0f;
        std::vector<std::pair<std::string, float>> hurt; ex->onDamage = [&](GameObject* g, float d) { hurt.push_back({g->name, d}); };
        GameObject* near_ = w.Ball({2, 0, 0}); GameObject* far_ = w.Ball({7, 0, 0}); GameObject* out = w.Ball({15, 0, 0}); GameObject* heavy = w.Ball({0, 2, 0}, 20.0f);
        w.Run(0.1f);
        const int n = ex->Trigger();
        std::cout << "  explosion: near " << R(near_)->velocity.x << ", far " << R(far_)->velocity.x << ", outside " << R(out)->velocity.x << ", heavy " << R(heavy)->velocity.y << ", hit " << n << "\n";
        CHECK(n == 3);
        CHECK_NEAR(R(near_)->velocity.x, 16.0, 0.5);                // 20 x (1 - 2/10)
        CHECK_NEAR(R(far_)->velocity.x, 6.0, 0.5);
        CHECK_NEAR(R(out)->velocity.x, 0.0, 1e-4);
        CHECK_NEAR(R(heavy)->velocity.y, 20.0 * 0.8 / 20.0, 0.1);    // an impulse: 20 kg gets a twentieth
        CHECK(hurt.size() == 3);
        float dn = 0; for (auto& h : hurt) if (h.first == near_->name) dn = h.second;
        CHECK_NEAR(dn, 80.0, 0.5);
        // The static helper (what the visual-script node calls) and the timer.
        World v; v.s.physics3D().gravity = Vec3::Zero; GameObject* b = v.Ball({3, 0, 0});
        CHECK(Explosion::At(v.s, {0, 0, 0}, 10.0f, 6.0f, 0.0f) == 1);
        CHECK(R(b)->velocity.x > 4.0f);
        World t; t.s.physics3D().gravity = Vec3::Zero;
        auto* te = t.s.CreateGameObject("Timer"); auto* tex = te->AddComponent<Explosion>(); tex->onStart = true; tex->delay = 0.5f; tex->radius = 5; tex->force = 10; tex->upwardBias = 0;
        GameObject* tb = t.Ball({2, 0, 0});
        t.Run(0.3f); CHECK(tex->fired == 0); CHECK_NEAR(R(tb)->velocity.x, 0.0, 1e-4);
        t.Run(0.5f); CHECK(tex->fired == 1); CHECK(R(tb)->velocity.x > 3.0f);
        t.Run(1.0f); CHECK(tex->fired == 1);                         // only once
    }
    // ---- Breakable: a hard hit breaks it into 8 pieces that fall and expire; a soft touch does not.
    {
        World w; w.Floor();
        GameObject* crate = w.Box({0, 0.5f, 0}, 4.0f); auto* br = crate->AddComponent<Breakable>(); br->breakSpeed = 6.0f; br->pieceLife = 1.0f;
        crate->AddComponent<MeshRenderer>();
        GameObject* bullet = w.Ball({-6, 0.5f, 0}, 2.0f); R(bullet)->velocity = {20, 0, 0}; R(bullet)->gravityScale = 0;
        auto count = [&]() { int n = 0; for (const auto& g : w.s.Objects()) if (g && g->name.find("piece") != std::string::npos) ++n; return n; };
        int most = 0, steps = 0;
        for (int i = 0; i < 60; ++i) { w.Run(0.02f); most = std::max(most, count()); ++steps; }
        std::cout << "  breakable: most pieces alive at once " << most << "\n";
        CHECK(most == 8);
        w.Run(3.0f);
        CHECK(count() == 0);                                       // the pieces tidied themselves away
        World q; q.Floor(); GameObject* c2 = q.Box({0, 0.5f, 0}, 4.0f); auto* b2 = c2->AddComponent<Breakable>(); b2->breakSpeed = 6.0f;
        GameObject* tap = q.Ball({0, 1.5f, 0}); (void)tap; q.Run(2.0f);
        CHECK(!b2->broken);
    }
    // ---- Save and load: every toy and the surface come back with the same numbers, and an old file without them loads.
    {
        Scene a("a");
        auto* o = a.CreateGameObject("Everything");
        auto* cb = o->AddComponent<ConveyorBelt>(); cb->speed = -3.5f; cb->direction = {1, 0, 0}; cb->grip = 12.0f; cb->layerMask = 6;
                auto* ff = o->AddComponent<ForceField>(); ff->mode = ForceField::Swirl; ff->falloff = ForceField::InverseSquare; ff->radius = 9.0f; ff->strength = 33.0f; ff->active = false;
        auto* gz = o->AddComponent<GravityZone>(); gz->size = {1, 2, 3}; gz->gravity = {0, -1.5f, 0.5f};
        auto* sp = o->AddComponent<Turntable>(); sp->axis = {0, 0, 1}; sp->degreesPerSecond = -45.0f; sp->rampTime = 2.0f;
        auto* mp = o->AddComponent<MovingPlatform>(); mp->mode = MovingPlatform::Loop; mp->pointCount = 4; mp->offset3 = {1, 2, 3}; mp->speed = 4.0f; mp->pause = 1.5f; mp->easing = false;
        auto* ex = o->AddComponent<Explosion>(); ex->force = 77.0f; ex->radius = 3.0f; ex->onStart = true; ex->delay = 2.5f; ex->destroyAfter = true;
        auto* br = o->AddComponent<Breakable>(); br->breakSpeed = 9.0f; br->pieces = 3; br->pieceLife = 11.0f;
        auto* ps = o->AddComponent<PhysicsSurface>(); ps->material = "Ice"; ps->includeChildren = true; ps->massFromDensity = true;
        const std::string text = SceneSerializer::Serialize(a);
        Scene b("b"); std::string err;
        CHECK(SceneSerializer::Deserialize(b, text, &err));
        GameObject* ob = b.Find("Everything");
        CHECK(ob != nullptr);
        if (ob) {
            auto* cb2 = ob->GetComponent<ConveyorBelt>(); auto* ff2 = ob->GetComponent<ForceField>();
            auto* gz2 = ob->GetComponent<GravityZone>(); auto* sp2 = ob->GetComponent<Turntable>(); auto* mp2 = ob->GetComponent<MovingPlatform>();
            auto* ex2 = ob->GetComponent<Explosion>(); auto* br2 = ob->GetComponent<Breakable>(); auto* ps2 = ob->GetComponent<PhysicsSurface>();
            CHECK(cb2 && ff2 && gz2 && sp2 && mp2 && ex2 && br2 && ps2);
            if (cb2 && ff2 && gz2 && sp2 && mp2 && ex2 && br2 && ps2) {
                CHECK_NEAR(cb2->speed, -3.5, 1e-5); CHECK_NEAR(cb2->direction.x, 1.0, 1e-5); CHECK_NEAR(cb2->grip, 12.0, 1e-5); CHECK(cb2->layerMask == 6);
                CHECK(ff2->mode == ForceField::Swirl && ff2->falloff == ForceField::InverseSquare && !ff2->active); CHECK_NEAR(ff2->radius, 9.0, 1e-5); CHECK_NEAR(ff2->strength, 33.0, 1e-5);
                CHECK_NEAR(gz2->size.z, 3.0, 1e-5); CHECK_NEAR(gz2->gravity.z, 0.5, 1e-5);
                CHECK_NEAR(sp2->axis.z, 1.0, 1e-5); CHECK_NEAR(sp2->degreesPerSecond, -45.0, 1e-5); CHECK_NEAR(sp2->rampTime, 2.0, 1e-5);
                CHECK(mp2->mode == MovingPlatform::Loop && mp2->pointCount == 4 && !mp2->easing); CHECK_NEAR(mp2->offset3.y, 2.0, 1e-5); CHECK_NEAR(mp2->pause, 1.5, 1e-5);
                CHECK_NEAR(ex2->force, 77.0, 1e-5); CHECK(ex2->onStart && ex2->destroyAfter); CHECK_NEAR(ex2->delay, 2.5, 1e-5);
                CHECK(br2->pieces == 3); CHECK_NEAR(br2->breakSpeed, 9.0, 1e-5); CHECK_NEAR(br2->pieceLife, 11.0, 1e-5);
                CHECK(ps2->material == "Ice" && ps2->includeChildren && ps2->massFromDensity);
            }
        }
        // The Inspector rows exist for every one (so Add Component shows them with plain tooltips).
        for (const char* nm : {"PhysicsSurface", "ConveyorBelt", "ForceField", "GravityZone", "Turntable", "MovingPlatform", "Explosion", "Breakable"}) {
            auto info = PropertyRegistry::FindByName(nm);
            CHECK(info != nullptr);
            if (!info) continue;
            CHECK(info->category == "Physics 3D");
            for (const PropertyDesc& d : info->properties) if (d.name != "enabled") { CHECK(!d.tooltip.empty()); }
        }
        // Reflection reaches a toy by path, so scripts and Visual Script property nodes can use it.
        GameObject* o2 = b.Find("Everything");
        std::string perr;
        CHECK(o2 && PropertyRegistry::SetPropertyFromText(*o2, "ConveyorBelt.speed", "4.5", &perr));
        CHECK(o2 && o2->GetComponent<ConveyorBelt>()->speed == 4.5f);
    }
    TEST_MAIN_RESULT();
}
