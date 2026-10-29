#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/AI/BehaviorTree.hpp"
#include "okay/AI/Navigation.hpp"
#include "okay/Components/BehaviorTreeRunner.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/NavMeshSurface.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace okay;

namespace {

float Dist2D(const Vec3& a, const Vec3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }

// A static box collider, like the shipped templates use.
GameObject* Box(Scene& s, const char* name, Vec3 pos, Vec3 scale) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = pos; g->transform->localScale = scale;
    g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    return g;
}
GameObject* Floor(Scene& s, float size = 20.0f) { return Box(s, "Ground", {0, -0.5f, 0}, {size, 1, size}); }

// A scene with one NPC running a tree built in code. Custom tasks are scripted by
// their name: "ok" succeeds, "fail" fails, "runN" succeeds on its N-th tick.
struct Lab {
    Scene s{"bt"};
    GameObject* go = nullptr;
    BehaviorTreeRunner* r = nullptr;
    std::shared_ptr<BehaviorTree> t = std::make_shared<BehaviorTree>();
    std::map<int, int> ticks, aborts, runTicks;   // per node id (runTicks: since the node last started)
    Lab() {
        s.physicsEnabled = false;
        go = s.CreateGameObject("NPC");
        r = go->AddComponent<BehaviorTreeRunner>();
        r->runOnStart = false;
        r->onCustomTask = [this](BehaviorTreeRunner&, const BtNode& n, const char* phase) {
            if (std::strcmp(phase, "abort") == 0) { ++aborts[n.id]; return BtStatus::Aborted; }
            ++ticks[n.id];
            if (std::strcmp(phase, "enter") == 0) runTicks[n.id] = 0;
            const int k = ++runTicks[n.id];
            if (n.name == "ok") return BtStatus::Success;
            if (n.name == "fail") return BtStatus::Failure;
            if (n.name.rfind("run", 0) == 0) return k >= std::atoi(n.name.c_str() + 3) ? BtStatus::Success : BtStatus::Running;
            return BtStatus::Failure;
        };
    }
    int Task(int parent, const char* name) { const int id = t->AddNode(BtNodeType::Custom, parent); t->Find(id)->name = name; return id; }
    int Node(BtNodeType type, int parent = -1) { return t->AddNode(type, parent); }
    void Start() { r->SetTree(t); s.Start(); r->Run(); }
    BtStatus Tick(float dt = 0.1f) { s.Update(dt); return r->LastResult(); }
    BtStatus StatusOf(int id) const { const auto* st = r->State(id); return st ? st->status : BtStatus::Invalid; }
    int Ticks(int id) { return ticks[id]; }
    int Aborts(int id) { return aborts[id]; }
    void Restart() { ticks.clear(); aborts.clear(); runTicks.clear(); r->Run(); }
};

} // namespace

int main() {
    RUN_SUITE("behavior_tree");

    // ---- The Guard sample survives a write / parse round trip ---------------------
    {
        BehaviorTree g = BehaviorTree::GuardSample();
        CHECK(g.root >= 0);
        CHECK(g.nodes.size() == 11);
        CHECK(g.keys.size() == 7);
        const std::string text = g.Serialize();
        CHECK(text.rfind("okaybt 1", 0) == 0);
        BehaviorTree back;
        std::string err;
        CHECK(back.Deserialize(text, &err));
        CHECK(err.empty());
        CHECK(back.name == "Guard");
        CHECK(back.nodes.size() == g.nodes.size());
        CHECK(back.keys.size() == g.keys.size());
        CHECK(back.root == g.root);
        CHECK(back.Serialize() == text);
        // Children keep their order; parameters and labels come back.
        const std::vector<int> kids = back.FlowChildren(back.root);
        CHECK(kids.size() == 2);
        CHECK(back.Services(back.root).size() == 2);
        if (kids.size() == 2) {
            CHECK(back.Find(kids[0])->type == BtNodeType::Guard);
            CHECK(back.Find(kids[0])->name == "Seen recently");
            CHECK(back.Find(kids[0])->Str("key") == "sinceSeen");
            CHECK(back.Find(kids[0])->Num("value") == 5.0f);
            CHECK(back.Find(kids[1])->type == BtNodeType::Patrol);
            CHECK(back.Find(kids[1])->Str("waypoints") == "Waypoint 1, Waypoint 2, Waypoint 3");
        }
        CHECK(back.Key("sinceSeen") && back.Key("sinceSeen")->value.type == BtValue::Type::Number && back.Key("sinceSeen")->value.number == 9999.0f);
        CHECK(back.Key("target") && back.Key("target")->value.type == BtValue::Type::Object);
        // Quoted text with spaces and quotes survives.
        BtNode* ev = back.Find(back.AddNode(BtNodeType::SendEvent, back.root));
        ev->name = "say \"hi\" there"; ev->Set("event", "hello world");
        BehaviorTree again;
        CHECK(again.Deserialize(back.Serialize(), &err));
        const BtNode* ev2 = again.Find(ev->id);
        CHECK(ev2 && ev2->name == "say \"hi\" there" && ev2->Str("event") == "hello world");
        // Files.
        const std::string path = (std::filesystem::temp_directory_path() / "okay_bt_roundtrip.okaybt").string();
        CHECK(g.SaveToFile(path, &err));
        BehaviorTree fromFile;
        CHECK(fromFile.LoadFromFile(path, &err));
        CHECK(fromFile.Serialize() == text);
        BehaviorTree::Forget(path);
        std::shared_ptr<const BehaviorTree> shared = BehaviorTree::Load(path, &err);
        CHECK(shared && shared->nodes.size() == g.nodes.size());
        CHECK(BehaviorTree::Load(path) == shared);   // cached
        BehaviorTree::Forget(path);
        CHECK(BehaviorTree::Load(path) != shared);
        std::filesystem::remove(path);
        BehaviorTree bad;
        CHECK(!bad.Deserialize("clip walk loop\n", &err));
        CHECK(!err.empty());
        // Validation: the sample is clean against a scene with its waypoints, and
        // reports what is missing elsewhere.
        {
            Scene sc("v");
            for (const char* w : {"Waypoint 1", "Waypoint 2", "Waypoint 3", "Player"}) sc.CreateGameObject(w);
            CHECK(g.Validate(&sc).empty());
            Scene empty("e");
            const auto issues = g.Validate(&empty);
            int missing = 0;
            for (const auto& is : issues) if (is.text.find("missing waypoint") != std::string::npos) ++missing;
            CHECK(missing == 3);
            BehaviorTree broken = g;
            BtNode* sel = broken.Find(broken.root);
            const int seq = broken.AddNode(BtNodeType::Sequence, sel->id);      // a composite without children
            const int inv = broken.AddNode(BtNodeType::Inverter, sel->id);      // a decorator without a child
            const int sb = broken.AddNode(BtNodeType::SetBlackboard, sel->id);  // an unknown key
            broken.Find(sb)->Set("key", "nope");
            bool badSeq = false, badInv = false, badKey = false;
            for (const auto& is : broken.Validate(&sc)) {
                if (is.node == seq && is.severity == 2) badSeq = true;
                if (is.node == inv && is.severity == 2) badInv = true;
                if (is.node == sb && is.severity == 2 && is.text.find("nope") != std::string::npos) badKey = true;
            }
            CHECK(badSeq && badInv && badKey);
        }
        // Editing: reorder, reparent (never into its own subtree), wrap in a decorator.
        {
            BehaviorTree e = g;
            const std::vector<int> k = e.FlowChildren(e.root);
            e.MoveChild(k[1], 0);
            CHECK(e.FlowChildren(e.root)[0] == k[1]);
            CHECK(!e.Reparent(e.root, k[0]));
            const int wrap = e.InsertParent(k[1], BtNodeType::Succeeder);
            CHECK(wrap >= 0 && e.Find(k[1])->parent == wrap && e.Find(wrap)->parent == e.root);
            CHECK(e.FlowChildren(e.root)[0] == wrap);
            e.RemoveSubtree(wrap);
            CHECK(!e.Find(wrap) && !e.Find(k[1]));
        }
    }

    // ---- Sequence stops at the first failure; selector at the first success ----------
    {
        Lab L;
        const int seq = L.Node(BtNodeType::Sequence);
        const int a = L.Task(seq, "ok"), b = L.Task(seq, "fail"), c = L.Task(seq, "ok");
        L.Start();
        CHECK(L.Tick() == BtStatus::Failure);
        CHECK(L.Ticks(a) == 1 && L.Ticks(b) == 1 && L.Ticks(c) == 0);
        CHECK(L.StatusOf(a) == BtStatus::Success && L.StatusOf(b) == BtStatus::Failure && L.StatusOf(c) == BtStatus::Invalid);
        CHECK(L.r->Completions() == 1);
    }
    {
        Lab L;
        const int sel = L.Node(BtNodeType::Selector);
        const int a = L.Task(sel, "fail"), b = L.Task(sel, "ok"), c = L.Task(sel, "ok");
        L.Start();
        CHECK(L.Tick() == BtStatus::Success);
        CHECK(L.Ticks(a) == 1 && L.Ticks(b) == 1 && L.Ticks(c) == 0);
        // All children succeed: the sequence succeeds in one tick, every child ran once.
        Lab M;
        const int seq = M.Node(BtNodeType::Sequence);
        const int x = M.Task(seq, "ok"), y = M.Task(seq, "ok");
        M.Start();
        CHECK(M.Tick() == BtStatus::Success);
        CHECK(M.Ticks(x) == 1 && M.Ticks(y) == 1);
    }

    // ---- Running carries across ticks: a sequence waits for a running child -----------
    {
        Lab L;
        const int seq = L.Node(BtNodeType::Sequence);
        const int a = L.Task(seq, "run3"), b = L.Task(seq, "ok");
        L.Start();
        CHECK(L.Tick() == BtStatus::Invalid);   // no run finished yet
        CHECK(L.StatusOf(seq) == BtStatus::Running && L.StatusOf(a) == BtStatus::Running);
        CHECK(L.r->State(a)->running && L.r->ActivePath().size() == 2);
        CHECK(L.Tick() == BtStatus::Invalid);
        CHECK(L.Ticks(b) == 0);
        CHECK(L.Tick() == BtStatus::Success);
        CHECK(L.Ticks(a) == 3 && L.Ticks(b) == 1);
        CHECK(!L.r->State(a)->running && L.r->ActivePath().empty());
        // The tree starts over on the next tick.
        CHECK(L.Tick() == BtStatus::Success);
        CHECK(L.Ticks(a) == 4 && L.StatusOf(a) == BtStatus::Running);
    }

    // ---- Parallel policies -------------------------------------------------------------
    {
        Lab L;   // succeed when all: waits for the slow child
        const int par = L.Node(BtNodeType::Parallel);
        const int a = L.Task(par, "run2"), b = L.Task(par, "ok");
        L.Start();
        CHECK(L.Tick() == BtStatus::Invalid);
        CHECK(L.Tick() == BtStatus::Success);
        CHECK(L.Ticks(a) == 2 && L.Ticks(b) == 1);
    }
    {
        Lab L;   // fail when one: the failure ends it and aborts the running child
        const int par = L.Node(BtNodeType::Parallel);
        const int a = L.Task(par, "run5"), b = L.Task(par, "fail");
        L.Start();
        CHECK(L.Tick() == BtStatus::Failure);
        CHECK(L.Aborts(a) == 1 && L.StatusOf(a) == BtStatus::Aborted);
        CHECK(L.Ticks(b) == 1 && L.StatusOf(b) == BtStatus::Failure);
    }
    {
        Lab L;   // succeed when one
        const int par = L.Node(BtNodeType::Parallel);
        L.t->Find(par)->Set("success", "one");
        const int a = L.Task(par, "run5"), b = L.Task(par, "ok");
        L.Start();
        CHECK(L.Tick() == BtStatus::Success);
        CHECK(L.Aborts(a) == 1 && L.Ticks(b) == 1);
    }
    {
        Lab L;   // fail when all: one failure is not enough
        const int par = L.Node(BtNodeType::Parallel);
        L.t->Find(par)->Set("failure", "all");
        const int a = L.Task(par, "run2"), b = L.Task(par, "fail");
        L.Start();
        CHECK(L.Tick() == BtStatus::Invalid);
        CHECK(L.Tick() == BtStatus::Failure);   // all finished, not all succeeded
        CHECK(L.Ticks(a) == 2 && L.Aborts(a) == 0 && L.Ticks(b) == 1);
    }

    // ---- Random selector: weights decide who goes first ------------------------------
    {
        Lab L;
        const int rs = L.Node(BtNodeType::RandomSelector);
        const int light = L.Task(rs, "ok"), heavy = L.Task(rs, "ok");
        L.t->Find(light)->Set("weight", 1.0f);
        L.t->Find(heavy)->Set("weight", 50.0f);
        L.Start();
        int heavyFirst = 0, lightFirst = 0;
        for (int i = 0; i < 100; ++i) {
            L.Restart();
            CHECK(L.Tick() == BtStatus::Success);
            if (L.Ticks(heavy) == 1 && L.Ticks(light) == 0) ++heavyFirst;
            if (L.Ticks(light) == 1 && L.Ticks(heavy) == 0) ++lightFirst;
        }
        CHECK(heavyFirst + lightFirst == 100);
        CHECK(heavyFirst >= 85);
        // A zero weight goes last: with the heavy child failing, both get tried.
        L.t->Find(light)->Set("weight", 0.0f);
        L.t->Find(heavy)->name = "fail";
        for (int i = 0; i < 20; ++i) {
            L.Restart();
            CHECK(L.Tick() == BtStatus::Success);
            CHECK(L.Ticks(heavy) == 1 && L.Ticks(light) == 1);
        }
    }

    // ---- Decorators ------------------------------------------------------------------
    {
        Lab L;   // inverter
        const int inv = L.Node(BtNodeType::Inverter);
        L.Task(inv, "ok");
        L.Start();
        CHECK(L.Tick() == BtStatus::Failure);
        Lab M;
        const int inv2 = M.Node(BtNodeType::Inverter);
        M.Task(inv2, "fail");
        M.Start();
        CHECK(M.Tick() == BtStatus::Success);
        Lab N;   // succeeder
        const int suc = N.Node(BtNodeType::Succeeder);
        N.Task(suc, "fail");
        N.Start();
        CHECK(N.Tick() == BtStatus::Success);
    }
    {
        Lab L;   // repeat 3 times: running until the third run finished
        const int rep = L.Node(BtNodeType::Repeat);
        L.t->Find(rep)->Set("count", 3);
        const int a = L.Task(rep, "ok");
        L.Start();
        CHECK(L.Tick() == BtStatus::Invalid);
        CHECK(L.Tick() == BtStatus::Invalid);
        CHECK(L.Tick() == BtStatus::Success);
        CHECK(L.Ticks(a) == 3);
        // Forever: never finishes.
        L.t->Find(rep)->Set("count", 0);
        L.Restart();
        for (int i = 0; i < 10; ++i) CHECK(L.Tick() == BtStatus::Invalid);
        CHECK(L.Ticks(a) == 10);
    }
    {
        Lab L;   // until fail: succeeds once the child fails
        const int uf = L.Node(BtNodeType::UntilFail);
        const int a = L.Task(uf, "custom");
        int calls = 0;
        L.r->onCustomTask = [&](BehaviorTreeRunner&, const BtNode&, const char* phase) {
            if (std::strcmp(phase, "abort") == 0) return BtStatus::Aborted;
            return ++calls <= 2 ? BtStatus::Success : BtStatus::Failure;
        };
        L.Start();
        CHECK(L.Tick() == BtStatus::Invalid);
        CHECK(L.Tick() == BtStatus::Invalid);
        CHECK(L.Tick() == BtStatus::Success);
        CHECK(calls == 3);
        (void)a;
    }
    {
        Lab L;   // cooldown: fails while cooling down, runs again after
        const int cd = L.Node(BtNodeType::Cooldown);
        L.t->Find(cd)->Set("seconds", 2.0f);
        const int a = L.Task(cd, "ok");
        L.Start();
        CHECK(L.Tick(0.1f) == BtStatus::Success);
        CHECK(L.Tick(0.1f) == BtStatus::Failure);   // 0.2 s in: still cooling down
        CHECK(L.Tick(1.0f) == BtStatus::Failure);   // 1.2 s
        CHECK(L.Tick(1.0f) == BtStatus::Success);   // 2.2 s: available again
        CHECK(L.Ticks(a) == 2);
    }
    {
        Lab L;   // time limit: a slow child is stopped
        const int tl = L.Node(BtNodeType::TimeLimit);
        L.t->Find(tl)->Set("seconds", 0.5f);
        const int a = L.Task(tl, "run100");
        L.Start();
        for (int i = 0; i < 5; ++i) CHECK(L.Tick(0.1f) == BtStatus::Invalid);
        CHECK(L.Tick(0.2f) == BtStatus::Failure);
        CHECK(L.Aborts(a) == 1 && L.StatusOf(a) == BtStatus::Aborted);
    }
    {
        Lab L;   // condition on the blackboard, with and without a child
        L.t->DeclareKey("hp", BtValue::Number(10.0f));
        L.t->DeclareKey("name", BtValue::String("Bob"));
        const int seq = L.Node(BtNodeType::Sequence);
        const int c1 = L.Node(BtNodeType::Condition, seq);
        L.t->Find(c1)->Set("key", "hp"); L.t->Find(c1)->Set("op", "<"); L.t->Find(c1)->Set("value", "20");
        const int c2 = L.Node(BtNodeType::Condition, seq);
        L.t->Find(c2)->Set("key", "name"); L.t->Find(c2)->Set("op", "=="); L.t->Find(c2)->Set("value", "Bob");
        const int a = L.Task(c2, "ok");
        L.Start();
        CHECK(L.Tick() == BtStatus::Success);
        CHECK(L.Ticks(a) == 1);
        L.r->blackboard.Set("hp", BtValue::Number(25.0f));
        CHECK(L.Tick() == BtStatus::Failure);
        CHECK(L.Ticks(a) == 1);
    }

    // ---- Guard: aborts its running subtree when the test flips, and takes over -------
    {
        Lab L;
        L.t->DeclareKey("go", BtValue::Bool(true));
        const int g = L.Node(BtNodeType::Guard);
        L.t->Find(g)->Set("key", "go"); L.t->Find(g)->Set("op", "=="); L.t->Find(g)->Set("value", "true");
        const int a = L.Task(g, "run100");
        L.Start();
        CHECK(L.Tick() == BtStatus::Invalid);
        CHECK(L.StatusOf(a) == BtStatus::Running);
        L.r->blackboard.Set("go", BtValue::Bool(false));
        CHECK(L.Tick() == BtStatus::Failure);
        CHECK(L.Aborts(a) == 1 && L.StatusOf(a) == BtStatus::Aborted);
        CHECK(L.Ticks(a) == 1);   // not ticked again after the abort
    }
    {
        Lab L;   // lower-priority takeover inside a selector
        L.t->DeclareKey("go", BtValue::Bool(false));
        const int sel = L.Node(BtNodeType::Selector);
        const int g = L.Node(BtNodeType::Guard, sel);
        L.t->Find(g)->Set("key", "go"); L.t->Find(g)->Set("op", "=="); L.t->Find(g)->Set("value", "true");
        const int a = L.Task(g, "run100");
        const int b = L.Task(sel, "run100");
        L.Start();
        L.Tick();
        CHECK(L.Ticks(a) == 0 && L.Ticks(b) == 1 && L.StatusOf(b) == BtStatus::Running);
        L.r->blackboard.Set("go", BtValue::Bool(true));
        L.Tick();
        CHECK(L.Aborts(b) == 1 && L.StatusOf(b) == BtStatus::Aborted);
        CHECK(L.Ticks(a) == 1 && L.StatusOf(a) == BtStatus::Running);
        L.r->blackboard.Set("go", BtValue::Bool(false));
        L.Tick();
        CHECK(L.Aborts(a) == 1 && L.Ticks(b) == 2 && L.StatusOf(b) == BtStatus::Running);   // back to the patrol-like task
        // abort=self never takes over: the later sibling keeps running.
        L.t->Find(g)->Set("abort", "self");
        L.Restart();
        L.Tick();
        L.r->blackboard.Set("go", BtValue::Bool(true));
        L.Tick();
        CHECK(L.Ticks(a) == 0 && L.Ticks(b) == 2);
    }

    // ---- Blackboard: types, conversions, overrides -------------------------------------
    {
        BtValue n = BtValue::Number(3.5f), b = BtValue::Bool(true), s = BtValue::String("hi"), v = BtValue::Vector({1, 2, 3}), o = BtValue::Object("Player");
        CHECK(n.ToText() == "3.5" && b.ToText() == "1" && s.ToText() == "hi" && v.ToText() == "1 2 3" && o.ToText() == "Player");
        CHECK(BtValue::FromText(BtValue::Type::Number, "2.25").number == 2.25f);
        CHECK(BtValue::FromText(BtValue::Type::Bool, "true").boolean && !BtValue::FromText(BtValue::Type::Bool, "0").boolean);
        CHECK(BtValue::FromText(BtValue::Type::Vector, "4 5 6").vector.z == 6.0f);
        CHECK(BtValue::Infer("12").type == BtValue::Type::Number && BtValue::Infer("false").type == BtValue::Type::Bool);
        CHECK(BtValue::Infer("1 2 3").type == BtValue::Type::Vector && BtValue::Infer("Bob").type == BtValue::Type::String);
        CHECK(n.AsBool() && !BtValue::Number(0).AsBool() && b.AsNumber() == 1.0f && s.AsString() == "hi");
        CHECK(BtValue::FromVs(n.ToVs()) == n && BtValue::FromVs(b.ToVs()) == b && BtValue::FromVs(s.ToVs()) == s && BtValue::FromVs(v.ToVs()) == v);
        Blackboard bb;
        bb.Declare("hp", BtValue::Number(10.0f));
        bb.Set("hp", BtValue::String("7"));            // keeps the declared type
        CHECK(bb.Find("hp")->type == BtValue::Type::Number && bb.GetNumber("hp") == 7.0f);
        bb.Set("fresh", BtValue::Vector({1, 0, 0}));   // a new key takes the value's type
        CHECK(bb.Find("fresh")->type == BtValue::Type::Vector);
        CHECK(bb.GetBool("missing", true) && bb.GetString("missing", "x") == "x");
        const std::uint32_t ver = bb.Version();
        bb.Remove("fresh");
        CHECK(!bb.Has("fresh") && bb.Version() > ver);
        // A runner's overrides replace the tree's defaults, converted to the key's type.
        Lab L;
        L.t->DeclareKey("hp", BtValue::Number(100.0f));
        L.t->DeclareKey("brave", BtValue::Bool(false));
        L.t->DeclareKey("post", BtValue::Vector({0, 0, 0}));
        L.t->DeclareKey("boss", BtValue::Object(""));
        L.t->DeclareKey("name", BtValue::String("guard"));
        L.r->overrides = {{"hp", "35"}, {"brave", "true"}, {"post", "1 2 3"}, {"boss", "Player"}, {"extra", "4"}};
        L.Node(BtNodeType::Sequence);
        L.Task(L.t->root, "ok");
        GameObject* player = L.s.CreateGameObject("Player");
        player->transform->localPosition = {9, 0, 9};
        L.Start();
        CHECK(L.r->blackboard.GetNumber("hp") == 35.0f && L.r->blackboard.Find("hp")->type == BtValue::Type::Number);
        CHECK(L.r->blackboard.GetBool("brave"));
        CHECK(L.r->blackboard.GetVector("post").y == 2.0f);
        CHECK(L.r->blackboard.GetObject("boss", &L.s) == player);
        Vec3 bp;
        CHECK(L.r->blackboard.GetPosition("boss", &L.s, bp) && bp.x == 9.0f);
        CHECK(L.r->blackboard.GetString("name") == "guard");
        CHECK(L.r->blackboard.GetNumber("extra") == 4.0f);
        // Set Blackboard writes in the key's notation.
        const int sb = L.t->AddNode(BtNodeType::SetBlackboard, L.t->root);
        L.t->Find(sb)->Set("key", "hp"); L.t->Find(sb)->Set("value", "12");
        L.Restart();
        L.Tick();
        CHECK(L.r->blackboard.GetNumber("hp") == 12.0f);
    }

    // ---- Perception: sight cone, line of sight through real colliders, hearing -------
    {
        auto build = [](Scene& s, Vec3 playerAt, bool wall, float angle, float hearing) {
            s.physicsEnabled = false;
            Floor(s);
            if (wall) Box(s, "Wall", {0, 1.0f, 2.5f}, {6, 2, 0.4f});
            GameObject* player = s.CreateGameObject("Player");
            player->transform->localPosition = playerAt;
            GameObject* npc = s.CreateGameObject("Guard");
            npc->transform->localPosition = {0, 0, 0};   // facing +Z
            auto* r = npc->AddComponent<BehaviorTreeRunner>();
            auto t = std::make_shared<BehaviorTree>();
            t->DeclareKey("target", BtValue::Object(""));
            t->DeclareKey("canSee", BtValue::Bool(false));
            t->DeclareKey("lastSeen", BtValue::Vector({0, 0, 0}));
            t->DeclareKey("sinceSeen", BtValue::Number(9999.0f));
            const int root = t->AddNode(BtNodeType::Sequence);
            BtNode* per = t->Find(t->AddNode(BtNodeType::Perception, root));
            per->Set("targetName", "Player"); per->Set("range", 12.0f); per->Set("angle", angle); per->Set("hearing", hearing); per->Set("interval", 0.0f);
            t->Find(t->AddNode(BtNodeType::Wait, root))->Set("min", 100.0f);
            r->SetTree(t);
            s.Start();
            s.Update(0.1f);
            return r;
        };
        {   // in front, nothing in between: seen
            Scene s("see");
            BehaviorTreeRunner* r = build(s, {0, 0, 5}, false, 120.0f, 0.0f);
            CHECK(r->blackboard.GetBool("canSee"));
            CHECK(r->blackboard.GetObject("target", &s) == s.Find("Player"));
            CHECK(r->blackboard.GetVector("lastSeen").z == 5.0f);
            CHECK(r->blackboard.GetNumber("sinceSeen") == 0.0f);
            // The player leaves: the clock since the last sighting runs.
            s.Find("Player")->transform->localPosition = {0, 0, 50};
            for (int i = 0; i < 10; ++i) s.Update(0.1f);
            CHECK(!r->blackboard.GetBool("canSee"));
            CHECK_NEAR(r->blackboard.GetNumber("sinceSeen"), 1.0f, 0.15f);
            CHECK(r->blackboard.GetObject("target", &s) == s.Find("Player"));   // still remembered
        }
        {   // a wall in the way: not seen
            Scene s("wall");
            BehaviorTreeRunner* r = build(s, {0, 0, 5}, true, 120.0f, 0.0f);
            CHECK(!r->blackboard.GetBool("canSee"));
            CHECK(r->blackboard.GetNumber("sinceSeen") > 100.0f);
        }
        {   // behind the guard: outside the cone
            Scene s("behind");
            BehaviorTreeRunner* r = build(s, {0, 0, -5}, false, 120.0f, 0.0f);
            CHECK(!r->blackboard.GetBool("canSee"));
        }
        {   // behind but close, with hearing: noticed
            Scene s("hear");
            BehaviorTreeRunner* r = build(s, {0, 0, -2}, false, 120.0f, 4.0f);
            CHECK(r->blackboard.GetBool("canSee"));
        }
        {   // all-round vision sees behind
            Scene s("around");
            BehaviorTreeRunner* r = build(s, {0, 0, -5}, false, 360.0f, 0.0f);
            CHECK(r->blackboard.GetBool("canSee"));
        }
    }

    // ---- Move To over a baked navmesh: around the wall, and failing on an island ------
    {
        auto world = [](Scene& s) {
            s.physicsEnabled = false;
            Floor(s);
            Box(s, "Wall", {0, 1.0f, 0}, {1, 2, 6});
            Box(s, "Island", {14, -0.5f, 0}, {4, 1, 4});
            auto* ns = s.CreateGameObject("Nav Mesh")->AddComponent<NavMeshSurface>();
            ns->settings.agentRadius = 0.4f; ns->settings.cellSize = 0.25f;
            CHECK(ns->Bake(s));
            CHECK(Navigation::Mesh(s) != nullptr);
        };
        auto mover = [](Scene& s, const char* name, Vec3 at, Vec3 goal, bool agent) {
            GameObject* npc = s.CreateGameObject(name);
            npc->transform->localPosition = at;
            if (agent) npc->AddComponent<NavMeshAgent>()->speed = 4.0f;
            auto* r = npc->AddComponent<BehaviorTreeRunner>();
            auto t = std::make_shared<BehaviorTree>();
            BtNode* mv = t->Find(t->AddNode(BtNodeType::MoveTo));
            mv->Set("position", goal); mv->Set("stopDistance", 0.5f); mv->Set("speed", 4.0f);
            r->SetTree(t);
            return r;
        };
        {
            Scene s("walk");
            world(s);
            BehaviorTreeRunner* self = mover(s, "Walker", {-5, 0, 0}, {5, 0, 0}, false);
            BehaviorTreeRunner* withAgent = mover(s, "Agent", {-5, 0, 4}, {5, 0, -4}, true);
            s.Start();
            bool crossedWall = false;
            int reached = -1, agentReached = -1;
            for (int i = 0; i < 900; ++i) {
                s.Update(1.0f / 60.0f);
                const Vec3 p = self->gameObject->transform->Position();
                if (std::fabs(p.x) < 0.8f && std::fabs(p.z) < 3.3f) crossedWall = true;   // the wall plus most of the 0.4 clearance
                if (reached < 0 && self->LastResult() == BtStatus::Success) reached = i;
                if (agentReached < 0 && withAgent->LastResult() == BtStatus::Success) agentReached = i;
                if (reached >= 0 && agentReached >= 0) break;
            }
            CHECK(reached > 0);
            CHECK(!crossedWall);
            CHECK(Dist2D(self->gameObject->transform->Position(), {5, 0, 0}) <= 0.6f);
            CHECK(agentReached > 0);
            CHECK(Dist2D(withAgent->gameObject->transform->Position(), {5, 0, -4}) <= 0.8f);
        }
        {
            Scene s("island");
            world(s);
            BehaviorTreeRunner* self = mover(s, "Walker", {-5, 0, 0}, {14, 0, 0}, false);
            BehaviorTreeRunner* withAgent = mover(s, "Agent", {-5, 0, 4}, {14, 0, 0}, true);
            s.Start();
            for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
            CHECK(self->LastResult() == BtStatus::Failure);
            CHECK(withAgent->LastResult() == BtStatus::Failure);
            CHECK(Dist2D(self->gameObject->transform->Position(), {-5, 0, 0}) < 0.5f);   // never set off
        }
    }

    // ---- Patrol cycles its waypoints (loop and ping-pong) -------------------------------
    {
        auto run = [](const char* mode, std::vector<int>& visited) {
            Scene s("patrol");
            s.physicsEnabled = false;
            const Vec3 wps[3] = {{3, 0, 0}, {3, 0, 3}, {0, 0, 3}};
            for (int i = 0; i < 3; ++i) s.CreateGameObject("WP " + std::to_string(i + 1))->transform->localPosition = wps[i];
            GameObject* npc = s.CreateGameObject("Guard");
            auto* r = npc->AddComponent<BehaviorTreeRunner>();
            auto t = std::make_shared<BehaviorTree>();
            BtNode* p = t->Find(t->AddNode(BtNodeType::Patrol));
            p->Set("waypoints", "WP 1, WP 2, WP 3"); p->Set("mode", mode); p->Set("wait", 0.2f); p->Set("speed", 6.0f); p->Set("stopDistance", 0.4f);
            r->SetTree(t);
            s.Start();
            int last = -1;
            for (int i = 0; i < 1500 && visited.size() < 6; ++i) {
                s.Update(1.0f / 60.0f);
                const Vec3 pos = npc->transform->Position();
                for (int w = 0; w < 3; ++w)
                    if (Dist2D(pos, wps[w]) < 0.5f && last != w) { visited.push_back(w); last = w; }
            }
            CHECK(r->LastResult() == BtStatus::Invalid);   // a patrol never finishes
        };
        std::vector<int> loop, pingpong;
        run("loop", loop);
        CHECK(loop.size() >= 5);
        if (loop.size() >= 5) CHECK(loop[0] == 0 && loop[1] == 1 && loop[2] == 2 && loop[3] == 0 && loop[4] == 1);
        run("pingpong", pingpong);
        CHECK(pingpong.size() >= 5);
        if (pingpong.size() >= 5) CHECK(pingpong[0] == 0 && pingpong[1] == 1 && pingpong[2] == 2 && pingpong[3] == 1 && pingpong[4] == 0);
    }

    // ---- Chase follows a moving target -----------------------------------------------
    {
        Scene s("chase");
        s.physicsEnabled = false;
        GameObject* prey = s.CreateGameObject("Prey");
        prey->transform->localPosition = {6, 0, 0};
        GameObject* npc = s.CreateGameObject("Hunter");
        auto* r = npc->AddComponent<BehaviorTreeRunner>();
        auto t = std::make_shared<BehaviorTree>();
        BtNode* c = t->Find(t->AddNode(BtNodeType::Chase));
        c->Set("target", "Prey"); c->Set("stopDistance", 1.0f); c->Set("speed", 6.0f);
        r->SetTree(t);
        s.Start();
        float best = 100.0f;
        int caught = -1;
        for (int i = 0; i < 600; ++i) {
            prey->transform->localPosition = prey->transform->localPosition + Vec3{0.03f, 0, 0.01f};   // 1.9 units/s
            s.Update(1.0f / 60.0f);
            best = std::fmin(best, Dist2D(npc->transform->Position(), prey->transform->Position()));
            if (r->LastResult() == BtStatus::Success) { caught = i; break; }
        }
        CHECK(caught > 0);
        CHECK(best <= 1.05f);
        CHECK(npc->transform->Position().x > 5.0f);   // it went after the prey, not to where it started
        // Without a target the chase fails.
        s.Destroy(prey);
        s.Update(1.0f / 60.0f);
        s.Update(1.0f / 60.0f);
        CHECK(r->LastResult() == BtStatus::Failure);
    }

    // ---- Scripts: Send Event, a Custom task and Run Script Function -----------------
    {
        Scene s("script");
        s.physicsEnabled = false;
        GameObject* npc = s.CreateGameObject("Guard");
        auto* sc = npc->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "var got = \"\";\nvar phases = \"\";\nvar arg = \"\";\n"
            "function on_bt_event(name) { got = name; }\n"
            "function my_task(phase) { phases = phases + phase + \",\"; if (phase == \"enter\") { return \"running\"; } return \"success\"; }\n"
            "function do_thing(a) { arg = a; return true; }\n"
            "function refuse() { return false; }\n"));
        auto* r = npc->AddComponent<BehaviorTreeRunner>();
        auto t = std::make_shared<BehaviorTree>();
        const int seq = t->AddNode(BtNodeType::Sequence);
        BtNode* ev = t->Find(t->AddNode(BtNodeType::SendEvent, seq));
        ev->Set("event", "alert");
        BtNode* cu = t->Find(t->AddNode(BtNodeType::Custom, seq));
        cu->Set("function", "my_task");
        BtNode* rs = t->Find(t->AddNode(BtNodeType::RunScript, seq));
        rs->Set("function", "do_thing"); rs->Set("arg", "now");
        r->SetTree(t);
        std::string lastEvent;
        r->onEvent = [&](const std::string& n) { lastEvent = n; };
        s.Start();
        s.Update(0.1f);
        CHECK(sc->VM()->GetGlobal("got").AsString() == "alert");
        CHECK(lastEvent == "alert");
        CHECK(r->LastResult() == BtStatus::Invalid);   // the custom task is running
        CHECK(sc->VM()->GetGlobal("phases").AsString() == "enter,");
        s.Update(0.1f);
        CHECK(sc->VM()->GetGlobal("phases").AsString() == "enter,tick,");
        CHECK(sc->VM()->GetGlobal("arg").AsString() == "now");
        CHECK(r->LastResult() == BtStatus::Success);
        // A function that returns false fails the task; a missing one too.
        rs->Set("function", "refuse");
        r->Run();
        s.Update(0.1f); s.Update(0.1f);
        CHECK(r->LastResult() == BtStatus::Failure);
        rs->Set("function", "nothing_here");
        r->Run();
        s.Update(0.1f); s.Update(0.1f);
        CHECK(r->LastResult() == BtStatus::Failure);
        // Aborting a running custom task tells the script.
        rs->Set("function", "do_thing");
        cu->Set("function", "my_task");
        r->Run();
        s.Update(0.1f);
        r->Stop();
        CHECK(sc->VM()->GetGlobal("phases").AsString().find("abort,") != std::string::npos);
        // The script builtins reach the blackboard and the tree.
        GameObject* other = s.CreateGameObject("Other");
        auto* sc2 = other->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc2->LoadSource(
            "var seen = 0;\nvar name = \"\";\n"
            "function start() { bt_set(\"Guard\", \"hp\", 42); bt_set(\"Guard\", \"name\", \"Bob\"); seen = bt_get(\"Guard\", \"hp\"); name = bt_get(\"Guard\", \"name\"); }\n"));
        s.Update(0.1f);
        CHECK(sc2->VM()->GetGlobal("seen").AsFloat() == 42.0f);
        CHECK(sc2->VM()->GetGlobal("name").AsString() == "Bob");
        CHECK(r->blackboard.GetNumber("hp") == 42.0f);
    }

    // ---- The runner's scene record round-trips; an older scene without it loads -------
    {
        Scene a("rec");
        GameObject* g = a.CreateGameObject("Guard");
        auto* r = g->AddComponent<BehaviorTreeRunner>();
        r->treePath = "Assets/Guard.okaybt"; r->tickRate = 10.0f; r->debug = true; r->runOnStart = false;
        r->homeOverride = true; r->homePoint = {1, 2, 3};
        r->overrides = {{"hp", "5"}, {"name", "Bob Smith"}};
        a.CreateGameObject("Plain");
        const std::string text = SceneSerializer::Serialize(a);
        CHECK(text.find("btrunner") != std::string::npos);
        Scene b("back");
        std::string err;
        CHECK(SceneSerializer::Deserialize(b, text, &err));
        CHECK(err.empty());
        auto* r2 = b.Find("Guard") ? b.Find("Guard")->GetComponent<BehaviorTreeRunner>() : nullptr;
        CHECK(r2 != nullptr);
        if (r2) {
            CHECK(r2->treePath == "Assets/Guard.okaybt");
            CHECK(r2->tickRate == 10.0f && r2->debug && !r2->runOnStart);
            CHECK(r2->homeOverride && r2->homePoint.z == 3.0f);
            CHECK(r2->overrides.size() == 2);
            if (r2->overrides.size() == 2) CHECK(r2->overrides[1].first == "name" && r2->overrides[1].second == "Bob Smith");
        }
        CHECK(b.Find("Plain") && !b.Find("Plain")->GetComponent<BehaviorTreeRunner>());
        // The same scene saved before behavior trees existed: no record, still loads.
        std::string older;
        std::size_t pos = 0;
        while (pos < text.size()) {
            std::size_t nl = text.find('\n', pos);
            if (nl == std::string::npos) nl = text.size();
            const std::string line = text.substr(pos, nl - pos);
            if (line.find("btrunner") == std::string::npos) older += line + "\n";
            pos = nl + 1;
        }
        Scene c("older");
        CHECK(SceneSerializer::Deserialize(c, older, &err));
        CHECK(c.Find("Guard") && !c.Find("Guard")->GetComponent<BehaviorTreeRunner>());
        CHECK(c.Find("Plain") != nullptr);
    }

    TEST_MAIN_RESULT();
}
