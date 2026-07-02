#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// Keyframing a clip (as the Animation editor does) drives the object's transform,
// including full euler rotation, and round-trips through serialization.
int main() {
    RUN_SUITE("animator_edit");

    {
        Scene s("A");
        GameObject* go = s.CreateGameObject("Mover");
        auto* an = go->AddComponent<Animator>();
        an->clip.loop = false;
        // Key position.x and rotation.y like the editor's "Key" button would.
        an->clip.AddKey("position.x", 0.0f, 0.0f);
        an->clip.AddKey("position.x", 1.0f, 10.0f);
        an->clip.AddKey("rotation.y", 0.0f, 0.0f);
        an->clip.AddKey("rotation.y", 1.0f, 90.0f);
        an->playing = false;

        an->SetTime(0.0f);
        CHECK_NEAR(go->transform->localPosition.x, 0.0f, 0.001f);
        an->SetTime(1.0f);
        CHECK_NEAR(go->transform->localPosition.x, 10.0f, 0.01f);
        // Full-euler rotation applied (Y axis) — proves rotation.y works, not just z.
        Quat r = go->transform->localRotation, e = Quat::Euler(0, 90, 0);
        float dot = r.x*e.x + r.y*e.y + r.z*e.z + r.w*e.w;
        CHECK(std::fabs(std::fabs(dot) - 1.0f) < 0.02f);

        // Serialize the authored clip and reload it.
        std::string text = SceneSerializer::Serialize(s);
        Scene b("B");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto* la = b.Find("Mover") ? b.Find("Mover")->GetComponent<Animator>() : nullptr;
        CHECK(la != nullptr);
        if (la) {
            CHECK(la->clip.HasTrack("position.x"));
            CHECK(la->clip.HasTrack("rotation.y"));
            la->playing = false; la->SetTime(1.0f);
            CHECK_NEAR(b.Find("Mover")->transform->localPosition.x, 10.0f, 0.01f);
        }
    }

    TEST_MAIN_RESULT();
}
