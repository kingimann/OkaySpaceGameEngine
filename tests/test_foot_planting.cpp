#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/FootIK.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/IKPose.hpp"
#include <cmath>

using namespace okay;

static void Tick(Scene& s, int frames, float dt = 1.0f / 60.0f) { for (int i = 0; i < frames; ++i) s.Update(dt); }

// A straight leg (hip at y=1, knee at 0.5, foot at 0) under a model whose clip
// re-poses all three bones every frame, like an imported character.
struct Leg {
    Scene s;
    GameObject* model; GameObject* hip; GameObject* knee; GameObject* foot;
    ModelAnimator* ma; FootIK* ik;
    explicit Leg(float footXEnd) {
        s.physicsEnabled = false;
        model = s.CreateGameObject("Model");
        hip = s.CreateGameObject("LeftUpLeg");  hip->transform->SetParent(model->transform, false);
        knee = s.CreateGameObject("LeftLeg");   knee->transform->SetParent(hip->transform, false);
        foot = s.CreateGameObject("LeftFoot");  foot->transform->SetParent(knee->transform, false);
        hip->transform->localPosition = {0, 1, 0};
        knee->transform->localPosition = {0.0f, -0.5f, 0.02f};   // a slight knee bend
        foot->transform->localPosition = {0.0f, -0.5f, -0.02f};
        ModelAnimator::Clip c; c.name = "walk";
        for (auto* g : {hip, knee, foot}) {
            ModelAnimator::NodeClip nc; nc.node = g->name; nc.clip.smoothKeys = false;
            Vec3 p = g->transform->localPosition;
            nc.clip.AddKey("position.x", 0.0f, p.x); nc.clip.AddKey("position.y", 0.0f, p.y); nc.clip.AddKey("position.z", 0.0f, p.z);
            float xe = (g == foot) ? p.x + footXEnd : p.x;
            nc.clip.AddKey("position.x", 1.0f, xe); nc.clip.AddKey("position.y", 1.0f, p.y); nc.clip.AddKey("position.z", 1.0f, p.z);
            c.nodes.push_back(nc);
        }
        ma = model->AddComponent<ModelAnimator>();
        ma->clips = {c}; ma->loop = false;
        ik = model->AddComponent<FootIK>();
        ik->leftHipName = "LeftUpLeg"; ik->leftKneeName = "LeftLeg"; ik->leftFootName = "LeftFoot";
        ik->useRaycast = false;
        ik->footOffset = 0.0f;
    }
};

int main() {
    RUN_SUITE("foot_planting");

    // ---- An animated leg on a raised step gets the whole lift, not a fraction.
    {
        Leg L(0.0f);
        L.ik->groundY = 0.3f;
        L.s.Start();
        Tick(L.s, 60);
        CHECK(std::fabs(L.foot->transform->Position().y - 0.3f) < 0.01f);   // planted on the step
        // The hip stays where the animation put it; the leg bends to reach.
        CHECK(std::fabs(L.hip->transform->Position().y - 1.0f) < 1e-3f);
        CHECK(L.knee->transform->Position().z > 0.05f);                     // knee bends forward
    }

    // ---- Same with a static (non-animated) leg: converges and holds, no drift.
    {
        Scene s; s.physicsEnabled = false;
        GameObject* root = s.CreateGameObject("Rig");
        GameObject* hip = s.CreateGameObject("LeftUpLeg");  hip->transform->SetParent(root->transform, false);
        GameObject* knee = s.CreateGameObject("LeftLeg");   knee->transform->SetParent(hip->transform, false);
        GameObject* foot = s.CreateGameObject("LeftFoot");  foot->transform->SetParent(knee->transform, false);
        hip->transform->localPosition = {0, 1, 0};
        knee->transform->localPosition = {0.0f, -0.5f, 0.02f};
        foot->transform->localPosition = {0.0f, -0.5f, -0.02f};
        auto* ik = root->AddComponent<FootIK>();
        ik->leftHipName = "LeftUpLeg"; ik->leftKneeName = "LeftLeg"; ik->leftFootName = "LeftFoot";
        ik->useRaycast = false; ik->footOffset = 0.0f; ik->groundY = 0.25f;
        s.Start();
        Tick(s, 90);
        float y1 = foot->transform->Position().y;
        Tick(s, 60);
        CHECK(std::fabs(y1 - 0.25f) < 0.01f);
        CHECK(std::fabs(foot->transform->Position().y - y1) < 1e-3f);
    }

    // ---- Foot locking: the clip slides a planted foot 0.15 forward over a
    //      second; with locking on it stays where it landed.
    {
        Leg L(0.15f);
        L.ik->groundY = 0.0f;
        L.ik->footLock = true;
        L.s.Start();
        Tick(L.s, 10);
        CHECK(L.ik->FootLocked(0));
        float x0 = L.foot->transform->Position().x;
        Tick(L.s, 40);
        float x1 = L.foot->transform->Position().x;
        CHECK(std::fabs(x1 - x0) < 0.02f);           // pinned (the animation moved it ~0.1)
        // Without locking the same clip slides it.
        Leg U(0.15f);
        U.ik->groundY = 0.0f;
        U.s.Start();
        Tick(U.s, 10);
        float u0 = U.foot->transform->Position().x;
        Tick(U.s, 40);
        CHECK(U.foot->transform->Position().x - u0 > 0.08f);
    }

    // ---- The lock lets go when the animated foot gets too far from the pin.
    {
        Leg L(0.9f);   // fast enough to count as moving? 0.9/s > lockSpeed: never locks
        L.ik->groundY = 0.0f;
        L.ik->footLock = true;
        L.ik->lockSpeed = 1.2f;          // ... so make it count as planted, then outrun the release
        L.ik->lockRelease = 0.2f;
        L.s.Start();
        Tick(L.s, 5);
        CHECK(L.ik->FootLocked(0));
        Tick(L.s, 25);                     // animated foot ~0.37 ahead of the pin
        CHECK(!L.ik->FootLocked(0) || std::fabs(L.foot->transform->Position().x - 0.9f * 30 / 60.0f) < 0.25f);
    }

    // ---- IK posing: moving a hand bends the elbow; bone lengths stay; the
    //      shoulder stays put; out-of-reach targets are reached as far as possible.
    {
        Scene s;
        GameObject* sh = s.CreateGameObject("Shoulder");
        GameObject* el = s.CreateGameObject("Elbow"); el->transform->SetParent(sh->transform, false);
        GameObject* ha = s.CreateGameObject("Hand");  ha->transform->SetParent(el->transform, false);
        sh->transform->localPosition = {0, 1.5f, 0};
        el->transform->localPosition = {0.3f, 0, 0};
        ha->transform->localPosition = {0.3f, 0, 0};          // a straight arm along +X
        Vec3 target{0.35f, 1.8f, 0.1f};
        CHECK(PoseTwoBoneChain(ha->transform, target));
        CHECK((ha->transform->Position() - target).Magnitude() < 1e-3f);
        CHECK(std::fabs((el->transform->Position() - sh->transform->Position()).Magnitude() - 0.3f) < 1e-4f);
        CHECK(std::fabs((ha->transform->Position() - el->transform->Position()).Magnitude() - 0.3f) < 1e-4f);
        CHECK((sh->transform->Position() - Vec3{0, 1.5f, 0}).Magnitude() < 1e-5f);
        // The bend kept: posing again from a bent arm keeps the elbow on the same side.
        Vec3 bend0 = el->transform->Position() - sh->transform->Position();
        PoseTwoBoneChain(ha->transform, Vec3{0.4f, 1.75f, 0.05f});
        Vec3 bend1 = el->transform->Position() - sh->transform->Position();
        CHECK(Vec3::Dot(bend0, bend1) > 0.0f);
        // Out of reach: the arm points at the target, fully extended.
        PoseTwoBoneChain(ha->transform, Vec3{5, 1.5f, 0});
        CHECK(std::fabs(ha->transform->Position().x - 0.6f) < 0.01f);
        CHECK(!PoseTwoBoneChain(sh->transform, target));      // no grandparent: nothing to bend
    }

    TEST_MAIN_RESULT();
}
