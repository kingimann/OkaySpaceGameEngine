// ObjectRef / ComponentRef, run order settings, fast scene queries, spatial grid, removal cost (docs/ecs_upgrade.md).
#include "test_framework.hpp"
#include <Okay.hpp>
#include <string>
#include <unordered_map>
#include <vector>

using namespace okay;

namespace {

std::vector<std::string>* g_log = nullptr;
struct First : Behaviour { void Update(float) override { if (g_log) g_log->push_back("First"); } void LateUpdate(float) override { if (g_log) g_log->push_back("First.late"); } };
struct Second : Behaviour { void Update(float) override { if (g_log) g_log->push_back("Second"); } void LateUpdate(float) override { if (g_log) g_log->push_back("Second.late"); } };
struct Third : Behaviour { void Update(float) override { if (g_log) g_log->push_back("Third"); } };
struct Needle : Behaviour {};
struct OwnOrder : Behaviour { int ExecutionOrder() const override { return 50; } void Update(float) override { if (g_log) g_log->push_back("OwnOrder"); } };

std::string Join(const std::vector<std::string>& v) { std::string s; for (auto& x : v) s += (s.empty() ? "" : ",") + x; return s; }

void TestObjectRef() {
    RUN_SUITE("ObjectRef / ComponentRef");
    Scene scene("s");
    GameObject* a = scene.CreateGameObject("A");
    First* f = a->AddComponent<First>();
    Second* sec = a->AddComponent<Second>();
    ObjectRef ro(a);
    ComponentRef<First> rf(f);
    ComponentRef<Second> rs(sec);
    ObjectRef none;
    CHECK(!none && none.Get() == nullptr && none.Id() == 0);
    CHECK(ro.Get() == a && ro.Alive());
    CHECK(rf.Get() == f && rf.Object() == a && rf.Alive());
    CHECK(ComponentRef<First>().Get() == nullptr);
    CHECK(rf.Id() != rs.Id() && rf.Id() != 0);
    CHECK(ObjectRef(a) == ro);
    std::unordered_map<ObjectRef, int> map; map[ro] = 5; CHECK(map[ObjectRef(a)] == 5);

    // A removed component reads as null at once; the object and the other component stay.
    a->RemoveComponent(f);
    CHECK(rf.Get() == nullptr && !rf);
    CHECK(rs.Get() == sec && ro.Get() == a);
    // A new component made afterwards never answers to the old ref, even at the same address.
    First* f2 = a->AddComponent<First>();
    CHECK(rf.Get() == nullptr && f2->ComponentId() != rf.Id());

    // Destroy is deferred: the ref still resolves until the frame ends, and says it is pending.
    scene.Destroy(a);
    CHECK(ro.Get() == a && ro.IsDestroyPending() && !ro.Alive());
    scene.Update(0.016f);
    CHECK(ro.Get() == nullptr && !ro && !ro.IsDestroyPending());
    CHECK(rs.Get() == nullptr && !rs.Alive());
    // New objects never take the id of the old.
    GameObject* b = scene.CreateGameObject("B");
    CHECK(ObjectRef(b) != ro && ro.Get() == nullptr);
    // A copy of a ref made before the destroy is null too.
    ObjectRef copy = ro; CHECK(copy.Get() == nullptr);
}

void TestRunOrder() {
    RUN_SUITE("run order settings");
    ExecutionOrderSettings::Clear();
    CHECK(!ExecutionOrderSettings::Any());
    std::vector<std::string> log; g_log = &log;
    {
        Scene scene("s");
        GameObject* g = scene.CreateGameObject("G");
        g->AddComponent<First>(); g->AddComponent<Second>(); g->AddComponent<Third>();
        scene.Update(0.016f); log.clear();
        scene.Update(0.016f);
        CHECK(Join(log) == "First,Second,Third,First.late,Second.late");   // added order by default
        // Second runs early, Third late-before... then First late.
        CHECK(ExecutionOrderSettings::Set("Second", RunWhen::Early));
        CHECK(ExecutionOrderSettings::Set("First", RunWhen::Late));
        log.clear(); scene.Update(0.016f);
        CHECK(Join(log) == "Second,Third,First,Second.late,First.late");
        CHECK(ExecutionOrderSettings::Get("First") == RunWhen::Late);
        CHECK(ExecutionOrderSettings::All().size() == 2);
        CHECK(std::string(ExecutionOrderSettings::Label(RunWhen::Early)) == "Run early");
        // New components made after the setting honour it too.
        g->AddComponent<OwnOrder>();   // its own number is 50: after Third (0), before the "late" First (1000)
        log.clear(); scene.Update(0.016f); log.clear(); scene.Update(0.016f);
        CHECK(Join(log) == "Second,Third,OwnOrder,First,Second.late,First.late");
        // Switching a setting back restores the added order.
        ExecutionOrderSettings::Set("Second", RunWhen::Normal);
        ExecutionOrderSettings::Set("First", RunWhen::Normal);
        log.clear(); scene.Update(0.016f);
        CHECK(Join(log) == "First,Second,Third,OwnOrder,First.late,Second.late");
        // Same input, same order: repeat.
        std::vector<std::string> again = log; log.clear(); scene.Update(0.016f);
        CHECK(log == again);
    }
    // Bad names are refused; the project file round trip keeps the settings.
    CHECK(!ExecutionOrderSettings::Set("", RunWhen::Early));
    CHECK(!ExecutionOrderSettings::Set("a b", RunWhen::Early));
    ExecutionOrderSettings::Set("Lifetime", RunWhen::Late);
    ExecutionOrderSettings::Set("Rigidbody2D", RunWhen::Early);
    const std::string text = ProjectLists::Write();
    CHECK(text.find("run_order_Lifetime=late\n") != std::string::npos);
    CHECK(text.find("run_order_Rigidbody2D=early\n") != std::string::npos);
    ExecutionOrderSettings::Clear();
    CHECK(ProjectLists::Apply(text) >= 2);
    CHECK(ExecutionOrderSettings::Get("Lifetime") == RunWhen::Late && ExecutionOrderSettings::Get("Rigidbody2D") == RunWhen::Early);
    CHECK(!ProjectLists::ApplyLine("run_order_Foo", "sideways"));
    ProjectLists::Reset();
    CHECK(!ExecutionOrderSettings::Any());
    g_log = nullptr;
}

void TestQueries() {
    RUN_SUITE("scene queries and removal");
    Scene scene("s");
    std::vector<GameObject*> objs;
    for (int i = 0; i < 600; ++i) { GameObject* g = scene.CreateGameObject("o"); g->AddComponent<First>(); objs.push_back(g); }
    CHECK(scene.FindObjectOfType<Needle>() == nullptr);
    Needle* n = objs[500]->AddComponent<Needle>();
    CHECK(scene.FindObjectOfType<Needle>() == n);          // long walk builds the list
    CHECK(scene.FindObjectOfType<Needle>() == n);          // answered from it
    Needle* n2 = objs[3]->AddComponent<Needle>();
    CHECK(scene.FindObjectOfType<Needle>() == n2);         // an addition is seen at once
    objs[3]->RemoveComponent(n2);
    CHECK(scene.FindObjectOfType<Needle>() == n);          // and so is a removal
    scene.Destroy(objs[500]);
    scene.Update(0.016f);
    CHECK(scene.FindObjectOfType<Needle>() == nullptr);

    // Removing a component of every object keeps the update list right (hint slots).
    scene.Update(0.016f);
    for (GameObject* g : objs) if (!g->IsDestroyed() && g != objs[500]) g->RemoveComponent<First>();
    std::vector<std::string> log; g_log = &log;
    scene.Update(0.016f);
    CHECK(log.empty());
    g_log = nullptr;
    // Add again before the first frame (pending list), remove half of them, the rest still run.
    std::vector<First*> fs;
    for (GameObject* g : objs) if (g != objs[500]) fs.push_back(g->AddComponent<First>());
    for (std::size_t i = 0; i < fs.size(); i += 2) fs[i]->gameObject->RemoveComponent(fs[i]);
    log.clear(); g_log = &log;
    scene.Update(0.016f);
    g_log = nullptr;
    int ran = 0; for (auto& l : log) if (l == "First") ++ran;
    CHECK(ran == static_cast<int>(fs.size() / 2));

    // Timing per type is off by default and lists the types when on.
    CHECK(!scene.ComponentTimingOn() && scene.LastFrameTimings().empty());
    scene.SetComponentTiming(true);
    scene.Update(0.016f);
    CHECK(!scene.LastFrameTimings().empty());
    CHECK(scene.LastFrameTimings()[0].type == "First" && scene.LastFrameTimings()[0].calls >= ran);
    scene.SetComponentTiming(false);
    CHECK(scene.LastFrameTimings().empty());
}

void TestGrid() {
    RUN_SUITE("radius queries");
    Scene scene("s");
    std::vector<GameObject*> objs;
    for (int x = 0; x < 20; ++x)
        for (int y = 0; y < 20; ++y) {
            GameObject* g = scene.CreateGameObject("p");
            g->transform->localPosition = Vec3{float(x), float(y), 0.0f};
            objs.push_back(g);
        }
    const Vec3 c{5.2f, 7.1f, 0.0f};
    auto brute = FindObjectsInRadius(scene, c, 3.0f);
    CHECK(!brute.empty());
    for (std::size_t i = 1; i < brute.size(); ++i)
        CHECK((brute[i - 1]->transform->Position() - c).SqrMagnitude() <= (brute[i]->transform->Position() - c).SqrMagnitude());
    SceneGrid grid; grid.Build(scene, 3.0f);
    CHECK(grid.Count() == 400 && !grid.IsStale(scene));
    CHECK(grid.Query(c, 3.0f) == brute);
    CHECK(grid.Query(c, 0.0f).empty() || grid.Query(c, 0.0f).size() == 1);
    CHECK(grid.Query(c, -1.0f).empty());
    CHECK(grid.Query(Vec3{1000, 1000, 0}, 3.0f).empty());
    CHECK(grid.Query(c, 1000.0f).size() == 400);            // huge radius: every object
    // Inactive objects are skipped unless asked for; a change makes the grid stale.
    objs[0]->active = false;
    CHECK(FindObjectsInRadius(scene, Vec3{0, 0, 0}, 0.1f).empty());
    CHECK(FindObjectsInRadius(scene, Vec3{0, 0, 0}, 0.1f, true).size() == 1);
    scene.CreateGameObject("new");
    CHECK(grid.IsStale(scene));
    scene.Destroy(objs[5]); scene.Update(0.016f);
    CHECK(grid.Query(Vec3{0, 5, 0}, 0.1f).empty());       // destroyed since Build: not returned
}

} // namespace

int main() {
    TestObjectRef();
    TestRunOrder();
    TestQueries();
    TestGrid();
    TEST_MAIN_RESULT();
}
