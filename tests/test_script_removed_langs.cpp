#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Testing/SceneValidator.hpp"
#include <algorithm>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::testing;

// The optional third-party Lua and Mono C# backends were removed, and both names are languages
// again: "lua" runs on our own interpreter (okay::lua, feature map 13.3) and "csharp" / "cs" is real
// C# through the hosted .NET runtime (13.9). So no language is listed as removed any more. An old
// scene naming Lua loads and its script runs; an old Mono-style C# script still loads without a crash
// and says plainly why it does not run; a name no build knows is reported as not available.
int main() {
    RUN_SUITE("script_removed_langs");

    const std::vector<std::string> langs = AvailableScriptLanguages();
    auto has = [&](const char* l) { return std::find(langs.begin(), langs.end(), l) != langs.end(); };
    const bool hostsDotnet = has("csharp");
    CHECK(has("okayscript"));
    CHECK(has("lua"));
    CHECK(langs.size() == (hostsDotnet ? 3u : 2u));
    CHECK(CreateScriptVM("lua") != nullptr);
    for (const char* lang : {"lua", "csharp", "cs", "okayscript", "nonsense"})
        CHECK(RemovedScriptLanguageMessage(lang).empty());   // nothing is "no longer supported"
    for (const char* lang : {"csharp", "cs"}) CHECK(CreateScriptVM(lang) != nullptr);

    std::vector<std::string> logged;
    Log::sink = [&](Log::Level l, const std::string& m) { if (l >= Log::Level::Error) logged.push_back(m); };

    // Build a scene as an old version would have saved it: a Lua script, a Mono-style C# script and a
    // language no build has.
    std::string text;
    {
        Scene scene("Old");
        GameObject* a = scene.CreateGameObject("LuaThing");
        a->AddComponent<ScriptComponent>("lua")->LoadSource("ticks = 0\nfunction update(dt) ticks = ticks + 1 end");
        GameObject* b = scene.CreateGameObject("CsThing");
        b->AddComponent<ScriptComponent>("csharp")->LoadSource("class Script { void Update(float dt) {} }");
        GameObject* c = scene.CreateGameObject("OddThing");
        c->AddComponent<ScriptComponent>("brainfreeze")->SetSource("+++");
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
        CHECK(!lb->LoadSource(lb->Source(), &e));   // an old Mono script: no Behaviour class (or C# not in this build)
        CHECK(e.find("CsThing") != std::string::npos);
        CHECK(e.find("Behaviour") != std::string::npos || e.find("not enabled in this build") != std::string::npos);
    }
    for (const auto& m : logged) CHECK(m.find("no longer supported") == std::string::npos);

    // Running the scene runs the Lua script; the C# one does nothing and does not crash.
    loaded.Start();
    for (int i = 0; i < 5; ++i) loaded.Update(1.0f / 60.0f);
    if (la && la->VM()) CHECK(la->VM()->GetGlobal("ticks").AsFloat() == 5.0f);

    // The scene validator: only the unknown language is a missing backend (the C# one is a compile problem).
    ValidationReport rep = SceneValidator::Validate(loaded);
    CHECK(rep.CountCode("script.backend") == 1);
    bool clear = false;
    for (const auto& i : rep.issues)
        if (i.code == "script.backend" && i.message.find("'brainfreeze' is not available") != std::string::npos) clear = true;
    CHECK(clear);

    TEST_MAIN_RESULT();
}
