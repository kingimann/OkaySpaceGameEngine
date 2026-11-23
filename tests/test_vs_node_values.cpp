// Table-driven value checks for nodes that before had only an "it does something" test
// (test_vs_registry_effect / test_vs_node_effect prove a node changes *something*; this file
// proves it gives the *right* answer). Each row: a node, its text field, its input pins and the
// exact expected output. Written from each node's help text (editor/VisualScriptNodeHelp.hpp)
// and plain maths; no outside code.
#include "test_framework.hpp"
#include "test_sandbox.hpp"
#include "VisualScriptEditor.hpp"
#include "okay/Audio/AudioMixer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/LineRenderer.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIModal.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/VehicleController.hpp"
#include "okay/Components/VehicleSeat.hpp"
#include "okay/Core/SaveGame.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Render/Atmosphere.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace okay;

static std::vector<std::string> g_errs;
static void OnErr(const vs::VsRuntimeError& e) { g_errs.push_back(e.nodeType + ": " + e.message); }

// A value printed the same way the tables write it: numbers %.3f, bools true/false,
// vectors x,y,z, text as is, lists [a;b].
static std::string Fmt(const vs::VsValue& v) {
    char b[128];
    if (v.IsVec3()) { const Vec3 p = v.AsVec3(); std::snprintf(b, sizeof b, "%.3f,%.3f,%.3f", p.x, p.y, p.z); return b; }
    if (v.IsBool()) return v.AsBool() ? "true" : "false";
    if (v.IsString()) return v.AsString();
    if (v.IsArray()) {
        std::string s = "[";
        const auto a = v.AsArray();
        for (std::size_t i = 0; i < a->size(); ++i) s += (i ? ";" : "") + Fmt((*a)[i]);
        return s + "]";
    }
    if (v.IsNumber()) {
        std::snprintf(b, sizeof b, "%.3f", (double)v.AsFloat());
        std::string s = b;
        return s == "-0.000" ? "0.000" : s;
    }
    return "(none)";
}

static VisualScriptComponent* Load(GameObject* go, const std::string& text) {
    auto* vc = go->AddComponent<VisualScriptComponent>();
    std::string err;
    if (!vc->LoadFromText(text, &err)) { std::printf("  load failed: %s\n%s\n", err.c_str(), text.c_str()); CHECK(false); }
    return vc;
}

// The graph: OnStart -> SetVar out (= node 2's output `pin`). Inputs "v:x,y,z" become a Make Vec3 node.
static std::string ValueGraph(const char* type, const char* arg, const std::vector<std::string>& ins, int pin) {
    std::string g = "node 1 OnStart\nentry OnStart 1\nnode 2 " + std::string(type) + (arg && *arg ? std::string(" ") + arg : "") + "\n";
    int next = 10;
    for (int i = 0; i < (int)ins.size(); ++i) {
        const std::string& in = ins[(std::size_t)i];
        if (in.empty()) continue;   // leave the pin unwired (its default)
        if (in.rfind("v:", 0) == 0) {
            float x = 0, y = 0, z = 0;
            std::sscanf(in.c_str() + 2, "%f,%f,%f", &x, &y, &z);
            const std::string id = std::to_string(next++);
            g += "node " + id + " MakeVec3\ndefault " + id + " 0 " + std::to_string(x) + "\ndefault " + id + " 1 " + std::to_string(y) +
                 "\ndefault " + id + " 2 " + std::to_string(z) + "\ndata 2 " + std::to_string(i) + " " + id + " 0\n";
        } else {
            g += "default 2 " + std::to_string(i) + " " + in + "\n";
        }
    }
    g += "node 3 SetVar out\nexec 1 0 3\ndata 3 0 2 " + std::to_string(pin) + "\n";
    return g;
}

struct ValueCase {
    const char* type;
    const char* arg;
    std::vector<std::string> ins;
    const char* expect;
    int pin;
};

static int RunValues(const std::vector<ValueCase>& rows) {
    int failed = 0;
    for (const ValueCase& c : rows) {
        Scene s; s.physicsEnabled = false;
        GameObject* me = s.CreateGameObject("Me");
        auto* vc = Load(me, ValueGraph(c.type, c.arg, c.ins, c.pin));
        s.Start();
        s.Update(0.1f);
        const std::string got = Fmt(vc->GetVariable("out"));
        if (got != c.expect) {
            ++failed;
            std::string ins;
            for (const auto& i : c.ins) ins += (ins.empty() ? "" : ", ") + (i.empty() ? std::string("-") : i);
            std::printf("  %s(%s) [%s]: expected %s, got %s\n", c.type, ins.c_str(), c.arg ? c.arg : "", c.expect, got.c_str());
        }
        CHECK(got == c.expect);
    }
    return failed;
}

// ---- Pure value nodes: maths, logic, text, vectors -------------------------------------------
static void Values() {
    const std::vector<ValueCase> rows = {
        // Logic
        {"And", "", {"true", "true"}, "true", 0},   {"And", "", {"true", "false"}, "false", 0}, {"And", "", {"false", "false"}, "false", 0},
        {"Or", "", {"false", "true"}, "true", 0},   {"Or", "", {"false", "false"}, "false", 0},
        {"Xor", "", {"true", "false"}, "true", 0},  {"Xor", "", {"true", "true"}, "false", 0}, {"Xor", "", {"false", "false"}, "false", 0},
        {"Not", "", {"true"}, "false", 0},          {"Not", "", {"false"}, "true", 0},
        {"NotEquals", "", {"1", "2"}, "true", 0},   {"NotEquals", "", {"3", "3"}, "false", 0},
        {"NotEquals", "", {"\"a\"", "\"a\""}, "false", 0}, {"NotEquals", "", {"\"a\"", "\"b\""}, "true", 0},
        {"Approx", "", {"0.1", "0.1001"}, "true", 0}, {"Approx", "", {"1", "1.1"}, "false", 0},
        {"Chance", "1", {}, "true", 0},             {"Chance", "0", {}, "false", 0},
        {"HasVar", "nothere", {}, "false", 0},
        // Maths
        {"Min", "", {"3", "-2"}, "-2.000", 0},      {"Min", "", {"4", "9"}, "4.000", 0},
        {"Ceil", "", {"1.2"}, "2.000", 0},          {"Ceil", "", {"-1.2"}, "-1.000", 0},   {"Ceil", "", {"3"}, "3.000", 0},
        {"Frac", "", {"2.75"}, "0.750", 0},         {"Frac", "", {"-0.25"}, "0.750", 0},
        {"OneMinus", "", {"0.25"}, "0.750", 0},     {"OneMinus", "", {"2"}, "-1.000", 0},
        {"Saturate", "", {"1.5"}, "1.000", 0},      {"Saturate", "", {"-3"}, "0.000", 0},  {"Saturate", "", {"0.4"}, "0.400", 0},
        {"Deg2Rad", "", {"180"}, "3.142", 0},       {"Deg2Rad", "", {"90"}, "1.571", 0},
        {"Rad2Deg", "", {"3.14159265"}, "180.000", 0}, {"Rad2Deg", "", {"1"}, "57.296", 0},
        {"Asin", "", {"1"}, "1.571", 0},            {"Asin", "", {"0"}, "0.000", 0},       {"Asin", "", {"5"}, "1.571", 0},   // clamped, no NaN
        {"Acos", "", {"1"}, "0.000", 0},            {"Acos", "", {"-1"}, "3.142", 0},      {"Acos", "", {"-7"}, "3.142", 0},
        {"Sin", "", {"1.5707963"}, "1.000", 0},     {"Sin", "", {"0"}, "0.000", 0},
        {"Cos", "", {"0"}, "1.000", 0},             {"Cos", "", {"3.14159265"}, "-1.000", 0},
        {"Sqrt", "", {"16"}, "4.000", 0},           {"Sqrt", "", {"2"}, "1.414", 0},
        {"Average", "", {"2", "6"}, "4.000", 0},    {"Average", "", {"-1", "1"}, "0.000", 0},
        {"Distance2D", "", {"0", "0", "3", "4"}, "5.000", 0}, {"Distance2D", "", {"1", "1", "1", "1"}, "0.000", 0},
        {"Noise", "", {"0", "0"}, "0.000", 0},      // Perlin noise is 0 on whole-number grid points
        {"Noise", "", {"3", "7"}, "0.000", 0},
        {"RandomInt", "", {"4", "4"}, "4.000", 0},  // a one-number range has one answer
        {"RandomInt", "", {"9", "9"}, "9.000", 0},
        {"RandomRange", "", {"2", "2"}, "2.000", 0},
        // Text
        {"StartsWith", "", {"\"Hello World\"", "\"Hell\""}, "true", 0}, {"StartsWith", "", {"\"Hello\"", "\"lo\""}, "false", 0},
        {"StartsWith", "", {"\"Hi\"", "\"\""}, "true", 0},
        {"EndsWith", "", {"\"Hello World\"", "\"World\""}, "true", 0},  {"EndsWith", "", {"\"Hi\"", "\"Hello Hi!\""}, "false", 0},
        {"EndsWith", "", {"\"player.png\"", "\".png\""}, "true", 0},
        {"IndexOf", "", {"\"Hello\"", "\"l\""}, "2.000", 0},          {"IndexOf", "", {"\"Hello\"", "\"z\""}, "-1.000", 0},
        {"IndexOf", "", {"\"Hello\"", "\"H\""}, "0.000", 0},
        // Vectors
        {"Vec3Cross", "", {"v:1,0,0", "v:0,1,0"}, "0.000,0.000,1.000", 0},
        {"Vec3Cross", "", {"v:0,1,0", "v:1,0,0"}, "0.000,0.000,-1.000", 0},
        {"Vec3Lerp", "", {"v:0,0,0", "v:10,20,30", "0.5"}, "5.000,10.000,15.000", 0},
        {"Vec3Lerp", "", {"v:0,0,0", "v:10,20,30", "0"}, "0.000,0.000,0.000", 0},
        {"Vec3Lerp", "", {"v:0,0,0", "v:10,20,30", "1"}, "10.000,20.000,30.000", 0},
        {"Vec3Negate", "", {"v:1,-2,3"}, "-1.000,2.000,-3.000", 0},
        // An unrotated object faces the engine's forward / up / right.
        {"ForwardVector", "", {}, "0.000,0.000,1.000", 0},
        {"UpVector", "", {}, "0.000,1.000,0.000", 0},
        {"RightVector", "", {}, "1.000,0.000,0.000", 0},
        {"GetScale", "", {}, "1.000,1.000,1.000", 0},
        {"GetRotation", "", {}, "0.000,0.000,0.000", 0},
        {"GetY", "", {}, "0.000", 0},
        {"GetZ", "", {}, "0.000", 0},
        {"VelY", "", {}, "0.000", 0},   // no rigidbody: zero, no error
        {"VelZ", "", {}, "0.000", 0},
    };
    const int failed = RunValues(rows);
    std::printf("  value rows: %d, failed %d\n", (int)rows.size(), failed);
}

// ---- Nodes that act on this object: compare the scene after one run -------------------------
struct SceneCase {
    const char* name;
    std::string graph;                                       // nodes 2.. hang off OnStart (node 1)
    std::function<void(Scene&, GameObject*)> setup;
    std::function<bool(Scene&, GameObject*, VisualScriptComponent*)> check;
    int frames;
};

static std::string Chain(const std::string& body, int last) {
    std::string g = "node 1 OnStart\nentry OnStart 1\n" + body;
    for (int i = 1; i < last; ++i) g += "exec " + std::to_string(i) + " 0 " + std::to_string(i + 1) + "\n";
    return g;
}
static bool Near(float a, float b, float e = 0.01f) { return std::fabs(a - b) <= e; }
static bool Near(const Vec3& a, const Vec3& b, float e = 0.01f) { return Near(a.x, b.x, e) && Near(a.y, b.y, e) && Near(a.z, b.z, e); }

static void SceneNodes() {
    auto none = [](Scene&, GameObject*) {};
    std::vector<SceneCase> rows = {
        {"MoveBy adds the vector", Chain("node 2 MoveBy\nnode 10 MakeVec3\ndefault 10 0 1\ndefault 10 1 2\ndefault 10 2 3\ndata 2 0 10 0\n", 2),
         [](Scene&, GameObject* me) { me->transform->SetPosition({1, 1, 1}); },
         [](Scene&, GameObject* me, VisualScriptComponent*) { return Near(me->transform->Position(), {2, 3, 4}); }, 1},
        {"MoveBy twice adds twice", Chain("node 2 MoveBy\nnode 3 MoveBy\nnode 10 MakeVec3\ndefault 10 0 1\ndata 2 0 10 0\ndata 3 0 10 0\n", 3), none,
         [](Scene&, GameObject* me, VisualScriptComponent*) { return Near(me->transform->Position(), {2, 0, 0}); }, 1},
        {"SetRotation3 then GetRotation reads the same degrees",
         Chain("node 2 SetRotation3\ndefault 2 0 10\ndefault 2 1 20\ndefault 2 2 30\nnode 3 SetVar r\nnode 4 GetRotation\ndata 3 0 4 0\n", 3), none,
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return Near(vc->GetVariable("r").AsVec3(), {10, 20, 30}, 0.05f); }, 1},
        {"Rotate3 turns by degrees (90 about Y)", Chain("node 2 Rotate3\ndefault 2 1 90\nnode 3 SetVar f\nnode 4 ForwardVector\ndata 3 0 4 0\n", 3), none,
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return Near(vc->GetVariable("f").AsVec3(), {1, 0, 0}, 0.02f); }, 1},
        {"Rotate3 adds up", Chain("node 2 Rotate3\ndefault 2 1 45\nnode 3 Rotate3\ndefault 3 1 45\n", 3), none,
         [](Scene&, GameObject* me, VisualScriptComponent*) { return Near(me->transform->EulerAngles().y, 90, 0.05f); }, 1},
        {"SetRotation is absolute degrees about Z", Chain("node 2 SetRotation\ndefault 2 0 30\nnode 3 SetRotation\ndefault 3 0 30\n", 3), none,
         [](Scene&, GameObject* me, VisualScriptComponent*) { return Near(me->transform->EulerAngles().z, 30, 0.05f); }, 1},
        {"Get Y / Get Z read the position", Chain("node 2 SetVar y\nnode 3 GetY\ndata 2 0 3 0\nnode 4 SetVar z\nnode 5 GetZ\ndata 4 0 5 0\nexec 2 0 4\n", 2),
         [](Scene&, GameObject* me) { me->transform->SetPosition({1, 2, 3}); },
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return Near(vc->GetVariable("y").AsFloat(), 2) && Near(vc->GetVariable("z").AsFloat(), 3); }, 1},
        {"GetScale reads the scale", Chain("node 2 SetVar s\nnode 3 GetScale\ndata 2 0 3 0\n", 2),
         [](Scene&, GameObject* me) { me->transform->localScale = {2, 3, 4}; },
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return Near(vc->GetVariable("s").AsVec3(), {2, 3, 4}); }, 1},
        {"VelY / VelZ read a 3D rigidbody", Chain("node 2 SetVar y\nnode 3 VelY\ndata 2 0 3 0\nnode 4 SetVar z\nnode 5 VelZ\ndata 4 0 5 0\nexec 2 0 4\n", 2),
         [](Scene&, GameObject* me) { auto* rb = me->AddComponent<Rigidbody3D>(); rb->velocity = {1, 2, 3}; },
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return Near(vc->GetVariable("y").AsFloat(), 2) && Near(vc->GetVariable("z").AsFloat(), 3); }, 1},
        {"AddImpulse changes velocity by impulse / mass", Chain("node 2 AddImpulse\ndefault 2 0 0\ndefault 2 1 4\ndefault 2 2 0\n", 2),
         [](Scene&, GameObject* me) { auto* rb = me->AddComponent<Rigidbody3D>(); rb->mass = 2; rb->gravityScale = 0; },
         [](Scene&, GameObject* me, VisualScriptComponent*) { return Near(me->GetComponent<Rigidbody3D>()->velocity.y, 2, 0.05f); }, 0},
        {"AddForce with no rigidbody does nothing and no error", Chain("node 2 AddForce\ndefault 2 0 5\n", 2), none,
         [](Scene&, GameObject* me, VisualScriptComponent*) { return Near(me->transform->Position(), {0, 0, 0}) && g_errs.empty(); }, 1},
        {"SetVisible false hides mesh and sprite", Chain("node 2 SetVisible\ndefault 2 0 false\n", 2),
         [](Scene&, GameObject* me) { me->AddComponent<MeshRenderer>(); me->AddComponent<SpriteRenderer>(); },
         [](Scene&, GameObject* me, VisualScriptComponent*) { return !me->GetComponent<MeshRenderer>()->enabled && !me->GetComponent<SpriteRenderer>()->enabled; }, 1},
        {"SetVisible true shows again", Chain("node 2 SetVisible\ndefault 2 0 false\nnode 3 SetVisible\ndefault 3 0 true\n", 3),
         [](Scene&, GameObject* me) { me->AddComponent<MeshRenderer>(); },
         [](Scene&, GameObject* me, VisualScriptComponent*) { return me->GetComponent<MeshRenderer>()->enabled; }, 1},
        {"HideUI / ShowUI switch the named object off and on", Chain("node 2 HideUI Panel\n", 2),
         [](Scene& s, GameObject*) { s.CreateGameObject("Panel"); },
         [](Scene& s, GameObject*, VisualScriptComponent*) { return !s.Find("Panel")->active; }, 1},
        {"ShowUI turns a hidden object on", Chain("node 2 ShowUI Panel\n", 2),
         [](Scene& s, GameObject*) { s.CreateGameObject("Panel")->active = false; },
         [](Scene& s, GameObject*, VisualScriptComponent*) { return s.Find("Panel")->active; }, 1},
        {"ShowUI on a missing name does not crash", Chain("node 2 ShowUI NoSuchPanel\n", 2), none,
         [](Scene&, GameObject*, VisualScriptComponent*) { return true; }, 1},
        {"SetTimeScale then TimeScale reads it back", Chain("node 2 SetTimeScale\ndefault 2 0 0.5\nnode 3 SetVar t\nnode 4 TimeScale\ndata 3 0 4 0\n", 3), none,
         [](Scene&, GameObject*, VisualScriptComponent* vc) { const bool ok = Near(vc->GetVariable("t").AsFloat(), 0.5f); Time::SetTimeScale(1); return ok; }, 0},
        {"SetTimeScale below zero clamps to 0", Chain("node 2 SetTimeScale\ndefault 2 0 -3\nnode 3 SetVar t\nnode 4 TimeScale\ndata 3 0 4 0\n", 3), none,
         [](Scene&, GameObject*, VisualScriptComponent* vc) { const bool ok = Near(vc->GetVariable("t").AsFloat(), 0); Time::SetTimeScale(1); return ok; }, 0},
        {"SaveValue then LoadValue (number)", Chain("node 2 SaveValue vsval_n\ndefault 2 0 42\nnode 3 SetVar v\nnode 4 LoadValue vsval_n\ndefault 4 0 0\ndata 3 0 4 0\n", 3), none,
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return Near(vc->GetVariable("v").AsFloat(), 42); }, 0},
        {"SaveValue then LoadValue (text)", Chain("node 2 SaveValue vsval_t\ndefault 2 0 \"Bob\"\nnode 3 SetVar v\nnode 4 LoadValue vsval_t\ndefault 4 0 \"none\"\ndata 3 0 4 0\n", 3), none,
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return vc->GetVariable("v").AsString() == "Bob"; }, 0},
        {"LoadValue of a missing key gives the default", Chain("node 2 SetVar v\nnode 3 LoadValue vsval_missing\ndefault 3 0 7\ndata 2 0 3 0\n", 2), none,
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return Near(vc->GetVariable("v").AsFloat(), 7); }, 0},
        {"SetStoryVar then GetStoryVar", Chain("node 2 SetStoryVar met\ndefault 2 0 3\nnode 3 SetVar v\nnode 4 GetStoryVar met\ndata 3 0 4 0\n", 3), none,
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return Near(vc->GetVariable("v").AsFloat(), 3); }, 0},
        {"SetGlobalVar then GetGlobalVar", Chain("node 2 SetGlobalVar gv\ndefault 2 0 9\nnode 3 SetVar v\nnode 4 GetGlobalVar gv\ndata 3 0 4 0\n", 3), none,
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return Near(vc->GetVariable("v").AsFloat(), 9) && Near(vs::GlobalVariables()["gv"].AsFloat(), 9); }, 0},
        {"HasVar sees a variable after Set Var", Chain("node 2 SetVar hp\ndefault 2 0 5\nnode 3 SetVar h\nnode 4 HasVar hp\ndata 3 0 4 0\n", 3), none,
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return vc->GetVariable("h").AsBool(); }, 0},
        {"QuestState of an unknown quest is inactive", Chain("node 2 SetVar q\nnode 3 QuestState q1\ndata 2 0 3 0\n", 2), none,
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return vc->GetVariable("q").AsString() == "inactive"; }, 0},
        {"SetFog on with start and end", Chain("node 2 SetFog\ndefault 2 0 true\ndefault 2 1 5\ndefault 2 2 50\n", 2), none,
         [](Scene& s, GameObject*, VisualScriptComponent*) { return s.renderSettings.fog && Near(s.renderSettings.fogStart, 5) && Near(s.renderSettings.fogEnd, 50); }, 0},
        {"SetFog off", Chain("node 2 SetFog\ndefault 2 0 false\n", 2), [](Scene& s, GameObject*) { s.renderSettings.fog = true; },
         [](Scene& s, GameObject*, VisualScriptComponent*) { return !s.renderSettings.fog; }, 0},
        {"SetWeather rain at 0.5", Chain("node 2 SetWeather rain\ndefault 2 0 0.5\n", 2), none,
         [](Scene& s, GameObject*, VisualScriptComponent*) { return (int)s.renderSettings.weather.preset == WeatherPresetFromName("rain") && Near(s.renderSettings.weather.intensity, 0.5f); }, 0},
        {"SetPostEffect exposure", Chain("node 2 SetPostEffect exposure\ndefault 2 0 2\n", 2), none,
         [](Scene& s, GameObject*, VisualScriptComponent*) { return Near(s.renderSettings.post.exposure, 2); }, 0},
        {"FrameCount counts up", Chain("node 2 SetVar a\nnode 3 FrameCount\ndata 2 0 3 0\n", 2), none,
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return vc->GetVariable("a").IsNumber(); }, 0},
    };
    int failed = 0;
    for (SceneCase& c : rows) {
        g_errs.clear();
        Scene s; s.physicsEnabled = false;
        GameObject* me = s.CreateGameObject("Me");
        c.setup(s, me);
        auto* vc = Load(me, c.graph);
        s.Start();
        for (int f = 0; f < c.frames; ++f) s.Update(0.1f);
        const bool ok = c.check(s, me, vc);
        if (!ok) {
            ++failed;
            std::printf("  FAIL: %s\n", c.name);
            for (const auto& e : g_errs) std::printf("    error: %s\n", e.c_str());
        }
        CHECK(ok);
    }
    std::printf("  scene rows: %d, failed %d\n", (int)rows.size(), failed);
}


// ---- Flow gates driven by On Update: the count must not depend on the frame rate --------------
// Each row: the gate node (with its text field and pins), seconds to run; expected passes at
// 10, 30 and 120 frames per second.
static void FlowGates() {
    struct Row { const char* node; const char* extra; float seconds; int expect; };
    const Row rows[] = {
        {"Timer 0.5", "", 3.25f, 6},
        {"Cooldown 1", "", 3.5f, 4},          // passes at once, then about 1 s, 2 s and 3 s later
        {"DoN 3", "", 2.0f, 3},
        {"Once", "", 2.0f, 1},
    };
    for (const Row& r : rows) {
        for (int fps : {10, 30, 120}) {
            Time::ResetForTesting();
            Scene s; s.physicsEnabled = false;
            GameObject* me = s.CreateGameObject("Me");
            auto* vc = Load(me, std::string("node 1 OnUpdate\nentry OnUpdate 1\nnode 2 ") + r.node + "\n" + r.extra +
                                    "node 3 AddVar n\ndefault 3 0 1\nexec 1 0 2\nexec 2 0 3\n");
            s.Start();
            const float dt = 1.0f / (float)fps;
            const int frames = (int)std::lround(r.seconds * (float)fps);
            for (int f = 0; f < frames; ++f) { Time::Step(dt); s.Update(dt); }
            const int got = (int)std::lround(vc->GetVariable("n").AsFloat());
            if (got != r.expect) std::printf("  %s at %d fps for %.2f s: expected %d passes, got %d\n", r.node, fps, r.seconds, r.expect, got);
            CHECK(got == r.expect);
        }
    }
}


// ---- Maps: build one with Set, then read it back ----------------------------------------------
static void Maps() {
    const std::string g =
        "node 1 OnStart\nentry OnStart 1\nnode 2 NewMap\n"
        "node 3 SetVar m\ndata 3 0 2 0\n"
        "node 4 GetVar m\n"
        "node 5 MapSet\ndata 5 0 4 0\ndefault 5 1 \"b\"\ndefault 5 2 2\n"
        "node 6 MapSet\ndata 6 0 4 0\ndefault 6 1 \"a\"\ndefault 6 2 1\n"
        "node 7 SetVar keys\nnode 8 MapKeys\ndata 8 0 4 0\ndata 7 0 8 0\n"
        "node 9 SetVar vals\nnode 10 MapValues\ndata 10 0 4 0\ndata 9 0 10 0\n"
        "node 11 SetVar got\nnode 12 MapGet\ndata 12 0 4 0\ndefault 12 1 \"b\"\ndata 11 0 12 0\n"
        "node 13 SetVar found\ndata 13 0 12 1\n"
        "node 14 SetVar hasA\nnode 15 MapHas\ndata 15 0 4 0\ndefault 15 1 \"a\"\ndata 14 0 15 0\n"
        "node 16 MapRemove\ndata 16 0 4 0\ndefault 16 1 \"a\"\n"
        "node 17 SetVar hasA2\nnode 18 MapHas\ndata 18 0 4 0\ndefault 18 1 \"a\"\ndata 17 0 18 0\n"
        "node 19 SetVar missing\nnode 20 MapGet\ndata 20 0 4 0\ndefault 20 1 \"zz\"\ndata 19 0 20 1\n"
        "exec 1 0 3\nexec 3 0 5\nexec 5 0 6\nexec 6 0 7\nexec 7 0 9\nexec 9 0 11\nexec 11 0 13\nexec 13 0 14\nexec 14 0 16\nexec 16 0 17\nexec 17 0 19\n";
    Scene s; s.physicsEnabled = false;
    GameObject* me = s.CreateGameObject("Me");
    auto* vc = Load(me, g);
    s.Start();
    s.Update(0.1f);
    const std::string keys = Fmt(vc->GetVariable("keys")), vals = Fmt(vc->GetVariable("vals")), got = Fmt(vc->GetVariable("got"));
    std::printf("  map: keys %s values %s get(b) %s found %s has(a) %s after remove %s missing-found %s\n", keys.c_str(), vals.c_str(), got.c_str(),
                Fmt(vc->GetVariable("found")).c_str(), Fmt(vc->GetVariable("hasA")).c_str(), Fmt(vc->GetVariable("hasA2")).c_str(), Fmt(vc->GetVariable("missing")).c_str());
    CHECK(keys == "[a;b]");   // sorted, as the help says
    CHECK(vals == "[1.000;2.000]");
    CHECK(got == "2.000");
    CHECK(vc->GetVariable("found").AsBool());
    CHECK(vc->GetVariable("hasA").AsBool());
    CHECK(!vc->GetVariable("hasA2").AsBool());
    CHECK(!vc->GetVariable("missing").AsBool());
}


// ---- Friendly blocks (tools/gameplay_nodes_batch7.py, On Damaged, On Button Clicked, Damage Type) ----------
// Each row works the answer out by hand: the camera looks along its -Z, a 90 degree turn about Y takes -Z to -X,
// a sphere's points all sit Radius from its centre, and so on.
struct FriendlyCase {
    const char* name;
    std::string graph;                                       // node 1 is OnStart unless the graph says otherwise
    std::function<void(Scene&, GameObject*)> setup;
    std::function<bool(Scene&, GameObject*, VisualScriptComponent*)> check;
    int frames;
    bool expectError;
};

static void FriendlyNodes() {
    auto none = [](Scene&, GameObject*) {};
    auto camAt = [](float yaw, float pitch) {
        return [yaw, pitch](Scene& s, GameObject*) {
            GameObject* c = s.CreateGameObject("Cam");
            c->AddComponent<Camera>();
            c->transform->SetRotation(Quat::AngleAxis(yaw, Vec3(0, 1, 0)) * Quat::AngleAxis(pitch, Vec3(1, 0, 0)));
        };
    };
    auto read = [](const char* type) {   // OnStart -> SetVar out = <type>
        return Chain(std::string("node 2 SetVar out\nnode 3 ") + type + "\ndata 2 0 3 0\n", 2);
    };
    auto outVec = [](Vec3 want) {
        return [want](Scene&, GameObject*, VisualScriptComponent* vc) { return Near(vc->GetVariable("out").AsVec3(), want, 0.01f); };
    };
    auto car = [](bool driverSeat) {
        return [driverSeat](Scene& s, GameObject*) {
            GameObject* c = s.CreateGameObject("Car");
            c->AddComponent<VehicleController>();
            GameObject* seat = s.CreateGameObject("Seat");
            seat->transform->SetParent(c->transform);
            seat->AddComponent<VehicleSeat>()->driver = driverSeat;
        };
    };
    std::vector<FriendlyCase> rows = {
        // Camera directions: flat and one unit long whatever the pitch.
        {"Camera Forward, camera not turned: -Z", read("GlGetCameraForward"), camAt(0, 0), outVec({0, 0, -1}), 1, false},
        {"Camera Right, camera not turned: +X", read("GlGetCameraRight"), camAt(0, 0), outVec({1, 0, 0}), 1, false},
        {"Camera Forward looking 45 down stays flat", read("GlGetCameraForward"), camAt(0, -45), outVec({0, 0, -1}), 1, false},
        {"Camera Forward turned 90 about Y: -X", read("GlGetCameraForward"), camAt(90, 0), outVec({-1, 0, 0}), 1, false},
        {"Camera Right turned 180 about Y: -X", read("GlGetCameraRight"), camAt(180, -20), outVec({-1, 0, 0}), 1, false},
        {"Camera Forward with no camera is an error", read("GlGetCameraForward"), none, outVec({0, 0, 1}), 1, true},
        // Character speed: run keeps its ratio to walk.
        {"Set Move Speed 6 on walk 4 / run 8: run 12", Chain("node 2 GlSetMoveSpeed\ndefault 2 0 6\n", 2),
         [](Scene&, GameObject* me) { auto* c = me->AddComponent<FirstPersonController>(); c->walkSpeed = 4; c->runSpeed = 8; },
         [](Scene&, GameObject* me, VisualScriptComponent*) { auto* c = me->GetComponent<FirstPersonController>(); return Near(c->walkSpeed, 6) && Near(c->runSpeed, 12); }, 0, false},
        {"Set Move Speed below 0 stops at 0", Chain("node 2 GlSetMoveSpeed\ndefault 2 0 -3\n", 2),
         [](Scene&, GameObject* me) { me->AddComponent<FirstPersonController>(); },
         [](Scene&, GameObject* me, VisualScriptComponent*) { auto* c = me->GetComponent<FirstPersonController>(); return Near(c->walkSpeed, 0) && Near(c->runSpeed, 0); }, 0, false},
        {"Get Move Speed reads the walk speed", read("GlGetMoveSpeed"),
         [](Scene&, GameObject* me) { me->AddComponent<ThirdPersonController>()->walkSpeed = 2.5f; },
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return Near(vc->GetVariable("out").AsFloat(), 2.5f); }, 0, false},
        {"Set Move Speed with no controller is an error", Chain("node 2 GlSetMoveSpeed\n", 2), none,
         [](Scene&, GameObject*, VisualScriptComponent*) { return true; }, 0, true},
        {"Set Sprinting on, then off", Chain("node 2 GlSetSprinting\ndefault 2 0 true\nnode 3 GlSetSprinting\ndefault 3 0 false\n", 3),
         [](Scene&, GameObject* me) { me->AddComponent<FirstPersonController>(); },
         [](Scene&, GameObject* me, VisualScriptComponent*) { return !me->GetComponent<FirstPersonController>()->sprintByScript; }, 0, false},
        {"Set Sprinting on holds sprint", Chain("node 2 GlSetSprinting\ndefault 2 0 true\n", 2),
         [](Scene&, GameObject* me) { me->AddComponent<FirstPersonController>(); },
         [](Scene&, GameObject* me, VisualScriptComponent*) { return me->GetComponent<FirstPersonController>()->sprintByScript; }, 0, false},
        // Vehicles.
        {"Enter Vehicle takes the driver seat; Is Driving is true",
         Chain("node 2 GlEnterVehicle\ndefault 2 0 Car\nnode 3 SetVar d\nnode 4 GlIsDriving\ndata 3 0 4 0\n", 3), car(true),
         [](Scene& s, GameObject* me, VisualScriptComponent* vc) { return s.Find("Seat")->GetComponent<VehicleSeat>()->Occupant() == me && vc->GetVariable("d").AsBool(); }, 0, false},
        {"Exit Vehicle gets out; Is Driving is false",
         Chain("node 2 GlEnterVehicle\ndefault 2 0 Car\nnode 3 GlExitVehicle\nnode 4 SetVar d\nnode 5 GlIsDriving\ndata 4 0 5 0\n", 4), car(true),
         [](Scene& s, GameObject*, VisualScriptComponent* vc) { return !s.Find("Seat")->GetComponent<VehicleSeat>()->Occupant() && !vc->GetVariable("d").AsBool(); }, 0, false},
        {"Enter Vehicle with only a passenger seat sits there and is not driving",
         Chain("node 2 GlEnterVehicle\ndefault 2 0 Car\nnode 3 SetVar d\nnode 4 GlIsDriving\ndata 3 0 4 0\n", 3), car(false),
         [](Scene& s, GameObject* me, VisualScriptComponent* vc) { return s.Find("Seat")->GetComponent<VehicleSeat>()->Occupant() == me && !vc->GetVariable("d").AsBool(); }, 0, false},
        {"Enter Vehicle with the seat taken runs Seats Full",
         "node 1 OnStart\nentry OnStart 1\nnode 2 GlEnterVehicle\ndefault 2 0 Car\nnode 3 SetVar r\ndefault 3 0 \"in\"\nnode 4 SetVar r\ndefault 4 0 \"full\"\nexec 1 0 2\nexec 2 0 3\nexec 2 1 4\n",
         [car](Scene& s, GameObject* me) { car(true)(s, me); GameObject* other = s.CreateGameObject("Other"); s.Find("Seat")->GetComponent<VehicleSeat>()->Enter(other); },
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return vc->GetVariable("r").AsString() == "full"; }, 0, false},
        // UI.
        {"Get Slider Value", read("GlGetSliderValue"), [](Scene&, GameObject* me) { me->AddComponent<UISlider>()->value = 0.25f; },
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return Near(vc->GetVariable("out").AsFloat(), 0.25f); }, 0, false},
        {"Open Menu opens a UI Modal", Chain("node 2 GlOpenMenu\ndefault 2 0 Box\n", 2),
         [](Scene& s, GameObject*) { GameObject* b = s.CreateGameObject("Box"); b->AddComponent<UIModal>(); b->active = false; },
         [](Scene& s, GameObject*, VisualScriptComponent*) { return s.Find("Box") && s.Find("Box")->GetComponent<UIModal>()->IsOpen(); }, 0, false},
        {"Close Menu closes a UI Modal", Chain("node 2 GlCloseMenu\ndefault 2 0 Box\n", 2),
         [](Scene& s, GameObject*) { s.CreateGameObject("Box")->AddComponent<UIModal>(); },
         [](Scene& s, GameObject*, VisualScriptComponent*) { for (const auto& up : s.Objects()) if (up && up->name == "Box") return !up->active; return false; }, 0, false},
        // Input.
        {"Rebind Action Jump to key:j", Chain("node 2 GlRebindAction\ndefault 2 0 \"Jump\"\ndefault 2 1 \"key:j\"\n", 2), none,
         [](Scene&, GameObject*, VisualScriptComponent*) {
             auto* a = InputActions::Find("Jump");
             bool has = false;
             if (a) for (const auto& b : a->bindings) if (b.kind == InputBinding::Kind::Key && b.code == 'j') has = true;
             InputActions::Reset();
             return has; }, 0, false},
        {"Rebind Action with a binding it does not understand runs Not Understood",
         "node 1 OnStart\nentry OnStart 1\nnode 2 GlRebindAction\ndefault 2 0 \"Jump\"\ndefault 2 1 \"banana\"\nnode 3 SetVar r\ndefault 3 0 \"ok\"\nnode 4 SetVar r\ndefault 4 0 \"bad\"\nexec 1 0 2\nexec 2 0 3\nexec 2 1 4\n", none,
         [](Scene&, GameObject*, VisualScriptComponent* vc) { InputActions::Reset(); return vc->GetVariable("r").AsString() == "bad"; }, 0, false},
        {"Rumble Controller asks for strength on both motors", Chain("node 2 GlRumble\ndefault 2 0 0.8\ndefault 2 1 0.5\n", 2),
         [](Scene&, GameObject*) { float a, b, c; Input::TakeRumble(a, b, c); },
         [](Scene&, GameObject*, VisualScriptComponent*) { float lo = 0, hi = 0, t = 0; return Input::TakeRumble(lo, hi, t) && Near(lo, 0.8f) && Near(hi, 0.8f) && Near(t, 0.5f); }, 0, false},
        {"Rumble Controller strength above 1 is 1", Chain("node 2 GlRumble\ndefault 2 0 3\ndefault 2 1 0.2\n", 2),
         [](Scene&, GameObject*) { float a, b, c; Input::TakeRumble(a, b, c); },
         [](Scene&, GameObject*, VisualScriptComponent*) { float lo = 0, hi = 0, t = 0; return Input::TakeRumble(lo, hi, t) && Near(lo, 1.0f) && Near(hi, 1.0f); }, 0, false},
        // Audio buses.
        {"Set Bus Volume Music 0.3", Chain("node 2 GlSetBusVolume\ndefault 2 0 \"music\"\ndefault 2 1 0.3\n", 2), none,
         [](Scene&, GameObject*, VisualScriptComponent*) { AudioGroup gr; AudioMixer::GroupFromName("Music", gr); const bool ok = Near(AudioMixer::groupVolume[(int)gr], 0.3f); AudioMixer::groupVolume[(int)gr] = 1; return ok; }, 0, false},
        {"Set Bus Volume above 1 is 1", Chain("node 2 GlSetBusVolume\ndefault 2 0 \"Voice\"\ndefault 2 1 1.5\n", 2),
         [](Scene&, GameObject*) { AudioGroup gr; AudioMixer::GroupFromName("Voice", gr); AudioMixer::groupVolume[(int)gr] = 0.2f; },
         [](Scene&, GameObject*, VisualScriptComponent*) { AudioGroup gr; AudioMixer::GroupFromName("Voice", gr); const bool ok = Near(AudioMixer::groupVolume[(int)gr], 1.0f); AudioMixer::groupVolume[(int)gr] = 1; return ok; }, 0, false},
        {"Set Bus Volume on an unknown bus is an error", Chain("node 2 GlSetBusVolume\ndefault 2 0 \"Drums\"\ndefault 2 1 0.5\n", 2), none,
         [](Scene&, GameObject*, VisualScriptComponent*) { return true; }, 0, true},
        {"Mute Bus UI silences it", Chain("node 2 GlMuteBus\ndefault 2 0 \"UI\"\ndefault 2 1 true\n", 2),
         none, [](Scene&, GameObject*, VisualScriptComponent*) { AudioGroup gr; AudioMixer::GroupFromName("UI", gr); const bool ok = AudioMixer::groupMuted[(int)gr] && AudioMixer::GroupGain(gr) == 0.0f; AudioMixer::groupMuted[(int)gr] = false; return ok; }, 0, false},
        // Colliders.
        {"Set Collider Size: sphere radius is half the width", Chain("node 2 GlSetColliderSize\ndefault 2 0 3\n", 2),
         [](Scene&, GameObject* me) { me->AddComponent<SphereCollider3D>(); },
         [](Scene&, GameObject* me, VisualScriptComponent*) { return Near(me->GetComponent<SphereCollider3D>()->radius, 1.5f); }, 0, false},
        {"Set Collider Size: capsule radius and height", Chain("node 2 GlSetColliderSize\ndefault 2 0 1\ndefault 2 1 4\n", 2),
         [](Scene&, GameObject* me) { me->AddComponent<CapsuleCollider3D>(); },
         [](Scene&, GameObject* me, VisualScriptComponent*) { auto* c = me->GetComponent<CapsuleCollider3D>(); return Near(c->radius, 0.5f) && Near(c->height, 4); }, 0, false},
        {"Set Collider Size: 2D box width and height", Chain("node 2 GlSetColliderSize\ndefault 2 0 2\ndefault 2 1 5\n", 2),
         [](Scene&, GameObject* me) { me->AddComponent<BoxCollider2D>(); },
         [](Scene&, GameObject* me, VisualScriptComponent*) { auto* c = me->GetComponent<BoxCollider2D>(); return Near(c->size.x, 2) && Near(c->size.y, 5); }, 0, false},
        {"Set Collider Size with no collider is an error", Chain("node 2 GlSetColliderSize\n", 2), none,
         [](Scene&, GameObject*, VisualScriptComponent*) { return true; }, 0, true},
        {"Disable Collider switches off 3D and 2D colliders", Chain("node 2 GlDisableCollider\n", 2),
         [](Scene&, GameObject* me) { me->AddComponent<BoxCollider3D>(); me->AddComponent<CircleCollider2D>(); },
         [](Scene&, GameObject* me, VisualScriptComponent*) { return !me->GetComponent<BoxCollider3D>()->enabled && !me->GetComponent<CircleCollider2D>()->enabled; }, 0, false},
        {"Is Grounded on a floor", read("GlIsGrounded"),
         [](Scene& s, GameObject* me) { me->AddComponent<BoxCollider3D>(); GameObject* f = s.CreateGameObject("Floor"); f->transform->SetPosition({0, -1, 0}); f->AddComponent<BoxCollider3D>()->size = {10, 1, 10}; },
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return vc->GetVariable("out").AsBool(); }, 0, false},
        {"Is Grounded one unit above the floor is false", read("GlIsGrounded"),
         [](Scene& s, GameObject* me) { me->transform->SetPosition({0, 1, 0}); me->AddComponent<BoxCollider3D>(); GameObject* f = s.CreateGameObject("Floor"); f->transform->SetPosition({0, -1, 0}); f->AddComponent<BoxCollider3D>()->size = {10, 1, 10}; },
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return !vc->GetVariable("out").AsBool(); }, 0, false},
        // Save slots.
        {"Save Game then Has Save", Chain("node 2 GlSaveGame\ndefault 2 0 \"slotA\"\nnode 3 SetVar h\nnode 4 GlHasSave\ndefault 4 0 \"slotA\"\ndata 3 0 4 0\n", 3), none,
         [](Scene&, GameObject*, VisualScriptComponent* vc) { const bool ok = vc->GetVariable("h").AsBool() && SaveGame::Exists("slotA"); SaveGame::Delete("slotA"); return ok; }, 0, false},
        {"Has Save of an empty slot is false", read("GlHasSave"), none,
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return !vc->GetVariable("out").AsBool(); }, 0, false},
        {"Load Game of an empty slot runs Nothing Saved",
         "node 1 OnStart\nentry OnStart 1\nnode 2 GlLoadGame\ndefault 2 0 \"empty9\"\nnode 3 SetVar r\ndefault 3 0 \"loading\"\nnode 4 SetVar r\ndefault 4 0 \"none\"\nexec 1 0 2\nexec 2 0 3\nexec 2 1 4\n", none,
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return vc->GetVariable("r").AsString() == "none"; }, 0, false},
        {"Load Game of a saved slot runs Loading",
         "node 1 OnStart\nentry OnStart 1\nnode 2 GlLoadGame\ndefault 2 0 \"slotB\"\nnode 3 SetVar r\ndefault 3 0 \"loading\"\nnode 4 SetVar r\ndefault 4 0 \"none\"\nexec 1 0 2\nexec 2 0 3\nexec 2 1 4\n",
         [](Scene& s, GameObject*) { CHECK(SaveGame::Save(s, "slotB", "B")); },
         [](Scene&, GameObject*, VisualScriptComponent* vc) { const bool ok = vc->GetVariable("r").AsString() == "loading"; SaveGame::Delete("slotB"); return ok; }, 0, false},
        // Lights.
        {"Set Light Color", Chain("node 2 GlSetLightColor\ndefault 2 0 0.1\ndefault 2 1 0.2\ndefault 2 2 0.3\n", 2),
         [](Scene&, GameObject* me) { me->AddComponent<Light>(); },
         [](Scene&, GameObject* me, VisualScriptComponent*) { Color c = me->GetComponent<Light>()->color; return Near(c.r, 0.1f) && Near(c.g, 0.2f) && Near(c.b, 0.3f); }, 0, false},
        {"Set Light Intensity below 0 is 0", Chain("node 2 GlSetLightIntensity\ndefault 2 0 -4\n", 2),
         [](Scene&, GameObject* me) { me->AddComponent<Light>(); },
         [](Scene&, GameObject* me, VisualScriptComponent*) { return Near(me->GetComponent<Light>()->intensity, 0); }, 0, false},
        {"Set Light Intensity with no light is an error", Chain("node 2 GlSetLightIntensity\n", 2), none,
         [](Scene&, GameObject*, VisualScriptComponent*) { return true; }, 0, true},
        // Debug drawing.
        {"Draw Debug Sphere: every point is Radius from the centre",
         Chain("node 2 GlDrawDebugSphere\nnode 10 MakeVec3\ndefault 10 0 1\ndefault 10 1 2\ndefault 10 2 3\ndata 2 0 10 0\ndefault 2 1 1.5\n", 2), none,
         [](Scene&, GameObject* me, VisualScriptComponent*) {
             auto* l = me->GetComponent<LineRenderer>();
             if (!l || l->positions.size() < 60) return false;
             for (const Vec3& p : l->positions) if (!Near((p - Vec3{1, 2, 3}).Magnitude(), 1.5f, 0.001f)) return false;
             return true; }, 0, false},
        {"Draw Debug Box: every point is a corner",
         Chain("node 2 GlDrawDebugBox\nnode 10 MakeVec3\ndefault 10 0 2\ndefault 10 1 4\ndefault 10 2 6\ndata 2 1 10 0\n", 2), none,
         [](Scene&, GameObject* me, VisualScriptComponent*) {
             auto* l = me->GetComponent<LineRenderer>();
             if (!l || l->positions.size() != 16) return false;
             for (const Vec3& p : l->positions) if (!Near(std::fabs(p.x), 1) || !Near(std::fabs(p.y), 2) || !Near(std::fabs(p.z), 3)) return false;
             return true; }, 0, false},
        {"Watch Value: one line per name, updated in place",
         Chain("node 2 GlWatchValue\ndefault 2 0 \"a\"\ndefault 2 1 1\nnode 3 GlWatchValue\ndefault 3 0 \"b\"\ndefault 3 1 \"two\"\nnode 4 GlWatchValue\ndefault 4 0 \"a\"\ndefault 4 1 3\n", 4), none,
         [](Scene& s, GameObject*, VisualScriptComponent*) { GameObject* w = s.Find("Watch Values"); return w && w->GetComponent<TextRenderer>()->text == "a: 3\nb: two"; }, 0, false},
        // Damage with a type, and On Damaged.
        {"Damage of type Fire reaches On Damaged with Amount, Type and Source",
         "node 1 OnStart\nentry OnStart 1\nnode 2 Damage\ndefault 2 0 30\ndefault 2 2 \"fire\"\nexec 1 0 2\n"
         "node 5 OnDamaged\nentry OnDamaged 5\nnode 6 SetVar amt\ndata 6 0 5 0\nnode 7 SetVar kind\ndata 7 0 5 1\nnode 8 SetVar who\ndata 8 0 5 2\nnode 9 AddVar hits\ndefault 9 0 1\nexec 5 0 6\nexec 6 0 7\nexec 7 0 8\nexec 8 0 9\n",
         [](Scene&, GameObject* me) { auto* h = me->AddComponent<HealthStat>(); h->playDeathAnim = false; h->playHitAnim = false; },
         [](Scene&, GameObject* me, VisualScriptComponent* vc) {
             return Near(vc->GetVariable("amt").AsFloat(), 30) && vc->GetVariable("kind").AsString() == "Fire" &&
                    vc->GetVariable("who").AsObjectId() == me->InstanceId() && Near(vc->GetVariable("hits").AsFloat(), 1) &&
                    Near(me->GetComponent<HealthStat>()->health, me->GetComponent<HealthStat>()->maxHealth - 30); }, 1, false},
        {"On Damaged counts the health really lost (armor 5 takes 5 off 12)",
         "node 1 OnStart\nentry OnStart 1\nnode 2 Damage\ndefault 2 0 12\nexec 1 0 2\nnode 5 OnDamaged\nentry OnDamaged 5\nnode 6 SetVar amt\ndata 6 0 5 0\nnode 7 SetVar kind\ndata 7 0 5 1\nexec 5 0 6\nexec 6 0 7\n",
         [](Scene&, GameObject* me) { auto* h = me->AddComponent<HealthStat>(); h->playDeathAnim = false; h->playHitAnim = false; h->armor = 5; },
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return Near(vc->GetVariable("amt").AsFloat(), 7) && vc->GetVariable("kind").AsString() == "Generic"; }, 1, false},
        {"On Damaged only hears hits on its own object",
         "node 1 OnStart\nentry OnStart 1\nnode 2 Damage\ndefault 2 0 5\ndefault 2 1 Dummy\nexec 1 0 2\nnode 5 OnDamaged\nentry OnDamaged 5\nnode 6 AddVar hits\ndefault 6 0 1\nexec 5 0 6\n",
         [](Scene& s, GameObject* me) { me->AddComponent<HealthStat>()->playDeathAnim = false; auto* h = s.CreateGameObject("Dummy")->AddComponent<HealthStat>(); h->playDeathAnim = false; h->playHitAnim = false; },
         [](Scene& s, GameObject*, VisualScriptComponent* vc) { return Near(vc->GetVariable("hits").AsFloat(), 0) && Near(s.Find("Dummy")->GetComponent<HealthStat>()->health, s.Find("Dummy")->GetComponent<HealthStat>()->maxHealth - 5); }, 2, false},
        {"Damage with an unknown type is an error and does no damage", Chain("node 2 Damage\ndefault 2 0 10\ndefault 2 2 \"lava\"\n", 2),
         [](Scene&, GameObject* me) { me->AddComponent<HealthStat>(); },
         [](Scene&, GameObject* me, VisualScriptComponent*) { auto* h = me->GetComponent<HealthStat>(); return Near(h->health, h->maxHealth); }, 0, true},
        // On Button Clicked: a button by its object's name, anywhere in the scene.
        {"On Button Clicked runs for the named button only",
         "node 1 OnButtonClicked Play\nentry OnButtonClicked:Play 1\nnode 2 AddVar play\ndefault 2 0 1\nexec 1 0 2\n"
         "node 3 OnButtonClicked Quit\nentry OnButtonClicked:Quit 3\nnode 4 AddVar quit\ndefault 4 0 1\nexec 3 0 4\n",
         [](Scene& s, GameObject*) { s.CreateGameObject("Play")->AddComponent<UIButton>(); s.CreateGameObject("Quit")->AddComponent<UIButton>(); },
         [](Scene&, GameObject*, VisualScriptComponent* vc) { return Near(vc->GetVariable("play").AsFloat(), 1) && Near(vc->GetVariable("quit").AsFloat(), 0); }, -1, false},
    };
    int failed = 0;
    for (FriendlyCase& c : rows) {
        g_errs.clear();
        Scene s; s.physicsEnabled = false;
        GameObject* me = s.CreateGameObject("Me");
        c.setup(s, me);
        auto* vc = Load(me, c.graph);
        s.Start();
        if (c.frames < 0) {   // press the Play button (keyboard / pad press), then this graph's frame sees it
            s.Update(0.1f);
            s.Find("Play")->GetComponent<UIButton>()->Press();
            vc->Update(0.1f);   // the scene may update UI buttons first, which clears a press made between frames
            s.Update(0.1f);
        }
        for (int f = 0; f < c.frames; ++f) s.Update(0.1f);
        const bool ok = c.check(s, me, vc);
        const bool errOk = c.expectError ? !g_errs.empty() : g_errs.empty();
        if (!ok || !errOk) {
            ++failed;
            std::printf("  FAIL: %s%s\n", c.name, errOk ? "" : (c.expectError ? " (expected a node error)" : " (unexpected node error)"));
            for (const auto& e : g_errs) std::printf("    error: %s\n", e.c_str());
        }
        CHECK(ok);
        CHECK(errOk);
    }
    std::printf("  friendly rows: %d, failed %d\n", (int)rows.size(), failed);

    // Split Text: value rows.
    const std::vector<ValueCase> split = {
        {"GlSplitText", "", {"\"red,green,blue\"", "\",\""}, "[red;green;blue]", 0},
        {"GlSplitText", "", {"\"a b  c\"", "\" \""}, "[a;b;;c]", 0},          // two spaces: an empty piece between
        {"GlSplitText", "", {"\"abc\"", "\"\""}, "[a;b;c]", 0},               // no separator: one letter each
        {"GlSplitText", "", {"\"one\"", "\",\""}, "[one]", 0},                // no separator in the text: the whole text
        {"GlSplitText", "", {"\"x::y\"", "\"::\""}, "[x;y]", 0},              // a separator longer than one letter
    };
    const int sf = RunValues(split);
    std::printf("  split rows: %d, failed %d\n", (int)split.size(), sf);
}

// Countdown Timer: On Finished runs exactly once, when the time is up, at any frame rate; Counting runs
// on every frame before that; Restart starts it again.
static void CountdownTimer() {
    for (int fps : {10, 30, 120}) {
        Time::ResetForTesting();
        Scene s; s.physicsEnabled = false;
        GameObject* me = s.CreateGameObject("Me");
        auto* vc = Load(me, "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 GlCountdownTimer\ndefault 2 0 2\n"
                            "node 3 AddVar done\ndefault 3 0 1\nexec 1 0 2\nexec 2 1 3\n"
                            "node 4 SetVar at\nnode 5 Time\ndata 4 0 5 0\nexec 3 0 4\n");
        s.Start();
        const float dt = 1.0f / (float)fps;
        float doneAt = -1;
        for (int f = 0; f < 4 * fps; ++f) {
            Time::Step(dt); s.Update(dt);
            if (doneAt < 0 && vc->GetVariable("done").AsFloat() > 0.5f) doneAt = (float)(f + 1) * dt;
        }
        const int done = (int)std::lround(vc->GetVariable("done").AsFloat());
        if (done != 1 || std::fabs(doneAt - 2.0f) > dt + 1e-3f)
            std::printf("  Countdown Timer at %d fps: finished %d times, at %.3f s (want once, at 2 s)\n", fps, done, doneAt);
        CHECK(done == 1);
        CHECK(std::fabs(doneAt - 2.0f) <= dt + 1e-3f);
    }
    {   // Restart: true on the frame it is wired true starts the count again.
        Scene s; s.physicsEnabled = false;
        GameObject* me = s.CreateGameObject("Me");
        auto* vc = Load(me, "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 GlCountdownTimer\ndefault 2 0 0.5\nnode 6 GetVar again\ndata 2 1 6 0\n"
                            "node 3 AddVar done\ndefault 3 0 1\nexec 1 0 2\nexec 2 1 3\nnode 4 SetVar again\ndefault 4 0 false\nexec 3 0 4\n");
        s.Start();
        for (int f = 0; f < 10; ++f) s.Update(0.1f);
        CHECK(std::lround(vc->GetVariable("done").AsFloat()) == 1);
        vc->SetVariable("again", vs::VsValue(true));
        s.Update(0.1f);
        vc->SetVariable("again", vs::VsValue(false));
        for (int f = 0; f < 10; ++f) s.Update(0.1f);
        const long n = std::lround(vc->GetVariable("done").AsFloat());
        if (n != 2) std::printf("  Countdown Timer after Restart finished %ld times (want 2)\n", n);
        CHECK(n == 2);
    }
}

int main() {
    RUN_SUITE("vs_node_values");
    okaytest::Sandbox sandbox("vs_node_values", OKAY_SOURCE_DIR);   // Save Value writes land in a temp folder
    vs::SetRuntimeErrorHook(&OnErr);
    // OKAY_DUMP_NODES=1: print every palette node (type, title, category) for the node inventory.
    if (std::getenv("OKAY_DUMP_NODES")) {
        for (const auto& d : okay::editor::vsed::Registry()) std::printf("NODE\t%s\t%s\t%d\n", d.type, d.title, (int)d.cat);
    }
    Values();
    SceneNodes();
    Maps();
    FlowGates();
    FriendlyNodes();
    CountdownTimer();
    TEST_MAIN_RESULT();
}
