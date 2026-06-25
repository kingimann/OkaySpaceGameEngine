#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
using namespace okay;

// DayNightCycle advances a 24h clock and drives the sun light + camera sky from day
// to night, and round-trips through serialization.
int main() {
    RUN_SUITE("daynight");

    // --- Noon = bright day; midnight = dim night ------------------------
    {
        Scene s("cycle");
        GameObject* camObj = s.CreateGameObject("Cam");
        camObj->AddComponent<Camera>();
        GameObject* sun = s.CreateGameObject("Sun");
        auto* light = sun->AddComponent<Light>();
        GameObject* mgr = s.CreateGameObject("Time");
        auto* dn = mgr->AddComponent<DayNightCycle>();
        dn->paused = true;                          // drive time by hand
        dn->dayIntensity = 1.2f; dn->nightIntensity = 0.15f;
        s.Start();

        dn->SetTime(12.0f); s.Update(0.0f);         // noon
        CHECK(!dn->IsNight());
        CHECK(dn->Elevation() > 0.9f);
        CHECK(light->intensity > 1.0f);             // bright
        Color daySky = s.mainCamera->backgroundColor;

        dn->SetTime(0.0f); s.Update(0.0f);          // midnight
        CHECK(dn->IsNight());
        CHECK(light->intensity < 0.3f);             // dim moonlight
        Color nightSky = s.mainCamera->backgroundColor;
        CHECK(nightSky.b < daySky.b);               // night sky darker/less blue than day
        CHECK((nightSky.r + nightSky.g + nightSky.b) < (daySky.r + daySky.g + daySky.b));
    }

    // --- The clock advances with real time ------------------------------
    {
        Scene s("advance");
        s.CreateGameObject("Cam")->AddComponent<Camera>();
        auto* dn = s.CreateGameObject("Time")->AddComponent<DayNightCycle>();
        dn->dayLengthSeconds = 24.0f;               // 1 real second == 1 in-game hour
        dn->SetTime(10.0f);
        s.Start();
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);   // 2 real s -> +2h
        CHECK_NEAR(dn->Hour(), 12.0f, 0.2f);
    }

    // --- Time wraps past midnight ---------------------------------------
    {
        Scene s("wrap");
        auto* dn = s.CreateGameObject("Time")->AddComponent<DayNightCycle>();
        dn->SetTime(23.5f);
        dn->AddHours(2.0f);
        CHECK_NEAR(dn->Hour(), 1.5f, 1e-3f);        // wrapped around 24
    }

    // --- Serialization round-trip ---------------------------------------
    {
        Scene s("ser");
        GameObject* g = s.CreateGameObject("Time");
        auto* dn = g->AddComponent<DayNightCycle>();
        dn->dayLengthSeconds = 300.0f; dn->time = 18.5f; dn->rotateSun = false;
        dn->nightIntensity = 0.2f; dn->skyNight = Color::FromBytes(10, 20, 40);
        std::string txt = SceneSerializer::SerializeObject(*g);
        CHECK(txt.find("daynight ") != std::string::npos);
        Scene s2("ser2");
        GameObject* c2 = SceneSerializer::InstantiateFromText(s2, txt);
        auto* d2 = c2 ? c2->GetComponent<DayNightCycle>() : nullptr;
        CHECK(d2 != nullptr);
        CHECK_NEAR(d2->dayLengthSeconds, 300.0f, 1e-3f);
        CHECK_NEAR(d2->time, 18.5f, 1e-3f);
        CHECK(!d2->rotateSun);
        CHECK_NEAR(d2->nightIntensity, 0.2f, 1e-3f);
        CHECK_NEAR(d2->skyNight.b, Color::FromBytes(10, 20, 40).b, 0.02f);
    }

    TEST_MAIN_RESULT();
}
