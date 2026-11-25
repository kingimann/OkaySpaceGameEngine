#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/JavaScript/JavaScriptVM.hpp"
#include "okay/Scripting/StarterScripts.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/UIFilePicker.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Scripting/ScriptBindings.hpp"
#include "okay/Scripting/ScriptVM.hpp"
#include <algorithm>
#include <string>
#include <vector>

using namespace okay;

// JavaScript game scripts on our own engine (okay::js), driving real GameObjects through
// the same binding table Lua and the C#-style language use: lifecycle, Inspector
// settings, input, events with the other object, GetComponent, the generated binding
// layer, errors with lines, the per-callback budget and the sandbox. Mirrors lua_script.

static std::vector<std::string> g_errs, g_logs;
static const float kDt = 1.0f / 60.0f;

// A game script the okay.js test (tests/okayjs/test_okayjs_script.mjs) runs too: both runtimes read the same file,
// so they are checked on the same text. The files are in tests/javascript/game_scripts/.
#ifndef OKAY_SOURCE_DIR
#define OKAY_SOURCE_DIR "."
#endif
struct ScriptFile {
    std::string path;
    explicit ScriptFile(const char* rel) : path(std::string(OKAY_SOURCE_DIR) + "/" + rel) {}
};
static ScriptFile GameScript(const char* name) {
    return ScriptFile((std::string("tests/javascript/game_scripts/") + name).c_str());
}

// dup-allow-begin: the same log capture and one-object World as the Lua and OkayScript-next script tests; a
// shared test header would edit those tests (other lanes' files), so it is left for a follow-up agreed on #422
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
    explicit World(const std::string& src, bool start = true) { Init(&src, nullptr, start); }
    explicit World(const ScriptFile& file, bool start = true) { Init(nullptr, &file.path, start); }
    void Init(const std::string* src, const std::string* path, bool start) {
        player = scene.CreateGameObject("Player");
        sc = player->AddComponent<ScriptComponent>("javascript");
        loaded = src ? sc->LoadSource(*src, &error) : sc->LoadFile(*path, &error);
        if (!loaded) std::cerr << "  load error: " << error << "\n";
        if (start) scene.Start();
    }
    void Frames(int n, std::vector<char> keys = {}) {
        for (int i = 0; i < n; ++i) { Input::FeedKeys(keys); scene.Update(kDt); }
    }
    vs::VsValue Get(const char* n) { return sc->VM()->GetGlobal(n); }
};
// dup-allow-end

// What a script holds after it ran, checked in a chain: Script(vm).Num("hits", 1).Text("name", "Coin").
struct Script {
    IScriptVM* vm;
    explicit Script(IScriptVM* v) : vm(v) {}
    Script& Num(const char* name, double want, double eps = 1e-5) {
        double got = vm->GetGlobal(name).AsFloat();
        if (std::fabs(got - want) > eps) std::cerr << "  " << name << " = " << got << ", want " << want << "\n";
        CHECK(std::fabs(got - want) <= eps);
        return *this;
    }
    Script& Yes(const char* name, bool want = true) {
        bool got = vm->GetGlobal(name).AsBool();
        if (got != want) std::cerr << "  " << name << " = " << got << ", want " << want << "\n";
        CHECK(got == want);
        return *this;
    }
    Script& Text(const char* name, const std::string& want) {
        std::string got = vm->GetGlobal(name).AsString();
        if (got != want) std::cerr << "  " << name << " = '" << got << "', want '" << want << "'\n";
        CHECK(got == want);
        return *this;
    }
    Script& Says(const char* name, const std::string& part) {
        std::string got = vm->GetGlobal(name).AsString();
        if (got.find(part) == std::string::npos) std::cerr << "  " << name << " = '" << got << "', want it to say '" << part << "'\n";
        CHECK(got.find(part) != std::string::npos);
        return *this;
    }
    Script& Settings(std::vector<std::string> want) {
        CHECK(vm->GlobalNames() == want);
        return *this;
    }
};
static bool HasAll(const std::vector<std::string>& v, std::initializer_list<const char*> names) {
    for (const char* n : names)
        if (std::find(v.begin(), v.end(), n) == v.end()) {
            std::cerr << "  missing " << n << "\n";
            return false;
        }
    return true;
}

int main() {
    RUN_SUITE("js_script");
    Log::sink = [](Log::Level l, const std::string& m) {
        if (l >= Log::Level::Error) g_errs.push_back(m); else g_logs.push_back(m);
    };

    // 0. The JavaScript library keeps its meaning next to the engine API.
    {
        World w("var ok = Math.floor(2.5) === 2 && JSON.stringify([1]) === '[1]' && typeof Object.keys === 'function' && [3, 1].sort().join() === '1,3';");
        CHECK(w.loaded);
        CHECK(w.Get("ok").AsBool());
    }

    // 1. A class script: fields are settings, Start, Update moving the transform with dt.
    {
        World w(GameScript("mover.js"));
        CHECK(w.loaded);
        w.Frames(60);
        Script(w.sc->VM()).Yes("started").Settings({"label", "speed", "started"});
        CHECK_NEAR(w.player->transform->Position().x, 2.0f, 0.01f);
        CHECK(LogHas("start Player"));
    }

    // 2. Inspector overrides (component fields) replace the script's defaults; an exported object works too.
    {
        Scene scene("S");
        GameObject* g = scene.CreateGameObject("Box");
        auto* sc = g->AddComponent<ScriptComponent>("javascript");
        sc->fields["speed"] = "10";
        CHECK(sc->LoadFile(GameScript("speed_object.js").path));
        scene.Start();
        for (int i = 0; i < 30; ++i) scene.Update(kDt);
        CHECK_NEAR(g->transform->Position().x, 5.0f, 0.02f);
        CHECK(sc->VM()->GetGlobal("speed").AsFloat() == 10.0f);
    }

    // 3. Input and Time through the binding table; static types are global objects. The script is okay.js's
    //    example (web/okayjs/examples/scripts/key_mover.js), which the okay.js test runs too.
    {
        World w(ScriptFile("web/okayjs/examples/scripts/key_mover.js"));
        CHECK(w.loaded);
        w.Frames(10, {'d'});   // ten frames holding d, then five without
        w.Frames(5);
        Script(w.sc->VM()).Num("moved", 10).Num("dtSeen", kDt);
        CHECK(w.player->transform->Position().x > 0.1f);
    }

    // 4. Events: OnTriggerEnter gets the other GameObject; engine names map to handler names; return values.
    {
        World w(R"JS(
export default class T {
    hits = 0;
    lastName = "";
    OnTriggerEnter(other) {
        this.hits++;
        this.lastName = other.name;
        other.transform.position = new Vector3(1, 2, 3);
    }
    OnSequenceEvent(a, b) { return a + b; }
}
)JS");
        CHECK(w.loaded);
        IScriptVM* vm = w.sc->VM();
        GameObject* coin = w.scene.CreateGameObject("Coin");
        CHECK(vm->HasEvent("on_trigger_enter") && !vm->HasEvent("on_trigger_exit"));
        vm->CallEventWith("on_trigger_enter", vs::VsValue::Object(coin->InstanceId()));
        Script(vm).Num("hits", 1).Text("lastName", "Coin");
        CHECK_NEAR(coin->transform->Position().y, 2.0f, 1e-5f);
        const std::vector<vs::VsValue> args = {vs::VsValue{std::string("a")}, vs::VsValue{std::string("b")}};
        CHECK(vm->CallEventArgs("on_sequence_event", args).AsString() == "ab");
    }

    // 5. GetComponent, GameObject.Find, engine object identity and printing.
    {
        World w(GameScript("find_other.js"), false);
        CHECK(w.loaded);
        w.scene.CreateGameObject("Other");
        w.scene.Start();
        Script(w.sc->VM()).Yes("sameTransform").Yes("noBody").Text("otherName", "Other").Text("text", "GameObject(Player)").Yes("isObject");
        CHECK(LogHas("found Other"));
    }

    // 6. Errors carry the line and do not stop later frames; a destroyed object is an error, not a crash.
    {
        g_errs.clear();
        World w(GameScript("error_line.js"));
        w.Frames(4);
        CHECK(w.Get("n").AsFloat() == 4.0f);
        CHECK(ErrHas("script:7: TypeError: can not set 'field' of undefined"));
        auto* vm = static_cast<JavaScriptVM*>(w.sc->VM());
        CHECK(vm->ErrorCount() == 1);
        g_errs.clear();
        World d(GameScript("keep_destroyed.js"), false);
        GameObject* victim = d.scene.CreateGameObject("Victim");
        d.scene.Start();
        d.scene.Destroy(victim);
        d.Frames(3);
        CHECK(ErrHas("destroyed"));
    }

    // 7. An endless loop in Update stops (budget), and the next frame runs again; try/catch can not swallow the stop.
    {
        g_errs.clear();
        World w(R"JS(
export default class L {
    frames = 0;
    Update(dt) {
        this.frames++;
        if (this.frames === 1) { for (;;) { try { while (true) {} } catch (e) {} } }
    }
}
)JS", false);
        static_cast<JavaScriptVM*>(w.sc->VM())->SetBudget(200000);
        w.scene.Start();
        w.Frames(3);
        CHECK(ErrHas("instruction budget"));
        CHECK(w.Get("frames").AsFloat() == 3.0f);
    }

    // 8. Scripts without an export: global functions are the handlers, top-level variables the settings.
    {
        World w("var count = 0;\nlet label = 'x';\nfunction start() { count = 100; }\nfunction Update(dt) { count++; }\n");
        CHECK(w.loaded);
        w.Frames(5);
        CHECK(w.Get("count").AsFloat() == 105.0f);
        std::vector<std::string> names = w.sc->VM()->GlobalNames();
        CHECK(names == (std::vector<std::string>{"count", "label"}));
    }

    // 9. Syntax errors: Load fails with the line; ValidateAll reports line and column.
    {
        World w("export default class M {\n  Update(dt) {\n    x = = 1;\n  }\n}\n", false);
        CHECK(!w.loaded);
        CHECK(w.error.find("line 3") != std::string::npos);
        JavaScriptVM vm;
        std::vector<ScriptDiagnostic> d = vm.ValidateAll("let a = 1;\nlet b = = 2;\n");
        CHECK(d.size() == 1);
        if (!d.empty()) { CHECK(d[0].line == 2); CHECK(d[0].column == 9); CHECK(d[0].severity == 0); }
        CHECK(vm.ValidateAll("export default {};").empty());
        std::string e;
        CHECK(!vm.Validate("if (", &e));
        CHECK(e.rfind("line 1", 0) == 0);
    }

    // 10. Bad engine calls are clear errors (catchable in the script).
    {
        g_errs.clear();
        World w(R"JS(
export default class B {
    Start() {
        try { Input.GetKey("notakey"); } catch (e) { this.e1 = String(e); }
        const tr = this.transform;
        try { tr.Translate.call(null, 1, 2, 3); } catch (e) { this.e2 = String(e); }
        try { tr.Translate("x"); } catch (e) { this.e3 = String(e); }
        this.e4 = String(tr.nope);
        try { tr.nope = 1; } catch (e) { this.e5 = String(e); }
    }
}
)JS");
        Script(w.sc->VM())
            .Says("e1", "isn't a key name")
            .Says("e2", "call Translate on a Transform")
            .Says("e3", "bad arguments to Transform.Translate")
            .Text("e4", "undefined")
            .Says("e5", "Transform has no settable property 'nope'");
    }

    // 11. Generators and async functions inside a game script (a timer over several frames).
    {
        World w(R"JS(
export default class C {
    stage = 0;
    done = false;
    Start() {
        const self = this;
        this.co = (function* () { for (let i = 1; i <= 3; i++) { self.stage = i; yield; } })();
        (async () => { await null; this.done = true; })();
    }
    Update(dt) { this.co.next(); }
}
)JS");
        Script(w.sc->VM()).Yes("done");   // the await finished in Start's own job pass
        w.Frames(2);
        Script(w.sc->VM()).Num("stage", 2);
        w.Frames(5);
        Script(w.sc->VM()).Num("stage", 3);   // the generator is done after three steps
    }

    // 12. The scene round trip keeps the language and the script runs after loading.
    {
        std::string text;
        {
            Scene s("Save");
            GameObject* g = s.CreateGameObject("Spinner");
            g->AddComponent<ScriptComponent>("javascript")->LoadSource(
                "export default { Update(dt) { this.transform.Rotate(0, 90 * dt, 0); } };");
            text = SceneSerializer::Serialize(s);
        }
        Scene back("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(back, text, &err) && back.Find("Spinner"));
        if (GameObject* spinner = back.Find("Spinner")) {
            const ScriptComponent* script = spinner->GetComponent<ScriptComponent>();
            CHECK(script != nullptr);
            CHECK(script && std::string(script->Language()) == JavaScriptVM::kLanguage && script->VM());
            back.Start();
            int frames = 30;
            while (frames-- > 0) back.Update(kDt);
            CHECK(std::fabs(spinner->transform->Rotation().ToEuler().y) > 10.0f);
        }
    }

    // 13. Editor support: the starter script, lint warnings, member completion and Inspector settings.
    {
        const std::string starter = StarterScriptSource("javascript");
        CHECK(starter.find("Update(dt) {") != std::string::npos);
        JavaScriptVM vm;
        CHECK(vm.ValidateAll(starter).empty());
        std::vector<ScriptDiagnostic> lint = JavaScriptVM::Lint(
            "export default class M {\n  Updte(dt) {\n    if (Input.GetKeyy(\"a\")) {}\n    const s = \"Input.Nope\"; // Time.Nope\n  }\n}\n");
        CHECK(lint.size() == 2);
        if (lint.size() == 2) {
            const ScriptDiagnostic& typo = lint[0];
            const ScriptDiagnostic& member = lint[1];
            CHECK(typo.line == 2 && typo.severity == 1);
            CHECK(typo.message.find("Did you mean Update?") != std::string::npos);
            CHECK(member.line == 3 && member.column == 15);
            CHECK(member.message.find("Input has no member 'GetKeyy'. Did you mean GetKey?") != std::string::npos);
        }
        CHECK(JavaScriptVM::Lint("function update(dt) {}\nfunction Start() {}\nconst v = new Vector3(1, 2, 3);").empty());
        struct MemberCase { const char* receiver; std::initializer_list<const char*> some; };
        const MemberCase members[] = {
            {"Input", {"GetKey", "GetAxis"}},
            {"this.transform", {"position", "Translate", "GetComponent"}},
            {"this.transform.position", {"x", "magnitude", "add"}},
            {"Math", {"floor", "PI"}},
            {"Vector3", {"Distance", "up"}},
        };
        for (const MemberCase& c : members) CHECK(HasAll(JavaScriptVM::CompletionMembers(c.receiver), c.some));
        CHECK(JavaScriptVM::CompletionMembers("nothingKnown").empty());
        struct DocCase { const char* name; const char* starts; };
        const DocCase docs[] = {
            {"Input.GetKey", "Input.GetKey(key: string) -> boolean"}, {"GetKey", "Input.GetKey(key: string) -> boolean"},
            {"Translate", "Transform.Translate("}, {"class", "JavaScript keyword"}, {"Update", "called every frame"},
        };
        for (const DocCase& d : docs) CHECK(JavaScriptVM::CompletionDoc(d.name).rfind(d.starts, 0) == 0);
        CHECK(JavaScriptVM::CompletionDoc("nothingHere").empty());
        for (const char* ext : {".js", "JS", ".mjs"}) CHECK(std::string(ScriptLanguageForExtension(ext)) == "javascript");
        CHECK(HasAll(JavaScriptVM::CompletionNames(), {"function", "Input.GetKey", "Update", "Math.floor"}));
        const std::vector<JavaScriptVM::Setting> settings = JavaScriptVM::Settings(
            "export default class P {\n  speed = 5;\n  name = 'ship';\n  alive = true;\n  offset = new Vector3(1, 2.5, 0);\n"
            "  onHit = () => {};\n  Start() { console.log('not shown'); }\n}\nconsole.log('not shown either');\n");
        const JavaScriptVM::Setting wantSettings[] = {
            {"speed", "number", "5"}, {"name", "string", "\"ship\""}, {"alive", "boolean", "true"}, {"offset", "Vector3", "Vector3(1, 2.5, 0)"}};
        CHECK(settings.size() == 4);
        for (std::size_t i = 0; i < settings.size() && i < 4; ++i)
            CHECK(settings[i].name == wantSettings[i].name && settings[i].type == wantSettings[i].type &&
                  settings[i].defaultText == wantSettings[i].defaultText);
        CHECK(!LogHas("not shown"));
        CHECK(JavaScriptVM::Settings("this is not javascript").empty());
        CHECK(HasAll(AvailableScriptLanguages(), {"javascript", "lua"}));
    }

    // 14. Format Document for JavaScript: re-indent by brackets; strings, comments and template text are not brackets.
    {
        const std::string messy =
            "export default class M {\n"
            "Update(dt) {\n"
            "if (dt > 0) {\n"
            "console.log(\"} { (\"); // }\n"
            "} else {\n"
            "const t = `\n"
            "  keep {\n"
            "`;\n"
            "}\n"
            "foo(() => {\n"
            "x = [\n"
            "1,\n"
            "];\n"
            "});\n"
            "}\n"
            "}";
        const std::string want =
            "export default class M {\n"
            "    Update(dt) {\n"
            "        if (dt > 0) {\n"
            "            console.log(\"} { (\"); // }\n"
            "        } else {\n"
            "            const t = `\n"
            "  keep {\n"
            "`;\n"
            "        }\n"
            "        foo(() => {\n"
            "            x = [\n"
            "                1,\n"
            "            ];\n"
            "        });\n"
            "    }\n"
            "}";
        const std::string got = JavaScriptVM::Format(messy);
        if (got != want) std::cerr << "--- formatted:\n" << got << "\n---\n";
        CHECK(got == want);
        CHECK(JavaScriptVM::Format(want) == want);
    }

    // 15. The generated binding layer (Scripting/ScriptBindings): a component the typed table does not list (Light)
    //     is a handle with its properties and generated methods; Rigidbody.InvMass falls through; a removed
    //     component is a clear error, never a crash.
    {
        Scene room{"B"};
        GameObject* lamp = room.CreateGameObject("Lamp");
        Light* light = lamp->AddComponent<Light>();
        lamp->AddComponent<Rigidbody3D>();
        auto* script = lamp->AddComponent<ScriptComponent>("javascript");
        std::string loadError;
        g_errs.clear();
        CHECK(script->LoadSource(R"JS(
var got, kind, text, owner, flicker, held, same, inv, missing, bad, nomember, later, gone;
function Start() {
    const l = GetComponent("Light");
    l.intensity = 2.5;
    got = l.intensity;
    kind = typeof l;
    text = String(l);
    owner = l.gameObject.name;
    l.type = "Spot";
    flicker = l.FlickerFactor();
    held = l;
    same = GetComponent("Light") === l;
    inv = GetComponent("Rigidbody").InvMass();
    missing = GetComponent("AudioSource") === null;
    try { l.intensity = "bright"; } catch (e) { bad = String(e); }
    try { l.noSuchThing = 1; } catch (e) { nomember = String(e); }
}
function Update(dt) {
    try { later = held.intensity; } catch (e) { gone = String(e); }
}
)JS", &loadError));
        if (!loadError.empty()) std::cerr << "  load: " << loadError << "\n";
        room.Start();
        for (const auto& e : g_errs) std::cerr << "  error: " << e << "\n";
        CHECK_NEAR(light->intensity, 2.5f, 1e-5f);
        CHECK(light->type == Light::Type::Spot);
        Script(script->VM())
            .Num("got", 2.5)
            .Text("kind", "object")
            .Says("text", "Light")
            .Text("owner", "Lamp")
            .Yes("same")
            .Yes("missing")
            .Says("bad", "intensity")
            .Says("nomember", "no settable property 'noSuchThing'");
        CHECK(script->VM()->GetGlobal("inv").AsFloat() > 0.0f);
        lamp->RemoveComponent(light);   // the handle the script kept now reads as an error
        room.Update(kDt);
        Script(script->VM()).Says("gone", "null");
    }

    // 16. The sandbox: no file, network, process or module access, no compiling code from strings.
    {
        World w("var open = ['require', 'process', 'fetch', 'XMLHttpRequest', 'WebSocket', 'importScripts', 'Deno', 'Bun', 'module', 'exports']\n"
                "  .filter(n => typeof globalThis[n] !== 'undefined').length;\n"
                "var noEval = false; try { eval('1'); } catch (e) { noEval = e instanceof EvalError; }\n"
                "var noFn = false; try { Function('return 1'); } catch (e) { noFn = e instanceof EvalError; }\n"
                "var noCtor = false; try { (() => {}).constructor('return 1'); } catch (e) { noCtor = e instanceof EvalError; }");
        CHECK(w.loaded);
        Script(w.sc->VM()).Num("open", 0).Yes("noEval").Yes("noFn").Yes("noCtor");
    }

    // 17. The generated layer's DENY_* rules hold for JavaScript too, through both binding tables.
    {
        Scene picks{"D"};
        GameObject* picker = picks.CreateGameObject("Picker");
        picker->AddComponent<UIFilePicker>();
        picker->AddComponent<AnimStateMachine>();   // what the typed table's Animator finds
        auto* script = picker->AddComponent<ScriptComponent>("javascript");
        std::string loadError;
        CHECK(script->LoadSource(R"JS(
var rootOk = true, openOk = true, animOk = true, animErr = "", rmOk = false, rmErr = "";
function Start() {
    const fp = GetComponent("UIFilePicker");
    try { fp.SetRoot("/"); } catch (e) { rootOk = false; }
    try { fp.Open("passwd"); } catch (e) { openOk = false; }
    try { GetComponent("Animator").SetCharacterModel("/etc/passwd"); } catch (e) { animOk = false; animErr = String(e); }
    try { GetComponent("Animator").SetRootMotion(false); rmOk = true; } catch (e) { rmErr = String(e); }
}
)JS", &loadError));
        if (!loadError.empty()) std::cerr << "  load: " << loadError << "\n";
        picks.Start();
        Script(script->VM())
            .Yes("rootOk", false)    // a denied class: UIFilePicker
            .Yes("openOk", false)
            .Yes("animOk", false)    // a typed method whose text parameter names a place
            .Says("animErr", "SetCharacterModel")
            .Yes("rmOk");            // the rest of Animator still works
    }

    // 18. A collision handler gets the other object and the contact globals; a class with inheritance works.
    {
        World w(R"JS(
class Base { Hit(o) { return "base " + o.name; } }
export default class Kid extends Base {
    msg = "";
    OnCollisionEnter(other) { this.msg = this.Hit(other) + " " + (typeof contact_point); }
}
)JS");
        CHECK(w.loaded);
        GameObject* wall = w.scene.CreateGameObject("Wall");
        w.sc->VM()->SetGlobal("contact_point", vs::VsValue(Vec3{1, 0, 0}));   // what ScriptComponent::SetContact sets
        w.sc->VM()->CallEventWith("on_collision_enter", vs::VsValue::Object(wall->InstanceId()));
        CHECK(w.Get("msg").AsString() == "base Wall object");
    }

    // 19. A script that keeps making garbage every frame stays within its memory (the collector runs).
    {
        World w("export default { Update(dt) { let a = []; for (let i = 0; i < 2000; i++) a.push({i, s: 'x' + i}); this.n = a.length; } };");
        static_cast<JavaScriptVM*>(w.sc->VM())->SetMemoryLimit(16u << 20);
        CHECK(w.loaded);
        g_errs.clear();
        w.Frames(60);
        CHECK(!ErrHas("memory"));
        CHECK(w.Get("n").AsFloat() == 2000.0f);
    }

    TEST_MAIN_RESULT();
}
