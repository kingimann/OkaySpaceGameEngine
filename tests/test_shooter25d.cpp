#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>

#define CHECK_EQ(a, b) CHECK((a) == (b))

using namespace okay;

// TopDownShooterController, Projectile3D and Platformer25DController (LittleBigPlanet-
// style 2.5D): movement, aiming, firing, ammo, dash, depth layers, jump, grab, saves.

static const float kDt = 1.0f / 60.0f;

static GameObject* Ground(Scene& s, float width = 200.0f, float depth = 20.0f, float z = 0.0f) {
    GameObject* g = s.CreateGameObject("Ground");
    g->transform->SetPosition({0, -0.5f, z});
    g->AddComponent<BoxCollider3D>()->size = {width, 1, depth};
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    return g;
}
static GameObject* Body(Scene& s, const char* name, const Vec3& feet) {
    GameObject* p = s.CreateGameObject(name);
    p->transform->SetPosition(feet);
    p->AddComponent<Rigidbody3D>();
    auto* col = p->AddComponent<BoxCollider3D>();
    col->size = {0.6f, 1.8f, 0.6f};
    col->offset = {0.0f, 0.9f, 0.0f};
    return p;
}
static GameObject* Box(Scene& s, const char* name, const Vec3& pos, const Vec3& size,
                       Rigidbody3D::BodyType body = Rigidbody3D::BodyType::Static) {
    GameObject* o = s.CreateGameObject(name);
    o->transform->SetPosition(pos);
    o->AddComponent<BoxCollider3D>()->size = size;
    o->AddComponent<Rigidbody3D>()->bodyType = body;
    return o;
}
static void Step(Scene& s, std::vector<char> keys = {}, int frames = 1) {
    for (int i = 0; i < frames; ++i) { Input::FeedKeys(keys); s.Update(kDt); }
}
static int CountNamed(Scene& s, const std::string& name) {
    int n = 0;
    for (const auto& up : s.Objects()) if (up && !up->IsDestroyed() && up->name == name) ++n;
    return n;
}

int main() {
    RUN_SUITE("shooter25d");
    Input::FeedMouse({0, 0}, 0);
    UICanvas::Set(1280.0f, 720.0f);

    // ================= Top-down shooter =================
    {   // WASD moves on the ground; move-direction aim faces the way you go.
        Scene s("tds");
        Ground(s);
        GameObject* p = Body(s, "Player", {0, 0.02f, 0});
        auto* ts = p->AddComponent<TopDownShooterController>();
        ts->aimMode = (int)TopDownShooterController::AimMode::MoveDirection;
        ts->dashKey = 0;
        s.Start();
        Step(s, {}, 30);
        Step(s, {'d'}, 40);
        CHECK(p->transform->Position().x > 2.0f);
        CHECK(Mathf::Abs(p->transform->Position().z) < 0.2f);
        CHECK(ts->AimDirection().x > 0.9f);
        // Body turned to face +X (meshes face -Z, so forward() points away from the aim).
        Vec3 f = p->transform->Rotation() * Vec3{0, 0, -1};
        CHECK(f.x > 0.8f);
    }
    {   // The mouse aims: cursor on the right half of the screen -> aim +X, while moving -X.
        Scene s("tdsmouse");
        Ground(s);
        GameObject* camGo = s.CreateGameObject("Main Camera");
        auto* cam = camGo->AddComponent<Camera>();
        cam->projection = Camera::Projection::Perspective; cam->main = true;
        GameObject* p = Body(s, "Player", {0, 0.02f, 0});
        auto* ts = p->AddComponent<TopDownShooterController>();
        ts->aimMode = (int)TopDownShooterController::AimMode::Mouse;
        ts->cameraDamping = 0.0f;
        s.Start();
        Input::FeedMouse({640, 360}, 0);
        Step(s, {}, 5);
        Input::FeedMouse({1150, 360}, 0);
        Step(s, {'a'}, 20);
        CHECK(ts->AimDirection().x > 0.8f);                // aims right ...
        CHECK(p->transform->Position().x < -0.3f);         // ... while running left
        CHECK(ts->AimPoint().x > p->transform->Position().x + 1.0f);
        Input::FeedMouse({640, 120}, 0);                    // top of the screen = -Z (away from the camera)
        Step(s, {}, 3);
        CHECK(ts->AimDirection().z < -0.8f);
        // The camera sits above and behind, looking down.
        CHECK(camGo->transform->Position().y > 5.0f);
        Input::FeedMouse({0, 0}, 0);
    }
    {   // Built-in bullets fly along the aim and damage a HealthStat target.
        Scene s("tdsfire");
        Ground(s);
        GameObject* p = Body(s, "Player", {0, 0.02f, 0});
        auto* ts = p->AddComponent<TopDownShooterController>();
        ts->aimMode = (int)TopDownShooterController::AimMode::MoveDirection;
        ts->spread = 0.0f; ts->damage = 15.0f; ts->dashKey = 0;
        GameObject* target = Box(s, "Target", {6, 1.0f, 0}, {1, 2, 1});
        auto* hp = target->AddComponent<HealthStat>();
        hp->maxHealth = hp->health = 100.0f;
        s.Start();
        Step(s, {}, 10);
        Step(s, {'d'}, 2);                                  // face +X
        Step(s, {}, 30);
        ts->Fire();
        CHECK_EQ(CountNamed(s, "Bullet"), 1);
        GameObject* b = s.Find("Bullet");
        auto* pr = b ? b->GetComponent<Projectile3D>() : nullptr;
        CHECK(pr != nullptr);
        if (pr) { CHECK(pr->velocity.x > 20.0f); CHECK(pr->owner == p); }
        Step(s, {}, 30);
        CHECK_NEAR(hp->health, 85.0f, 0.01f);               // one hit
        CHECK_EQ(CountNamed(s, "Bullet"), 0);               // the bullet is gone
        CHECK_EQ(ts->ShotsFired(), 1);

        // A shotgun: 5 pellets per shot.
        ts->pellets = 5; ts->spread = 30.0f;
        ts->Fire();
        CHECK_EQ(CountNamed(s, "Bullet"), 5);
        // Holding fire with autoFire respects the fire rate.
        ts->pellets = 1;
        Step(s, {}, 120);
        int before = ts->ShotsFired();
        Input::FeedMouse({0, 0}, 1u);                       // hold left mouse
        Step(s, {}, 60);                                    // 1 second at 8 / s
        Input::FeedMouse({0, 0}, 0);
        int fired = ts->ShotsFired() - before;
        CHECK(fired >= 7 && fired <= 9);
    }
    {   // Hitscan hits instantly; magazine + reload; script-free ammo flow.
        Scene s("tdsammo");
        Ground(s);
        GameObject* p = Body(s, "Player", {0, 0.02f, 0});
        auto* ts = p->AddComponent<TopDownShooterController>();
        ts->aimMode = (int)TopDownShooterController::AimMode::MoveDirection;
        ts->fireMode = (int)TopDownShooterController::FireMode::Hitscan;
        ts->spread = 0.0f; ts->damage = 10.0f; ts->magazineSize = 3; ts->reloadTime = 0.5f; ts->dashKey = 0;
        GameObject* target = Box(s, "Target", {8, 1.0f, 0}, {1, 2, 1});
        auto* hp = target->AddComponent<HealthStat>();
        hp->maxHealth = hp->health = 100.0f;
        s.Start();
        CHECK_EQ(ts->ammo, 3);
        Step(s, {}, 10);
        Step(s, {'d'}, 2);
        Step(s, {}, 30);
        ts->Fire();
        CHECK_NEAR(hp->health, 90.0f, 0.01f);               // instant
        CHECK_EQ(CountNamed(s, "Bullet"), 0);               // no projectile
        ts->Fire(); ts->Fire();
        CHECK_EQ(ts->ammo, 0);
        CHECK(ts->IsReloading());                           // auto reload on empty
        ts->Fire();                                         // can't fire while reloading
        CHECK_NEAR(hp->health, 70.0f, 0.01f);
        Step(s, {}, 40);
        CHECK(!ts->IsReloading());
        CHECK_EQ(ts->ammo, 3);
        // R reloads a partly used magazine.
        ts->Fire();
        CHECK_EQ(ts->ammo, 2);
        Step(s, {}, 1); Step(s, {'r'}, 1);
        CHECK(ts->IsReloading());
        Step(s, {}, 40);
        CHECK_EQ(ts->ammo, 3);
    }
    {   // Dash: a burst of speed with i-frames that end afterwards.
        Scene s("tdsdash");
        Ground(s);
        GameObject* p = Body(s, "Player", {0, 0.02f, 0});
        auto* ts = p->AddComponent<TopDownShooterController>();
        ts->aimMode = (int)TopDownShooterController::AimMode::MoveDirection;
        auto* hp = p->AddComponent<HealthStat>();
        s.Start();
        Step(s, {}, 30);
        Step(s, {'d'}, 1);
        Step(s, {'d', ' '}, 1);
        CHECK(ts->IsDashing());
        CHECK(hp->godMode);
        auto* rb = p->GetComponent<Rigidbody3D>();
        CHECK(rb->velocity.x > 15.0f);
        Step(s, {}, 30);
        CHECK(!ts->IsDashing());
        CHECK(!hp->godMode);
    }
    {   // Dash feel: an eased burst that hands back to normal movement (no skid),
        // even with the old saved values (18 m/s for 0.18 s used to carry ~4.8 m).
        for (int variant = 0; variant < 2; ++variant) {
            Scene s("tdsdashfeel");
            Ground(s);
            GameObject* p = Body(s, "Player", {0, 0.02f, 0});
            auto* ts = p->AddComponent<TopDownShooterController>();
            ts->aimMode = (int)TopDownShooterController::AimMode::MoveDirection;
            if (variant == 1) { ts->dashSpeed = 18.0f; ts->dashTime = 0.18f; }   // a scene saved before
            auto* rb = p->GetComponent<Rigidbody3D>();
            s.Start();
            Step(s, {}, 30);
            Step(s, {'d'}, 1);                              // face +X
            Step(s, {}, 30);                                // stop
            float x0 = p->transform->Position().x;
            Step(s, {' '}, 1);                              // tap Space while standing still
            CHECK(ts->IsDashing());
            float peak = rb->velocity.x, prev = peak;
            bool eased = true;
            int frames = 1;
            while (ts->IsDashing() && frames < 120) {
                Step(s, {}, 1); ++frames;
                if (rb->velocity.x > prev + 1e-3f) eased = false;   // never speeds up again
                prev = rb->velocity.x;
            }
            CHECK(eased);
            CHECK(rb->velocity.x <= ts->walkSpeed * 0.5f + 0.05f);  // no skid at full speed
            Step(s, {}, 20);
            CHECK_NEAR(rb->velocity.x, 0.0f, 0.05f);
            float dist = p->transform->Position().x - x0;
            CHECK(dist > 1.2f && dist < 3.0f);              // a dodge, not a teleport
            CHECK(peak > 10.0f);
        }
    }
    {   // Dash while moving keeps going at walk speed (no stop, no jump in speed),
        // the pad's A button dashes too, and the cooldown runs after the dash ends.
        Scene s("tdsdashmove");
        Ground(s);
        GameObject* p = Body(s, "Player", {0, 0.02f, 0});
        auto* ts = p->AddComponent<TopDownShooterController>();
        ts->aimMode = (int)TopDownShooterController::AimMode::MoveDirection;
        auto* rb = p->GetComponent<Rigidbody3D>();
        s.Start();
        Step(s, {}, 30);
        Step(s, {'d'}, 40);
        CHECK_NEAR(rb->velocity.x, ts->walkSpeed, 0.3f);
        Step(s, {'d', ' '}, 1);
        CHECK(ts->IsDashing());
        int n = 0;
        while (ts->IsDashing() && n < 120) { Step(s, {'d'}, 1); ++n; }
        Step(s, {'d'}, 1);
        CHECK_NEAR(rb->velocity.x, ts->walkSpeed, 0.4f);   // straight back to walking
        Step(s, {'d', ' '}, 1);
        CHECK(!ts->IsDashing());                            // still cooling down
        Step(s, {'d'}, (int)(ts->dashCooldown * 60.0f) + 2);
        Input::FeedGamepad({0, 0}, 1u << 0);                // pad A
        Step(s, {'d'}, 1);
        CHECK(ts->IsDashing());
        Input::FeedGamepad({0, 0}, 0);
        Step(s, {'d'}, 30);
    }
    {   // Animation follows the body: duck while dashing, walk backwards when
        // backpedalling, idle as soon as it has stopped (not while still sliding).
        Scene s("tdsanim");
        Ground(s);
        GameObject* p = Body(s, "Player", {0, 0.02f, 0});
        auto* ch = p->AddComponent<Character>();
        auto* ts = p->AddComponent<TopDownShooterController>();
        ts->aimMode = (int)TopDownShooterController::AimMode::Mouse;
        GameObject* cam = s.CreateGameObject("Cam");
        auto* c = cam->AddComponent<Camera>();
        c->projection = Camera::Projection::Perspective; c->main = true;
        s.Start();
        Input::FeedMouse({640.0f, 100.0f}, 0);              // aim "up" the screen (-Z)
        Step(s, {}, 30);
        CHECK_EQ(ch->anim, 1);
        Step(s, {'w'}, 40);                                 // toward the aim
        CHECK_EQ(ch->anim, 2);
        CHECK(ch->animSpeed > 0.8f);
        Step(s, {'s'}, 40);                                 // away from the aim
        CHECK_EQ(ch->anim, 2);
        CHECK(ch->animSpeed < 0.0f);                        // legs step backwards
        Step(s, {}, 2);
        CHECK(ch->animSpeed < 0.0f);                        // ...while slowing down too
        Step(s, {}, 20);
        CHECK_EQ(ch->anim, 1);
        CHECK_NEAR(ch->animSpeed, 1.0f, 1e-4f);
        Step(s, {' '}, 1);
        CHECK(ts->IsDashing());
        CHECK_EQ(ch->anim, 6);                              // ducked dodge, not a run cycle
        Step(s, {}, 40);
        CHECK_EQ(ch->anim, 1);
        Input::FeedMouse({0, 0}, 0);
    }
    {   // Built-in animation changes crossfade instead of snapping.
        Scene s("charblend");
        GameObject* g = s.CreateGameObject("C");
        auto* ch = g->AddComponent<Character>();
        ch->Apply();
        s.Start();
        for (int i = 0; i < 30; ++i) s.Update(kDt);
        std::vector<Vec3> idle = ch->CurrentPose();
        ch->anim = 3;                                        // idle -> run
        s.Update(kDt);
        std::vector<Vec3> mid = ch->CurrentPose();
        std::vector<Vec3> run = ch->PoseAt(ch->animTime);    // the pure run pose this frame
        float dIdle = 0.0f, dRun = 0.0f;
        for (std::size_t i = 0; i < mid.size() && i < run.size() && i < idle.size(); ++i) {
            dIdle += (mid[i] - idle[i]).Magnitude();
            dRun  += (mid[i] - run[i]).Magnitude();
        }
        CHECK(dRun > 1.0f);                                  // not snapped to the run pose yet
        for (int i = 0; i < 30; ++i) s.Update(kDt);
        std::vector<Vec3> done = ch->CurrentPose();
        std::vector<Vec3> run2 = ch->PoseAt(ch->animTime);
        float dEnd = 0.0f;
        for (std::size_t i = 0; i < done.size() && i < run2.size(); ++i) dEnd += (done[i] - run2[i]).Magnitude();
        CHECK(dEnd < 1e-3f);                                 // fully the run pose once blended
        (void)dIdle;
    }
    {   // Projectile3D on its own: pierce, bounce, lifetime, impulse.
        Scene s("proj");
        GameObject* a = Box(s, "A", {3, 0, 0}, {1, 1, 1});
        GameObject* b = Box(s, "B", {6, 0, 0}, {1, 1, 1});
        auto* ha = a->AddComponent<HealthStat>(); ha->maxHealth = ha->health = 50;
        auto* hb = b->AddComponent<HealthStat>(); hb->maxHealth = hb->health = 50;
        GameObject* bullet = s.CreateGameObject("Shot");
        auto* pr = bullet->AddComponent<Projectile3D>();
        pr->velocity = {30, 0, 0}; pr->damage = 5; pr->pierce = 1;
        s.Start();
        Step(s, {}, 30);
        CHECK_NEAR(ha->health, 45.0f, 0.01f);               // went through A ...
        CHECK_NEAR(hb->health, 45.0f, 0.01f);               // ... and hit B
        CHECK_EQ(CountNamed(s, "Shot"), 0);

        GameObject* wall = Box(s, "Wall", {4, 0, 10}, {1, 4, 4});
        (void)wall;
        GameObject* bb = s.CreateGameObject("Bouncer");
        bb->transform->SetPosition({0, 0, 10});
        auto* bp = bb->AddComponent<Projectile3D>();
        bp->velocity = {20, 0, 0}; bp->bounce = true; bp->maxBounces = 1; bp->lifetime = 0.5f;
        Step(s, {}, 12);
        CHECK(bp->Alive());
        CHECK(bp->velocity.x < 0.0f);                       // bounced back
        Step(s, {}, 40);
        CHECK_EQ(CountNamed(s, "Bouncer"), 0);              // lifetime ran out

        GameObject* crate = Box(s, "Crate", {3, 0, 5}, {1, 1, 1}, Rigidbody3D::BodyType::Dynamic);
        crate->GetComponent<Rigidbody3D>()->gravityScale = 0.0f;
        GameObject* k = s.CreateGameObject("Kick");
        k->transform->SetPosition({0, 0, 5});
        auto* kp = k->AddComponent<Projectile3D>();
        kp->velocity = {30, 0, 0}; kp->impactForce = 6.0f;
        Step(s, {}, 10);
        CHECK(crate->GetComponent<Rigidbody3D>()->velocity.x > 1.0f);
    }

    // ================= 2.5D platformer =================
    {   // Run along X; layers along Z; W / S switch, blocked when there's no room.
        Scene s("p25");
        Ground(s);
        GameObject* p = Body(s, "Player", {0, 0.02f, 0});
        auto* pc = p->AddComponent<Platformer25DController>();
        s.Start();
        CHECK_EQ(pc->layer, 0);
        CHECK_NEAR(pc->LayerZ(0), 0.0f, 1e-4f);
        CHECK_NEAR(pc->LayerZ(2), -3.0f, 1e-4f);
        CHECK_NEAR(pc->MiddleZ(), -1.5f, 1e-4f);
        Step(s, {}, 30);
        CHECK(pc->IsGrounded());
        Step(s, {'d'}, 40);
        CHECK(p->transform->Position().x > 2.0f);
        CHECK(pc->FacingRight());
        CHECK_NEAR(p->transform->Position().z, 0.0f, 0.05f);

        Step(s, {'w'}, 1); Step(s, {}, 40);                 // hop back to the middle layer
        CHECK_EQ(pc->layer, 1);
        CHECK_NEAR(p->transform->Position().z, -1.5f, 0.1f);
        // A wall on the back layer right where we stand: W does nothing.
        float x = p->transform->Position().x;
        Box(s, "Back Wall", {x, 1.0f, -3.0f}, {1.5f, 2.0f, 1.0f});
        CHECK(!pc->LayerFree(2));
        Step(s, {'w'}, 1); Step(s, {}, 20);
        CHECK_EQ(pc->layer, 1);
        CHECK(pc->LayerFree(0));
        Step(s, {'s'}, 1); Step(s, {}, 40);                 // back to the front
        CHECK_EQ(pc->layer, 0);
        CHECK_NEAR(p->transform->Position().z, 0.0f, 0.1f);
        CHECK(!pc->SwitchLayer(-1));                        // no layer in front of the front
        CHECK(pc->SwitchLayer(1));
        CHECK_EQ(pc->layer, 1);
    }
    {   // Jump: a held jump goes higher than a quick tap (variable height); double jump.
        auto peak = [](bool hold) {
            Scene s("p25j");
            Ground(s);
            GameObject* p = Body(s, "Player", {0, 0.02f, 0});
            p->AddComponent<Platformer25DController>();
            s.Start();
            Step(s, {}, 30);
            Step(s, {' '}, 1);
            float top = 0.0f;
            for (int i = 0; i < 90; ++i) {
                Step(s, hold ? std::vector<char>{' '} : std::vector<char>{});
                top = std::max(top, p->transform->Position().y);
            }
            return top;
        };
        float tall = peak(true), hop = peak(false);
        CHECK(tall > 2.0f);
        CHECK(hop < tall * 0.75f);
        CHECK(hop > 0.3f);

        Scene s("p25dj");
        Ground(s);
        GameObject* p = Body(s, "Player", {0, 0.02f, 0});
        auto* pc = p->AddComponent<Platformer25DController>();
        pc->maxJumps = 2;
        auto* rb = p->GetComponent<Rigidbody3D>();
        s.Start();
        Step(s, {}, 30);
        Step(s, {' '}, 1);
        CHECK(rb->velocity.y > 5.0f);
        CHECK(!pc->IsGrounded());
        Step(s, {' '}, 10);
        float vy = rb->velocity.y;
        Step(s, {}, 1); Step(s, {' '}, 1);
        CHECK(rb->velocity.y > vy + 1.0f);                  // second jump
    }
    {   // Grab: drag a crate; hang from a static bar in the air.
        Scene s("p25g");
        Ground(s);
        GameObject* p = Body(s, "Player", {0, 0.02f, 0});
        auto* pc = p->AddComponent<Platformer25DController>();
        GameObject* crate = Box(s, "Crate", {1.1f, 0.5f, 0}, {1, 1, 1}, Rigidbody3D::BodyType::Dynamic);
        s.Start();
        Step(s, {}, 30);
        float cx0 = crate->transform->Position().x;
        Step(s, {'e'}, 2);
        CHECK(pc->IsGrabbing());
        CHECK(pc->Grabbed() == crate);
        Step(s, {'e', 'a'}, 50);                            // pull it left
        CHECK(pc->IsGrabbing());
        CHECK(crate->transform->Position().x < cx0 - 1.0f);
        CHECK(pc->FacingRight());                           // still facing the crate while pulling
        Step(s, {}, 2);
        CHECK(!pc->IsGrabbing());                           // let go

        Scene h("p25h");
        Ground(h);
        GameObject* q = Body(h, "Player", {0, 0.02f, 0});
        auto* qc = q->AddComponent<Platformer25DController>();
        Box(h, "Bar", {0.7f, 2.4f, 0}, {0.4f, 0.4f, 0.4f});
        h.Start();
        Step(h, {}, 30);
        Step(h, {' '}, 1);
        Step(h, {' '}, 12);                                 // rising, the bar is in front of the chest
        Step(h, {' ', 'e'}, 1);
        CHECK(qc->IsHanging());
        float y = q->transform->Position().y;
        Step(h, {'e'}, 30);
        CHECK_NEAR(q->transform->Position().y, y, 0.1f);    // stays put while hanging
        Step(h, {}, 1);
        CHECK(!qc->IsHanging());
        CHECK_NEAR(q->GetComponent<Rigidbody3D>()->gravityScale, 1.0f, 1e-4f);   // gravity restored
    }

    // ================= Saving =================
    {
        Scene s("ser");
        auto* a = s.CreateGameObject("A");
        auto* ts = a->AddComponent<TopDownShooterController>();
        ts->walkSpeed = 7.5f; ts->fireMode = 2; ts->pellets = 6; ts->spread = 22.0f; ts->magazineSize = 12;
        ts->reloadKey = 't'; ts->dashKey = 0; ts->dashGamepadButton = 2; ts->bulletColor = Color{0.25f, 0.5f, 0.75f, 1.0f};
        ts->projectile = "Prefabs/My Rocket.okayprefab"; ts->aimMode = 1; ts->wheelZoom = false;
        auto* b = s.CreateGameObject("B");
        auto* pc = b->AddComponent<Platformer25DController>();
        pc->layerCount = 4; pc->layerSpacing = 2.25f; pc->frontLayerZ = -3.0f; pc->layer = 2;
        pc->grabKey = 'f'; pc->maxJumps = 2; pc->jumpCut = 0.3f; pc->cameraFollowsLayer = true;
        auto* c = s.CreateGameObject("C");
        auto* pj = c->AddComponent<Projectile3D>();
        pj->velocity = {1, 2, 3}; pj->damage = 33; pj->pierce = 2; pj->bounce = true; pj->gravityScale = 1.0f;

        std::string text = SceneSerializer::Serialize(s);
        Scene s2("ser2");
        CHECK(SceneSerializer::Deserialize(s2, text));
        auto* ts2 = s2.Find("A") ? s2.Find("A")->GetComponent<TopDownShooterController>() : nullptr;
        auto* pc2 = s2.Find("B") ? s2.Find("B")->GetComponent<Platformer25DController>() : nullptr;
        auto* pj2 = s2.Find("C") ? s2.Find("C")->GetComponent<Projectile3D>() : nullptr;
        CHECK(ts2 && pc2 && pj2);
        if (ts2) {
            CHECK_NEAR(ts2->walkSpeed, 7.5f, 1e-4f); CHECK_EQ(ts2->fireMode, 2); CHECK_EQ(ts2->pellets, 6);
            CHECK_NEAR(ts2->spread, 22.0f, 1e-4f); CHECK_EQ(ts2->magazineSize, 12); CHECK(ts2->reloadKey == 't');
            CHECK(ts2->dashKey == 0); CHECK_EQ(ts2->dashGamepadButton, 2); CHECK_NEAR(ts2->bulletColor.b, 0.75f, 1e-4f);
            CHECK(ts2->projectile == "Prefabs/My Rocket.okayprefab"); CHECK_EQ(ts2->aimMode, 1);
            CHECK(!ts2->wheelZoom);
        }
        if (pc2) {
            CHECK_EQ(pc2->layerCount, 4); CHECK_NEAR(pc2->layerSpacing, 2.25f, 1e-4f);
            CHECK_NEAR(pc2->frontLayerZ, -3.0f, 1e-4f); CHECK_EQ(pc2->layer, 2); CHECK(pc2->grabKey == 'f');
            CHECK_EQ(pc2->maxJumps, 2); CHECK_NEAR(pc2->jumpCut, 0.3f, 1e-4f); CHECK(pc2->cameraFollowsLayer);
        }
        if (pj2) {
            CHECK_NEAR(pj2->velocity.z, 3.0f, 1e-4f); CHECK_NEAR(pj2->damage, 33.0f, 1e-4f);
            CHECK_EQ(pj2->pierce, 2); CHECK(pj2->bounce); CHECK_NEAR(pj2->gravityScale, 1.0f, 1e-4f);
        }
        // Unknown keys from a newer build are skipped, not fatal.
        std::string future = text;
        auto at = future.find("  p25ctrl");
        CHECK(at != std::string::npos);
        if (at != std::string::npos) future.insert(at + 9, " someNewThing=42 quoted=\"a b\"");
        Scene s3("ser3");
        CHECK(SceneSerializer::Deserialize(s3, future));
        auto* pc3 = s3.Find("B") ? s3.Find("B")->GetComponent<Platformer25DController>() : nullptr;
        CHECK(pc3 && pc3->layerCount == 4 && pc3->grabKey == 'f');
    }

    // ================= Templates =================
    {
        Scene s("tpl1");
        Templates::TopDownShooter(s);
        GameObject* p = s.Find("Player");
        CHECK(p && p->GetComponent<TopDownShooterController>() && p->GetComponent<HealthStat>());
        CHECK(s.Find("Target Dummy") != nullptr);
        s.Start();
        Step(s, {}, 10);
        CHECK(s.mainCamera != nullptr);

        Scene t("tpl2");
        Templates::Platformer25D(t);
        GameObject* q = t.Find("Player");
        auto* pc = q ? q->GetComponent<Platformer25DController>() : nullptr;
        CHECK(pc != nullptr);
        t.Start();
        Step(t, {}, 40);
        CHECK(pc && pc->IsGrounded());
        // The front wall blocks layer 0 but the middle layer lets you walk past it.
        Step(t, {'d'}, 120);
        CHECK(q->transform->Position().x < 6.0f);
        Step(t, {'w'}, 1); Step(t, {}, 20);
        Step(t, {'d'}, 60);
        CHECK(q->transform->Position().x > 6.5f);
    }

    Input::FeedKeys({});
    Input::FeedMouse({0, 0}, 0);
    TEST_MAIN_RESULT();
}
