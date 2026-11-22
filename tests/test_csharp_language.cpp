#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scripting/NextScriptVM.hpp"
#include "okay/Scripting/CSharpAssist.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;

// C# language features, round 2 (our own C#, written from the ECMA-334 C# language
// specification: struct types (16), static and const members (15.5, 15.4),
// optional parameters (15.6.2), generic classes and methods (15.3.2, 15.6.1)), plus
// the Unity-style Quaternion type a game script expects. Each group: programs that
// should work give the right values; programs that shouldn't give a clear error.

static const float kDt = 1.0f / 60.0f;

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
static bool HasError(const std::string& src, const std::string& text) {
    auto d = Diags(src);
    for (auto& x : d) if (x.severity == 0 && x.message.find(text) != std::string::npos) return true;
    Dump(d);
    return false;
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
    std::string S(const char* n) { return sc->VM() ? sc->VM()->GetGlobal(n).AsString() : std::string("?"); }
};

// ---- struct: a value type, copied when assigned, passed or stored ----
static void Structs() {
    const std::string src =
        "using UnityEngine;\n"
        "using System.Collections.Generic;\n"
        "public struct Point {\n"
        "    public float x, y;\n"
        "    public Point(float x, float y) { this.x = x; this.y = y; }\n"
        "    public float Len() => Mathf.Sqrt(x * x + y * y);\n"
        "    public void Grow() { x *= 2; y *= 2; }\n"
        "}\n"
        "public class S : MonoBehaviour {\n"
        "    public float a, b, c, d, e, f, g, h, k;\n"
        "    Point field;\n"
        "    Point[] pts = new Point[2];\n"
        "    List<Point> list = new List<Point>();\n"
        "    void Bump(Point z) { z.x = 100; }\n"
        "    Point Make() { return new Point(1, 1); }\n"
        "    void Start() {\n"
        "        Point q = new Point(3, 4);\n"
        "        Point r = q; r.x = 10;\n"
        "        a = q.x; b = r.x; c = q.Len();\n"
        "        field.x = 7; d = field.x;\n"            // a struct field starts as all zeroes, never null
        "        Bump(q); e = q.x;\n"                    // the method got a copy
        "        pts[0].x = 2; f = pts[0].x + pts[1].x;\n"  // array items are structs in place
        "        list.Add(q); q.x = 99; g = list[0].x;\n" // the list holds its own copy
        "        Point m = Make(); m.Grow(); h = m.x;\n"  // a method changes the variable it's called on
        "        Point z = new Point(); k = z.x + z.y;\n" // new S() with no arguments: all zeroes
        "    }\n"
        "}\n";
    CHECK(Clean(src));
    World w(src);
    CHECK(w.loaded);
    CHECK_NEAR(w.F("a"), 3.0f, 1e-5f);
    CHECK_NEAR(w.F("b"), 10.0f, 1e-5f);
    CHECK_NEAR(w.F("c"), 5.0f, 1e-5f);
    CHECK_NEAR(w.F("d"), 7.0f, 1e-5f);
    CHECK_NEAR(w.F("e"), 3.0f, 1e-5f);
    CHECK_NEAR(w.F("f"), 2.0f, 1e-5f);
    CHECK_NEAR(w.F("g"), 3.0f, 1e-5f);
    CHECK_NEAR(w.F("h"), 2.0f, 1e-5f);
    CHECK_NEAR(w.F("k"), 0.0f, 1e-5f);

    // a struct holding a struct: copies go all the way down
    const std::string nested =
        "public struct Inner { public int v; }\n"
        "public struct Outer { public Inner i; public int n; }\n"
        "public class N : MonoBehaviour {\n"
        "    public int a, b;\n"
        "    void Start() { Outer o = new Outer(); o.i.v = 5; Outer p = o; p.i.v = 9; a = o.i.v; b = p.i.v; }\n"
        "}\n";
    CHECK(Clean(nested));
    World wn(nested);
    CHECK_NEAR(wn.F("a"), 5.0f, 1e-5f);
    CHECK_NEAR(wn.F("b"), 9.0f, 1e-5f);

    // mistakes
    CHECK(HasError("public struct P { public int x; }\npublic class A : MonoBehaviour { void Start() { P p = null; } }\n", "can't be null"));
    CHECK(HasError("public class B { }\npublic struct P : B { public int x; }\npublic class A : MonoBehaviour { void Start() { } }\n", "struct"));
    CHECK(HasError("public struct P { public int x; }\npublic class A : MonoBehaviour { public bool s; void Start() { P a = new P(); P b = new P(); s = a == b; } }\n", "=="));
    CHECK(HasError("public struct P { public P inner; }\npublic class A : MonoBehaviour { void Start() { } }\n", "itself"));
    CHECK(HasError("public struct P : MonoBehaviour { }\n", "struct"));
}

// ---- Quaternion and transform.rotation ----
static void Quaternions() {
    const std::string src =
        "using UnityEngine;\n"
        "public class Q : MonoBehaviour {\n"
        "    public float ey, fx, fz, ang, half, back, idw, inv, aa, lr, comp, eqs, rx, step;\n"
        "    void Start() {\n"
        "        Quaternion q = Quaternion.Euler(0, 90, 0);\n"
        "        transform.rotation = q;\n"
        "        ey = transform.rotation.eulerAngles.y;\n"
        "        Vector3 f = q * Vector3.forward; fx = f.x; fz = f.z;\n"   // forward turned 90 degrees about Y is +X
        "        Quaternion look = Quaternion.LookRotation(new Vector3(1, 0, 0));\n"
        "        ang = Quaternion.Angle(q, look);\n"
        "        half = Quaternion.Slerp(Quaternion.identity, q, 0.5f).eulerAngles.y;\n"
        "        back = ((q * q) * Vector3.forward).z;\n"
        "        idw = Quaternion.identity.w;\n"
        "        inv = (Quaternion.Inverse(q) * q).w;\n"
        "        aa = Quaternion.Angle(Quaternion.AngleAxis(30, Vector3.up), Quaternion.identity);\n"
        "        transform.localRotation = Quaternion.Euler(10, 0, 0);\n"
        "        lr = transform.localRotation.eulerAngles.x;\n"
        "        Quaternion c = Quaternion.identity; c.y = 1; c.w = 0; comp = c.y;\n"
        "        eqs = q == look ? 1 : 0;\n"
        "        Quaternion r = new Quaternion(0, 0, 0, 1); r *= Quaternion.Euler(20, 0, 0); rx = r.eulerAngles.x;\n"
        "        step = Quaternion.Angle(Quaternion.RotateTowards(Quaternion.identity, q, 15), Quaternion.identity);\n"
        "        Debug.Log(Quaternion.identity);\n"
        "    }\n"
        "}\n";
    CHECK(Clean(src));
    World w(src);
    CHECK(w.loaded);
    CHECK_NEAR(w.F("ey"), 90.0f, 1e-3f);
    CHECK_NEAR(w.F("fx"), 1.0f, 1e-4f);
    CHECK_NEAR(w.F("fz"), 0.0f, 1e-4f);
    CHECK_NEAR(w.F("ang"), 0.0f, 0.05f);
    CHECK_NEAR(w.F("half"), 45.0f, 1e-2f);
    CHECK_NEAR(w.F("back"), -1.0f, 1e-4f);
    CHECK_NEAR(w.F("idw"), 1.0f, 1e-6f);
    CHECK_NEAR(std::fabs(w.F("inv")), 1.0f, 1e-4f);
    CHECK_NEAR(w.F("aa"), 30.0f, 1e-2f);
    CHECK_NEAR(w.F("lr"), 10.0f, 1e-3f);
    CHECK_NEAR(w.F("comp"), 1.0f, 1e-6f);
    CHECK_NEAR(w.F("eqs"), 1.0f, 1e-6f);
    CHECK_NEAR(w.F("rx"), 20.0f, 1e-3f);
    CHECK_NEAR(w.F("step"), 15.0f, 1e-2f);
    // the object really turned: its forward is +X now (then the local turn of 10 degrees about X)
    CHECK_NEAR(w.player->transform->localRotation.ToEuler().x, 10.0f, 1e-3f);

    // A public Quaternion field and rotation over frames
    const std::string spin =
        "public class Spin : MonoBehaviour {\n"
        "    void Update() { transform.rotation = transform.rotation * Quaternion.Euler(0, 90 * Time.deltaTime, 0); }\n"
        "}\n";
    World ws(spin);
    CHECK(ws.loaded);
    ws.Frames(30);
    CHECK_NEAR(ws.player->transform->Rotation().ToEuler().y, 45.0f, 0.05f);

    // mistakes
    CHECK(HasError("public class A : MonoBehaviour { void Start() { Quaternion q = Quaternion.identity; Vector3 v = Vector3.up * q; } }\n", "*"));
    CHECK(HasError("public class A : MonoBehaviour { void Start() { Quaternion q = null; } }\n", "null"));
    CHECK(HasError("public class A : MonoBehaviour { void Start() { Quaternion q = Quaternion.identity; float f = q.v; } }\n", "Quaternion has no"));
}

// ---- static and const in plain classes ----
static void PlainStatics() {
    const std::string src =
        "public class Counter {\n"
        "    public static int made;\n"
        "    public const int Max = 3;\n"
        "    static int secret = 40;\n"
        "    public int id;\n"
        "    public Counter() { made++; id = made; }\n"
        "    public static int Twice(int x) => x * 2;\n"
        "    public static int Secret() { return secret + Twice(1); }\n"
        "    public bool Full() => made >= Max;\n"
        "}\n"
        "public class T : MonoBehaviour {\n"
        "    public int a, b, c, d, e, f;\n"
        "    void Start() {\n"
        "        new Counter(); Counter k = new Counter();\n"
        "        a = Counter.made; b = k.id; c = Counter.Max; d = Counter.Twice(4); e = Counter.Secret();\n"
        "        new Counter(); f = k.Full() ? 1 : 0;\n"
        "        Counter.made = 10;\n"
        "    }\n"
        "}\n";
    CHECK(Clean(src));
    World w(src);
    CHECK(w.loaded);
    CHECK_NEAR(w.F("a"), 2.0f, 1e-6f);
    CHECK_NEAR(w.F("b"), 2.0f, 1e-6f);
    CHECK_NEAR(w.F("c"), 3.0f, 1e-6f);
    CHECK_NEAR(w.F("d"), 8.0f, 1e-6f);
    CHECK_NEAR(w.F("e"), 42.0f, 1e-6f);
    CHECK_NEAR(w.F("f"), 1.0f, 1e-6f);
    // mistakes: a static method can't use the object's fields; private statics stay private
    CHECK(HasError("public class C { int n; public static int Get() { return n; } }\npublic class T : MonoBehaviour { void Start() { C.Get(); } }\n", "static"));
    CHECK(HasError("public class C { static int n; }\npublic class T : MonoBehaviour { void Start() { int x = C.n; } }\n", "private"));
    CHECK(HasError("public class C { public const int K = 1; }\npublic class T : MonoBehaviour { void Start() { C.K = 2; } }\n", "const"));
    CHECK(HasError("public class C { public int n; }\npublic class T : MonoBehaviour { void Start() { int x = C.n; } }\n", "static"));
}

// ---- default parameter values on constructors and across scripts ----
static void MoreDefaults() {
    const std::string src =
        "public class Enemy {\n"
        "    public int hp; public string name;\n"
        "    public Enemy(int hp = 10, string name = \"slime\") { this.hp = hp; this.name = name; }\n"
        "}\n"
        "public class Boss : Enemy { public Boss() : base(50) { } }\n"
        "public class T : MonoBehaviour {\n"
        "    public int a, b, c; public string n1, n2;\n"
        "    void Start() {\n"
        "        Enemy e1 = new Enemy(); Enemy e2 = new Enemy(3); Enemy e3 = new Enemy(4, \"bat\");\n"
        "        a = e1.hp; b = e2.hp; n1 = e1.name; n2 = e3.name; c = new Boss().hp;\n"
        "    }\n"
        "}\n";
    CHECK(Clean(src));
    World w(src);
    CHECK(w.loaded);
    CHECK_NEAR(w.F("a"), 10.0f, 1e-6f);
    CHECK_NEAR(w.F("b"), 3.0f, 1e-6f);
    CHECK_NEAR(w.F("c"), 50.0f, 1e-6f);
    CHECK(w.S("n1") == "slime");
    CHECK(w.S("n2") == "bat");

    // Another script's public method with default values, called with fewer values.
    Scene scene("Two");
    GameObject* go = scene.CreateGameObject("Hero");
    auto* health = go->AddComponent<ScriptComponent>("csharp");
    std::string err;
    CHECK(health->LoadSource(
        "public class Health : MonoBehaviour {\n"
        "    public int hp = 100;\n"
        "    public void Damage(int amount = 5, bool loud = false) { hp -= amount; if (loud) hp -= 1000; }\n"
        "}\n", &err));
    auto* hitter = go->AddComponent<ScriptComponent>("csharp");
    CHECK(hitter->LoadSource(
        "public class Hitter : MonoBehaviour {\n"
        "    void Start() { Health h = GetComponent<Health>(); h.Damage(); h.Damage(20); }\n"
        "}\n", &err));
    if (!err.empty()) std::printf("    %s\n", err.c_str());
    scene.Start();
    scene.Update(kDt);
    CHECK_NEAR(health->VM()->GetGlobal("hp").AsFloat(), 75.0f, 1e-6f);
}

// ---- generic classes and methods of the script's own ----
static void Generics() {
    const std::string src =
        "using System.Collections.Generic;\n"
        "public class Box<T> {\n"
        "    public T value;\n"
        "    public Box(T v) { value = v; }\n"
        "    public T Get() => value;\n"
        "}\n"
        "public class Stack<T> {\n"
        "    List<T> items = new List<T>();\n"
        "    public int Count => items.Count;\n"
        "    public void Push(T x) { items.Add(x); }\n"
        "    public T Pop() { T x = items[items.Count - 1]; items.RemoveAt(items.Count - 1); return x; }\n"
        "}\n"
        "public class Pair<K, V> { public K key; public V val; public Pair(K k, V v) { key = k; val = v; } }\n"
        "public class G : MonoBehaviour {\n"
        "    public int a, c, d, n, z; public float zf; public string b, e;\n"
        "    T First<T>(List<T> xs) { return xs[0]; }\n"
        "    T Or<T>(T x, T y, bool pick) => pick ? x : y;\n"
        "    T Zero<T>() => default(T);\n"
        "    void Start() {\n"
        "        Box<int> bi = new Box<int>(5); a = bi.Get();\n"
        "        Box<string> bs = new Box<string>(\"hi\"); b = bs.value;\n"
        "        Stack<int> st = new Stack<int>(); st.Push(1); st.Push(2); st.Push(3); c = st.Pop() + st.Pop(); n = st.Count;\n"
        "        Pair<string, int> p = new Pair<string, int>(\"k\", 7); d = p.val; e = p.key;\n"
        "        List<int> xs = new List<int> { 9, 8 }; a += First<int>(xs);\n"
        "        Box<Box<int>> nested = new Box<Box<int>>(bi); c += nested.Get().Get();\n"
        "        b += Or<string>(\"!\", \"?\", true);\n"
        "        z = Zero<int>(); zf = Zero<float>() + 0.5f;\n"
        "    }\n"
        "}\n";
    CHECK(Clean(src));
    World w(src);
    CHECK(w.loaded);
    CHECK_NEAR(w.F("a"), 14.0f, 1e-6f);
    CHECK(w.S("b") == "hi!");
    CHECK_NEAR(w.F("c"), 10.0f, 1e-6f);
    CHECK_NEAR(w.F("n"), 1.0f, 1e-6f);
    CHECK_NEAR(w.F("d"), 7.0f, 1e-6f);
    CHECK(w.S("e") == "k");
    CHECK_NEAR(w.F("z"), 0.0f, 1e-6f);
    CHECK_NEAR(w.F("zf"), 0.5f, 1e-6f);

    // T Find<T>() over GetComponent<T>(), a constraint (read, not enforced)
    const std::string finder =
        "public class Finder : MonoBehaviour {\n"
        "    public float hasTr, noRb;\n"
        "    T Find<T>() where T : Component { return GetComponent<T>(); }\n"
        "    void Start() { hasTr = Find<Transform>() != null ? 1 : 0; noRb = Find<Rigidbody>() == null ? 1 : 0; }\n"
        "}\n";
    CHECK(Clean(finder));
    World wf(finder);
    CHECK_NEAR(wf.F("hasTr"), 1.0f, 1e-6f);
    CHECK_NEAR(wf.F("noRb"), 1.0f, 1e-6f);

    // mistakes
    CHECK(HasError("public class Box<T> { public T v; }\npublic class A : MonoBehaviour { Box b; void Start() { } }\n", "Box takes 1 type"));
    CHECK(HasError("public class A : MonoBehaviour { T Id<T>(T x) => x; void Start() { int y = Id(3); } }\n", "generic"));
    CHECK(HasError("public class Bad<T> { public int F(T x) { return x * 2; } }\n"
                   "public class A : MonoBehaviour { void Start() { Bad<string> b = new Bad<string>(); b.F(\"s\"); } }\n", "string"));
    // a generic type that grows without end stops with an error, not a hang
    CHECK(HasError("public class Grow<T> { public Grow<Grow<T>> next; }\n"
                   "public class A : MonoBehaviour { Grow<int> g; void Start() { } }\n", "Too many generic"));
}

// ---- Script Editor help for C#: member lists and signature help from the binding table ----
static bool HasMember(const std::vector<csharp::Member>& ms, const std::string& n, bool method) {
    for (auto& m : ms) if (m.name == n && m.method == method) return true;
    return false;
}
static void EditorHelp() {
    const std::string src =
        "using UnityEngine;\n"
        "using System.Collections.Generic;\n"
        "public class Enemy { public int hp; public Enemy(int hp = 10) { this.hp = hp; } public void Hit(int amount, string why) { } public static int Made; }\n"
        "public class Mover : MonoBehaviour {\n"
        "    public float speed = 5f;\n"
        "    Rigidbody rb;\n"
        "    List<Enemy> enemies = new List<Enemy>();\n"
        "    int Twice(int x) => x * 2;\n"
        "    void Update() {\n"
        "        var e = new Enemy(3);\n"
        "        string label = \"hi\";\n"
        "        Quaternion q = transform.rotation;\n"
        "    }\n"
        "}\n";
    CHECK(csharp::ReceiverType(src, "transform") == "Transform");
    CHECK(csharp::ReceiverType(src, "transform.position") == "Vector3");
    CHECK(csharp::ReceiverType(src, "rb") == "Rigidbody");
    CHECK(csharp::ReceiverType(src, "e") == "Enemy");
    CHECK(csharp::ReceiverType(src, "enemies[0]") == "Enemy");
    CHECK(csharp::ReceiverType(src, "GetComponent<Rigidbody>()") == "Rigidbody");
    CHECK(csharp::ReceiverType(src, "Mathf") == "static:Mathf");
    CHECK(csharp::ReceiverType(src, "q") == "Quaternion");
    CHECK(csharp::ReceiverType(src, "nothingHere").empty());

    auto tr = csharp::MembersAfter(src, "transform");
    CHECK(HasMember(tr, "position", false));
    CHECK(HasMember(tr, "rotation", false));
    CHECK(HasMember(tr, "Translate", true));
    CHECK(HasMember(csharp::MembersAfter(src, "transform.position"), "normalized", false));
    CHECK(HasMember(csharp::MembersAfter(src, "transform.position"), "x", false));
    CHECK(HasMember(csharp::MembersAfter(src, "rb"), "AddForce", true));
    CHECK(HasMember(csharp::MembersAfter(src, "Quaternion"), "Euler", true));
    CHECK(HasMember(csharp::MembersAfter(src, "q"), "eulerAngles", false));
    CHECK(HasMember(csharp::MembersAfter(src, "e"), "Hit", true));
    CHECK(HasMember(csharp::MembersAfter(src, "Enemy"), "Made", false));
    CHECK(!HasMember(csharp::MembersAfter(src, "e"), "Made", false));   // a static isn't on the object
    CHECK(HasMember(csharp::MembersAfter(src, "label"), "Length", false));
    CHECK(HasMember(csharp::MembersAfter(src, "enemies"), "Add", true));
    CHECK(HasMember(csharp::MembersAfter(src, "this"), "Twice", true));

    auto tl = csharp::SignaturesFor(src, "transform", "Translate");
    CHECK(tl.size() == 2);   // Translate(Vector3) and Translate(float, float, float)
    bool three = false;
    for (auto& s : tl) if (s.params.size() == 3 && s.text.find("float x") != std::string::npos) three = true;
    CHECK(three);
    auto hit = csharp::SignaturesFor(src, "e", "Hit");
    CHECK(hit.size() == 1 && hit[0].text == "void Hit(int amount, string why)");
    auto own = csharp::SignaturesFor(src, "", "Twice");
    CHECK(own.size() == 1 && own[0].params.size() == 1);
    auto ctor = csharp::SignaturesFor(src, "", "Enemy");
    CHECK(ctor.size() == 1 && ctor[0].text.find("new Enemy(int hp = ...)") == 0);
    auto slerp = csharp::SignaturesFor(src, "Quaternion", "Slerp");
    CHECK(slerp.size() == 1 && !slerp[0].doc.empty());

    // the language box's names
    CHECK(ScriptLanguageDisplayName("csharp") == "C#");
    CHECK(ScriptLanguageDisplayName("cs") == "C#");
    CHECK(ScriptLanguageDisplayName("okayscript") == "OkayScript (old)");
    CHECK(ScriptLanguageDisplayName("lua") == "Lua");
}

int main() {
    RUN_SUITE("csharp_language");
    Structs();
    Quaternions();
    PlainStatics();
    MoreDefaults();
    Generics();
    EditorHelp();
    TEST_MAIN_RESULT();
}
