// Raycast / collision / trigger improvements: RaycastAll ordering, layer masks,
// ignore-triggers, sphere / box / capsule casts, 3D OverlapBox / OverlapCapsule,
// trigger Stay + tag filter, contact point / normal / impact speed, continuous
// collision (no tunnelling), debug recording, serialization, and the new
// visual-script / OkayScript hooks.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/VisualScript/Nodes.hpp"
#include <cmath>
#include <cstdio>
#include <string>

using namespace okay;

namespace {

// Records the physics callbacks it receives.
struct Probe : Component {
    int enter3 = 0, stay3 = 0, exit3 = 0, enter2 = 0, stay2 = 0;
    int collide3 = 0, collide2 = 0;
    bool sawTriggerInfo = false;
    Vec3 triggerPoint{0, 0, 0};
    Collision3D first3; Collision2D first2;
    void OnTriggerEnter3D(Collider3D*) override {
        ++enter3;
        if (const TriggerContact3D* t = Physics3D::CurrentTrigger()) { sawTriggerInfo = true; triggerPoint = t->point; }
    }
    void OnTriggerStay3D(Collider3D*) override { ++stay3; }
    void OnTriggerExit3D(Collider3D*) override { ++exit3; }
    void OnTriggerEnter2D(Collider2D*) override { ++enter2; }
    void OnTriggerStay2D(Collider2D*) override { ++stay2; }
    void OnCollisionEnter3D(const Collision3D& c) override { if (collide3++ == 0) first3 = c; }
    void OnCollisionEnter2D(const Collision2D& c) override { if (collide2++ == 0) first2 = c; }
};

GameObject* Box3(Scene& s, const char* name, const Vec3& pos, const Vec3& size, int layer = 0, bool trigger = false) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = pos;
    auto* b = g->AddComponent<BoxCollider3D>();
    b->size = size; b->layer = layer; b->isTrigger = trigger;
    return g;
}

} // namespace

int main() {
    RUN_SUITE("physics_queries_ext");

    // ---- RaycastAll ordering, layer masks, ignore triggers -------------------
    {
        Scene s("Q3");
        s.physicsEnabled = false;
        GameObject* far  = Box3(s, "Far",  {8, 0, 0}, {1, 1, 1});
        GameObject* mid  = Box3(s, "Mid",  {5, 0, 0}, {1, 1, 1}, 3);           // layer 3
        GameObject* near = Box3(s, "Near", {2, 0, 0}, {1, 1, 1}, 0, true);     // a trigger
        s.Start();
        Physics3D& p = s.physics3D();

        auto all = p.RaycastAll(s, {0, 0, 0}, {1, 0, 0}, 100.0f);
        CHECK(all.size() == 3);
        if (all.size() == 3) {
            CHECK(all[0].gameObject == near);
            CHECK(all[1].gameObject == mid);
            CHECK(all[2].gameObject == far);
            CHECK_NEAR(all[0].distance, 1.5f, 1e-3f);
            CHECK_NEAR(all[1].distance, 4.5f, 1e-3f);
            CHECK_NEAR(all[2].distance, 7.5f, 1e-3f);
            CHECK(all[0].distance <= all[1].distance && all[1].distance <= all[2].distance);
        }
        // Max distance limits RaycastAll too.
        CHECK(p.RaycastAll(s, {0, 0, 0}, {1, 0, 0}, 5.0f).size() == 2);

        // Default raycast still sees the trigger first (unchanged behaviour)...
        CHECK(p.Raycast(s, {0, 0, 0}, {1, 0, 0}, 100.0f).gameObject == near);
        // ...ignore triggers skips it...
        RaycastHit3D noTrig = p.Raycast(s, {0, 0, 0}, {1, 0, 0}, 100.0f, nullptr, Physics3D::kAllLayers, false);
        CHECK(noTrig.gameObject == mid);
        // ...and a layer mask without layer 3 skips "Mid" too.
        RaycastHit3D layer0 = p.Raycast(s, {0, 0, 0}, {1, 0, 0}, 100.0f, nullptr, 1u << 0, false);
        CHECK(layer0.gameObject == far);
        // Only layer 3.
        CHECK(p.Raycast(s, {0, 0, 0}, {1, 0, 0}, 100.0f, nullptr, 1u << 3).gameObject == mid);
        CHECK(p.RaycastAll(s, {0, 0, 0}, {1, 0, 0}, 100.0f, nullptr, Physics3D::kAllLayers, false).size() == 2);
        // Ignore object.
        CHECK(p.Raycast(s, {0, 0, 0}, {1, 0, 0}, 100.0f, near).gameObject == mid);
        // Friendly layer lists.
        CHECK(Layers::MaskFromList("") == 0xFFFFFFFFu);
        CHECK(Layers::MaskFromList("0, 3") == ((1u << 0) | (1u << 3)));
        CHECK(Layers::MaskFromList("Default") == 1u);
        CHECK(Layers::MaskFromList("!3") == ~(1u << 3));

        // ---- Sphere / box / capsule casts ----
        RaycastHit3D sc = p.SphereCast(s, {0, 0, 0}, {1, 0, 0}, 0.5f, 100.0f);   // skips the trigger by default
        CHECK(sc.hit && sc.gameObject == mid);
        CHECK_NEAR(sc.distance, 4.0f, 0.02f);              // centre stops 0.5 before the face at 4.5
        CHECK_NEAR(sc.point.x, 4.5f, 0.02f);
        CHECK(sc.normal.x < -0.9f);
        // A thick ray clips a box the thin ray misses.
        CHECK(!p.Raycast(s, {0, 0.9f, 0}, {1, 0, 0}, 100.0f, nullptr, Physics3D::kAllLayers, false).hit);
        CHECK(p.SphereCast(s, {0, 0.9f, 0}, {1, 0, 0}, 0.5f, 100.0f).hit);
        // Sphere cast with triggers counted.
        CHECK(p.SphereCast(s, {0, 0, 0}, {1, 0, 0}, 0.5f, 100.0f, nullptr, Physics3D::kAllLayers, true).gameObject == near);

        RaycastHit3D bc = p.BoxCast(s, {0, 0, 0}, {0.5f, 0.5f, 0.5f}, {1, 0, 0});
        CHECK(bc.hit && bc.gameObject == mid);
        CHECK_NEAR(bc.distance, 4.0f, 0.02f);
        CHECK(bc.normal.x < -0.9f);
        // A box cast passing beside everything misses.
        CHECK(!p.BoxCast(s, {0, 3, 0}, {0.5f, 0.5f, 0.5f}, {1, 0, 0}, Quat::Identity, 100.0f).hit);

        RaycastHit3D cc = p.CapsuleCast(s, {0, -0.5f, 0}, {0, 0.5f, 0}, 0.25f, {1, 0, 0}, 100.0f);
        CHECK(cc.hit && cc.gameObject == mid);
        CHECK_NEAR(cc.distance, 4.25f, 0.02f);
        // Layer mask on a cast.
        CHECK(p.SphereCast(s, {0, 0, 0}, {1, 0, 0}, 0.5f, 100.0f, nullptr, 1u).gameObject == far);

        // ---- OverlapBox / OverlapCapsule (3D) ----
        auto ob = p.OverlapBox(s, {5, 0, 0}, {0.6f, 0.6f, 0.6f});
        CHECK(ob.size() == 1 && ob[0]->gameObject == mid);
        CHECK(p.OverlapBox(s, {3.5f, 0, 0}, {0.4f, 0.4f, 0.4f}).empty());   // in the gap
        auto wide = p.OverlapBox(s, {5, 0, 0}, {4, 1, 1});
        CHECK(wide.size() == 3);
        CHECK(p.OverlapBox(s, {5, 0, 0}, {4, 1, 1}, Quat::Identity, Physics3D::kAllLayers, false).size() == 2);
        CHECK(p.OverlapBox(s, {5, 0, 0}, {4, 1, 1}, Quat::Identity, 1u << 3).size() == 1);
        // A thin box rotated 90 degrees about Y lies along X and so reaches the boxes.
        Quat yaw = Quat::Euler({0, 90, 0});
        CHECK(p.OverlapBox(s, {5, 0, 0}, {0.1f, 0.1f, 3.6f}, yaw).size() == 3);
        CHECK(p.OverlapBox(s, {5, 0, 0}, {3.6f, 0.1f, 0.1f}, yaw).size() == 1);
        auto ocap = p.OverlapCapsule(s, {2, 0, 0}, {5, 0, 0}, 0.2f);
        CHECK(ocap.size() == 2);
        CHECK(p.OverlapCapsule(s, {2, 3, 0}, {5, 3, 0}, 0.2f).empty());
        CHECK(p.OverlapSphere(s, {2, 0, 0}, 0.2f, Physics3D::kAllLayers, false).empty());   // only the trigger is there
    }

    // ---- Ray vs capsule is exact (not the capsule's bounding box) -----------
    {
        Scene s("Cap");
        s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Pill");
        auto* cap = g->AddComponent<CapsuleCollider3D>();
        cap->radius = 0.5f; cap->height = 2.0f;
        s.Start();
        RaycastHit3D h = s.physics3D().Raycast(s, {-5, 0, 0}, {1, 0, 0});
        CHECK(h.hit); CHECK_NEAR(h.distance, 4.5f, 1e-3f);
        // Through the empty corner of its bounding box: misses the rounded cap.
        CHECK(!s.physics3D().Raycast(s, {-5, 0.95f, 0.45f}, {1, 0, 0}).hit);
    }

    // ---- 2D RaycastAll / masks ----
    {
        Scene s("Q2");
        s.physicsEnabled = false;
        for (int i = 0; i < 3; ++i) {
            GameObject* g = s.CreateGameObject("B" + std::to_string(i));
            g->transform->localPosition = {2.0f + 3.0f * (float)(2 - i), 0, 0};   // created far -> near
            auto* b = g->AddComponent<BoxCollider2D>();
            b->size = {1, 1}; b->layer = i; b->isTrigger = (i == 2);
        }
        s.Start();
        auto all = s.physics().RaycastAll(s, {0, 0}, {1, 0}, 100.0f);
        CHECK(all.size() == 3);
        if (all.size() == 3) {
            CHECK(all[0].distance < all[1].distance && all[1].distance < all[2].distance);
            CHECK_NEAR(all[0].distance, 1.5f, 1e-3f);
        }
        CHECK(s.physics().Raycast(s, {0, 0}, {1, 0}, 100.0f, nullptr, Physics2D::kAllLayers, false).distance > 4.0f);
        CHECK(s.physics().Raycast(s, {0, 0}, {1, 0}, 100.0f, nullptr, 1u << 0).gameObject->name == "B0");
        RaycastHit2D cc = s.physics().CircleCast(s, {0, 0}, {1, 0}, 0.5f, 100.0f);
        CHECK(cc.hit); CHECK_NEAR(cc.distance, 4.0f, 0.02f);   // the trigger at 2 is skipped
    }

    // ---- Trigger Stay (3D + 2D) and trigger info ----------------------------
    {
        Scene s("TS");
        s.physics3D().gravity = {0, 0, 0};
        s.physics().gravity = {0, 0};
        GameObject* zone = Box3(s, "Zone", {0, 0, 0}, {4, 4, 4}, 0, true);
        GameObject* hero = s.CreateGameObject("Hero");
        hero->AddComponent<SphereCollider3D>()->radius = 0.5f;
        hero->AddComponent<Rigidbody3D>();
        auto* zp = zone->AddComponent<Probe>();
        auto* hp = hero->AddComponent<Probe>();

        GameObject* zone2 = s.CreateGameObject("Zone2");
        zone2->transform->localPosition = {50, 0, 0};
        auto* z2c = zone2->AddComponent<BoxCollider2D>(); z2c->size = {4, 4}; z2c->isTrigger = true;
        auto* z2p = zone2->AddComponent<Probe>();
        GameObject* hero2 = s.CreateGameObject("Hero2");
        hero2->transform->localPosition = {50, 0, 0};
        hero2->AddComponent<CircleCollider2D>()->radius = 0.5f;
        hero2->AddComponent<Rigidbody2D>();

        s.Start();
        for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);
        CHECK(zp->enter3 == 1);
        CHECK(hp->enter3 == 1);
        CHECK(zp->stay3 == 4);          // every step after the first
        CHECK(hp->stay3 == 4);
        CHECK(zp->sawTriggerInfo);      // "where" is available during the callback
        CHECK(Physics3D::CurrentTrigger() == nullptr);   // ...and only then
        CHECK(z2p->enter2 == 1);
        CHECK(z2p->stay2 == 4);
        hero->transform->localPosition = {20, 0, 0};
        s.Update(1.0f / 60.0f);
        CHECK(zp->exit3 == 1);
    }

    // ---- Trigger tag / layer filter -----------------------------------------
    {
        Scene s("TF");
        s.physics3D().gravity = {0, 0, 0};
        GameObject* pickup = Box3(s, "Pickup", {0, 0, 0}, {2, 2, 2}, 0, true);
        pickup->GetComponent<BoxCollider3D>()->triggerTag = "Player";
        auto* pp = pickup->AddComponent<Probe>();
        GameObject* enemy = s.CreateGameObject("Enemy");
        enemy->tag = "Enemy";
        enemy->AddComponent<SphereCollider3D>();
        auto* ep = enemy->AddComponent<Probe>();
        s.Start();
        for (int i = 0; i < 3; ++i) s.Update(1.0f / 60.0f);
        CHECK(pp->enter3 == 0);         // wrong tag: no events on either side
        CHECK(ep->enter3 == 0);
        enemy->tag = "Player";
        for (int i = 0; i < 2; ++i) s.Update(1.0f / 60.0f);
        CHECK(pp->enter3 == 1);
        CHECK(ep->enter3 == 1);
        // Layer filter: only layer 4 sets it off.
        pickup->GetComponent<BoxCollider3D>()->triggerTag.clear();
        pickup->GetComponent<BoxCollider3D>()->triggerLayers = 1u << 4;
        s.Update(1.0f / 60.0f);
        CHECK(pp->exit3 == 1);          // the (layer 0) sphere no longer counts
        enemy->GetComponent<SphereCollider3D>()->layer = 4;
        s.Update(1.0f / 60.0f);
        CHECK(pp->enter3 == 2);
    }

    // ---- Contact point, normal and impact speed -----------------------------
    {
        Scene s("CP");
        GameObject* floor = Box3(s, "Floor", {0, 0, 0}, {10, 1, 10});   // top at y = 0.5
        (void)floor;
        GameObject* ball = s.CreateGameObject("Ball");
        ball->transform->localPosition = {0, 1.2f, 0};
        ball->AddComponent<SphereCollider3D>()->radius = 0.5f;
        auto* rb = ball->AddComponent<Rigidbody3D>();
        rb->velocity = {0, -6, 0};
        auto* bp = ball->AddComponent<Probe>();
        s.Start();
        for (int i = 0; i < 30 && bp->collide3 == 0; ++i) s.Update(1.0f / 60.0f);
        CHECK(bp->collide3 > 0);
        CHECK(bp->first3.gameObject == floor);
        CHECK_NEAR(bp->first3.point.y, 0.5f, 0.05f);          // on the floor's top face
        CHECK_NEAR(bp->first3.point.x, 0.0f, 0.05f);
        CHECK(bp->first3.normal.y < -0.95f);                  // from the ball toward the floor
        CHECK(bp->first3.impactSpeed > 5.0f);                 // it was falling at ~6 m/s
        CHECK(bp->first3.relativeVelocity.y > 5.0f);          // floor minus ball velocity

        // 2D: a circle dropped on a box.
        GameObject* f2 = s.CreateGameObject("Floor2");
        f2->transform->localPosition = {100, 0, 0};
        f2->AddComponent<BoxCollider2D>()->size = {10, 1};
        GameObject* b2 = s.CreateGameObject("Ball2");
        b2->transform->localPosition = {100, 1.2f, 0};
        b2->AddComponent<CircleCollider2D>()->radius = 0.5f;
        b2->AddComponent<Rigidbody2D>()->velocity = {0, -6};
        auto* p2 = b2->AddComponent<Probe>();
        for (int i = 0; i < 30 && p2->collide2 == 0; ++i) s.Update(1.0f / 60.0f);
        CHECK(p2->collide2 > 0);
        CHECK_NEAR(p2->first2.point.y, 0.5f, 0.05f);
        CHECK(p2->first2.normal.y < -0.95f);
        CHECK(p2->first2.impactSpeed > 5.0f);
    }

    // ---- Continuous collision stops tunnelling ------------------------------
    for (int ccd = 0; ccd < 2; ++ccd) {
        Scene s("CCD");
        s.physics3D().gravity = {0, 0, 0};
        s.physics().gravity = {0, 0};
        Box3(s, "Wall", {5, 0, 0}, {0.1f, 4, 4});
        GameObject* bullet = s.CreateGameObject("Bullet");
        bullet->AddComponent<SphereCollider3D>()->radius = 0.25f;
        auto* rb = bullet->AddComponent<Rigidbody3D>();
        rb->velocity = {600, 0, 0};          // 10 units per 60 Hz step
        rb->continuousCollision = (ccd == 1);

        GameObject* wall2 = s.CreateGameObject("Wall2");
        wall2->transform->localPosition = {5, 100, 0};
        wall2->AddComponent<BoxCollider2D>()->size = {0.1f, 4};
        GameObject* b2 = s.CreateGameObject("Bullet2");
        b2->transform->localPosition = {0, 100, 0};
        b2->AddComponent<CircleCollider2D>()->radius = 0.25f;
        auto* rb2 = b2->AddComponent<Rigidbody2D>();
        rb2->velocity = {600, 0};
        rb2->continuousCollision = (ccd == 1);

        s.Start();
        for (int i = 0; i < 4; ++i) s.Update(1.0f / 60.0f);
        float x3 = bullet->transform->Position().x, x2 = b2->transform->Position().x;
        if (ccd == 0) {   // discrete: it jumps straight past the thin wall
            CHECK(x3 > 6.0f);
            CHECK(x2 > 6.0f);
        } else {          // continuous: stopped at (or bounced back from) the wall
            CHECK(x3 < 5.0f);
            CHECK(x2 < 5.0f);
            CHECK(rb->velocity.x <= 1e-3f);
        }
    }

    // ---- Debug recording is off by default and cheap ------------------------
    {
        Scene s("DBG");
        Box3(s, "Floor", {0, 0, 0}, {10, 1, 10});
        GameObject* ball = s.CreateGameObject("Ball");
        ball->transform->localPosition = {0, 0.9f, 0};
        ball->AddComponent<SphereCollider3D>()->radius = 0.5f;
        ball->AddComponent<Rigidbody3D>();
        s.Start();
        s.Update(1.0f / 60.0f);
        CHECK(s.physics3D().DebugContacts().empty());
        PhysicsDebug::Collect() = true;
        s.Update(1.0f / 60.0f);
        CHECK(!s.physics3D().DebugContacts().empty());
        RaycastHit3D h = s.physics3D().Raycast(s, {0, 5, 0}, {0, -1, 0}, 10.0f);
        s.physics3D().RecordQueryRay({0, 5, 0}, {0, -1, 0}, 10.0f, h);
        CHECK(s.physics3D().DebugRays().size() == 1);
        PhysicsDebug::Collect() = false;
        s.Update(1.0f / 60.0f);
        CHECK(s.physics3D().DebugRays().empty());
        CHECK(s.physics3D().DebugContacts().empty());
    }

    // ---- Serialization: CCD flag and trigger filters round-trip -------------
    {
        Scene a("SER");
        GameObject* g = a.CreateGameObject("Thing");
        auto* rb = g->AddComponent<Rigidbody3D>(); rb->continuousCollision = true;
        auto* bc = g->AddComponent<BoxCollider3D>(); bc->isTrigger = true; bc->triggerTag = "Player Two"; bc->triggerLayers = 0x9u;
        GameObject* g2 = a.CreateGameObject("Thing2");
        g2->AddComponent<Rigidbody2D>()->continuousCollision = true;
        auto* c2 = g2->AddComponent<CircleCollider2D>(); c2->isTrigger = true; c2->triggerTag = "Coin";
        GameObject* plain = a.CreateGameObject("Plain");
        plain->AddComponent<SphereCollider3D>();
        std::string text = SceneSerializer::Serialize(a);
        Scene b("SER2");
        CHECK(SceneSerializer::Deserialize(b, text));
        GameObject* t = b.Find("Thing");
        CHECK(t && t->GetComponent<Rigidbody3D>() && t->GetComponent<Rigidbody3D>()->continuousCollision);
        CHECK(t && t->GetComponent<BoxCollider3D>() && t->GetComponent<BoxCollider3D>()->triggerTag == "Player Two");
        CHECK(t && t->GetComponent<BoxCollider3D>() && t->GetComponent<BoxCollider3D>()->triggerLayers == 0x9u);
        GameObject* t2 = b.Find("Thing2");
        CHECK(t2 && t2->GetComponent<Rigidbody2D>()->continuousCollision);
        CHECK(t2 && t2->GetComponent<CircleCollider2D>()->triggerTag == "Coin");
        GameObject* p = b.Find("Plain");
        CHECK(p && p->GetComponent<SphereCollider3D>()->triggerTag.empty());
        CHECK(p && p->GetComponent<SphereCollider3D>()->triggerLayers == 0xFFFFFFFFu);
        CHECK(text.find("trigfilter3d") != std::string::npos);
        // An old-style record (no trailing CCD flag) still loads with CCD off.
        Scene c("OLD");
        CHECK(SceneSerializer::Deserialize(c, "okayscene 1\ngameobject 0 \"Old\"\n"
                                              "  rigidbody3d 0 1 1 0 0 0 0 0 0 0.5 0.05 1 1\n"
                                              "  spherecollider3d 0.5 0 0 0 1 0 0\n"));
        GameObject* old = c.Find("Old");
        CHECK(old && old->GetComponent<Rigidbody3D>() && !old->GetComponent<Rigidbody3D>()->continuousCollision);
        CHECK(old && old->GetComponent<SphereCollider3D>() && old->GetComponent<SphereCollider3D>()->isTrigger);
        CHECK(old && old->GetComponent<SphereCollider3D>()->triggerTag.empty());
    }

    // ---- Visual scripts: Raycast 3D / Overlap nodes, On Trigger Stay, contact info
    {
        Scene s("VS");
        s.physics3D().gravity = {0, 0, 0};
        GameObject* self = s.CreateGameObject("Eye");
        self->AddComponent<SphereCollider3D>()->radius = 0.25f;   // ignored by its own casts
        Box3(s, "Target", {0, 0, 6}, {1, 1, 1});
        auto* vsc = self->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vsc->LoadFromText(
            "node 1 OnStart\n"
            "node 2 SetVar hit\nnode 3 Raycast3D\n"
            "node 4 SetVar what\n"
            "node 5 SetVar dist\n"
            "node 6 SetVar n\nnode 7 OverlapSphere3D\nnode 8 Const 20\n"
            "data 2 0 3 0\ndata 4 0 3 4\ndata 5 0 3 3\ndata 6 0 7 0\ndata 7 1 8 0\n"
            "exec 1 0 2\nexec 2 0 4\nexec 4 0 5\nexec 5 0 6\n"
            "entry OnStart 1\n"
            "node 10 OnTriggerStay\nnode 11 AddVar stays\nnode 12 Const 1\nexec 10 0 11\ndata 11 0 12 0\n"
            "entry OnTriggerStay 10\n"
            "node 20 OnCollision\nnode 21 SetVar cy\nnode 22 ContactPoint\nnode 23 VecY\n"
            "node 24 SetVar spd\nnode 25 ImpactSpeed\n"
            "data 23 0 22 0\ndata 21 0 23 0\ndata 24 0 25 0\nexec 20 0 21\nexec 21 0 24\n"
            "entry OnCollision 20\n", &err));
        CHECK(err.empty());
        s.Start();
        s.Update(1.0f / 60.0f);
        CHECK(vsc->GetVariable("hit").AsBool());                       // forward (+Z) hits the target
        CHECK(vsc->GetVariable("what").AsString() == "Target");
        CHECK_NEAR(vsc->GetVariable("dist").AsFloat(), 5.5f, 1e-3f);    // origin is the eye, which it skips
        CHECK_NEAR(vsc->GetVariable("n").AsFloat(), 1.0f, 1e-4f);       // the target, not itself

        // Contact info reaches the graph.
        Collision3D c; c.gameObject = self; c.point = {1, 2, 3}; c.impactSpeed = 7.0f;
        vsc->OnCollisionEnter3D(c);
        CHECK_NEAR(vsc->GetVariable("cy").AsFloat(), 2.0f, 1e-4f);
        CHECK_NEAR(vsc->GetVariable("spd").AsFloat(), 7.0f, 1e-4f);

        // On Trigger Stay runs every frame something stays inside.
        GameObject* zone = Box3(s, "Zone", {0, 0, 0}, {3, 3, 3}, 0, true);
        (void)zone;
        for (int i = 0; i < 4; ++i) s.Update(1.0f / 60.0f);
        CHECK(vsc->GetVariable("stays").AsFloat() >= 2.0f);

        for (const char* t : {"Raycast3D", "SphereCast3D", "RaycastAll3D", "OverlapSphere3D", "OverlapBox3D",
                              "ContactPoint", "ContactNormal", "ImpactSpeed", "OnTriggerStay"}) {
            std::string e;
            CHECK(vs::MakeNode(t, {}, &e) != nullptr);
        }
    }

    // ---- OkayScript: raycast_all3, layer masks, contact globals --------------
    {
        Scene s("OS");
        s.physicsEnabled = false;
        Box3(s, "A", {3, 0, 0}, {1, 1, 1});
        Box3(s, "B", {6, 0, 0}, {1, 1, 1}, 2);
        GameObject* go = s.CreateGameObject("Logic");
        go->transform->localPosition = {0, 50, 0};
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        sc->LoadSource(
            "var n = 0\n"
            "var nearest = \"\"\n"
            "var masked = \"\"\n"
            "var thick = \"\"\n"
            "var inside = 0\n"
            "var cy = 0\n"
            "var rc = \"\"\n"
            "var rd = 0\n"
            "var oc = 0\n"
            "function start() {\n"
            "  rc = ray3_cast(\"object\", vec3(0, 0, 0), vec3(1, 0, 0), 100)\n"   // what compiled graphs call
            "  rd = ray3_cast(\"distance\", vec3(0, 0, 0), vec3(1, 0, 0), 100, \"2\")\n"
            "  oc = count(overlap3(\"box\", vec3(4.5, 0, 0), vec3(2, 1, 1)))\n"
            "  n = count(raycast_all3(0, 0, 0, 1, 0, 0, 100))\n"
            "  nearest = ray_all_object(0)\n"
            "  masked = raycast3(0, 0, 0, 1, 0, 0, 100, \"2\")\n"
            "  thick = sphere_cast3(0, 0.9, 0, 1, 0, 0, 0.5, 100)\n"
            "  inside = count(overlap_box3(4.5, 0, 0, 2, 1, 1))\n"
            "}\n"
            "function on_collision_enter(other) { cy = contact_y }\n");
        s.Start();
        auto* vm = sc->VM();
        CHECK(vm != nullptr);
        if (vm) {
            CHECK_NEAR(vm->GetGlobal("n").AsFloat(), 2.0f, 1e-4f);
            CHECK(vm->GetGlobal("nearest").AsString() == "A");
            CHECK(vm->GetGlobal("masked").AsString() == "B");
            CHECK(vm->GetGlobal("thick").AsString() == "A");
            CHECK_NEAR(vm->GetGlobal("inside").AsFloat(), 2.0f, 1e-4f);
            CHECK(vm->GetGlobal("rc").AsString() == "A");
            CHECK_NEAR(vm->GetGlobal("rd").AsFloat(), 5.5f, 1e-3f);
            CHECK_NEAR(vm->GetGlobal("oc").AsFloat(), 2.0f, 1e-4f);
            Collision3D c; c.gameObject = s.Find("A"); c.point = {0, 4, 0};
            sc->OnCollisionEnter3D(c);
            CHECK_NEAR(vm->GetGlobal("cy").AsFloat(), 4.0f, 1e-4f);
        }
    }

    TEST_MAIN_RESULT();
}
