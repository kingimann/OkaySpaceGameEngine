#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// Animation events: a clip carries named markers; as it plays past one the Character
// fires it (callback + polled queue). They re-fire every loop and survive save/load.
static AnimClip walkWithSteps() {
    AnimClip c; c.name = "walk"; c.loop = true;
    AnimKey k0; k0.time = 0.0f; k0.pose.assign(Character::BoneCount(), Vec3{0, 0, 0});
    AnimKey k1; k1.time = 1.0f; k1.pose.assign(Character::BoneCount(), Vec3{0, 0, 0});
    c.keys = {k0, k1};
    c.events = { {0.32f, "step"}, {0.68f, "step"} };   // two footsteps per 1s loop
    return c;
}

static int countSteps(const std::vector<std::string>& v) {
    int n = 0; for (auto& s : v) if (s == "step") ++n; return n;
}

int main() {
    RUN_SUITE("char_anim_events");

    // Polled queue: draining ConsumeAnimEvents each frame catches both steps in one loop.
    {
        Scene s("A");
        auto* ch = s.CreateGameObject("Hero")->AddComponent<Character>();
        ch->Apply(); ch->separateParts = true;
        ch->AddClip(walkWithSteps());
        ch->PlayClip("walk");
        s.Start();
        int steps = 0;
        for (int i = 0; i < 20; ++i) { s.Update(0.05f); steps += countSteps(ch->ConsumeAnimEvents()); }
        CHECK(steps == 2);
    }

    // NextAnimEvent pops one fired event at a time, then "" when drained.
    {
        Scene s("A");
        auto* ch = s.CreateGameObject("Hero")->AddComponent<Character>();
        ch->Apply(); ch->separateParts = true;
        ch->AddClip(walkWithSteps());
        ch->PlayClip("walk");
        s.Start();
        int steps = 0; std::string e;
        for (int i = 0; i < 20; ++i) {
            s.Update(0.05f);
            while (!(e = ch->NextAnimEvent()).empty()) if (e == "step") ++steps;
        }
        CHECK(steps == 2);
        CHECK(ch->NextAnimEvent().empty());   // queue drained
    }

    // Callback + loop: over two full loops the markers fire four times.
    {
        Scene s("A");
        auto* ch = s.CreateGameObject("Hero")->AddComponent<Character>();
        ch->Apply(); ch->separateParts = true;
        std::vector<std::string> fired;
        ch->onAnimEvent = [&](const std::string& n) { fired.push_back(n); };
        ch->AddClip(walkWithSteps());
        ch->PlayClip("walk");
        s.Start();
        for (int i = 0; i < 40; ++i) s.Update(0.05f);   // 2.0s = two loops
        CHECK(countSteps(fired) == 4);
    }

    // Round-trip: events survive serialization (verified behaviourally — they still fire).
    {
        Scene s("A");
        auto* ch = s.CreateGameObject("Hero")->AddComponent<Character>();
        ch->Apply(); ch->separateParts = true;
        ch->AddClip(walkWithSteps());

        std::string text = SceneSerializer::Serialize(s);
        Scene b("B");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto* lch = b.Find("Hero") ? b.Find("Hero")->GetComponent<Character>() : nullptr;
        CHECK(lch != nullptr);
        if (lch) {
            int steps = 0;
            lch->onAnimEvent = [&](const std::string& n) { if (n == "step") ++steps; };
            lch->PlayClip("walk");
            b.Start();
            for (int i = 0; i < 20; ++i) b.Update(0.05f);
            CHECK(steps == 2);
        }
    }

    TEST_MAIN_RESULT();
}
