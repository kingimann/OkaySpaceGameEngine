#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Physics2D.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;

// OkayScript accepts the C# / Unity style people already know: classes with
// fields + methods, using/namespace, casts, enums, generics (List, Dictionary,
// arrays), methods on values (s.ToUpper(), xs.Add()), lambdas + LINQ-style
// helpers, Vector3 math, GetComponent<T>() references, collision handlers with
// an `other` parameter, Destroy(obj, delay) and coroutines (IEnumerator +
// yield return new WaitForSeconds).
static std::vector<std::string> g_errs;
static bool Try(const char* name, const std::string& body, const char* check = nullptr) {
    Scene s; auto* go = s.CreateGameObject("Hero"); s.CreateGameObject("Enemy");
    auto* sc = go->AddComponent<ScriptComponent>(); std::string e; g_errs.clear();
    if (!sc->LoadSource(body, &e)) { std::printf("  %s: parse: %s\n", name, e.c_str()); return false; }
    s.Start(); for (int i = 0; i < 3; ++i) s.Update(1.0f / 60.0f);
    for (auto& m : g_errs) { std::printf("  %s: %s\n", name, m.c_str()); return false; }
    if (check && !sc->VM()->GetGlobal(check).AsBool()) { std::printf("  %s: '%s' is false\n", name, check); return false; }
    return true;
}
#define CLS(body) std::string("using UnityEngine;\nusing System.Collections;\n\npublic class Player : MonoBehaviour\n{\n") + body + "\n}\n"

int main() {
    RUN_SUITE("okayscript_csharp");
    Log::sink = [](Log::Level l, const std::string& m) { if (l >= Log::Level::Error) g_errs.push_back(m); };

    CHECK(Try("using + MonoBehaviour", CLS("void Start() { ok = true; }\nbool ok;"), "ok"));
    CHECK(Try("public field", CLS("public float speed = 5f;\nbool ok;\nvoid Start() { ok = speed == 5; }"), "ok"));
    CHECK(Try("[SerializeField] private", CLS("[SerializeField] private float speed = 5f;\nprivate bool ok = false;\nvoid Start() { ok = speed > 4; }"), "ok"));
    CHECK(Try("[Header] attribute", CLS("[Header(\"Move\")]\npublic float speed = 2.0f;\nbool ok;\nvoid Start(){ ok = speed == 2; }"), "ok"));
    CHECK(Try("Update() no dt + Time.deltaTime", CLS("float t;\nbool ok;\nvoid Update() { t += Time.deltaTime; ok = t > 0; }"), "ok"));
    CHECK(Try("Debug.Log", CLS("bool ok;\nvoid Start() { Debug.Log(\"hi\"); Debug.LogWarning(\"w\"); ok = true; }"), "ok"));
    CHECK(Try("Input.GetKey(KeyCode.Space)", CLS("bool ok;\nvoid Update() { if (Input.GetKey(KeyCode.Space)) {} if (Input.GetKeyDown(KeyCode.W)) {} ok = true; }"), "ok"));
    CHECK(Try("Input.GetAxis", CLS("bool ok;\nvoid Update() { float h = Input.GetAxis(\"Horizontal\"); float v = Input.GetAxisRaw(\"Vertical\"); ok = h == 0 && v == 0; }"), "ok"));
    CHECK(Try("Input.GetMouseButtonDown", CLS("bool ok;\nvoid Update() { if (Input.GetMouseButtonDown(0)) {} ok = true; }"), "ok"));
    CHECK(Try("new Vector3 + Translate", CLS("bool ok;\nvoid Start() { transform.Translate(new Vector3(1, 2, 3)); ok = transform.position.x == 1 && transform.position.z == 3; }"), "ok"));
    CHECK(Try("Translate(x,y,z)", CLS("bool ok;\nvoid Start() { transform.Translate(1, 0, 0); ok = transform.position.x == 1; }"), "ok"));
    CHECK(Try("position += Vector3.up", CLS("bool ok;\nvoid Start() { transform.position += Vector3.up * 2; ok = transform.position.y == 2; }"), "ok"));
    CHECK(Try("transform.Rotate", CLS("bool ok;\nvoid Start() { transform.Rotate(0, 90, 0); ok = true; }"), "ok"));
    CHECK(Try("localScale = Vector3.one*2", CLS("bool ok;\nvoid Start() { transform.localScale = Vector3.one * 2; ok = transform.localScale.x == 2; }"), "ok"));
    CHECK(Try("Mathf.*", CLS("bool ok;\nvoid Start() { float a = Mathf.Clamp(5, 0, 3); float b = Mathf.Abs(-2); float c = Mathf.Lerp(0, 10, 0.5f); ok = a == 3 && b == 2 && c == 5 && Mathf.Max(1, 2) == 2 && Mathf.PI > 3; }"), "ok"));
    CHECK(Try("Random.Range", CLS("bool ok;\nvoid Start() { float r = Random.Range(1f, 2f); int i = Random.Range(0, 10); ok = r >= 1 && r <= 2 && i >= 0; }"), "ok"));
    CHECK(Try("int/string/bool typed", CLS("int count = 3;\nstring nm = \"a\";\nbool ok;\nvoid Start() { count++; nm += \"b\"; ok = count == 4 && nm == \"ab\"; }"), "ok"));
    CHECK(Try("const + static", CLS("const float MAX = 3f;\nstatic int hits = 0;\nbool ok;\nvoid Start(){ hits += 1; ok = MAX == 3 && hits == 1; }"), "ok"));
    CHECK(Try("private method w/ return", CLS("bool ok;\nprivate int Twice(int x) { return x * 2; }\nvoid Start() { ok = Twice(4) == 8; }"), "ok"));
    CHECK(Try("public method + this.", CLS("bool ok;\nfloat hp = 10;\npublic void Hurt(float d) { this.hp -= d; }\nvoid Start() { Hurt(3); ok = this.hp == 7; }"), "ok"));
    CHECK(Try("List<int> Add/Count", CLS("bool ok;\nList<int> xs = new List<int>();\nvoid Start() { xs.Add(1); xs.Add(2); ok = xs.Count == 2 && xs[1] == 2; }"), "ok"));
    CHECK(Try("array int[]", CLS("bool ok;\nint[] arr = new int[] { 1, 2, 3 };\nvoid Start() { ok = arr.Length == 3 && arr[2] == 3; }"), "ok"));
    CHECK(Try("foreach (var x in xs)", CLS("bool ok;\nvoid Start() { int s = 0; int[] a = {1,2,3}; foreach (var x in a) { s += x; } ok = s == 6; }"), "ok"));
    CHECK(Try("for (int i...)", CLS("bool ok;\nvoid Start() { int s = 0; for (int i = 0; i < 4; i++) { s += i; } ok = s == 6; }"), "ok"));
    CHECK(Try("string interpolation", CLS("bool ok;\nvoid Start() { int a = 3; string t = $\"a={a}\"; ok = t == \"a=3\"; }"), "ok"));
    CHECK(Try("ternary + else if", CLS("bool ok;\nvoid Start() { int a = 5; string t = a > 3 ? \"big\" : \"small\"; if (a < 0) {} else if (a == 5) { ok = t == \"big\"; } }"), "ok"));
    CHECK(Try("switch/case", CLS("bool ok;\nvoid Start() { int a = 2; switch (a) { case 1: break; case 2: ok = true; break; default: break; } }"), "ok"));
    CHECK(Try("gameObject.name / SetActive", CLS("bool ok;\nvoid Start() { ok = gameObject.name == \"Hero\"; gameObject.SetActive(true); }"), "ok"));
    CHECK(Try("GameObject.Find", CLS("bool ok;\nvoid Start() { var e = GameObject.Find(\"Enemy\"); ok = e != null; }"), "ok"));
    CHECK(Try("Destroy(gameObject)", CLS("bool ok = true;\nvoid Start() { if (false) Destroy(gameObject); }"), "ok"));
    CHECK(Try("Vector3.Distance", CLS("bool ok;\nvoid Start() { float d = Vector3.Distance(Vector3.zero, new Vector3(3, 4, 0)); ok = d == 5; }"), "ok"));
    CHECK(Try("vector .x member write", CLS("bool ok;\nvoid Start() { Vector3 p = transform.position; p.x = 4; transform.position = p; ok = transform.position.x == 4; }"), "ok"));
    CHECK(Try("OnCollisionEnter sig", CLS("bool ok = true;\nvoid OnCollisionEnter(Collision other) { }\nvoid OnTriggerEnter2D(Collider2D c) { }"), "ok"));
    CHECK(Try("Invoke(\"Fn\", t)", CLS("bool ok = true;\nvoid Fire() { }\nvoid Start() { Invoke(\"Fire\", 0.5f); }"), "ok"));
    CHECK(Try("Time.time", CLS("bool ok;\nvoid Update() { ok = Time.time >= 0; }"), "ok"));
    CHECK(Try("float literal 1.5f / 2.0", CLS("bool ok;\nvoid Start() { float a = 1.5f; double b = 2.0; ok = a + b == 3.5; }"), "ok"));
    CHECK(Try("logical ops / !", CLS("bool ok;\nvoid Start() { bool a = true; bool b = !a; ok = a && !b || false; }"), "ok"));
    CHECK(Try("// and /* */ comments", CLS("bool ok;\n/* block\ncomment */\nvoid Start() { ok = true; // done\n}"), "ok"));
    CHECK(Try("Awake + OnEnable", CLS("bool ok;\nvoid Awake() { ok = true; }"), "ok"));
    CHECK(Try("GetComponent<T>()", CLS("bool ok = true;\nvoid Start() { var rb = GetComponent<Rigidbody2D>(); }"), "ok"));
    CHECK(Try("Quaternion.Euler rot", CLS("bool ok;\nvoid Start() { transform.rotation = Quaternion.Euler(0, 45, 0); ok = true; }"), "ok"));
    CHECK(Try("Mathf.Sin(Time.time)", CLS("bool ok;\nvoid Update() { transform.position = new Vector3(Mathf.Sin(Time.time), 0, 0); ok = true; }"), "ok"));
    CHECK(Try("namespace wrapper", "namespace Game {\npublic class P : MonoBehaviour {\n bool ok;\n void Start() { ok = true; }\n}\n}\n", "ok"));
    CHECK(Try("string.Length / ToUpper", CLS("bool ok;\nvoid Start() { string s = \"ab\"; ok = s.Length == 2 && s.ToUpper() == \"AB\"; }"), "ok"));
    CHECK(Try("x.ToString()", CLS("bool ok;\nvoid Start() { int a = 5; ok = a.ToString() == \"5\"; }"), "ok"));
    CHECK(Try("Dictionary", CLS("bool ok;\nDictionary<string,int> d = new Dictionary<string,int>();\nvoid Start() { d[\"a\"] = 2; ok = d[\"a\"] == 2 && d.ContainsKey(\"a\"); }"), "ok"));
    CHECK(Try("cast (int)/(float)", CLS("bool ok;\nvoid Start() { float f = 3.7f; int i = (int)f; float r = (float)7 / 2; ok = i == 3 && r == 3.5; }"), "ok"));
    CHECK(Try("cast (int)Mathf.Floor", CLS("bool ok;\nvoid Start() { int i = (int)Mathf.Floor(2.9f); ok = i == 2; }"), "ok"));
    CHECK(Try("enum", CLS("public enum State { Idle, Run, Jump }\nState s = State.Run;\nbool ok;\nvoid Start() { ok = s == State.Run && s != State.Idle; }"), "ok"));
    CHECK(Try("multi declarators", CLS("float a = 1f, b = 2f;\nint c, d;\nbool ok;\nvoid Start() { ok = a + b == 3; }"), "ok"));
    CHECK(Try("%= and prefix ++", CLS("bool ok;\nvoid Start() { int a = 7; a %= 4; int b = 1; ++b; ok = a == 3 && b == 2; }"), "ok"));
    CHECK(Try("char literal", CLS("bool ok;\nvoid Start() { char c = 'x'; string s = \"a\" + c; ok = s == \"ax\"; }"), "ok"));
    CHECK(Try("(a-b).normalized", CLS("bool ok;\nvoid Start() { Vector3 t = new Vector3(3, 0, 0); var d = (t - transform.position).normalized; ok = d.x == 1; }"), "ok"));
    CHECK(Try("Vector3.MoveTowards", CLS("bool ok;\nvoid Start() { transform.position = Vector3.MoveTowards(transform.position, new Vector3(10, 0, 0), 2); ok = transform.position.x == 2; }"), "ok"));
    CHECK(Try("transform.forward*speed", CLS("bool ok;\nfloat speed = 2;\nvoid Start() { transform.position += transform.forward * speed; ok = transform.position.z == 2; }"), "ok"));
    CHECK(Try("localEulerAngles", CLS("bool ok;\nvoid Start() { transform.localEulerAngles = new Vector3(0, 90, 0); ok = Mathf.Abs(transform.eulerAngles.y - 90) < 0.1f; }"), "ok"));
    CHECK(Try("CompareTag / tag", CLS("bool ok;\nvoid Start() { gameObject.tag = \"Player\"; ok = CompareTag(\"Player\") && gameObject.CompareTag(\"Player\"); }"), "ok"));
    CHECK(Try("Destroy(gameObject, 2f)", CLS("bool ok = true;\nvoid Start() { Destroy(gameObject, 5f); }"), nullptr));
    CHECK(Try("Instantiate(prefab,pos,rot)", CLS("public GameObject bullet;\nbool ok = true;\nvoid Start() { if (bullet != null) Instantiate(bullet, transform.position, Quaternion.identity); }"), "ok"));
    CHECK(Try("transform.LookAt", CLS("bool ok = true;\nvoid Start() { transform.LookAt(new Vector3(0, 0, 5)); }"), "ok"));
    CHECK(Try("Vector3.forward/back", CLS("bool ok;\nvoid Start() { ok = Vector3.forward.z == 1 && Vector3.back.z == -1; }"), "ok"));
    CHECK(Try("override/virtual", CLS("bool ok;\nprotected virtual void Hit() { ok = true; }\npublic override void Start() { Hit(); }"), "ok"));
    CHECK(Try("switch on string", CLS("bool ok;\nvoid Start() { string w = \"b\"; switch (w) { case \"a\": break; case \"b\": ok = true; break; } }"), "ok"));
    CHECK(Try("while/break/do", CLS("bool ok;\nvoid Start() { int n = 0; while (true) { n++; if (n > 3) break; } do { n++; } while (n < 6); ok = n == 6; }"), "ok"));
    CHECK(Try("void return;", CLS("bool ok = true;\nvoid Check() { if (ok) return; ok = false; }\nvoid Start() { Check(); }"), "ok"));
    CHECK(Try("List<Vector3> of positions", CLS("bool ok;\nList<Vector3> pts = new List<Vector3>();\nvoid Start() { pts.Add(new Vector3(1,2,3)); ok = pts[0].y == 2 && pts.Count == 1; }"), "ok"));
    CHECK(Try("float[] new float[3]", CLS("bool ok;\nfloat[] arr = new float[3];\nvoid Start() { arr[1] = 1.5f; ok = arr.Length == 3 && arr[1] == 1.5; }"), "ok"));
    CHECK(Try("foreach KeyValuePair", CLS("bool ok;\nvoid Start() { var d = new Dictionary<string, int>(); d[\"a\"] = 1; d[\"b\"] = 2; int s = 0; foreach (KeyValuePair<string, int> kv in d) { s += kv.Value; } ok = s == 3; }"), "ok"));
    CHECK(Try("foreach (var k in d.Keys)", CLS("bool ok;\nvoid Start() { var d = new Dictionary<string, int> { {\"a\", 1}, {\"b\", 2} }; int s = 0; foreach (var k in d.Keys) { s += d[k]; } ok = s == 3; }"), "ok"));
    CHECK(Try("nextFire pattern", CLS("float nextFire = 0f;\npublic float fireRate = 0.5f;\nint shots;\nbool ok;\nvoid Update() { if (Time.time >= nextFire) { nextFire = Time.time + fireRate; shots++; } ok = shots >= 1; }"), "ok"));
    CHECK(Try("static field + method", CLS("public static int Score;\nbool ok;\npublic static void AddScore(int n) { Score += n; }\nvoid Start() { AddScore(5); ok = Score == 5; }"), "ok"));
    CHECK(Try("bitwise & |", CLS("bool ok;\nvoid Start() { int m = 5 & 3; int o = 4 | 1; ok = m == 1 && o == 5; }"), "ok"));
    CHECK(Try("Coroutine IEnumerator", CLS("bool ok;\nIEnumerator Wait() { yield return new WaitForSeconds(0.01f); ok = true; }\nvoid Start() { StartCoroutine(Wait()); }"), "ok"));
    CHECK(Try("property get;set;", CLS("public int Lives { get; set; } = 3;\nbool ok;\nvoid Start() { Lives--; ok = Lives == 2; }"), "ok"));
    CHECK(Try("lambda ForEach", CLS("bool ok;\nvoid Start() { var xs = new List<int> { 1, 2, 3 }; int s = 0; xs.ForEach(x => s += x); ok = s == 6; }"), "ok"));
    CHECK(Try("Mathf.Approximately/Epsilon", CLS("bool ok;\nvoid Start() { ok = Mathf.Approximately(0.1f + 0.2f, 0.3f); }"), "ok"));
    CHECK(Try("string.Format {0}", CLS("bool ok;\nvoid Start() { ok = string.Format(\"{0}-{1}\", 1, 2) == \"1-2\"; }"), "ok"));
    CHECK(Try("verbatim nested ternary", CLS("bool ok;\nvoid Start() { int a = 2; string t = a == 1 ? \"one\" : a == 2 ? \"two\" : \"many\"; ok = t == \"two\"; }"), "ok"));

    // ---- Coroutines: resume timing and waiting on another coroutine.
    {
        Scene s; auto* go = s.CreateGameObject("Hero");
        auto* sc = go->AddComponent<ScriptComponent>(); std::string e;
        CHECK(sc->LoadSource(CLS(
            "int steps = 0;\nint inner = 0;\nbool after = false;\n"
            "IEnumerator Loop() { while (true) { steps++; yield return new WaitForSeconds(0.1f); } }\n"
            "IEnumerator Inner() { yield return null; inner = 1; yield return null; inner = 2; }\n"
            "IEnumerator Outer() { yield return StartCoroutine(Inner()); after = inner == 2; }\n"
            "void Start() { StartCoroutine(Loop()); StartCoroutine(\"Outer\"); }"), &e));
        s.Start();
        CHECK(sc->VM()->GetGlobal("steps").AsFloat() == 1.0f);          // ran to its first yield at once
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);            // 0.5 s
        float steps = sc->VM()->GetGlobal("steps").AsFloat();
        CHECK(steps >= 5.0f && steps <= 7.0f);
        CHECK(sc->VM()->GetGlobal("after").AsBool());
        CHECK(sc->LoadSource(CLS("bool ok = true;"), &e));              // reload stops the old routines
        for (int i = 0; i < 10; ++i) s.Update(1.0f / 60.0f);
        CHECK(g_errs.empty());
    }
    // ---- Destroy(gameObject, t) waits; Destroy(other.gameObject) removes the other object.
    {
        Scene s; auto* go = s.CreateGameObject("Hero"); s.CreateGameObject("Enemy");
        auto* sc = go->AddComponent<ScriptComponent>(); std::string e;
        CHECK(sc->LoadSource(CLS("void Start() { var e = GameObject.Find(\"Enemy\"); if (e != null) Destroy(e); Destroy(gameObject, 0.25f); }"), &e));
        s.Start(); s.Update(1.0f / 60.0f);
        CHECK(s.Find("Enemy") == nullptr);
        CHECK(s.Find("Hero") != nullptr);
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
        CHECK(s.Find("Hero") == nullptr);
    }
    // ---- Collision handlers receive the other object.
    {
        Scene s; auto* go = s.CreateGameObject("Hero"); auto* coin = s.CreateGameObject("Coin"); coin->tag = "Pickup";
        auto* sc = go->AddComponent<ScriptComponent>(); std::string e;
        CHECK(sc->LoadSource(CLS("string hit = \"\";\nbool tagged;\n"
                                 "void OnTriggerEnter2D(Collider2D other) { hit = other.name; tagged = other.CompareTag(\"Pickup\") && other.tag == \"Pickup\"; }"), &e));
        s.Start();
        auto* col = coin->AddComponent<BoxCollider2D>();
        sc->OnTriggerEnter2D(col);
        CHECK(sc->VM()->GetGlobal("hit").AsString() == "Coin");
        CHECK(sc->VM()->GetGlobal("tagged").AsBool());
    }
    // ---- Vector3 arithmetic.
    {
        auto vm = CreateScriptVM("okayscript"); std::string e;
        CHECK(vm->Load("var a = new Vector3(1, 2, 3) * 2;\nvar b = a - Vector3.one;\nvar c = -b / 2;\nvar d = 2 * Vector3.up + Vector3.right;\n", &e));
        Vec3 a = vm->GetGlobal("a").AsVec3(), b = vm->GetGlobal("b").AsVec3(), c = vm->GetGlobal("c").AsVec3(), d = vm->GetGlobal("d").AsVec3();
        CHECK(a.x == 2 && a.y == 4 && a.z == 6);
        CHECK(b.x == 1 && b.y == 3 && b.z == 5);
        CHECK(std::fabs(c.x + 0.5f) < 1e-6f && std::fabs(c.z + 2.5f) < 1e-6f);
        CHECK(d.x == 1 && d.y == 2 && d.z == 0);
    }
    TEST_MAIN_RESULT();
}
