// Issue #888, findings 31 and 33 (the Visual Script half).
//
// Finding 31: one NaN or infinite number from a math node (Fn.sqrt(-1), a divide by zero, a typed "nan") went
// straight into an object's position, scale or Rigidbody velocity, and the object was gone for good: every later
// move added to NaN. The shared setters (Transform::SetPosition / Translate / SetLocalPosition / SetLocalScale /
// SetLossyScale, Rigidbody velocity / force / impulse / gravity scale) now drop a value that is not finite, keep
// the old one and warn once per object; Fn.* nodes, whose script builtins write the fields directly, are guarded
// where the graph calls them.
//
// Finding 33: float -> int casts in nodes were undefined for NaN, Inf and 1e30 (a UBSan build traps). Each listed
// site now saturates (SafeInt) or clamps first; ParticleSystem::Emit never loops past its pool.
//
// Everything is driven through graph text, like a saved .okayvs file, and uses only public engine API, so the same
// file builds and runs on the code before the fix (where the checks fail) and after it.
//
// Optional arguments: a section name (or a prefix such as "cast:", or "nodes:SetPosition" for one node), then a value
// ("1e30", "nan", "inf", "-inf") to run one case. The UBSan build stops at the first undefined operation, so each
// cast site (and each node) can be run in its own process.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Crafting.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/VisualScript/VsTypes.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace okay;

namespace {

std::string g_section;   // argv[1]: run only sections that start with this
std::string g_value;     // argv[2]: run only this bad value in the loops below
std::vector<std::string> g_warnings;

// "cast:" runs every cast section; "nodes:SetPosition" runs one node of the nodes section.
bool Want(const char* section) {
    const std::string s(section);
    return g_section.empty() || s.rfind(g_section, 0) == 0 || g_section.rfind(s + ":", 0) == 0;
}
bool WantNode(const char* type) {
    const std::size_t colon = g_section.find(':');
    if (colon == std::string::npos || g_section.rfind("cast:", 0) == 0) return true;
    std::string name(type);
    name = name.substr(0, name.find(' '));
    return name == g_section.substr(colon + 1);
}
bool WantValue(const std::string& v) { return g_value.empty() || g_value == v; }

bool Fin(float f) { return std::isfinite(f); }
bool Fin(const Vec3& v) { return Fin(v.x) && Fin(v.y) && Fin(v.z); }
bool Same(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

void Report(bool ok, const std::string& what) {
    CHECK(ok);
    if (!ok) std::printf("    case: %s\n", what.c_str());
}

VisualScriptComponent* Attach(GameObject* go, const std::string& text) {
    auto* vc = go->AddComponent<VisualScriptComponent>();
    std::string err;
    if (!vc->LoadFromText(text, &err)) std::printf("  load error (%s): %s\n", go->name.c_str(), err.c_str());
    return vc;
}

void Tick(Scene& s, int frames, float dt = 1.0f / 60.0f) { for (int i = 0; i < frames; ++i) s.Update(dt); }

int WarningsAbout(const std::string& name) {
    int n = 0;
    for (const auto& w : g_warnings)
        if (w.find(name) != std::string::npos && w.find("NaN") != std::string::npos) ++n;
    return n;
}

// Everything a moving object can lose to a NaN: where it is, how big it is, how its body moves.
std::string BadState(GameObject* go) {
    std::string bad;
    const Transform* t = go->transform;
    if (!Fin(t->localPosition)) bad += " position";
    if (!Fin(t->Position())) bad += " world-position";
    if (!Fin(t->localScale)) bad += " scale";
    if (auto* rb = go->GetComponent<Rigidbody3D>()) {
        if (!Fin(rb->velocity)) bad += " velocity";
        if (!Fin(rb->angularVelocity)) bad += " angular-velocity";
        if (!Fin(rb->gravityScale)) bad += " gravity-scale";
    }
    if (auto* rb = go->GetComponent<Rigidbody2D>()) {
        if (!Fin(rb->velocity.x) || !Fin(rb->velocity.y)) bad += " velocity2d";
        if (!Fin(rb->gravityScale)) bad += " gravity-scale2d";
    }
    return bad;
}

const char* const kBad[] = {"nan", "inf", "-inf"};

// ---- One node, fed one bad number on one input --------------------------------------------------------------
// pins: one letter per input. 'n' a number, 'v' a Vector3 (fed through a Make Vector3 node, one bad axis at a
// time), '-' left alone. `fill` gives the other number inputs a neutral value (Fn.* nodes pass their inputs as a
// plain argument list, so a gap would shift the bad value into another argument).
struct NodeCase {
    const char* type;
    const char* event;   // "OnStart" or "OnUpdate"
    const char* pins;
    const char* fill;    // space-separated neutral values for the 'n' pins, or "" for none
    bool body;           // the object needs a Rigidbody3D
    int frames;
};

std::vector<std::string> Words(const char* s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char* p = s; ; ++p) {
        if (*p == ' ' || *p == 0) { if (!cur.empty()) out.push_back(cur); cur.clear(); if (!*p) break; }
        else cur += *p;
    }
    return out;
}

// Graph text for one case: the node is node 2, on the event; Make Vector3 nodes are 10, 11, ...
std::string CaseGraph(const NodeCase& c, int badPin, int badAxis, const std::string& bad) {
    std::string t = std::string("node 1 ") + c.event + "\nentry " + c.event + " 1\nnode 2 " + c.type + "\nexec 1 0 2\n";
    const auto fill = Words(c.fill);
    for (int p = 0; c.pins[p]; ++p) {
        const char kind = c.pins[p];
        if (kind == 'n') {
            std::string v = p < (int)fill.size() ? fill[(std::size_t)p] : std::string();
            if (p == badPin) v = bad;
            if (!v.empty()) t += "default 2 " + std::to_string(p) + " " + v + "\n";
        } else if (kind == 'v') {
            const int id = 10 + p;
            t += "node " + std::to_string(id) + " MakeVec3\n";
            for (int a = 0; a < 3; ++a)
                t += "default " + std::to_string(id) + " " + std::to_string(a) + " " +
                     (p == badPin && a == badAxis ? bad : std::string(a == 1 ? "1" : "0")) + "\n";
            t += "data 2 " + std::to_string(p) + " " + std::to_string(id) + " 0\n";
        }
    }
    return t;
}

// Runs every (input, bad value) pair of a node in its own scene; the object must keep finite numbers.
void RunNodeCase(const NodeCase& c) {
    if (!WantNode(c.type)) return;
    for (int p = 0; c.pins[p]; ++p) {
        if (c.pins[p] == '-') continue;
        for (int axis = 0; axis < (c.pins[p] == 'v' ? 3 : 1); ++axis) {
            for (const char* bad : kBad) {
                if (!WantValue(bad)) continue;
                Scene s("NanNode");
                GameObject* go = s.CreateGameObject("Mover");
                go->transform->localPosition = {1, 2, 3};
                if (c.body) {
                    auto* rb = go->AddComponent<Rigidbody3D>();
                    rb->gravityScale = 0.0f;
                }
                Attach(go, CaseGraph(c, p, axis, bad));
                s.Start();
                Tick(s, c.frames);
                const std::string state = BadState(go);
                Report(state.empty(), std::string(c.type) + " input " + std::to_string(p) +
                                      (c.pins[p] == 'v' ? " axis " + std::to_string(axis) : std::string()) + " = " + bad +
                                      " left:" + state);
            }
        }
    }
}

// ---- Number outputs read back through a variable -------------------------------------------------------------
vs::VsValue RunPure(GameObject* go, Scene& s, const std::string& node, const std::string& defaults) {
    auto* vc = Attach(go, "node 1 OnStart\nentry OnStart 1\nnode 2 " + node + "\n" + defaults +
                          "node 3 SetVar out\nexec 1 0 3\ndata 3 0 2 0\n");
    s.Start();
    Tick(s, 1);
    return vc->GetVariable("out");
}

} // namespace

int main(int argc, char** argv) {
    RUN_SUITE("vs_nan_writes");
    if (argc > 1) g_section = argv[1];
    if (argc > 2) g_value = argv[2];
    Log::sink = [](Log::Level level, const std::string& text) {
        if (level == Log::Level::Warning) g_warnings.push_back(text);
    };

    // ==== The example scene the Windows release ships (docs/try_it.md): the same chain on "NaN Cube" ==========
    if (Want("example")) {
        Scene s("Example");
        std::string err;
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/node_playground/nan_guard.okayscene";
        CHECK(SceneSerializer::LoadFromFile(s, path, &err));
        if (!err.empty()) std::printf("  example: %s\n", err.c_str());
        GameObject* cube = s.Find("NaN Cube");
        CHECK(cube != nullptr);
        if (cube) {
            g_warnings.clear();
            s.Start();
            Tick(s, 120);   // two seconds
            const Vec3 p = cube->transform->localPosition;
            std::printf("  example: NaN Cube at (%g, %g, %g), %d warning(s)\n", p.x, p.y, p.z, WarningsAbout("NaN Cube"));
            CHECK(Fin(p));
            CHECK(p.x > -2.0f && p.x < 0.0f);   // it starts at x = -2 and slides right 0.01 a frame
            CHECK(p.y == 0.0f && p.z == 0.0f);
            CHECK(WarningsAbout("NaN Cube") == 1);
        }
    }

    // ==== Finding 31: Grok's chain. Fn.sqrt(-1) into Set Position, then Translate every frame for 90 frames ====
    if (Want("chain")) {
        Scene s("Chain");
        GameObject* cube = s.CreateGameObject("Cube");
        GameObject* control = s.CreateGameObject("Control");   // the same graph with a finite number
        const char* graph =
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 Fn.sqrt\ndefault 2 0 %s\nexec 1 0 2\n"
            "node 3 SetPosition\ndata 3 0 2 0\nexec 2 0 3\n"
            "node 4 OnUpdate\nentry OnUpdate 4\n"
            "node 5 Translate\ndefault 5 0 1\nexec 4 0 5\n";
        char buf[512];
        std::snprintf(buf, sizeof buf, graph, "-1");
        Attach(cube, buf);
        std::snprintf(buf, sizeof buf, graph, "0");
        Attach(control, buf);
        g_warnings.clear();
        s.Start();
        Tick(s, 90);
        const Vec3 p = cube->transform->localPosition;
        std::printf("  chain: Cube at (%g, %g, %g), Control at (%g, %g, %g), %d warning(s) about Cube\n",
                    p.x, p.y, p.z, control->transform->localPosition.x, control->transform->localPosition.y,
                    control->transform->localPosition.z, WarningsAbout("Cube"));
        CHECK(Fin(p));
        CHECK(p.x > 80.0f);                                          // Translate still moves it, 1 a frame
        CHECK(Same(p, control->transform->localPosition));           // exactly where the finite graph ends
        CHECK(WarningsAbout("Cube") == 1);                           // one warning, not one a frame
        CHECK(WarningsAbout("Control") == 0);
    }

    // ==== Finding 31: every hand-written node on the list, each number input, NaN / Inf / -Inf ===========
    if (Want("nodes")) {
        const NodeCase cases[] = {
            {"AddForce", "OnStart", "nnn", "", true, 5},
            {"AddImpulse", "OnStart", "nnn", "", true, 5},
            {"Bob", "OnUpdate", "nn", "0.25 2", false, 5},
            {"GlKeepAboveHeight", "OnUpdate", "n", "", false, 3},
            {"MoveBy", "OnStart", "v", "", false, 2},
            {"SetPosition", "OnStart", "nnn", "", false, 2},
            {"SetPositionVec", "OnStart", "v", "", false, 2},
            {"SetScale", "OnStart", "nnn", "", false, 2},
            {"SetVelocity", "OnStart", "nnn", "", true, 5},
            {"Translate", "OnUpdate", "nnn", "", false, 3},
            {"TweenPosition linear", "OnStart", "vn", "- 0.05", false, 10},
            {"TweenScale linear", "OnStart", "vn", "- 0.05", false, 10},
            {"Wander", "OnUpdate", "nn", "4 1.5", false, 30},
        };
        for (const auto& c : cases) RunNodeCase(c);
    }

    // ==== Finding 31: the engine-API nodes (the C#-style bindings) on the list ===========================
    if (Want("api")) {
        const NodeCase cases[] = {
            {"Rigidbody.AddForce/1", "OnStart", "v", "", true, 5},
            {"Rigidbody.AddForceAtPosition/2", "OnStart", "vv", "", true, 5},
            {"Rigidbody.AddImpulse/1", "OnStart", "v", "", true, 5},
            {"Rigidbody.gravityScale=", "OnStart", "n", "", true, 5},
            {"Rigidbody.velocity=", "OnStart", "v", "", true, 5},
            {"Transform.Translate/1", "OnUpdate", "v", "", false, 3},
            {"Transform.Translate/3", "OnUpdate", "nnn", "", false, 3},
            {"Transform.localPosition=", "OnStart", "v", "", false, 2},
            {"Transform.localScale=", "OnStart", "v", "", false, 2},
            {"Transform.position=", "OnStart", "v", "", false, 2},
        };
        for (const auto& c : cases) RunNodeCase(c);
    }

    // ==== Finding 31: a sample of the Fn.* nodes. Some call Translate / AddForce / AddImpulse (the guarded
    // setters); the rest write the fields from inside the script VM and are caught where the graph calls them ====
    if (Want("fn")) {
        const NodeCase cases[] = {
            {"Fn.move", "OnUpdate", "nn", "0 0", false, 3},                 // Translate
            {"Fn.translate", "OnUpdate", "nn", "0 0", false, 3},            // alias of move
            {"Fn.move3", "OnUpdate", "nnn", "0 0 0", false, 3},             // Translate
            {"Fn.walk", "OnUpdate", "nnn", "1 0 1", false, 3},              // Translate
            {"Fn.set_pos", "OnStart", "nn", "0 0", false, 2},               // writes localPosition
            {"Fn.set_pos3", "OnStart", "nnn", "0 0 0", false, 2},           // writes localPosition
            {"Fn.set_x", "OnStart", "n", "0", false, 2},                    // writes localPosition.x
            {"Fn.set_scale", "OnStart", "n", "1", false, 2},                // writes localScale
            {"Fn.set_scale3", "OnStart", "nnn", "1 1 1", false, 2},         // writes localScale
            {"Fn.pulse", "OnUpdate", "nn", "0.2 3", false, 3},              // writes localScale
            {"Fn.bob", "OnUpdate", "nn", "0.5 2", false, 3},                // writes localPosition.y
            {"Fn.keep_in_box", "OnUpdate", "nnnn", "-5 -5 5 5", false, 3},  // writes localPosition
            {"Fn.set_velocity", "OnStart", "nnn", "0 0 0", true, 5},        // writes velocity
            {"Fn.set_vx", "OnStart", "n", "0", true, 5},                    // writes velocity.x
            {"Fn.jump", "OnStart", "n", "5", true, 5},                      // writes velocity.y
            {"Fn.add_force", "OnStart", "nnn", "0 0 0", true, 5},           // AddForce
            {"Fn.add_impulse", "OnStart", "nnn", "0 0 0", true, 5},         // AddImpulse
        };
        for (const auto& c : cases) RunNodeCase(c);
    }

    // ==== Finding 31: the setters themselves, called from C++ (physics, animation and network code use them) ====
    if (Want("direct")) {
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        Scene s("Direct");
        s.physicsEnabled = false;
        GameObject* parent = s.CreateGameObject("Parent");
        GameObject* go = s.CreateGameObject("Direct");
        go->transform->localPosition = {1, 2, 3};
        go->transform->SetPosition({nan, 0, 0});
        CHECK(Same(go->transform->localPosition, {1, 2, 3}));
        go->transform->Translate({inf, 0, 0});
        CHECK(Same(go->transform->localPosition, {1, 2, 3}));
        go->transform->Translate({0, nan, 0}, Space::World);
        CHECK(Same(go->transform->localPosition, {1, 2, 3}));
        go->transform->SetScale({2, nan, 2});
        CHECK(Same(go->transform->localScale, {1, 1, 1}));
        go->transform->SetLocalPositionAndRotation({nan, nan, nan}, Quat::Identity);
        CHECK(Same(go->transform->localPosition, {1, 2, 3}));
        auto* rb = go->AddComponent<Rigidbody3D>();
        rb->AddImpulse({0, nan, 0});
        CHECK(Same(rb->velocity, {0, 0, 0}));
        rb->AddForce({inf, 0, 0}, ForceMode::VelocityChange);
        CHECK(Same(rb->velocity, {0, 0, 0}));
        // A child of a parent: the world position goes through the parent's inverse.
        go->transform->SetParent(parent->transform, false);
        parent->transform->localScale = {2, 2, 2};
        go->transform->SetPosition({inf, 1, 1});
        CHECK(Fin(go->transform->localPosition));
        // A force waits for the next physics step: step it and look at the body.
        Scene s2("DirectStep");
        GameObject* b = s2.CreateGameObject("Ball");
        auto* rb2 = b->AddComponent<Rigidbody3D>();
        rb2->gravityScale = 0.0f;
        s2.Start();
        rb2->AddForce({nan, 0, 0});
        rb2->AddForceAtPosition({0, inf, 0}, {0, 0, 0});
        Tick(s2, 3);
        Report(BadState(b).empty(), "AddForce / AddForceAtPosition with NaN / Inf, then 3 physics steps, left:" + BadState(b));
    }

    // ==== Finite numbers: exactly the results the nodes gave before ====================================
    if (Want("finite")) {
        auto run = [](const char* name, const std::string& graph, bool body, bool physics = false) {
            Scene* s = new Scene("Finite");
            s->physicsEnabled = physics;
            GameObject* go = s->CreateGameObject(name);
            go->transform->localPosition = {1, 0, 0};
            if (body) { auto* rb = go->AddComponent<Rigidbody3D>(); rb->gravityScale = 0.0f; rb->mass = 2.0f; }
            Attach(go, "node 1 OnStart\nentry OnStart 1\n" + graph);
            s->Start();
            Tick(*s, 1);
            return std::make_pair(s, go);
        };
        {
            auto [s, go] = run("SetPosition", "node 2 SetPosition\ndefault 2 0 1.5\ndefault 2 1 -2.25\ndefault 2 2 3\nexec 1 0 2\n", false);
            CHECK(Same(go->transform->localPosition, {1.5f, -2.25f, 3.0f}));
            delete s;
        }
        {
            auto [s, go] = run("Translate", "node 2 Translate\ndefault 2 0 0.5\ndefault 2 1 -1\ndefault 2 2 2\nexec 1 0 2\n", false);
            CHECK(Same(go->transform->localPosition, {1.5f, -1.0f, 2.0f}));
            delete s;
        }
        {
            auto [s, go] = run("SetScale", "node 2 SetScale\ndefault 2 0 2\ndefault 2 1 3\ndefault 2 2 0.25\nexec 1 0 2\n", false);
            CHECK(Same(go->transform->localScale, {2.0f, 3.0f, 0.25f}));
            delete s;
        }
        {
            auto [s, go] = run("MoveBy", "node 2 MoveBy\nnode 3 MakeVec3\ndefault 3 0 1\ndefault 3 1 1\ndefault 3 2 1\ndata 2 0 3 0\nexec 1 0 2\n", false);
            CHECK(Same(go->transform->localPosition, {2.0f, 1.0f, 1.0f}));
            delete s;
        }
        {
            auto [s, go] = run("SetPositionVec", "node 2 SetPositionVec\nnode 3 MakeVec3\ndefault 3 0 4\ndefault 3 1 5\ndefault 3 2 6\ndata 2 0 3 0\nexec 1 0 2\n", false);
            CHECK(Same(go->transform->localPosition, {4.0f, 5.0f, 6.0f}));
            delete s;
        }
        {
            auto [s, go] = run("SetVelocity", "node 2 SetVelocity\ndefault 2 0 1\ndefault 2 1 2\ndefault 2 2 3\nexec 1 0 2\n", true);
            CHECK(Same(go->GetComponent<Rigidbody3D>()->velocity, {1.0f, 2.0f, 3.0f}));
            delete s;
        }
        {
            auto [s, go] = run("AddImpulse", "node 2 AddImpulse\ndefault 2 0 0\ndefault 2 1 2\ndefault 2 2 0\nexec 1 0 2\n", true);
            CHECK(Same(go->GetComponent<Rigidbody3D>()->velocity, {0.0f, 1.0f, 0.0f}));   // impulse 2 / mass 2
            delete s;
        }
        {
            auto [s, go] = run("GravityScale", "node 2 Rigidbody.gravityScale=\ndefault 2 0 0.5\nexec 1 0 2\n", true);
            CHECK(go->GetComponent<Rigidbody3D>()->gravityScale == 0.5f);
            delete s;
        }
        {
            auto [s, go] = run("LocalScale", "node 2 Transform.localScale=\nnode 3 MakeVec3\ndefault 3 0 2\ndefault 3 1 2\ndefault 3 2 2\ndata 2 0 3 0\nexec 1 0 2\n", false);
            CHECK(Same(go->transform->localScale, {2.0f, 2.0f, 2.0f}));
            delete s;
        }
        {
            auto [s, go] = run("FnSetX", "node 2 Fn.set_x\ndefault 2 0 7\nexec 1 0 2\n", false);
            CHECK(Same(go->transform->localPosition, {7.0f, 0.0f, 0.0f}));
            delete s;
        }
        {
            auto [s, go] = run("FnSetScale", "node 2 Fn.set_scale\ndefault 2 0 3\nexec 1 0 2\n", false);
            CHECK(Same(go->transform->localScale, {3.0f, 3.0f, 3.0f}));
            delete s;
        }
        {
            auto [s, go] = run("FnSetVelocity", "node 2 Fn.set_velocity\ndefault 2 0 1\ndefault 2 1 2\ndefault 2 2 3\nexec 1 0 2\n", true);
            CHECK(Same(go->GetComponent<Rigidbody3D>()->velocity, {1.0f, 2.0f, 3.0f}));
            delete s;
        }
        {   // A world position under a moved, scaled parent: (5 - 1) / 2 = 2.
            Scene s("FiniteParent");
            s.physicsEnabled = false;
            GameObject* parent = s.CreateGameObject("Parent");
            parent->transform->localPosition = {1, 0, 0};
            parent->transform->localScale = {2, 2, 2};
            GameObject* child = s.CreateGameObject("Child");
            child->transform->SetParent(parent->transform, false);
            Attach(child, "node 1 OnStart\nentry OnStart 1\nnode 2 Transform.position=\nnode 3 MakeVec3\n"
                          "default 3 0 5\ndefault 3 1 0\ndefault 3 2 0\ndata 2 0 3 0\nexec 1 0 2\n");
            s.Start();
            Tick(s, 1);
            CHECK(Same(child->transform->localPosition, {2.0f, 0.0f, 0.0f}));
        }
        {   // The C++ setters with finite numbers.
            Scene s("FiniteDirect");
            s.physicsEnabled = false;
            GameObject* go = s.CreateGameObject("D");
            go->transform->Translate({0.25f, 0.5f, -1.0f});
            CHECK(Same(go->transform->localPosition, {0.25f, 0.5f, -1.0f}));
            go->transform->SetPosition({3, 4, 5});
            CHECK(Same(go->transform->localPosition, {3.0f, 4.0f, 5.0f}));
            go->transform->SetScale({0.5f, 2.0f, 4.0f});
            CHECK(Same(go->transform->localScale, {0.5f, 2.0f, 4.0f}));
            auto* rb = go->AddComponent<Rigidbody3D>();
            rb->mass = 4.0f;
            rb->AddImpulse({4, 0, 0});
            CHECK(Same(rb->velocity, {1.0f, 0.0f, 0.0f}));
            rb->AddForce({0, 1, 0}, ForceMode::VelocityChange);
            CHECK(Same(rb->velocity, {1.0f, 1.0f, 0.0f}));
        }
        {   // A force through a physics step still moves the body the same way: v = F / m * dt.
            Scene s("FiniteForce");
            GameObject* b = s.CreateGameObject("Ball");
            auto* rb = b->AddComponent<Rigidbody3D>();
            rb->gravityScale = 0.0f;
            rb->mass = 2.0f;
            s.Start();
            rb->AddForce({0, 0, 120});
            Tick(s, 1);
            CHECK(rb->velocity.z > 0.0f && Fin(rb->velocity));
        }
    }

    // ==== Check Graph: a typed nan / inf in a pin is a warning on that pin; quoted text is not ===============
    if (Want("check")) {
        const auto issues = vs::ValidateGraphText(
            "node 1 OnStart\nentry OnStart 1\nnode 2 SetPosition\ndefault 2 0 nan\ndefault 2 1 -inf\ndefault 2 2 3\nexec 1 0 2\n"
            "node 3 Print\ndefault 3 0 \"nan\"\nexec 2 0 3\n");
        int onPin0 = 0, onPin1 = 0, onOther = 0;
        for (const auto& is : issues) {
            if (is.message.find("not a finite number") == std::string::npos) continue;
            if (is.node == 2 && is.pin == 0 && is.severity == 1) ++onPin0;
            else if (is.node == 2 && is.pin == 1 && is.severity == 1) ++onPin1;
            else ++onOther;
        }
        CHECK(onPin0 == 1);
        CHECK(onPin1 == 1);
        CHECK(onOther == 0);   // the finite 3 and the quoted text "nan" are fine
    }

    // ==== Finding 33: float -> int casts in nodes, fed 1e30, NaN and Inf ===============================
    const char* const kCastValues[] = {"1e30", "nan", "inf", "-inf"};
    auto castCase = [&](const char* section, auto&& body) {
        if (!Want(section)) return;
        for (const char* v : kCastValues) {
            if (!WantValue(v)) continue;
            body(std::string(v));
        }
    };

    castCase("cast:AddItem", [](const std::string& v) {
        Scene s("CastAdd");
        GameObject* go = s.CreateGameObject("Bag");
        auto* inv = go->AddComponent<Inventory>();
        auto* vc = Attach(go, "node 1 OnStart\nentry OnStart 1\nnode 2 AddItem wood\ndefault 2 0 " + v +
                              "\nexec 1 0 2\nnode 3 SetVar left\ndata 3 0 2 0\nexec 2 0 3\n");
        s.Start();
        Tick(s, 1);
        const float l = vc->GetVariable("left").AsFloat();
        Report(Fin(l) && l >= 0.0f, "AddItem count " + v + " left " + std::to_string(l));
        Report(inv->Count("wood") >= 0, "AddItem count " + v + " bag count " + std::to_string(inv->Count("wood")));
    });
    castCase("cast:RemoveItem", [](const std::string& v) {
        Scene s("CastRemove");
        GameObject* go = s.CreateGameObject("Bag");
        auto* inv = go->AddComponent<Inventory>();
        inv->Add("wood", 3);
        auto* vc = Attach(go, "node 1 OnStart\nentry OnStart 1\nnode 2 RemoveItem wood\ndefault 2 0 " + v +
                              "\nexec 1 0 2\nnode 3 SetVar ok\ndata 3 0 2 0\nexec 2 0 3\n");
        s.Start();
        Tick(s, 1);
        Report(!vc->GetVariable("ok").AsBool() && inv->Count("wood") == 3, "RemoveItem count " + v);
    });
    castCase("cast:HasItem", [](const std::string& v) {
        Scene s("CastHas");
        GameObject* go = s.CreateGameObject("Bag");
        go->AddComponent<Inventory>()->Add("wood", 3);
        const vs::VsValue has = RunPure(go, s, "HasItem wood", "default 2 0 " + v + "\n");
        // NaN counts as 0 (has at least none: true); 1e30 / Inf as a huge count (false); -Inf as the lowest (true).
        Report(has.IsBool(), "HasItem count " + v);
    });
    castCase("cast:CraftItem", [](const std::string& v) {
        Scene s("CastCraft");
        GameObject* go = s.CreateGameObject("Bench");
        go->AddComponent<Inventory>();
        go->AddComponent<Crafting>();
        auto* vc = Attach(go, "node 1 OnStart\nentry OnStart 1\nnode 2 CraftItem plank\ndefault 2 0 " + v +
                              "\nexec 1 0 2\nnode 3 SetVar ok\ndata 3 0 2 0\nexec 2 0 3\n");
        s.Start();
        Tick(s, 1);
        Report(!vc->GetVariable("ok").AsBool(), "CraftItem times " + v + " (no recipe: nothing made)");
    });
    castCase("cast:SpawnPickup", [](const std::string& v) {
        Scene s("CastPickup");
        GameObject* go = s.CreateGameObject("Dropper");
        auto* vc = Attach(go, "node 1 OnStart\nentry OnStart 1\nnode 2 SpawnPickup coin\ndefault 2 0 " + v +
                              "\ndefault 2 1 " + v + "\ndefault 2 2 1\ndefault 2 3 2\nexec 1 0 2\n"
                              "node 3 SetVar name\ndata 3 0 2 0\nexec 2 0 3\n");
        s.Start();
        Tick(s, 1);
        GameObject* pk = s.Find(vc->GetVariable("name").AsString());
        Report(pk != nullptr, "SpawnPickup count " + v + " made a pickup");
        if (pk) Report(BadState(pk).empty(), "SpawnPickup at x = " + v + " left:" + BadState(pk));
    });
    castCase("cast:CompleteObjective", [](const std::string& v) {
        Scene s("CastObjective");
        GameObject* go = s.CreateGameObject("Hero");
        Attach(go, "node 1 OnStart\nentry OnStart 1\nnode 2 CompleteObjective q1\ndefault 2 0 " + v + "\nexec 1 0 2\n");
        s.Start();
        Tick(s, 1);
        CHECK(true);   // reaching here without a trap is the check
    });
    castCase("cast:Noise", [](const std::string& v) {
        for (int pin = 0; pin < 2; ++pin) {
            Scene s("CastNoise");
            GameObject* go = s.CreateGameObject("N");
            const std::string other = pin == 0 ? "default 2 1 0.5\n" : "default 2 0 0.5\n";
            const float n = RunPure(go, s, "Noise", "default 2 " + std::to_string(pin) + " " + v + "\n" + other).AsFloat();
            Report(Fin(n) && std::fabs(n) <= 1.5f, "Noise input " + std::to_string(pin) + " = " + v + " gave " + std::to_string(n));
        }
    });
    castCase("cast:LbSampleNoise", [](const std::string& v) {
        Scene s("CastLbNoise");
        GameObject* go = s.CreateGameObject("N");
        const float n = RunPure(go, s, "LbSampleNoise", "default 2 0 " + v + "\n").AsFloat();
        Report(Fin(n) && n >= 0.0f && n <= 1.0f, "LbSampleNoise " + v + " gave " + std::to_string(n));
    });
    castCase("cast:LbSampleCurve", [](const std::string& v) {
        Scene s("CastLbCurve");
        GameObject* go = s.CreateGameObject("C");
        const float n = RunPure(go, s, "LbSampleCurve",
                                "default 2 0 " + v + "\ndefault 2 1 0\ndefault 2 2 2\ndefault 2 3 4\ndefault 2 4 6\n").AsFloat();
        Report(Fin(n) && n >= 0.0f && n <= 6.0f, "LbSampleCurve portion " + v + " gave " + std::to_string(n));
    });
    castCase("cast:GlFormatClock", [](const std::string& v) {
        Scene s("CastClock");
        GameObject* go = s.CreateGameObject("C");
        const std::string t = RunPure(go, s, "GlFormatClock", "default 2 0 " + v + "\n").AsString();
        Report(!t.empty() && t.find('-') == std::string::npos && t.find(':') != std::string::npos,
               "GlFormatClock " + v + " gave \"" + t + "\"");
    });
    castCase("cast:ParticleBurst", [](const std::string& v) {
        Scene s("CastBurst");
        GameObject* go = s.CreateGameObject("Fx");
        auto* ps = go->AddComponent<ParticleSystem>();
        ps->playing = false;
        Attach(go, "node 1 OnStart\nentry OnStart 1\nnode 2 ParticleBurst\ndefault 2 0 " + v + "\nexec 1 0 2\n");
        const auto t0 = std::chrono::steady_clock::now();
        s.Start();
        Tick(s, 1);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        Report(ps->AliveCount() <= (int)ps->Particles().size(), "ParticleBurst count " + v);
        Report(ms < 5000.0, "ParticleBurst count " + v + " took " + std::to_string(ms) + " ms");
    });
    castCase("cast:SteamLeaderboard", [](const std::string& v) {
        Scene s("CastSteam");
        GameObject* go = s.CreateGameObject("S");
        Attach(go, "node 1 OnStart\nentry OnStart 1\nnode 2 SteamLeaderboard best\ndefault 2 0 " + v + "\nexec 1 0 2\n");
        s.Start();
        Tick(s, 1);
        CHECK(true);   // reaching here without a trap is the check
    });

    Log::sink = nullptr;
    TEST_MAIN_RESULT();
}
