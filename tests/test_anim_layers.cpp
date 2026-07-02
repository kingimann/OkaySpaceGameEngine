#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

// Animation system: per-clip easing (linear/smooth/step), playback speed, and partial-
// body layers (play a clip on a masked set of bones over the base animation).
int main() {
    RUN_SUITE("anim_layers");

    const int RU = Character::BoneIndex("r_uparm");
    const int LT = Character::BoneIndex("l_thigh");

    // ---- Easing modes parse and shape interpolation differently ----
    {
        std::string text =
            "clip lin loop linear\n key 0\n  r_uparm 0 0 0\n key 1\n  r_uparm 0 0 100\n"
            "clip smo loop smooth\n key 0\n  r_uparm 0 0 0\n key 1\n  r_uparm 0 0 100\n"
            "clip stp loop step\n key 0\n  r_uparm 0 0 0\n key 1\n  r_uparm 0 0 100\n"
            "clip fast once\n speed 2\n key 0\n  r_uparm 0 0 0\n key 1\n  r_uparm 0 0 100\n";
        std::string err;
        auto clips = AnimClip::ParseAll(text, &Character::BoneIndex, Character::BoneCount(), &err);
        CHECK(err.empty());
        CHECK(clips.size() == 4);

        // At the midpoint, linear = 50, smooth = 50 too (smoothstep(0.5)=0.5) — so test
        // a quarter point where they differ: linear(0.25)=25, smooth(0.25)=~15.6, step=0.
        auto val = [&](const AnimClip& c) { return c.Sample(0.25f)[RU].z; };
        const AnimClip& lin = clips[0]; const AnimClip& smo = clips[1]; const AnimClip& stp = clips[2];
        CHECK(std::fabs(val(lin) - 25.0f) < 0.5f);
        CHECK(val(smo) < val(lin) - 2.0f);            // eased in: behind linear at 0.25
        CHECK(std::fabs(val(stp) - 0.0f) < 1e-3f);    // step holds the earlier key
        CHECK(std::fabs(clips[3].speed - 2.0f) < 1e-3f);
    }

    // ---- Clip speed makes the playhead advance faster ----
    {
        Scene s("spd");
        auto* ch = s.CreateGameObject("C")->AddComponent<Character>();
        ch->Apply();
        int n = ch->LoadClips("clip go once\n speed 3\n key 0\n  r_uparm 0 0 0\n key 3\n  r_uparm 0 0 90\n");
        CHECK(n == 1);
        ch->PlayClip("go");
        s.Start();
        s.Update(1.0f);                               // 1s of dt * speed 3 -> playhead ~3s (end)
        CHECK(ch->ClipNormalizedTime() > 0.95f);
    }

    // ---- Layer: play a clip on the ARMS only, over a base leg animation ----
    {
        Scene s("layer");
        auto* ch = s.CreateGameObject("C")->AddComponent<Character>();
        ch->Apply();
        ch->blendTime = 0.0f;   // no crossfade so we read the clip's steady-state pose
        ch->LoadClips(
            "clip legs loop\n key 0\n  l_thigh 40 0 0\n key 1\n  l_thigh 40 0 0\n"
            "clip wave loop\n key 0\n  r_uparm 0 0 -150\n key 1\n  r_uparm 0 0 -150\n");
        ch->PlayClip("legs");
        CHECK(ch->PlayLayer("wave", Character::ArmsMask()));
        CHECK(ch->IsLayering());
        s.Start();
        s.Update(0.1f);
        std::vector<Vec3> pose = ch->CurrentPose();
        // The arm follows the layer (wave), the leg keeps the base clip.
        CHECK(std::fabs(pose[RU].z - (-150.0f)) < 1.0f);   // arm overridden by the layer
        CHECK(std::fabs(pose[LT].x - 40.0f) < 1.0f);       // leg keeps the base clip

        // The arm is NOT in the mask if we only mask the thigh.
        ch->PlayLayer("wave", Character::BoneBit(LT));
        s.Update(0.1f);
        pose = ch->CurrentPose();
        CHECK(std::fabs(pose[RU].z - (-150.0f)) > 1.0f);   // arm no longer forced by the layer

        ch->StopLayer();
        CHECK(!ch->IsLayering());
    }

    // ---- 1D blend tree: blends clips by a parameter (idle/walk/run by speed) ----
    {
        Scene s("blend");
        auto* ch = s.CreateGameObject("C")->AddComponent<Character>();
        ch->Apply();
        ch->blendTime = 0.0f;
        // idle: thigh 0; run: thigh 60. A static pose per clip so we can read the blend.
        ch->LoadClips(
            "clip idle loop\n key 0\n  l_thigh 0 0 0\n key 1\n  l_thigh 0 0 0\n"
            "clip run loop\n key 0\n  l_thigh 60 0 0\n key 1\n  l_thigh 60 0 0\n");
        ch->SetBlendTree({ {0.0f, "idle"}, {5.0f, "run"} });
        CHECK(ch->HasBlendTree());
        s.Start();

        ch->SetBlendParam(0.0f);  s.Update(0.05f);
        CHECK(std::fabs(ch->CurrentPose()[LT].x - 0.0f) < 1.0f);    // pure idle

        ch->SetBlendParam(5.0f);  s.Update(0.05f);
        CHECK(std::fabs(ch->CurrentPose()[LT].x - 60.0f) < 1.0f);   // pure run

        ch->SetBlendParam(2.5f);  s.Update(0.05f);
        CHECK(std::fabs(ch->CurrentPose()[LT].x - 30.0f) < 2.0f);   // halfway blend

        ch->ClearBlendTree();
        CHECK(!ch->HasBlendTree());
    }

    // ---- Additive layer + weight ----
    {
        Scene s("add");
        auto* ch = s.CreateGameObject("C")->AddComponent<Character>();
        ch->Apply(); ch->blendTime = 0.0f;
        ch->LoadClips(
            "clip base loop\n key 0\n  r_uparm 10 0 0\n key 1\n  r_uparm 10 0 0\n"
            "clip off loop\n key 0\n  r_uparm 20 0 0\n key 1\n  r_uparm 20 0 0\n");
        ch->PlayClip("base");
        // Additive at full weight: 10 (base) + 20 (layer) = 30.
        ch->PlayLayer("off", Character::ArmsMask(), /*additive=*/true, 1.0f);
        s.Start(); s.Update(0.05f);
        CHECK(std::fabs(ch->CurrentPose()[RU].x - 30.0f) < 1.0f);
        // Additive at half weight: 10 + 20*0.5 = 20.
        ch->SetLayerWeight(0.5f); s.Update(0.05f);
        CHECK(std::fabs(ch->CurrentPose()[RU].x - 20.0f) < 1.0f);
    }

    // ---- Text-authored events fire as the clip plays ----
    {
        Scene s("evt");
        auto* ch = s.CreateGameObject("C")->AddComponent<Character>();
        ch->Apply();
        ch->LoadClips("clip walk loop\n event 0.1 step\n key 0\n  l_thigh 0 0 0\n key 0.3\n  l_thigh 20 0 0\n");
        std::string got;
        ch->onAnimEvent = [&](const std::string& e) { got = e; };
        ch->PlayClip("walk");
        s.Start();
        for (int i = 0; i < 5; ++i) s.Update(0.05f);   // crosses t=0.1
        CHECK(got == "step");
    }

    // ---- Pose mirroring swaps L/R ----
    {
        std::vector<Vec3> pose(Character::BoneCount(), Vec3{0, 0, 0});
        int LU = Character::BoneIndex("l_uparm"), RUp = Character::BoneIndex("r_uparm");
        pose[RUp] = {30, 10, 5};
        Character::MirrorPose(pose);
        // The right-arm pose moved to the left arm with yaw/roll flipped.
        CHECK(std::fabs(pose[LU].x - 30.0f) < 1e-3f);
        CHECK(std::fabs(pose[LU].y + 10.0f) < 1e-3f);
        CHECK(std::fabs(pose[LU].z + 5.0f) < 1e-3f);
    }

    TEST_MAIN_RESULT();
}
