// Stable object ids, sibling order and enabled flags across save/load, duplicate
// components, the component serializer registry, and prefab overrides.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

static std::string Round(Scene& from, Scene& to, std::vector<std::string>* warnings = nullptr) {
    std::string err;
    const std::string text = SceneSerializer::Serialize(from);
    if (!SceneSerializer::Deserialize(to, text, &err, warnings)) std::cerr << "  load failed: " << err << "\n";
    return text;
}

static bool Has(const std::vector<std::string>& v, const std::string& needle) {
    for (const auto& w : v) if (w.find(needle) != std::string::npos) return true;
    return false;
}

static std::vector<std::string> ChildNames(GameObject* go) {
    std::vector<std::string> n;
    for (Transform* c : go->transform->Children()) n.push_back(c->gameObject->name);
    return n;
}

static GameObject* Find(Scene& s, const std::string& name) {
    for (const auto& o : s.Objects()) if (o->name == name) return o.get();
    return nullptr;
}

static void TestGuids() {
    RUN_SUITE("object ids");
    Scene a("a");
    GameObject* x = a.CreateGameObject("X");
    GameObject* y = a.CreateGameObject("Y");
    std::set<std::string> ids{x->Guid(), y->Guid()};
    CHECK(x->Guid().size() == 32);
    CHECK(ids.size() == 2);

    Scene b("b");
    Round(a, b);
    CHECK(Find(b, "X") && Find(b, "X")->Guid() == x->Guid());
    CHECK(Find(b, "Y") && Find(b, "Y")->Guid() == y->Guid());
    CHECK(b.FindByGuid(y->Guid()) == Find(b, "Y"));
    CHECK(b.FindByGuid("nope") == nullptr);

    // Saving again gives the same ids (they are not regenerated).
    Scene c("c");
    Round(b, c);
    CHECK(Find(c, "X")->Guid() == x->Guid());

    // Duplicates get their own ids: Instantiate, a paste and a merge.
    GameObject* copy = a.Instantiate(*x);
    CHECK(copy && copy->Guid() != x->Guid());
    GameObject* pasted = SceneSerializer::InstantiateFromText(a, SceneSerializer::SerializeObject(*x));
    CHECK(pasted && pasted->Guid() != x->Guid() && pasted->Guid() != copy->Guid());
    const std::string text = SceneSerializer::Serialize(a);
    SceneSerializer::MergeFromText(a, text);
    std::set<std::string> all;
    for (const auto& o : a.Objects()) all.insert(o->Guid());
    CHECK(all.size() == a.Objects().size());   // every id still unique after the merge

    // Old files (no guid record) still load, and each object gets an id.
    Scene d("d");
    CHECK(SceneSerializer::Deserialize(d, "okayscene 3\ngameobject 0 \"Old\"\n  active 1\n  transform 0 0 0 0 0 0 1 1 1 1\n  parent -1\nend\n"));
    CHECK(Find(d, "Old") && Find(d, "Old")->Guid().size() == 32);

    // A repeated or malformed guid in a file is reported and replaced.
    std::vector<std::string> warn;
    Scene e("e");
    const std::string dup =
        "okayscene 3\n"
        "gameobject 0 \"A\"\n  guid \"abc123\"\n  transform 0 0 0 0 0 0 1 1 1 1\n  parent -1\nend\n"
        "gameobject 1 \"B\"\n  guid \"abc123\"\n  transform 0 0 0 0 0 0 1 1 1 1\n  parent -1\nend\n"
        "gameobject 2 \"C\"\n  guid \"NOT HEX!\"\n  transform 0 0 0 0 0 0 1 1 1 1\n  parent -1\nend\n";
    CHECK(SceneSerializer::Deserialize(e, dup, nullptr, &warn));
    CHECK(Find(e, "A")->Guid() == "abc123");
    CHECK(Find(e, "B")->Guid() != "abc123" && Find(e, "B")->Guid().size() == 32);
    CHECK(Find(e, "C")->Guid().size() == 32);
    CHECK(Has(warn, "duplicate guid"));
}

static void TestOrderAndFlags() {
    RUN_SUITE("sibling order and enabled flags");
    Scene a("a");
    GameObject* p = a.CreateGameObject("P");
    GameObject* k1 = a.CreateGameObject("K1");
    GameObject* k2 = a.CreateGameObject("K2");
    GameObject* k3 = a.CreateGameObject("K3");
    GameObject* k4 = a.CreateGameObject("K4");
    for (GameObject* k : {k1, k2, k3, k4}) k->transform->SetParent(p->transform, false);
    CHECK(ChildNames(p) == (std::vector<std::string>{"K1", "K2", "K3", "K4"}));
    p->transform->MoveChildToEdge(k4->transform, true);          // K4 K1 K2 K3
    p->transform->MoveChild(k1->transform, 1);                    // K4 K2 K1 K3
    const std::vector<std::string> want{"K4", "K2", "K1", "K3"};
    CHECK(ChildNames(p) == want);

    Scene b("b");
    Round(a, b);
    CHECK(ChildNames(Find(b, "P")) == want);

    // A reparent into the middle of an existing family.
    GameObject* late = a.CreateGameObject("Late");
    late->transform->SetParent(p->transform, false);
    p->transform->ReorderChild(late->transform, k2->transform, false);   // K4 Late K2 K1 K3
    Scene c("c");
    Round(a, c);
    CHECK(ChildNames(Find(c, "P")) == ChildNames(p));

    // Nested, with a second family reordered independently.
    GameObject* q = a.CreateGameObject("Q");
    GameObject* q1 = a.CreateGameObject("Q1");
    GameObject* q2 = a.CreateGameObject("Q2");
    q1->transform->SetParent(q->transform, false);
    q2->transform->SetParent(q->transform, false);
    q->transform->MoveChild(q2->transform, -1);
    Scene d("d");
    Round(a, d);
    CHECK(ChildNames(Find(d, "Q")) == (std::vector<std::string>{"Q2", "Q1"}));
    CHECK(ChildNames(Find(d, "P")) == ChildNames(p));
    // A second round trip changes nothing.
    Scene e("e");
    CHECK(Round(d, e) == SceneSerializer::Serialize(d));
    CHECK(SceneSerializer::Serialize(e) == SceneSerializer::Serialize(d));

    // An in-order scene writes no sibling records (the text stays as it was).
    Scene f("f");
    GameObject* fp = f.CreateGameObject("FP");
    for (int i = 0; i < 3; ++i) f.CreateGameObject("C" + std::to_string(i))->transform->SetParent(fp->transform, false);
    CHECK(SceneSerializer::Serialize(f).find("sibling") == std::string::npos);

    // A prefab (single object tree) keeps child order too.
    const std::string pre = SceneSerializer::SerializeObject(*p);
    Scene g("g");
    GameObject* inst = SceneSerializer::InstantiateFromText(g, pre);
    CHECK(inst && ChildNames(inst) == ChildNames(p));

    // Active flag and per-component enabled flags.
    Scene h("h");
    GameObject* o = h.CreateGameObject("O");
    o->active = false;
    auto* sr = o->AddComponent<SpriteRenderer>(); sr->enabled = false;
    auto* cam = o->AddComponent<Camera>();         cam->enabled = false;
    auto* box = o->AddComponent<BoxCollider2D>();  box->enabled = false;
    GameObject* o2 = h.CreateGameObject("O2");
    o2->AddComponent<SpriteRenderer>();
    o2->AddComponent<BoxCollider2D>();
    Scene i("i");
    Round(h, i);
    GameObject* lo = Find(i, "O");
    CHECK(lo && !lo->active);
    CHECK(lo->GetComponent<SpriteRenderer>() && !lo->GetComponent<SpriteRenderer>()->enabled);
    CHECK(lo->GetComponent<Camera>() && !lo->GetComponent<Camera>()->enabled);
    CHECK(lo->GetComponent<BoxCollider2D>() && !lo->GetComponent<BoxCollider2D>()->enabled);
    GameObject* lo2 = Find(i, "O2");
    CHECK(lo2->active && lo2->GetComponent<SpriteRenderer>()->enabled && lo2->GetComponent<BoxCollider2D>()->enabled);
    // Two colliders, only the second off.
    GameObject* two = h.CreateGameObject("Two");
    two->AddComponent<BoxCollider2D>();
    two->AddComponent<CircleCollider2D>()->enabled = false;
    Scene j("j");
    Round(h, j);
    CHECK(Find(j, "Two")->GetComponent<BoxCollider2D>() && Find(j, "Two")->GetComponent<BoxCollider2D>()->enabled);
    CHECK(Find(j, "Two")->GetComponent<CircleCollider2D>() && !Find(j, "Two")->GetComponent<CircleCollider2D>()->enabled);
    // A disabled-flag naming a component that is not there is ignored.
    Scene k("k");
    CHECK(SceneSerializer::Deserialize(k, "okayscene 3\ngameobject 0 \"A\"\n  transform 0 0 0 0 0 0 1 1 1 1\n  compoff \"Nothing\" 0\n  parent -1\nend\n"));
}

static void TestDuplicateComponents() {
    RUN_SUITE("duplicate components");
    // AddComponent twice is allowed at runtime; GetComponent returns the first.
    Scene a("a");
    GameObject* o = a.CreateGameObject("O");
    auto* first = o->AddComponent<SpriteRenderer>(); first->glyph = 'A';
    auto* second = o->AddComponent<SpriteRenderer>(); second->glyph = 'B';
    CHECK(o->GetComponent<SpriteRenderer>() == first);
    CHECK(o->GetComponents<SpriteRenderer>().size() == 2);
    // Saving writes both, in the order they were added.
    Scene b("b");
    Round(a, b);
    CHECK(Find(b, "O")->GetComponents<SpriteRenderer>().size() == 2);
    CHECK(Find(b, "O")->GetComponents<SpriteRenderer>()[0]->glyph == 'A');
    CHECK(Find(b, "O")->GetComponents<SpriteRenderer>()[1]->glyph == 'B');

    // Two records in a file load as two components, with no "only the first is saved" warning.
    std::vector<std::string> warn;
    Scene c("c");
    const std::string two =
        "okayscene 3\ngameobject 0 \"O\"\n  transform 0 0 0 0 0 0 1 1 1 1\n"
        "  sprite 65 1 1 1 1 1 1\n  sprite 66 1 1 1 1 1 1\n  parent -1\nend\n";
    CHECK(SceneSerializer::Deserialize(c, two, nullptr, &warn));
    CHECK(Find(c, "O")->GetComponents<SpriteRenderer>().size() == 2);
    CHECK(!Has(warn, "only the first is saved"));

    // Different types side by side are fine. Two colliders of one type are not: the
    // second is not saved and loading text that has both warns.
    Scene d("d");
    GameObject* cc = d.CreateGameObject("Cols");
    cc->AddComponent<BoxCollider2D>();
    cc->AddComponent<CircleCollider2D>();
    std::vector<std::string> w2;
    Scene e("e");
    Round(d, e, &w2);
    CHECK(Find(e, "Cols")->GetComponent<BoxCollider2D>() && Find(e, "Cols")->GetComponent<CircleCollider2D>());
    CHECK(w2.empty());
    // Components saved per instance (scripts, tweens, sync vars...) keep every copy.
    Scene f("f");
    GameObject* tw = f.CreateGameObject("Tweens");
    tw->AddComponent<TweenAnimation>();
    tw->AddComponent<TweenAnimation>();
    std::vector<std::string> w3;
    Scene g("g");
    Round(f, g, &w3);
    CHECK(Find(g, "Tweens")->GetComponents<TweenAnimation>().size() == 2);
    CHECK(w3.empty());
}

struct MarkComponent : Component { int value = 0; };

static void TestRegistry() {
    RUN_SUITE("component serializer registry");
    // Trail renderer now goes through the registry; its text is unchanged.
    Scene a("a");
    GameObject* o = a.CreateGameObject("Trail");
    auto* tr = o->AddComponent<TrailRenderer>();
    tr->time = 2.5f; tr->width = 0.7f; tr->emitting = false;
    const std::string text = SceneSerializer::Serialize(a);
    CHECK(text.find("  trailrenderer time=2.5") != std::string::npos || text.find("  trailrenderer") != std::string::npos);
    Scene b("b");
    CHECK(SceneSerializer::Deserialize(b, text));
    auto* tr2 = Find(b, "Trail")->GetComponent<TrailRenderer>();
    CHECK(tr2 != nullptr);
    if (tr2) { CHECK_NEAR(tr2->time, 2.5, 1e-6); CHECK_NEAR(tr2->width, 0.7, 1e-6); CHECK(!tr2->emitting); }
    CHECK(ComponentSerializerRegistry::Find("trailrenderer") != nullptr);
    CHECK(ComponentSerializerRegistry::Find("sprite") == nullptr);   // the old chain keeps its names

    // A new component registers itself and needs no change to SceneSerializer.cpp.
    ComponentSerializerRegistry::Entry e;
    e.name = "testmark";
    e.write = [](const GameObject& go, std::ostream& out) {
        if (auto* m = go.GetComponent<MarkComponent>()) out << "  testmark " << m->value << "\n";
    };
    e.read = [](GameObject& go, std::istream& in) { int v = 0; in >> v; go.AddComponent<MarkComponent>()->value = v; };
    ComponentSerializerRegistry::Register(e);
    Scene c("c");
    c.CreateGameObject("M")->AddComponent<MarkComponent>()->value = 42;
    Scene d("d");
    std::vector<std::string> warn;
    Round(c, d, &warn);
    CHECK(Find(d, "M")->GetComponent<MarkComponent>() && Find(d, "M")->GetComponent<MarkComponent>()->value == 42);
    CHECK(warn.empty());
    // A name nobody registered is skipped with a warning, as before.
    Scene f("f");
    std::vector<std::string> w2;
    CHECK(SceneSerializer::Deserialize(f, "okayscene 3\ngameobject 0 \"A\"\n  transform 0 0 0 0 0 0 1 1 1 1\n  neverheardof 1 2 3\n  parent -1\nend\n", nullptr, &w2));
    CHECK(Has(w2, "neverheardof"));
}

static void TestPrefabOverrides() {
    RUN_SUITE("prefab overrides");
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "okay_prefab_override_test";
    fs::create_directories(dir);
    const std::string path = (dir / "thing.okayprefab").string();

    Scene src("src");
    GameObject* root = src.CreateGameObject("Thing");
    root->AddComponent<SpriteRenderer>()->color = Color{1, 0, 0, 1};
    GameObject* arm = src.CreateGameObject("Arm");
    arm->transform->SetParent(root->transform, false);
    arm->AddComponent<SpriteRenderer>()->glyph = 'a';
    CHECK(SceneSerializer::SaveObjectToFile(*root, path));

    Scene live("live");
    GameObject* holder = live.CreateGameObject("Holder");
    live.CreateGameObject("Before")->transform->SetParent(holder->transform, false);
    GameObject* inst = SceneSerializer::InstantiateFromFile(live, path);
    GameObject* inst2 = SceneSerializer::InstantiateFromFile(live, path);
    CHECK(inst && inst2 && inst->prefabSource == path);
    live.CreateGameObject("After")->transform->SetParent(holder->transform, false);
    inst->transform->SetParent(holder->transform, false);
    holder->transform->ReorderChild(inst->transform, Find(live, "After")->transform, false);   // Before Thing After
    inst->name = "Thing (mine)";

    // Nothing differs yet: no overrides.
    CHECK(SceneSerializer::CapturePrefabOverrides(*inst2));
    CHECK(inst2->prefabOverrides.empty());

    // Edit the instance: move it, recolour the root, retag the arm.
    inst->transform->localPosition = Vec3{4, 5, 6};
    inst->GetComponent<SpriteRenderer>()->color = Color{0, 1, 0, 1};
    Find(live, "Arm")->tag = "limb";
    CHECK(SceneSerializer::CapturePrefabOverrides(*inst));
    bool sawTransform = false, sawSprite = false, sawTag = false;
    for (const auto& ov : inst->prefabOverrides) {
        if (ov.object == 0 && ov.line.rfind("transform", 0) == 0) sawTransform = true;
        if (ov.object == 0 && ov.line.rfind("sprite", 0) == 0) sawSprite = true;
        if (ov.object == 1 && ov.line.rfind("tag", 0) == 0) sawTag = true;
        CHECK(ov.line.rfind("prefab", 0) != 0);
    }
    CHECK(sawTransform && sawSprite && sawTag);
    CHECK(inst->prefabOverrides.size() == 3);

    // Overrides are saved with the scene and survive a duplicate.
    Scene reload("reload");
    Round(live, reload);
    CHECK(Find(reload, "Thing (mine)")->prefabOverrides.size() == 3);
    GameObject* dup = live.Instantiate(*inst);
    CHECK(dup && dup->prefabOverrides.size() == 3 && dup->Guid() != inst->Guid());

    // The prefab file changes: the root gains a tag and the arm a different glyph.
    root->tag = "from-prefab";
    arm->GetComponent<SpriteRenderer>()->glyph = 'z';
    CHECK(SceneSerializer::SaveObjectToFile(*root, path));

    const std::string guidRoot = inst->Guid(), guidArm = Find(live, "Arm") ? Find(live, "Arm")->Guid() : "";
    std::string err;
    GameObject* fresh = SceneSerializer::ReapplyPrefab(live, *inst, &err);
    CHECK(fresh != nullptr);
    live.FlushDestroyed();
    if (fresh) {
        // Prefab edits arrive...
        CHECK(fresh->tag == "from-prefab");
        // ...and the overrides are still applied.
        CHECK_NEAR(fresh->transform->localPosition.x, 4, 1e-5);
        CHECK_NEAR(fresh->GetComponent<SpriteRenderer>()->color.g, 1, 1e-5);
        CHECK_NEAR(fresh->GetComponent<SpriteRenderer>()->color.r, 0, 1e-5);
        GameObject* narm = fresh->transform->Children().empty() ? nullptr : fresh->transform->Children()[0]->gameObject;
        CHECK(narm && narm->tag == "limb");
        // The spot, name and ids carry over.
        CHECK(fresh->name == "Thing (mine)");
        CHECK(fresh->Guid() == guidRoot);
        CHECK(narm && narm->Guid() == guidArm);
        CHECK(fresh->transform->Parent() == holder->transform);
        CHECK(ChildNames(holder) == (std::vector<std::string>{"Before", "Thing (mine)", "After"}));
        CHECK(fresh->prefabOverrides.size() == 3);
    }
    CHECK(live.FindByGuid(guidRoot) == fresh);

    // A missing prefab leaves the instance alone.
    inst2->prefabSource = (dir / "gone.okayprefab").string();
    CHECK(SceneSerializer::ReapplyPrefab(live, *inst2, &err) == nullptr);
    CHECK(!err.empty() && !inst2->IsDestroyed());
    // A non-instance is refused.
    CHECK(SceneSerializer::ReapplyPrefab(live, *Find(live, "Holder"), &err) == nullptr);

    // An override for an object the prefab no longer has is dropped with a warning.
    std::vector<std::string> warn;
    std::vector<GameObject::PrefabOverride> stale{{7, "tag \"x\""}};
    GameObject* s = SceneSerializer::InstantiatePrefab(live, path, stale, nullptr, &warn);
    CHECK(s != nullptr && Has(warn, "no longer has it"));
    fs::remove_all(dir);
}

int main() {
    TestGuids();
    TestOrderAndFlags();
    TestDuplicateComponents();
    TestRegistry();
    TestPrefabOverrides();
    TEST_MAIN_RESULT();
}
