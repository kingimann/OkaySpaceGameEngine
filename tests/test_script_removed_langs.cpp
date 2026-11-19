#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Testing/SceneValidator.hpp"
#include <string>
#include <vector>

using namespace okay;
using namespace okay::testing;

// The optional third-party Mono C# backend was removed (and the third-party Lua with it; Lua is
// back as our own, engine/src/Lua). An old scene that names a removed language must still load
// (no crash), keep its script text, and say clearly that the language is no longer supported,
// instead of failing silently. "lua" now works and is not a removed language.
int main() {
    RUN_SUITE("script_removed_langs");

    CHECK(AvailableScriptLanguages().size() == 2);   // okayscript, lua
    CHECK(CreateScriptVM("lua") != nullptr);
    CHECK(RemovedScriptLanguageMessage("lua").empty());
    // "csharp" is a language again (hosted .NET, rule 24c). With OKAY_WITH_CSHARP off its VM exists but says so.
    for (const char* lang : {"csharp", "cs"}) {
        auto vm = CreateScriptVM(lang);
        CHECK(vm != nullptr);
        CHECK(RemovedScriptLanguageMessage(lang).empty());
#ifndef OKAY_WITH_CSHARP
        std::string e;
        if (vm) CHECK(!vm->Load("public class S : Behaviour { }", &e));
        CHECK(e.find("C# is not enabled in this build") != std::string::npos);
#endif
    }
    CHECK(RemovedScriptLanguageMessage("okayscript").empty());
    CHECK(RemovedScriptLanguageMessage("nonsense").empty());

    std::vector<std::string> logged;
    Log::sink = [&](Log::Level l, const std::string& m) { if (l >= Log::Level::Error) logged.push_back(m); };

    // Build a scene as an old version would have saved it: a Lua script (still supported) and a C# script.
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
        CHECK(la->LoadSource(la->Source(), &e));     // our own Lua runs it now
        CHECK(e.empty());
    }
    if (lb) {
        CHECK(lb->Language() == "csharp");
        std::string e;
        CHECK(!lb->LoadSource(lb->Source(), &e));       // this old script has no Behaviour class (and C# may be off)
        CHECK(!e.empty());
    }
    bool sawMsg = false;
    for (const auto& m : logged) if (!m.empty()) sawMsg = true;
    CHECK(sawMsg);   // the Console shows it too, not just the return value

    // Running the scene does nothing and does not crash.
    loaded.Start();
    for (int i = 0; i < 5; ++i) loaded.Update(1.0f / 60.0f);

    // The scene validator names the problem clearly.
    ValidationReport rep = SceneValidator::Validate(loaded);
    CHECK(rep.CountCode("script.backend") == 1);   // only the C# one
    bool clear = false;
    for (const auto& i : rep.issues)
        if (i.code == "script.backend" && i.message.find("no longer supported") != std::string::npos) clear = true;
    CHECK(clear);

    TEST_MAIN_RESULT();
}
