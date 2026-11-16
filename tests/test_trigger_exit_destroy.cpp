// Destroying (or removing the collider of) one side of a trigger overlap must end it with an
// OnTriggerExit, the same as walking out or switching the object off does (#888 finding 56 and
// its addendum). Before the fix the pair was dropped silently: a SurvivalZone destroyed with the
// player inside kept the player "in radiation" for good, and a room trigger kept counting an
// enemy that was destroyed inside it.
#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

namespace {

// Counters live outside the components, so a destroyed object's count can still be read.
struct Counts { int enter = 0, exit = 0; };

class Probe3D : public Behaviour {
public:
    Counts* n = nullptr;
    void OnTriggerEnter3D(Collider3D* o) override { if (n && o) ++n->enter; }
    void OnTriggerExit3D(Collider3D* o) override { if (n && o) ++n->exit; }
};
class Probe2D : public Behaviour {
public:
    Counts* n = nullptr;
    void OnTriggerEnter2D(Collider2D* o) override { if (n && o) ++n->enter; }
    void OnTriggerExit2D(Collider2D* o) override { if (n && o) ++n->exit; }
};

const float kDt = 1.0f / 60.0f;
void Run(Scene& s, int frames) { for (int i = 0; i < frames; ++i) s.Update(kDt); }

// ---------------------------------------------------------------- 3D
GameObject* Zone3D(Scene& s, Counts* n) {
    GameObject* z = s.CreateGameObject("Zone");
    auto* c = z->AddComponent<BoxCollider3D>();
    c->size = {4, 4, 4};
    c->isTrigger = true;
    z->AddComponent<Probe3D>()->n = n;
    return z;
}
GameObject* Body3D(Scene& s, Counts* n, Vec3 at) {
    GameObject* b = s.CreateGameObject("Body");
    b->transform->localPosition = at;
    auto* rb = b->AddComponent<Rigidbody3D>();
    rb->bodyType = Rigidbody3D::BodyType::Kinematic;
    b->AddComponent<BoxCollider3D>()->size = {0.5f, 0.5f, 0.5f};
    b->AddComponent<Probe3D>()->n = n;
    return b;
}

void Test3D(PhysicsQuality q) {
    {   // the zone is destroyed with A inside: A gets exactly one exit, and so does the zone
        Scene s("z3a"); s.physics3D().quality = q;
        Counts zn, an;
        GameObject* z = Zone3D(s, &zn);
        Body3D(s, &an, {0, 0, 0});
        s.Start(); Run(s, 5);
        CHECK(an.enter == 1 && zn.enter == 1);
        s.Destroy(z); Run(s, 5);
        CHECK(an.exit == 1);
        CHECK(zn.exit == 1);
    }
    {   // A is destroyed inside: the zone gets one exit
        Scene s("z3b"); s.physics3D().quality = q;
        Counts zn, an;
        Zone3D(s, &zn);
        GameObject* a = Body3D(s, &an, {0, 0, 0});
        s.Start(); Run(s, 5);
        s.Destroy(a); Run(s, 5);
        CHECK(zn.exit == 1);
        CHECK(an.exit == 1);
    }
    {   // both destroyed in the same frame: one exit each, nothing touched after it is freed
        Scene s("z3c"); s.physics3D().quality = q;
        Counts zn, an;
        GameObject* z = Zone3D(s, &zn);
        GameObject* a = Body3D(s, &an, {0, 0, 0});
        s.Start(); Run(s, 5);
        s.Destroy(a); s.Destroy(z); Run(s, 5);
        CHECK(zn.exit == 1);
        CHECK(an.exit == 1);
    }
    {   // A's collider is switched off: the zone gets one exit
        Scene s("z3d"); s.physics3D().quality = q;
        Counts zn, an;
        Zone3D(s, &zn);
        GameObject* a = Body3D(s, &an, {0, 0, 0});
        s.Start(); Run(s, 5);
        a->GetComponent<BoxCollider3D>()->enabled = false; Run(s, 5);
        CHECK(zn.exit == 1);
    }
    {   // the zone's collider component is removed (not the object): both sides get one exit
        Scene s("z3e"); s.physics3D().quality = q;
        Counts zn, an;
        GameObject* z = Zone3D(s, &zn);
        Body3D(s, &an, {0, 0, 0});
        s.Start(); Run(s, 5);
        z->RemoveComponent<BoxCollider3D>(); Run(s, 5);
        CHECK(an.exit == 1);
        CHECK(zn.exit == 1);
    }
    {   // A is destroyed in the frame it enters (addendum B): its enter is matched by an exit
        Scene s("z3f"); s.physics3D().quality = q;
        Counts zn, an;
        Zone3D(s, &zn);
        s.Start(); Run(s, 2);
        GameObject* a = Body3D(s, &an, {0, 0, 0});
        s.Destroy(a); Run(s, 5);
        CHECK(zn.enter == zn.exit);
        CHECK(an.enter == an.exit);
    }
    {   // a radiation SurvivalZone destroyed with the player inside: the radiation stops
        Scene s("z3g"); s.physics3D().quality = q;
        GameObject* zone = s.CreateGameObject("RadZone");
        auto* zc = zone->AddComponent<BoxCollider3D>();
        zc->size = {4, 4, 4}; zc->isTrigger = true;
        zone->AddComponent<SurvivalZone>()->effect = (int)SurvivalZone::Effect::Radiation;
        Counts pn;
        GameObject* player = Body3D(s, &pn, {0, 0, 0});
        auto* rad = player->AddComponent<RadiationStat>();
        s.Start(); Run(s, 5);
        CHECK(rad->inRadiation);
        s.Destroy(zone); Run(s, 3);
        CHECK(!rad->inRadiation);
        const float r0 = rad->radiation;
        Run(s, 60);
        CHECK(rad->radiation <= r0);   // decays, never grows again
    }
}

// ---------------------------------------------------------------- 2D
GameObject* Zone2D(Scene& s, Counts* n) {
    GameObject* z = s.CreateGameObject("Zone");
    auto* c = z->AddComponent<BoxCollider2D>();
    c->size = {4, 4};
    c->isTrigger = true;
    z->AddComponent<Probe2D>()->n = n;
    return z;
}
GameObject* Body2D(Scene& s, Counts* n, Vec3 at) {
    GameObject* b = s.CreateGameObject("Body");
    b->transform->localPosition = at;
    auto* rb = b->AddComponent<Rigidbody2D>();
    rb->bodyType = Rigidbody2D::BodyType::Kinematic;
    b->AddComponent<BoxCollider2D>()->size = {0.5f, 0.5f};
    b->AddComponent<Probe2D>()->n = n;
    return b;
}

void Test2D(PhysicsQuality q) {
    {
        Scene s("z2a"); s.physics().quality = q;
        Counts zn, an;
        GameObject* z = Zone2D(s, &zn);
        Body2D(s, &an, {0, 0, 0});
        s.Start(); Run(s, 5);
        CHECK(an.enter == 1 && zn.enter == 1);
        s.Destroy(z); Run(s, 5);
        CHECK(an.exit == 1);
        CHECK(zn.exit == 1);
    }
    {
        Scene s("z2b"); s.physics().quality = q;
        Counts zn, an;
        Zone2D(s, &zn);
        GameObject* a = Body2D(s, &an, {0, 0, 0});
        s.Start(); Run(s, 5);
        s.Destroy(a); Run(s, 5);
        CHECK(zn.exit == 1);
        CHECK(an.exit == 1);
    }
    {
        Scene s("z2c"); s.physics().quality = q;
        Counts zn, an;
        GameObject* z = Zone2D(s, &zn);
        GameObject* a = Body2D(s, &an, {0, 0, 0});
        s.Start(); Run(s, 5);
        s.Destroy(z); s.Destroy(a); Run(s, 5);
        CHECK(zn.exit == 1);
        CHECK(an.exit == 1);
    }
    {
        Scene s("z2d"); s.physics().quality = q;
        Counts zn, an;
        Zone2D(s, &zn);
        GameObject* a = Body2D(s, &an, {0, 0, 0});
        s.Start(); Run(s, 5);
        a->GetComponent<BoxCollider2D>()->enabled = false; Run(s, 5);
        CHECK(zn.exit == 1);
    }
    {
        Scene s("z2e"); s.physics().quality = q;
        Counts zn, an;
        GameObject* z = Zone2D(s, &zn);
        Body2D(s, &an, {0, 0, 0});
        s.Start(); Run(s, 5);
        z->RemoveComponent<BoxCollider2D>(); Run(s, 5);
        CHECK(an.exit == 1);
        CHECK(zn.exit == 1);
    }
    {
        Scene s("z2f"); s.physics().quality = q;
        Counts zn, an;
        Zone2D(s, &zn);
        s.Start(); Run(s, 2);
        GameObject* a = Body2D(s, &an, {0, 0, 0});
        s.Destroy(a); Run(s, 5);
        CHECK(zn.enter == zn.exit);
        CHECK(an.enter == an.exit);
    }
    {
        Scene s("z2g"); s.physics().quality = q;
        GameObject* zone = s.CreateGameObject("RadZone");
        auto* zc = zone->AddComponent<BoxCollider2D>();
        zc->size = {4, 4}; zc->isTrigger = true;
        zone->AddComponent<SurvivalZone>()->effect = (int)SurvivalZone::Effect::Radiation;
        Counts pn;
        GameObject* player = Body2D(s, &pn, {0, 0, 0});
        auto* rad = player->AddComponent<RadiationStat>();
        s.Start(); Run(s, 5);
        CHECK(rad->inRadiation);
        s.Destroy(zone); Run(s, 3);
        CHECK(!rad->inRadiation);
        const float r0 = rad->radiation;
        Run(s, 60);
        CHECK(rad->radiation <= r0);
    }
}

// An exit handler that destroys more objects (the zone it left, or a third body) must not break
// the clean-up: everything still gets one exit and nothing freed is touched.
class Chain3D : public Behaviour {
public:
    Counts* n = nullptr;
    GameObject* kill = nullptr;
    void OnTriggerExit3D(Collider3D* o) override {
        if (n && o) ++n->exit;
        if (kill) { GetScene()->Destroy(kill); kill = nullptr; }
    }
};

void TestChain() {
    Scene s("chain");
    Counts zn, an, bn, cn;
    GameObject* z = Zone3D(s, &zn);
    Body3D(s, &an, {0, 0, 0});
    GameObject* c = s.CreateGameObject("C");   // listed before B: its exit runs first and destroys B
    auto* cc = c->AddComponent<BoxCollider3D>(); cc->size = {0.5f, 0.5f, 0.5f};
    auto* ch = c->AddComponent<Chain3D>(); ch->n = &cn;
    c->transform->localPosition = {-1, 0, 0};
    GameObject* b = Body3D(s, &bn, {1, 0, 0});
    ch->kill = b;
    s.Start(); Run(s, 5);
    s.Destroy(z); Run(s, 5);
    CHECK(an.exit == 1);
    CHECK(bn.exit == 1);
    CHECK(cn.exit == 1);
    CHECK(zn.exit == 3);
}

} // namespace

int main() {
    RUN_SUITE("trigger exit on destroy");
    Test3D(PhysicsQuality::Classic);
    Test3D(PhysicsQuality::Improved);
    Test2D(PhysicsQuality::Classic);
    Test2D(PhysicsQuality::Improved);
    TestChain();
    TEST_MAIN_RESULT();
}
