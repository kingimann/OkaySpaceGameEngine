#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>
#include <fstream>

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

    // A manual clip gives way to movement: a looping "Apply"/autoPlay clip no longer
    // freezes the player; one flagged overrideMovement keeps playing; jump/fall/crouch
    // bind like idle/walk/run.
    {
        Scene s("A");
        auto* ch = s.CreateGameObject("Hero")->AddComponent<Character>();
        ch->Apply();
        ch->AddClip(clip("flourish"));
        ch->AddClip(clip("hop"));
        ch->autoPlayClip = "flourish";
        ch->clipJump = "hop";
        s.Start();
        ch->anim = 1; s.Update(0.05f);
        CHECK(ch->PlayingClip() == "flourish");   // standing still: it plays
        ch->anim = 2; s.Update(0.05f);
        CHECK(!ch->IsPlayingClip());              // walking: back to the built-in walk
        ch->anim = 5; s.Update(0.05f);
        CHECK(ch->PlayingClip() == "hop");        // jump binding

        AnimClip ov = clip("dance"); ov.overrideMovement = true;
        ch->AddClip(ov);
        ch->anim = 1; s.Update(0.05f);
        ch->PlayClip("dance");
        ch->anim = 2; s.Update(0.05f);
        CHECK(ch->PlayingClip() == "dance");      // override: movement doesn't stop it
    }

    // Persistence: new bindings, the linked flag, per-clip interp/speed/length/override,
    // and a clips path with spaces all survive a save/load; in-scene clips win over the
    // clips file on Start unless the file is linked.
    {
        const std::string path = std::string("anim dir test ") + "clips.okayanim";
        {
            std::ofstream f(path);
            f << "clip idle loop\nkey 0\n  r_uparm 0 0 -45\nkey 1\n  r_uparm 0 0 -45\n"
                 "clip extra loop\nkey 0\nkey 1\n";
        }
        Scene s("A");
        auto* ch = s.CreateGameObject("Hero")->AddComponent<Character>();
        ch->Apply();
        AnimClip idle = clip("idle"); idle.interp = AnimInterp::Step; idle.speed = 2.0f;
        idle.length = 1.5f; idle.overrideMovement = true;
        ch->AddClip(idle);
        ch->clipsFile = path; ch->clipJump = "idle"; ch->clipFall = "idle"; ch->clipCrouch = "idle";
        std::string text = SceneSerializer::Serialize(s);
        Scene b("B");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto* lch = b.Find("Hero") ? b.Find("Hero")->GetComponent<Character>() : nullptr;
        CHECK(lch != nullptr);
        if (lch) {
            CHECK(lch->clipsFile == path);
            CHECK(lch->clipJump == "idle" && lch->clipFall == "idle" && lch->clipCrouch == "idle");
            CHECK(!lch->clipsLinked);
            const AnimClip* c = lch->FindClip("idle");
            CHECK(c && c->interp == AnimInterp::Step && c->speed == 2.0f && c->length == 1.5f && c->overrideMovement);
            b.Start();
            c = lch->FindClip("idle");
            CHECK(c && c->keys.size() == 2 && c->keys[0].pose[Character::BoneIndex("r_uparm")].z == 0.0f);  // scene's idle kept
            CHECK(lch->HasClip("extra"));                       // the file still adds what's missing
        }
        Scene l("L");
        auto* lk = l.CreateGameObject("Hero")->AddComponent<Character>();
        lk->Apply();
        lk->AddClip(clip("idle"));
        lk->clipsFile = path; lk->clipsLinked = true;
        l.Start();
        const AnimClip* c = lk->FindClip("idle");
        CHECK(c && c->keys[0].pose[Character::BoneIndex("r_uparm")].z == -45.0f);   // linked: the file wins
        std::remove(path.c_str());
    }

    // Old saves wrote the clips path as one bare token; it still reads.
    {
        Character base;
        base.clipsFile = "old/clips.okayanim";
        std::string t = base.ToText();
        const std::string quoted = "\"old/clips.okayanim\"";
        auto at = t.find(quoted);
        CHECK(at != std::string::npos);
        if (at != std::string::npos) t.replace(at, quoted.size(), "old/clips.okayanim");
        Character c;
        c.FromText(t);
        CHECK(c.clipsFile == "old/clips.okayanim");
    }

    TEST_MAIN_RESULT();
}
