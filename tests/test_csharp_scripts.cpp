#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scripting/NextScriptVM.hpp"
#include "okay/Scripting/StarterScripts.hpp"
#include "okay/Testing/SceneValidator.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
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

    // Class attributes with dotted names, as Unity data classes carry them.
    CHECK(Clean("[System.Serializable]\npublic class Item { public int count; }\n"
                "[RequireComponent(typeof(Rigidbody))]\npublic class A : MonoBehaviour { public Item item; void Start() { item = new Item(); item.count = 2; } }\n"));
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

// Random.Range (int: max excluded; float: both ends), Random.value, Random.InitState.
static void RandomApi() {
    const std::string src =
        "public class R : MonoBehaviour {\n"
        "    public int minI = 100, maxI = -100;\n"
        "    public float minF = 100f, maxF = -100f, v;\n"
        "    public int same;\n"
        "    void Start() {\n"
        "        for (int i = 0; i < 500; i++) {\n"
        "            int n = Random.Range(0, 3);\n"
        "            if (n < minI) minI = n; if (n > maxI) maxI = n;\n"
        "            float f = Random.Range(-1f, 1f);\n"
        "            if (f < minF) minF = f; if (f > maxF) maxF = f;\n"
        "        }\n"
        "        v = Random.value;\n"
        "        Random.InitState(7); int a = Random.Range(0, 1000000);\n"
        "        Random.InitState(7); int b = Random.Range(0, 1000000);\n"
        "        same = a == b ? 1 : 0;\n"
        "    }\n"
        "}\n";
    CHECK(Clean(src));
    World w(src);
    CHECK(w.loaded);
    CHECK_NEAR(w.F("minI"), 0.0f, 1e-4f);
    CHECK_NEAR(w.F("maxI"), 2.0f, 1e-4f);   // 3 is never returned
    CHECK(w.F("minF") >= -1.0f && w.F("minF") < -0.8f);
    CHECK(w.F("maxF") <= 1.0f && w.F("maxF") > 0.8f);
    CHECK(w.F("v") >= 0.0f && w.F("v") < 1.0f);
    CHECK_NEAR(w.F("same"), 1.0f, 1e-4f);
    // int result: assigning Random.Range(0f, 1f) to an int is an error
    CHECK(Errors(Diags("public class R : MonoBehaviour { void Start() { int n = Random.Range(0f, 1f); } }\n")) >= 1);
}

// Unity's collision messages: OnTriggerEnter(Collider other), OnCollisionEnter(Collision c)
// and the 2D ones, with other.name / tag / gameObject / CompareTag. Private methods, as
// a Unity user writes them, with no "never called" warning.
static void CollisionMessages() {
    const std::string src =
        "public class Hit : MonoBehaviour {\n"
        "    public int enters, exits, coins, hits, hits2d;\n"
        "    public string lastName = \"\", lastHit = \"\";\n"
        "    void OnTriggerEnter(Collider other) {\n"
        "        enters++;\n"
        "        lastName = other.name;\n"
        "        if (other.CompareTag(\"Coin\")) coins++;\n"
        "        if (other.gameObject.tag == \"Coin\") coins += 10;\n"
        "    }\n"
        "    void OnTriggerExit(Collider other) { exits++; }\n"
        "    void OnCollisionEnter(Collision c) { hits++; lastHit = c.gameObject.name + \"@\" + c.transform.position.x; }\n"
        "    void OnCollisionEnter2D(Collision2D c) { hits2d++; }\n"
        "}\n";
    auto d = Diags(src);
    CHECK(Errors(d) == 0);
    CHECK((int)d.size() == 0);   // no "never called" warnings
    Dump(d);
    World w(src);
    CHECK(w.loaded);
    GameObject* coin = w.scene.CreateGameObject("Coin1");
    coin->tag = "Coin";
    coin->transform->localPosition = {4.0f, 0.0f, 0.0f};
    auto* box = coin->AddComponent<BoxCollider3D>();
    w.sc->OnTriggerEnter3D(box);
    w.sc->OnTriggerExit3D(box);
    Collision3D c3; c3.gameObject = coin; c3.collider = box;
    w.sc->OnCollisionEnter3D(c3);
    Collision2D c2; c2.gameObject = coin;
    w.sc->OnCollisionEnter2D(c2);
    for (auto& e : g_errs) std::printf("    error: %s\n", e.c_str());
    CHECK_NEAR(w.F("enters"), 1.0f, 1e-4f);
    CHECK_NEAR(w.F("exits"), 1.0f, 1e-4f);
    CHECK_NEAR(w.F("coins"), 11.0f, 1e-4f);
    CHECK_NEAR(w.F("hits"), 1.0f, 1e-4f);
    CHECK_NEAR(w.F("hits2d"), 1.0f, 1e-4f);
    CHECK(w.sc->VM() && w.sc->VM()->GetGlobal("lastName").AsString() == "Coin1");
    CHECK(w.sc->VM() && w.sc->VM()->GetGlobal("lastHit").AsString() == "Coin1@4");
    // The old way still works: a public method without a parameter.
    World w2("public class H : MonoBehaviour { public int n; public void OnTriggerEnter() { n++; } }\n");
    w2.sc->OnTriggerEnter3D(box);
    CHECK_NEAR(w2.F("n"), 1.0f, 1e-4f);
    // A wrong parameter type is caught before the game runs.
    CHECK(Errors(Diags("public class H : MonoBehaviour { void OnTriggerEnter(int x) { } }\n")) >= 1);
}

// Text members C# scripts use: Length, ToUpper / ToLower, Contains, StartsWith, EndsWith,
// IndexOf, Substring, Replace, Trim, Split; ToString() on numbers and true/false.
static void StringMembers() {
    const std::string src =
        "public class S : MonoBehaviour {\n"
        "    public string up, low, sub, sub2, rep, trimmed, joined, num;\n"
        "    public int len, idx, parts;\n"
        "    public bool has, starts, ends;\n"
        "    void Start() {\n"
        "        string s = \"  Hello World  \";\n"
        "        trimmed = s.Trim();\n"
        "        len = trimmed.Length;\n"
        "        up = trimmed.ToUpper(); low = trimmed.ToLower();\n"
        "        has = trimmed.Contains(\"lo W\"); starts = trimmed.StartsWith(\"He\"); ends = trimmed.EndsWith(\"x\");\n"
        "        idx = trimmed.IndexOf(\"World\");\n"
        "        sub = trimmed.Substring(6); sub2 = trimmed.Substring(0, 5);\n"
        "        rep = trimmed.Replace(\"World\", \"There\");\n"
        "        string[] bits = \"a,b,c\".Split(',');\n"
        "        parts = bits.Length; joined = bits[2];\n"
        "        int score = 42; float t = 1.5f; bool ok = true;\n"
        "        num = score.ToString() + \"/\" + t.ToString() + \"/\" + ok.ToString();\n"
        "    }\n"
        "}\n";
    CHECK(Clean(src));
    World w(src);
    CHECK(w.loaded);
    auto S = [&](const char* n) { return w.sc->VM() ? w.sc->VM()->GetGlobal(n).AsString() : std::string(); };
    CHECK(S("trimmed") == "Hello World");
    CHECK_NEAR(w.F("len"), 11.0f, 1e-4f);
    CHECK(S("up") == "HELLO WORLD");
    CHECK(S("low") == "hello world");
    CHECK(w.sc->VM() && w.sc->VM()->GetGlobal("has").AsBool());
    CHECK(w.sc->VM() && w.sc->VM()->GetGlobal("starts").AsBool());
    CHECK(w.sc->VM() && !w.sc->VM()->GetGlobal("ends").AsBool());
    CHECK_NEAR(w.F("idx"), 6.0f, 1e-4f);
    CHECK(S("sub") == "World");
    CHECK(S("sub2") == "Hello");
    CHECK(S("rep") == "Hello There");
    CHECK_NEAR(w.F("parts"), 3.0f, 1e-4f);
    CHECK(S("joined") == "c");
    CHECK(S("num") == "42/1.5/True");
    // int.Parse / float.Parse / string.IsNullOrEmpty / int.MaxValue
    {
        World p("public class P : MonoBehaviour { public int n, big; public float f; public bool empty, full;\n"
                "  void Start() { n = int.Parse(\"42\") + 1; f = float.Parse(\"2.5\"); big = int.MaxValue;\n"
                "    empty = string.IsNullOrEmpty(\"\"); full = string.IsNullOrEmpty(\"x\"); } }\n");
        CHECK(p.loaded);
        CHECK_NEAR(p.F("n"), 43.0f, 1e-4f);
        CHECK_NEAR(p.F("f"), 2.5f, 1e-4f);
        CHECK(p.F("big") > 2.0e9f);
        CHECK(p.sc->VM() && p.sc->VM()->GetGlobal("empty").AsBool());
        CHECK(p.sc->VM() && !p.sc->VM()->GetGlobal("full").AsBool());
        g_errs.clear();
        World q("public class P : MonoBehaviour { public int n = 7; void Start() { n = int.Parse(\"seven\"); } }\n");
        CHECK_NEAR(q.F("n"), 7.0f, 1e-4f);   // the error stops Start before the assignment
        bool said = false;
        for (auto& e : g_errs) if (e.find("seven") != std::string::npos) said = true;
        CHECK(said);
    }
    // Out of range is a runtime error with the line, not a crash.
    World bad("public class S : MonoBehaviour { public string r; void Start() { r = \"abc\".Substring(5); } }\n");
    CHECK(!bad.loaded || (bad.sc->VM() && bad.sc->VM()->GetGlobal("r").AsString().empty()));
    CHECK(Errors(Diags("public class S : MonoBehaviour { void Start() { int n = \"abc\".Lenght; } }\n")) >= 1);
}

// Default parameter values: void Hit(int amount = 1, string why = "bump").
static void DefaultParams() {
    const std::string src =
        "public enum Kind { Small, Big }\n"
        "public class Counter { public int total; public void Add(int n = 1) { total += n; } }\n"
        "public class D : MonoBehaviour {\n"
        "    public int hp = 10; public string last = \"\"; public float scaled; public int kind, viaObj;\n"
        "    void Hit(int amount = 1, string why = \"bump\") { hp -= amount; last = why; }\n"
        "    float Scale(float x, float by = 2f) => x * by;\n"
        "    int KindOf(Kind k = Kind.Big) => (int)k;\n"
        "    void Start() {\n"
        "        Hit(); Hit(3); Hit(2, \"spikes\");\n"
        "        scaled = Scale(1.5f);\n"
        "        kind = KindOf();\n"
        "        Counter c = new Counter(); c.Add(); c.Add(5); viaObj = c.total;\n"
        "    }\n"
        "}\n";
    CHECK(Clean(src));
    World w(src);
    CHECK(w.loaded);
    CHECK_NEAR(w.F("hp"), 4.0f, 1e-4f);
    CHECK(w.sc->VM() && w.sc->VM()->GetGlobal("last").AsString() == "spikes");
    CHECK_NEAR(w.F("scaled"), 3.0f, 1e-4f);
    CHECK_NEAR(w.F("kind"), 1.0f, 1e-4f);
    CHECK_NEAR(w.F("viaObj"), 6.0f, 1e-4f);
    // A default before a parameter without one, or a value worked out while playing, is an error.
    CHECK(Errors(Diags("public class D : MonoBehaviour { void F(int a = 1, int b) { } }\n")) >= 1);
    CHECK(Errors(Diags("public class D : MonoBehaviour { int x; void F(int a = x) { } }\n")) >= 1);
    CHECK(Errors(Diags("public class D : MonoBehaviour { void F(int a = 1) { } void Start() { F(1, 2); } }\n")) >= 1);
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

#ifndef OKAY_SOURCE_DIR
#define OKAY_SOURCE_DIR "."
#endif
static std::string ReadText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

// examples/csharp: the shipped C# example scene loads, every script compiles without
// errors, and playing it runs them (the spinner turns, the scoreboard reads the
// mover's Hops property through GetComponent<PlayerMover>()).
static void ExampleScene() {
    const std::string dir = std::string(OKAY_SOURCE_DIR) + "/examples/csharp/";
    {   // Scoreboard uses PlayerMover: the checker knows another script once it has loaded
        auto vm = CreateScriptVM("csharp");
        std::string e;
        CHECK(vm && vm->Load(ReadText(dir + "PlayerMover.cs"), &e));
    }
    for (const char* f : {"PlayerMover.cs", "Spinner.cs", "Scoreboard.cs"}) {
        const std::string text = ReadText(dir + f);
        CHECK(!text.empty());
        auto d = Diags(text);
        CHECK(Errors(d) == 0);
        int warnings = (int)d.size() - Errors(d);
        CHECK(warnings == 0);
        if (!d.empty()) { std::printf("  %s:\n", f); Dump(d); }
    }
    Scene scene("Example");
    std::string err;
    const std::string text = ReadText(dir + "csharp_playground.okayscene");
    CHECK(!text.empty());
    CHECK(SceneSerializer::Deserialize(scene, text, &err));
    if (!err.empty()) std::printf("    scene: %s\n", err.c_str());
    GameObject* spinner = scene.Find("Spinner");
    GameObject* player = scene.Find("Player");
    CHECK(spinner != nullptr);
    CHECK(player != nullptr);
    if (!spinner || !player) return;
    CHECK(player->GetComponents<ScriptComponent>().size() == 2);
    for (auto* sc : player->GetComponents<ScriptComponent>()) CHECK(sc->Language() == "csharp");
    std::vector<std::string> logs;
    g_errs.clear();
    Log::sink = [&](Log::Level l, const std::string& m) {
        if (l >= Log::Level::Error) g_errs.push_back(m); else logs.push_back(m);
    };
    scene.Start();
    for (int i = 0; i < 200; ++i) scene.Update(kDt);   // a little over three seconds
    CHECK(g_errs.empty());
    for (auto& e : g_errs) std::printf("    error: %s\n", e.c_str());
    bool hello = false, score = false;
    for (auto& l : logs) {
        if (l.find("C# playground") != std::string::npos) hello = true;
        if (l.find("Score 0, best 0: no hops yet") != std::string::npos) score = true;
    }
    CHECK(hello);
    CHECK(score);
    auto* spin = spinner->GetComponent<ScriptComponent>();
    CHECK(spin && spin->VM());
    if (spin && spin->VM()) CHECK(spin->VM()->GetGlobal("turned").AsFloat() > 200.0f);   // 90 degrees a second, ~3.3 s
    Log::sink = [&](Log::Level l, const std::string& m) { if (l >= Log::Level::Error) g_errs.push_back(m); };
}

int main() {
    RUN_SUITE("csharp_scripts");
    Log::sink = [&](Log::Level l, const std::string& m) { if (l >= Log::Level::Error) g_errs.push_back(m); };
    LanguageIds();
    UnityStyleFile();
    KeyCodes();
    Properties();
    RandomApi();
    CollisionMessages();
    StringMembers();
    DefaultParams();
    OldScenes();
    ExampleScene();
    TEST_MAIN_RESULT();
}
