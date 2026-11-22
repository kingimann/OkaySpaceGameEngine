#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scripting/NextScriptVM.hpp"
#include "okay/Scripting/ScriptConvert.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

// The OkayScript -> C# converter (Script Editor > File > Convert to C#): every shipped
// old script converts and compiles in C# mode with no errors, and the shipped scenes
// behave the same with their scripts converted (same frames, keys and UI events, the
// same saved scene afterwards).

#ifndef OKAY_SOURCE_DIR
#define OKAY_SOURCE_DIR "."
#endif

static const float kDt = 1.0f / 60.0f;

static std::string ReadText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}
static std::vector<ScriptDiagnostic> CsDiags(const std::string& src) {
    auto vm = CreateScriptVM("csharp");
    return vm->ValidateAll(src);
}
static int Errors(const std::vector<ScriptDiagnostic>& d) {
    return (int)std::count_if(d.begin(), d.end(), [](const ScriptDiagnostic& x) { return x.severity == 0; });
}

// The shipped scenes that carry old scripts, and the plugin scripts.
static const char* kScenes[] = {
    "examples/camera/camera_2d_rooms.okayscene",  "examples/camera/camera_rigs.okayscene",
    "examples/input/input_playground.okayscene",  "examples/physics/script_object_refs.okayscene",
    "examples/security_paths/script_paths.okayscene", "examples/touch_controls/touch_controls.okayscene",
    "examples/ui_library/ui_library_2.okayscene", "examples/ui_library/ui_library_4.okayscene",
    "examples/ui_library/ui_mix_styles.okayscene", "examples/ui_library/ui_starter_pack.okayscene",
    "examples/ui_text/ui_text.okayscene",
};
static const char* kPlugins[] = {"examples/editor-plugins/AlignTools.okay", "examples/editor-plugins/CleanupNames.okay"};

static void Basics() {
    auto r = ConvertOkayScriptToCSharp(
        "// Walk with W A S D.\n"
        "var speed = 4\n"
        "var label = \"hi\"\n"
        "var count = 0\n"
        "function start() { count = 1; }\n"
        "function update(dt) {\n"
        "  var s = speed * dt\n"
        "  if (key(\"d\")) { set_x(pos_x() + s) }\n"
        "  count++\n"
        "  label = \"n=\" + count\n"
        "}\n"
        "function twice(x) { return x * 2; }\n"
        "function on_click() { print(\"clicked\"); count = twice(count) + sin(0); }\n",
        "Walker");
    CHECK(r.ok);
    std::printf("%s\n", r.csharp.c_str());
    CHECK(r.csharp.find("public class Walker : MonoBehaviour") != std::string::npos);
    CHECK(r.csharp.find("public float speed = 4;") != std::string::npos);
    CHECK(r.csharp.find("public string label = \"hi\";") != std::string::npos);
    CHECK(r.csharp.find("void Update()") != std::string::npos);
    CHECK(r.csharp.find("Time.deltaTime") != std::string::npos);
    CHECK(r.csharp.find("Debug.Log(\"clicked\")") != std::string::npos);
    CHECK(r.csharp.find("Mathf.Sin(0)") != std::string::npos);
    CHECK(r.csharp.find("OkayScript.Bool(\"key\", \"d\")") != std::string::npos);
    CHECK(std::find(r.bridged.begin(), r.bridged.end(), "set_x") != r.bridged.end());
    auto d = CsDiags(r.csharp);
    for (auto& x : d) std::printf("    %s %d: %s\n", x.severity ? "warning" : "error", x.line, x.message.c_str());
    CHECK(Errors(d) == 0);

    // It runs and does what the old one did.
    for (const char* lang : {"okayscript", "csharp"}) {
        Scene scene("S");
        GameObject* go = scene.CreateGameObject("W");
        auto* sc = go->AddComponent<ScriptComponent>(lang);
        std::string err;
        CHECK(sc->LoadSource(std::string(lang) == "csharp" ? r.csharp :
            "var speed = 4\nvar label = \"hi\"\nvar count = 0\nfunction start() { count = 1; }\n"
            "function update(dt) {\n  var s = speed * dt\n  if (key(\"d\")) { set_x(pos_x() + s) }\n  count++\n  label = \"n=\" + count\n}\n"
            "function twice(x) { return x * 2; }\n"
            "function on_click() { print(\"clicked\"); count = twice(count) + sin(0); }\n", &err));
        scene.Start();
        for (int i = 0; i < 30; ++i) { Input::FeedKeys({'d'}); scene.Update(kDt); }
        Input::FeedKeys({});
        sc->VM()->CallEvent("on_click");
        std::printf("    %s: x=%.4f count=%.1f label=%s\n", lang, go->transform->localPosition.x,
                    sc->VM()->GetGlobal("count").AsFloat(), sc->VM()->GetGlobal("label").AsString().c_str());
        CHECK_NEAR(go->transform->localPosition.x, 2.0f, 0.02f);
        CHECK_NEAR(sc->VM()->GetGlobal("count").AsFloat(), 62.0f, 1e-4f);
        CHECK(sc->VM()->GetGlobal("label").AsString() == "n=31");
    }

    // A script with a mistake isn't converted, and says why.
    auto bad = ConvertOkayScriptToCSharp("function update(dt) { if ( }", "Bad");
    CHECK(!bad.ok);
    CHECK(!bad.error.empty());
}

// Every old script in the shipped scenes and plugins converts and compiles.
struct Found { std::string where; std::string source; };
static std::vector<Found> ShippedScripts() {
    std::vector<Found> out;
    std::regex re("script \"okayscript\" \"((?:[^\"\\\\]|\\\\.)*)\"");
    for (const char* rel : kScenes) {
        std::string text = ReadText(std::string(OKAY_SOURCE_DIR) + "/" + rel);
        CHECK(!text.empty());
        int n = 0;
        for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) {
            std::string raw = (*it)[1].str(), src;
            for (std::size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == '\\' && i + 1 < raw.size()) {
                    char c = raw[++i];
                    src += c == 'n' ? '\n' : c == 't' ? '\t' : c;
                } else src += raw[i];
            }
            out.push_back({std::string(rel) + " #" + std::to_string(++n), src});
        }
    }
    for (const char* rel : kPlugins) out.push_back({rel, ReadText(std::string(OKAY_SOURCE_DIR) + "/" + rel)});
    return out;
}

static void EveryShippedScript() {
    auto all = ShippedScripts();
    std::printf("    %zu shipped old scripts\n", all.size());
    CHECK(all.size() >= 61);
    int clean = 0;
    std::vector<std::string> bridgedAll;
    for (const Found& f : all) {
        auto r = ConvertOkayScriptToCSharp(f.source, "Converted");
        if (!r.ok) { std::printf("    %s: %s\n", f.where.c_str(), r.error.c_str()); CHECK(r.ok); continue; }
        auto d = CsDiags(r.csharp);
        if (Errors(d) == 0) { ++clean; }
        else {
            std::printf("  --- %s did not compile:\n%s\n", f.where.c_str(), r.csharp.c_str());
            for (auto& x : d) if (x.severity == 0) std::printf("    error %d:%d: %s\n", x.line, x.column, x.message.c_str());
        }
        for (const auto& b : r.bridged) if (std::find(bridgedAll.begin(), bridgedAll.end(), b) == bridgedAll.end()) bridgedAll.push_back(b);
    }
    std::printf("    %d of %zu compile in C# mode; %zu distinct built-ins still bridged\n", clean, all.size(), bridgedAll.size());
    CHECK(clean == (int)all.size());
}

// The same scene, with its old scripts or with them converted, behaves the same: the
// same keys held over the same frames, then every function of every script called as
// a UI event would, then the saved scene compared (scripts' own lines left out).
static std::string Strip(const std::string& saved) {
    std::istringstream in(saved);
    std::string line, out;
    bool inScript = false;
    while (std::getline(in, line)) {
        std::string t = line.substr(line.find_first_not_of(' ') == std::string::npos ? 0 : line.find_first_not_of(' '));
        if (t.rfind("script ", 0) == 0) {   // a script's source runs over several lines until its closing quote
            std::size_t quotes = 0;
            for (std::size_t i = 0; i < t.size(); ++i) if (t[i] == '"' && (i == 0 || t[i - 1] != '\\')) ++quotes;
            inScript = quotes % 2 == 1;
            continue;
        }
        if (inScript) {
            std::size_t quotes = 0;
            for (std::size_t i = 0; i < line.size(); ++i) if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) ++quotes;
            if (quotes % 2 == 1) inScript = false;
            continue;
        }
        if (t.rfind("scriptfields", 0) == 0 || t.rfind("scriptvar", 0) == 0) continue;
        out += line + "\n";
    }
    return out;
}
static std::vector<std::string> Functions(const std::string& src) {
    std::vector<std::string> names;
    std::regex re("function\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\(\\s*\\)");
    for (std::sregex_iterator it(src.begin(), src.end(), re), end; it != end; ++it) {
        std::string n = (*it)[1].str();
        if (n != "start" && n != "update") names.push_back(n);
    }
    return names;
}
static std::string Play(const std::string& text, bool convert, const char* rel) {
    Scene scene("Play");
    std::string err;
    if (!SceneSerializer::Deserialize(scene, text, &err)) { std::printf("    %s: %s\n", rel, err.c_str()); return "load failed"; }
    std::vector<std::pair<ScriptComponent*, std::string>> scripts;
    for (const auto& up : scene.Objects()) {
        for (ScriptComponent* sc : up->GetComponents<ScriptComponent>()) {
            GameObject* go = up.get();
            if (sc->Language() != std::string("okayscript")) continue;
            const std::string src = sc->Source();
            scripts.push_back({sc, src});
            if (convert) {
                auto r = ConvertOkayScriptToCSharp(src, "Converted");
                sc->SetLanguage("csharp");
                if (!sc->LoadSource(r.csharp, &err)) std::printf("    %s: converted script on %s didn't load: %s\n", rel, go->name.c_str(), err.c_str());
            }
        }
    }
    scene.Start();
    const char keys[] = {'d', 'w', 'a', 's', ' ', 'e', '1', '2'};
    for (int i = 0; i < 96; ++i) {
        Input::FeedKeys({keys[(i / 12) % 8]});
        scene.Update(kDt);
    }
    Input::FeedKeys({});
    scene.Update(kDt);
    for (auto& [sc, src] : scripts) {
        for (const std::string& fn : Functions(src)) { sc->VM()->CallEvent(fn); scene.Update(kDt); }
    }
    for (int i = 0; i < 5; ++i) scene.Update(kDt);
    return Strip(SceneSerializer::Serialize(scene));
}

static void ScenesBehaveTheSame() {
    for (const char* rel : kScenes) {
        const std::string text = ReadText(std::string(OKAY_SOURCE_DIR) + "/" + rel);
        const std::string a = Play(text, false, rel), b = Play(text, true, rel);
        const bool same = a == b;
        std::printf("    %s: %s\n", rel, same ? "same" : "DIFFERENT");
        if (!same) {   // the first differing line
            std::istringstream ia(a), ib(b);
            std::string la, lb;
            int n = 0;
            while (std::getline(ia, la) && std::getline(ib, lb)) {
                ++n;
                if (la != lb) { std::printf("      line %d\n      old: %s\n      C#:  %s\n", n, la.c_str(), lb.c_str()); break; }
            }
        }
        CHECK(same);
    }
}

int main() {
    RUN_SUITE("csharp_convert");
    Basics();
    EveryShippedScript();
    ScenesBehaveTheSame();
    TEST_MAIN_RESULT();
}
