#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <string>
#include <vector>

using namespace okay;

#define CHECK_EQ(a, b) CHECK((a) == (b))

// Every piece of script syntax either does something or is flagged as doing
// nothing: typed variables convert / reject values, const can't change, static is
// shared between objects, the lifecycle methods are all called, and the editor's
// checker warns about syntax that has no effect.

static std::vector<std::string> g_errs;

struct Run {
    Scene scene{"S"};
    GameObject* go = nullptr;
    ScriptComponent* sc = nullptr;
    bool loaded = false;
    std::string loadError;
    explicit Run(const std::string& src, const char* name = "Obj") {
        go = scene.CreateGameObject(name);
        sc = go->AddComponent<ScriptComponent>("okayscript");
        g_errs.clear();
        loaded = sc->LoadSource(src, &loadError);
        scene.Start();
    }
    void Frames(int n, float dt = 1.0f / 60.0f) { for (int i = 0; i < n; ++i) scene.Update(dt); }
    vs::VsValue Get(const char* n) { return sc->VM()->GetGlobal(n); }
};

static bool AnyErrorContains(const std::string& s) {
    return std::any_of(g_errs.begin(), g_errs.end(), [&](const std::string& e) { return e.find(s) != std::string::npos; });
}

static std::vector<ScriptDiagnostic> Check(const std::string& src) {
    OkayScriptVM vm;
    return vm.ValidateAll(src);
}
static int Count(const std::vector<ScriptDiagnostic>& d, int severity, const std::string& has = "") {
    return (int)std::count_if(d.begin(), d.end(), [&](const ScriptDiagnostic& x) {
        return x.severity == severity && (has.empty() || x.message.find(has) != std::string::npos);
    });
}

int main() {
    RUN_SUITE("script_semantics");
    Log::sink = [](Log::Level l, const std::string& m) { if (l >= Log::Level::Error) g_errs.push_back(m); };

    // ---- int really is an int ----
    {
        Run r("int half = 7 / 2;\n"
              "int lives = 3;\n"
              "float speed = 2.5;\n"
              "void Start() { lives += 0.9; speed += 0.25; }\n");
        CHECK(r.loaded);
        CHECK_NEAR(r.Get("half").AsFloat(), 3.0f, 1e-6f);     // 3.5 -> 3
        CHECK_NEAR(r.Get("lives").AsFloat(), 3.0f, 1e-6f);    // 3.9 -> 3
        CHECK_NEAR(r.Get("speed").AsFloat(), 2.75f, 1e-6f);   // floats keep decimals
        CHECK(g_errs.empty());
    }
    {   // negative values drop the fraction toward zero, like (int)
        Run r("int a = -2.7;\n");
        CHECK_NEAR(r.Get("a").AsFloat(), -2.0f, 1e-6f);
    }
    // ---- wrong kinds of value are errors, not silent ----
    {
        Run r("int lives = 3;\nvoid Start() { lives = \"three\"; }\n");
        CHECK(r.loaded);
        CHECK(AnyErrorContains("'lives' is an int, so it can't hold text"));
        CHECK_NEAR(r.Get("lives").AsFloat(), 3.0f, 1e-6f);    // unchanged
    }
    {
        Run r("string title = \"a\";\nvoid Start() { title = 5; }\n");
        CHECK(AnyErrorContains("'title' is a string"));
        CHECK(r.Get("title").AsString() == "a");
    }
    {   // a declaration with a wrong literal won't even load
        Run r("int lives = \"three\";\n");
        CHECK(!r.loaded);
        CHECK(r.loadError.find("lives") != std::string::npos);
    }
    {   // bool accepts true/false and numbers (0 = false); defaults are typed
        Run r("bool on = 1;\nbool off;\nstring name;\nint count;\n");
        CHECK(r.Get("on").IsBool() && r.Get("on").AsBool());
        CHECK(r.Get("off").IsBool() && !r.Get("off").AsBool());
        CHECK(r.Get("name").IsString() && r.Get("name").AsString().empty());
        CHECK(r.Get("count").IsNumber());
    }
    {   // var and other types hold anything
        Run r("var x = 1;\nvoid Start() { x = \"now text\"; }\n");
        CHECK(r.Get("x").AsString() == "now text");
        CHECK(g_errs.empty());
    }
    {   // locals are typed too
        Run r("float result;\nvoid Start() { int n = 9.99; result = n; }\n");
        CHECK_NEAR(r.Get("result").AsFloat(), 9.0f, 1e-6f);
    }
    {   // the Inspector writes through the type as well
        Run r("public int damage = 5;\n");
        r.sc->VM()->SetGlobal("damage", vs::VsValue{12.8f});
        CHECK_NEAR(r.Get("damage").AsFloat(), 12.0f, 1e-6f);
    }

    // ---- function signatures are real ----
    {
        Run r("int dealt;\nint total;\nstring label;\n"
              "void Hit(int dmg) { dealt = dmg; }\n"
              "int Sum(float a, float b) { return a + b; }\n"
              "void Start() { Hit(7.9); total = Sum(1.25, 2.5); label = Describe(); }\n"
              "string Describe() { return \"ok\"; }\n");
        CHECK_NEAR(r.Get("dealt").AsFloat(), 7.0f, 1e-6f);    // int parameter drops the fraction
        CHECK_NEAR(r.Get("total").AsFloat(), 3.0f, 1e-6f);    // int return: 3.75 -> 3
        CHECK(r.Get("label").AsString() == "ok");
        CHECK(g_errs.empty());
        Run bad("void Hit(int dmg) { }\nvoid Start() { Hit(\"lots\"); }\n");
        CHECK(AnyErrorContains("'dmg' is an int"));
        auto d = Check("void Start() { return 5; }\nfunction Twice(x) { return x * 2; }\nvoid Ok() { var f = x => x + 1; return; }\n");
        CHECK_EQ(Count(d, 0, "Start() is void"), 1);
        CHECK_EQ(Count(d, 0), 1);                              // lambdas and `return;` are fine
    }

    // ---- const / readonly ----
    {
        Run r("const int MAX = 3;\nreadonly float RATE = 0.5;\nvoid Start() { MAX = 4; }\nvoid Update() { RATE = 1; }\n");
        CHECK(r.loaded);
        r.Frames(1);
        CHECK(AnyErrorContains("'MAX' is const"));
        CHECK(AnyErrorContains("'RATE' is const"));
        CHECK_NEAR(r.Get("MAX").AsFloat(), 3.0f, 1e-6f);
        CHECK_NEAR(r.Get("RATE").AsFloat(), 0.5f, 1e-6f);
    }

    // ---- static is shared by every object running the script ----
    {
        const std::string src = "static int alive = 0;\nint seen;\nvoid Start() { alive++; }\nvoid Update() { seen = alive; }\n";
        Scene s("statics");
        auto* a = s.CreateGameObject("A")->AddComponent<ScriptComponent>("okayscript");
        auto* b = s.CreateGameObject("B")->AddComponent<ScriptComponent>("okayscript");
        auto* c = s.CreateGameObject("C")->AddComponent<ScriptComponent>("okayscript");
        CHECK(a->LoadSource(src) && b->LoadSource(src) && c->LoadSource(src));
        s.Start();
        s.Update(1.0f / 60.0f);
        CHECK_NEAR(a->VM()->GetGlobal("seen").AsFloat(), 3.0f, 1e-6f);
        CHECK_NEAR(c->VM()->GetGlobal("seen").AsFloat(), 3.0f, 1e-6f);
        // a different script has its own statics
        Run other("static int alive = 0;\nint seen;\nvoid Start() { alive += 10; }\nvoid Update() { seen = alive; }\n// other\n");
        other.Frames(1);
        CHECK_NEAR(other.Get("seen").AsFloat(), 10.0f, 1e-6f);
    }

    // ---- every lifecycle method runs ----
    {
        Run r("string order = \"\";\nint fixedSteps = 0;\n"
              "void Awake() { order += \"A\"; }\nvoid OnEnable() { order += \"E\"; }\nvoid Start() { order += \"S\"; }\n"
              "void FixedUpdate() { fixedSteps++; }\n");
        CHECK(r.Get("order").AsString() == "AES");
        r.Frames(60);                                           // one second at 60 fps
        const float steps = r.Get("fixedSteps").AsFloat();
        CHECK(steps >= 49.0f && steps <= 51.0f);                // 50 fixed steps per second
        Run slow("int fixedSteps = 0;\nvoid FixedUpdate() { fixedSteps++; }\n");
        slow.Frames(10, 0.1f);                                  // 10 fps: 5 steps a frame
        CHECK(slow.Get("fixedSteps").AsFloat() >= 49.0f);
    }
    {
        Scene s("destroy");
        auto* go = s.CreateGameObject("Dies");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("void OnDisable() { set(\"x\", 1); }\nvoid OnDestroy() { log_error(\"destroyed-ok\"); }\n"));
        s.Start();
        g_errs.clear();
        s.Destroy(go);
        s.Update(1.0f / 60.0f);
        CHECK(AnyErrorContains("destroyed-ok"));
    }

    // ---- the checker: errors for type mistakes, warnings for syntax that does nothing ----
    {
        auto d = Check("using System.Collections;\n"            // 1
                       "namespace Game {\n"                     // 2
                       "public class Hero : Component\n"        // 3
                       "{\n"
                       "    private float speed = 5f;\n"        // 5: private + suffix
                       "    protected int hp = 3;\n"            // 6
                       "    int bad = \"x\";\n"                 // 7: error
                       "    int half = 0.5;\n"                  // 8: warning
                       "    string s = 3;\n"                    // 9: error
                       "    public override void Start() { }\n" // 10: override
                       "    void OnGUI() { }\n"                 // 11: never called
                       "}\n}\n");
        auto at = [&](int line, int sev, const char* has) {
            return std::any_of(d.begin(), d.end(), [&](const ScriptDiagnostic& x) {
                return x.line == line && x.severity == sev && x.message.find(has) != std::string::npos; });
        };
        CHECK(at(1, 1, "`using` does nothing"));
        CHECK(at(2, 1, "`namespace` does nothing"));
        CHECK(at(3, 1, "class wrapper"));
        CHECK(at(5, 1, "`private` is already the default"));
        CHECK(at(5, 1, "5f"));
        CHECK(at(6, 1, "`protected` does nothing"));
        CHECK(at(7, 0, "can't hold text"));
        CHECK(at(8, 1, "becomes 0"));
        CHECK(at(9, 0, "write the value in quotes"));
        CHECK(at(10, 1, "`override` does nothing"));
        CHECK(at(11, 1, "never called"));
        CHECK(std::is_sorted(d.begin(), d.end(), [](const ScriptDiagnostic& a, const ScriptDiagnostic& b) { return a.line < b.line; }));
    }
    {   // plain, meaningful code is clean
        auto d = Check("[Header(\"Movement\")]\n"
                       "public float speed = 5;\n"
                       "public int lives = 3;\n"
                       "const float GRAVITY = 9.8;\n"
                       "static int spawned = 0;\n"
                       "bool grounded;\n"
                       "void Start() { spawned++; }\n"
                       "void Update() {\n"
                       "    float h = Input.GetAxis(\"Horizontal\");\n"
                       "    transform.position += new Vector3(h, 0, 0) * speed * Time.deltaTime;\n"
                       "}\n"
                       "IEnumerator Blink() { yield return new WaitForSeconds(0.2); }\n");
        CHECK_EQ(Count(d, 0), 0);
        CHECK_EQ(Count(d, 1), 0);
        for (auto& x : d) std::printf("  unexpected: line %d: %s\n", x.line, x.message.c_str());
    }
    {   // pasted C# with a wrapper still runs (just flagged)
        Run r("using System;\npublic class P : Component {\n  public bool ok;\n  void Start() { ok = true; }\n}\n");
        CHECK(r.loaded);
        CHECK(r.Get("ok").AsBool());
    }

    Log::sink = nullptr;
    TEST_MAIN_RESULT();
}
