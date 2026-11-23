// The shared script-function table (okay/Scripting/ScriptApiRegistry.hpp): every entry gives the same
// answer and does the same thing from OkayScript and from the typed language (okayscript-next / C#),
// because both now run the one C++ function. The test walks the whole table, so a new entry is checked
// as soon as it is added (it only needs a sample argument below if it brings a new parameter name).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scripting/NextScriptVM.hpp"
#include "okay/Scripting/ScriptApiRegistry.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;

static std::vector<std::string> g_errs, g_warns;
static const float kDt = 1.0f / 60.0f;
static const char* kSlot = "parity_slot";

static bool Logged(const std::vector<std::string>& v, const std::string& s) {
    return std::any_of(v.begin(), v.end(), [&](const std::string& l) { return l.find(s) != std::string::npos; });
}

// A sample argument for each parameter name the table uses, as text in each language.
struct Sample { std::string okay, typed; bool known = true; };
static Sample SampleFor(const ScriptApiParam& p) {
    auto str = [](const char* s) { std::string q = std::string("\"") + s + "\""; return Sample{q, q}; };
    if (p.name == "key") return str("space");
    if (p.name == "action") return str("Jump");
    if (p.name == "slot") return str(kSlot);
    if (p.name == "label") return str("Parity");
    if (p.name == "group") return str("Music");
    if (p.name == "player") return Sample{"0", "0"};
    if (p.name == "volume") return Sample{"0.25", "0.25f"};
    if (p.type == ScriptApiType::Vector3) return Sample{"vec3(1, 2, 3)", "new Vector3(1, 2, 3)"};
    return Sample{"", "", false};
}
static const char* TypedTypeName(ScriptApiType t) {
    switch (t) {
        case ScriptApiType::Bool: return "bool";
        case ScriptApiType::Int: return "int";
        case ScriptApiType::Float: return "float";
        case ScriptApiType::String: return "string";
        case ScriptApiType::Vector3: return "Vector3";
        default: return "";
    }
}

// What a run left behind, compared between the two languages.
struct Outcome {
    bool loaded = false;
    std::string error;
    vs::VsValue result;
    Vec3 velocity, spin;
    float musicVolume = 0.0f;
    bool slotExists = false;
    std::string slotLabel;
};

// Run one call of `e` with the first `nargs` parameters, in one language, in a fresh scene.
static Outcome Run(const ScriptApiEntry& e, std::size_t nargs, bool typed) {
    // The same starting state for both languages.
    SaveGame::Delete(kSlot);
    AudioMixer::groupVolume[(int)AudioGroup::Music] = 1.0f;
    Scene scene("Parity");
    GameObject* go = scene.CreateGameObject("Probe");
    auto* body = go->AddComponent<Rigidbody3D>();
    go->AddComponent<BoxCollider3D>();   // a shape, so a torque has an inertia to turn
    body->freezeRotation = false;
    if (e.name == "load_game" || e.name == "has_save" || e.name == "delete_save" || e.name == "save_label")
        SaveGame::Save(scene, kSlot, "Parity");
    if (e.name == "key_up" || e.name == "action_up") { Input::FeedKeys({' '}); Input::FeedKeys({}); }
    else { Input::FeedKeys({}); Input::FeedKeys({' '}); }

    std::string okayArgs, typedArgs;
    for (std::size_t i = 0; i < nargs; ++i) {
        const Sample s = SampleFor(e.params[i]);
        okayArgs += (i ? ", " : "") + s.okay;
        typedArgs += (i ? ", " : "") + s.typed;
    }
    const bool hasResult = e.ret != ScriptApiType::Void;
    std::string src;
    if (!typed) {
        const std::string call = e.name + "(" + okayArgs + ")";
        src = "var r = 0;\nfunction start() {\n    " + (hasResult ? "r = " + call : call) + ";\n}\n";
    } else {
        std::string call;
        if (e.kind == ScriptApiKind::StaticProperty) call = e.typedType + "." + e.typedName;
        else if (e.kind == ScriptApiKind::Method) call = "GetComponent<" + e.typedType + ">()." + e.typedName + "(" + typedArgs + ")";
        else call = e.typedType + "." + e.typedName + "(" + typedArgs + ")";
        src = "class T : Behaviour {\n";
        if (hasResult) src += std::string("    public ") + TypedTypeName(e.ret) + " r;\n";
        src += "    void Start() { " + (hasResult ? "r = " + call : call) + "; }\n}\n";
    }
    auto* sc = go->AddComponent<ScriptComponent>(typed ? NextScriptVM::kLanguage : "okayscript");
    Outcome o;
    o.loaded = sc->LoadSource(src, &o.error);
    scene.Start();
    if (hasResult && o.loaded) o.result = sc->VM()->GetGlobal("r");
    scene.Update(kDt);   // a physics step, so forces show in the velocity
    // load_game replaces the scene's objects during Update (from the save made above), so `body` may be gone: look the
    // probe up again instead of reading through the old pointer.
    GameObject* probe = scene.Find("Probe");
    body = probe ? probe->GetComponent<Rigidbody3D>() : nullptr;
    o.velocity = body ? body->velocity : Vec3{};
    o.spin = body ? body->angularVelocity : Vec3{};
    o.musicVolume = AudioMixer::groupVolume[(int)AudioGroup::Music];
    o.slotExists = SaveGame::Exists(kSlot);
    o.slotLabel = SaveGame::Info(kSlot).label;
    if (!o.loaded) std::printf("  %s (%s) did not load: %s\n%s", e.name.c_str(), typed ? "typed" : "OkayScript", o.error.c_str(), src.c_str());
    return o;
}

static bool Near(const Vec3& a, const Vec3& b) { return (a - b).Magnitude() < 1e-4f; }

static bool Same(const ScriptApiEntry& e, const Outcome& a, const Outcome& b) {
    bool same = Near(a.velocity, b.velocity) && Near(a.spin, b.spin) && std::fabs(a.musicVolume - b.musicVolume) < 1e-6f &&
                a.slotExists == b.slotExists && a.slotLabel == b.slotLabel;
    switch (e.ret) {
        case ScriptApiType::Bool:    same = same && a.result.AsBool() == b.result.AsBool(); break;
        case ScriptApiType::Int:
        case ScriptApiType::Float:   same = same && std::fabs(a.result.AsFloat() - b.result.AsFloat()) < 1e-4f; break;
        case ScriptApiType::String:  same = same && a.result.AsString() == b.result.AsString(); break;
        case ScriptApiType::Vector3: same = same && Near(a.result.AsVec3(), b.result.AsVec3()); break;
        default: break;
    }
    return same;
}

int main() {
    RUN_SUITE("script_api_parity");
    Log::sink = [](Log::Level l, const std::string& m) {
        if (l >= Log::Level::Error) g_errs.push_back(m);
        else if (l == Log::Level::Warning) g_warns.push_back(m);
    };

    // ---- Every shared entry: both languages, the same result and the same effect ----
    int entries = 0, calls = 0;
    for (const ScriptApiEntry& e : ScriptApis()) {
        if (e.typedType.empty()) continue;   // OkayScript-only (a module's own function)
        ++entries;
        for (const ScriptApiParam& p : e.params)
            if (!SampleFor(p).known) { std::printf("  no sample argument for '%s' of %s: add one to SampleFor\n", p.name.c_str(), e.name.c_str()); CHECK(false); }
        const std::size_t all = e.params.size();
        const std::size_t fewest = e.typedMinParams < 0 ? all : (std::size_t)e.typedMinParams;
        for (std::size_t n = fewest; n <= all; ++n) {
            ++calls;
            const Outcome okay = Run(e, n, false), typed = Run(e, n, true);
            CHECK(okay.loaded);
            CHECK(typed.loaded);
            const bool same = Same(e, okay, typed);
            if (!same)
                std::printf("  %s/%zu differs: OkayScript '%s' vs typed '%s'\n", e.name.c_str(), n,
                            okay.result.AsString().c_str(), typed.result.AsString().c_str());
            CHECK(same);
            // OkayScript's old save functions answered 1 / 0; they still do.
            if (e.boolAsNumber) CHECK(okay.result.IsNumber());
        }
    }
    std::printf("  %d shared entries, %d calls compared\n", entries, calls);
    CHECK(entries >= 20);

    // ---- The answers are the real ones, not just equal ----
    {
        const ScriptApiEntry* key = FindScriptApi("key");
        CHECK(key && Run(*key, 1, false).result.AsBool() && Run(*key, 1, true).result.AsBool());   // Space is held
        const ScriptApiEntry* torque = FindScriptApi("add_torque");
        CHECK(torque && Run(*torque, 1, false).spin.Magnitude() > 0.0f);   // #888: OkayScript had no add_torque
        const ScriptApiEntry* label = FindScriptApi("save_label");
        CHECK(label && Run(*label, 1, true).result.AsString() == "Parity");
        const ScriptApiEntry* vol = FindScriptApi("set_group_volume");
        CHECK(vol && std::fabs(Run(*vol, 2, false).musicVolume - 0.25f) < 1e-6f);
    }

    // ---- Every name is reachable in both languages, and nothing is bound twice ----
    {
        OkayScriptVM okay;
        const std::vector<std::string> okayNames = okay.BuiltinNames();
        NextScriptVM next;
        const std::vector<std::string> typedNames = next.BuiltinNames();
        auto has = [](const std::vector<std::string>& v, const std::string& n) { return std::find(v.begin(), v.end(), n) != v.end(); };
        for (const ScriptApiEntry& e : ScriptApis()) {
            CHECK(has(okayNames, e.name));
            for (const auto& a : e.aliases) CHECK(has(okayNames, a));
            if (!e.typedType.empty()) CHECK(has(typedNames, e.typedName));
        }
        CHECK(ScriptApiDuplicateCount() == 0);   // no second, hand-written copy in either language
        CHECK(!Logged(g_errs, "already has it"));
    }

    // ---- save_exists is has_save's other name (it used to be two different functions) ----
    {
        const ScriptApiEntry* a = FindScriptApi("save_exists");
        CHECK(a && a->name == "has_save");
    }

    // ---- OkayScript forms: three numbers are a vector; a left-out slot is "save1" ----
    {
        auto velocityAfter = [](const std::string& src) {
            Scene s("Forms");
            GameObject* go = s.CreateGameObject("B");
            auto* rb = go->AddComponent<Rigidbody3D>();
            go->AddComponent<ScriptComponent>("okayscript")->LoadSource(src);
            s.Start();
            s.Update(kDt);
            return rb->velocity;
        };
        CHECK(Near(velocityAfter("function start() { add_impulse(1, 2, 3); }"),
                   velocityAfter("function start() { add_impulse(vec3(1, 2, 3)); }")));
        SaveGame::Delete("save1");
        Scene s("Slot");
        GameObject* go = s.CreateGameObject("S");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("var ok = 0; var there = 0;\nfunction start() { ok = save_game(); there = has_save(\"save1\"); }\n"));
        s.Start();
        CHECK(sc->VM()->GetGlobal("ok").AsFloat() == 1.0f);
        CHECK(sc->VM()->GetGlobal("there").AsFloat() == 1.0f);
        SaveGame::Delete("save1");
    }

    // ---- Errors: the typed language stops with the message; OkayScript warns once and carries on ----
    {
        g_errs.clear(); g_warns.clear();
        Scene s("Errors");
        GameObject* a = s.CreateGameObject("NoBodyA");
        auto* okay = a->AddComponent<ScriptComponent>("okayscript");
        CHECK(okay->LoadSource("var after = 0;\nfunction update(dt) { add_force(0, 1, 0); after = after + 1; }\n"));
        GameObject* b = s.CreateGameObject("NoBodyB");
        auto* typed = b->AddComponent<ScriptComponent>(NextScriptVM::kLanguage);
        CHECK(typed->LoadSource("class T : Behaviour { void Start() { GetComponent<Rigidbody>().AddForce(Vector3.up); } }\n"));
        // (A typed script reaches the binding through GetComponent, which is null here; the binding's own
        // "has no Rigidbody" message is what a visual-script node with a Target shows: vs_acceptance checks it.)
        s.Start();
        for (int i = 0; i < 3; ++i) s.Update(kDt);
        CHECK(okay->VM()->GetGlobal("after").AsFloat() == 3.0f);   // kept running
        const auto warned = std::count_if(g_warns.begin(), g_warns.end(), [](const std::string& l) {
            return l.find("add_force") != std::string::npos && l.find("'NoBodyA' has no Rigidbody") != std::string::npos; });
        CHECK(warned == 1);   // once, not every frame
        CHECK(Logged(g_errs, "Script on 'NoBodyB'"));   // the typed script stopped with an error
    }

    SaveGame::Delete(kSlot);
    TEST_MAIN_RESULT();
}
