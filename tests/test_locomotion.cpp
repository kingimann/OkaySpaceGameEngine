// Built-in Character locomotion: a stride laid out from the real ground speed
// (planted feet don't skate or sink), human joint directions (knees and elbows
// bend the right way), state picked from what the body really does, and the
// first/third-person prone + crouch body (shorter collider, headroom, gamepad).
#include "test_framework.hpp"
#include <Okay.hpp>

#define CHECK_EQ(a, b) CHECK((a) == (b))
#include <algorithm>
#include <cmath>
#include <vector>

using namespace okay;

namespace {
const float kDt = 1.0f / 60.0f;

// Move a lone Character along a direction at speed v and measure its feet: how far
// the lowest point of a planted foot slips per frame, and how deep a foot dips.
struct GaitResult { float medianSlip = 0, deepest = 0; int planted = 0; };
GaitResult RunGait(int state, float v, float angleDeg) {
    Scene s("gait");
    GameObject* go = s.CreateGameObject("P");
    auto* ch = go->AddComponent<Character>();
    ch->Apply();
    std::vector<int> bone; ch->BuildRig(bone);
    const int feet[2] = {Character::BoneIndex("l_foot"), Character::BoneIndex("r_foot")};
    auto* mr = go->GetComponent<MeshRenderer>();
    auto world = [&](std::size_t i) { return go->transform->TransformPoint(mr->mesh.vertices[i]); };
    auto lowest = [&](int b) {
        std::size_t best = 0; float lo = 1e9f;
        for (std::size_t i = 0; i < bone.size(); ++i)
            if (bone[i] == b) { float y = world(i).y; if (y < lo) { lo = y; best = i; } }
        return best;
    };
    const float dt = 1.0f / 120.0f, a = angleDeg * 3.14159265f / 180.0f;
    const Vec3 dir{std::sin(a), 0.0f, -std::cos(a)};          // body faces -Z, + angle = to its right (+X)
    std::size_t idx[2] = {0, 0}; Vec3 pos[2];
    std::vector<float> slips; GaitResult r;
    s.Start();
    for (int i = 0; i < 480; ++i) {
        ch->anim = state; ch->moveSpeed = v; ch->moveAngle = angleDeg;
        go->transform->localPosition = go->transform->localPosition + dir * (v * dt);
        s.Update(dt);
        if (mr->mesh.vertices.size() != bone.size()) continue;
        for (int k = 0; k < 2; ++k) {
            if (i > 240 && pos[k].y < 0.01f) {
                Vec3 w = world(idx[k]);
                slips.push_back(std::hypot(w.x - pos[k].x, w.z - pos[k].z) / dt);
            }
            idx[k] = lowest(feet[k]); pos[k] = world(idx[k]);
            if (i > 240) r.deepest = std::min(r.deepest, pos[k].y);
        }
    }
    std::sort(slips.begin(), slips.end());
    r.planted = (int)slips.size();
    r.medianSlip = slips.empty() ? 99.0f : slips[slips.size() / 2];
    return r;
}

GameObject* Block(Scene& s, const char* n, Vec3 p, Vec3 size) {
    auto* o = s.CreateGameObject(n);
    o->transform->localPosition = p;
    o->AddComponent<BoxCollider3D>()->size = size;
    o->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    return o;
}
void Step(Scene& s, std::vector<char> keys, int n) {
    for (int i = 0; i < n; ++i) { Input::FeedKeys(keys); s.Update(kDt); }
}

// Prone / crouch body for either controller: crawl under a low bar, can't stand
// under it, stands up by itself once clear; gamepad tap/hold; allowProne off.
// Crouch / prone through a StanceControl: keys (where the controller has them) or
// SetStance, the collider eases down and back up, slower on the way, no jump.
template <class C>
void StanceOn(bool keys) {
    Scene s("stance");
    Block(s, "Ground", {0, -0.5f, 0}, {100, 1, 100});
    GameObject* p = Templates::BuildPlayerBody(s, {0, 0.02f, 0}, "Player");
    auto* c = p->AddComponent<C>();
    auto* box = p->GetComponent<BoxCollider3D>();
    StanceControl& sc = c->stanceControl;
    sc.allowProne = true;                                          // off by default on the top-down one
    s.Start();
    Step(s, {}, 30);
    if (keys) { sc.crouchKey = Input::KeyCtrl; Step(s, {Input::KeyCtrl}, 1); }
    else c->SetStance(1);
    Step(s, {}, 3);
    CHECK_EQ(sc.Stance(), 1);
    CHECK(box->size.y < 1.79f && box->size.y > 1.2f);              // on the way down, not snapped
    Step(s, {}, 30);
    CHECK_NEAR(box->size.y, 1.8f * sc.crouchHeight, 1e-3f);
    CHECK_NEAR(box->offset.y, box->size.y * 0.5f, 1e-3f);          // feet stay on the ground
    CHECK_NEAR(sc.Speed(4.0f), sc.crouchSpeed, 1e-4f);
    c->SetStance(2); Step(s, {}, 60);
    CHECK_NEAR(box->size.y, 1.8f * sc.proneHeight, 1e-3f);
    c->SetStance(0); Step(s, {}, 60);
    CHECK_EQ(sc.Stance(), 0);
    CHECK_NEAR(box->size.y, 1.8f, 1e-3f);
    sc.allowProne = false; c->SetStance(2); Step(s, {}, 5);
    CHECK_EQ(sc.Stance(), 0);
    Input::FeedKeys({});
}

template <class C>
void ProneBody(void (*tpl)(Scene&)) {
    Scene s("prone"); tpl(s);
    GameObject* p = s.Find("Player");
    CHECK(p != nullptr);
    if (!p) return;
    auto* c = p->GetComponent<C>();
    auto* box = p->GetComponent<BoxCollider3D>();
    auto* ch = p->GetComponent<Character>();
    const Vec3 start = p->transform->Position();
    Block(s, "Bar", {start.x, 1.25f, start.z - 3.0f}, {6.0f, 1.0f, 1.0f});   // 0.75 m gap underneath
    s.Start();
    Step(s, {}, 60);
    Step(s, {'w'}, 90);
    CHECK(p->transform->Position().z - start.z > -2.6f);          // standing: the bar stops you
    CHECK_NEAR(box->size.y, 1.8f, 1e-3f);
    Step(s, {'z'}, 1); Step(s, {}, 50);                            // the body gets down over ~0.75 s
    CHECK(c->stance() == C::Stance::Prone);
    CHECK(box->size.y < 0.7f);                                     // the body really is low
    CHECK_NEAR(box->offset.y, box->size.y * 0.5f, 1e-3f);          // feet stay on the ground
    CHECK_EQ(ch->anim, 7);
    Step(s, {'w'}, 150);
    CHECK(p->transform->Position().z - start.z < -4.0f);          // crawled under and out the far side
    CHECK(ch->moveSpeed > 0.5f);                                   // the crawl cycle follows the distance
    Step(s, {'s'}, 75); Step(s, {}, 10);                           // back under the bar
    Step(s, {'z'}, 1); Step(s, {}, 20);                            // ask to stand: no headroom
    CHECK(c->stance() == C::Stance::Prone);
    Step(s, {'s'}, 150); Step(s, {}, 30);                          // out from under it
    CHECK(c->stance() == C::Stance::Stand);                        // the remembered stand-up happens
    CHECK_NEAR(box->size.y, 1.8f, 1e-3f);
    // Gamepad: hold the stance button -> prone, tap -> stand, tap -> crouch.
    for (int i = 0; i < 40; ++i) { Input::FeedKeys({}); Input::FeedGamepad({0, 0}, 1u << 1); s.Update(kDt); }
    Input::FeedGamepad({0, 0}, 0); s.Update(kDt);
    CHECK(c->stance() == C::Stance::Prone);
    for (int t = 0; t < 2; ++t) {
        for (int i = 0; i < 3; ++i) { Input::FeedGamepad({0, 0}, 1u << 1); s.Update(kDt); }
        Input::FeedGamepad({0, 0}, 0); Step(s, {}, 5);
    }
    CHECK(c->stance() == C::Stance::Crouch);
    Step(s, {}, 30);                                               // settled into the crouch
    CHECK(box->size.y < 1.4f && box->size.y > 1.0f);
    Step(s, {Input::KeyCtrl}, 1); Step(s, {}, 5);
    CHECK(c->stance() == C::Stance::Stand);
    c->allowProne = false;
    Step(s, {'z'}, 1); Step(s, {}, 5);
    CHECK(c->stance() == C::Stance::Stand);                        // no prone when it's switched off
    Input::FeedGamepad({0, 0}, 0);
}
} // namespace

int main() {
    RUN_SUITE("locomotion");

    // ---- Planted feet stay planted and on the ground, at every speed / direction ----
    {
        struct Case { int state; float v, angle, maxSlip; };
        const Case cases[] = {
            {2, 1.5f, 0.0f, 0.3f},  {2, 4.5f, 0.0f, 0.8f},  {3, 8.0f, 0.0f, 1.2f},  {17, 2.2f, 0.0f, 0.3f},
            {2, 4.5f, 90.0f, 1.0f}, {2, 4.5f, -90.0f, 1.0f}, {2, 4.5f, 180.0f, 0.8f}, {2, 1.5f, 135.0f, 0.4f},
        };
        for (const Case& k : cases) {
            GaitResult r = RunGait(k.state, k.v, k.angle);
            CHECK(r.planted > 50);
            CHECK(r.medianSlip < k.maxSlip);       // the old fixed-rate cycle slid at the full body speed
            CHECK(r.deepest > -0.02f);             // no foot sinks into the floor
        }
    }

    // ---- Human joint directions: knees bend forward, elbows bend forward ----
    {
        Scene s("joints");
        auto* ch = s.CreateGameObject("C")->AddComponent<Character>();
        ch->Apply();
        const int shins[2] = {Character::BoneIndex("l_shin"), Character::BoneIndex("r_shin")};
        const int fores[2] = {Character::BoneIndex("l_fore"), Character::BoneIndex("r_fore")};
        for (int state : {1, 2, 3, 5, 6, 17, 18, 19}) {
            ch->anim = state;
            for (int i = 0; i < 16; ++i) {
                std::vector<Vec3> p = ch->PoseAt(i * 0.07f);
                for (int k = 0; k < 2; ++k) {
                    CHECK(p[shins[k]].x > -1.0f);   // shin +X = knee flexion; negative = bent backwards
                    CHECK(p[fores[k]].x < 1.0f);    // forearm -X = elbow flexion; positive = hyperextended
                }
            }
        }
        // Crouch: the knees really bend (not a stiff lean) and the body drops.
        ch->anim = 6;
        CHECK(ch->PoseAt(0.0f)[shins[0]].x > 40.0f);
        CHECK(ch->StanceOffsetAt(0.0f).y < -0.1f);
        // Hang: hands above the head.
        ch->anim = 19;
        CHECK(ch->PoseAt(0.0f)[Character::BoneIndex("l_uparm")].x < -150.0f);
    }

    // ---- State from what the body really does ----
    {
        Scene s("state");
        Block(s, "Ground", {0, -0.5f, 0}, {60, 1, 60});
        Block(s, "Wall", {0, 1.5f, -3.0f}, {10, 3, 1});
        GameObject* p = s.CreateGameObject("Player");
        p->transform->localPosition = {0, 0.02f, 0};
        p->AddComponent<Character>()->Apply();
        p->AddComponent<Rigidbody3D>();
        auto* col = p->AddComponent<BoxCollider3D>(); col->size = {0.6f, 1.8f, 0.6f}; col->offset = {0, 0.9f, 0};
        p->AddComponent<ThirdPersonController>();
        auto* ch = p->GetComponent<Character>();
        s.Start();
        Step(s, {}, 60);
        CHECK_EQ(ch->anim, 1);                     // resting: idle, not the fall pose
        Step(s, {'w'}, 20);
        CHECK(ch->anim == 2 || ch->anim == 3);
        CHECK(ch->moveSpeed > 2.0f);
        Step(s, {'w'}, 120);                       // now pinned against the wall, still holding W
        CHECK_EQ(ch->anim, 1);                     // stands instead of running in place
        CHECK(ch->moveSpeed < 0.3f);
        Step(s, {' '}, 1);
        CHECK_EQ(ch->anim, 5);                     // jump pose from the jump frame
        Step(s, {}, 30);
        CHECK(ch->anim == 5 || ch->anim == 18);    // rising, then falling
        bool fell = false;
        for (int i = 0; i < 40 && !fell; ++i) { Step(s, {}, 1); fell = ch->anim == 18; }
        CHECK(fell);
        Step(s, {}, 90);
        CHECK_EQ(ch->anim, 1);                     // landed
    }

    // ---- First-person strafing: legs turn toward the travel direction ----
    {
        Scene s("fpstrafe");
        Block(s, "Ground", {0, -0.5f, 0}, {80, 1, 80});
        GameObject* p = Templates::AddFirstPersonPlayer(s, {0, 0.02f, 0});
        auto* ch = p->GetComponent<Character>();
        s.Start();
        Step(s, {}, 30);
        Step(s, {'d'}, 60);
        CHECK(ch->moveAngle > 70.0f && ch->moveAngle < 110.0f);   // moving to the body's right
        Step(s, {'a'}, 60);
        CHECK(ch->moveAngle < -70.0f && ch->moveAngle > -110.0f);
        Step(s, {}, 30);
    }

    // ---- Prone in first and third person ----
    ProneBody<ThirdPersonController>(Templates::ThirdPerson);
    ProneBody<FirstPersonController>(Templates::FPS);

    // ---- Crouch / prone on the other movement controllers ----
    StanceOn<CharacterController3D>(true);
    StanceOn<ThirdPersonShooterController>(true);
    StanceOn<TopDownController>(true);
    StanceOn<ClickToMoveController>(false);
    StanceOn<NPCController>(false);
    {
        Scene s("ser");
        GameObject* p = Templates::BuildPlayerBody(s, {0, 0, 0}, "N");
        auto* npc = p->AddComponent<NPCController>();
        npc->stanceControl.allowProne = false; npc->stanceControl.crouchKey = 'c';
        npc->stanceControl.crouchSpeed = 1.5f; npc->stanceControl.proneHeight = 0.4f;
        npc->stanceControl.Restore(1);
        std::string text = SceneSerializer::Serialize(s);
        Scene s2("ser2");
        CHECK(SceneSerializer::Deserialize(s2, text));
        auto* n2 = s2.Find("N") ? s2.Find("N")->GetComponent<NPCController>() : nullptr;
        CHECK(n2 != nullptr);
        if (n2) {
            const StanceControl& sc = n2->stanceControl;
            CHECK(!sc.allowProne); CHECK_EQ(sc.crouchKey, 'c'); CHECK_EQ(sc.proneKey, 0);
            CHECK_NEAR(sc.crouchSpeed, 1.5f, 1e-4f); CHECK_NEAR(sc.proneHeight, 0.4f, 1e-4f);
            CHECK_EQ(sc.Stance(), 1);                                  // saved crouched, loads crouched
            CHECK_NEAR(s2.Find("N")->GetComponent<BoxCollider3D>()->size.y, 1.8f, 1e-3f);   // collider saved standing
        }
    }

    // ---- Stance options survive save / load ----
    {
        Scene s("ser");
        auto* fp = s.CreateGameObject("F")->AddComponent<FirstPersonController>();
        auto* tp = s.CreateGameObject("T")->AddComponent<ThirdPersonController>();
        fp->allowProne = false; fp->resizeBody = false; fp->crouchHeight = 0.5f; fp->proneHeight = 0.25f;
        fp->stanceGamepadButton = 3; fp->proneHoldTime = 0.7f;
        tp->allowProne = false; tp->crouchHeight = 0.6f; tp->proneHeight = 0.3f; tp->stanceGamepadButton = -1;
        std::string text = SceneSerializer::Serialize(s);
        Scene s2("ser2");
        CHECK(SceneSerializer::Deserialize(s2, text));
        auto* fp2 = s2.Find("F") ? s2.Find("F")->GetComponent<FirstPersonController>() : nullptr;
        auto* tp2 = s2.Find("T") ? s2.Find("T")->GetComponent<ThirdPersonController>() : nullptr;
        CHECK(fp2 && tp2);
        if (fp2) {
            CHECK(!fp2->allowProne); CHECK(!fp2->resizeBody);
            CHECK_NEAR(fp2->crouchHeight, 0.5f, 1e-4f); CHECK_NEAR(fp2->proneHeight, 0.25f, 1e-4f);
            CHECK_EQ(fp2->stanceGamepadButton, 3); CHECK_NEAR(fp2->proneHoldTime, 0.7f, 1e-4f);
        }
        if (tp2) {
            CHECK(!tp2->allowProne); CHECK(tp2->resizeBody);
            CHECK_NEAR(tp2->crouchHeight, 0.6f, 1e-4f); CHECK_NEAR(tp2->proneHeight, 0.3f, 1e-4f);
            CHECK_EQ(tp2->stanceGamepadButton, -1);
        }
    }

    TEST_MAIN_RESULT();
}
