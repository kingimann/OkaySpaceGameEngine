#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

// A keyframed character clip (as the Animation editor builds) plays on a
// separated-parts character — driving the part transforms — and persists through
// a save/load round-trip (auto-plays on load).
int main() {
    RUN_SUITE("char_anim");

    const int RUPARM = 6;   // bone index (B_RUPARM)
    const int nb = Character::BoneCount();

    auto makeClip = []() {
        AnimClip c; c.name = "Wave"; c.loop = false;
        AnimKey k0; k0.time = 0.0f; k0.pose.assign(Character::BoneCount(), Vec3{0, 0, 0});
        AnimKey k1; k1.time = 1.0f; k1.pose.assign(Character::BoneCount(), Vec3{0, 0, 0});
        k1.pose[6] = {0.0f, 0.0f, -90.0f};   // raise the right upper arm by t=1s
        c.keys = {k0, k1};
        return c;
    };

    {
        Scene s("A");
        auto* ch = s.CreateGameObject("Hero")->AddComponent<Character>();
        ch->Apply(); ch->separateParts = true;
        ch->AddClip(makeClip());
        ch->PlayClip("Wave");
        s.Start();
        for (int i = 0; i < 80; ++i) s.Update(0.05f);   // reach + hold t=1s (clip clamps)

        CHECK(ch->PartsBuilt());
        GameObject* arm = ch->Part(RUPARM);
        CHECK(arm != nullptr);
        if (arm) {
            Quat r = arm->transform->localRotation, e = Quat::Euler(0, 0, -90);
            float dot = r.x*e.x + r.y*e.y + r.z*e.z + r.w*e.w;
            CHECK(std::fabs(std::fabs(dot) - 1.0f) < 0.05f);   // part posed by the clip
        }

        // Round-trip: the authored clip persists and auto-plays on load.
        ch->autoPlayClip = "Wave";
        std::string text = SceneSerializer::Serialize(s);
        Scene b("B");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto* lch = b.Find("Hero") ? b.Find("Hero")->GetComponent<Character>() : nullptr;
        CHECK(lch != nullptr);
        b.Start();
        for (int i = 0; i < 80; ++i) b.Update(0.05f);
        if (lch && lch->Part(RUPARM)) {
            Quat r = lch->Part(RUPARM)->transform->localRotation, e = Quat::Euler(0, 0, -90);
            float dot = r.x*e.x + r.y*e.y + r.z*e.z + r.w*e.w;
            CHECK(std::fabs(std::fabs(dot) - 1.0f) < 0.05f);   // clip survived save/load
        }
    }
    (void)nb;

    TEST_MAIN_RESULT();
}
