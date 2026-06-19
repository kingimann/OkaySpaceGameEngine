#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// OkayScript can be written in a Unity/C# style: PascalCase Start()/Update(),
// dotted property access (transform.position.x), new Vector3(...), typed vars,
// i++ loops, and a `class : MonoBehaviour` wrapper.
int main() {
    RUN_SUITE("unity_script");

    // --- void Start() + transform.position = new Vector3(...) -----------
    {
        Scene s("UStart"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Player");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "void Start() {\n"
            "    transform.position = new Vector3(3, 4, 0);\n"
            "}\n"));
        s.Start();
        CHECK_NEAR(go->transform->Position().x, 3.0f, 0.001f);
        CHECK_NEAR(go->transform->Position().y, 4.0f, 0.001f);
    }

    // --- void Update() with Time.deltaTime + property write ------------
    {
        Scene s("UUpdate"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Mover");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "float speed = 2f;\n"
            "void Update() {\n"
            "    transform.position.x = transform.position.x + speed * Time.deltaTime;\n"
            "}\n"));
        s.Start();
        for (int i = 0; i < 10; ++i) s.Update(0.1f);   // 1.0s * 2 = 2.0
        CHECK_NEAR(go->transform->Position().x, 2.0f, 0.05f);
    }

    // --- class : MonoBehaviour wrapper, typed vars, i++ for-loop --------
    {
        Scene s("UClass"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Counter");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "public class Counter : MonoBehaviour {\n"
            "    int total = 0;\n"
            "    void Start() {\n"
            "        for (int i = 0; i < 5; i++) { total = total + i; }\n"  // 0+1+2+3+4 = 10
            "        transform.position.y = total;\n"
            "    }\n"
            "}\n"));
        s.Start();
        CHECK_NEAR(go->transform->Position().y, 10.0f, 0.001f);
    }

    // --- Mathf.* + gameObject.name + dotted method call ----------------
    {
        Scene s("UMath"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Hero");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "void Start() {\n"
            "    transform.position.x = Mathf.Abs(-7);\n"
            "    transform.Translate(0, 1);\n"          // Unity-style move
            "    if (gameObject.name == \"Hero\") { transform.position.z = 9; }\n"
            "}\n"));
        s.Start();
        CHECK_NEAR(go->transform->Position().x, 7.0f, 0.001f);
        CHECK_NEAR(go->transform->Position().y, 1.0f, 0.001f);
        CHECK_NEAR(go->transform->Position().z, 9.0f, 0.001f);
    }

    // --- [SerializeField] attributes, foreach, GetComponent<T> ---------
    {
        Scene s("UAdv"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Adv");
        go->AddComponent<SpriteRenderer>();
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "public class Adv : MonoBehaviour {\n"
            "    [SerializeField] float total = 0f;\n"
            "    [Header(\"Stats\")]\n"
            "    int hits = 0;\n"
            "    void Start() {\n"
            "        var nums = [10, 20, 30];\n"
            "        foreach (var n in nums) { total = total + n; }\n"   // 60
            "        if (GetComponent<SpriteRenderer>()) { hits = 1; }\n"
            "        transform.position.x = total;\n"
            "        transform.position.y = hits;\n"
            "    }\n"
            "}\n"));
        s.Start();
        CHECK_NEAR(go->transform->Position().x, 60.0f, 0.001f);
        CHECK_NEAR(go->transform->Position().y, 1.0f, 0.001f);   // GetComponent found it
    }

    // --- Quaternion.Euler / transform.rotation + AddComponent ----------
    {
        Scene s("UQuat"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Q");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "void Start() {\n"
            "    transform.rotation = Quaternion.Euler(0, 0, 90);\n"
            "    transform.position.y = transform.eulerAngles.z;\n"   // read it back
            "    if (AddComponent<Rigidbody2D>()) { transform.position.x = 1; }\n"
            "}\n"));
        s.Start();
        CHECK_NEAR(go->transform->Position().x, 1.0f, 0.001f);    // component added
        CHECK(go->GetComponent<Rigidbody2D>() != nullptr);
        CHECK_NEAR(go->transform->Position().y, 90.0f, 0.5f);     // rotation applied
    }

    // --- Plain-English aliases read naturally --------------------------
    {
        Scene s("UFriendly"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Friendly");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() {\n"
            "    place_at(3, 4);\n"
            "    set_position_x(position_x() + 1);\n"      // 3 -> 4
            "    set_position_y(position_y() + 6);\n"      // 4 -> 10
            "}\n"));
        s.Start();
        CHECK_NEAR(go->transform->localPosition.x, 4.0f, 0.001f);
        CHECK_NEAR(go->transform->localPosition.y, 10.0f, 0.001f);
    }

    // --- String interpolation: $"...{expr}..." -------------------------
    {
        Scene s("UInterp"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Label");
        go->AddComponent<TextRenderer>();
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "void Start() {\n"
            "    int score = 42;\n"
            "    set_text($\"Score: {score} ({score + 8})\");\n"
            "}\n"));
        s.Start();
        CHECK(go->GetComponent<TextRenderer>()->text == "Score: 42 (50)");
    }

    // --- switch / case / default ---------------------------------------
    {
        Scene s("USwitch"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Sw");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "void Start() {\n"
            "    int kind = 2;\n"
            "    switch (kind) {\n"
            "        case 1: transform.position.x = 10; break;\n"
            "        case 2: transform.position.x = 20; break;\n"
            "        default: transform.position.x = 99; break;\n"
            "    }\n"
            "}\n"));
        s.Start();
        CHECK_NEAR(go->transform->Position().x, 20.0f, 0.001f);
    }

    // --- More Mathf: clamp01, repeat, inverse_lerp ---------------------
    {
        Scene s("UMath2"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("M");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "void Start() {\n"
            "    transform.position.x = Mathf.Clamp01(2.5);\n"        // 1
            "    transform.position.y = Mathf.Repeat(7, 3);\n"        // 1
            "    transform.position.z = Mathf.InverseLerp(0, 10, 4);\n"  // 0.4
            "}\n"));
        s.Start();
        CHECK_NEAR(go->transform->Position().x, 1.0f, 0.001f);
        CHECK_NEAR(go->transform->Position().y, 1.0f, 0.001f);
        CHECK_NEAR(go->transform->Position().z, 0.4f, 0.001f);
    }

    // --- Vector3 variables: component access + vector math -------------
    {
        Scene s("UVec"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("V");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "void Start() {\n"
            "    var v = new Vector3(3, 4, 0);\n"
            "    v.x = v.x + 1;\n"                       // 4
            "    var w = vec_add(v, new Vector3(0, 1, 0));\n"  // (4,5,0)
            "    transform.position.x = v.x;\n"          // 4
            "    transform.position.y = w.y;\n"          // 5
            "    transform.position.z = vec_length(new Vector3(3, 4, 0));\n"  // 5
            "}\n"));
        s.Start();
        CHECK_NEAR(go->transform->Position().x, 4.0f, 0.001f);
        CHECK_NEAR(go->transform->Position().y, 5.0f, 0.001f);
        CHECK_NEAR(go->transform->Position().z, 5.0f, 0.001f);
    }

    // --- Vector properties: .magnitude / .normalized -------------------
    {
        Scene s("UVecProp"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("VP");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "void Start() {\n"
            "    var v = new Vector3(3, 4, 0);\n"
            "    transform.position.x = v.magnitude;\n"      // 5
            "    var n = v.normalized;\n"
            "    transform.position.y = n.x;\n"              // 0.6
            "    transform.position.z = Mathf.PerlinNoise(5, 5);\n"  // in [0,1]
            "}\n"));
        s.Start();
        CHECK_NEAR(go->transform->Position().x, 5.0f, 0.001f);
        CHECK_NEAR(go->transform->Position().y, 0.6f, 0.001f);
        CHECK(go->transform->Position().z >= 0.0f && go->transform->Position().z <= 1.0f);
    }

    // --- do-while runs at least once -----------------------------------
    {
        Scene s("UDo"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("D");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "void Start() {\n"
            "    int n = 0;\n"
            "    do { n = n + 1; } while (n < 5);\n"
            "    transform.position.x = n;\n"            // 5
            "}\n"));
        s.Start();
        CHECK_NEAR(go->transform->Position().x, 5.0f, 0.001f);
    }

    // --- Legacy lowercase still works (backward compatible) ------------
    {
        Scene s("ULegacy"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Old");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function start() { set_x(5); }\n"));
        s.Start();
        CHECK_NEAR(go->transform->localPosition.x, 5.0f, 0.001f);
    }

    TEST_MAIN_RESULT();
}
