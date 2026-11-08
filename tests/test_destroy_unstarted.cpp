// Destroying an object before Start must not run OnDestroy. That callback
// assumes the component has been alive (a network manager closes its socket,
// a weather rig used to drop a count it had never taken). ReleaseUnstarted is
// the only hook, and it is a no-op unless the component grabbed something
// already: an IK target previewed in edit mode, or a weather rig whose count
// was taken at creation.
#include "test_framework.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Components/IKTarget.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/NetworkSync.hpp"
#include "okay/Environment/Weather.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"

using namespace okay;

namespace {

struct Probe : Component {
    static int destroys;
    static int releases;
    void OnDestroy() override { ++destroys; }
    void ReleaseUnstarted() override { ++releases; }
};
int Probe::destroys = 0;
int Probe::releases = 0;

} // namespace

int main() {
    RUN_SUITE("destroy_unstarted");

    // One component has started. Another is still pending. Destroying both
    // runs OnDestroy only for the one that started.
    {
        Probe::destroys = 0;
        Probe::releases = 0;
        Scene sc;
        GameObject* live = sc.CreateGameObject("Live");
        live->AddComponent<Probe>();
        sc.Start();
        GameObject* waiting = sc.CreateGameObject("Waiting");
        waiting->AddComponent<Probe>();
        sc.Destroy(live);
        sc.Destroy(waiting);
        sc.FlushDestroyed();
        CHECK(Probe::destroys == 1);
        CHECK(Probe::releases == 1);
    }

    // Two weather rigs. The live one owns a count. A spare rig that was never
    // counted, destroyed before Start, must not take the live count down. The
    // counted rig gives its own count back even if it never started.
    {
        const int base = WeatherRigCount();
        Scene live;
        live.renderSettings.weather.preset = Scene::RenderSettings::WeatherSettings::Rain;
        live.renderSettings.weather.intensity = 1.0f;
        UpdateWeather(live, 0.016f);
        CHECK(WeatherRigCount() == base + 1);
        live.Start();

        Scene other;
        other.renderSettings.weather.preset = Scene::RenderSettings::WeatherSettings::Rain;
        other.renderSettings.weather.intensity = 1.0f;
        UpdateWeather(other, 0.016f);
        CHECK(WeatherRigCount() == base + 2);
        GameObject* spare = other.CreateGameObject("SpareRig");
        spare->AddComponent<WeatherRig>();
        other.Destroy(spare);
        GameObject* pendingRig = other.Find(WeatherRig::kObjectName);
        CHECK(pendingRig != nullptr);
        other.Destroy(pendingRig);
        other.FlushDestroyed();
        CHECK(WeatherRigCount() == base + 1);

        live.Destroy(live.Find(WeatherRig::kObjectName));
        live.FlushDestroyed();
        CHECK(WeatherRigCount() == base);
    }

    // Edit mode: the target never receives Start, but Preview already aimed the
    // hand. Deleting the target lets the hand go.
    {
        Scene sc;
        GameObject* hero = sc.CreateGameObject("Hero");
        hero->AddComponent<FullBodyIK>();
        GameObject* tgt = sc.CreateGameObject("HandIK");
        auto* ik = tgt->AddComponent<IKTarget>();
        ik->character = hero;
        ik->limb = IKTarget::LeftHand;
        ik->blendTime = 0.0f;
        ik->weight = 1.0f;
        ik->Preview(0.016f);
        FullBodyIK* fb = hero->GetComponent<FullBodyIK>();
        CHECK(fb != nullptr);
        CHECK(fb->effectors[FullBodyIK::LeftHand].enabled);
        CHECK(fb->effectors[FullBodyIK::LeftHand].target == tgt->transform);
        sc.Destroy(tgt);
        sc.FlushDestroyed();
        CHECK(!fb->effectors[FullBodyIK::LeftHand].enabled);
        CHECK(fb->effectors[FullBodyIK::LeftHand].target == nullptr);
        CHECK(fb->effectors[FullBodyIK::LeftHand].targetName.empty());
    }

    // Strafe slot names survive a save, and a scene without that record does not
    // invent them. The animation sync tail stays optional.
    {
        Scene sc;
        auto* ma = sc.CreateGameObject("Model")->AddComponent<ModelAnimator>();
        ma->strafeLeftClip = "StrafeLeft";
        ma->walkBackClip = "WalkBack";
        std::string text = SceneSerializer::Serialize(sc);
        CHECK(text.find("modelanimdir") != std::string::npos);
        Scene loaded;
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        ModelAnimator* back = nullptr;
        for (const auto& up : loaded.Objects())
            if (auto* m = up->GetComponent<ModelAnimator>()) back = m;
        CHECK(back != nullptr);
        CHECK(back && back->strafeLeftClip == "StrafeLeft" && back->strafeRightClip.empty());
        CHECK(back && back->walkBackClip == "WalkBack" && back->runBackClip.empty());

        Scene plain;
        plain.CreateGameObject("Model")->AddComponent<ModelAnimator>();
        std::string old = SceneSerializer::Serialize(plain);
        CHECK(old.find("modelanimdir") == std::string::npos);
        Scene plainBack;
        CHECK(SceneSerializer::Deserialize(plainBack, old, &err));
        ModelAnimator* kept = nullptr;
        for (const auto& up : plainBack.Objects())
            if (auto* m = up->GetComponent<ModelAnimator>()) kept = m;
        CHECK(kept && kept->strafeLeftClip.empty() && kept->walkBackClip.empty());

        AnimSyncView cut;
        CHECK(ParseAnimSync("2 0 1.5 30 4", cut));
        CHECK(cut.haveGait && !cut.haveAction && !cut.haveParam);
        AnimSyncView full;
        CHECK(ParseAnimSync("2 0 1.5 30 7 2 -10 4 3", full));
        CHECK(full.haveAction && full.haveParam && full.actionId == 2);
        CHECK(std::fabs(full.actionParam - 3.0f) < 1e-4f);
    }

    TEST_MAIN_RESULT();
}
