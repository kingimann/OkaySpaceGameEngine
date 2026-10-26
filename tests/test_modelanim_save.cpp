#include "test_framework.hpp"
#include <Okay.hpp>
#include <string>

using namespace okay;

// An imported clip plays its keys linearly (smoothKeys = false). Saving and
// reopening the scene used to rebuild it with the cubic default, so an
// imported walk changed shape after Save + Reopen.
int main() {
    RUN_SUITE("modelanim_save");

    for (bool smooth : {false, true}) {
        Scene s("A"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Model");
        auto* ma = g->AddComponent<ModelAnimator>();
        ModelAnimator::Clip clip;
        clip.name = "Walk";
        AnimationClip ac; ac.smoothKeys = smooth;
        for (float t : {0.0f, 0.5f, 1.0f, 1.5f}) ac.AddKey("position.y", t, t == 0.5f ? 1.0f : 0.0f);
        clip.nodes.push_back({"Hips", ac});
        ma->clips.push_back(clip);

        Scene s2("B"); s2.physicsEnabled = false;
        CHECK(SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s)));
        auto* ma2 = s2.Find("Model") ? s2.Find("Model")->GetComponent<ModelAnimator>() : nullptr;
        CHECK(ma2 && ma2->clips.size() == 1);
        if (!ma2 || ma2->clips.empty()) continue;
        const AnimationClip& back = ma2->clips[0].nodes[0].clip;
        for (float t : {0.2f, 0.7f, 1.2f}) {
            bool f1 = false, f2 = false;
            CHECK_NEAR(back.Evaluate("position.y", t, f2), ac.Evaluate("position.y", t, f1), 1e-5f);
        }
    }

    TEST_MAIN_RESULT();
}
