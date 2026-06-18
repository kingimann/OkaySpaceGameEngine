#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("animation");

    // Curve: linear evaluation + clamp wrap.
    {
        AnimationCurve c = AnimationCurve::Linear(0.0f, 10.0f, 1.0f);
        CHECK_NEAR(c.Evaluate(0.0f), 0.0f, 1e-4);
        CHECK_NEAR(c.Evaluate(0.5f), 5.0f, 1e-4);
        CHECK_NEAR(c.Evaluate(1.0f), 10.0f, 1e-4);
        CHECK_NEAR(c.Evaluate(2.0f), 10.0f, 1e-4); // clamped
    }

    // Loop wrap.
    {
        AnimationCurve c;
        c.wrap = AnimationCurve::Wrap::Loop;
        c.smooth = false;
        c.AddKey(0.0f, 0.0f);
        c.AddKey(2.0f, 4.0f);
        CHECK_NEAR(c.Evaluate(1.0f), 2.0f, 1e-4);
        CHECK_NEAR(c.Evaluate(3.0f), 2.0f, 1e-4); // 3 wraps to 1
    }

    // Animator drives the Transform from clip tracks.
    {
        Scene scene("Anim");
        GameObject* go = scene.CreateGameObject("Mover");
        auto* an = go->AddComponent<Animator>();
        an->clip.loop = false;
        an->clip.SetCurve("position.x", AnimationCurve::Linear(0.0f, 10.0f, 1.0f));
        an->clip.SetCurve("position.y", AnimationCurve::Linear(0.0f, -4.0f, 1.0f));

        scene.physicsEnabled = false;
        scene.Start();
        for (int i = 0; i < 5; ++i) scene.Update(0.1f); // 0.5s -> halfway
        CHECK_NEAR(go->transform->localPosition.x, 5.0f, 0.1f);
        CHECK_NEAR(go->transform->localPosition.y, -2.0f, 0.1f);

        for (int i = 0; i < 10; ++i) scene.Update(0.1f); // past the end -> clamped
        CHECK_NEAR(go->transform->localPosition.x, 10.0f, 0.01f);
        CHECK_NEAR(go->transform->localPosition.y, -4.0f, 0.01f);
    }

    TEST_MAIN_RESULT();
}
