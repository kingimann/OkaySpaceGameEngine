#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("script_lib");

    // --- New math helpers: wrap / ping_pong / smoothstep / hypot / angle_to ---
    {
        const char* src = R"SCRIPT(
            var w1 = wrap(370, 0, 360);     // 10
            var w2 = wrap(-10, 0, 360);     // 350
            var p1 = ping_pong(0.5, 1);     // 0.5
            var p2 = ping_pong(1.5, 1);     // 0.5 (coming back down)
            var s0 = smoothstep(0, 10, 0);  // 0
            var s1 = smoothstep(0, 10, 1);  // 10
            var sm = smoothstep(0, 10, 0.5);// 5 (symmetric midpoint)
            var h  = hypot(3, 4);           // 5
            var ar = angle_to(0, 0, 1, 0);  // 0 degrees (+x)
            var au = angle_to(0, 0, 0, 1);  // 90 degrees (+y)
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("w1").AsFloat(), 10.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("w2").AsFloat(), 350.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("p1").AsFloat(), 0.5f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("p2").AsFloat(), 0.5f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("s0").AsFloat(), 0.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("s1").AsFloat(), 10.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("sm").AsFloat(), 5.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("h").AsFloat(), 5.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("ar").AsFloat(), 0.0f, 0.01f);
        CHECK_NEAR(vm->GetGlobal("au").AsFloat(), 90.0f, 0.01f);
    }

    // --- chance(p): chance(1) is always true, chance(0) always false ---
    {
        const char* src = R"SCRIPT(
            var always = chance(1);
            var never = chance(0);
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK(vm->GetGlobal("always").AsBool() == true);
        CHECK(vm->GetGlobal("never").AsBool() == false);
    }

    // --- String helpers: contains / starts/ends / replace / trim / repeat ---
    {
        const char* src = R"SCRIPT(
            var c  = str_contains("hello world", "o w");   // true
            var sw = starts_with("game_over", "game");     // true
            var ew = ends_with("level.okayscene", ".okayscene"); // true
            var rp = replace("a-b-c", "-", "+");           // a+b+c
            var tr = trim("   hi  ");                       // "hi"
            var bar = repeat("=", 5);                       // "====="
            var barlen = str_len(bar);                      // 5
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK(vm->GetGlobal("c").AsBool() == true);
        CHECK(vm->GetGlobal("sw").AsBool() == true);
        CHECK(vm->GetGlobal("ew").AsBool() == true);
        CHECK(vm->GetGlobal("rp").AsString() == "a+b+c");
        CHECK(vm->GetGlobal("tr").AsString() == "hi");
        CHECK(vm->GetGlobal("bar").AsString() == "=====");
        CHECK_NEAR(vm->GetGlobal("barlen").AsFloat(), 5.0f, 0.001f);
    }

    // --- Scene queries: obj_x/obj_y and dist_to a named object ---
    {
        Scene scene("AI");
        GameObject* player = scene.CreateGameObject("Player");
        player->transform->localPosition = {3.0f, 4.0f, 0.0f};

        // Enemy at origin reads the player's position, then its distance (3-4-5).
        GameObject* enemy = scene.CreateGameObject("Enemy");
        auto* sc = enemy->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() {"
            "  var d = dist_to(\"Player\");" // distance from origin to (3,4) = 5
            "  set_z(d);"
            "  set_x(obj_x(\"Player\"));"    // -> 3
            "  set_y(obj_y(\"Player\"));"    // -> 4
            "}"));
        scene.Start();
        CHECK_NEAR(enemy->transform->localPosition.x, 3.0f, 0.001f);
        CHECK_NEAR(enemy->transform->localPosition.y, 4.0f, 0.001f);
        CHECK_NEAR(enemy->transform->localPosition.z, 5.0f, 0.001f);
    }

    TEST_MAIN_RESULT();
}
