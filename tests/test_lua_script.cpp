#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Lua/LuaScriptVM.hpp"
#include "okay/Scripting/StarterScripts.hpp"
#include <algorithm>
#include <string>
#include <vector>

using namespace okay;

// Lua game scripts on our own interpreter, driving real GameObjects through the
// shared engine binding table: lifecycle, Inspector settings, input, events with
// the other object, GetComponent, errors with lines, and the per-callback budget.

static std::vector<std::string> g_errs, g_logs;
static const float kDt = 1.0f / 60.0f;

static bool ErrHas(const std::string& s) {
    return std::any_of(g_errs.begin(), g_errs.end(), [&](const std::string& l) { return l.find(s) != std::string::npos; });
}
static bool LogHas(const std::string& s) {
    return std::any_of(g_logs.begin(), g_logs.end(), [&](const std::string& l) { return l.find(s) != std::string::npos; });
}

struct World {
    Scene scene{"W"};
    GameObject* player = nullptr;
    ScriptComponent* sc = nullptr;
    bool loaded = false;
    std::string error;
    explicit World(const std::string& src, bool start = true) {
        player = scene.CreateGameObject("Player");
        sc = player->AddComponent<ScriptComponent>("lua");
        loaded = sc->LoadSource(src, &error);
        if (start) scene.Start();
    }
    void Frames(int n, std::vector<char> keys = {}) {
        for (int i = 0; i < n; ++i) { Input::FeedKeys(keys); scene.Update(kDt); }
    }
    vs::VsValue Get(const char* n) { return sc->VM()->GetGlobal(n); }
};

int main() {
    RUN_SUITE("lua_script");
    Log::sink = [](Log::Level l, const std::string& m) {
        if (l >= Log::Level::Error) g_errs.push_back(m); else g_logs.push_back(m);
    };

    // 0. Lua's own library tables keep their Lua meaning next to the engine API (the typed
    //    language's "string" / "int" / "float" value types are not Lua globals).
    {
        World w("ok = string.format('%d-%s', 7, string.upper('x')) == '7-X' and math.floor(2.5) == 2 and type(table.concat) == 'function'");
        CHECK(w.loaded);
        CHECK(w.Get("ok").AsBool());
        CHECK(LuaScriptVM::CompletionDoc("string.Format").empty());
        CHECK(!LuaScriptVM::CompletionDoc("KeyCode.Space").empty());   // text-script-only bindings stay in Lua
    }

    // 1. A table script: settings, Start, Update moving the transform with dt.
    {
        World w(R"LUA(
local Mover = { speed = 2, label = "hi", started = false }
function Mover:Start()
    self.started = true
    print("start " .. self.gameObject.name)
end
function Mover:Update(dt)
    self.transform.position = self.transform.position + Vector3.new(self.speed * dt, 0, 0)
end
return Mover
)LUA");
        CHECK(w.loaded);
        if (!w.loaded) std::cerr << w.error << "\n";
        w.Frames(60);
        CHECK(w.Get("started").AsBool());
        CHECK_NEAR(w.player->transform->Position().x, 2.0f, 0.01f);
        CHECK(LogHas("start Player"));
        std::vector<std::string> names = w.sc->VM()->GlobalNames();
        CHECK(names == (std::vector<std::string>{"label", "speed", "started"}));
    }

    // 2. Inspector overrides (component fields) replace the script's defaults.
    {
        Scene scene("S");
        GameObject* g = scene.CreateGameObject("Box");
        auto* sc = g->AddComponent<ScriptComponent>("lua");
        sc->fields["speed"] = "10";
        CHECK(sc->LoadSource("local M = { speed = 2 } function M:Update(dt) self.transform:Translate(self.speed * dt, 0, 0) end return M"));
        scene.Start();
        for (int i = 0; i < 30; ++i) scene.Update(kDt);
        CHECK_NEAR(g->transform->Position().x, 5.0f, 0.02f);
        CHECK(sc->VM()->GetGlobal("speed").AsFloat() == 10.0f);
    }

    // 3. Input and Time through the binding table; static types are global tables.
    {
        World w(R"LUA(
local P = { moved = 0, dtSeen = 0 }
function P:Update(dt)
    self.dtSeen = Time.deltaTime
    if Input.GetKey("d") then
        self.moved = self.moved + 1
        self.transform:Translate(Vector3.right * dt)
    end
end
return P
)LUA");
        CHECK(w.loaded);
        w.Frames(10, {'d'});
        w.Frames(5);
        CHECK(w.Get("moved").AsFloat() == 10.0f);
        CHECK_NEAR(w.Get("dtSeen").AsFloat(), kDt, 1e-5f);
        CHECK(w.player->transform->Position().x > 0.1f);
    }

    // 4. Events: OnTriggerEnter gets the other GameObject; engine names map to Lua names.
    {
        World w(R"LUA(
local T = { hits = 0, lastName = "" }
function T:OnTriggerEnter(other)
    self.hits = self.hits + 1
    self.lastName = other.name
    other.transform.position = Vector3(1, 2, 3)
end
function T:OnSequenceEvent(a, b) return a .. b end
return T
)LUA");
        CHECK(w.loaded);
        GameObject* coin = w.scene.CreateGameObject("Coin");
        CHECK(w.sc->VM()->HasEvent("on_trigger_enter"));
        w.sc->VM()->CallEventWith("on_trigger_enter", vs::VsValue::Object(coin->InstanceId()));
        CHECK(w.Get("hits").AsFloat() == 1.0f);
        CHECK(w.Get("lastName").AsString() == "Coin");
        CHECK_NEAR(coin->transform->Position().y, 2.0f, 1e-5f);
        vs::VsValue r = w.sc->VM()->CallEventArgs("on_sequence_event", {vs::VsValue{std::string("a")}, vs::VsValue{std::string("b")}});
        CHECK(r.AsString() == "ab");
    }

    // 5. GetComponent, GameObject.Find, engine object equality and printing.
    {
        World w(R"LUA(
local S = {}
function S:Start()
    local tr = self.gameObject:GetComponent("Transform")
    self.sameTransform = (tr == self.transform)
    self.noBody = (GetComponent("Rigidbody") == nil)
    local other = GameObject.Find("Other")
    self.otherName = other and other.name or "none"
    self.text = tostring(self.gameObject)
    Debug.Log("found " .. self.otherName)
end
return S
)LUA", false);
        w.scene.CreateGameObject("Other");
        w.scene.Start();
        CHECK(w.Get("sameTransform").AsBool());
        CHECK(w.Get("noBody").AsBool());
        CHECK(w.Get("otherName").AsString() == "Other");
        CHECK(w.Get("text").AsString() == "GameObject(Player)");
        CHECK(LogHas("found Other"));
    }

    // 6. Errors carry the line and do not stop later frames; a destroyed object is an error, not a crash.
    {
        g_errs.clear();
        World w(R"LUA(
local E = { n = 0 }
function E:Update(dt)
    self.n = self.n + 1
    if self.n == 2 then
        local missing = nil
        missing.field = 1
    end
end
return E
)LUA");
        w.Frames(4);
        CHECK(w.Get("n").AsFloat() == 4.0f);
        CHECK(ErrHas("script:7: attempt to index a nil value (local 'missing')"));
        auto* vm = static_cast<LuaScriptVM*>(w.sc->VM());
        CHECK(vm->ErrorCount() == 1);
        g_errs.clear();
        World d(R"LUA(
local D = {}
function D:Start() self.keep = GameObject.Find("Victim") end
function D:Update(dt) if self.keep then local n = self.keep.name end end
return D
)LUA", false);
        GameObject* victim = d.scene.CreateGameObject("Victim");
        d.scene.Start();
        d.scene.Destroy(victim);
        d.Frames(3);
        CHECK(ErrHas("destroyed"));
    }

    // 7. An endless loop in Update stops (budget), and the next frame runs again.
    {
        g_errs.clear();
        World w(R"LUA(
local L = { frames = 0 }
function L:Update(dt)
    self.frames = self.frames + 1
    if self.frames == 1 then while true do end end
end
return L
)LUA", false);
        static_cast<LuaScriptVM*>(w.sc->VM())->SetBudget(200000);
        w.scene.Start();
        w.Frames(3);
        CHECK(ErrHas("instruction budget"));
        CHECK(w.Get("frames").AsFloat() == 3.0f);
    }

    // 8. Old scenes: global functions named the old way still run.
    {
        World w("count = 0\nfunction start() count = 100 end\nfunction update(dt) count = count + 1 end\n");
        CHECK(w.loaded);
        w.Frames(5);
        CHECK(w.Get("count").AsFloat() == 105.0f);
        std::vector<std::string> names = w.sc->VM()->GlobalNames();
        CHECK(names == (std::vector<std::string>{"count"}));
    }

    // 9. Syntax errors: Load fails with the line; ValidateAll reports line and column.
    {
        World w("local M = {}\nfunction M:Update(dt)\n  x = = 1\nend\nreturn M\n", false);
        CHECK(!w.loaded);
        CHECK(w.error.find("line 3") != std::string::npos);
        LuaScriptVM vm;
        std::vector<ScriptDiagnostic> d = vm.ValidateAll("local a = 1\nlocal b = = 2\n");
        CHECK(d.size() == 1);
        if (!d.empty()) { CHECK(d[0].line == 2); CHECK(d[0].column == 11); CHECK(d[0].severity == 0); }
        CHECK(vm.ValidateAll("return {}").empty());
        std::string e;
        CHECK(!vm.Validate("if then", &e));
        CHECK(e.rfind("line 1", 0) == 0);
    }

    // 10. Bad engine calls are clear errors.
    {
        g_errs.clear();
        World w(R"LUA(
local B = {}
function B:Start()
    local ok1, e1 = pcall(function() return Input.GetKey("notakey") end)
    self.e1 = tostring(e1)
    local ok2, e2 = pcall(function() self.transform.Translate(1, 2, 3) end)
    self.e2 = tostring(e2)
    local ok3, e3 = pcall(function() self.transform:Translate("x") end)
    self.e3 = tostring(e3)
    local ok4, e4 = pcall(function() return self.transform.nope end)
    self.e4 = tostring(e4)
end
return B
)LUA");
        CHECK(w.Get("e1").AsString().find("isn't a key name") != std::string::npos);
        CHECK(w.Get("e2").AsString().find("with ':'") != std::string::npos);
        CHECK(w.Get("e3").AsString().find("bad arguments to Transform.Translate") != std::string::npos);
        CHECK(w.Get("e4").AsString().find("Transform has no member 'nope'") != std::string::npos);
    }

    // 11. Coroutines inside a game script (a timer over several frames).
    {
        World w(R"LUA(
local C = { stage = 0 }
function C:Start()
    self.co = coroutine.create(function()
        for i = 1, 3 do self.stage = i coroutine.yield() end
    end)
end
function C:Update(dt) if coroutine.status(self.co) ~= "dead" then coroutine.resume(self.co) end end
return C
)LUA");
        w.Frames(2);
        CHECK(w.Get("stage").AsFloat() == 2.0f);
        w.Frames(5);
        CHECK(w.Get("stage").AsFloat() == 3.0f);
    }

    // 12. The scene round trip keeps the language and the script runs after loading.
    {
        std::string text;
        {
            Scene s("Save");
            GameObject* g = s.CreateGameObject("Spinner");
            g->AddComponent<ScriptComponent>("lua")->LoadSource(
                "local R = {} function R:Update(dt) self.transform:Rotate(0, 90 * dt, 0) end return R");
            text = SceneSerializer::Serialize(s);
        }
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        GameObject* g = loaded.Find("Spinner");
        CHECK(g != nullptr);
        if (g) {
            auto* sc = g->GetComponent<ScriptComponent>();
            CHECK(sc && sc->Language() == "lua" && sc->VM() != nullptr);
            loaded.Start();
            for (int i = 0; i < 30; ++i) loaded.Update(kDt);
            CHECK(std::fabs(g->transform->Rotation().ToEuler().y) > 10.0f);
        }
    }

    // 13. Editor support: the starter script, lint warnings, member completion and Inspector settings.
    {
        const std::string starter = StarterScriptSource("lua");
        CHECK(starter.find("function Script:Update(dt)") != std::string::npos);
        LuaScriptVM vm;
        CHECK(vm.ValidateAll(starter).empty());
        std::vector<ScriptDiagnostic> w = LuaScriptVM::Lint(
            "local M = {}\nfunction M:Updte(dt)\n  if Input.GetKeyy(\"a\") then end\n  local s = \"Input.Nope\" -- Time.Nope\nend\nreturn M\n");
        CHECK(w.size() == 2);
        if (w.size() == 2) {
            CHECK(w[0].line == 2 && w[0].severity == 1 && w[0].message.find("Did you mean Update?") != std::string::npos);
            CHECK(w[1].line == 3 && w[1].message.find("Input has no member 'GetKeyy'. Did you mean GetKey?") != std::string::npos);
            CHECK(w[1].column == 12);
        }
        CHECK(LuaScriptVM::Lint("function update(dt) end\nfunction Start() end\nlocal v = Vector3.new(1, 2, 3)").empty());
        auto has = [](const std::vector<std::string>& v, const char* x) { return std::find(v.begin(), v.end(), x) != v.end(); };
        std::vector<std::string> m = LuaScriptVM::CompletionMembers("Input");
        CHECK(has(m, "GetKey") && has(m, "GetAxis"));
        m = LuaScriptVM::CompletionMembers("self.transform");
        CHECK(has(m, "position") && has(m, "Translate") && has(m, "GetComponent"));
        m = LuaScriptVM::CompletionMembers("self.transform.position");
        CHECK(has(m, "x") && has(m, "magnitude"));
        m = LuaScriptVM::CompletionMembers("string");
        CHECK(has(m, "format") && has(m, "gsub"));
        m = LuaScriptVM::CompletionMembers("Vector3");
        CHECK(has(m, "new") && has(m, "Distance") && has(m, "up"));
        CHECK(LuaScriptVM::CompletionMembers("nothingKnown").empty());
        CHECK(LuaScriptVM::CompletionDoc("Input.GetKey").find("Input.GetKey(key: string) -> boolean") == 0);
        CHECK(LuaScriptVM::CompletionDoc("Translate").find("Transform:Translate(") == 0);
        CHECK(LuaScriptVM::CompletionDoc("Update").find("every frame") != std::string::npos);
        CHECK(LuaScriptVM::CompletionDoc("Vector3").find("engine type Vector3") == 0);
        CHECK(LuaScriptVM::CompletionDoc("nothingHere").empty());
        // hover on the member part of "Input.GetKey" / "Time.deltaTime" (the editor's word is the bare name)
        CHECK(LuaScriptVM::CompletionDoc("GetKey").find("Input.GetKey(key: string) -> boolean") == 0);
        CHECK(LuaScriptVM::CompletionDoc("deltaTime").find("Time.deltaTime") == 0);
        // a .lua file attached to an object (New Script with "Also attach", drag and drop) is a Lua script
        CHECK(std::string(ScriptLanguageForExtension(".lua")) == "lua");
        CHECK(std::string(ScriptLanguageForExtension(".LUA")) == "lua");
        std::vector<std::string> words = LuaScriptVM::CompletionNames();
        CHECK(has(words, "function") && has(words, "Input.GetKey") && has(words, "Update"));
        std::vector<LuaScriptVM::Setting> st = LuaScriptVM::Settings(
            "local P = { speed = 5, name = 'ship', alive = true, offset = Vector3.new(1, 2.5, 0), onHit = function() end }\n"
            "print('not shown')\nreturn P\n");
        CHECK(st.size() == 4);
        if (st.size() == 4) {
            CHECK(st[0].name == "speed" && st[0].type == "number" && st[0].defaultText == "5");
            CHECK(st[1].name == "name" && st[1].defaultText == "\"ship\"");
            CHECK(st[2].name == "alive" && st[2].defaultText == "true");
            CHECK(st[3].name == "offset" && st[3].defaultText == "Vector3(1, 2.5, 0)");
        }
        CHECK(!LogHas("not shown"));
        CHECK(LuaScriptVM::Settings("this is not lua").empty());
    }

    // 14. Format Document for Lua: re-indent by blocks; strings, comments and long strings are not blocks.
    {
        const std::string messy =
            "local M = {\n"
            "speed = 1,\n"
            "}\n"
            "function M:Update(dt)\n"
            "if dt > 0 then\n"
            "print(\"end then do\") -- end\n"
            "elseif dt < 0 then\n"
            "x = 1\n"
            "else\n"
            "for i = 1, 3 do y = i end\n"
            "end\n"
            "foo(function()\n"
            "z = [[\n"
            "  keep\n"
            "]]\n"
            "end)\n"
            "end\n"
            "return M";
        const std::string want =
            "local M = {\n"
            "    speed = 1,\n"
            "}\n"
            "function M:Update(dt)\n"
            "    if dt > 0 then\n"
            "        print(\"end then do\") -- end\n"
            "    elseif dt < 0 then\n"
            "        x = 1\n"
            "    else\n"
            "        for i = 1, 3 do y = i end\n"
            "    end\n"
            "    foo(function()\n"
            "        z = [[\n"
            "  keep\n"
            "]]\n"
            "    end)\n"
            "end\n"
            "return M";
        const std::string got = LuaScriptVM::Format(messy);
        if (got != want) std::cerr << "--- formatted:\n" << got << "\n---\n";
        CHECK(got == want);
        CHECK(LuaScriptVM::Format(want) == want);   // formatting twice changes nothing
        LuaScriptVM vm;
        CHECK(vm.ValidateAll(got).empty());
    }

    TEST_MAIN_RESULT();
}
