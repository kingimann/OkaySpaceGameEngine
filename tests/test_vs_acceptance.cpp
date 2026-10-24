// Visual scripts doing real gameplay jobs end to end: real scenes, real input
// (Input::FeedKeys), real 3D physics, save -> reload. Each block is one
// behaviour a user builds in the Visual Script editor.
#include "test_framework.hpp"
#include <Okay.hpp>
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
        const char* move = R"(
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
)";
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
        auto* v = Attach(go, R"(
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
)");
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
        auto* v = Attach(go, R"(
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
)");
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
        Attach(go, R"(
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
)");
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
        Attach(p, R"(
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
)");
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
        Scene s("Read"); GameObject* me = s.CreateGameObject("Me"); GameObject* far = s.CreateGameObject("Far");
        far->transform->localPosition = {5, 6, 7}; far->tag = "Loot";
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
        auto* v = Attach(me, R"(
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
)");
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
        auto* v = Attach(me, R"(
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
)");
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
        auto* vm = Attach(me, R"(
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
)");
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

    std::remove(prefab.c_str());
    std::remove("vs_acceptance_two.okayscene");
    Log::sink = nullptr;
    TEST_MAIN_RESULT();
}
