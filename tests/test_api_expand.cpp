#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

static AnimClip wave() {
    AnimClip c; c.name = "wave"; c.loop = false;
    AnimKey k0; k0.time = 0.0f; k0.pose.assign(Character::BoneCount(), Vec3{0, 0, 0});
    AnimKey k1; k1.time = 1.0f; k1.pose.assign(Character::BoneCount(), Vec3{0, 0, 0});
    c.keys = {k0, k1};
    return c;
}

int main() {
    RUN_SUITE("api_expand");

    // ---- Input API ----
    Input::FeedKeys({});
    Input::FeedKeys({'a'});
    CHECK(Input::AnyKey());
    CHECK(Input::AnyKeyDown());            // 'a' is newly down this frame
    Input::FeedKeys({'a'});
    CHECK(Input::AnyKey());
    CHECK(!Input::AnyKeyDown());           // still held, not a fresh press
    Input::FeedKeys({});
    CHECK(!Input::AnyKey());

    Input::FeedMouse({10.0f, 10.0f}, 0);
    Input::FeedMouse({15.0f, 18.0f}, 0);
    CHECK(std::fabs(Input::MouseDelta().x - 5.0f) < 1e-4f);
    CHECK(std::fabs(Input::MouseDelta().y - 8.0f) < 1e-4f);

    Input::FeedGamepad({0, 0}, 1u << 0);   // A pressed
    Input::FeedGamepad({0, 0}, 0);         // A released
    CHECK(Input::GetGamepadButtonUp(0));

    // ---- Animation API ----
    Scene s("A");
    auto* ch = s.CreateGameObject("Hero")->AddComponent<Character>();
    ch->Apply();
    ch->AddClip(wave());
    CHECK(ch->HasClip("wave"));
    CHECK(!ch->HasClip("nope"));
    CHECK(ch->ClipCount() == 1);
    CHECK(std::fabs(ch->ClipDuration("wave") - 1.0f) < 1e-4f);
    CHECK(ch->ClipDuration("nope") == 0.0f);

    ch->PlayClip("wave");
    s.Start();
    for (int i = 0; i < 6; ++i) s.Update(0.1f);   // ~0.6s in
    CHECK(ch->ClipTime() > 0.3f && ch->ClipTime() < 0.9f);
    CHECK(ch->ClipNormalizedTime() > 0.3f && ch->ClipNormalizedTime() < 0.9f);
    CHECK(!ch->ClipFinished());
    for (int i = 0; i < 8; ++i) s.Update(0.1f);   // run past the end (non-loop clamps)
    CHECK(ch->ClipFinished());
    CHECK(std::fabs(ch->ClipNormalizedTime() - 1.0f) < 1e-4f);

    // AnimClip::AddEvent keeps events sorted by time.
    AnimClip c = wave();
    c.AddEvent(0.8f, "late");
    c.AddEvent(0.2f, "early");
    CHECK(c.events.size() == 2);
    CHECK(c.events[0].name == "early" && c.events[1].name == "late");

    // ---- UI text: Best Fit (auto-size) ----
    {
        TextRenderer tr;
        tr.screenSpace = true;
        tr.size = {220.0f, 48.0f};
        tr.pixelSize = 1.0f;
        tr.wrap = false;
        tr.autoSize = false;
        CHECK(tr.EffectivePixelSize() == 1.0f);          // off -> the plain pixelSize

        tr.autoSize = true; tr.autoSizeMin = 0.02f; tr.autoSizeMax = 100.0f;
        tr.text = "Hi";
        float big = tr.EffectivePixelSize();
        CHECK(big > 1.0f);                               // short text grows to fill the box
        tr.text = std::string(60, 'W');
        float small = tr.EffectivePixelSize();
        CHECK(small < big);                              // long text shrinks
        CHECK(small >= tr.autoSizeMin);                  // never below the floor
        tr.wrap = true;
        CHECK(tr.EffectivePixelSize() == tr.pixelSize);  // ignored while wrapping
    }

    // Best-fit settings round-trip through the scene file.
    {
        Scene a("A");
        auto* t = a.CreateGameObject("Label")->AddComponent<TextRenderer>();
        t->autoSize = true; t->autoSizeMin = 0.5f; t->autoSizeMax = 9.0f;
        std::string text = SceneSerializer::Serialize(a);
        Scene b("B");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto* lt = b.Find("Label") ? b.Find("Label")->GetComponent<TextRenderer>() : nullptr;
        CHECK(lt != nullptr);
        if (lt) {
            CHECK(lt->autoSize);
            CHECK(std::fabs(lt->autoSizeMin - 0.5f) < 1e-4f);
            CHECK(std::fabs(lt->autoSizeMax - 9.0f) < 1e-4f);
        }
    }

    TEST_MAIN_RESULT();
}
