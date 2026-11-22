#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scripting/NextScriptVM.hpp"
#include "okay/Scripting/StarterScripts.hpp"
#include "okay/Testing/SceneValidator.hpp"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::testing;

// C# as a script language: `.cs` files and the language ids "csharp" / "cs" run on our
// own typed language (NextScriptVM), and the C# a Unity-style user writes without
// thinking (using lines, a namespace, MonoBehaviour, [SerializeField], properties,
// KeyCode) is accepted. Written from the ECMA-334 C# specification's grammar for
// using directives, namespaces and properties; no other project's code.

static const float kDt = 1.0f / 60.0f;
static std::vector<std::string> g_errs;

static std::vector<ScriptDiagnostic> Diags(const std::string& src) {
    auto vm = CreateScriptVM("csharp");
    return vm ? vm->ValidateAll(src) : std::vector<ScriptDiagnostic>{{1, "no csharp VM", 0, 1}};
}
static int Errors(const std::vector<ScriptDiagnostic>& d) {
    return (int)std::count_if(d.begin(), d.end(), [](const ScriptDiagnostic& x) { return x.severity == 0; });
}
static void Dump(const std::vector<ScriptDiagnostic>& d) {
    for (auto& x : d) std::printf("    %s %d:%d: %s\n", x.severity ? "warning" : "error", x.line, x.column, x.message.c_str());
}
static bool Clean(const std::string& src) {
    auto d = Diags(src);
    if (Errors(d) != 0) { Dump(d); return false; }
    return true;
}

struct World {
    Scene scene{"W"};
    GameObject* player = nullptr;
    ScriptComponent* sc = nullptr;
    bool loaded = false;
    std::string error;
    explicit World(const std::string& src, const char* language = "csharp") {
        player = scene.CreateGameObject("Player");
        sc = player->AddComponent<ScriptComponent>(language);
        loaded = sc->LoadSource(src, &error);
        if (!loaded) std::printf("    load failed: %s\n", error.c_str());
        scene.Start();
    }
    void Frames(int n) { for (int i = 0; i < n; ++i) scene.Update(kDt); }
    float F(const char* n) { return sc->VM() ? sc->VM()->GetGlobal(n).AsFloat() : -12345.0f; }
};

static void LanguageIds() {
    bool listed = false;
    for (auto& l : AvailableScriptLanguages()) if (l == "csharp") listed = true;
    CHECK(listed);
    for (const char* id : {"csharp", "cs"}) {
        auto vm = CreateScriptVM(id);
        CHECK(vm != nullptr);
        if (vm) CHECK(std::string(vm->Language()) == "csharp");
        CHECK(RemovedScriptLanguageMessage(id).empty());
    }
    CHECK(CreateScriptVM(NextScriptVM::kLanguage) != nullptr);   // the old id keeps working
    CHECK(!RemovedScriptLanguageMessage("lua").empty());         // Lua is a separate piece of work
    CHECK(std::string(ScriptLanguageForExtension(".cs")) == "csharp");
    CHECK(std::string(ScriptLanguageForExtension(".CS")) == "csharp");
    CHECK(std::string(ScriptLanguageForExtension(".okay")) == "okayscript");

    // The C# starter script loads and runs quietly.
    std::string starter = StarterScriptSource("csharp");
    CHECK(starter.find("class") != std::string::npos);
    CHECK(Clean(starter));
}

// The script a Unity user writes first, unchanged.
static void UnityStyleFile() {
    const std::string src =
        "using UnityEngine;\n"
        "using System.Collections;\n"
        "using System.Collections.Generic;\n"
        "\n"
        "namespace MyGame\n"
        "{\n"
        "    public class Mover : MonoBehaviour\n"
        "    {\n"
        "        [SerializeField] private float speed = 6f;\n"
        "        [Header(\"Jump\")]\n"
        "        public float jumpHeight = 2.0f;\n"
        "        public float x;\n"
        "\n"
        "        void Update()\n"
        "        {\n"
        "            transform.position += new Vector3(1, 0, 0) * speed * Time.deltaTime;\n"
        "            x = transform.position.x;\n"
        "        }\n"
        "    }\n"
        "}\n";
    CHECK(Clean(src));
    World w(src);
    CHECK(w.loaded);
    w.Frames(60);
    CHECK_NEAR(w.player->transform->localPosition.x, 6.0f, 0.05f);
    CHECK_NEAR(w.F("x"), 6.0f, 0.05f);

    // File-scoped namespace (C# 10) works too.
    CHECK(Clean("using UnityEngine;\nnamespace Game;\npublic class A : MonoBehaviour { void Start() {} }\n"));
    // `using` is still an error in the old typed id, which never promised C# files.
    {
        NextScriptVM vm;
        auto d = vm.ValidateAll("using UnityEngine;\nclass A : Behaviour { }\n");
        CHECK(Errors(d) >= 1);
    }
    // A using line in the middle of a file is still a mistake.
    {
        auto d = Diags("class A : MonoBehaviour {\n  using X;\n}\n");
        CHECK(Errors(d) >= 1);
    }
}

static void KeyCodes() {
    const std::string src =
        "public class K : MonoBehaviour {\n"
        "    public int presses;\n"
        "    void Update() {\n"
        "        if (Input.GetKey(KeyCode.W)) presses++;\n"
        "        if (Input.GetKeyDown(KeyCode.Space)) presses += 100;\n"
        "        if (Input.GetKeyUp(KeyCode.LeftShift)) presses += 1000;\n"
        "    }\n"
        "}\n";
    CHECK(Clean(src));
    World w(src);
    CHECK(w.loaded);
    w.Frames(3);
    CHECK_NEAR(w.F("presses"), 0.0f, 1e-4f);
    // A misspelt key is caught before the game runs.
    auto d = Diags("public class K : MonoBehaviour { void Update() { if (Input.GetKey(KeyCode.Spcae)) {} } }\n");
    CHECK(Errors(d) >= 1);
}

static void Properties() {
    const std::string src =
        "public class P : MonoBehaviour {\n"
        "    public int Score { get; private set; }\n"
        "    public float Health { get; set; } = 10f;\n"
        "    private int hp = 3;\n"
        "    public bool Alive => hp > 0;\n"
        "    private float speed;\n"
        "    public float Speed {\n"
        "        get { return speed; }\n"
        "        set { speed = Mathf.Clamp(value, 0f, 5f); }\n"
        "    }\n"
        "    public float score, health, sp, alive;\n"
        "    void Start() {\n"
        "        Score += 2; Score++;\n"
        "        Health -= 4f;\n"
        "        Speed = 99f;\n"
        "        hp = 0;\n"
        "        score = Score; health = Health; sp = Speed; alive = Alive ? 1 : 0;\n"
        "    }\n"
        "}\n";
    CHECK(Clean(src));
    World w(src);
    CHECK(w.loaded);
    CHECK_NEAR(w.F("score"), 3.0f, 1e-4f);
    CHECK_NEAR(w.F("health"), 6.0f, 1e-4f);
    CHECK_NEAR(w.F("sp"), 5.0f, 1e-4f);
    CHECK_NEAR(w.F("alive"), 0.0f, 1e-4f);
    // A get-only property can't be assigned.
    auto d = Diags("public class P : MonoBehaviour { int hp; public bool Alive => hp > 0; void Start() { Alive = true; } }\n");
    CHECK(Errors(d) >= 1);
}

// An old scene that named "csharp" (the removed Mono backend) now loads and runs if the
// script compiles, and shows the checker's errors if it does not.
static void OldScenes() {
    std::string text;
    {
        Scene scene("Old");
        GameObject* a = scene.CreateGameObject("Good");
        a->AddComponent<ScriptComponent>("csharp")->LoadSource(
            "using UnityEngine;\npublic class Spin : MonoBehaviour { void Update() { transform.Translate(0, 1f * Time.deltaTime, 0); } }\n");
        GameObject* b = scene.CreateGameObject("Bad");
        b->AddComponent<ScriptComponent>("cs")->LoadSource("class Script { void Update(float dt) { frobnicate(); } }");
        text = SceneSerializer::Serialize(scene);
    }
    Scene loaded("Loaded");
    std::string err;
    CHECK(SceneSerializer::Deserialize(loaded, text, &err));
    auto* good = loaded.Find("Good") ? loaded.Find("Good")->GetComponent<ScriptComponent>() : nullptr;
    auto* bad = loaded.Find("Bad") ? loaded.Find("Bad")->GetComponent<ScriptComponent>() : nullptr;
    CHECK(good != nullptr);
    CHECK(bad != nullptr);
    if (!good || !bad) return;
    CHECK(good->VM() != nullptr);
    loaded.Start();
    for (int i = 0; i < 60; ++i) loaded.Update(kDt);
    CHECK_NEAR(loaded.Find("Good")->transform->localPosition.y, 1.0f, 0.05f);

    ValidationReport rep = SceneValidator::Validate(loaded);
    CHECK(rep.CountCode("script.backend") == 0);
    bool named = false;
    for (const auto& i : rep.issues)
        if (i.message.find("frobnicate") != std::string::npos || i.message.find("Behaviour") != std::string::npos) named = true;
    CHECK(named);
    for (const auto& i : rep.issues) std::printf("    validator: %s: %s\n", i.code.c_str(), i.message.c_str());
}

int main() {
    RUN_SUITE("csharp_scripts");
    Log::sink = [&](Log::Level l, const std::string& m) { if (l >= Log::Level::Error) g_errs.push_back(m); };
    LanguageIds();
    UnityStyleFile();
    KeyCodes();
    Properties();
    OldScenes();
    TEST_MAIN_RESULT();
}
