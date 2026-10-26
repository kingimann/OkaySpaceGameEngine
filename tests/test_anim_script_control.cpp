#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/AnimControl.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include <algorithm>
#include <cmath>

using namespace okay;

// Scripts and visual scripts reach a player's animator and keep what they play;
// a state machine and locomotion don't fight; clip events at 0 fire.

static ModelAnimator::Clip MakeClip(const std::string& name, std::vector<ModelAnimator::ClipEvent> events = {}) {
    ModelAnimator::Clip c;
    c.name = name;
    ModelAnimator::NodeClip nc{"bone", AnimationClip{}};
    nc.clip.AddKey("position.x", 0.0f, 0.0f);
    nc.clip.AddKey("position.x", 1.0f, 1.0f);
    c.nodes.push_back(nc);
    c.events = std::move(events);
    return c;
}
static int Count(const std::vector<std::string>& v, const char* s) { return (int)std::count(v.begin(), v.end(), s); }

int main() {
    RUN_SUITE("anim_script_control");
    const float dt = 1.0f / 60.0f;

    {   // the player's script finds the model's animator in a child, and a held clip survives locomotion
        Scene s("held");
        GameObject* player = s.CreateGameObject("Player");
        GameObject* model = s.CreateGameObject("Model");
        GameObject* bone = s.CreateGameObject("bone");
        model->transform->SetParent(player->transform, false);
        bone->transform->SetParent(model->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->clips = {MakeClip("Idle"), MakeClip("Wave")};
        ma->driveByMovement = true;
        ma->idleClip = "Idle";
        ma->blendTime = 0.0f;
        s.Start();
        for (int i = 0; i < 3; ++i) s.Update(dt);
        CHECK(FindAnimComponent<ModelAnimator>(player) == ma);
        bool warned = false;
        CHECK(PlayClipFrom(player, "Wave", warned));
        for (int i = 0; i < 10; ++i) s.Update(dt);
        CHECK(ma->CurrentName() == "Wave");          // locomotion used to switch back to Idle the next frame
        CHECK(ma->ClipOverridesPose());
        StopClipFrom(player);
        s.Update(dt);
        CHECK(ma->CurrentName() == "Idle");          // movement drives again
        CHECK(!PlayClipFrom(s.CreateGameObject("Empty"), "Wave", warned) && warned);
    }
    {   // the default player (a Character): play_clip("Wave") holds the built-in wave over the controller's pick
        Scene s("character");
        GameObject* player = s.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>();
        s.Start();
        bool warned = false;
        CHECK(PlayClipFrom(player, "Wave", warned));
        ch->anim = 1;                                // what a controller sets every frame
        s.Update(dt);
        CHECK(ch->anim == Character::BuiltinAnimIndex("wave"));
        StopClipFrom(player);
        s.Update(dt);
        CHECK(ch->anim != Character::BuiltinAnimIndex("wave"));
    }
    {   // a state machine picks the clips: driveByMovement waits, and its blend doesn't overwrite blendTime
        Scene s("sm");
        GameObject* model = s.CreateGameObject("Model");
        s.CreateGameObject("bone")->transform->SetParent(model->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->clips = {MakeClip("Idle"), MakeClip("Attack")};
        ma->driveByMovement = true;
        ma->idleClip = "Idle";
        ma->blendTime = 0.4f;
        auto* sm = model->AddComponent<AnimStateMachine>();
        AnimStateMachine::State st;
        st.name = "Attack"; st.clip = "Attack";
        sm->states.push_back(st);
        s.Start();
        for (int i = 0; i < 5; ++i) s.Update(dt);
        CHECK(ma->CurrentName() == "Attack");
        CHECK(ma->StateMachineDrives());
        CHECK(std::fabs(ma->blendTime - 0.4f) < 1e-6f);
    }
    {   // an event at 0 fires right after Play and again after each loop wrap
        Scene s("events");
        GameObject* model = s.CreateGameObject("Model");
        s.CreateGameObject("bone")->transform->SetParent(model->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->autoPlay = false;
        ma->clips = {MakeClip("Step", {{0.0f, "start"}, {0.5f, "mid"}})};
        s.Start();
        ma->Play("Step");
        s.Update(0.1f);
        CHECK(Count(ma->ConsumeAnimEvents(), "start") == 1);
        for (int i = 0; i < 10; ++i) s.Update(0.1f);   // past 1.0: wraps once
        auto ev = ma->ConsumeAnimEvents();
        CHECK(Count(ev, "start") == 1);
        CHECK(Count(ev, "mid") == 1);
    }

    TEST_MAIN_RESULT();
}
