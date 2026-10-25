#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <vector>

using namespace okay;

namespace {

// Walk the one-click humanoid forward over flat ground and measure its legs,
// relative to the body: the highest a foot lifts, the widest the feet spread
// front to back, and the lowest the hips dip.
// `gap`: the most any shin or foot part came away from its joint (IK corrections).
struct Walk { float lift = 0.0f, spread = 0.0f, hipsLow = 1e9f, gap = 0.0f; };
Walk WalkOver(bool footIK, bool step) {
    Scene s("W");
    auto box = [&](Vec3 pos, Vec3 scale) {
        GameObject* g = s.CreateGameObject("Ground");
        g->transform->localPosition = pos;
        g->transform->localScale = scale;
        g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    };
    box({0, -0.5f, 0}, {200, 1, 200});
    if (step) box({0, 0.05f, -4.0f}, {4, 0.1f, 1.5f});   // 10 cm high, across the path
    GameObject* p = Templates::AddHumanoidPlayer(s, {0, 0.05f, 0});
    if (!footIK) p->GetComponent<FootIK>()->weight = 0.0f;
    Character* pc = p->GetComponent<Character>();
    std::vector<Vec3> rest;
    for (int b : {10, 11, 13, 14}) rest.push_back(pc->Part(b)->transform->localPosition);
    s.Start();
    Walk w;
    for (int i = 0; i < 240; ++i) {
        Input::FeedKeys({'W'});
        s.Update(1.0f / 60.0f);
        // Shin under thigh, foot under shin: each should sit where its parent's joint is.
        int k = 0;
        for (int b : {10, 11, 13, 14}) {
            Vec3 joint = pc->Part(b - 1)->transform->TransformPoint(rest[k++]);
            w.gap = std::fmax(w.gap, (pc->Part(b)->transform->Position() - joint).Magnitude());
        }
        if (i < 60) continue;                        // up to speed
        Vec3 base = p->transform->Position();
        Vec3 l = pc->Part(11)->transform->Position(), r = pc->Part(14)->transform->Position();
        w.lift = std::fmax(w.lift, std::fmax(l.y, r.y) - base.y);
        w.spread = std::fmax(w.spread, std::fabs(l.z - r.z));   // walking along -Z
        w.hipsLow = std::fmin(w.hipsLow, pc->Part(0)->transform->Position().y - base.y);
    }
    return w;
}

} // namespace

// The one-click humanoid: rig + capsule + camera + foot IK + root motion, wired.
int main() {
    RUN_SUITE("humanoid_template");

    Scene s("H");
    GameObject* player = Templates::AddHumanoidPlayer(s, {0, 1, 0});
    CHECK(player != nullptr);

    auto* pc = player->GetComponent<Character>();
    CHECK(pc != nullptr);
    CHECK(pc->PartsBuilt());                       // the bone rig was built for IK wiring

    // Physics: a capsule + body.
    CHECK(player->GetComponent<Rigidbody3D>() != nullptr);
    CHECK(player->GetComponent<CapsuleCollider3D>() != nullptr);

    // A controller and a camera came along.
    CHECK(player->GetComponent<ThirdPersonController>() != nullptr);
    bool hasCam = false;
    for (const auto& go : s.Objects()) if (go->GetComponent<Camera>()) hasCam = true;
    CHECK(hasCam);

    // Foot IK is wired to the actual leg bones (L thigh/shin/foot = 9/10/11, R = 12/13/14).
    auto* fik = player->GetComponent<FootIK>();
    CHECK(fik != nullptr);
    CHECK(fik->leftHip   == pc->Part(9)->transform);
    CHECK(fik->leftKnee  == pc->Part(10)->transform);
    CHECK(fik->leftFoot  == pc->Part(11)->transform);
    CHECK(fik->rightHip  == pc->Part(12)->transform);
    CHECK(fik->rightKnee == pc->Part(13)->transform);
    CHECK(fik->rightFoot == pc->Part(14)->transform);

    // Root motion is wired to the hips, left Disabled (procedural anims bob the hips).
    auto* rm = player->GetComponent<RootMotion>();
    CHECK(rm != nullptr);
    CHECK(rm->rootNode == pc->Part(0)->transform);
    CHECK(rm->mode == (int)RootMotion::Mode::Disabled);

    // It runs without blowing up and stays upright (rotation frozen by default).
    s.Start();
    for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
    CHECK(player->transform->Position().y > -1.0f);   // didn't fall through the world

    // On flat ground foot IK has nothing to correct: the walk keeps its stride.
    // (It used to press the swinging foot down to the floor and sink the pelvis a
    // little more every frame, so the legs only bent while the body slid along.)
    {
        Walk anim = WalkOver(false, false), ik = WalkOver(true, false);
        CHECK(anim.lift > 0.2f && anim.spread > 0.5f);   // the walk itself steps
        CHECK(std::fabs(ik.lift - anim.lift) < 0.01f);
        CHECK(std::fabs(ik.spread - anim.spread) < 0.01f);
        CHECK(std::fabs(ik.hipsLow - anim.hipsLow) < 0.01f);
        CHECK(ik.gap < 1e-3f);
    }
    // Over a step the feet are moved, and the legs stay in one piece.
    {
        Walk ik = WalkOver(true, true);
        CHECK(ik.lift > 0.2f && ik.spread > 0.5f);
        CHECK(ik.gap < 0.01f);
    }

    TEST_MAIN_RESULT();
}
