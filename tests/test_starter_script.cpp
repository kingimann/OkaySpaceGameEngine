#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scripting/StarterScripts.hpp"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;

// The script a new script file starts with: an empty Start and Update, like a
// fresh Unity MonoBehaviour. It must load in the language's own script engine,
// run without a complaint, and stay out of the way (no movement, no logging).
int main() {
    RUN_SUITE("starter_script");
    std::vector<std::string> errs, logs;
    Log::sink = [&](Log::Level l, const std::string& m) {
        if (l >= Log::Level::Error) errs.push_back(m); else logs.push_back(m);
    };

    for (const std::string& lang : AvailableScriptLanguages()) {
        std::printf("-- %s\n", lang.c_str());
        const std::string src = StarterScriptSource(lang);
        CHECK(!src.empty());
        CHECK(src.find("Start") != std::string::npos || src.find("start") != std::string::npos);
        CHECK(src.find("Update") != std::string::npos || src.find("update") != std::string::npos);
        CHECK(std::count(src.begin(), src.end(), '\n') <= 16);    // simple: a screenful at most

        errs.clear(); logs.clear();
        Scene scene("S");
        GameObject* go = scene.CreateGameObject("Thing");
        go->transform->localPosition = {1.0f, 2.0f, 3.0f};
        auto* sc = go->AddComponent<ScriptComponent>(lang);
        std::string error;
        CHECK(sc->LoadSource(src, &error));
        if (!error.empty()) std::printf("    load error: %s\n", error.c_str());
        scene.Start();
        for (int i = 0; i < 10; ++i) scene.Update(1.0f / 60.0f);
        CHECK(errs.empty());
        for (const auto& e : errs) std::printf("    error: %s\n", e.c_str());
        CHECK(logs.empty());                                       // says nothing
        CHECK_NEAR(go->transform->localPosition.x, 1.0f, 1e-5f);   // moves nothing
        CHECK_NEAR(go->transform->localPosition.y, 2.0f, 1e-5f);
        CHECK_NEAR(go->transform->localPosition.z, 3.0f, 1e-5f);
    }
    TEST_MAIN_RESULT();
}
