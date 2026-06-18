#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// Only built when the engine is configured with -DOKAY_WITH_CSHARP=ON.
// Requires `mcs` and the Mono runtime at run time.
int main() {
    RUN_SUITE("csharp");

    auto langs = AvailableScriptLanguages();
    bool hasCs = false;
    for (auto& l : langs) if (l == "csharp") hasCs = true;
    CHECK(hasCs);

    // Drive a Transform from a real C# script through a ScriptComponent.
    Scene scene("CSharpScene");
    GameObject* go = scene.CreateGameObject("Hero");
    auto* sc = go->AddComponent<ScriptComponent>("csharp");
    const char* src = R"CS(
        public static class Script {
            public static void Start() { Okay.SetPos(0, 0); Okay.SetVar("ran", 1); }
            public static void Update(float dt) { Okay.Move(4 * dt, 0); }
        }
    )CS";
    std::string err;
    bool ok = sc->LoadSource(src, &err);
    CHECK(ok);
    if (!ok) { std::cerr << "  csharp error: " << err << "\n"; TEST_MAIN_RESULT(); }

    scene.Start();
    for (int i = 0; i < 10; ++i) scene.Update(0.1f); // ~1 second
    CHECK_NEAR(go->transform->localPosition.x, 4.0f, 0.05f);
    CHECK_NEAR(sc->Host().globals["ran"].AsFloat(), 1.0f, 0.001f);

    TEST_MAIN_RESULT();
}
