#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scripting/NextScriptVM.hpp"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;

#define CHECK_EQ(a, b) CHECK((a) == (b))

// The C# features of the new OkayScript, one group per feature: programs that
// should work give the right results; programs that shouldn't compile give a
// clear error at the right line and column.

static std::vector<std::string> g_errs, g_logs;
static const float kDt = 1.0f / 60.0f;

static std::vector<ScriptDiagnostic> Diags(const std::string& src) { NextScriptVM vm; return vm.ValidateAll(src); }
static bool Has(const std::vector<ScriptDiagnostic>& d, int sev, const std::string& text, int line = -1, int col = -1) {
    return std::any_of(d.begin(), d.end(), [&](const ScriptDiagnostic& x) {
        return x.severity == sev && x.message.find(text) != std::string::npos && (line < 0 || x.line == line) &&
               (col < 0 || x.column == col);
    });
}
static int Count(const std::vector<ScriptDiagnostic>& d, int sev) {
    return (int)std::count_if(d.begin(), d.end(), [&](const ScriptDiagnostic& x) { return x.severity == sev; });
}
static void Dump(const std::vector<ScriptDiagnostic>& d) {
    for (auto& x : d) std::printf("    %s %d:%d: %s\n", x.severity ? "warning" : "error", x.line, x.column, x.message.c_str());
}
static std::string Wrap(const std::string& body) { return "class T : Behaviour {\n" + body + "\n}\n"; }
static bool ErrHas(const std::string& s) {
    return std::any_of(g_errs.begin(), g_errs.end(), [&](const std::string& l) { return l.find(s) != std::string::npos; });
}
static bool LogHas(const std::string& s) {
    return std::any_of(g_logs.begin(), g_logs.end(), [&](const std::string& l) { return l.find(s) != std::string::npos; });
}
// A program with no errors (warnings are printed but allowed).
static bool Clean(const std::string& src) {
    auto d = Diags(src);
    if (Count(d, 0) != 0) { Dump(d); return false; }
    return true;
}

struct World {
    Scene scene{"W"};
    GameObject* player = nullptr;
    ScriptComponent* sc = nullptr;
    bool loaded = false;
    std::string error;
    explicit World(const std::string& src, const char* language = NextScriptVM::kLanguage) {
        player = scene.CreateGameObject("Player");
        sc = player->AddComponent<ScriptComponent>(language);
        loaded = sc->LoadSource(src, &error);
        if (!loaded) std::printf("    load failed: %s\n", error.c_str());
        scene.Start();
    }
    void Frames(int n) { for (int i = 0; i < n; ++i) scene.Update(kDt); }
    float F(const char* n) { return sc->VM()->GetGlobal(n).AsFloat(); }
    std::string S(const char* n) { return sc->VM()->GetGlobal(n).AsString(); }
};

static void Enums() {
    {   // declared at the top of the file or inside the class; values count up from 0 or from an explicit value
        World w(
            "enum State { Idle, Walking, Running = 10, Flying }\n"
            "class T : Behaviour {\n"
            "    public enum Team { Red = -1, Blue, Green, }\n"
            "    public State state = State.Running;\n"
            "    public int idle, walking, running, flying, red, blue;\n"
            "    public string text;\n"
            "    public bool same, less;\n"
            "    Team team;\n"
            "    void Start() {\n"
            "        idle = (int)State.Idle; walking = (int)State.Walking; running = (int)state; flying = (int)State.Flying;\n"
            "        red = (int)Team.Red; blue = (int)Team.Blue;\n"
            "        team = Team.Green;\n"
            "        State s = (State)11;\n"
            "        text = \"state=\" + s + \" team=\" + team + \" odd=\" + (State)42;\n"
            "        same = s == State.Flying;\n"
            "        less = State.Idle < State.Walking;\n"
            "        Debug.Log(State.Walking);\n"
            "    }\n"
            "}\n");
        CHECK(w.loaded);
        CHECK_NEAR(w.F("idle"), 0.0f, 1e-6f);
        CHECK_NEAR(w.F("walking"), 1.0f, 1e-6f);
        CHECK_NEAR(w.F("running"), 10.0f, 1e-6f);
        CHECK_NEAR(w.F("flying"), 11.0f, 1e-6f);
        CHECK_NEAR(w.F("red"), -1.0f, 1e-6f);
        CHECK_NEAR(w.F("blue"), 0.0f, 1e-6f);
        CHECK(w.S("text") == "state=Flying team=Green odd=42");
        CHECK(w.sc->VM()->GetGlobal("same").AsBool());
        CHECK(w.sc->VM()->GetGlobal("less").AsBool());
        CHECK(LogHas("Walking"));
    }
    {   // a const can hold an enum value; enums are fields' and parameters' types
        CHECK(Clean(
            "enum Dir { Left, Right }\n"
            "class T : Behaviour {\n"
            "    const Dir Start = Dir.Right;\n"
            "    Dir Flip(Dir d) { return d == Dir.Left ? Dir.Right : Dir.Left; }\n"
            "    void Update() { Debug.Log(Flip(Start)); }\n"
            "}\n"));
    }
    {   // mistakes, with line and column
        auto d = Diags(
            "enum State { Idle, Walking, Idle }\n"                       // 1: duplicate member
            "enum State { A }\n"                                         // 2: duplicate enum
            "enum Big { A = 2147483647, B }\n"                           // 3: overflow
            "enum Bad { A = 1.5f }\n"                                    // 4: not an int
            "enum Rigidbody { A }\n"                                     // 5: engine type's name
            "class T : Behaviour {\n"                                    // 6
            "    void Start() {\n"                                       // 7
            "        State s = State.Walkin;\n"                         // 8
            "        int n = State.Idle;\n"                              // 9: no implicit enum -> int
            "        State t = 1;\n"                                     // 10: no implicit int -> enum
            "        State u = Walking;\n"                               // 11: needs State.
            "        Debug.Log(s + \" \" + n + t + u);\n"                // 12
            "    }\n"
            "}\n");
        CHECK(Has(d, 0, "'Idle' is already in State", 1, 29));
        CHECK(Has(d, 0, "'State' is declared twice", 2, 6));
        CHECK(Has(d, 0, "too big for an int", 3));
        CHECK(Has(d, 0, "An enum value must be a whole number", 4, 16));
        CHECK(Has(d, 0, "'Rigidbody' is already an engine type", 5, 6));
        CHECK(Has(d, 0, "State has no 'Walkin'. Did you mean 'Walking'?", 8, 25));
        CHECK(Has(d, 0, "is State but needs int", 9));
        CHECK(Has(d, 0, "is int but needs State", 10));
        CHECK(Has(d, 0, "Unknown name 'Walking'. Did you mean State.Walking?", 11, 19));
        // `: int` would only repeat the default
        CHECK(Has(Diags("enum E : int { A }\n" + Wrap("")), 0, "Enums are always ints: delete the ': type'", 1, 8));
        // switch on a float or bool is an if
        CHECK(Has(Diags(Wrap("    void Start() { bool b = true; switch (b) { default: break; } }")), 0, "not bool: use if / else", 2));
    }
}

static void Switches() {
    {   // ints, strings, enums; several labels share a section; default; break, return and continue end a section
        World w(
            "enum Weapon { Sword, Bow, Staff }\n"
            "class T : Behaviour {\n"
            "    const int Boss = 99;\n"
            "    public string a, b, c;\n"
            "    public int loopSum;\n"
            "    string Name(int n) {\n"
            "        switch (n) {\n"
            "            case 1: return \"one\";\n"
            "            case 2:\n"
            "            case 3: return \"two or three\";\n"
            "            case Boss: return \"boss\";\n"
            "            case -4: return \"minus four\";\n"
            "            default: return \"many\";\n"
            "        }\n"
            "    }\n"
            "    int Damage(Weapon w) {\n"
            "        int dmg = 0;\n"
            "        switch (w) {\n"
            "            case Weapon.Sword: dmg = 10; break;\n"
            "            case Weapon.Bow: { dmg = 6; break; }\n"
            "        }\n"
            "        return dmg;\n"
            "    }\n"
            "    void Start() {\n"
            "        a = Name(1) + \",\" + Name(3) + \",\" + Name(99) + \",\" + Name(-4) + \",\" + Name(7);\n"
            "        b = \"\" + Damage(Weapon.Sword) + \",\" + Damage(Weapon.Bow) + \",\" + Damage(Weapon.Staff);\n"
            "        string cmd = \"jump\";\n"
            "        switch (cmd) { case \"run\": c = \"R\"; break; case \"jump\": c = \"J\"; break; default: c = \"?\"; break; }\n"

            "        for (int i = 0; i < 6; i++) {\n"
            "            switch (i % 3) {\n"
            "                case 0: continue;\n"
            "                case 1: loopSum += i; break;\n"
            "                default: loopSum += 100; break;\n"
            "            }\n"
            "            loopSum += 1000;\n"
            "        }\n"

            "    }\n"
            "}\n");
        CHECK(w.loaded);
        CHECK(w.S("a") == "one,two or three,boss,minus four,many");
        CHECK(w.S("b") == "10,6,0");
        CHECK(w.S("c") == "J");
        // i=1: +1 +1000, i=2: +100 +1000, i=4: +4 +1000, i=5: +100 +1000 (0 and 3 continue)
        CHECK_NEAR(w.F("loopSum"), 4205.0f, 1e-3f);
    }
    {   // a switch whose every section returns (with a default) ends the method
        CHECK(Clean(Wrap(
            "    int Sign(int n) { switch (n) { case 0: return 0; default: return n > 0 ? 1 : -1; } }\n"
            "    void Start() { Debug.Log(Sign(4)); }\n")));
    }
    {   // mistakes, with line and column
        auto d = Diags(Wrap(
            "    int field = 3;\n"                                                   // 2
            "    void Start() {\n"                                                   // 3
            "        int n = 2;\n"                                                   // 4
            "        switch (n) { case 1: Debug.Log(1); case 2: break; }\n"          // 5: fall through
            "        switch (n) { case 1: break; case 1: break; }\n"                 // 6: duplicate
            "        switch (n) { case field: break; }\n"                            // 7: not constant
            "        switch (n) { case \"x\": break; }\n"                            // 8: wrong type
            "        switch (n) { default: break; default: break; }\n"               // 9: two defaults
            "        switch (transform) { default: break; }\n"                       // 10: bad type
            "        switch (n) { Debug.Log(1); }\n"                                 // 11: no label
            "        switch (n) { case 3: Debug.Log(3); }\n"                         // 12: last falls out
            "        continue;\n"                                                    // 13
            "    }\n"));
        CHECK(Has(d, 0, "can't fall through from one case to the next", 5, 22));
        CHECK(Has(d, 0, "case 1 appears twice", 6, 37));
        CHECK(Has(d, 0, "A case value must be fixed", 7, 27));
        CHECK(Has(d, 0, "is string but needs int", 8, 27));
        CHECK(Has(d, 0, "only one default", 9, 38));
        CHECK(Has(d, 0, "switch works on an int, a string or an enum, not Transform", 10, 17));
        CHECK(Has(d, 0, "Expected case or default", 11, 22));
        CHECK(Has(d, 0, "can't fall through", 12, 22));
        CHECK(Has(d, 0, "continue only works inside a loop", 13, 9));
        // case values are folded: 7 / 2 and 7 % 4 are both 3
        CHECK(Has(Diags(Wrap("    void Start() { int n = 3; switch (n) { case 7 / 2: break; case 7 % 4: break; } }")), 0,
                  "case 3 appears twice", 2, 63));
        // break inside a switch is fine outside a loop, continue isn't
        CHECK(!Has(d, 0, "break only works inside a loop"));
    }
}

static void Arrays() {
    {   // creating, reading, writing, Length, defaults, initialisers, arrays of vectors and enums
        World w(
            "enum Kind { A, B, C }\n"
            "class T : Behaviour {\n"
            "    int[] scores = { 5, 10, 15, };\n"
            "    public int len, sum, zero, last, jag, kinds;\n"
            "    public float vx;\n"
            "    public string empty, text;\n"
            "    public bool isNull, sameRef;\n"
            "    void Start() {\n"
            "        len = scores.Length;\n"
            "        int[] more = new int[4];\n"
            "        zero = more[3];\n"
            "        more[1] = 7; more[1] += 3; more[2]++;\n"
            "        sum = more[0] + more[1] + more[2] + scores[2];\n"
            "        string[] names = new string[2];\n"
            "        empty = \"[\" + names[0] + \"]\";\n"
            "        float[] fs = new float[] { 1, 2.5f };\n"
            "        Vector3[] ps = new Vector3[3];\n"
            "        ps[1] = new Vector3(1, 2, 3);\n"
            "        ps[1].x = 9;\n"
            "        vx = ps[1].x + fs[1];\n"
            "        var copy = scores;\n"
            "        copy[0] = 100;\n"
            "        last = scores[0];\n"
            "        sameRef = copy == scores;\n"
            "        int[][] grid = new int[3][];\n"
            "        grid[1] = new int[] { 4, 5 };\n"
            "        jag = grid[1][1] + grid.Length;\n"
            "        isNull = grid[0] == null;\n"
            "        Kind[] ks = { Kind.C, Kind.B };\n"
            "        kinds = (int)ks[0];\n"
            "        text = \"\" + ks[1];\n"
            "    }\n"
            "}\n");
        CHECK(w.loaded);
        CHECK_NEAR(w.F("len"), 3.0f, 1e-6f);
        CHECK_NEAR(w.F("zero"), 0.0f, 1e-6f);
        CHECK_NEAR(w.F("sum"), 0 + 10 + 1 + 15, 1e-6f);
        CHECK(w.S("empty") == "[]");          // text starts empty, as everywhere in OkayScript
        CHECK_NEAR(w.F("vx"), 11.5f, 1e-6f);
        CHECK_NEAR(w.F("last"), 100.0f, 1e-6f);          // arrays are references
        CHECK(w.sc->VM()->GetGlobal("sameRef").AsBool());
        CHECK_NEAR(w.F("jag"), 8.0f, 1e-6f);
        CHECK(w.sc->VM()->GetGlobal("isNull").AsBool());
        CHECK_NEAR(w.F("kinds"), 2.0f, 1e-6f);
        CHECK(w.S("text") == "B");
    }
    {   // an index is evaluated once in a compound assignment
        World w(Wrap(
            "    public int i, a0, a1;\n"
            "    void Start() { int[] a = new int[3]; a[i++] += 5; a[i++]++; a0 = a[0]; a1 = a[1]; }\n"));
        CHECK_NEAR(w.F("i"), 2.0f, 1e-6f);
        CHECK_NEAR(w.F("a0"), 5.0f, 1e-6f);
        CHECK_NEAR(w.F("a1"), 1.0f, 1e-6f);
    }
    {   // runtime failures are reported with their line, never a crash
        g_errs.clear();
        World a(Wrap("    public int k = 3;\n    void Start() { int[] a = new int[2]; a[k] = 1; }\n"));
        CHECK(ErrHas("Start() line 3: Index 3 is outside the array (length 2)"));
        World b(Wrap("    public int k = -1;\n    void Start() { int[] a = new int[k]; Debug.Log(a.Length); }\n"));
        CHECK(ErrHas("Start() line 3: An array can't have a negative length (-1)"));
        World c(Wrap("    public int k = 2000000000;\n    void Start() { int[] a = new int[k]; Debug.Log(a.Length); }\n"));
        CHECK(ErrHas("Start() line 3: An array of 2000000000 items is too big"));
        World e(Wrap("    int[] none;\n    void Start() { Debug.Log(none.Length); }\n"));
        CHECK(ErrHas("Start() line 3: 'none' is null here"));
    }
    {   // mistakes, with line and column
        auto d = Diags(Wrap(
            "    void Start() {\n"                                  // 2
            "        int[] a = new int[2];\n"                       // 3
            "        a[\"x\"] = 1;\n"                               // 4
            "        a[0] = \"x\";\n"                               // 5
            "        a.Length = 3;\n"                               // 6
            "        int n = 3; n[0] = 1;\n"                        // 7
            "        int[,] m;\n"                                   // 8
            "        float[] f = { 1, \"two\" };\n"                 // 9
            "        int[] g = new int[2] { 1, 2, 3 };\n"           // 10
            "        var h = { 1, 2 };\n"                           // 11
            "        Debug.Log(a.Count);\n"                         // 12
            "        var k = new[] { 1, \"x\" };\n"              // 13
            "        var q = new[] { 1, 2.5f };\n"                // 14
            "        float r = q[0];\n"                           // 15
            "        Debug.Log(k.Length + r);\n"                  // 16
            "    }\n"));
        CHECK(Has(d, 0, "An array index must be an int, not string", 4, 11));
        CHECK(Has(d, 0, "is string but needs int", 5, 16));
        CHECK(Has(d, 0, "'Length' can only be read", 6, 11));
        CHECK(Has(d, 0, "int values can't be indexed with [ ]", 7, 21));
        CHECK(Has(d, 0, "Multi-dimensional arrays", 8, 13));
        CHECK(Has(d, 0, "is string but needs float", 9, 26));
        CHECK(Has(d, 0, "The array has length 2 but 3 values", 10, 19));
        CHECK(Has(d, 0, "`var` can't tell the type of { ... }", 11, 13));
        CHECK(Has(d, 0, "int[] has no 'Count'. Did you mean 'Length'?", 12, 21));
        CHECK(Has(d, 0, "new[] can't tell the items' type", 13, 17));
        CHECK(!Has(d, 0, "", 14));   // ints with floats make a float[]
        CHECK(!Has(d, 0, "", 15));
    }
}

static void Foreach() {
    {   // over arrays, with var or a type, break/continue, nested
        World w(Wrap(
            "    public int sum, odd, pairs;\n"
            "    public float fsum;\n"
            "    public string names = \"\";\n"
            "    void Start() {\n"
            "        int[] xs = { 1, 2, 3, 4, 5, 6 };\n"
            "        foreach (var x in xs) { if (x == 5) break; sum += x; }\n"
            "        foreach (int x in xs) { if (x % 2 == 0) continue; odd += x; }\n"
            "        foreach (float f in xs) fsum += f / 2;\n"
            "        foreach (var a in new int[] { 1, 2 }) foreach (var b in xs) pairs++;\n"
            "        string[] ns = { \"a\", \"b\" };\n"
            "        foreach (string n in ns) names += n;\n"
            "    }\n"));
        CHECK(w.loaded);
        CHECK_NEAR(w.F("sum"), 10.0f, 1e-6f);
        CHECK_NEAR(w.F("odd"), 9.0f, 1e-6f);
        CHECK_NEAR(w.F("fsum"), 10.5f, 1e-6f);
        CHECK_NEAR(w.F("pairs"), 12.0f, 1e-6f);
        CHECK(w.S("names") == "ab");
    }
    {   // a method can return from inside a foreach; a foreach in a switch
        World w(Wrap(
            "    public int found;\n"
            "    int IndexOf(int[] xs, int v) { int i = 0; foreach (var x in xs) { if (x == v) return i; i++; } return -1; }\n"
            "    void Start() {\n"
            "        int[] xs = { 4, 8, 15 };\n"
            "        switch (xs.Length) { case 3: foreach (var x in xs) { if (x == 8) break; } found = IndexOf(xs, 15); break; }\n"
            "    }\n"));
        CHECK_NEAR(w.F("found"), 2.0f, 1e-6f);
    }
    {   // mistakes, with line and column
        g_errs.clear();
        auto d = Diags(Wrap(
            "    void Start() {\n"                                // 2
            "        int[] xs = { 1 };\n"                         // 3
            "        foreach (var x in xs) { x = 2; }\n"          // 4
            "        foreach (var y in 5) { }\n"                  // 5
            "        foreach (string s in xs) { }\n"              // 6
            "        foreach (var z of xs) { }\n"                 // 7
            "        foreach (int xs in xs) { }\n"                // 8
            "    }\n"));
        CHECK(Has(d, 0, "'x' is the foreach variable, so it can't be changed", 4, 33));
        CHECK(Has(d, 0, "foreach needs an array, not int", 5, 27));
        CHECK(Has(d, 0, "The items of xs are int but 's' is string", 6, 25));
        CHECK(Has(d, 0, "Expected 'in' here", 7, 24));
        CHECK(Has(d, 0, "'xs' is already a variable here", 8, 22));
        // a null array is a runtime error with its line
        World w(Wrap("    int[] none;\n    void Start() { foreach (var x in none) Debug.Log(x); }\n"));
        CHECK(ErrHas("Start() line 3: 'none' is null here"));
    }
}

// Several objects in one scene running `src` (on `language`); each object's field `field` after `frames` updates.
static std::vector<float> RunMany(const std::string& src, int objects, const char* field, int frames = 1,
                                  const char* language = NextScriptVM::kLanguage) {
    Scene scene("many");
    std::vector<ScriptComponent*> scs;
    for (int i = 0; i < objects; ++i) {
        auto* sc = scene.CreateGameObject("O" + std::to_string(i))->AddComponent<ScriptComponent>(language);
        std::string err;
        if (!sc->LoadSource(src, &err)) std::printf("    load failed: %s\n", err.c_str());
        scs.push_back(sc);
    }
    scene.Start();
    for (int i = 0; i < frames; ++i) scene.Update(kDt);
    std::vector<float> out;
    for (auto* sc : scs) out.push_back(sc->VM()->GetGlobal(field).AsFloat());
    return out;
}

static void Statics() {
    {   // one value shared by every object running the script: a count of all the enemies
        const std::string src = Wrap(
            "    // statics: alive count\n"
            "    static int alive;\n"
            "    public int seen;\n"
            "    void Start() { alive++; }\n"
            "    void Update() { seen = alive; }\n");
        auto seen = RunMany(src, 3, "seen");
        CHECK(seen.size() == 3 && seen[0] == 3.0f && seen[2] == 3.0f);
        // the old runtime shares them the same way
        auto old = RunMany(src, 3, "seen", 1, "okayscript");
        CHECK(old.size() == 3 && old[0] == 3.0f && old[2] == 3.0f);
    }
    {   // the starting value is set once, by the first object: unique ids
        auto ids = RunMany(Wrap(
            "    // statics: ids\n"
            "    static int nextId = 100;\n"
            "    public int id;\n"
            "    void Start() { id = nextId++; }\n"), 3, "id");
        CHECK(ids.size() == 3 && ids[0] == 100.0f && ids[1] == 101.0f && ids[2] == 102.0f);
    }
    {   // statics last one play session: a new scene, or the same scene rebuilt (what Stop does), starts them again
        const std::string src = Wrap(
            "    // statics: restart\n"
            "    static int nextId = 1;\n"
            "    public int id;\n"
            "    void Start() { id = nextId++; }\n");
        auto first = RunMany(src, 3, "id");
        auto again = RunMany(src, 3, "id");
        CHECK(first.size() == 3 && first[0] == 1.0f && first[2] == 3.0f);
        CHECK(again == first);
        Scene scene("rebuilt");
        auto* sc = scene.CreateGameObject("A")->AddComponent<ScriptComponent>(NextScriptVM::kLanguage);
        CHECK(sc->LoadSource(src));
        scene.Update(kDt);
        CHECK_NEAR(sc->VM()->GetGlobal("id").AsFloat(), 1.0f, 1e-6f);
        const std::string saved = SceneSerializer::Serialize(scene);
        scene.CreateGameObject("B")->AddComponent<ScriptComponent>(NextScriptVM::kLanguage)->LoadSource(src);
        scene.Update(kDt);   // B gets id 2 in this session
        CHECK(SceneSerializer::Deserialize(scene, saved));
        scene.Update(kDt);
        GameObject* a = scene.Find("A");
        auto* rebuilt = a ? a->GetComponent<ScriptComponent>() : nullptr;
        CHECK(rebuilt && rebuilt->VM() && rebuilt->VM()->GetGlobal("id").AsFloat() == 1.0f);
    }
    {   // within one session the value lasts even while no object of the script exists (wave after wave)
        const std::string src = Wrap(
            "    // statics: waves\n"
            "    static int nextId = 1;\n"
            "    public int id;\n"
            "    void Start() { id = nextId++; }\n");
        Scene scene("waves");
        GameObject* first = scene.CreateGameObject("E1");
        first->AddComponent<ScriptComponent>(NextScriptVM::kLanguage)->LoadSource(src);
        scene.Update(kDt);
        scene.Destroy(first);
        scene.Update(kDt);
        auto* next = scene.CreateGameObject("E2")->AddComponent<ScriptComponent>(NextScriptVM::kLanguage);
        next->LoadSource(src);
        scene.Update(kDt);
        CHECK_NEAR(next->VM()->GetGlobal("id").AsFloat(), 2.0f, 1e-6f);
    }
    {   // static methods: helpers that need no object; Class.member works too
        World w(Wrap(
            "    // statics: helpers\n"
            "    public static int calls;\n"
            "    public float a, b;\n"
            "    public int seen;\n"
            "    static float Remap(float v, float lo, float hi) { calls++; return (v - lo) / (hi - lo); }\n"
            "    static int Twice(int n) { return n <= 0 ? 0 : 2 + Twice(n - 1); }\n"
            "    void Start() { a = Remap(5, 0, 10); b = T.Remap(3, 2, 4); T.calls += Twice(3); seen = calls; }\n"));
        CHECK(w.loaded);
        CHECK_NEAR(w.F("a"), 0.5f, 1e-6f);
        CHECK_NEAR(w.F("b"), 0.5f, 1e-6f);
        CHECK_NEAR(w.F("seen"), 8.0f, 1e-6f);
        CHECK_NEAR(w.F("calls"), 8.0f, 1e-6f);   // a static field reads like any other
    }
    {   // mistakes, with line and column
        auto d = Diags(Wrap(
            "    public float speed = 1;\n"                                      // 2
            "    static int count;\n"                                            // 3
            "    static void Bad() {\n"                                          // 4
            "        speed = 2;\n"                                               // 5
            "        transform.Translate(1, 0, 0);\n"                           // 6
            "        Helper();\n"                                                // 7
            "        this.speed = 3;\n"                                          // 8
            "    }\n"
            "    void Helper() { count = T.speed; }\n"                           // 10
            "    static void Update() { Bad(); }\n"                              // 11
            "    static const int Max = 3;\n"                                    // 12
            "    void Start() { Helper(); Debug.Log(Max); }\n"));                // 13
        CHECK(Has(d, 0, "'speed' belongs to each object, so the static method Bad() can't use it", 5, 9));
        CHECK(Has(d, 0, "'transform' belongs to each object, so the static method Bad() can't use it", 6, 9));
        CHECK(Has(d, 0, "'Helper' belongs to each object, so the static method Bad() can't use it", 7, 9));
        CHECK(Has(d, 0, "A static method has no `this`", 8, 9));
        CHECK(Has(d, 0, "'speed' isn't static: use it without T.", 10, 31));
        CHECK(Has(d, 0, "Update() can't be static: the engine calls it on each object", 11, 17));
        CHECK(Has(d, 0, "A const is already shared: delete `static`", 12, 5));
        CHECK(Has(Diags(Wrap("    void Start() { static int n = 1; Debug.Log(n); }")), 0, "static only works on fields and methods", 2, 20));
        CHECK(Has(Diags("static class T : Behaviour { }\n"), 0, "A script class can't be static", 1, 1));
    }
    {   // the Inspector sets one object's fields, never a shared static
        World w(Wrap("    // statics: inspector\n    public static int shared = 1;\n    void Start() { Debug.Log(shared); }\n"));
        w.sc->fields["shared"] = "9";
        w.sc->ApplyFieldOverrides();
        CHECK_NEAR(w.F("shared"), 1.0f, 1e-6f);
    }
}

static void Interpolation() {
    {   // values of every type in { }, {{ }} for braces, :F<n> for fixed decimals, text in holes, escapes
        World w(
            "enum Mode { Easy, Hard }\n"
            "class T : Behaviour {\n"
            "    public string a, b, c, d, e;\n"
            "    public int score = 5;\n"
            "    void Start() {\n"
            "        float t = 3.14159f;\n"
            "        bool alive = true;\n"
            "        Mode m = Mode.Hard;\n"
            "        a = $\"Score: {score} next={score + 1} {{not a hole}}\";\n"
            "        b = $\"{m} {alive} {new Vector3(1, 2, 3)} {gameObject.name}\";\n"
            "        c = $\"time {t:F2}s, lives {score:F1}, whole {t:F0}\";\n"
            "        d = $\"{(alive ? \"yes\" : \"no\")} {$\"in{score}\"}\";\n"
            "        e = $\"say \\\"hi\\\"\\tok\";\n"
            "    }\n"
            "}\n");
        CHECK(w.loaded);
        CHECK(w.S("a") == "Score: 5 next=6 {not a hole}");
        CHECK(w.S("b") == "Hard True (1, 2, 3) Player");
        CHECK(w.S("c") == "time 3.14s, lives 5.0, whole 3");
        CHECK(w.S("d") == "yes in5");
        CHECK(w.S("e") == "say \"hi\"\tok");
    }
    {   // the same text on the old runtime
        const std::string src = Wrap(
            "    public string s;\n    public int score = 7;\n"
            "    void Start() { s = $\"Score: {score} next={score + 1} {{x}} {score * 2}\"; }\n");
        World next(src), old(src, "okayscript");
        CHECK(next.S("s") == "Score: 7 next=8 {x} 14");
        CHECK(next.S("s") == old.S("s"));
    }
    {   // mistakes, with line and column (inside { } too)
        auto d = Diags(Wrap(
            "    void Hit() { }\n"                                  // 2
            "    void Start() {\n"                                  // 3
            "        int hp = 3;\n"                                 // 4
            "        string a = $\"hp={hpp}\";\n"                  // 5
            "        string b = $\"{}\";\n"                         // 6
            "        string c = $\"a } b\";\n"                      // 7
            "        string d = $\"{hp:X4}\";\n"                    // 8
            "        string e = $\"{a:F2}\";\n"                     // 9
            "        string f = $\"{Hit()}\";\n"                    // 10
            "        string g = $\"{hp b}\";\n"                     // 11
            "        Debug.Log(a + b + c + d + e + f + g);\n"       // 12
            "        string h = $\"open {hp\";\n"                   // 13
            "    }\n"));
        CHECK(Has(d, 0, "Unknown name 'hpp'. Did you mean 'hp'?", 5, 26));
        CHECK(Has(d, 0, "Empty { } in $\"...\"", 6, 22));
        CHECK(Has(d, 0, "A } on its own in $\"...\" is written }}", 7, 24));
        CHECK(Has(d, 0, "Only :F formats work here, like {time:F1}", 8, 26));
        CHECK(Has(d, 0, ":F needs a number, not string", 9, 25));
        CHECK(Has(d, 0, "doesn't produce a value", 10, 23));
        CHECK(Has(d, 0, "Expected '}' to close the { } here", 11, 26));
        CHECK(Has(d, 0, "This { in $\"...\" is never closed", 13, 27));
    }
}

static void Parity() {
    // The same C# snippet gives the same results on the old OkayScript runtime.
    const std::string src =
        "enum State { Idle, Walking, Running = 10, Flying }\n"
        "class T : Behaviour {\n"
        "    public string log = \"\";\n"
        "    public int sum;\n"
        "    string Name(int n) { switch (n) { case 1: return \"one\"; case 2: case 3: return \"few\"; default: return \"many\"; } }\n"
        "    void Start() {\n"
        "        State s = State.Flying;\n"
        "        log = \"\" + (int)s + \",\" + Name(1) + Name(3) + Name(9);\n"
        "        switch (s) { case State.Idle: log += \"!idle\"; break; case State.Flying: log += \"!fly\"; break; }\n"
        "        int[] xs = { 1, 2, 3 };\n"
        "        int[] ys = new int[3];\n"
        "        ys[1] = 5;\n"
        "        var zs = new[] { 4, 5 };\n"
        "        foreach (var x in xs) sum += x;\n"
        "        sum += ys[1] + xs.Length + zs[1] + zs.Length * 100;\n"
        "    }\n"
        "}\n";
    World next(src), old(src, "okayscript");
    CHECK(next.loaded && old.loaded);
    CHECK(next.S("log") == "11,onefewmany!fly");
    CHECK(next.S("log") == old.S("log"));
    CHECK_NEAR(next.F("sum"), 6 + 5 + 3 + 5 + 200, 1e-6f);
    CHECK_NEAR(next.F("sum"), old.F("sum"), 1e-6f);
    // One deliberate difference: an enum turned into text is its name, as in C#
    // (the old runtime prints the number).
}

static void BackCompat() {
    // Words that are new keywords in context only stay usable as names.
    CHECK(Clean(Wrap("    public int in_ = 1;\n    void Start() { int enumCount = 2, cases = 3; Debug.Log(enumCount + cases + in_); }\n")));
    // Features still to come keep their message.
    auto d = Diags(Wrap("    virtual void V() { }\n    void Start() { try { } }\n"));
    CHECK(Has(d, 0, "virtual isn't supported"));
    CHECK(Has(d, 0, "try / catch isn't supported yet"));
}

static void Fuzz() {
    // Truncated and garbled programs never crash the compiler.
    const std::string src =
        "enum E { A, B = 2 }\nclass T : Behaviour {\n int[] a = { 1, 2 };\n void Start() {\n"
        "  switch (E.A) { case E.A: foreach (var x in a) { a[x % 2] += 1; } break; default: break; }\n"
        "  string s = $\"{a[0]:F1} {{ {$\"{E.B}\"}\";\n }\n}\n";
    int n = 0;
    for (std::size_t i = 0; i <= src.size(); ++i) { Diags(src.substr(0, i)); ++n; }
    for (std::size_t i = 0; i < src.size(); ++i) { std::string s = src; s.erase(i, 1); Diags(s); }
    CHECK_EQ(n, (int)src.size() + 1);
    std::string deep = Wrap("    int[] a = " + std::string(500, '{') + "1" + std::string(500, '}') + ";\n");
    CHECK(Count(Diags(deep), 0) > 0);
    std::string nested = "\"x\"";
    for (int i = 0; i < 40; ++i) nested = "$\"{" + nested + "}\"";
    CHECK(Has(Diags(Wrap("    void Start() { Debug.Log(" + nested + "); }")), 0, "nested too deeply"));
    // Statements nested far too deep are an error, not a stack overflow.
    CHECK(Has(Diags(Wrap("    void Start() " + std::string(100000, '{') + std::string(100000, '}'))), 0, "This code is nested too deeply"));
    std::string ifs;
    for (int i = 0; i < 20000; ++i) ifs += "if (true) ";
    CHECK(Has(Diags(Wrap("    void Start() { " + ifs + "Debug.Log(1); }")), 0, "This code is nested too deeply"));
}

int main() {
    RUN_SUITE("okayscript_next_csharp");
    Log::sink = [](Log::Level l, const std::string& m) {
        if (l >= Log::Level::Error) g_errs.push_back(m); else g_logs.push_back(m);
    };
    Enums();
    Switches();
    Arrays();
    Foreach();
    Statics();
    Interpolation();
    Parity();
    BackCompat();
    Fuzz();
    Log::sink = nullptr;
    TEST_MAIN_RESULT();
}
