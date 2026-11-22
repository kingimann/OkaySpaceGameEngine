#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Testing/SceneValidator.hpp"
#include <string>
#include <vector>

using namespace okay;
using namespace okay::testing;

// The optional Lua and Mono C# backends were removed. An old scene that names one of
// those languages must still load (no crash), keep its script text, and say clearly
// what is wrong instead of failing silently.
// Since C# became our own language (the typed OkayScript under the ids "csharp" / "cs",
// see test_csharp_scripts), a C# script is no longer "removed": it runs if it compiles
// and otherwise reports the checker's error. Lua is still removed here.
int main() {
    RUN_SUITE("script_removed_langs");

    {
        bool okay = false;
        for (const auto& l : AvailableScriptLanguages()) if (l == "okayscript") okay = true;
        CHECK(okay);
    }
    CHECK(CreateScriptVM("lua") == nullptr);
    CHECK(RemovedScriptLanguageMessage("lua").find("no longer supported") != std::string::npos);
    for (const char* lang : {"csharp", "cs"}) {
        CHECK(CreateScriptVM(lang) != nullptr);
        CHECK(RemovedScriptLanguageMessage(lang).empty());
    }
    CHECK(RemovedScriptLanguageMessage("okayscript").empty());
    CHECK(RemovedScriptLanguageMessage("nonsense").empty());

    std::vector<std::string> logged;
    Log::sink = [&](Log::Level l, const std::string& m) { if (l >= Log::Level::Error) logged.push_back(m); };

    // Build a scene as an old version would have saved it: a Lua script and a C# script.
    std::string text;
    {
        Scene scene("Old");
        GameObject* a = scene.CreateGameObject("LuaThing");
        a->AddComponent<ScriptComponent>("lua")->LoadSource("function update(dt) end");
        GameObject* b = scene.CreateGameObject("CsThing");
        b->AddComponent<ScriptComponent>("csharp")->LoadSource("class Script { void Update(float dt) {} }");
        text = SceneSerializer::Serialize(scene);
    }
    CHECK(text.find("\"lua\"") != std::string::npos);
    logged.clear();

    // Load it back: no crash, the scripts stay on their objects with their text.
    Scene loaded("Loaded");
    std::string err;
    CHECK(SceneSerializer::Deserialize(loaded, text, &err));
    auto* la = loaded.Find("LuaThing") ? loaded.Find("LuaThing")->GetComponent<ScriptComponent>() : nullptr;
    auto* lb = loaded.Find("CsThing") ? loaded.Find("CsThing")->GetComponent<ScriptComponent>() : nullptr;
    CHECK(la != nullptr);
    CHECK(lb != nullptr);
    if (la) {
        CHECK(la->Language() == "lua");
        CHECK(la->Source() == "function update(dt) end");
        std::string e;
        CHECK(!la->LoadSource(la->Source(), &e));
        CHECK(e.find("no longer supported") != std::string::npos);
        CHECK(e.find("lua") != std::string::npos);
    }
    if (lb) {
        CHECK(lb->Language() == "csharp");
        std::string e;
        CHECK(!lb->LoadSource(lb->Source(), &e));        // no class extends MonoBehaviour / Behaviour
        CHECK(e.find("no longer supported") == std::string::npos);
        CHECK(e.find("line") != std::string::npos);      // the checker's error, with its line
    }
    bool sawMsg = false;
    for (const auto& m : logged) if (m.find("no longer supported") != std::string::npos) sawMsg = true;
    CHECK(sawMsg);   // the Console shows it too, not just the return value

    // Running the scene does nothing and does not crash.
    loaded.Start();
    for (int i = 0; i < 5; ++i) loaded.Update(1.0f / 60.0f);

    // The scene validator names the problem clearly.
    ValidationReport rep = SceneValidator::Validate(loaded);
    CHECK(rep.CountCode("script.backend") == 1);   // the Lua one
    CHECK(rep.CountCode("script.compile") == 1);   // the C# one, with the checker's reason
    bool clear = false;
    for (const auto& i : rep.issues)
        if (i.code == "script.backend" && i.message.find("no longer supported") != std::string::npos) clear = true;
    CHECK(clear);

    TEST_MAIN_RESULT();
}
