#include "test_framework.hpp"
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

    TEST_MAIN_RESULT();
}
