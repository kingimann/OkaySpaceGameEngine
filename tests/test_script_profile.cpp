#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Script/Format.hpp"
#include "okay/Scripting/ScriptProfile.hpp"
#include "okay/Scripting/StarterScripts.hpp"
#include <string>
#include <vector>

using namespace okay;

int main() {
    RUN_SUITE("script_profile");

    // ---- profile model ----
    {
        scriptprof::Table t;
        t.Record("A.okay", "update", 2.0); t.Record("A.okay", "update", 4.0);
        t.Record("B.okay", "update", 1.0);
        t.Record("A.okay", "work", 3.0, false);   // nested: must not be added to the script total again
        auto rows = t.Rows();
        CHECK(rows.size() == 3);
        auto per = t.PerScript();
        CHECK(per.size() == 2 && per[0].script == "A.okay" && per[0].totalMs == 6.0 && per[0].calls == 2);
        CHECK(per[0].maxMs == 4.0 && per[0].avgMs() == 3.0);
        scriptprof::Table::Sort(rows, scriptprof::SortKey::Total, true);
        CHECK(rows[0].totalMs == 6.0);
        scriptprof::Table::Sort(rows, scriptprof::SortKey::Calls, false);
        CHECK(rows[0].calls == 1);
        CHECK(t.Summary(1).find("A.okay") != std::string::npos);
        t.Reset(); CHECK(t.Rows().empty());
    }

    // ---- per-script and per-function timing through a real script ----
    for (const std::string& lang : AvailableScriptLanguages()) {
        scriptprof::Global().Reset();
        scriptprof::SetEnabled(true);
        Scene scene("S");
        GameObject* go = scene.CreateGameObject("Thing");
        auto* sc = go->AddComponent<ScriptComponent>(lang);
        std::string src = lang == "okayscript"
            ? "var n = 0;\nfunction work() { n = n + 1; }\nfunction update(dt) { work(); work(); }\n"
            : "int n = 0;\nvoid Work() { n = n + 1; }\nvoid Update() { Work(); Work(); }\n";
        std::string err;
        CHECK(sc->LoadSource(src, &err));
        scene.Start();
        for (int i = 0; i < 5; ++i) scene.Update(1.0f / 60.0f);
        scriptprof::SetEnabled(false);
        bool sawUpdate = false, sawFn = false;
        for (const auto& r : scriptprof::Global().Rows()) {
            std::printf("    [%s] %s %s entry=%d calls=%lld\n", lang.c_str(), r.script.c_str(), r.function.c_str(), (int)r.entry, r.calls);
            if (r.entry && r.calls == 5 && (r.function == "update")) sawUpdate = true;
            if (!r.entry && r.calls == 10 && (r.function == "work" || r.function == "Work")) sawFn = true;
        }
        CHECK(sawUpdate);
        scriptprof::Global().Reset();
        scene.Update(0.016f);
        CHECK(scriptprof::Global().Rows().empty());   // off: nothing recorded
    }

    TEST_MAIN_RESULT();
}
