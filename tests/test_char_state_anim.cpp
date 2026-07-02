#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// No-code state animations: bind authored clips to movement states (idle/walk/run)
// and the character auto-plays YOUR clip as `anim` changes — no scripting. Bindings
// also survive a save/load round-trip.
static AnimClip clip(const char* name) {
    AnimClip c; c.name = name; c.loop = true;
    AnimKey k0; k0.time = 0.0f; k0.pose.assign(Character::BoneCount(), Vec3{0, 0, 0});
    AnimKey k1; k1.time = 1.0f; k1.pose.assign(Character::BoneCount(), Vec3{0, 0, 0});
    c.keys = {k0, k1};
    return c;
}

int main() {
    RUN_SUITE("char_state_anim");

    {
        Scene s("A");
        auto* ch = s.CreateGameObject("Hero")->AddComponent<Character>();
        ch->Apply(); ch->separateParts = true;
        ch->AddClip(clip("idle"));
        ch->AddClip(clip("walk"));
        ch->clipIdle = "idle";
        ch->clipWalk = "walk";              // run intentionally left unbound
        s.Start();

        // Idle state -> our idle clip drives.
        ch->anim = 1; s.Update(0.05f);
        CHECK(ch->PlayingClip() == "idle");
        CHECK(ch->StateDriven());

        // Walk state -> our walk clip takes over.
        ch->anim = 2; s.Update(0.05f);
        CHECK(ch->PlayingClip() == "walk");

        // Run has no binding -> hand back to the built-in animation (no active clip).
        ch->anim = 3; s.Update(0.05f);
        CHECK(!ch->IsPlayingClip());
        CHECK(!ch->StateDriven());

        // Back to walking re-engages the bound clip.
        ch->anim = 2; s.Update(0.05f);
        CHECK(ch->PlayingClip() == "walk");
    }

    // Round-trip: the bindings persist with the scene.
    {
        Scene s("A");
        auto* ch = s.CreateGameObject("Hero")->AddComponent<Character>();
        ch->Apply();
        ch->AddClip(clip("idle"));
        ch->AddClip(clip("walk"));
        ch->AddClip(clip("dash"));
        ch->clipIdle = "idle"; ch->clipWalk = "walk"; ch->clipRun = "dash";

        std::string text = SceneSerializer::Serialize(s);
        Scene b("B");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto* lch = b.Find("Hero") ? b.Find("Hero")->GetComponent<Character>() : nullptr;
        CHECK(lch != nullptr);
        if (lch) {
            CHECK(lch->clipIdle == "idle");
            CHECK(lch->clipWalk == "walk");
            CHECK(lch->clipRun  == "dash");
        }
    }

    TEST_MAIN_RESULT();
}
