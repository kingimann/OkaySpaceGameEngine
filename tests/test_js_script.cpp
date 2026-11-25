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
    explicit World(const std::string& src, bool start = true) {
        player = scene.CreateGameObject("Player");
        sc = player->AddComponent<ScriptComponent>("javascript");
        loaded = sc->LoadSource(src, &error);
        if (!loaded) std::cerr << "  load error: " << error << "\n";
        if (start) scene.Start();
    }
    void Frames(int n, std::vector<char> keys = {}) {
        for (int i = 0; i < n; ++i) { Input::FeedKeys(keys); scene.Update(kDt); }
    }
    vs::VsValue Get(const char* n) { return sc->VM()->GetGlobal(n); }
};
// dup-allow-end

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
        World w(R"JS(
export default class Mover {
    speed = 2;
    label = "hi";
    started = false;
    Start() {
        this.started = true;
        console.log("start " + this.gameObject.name);
    }
    Update(dt) {
        this.transform.position = this.transform.position.add(new Vector3(this.speed * dt, 0, 0));
    }
}
)JS");
        CHECK(w.loaded);
        w.Frames(60);
        CHECK(w.Get("started").AsBool());
        CHECK_NEAR(w.player->transform->Position().x, 2.0f, 0.01f);
        CHECK(LogHas("start Player"));
        std::vector<std::string> names = w.sc->VM()->GlobalNames();
        CHECK(names == (std::vector<std::string>{"label", "speed", "started"}));
    }

    // 2. Inspector overrides (component fields) replace the script's defaults; an exported object works too.
    {
        Scene scene("S");
        GameObject* g = scene.CreateGameObject("Box");
        auto* sc = g->AddComponent<ScriptComponent>("javascript");
        sc->fields["speed"] = "10";
        CHECK(sc->LoadSource("export default { speed: 2, Update(dt) { this.transform.Translate(this.speed * dt, 0, 0); } };"));
        scene.Start();
        for (int i = 0; i < 30; ++i) scene.Update(kDt);
        CHECK_NEAR(g->transform->Position().x, 5.0f, 0.02f);
        CHECK(sc->VM()->GetGlobal("speed").AsFloat() == 10.0f);
    }

    // 3. Input and Time through the binding table; static types are global objects.
    {
        World w(R"JS(
export default class P {
    moved = 0;
    dtSeen = 0;
    Update(dt) {
        this.dtSeen = Time.deltaTime;
        if (Input.GetKey("d")) {
            this.moved++;
            this.transform.Translate(Vector3.right.scale(dt));
        }
    }
}
)JS");
        CHECK(w.loaded);
        w.Frames(10, {'d'});
        w.Frames(5);
        CHECK(w.Get("moved").AsFloat() == 10.0f);
        CHECK_NEAR(w.Get("dtSeen").AsFloat(), kDt, 1e-5f);
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
        GameObject* coin = w.scene.CreateGameObject("Coin");
        CHECK(w.sc->VM()->HasEvent("on_trigger_enter"));
        w.sc->VM()->CallEventWith("on_trigger_enter", vs::VsValue::Object(coin->InstanceId()));
        CHECK(w.Get("hits").AsFloat() == 1.0f);
        CHECK(w.Get("lastName").AsString() == "Coin");
        CHECK_NEAR(coin->transform->Position().y, 2.0f, 1e-5f);
        vs::VsValue r = w.sc->VM()->CallEventArgs("on_sequence_event", {vs::VsValue{std::string("a")}, vs::VsValue{std::string("b")}});
        CHECK(r.AsString() == "ab");
    }

    // 5. GetComponent, GameObject.Find, engine object identity and printing.
    {
        World w(R"JS(
export default class S {
    Start() {
        const tr = this.gameObject.GetComponent("Transform");
        this.sameTransform = tr === this.transform;
        this.noBody = GetComponent("Rigidbody") === null;
        const other = GameObject.Find("Other");
        this.otherName = other ? other.name : "none";
        this.text = String(this.gameObject);
        Debug.Log("found " + this.otherName);
    }
}
)JS", false);
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
        World w(R"JS(
export default class E {
    n = 0;
    Update(dt) {
        this.n++;
        if (this.n === 2) {
            const missing = undefined;
            missing.field = 1;
        }
    }
}
)JS");
        w.Frames(4);
        CHECK(w.Get("n").AsFloat() == 4.0f);
        CHECK(ErrHas("script:8: TypeError: can not set 'field' of undefined"));
        auto* vm = static_cast<JavaScriptVM*>(w.sc->VM());
        CHECK(vm->ErrorCount() == 1);
        g_errs.clear();
        World d(R"JS(
export default {
    Start() { this.keep = GameObject.Find("Victim"); },
    Update(dt) { if (this.keep) { const n = this.keep.name; } },
};
)JS", false);
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
        CHECK(w.Get("e1").AsString().find("isn't a key name") != std::string::npos);
        CHECK(w.Get("e2").AsString().find("call Translate on a Transform") != std::string::npos);
        CHECK(w.Get("e3").AsString().find("bad arguments to Transform.Translate") != std::string::npos);
        CHECK(w.Get("e4").AsString() == "undefined");
        CHECK(w.Get("e5").AsString().find("Transform has no settable property 'nope'") != std::string::npos);
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
        CHECK(w.Get("done").AsBool());
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
            g->AddComponent<ScriptComponent>("javascript")->LoadSource(
                "export default { Update(dt) { this.transform.Rotate(0, 90 * dt, 0); } };");
            text = SceneSerializer::Serialize(s);
        }
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        GameObject* g = loaded.Find("Spinner");
        CHECK(g != nullptr);
        if (g) {
            auto* sc = g->GetComponent<ScriptComponent>();
            CHECK(sc && sc->Language() == "javascript" && sc->VM() != nullptr);
            loaded.Start();
            for (int i = 0; i < 30; ++i) loaded.Update(kDt);
            CHECK(std::fabs(g->transform->Rotation().ToEuler().y) > 10.0f);
        }
    }

    // 13. Editor support: the starter script, lint warnings, member completion and Inspector settings.
    {
        const std::string starter = StarterScriptSource("javascript");
        CHECK(starter.find("Update(dt) {") != std::string::npos);
        JavaScriptVM vm;
        CHECK(vm.ValidateAll(starter).empty());
        std::vector<ScriptDiagnostic> w = JavaScriptVM::Lint(
            "export default class M {\n  Updte(dt) {\n    if (Input.GetKeyy(\"a\")) {}\n    const s = \"Input.Nope\"; // Time.Nope\n  }\n}\n");
        CHECK(w.size() == 2);
        if (w.size() == 2) {
            CHECK(w[0].line == 2 && w[0].severity == 1 && w[0].message.find("Did you mean Update?") != std::string::npos);
            CHECK(w[1].line == 3 && w[1].message.find("Input has no member 'GetKeyy'. Did you mean GetKey?") != std::string::npos);
            CHECK(w[1].column == 15);
        }
        CHECK(JavaScriptVM::Lint("function update(dt) {}\nfunction Start() {}\nconst v = new Vector3(1, 2, 3);").empty());
        auto has = [](const std::vector<std::string>& v, const char* x) { return std::find(v.begin(), v.end(), x) != v.end(); };
        std::vector<std::string> m = JavaScriptVM::CompletionMembers("Input");
        CHECK(has(m, "GetKey") && has(m, "GetAxis"));
        m = JavaScriptVM::CompletionMembers("this.transform");
        CHECK(has(m, "position") && has(m, "Translate") && has(m, "GetComponent"));
        m = JavaScriptVM::CompletionMembers("this.transform.position");
        CHECK(has(m, "x") && has(m, "magnitude") && has(m, "add"));
        m = JavaScriptVM::CompletionMembers("Math");
        CHECK(has(m, "floor") && has(m, "PI"));
        m = JavaScriptVM::CompletionMembers("Vector3");
        CHECK(has(m, "Distance") && has(m, "up"));
        CHECK(JavaScriptVM::CompletionMembers("nothingKnown").empty());
        CHECK(JavaScriptVM::CompletionDoc("Input.GetKey").find("Input.GetKey(key: string) -> boolean") == 0);
        CHECK(JavaScriptVM::CompletionDoc("Translate").find("Transform.Translate(") == 0);
        CHECK(JavaScriptVM::CompletionDoc("Update").find("every frame") != std::string::npos);
        CHECK(JavaScriptVM::CompletionDoc("class") == "JavaScript keyword");
        CHECK(JavaScriptVM::CompletionDoc("nothingHere").empty());
        CHECK(JavaScriptVM::CompletionDoc("GetKey").find("Input.GetKey(key: string) -> boolean") == 0);
        CHECK(std::string(ScriptLanguageForExtension(".js")) == "javascript");
        CHECK(std::string(ScriptLanguageForExtension("JS")) == "javascript");
        std::vector<std::string> words = JavaScriptVM::CompletionNames();
        CHECK(has(words, "function") && has(words, "Input.GetKey") && has(words, "Update") && has(words, "Math.floor"));
        std::vector<JavaScriptVM::Setting> st = JavaScriptVM::Settings(
            "export default class P {\n  speed = 5;\n  name = 'ship';\n  alive = true;\n  offset = new Vector3(1, 2.5, 0);\n"
            "  onHit = () => {};\n  Start() { console.log('not shown'); }\n}\nconsole.log('not shown either');\n");
        CHECK(st.size() == 4);
        if (st.size() == 4) {
            CHECK(st[0].name == "speed" && st[0].type == "number" && st[0].defaultText == "5");
            CHECK(st[1].name == "name" && st[1].defaultText == "\"ship\"");
            CHECK(st[2].name == "alive" && st[2].defaultText == "true");
            CHECK(st[3].name == "offset" && st[3].defaultText == "Vector3(1, 2.5, 0)");
        }
        CHECK(!LogHas("not shown"));
        CHECK(JavaScriptVM::Settings("this is not javascript").empty());
        CHECK(std::find(AvailableScriptLanguages().begin(), AvailableScriptLanguages().end(), "javascript") != AvailableScriptLanguages().end());
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
        Scene scene{"B"};
        GameObject* lamp = scene.CreateGameObject("Lamp");
        Light* light = lamp->AddComponent<Light>();
        lamp->AddComponent<Rigidbody3D>();
        auto* sc = lamp->AddComponent<ScriptComponent>("javascript");
        std::string err;
        g_errs.clear();
        CHECK(sc->LoadSource(R"JS(
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
)JS", &err));
        if (!err.empty()) std::cerr << "  load: " << err << "\n";
        scene.Start();
        for (const auto& e : g_errs) std::cerr << "  error: " << e << "\n";
        CHECK_NEAR(light->intensity, 2.5f, 1e-5f);
        CHECK_NEAR(sc->VM()->GetGlobal("got").AsFloat(), 2.5f, 1e-5f);
        CHECK(sc->VM()->GetGlobal("kind").AsString() == "object");
        CHECK(sc->VM()->GetGlobal("text").AsString().find("Light") != std::string::npos);
        CHECK(sc->VM()->GetGlobal("owner").AsString() == "Lamp");
        CHECK(light->type == Light::Type::Spot);
        CHECK(sc->VM()->GetGlobal("same").AsBool());
        CHECK(sc->VM()->GetGlobal("inv").AsFloat() > 0.0f);
        CHECK(sc->VM()->GetGlobal("missing").AsBool());
        CHECK(sc->VM()->GetGlobal("bad").AsString().find("intensity") != std::string::npos);
        CHECK(sc->VM()->GetGlobal("nomember").AsString().find("no settable property 'noSuchThing'") != std::string::npos);
        lamp->RemoveComponent(light);
        scene.Update(kDt);
        CHECK(sc->VM()->GetGlobal("gone").AsString().find("null") != std::string::npos);
    }

    // 16. The sandbox: no file, network, process or module access, no compiling code from strings.
    {
        World w("var open = ['require', 'process', 'fetch', 'XMLHttpRequest', 'WebSocket', 'importScripts', 'Deno', 'Bun', 'module', 'exports']\n"
                "  .filter(n => typeof globalThis[n] !== 'undefined').length;\n"
                "var noEval = false; try { eval('1'); } catch (e) { noEval = e instanceof EvalError; }\n"
                "var noFn = false; try { Function('return 1'); } catch (e) { noFn = e instanceof EvalError; }\n"
                "var noCtor = false; try { (() => {}).constructor('return 1'); } catch (e) { noCtor = e instanceof EvalError; }");
        CHECK(w.loaded);
        CHECK(w.Get("open").AsFloat() == 0.0f);
        CHECK(w.Get("noEval").AsBool());
        CHECK(w.Get("noFn").AsBool());
        CHECK(w.Get("noCtor").AsBool());
    }

    // 17. The generated layer's DENY_* rules hold for JavaScript too, through both binding tables.
    {
        Scene scene{"D"};
        GameObject* g = scene.CreateGameObject("Picker");
        g->AddComponent<UIFilePicker>();
        g->AddComponent<AnimStateMachine>();
        auto* sc = g->AddComponent<ScriptComponent>("javascript");
        std::string err;
        CHECK(sc->LoadSource(R"JS(
var rootOk = true, openOk = true, animOk = true, animErr = "", rmOk = false, rmErr = "";
function Start() {
    const fp = GetComponent("UIFilePicker");
    try { fp.SetRoot("/"); } catch (e) { rootOk = false; }
    try { fp.Open("passwd"); } catch (e) { openOk = false; }
    try { GetComponent("Animator").SetCharacterModel("/etc/passwd"); } catch (e) { animOk = false; animErr = String(e); }
    try { GetComponent("Animator").SetRootMotion(false); rmOk = true; } catch (e) { rmErr = String(e); }
}
)JS", &err));
        if (!err.empty()) std::cerr << "  load: " << err << "\n";
        scene.Start();
        CHECK(!sc->VM()->GetGlobal("rootOk").AsBool());
        CHECK(!sc->VM()->GetGlobal("openOk").AsBool());
        if (!sc->VM()->GetGlobal("rmOk").AsBool()) std::cerr << "  allowed call failed: " << sc->VM()->GetGlobal("rmErr").AsString() << "\n";
        CHECK(!sc->VM()->GetGlobal("animOk").AsBool());
        CHECK(sc->VM()->GetGlobal("animErr").AsString().find("SetCharacterModel") != std::string::npos);
        CHECK(sc->VM()->GetGlobal("rmOk").AsBool());
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
