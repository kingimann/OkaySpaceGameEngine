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

    // --- after() fires once at its delay; every() repeats; cancel_timers stops ---
    {
        Scene scene("Timers");
        GameObject* go = scene.CreateGameObject("T");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() { after(0.5, \"boom\"); every(0.2, \"tick\"); }\n"
            "function boom() { set_x(1); }\n"
            "function tick() { move(0, 1); }\n"));   // y += 1 per tick
        scene.Start();
        for (int i = 0; i < 5; ++i) scene.Update(0.1f);   // 0.5s total
        CHECK_NEAR(go->transform->localPosition.x, 1.0f, 0.001f);   // boom fired once
        CHECK_NEAR(go->transform->localPosition.y, 2.0f, 0.001f);   // ticks at 0.2, 0.4

        // boom does not fire again; ticks keep coming.
        for (int i = 0; i < 5; ++i) scene.Update(0.1f);   // to 1.0s
        CHECK_NEAR(go->transform->localPosition.x, 1.0f, 0.001f);   // still once
        CHECK(go->transform->localPosition.y >= 4.0f);              // more ticks
    }

    // --- set_parent / detach / has_parent reparent the object ---
    {
        Scene scene("Parent");
        GameObject* platform = scene.CreateGameObject("Platform");
        platform->transform->localPosition = {5, 0, 0};

        GameObject* go = scene.CreateGameObject("Crate");
        go->transform->localPosition = {5, 2, 0};   // world (5,2)
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function mount() { set_parent(\"Platform\"); set_x(has_parent()); }\n"
            "function drop()  { detach();              set_y(has_parent()); }\n"));
        scene.Start();

        sc->VM()->CallEvent("mount");
        CHECK(go->transform->Parent() == platform->transform);
        CHECK_NEAR(go->transform->localPosition.x, 1.0f, 0.001f);   // has_parent() -> true(1)

        sc->VM()->CallEvent("drop");
        CHECK(go->transform->Parent() == nullptr);
        CHECK_NEAR(go->transform->localPosition.y, 0.0f, 0.001f);   // has_parent() -> false(0)
    }

    // --- cancel_timers() halts a repeating callback ---
    {
        Scene scene("Cancel");
        GameObject* go = scene.CreateGameObject("C");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() { every(0.1, \"tick\"); }\n"
            "function tick() { move(1, 0); cancel_timers(); }\n"));  // fire once then stop
        scene.Start();
        for (int i = 0; i < 10; ++i) scene.Update(0.1f);
        CHECK_NEAR(go->transform->localPosition.x, 1.0f, 0.001f);    // exactly one tick
    }

    TEST_MAIN_RESULT();
}
