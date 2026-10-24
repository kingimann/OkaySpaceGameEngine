// Visual scripts doing real gameplay jobs end to end: real scenes, real input
// (Input::FeedKeys), real 3D physics, save -> reload. Each block is one
// behaviour a user builds in the Visual Script editor.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scripting/NextScriptVM.hpp"
#include "okay/VisualScript/VsApi.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;

static std::vector<std::string> g_log;

static VisualScriptComponent* Attach(GameObject* go, const std::string& src) {
    auto* v = go->AddComponent<VisualScriptComponent>();
    std::string err;
    const bool ok = v->LoadFromText(src, &err);
    CHECK(ok);
    if (!ok) std::cerr << "  parse error: " << err << "\n";
    return v;
}
static bool Logged(const std::string& part) {
    for (const auto& l : g_log) if (l.find(part) != std::string::npos) return true;
    return false;
}
static void Tick(Scene& s, int frames, float dt = 1.0f / 60.0f) {
    for (int i = 0; i < frames; ++i) s.Update(dt);
}

int main() {
    RUN_SUITE("vs_acceptance");
    Log::sink = [](Log::Level, const std::string& m) { g_log.push_back(m); };

    // ---- A: On Update -> Axis X * speed * Delta Time -> Translate ----------
    {
        const char* move = R"VS(
node 0 OnUpdate
node 1 AxisX
node 2 Const 5
node 3 Mul
node 4 DeltaTime
node 5 Mul
node 6 Translate
data 3 0 1 0
data 3 1 2 0
data 5 0 3 0
data 5 1 4 0
data 6 0 5 0
exec 0 0 6
entry OnUpdate 0
)VS";
        Scene s("A"); GameObject* p = s.CreateGameObject("P"); Attach(p, move);
        s.Start();
        for (int i = 0; i < 10; ++i) { Input::FeedKeys({'d'}); s.Update(0.1f); }
        CHECK_NEAR(p->transform->localPosition.x, 5.0f, 1e-3f);
        for (int i = 0; i < 10; ++i) { Input::FeedKeys({}); s.Update(0.1f); }
        CHECK_NEAR(p->transform->localPosition.x, 5.0f, 1e-3f);
        for (int i = 0; i < 5; ++i) { Input::FeedKeys({'a'}); s.Update(0.1f); }
        CHECK_NEAR(p->transform->localPosition.x, 2.5f, 1e-3f);
        Input::FeedKeys({}); Input::FeedKeys({});
    }

    // ---- C: Start -> Set Var -> Add -> Print --------------------------------
    {
        Scene s("C"); GameObject* go = s.CreateGameObject("C");
        auto* v = Attach(go, R"VS(
node 0 OnStart
node 1 Const 10
node 2 SetVar score
node 3 GetVar score
node 4 Const 5
node 5 Add
node 6 SetVar score
node 7 GetVar score
node 8 Print
data 2 0 1 0
data 5 0 3 0
data 5 1 4 0
data 6 0 5 0
data 8 0 7 0
exec 0 0 2
exec 2 0 6
exec 6 0 8
entry OnStart 0
)VS");
        g_log.clear(); s.Start(); Tick(s, 1);
        CHECK_NEAR(v->GetVariable("score").AsFloat(), 15.0f, 1e-6f);
        CHECK(!g_log.empty() && g_log[0] == "15");
    }

    // ---- G: Graph A -> Send Event -> Graph B --------------------------------
    {
        Scene s("G"); GameObject* a = s.CreateGameObject("A"); GameObject* b = s.CreateGameObject("B");
        Attach(a, "node 0 OnStart\nnode 1 SendEvent Ping\nnode 2 Const \"B\"\nnode 3 Const 42\n"
                  "data 1 0 2 0\ndata 1 1 3 0\nexec 0 0 1\nentry OnStart 0\n");
        auto* vb = Attach(b, "node 0 OnEvent Ping\nnode 1 EventValue\nnode 2 SetVar got\nnode 3 OtherName\nnode 4 SetVar from\n"
                             "data 2 0 1 0\ndata 4 0 3 0\nexec 0 0 2\nexec 2 0 4\nentry OnEvent:Ping 0\n");
        s.Start(); Tick(s, 1);
        CHECK_NEAR(vb->GetVariable("got").AsFloat(), 42.0f, 1e-6f);
        CHECK(vb->GetVariable("from").AsString() == "A");
    }

    // ---- F: Start -> For Loop -> Spawn (exact count) -------------------------
    const std::string prefab = "vs_acceptance_bullet.okayprefab";
    {
        Scene t("T"); GameObject* b = t.CreateGameObject("Bullet"); b->tag = "Bullet";
        CHECK(SceneSerializer::SaveObjectToFile(*b, prefab));
    }
    auto countTag = [](Scene& s, const char* tag) {
        int n = 0; for (const auto& g : s.Objects()) if (g->tag == tag) ++n; return n;
    };
    {
        Scene s("F"); GameObject* go = s.CreateGameObject("Spawner");
        Attach(go, "node 0 OnStart\nnode 1 ForLoop\nnode 2 Spawn " + prefab + "\nnode 3 Const 2\nnode 4 Mul\n"
                   "default 1 0 0\ndefault 1 1 9\ndata 4 0 1 0\ndata 4 1 3 0\ndata 2 0 4 0\n"
                   "exec 0 0 1\nexec 1 0 2\nentry OnStart 0\n");
        s.Start(); Tick(s, 1);
        CHECK(countTag(s, "Bullet") == 10);
        float maxX = -1; for (const auto& g : s.Objects()) if (g->tag == "Bullet") maxX = std::max(maxX, g->transform->Position().x);
        CHECK_NEAR(maxX, 18.0f, 1e-4f);
    }

    // ---- Runaway loops are stopped AND reported, then the graph keeps working --
    {
        Scene s("Loop"); GameObject* go = s.CreateGameObject("Looper");
        auto* v = Attach(go, R"VS(
node 0 OnUpdate
node 1 ForLoop
node 2 AddVar n
node 3 Const 1
node 4 AddVar frames
default 1 0 0
default 1 1 100000000
data 2 0 3 0
data 4 0 3 0
exec 0 0 1
exec 1 0 2
exec 1 1 4
entry OnUpdate 0
)VS");
        g_log.clear();
        s.Start();
        const auto t0 = std::chrono::steady_clock::now();
        Tick(s, 2);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(Logged("runaway loop"));
        CHECK(Logged("Looper"));                                    // says which object
        CHECK(Logged("ForLoop") || Logged("AddVar"));               // and which node
        CHECK(!v->Graph()->LastError().empty());
        CHECK(v->Graph()->LastErrorNode() >= 0);
        // Each frame was stopped at the budget (not 100M iterations), and the
        // "Done" branch never ran because the event was cut off.
        CHECK(v->GetVariable("n").AsFloat() < 2.0f * vs::NodeGraph::kMaxStepsPerEvent);
        CHECK(v->GetVariable("frames").AsFloat() == 0.0f);
        CHECK(ms < 5000.0);
        // Logged once, counted after.
        int reports = 0; for (const auto& l : g_log) if (l.find("runaway loop") != std::string::npos) ++reports;
        CHECK(reports == 1);
        CHECK(v->Graph()->ErrorCount() >= 2);
    }
    // Nested loops share one budget (10k x 10k would be 100M steps).
    {
        Scene s("Nested"); GameObject* go = s.CreateGameObject("Nest");
        Attach(go, R"VS(
node 0 OnStart
node 1 ForLoop
node 2 ForLoop
node 3 AddVar n
node 4 Const 1
default 1 0 0
default 1 1 9999
default 2 0 0
default 2 1 9999
data 3 0 4 0
exec 0 0 1
exec 1 0 2
exec 2 0 3
entry OnStart 0
)VS");
        g_log.clear();
        const auto t0 = std::chrono::steady_clock::now();
        s.Start(); Tick(s, 1);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(Logged("runaway loop"));
        CHECK(ms < 5000.0);
    }
    // A data wire into its own input is reported, not a crash / hang.
    {
        Scene s("Cycle"); GameObject* go = s.CreateGameObject("Cyc");
        auto* v = Attach(go, "node 0 OnStart\nnode 1 Add\nnode 2 SetVar x\nnode 3 Const 1\n"
                             "data 1 0 1 0\ndata 1 1 3 0\ndata 2 0 1 0\nexec 0 0 2\nentry OnStart 0\n");
        g_log.clear(); s.Start(); Tick(s, 1);
        CHECK(Logged("loops back into itself"));
        CHECK(!v->Graph()->LastError().empty());
    }
    // Two graphs sending each other events forever: dropped with an error, no crash.
    {
        Scene s("PingPong"); GameObject* a = s.CreateGameObject("A"); GameObject* b = s.CreateGameObject("B");
        Attach(a, "node 0 OnStart\nnode 1 SendEvent Ping\nnode 2 Const \"B\"\ndata 1 0 2 0\nexec 0 0 1\nentry OnStart 0\n"
                  "node 3 OnEvent Pong\nnode 4 SendEvent Ping\nnode 5 Const \"B\"\ndata 4 0 5 0\nexec 3 0 4\nentry OnEvent:Pong 3\n");
        Attach(b, "node 0 OnEvent Ping\nnode 1 SendEvent Pong\nnode 2 Const \"A\"\ndata 1 0 2 0\nexec 0 0 1\nentry OnEvent:Ping 0\n");
        g_log.clear(); s.Start(); Tick(s, 1);
        CHECK(Logged("endless event loop"));
    }

    // ---- Wait: pauses the chain, resumes on its own (also under On Start) -----
    {
        Scene s("Wait"); GameObject* go = s.CreateGameObject("W");
        auto* v = Attach(go, "node 0 OnStart\nnode 1 Wait 0.5\nnode 2 SetVar done\nnode 3 Const 1\n"
                             "data 2 0 3 0\nexec 0 0 1\nexec 1 0 2\nentry OnStart 0\n");
        s.Start();
        Tick(s, 1, 0.1f);
        CHECK(v->GetVariable("done").AsFloat() == 0.0f);
        Tick(s, 3, 0.1f);
        CHECK(v->GetVariable("done").AsFloat() == 0.0f);
        Tick(s, 3, 0.1f);
        CHECK(v->GetVariable("done").AsFloat() == 1.0f);
    }
    {   // Under On Update: one copy while counting, then passes every frame.
        Scene s("WaitU"); GameObject* go = s.CreateGameObject("W");
        auto* v = Attach(go, "node 0 OnUpdate\nnode 1 Wait 0.3\nnode 2 AddVar n\nnode 3 Const 1\n"
                             "data 2 0 3 0\nexec 0 0 1\nexec 1 0 2\nentry OnUpdate 0\n");
        s.Start();
        Tick(s, 3, 0.1f);
        CHECK(v->GetVariable("n").AsFloat() == 0.0f);
        Tick(s, 2, 0.1f);
        const float after = v->GetVariable("n").AsFloat();
        CHECK(after >= 1.0f && after <= 3.0f);
        Tick(s, 10, 0.1f);
        CHECK_NEAR(v->GetVariable("n").AsFloat(), after + 10.0f, 1e-4f);
    }

    // ---- "OnKey:space" spelling is accepted; On Key fires once per press ------
    {
        Scene s("Key"); GameObject* go = s.CreateGameObject("K");
        auto* v = Attach(go, "node 0 OnKey:space\nnode 1 AddVar presses\nnode 2 Const 1\n"
                             "data 1 0 2 0\nexec 0 0 1\nentry OnKey:space 0\n");
        s.Start();
        Input::FeedKeys({}); Tick(s, 1);
        Input::FeedKeys({' '}); Tick(s, 1);
        Input::FeedKeys({' '}); Tick(s, 1);
        Input::FeedKeys({}); Tick(s, 1);
        Input::FeedKeys({' '}); Tick(s, 1);
        CHECK(v->GetVariable("presses").AsFloat() == 2.0f);
        Input::FeedKeys({}); Input::FeedKeys({});
    }

    // ---- On Fixed Update: 50 steps a second, Delta Time = 0.02 ----------------
    {
        Scene s("Fixed"); GameObject* go = s.CreateGameObject("F");
        auto* v = Attach(go, "node 0 OnFixedUpdate\nnode 1 AddVar steps\nnode 2 Const 1\nnode 3 DeltaTime\nnode 4 SetVar dt\n"
                             "data 1 0 2 0\ndata 4 0 3 0\nexec 0 0 1\nexec 1 0 4\nentry OnFixedUpdate 0\n");
        s.Start();
        Tick(s, 60, 1.0f / 60.0f);
        CHECK(std::abs(v->GetVariable("steps").AsFloat() - 50.0f) <= 1.0f);
        CHECK_NEAR(v->GetVariable("dt").AsFloat(), 0.02f, 1e-6f);
    }

    // ---- On Enable / On Disable when the object is switched off and on ---------
    {
        Scene s("Act"); GameObject* go = s.CreateGameObject("Lamp");
        auto* v = Attach(go, "node 0 OnEnable\nnode 1 AddVar enabled\nnode 2 Const 1\n"
                             "node 3 OnDisable\nnode 4 AddVar disabled\n"
                             "data 1 0 2 0\ndata 4 0 2 0\nexec 0 0 1\nexec 3 0 4\nentry OnEnable 0\nentry OnDisable 3\n");
        s.Start(); Tick(s, 2);
        CHECK(v->GetVariable("enabled").AsFloat() == 1.0f);   // once when it started
        CHECK(v->GetVariable("disabled").AsFloat() == 0.0f);
        go->active = false; Tick(s, 2);
        CHECK(v->GetVariable("disabled").AsFloat() == 1.0f);
        go->active = true; Tick(s, 2);
        CHECK(v->GetVariable("enabled").AsFloat() == 2.0f);
        CHECK(v->GetVariable("disabled").AsFloat() == 1.0f);
    }
    // ... and a text script on the same kind of object gets OnEnable / OnDisable too.
    {
        Scene s("ActScript"); GameObject* go = s.CreateGameObject("Lamp");
        auto* sc = go->AddComponent<ScriptComponent>();
        CHECK(sc->LoadSource("var on = 0\nvar off = 0\nfunction OnEnable() { on = on + 1 }\nfunction OnDisable() { off = off + 1 }\n"));
        s.Start(); Tick(s, 2);
        go->active = false; Tick(s, 2);
        go->active = true; Tick(s, 2);
        CHECK(sc->VM()->GetGlobal("on").AsFloat() == 2.0f);
        CHECK(sc->VM()->GetGlobal("off").AsFloat() == 1.0f);
    }
    // Switched off, then destroyed: OnDisable runs once, not twice.
    {
        Scene s("OffThenDestroy"); GameObject* go = s.CreateGameObject("Lamp");
        auto* sc = go->AddComponent<ScriptComponent>();
        CHECK(sc->LoadSource("function OnDisable() { print(\"lamp-off\") }\nfunction OnDestroy() { print(\"lamp-gone\") }\n"));
        s.Start(); Tick(s, 2);
        g_log.clear();
        go->active = false; Tick(s, 2);
        s.Destroy(go); Tick(s, 2);
        int offs = 0; for (const auto& l : g_log) if (l.find("lamp-off") != std::string::npos) ++offs;
        CHECK(offs == 1);
        CHECK(Logged("lamp-gone"));
    }

    // ---- On Destroy runs before the object goes, and may spawn things ----------
    {
        Scene s("Destroy"); GameObject* go = s.CreateGameObject("Crate");
        Attach(go, "node 0 OnDestroy\nnode 1 Spawn " + prefab + "\nnode 2 Print\ndefault 2 0 \"crate gone\"\n"
                   "exec 0 0 1\nexec 1 0 2\nentry OnDestroy 0\n"
                   "node 3 OnKey space\nnode 4 Destroy\nexec 3 0 4\nentry OnKey:space 3\n");
        s.Start(); Tick(s, 1);
        g_log.clear();
        Input::FeedKeys({}); Tick(s, 1);
        Input::FeedKeys({' '}); Tick(s, 1);
        Input::FeedKeys({}); Tick(s, 2);
        CHECK(s.Find("Crate") == nullptr);
        CHECK(countTag(s, "Bullet") == 1);
        CHECK(Logged("crate gone"));
        Input::FeedKeys({});
    }

    // ---- Every graph on an object survives save -> reload ---------------------
    {
        Scene s("Two"); GameObject* g = s.CreateGameObject("G");
        Attach(g, "node 0 OnStart\nnode 1 Print\ndefault 1 0 \"graph-one\"\nexec 0 0 1\nentry OnStart 0\n");
        Attach(g, "node 0 OnStart\nnode 1 Print\ndefault 1 0 \"graph-two\"\nexec 0 0 1\nentry OnStart 0\n");
        CHECK(SceneSerializer::SaveToFile(s, "vs_acceptance_two.okayscene"));
        Scene r("R");
        CHECK(SceneSerializer::LoadFromFile(r, "vs_acceptance_two.okayscene"));
        GameObject* rg = r.Find("G");
        CHECK(rg && rg->GetComponents<VisualScriptComponent>().size() == 2);
        g_log.clear(); r.Start(); Tick(r, 1);
        CHECK(Logged("graph-one") && Logged("graph-two"));
    }

    // ---- Nothing runs while scripts are off (the editor outside Play) ---------
    {
        Scene s("Edit"); s.runScripts = false; GameObject* go = s.CreateGameObject("E");
        auto* v = Attach(go, "node 0 OnLateUpdate\nnode 1 AddVar late\nnode 2 Const 1\n"
                             "node 3 OnFixedUpdate\nnode 4 AddVar fixed\n"
                             "data 1 0 2 0\ndata 4 0 2 0\nexec 0 0 1\nexec 3 0 4\nentry OnLateUpdate 0\nentry OnFixedUpdate 3\n");
        s.Start(); Tick(s, 10);
        CHECK(v->GetVariable("late").AsFloat() == 0.0f);
        CHECK(v->GetVariable("fixed").AsFloat() == 0.0f);
        s.runScripts = true; Tick(s, 3);
        CHECK(v->GetVariable("late").AsFloat() == 3.0f);
    }

    // ==== Object references =====================================================
    auto collisionScene = [](Scene& s, GameObject*& player, GameObject*& enemy) {
        enemy = s.CreateGameObject("Enemy"); enemy->tag = "Enemy";
        enemy->AddComponent<BoxCollider3D>()->size = {4, 1, 4};
        enemy->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        player = s.CreateGameObject("Player"); player->tag = "Player";
        player->transform->localPosition = {0, 3, 0};
        player->AddComponent<SphereCollider3D>()->radius = 0.5f;
        player->AddComponent<Rigidbody3D>();
    };
    // ---- B: Collision -> Other's tag is Enemy -> Destroy the Enemy (graph on Player) --
    {
        Scene s("B"); GameObject *p, *e; collisionScene(s, p, e);
        Attach(p, R"VS(
node 0 OnCollision
node 1 Other
node 2 GetTag
node 3 Const "Enemy"
node 4 Equals
node 5 Branch
node 6 Destroy
node 7 AddVar hits
node 8 Const 1
data 2 0 1 0
data 4 0 2 0
data 4 1 3 0
data 5 0 4 0
data 6 0 1 0
data 7 0 8 0
exec 0 0 5
exec 5 0 6
exec 6 0 7
entry OnCollision 0
)VS");
        const auto pid = p->InstanceId(), eid = e->InstanceId();
        s.Start(); Tick(s, 120);
        CHECK(GameObject::FromInstanceId(eid) == nullptr);   // the Enemy is gone
        CHECK(GameObject::FromInstanceId(pid) != nullptr);   // the Player is not
        // Destroying another object doesn't end the chain: the step after it ran.
        CHECK(p->GetComponent<VisualScriptComponent>()->GetVariable("hits").AsFloat() >= 1.0f);
    }
    // ... and a non-matching tag destroys nothing.
    {
        Scene s("B2"); GameObject *p, *e; collisionScene(s, p, e); e->tag = "Ground";
        Attach(p, "node 0 OnCollision\nnode 1 Other\nnode 2 GetTag\nnode 3 Const \"Enemy\"\nnode 4 Equals\nnode 5 Branch\nnode 6 Destroy\n"
                  "data 2 0 1 0\ndata 4 0 2 0\ndata 4 1 3 0\ndata 5 0 4 0\ndata 6 0 1 0\nexec 0 0 5\nexec 5 0 6\nentry OnCollision 0\n");
        const auto pid = p->InstanceId(), eid = e->InstanceId();
        s.Start(); Tick(s, 120);
        CHECK(GameObject::FromInstanceId(eid) != nullptr);
        CHECK(GameObject::FromInstanceId(pid) != nullptr);
    }

    // ---- D: Key press -> Spawn -> Set Position of the SPAWNED object -----------
    {
        Scene s("D"); GameObject* sp = s.CreateGameObject("Spawner"); sp->transform->localPosition = {1, 1, 1};
        Attach(sp, "node 0 OnKey space\nnode 1 Spawn " + prefab + "\nnode 2 SetPosition\n"
                   "default 2 0 7\ndefault 2 1 8\ndefault 2 2 9\ndata 2 3 1 0\n"
                   "exec 0 0 1\nexec 1 0 2\nentry OnKey:space 0\n");
        s.Start();
        Input::FeedKeys({}); Tick(s, 1);
        Input::FeedKeys({' '}); Tick(s, 1);
        Input::FeedKeys({' '}); Tick(s, 1);   // held: still one
        Input::FeedKeys({}); Tick(s, 1);
        CHECK(countTag(s, "Bullet") == 1);
        Vec3 bp{-1, -1, -1};
        for (const auto& g : s.Objects()) if (g->tag == "Bullet") bp = g->transform->Position();
        CHECK_NEAR(bp.x, 7.0f, 1e-4f); CHECK_NEAR(bp.y, 8.0f, 1e-4f); CHECK_NEAR(bp.z, 9.0f, 1e-4f);
        CHECK_NEAR(sp->transform->localPosition.x, 1.0f, 1e-6f);   // the spawner didn't move
        CHECK_NEAR(sp->transform->localPosition.y, 1.0f, 1e-6f);
        Input::FeedKeys({' '}); Tick(s, 1); Input::FeedKeys({}); Tick(s, 1);
        CHECK(countTag(s, "Bullet") == 2);
        Input::FeedKeys({});
    }

    // ---- A targeted node's other inputs are still read from the graph's own object --
    {
        Scene s("Inputs"); GameObject* me = s.CreateGameObject("Me"); GameObject* box = s.CreateGameObject("Box");
        me->transform->localPosition = {3, 0, 0};
        box->transform->localPosition = {10, 0, 0};
        // Box.position = (my X, 2, 0): Get X has no Target, so it reads Me (3), not Box (10).
        Attach(me, "node 0 OnStart\nnode 1 SetPosition\nnode 2 GetX\nnode 3 FindObject Box\n"
                   "default 1 1 2\ndefault 1 2 0\ndata 1 0 2 0\ndata 1 3 3 0\nexec 0 0 1\nentry OnStart 0\n");
        s.Start(); Tick(s, 1);
        CHECK_NEAR(box->transform->localPosition.x, 3.0f, 1e-5f);
        CHECK_NEAR(box->transform->localPosition.y, 2.0f, 1e-5f);
        CHECK_NEAR(me->transform->localPosition.x, 3.0f, 1e-5f);
    }
    // ---- Get Position / Get Name with a Target; a typed name works as Target too ----
    {
        Scene s("Read"); GameObject* me = s.CreateGameObject("Me"); GameObject* farObj = s.CreateGameObject("Far");
        farObj->transform->localPosition = {5, 6, 7}; farObj->tag = "Loot";
        auto* v = Attach(me, "node 0 OnStart\nnode 1 GetPosition\nnode 2 SetVar pos\nnode 3 GetTag\nnode 4 SetVar tag\n"
                             "default 1 0 \"Far\"\ndefault 3 0 \"Far\"\ndata 2 0 1 0\ndata 4 0 3 0\n"
                             "exec 0 0 2\nexec 2 0 4\nentry OnStart 0\n");
        s.Start(); Tick(s, 1);
        CHECK_NEAR(v->GetVariable("pos").AsVec3().z, 7.0f, 1e-5f);
        CHECK(v->GetVariable("tag").AsString() == "Loot");
    }
    // ---- A destroyed target: reported, node skipped, chain goes on, no crash ----
    {
        Scene s("Gone"); GameObject* me = s.CreateGameObject("Me"); GameObject* t = s.CreateGameObject("Temp");
        auto* v = Attach(me, R"VS(
node 0 OnStart
node 1 FindObject Temp
node 2 SetVar target
data 2 0 1 0
exec 0 0 2
entry OnStart 0
node 10 OnKey space
node 11 GetVar target
node 12 Translate
node 13 AddVar after
node 14 Const 1
node 15 Branch
node 16 SetVar alive
default 12 0 1
data 12 3 11 0
data 13 0 14 0
data 15 0 11 0
data 16 0 14 0
exec 10 0 15
exec 15 0 16
exec 15 1 12
exec 12 0 13
entry OnKey:space 10
)VS");
        s.Start(); Tick(s, 1);
        CHECK(v->GetVariable("target").IsObject());
        CHECK(v->GetVariable("target").AsString() == "Temp");   // reads as its name
        s.Destroy(t); Tick(s, 1);
        g_log.clear();
        Input::FeedKeys({}); Tick(s, 1);
        Input::FeedKeys({' '}); Tick(s, 1);
        Input::FeedKeys({}); Tick(s, 1);
        CHECK(v->GetVariable("alive").AsFloat() == 0.0f);   // Branch on a destroyed object is false
        CHECK(v->GetVariable("after").AsFloat() == 1.0f);   // the chain carried on past the skipped node
        CHECK(Logged("no longer exists"));
        CHECK_NEAR(me->transform->localPosition.x, 0.0f, 1e-6f);   // it did NOT fall back to moving itself
        Input::FeedKeys({});
    }
    // ---- Raycast 3D (outs: hit, point, normal, distance, object): the object is a real object ----
    {
        Scene s("Ray"); GameObject* me = s.CreateGameObject("Eye");
        me->AddComponent<SphereCollider3D>()->radius = 0.5f;            // its own collider is ignored
        GameObject* wall = s.CreateGameObject("Wall"); wall->transform->localPosition = {0, 0, -10};
        wall->AddComponent<BoxCollider3D>()->size = {4, 4, 1};
        auto* v = Attach(me, R"VS(
node 0 OnStart
node 1 Raycast3D
node 2 SetVar hit
node 3 SetVar what
node 4 SetVar dist
node 5 MakeVec3
node 6 SetVar point
default 1 0 0
default 5 2 -1
data 1 1 5 0
data 2 0 1 0
data 3 0 1 4
data 4 0 1 3
data 6 0 1 1
exec 0 0 2
exec 2 0 3
exec 3 0 4
exec 4 0 6
entry OnStart 0
)VS");
        s.Start(); Tick(s, 1);
        CHECK(v->GetVariable("hit").AsBool());
        CHECK(v->GetVariable("what").IsObject());
        CHECK(v->GetVariable("what").AsString() == "Wall");
        CHECK_NEAR(v->GetVariable("dist").AsFloat(), 9.5f, 0.05f);
        CHECK_NEAR(v->GetVariable("point").AsVec3().z, -9.5f, 0.05f);
    }
    // ---- Find With Tag gives an object: Send Event to it; compare objects --------
    {
        Scene s("Tag"); GameObject* me = s.CreateGameObject("Me"); GameObject* boss = s.CreateGameObject("Boss"); boss->tag = "Boss";
        auto* vb = Attach(boss, "node 0 OnEvent Hit\nnode 1 EventValue\nnode 2 SetVar dmg\ndata 2 0 1 0\nexec 0 0 2\nentry OnEvent:Hit 0\n");
        auto* vm = Attach(me, R"VS(
node 0 OnStart
node 1 FindWithTag Boss
node 2 SendEvent Hit
node 3 Const 25
node 4 FindObject Boss
node 5 Equals
node 6 SetVar same
node 7 Const "Boss"
node 8 Equals
node 9 SetVar sameName
data 2 0 1 0
data 2 1 3 0
data 5 0 1 0
data 5 1 4 0
data 6 0 5 0
data 8 0 1 0
data 8 1 7 0
data 9 0 8 0
exec 0 0 2
exec 2 0 6
exec 6 0 9
entry OnStart 0
)VS");
        s.Start(); Tick(s, 1);
        CHECK_NEAR(vb->GetVariable("dmg").AsFloat(), 25.0f, 1e-6f);
        CHECK(vm->GetVariable("same").AsBool());
        CHECK(vm->GetVariable("sameName").AsBool());
    }
    // ---- Object graphs survive save -> reload and still hit the right object ----
    {
        Scene s("SaveB"); GameObject *p, *e; collisionScene(s, p, e);
        Attach(p, "node 0 OnCollision\nnode 1 Other\nnode 2 Destroy\ndata 2 0 1 0\nexec 0 0 2\nentry OnCollision 0\n");
        CHECK(SceneSerializer::SaveToFile(s, "vs_acceptance_b.okayscene"));
        Scene r("R");
        CHECK(SceneSerializer::LoadFromFile(r, "vs_acceptance_b.okayscene"));
        GameObject* rp = r.Find("Player"); GameObject* re = r.Find("Enemy");
        CHECK(rp && re);
        const auto pid = rp ? rp->InstanceId() : 0, eid = re ? re->InstanceId() : 0;
        r.Start(); Tick(r, 120);
        CHECK(GameObject::FromInstanceId(eid) == nullptr);
        CHECK(GameObject::FromInstanceId(pid) != nullptr);
        std::remove("vs_acceptance_b.okayscene");
    }

    // ==== Functions =============================================================
    // ---- E: Start -> Call Function -> Return -> Print ---------------------------
    const std::string doubleFn = R"VS(
node 10 FunctionStart "Double(x: Number) -> result: Number"
node 11 Mul
node 12 Const 2
node 13 Return Double
data 11 0 10 0
data 11 1 12 0
data 13 0 11 0
exec 10 0 13
)VS";
    {
        Scene s("E"); GameObject* go = s.CreateGameObject("Calc");
        Attach(go, doubleFn + R"VS(
node 0 OnStart
node 1 CallFunction Double
node 2 Const 21
node 3 Print
data 1 0 2 0
data 3 0 1 0
exec 0 0 1
exec 1 0 3
entry OnStart 0
)VS");
        g_log.clear(); s.Start(); Tick(s, 1);
        CHECK(Logged("42"));
        CHECK(g_log.size() == 1);   // and nothing else (no errors)
    }
    // ---- Recursion: Factorial(5) = 120 ---------------------------------------------
    {
        Scene s("Fact"); GameObject* go = s.CreateGameObject("Calc");
        auto* v = Attach(go, R"VS(
node 10 FunctionStart "Fact(n: Number) -> r: Number"
node 11 Compare <=
node 12 Const 1
node 13 Branch
node 14 Return Fact
node 15 Sub
node 16 CallFunction Fact
node 17 Mul
node 18 Return Fact
data 11 0 10 0
data 11 1 12 0
data 13 0 11 0
data 14 0 12 0
data 15 0 10 0
data 15 1 12 0
data 16 0 15 0
data 17 0 10 0
data 17 1 16 0
data 18 0 17 0
exec 10 0 13
exec 13 0 14
exec 13 1 16
exec 16 0 18
node 0 OnStart
node 1 CallFunction Fact
node 2 Const 5
node 3 SetVar out
data 1 0 2 0
data 3 0 1 0
exec 0 0 1
exec 1 0 3
entry OnStart 0
)VS");
        s.Start(); Tick(s, 1);
        CHECK_NEAR(v->GetVariable("out").AsFloat(), 120.0f, 1e-3f);
    }
    // ---- Endless recursion is refused and reported, not a crash ----------------
    {
        Scene s("Rec"); GameObject* go = s.CreateGameObject("Loop");
        auto* v = Attach(go, R"VS(
node 10 FunctionStart "Forever()"
node 11 CallFunction Forever
exec 10 0 11
node 0 OnStart
node 1 CallFunction Forever
node 2 SetVar after
node 3 Const 1
data 2 0 3 0
exec 0 0 1
exec 1 0 2
entry OnStart 0
)VS");
        g_log.clear(); s.Start(); Tick(s, 1);
        CHECK(Logged("more than 64 deep"));
        CHECK(v->GetVariable("after").AsFloat() == 1.0f);   // the event carried on after the refused call
    }
    // ---- Return inside a loop ends the whole function, at once ------------------
    {
        Scene s("Find"); GameObject* go = s.CreateGameObject("Calc");
        auto* v = Attach(go, R"VS(
node 10 FunctionStart "FirstOver(limit: Number) -> found: Number"
node 11 ForLoop
node 12 AddVar steps
node 13 Const 1
node 14 Compare >
node 15 Branch
node 16 Return FirstOver
default 11 0 0
default 11 1 100
data 12 0 13 0
data 14 0 11 0
data 14 1 10 0
data 15 0 14 0
data 16 0 11 0
exec 10 0 11
exec 11 0 12
exec 12 0 15
exec 15 0 16
node 0 OnStart
node 1 CallFunction FirstOver
node 2 Const 3
node 3 SetVar out
data 1 0 2 0
data 3 0 1 0
exec 0 0 1
exec 1 0 3
entry OnStart 0
)VS");
        s.Start(); Tick(s, 1);
        CHECK_NEAR(v->GetVariable("out").AsFloat(), 4.0f, 1e-4f);
        CHECK_NEAR(v->GetVariable("steps").AsFloat(), 5.0f, 1e-4f);   // indexes 0..4, then it returned
    }
    // ---- Locals live for one call; a call can use many functions ---------------
    {
        Scene s("Loc"); GameObject* go = s.CreateGameObject("Calc");
        auto* v = Attach(go, doubleFn + R"VS(
node 20 FunctionStart "Quad(v: Number) -> q: Number"
node 21 CallFunction Double
node 22 SetLocal half
node 23 CallFunction Double
node 24 GetLocal half
node 25 Return Quad
data 21 0 20 0
data 22 0 21 0
data 23 0 24 0
data 25 0 23 0
exec 20 0 21
exec 21 0 22
exec 22 0 23
exec 23 0 25
node 0 OnStart
node 1 CallFunction Quad
node 2 Const 5
node 3 SetVar out
data 1 0 2 0
data 3 0 1 0
exec 0 0 1
exec 1 0 3
entry OnStart 0
)VS");
        s.Start(); Tick(s, 1);
        CHECK_NEAR(v->GetVariable("out").AsFloat(), 20.0f, 1e-4f);
        CHECK(!v->GetVariable("half").AsBool());   // the local didn't leak into the graph's variables
    }
    // ---- Misuse is reported at runtime too --------------------------------------
    {
        Scene s("Bad"); GameObject* go = s.CreateGameObject("Bad");
        Attach(go, R"VS(
node 10 FunctionStart "Slow()"
node 11 Delay 1
node 12 Print
default 12 0 "after delay"
exec 10 0 11
exec 11 0 12
node 0 OnStart
node 1 CallFunction Slow
node 2 CallFunction Missing
node 3 Print
default 3 0 "event done"
exec 0 0 1
exec 1 0 2
exec 2 0 3
entry OnStart 0
)VS");
        g_log.clear(); s.Start(); Tick(s, 3, 1.0f);
        CHECK(Logged("can't pause inside a function"));
        CHECK(Logged("no function named 'Missing'"));
        CHECK(Logged("event done"));
        CHECK(!Logged("after delay"));
    }
    // ---- Function graphs survive save -> reload --------------------------------
    {
        Scene s("SaveE"); GameObject* go = s.CreateGameObject("Calc");
        Attach(go, doubleFn + "node 0 OnStart\nnode 1 CallFunction Double\nnode 2 Const 4\nnode 3 Print\n"
                              "data 1 0 2 0\ndata 3 0 1 0\nexec 0 0 1\nexec 1 0 3\nentry OnStart 0\n");
        CHECK(SceneSerializer::SaveToFile(s, "vs_acceptance_fn.okayscene"));
        Scene r("R");
        CHECK(SceneSerializer::LoadFromFile(r, "vs_acceptance_fn.okayscene"));
        g_log.clear(); r.Start(); Tick(r, 1);
        CHECK(Logged("8"));
        std::remove("vs_acceptance_fn.okayscene");
    }

    // ==== Shared engine API: the nodes C#-style scripts' bindings generate ======
    // ---- The same binding from a graph and from a script gives the same result --
    {
        Scene s("Api1");
        auto ball = [&](const char* name, float x) {
            GameObject* b = s.CreateGameObject(name);
            b->transform->localPosition = {x, 0, 0};
            auto* rb = b->AddComponent<Rigidbody3D>();
            rb->gravityScale = 0.0f;
            rb->mass = 2.0f;
            return b;
        };
        GameObject* bg = ball("BallG", 0);
        GameObject* bs = ball("BallS", 10);
        // Graph on another object kicks BallG through its Target pin.
        GameObject* kicker = s.CreateGameObject("Kicker");
        Attach(kicker, R"VS(
node 0 OnStart
node 1 Rigidbody.AddImpulse/1
node 2 Vector3.up
default 1 1 "BallG"
data 1 0 2 0
exec 0 0 1
entry OnStart 0
)VS");
        auto* sc = bs->AddComponent<ScriptComponent>(NextScriptVM::kLanguage);
        std::string err;
        CHECK(sc->LoadSource("class Kick : Behaviour {\n"
                             "    void Start() { GetComponent<Rigidbody>().AddImpulse(Vector3.up); }\n"
                             "}\n", &err));
        if (!err.empty()) std::cerr << "  script error: " << err << "\n";
        s.Start(); Tick(s, 30);
        auto* rg = bg->GetComponent<Rigidbody3D>();
        auto* rs = bs->GetComponent<Rigidbody3D>();
        CHECK_NEAR(rg->velocity.y, 0.5f, 1e-4f);                  // impulse 1 / mass 2
        CHECK_NEAR(rg->velocity.y, rs->velocity.y, 1e-6f);
        CHECK(bg->transform->Position().y > 0.2f);
        CHECK_NEAR(bg->transform->Position().y, bs->transform->Position().y, 1e-5f);
    }
    // ---- Value nodes (Mathf, Vector3) feed steps (Set position, Debug.Log) -------
    {
        Scene s("Api2"); GameObject* go = s.CreateGameObject("Mover");
        Attach(go, R"VS(
node 0 OnStart
node 1 Transform.position=
node 2 Vector3.Lerp/3
node 3 Vector3.zero
node 4 Vector3.one
node 5 Debug.Log/1
node 6 Mathf.Clamp/3
node 7 Transform.position
node 8 Debug.Log/1
default 2 2 0.5
default 6 0 50
default 6 1 0
default 6 2 7
data 2 0 3 0
data 2 1 4 0
data 1 0 2 0
data 5 0 6 0
data 8 0 7 0
exec 0 0 1
exec 1 0 5
exec 5 0 8
entry OnStart 0
)VS");
        g_log.clear(); s.Start(); Tick(s, 1);
        CHECK_NEAR(go->transform->Position().x, 0.5f, 1e-5f);
        CHECK_NEAR(go->transform->Position().z, 0.5f, 1e-5f);
        CHECK(Logged("[script] 7"));
        CHECK(Logged("[script] (0.5, 0.5, 0.5)"));
    }
    // ---- Input.GetKey drives Transform.Translate (real key feed) ----------------
    {
        Scene s("Api3"); GameObject* go = s.CreateGameObject("P");
        Attach(go, R"VS(
node 0 OnUpdate
node 1 Branch
node 2 Input.GetKey/1
node 3 Transform.Translate/3
default 2 0 "d"
default 3 0 1
default 3 1 0
default 3 2 0
data 1 0 2 0
exec 0 0 1
exec 1 0 3
entry OnUpdate 0
)VS");
        s.Start();
        for (int i = 0; i < 3; ++i) { Input::FeedKeys({'d'}); s.Update(1.0f / 60.0f); }
        for (int i = 0; i < 2; ++i) { Input::FeedKeys({}); s.Update(1.0f / 60.0f); }
        CHECK_NEAR(go->transform->localPosition.x, 3.0f, 1e-5f);
        Input::FeedKeys({}); Input::FeedKeys({});
    }
    // ---- Binding errors are reported on the node and the event carries on -------
    {
        Scene s("Api4"); GameObject* go = s.CreateGameObject("NoBody");
        auto* v = Attach(go, R"VS(
node 0 OnStart
node 1 Rigidbody.AddForce/1
node 2 Transform.Translate/3
node 3 Input.GetAxis/1
node 4 Print
node 5 SetVar ax
default 2 0 1
default 2 3 "Nobody"
default 3 0 "Sideways"
default 4 0 "still running"
data 5 0 3 0
exec 0 0 1
exec 1 0 2
exec 2 0 5
exec 5 0 4
entry OnStart 0
)VS");
        g_log.clear(); s.Start(); Tick(s, 1);
        CHECK(Logged("'NoBody' has no Rigidbody"));
        CHECK(Logged("no object named 'Nobody'"));
        CHECK(Logged("isn't an axis"));
        CHECK(Logged("still running"));
        CHECK(v->Graph() && v->Graph()->ErrorCount() >= 3);
        CHECK_NEAR(go->transform->localPosition.x, 0.0f, 1e-6f);   // Translate on a missing Target did nothing
    }
    // ---- Engine-API graphs survive save -> reload --------------------------------
    {
        Scene s("SaveApi"); GameObject* go = s.CreateGameObject("Saved");
        Attach(go, R"VS(
node 0 OnStart
node 1 Transform.Translate/3
node 2 Mathf.Max/2
default 2 0 2
default 2 1 9
default 1 1 0
default 1 2 0
data 1 0 2 0
exec 0 0 1
entry OnStart 0
)VS");
        CHECK(SceneSerializer::SaveToFile(s, "vs_acceptance_api.okayscene"));
        Scene r("R");
        CHECK(SceneSerializer::LoadFromFile(r, "vs_acceptance_api.okayscene"));
        r.Start(); Tick(r, 1);
        GameObject* back = r.Find("Saved");
        CHECK(back != nullptr);
        if (back) CHECK_NEAR(back->transform->localPosition.x, 9.0f, 1e-5f);
        std::remove("vs_acceptance_api.okayscene");
    }
    // ---- Every member of the binding table is a node, and scripts know it too -----
    {
        const auto names = NextScriptVM().BuiltinNames();
        int missing = 0;
        for (const auto& e : vs::VsApiEntries()) {
            if (std::find(names.begin(), names.end(), e.member) == names.end()) ++missing;
            std::string err;
            if (!vs::MakeNode(e.type, {}, &err)) ++missing;
        }
        CHECK(missing == 0);
        CHECK(vs::VsApiFind("Rigidbody.AddForce/1") != nullptr);
        CHECK(vs::VsApiFind("Mathf.Clamp/3") && !vs::VsApiFind("Mathf.Clamp/3")->exec);   // maths: a value node
        CHECK(vs::VsApiFind("Debug.Log/1") && vs::VsApiFind("Debug.Log/1")->exec);         // a step
    }

    // ==== Loops, lists, scene / global variables, Variable Changed ===============
    auto runOnce = [&](const std::string& src, const char* name = "Looper") {
        static Scene* keep = nullptr;
        delete keep;
        keep = new Scene("Loops");
        GameObject* go = keep->CreateGameObject(name);
        auto* v = Attach(go, src);
        g_log.clear(); keep->Start(); Tick(*keep, 1);
        return v;
    };
    // ---- While: counts up while i < 5 --------------------------------------------
    {
        auto* v = runOnce(R"VS(
node 0 OnStart
node 1 While
node 2 Compare <
node 3 GetVar i
node 4 Const 5
node 5 AddVar i
node 6 Const 1
node 7 Print
node 8 GetVar i
data 2 0 3 0
data 2 1 4 0
data 1 0 2 0
data 5 0 6 0
data 7 0 8 0
exec 0 0 1
exec 1 0 5
exec 1 1 7
entry OnStart 0
)VS");
        CHECK(v->GetVariable("i").AsFloat() == 5.0f);
        CHECK(Logged("5"));
    }
    // ---- Break / Continue in a For Loop: sum of odd numbers until 7 -----------------
    {
        // For i in 1..9: if i is even Continue; if i > 7 Break; sum += i  -> 1+3+5+7 = 16
        auto* v = runOnce(R"VS(
node 0 OnStart
node 1 ForLoop
node 2 Const 1
node 3 Const 9
node 4 Branch
node 5 Equals
node 6 Mod
node 7 Const 2
node 8 Const 0
node 9 Continue
node 10 Branch
node 11 Compare >
node 12 Const 7
node 13 Break
node 14 AddVar sum
node 15 AddVar runs
node 16 Const 1
node 17 Print
node 18 Const "loop done"
data 1 0 2 0
data 1 1 3 0
data 6 0 1 0
data 6 1 7 0
data 5 0 6 0
data 5 1 8 0
data 4 0 5 0
data 11 0 1 0
data 11 1 12 0
data 10 0 11 0
data 14 0 1 0
data 15 0 16 0
data 17 0 18 0
exec 0 0 1
exec 1 0 15
exec 15 0 4
exec 4 0 9
exec 4 1 10
exec 10 0 13
exec 10 1 14
exec 1 1 17
entry OnStart 0
)VS");
        CHECK(v->GetVariable("sum").AsFloat() == 16.0f);
        CHECK(v->GetVariable("runs").AsFloat() == 9.0f);   // 8 is skipped by Continue, 9 breaks
        CHECK(Logged("loop done"));                          // Break goes on to Done
        CHECK(v->Graph()->ErrorCount() == 0);
    }
    // ---- For Each over All With Tag, Break inside it; nested loops break the inner one only --
    {
        Scene s("Each");
        for (const char* n : {"E1", "E2", "E3"}) { GameObject* e = s.CreateGameObject(n); e->tag = "Enemy"; }
        GameObject* go = s.CreateGameObject("Counter");
        auto* v = Attach(go, R"VS(
node 0 OnStart
node 1 ForEach
node 2 AllWithTag Enemy
node 3 AddVar seen
node 4 Const 1
node 5 SetVar last
node 6 Repeat 10
node 7 AddVar inner
node 8 Break
node 9 Print
node 10 Format "seen {0}, last {1}"
node 11 GetVar seen
node 12 GetVar last
node 13 SetVar lastIndex
data 1 0 2 0
data 3 0 4 0
data 5 0 1 0
data 13 0 1 1
data 7 0 4 0
data 10 0 11 0
data 10 1 12 0
data 9 0 10 0
exec 0 0 1
exec 1 0 3
exec 3 0 5
exec 5 0 13
exec 13 0 6
exec 6 0 7
exec 7 0 8
exec 1 1 9
entry OnStart 0
)VS");
        g_log.clear(); s.Start(); Tick(s, 1);
        CHECK(v->GetVariable("seen").AsFloat() == 3.0f);   // the outer loop wasn't broken
        CHECK(v->GetVariable("inner").AsFloat() == 3.0f);  // each inner Repeat broke after one run
        CHECK(v->GetVariable("lastIndex").AsFloat() == 2.0f);
        CHECK(Logged("seen 3, last E3"));
    }
    // ---- Break outside a loop, and inside a function called from a loop, is reported --
    {
        auto* v = runOnce(R"VS(
node 20 FunctionStart "Quit()"
node 21 Break
exec 20 0 21
node 0 OnStart
node 1 Break
node 2 Repeat 3
node 3 CallFunction Quit
node 4 AddVar n
node 5 Const 1
data 4 0 5 0
exec 0 0 2
exec 2 0 3
exec 3 0 4
node 6 OnUpdate
exec 6 0 1
entry OnStart 0
entry OnUpdate 6
)VS");
        CHECK(v->GetVariable("n").AsFloat() == 3.0f);   // the function's Break didn't stop the caller's Repeat
        CHECK(Logged("Break isn't inside the Body of a loop"));
    }
    // ---- A While that never ends is stopped and reported, the game keeps going ------
    {
        auto* v = runOnce(R"VS(
node 0 OnStart
node 1 While
node 2 AddVar spins
node 3 Const 1
default 1 0 true
data 2 0 3 0
exec 0 0 1
exec 1 0 2
entry OnStart 0
)VS");
        CHECK(Logged("stopped a runaway loop"));
        CHECK(v->GetVariable("spins").AsFloat() > 1000.0f);
    }
    // ---- Lists: add, length, get, contains, remove ---------------------------------
    {
        auto* v = runOnce(R"VS(
node 0 OnStart
node 1 SetVar bag
node 2 NewList
node 3 ListAdd
node 4 GetVar bag
node 5 ListAdd
node 6 ListAdd
node 7 ListRemoveAt
node 8 SetVar len
node 9 ListLength
node 10 SetVar second
node 11 ListGet
node 12 SetVar hasKey
node 13 ListContains
node 14 SetVar hasSword
node 15 ListContains
node 16 ListGet
default 3 1 "sword"
default 5 1 "shield"
default 6 1 "key"
default 7 1 0
default 11 1 1
default 13 1 "key"
default 15 1 "sword"
default 16 1 7
data 1 0 2 0
data 3 0 4 0
data 5 0 4 0
data 6 0 4 0
data 7 0 4 0
data 9 0 4 0
data 8 0 9 0
data 11 0 4 0
data 10 0 11 0
data 13 0 4 0
data 12 0 13 0
data 15 0 4 0
data 14 0 15 0
exec 0 0 1
exec 1 0 3
exec 3 0 5
exec 5 0 6
exec 6 0 7
exec 7 0 8
exec 8 0 10
exec 10 0 12
exec 12 0 14
node 17 SetVar bad
data 16 0 4 0
data 17 0 16 0
exec 14 0 17
entry OnStart 0
)VS");
        CHECK(v->GetVariable("len").AsFloat() == 2.0f);           // sword removed
        CHECK(v->GetVariable("second").AsString() == "key");
        CHECK(v->GetVariable("hasKey").AsBool());
        CHECK(!v->GetVariable("hasSword").AsBool());
        CHECK(Logged("index 7 is outside the list"));
    }
    // ---- Scene variables are shared by the scene's graphs; a new scene starts empty --
    {
        Scene s("SceneVars");
        GameObject* a = s.CreateGameObject("Setter");
        GameObject* b = s.CreateGameObject("Reader");
        Attach(a, "node 0 OnStart\nnode 1 SetSceneVar score\ndefault 1 0 10\nnode 2 AddSceneVar score\ndefault 2 0 5\n"
                  "exec 0 0 1\nexec 1 0 2\nentry OnStart 0\n");
        auto* rv = Attach(b, "node 0 OnUpdate\nnode 1 SetVar seen\nnode 2 GetSceneVar score\ndata 1 0 2 0\nexec 0 0 1\nentry OnUpdate 0\n");
        s.Start(); Tick(s, 1);
        CHECK(rv->GetVariable("seen").AsFloat() == 15.0f);
        CHECK(vs::SceneVariables(&s).count("score") == 1);
        CHECK(SceneSerializer::SaveToFile(s, "vs_acceptance_scope.okayscene"));
        CHECK(SceneSerializer::LoadFromFile(s, "vs_acceptance_scope.okayscene"));   // what loading a level does
        CHECK(vs::SceneVariables(&s).count("score") == 0);
        Scene other("Other");
        CHECK(vs::SceneVariables(&other).count("score") == 0);   // another scene has its own
        std::remove("vs_acceptance_scope.okayscene");
    }
    // ---- Global variables survive loading another scene ------------------------------
    {
        vs::ClearGlobalVariables();
        Scene s("Level1");
        Attach(s.CreateGameObject("Coins"), "node 0 OnStart\nnode 1 AddGlobalVar coins\ndefault 1 0 3\nexec 0 0 1\nentry OnStart 0\n");
        s.Start(); Tick(s, 1);
        Scene level2("Level2");
        auto* rv = Attach(level2.CreateGameObject("HUD"),
                          "node 0 OnStart\nnode 1 SetVar c\nnode 2 GetGlobalVar coins\ndata 1 0 2 0\nexec 0 0 1\nentry OnStart 0\n");
        CHECK(SceneSerializer::SaveToFile(level2, "vs_acceptance_l2.okayscene"));
        CHECK(SceneSerializer::LoadFromFile(s, "vs_acceptance_l2.okayscene"));   // level 1 replaced by level 2
        s.Start(); Tick(s, 1);
        GameObject* hud = s.Find("HUD");
        CHECK(hud && hud->GetComponent<VisualScriptComponent>() &&
              hud->GetComponent<VisualScriptComponent>()->GetVariable("c").AsFloat() == 3.0f);
        (void)rv;
        vs::ClearGlobalVariables();
        CHECK(vs::GlobalVariables().empty());
        std::remove("vs_acceptance_l2.okayscene");
    }
    // ---- On Var Changed: new and old value, changes from other graphs, no repeat ----
    {
        Scene s("Changed");
        GameObject* p = s.CreateGameObject("Hero");
        auto* v = Attach(p, R"VS(
node 0 OnStart
node 1 SetVar hp
default 1 0 10
exec 0 0 1
node 2 OnVarChanged hp
node 3 Print
node 4 Format "hp {0} was {1}"
node 5 AddVar changes
node 6 Const 1
data 4 0 2 0
data 4 1 2 1
data 3 0 4 0
data 5 0 6 0
exec 2 0 3
exec 3 0 5
node 7 OnKey space
node 8 AddVar hp
default 8 0 -3
exec 7 0 8
node 9 OnKey a
node 10 SetVar hp
default 10 0 7
exec 9 0 10
entry OnStart 0
entry OnVarChanged:hp 2
entry OnKey:space 7
entry OnKey:a 9
)VS");
        GameObject* other = s.CreateGameObject("Healer");
        Attach(other, "node 0 OnKey h\nnode 1 SetObjectVar hp\ndefault 1 0 \"Hero\"\ndefault 1 1 50\nexec 0 0 1\nentry OnKey:h 0\n");
        g_log.clear(); s.Start(); Tick(s, 1);
        CHECK(Logged("hp 10 was 0"));                      // set in On Start: from unset to 10
        Input::FeedKeys({' '}); s.Update(1.0f / 60.0f); Input::FeedKeys({}); s.Update(1.0f / 60.0f);
        CHECK(Logged("hp 7 was 10"));
        const float before = v->GetVariable("changes").AsFloat();
        Input::FeedKeys({'a'}); s.Update(1.0f / 60.0f); Input::FeedKeys({}); s.Update(1.0f / 60.0f);
        CHECK(v->GetVariable("changes").AsFloat() == before);   // set to the same value: no event
        Input::FeedKeys({'h'}); s.Update(1.0f / 60.0f); Input::FeedKeys({}); s.Update(1.0f / 60.0f); s.Update(1.0f / 60.0f);
        CHECK(Logged("hp 50 was 7"));                      // changed by another object's graph
        CHECK(v->GetVariable("changes").AsFloat() == 3.0f);
        Input::FeedKeys({}); Input::FeedKeys({});
    }
    // ---- On Scene Var Changed fires on another graph; a self-feeding handler is stopped --
    {
        Scene s("SceneChanged");
        // Scored after the HUD is watching (a value already set when a graph starts isn't a change).
        Attach(s.CreateGameObject("Scorer"), "node 0 OnUpdate\nnode 2 Once\nnode 1 AddSceneVar score\ndefault 1 0 5\n"
                                             "exec 0 0 2\nexec 2 0 1\nentry OnUpdate 0\n");
        auto* hud = Attach(s.CreateGameObject("HUD"), R"VS(
node 0 OnSceneVarChanged score
node 1 SetVar shown
data 1 0 0 0
exec 0 0 1
node 2 OnVarChanged loop
node 3 AddVar loop
default 3 0 1
exec 2 0 3
node 4 OnStart
node 5 SetVar loop
default 5 0 1
exec 4 0 5
entry OnSceneVarChanged:score 0
entry OnVarChanged:loop 2
entry OnStart 4
)VS");
        g_log.clear(); s.Start(); Tick(s, 3);
        CHECK(hud->GetVariable("shown").AsFloat() == 5.0f);
        CHECK(Logged("kept changing every time its On Variable Changed ran"));
    }

    std::remove(prefab.c_str());
    std::remove("vs_acceptance_two.okayscene");
    Log::sink = nullptr;
    TEST_MAIN_RESULT();
}
