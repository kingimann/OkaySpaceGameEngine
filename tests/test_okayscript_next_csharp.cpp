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
        CHECK(Has(d, 0, "foreach needs an array, a List or a Dictionary, not int", 5, 27));
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
    {   // numbers that aren't finite read the same in $"..." with :F as anywhere else
        World w(Wrap(
            "    public string a, b, c;\n"
            "    public float zero;\n"
            "    void Start() { float n = zero / zero; float inf = 1 / zero; a = $\"{n:F2}|{inf:F1}|{-inf:F0}\"; b = \"\" + n + \"|\" + inf; c = $\"{-inf}\"; }\n"));
        CHECK(w.S("a") == "NaN|Infinity|-Infinity");
        CHECK(w.S("b") == "NaN|Infinity");
        CHECK(w.S("c") == "-Infinity");
    }
    {   // a line that ends inside text with a \ is unclosed text, reported on that line with a real column
        for (const char* start : {"$\"abc\\", "\"abc\\"}) {
            auto d = Diags(Wrap(std::string("    void Start() { string s = ") + start + "\n\"; Debug.Log(s); }\n"));
            CHECK(Has(d, 0, "This text is never closed", 2));
            CHECK(std::none_of(d.begin(), d.end(), [](const ScriptDiagnostic& x) {
                return x.column < 1 || x.message.find('\n') != std::string::npos;
            }));
            if (Has(d, 0, "isn't an escape")) Dump(d);
            CHECK(!Has(d, 0, "isn't an escape"));
        }
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

static void NullOperators() {
    {   // ?? picks a fallback, ??= fills an empty reference once, ?. skips the rest when the object is gone
        Scene scene("null-ops");
        GameObject* player = scene.CreateGameObject("Player");
        GameObject* crate = scene.CreateGameObject("Crate");
        auto* sc = player->AddComponent<ScriptComponent>(NextScriptVM::kLanguage);
        std::string err;
        CHECK(sc->LoadSource(Wrap(
            "    public string picked, after, path;\n"
            "    public int looks, moved, len;\n"
            "    GameObject target;\n"
            "    GameObject Look() { looks++; return GameObject.Find(\"Crate\"); }\n"
            "    void Start() {\n"
            "        picked = (GameObject.Find(\"Boss\") ?? GameObject.Find(\"Nobody\") ?? GameObject.Find(\"Crate\")).name;\n"
            "        int[] none = null;\n"
            "        len = (none ?? new int[] { 1, 2, 3 }).Length;\n"
            "    }\n"
            "    void Update() {\n"
            "        target ??= Look();\n"
            "        target?.transform.Translate(1, 0, 0);\n"
            "        Transform t = target?.transform;\n"
            "        if (t != null) moved++;\n"
            "        after = (target ?? gameObject).name;\n"
            "    }\n"), &err));
        if (!err.empty()) std::printf("    %s\n", err.c_str());
        scene.Start();
        for (int i = 0; i < 3; ++i) scene.Update(kDt);
        CHECK(sc->VM()->GetGlobal("picked").AsString() == "Crate");
        CHECK_NEAR(sc->VM()->GetGlobal("len").AsFloat(), 3.0f, 1e-6f);
        CHECK_NEAR(sc->VM()->GetGlobal("looks").AsFloat(), 1.0f, 1e-6f);     // ??= only looked once
        CHECK_NEAR(crate->transform->Position().x, 3.0f, 1e-5f);
        CHECK_NEAR(sc->VM()->GetGlobal("moved").AsFloat(), 3.0f, 1e-6f);
        CHECK(sc->VM()->GetGlobal("after").AsString() == "Crate");
        // A destroyed object counts as null: ?. skips, ?? falls back, ??= looks again.
        // (Destroy takes effect at the end of the frame, so the crate still moves once.)
        scene.Destroy(crate);
        scene.Update(kDt);
        scene.Update(kDt);
        scene.Update(kDt);
        CHECK(sc->VM()->GetGlobal("after").AsString() == "Player");
        CHECK_NEAR(sc->VM()->GetGlobal("moved").AsFloat(), 4.0f, 1e-6f);
        CHECK_NEAR(sc->VM()->GetGlobal("looks").AsFloat(), 3.0f, 1e-6f);
    }
    {   // a ternary with a number like .5f still parses
        CHECK(Clean(Wrap("    void Start() { bool b = true; float f = b ? .5f : 1f; Debug.Log(f); }")));
    }
    {   // mistakes, with line and column
        auto d = Diags(Wrap(
            "    void Start() {\n"                                         // 2
            "        int n = 3;\n"                                         // 3
            "        int m = n ?? 4;\n"                                    // 4
            "        GameObject g = GameObject.Find(\"A\");\n"           // 5
            "        Vector3 p = g?.transform.position;\n"                 // 6
            "        n ??= 2;\n"                                           // 7
            "        GameObject h = g ?? transform;\n"                     // 8
            "        int[] xs = null;\n"                                   // 9
            "        int k = xs?.Length;\n"                                // 10
            "        Debug.Log(m + p + h + k);\n"                          // 11
            "    }\n"));
        CHECK(Has(d, 0, "?? only works on objects and arrays, which can be null: an int never is", 4, 19));
        CHECK(Has(d, 0, "?. gives null when 'g' is null, but a Vector3 can't be null: check g != null first", 6, 22));
        CHECK(Has(d, 0, "??= only works on objects and arrays, which can be null: an int never is", 7, 11));
        CHECK(Has(d, 0, "is Transform but needs GameObject", 8, 29));
        CHECK(Has(d, 0, "?. gives null when 'xs' is null, but an int can't be null: check xs != null first", 10, 19));
        auto d2 = Diags(Wrap("    void Start() { Vector3 v = Vector3.zero; float x = v?.x; int[] a = null; a?.Length++; Debug.Log(x); }"));
        CHECK(Has(d2, 0, "?. only works on objects and arrays, which can be null: use . here", 2, 59));
        CHECK(Has(d2, 0, "Something reached with ?. can't be changed", 2, 87));
        std::string chain = "null";
        for (int i = 0; i < 100000; ++i) chain += " ?? null";
        CHECK(Has(Diags(Wrap("    void Start() { GameObject g = " + chain + "; }")), 0, "nested too deeply"));
    }
}

static void Classes() {
    {   // plain classes: fields with starting values, constructors, methods, `this`, references, null
        World w(
            "class Node {\n"
            "    public int value;\n"
            "    public Node next;\n"
            "    public Node(int v) { value = v; }\n"
            "    public Node Prepend(int v) { Node n = new Node(v); n.next = this; return n; }\n"
            "}\n"
            "class Stats {\n"
            "    public int hp = 10;\n"
            "    public int max;\n"
            "    public Stats(int max) { this.max = max; hp = max; }\n"
            "    public bool Hit(int damage) { hp -= damage; return hp <= 0; }\n"
            "    public Stats Copy() { Stats s = new Stats(max); s.hp = hp; return s; }\n"
            "}\n"
            "class Point {\n"
            "    public float x, y;\n"
            "    public Point() { }\n"
            "    public Point(float x, float y) { this.x = x; this.y = y; }\n"
            "}\n"
            "class Bag { public int count; }\n"
            "class T : Behaviour {\n"
            "    public int sum, count, hp, copyHp, bagCount;\n"
            "    public float px, speed;\n"
            "    public bool firstHit, dead, missing;\n"
            "    public string text;\n"
            "    void SetSpeed(float speed) { this.speed = speed; }\n"
            "    void Start() {\n"
            "        Node head = null;\n"
            "        for (int i = 1; i <= 3; i++) { Node n = new Node(i); n.next = head; head = n; }\n"
            "        head = head.Prepend(4);\n"
            "        for (Node p = head; p != null; p = p.next) { sum += p.value; count++; }\n"
            "        Stats s = new Stats(5);\n"
            "        firstHit = s.Hit(3);\n"
            "        dead = s.Hit(3);\n"
            "        Stats c = s.Copy();\n"
            "        c.hp = 100;\n"
            "        Stats r = s;\n"
            "        r.hp = 7;\n"
            "        hp = s.hp;\n"
            "        copyHp = c.hp;\n"
            "        Point[] ps = new Point[2];\n"
            "        ps[0] = new Point(1.5f, 2);\n"
            "        px = ps[0].x + new Point().x;\n"
            "        missing = ps[1] == null;\n"
            "        text = \"\" + s + $\" {head.next.value}\";\n"
            "        Bag bag = (ps[1] == null ? null : new Bag()) ?? new Bag();\n"
            "        bag.count += 2;\n"
            "        bagCount = bag.count;\n"
            "        SetSpeed(4);\n"
            "    }\n"
            "}\n");
        CHECK(w.loaded);
        CHECK_NEAR(w.F("sum"), 10.0f, 1e-6f);
        CHECK_NEAR(w.F("count"), 4.0f, 1e-6f);
        CHECK(!w.sc->VM()->GetGlobal("firstHit").AsBool());
        CHECK(w.sc->VM()->GetGlobal("dead").AsBool());
        CHECK_NEAR(w.F("hp"), 7.0f, 1e-6f);           // r is the same object as s
        CHECK_NEAR(w.F("copyHp"), 100.0f, 1e-6f);     // the copy is its own object
        CHECK_NEAR(w.F("px"), 1.5f, 1e-6f);
        CHECK(w.sc->VM()->GetGlobal("missing").AsBool());
        CHECK(w.S("text") == "Stats 3");
        CHECK_NEAR(w.F("speed"), 4.0f, 1e-6f);        // this.speed is the field, not the parameter
        CHECK_NEAR(w.F("bagCount"), 2.0f, 1e-6f);
    }
    {   // the example in docs/scripting.md
        CHECK(Clean(
            "class Item {\n"
            "    public string name;\n"
            "    public int count = 1;\n"
            "    public Item(string name) { this.name = name; }\n"
            "    public bool Use() { count--; return count <= 0; }\n"
            "}\n"
            "class Inventory : Behaviour {\n"
            "    Item[] slots = new Item[4];\n"
            "    void Start() { slots[0] = new Item(\"Potion\"); }\n"
            "    public void UseFirst() {\n"
            "        if (slots[0] != null && slots[0].Use()) slots[0] = null;\n"
            "    }\n"
            "}\n"));
    }
    {   // a very long chain of objects is freed without running out of stack
        World w(
            "class Link { public Link next; }\n"
            "class T : Behaviour {\n"
            "    public int made;\n"
            "    void Start() { Link head = null; for (int i = 0; i < 300000; i++) { Link l = new Link(); l.next = head; head = l; made++; } }\n"
            "}\n");
        CHECK_NEAR(w.F("made"), 300000.0f, 1e-3f);
    }
    {   // runtime: using null is an error with its line
        g_errs.clear();
        World w("class Box { public int v; }\nclass T : Behaviour {\n    void Start() { Box b = null; Debug.Log(b.v); }\n}\n");
        CHECK(ErrHas("Start() line 3: 'b' is null here"));
        World loop("class Node { public Node next = new Node(); }\nclass T : Behaviour {\n    void Start() { Node n = new Node(); Debug.Log(n); }\n}\n");
        CHECK(ErrHas("Making a Node makes more objects without end"));
    }
    {   // mistakes, with line and column
        auto d = Diags(
            "class Bag {\n"                                              // 1
            "    int secret;\n"                                          // 2
            "    void Hidden() { secret = 1; }\n"                        // 3
            "    public int Count() { Hidden(); return secret; }\n"      // 4
            "    public void Move() { transform.Translate(1, 0, 0); }\n" // 5
            "    static int shared;\n"                                   // 6
            "    public Bag(int n) { return n; }\n"                      // 7
            "    public Bag(int a) { }\n"                                // 8
            "}\n"                                                         // 9
            "class Node { public int value; }\n"                         // 10
            "class Fancy : Node, Bag { }\n"                              // 11
            "class Node { }\n"                                           // 12
            "class T : Behaviour {\n"                                    // 13
            "    T() { }\n"                                              // 14
            "    void Start() {\n"                                       // 15
            "        Bag b = new Bag(1);\n"                              // 16
            "        Debug.Log(b.secret);\n"                             // 17
            "        b.Hidden();\n"                                      // 18
            "        Bag c = new Bag();\n"                               // 19
            "        T t = new T();\n"                                   // 20
            "        Node n = new Node();\n"                             // 21
            "        Debug.Log(n.valu + b.Count());\n"                   // 22
            "    }\n"
            "}\n"
            "class Other : Behaviour { }\n");                            // 25
        CHECK(Has(d, 0, "'secret' is private to Bag: make it public to use it here", 17, 21));
        CHECK(Has(d, 0, "'Hidden' is private to Bag: make it public to use it here", 18, 11));
        CHECK(Has(d, 0, "'transform' only exists in the Behaviour class: Bag isn't on an object", 5, 26));
        CHECK(Has(d, 0, "static members only work in the Behaviour class for now", 6, 16));
        CHECK(Has(d, 0, "A constructor can't return a value", 7, 25));
        CHECK(Has(d, 0, "Bag already has a constructor that takes 1 value", 8, 12));
        CHECK(Has(d, 0, "A class extends one class, named first: Bag is a class", 11, 21));
        CHECK(Has(d, 0, "'Node' is declared twice", 12, 7));
        CHECK(Has(d, 0, "A Behaviour script has no constructor: set things up in Awake() or Start()", 14, 5));
        CHECK(Has(d, 0, "Bag has no constructor that takes 0 values", 19, 17));
        CHECK(Has(d, 0, "Behaviour scripts are added to objects, not made with new", 20, 15));
        CHECK(Has(d, 0, "Node has no 'valu'. Did you mean 'value'?", 22, 21));
        CHECK(Has(d, 0, "Only one Behaviour class per script: put 'Other' in its own script", 25, 7));
        CHECK(!Has(d, 1, "'secret' is never read"));
    }
}

static void Collections() {
    {   // do / while runs the body at least once
        World w(Wrap("    public int n, tries;\n    void Start() { do { tries++; n += 3; } while (n < 10); do { tries++; } while (false); }\n"));
        CHECK(w.loaded);
        CHECK_NEAR(w.F("n"), 12.0f, 1e-6f);
        CHECK_NEAR(w.F("tries"), 5.0f, 1e-6f);
    }
    {   // List<T>: add / insert / remove / index / sort / foreach / Count, a reference like an array
        World w(Wrap(
            "    public int count, sum, idx, first, removedAt, total;\n"
            "    public bool has, removed, missing;\n"
            "    public string text, names;\n"
            "    void Start() {\n"
            "        List<int> xs = new List<int>();\n"
            "        xs.Add(5); xs.Add(1); xs.Add(9);\n"
            "        xs.Insert(0, 7);\n"
            "        xs[1] += 10;\n"
            "        has = xs.Contains(9);\n"
            "        idx = xs.IndexOf(9);\n"
            "        removed = xs.Remove(1);\n"
            "        missing = !xs.Remove(42);\n"
            "        xs.Sort();\n"
            "        first = xs[0];\n"
            "        foreach (int x in xs) sum += x;\n"
            "        count = xs.Count;\n"
            "        var copy = xs;\n"
            "        copy.RemoveAt(0);\n"
            "        removedAt = xs.Count;\n"
            "        text = \"\" + xs;\n"
            "        var ns = new List<string> { \"b\", \"a\", \"c\" };\n"
            "        ns.Sort(); ns.Reverse();\n"
            "        foreach (var n in ns) names += n;\n"
            "        int[] arr = ns.Count > 2 ? new List<int> { 1, 2, 3 }.ToArray() : new int[0];\n"
            "        List<int> more = new List<int> { 4 }; more.AddRange(xs);\n"
            "        total = arr.Length + more.Count;\n"
            "        xs.Clear();\n"
            "        count += xs.Count;\n"
            "    }\n"));
        CHECK(w.loaded);
        CHECK(w.sc->VM()->GetGlobal("has").AsBool());
        CHECK_NEAR(w.F("idx"), 3.0f, 1e-6f);            // [7, 15, 1, 9]
        CHECK(w.sc->VM()->GetGlobal("removed").AsBool());
        CHECK(w.sc->VM()->GetGlobal("missing").AsBool());
        CHECK_NEAR(w.F("first"), 7.0f, 1e-6f);          // sorted [7, 9, 15]
        CHECK_NEAR(w.F("sum"), 31.0f, 1e-6f);
        CHECK_NEAR(w.F("count"), 3.0f, 1e-6f);          // 3, then Clear adds 0
        CHECK_NEAR(w.F("removedAt"), 2.0f, 1e-6f);      // copy is the same list
        CHECK(w.S("text") == "List<int>");
        CHECK(w.S("names") == "cba");
        CHECK_NEAR(w.F("total"), 3.0f + 3.0f, 1e-6f);   // [1,2,3] and [4, 9, 15]
    }
    {   // Dictionary<K,V>: indexer get/set, Add, ContainsKey, Remove, GetValueOrDefault, Keys/Values, foreach in insertion order
        World w(
            "enum Item { Sword, Shield, Potion }\n"
            "class T : Behaviour {\n"
            "    public int sword, potions, missing, count, keyCount;\n"
            "    public bool has, gone, added, again;\n"
            "    public string order = \"\", valuesText = \"\";\n"
            "    void Start() {\n"
            "        var bag = new Dictionary<Item, int>();\n"
            "        bag[Item.Potion] = 2;\n"
            "        bag.Add(Item.Sword, 1);\n"
            "        bag[Item.Potion] += 3;\n"
            "        added = bag.TryAdd(Item.Shield, 1);\n"
            "        again = bag.TryAdd(Item.Shield, 9);\n"
            "        sword = bag[Item.Sword];\n"
            "        potions = bag[Item.Potion];\n"
            "        has = bag.ContainsKey(Item.Shield);\n"
            "        gone = bag.Remove(Item.Shield);\n"
            "        missing = bag.GetValueOrDefault(Item.Shield, -1);\n"
            "        foreach (var kv in bag) order += kv.Key + \"=\" + kv.Value + \";\";\n"
            "        foreach (int v in bag.Values) valuesText += v;\n"
            "        keyCount = bag.Keys.Count;\n"
            "        count = bag.Count;\n"
            "        Dictionary<string, GameObject> byName = new Dictionary<string, GameObject>();\n"
            "        byName[\"me\"] = gameObject;\n"
            "        if (byName[\"me\"] == gameObject) count += 100;\n"
            "    }\n"
            "}\n");
        CHECK(w.loaded);
        CHECK_NEAR(w.F("sword"), 1.0f, 1e-6f);
        CHECK_NEAR(w.F("potions"), 5.0f, 1e-6f);
        CHECK(w.sc->VM()->GetGlobal("added").AsBool());
        CHECK(!w.sc->VM()->GetGlobal("again").AsBool());
        CHECK(w.sc->VM()->GetGlobal("has").AsBool());
        CHECK(w.sc->VM()->GetGlobal("gone").AsBool());
        CHECK_NEAR(w.F("missing"), -1.0f, 1e-6f);
        CHECK(w.S("order") == "Potion=5;Sword=1;");
        CHECK(w.S("valuesText") == "51");
        CHECK_NEAR(w.F("keyCount"), 2.0f, 1e-6f);
        CHECK_NEAR(w.F("count"), 102.0f, 1e-6f);
    }
    {   // the same list and dictionary code gives the same results on the old runtime
        const std::string src = Wrap(
            "    public int count, sum, idx;\n"
            "    public bool has;\n"
            "    void Start() {\n"
            "        List<int> xs = new List<int>();\n"
            "        xs.Add(5); xs.Add(1); xs.Add(9); xs.Insert(0, 7); xs.Remove(1); xs.Sort();\n"
            "        foreach (int x in xs) sum += x;\n"
            "        idx = xs.IndexOf(9); count = xs.Count;\n"
            "        Dictionary<string, int> d = new Dictionary<string, int>();\n"
            "        d[\"a\"] = 2; d.Add(\"b\", 3);\n"
            "        has = d.ContainsKey(\"b\");\n"
            "        sum += d.GetValueOrDefault(\"a\", 0) + d.GetValueOrDefault(\"z\", 10);\n"
            "    }\n");
        World next(src), old(src, "okayscript");
        for (const char* f : {"count", "sum", "idx"}) CHECK_NEAR(next.F(f), old.F(f), 1e-6f);
        CHECK(next.sc->VM()->GetGlobal("has").AsBool() == old.sc->VM()->GetGlobal("has").AsBool());
        CHECK_NEAR(next.F("sum"), 21.0f + 12.0f, 1e-6f);
    }
    {   // runtime errors with their line
        g_errs.clear();
        World a(Wrap("    void Start() { var d = new Dictionary<string, int>(); Debug.Log(d[\"hp\"]); }\n"));
        CHECK(ErrHas("Start() line 2: The key \"hp\" isn't in this Dictionary<string, int>"));
        World b(Wrap("    void Start() { var d = new Dictionary<string, int>(); d.Add(\"a\", 1); d.Add(\"a\", 2); }\n"));
        CHECK(ErrHas("Start() line 2: The key \"a\" is already in this Dictionary<string, int>"));
        World c(Wrap("    void Start() { var xs = new List<int> { 1, 2 }; foreach (var x in xs) xs.Add(x); }\n"));
        CHECK(ErrHas("Start() line 2: The List<int> was changed during foreach"));
        World e(Wrap("    void Start() { var xs = new List<int>(); xs.RemoveAt(0); }\n"));
        CHECK(ErrHas("Start() line 2: Index 0 is outside the List<int> (length 0)"));
    }
    {   // mistakes, with line and column
        auto d = Diags(Wrap(
            "    void Start() {\n"                                              // 2
            "        List<int> xs = new List<int>();\n"                         // 3
            "        xs.Add(\"a\");\n"                                        // 4
            "        var d = new Dictionary<float, int>();\n"                   // 5
            "        HashSet<int> h;\n"                                          // 6
            "        List<int, int> bad;\n"                                      // 7
            "        xs.Push(1);\n"                                              // 8
            "        var m = new Dictionary<string, int> { 1 };\n"              // 9
            "        do { } while (1);\n"                                        // 10
            "        Debug.Log(xs.Length);\n"                                    // 11
            "    }\n"));
        CHECK(Has(d, 0, "is string but needs int", 4, 16));
        CHECK(Has(d, 0, "A Dictionary key must be an int, string, bool, enum or object, not float", 5, 32));
        CHECK(Has(d, 0, "Only List<T>, Dictionary<K, V>, Func<...> and Action<...> are generic here", 6, 9));
        CHECK(Has(d, 0, "List takes one type: List<int>", 7, 9));
        CHECK(Has(d, 0, "List<int> has no method 'Push'", 8, 12));
        CHECK(Has(d, 0, "Fill a Dictionary with Add or d[key] = value", 9, 17));
        CHECK(Has(d, 0, "The while condition must be true/false, not int", 10, 23));
        CHECK(Has(d, 0, "List<int> has no 'Length'. Did you mean 'Count'?", 11, 22));
    }
}

static void Lambdas() {
    {   // Func / Action, closures that see later changes and outlive their method, Invoke and ?.Invoke
        World w(
            "class Button { public Action onClick; }\n"
            "class T : Behaviour {\n"
            "    public int a, b, total, c1, c2, clicks;\n"
            "    Func<int, int> twice = x => x * 2;\n"
            "    Func<int> MakeCounter() { int n = 0; return () => { n++; return n; }; }\n"
            "    void Start() {\n"
            "        a = twice(4);\n"
            "        int k = 10;\n"
            "        Func<int, int> addK = x => x + k;\n"
            "        k = 20;\n"
            "        b = addK(1);\n"
            "        Action<int> add = x => { total += x; };\n"
            "        add(5); add.Invoke(2);\n"
            "        Func<int> counter = MakeCounter();\n"
            "        counter(); c1 = counter();\n"
            "        c2 = MakeCounter()();\n"
            "        Button btn = new Button();\n"
            "        btn.onClick?.Invoke();\n"
            "        btn.onClick = () => clicks++;\n"
            "        btn.onClick?.Invoke();\n"
            "        btn.onClick();\n"
            "    }\n"
            "}\n");
        CHECK(w.loaded);
        CHECK_NEAR(w.F("a"), 8.0f, 1e-6f);
        CHECK_NEAR(w.F("b"), 21.0f, 1e-6f);            // captured by reference, as in C#
        CHECK_NEAR(w.F("total"), 7.0f, 1e-6f);
        CHECK_NEAR(w.F("c1"), 2.0f, 1e-6f);            // the counter keeps its own n
        CHECK_NEAR(w.F("c2"), 1.0f, 1e-6f);
        CHECK_NEAR(w.F("clicks"), 2.0f, 1e-6f);
    }
    {   // LINQ-style methods on List<T> and arrays
        World w(Wrap(
            "    public int big, tens, first, fod, odd, sum, sum2, min, maxNeg, found, foundAt, removed, total, sorted, arrCount, arrFirst, top;\n"
            "    public bool any, all;\n"
            "    public string names;\n"
            "    void Start() {\n"
            "        List<int> xs = new List<int> { 5, 1, 9, 3 };\n"
            "        big = xs.Where(x => x > 2).Count();\n"
            "        tens = xs.Select(x => x * 10).First();\n"
            "        names = xs.Select(x => \"n\" + x).ToList()[1];\n"
            "        top = xs.OrderByDescending(x => x).First() + xs.OrderBy(x => -x).ToList()[1] * 100;\n"
            "        first = xs.First(x => x > 4);\n"
            "        fod = xs.FirstOrDefault(x => x > 100);\n"
            "        any = xs.Any(x => x == 9);\n"
            "        all = xs.All(x => x > 0);\n"
            "        odd = xs.Count(x => x % 2 == 1);\n"
            "        sum = xs.Sum(); sum2 = xs.Sum(x => x * 2);\n"
            "        min = xs.Min(); maxNeg = xs.Max(x => -x);\n"
            "        found = xs.Find(x => x > 8); foundAt = xs.FindIndex(x => x == 3);\n"
            "        xs.ForEach(x => total += x);\n"
            "        removed = xs.RemoveAll(x => x < 3);\n"
            "        xs.Sort((p, q) => q - p);\n"
            "        sorted = xs[0];\n"
            "        int[] arr = { 3, 1, 2 };\n"
            "        arrCount = arr.Where(x => x > 1).ToList().Count;\n"
            "        arrFirst = arr.OrderBy(x => x).First();\n"
            "    }\n"));
        CHECK(w.loaded);
        CHECK_NEAR(w.F("big"), 3.0f, 1e-6f);
        CHECK_NEAR(w.F("tens"), 50.0f, 1e-6f);
        CHECK(w.S("names") == "n1");
        CHECK_NEAR(w.F("top"), 9.0f + 5.0f * 100.0f, 1e-6f);
        CHECK_NEAR(w.F("first"), 5.0f, 1e-6f);
        CHECK_NEAR(w.F("fod"), 0.0f, 1e-6f);
        CHECK(w.sc->VM()->GetGlobal("any").AsBool());
        CHECK(w.sc->VM()->GetGlobal("all").AsBool());
        CHECK_NEAR(w.F("odd"), 4.0f, 1e-6f);
        CHECK_NEAR(w.F("sum"), 18.0f, 1e-6f);
        CHECK_NEAR(w.F("sum2"), 36.0f, 1e-6f);
        CHECK_NEAR(w.F("min"), 1.0f, 1e-6f);
        CHECK_NEAR(w.F("maxNeg"), -1.0f, 1e-6f);
        CHECK_NEAR(w.F("found"), 9.0f, 1e-6f);
        CHECK_NEAR(w.F("foundAt"), 3.0f, 1e-6f);
        CHECK_NEAR(w.F("total"), 18.0f, 1e-6f);
        CHECK_NEAR(w.F("removed"), 1.0f, 1e-6f);
        CHECK_NEAR(w.F("sorted"), 9.0f, 1e-6f);
        CHECK_NEAR(w.F("arrCount"), 2.0f, 1e-6f);
        CHECK_NEAR(w.F("arrFirst"), 1.0f, 1e-6f);
    }
    {   // C#'s capture rules: a variable declared in a loop is new each time round; a for
        // loop's own variable is one variable; nested lambdas share what they capture
        World w(Wrap(
            "    public int a0, a2, f0, sum, inner, total;\n"
            "    void Start() {\n"
            "        var each = new List<Func<int>>();\n"
            "        for (int i = 0; i < 3; i++) { int j = i * 10; each.Add(() => j); }\n"
            "        a0 = each[0](); a2 = each[2]();\n"
            "        var shared = new List<Func<int>>();\n"
            "        for (int i = 0; i < 3; i++) shared.Add(() => i);\n"
            "        f0 = shared[0]();\n"
            "        foreach (int v in new int[] { 1, 2, 3 }) each.Add(() => v);\n"
            "        sum = each[3]() + each[4]() + each[5]();\n"
            "        int n = 1;\n"
            "        Func<Func<int>> outer = () => () => n * 100;\n"
            "        Func<int> got = outer();\n"
            "        n = 4;\n"
            "        inner = got();\n"
            "        Action<int> addTo = x => { Action bump = () => total += x + n; bump(); };\n"
            "        addTo(1); addTo(2);\n"
            "    }\n"));
        CHECK(w.loaded);
        CHECK_NEAR(w.F("a0"), 0.0f, 1e-6f);
        CHECK_NEAR(w.F("a2"), 20.0f, 1e-6f);
        CHECK_NEAR(w.F("f0"), 3.0f, 1e-6f);
        CHECK_NEAR(w.F("sum"), 6.0f, 1e-6f);
        CHECK_NEAR(w.F("inner"), 400.0f, 1e-6f);
        CHECK_NEAR(w.F("total"), 11.0f, 1e-6f);
    }
    {   // a lambda made in a plain class's method runs for that object; one stored in an
        // object keeps the variables it captured; a comparison that makes no sense doesn't crash
        World w(
            "class Counter {\n"
            "    public int hits;\n"
            "    public Action Hook() { return () => hits++; }\n"
            "}\n"
            "class T : Behaviour {\n"
            "    public int hits, first, lambdaDepth;\n"
            "    Func<int, int> fact;\n"
            "    void Start() {\n"
            "        Counter c = new Counter();\n"
            "        Action a = c.Hook();\n"
            "        a(); a();\n"
            "        hits = c.hits;\n"
            "        var xs = new List<int> { 4, 2, 7, 1, 9, 3, 8, 5, 6, 0 };\n"
            "        xs.Sort((p, q) => -1);\n"
            "        xs.Sort((p, q) => p - q);\n"
            "        first = xs[0];\n"
            "        fact = n => n <= 1 ? 1 : n * fact(n - 1);\n"
            "        lambdaDepth = fact(5);\n"
            "    }\n"
            "}\n");
        CHECK(w.loaded);
        CHECK_NEAR(w.F("hits"), 2.0f, 1e-6f);
        CHECK_NEAR(w.F("first"), 0.0f, 1e-6f);
        CHECK_NEAR(w.F("lambdaDepth"), 120.0f, 1e-6f);
        g_errs.clear();
        World r(Wrap("    Func<int, int> f;\n    void Start() { f = n => f(n + 1); f(0); }\n"));
        CHECK(ErrHas("This lambda calls itself too deeply (endless recursion?)"));
    }
    {   // a lambda kept in a static field runs for the object that made it, and says so once that object is gone
        const std::string src = Wrap(
            "    static Func<int> shared;\n"
            "    public int n, got;\n"
            "    void Start() { n = gameObject.name == \"A\" ? 7 : 1; if (shared == null) shared = () => n; }\n"
            "    void Update() { if (gameObject.name == \"B\") got = shared(); }\n");
        Scene scene{"S"};
        GameObject* a = scene.CreateGameObject("A");
        GameObject* b = scene.CreateGameObject("B");
        std::string err;
        CHECK(a->AddComponent<ScriptComponent>(NextScriptVM::kLanguage)->LoadSource(src, &err));
        auto* sb = b->AddComponent<ScriptComponent>(NextScriptVM::kLanguage);
        CHECK(sb->LoadSource(src, &err));
        scene.Start();
        scene.Update(kDt);
        CHECK_NEAR(sb->VM()->GetGlobal("got").AsFloat(), 7.0f, 1e-6f);
        g_errs.clear();
        scene.Destroy(a);
        scene.Update(kDt);
        scene.Update(kDt);
        CHECK(ErrHas("Update() line 5: This function belongs to a script that was destroyed"));
    }
    {   // the same LINQ-style code on the old runtime
        const std::string src = Wrap(
            "    public int big, sum, odd;\n"
            "    public bool any;\n"
            "    void Start() {\n"
            "        List<int> xs = new List<int>();\n"
            "        xs.Add(5); xs.Add(1); xs.Add(9); xs.Add(3);\n"
            "        big = xs.Where(x => x > 2).Count();\n"
            "        sum = xs.Sum();\n"
            "        odd = xs.Count(x => x % 2 == 1);\n"
            "        any = xs.Any(x => x == 9);\n"
            "    }\n");
        World next(src), old(src, "okayscript");
        for (const char* f : {"big", "sum", "odd"}) CHECK_NEAR(next.F(f), old.F(f), 1e-6f);
        CHECK(next.sc->VM()->GetGlobal("any").AsBool() == old.sc->VM()->GetGlobal("any").AsBool());
    }
    {   // runtime errors with their line
        g_errs.clear();
        World a(Wrap("    void Start() { var xs = new List<int>(); Debug.Log(xs.First()); }\n"));
        CHECK(ErrHas("Start() line 2: First() found nothing in this List<int>"));
        World b(Wrap("    void Start() { var xs = new List<int>(); Debug.Log(xs.Max()); }\n"));
        CHECK(ErrHas("Start() line 2: Max() of an empty List<int>"));
    }
    {   // mistakes, with line and column
        auto d = Diags(Wrap(
            "    void Start() {\n"                                              // 2
            "        var f = x => x;\n"                                         // 3
            "        Func<int, int> g = (a, b) => a;\n"                         // 4
            "        Func<int, bool> h = x => x + 1;\n"                         // 5
            "        var xs = new List<int>();\n"                               // 6
            "        var ys = xs.Select(x => { Debug.Log(x); });\n"             // 7
            "        Debug.Log(x => x);\n"                                      // 8
            "        int n = 3; n(2);\n"                                        // 9
            "        for (int i = 0; i < 3; i++) { Action a = () => { break; }; a(); }\n"   // 10
            "        Func<int, int> k = x => { if (x > 0) return 1; };\n"       // 11
            "        Debug.Log(g(1) + h(1) + ys.Count + k(1));\n"
            "    }\n"));
        CHECK(Has(d, 0, "`var` can't tell a lambda's type: write Func<...> or Action<...>", 3, 13));
        CHECK(Has(d, 0, "This lambda takes 2 values but Func<int, int> gives it 1", 4, 28));
        CHECK(Has(d, 0, "is int but needs bool", 5, 36));
        CHECK(Has(d, 0, "Select needs a lambda that gives a value, like x => x * 2", 7, 28));
        CHECK(Has(d, 0, "A lambda only works where a Func or Action is expected", 8, 19));
        CHECK(Has(d, 0, "'n' is int, not a function", 9, 20));
        CHECK(Has(d, 0, "break only works inside a loop or a switch", 10, 58));
        CHECK(Has(d, 0, "This lambda must return an int on every path", 11, 28));
        auto e = Diags(Wrap(
            "    void Start() {\n"                                                         // 2
            "        Func<int, int> f = true ? (x => x) : (x => 2);\n"                       // 3
            "        Action a = () => 5;\n"                                                // 4
            "        var xs = new List<int>();\n"                                          // 5
            "        Debug.Log(f(1) + xs.OrderBy(x => xs).Count() + xs.Sum(x => \"a\"));\n"   // 6
            "        a(); a.Run();\n"                                                      // 7
            "    }\n"));
        CHECK(Has(e, 0, "A lambda only works where a Func or Action is expected", 3, 36));
        CHECK(Has(e, 0, "This lambda works out a value but an Action doesn't use it", 4, 26));
        CHECK(Has(e, 0, "OrderBy needs a key it can sort (a number, text or an enum), like x => x.distance, not List<int>", 6, 37));
        CHECK(Has(e, 0, "Sum() works on numbers, not string", 6, 59));
        CHECK(Has(e, 0, "A function has no 'Run': call it with Invoke(...) or (...)", 7, 16));
    }
    {   // a null function given to a LINQ-style method is a script error
        g_errs.clear();
        World w(Wrap("    Func<int, bool> keep;\n    void Start() { var xs = new List<int> { 1 }; Debug.Log(xs.Where(keep).Count()); }\n"));
        CHECK(ErrHas("Start() line 3: The function given to Where() is null"));
    }
}

static void Events() {
    {   // event fields, += / -= with lambdas and methods, called in the order added
        World w(
            "class Health {\n"
            "    public event Action<int> Damaged;\n"
            "    public event Action Died;\n"
            "    public int hp = 10;\n"
            "    public void Hit(int d) {\n"
            "        hp -= d;\n"
            "        Damaged?.Invoke(d);\n"
            "        if (hp <= 0) Died?.Invoke();\n"
            "    }\n"
            "}\n"
            "class T : Behaviour {\n"
            "    public int taken, deaths, order, afterRemove, cleared;\n"
            "    public bool same, noneLeft;\n"
            "    void OnDamaged(int d) { taken += d; order = order * 10 + 1; }\n"
            "    void OnDied() { deaths++; }\n"
            "    void Start() {\n"
            "        Health h = new Health();\n"
            "        h.Damaged += OnDamaged;\n"
            "        h.Damaged += d => order = order * 10 + 2;\n"
            "        h.Died += OnDied;\n"
            "        h.Hit(4);\n"
            "        h.Damaged -= OnDamaged;\n"
            "        h.Hit(6);\n"
            "        afterRemove = taken;\n"
            "        h.Died -= OnDied;\n"
            "        h.Hit(1);\n"
            "        Action a = OnDied, b = OnDied;\n"
            "        same = a == b;\n"
            "        Action c = a;\n"
            "        c += b;\n"
            "        c -= a; c -= b;\n"
            "        noneLeft = c == null;\n"
            "        Func<int> one = () => 1, two = () => 2;\n"
            "        Func<int> both = one;\n"
            "        both += two;\n"
            "        cleared = both();\n"
            "    }\n"
            "}\n");
        CHECK(w.loaded);
        CHECK_NEAR(w.F("taken"), 4.0f, 1e-6f);
        CHECK_NEAR(w.F("afterRemove"), 4.0f, 1e-6f);
        CHECK_NEAR(w.F("deaths"), 1.0f, 1e-6f);
        CHECK_NEAR(w.F("order"), 1222.0f, 1e-6f);        // 1 then 2 (first hit), 2 (second), 2 (third)
        CHECK(w.sc->VM()->GetGlobal("same").AsBool());     // the same method of the same object
        CHECK(w.sc->VM()->GetGlobal("noneLeft").AsBool());
        CHECK_NEAR(w.F("cleared"), 2.0f, 1e-6f);           // a Func with two parts gives the last one's value
    }
    {   // a handler that removes itself while the event runs doesn't upset it; methods given to LINQ-style calls
        World w(
            "class Bus { public event Action Ping; public void Send() { Ping?.Invoke(); } }\n"
            "class T : Behaviour {\n"
            "    public int pings, alive;\n"
            "    Bus bus = new Bus();\n"
            "    void Once() { pings++; bus.Ping -= Once; }\n"
            "    bool IsAlive(int hp) { return hp > 0; }\n"
            "    void Start() {\n"
            "        bus.Ping += Once;\n"
            "        bus.Ping += () => pings += 10;\n"
            "        bus.Send(); bus.Send();\n"
            "        alive = new List<int> { 3, 0, 5 }.Count(IsAlive);\n"
            "    }\n"
            "}\n");
        CHECK(w.loaded);
        CHECK_NEAR(w.F("pings"), 21.0f, 1e-6f);
        CHECK_NEAR(w.F("alive"), 2.0f, 1e-6f);
    }
    {   // mistakes, with line and column
        auto d = Diags(
            "class Health { public event Action<int> Damaged; public event int hp; void Fire() { Damaged(1); Damaged = null; } }\n"   // 1
            "class T : Behaviour {\n"                                                       // 2
            "    void OnHit() { }\n"                                                        // 3
            "    void Start() {\n"                                                          // 4
            "        Health h = new Health();\n"                                            // 5
            "        h.Damaged(3);\n"                                                       // 6
            "        h.Damaged = null;\n"                                                   // 7
            "        var f = h.Damaged;\n"                                                  // 8
            "        h.Damaged += OnHit;\n"                                                 // 9
            "        Debug.Log(OnHit);\n"                                                   // 10
            "        h.Damaged *= null;\n"                                                  // 11
            "    }\n"
            "    public event void Bad() { }\n"                                             // 13
            "}\n");
        CHECK(Has(d, 0, "An event holds functions: event Action hp; or event Action<int> hp;", 1, 67));
        CHECK(Has(d, 0, "'Damaged' is an event of Health: outside Health it can only be added to (+=) or removed from (-=)", 6, 11));
        CHECK(Has(d, 0, "'Damaged' is an event of Health: outside Health it can only be added to (+=) or removed from (-=)", 7, 11));
        CHECK(Has(d, 0, "'Damaged' is an event of Health: outside Health it can only be added to (+=) or removed from (-=)", 8, 19));
        CHECK(Has(d, 0, "OnHit() takes 0 values, not 1", 9, 22));
        CHECK(Has(d, 0, "'OnHit' is a method: call it with OnHit()", 10, 19));
        CHECK(Has(d, 0, "Only fields can be events: event Action<int> Damaged;", 13, 12));
        CHECK(std::none_of(d.begin(), d.end(), [](const ScriptDiagnostic& x) { return x.line == 1 && x.message.find("Damaged") != std::string::npos; }));
    }
}

static void Inheritance() {
    {   // classes that extend classes, interfaces, virtual / override / abstract, base, protected, is / as / casts
        World w(
            "interface IDamageable { void TakeDamage(int amount); bool IsDead(); }\n"
            "abstract class Enemy : IDamageable {\n"
            "    protected int hp;\n"
            "    public string kind = \"enemy\";\n"
            "    public Enemy(int hp) { this.hp = hp; }\n"
            "    public virtual void TakeDamage(int amount) { hp -= amount; }\n"
            "    public bool IsDead() { return hp <= 0; }\n"
            "    public abstract int Reward();\n"
            "    public int Hp() { return hp; }\n"
            "    public string Describe() { return kind + \":\" + Reward(); }\n"
            "}\n"
            "class Grunt : Enemy {\n"
            "    public Grunt() : base(10) { kind = \"grunt\"; }\n"
            "    public override int Reward() { return 5; }\n"
            "}\n"
            "class Boss : Enemy {\n"
            "    int armor = 2;\n"
            "    public Boss(int hp) : base(hp) { }\n"
            "    public override void TakeDamage(int amount) { base.TakeDamage(amount - armor); }\n"
            "    public override int Reward() { return 100; }\n"
            "}\n"
            "class Crate : IDamageable {\n"
            "    public int hits;\n"
            "    public void TakeDamage(int amount) { hits++; }\n"
            "    public bool IsDead() { return hits >= 2; }\n"
            "}\n"
            "class T : Behaviour {\n"
            "    public int gruntHp, bossHp, reward, dead, bosses, crateHits;\n"
            "    public string desc;\n"
            "    public bool notEnemy, same;\n"
            "    void Start() {\n"
            "        List<IDamageable> targets = new List<IDamageable> { new Grunt(), new Boss(50), new Crate() };\n"
            "        foreach (IDamageable t in targets) t.TakeDamage(10);\n"
            "        foreach (var t in targets) t.TakeDamage(10);\n"
            "        Enemy g = targets[0] as Enemy;\n"
            "        gruntHp = g.Hp();\n"
            "        Boss b = (Boss)targets[1];\n"
            "        bossHp = b.Hp();\n"
            "        foreach (var t in targets) {\n"
            "            if (t is Enemy) reward += (t as Enemy).Reward();\n"
            "            if (t.IsDead()) dead++;\n"
            "            if (t is Boss) bosses++;\n"
            "        }\n"
            "        crateHits = (targets[2] as Crate).hits;\n"
            "        desc = g.Describe() + \",\" + b.Describe();\n"
            "        notEnemy = targets[2] as Enemy == null;\n"
            "        Enemy e = b;\n"
            "        same = e == targets[1];\n"
            "    }\n"
            "}\n");
        CHECK(w.loaded);
        CHECK_NEAR(w.F("gruntHp"), -10.0f, 1e-6f);
        CHECK_NEAR(w.F("bossHp"), 34.0f, 1e-6f);          // the override takes armor off, then runs the base method
        CHECK_NEAR(w.F("reward"), 105.0f, 1e-6f);
        CHECK_NEAR(w.F("dead"), 2.0f, 1e-6f);
        CHECK_NEAR(w.F("bosses"), 1.0f, 1e-6f);
        CHECK_NEAR(w.F("crateHits"), 2.0f, 1e-6f);
        CHECK(w.S("desc") == "grunt:5,enemy:100");          // base code calls the object's own Reward()
        CHECK(w.sc->VM()->GetGlobal("notEnemy").AsBool());
        CHECK(w.sc->VM()->GetGlobal("same").AsBool());
    }
    {   // a cast to the wrong class is an error with its line
        g_errs.clear();
        World w("class A { } class B : A { } class C : A { }\nclass T : Behaviour {\n    void Start() { A a = new C(); B b = (B)a; Debug.Log(b); }\n}\n");
        CHECK(ErrHas("Start() line 3: This C isn't a B"));
    }
    {   // mistakes, with line and column
        auto d = Diags(
            "interface IA { void Run(); }\n"   // 1
            "class Base { public void Walk() { } public virtual int Score() { return 1; } int secret; protected int shared; }\n"   // 2
            "class D : Base {\n"   // 3
            "    public void Walk() { }\n"   // 4
            "    public override string Score() { return \"\"; }\n"   // 5
            "    public override void Fly() { }\n"   // 6
            "    public void Use() { secret = 1; shared = 2; base.Nope(); }\n"   // 7
            "}\n"   // 8
            "class C : IA { }\n"   // 9
            "abstract class A { public abstract void Go(); }\n"   // 10
            "class B : A { }\n"   // 11
            "class P { public P(int x) { } }\n"   // 12
            "class Q : P { }\n"   // 13
            "class R : P { public R() : base(\"a\") { } }\n"   // 14
            "class K : A { public override void Go() { base.Go(); } }\n"   // 15
            "class X : Y { }\n"   // 16
            "class Y : X { }\n"   // 17
            "class T : Behaviour, IA {\n"   // 18
            "    void Start() {\n"   // 19
            "        A a = new A();\n"   // 20
            "        IA i = new IA();\n"   // 21
            "        D d = new D(); C c = new C();\n"   // 22
            "        bool x = d is C;\n"   // 23
            "        Debug.Log(\"\" + a + i + x + c);\n"   // 24
            "    }\n"   // 25
            "    public void Run() { }\n"   // 26
            "}\n"   // 27
            );
        CHECK(Has(d, 0, "Walk() is already in Base: to change what it does, mark it virtual there and override here", 4, 17));
        CHECK(Has(d, 0, "An override must match what it overrides: int Score() in Base", 5, 28));
        CHECK(Has(d, 0, "There's no virtual Fly() in what D extends, so there's nothing to override", 6, 26));
        CHECK(Has(d, 0, "'secret' is private to Base: make it protected (or public) to use it in classes that extend it", 7, 25));
        CHECK(Has(d, 0, "Base has no method 'Nope'", 7, 54));
        CHECK(Has(d, 0, "C must have IA's method: public void Run()", 9, 7));
        CHECK(Has(d, 0, "B must override A.Go()", 11, 7));
        CHECK(Has(d, 0, "Q needs a constructor that calls base(...): P has no constructor that takes 0 values", 13, 7));
        CHECK(Has(d, 0, "is string but needs int", 14, 33));
        CHECK(Has(d, 0, "A.Go() is abstract: there's nothing to call", 15, 48));
        CHECK(Has(d, 0, "X ends up extending itself", 16, 11));
        CHECK(Has(d, 0, "A Behaviour class can't extend a class or implement interfaces yet", 18, 22));
        CHECK(Has(d, 0, "A is abstract: make one of the classes that extend it", 20, 15));
        CHECK(Has(d, 0, "IA is an interface: make one of the classes that implement it", 21, 16));
        CHECK(Has(d, 0, "This D is never a C", 23, 20));
    }
}

static void TryCatch() {
    {   // throw / catch by class, engine errors as Exception, throw; and finally in every case
        World w(
            "class NotEnoughGold : Exception {\n"
            "    public int needed;\n"
            "    public NotEnoughGold(int needed) : base(\"Need \" + needed + \" gold\") { this.needed = needed; }\n"
            "}\n"
            "class Shop {\n"
            "    public int gold = 5;\n"
            "    public void Buy(int price) { if (price > gold) throw new NotEnoughGold(price - gold); gold -= price; }\n"
            "}\n"
            "class T : Behaviour {\n"
            "    public int needed, finallies, passes, ret, caught;\n"
            "    public string msg, nullMsg, order;\n"
            "    int Tries() { try { return 1; } finally { finallies++; } }\n"
            "    int Forever(int n) { return Forever(n + 1); }\n"
            "    void Start() {\n"
            "        Shop s = new Shop();\n"
            "        try { s.Buy(3); s.Buy(10); msg = \"bought\"; }\n"
            "        catch (NotEnoughGold e) { needed = e.needed; msg = e.Message; }\n"
            "        finally { finallies++; }\n"
            "        try { Shop none = null; none.Buy(1); }\n"
            "        catch (Exception e) { nullMsg = e.Message; }\n"
            "        order = \"\";\n"
            "        try {\n"
            "            try { throw new Exception(\"inner\"); }\n"
            "            catch (Exception e) { order += \"c1\"; throw; }\n"
            "            finally { order += \"f1\"; }\n"
            "        } catch (Exception e) { order += \"c2:\" + e.Message; }\n"
            "        ret = Tries();\n"
            "        for (int i = 0; i < 3; i++) { try { if (i == 1) break; passes++; } finally { finallies++; } }\n"
            "    }\n"
            "}\n");
        CHECK(w.loaded);
        CHECK_NEAR(w.F("needed"), 8.0f, 1e-6f);
        CHECK(w.S("msg") == "Need 8 gold");
        CHECK(w.S("nullMsg") == "'none' is null here");
        CHECK(w.S("order") == "c1f1c2:inner");
        CHECK_NEAR(w.F("ret"), 1.0f, 1e-6f);
        CHECK_NEAR(w.F("passes"), 1.0f, 1e-6f);
        CHECK_NEAR(w.F("finallies"), 4.0f, 1e-6f);     // the first try, Tries(), and the loop's two passes (one breaks)
    }
    {   // an exception nobody catches is reported with its line; endless recursion can't be caught
        g_errs.clear();
        World a(
            "class Oops : Exception { public Oops() : base(\"no ammo\") { } }\n"
            "class T : Behaviour {\n"
            "    void Start() { throw new Oops(); }\n"
            "}\n");
        CHECK(ErrHas("Start() line 3: Oops: no ammo"));
        World b(Wrap(
            "    public int caught;\n"
            "    int Forever(int n) { return Forever(n + 1); }\n"
            "    void Start() { try { Forever(0); } catch { caught = 1; } }\n"));
        CHECK(ErrHas("calls itself too deeply"));
        CHECK_NEAR(b.F("caught"), 0.0f, 1e-6f);
    }
    {   // mistakes, with line and column
        auto d = Diags(
            "class T : Behaviour {\n"   // 1
            "    int F() { try { return 1; } finally { return 2; } }\n"   // 2
            "    void Start() {\n"   // 3
            "        throw;\n"   // 4
            "        throw 5;\n"   // 5
            "        try { } catch (int e) { }\n"   // 6
            "        try { } catch (Exception) { } catch (Exception e) { }\n"   // 7
            "        for (;;) { try { } finally { break; } }\n"   // 8
            "        Exception x = new Exception(\"a\"); x.Message = \"b\";\n"   // 9
            "        try { }\n"   // 10
            "        Debug.Log(F());\n"   // 11
            "    }\n"   // 12
            "}\n"   // 13
            );
        CHECK(Has(d, 0, "return can't leave a finally block", 2, 43));
        CHECK(Has(d, 0, "throw; on its own only works inside a catch (it throws the caught exception again)", 4, 9));
        CHECK(Has(d, 0, "throw needs an Exception, like throw new Exception(\"what went wrong\"), not int", 5, 15));
        CHECK(Has(d, 0, "catch takes Exception or a class that extends it, not int", 6, 24));
        CHECK(Has(d, 0, "An earlier catch already catches Exception, so this one never runs", 7, 39));
        CHECK(Has(d, 0, "break can't leave a finally block", 8, 38));
        CHECK(Has(d, 0, "'Message' can only be read", 9, 45));
        CHECK(Has(d, 0, "A try needs a catch or a finally after it", 10, 9));
    }
}

static void Coroutines() {
    {   // StartCoroutine runs to the first yield; null waits a frame, WaitForSeconds its time,
        // a coroutine its end, WaitUntil its condition; StopCoroutine and yield break end one
        World w(Wrap(
            "    public int ticks, blinks, after, nested, finished, cond, early;\n"
            "    public bool ready;\n"
            "    Coroutine loop;\n"
            "    IEnumerator Count() { while (true) { ticks++; yield return null; } }\n"
            "    IEnumerator Blink(int times) { for (int i = 0; i < times; i++) { blinks++; yield return new WaitForSeconds(0.1f); } after = 1; }\n"
            "    IEnumerator Child() { yield return null; yield return null; nested = 1; }\n"
            "    IEnumerator Parent() { yield return StartCoroutine(Child()); finished = nested * 10; }\n"
            "    IEnumerator WaitReady() { yield return new WaitUntil(() => ready); cond = 1; }\n"
            "    IEnumerator Early() { early = 1; yield break; }\n"
            "    void Start() {\n"
            "        loop = StartCoroutine(Count());\n"
            "        StartCoroutine(Blink(3)); StartCoroutine(Parent()); StartCoroutine(WaitReady()); StartCoroutine(Early());\n"
            "    }\n"
            "    void Update() { if (ticks == 5) { StopCoroutine(loop); ready = true; } }\n"));
        CHECK(w.loaded);
        CHECK_NEAR(w.F("ticks"), 1.0f, 1e-6f);      // ran to its first yield inside Start
        CHECK_NEAR(w.F("early"), 1.0f, 1e-6f);
        w.Frames(1);
        CHECK_NEAR(w.F("ticks"), 1.0f, 1e-6f);      // paused during Start's frame: goes on from the next one
        w.Frames(1);
        CHECK_NEAR(w.F("ticks"), 2.0f, 1e-6f);
        w.Frames(4);
        CHECK_NEAR(w.F("blinks"), 1.0f, 1e-6f);     // 0.1 s is six frames
        CHECK_NEAR(w.F("finished"), 10.0f, 1e-6f);  // waited for Child's end
        w.Frames(1);
        CHECK_NEAR(w.F("blinks"), 2.0f, 1e-6f);
        CHECK_NEAR(w.F("cond"), 1.0f, 1e-6f);
        w.Frames(20);
        CHECK_NEAR(w.F("ticks"), 5.0f, 1e-6f);      // stopped
        CHECK_NEAR(w.F("blinks"), 3.0f, 1e-6f);
        CHECK_NEAR(w.F("after"), 1.0f, 1e-6f);
    }
    {   // coroutines of plain classes; loops, foreach, switch, break and continue around yields
        World w(
            "class Spawner {\n"
            "    public int spawned;\n"
            "    public IEnumerator Waves(int[] sizes) {\n"
            "        foreach (int n in sizes) {\n"
            "            for (int i = 0; i < n; i++) { if (i == 2) break; spawned++; yield return null; }\n"
            "            yield return null;\n"
            "        }\n"
            "    }\n"
            "}\n"
            "class T : Behaviour {\n"
            "    public int spawned, log;\n"
            "    Spawner sp = new Spawner();\n"
            "    IEnumerator Machine() {\n"
            "        int step = 0;\n"
            "        while (true) {\n"
            "            switch (step) {\n"
            "                case 0: log = log * 10 + 1; step = 1; yield return null; break;\n"
            "                case 1: log = log * 10 + 2; step = 2; continue;\n"
            "                default: log = log * 10 + 3; yield break;\n"
            "            }\n"
            "            log = log * 10 + 9;\n"
            "        }\n"
            "    }\n"
            "    void Start() { StartCoroutine(sp.Waves(new int[] { 1, 3 })); StartCoroutine(Machine()); }\n"
            "    void Update() { spawned = sp.spawned; }\n"
            "}\n");
        CHECK(w.loaded);
        w.Frames(20);
        CHECK_NEAR(w.F("spawned"), 3.0f, 1e-6f);
        CHECK_NEAR(w.F("log"), 1923.0f, 1e-6f);
    }
    {   // the same coroutine timing on the old runtime (yield return null exactly)
        const std::string src = Wrap(
            "    public int ticks, blinks, a1, a7, a8;\n"
            "    IEnumerator Count() { while (true) { ticks++; yield return null; } }\n"
            "    IEnumerator Blink() { while (true) { blinks++; yield return new WaitForSeconds(0.1f); } }\n"
            "    void Start() { StartCoroutine(Count()); StartCoroutine(Blink()); }\n");
        World next(src), old(src, "okayscript");
        for (int f = 0; f < 15; ++f) {
            CHECK_NEAR(next.F("ticks"), old.F("ticks"), 1e-6f);
            // WaitForSeconds: the old runtime can land a frame later (float rounding on 0.1 - 6 * 1/60)
            CHECK_NEAR(next.F("blinks"), old.F("blinks"), 1.0f + 1e-6f);
            next.Frames(1); old.Frames(1);
        }
    }
    {   // an error in a coroutine is reported with its line and ends that coroutine
        g_errs.clear();
        World w(Wrap(
            "    public int after;\n"
            "    IEnumerator Bad() { yield return null; int[] a = new int[1]; a[3] = 1; after = 1; }\n"
            "    void Start() { StartCoroutine(Bad()); }\n"));
        w.Frames(3);
        CHECK(ErrHas("coroutine line 3: Index 3 is outside the array (length 1)"));
        CHECK_NEAR(w.F("after"), 0.0f, 1e-6f);
    }
    {   // mistakes, with line and column
        auto d = Diags(
            "class T : Behaviour {\n"   // 1
            "    IEnumerator NoYield() { Debug.Log(1); }\n"   // 2
            "    IEnumerator Ret() { yield return null; return; }\n"   // 3
            "    void NotCo() { yield return null; }\n"   // 4
            "    IEnumerator InTry() { try { yield return null; } finally { } }\n"   // 5
            "    void Start() {\n"   // 6
            "        Ret();\n"   // 7
            "        StartCoroutine(\"Ret\");\n"   // 8
            "        var w = new WaitForSeconds();\n"   // 9
            "        Action a = () => { yield return null; };\n"   // 10
            "        StartCoroutine(InTry()); StartCoroutine(NoYield()); a(); NotCo(); Debug.Log(\"\" + w);\n"   // 11
            "    }\n"   // 12
            "}\n"   // 13
            );
        CHECK(Has(d, 0, "NoYield() returns IEnumerator, so it's a coroutine: it needs a yield return (or yield break)", 2, 17));
        CHECK(Has(d, 0, "A coroutine ends with yield break, not return", 3, 44));
        CHECK(Has(d, 0, "yield only works in a coroutine: a method that returns IEnumerator", 4, 20));
        CHECK(Has(d, 0, "yield can't be inside a try, catch or finally", 5, 33));
        CHECK(Has(d, 0, "Ret() makes a coroutine but doesn't run it: StartCoroutine(Ret())", 7, 9));
        CHECK(Has(d, 0, "StartCoroutine takes the coroutine itself, not its name: StartCoroutine(Blink())", 8, 24));
        CHECK(Has(d, 0, "new WaitForSeconds takes 1 value, not 0", 9, 17));
        CHECK(Has(d, 0, "yield doesn't work inside a lambda", 10, 28));
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

static void Followups() {
    {   // each switch section has its own scope: a local from one section can't be read in another
        auto d = Diags(Wrap(
            "    void Start() {\n"                                                                   // 2
            "        int n = 2; string r = \"\";\n"                                               // 3
            "        switch (n) { case 1: string s = \"a\"; break; case 2: r = s + \"!\"; break; }\n"   // 4
            "        Debug.Log(r);\n"
            "    }\n"));
        CHECK(Has(d, 0, "Unknown name 's'", 4, 65));
    }
    {   // a float too big for an int is a script error, not undefined behaviour
        g_errs.clear();
        World w(Wrap("    public float big = 1000000;\n    public int n;\n    void Start() { n = (int)(big * big * big); }\n"));
        CHECK(ErrHas("Start() line 4: 1e+18 doesn't fit in an int"));
        World e("enum State { Idle }\nclass T : Behaviour {\n    public float big = -1000000;\n    void Start() { State s = (State)(big * big * big); Debug.Log(s); }\n}\n");
        CHECK(ErrHas("Start() line 4: -1e+18 doesn't fit in an int"));
        // the Inspector can't put one into an int field either
        World f(Wrap("    public int lives = 3;\n    void Start() { }\n"));
        f.sc->fields["lives"] = "1e30";
        f.sc->ApplyFieldOverrides();
        CHECK_NEAR(f.F("lives"), 3.0f, 1e-6f);
    }
    {   // a long else-if chain past the nesting limit gives one error, not a cascade
        std::string chain = "if (n == 0) n = 1;";
        for (int i = 1; i < 400; ++i) chain += " else if (n == " + std::to_string(i) + ") n = " + std::to_string(i + 1) + ";";
        auto d = Diags(Wrap("    void Start() { int n = 0; " + chain + " Debug.Log(n); }"));
        CHECK_EQ(Count(d, 0), 1);
        CHECK(Has(d, 0, "nested too deeply"));
    }
    {   // an enum value can't use a field; the message says so instead of "did you mean" itself
        auto d = Diags("class T : Behaviour {\n    const int Base = 3;\n    enum E { A = Base }\n    void Start() { Debug.Log(E.A + \" \" + Base); }\n}\n");
        CHECK(Has(d, 0, "An enum value can't use the field 'Base'", 3, 18));
        CHECK(!Has(d, 0, "Did you mean 'Base'"));
    }
}

static void BackCompat() {
    // switch, foreach, case, enum and in are keywords only where they start that
    // construct, so scripts that used them as names keep working.
    World w(Wrap(
        "    public int enum = 1, foreach = 2;\n"
        "    public int total;\n"
        "    void Start() { int switch = 3, case = 4, in = 5; switch = enum + foreach + case + in; total = switch; }\n"));
    CHECK(w.loaded);
    CHECK_NEAR(w.F("total"), 12.0f, 1e-6f);
    // Features still to come keep their message.
    auto d = Diags(Wrap("    readonly int r = 1;\n    void Start() { goto end; }\n"));
    CHECK(Has(d, 0, "readonly isn't supported yet"));
    CHECK(Has(d, 0, "goto isn't supported"));
}

static void Fuzz() {
    // Truncated and garbled programs never crash the compiler.
    const std::string src =
        "enum E { A, B = 2 }\nclass P { public int v; public P(int x) { v = x; } public P Twin() { return new P(v); } }\nclass T : Behaviour {\n int[] a = { 1, 2 };\n void Start() {\n"
        "  switch (E.A) { case E.A: foreach (var x in a) { a[x % 2] += 1; } break; default: break; }\n"
        "  string s = $\"{a[0]:F1} {{ {$\"{E.B}\"}\";\n"
        "  GameObject g = GameObject.Find(s) ?? gameObject; g?.transform.Translate(1, 0, 0); a ??= new int[1];\n"
        "  P p = new P(3).Twin(); p.v += 1; Debug.Log(p?.Twin());\n"
        "  var xs = new List<int> { 1, 2 }; var d = new Dictionary<string, List<int>>(); d[\"a\"] = xs;\n"
        "  do { xs.RemoveAt(0); } while (xs.Count > 0); foreach (var kv in d) Debug.Log(kv.Key + kv.Value.Count);\n }\n}\n";
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
    NullOperators();
    Classes();
    Collections();
    Lambdas();
    Events();
    Inheritance();
    TryCatch();
    Coroutines();
    Parity();
    Followups();
    BackCompat();
    Fuzz();
    Log::sink = nullptr;
    TEST_MAIN_RESULT();
}
