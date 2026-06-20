#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// Keyframe Animator: clip drives the Transform, and clips survive a round-trip.
int main() {
    RUN_SUITE("animator");

    // --- A clip moves the transform from 0 to 10 over 2s on position.x --------
    {
        Scene s("Anim"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Mover");
        auto* an = go->AddComponent<Animator>();
        an->clip.name = "Slide";
        an->clip.loop = false;
        an->clip.AddKey("position.x", 0.0f, 0.0f);
        an->clip.AddKey("position.x", 2.0f, 10.0f);
        CHECK_NEAR(an->clip.Length(), 2.0f, 0.001f);

        an->SetTime(0.0f);
        CHECK_NEAR(go->transform->localPosition.x, 0.0f, 0.001f);
        an->SetTime(2.0f);
        CHECK_NEAR(go->transform->localPosition.x, 10.0f, 0.01f);
    }

    // --- AddKey replaces a key at the same time (no duplicates) ----------------
    {
        AnimationClip c; c.loop = false;
        c.AddKey("scale.x", 1.0f, 2.0f);
        c.AddKey("scale.x", 1.0f, 5.0f);     // overwrite
        bool f; CHECK_NEAR(c.Evaluate("scale.x", 1.0f, f), 5.0f, 0.001f);
        CHECK(c.Track("scale.x") && c.Track("scale.x")->Count() == 1);
    }

    // --- Serialize + deserialize the Animator (clip + keys) --------------------
    {
        Scene s("A"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Spin");
        auto* an = go->AddComponent<Animator>();
        an->speed = 1.5f; an->clip.name = "Turn"; an->clip.loop = true;
        an->clip.AddKey("rotation.z", 0.0f, 0.0f);
        an->clip.AddKey("rotation.z", 1.0f, 90.0f);
        an->clip.AddKey("position.y", 0.0f, 0.0f);
        an->clip.AddKey("position.y", 1.0f, 3.0f);

        std::string text = SceneSerializer::Serialize(s);
        Scene loaded("L");
        CHECK(SceneSerializer::Deserialize(loaded, text));
        GameObject* lg = loaded.Find("Spin");
        CHECK(lg != nullptr);
        auto* la = lg ? lg->GetComponent<Animator>() : nullptr;
        CHECK(la != nullptr);
        if (la) {
            CHECK_NEAR(la->speed, 1.5f, 0.001f);
            CHECK(la->clip.name == "Turn");
            CHECK(la->clip.loop == true);
            CHECK(la->clip.HasTrack("rotation.z"));
            CHECK(la->clip.HasTrack("position.y"));
            // Midpoint of a 0->90 smoothstep curve is 45 (t=1.0 would loop-wrap to 0).
            bool f; CHECK_NEAR(la->clip.Evaluate("rotation.z", 0.5f, f), 45.0f, 0.01f);
        }
    }

    TEST_MAIN_RESULT();
}
