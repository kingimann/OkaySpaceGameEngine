#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("timescale");

    // --- Time::Step scales DeltaTime and advances ElapsedTime ---
    {
        Time::SetTimeScale(1.0f);
        float before = Time::ElapsedTime();
        Time::Step(0.1f);
        CHECK_NEAR(Time::DeltaTime(), 0.1f, 1e-4f);
        CHECK_NEAR(Time::UnscaledDeltaTime(), 0.1f, 1e-4f);
        CHECK(Time::ElapsedTime() > before);

        Time::SetTimeScale(0.5f);
        Time::Step(0.1f);
        CHECK_NEAR(Time::DeltaTime(), 0.05f, 1e-4f);        // scaled
        CHECK_NEAR(Time::UnscaledDeltaTime(), 0.1f, 1e-4f); // raw

        Time::SetTimeScale(0.0f); // paused
        Time::Step(0.1f);
        CHECK_NEAR(Time::DeltaTime(), 0.0f, 1e-4f);
        Time::SetTimeScale(1.0f); // restore for other suites
    }

    // --- A script can set/read the time scale (pause/slow-mo) ---
    {
        Scene scene("Pause");
        GameObject* go = scene.CreateGameObject("Mgr");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() { set_timescale(0.25); set_x(timescale()); }"));
        scene.Start();
        CHECK_NEAR(Time::TimeScale(), 0.25f, 1e-4f);
        CHECK_NEAR(go->transform->localPosition.x, 0.25f, 1e-4f);
        Time::SetTimeScale(1.0f);
    }

    TEST_MAIN_RESULT();
}
