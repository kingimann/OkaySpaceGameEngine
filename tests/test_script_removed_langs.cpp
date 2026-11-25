#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Testing/SceneValidator.hpp"
#include <algorithm>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::testing;

// The optional third-party Lua and Mono C# backends were removed. Lua runs again on
// our own interpreter (okay::lua), so an old scene naming "lua" loads and its script
// runs. An old scene naming the removed C# backend must still load (no crash), keep
// its script text, and say clearly that the language is no longer supported.
int main() {
    RUN_SUITE("script_removed_langs");

    const std::vector<std::string> langs = AvailableScriptLanguages();
    auto has = [&](const char* l) { return std::find(langs.begin(), langs.end(), l) != langs.end(); };
    CHECK(has("okayscript"));
    CHECK(has("lua"));
    CHECK(!has("csharp") && !has("cs"));
    CHECK(CreateScriptVM("lua") != nullptr);
    CHECK(RemovedScriptLanguageMessage("lua").empty());
    for (const char* lang : {"csharp", "cs"}) {
        CHECK(CreateScriptVM(lang) == nullptr);
        CHECK(RemovedScriptLanguageMessage(lang).find("no longer supported") != std::string::npos);
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
        a->AddComponent<ScriptComponent>("lua")->LoadSource("ticks = 0\nfunction update(dt) ticks = ticks + 1 end");
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
    if (la) {   // Lua runs again: the old script loads without an error
        CHECK(la->Language() == "lua");
        CHECK(la->Source() == "ticks = 0\nfunction update(dt) ticks = ticks + 1 end");
        std::string e;
        CHECK(la->LoadSource(la->Source(), &e));
        CHECK(e.empty());
        CHECK(la->VM() != nullptr);
    }
    if (lb) {
        CHECK(lb->Language() == "csharp");
        std::string e;
        CHECK(!lb->LoadSource(lb->Source(), &e));
        CHECK(e.find("no longer supported") != std::string::npos);
    }
    bool sawMsg = false;
    for (const auto& m : logged) if (m.find("no longer supported") != std::string::npos) sawMsg = true;
    CHECK(sawMsg);   // the Console shows it too, not just the return value

    // Running the scene runs the Lua script; the C# one does nothing and does not crash.
    loaded.Start();
    for (int i = 0; i < 5; ++i) loaded.Update(1.0f / 60.0f);
    if (la && la->VM()) CHECK(la->VM()->GetGlobal("ticks").AsFloat() == 5.0f);

    // The scene validator names the problem clearly (only the C# script now).
    ValidationReport rep = SceneValidator::Validate(loaded);
    CHECK(rep.CountCode("script.backend") == 1);
    bool clear = false;
    for (const auto& i : rep.issues)
        if (i.code == "script.backend" && i.message.find("no longer supported") != std::string::npos) clear = true;
    CHECK(clear);

    TEST_MAIN_RESULT();
}
