#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/VisualScript/Nodes.hpp"
#include <cmath>
#include <string>
#include <vector>

using namespace okay;

// Values typed into unconnected node inputs (the editor's inline boxes) reach
// the runtime; blank node fields don't break a graph; every node type loads and
// runs unwired without crashing.
int main() {
    RUN_SUITE("vs_pin_defaults");

    // ---- Typed values on unconnected inputs.
    {
        Scene scene;
        GameObject* self = scene.CreateGameObject("Hero");
        auto* vs = self->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vs->LoadFromText(
            "node 1 OnStart\n"
            "node 2 SetVar greeting\n" "default 2 0 \"hello world\"\n"
            "node 3 SetVar n\n"        "default 3 0 42\n"
            "node 4 Translate\n"       "default 4 0 1\n" "default 4 1 2\n" "default 4 2 3\n"
            "node 5 Print\n"           "default 5 0 \"printed text\"\n"
            "node 6 SetVar on\n"       "default 6 0 true\n"
            "node 7 SetVar \"two words\"\n"   // a quoted name with a space
            "default 7 0 7\n"
            "entry OnStart 1\n"
            "exec 1 0 2\n" "exec 2 0 3\n" "exec 3 0 4\n" "exec 4 0 5\n" "exec 5 0 6\n" "exec 6 0 7\n", &err));
        CHECK(err.empty());
        scene.Start();
        scene.Update(1.0f / 60.0f);
        CHECK(vs->GetVariable("greeting").AsString() == "hello world");
        CHECK(std::fabs(vs->GetVariable("n").AsFloat() - 42.0f) < 1e-5f);
        CHECK(vs->GetVariable("on").AsBool());
        CHECK(std::fabs(vs->GetVariable("two words").AsFloat() - 7.0f) < 1e-5f);
        Vec3 p = self->transform->Position();
        CHECK(std::fabs(p.x - 1) < 1e-4f && std::fabs(p.y - 2) < 1e-4f && std::fabs(p.z - 3) < 1e-4f);
    }

    // ---- Graphs saved by the editor carry layout lines ("pos") and must load.
    {
        Scene scene;
        GameObject* self = scene.CreateGameObject("Hero");
        auto* vs = self->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vs->LoadFromText(
            "# OkayVS graph (edited in the Visual Script editor)\n"
            "node 1 OnStart\n" "pos 1 40 40\n" "entry OnStart 1\n"
            "node 2 SetVar said\n" "pos 2 260 40\n" "default 2 0 \"hi there\"\n"
            "some_future_directive 1 2 3\n"
            "exec 1 0 2\n", &err));
        CHECK(err.empty());
        scene.Start(); scene.Update(1.0f / 60.0f);
        CHECK(vs->GetVariable("said").AsString() == "hi there");
    }

    // ---- A wired input wins over a typed value.
    {
        Scene scene;
        GameObject* self = scene.CreateGameObject("Hero");
        auto* vs = self->AddComponent<VisualScriptComponent>();
        CHECK(vs->LoadFromText(
            "node 1 OnStart\n" "node 2 SetVar v\n" "default 2 0 5\n" "node 3 Const 9\n"
            "entry OnStart 1\n" "exec 1 0 2\n" "data 2 0 3 0\n"));
        scene.Start(); scene.Update(1.0f / 60.0f);
        CHECK(std::fabs(vs->GetVariable("v").AsFloat() - 9.0f) < 1e-5f);
    }

    // ---- Blank node fields load (with defaults) instead of failing the graph.
    {
        Scene scene;
        GameObject* self = scene.CreateGameObject("Hero");
        auto* vs = self->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vs->LoadFromText("node 1 OnStart\nnode 2 SetVar\ndefault 2 0 3\nnode 3 Key\nnode 4 Timer\n"
                               "entry OnStart 1\nexec 1 0 2\n", &err));
        CHECK(err.empty());
        scene.Start(); scene.Update(1.0f / 60.0f);
        CHECK(std::fabs(vs->GetVariable("x").AsFloat() - 3.0f) < 1e-5f);
    }

    // ---- Every node type: builds with no arguments and runs unwired.
    {
        const char* types[] = {
            "Abs","Add","AddForce","AddImpulse","AddVar","And","AnimEventName","AnimSetBool","AnimSetFloat","AnimState",
            "AnimTrigger","Approx","Atan2","AxisX","AxisY","Between","Branch","Chance","Clamp","Compare","Concat","Const",
            "Contains","Cooldown","Cos","DeltaAngle","DeltaTime","DistanceTo","Div","EaseIn","EaseInOut","EaseOut",
            "Equals","Exp","FlipFlop","Floor","GetPosition","GetVar","GetX","GetY","GetZ","HasVar","IkHand","IkLook",
            "InverseLerp","Key","KeyDown","KeyUp","Lerp","Log","LogError","LogWarn","LookAt","Lower","MakeVec3","Max",
            "Min","Mod","MouseButton","MouseButtonDown","MouseX","MouseY","MoveTowards","MoveTowardsValue","Mul","Neg",
            "Not","NotEquals","ObjectExists","ObjectPosition","Once","Or","OtherName","OtherTag","PingPong","PlaySound",
            "Pow","Print","Random","RandomRange","Raycast","Remap","Repeat","Repeat01","Rotate","Round","Select",
            "SelfName","Sequence","SetActive","SetPosition","SetRotation","SetScale","SetText","SetVar","SetVelocity",
            "Sign","Sin","SmoothStep","Spawn","Sqrt","Sub","Tan","TextLength","Time","Timer","ToFixed","ToString",
            "Toggle","Translate","Upper","Vec3Add","Vec3Distance","Vec3Dot","Vec3Length","Vec3Normalize","Vec3Scale",
            "Vec3Sub","VecX","VecY","VecZ","VelX","VelY","Wait","Xor",
            // Third wave.
            "ForLoop","Gate","DoN","SwitchInt","RandomInt","Ceil","Asin","Acos","Deg2Rad","Rad2Deg","Frac",
            "OneMinus","Saturate","Vec3Cross","Vec3Lerp","Vec3Negate","UpVector","ForwardVector","RightVector",
            "GetScale","GetRotation","SetPositionVec","MoveBy","Rotate3","SetRotation3","SetColor","PlayClip",
            "SetTimeScale","SetBlendShape","GetBlendShape","MouseWheel","TimeScale","FrameCount",
            // Physics queries / contact details.
            "Raycast3D","SphereCast3D","RaycastAll3D","OverlapSphere3D","OverlapBox3D",
            "ContactPoint","ContactNormal","ImpactSpeed"};
        int built = 0, ran = 0;
        for (const char* t : types) {
            std::string e;
            auto node = vs::MakeNode(t, {}, &e);
            if (!node) { std::printf("  cannot build %s: %s\n", t, e.c_str()); continue; }
            ++built;
            const bool exec = node->ExecOutCount() > 0;
            Scene scene;
            GameObject* self = scene.CreateGameObject("Hero");
            auto* vs = self->AddComponent<VisualScriptComponent>();
            std::string g = exec
                ? std::string("node 1 OnUpdate\nnode 2 ") + t + "\nentry OnUpdate 1\nexec 1 0 2\n"
                : std::string("node 1 OnUpdate\nnode 2 SetVar out\nnode 3 ") + t + "\nentry OnUpdate 1\nexec 1 0 2\ndata 2 0 3 0\n";
            std::string err;
            if (!vs->LoadFromText(g, &err)) { std::printf("  cannot load %s: %s\n", t, err.c_str()); continue; }
            scene.Start();
            for (int f = 0; f < 3; ++f) scene.Update(1.0f / 60.0f);
            ++ran;
        }
        CHECK(built == (int)(sizeof(types) / sizeof(types[0])));
        CHECK(ran == built);
    }
    // ---- Third-wave nodes behave.
    auto run = [](const std::string& graph, int frames, GameObject** outSelf = nullptr) {
        auto scene = std::make_unique<Scene>();
        GameObject* self = scene->CreateGameObject("Hero");
        auto* vs = self->AddComponent<VisualScriptComponent>();
        std::string err;
        bool ok = vs->LoadFromText(graph, &err);
        if (!ok) std::printf("  load failed: %s\n", err.c_str());
        scene->Start();
        for (int f = 0; f < frames; ++f) scene->Update(1.0f / 60.0f);
        if (outSelf) *outSelf = self;
        return std::make_pair(std::move(scene), vs);
    };
    {   // For Loop: sum = 1+2+3+4 via the Index output, Done runs once afterwards.
        auto r = run("node 1 OnStart\nentry OnStart 1\n"
                     "node 2 ForLoop\ndefault 2 0 1\ndefault 2 1 4\n"
                     "node 3 GetVar sum\nnode 4 Add\nnode 5 SetVar sum\n"
                     "node 6 SetVar done\ndefault 6 0 true\n"
                     "exec 1 0 2\nexec 2 0 5\nexec 2 1 6\n"
                     "data 4 0 3 0\ndata 4 1 2 0\ndata 5 0 4 0\n", 1);
        CHECK(std::fabs(r.second->GetVariable("sum").AsFloat() - 10.0f) < 1e-4f);
        CHECK(r.second->GetVariable("done").AsBool());
    }
    {   // Do N Times passes 3 of 10 frames; Switch picks pin 2; Gate closed blocks.
        auto r = run("node 1 OnUpdate\nentry OnUpdate 1\n"
                     "node 2 DoN 3\nnode 3 AddVar n\ndefault 3 0 1\n"
                     "exec 1 0 2\nexec 2 0 3\n", 10);
        CHECK(std::fabs(r.second->GetVariable("n").AsFloat() - 3.0f) < 1e-4f);
        auto s2 = run("node 1 OnStart\nentry OnStart 1\n"
                      "node 2 SwitchInt\ndefault 2 0 2\n"
                      "node 3 SetVar hit\ndefault 3 0 0\nnode 4 SetVar hit\ndefault 4 0 2\nnode 5 SetVar hit\ndefault 5 0 99\n"
                      "node 6 Gate\ndefault 6 0 false\nnode 7 SetVar gated\ndefault 7 0 true\n"
                      "exec 1 0 6\nexec 6 0 7\n"
                      "exec 2 0 3\nexec 2 2 4\nexec 2 3 5\n"
                      "node 8 OnStart\nentry OnStart 8\nexec 8 0 2\n", 1);
        CHECK(std::fabs(s2.second->GetVariable("hit").AsFloat() - 2.0f) < 1e-4f);
        CHECK(!s2.second->GetVariable("gated").AsBool());
    }
    {   // Random Int stays inside [min, max] and hits both ends.
        vs::RandomIntNode ri; ri.SetDefault(0, vs::VsValue{2.0f}); ri.SetDefault(1, vs::VsValue{4.0f});
        vs::NodeGraph g; vs::VsContext ctx; bool lo = false, hi = false, inside = true;
        for (int i = 0; i < 400; ++i) {
            float v = ri.Eval(0, g, ctx).AsFloat();
            inside = inside && v >= 2.0f && v <= 4.0f && std::fabs(v - std::round(v)) < 1e-6f;
            lo = lo || v == 2.0f; hi = hi || v == 4.0f;
        }
        CHECK(inside && lo && hi);
    }
    {   // Set Position (Vec3) from Make Vec3 + Vec3 Cross; Set Rotation (3D).
        GameObject* self = nullptr;
        auto r = run("node 1 OnStart\nentry OnStart 1\n"
                     "node 2 MakeVec3\ndefault 2 0 1\nnode 3 MakeVec3\ndefault 3 1 1\n"
                     "node 4 Vec3Cross\nnode 5 SetPositionVec\n"
                     "node 6 SetRotation3\ndefault 6 1 90\n"
                     "data 4 0 2 0\ndata 4 1 3 0\ndata 5 0 4 0\n"
                     "exec 1 0 5\nexec 5 0 6\n", 1, &self);
        Vec3 p = self->transform->localPosition;   // (1,0,0) x (0,1,0) = (0,0,1)
        CHECK(std::fabs(p.x) < 1e-4f && std::fabs(p.y) < 1e-4f && std::fabs(p.z - 1.0f) < 1e-4f);
        Vec3 e = self->transform->localRotation.ToEuler();
        CHECK(std::fabs(e.y - 90.0f) < 0.05f);
    }
    {   // Set Time Scale writes the global scale (restored afterwards).
        auto r = run("node 1 OnStart\nentry OnStart 1\nnode 2 SetTimeScale\ndefault 2 0 0.5\nexec 1 0 2\n", 1);
        CHECK(std::fabs(Time::TimeScale() - 0.5f) < 1e-5f);
        Time::SetTimeScale(1.0f);
    }
    {   // Key nodes resolve key NAMES ("space" used to mean the S key), mouse
        // buttons accept names, and the rigidbody nodes drive a Rigidbody3D.
        CHECK(Input::KeyFromName("space") == ' ');
        CHECK(Input::KeyFromName("Shift") == Input::KeyShift);
        CHECK(Input::KeyFromName("E") == 'e');
        Scene s; s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("K");
        auto* rb = go->AddComponent<Rigidbody3D>();
        auto* vc = go->AddComponent<VisualScriptComponent>();
        CHECK(vc->LoadFromText(
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 SetVar down\nnode 3 KeyDown space\nexec 1 0 2\ndata 2 0 3 0\n"
            "node 4 SetVelocity\ndefault 4 0 1\ndefault 4 1 2\ndefault 4 2 3\nexec 2 0 4\n"
            "node 5 SetVar vz\nnode 6 VelZ\nexec 4 0 5\ndata 5 0 6 0\n"
            "node 7 SetVar mb\nnode 8 MouseButton right\nexec 5 0 7\ndata 7 0 8 0\n"));
        s.Start();
        Input::FeedKeys({' '}); s.Update(0.016f);
        CHECK(vc->GetVariable("down").AsBool());
        Input::FeedKeys({'s'}); s.Update(0.016f);
        CHECK(!vc->GetVariable("down").AsBool());   // S is not Space any more
        CHECK(std::fabs(rb->velocity.z - 3.0f) < 1e-4f && std::fabs(rb->velocity.x - 1.0f) < 1e-4f);
        CHECK(std::fabs(vc->GetVariable("vz").AsFloat() - 3.0f) < 1e-4f);
        CHECK(!vc->GetVariable("mb").AsBool());
        Input::FeedKeys({});
    }
    TEST_MAIN_RESULT();
}
