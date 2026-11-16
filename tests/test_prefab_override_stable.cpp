// Prefab overrides, variant records and nested-prefab records name their target object by a
// stable name path ("Head", "Body/Head", "Wheel[1]"), not by its depth-first index, so adding,
// removing or reordering objects in the prefab no longer moves a change onto another object
// (issue #888, finding 58 and its addenda). Old index-keyed records still load, resolve once
// against the current prefab, and are written back in the new form.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

static std::string Q(const std::string& s) {
    std::string o = "\"";
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
    return o + "\"";
}
static void Write(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}
static std::string Read(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
static bool Contains(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }
static bool Has(const std::vector<std::string>& v, const std::string& needle) {
    for (const auto& w : v) if (Contains(w, needle)) return true;
    return false;
}
static void Dump(const char* what, const std::vector<std::string>& v) {
    for (const auto& w : v) std::cerr << "    " << what << ": " << w << "\n";
}
static GameObject* Child(GameObject* g, const std::string& name) {
    if (!g) return nullptr;
    for (Transform* c : g->transform->Children()) if (c->gameObject->name == name) return c->gameObject;
    return nullptr;
}
static std::vector<std::string> ChildNames(GameObject* go) {
    std::vector<std::string> n;
    if (!go) return n;
    for (Transform* c : go->transform->Children()) n.push_back(c->gameObject->name);
    return n;
}
static float X(GameObject* g) { return g ? g->transform->localPosition.x : -999.0f; }

struct Dir {
    fs::path dir;
    explicit Dir(const std::string& name) {
        dir = fs::temp_directory_path() / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~Dir() { std::error_code ec; fs::remove_all(dir, ec); }
    std::string P(const std::string& f) const { return (dir / f).string(); }
};

// door.okayprefab: Door > [children...]. Each child is (name, x).
static void MakeDoor(const std::string& path, const std::vector<std::pair<std::string, float>>& kids) {
    Scene s("d");
    GameObject* r = s.CreateGameObject("Door");
    for (const auto& k : kids) {
        GameObject* c = s.CreateGameObject(k.first);
        c->transform->SetParent(r->transform, false);
        c->transform->localPosition = Vec3{k.second, 0, 0};
    }
    SceneSerializer::SaveObjectToFile(*r, path);
    SceneSerializer::ClearPrefabCache();
}

// ---- Instances: Reapply after the prefab gained, lost or reordered children ----------------

static void TestInstanceReapply() {
    RUN_SUITE("instance override stays on Handle after the prefab changes");
    Dir d("okay_po_inst");
    const std::string door = d.P("door.okayprefab");
    MakeDoor(door, {{"Frame", 0}, {"Handle", 1}});
    Scene s("s");
    GameObject* inst = SceneSerializer::InstantiateFromFile(s, door);
    CHECK(inst != nullptr);
    if (!inst) return;
    Child(inst, "Handle")->transform->localPosition = Vec3{5, 5, 5};
    CHECK(SceneSerializer::CapturePrefabOverrides(*inst));
    CHECK(inst->prefabOverrides.size() == 1);
    if (!inst->prefabOverrides.empty()) CHECK(inst->prefabOverrides[0].path == "Handle");
    const std::string sceneText = SceneSerializer::Serialize(s);
    CHECK(Contains(sceneText, "override \"Handle\" \"transform 5 5 5"));
    CHECK(!Contains(sceneText, "override 2 "));

    // A child is added in front: Glass, Frame, Handle.
    MakeDoor(door, {{"Glass", 0}, {"Frame", 0}, {"Handle", 1}});
    std::string err;
    std::vector<std::string> warn;
    GameObject* r1 = SceneSerializer::ReapplyPrefab(s, *inst, &err, &warn);
    CHECK(r1 != nullptr);
    CHECK((ChildNames(r1) == std::vector<std::string>{"Glass", "Frame", "Handle"}));
    CHECK_NEAR(X(Child(r1, "Handle")), 5, 1e-5);
    CHECK_NEAR(X(Child(r1, "Frame")), 0, 1e-5);
    CHECK_NEAR(X(Child(r1, "Glass")), 0, 1e-5);
    s.FlushDestroyed();

    // The same through PrefabTools::Reapply after a reorder: Handle, Glass, Frame.
    MakeDoor(door, {{"Handle", 1}, {"Glass", 0}, {"Frame", 0}});
    GameObject* r2 = r1 ? PrefabTools::Reapply(s, *r1, &err, &warn) : nullptr;
    CHECK(r2 != nullptr);
    CHECK_NEAR(X(Child(r2, "Handle")), 5, 1e-5);
    CHECK_NEAR(X(Child(r2, "Frame")), 0, 1e-5);
    s.FlushDestroyed();

    // The scene saved before the prefab edits loads and re-applies onto Handle too.
    {
        Scene s2("s2");
        std::vector<std::string> w2;
        CHECK(SceneSerializer::Deserialize(s2, sceneText, &err, &w2));
        GameObject* i2 = s2.Find("Door");
        CHECK(i2 && i2->prefabOverrides.size() == 1 && i2->prefabOverrides[0].path == "Handle");
        GameObject* r3 = i2 ? SceneSerializer::ReapplyPrefab(s2, *i2, &err) : nullptr;
        CHECK(r3 != nullptr);
        CHECK_NEAR(X(Child(r3, "Handle")), 5, 1e-5);
        CHECK_NEAR(X(Child(r3, "Glass")), 0, 1e-5);
        CHECK_NEAR(X(Child(r3, "Frame")), 0, 1e-5);
    }

    // The target is removed from the prefab: the override is dropped with a warning and
    // lands on nothing else.
    MakeDoor(door, {{"Glass", 0}, {"Frame", 0}});
    warn.clear();
    GameObject* r4 = r2 ? SceneSerializer::ReapplyPrefab(s, *r2, &err, &warn) : nullptr;
    CHECK(r4 != nullptr);
    CHECK_NEAR(X(Child(r4, "Frame")), 0, 1e-5);
    CHECK_NEAR(X(Child(r4, "Glass")), 0, 1e-5);
    CHECK(Has(warn, "Handle"));
    if (!Has(warn, "Handle")) Dump("warn", warn);
    s.FlushDestroyed();

    // Renamed in the prefab (Handle -> Knob): a name path cannot follow a rename, so the
    // override is dropped with a warning (documented), never applied to another object.
    MakeDoor(door, {{"Frame", 0}, {"Knob", 1}});
    Scene s3("s3");
    GameObject* i3 = SceneSerializer::InstantiatePrefab(s3, door, {GameObject::PrefabOverride{2, "transform 5 5 5 0 0 0 1 1 1 1", "Handle"}}, &err, &warn);
    CHECK(i3 != nullptr);
    CHECK_NEAR(X(Child(i3, "Knob")), 1, 1e-5);
    CHECK_NEAR(X(Child(i3, "Frame")), 0, 1e-5);
}

// Two children with the same name: the second one is "Wheel[1]".
static void TestSameNameSiblings() {
    RUN_SUITE("same-name siblings");
    Dir d("okay_po_same");
    const std::string p = d.P("cart.okayprefab");
    MakeDoor(p, {{"Wheel", 0}, {"Wheel", 1}});
    Scene s("s");
    GameObject* inst = SceneSerializer::InstantiateFromFile(s, p);
    CHECK(inst != nullptr);
    if (!inst) return;
    inst->transform->Children()[1]->localPosition = Vec3{7, 0, 0};
    CHECK(SceneSerializer::CapturePrefabOverrides(*inst));
    CHECK(inst->prefabOverrides.size() == 1 && inst->prefabOverrides[0].path == "Wheel[1]");
    MakeDoor(p, {{"Axle", 0}, {"Wheel", 0}, {"Wheel", 1}});
    GameObject* r = SceneSerializer::ReapplyPrefab(s, *inst);
    CHECK(r != nullptr);
    if (r) {
        CHECK_NEAR(r->transform->Children()[2]->localPosition.x, 7, 1e-5);
        CHECK_NEAR(r->transform->Children()[1]->localPosition.x, 0, 1e-5);
        CHECK_NEAR(r->transform->Children()[0]->localPosition.x, 0, 1e-5);
    }
    // Paths are written and read back with escapes (a '/' inside a name).
    std::vector<std::string> names{"Root", "a/b", "c"};
    std::vector<int> parents{-1, 0, 1};
    const auto paths = PrefabTools::ObjectPaths(names, parents);
    CHECK(paths.size() == 3 && paths[0] == "." && paths[1] == "a\\/b" && paths[2] == "a\\/b/c");
    CHECK(PrefabTools::CanonicalObjectPath("Wheel[0]") == "Wheel");
    CHECK(PrefabTools::CanonicalObjectPath("a\\/b/c") == "a\\/b/c");
}

// ---- Variants ---------------------------------------------------------------------------

// robot.okayprefab: Robot > [Shield] Arm, Head (Head has a box collider).
static void MakeRobot(const std::string& path, bool shield) {
    Scene s("r");
    GameObject* r = s.CreateGameObject("Robot");
    if (shield) s.CreateGameObject("Shield")->transform->SetParent(r->transform, false);
    GameObject* arm = s.CreateGameObject("Arm");
    arm->transform->SetParent(r->transform, false);
    GameObject* head = s.CreateGameObject("Head");
    head->transform->SetParent(r->transform, false);
    head->AddComponent<BoxCollider2D>();
    SceneSerializer::SaveObjectToFile(*r, path);
    SceneSerializer::ClearPrefabCache();
}

static void CheckRobotVariant(GameObject* v, bool shield) {
    CHECK(v != nullptr);
    if (!v) return;
    const std::vector<std::string> want = shield ? std::vector<std::string>{"Shield", "Head"} : std::vector<std::string>{"Head"};
    CHECK(ChildNames(v) == want);                                    // Arm removed, Shield kept
    GameObject* head = Child(v, "Head");
    CHECK(head && head->tag == "head");                              // override on Head
    CHECK(head && head->GetComponent<BoxCollider2D>() == nullptr);   // unset on Head
    CHECK(head && Child(head, "Antenna") != nullptr);                // under Head
    if (shield) {
        GameObject* sh = Child(v, "Shield");
        CHECK(sh && sh->tag.empty() && ChildNames(sh).empty());
    }
}

static void TestVariantRecords() {
    RUN_SUITE("variant override / unset / remove / under after a Shield is added before Arm");
    Dir d("okay_po_var");
    const std::string base = d.P("robot.okayprefab");
    MakeRobot(base, false);
    // Saved by SaveVariant from an edited instance (the editor's Create Variant).
    Scene s("s");
    GameObject* inst = SceneSerializer::InstantiateFromFile(s, base);
    CHECK(inst != nullptr);
    if (!inst) return;
    Child(inst, "Head")->tag = "head";
    Child(inst, "Head")->RemoveComponent<BoxCollider2D>();
    s.Destroy(Child(inst, "Arm"));
    s.FlushDestroyed();
    GameObject* ant = s.CreateGameObject("Antenna");
    ant->transform->SetParent(Child(inst, "Head")->transform, false);
    std::string err;
    CHECK(PrefabTools::SaveVariant(*inst, d.P("red.okayprefab"), &err));
    if (!err.empty()) std::cerr << "    err: " << err << "\n";
    const std::string vt = Read(d.P("red.okayprefab"));
    CHECK(Contains(vt, "override \"Head\" "));
    CHECK(Contains(vt, "unset \"Head\" \"boxcollider2d\" 0"));
    CHECK(Contains(vt, "remove \"Arm\""));
    CHECK(Contains(vt, "under \"Head\""));
    std::vector<std::string> warn;
    CheckRobotVariant(SceneSerializer::InstantiateFromFile(s, d.P("red.okayprefab"), &err, &warn), false);

    // The base gains a Shield ahead of Arm; the variant file is unchanged.
    MakeRobot(base, true);
    warn.clear();
    CheckRobotVariant(SceneSerializer::InstantiateFromFile(s, d.P("red.okayprefab"), &err, &warn), true);
    if (!warn.empty()) Dump("warn", warn);
    CHECK(warn.empty());

    // A hand-written variant in the new form reads the same.
    std::string t = "okayscene 3\nvariant " + Q(base) + "\n";
    t += "override \"Head\" " + Q("tag \"head\"") + "\n";
    t += "unset \"Head\" \"boxcollider2d\" 0\nremove \"Arm\"\n";
    t += "gameobject 0 \"Antenna\"\n  under \"Head\"\n  parent -1\nend\n";
    Write(d.P("hand.okayprefab"), t);
    SceneSerializer::ClearPrefabCache();
    CheckRobotVariant(SceneSerializer::InstantiateFromFile(s, d.P("hand.okayprefab"), &err, &warn), true);
}

// ---- Nested prefabs ----------------------------------------------------------------------

static void MakeWheel(const std::string& path, const std::vector<std::string>& kids) {
    Scene s("w");
    GameObject* r = s.CreateGameObject("Wheel");
    for (const auto& k : kids) s.CreateGameObject(k)->transform->SetParent(r->transform, false);
    SceneSerializer::SaveObjectToFile(*r, path);
    SceneSerializer::ClearPrefabCache();
}

static void TestNested() {
    RUN_SUITE("nested: the Hub tag stays on Hub after a Tire is added");
    Dir d("okay_po_nest");
    const std::string wheel = d.P("wheel.okayprefab");
    const std::string car = d.P("car.okayprefab");
    MakeWheel(wheel, {"Rim", "Hub"});
    {
        Scene s("s");
        GameObject* c = s.CreateGameObject("Car");
        GameObject* w = SceneSerializer::InstantiateFromFile(s, wheel);
        CHECK(w != nullptr);
        if (!w) return;
        w->name = "Wheel L";
        w->transform->SetParent(c->transform, false);
        Child(w, "Hub")->tag = "spinner";
        std::string err;
        CHECK(PrefabTools::SavePrefab(*c, car, &err));
        if (!err.empty()) std::cerr << "    err: " << err << "\n";
    }
    CHECK(Contains(Read(car), "override \"Hub\" "));
    auto check = [&](const std::vector<std::string>& expectKids, bool hubThere) {
        SceneSerializer::ClearPrefabCache();
        Scene s("s");
        std::string err;
        std::vector<std::string> warn;
        GameObject* c = SceneSerializer::InstantiateFromFile(s, car, &err, &warn);
        CHECK(c != nullptr);
        GameObject* w = Child(c, "Wheel L");
        CHECK(w != nullptr);
        CHECK(ChildNames(w) == expectKids);
        for (const auto& k : expectKids) {
            GameObject* g = Child(w, k);
            CHECK(g && g->tag == (k == "Hub" ? "spinner" : ""));
        }
        if (!hubThere) { CHECK(Has(warn, "Hub")); if (!Has(warn, "Hub")) Dump("warn", warn); }
    };
    check({"Rim", "Hub"}, true);
    MakeWheel(wheel, {"Tire", "Rim", "Hub"});
    check({"Tire", "Rim", "Hub"}, true);
    MakeWheel(wheel, {"Hub"});
    check({"Hub"}, true);
    MakeWheel(wheel, {"Tire", "Rim"});
    check({"Tire", "Rim"}, false);   // the target is gone: dropped with a warning, Rim untouched
}

// ---- Old index-keyed files ---------------------------------------------------------------

static std::string OldScene(const std::string& door) {
    std::string t = "okayscene 3\n";
    t += "gameobject 0 \"Door\"\n  prefab " + Q(door) + "\n  override 2 " + Q("transform 5 5 5 0 0 0 1 1 1 1") +
         "\n  parent -1\n  transform 0 0 0 0 0 0 1 1 1 1\nend\n";
    t += "gameobject 1 \"Frame\"\n  parent 0\n  transform 0 0 0 0 0 0 1 1 1 1\nend\n";
    t += "gameobject 2 \"Handle\"\n  parent 0\n  transform 5 5 5 0 0 0 1 1 1 1\nend\n";
    return t;
}

static void TestLegacy() {
    RUN_SUITE("old index-keyed records");
    Dir d("okay_po_old");
    const std::string door = d.P("door.okayprefab");
    MakeDoor(door, {{"Frame", 0}, {"Handle", 1}});
    std::string err;
    {
        // Unchanged prefab: loads as before, and is written back in the new form.
        Scene s("s");
        std::vector<std::string> warn;
        CHECK(SceneSerializer::Deserialize(s, OldScene(door), &err, &warn));
        if (!warn.empty()) Dump("warn", warn);
        CHECK(!Has(warn, "override"));
        GameObject* i = s.Find("Door");
        CHECK(i && i->prefabOverrides.size() == 1);
        if (i && !i->prefabOverrides.empty()) CHECK(i->prefabOverrides[0].path == "Handle");
        const std::string saved = SceneSerializer::Serialize(s);
        CHECK(Contains(saved, "override \"Handle\" \"transform 5 5 5"));
        CHECK(!Contains(saved, "override 2 "));
        GameObject* r = i ? SceneSerializer::ReapplyPrefab(s, *i, &err) : nullptr;
        CHECK_NEAR(X(Child(r, "Handle")), 5, 1e-5);
        CHECK_NEAR(X(Child(r, "Frame")), 0, 1e-5);
    }
    {
        // The prefab changed before the old scene was opened: the old index resolves
        // against the current prefab (the version it was made from is not known), and a
        // one-line warning says the target looks wrong.
        MakeDoor(door, {{"Glass", 0}, {"Frame", 0}, {"Handle", 1}});
        Scene s("s");
        std::vector<std::string> warn;
        CHECK(SceneSerializer::Deserialize(s, OldScene(door), &err, &warn));
        CHECK(Has(warn, "old prefab override"));
        if (!Has(warn, "old prefab override")) Dump("warn", warn);
    }
    {
        // An old index-keyed variant: same result as before while the base is unchanged.
        const std::string base = d.P("robot.okayprefab");
        MakeRobot(base, false);
        std::string t = "okayscene 3\nvariant " + Q(base) + "\n";
        t += "override 2 " + Q("tag \"head\"") + "\nunset 2 \"boxcollider2d\" 0\nremove 1\n";
        t += "gameobject 0 \"Antenna\"\n  under 2\n  parent -1\nend\n";
        const std::string vp = d.P("old_variant.okayprefab");
        Write(vp, t);
        SceneSerializer::ClearPrefabCache();
        Scene s("s");
        std::vector<std::string> warn;
        GameObject* v = SceneSerializer::InstantiateFromFile(s, vp, &err, &warn);
        CheckRobotVariant(v, false);
        // Applying a change to the variant rewrites its file: the old records come out in
        // the new form.
        if (v) {
            Child(v, "Head")->isStatic = true;
            CHECK(PrefabTools::ApplyAllToSource(*v, &err));
            if (!err.empty()) std::cerr << "    err: " << err << "\n";
            const std::string nt = Read(vp);
            CHECK(Contains(nt, "override \"Head\" "));
            CHECK(Contains(nt, "unset \"Head\" \"boxcollider2d\" 0"));
            CHECK(Contains(nt, "remove \"Arm\""));
            CHECK(Contains(nt, "under \"Head\""));
            CHECK(!Contains(nt, "override 2 ") && !Contains(nt, "remove 1"));
            // and it now survives the Shield.
            MakeRobot(base, true);
            Scene s2("s2");
            GameObject* v2 = SceneSerializer::InstantiateFromFile(s2, vp, &err, &warn);
            CheckRobotVariant(v2, true);
            CHECK(v2 && Child(v2, "Head") && Child(v2, "Head")->isStatic);
        }
    }
}

int main() {
    TestInstanceReapply();
    TestSameNameSiblings();
    TestVariantRecords();
    TestNested();
    TestLegacy();
    TEST_MAIN_RESULT();
}
