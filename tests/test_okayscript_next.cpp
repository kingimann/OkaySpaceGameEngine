#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scripting/NextScriptVM.hpp"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;

#define CHECK_EQ(a, b) CHECK((a) == (b))

// The new OkayScript: checked before it runs (errors and "does nothing" warnings
// with lines), then played in real scenes with simulated input.

static std::vector<std::string> g_errs, g_logs;
static const float kDt = 1.0f / 60.0f;

static std::vector<ScriptDiagnostic> Diags(const std::string& src) { NextScriptVM vm; return vm.ValidateAll(src); }
static bool Has(const std::vector<ScriptDiagnostic>& d, int sev, const std::string& text, int line = -1) {
    return std::any_of(d.begin(), d.end(), [&](const ScriptDiagnostic& x) {
        return x.severity == sev && x.message.find(text) != std::string::npos && (line < 0 || x.line == line);
    });
}
static int Count(const std::vector<ScriptDiagnostic>& d, int sev) {
    return (int)std::count_if(d.begin(), d.end(), [&](const ScriptDiagnostic& x) { return x.severity == sev; });
}
static void Dump(const std::vector<ScriptDiagnostic>& d) {
    for (auto& x : d) std::printf("    %s line %d: %s\n", x.severity ? "warning" : "error", x.line, x.message.c_str());
}
static std::string Wrap(const std::string& body) { return "class T : Behaviour {\n" + body + "\n}\n"; }
static bool LogHas(const std::string& s) {
    return std::any_of(g_logs.begin(), g_logs.end(), [&](const std::string& l) { return l.find(s) != std::string::npos; });
}
static bool ErrHas(const std::string& s) {
    return std::any_of(g_errs.begin(), g_errs.end(), [&](const std::string& l) { return l.find(s) != std::string::npos; });
}

struct World {
    Scene scene{"W"};
    GameObject* player = nullptr;
    ScriptComponent* sc = nullptr;
    bool loaded = false;
    std::string error;
    explicit World(const std::string& src, bool start = true) {
        player = scene.CreateGameObject("Player");
        sc = player->AddComponent<ScriptComponent>(NextScriptVM::kLanguage);
        loaded = sc->LoadSource(src, &error);
        if (start) scene.Start();
    }
    void Frames(int n, std::vector<char> keys = {}) {
        for (int i = 0; i < n; ++i) { Input::FeedKeys(keys); scene.Update(kDt); }
    }
    vs::VsValue Get(const char* n) { return sc->VM()->GetGlobal(n); }
};

int main() {
    RUN_SUITE("okayscript_next");
    Log::sink = [](Log::Level l, const std::string& m) {
        if (l >= Log::Level::Error) g_errs.push_back(m); else g_logs.push_back(m);
    };

    // ================= The checker =================
    {   // a plain, correct script is clean
        auto d = Diags(
            "class Mover : Behaviour {\n"
            "    [Header(\"Movement\")]\n"
            "    public float speed = 5;\n"
            "    public int lives = 3;\n"
            "    const float Gravity = 9.8f;\n"
            "    int hits = 0;\n"
            "    void Start() { Debug.Log(gameObject.name + \" ready, gravity \" + Gravity); }\n"
            "    void Update() {\n"
            "        float h = Input.GetAxis(\"Horizontal\");\n"
            "        transform.position += new Vector3(h, 0, 0) * speed * Time.deltaTime;\n"
            "        if (Input.GetKeyDown(\"space\")) { hits++; Debug.Log(\"hits: \" + hits); }\n"
            "        for (int i = 0; i < lives; i++) { if (i == 2) break; }\n"
            "    }\n"
            "}\n");
        CHECK_EQ(Count(d, 0), 0);
        CHECK_EQ(Count(d, 1), 0);
        if (!d.empty()) Dump(d);
    }
    {   // things that won't run are errors, at the right line
        auto d = Diags(
            "using System;\n"                                           // 1
            "class T : MonoBehaviour {\n"                               // 2
            "    int count = 2.5f;\n"                                   // 3
            "    Vectr3 where;\n"                                       // 4
            "    void Update(float dt) { }\n"                          // 5
            "    void Start() {\n"                                      // 6
            "        int n = 3;\n"                                      // 7
            "        if (n) { }\n"                                      // 8
            "        n + 1;\n"                                          // 9
            "        transform.positon = Vector3.zero;\n"              // 10
            "        transform.position.x = 3;\n"                       // 11
            "        bool k = Input.GetKey(\"jump\");\n"                // 12
            "        float a = Input.GetAxis(\"Sideways\");\n"          // 13
            "        transform.Translate(1, 2);\n"                      // 14
            "        Debug.Log(k + \" \" + a + count);\n"               // 15
            "        return 4;\n"                                       // 16
            "    }\n"
            "    int Twice(int x) { if (x > 0) return x * 2; }\n"       // 18
            "}\n");
        CHECK(Has(d, 0, "`using` isn't needed", 1));
        CHECK(Has(d, 0, "'MonoBehaviour' isn't something a script can extend", 2));
        CHECK(Has(d, 0, "is a float but needs an int", 3));
        CHECK(Has(d, 0, "Unknown type 'Vectr3'. Did you mean 'Vector3'?", 4));
        CHECK(Has(d, 0, "Update() takes no parameters", 5));
        CHECK(Has(d, 0, "condition must be true/false, not int", 8));
        CHECK(Has(d, 0, "This line does nothing", 9));
        CHECK(Has(d, 0, "Transform has no 'positon'. Did you mean 'position'?", 10));
        CHECK(Has(d, 0, "changes a copy", 11));
        CHECK(Has(d, 0, "\"jump\" isn't a key name", 12));
        CHECK(Has(d, 0, "\"Sideways\" isn't an axis", 13));
        CHECK(Has(d, 0, "Translate takes 1 or 3 values, not 2", 14));
        CHECK(Has(d, 0, "Start() is void, so it can't return a value", 16));
        CHECK(Has(d, 0, "Twice() must return a int on every path", 18));
    }
    {   // code that runs but does nothing is a warning
        auto d = Diags(Wrap(
            "    int unusedField = 1;\n"                     // 2
            "    void Start() {\n"                            // 3
            "        int spare = 4;\n"                        // 4
            "        return;\n"                               // 5
            "        Debug.Log(\"never\");\n"                 // 6
            "    }\n"
            "    void update() { }\n"                         // 8
            "    void Helper() { }\n"));                      // 9
        CHECK(Has(d, 1, "'unusedField' is never read", 2));
        CHECK(Has(d, 1, "'spare' is never used", 4));
        CHECK(Has(d, 1, "This can never run", 6));
        CHECK(Has(d, 1, "update() is never called, so it never runs. Did you mean Update?", 8));
        CHECK(Has(d, 1, "Helper() is never called", 9));
        CHECK_EQ(Count(d, 0), 0);
    }
    {   // C# features that aren't in the language yet say so
        auto d = Diags(Wrap("    readonly int r = 1;\n    void Start() { goto end; }\n"));
        CHECK(Has(d, 0, "readonly isn't supported yet"));
        CHECK(Has(d, 0, "goto isn't supported"));
        auto d2 = Diags(Wrap("    [Range(0, 1)] float hidden = 0;\n    void Start() { Debug.Log(hidden); }\n"));
        CHECK(Has(d2, 0, "only shows in the Inspector for public fields"));
        auto d3 = Diags(Wrap("    const int Max = 3;\n    void Start() { Max = 4; }\n"));
        CHECK(Has(d3, 0, "'Max' is const"));
        auto d4 = Diags("class A : Behaviour { }\nclass B : Behaviour { }\n");
        CHECK(Has(d4, 0, "Only one Behaviour class per script"));
    }
    {   // a script with errors refuses to load and never runs
        World w(Wrap("    void Start() { Debug.Log(\"should not run\"); int x = \"no\"; }"));
        CHECK(!w.loaded);
        CHECK(w.error.find("line 2") != std::string::npos);
        w.Frames(2);
        CHECK(!LogHas("should not run"));
    }

    // ================= Gameplay =================
    {   // WASD moves the object; public fields are its settings
        g_logs.clear();
        World w(Wrap(
            "    public float speed = 4;\n"
            "    int frames = 0;\n"
            "    void Start() { Debug.Log(\"started on \" + gameObject.name); }\n"
            "    void Update() {\n"
            "        transform.position += new Vector3(Input.GetAxis(\"Horizontal\"), 0, 0) * speed * Time.deltaTime;\n"
            "        frames++;\n"
            "    }\n"));
        CHECK(w.loaded);
        CHECK(LogHas("started on Player"));
        w.Frames(30, {'d'});
        CHECK_NEAR(w.player->transform->Position().x, 2.0f, 0.05f);     // 4 units/s for 0.5 s
        CHECK_NEAR(w.Get("frames").AsFloat(), 30.0f, 1e-4f);
        w.Frames(15, {'a'});
        CHECK_NEAR(w.player->transform->Position().x, 1.0f, 0.05f);
        // The Inspector's override for the field takes effect.
        w.sc->fields["speed"] = "8";
        w.sc->ApplyFieldOverrides();
        w.Frames(15, {'d'});
        CHECK_NEAR(w.player->transform->Position().x, 3.0f, 0.05f);
        CHECK_NEAR(w.Get("speed").AsFloat(), 8.0f, 1e-4f);
    }
    {   // references are real: another object's transform moves THAT object
        World w(Wrap(
            "    GameObject enemy;\n"
            "    void Start() {\n"
            "        enemy = GameObject.Find(\"Enemy\");\n"
            "        enemy.transform.position = new Vector3(5, 0, 0);\n"
            "        enemy.GetComponent<Transform>().localScale = new Vector3(2, 2, 2);\n"
            "        enemy.name = \"Boss\";\n"
            "    }\n"
            "    void Update() { enemy.transform.Translate(1, 0, 0); }\n"), false);
        GameObject* e1 = w.scene.CreateGameObject("Enemy");
        GameObject* e2 = w.scene.CreateGameObject("Enemy");   // same name: the reference keeps its own object
        w.scene.Start();
        w.Frames(3);
        CHECK_NEAR(e1->transform->Position().x, 8.0f, 1e-4f);  // 5, then +1 per frame
        CHECK_NEAR(e1->transform->localScale.x, 2.0f, 1e-4f);
        CHECK(e1->name == "Boss");
        CHECK_NEAR(e2->transform->Position().x, 0.0f, 1e-6f);
        CHECK_NEAR(w.player->transform->Position().x, 0.0f, 1e-6f);   // not the script's own object
    }
    {   // a destroyed object turns into null instead of crashing
        g_errs.clear(); g_logs.clear();
        World w(Wrap(
            "    GameObject target;\n"
            "    void Start() { target = GameObject.Find(\"Crate\"); Destroy(target); }\n"
            "    void Update() {\n"
            "        if (target == null) Debug.Log(\"crate gone\");\n"
            "        else target.transform.Translate(1, 0, 0);\n"
            "    }\n"
            "    public void Poke() { target.transform.Translate(0, 1, 0); }\n"), false);
        w.scene.CreateGameObject("Crate");
        w.scene.Start();
        w.Frames(2);
        CHECK(LogHas("crate gone"));
        w.sc->VM()->CallEvent("Poke");                      // using it anyway is a reported error, not a crash
        CHECK(ErrHas("Poke() line 8: This object was destroyed"));
    }
    {   // language: ints, floats, text, loops, methods, recursion
        World w(Wrap(
            "    public int half;\n"
            "    public int wrapped;\n"
            "    public float avg;\n"
            "    public string text;\n"
            "    public int fact;\n"
            "    public int evens;\n"
            "    void Start() {\n"
            "        half = 7 / 2;\n"
            "        wrapped = 2147483647 + 1;\n"
            "        avg = (1 + 2) / 2.0f;\n"
            "        text = \"v=\" + new Vector3(1, 2.5f, 0) + \" ok=\" + (half == 3) + \" n=\" + 3;\n"
            "        fact = Factorial(5);\n"
            "        int i = 0;\n"
            "        while (true) { i++; if (i > 10) break; if (i % 2 == 1) continue; evens += i; }\n"
            "    }\n"
            "    int Factorial(int n) { return n <= 1 ? 1 : n * Factorial(n - 1); }\n"));
        CHECK(w.loaded);
        if (!w.loaded) std::printf("    %s\n", w.error.c_str());
        CHECK_NEAR(w.Get("half").AsFloat(), 3.0f, 1e-6f);
        CHECK_NEAR(w.Get("wrapped").AsFloat(), -2147483648.0f, 1.0f);
        CHECK_NEAR(w.Get("avg").AsFloat(), 1.5f, 1e-6f);
        CHECK(w.Get("text").AsString() == "v=(1, 2.5, 0) ok=True n=3");
        CHECK_NEAR(w.Get("fact").AsFloat(), 120.0f, 1e-4f);
        CHECK_NEAR(w.Get("evens").AsFloat(), 30.0f, 1e-4f);   // 2+4+6+8+10
    }
    {   // lifecycle: order, FixedUpdate at 50 per second, LateUpdate, enabled
        World w(Wrap(
            "    public string order = \"\";\n"
            "    public int fixedSteps;\n"
            "    public float fixedDt;\n"
            "    public int updates;\n"
            "    public int lates;\n"
            "    void Awake() { order += \"A\"; }\n"
            "    void OnEnable() { order += \"E\"; }\n"
            "    void Start() { order += \"S\"; }\n"
            "    void FixedUpdate() { fixedSteps++; fixedDt = Time.deltaTime; }\n"
            "    void Update() { updates++; if (updates == 30) enabled = false; }\n"
            "    void LateUpdate() { lates++; }\n"));
        CHECK(w.Get("order").AsString() == "AES");
        w.Frames(60);
        CHECK_NEAR(w.Get("updates").AsFloat(), 30.0f, 1e-4f);       // turned itself off
        CHECK_NEAR(w.Get("lates").AsFloat(), 29.0f, 1e-4f);         // the 30th frame disabled it before LateUpdate
        CHECK(w.Get("fixedSteps").AsFloat() >= 24.0f && w.Get("fixedSteps").AsFloat() <= 26.0f);   // ~0.5 s of fixed steps
        CHECK_NEAR(w.Get("fixedDt").AsFloat(), 0.02f, 1e-5f);
        CHECK(!w.sc->enabled);
    }
    {   // runaway code is stopped and reported; the game keeps going
        g_errs.clear();
        World w(Wrap("    public int after;\n    void Start() { while (true) { } }\n    void Update() { after++; }\n"));
        w.Frames(2);
        CHECK(ErrHas("ran too long"));
        CHECK_NEAR(w.Get("after").AsFloat(), 2.0f, 1e-4f);
        g_errs.clear();
        World div(Wrap("    public int z;\n    void Start() { int q = 5 / z; Debug.Log(q); }\n"));
        CHECK(ErrHas("Start() line 3: Division by zero"));
    }
    {   // outside Play (scripts switched off) nothing runs
        g_logs.clear();
        Scene s("edit");
        s.runScripts = false;
        auto* sc = s.CreateGameObject("G")->AddComponent<ScriptComponent>(NextScriptVM::kLanguage);
        CHECK(sc->LoadSource(Wrap("    void Start() { Debug.Log(\"edit-mode start\"); }")));
        s.Start();
        s.Update(0.0f);
        CHECK(!LogHas("edit-mode start"));
        s.runScripts = true;
        s.Update(kDt);
        CHECK(LogHas("edit-mode start"));
    }
    {   // a public method can be wired to UI (called by name); private ones can't
        World w(Wrap("    public int clicks;\n    public void OnPlayPressed() { clicks++; }\n"));
        w.sc->VM()->CallEvent("OnPlayPressed");
        w.sc->VM()->CallEvent("OnPlayPressed");
        CHECK_NEAR(w.Get("clicks").AsFloat(), 2.0f, 1e-4f);
    }
    {   // saving and loading a scene keeps the script and its language
        Scene s("save");
        auto* go = s.CreateGameObject("Hero");
        auto* sc = go->AddComponent<ScriptComponent>(NextScriptVM::kLanguage);
        CHECK(sc->LoadSource(Wrap("    public float speed = 2;\n    void Update() { transform.Translate(speed * Time.deltaTime, 0, 0); }")));
        sc->fields["speed"] = "6";
        std::string text = SceneSerializer::Serialize(s);
        Scene s2("load");
        CHECK(SceneSerializer::Deserialize(s2, text));
        GameObject* hero = s2.Find("Hero");
        auto* sc2 = hero ? hero->GetComponent<ScriptComponent>() : nullptr;
        CHECK(sc2 && sc2->Language() == NextScriptVM::kLanguage);
        s2.Start();
        for (int i = 0; i < 30; ++i) s2.Update(kDt);
        CHECK(hero && std::fabs(hero->transform->Position().x - 3.0f) < 0.05f);   // 6 units/s for 0.5 s
    }

    {   // a float literal too small or too big for a float is a parse error, not a crash
        const std::string tiny = "0." + std::string(60, '0') + "1";
        const std::string huge = "1" + std::string(60, '0') + ".0";
        for (const std::string& lit : {tiny, huge}) {
            auto d = Diags(Wrap("    void Start() { float f = " + lit + "; }"));
            CHECK(Has(d, 0, "number out of range", 2));
        }
    }
    {   // deeply nested parentheses are a parse error, not a stack overflow
        auto d = Diags(Wrap("    void Start() { int x = " + std::string(5000, '(') + "1" + std::string(5000, ')') + "; }"));
        CHECK(Has(d, 0, "nested too deeply", 2));
        std::string negs;
        for (int i = 0; i < 5000; ++i) negs += "- ";
        auto u = Diags(Wrap("    void Start() { int x = " + negs + "1; }"));
        CHECK(Has(u, 0, "nested too deeply", 2));
        // ...while ordinary nesting still works
        World w(Wrap("    public int x;\n    void Start() { x = ((((2 + 1)))) * -(-2); }"));
        CHECK(w.loaded);
        CHECK_NEAR(w.Get("x").AsFloat(), 6.0f, 1e-4f);
    }

    {   // a very long flat operator chain is a parse error, not a stack overflow
        // (1+1+...+1 parses in a loop but builds a left-deep tree that checking,
        // running and destroying would walk recursively, once per term)
        auto chain = [](const std::string& term, const char* op, int n) {
            std::string s = term;
            for (int i = 1; i < n; ++i) { s += op; s += term; }
            return s;
        };
        struct Case { const char* type; std::string term; const char* op; };
        for (const Case& c : {Case{"int", "1", " + "}, Case{"bool", "true", " && "}, Case{"bool", "false", " || "},
                              Case{"string", "\"a\"", " + "}}) {
            const std::string src = Wrap(std::string("    public ") + c.type + " x;\n    void Start() { x = " +
                                         chain(c.term, c.op, 100000) + "; }");
            auto d = Diags(src);
            CHECK(Has(d, 0, "too long", 3));
            World w(src);
            CHECK(!w.loaded);
            w.Frames(2);
        }
        // ...while a long but sensible chain still works.
        World w(Wrap("    public int n;\n    public bool a;\n    public bool o;\n    public string s;\n"
                     "    void Start() { n = " + chain("1", " + ", 1001) + "; a = " + chain("true", " && ", 1001) +
                     "; o = " + chain("false", " || ", 1001) + "; s = " + chain("\"ab\"", " + ", 1001) + "; }"));
        CHECK(w.loaded);
        if (!w.loaded) std::cerr << "  load error: " << w.error << "\n";
        CHECK_NEAR(w.Get("n").AsFloat(), 1001.0f, 1e-3f);
        CHECK(w.Get("a").AsBool());
        CHECK(!w.Get("o").AsBool());
        CHECK(w.Get("s").AsString().size() == 2002);
        // the cap is 1000 operators per expression: one more is an error
        CHECK(Has(Diags(Wrap("    public int n;\n    void Start() { n = " + chain("1", " + ", 1002) + "; }")), 0, "too long", 3));
    }

    Input::FeedKeys({});
    Log::sink = nullptr;
    TEST_MAIN_RESULT();
}
