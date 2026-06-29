#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

// Animation blending: switching clips crossfades from the current pose into the new
// clip over `blendTime` instead of snapping. Mid-blend the bone sits partway; once
// the blend completes it reaches the new clip's pose.
static AnimClip holdClip(const char* name, int bone, Vec3 rot) {
    AnimClip c; c.name = name; c.loop = true;
    AnimKey k0; k0.time = 0.0f; k0.pose.assign(Character::BoneCount(), Vec3{0, 0, 0});
    AnimKey k1; k1.time = 1.0f; k1.pose.assign(Character::BoneCount(), Vec3{0, 0, 0});
    k0.pose[bone] = rot; k1.pose[bone] = rot;   // constant pose, so timing doesn't matter
    c.keys = {k0, k1};
    return c;
}

static float dotTo(const Quat& r, const Quat& e) {
    return std::fabs(r.x*e.x + r.y*e.y + r.z*e.z + r.w*e.w);
}

int main() {
    RUN_SUITE("char_blend");

    const int ARM = 6;   // B_RUPARM
    Scene s("A");
    auto* ch = s.CreateGameObject("Hero")->AddComponent<Character>();
    ch->Apply(); ch->separateParts = true;
    ch->blendTime = 0.2f;
    ch->AddClip(holdClip("rest",  ARM, {0, 0, 0}));
    ch->AddClip(holdClip("raise", ARM, {0, 0, -90}));
    ch->PlayClip("rest");
    s.Start();
    for (int i = 0; i < 30; ++i) s.Update(0.05f);   // settle on the rest pose

    GameObject* arm = ch->Part(ARM);
    CHECK(arm != nullptr);
    Quat raised = Quat::Euler(0, 0, -90), rest = Quat::Euler(0, 0, 0);
    CHECK(dotTo(arm->transform->localRotation, rest) > 0.99f);   // settled on the rest pose

    // Switch to the raised clip — this starts a crossfade.
    ch->PlayClip("raise");
    CHECK(ch->Blending());
    s.Update(0.05f);                       // 0.05s into a 0.2s blend
    CHECK(ch->Blending());                 // still mid-blend
    if (arm) CHECK(dotTo(arm->transform->localRotation, raised) < 0.99f);  // partway, not snapped

    for (int i = 0; i < 20; ++i) s.Update(0.05f);   // let the blend finish
    CHECK(!ch->Blending());
    if (arm) CHECK(dotTo(arm->transform->localRotation, raised) > 0.99f);  // reached the new pose

    // blendTime persists through a save/load round-trip.
    std::string text = SceneSerializer::Serialize(s);
    Scene b("B");
    CHECK(SceneSerializer::Deserialize(b, text));
    auto* lch = b.Find("Hero") ? b.Find("Hero")->GetComponent<Character>() : nullptr;
    CHECK(lch != nullptr);
    if (lch) CHECK(std::fabs(lch->blendTime - 0.2f) < 1e-4f);

    TEST_MAIN_RESULT();
}
