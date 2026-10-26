#include "test_framework.hpp"
#include <cmath>
#include <Okay.hpp>
#include "okay/Components/AnimClip.hpp"

using namespace okay;

// Authoring a custom character animation should be as easy as writing a few
// keyframes in text and playing it by name. This exercises the parser, the
// interpolation, and the Character playback API.
int main() {
    RUN_SUITE("animclip");

    const char* src = R"CLIP(
        # A wave that loops, and a one-shot bow.
        clip wave loop
        key 0.0
          r_uparm 0 0 0
        key 1.0
          r_uparm 0 0 -90
        clip bow once
        key 0.0
        key 0.5
          torso 40 0 0
    )CLIP";

    std::string err;
    auto clips = AnimClip::ParseAll(src, &Character::BoneIndex, Character::BoneCount(), &err);
    CHECK(err.empty());
    if (!err.empty()) std::cerr << "  parse: " << err << "\n";
    CHECK(clips.size() == 2);

    // Clip 0: wave, loops, 1s long, r_uparm.z eases 0 -> -90.
    const AnimClip& wave = clips[0];
    CHECK(wave.name == "wave");
    CHECK(wave.loop);
    CHECK_NEAR(wave.Duration(), 1.0f, 1e-4f);
    int ru = Character::BoneIndex("r_uparm");
    CHECK(ru >= 0);
    auto mid = wave.Sample(0.5f);
    CHECK_NEAR(mid[ru].z, -45.0f, 0.01f);   // halfway between the two keys
    auto endP = wave.Sample(5.0f);          // past the end clamps to the last key
    CHECK_NEAR(endP[ru].z, -90.0f, 0.01f);

    // Clip 1: bow, one-shot.
    CHECK(clips[1].name == "bow");
    CHECK(!clips[1].loop);

    // Character playback API.
    Scene scene("Anim"); scene.physicsEnabled = false;
    GameObject* go = scene.CreateGameObject("Hero");
    Character* ch = go->AddComponent<Character>();
    int n = ch->LoadClips(src);
    CHECK(n == 2);
    CHECK(ch->PlayClip("wave"));
    CHECK(ch->IsPlayingClip());
    CHECK(ch->PlayingClip() == "wave");
    CHECK(!ch->PlayClip("does-not-exist"));   // unknown clip -> false
    scene.Start();
    for (int i = 0; i < 5; ++i) scene.Update(0.1f);   // drive the clip; must not crash
    ch->StopClip();
    CHECK(!ch->IsPlayingClip());

    // A malformed bone token is reported, not silently ignored.
    std::string e2;
    AnimClip::ParseAll("clip x\nkey 0\n  nosuchbone 1 2 3\n",
                       &Character::BoneIndex, Character::BoneCount(), &e2);
    CHECK(!e2.empty());

    // clipsFile / autoPlayClip survive a save/load round-trip...
    {
        Character a;
        a.clipsFile = "hero.okayanim";
        a.autoPlayClip = "wave";
        Character b;
        b.FromText(a.ToText());
        CHECK(b.clipsFile == "hero.okayanim");
        CHECK(b.autoPlayClip == "wave");
    }
    // ...and an older save without those fields still loads (defaults stay empty).
    {
        Character base;
        std::string legacy = base.ToText();
        for (int k = 0; k < 2; ++k) legacy.erase(legacy.find_last_of(' '));  // drop the 2 clip tokens
        Character c;
        c.FromText(legacy);
        CHECK(c.autoPlayClip.empty());        // field absent -> stays default
        CHECK_NEAR(c.height, base.height, 1e-4f);   // the rest parsed fine
    }

    // Rotations interpolate along the short arc (slerp), not by lerping Euler angles:
    // -150 -> +150 about z swings 60 deg through 180, instead of 300 deg the long way
    // through 0 (the "arm flips" bug).
    {
        const int ru = Character::BoneIndex("r_uparm");
        AnimClip c; c.loop = false;
        AnimKey a; a.time = 0.0f; a.pose.assign(Character::BoneCount(), Vec3{0, 0, 0}); a.pose[ru] = Vec3{0, 0, -150};
        AnimKey b = a; b.time = 1.0f; b.pose[ru] = Vec3{0, 0, 150};
        c.keys = {a, b};
        auto angleTo = [](const Vec3& e, float zDeg) {   // rotation angle between two poses, degrees
            Quat d = Quat::Euler(e) * Quat::Euler(0, 0, zDeg).Inverse();
            float w = std::fabs(d.w) > 1.0f ? 1.0f : std::fabs(d.w);
            return 2.0f * std::acos(w) * 57.29578f;
        };
        CHECK(angleTo(c.Sample(0.5f)[ru], 180.0f) < 0.5f);
        CHECK(angleTo(c.Sample(0.25f)[ru], -165.0f) < 0.5f);
        CHECK(c.Sample(0.0f)[ru].z == -150.0f);      // keys themselves come back exactly
    }

    // A looping clip with a length past its last key wraps last -> first over the gap,
    // so the loop has no seam (and Duration is the loop length).
    {
        const int ru = Character::BoneIndex("r_uparm");
        AnimClip c; c.loop = true; c.length = 1.0f;
        AnimKey a; a.time = 0.0f; a.pose.assign(Character::BoneCount(), Vec3{0, 0, 0});
        AnimKey b = a; b.time = 0.5f; b.pose[ru] = Vec3{0, 0, 90};
        c.keys = {a, b};
        CHECK_NEAR(c.Duration(), 1.0f, 1e-5f);
        CHECK_NEAR(c.Sample(0.75f)[ru].z, 45.0f, 0.01f);
        CHECK_NEAR(c.Sample(0.999f)[ru].z, 0.18f, 0.05f);
        c.loop = false;                               // a one-shot ignores length
        CHECK_NEAR(c.Duration(), 0.5f, 1e-5f);
        CHECK_NEAR(c.Sample(0.75f)[ru].z, 90.0f, 0.01f);
    }

    TEST_MAIN_RESULT();
}
