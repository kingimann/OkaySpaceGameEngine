#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// Only meaningful when the engine was built with -DOKAY_WITH_LUA=ON.
int main() {
    RUN_SUITE("lua");

    auto langs = AvailableScriptLanguages();
    bool hasLua = false;
    for (auto& l : langs) if (l == "lua") hasLua = true;
    CHECK(hasLua);

    auto vm = CreateScriptVM("lua");
    CHECK(vm != nullptr);
    if (!vm) TEST_MAIN_RESULT();

    std::string err;
    bool ok = vm->Load("answer = 6 * 7\nfunction start() answer = answer + 0 end", &err);
    CHECK(ok);
    if (!ok) std::cerr << "  lua error: " << err << "\n";
    CHECK_NEAR(vm->GetGlobal("answer").AsFloat(), 42.0f, 0.001f);

    // Drive a Transform from Lua through a ScriptComponent.
    Scene scene("LuaScene");
    GameObject* go = scene.CreateGameObject("Hero");
    auto* sc = go->AddComponent<ScriptComponent>("lua");
    bool loaded = sc->LoadSource(
        "function start() set_pos(0,0) end\n"
        "function update(d) move(3*d, 0) end", &err);
    CHECK(loaded);
    if (!loaded) std::cerr << "  lua error: " << err << "\n";

    scene.Start();
    for (int i = 0; i < 10; ++i) scene.Update(0.1f);
    CHECK_NEAR(go->transform->localPosition.x, 3.0f, 0.01f);

    TEST_MAIN_RESULT();
}
