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
