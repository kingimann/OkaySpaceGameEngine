// The Lua engine binding: GameObject, Transform, components, Input, Time, Log, Physics, vectors,
// events, public fields, hot reload, error messages, limits. Runs through ScriptComponent (the
// same component every language uses).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Lua/LuaScriptVM.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

static std::vector<std::string> g_errors, g_infos;
static bool Has(const std::vector<std::string>& v, const std::string& part) {
    for (auto& s : v) if (s.find(part) != std::string::npos) return true;
    return false;
}

int main() {
    RUN_SUITE("lua_binding");
    Log::sink = [&](Log::Level l, const std::string& m) { if (l >= Log::Level::Error) g_errors.push_back(m); else g_infos.push_back(m); };

    // ---- registry
    {
        auto langs = AvailableScriptLanguages();
        CHECK(std::find(langs.begin(), langs.end(), "lua") != langs.end());
        CHECK(std::find(langs.begin(), langs.end(), "okayscript") != langs.end());
        auto vm = CreateScriptVM("lua");
        CHECK(vm != nullptr);
        if (vm) CHECK(std::string(vm->Language()) == "lua");
        CHECK(RemovedScriptLanguageMessage("lua").empty());
    }

    // ---- movement through Transform, Time, Vector3, and a public field with an inspector override
    {
        Scene scene("S");
        GameObject* go = scene.CreateGameObject("Mover");
        auto* sc = go->AddComponent<ScriptComponent>("lua");
        sc->fields["speed"] = "5";
        std::string err;
        CHECK(sc->LoadSource("speed = 2\nfunction Update(dt)\n  transform.position = transform.position + Vector3.new(speed * dt, 0, 0)\nend\n", &err));
        CHECK(err.empty());
        scene.Start();
        for (int i = 0; i < 10; ++i) scene.Update(0.1f);
        CHECK_NEAR(go->transform->localPosition.x, 5.0f, 1e-3f);   // 5 per second for one second (override beat the default 2)
        CHECK(g_errors.empty());
        auto names = sc->VM()->GlobalNames();
        CHECK(std::find(names.begin(), names.end(), "speed") != names.end());
        CHECK(std::find(names.begin(), names.end(), "Update") == names.end());
        CHECK(std::find(names.begin(), names.end(), "gameObject") == names.end());
        CHECK(std::find(names.begin(), names.end(), "Vector3") == names.end());
        CHECK_NEAR(sc->VM()->GetGlobal("speed").AsFloat(), 5.0f, 1e-6f);
    }

    // ---- start order, Awake -> OnEnable -> Start, and snake_case / PascalCase names
    {
        Scene scene("S");
        GameObject* go = scene.CreateGameObject("Order");
        auto* sc = go->AddComponent<ScriptComponent>("lua");
        CHECK(sc->LoadSource("log = ''\nfunction Awake() log = log .. 'A' end\nfunction on_enable() log = log .. 'E' end\nfunction start() log = log .. 'S' end\nfunction update(dt) log = log .. 'U' end\n"));
        scene.Start(); scene.Update(0.016f);
        CHECK(sc->VM()->GetGlobal("log").AsString() == "AESU");
        CHECK(sc->VM()->HasEvent("on_enable")); CHECK(sc->VM()->HasEvent("OnEnable")); CHECK(!sc->VM()->HasEvent("on_nothing"));
    }

    // ---- GameObject, Find, components through the property registry, Destroy
    {
        Scene scene("S");
        GameObject* go = scene.CreateGameObject("Me");
        GameObject* other = scene.CreateGameObject("Other");
        other->transform->localPosition = {1, 2, 3};
        go->AddComponent<Rigidbody3D>();
        auto* sc = go->AddComponent<ScriptComponent>("lua");
        std::string err;
        CHECK(sc->LoadSource(
            "function Start()\n"
            "  me_name = gameObject.name\n"
            "  local o = Find('Other')\n"
            "  other_x = o.transform.position.x\n"
            "  o.name = 'Renamed'\n"
            "  o.tag = 'Enemy'\n"
            "  local rb = gameObject:GetComponent('Rigidbody3D')\n"
            "  rb.mass = 7\n"
            "  mass = rb.mass\n"
            "  missing = tostring(gameObject:GetComponent('Camera'))\n"
            "  same = (gameObject == transform.gameObject)\n"
            "  tr_pos = tostring(o.transform.localPosition)\n"
            "  made = CreateGameObject('Spawned').name\n"
            "end\n", &err));
        scene.Start();
        CHECK(g_errors.empty()); for (auto& e : g_errors) std::printf("    %s\n", e.c_str());
        auto* vm = sc->VM();
        CHECK(vm->GetGlobal("me_name").AsString() == "Me");
        CHECK_NEAR(vm->GetGlobal("other_x").AsFloat(), 1.0f, 1e-5f);
        CHECK(other->name == "Renamed"); CHECK(std::string(other->tag) == "Enemy");
        CHECK_NEAR(go->GetComponent<Rigidbody3D>()->mass, 7.0f, 1e-5f);
        CHECK_NEAR(vm->GetGlobal("mass").AsFloat(), 7.0f, 1e-5f);
        CHECK(vm->GetGlobal("missing").AsString() == "nil");
        CHECK(vm->GetGlobal("same").AsBool());
        CHECK(vm->GetGlobal("tr_pos").AsString() == "(1.0, 2.0, 3.0)");
        CHECK(vm->GetGlobal("made").AsString() == "Spawned"); CHECK(scene.Find("Spawned") != nullptr);
    }
    {   // Destroy by object; a destroyed object reads as dead, not as a crash
        Scene scene("S");
        GameObject* go = scene.CreateGameObject("Me");
        scene.CreateGameObject("Victim");
        auto* sc = go->AddComponent<ScriptComponent>("lua");
        CHECK(sc->LoadSource("function Start() v = Find('Victim') Destroy(v) end\nfunction Update(dt) alive = v:IsAlive() end\n"));
        scene.Start(); scene.Update(0.016f); scene.Update(0.016f);
        CHECK(!sc->VM()->GetGlobal("alive").AsBool());
        CHECK(g_errors.empty());
    }

    // ---- events with `other`, contact globals, return values
    {
        Scene scene("S");
        GameObject* go = scene.CreateGameObject("Me");
        GameObject* wall = scene.CreateGameObject("Wall");
        wall->tag = "Solid";
        auto* sc = go->AddComponent<ScriptComponent>("lua");
        CHECK(sc->LoadSource(
            "function OnCollisionEnter(o) hit = o.name .. ':' .. o.tag end\n"
            "function on_trigger_stay(o) stay = o == other end\n"
            "function on_validate_hit(a, b) return a + b end\n"));
        scene.Start();
        sc->VM()->SetGlobal("other", vs::VsValue::Object(wall->InstanceId()));
        sc->VM()->CallEventWith("on_collision_enter", vs::VsValue::Object(wall->InstanceId()));
        CHECK(sc->VM()->GetGlobal("hit").AsString() == "Wall:Solid");
        sc->VM()->CallEventWith("on_trigger_stay", vs::VsValue::Object(wall->InstanceId()));
        CHECK(sc->VM()->GetGlobal("stay").AsBool());
        std::string err;
        vs::VsValue r = sc->VM()->CallEventArgs("on_validate_hit", {vs::VsValue(2.0f), vs::VsValue(3.5f)}, &err);
        CHECK_NEAR(r.AsFloat(), 5.5f, 1e-6f); CHECK(err.empty());
        sc->VM()->CallEvent("on_nothing_defined");   // no handler: silently nothing
        CHECK(g_errors.empty());
    }

    // ---- values both ways: vectors, arrays, maps, save-game style globals
    {
        Scene scene("S");
        GameObject* go = scene.CreateGameObject("Me");
        auto* sc = go->AddComponent<ScriptComponent>("lua");
        CHECK(sc->LoadSource("v = (Vector3.new(1, 2, 3) + Vector3.new(1, 1, 1)) * 2\nm = Vector3.new(3, 4, 0).magnitude\nlist = {1, 2, 3}\nconf = {a = 1, b = 'x'}\nflag = true\nname = 'n'\ncount = 3\n"));
        auto* vm = sc->VM();
        CHECK(vm->GetGlobal("v").IsVec3());
        Vec3 v = vm->GetGlobal("v").AsVec3();
        CHECK_NEAR(v.x, 4, 1e-5f); CHECK_NEAR(v.y, 6, 1e-5f); CHECK_NEAR(v.z, 8, 1e-5f);
        CHECK_NEAR(vm->GetGlobal("m").AsFloat(), 5.0f, 1e-5f);
        CHECK(vm->GetGlobal("list").IsArray()); CHECK(vm->GetGlobal("list").AsArray()->size() == 3);
        CHECK(vm->GetGlobal("conf").IsMap()); CHECK(vm->GetGlobal("conf").AsMap()->at("b").AsString() == "x");
        CHECK(vm->GetGlobal("flag").AsBool()); CHECK(vm->GetGlobal("name").AsString() == "n");
        vm->SetGlobal("count", vs::VsValue(9.0f)); vm->SetGlobal("pos", vs::VsValue(Vec3{1, 2, 3}));
        vm->SetGlobal("arr", [] { auto a = vs::VsValue::MakeArray(); a.AsArray()->push_back(vs::VsValue(4.0f)); return a; }());
        CHECK(sc->Source().size() > 0);
        std::string e2;
        // read back through a script
        auto sc2 = go->AddComponent<ScriptComponent>("lua");
        (void)sc2;
        CHECK(vm->GetGlobal("count").AsFloat() == 9.0f);
        auto names = vm->GlobalNames();
        for (const char* n : {"v", "m", "list", "conf", "flag", "name", "count", "pos", "arr"}) CHECK(std::find(names.begin(), names.end(), n) != names.end());
    }

    // ---- Input, Time, Log, Physics
    {
        Scene scene("S");
        GameObject* go = scene.CreateGameObject("Me");
        GameObject* box = scene.CreateGameObject("Box");
        box->transform->localPosition = {0, 0, 5};
        box->AddComponent<BoxCollider3D>();
        auto* sc = go->AddComponent<ScriptComponent>("lua");
        g_infos.clear();
        CHECK(sc->LoadSource(
            "function Start()\n"
            "  key = Input.GetKey('w')\n"
            "  down = Input.GetKeyDown('space')\n"
            "  mb = Input.GetMouseButton(0)\n"
            "  local hit = Physics.Raycast(Vector3.new(0, 0, 0), Vector3.new(0, 0, 1), 100)\n"
            "  hit_name = hit and hit.gameObject.name or 'none'\n"
            "  hit_dist = hit and hit.distance or -1\n"
            "  miss = tostring(Physics.Raycast(Vector3.new(0, 0, 0), Vector3.new(0, 1, 0), 100))\n"
            "  Log.info('hello', 42)\n"
            "  print('printed', 1)\n"
            "end\n"
            "function Update(dt) dt_seen = Time.deltaTime end\n"));
        scene.Start(); scene.Update(0.25f);
        CHECK(g_errors.empty()); for (auto& e : g_errors) std::printf("    %s\n", e.c_str());
        auto* vm = sc->VM();
        CHECK(!vm->GetGlobal("key").AsBool()); CHECK(!vm->GetGlobal("down").AsBool()); CHECK(!vm->GetGlobal("mb").AsBool());
        CHECK(vm->GetGlobal("hit_name").AsString() == "Box");
        CHECK_NEAR(vm->GetGlobal("hit_dist").AsFloat(), 4.5f, 0.05f);
        CHECK(vm->GetGlobal("miss").AsString() == "nil");
        CHECK(Has(g_infos, "hello 42")); CHECK(Has(g_infos, "printed\t1"));
        CHECK_NEAR(vm->GetGlobal("dt_seen").AsFloat(), 0.25f, 1e-5f);
    }

    // ---- runtime errors: plain words, script name and line, game keeps running
    {
        Scene scene("S");
        GameObject* go = scene.CreateGameObject("Me");
        auto* sc = go->AddComponent<ScriptComponent>("lua");
        g_errors.clear();
        CHECK(sc->LoadSource("n = 0\nfunction Update(dt)\n  n = n + 1\n  local x = nil\n  x.y = 1\nend\n"));
        scene.Start(); scene.Update(0.016f); scene.Update(0.016f);
        CHECK(g_errors.size() >= 2);
        CHECK(Has(g_errors, "script.lua:5: attempt to index a nil value (local 'x')"));
        CHECK_NEAR(sc->VM()->GetGlobal("n").AsFloat(), 2.0f, 1e-6f);   // ran twice: an error does not stop the game
        // unknown names say what is wrong
        g_errors.clear();
        CHECK(sc->LoadSource("function Update(dt) transform.nonsense = 1 end\n"));
        scene.Update(0.016f);
        CHECK(Has(g_errors, "cannot set 'nonsense' on a Transform"));
        g_errors.clear();
        CHECK(sc->LoadSource("function Update(dt) local k = Input.GetKey('nokey') end\n"));
        scene.Update(0.016f);
        CHECK(Has(g_errors, "unknown key name 'nokey'"));
        g_errors.clear();
        CHECK(sc->LoadSource("function Update(dt) gameObject:GetComponent('Rigidbody3D').nothing = 1 end\nfunction Start() end\n"));
        scene.Update(0.016f);
        CHECK(g_errors.empty() || Has(g_errors, "attempt to index a nil value"));   // no such component on this object: a script error, not a crash
    }

    // ---- limits: an endless loop and a memory hog do not freeze or crash the game
    {
        Scene scene("S");
        GameObject* go = scene.CreateGameObject("Me");
        auto* sc = go->AddComponent<ScriptComponent>("lua");
        CHECK(sc->LoadSource("ticks = 0\nfunction Update(dt) ticks = ticks + 1 if ticks == 2 then while true do end end end\n"));
        auto* lvm = static_cast<LuaScriptVM*>(sc->VM());
        lvm->SetLimits(50000, 0.0, 0);
        scene.Start(); g_errors.clear();
        scene.Update(0.016f); scene.Update(0.016f); scene.Update(0.016f);
        CHECK(Has(g_errors, "too many instructions"));
        CHECK_NEAR(sc->VM()->GetGlobal("ticks").AsFloat(), 3.0f, 1e-6f);   // frame 3 still ran
        g_errors.clear();
        CHECK(sc->LoadSource("function Update(dt) local t = {} local i = 0 while true do i = i + 1 t[i] = string.rep('x', 200) .. i end end\n"));
        lvm = static_cast<LuaScriptVM*>(sc->VM());
        lvm->SetLimits(0, 0.0, 4u << 20);
        scene.Update(0.016f);
        CHECK(Has(g_errors, "not enough memory"));
        CHECK(lvm->MemoryUsed() < (16u << 20));
    }

    // ---- sandbox surface inside game scripts
    {
        Scene scene("S");
        GameObject* go = scene.CreateGameObject("Me");
        auto* sc = go->AddComponent<ScriptComponent>("lua");
        CHECK(sc->LoadSource("a = tostring(os) b = tostring(io.open) c = tostring(require) d = tostring(dofile) e = tostring(debug) f = type(string) g = type(Mathf)\n"));
        auto* vm = sc->VM();
        CHECK(vm->GetGlobal("a").AsString() == "nil"); CHECK(vm->GetGlobal("b").AsString() == "nil");
        CHECK(vm->GetGlobal("c").AsString() == "nil"); CHECK(vm->GetGlobal("d").AsString() == "nil");
        CHECK(vm->GetGlobal("e").AsString() == "nil"); CHECK(vm->GetGlobal("f").AsString() == "table");
        CHECK(vm->GetGlobal("g").AsString() == "table");
    }

    // ---- hot reload keeps the public fields, a bad reload keeps the old script running
    {
        Scene scene("S");
        GameObject* go = scene.CreateGameObject("Me");
        auto* sc = go->AddComponent<ScriptComponent>("lua");
        CHECK(sc->LoadSource("counter = 0\nfunction Update(dt) counter = counter + 1 end\n"));
        scene.Start();
        for (int i = 0; i < 3; ++i) scene.Update(0.016f);
        CHECK_NEAR(sc->VM()->GetGlobal("counter").AsFloat(), 3.0f, 1e-6f);
        std::string err;
        CHECK(sc->VM()->HotReload("counter = 100\nextra = 7\nfunction Update(dt) counter = counter + 10 end\n", &err));
        scene.Update(0.016f);
        CHECK_NEAR(sc->VM()->GetGlobal("counter").AsFloat(), 13.0f, 1e-6f);   // old value kept, new code running
        CHECK_NEAR(sc->VM()->GetGlobal("extra").AsFloat(), 7.0f, 1e-6f);      // a new field gets its default
        CHECK(!sc->VM()->HotReload("counter = = 1\n", &err));
        CHECK(err.find("script.lua:1:") == 0);
        scene.Update(0.016f);
        CHECK_NEAR(sc->VM()->GetGlobal("counter").AsFloat(), 23.0f, 1e-6f);   // old script still running
        CHECK(!sc->VM()->HotReload("error('load failed')\n", &err));
        CHECK(err.find("load failed") != std::string::npos);
        // other languages say "not supported"
        auto ok = CreateScriptVM("okayscript");
        CHECK(!ok->HotReload("x = 1", &err));
    }

    // ---- validation: every problem has a line and plain words
    {
        auto vm = CreateScriptVM("lua");
        CHECK(vm->ValidateAll("x = 1\nfunction f()\n  return 2\nend\n").empty());
        auto d = vm->ValidateAll("x = 1\ny = 2\nz = = 3\n");
        CHECK(d.size() == 1); if (!d.empty()) { CHECK(d[0].line == 3); CHECK(d[0].message.find("unexpected symbol near '='") != std::string::npos); }
        std::string err;
        CHECK(!vm->Validate("if x then\n", &err)); CHECK(err.rfind("line 2:", 0) == 0);
        CHECK(vm->Validate("x = 1", &err));
        std::string e2;
        CHECK(!vm->Load("function broken(", &e2)); CHECK(e2.find("script.lua:1:") == 0);
    }

    // ---- scene round trip: language and source survive saving, and the script runs after loading
    {
        std::string text;
        {
            Scene scene("S");
            GameObject* go = scene.CreateGameObject("Saved");
            auto* sc = go->AddComponent<ScriptComponent>("lua");
            sc->LoadSource("x = 41\nfunction Start() x = x + 1 end\n");
            text = SceneSerializer::Serialize(scene);
        }
        CHECK(text.find("\"lua\"") != std::string::npos);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* sc = loaded.Find("Saved") ? loaded.Find("Saved")->GetComponent<ScriptComponent>() : nullptr;
        CHECK(sc != nullptr);
        if (sc) { CHECK(sc->Language() == "lua"); loaded.Start(); CHECK_NEAR(sc->VM()->GetGlobal("x").AsFloat(), 42.0f, 1e-6f); }
    }

    // ---- file-backed script: Reload() hot reloads (fields kept; a broken file leaves the old script running)
    {
        const std::string path = (std::filesystem::temp_directory_path() / "okay_lua_reload_test.lua").string();
        auto write = [&](const std::string& t) { std::ofstream(path, std::ios::trunc) << t; };
        write("hp = 10\nfunction Update(dt) hp = hp - 1 end\n");
        Scene scene("S");
        GameObject* go = scene.CreateGameObject("Me");
        auto* sc = go->AddComponent<ScriptComponent>("lua");
        std::string err;
        CHECK(sc->LoadFile(path, &err));
        scene.Start(); scene.Update(0.016f); scene.Update(0.016f);
        CHECK_NEAR(sc->VM()->GetGlobal("hp").AsFloat(), 8.0f, 1e-6f);
        write("hp = 999\nfunction Update(dt) hp = hp - 5 end\n");
        CHECK(sc->Reload(&err));
        scene.Update(0.016f);
        CHECK_NEAR(sc->VM()->GetGlobal("hp").AsFloat(), 3.0f, 1e-6f);
        write("hp = = 1\n");
        CHECK(!sc->Reload(&err)); CHECK(err.find("script.lua:1:") == 0);
        scene.Update(0.016f);
        CHECK_NEAR(sc->VM()->GetGlobal("hp").AsFloat(), -2.0f, 1e-6f);
        std::remove(path.c_str());
    }

    // ---- the shipped example scene examples/lua/lua_pickup.okayscene
    {
        std::ifstream in(std::string(OKAY_SOURCE_DIR) + "/examples/lua/lua_pickup.okayscene");
        CHECK(in.good());
        std::stringstream ss; ss << in.rdbuf();
        Scene scene("Example");
        std::string err;
        CHECK(SceneSerializer::Deserialize(scene, ss.str(), &err));
        GameObject* pickup = scene.Find("Pickup");
        GameObject* player = scene.Find("Player");
        CHECK(pickup != nullptr); CHECK(player != nullptr);
        auto* sc = pickup ? pickup->GetComponent<ScriptComponent>() : nullptr;
        CHECK(sc != nullptr);
        if (sc && player) {
            CHECK(sc->Language() == "lua");
            g_errors.clear(); g_infos.clear();
            scene.Start();
            float minY = 9, maxY = -9;
            for (int i = 0; i < 120; ++i) { scene.Update(1.0f / 60.0f); minY = std::min(minY, pickup->transform->localPosition.y); maxY = std::max(maxY, pickup->transform->localPosition.y); }
            CHECK(g_errors.empty());
            CHECK(maxY > 1.2f); CHECK(minY < 0.8f);                     // it bobs around y = 1
            CHECK(!sc->VM()->GetGlobal("collected").AsBool());           // the Player is far away
            player->transform->localPosition = {2.5f, 1.0f, 0.0f};
            scene.Update(1.0f / 60.0f);
            CHECK(sc->VM()->GetGlobal("collected").AsBool());
            CHECK(Has(g_infos, "Picked up!"));
        }
    }
    TEST_MAIN_RESULT();
}
