// Prefab variants: a prefab file that says "variant of <base>" and lists its own
// overrides, added objects and removed objects. Chains, cycles, damaged files, older
// loaders, saving a variant from an instance, apply to a variant and the validator.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <filesystem>
#include <fstream>
#include <set>
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
static bool Has(const std::vector<std::string>& v, const std::string& needle) {
    for (const auto& w : v) if (w.find(needle) != std::string::npos) return true;
    return false;
}
static bool Contains(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }
static GameObject* Child(GameObject* g, const std::string& name) {
    if (!g) return nullptr;
    for (Transform* c : g->transform->Children()) if (c->gameObject->name == name) return c->gameObject;
    return nullptr;
}
static std::vector<std::string> ChildNames(GameObject* go) {
    std::vector<std::string> n;
    for (Transform* c : go->transform->Children()) n.push_back(c->gameObject->name);
    return n;
}
static std::size_t Live(Scene& s) {
    std::size_t n = 0;
    for (const auto& o : s.Objects()) if (!o->IsDestroyed()) ++n;
    return n;
}
static void Walk(GameObject* g, std::vector<GameObject*>& v) {
    v.push_back(g);
    for (Transform* c : g->transform->Children()) Walk(c->gameObject, v);
}

struct Files {
    fs::path dir;
    std::string base;
    explicit Files(const std::string& name) {
        dir = fs::temp_directory_path() / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
        base = (dir / "robot.okayprefab").string();
    }
    ~Files() { std::error_code ec; fs::remove_all(dir, ec); }
    std::string P(const std::string& f) const { return (dir / f).string(); }
};

// robot.okayprefab: 0 Robot (blue sprite), 1 Arm, 2 Head (with a box collider).
static void MakeRobot(const std::string& path, char headGlyph = 'h') {
    Scene s("r");
    GameObject* r = s.CreateGameObject("Robot");
    r->AddComponent<SpriteRenderer>()->color = Color{0, 0, 1, 1};
    GameObject* arm = s.CreateGameObject("Arm");
    arm->transform->SetParent(r->transform, false);
    arm->AddComponent<SpriteRenderer>()->glyph = 'a';
    GameObject* head = s.CreateGameObject("Head");
    head->transform->SetParent(r->transform, false);
    head->AddComponent<SpriteRenderer>()->glyph = headGlyph;
    head->AddComponent<BoxCollider2D>();
    SceneSerializer::SaveObjectToFile(*r, path);
}

// A variant of `base`: recolour the root, tag the head, drop the arm, add an antenna
// (with a tip) under the head.
static std::string VariantText(const std::string& base) {
    std::string t = "okayscene 3\n";
    t += "variant " + Q(base) + "\n";
    t += "override 0 " + Q("sprite 82 1 0 0 1 1 1") + "\n";
    t += "override 2 " + Q("tag \"head\"") + "\n";
    t += "remove 1\n";
    t += "gameobject 0 \"Antenna\"\n  under 2\n  transform 0 1 0 0 0 0 1 1 1 1\n  sprite 94 1 1 0 1 1 1\n  parent -1\nend\n";
    t += "gameobject 1 \"Tip\"\n  transform 0 0.5 0 0 0 0 1 1 1 1\n  parent 0\nend\n";
    return t;
}

static void TestVariantInstantiate() {
    RUN_SUITE("variant instantiate");
    Files f("okay_prefab_var_a");
    MakeRobot(f.base);
    Write(f.P("v1.okayprefab"), VariantText(f.base));
    Scene s("s");
    std::string err;
    std::vector<std::string> warn;
    GameObject* v = SceneSerializer::InstantiateFromFile(s, f.P("v1.okayprefab"), &err, &warn);   // the loader routes it
    CHECK(v != nullptr);
    if (!v) { std::cerr << "  err: " << err << "\n"; return; }
    CHECK(v->name == "Robot" && v->prefabSource == f.P("v1.okayprefab"));
    CHECK(v->GetComponent<SpriteRenderer>()->glyph == 'R' && v->GetComponent<SpriteRenderer>()->color.r == 1);
    CHECK((ChildNames(v) == std::vector<std::string>{"Head"}));              // Arm removed
    GameObject* head = Child(v, "Head");
    CHECK(head && head->tag == "head");
    CHECK(head && head->GetComponent<BoxCollider2D>() != nullptr);            // kept from the base
    CHECK(head && head->GetComponent<SpriteRenderer>()->glyph == 'h');
    GameObject* ant = Child(head, "Antenna");
    CHECK(ant && ant->GetComponent<SpriteRenderer>()->glyph == '^');
    CHECK(ant && Child(ant, "Tip") != nullptr);
    CHECK_NEAR(ant ? ant->transform->localPosition.y : 0, 1, 1e-5);
    CHECK(Live(s) == 4);
    CHECK(v->prefabOverrides.empty());                                        // nothing differs from the variant
    CHECK(warn.empty());

    // The base file itself is untouched and still instantiates on its own.
    GameObject* b = SceneSerializer::InstantiateFromFile(s, f.base);
    CHECK(b && ChildNames(b).size() == 2 && b->GetComponent<SpriteRenderer>()->glyph != 'R');

    // Variant of a variant: the second file's indices count the first one's result
    // (0 Robot, 1 Head, 2 Antenna, 3 Tip).
    std::string t2 = "okayscene 3\nvariant " + Q(f.P("v1.okayprefab")) + "\n";
    t2 += "override 1 " + Q("tag \"head-v2\"") + "\nremove 3\n";
    t2 += "gameobject 0 \"Dish\"\n  under 0\n  parent -1\nend\n";
    Write(f.P("v2.okayprefab"), t2);
    std::string t3 = "okayscene 3\nvariant " + Q(f.P("v2.okayprefab")) + "\noverride 3 " + Q("tag \"dish\"") + "\n";
    Write(f.P("v3.okayprefab"), t3);
    GameObject* v3 = SceneSerializer::InstantiateFromFile(s, f.P("v3.okayprefab"), &err, &warn);
    CHECK(v3 != nullptr);
    if (v3) {
        CHECK((ChildNames(v3) == std::vector<std::string>{"Head", "Dish"}));
        CHECK(Child(v3, "Head")->tag == "head-v2");
        CHECK(Child(Child(v3, "Head"), "Antenna") && !Child(Child(Child(v3, "Head"), "Antenna"), "Tip"));
        CHECK(Child(v3, "Dish")->tag == "dish");
        CHECK(v3->GetComponent<SpriteRenderer>()->glyph == 'R');                // from v1 through v2
        CHECK(v3->prefabSource == f.P("v3.okayprefab"));
    }

    // Instance level overrides work on top of a variant (resolved numbering).
    std::vector<GameObject::PrefabOverride> ov{{1, "tag \"instance\""}};
    GameObject* v1b = SceneSerializer::InstantiatePrefab(s, f.P("v1.okayprefab"), ov, &err, &warn);
    CHECK(v1b && Child(v1b, "Head")->tag == "instance" && v1b->prefabOverrides.size() == 1);

    // An older loader (no variant support) skips the records it does not know, with
    // warnings, and loads the objects the variant file lists.
    Scene old("old");
    std::vector<std::string> w2;
    GameObject* a = SceneSerializer::InstantiateFromText(old, VariantText(f.base), &err, &w2);
    CHECK(a != nullptr && a->name == "Antenna");
    CHECK(Has(w2, "variant") && Has(w2, "override") && Has(w2, "remove") && Has(w2, "under"));
    CHECK(Live(old) == 2);

    // Stale delta records are dropped with readable warnings; the instance is still built.
    std::string st = "okayscene 3\nvariant " + Q(f.base) + "\n";
    st += "remove 9\noverride 9 " + Q("tag \"x\"") + "\nremove 0\nunset 9 " + Q("sprite") + "\n";
    st += "gameobject 0 \"Lost\"\n  under 9\n  parent -1\nend\n";
    Write(f.P("stale.okayprefab"), st);
    warn.clear();
    GameObject* sv = SceneSerializer::InstantiateFromFile(s, f.P("stale.okayprefab"), &err, &warn);
    CHECK(sv != nullptr);
    CHECK(Has(warn, "remove of object 9 dropped") && Has(warn, "override for object 9 dropped") && Has(warn, "root object cannot be removed"));
    CHECK(Has(warn, "Lost") && Has(warn, "no longer exists"));
    if (sv) CHECK(ChildNames(sv).size() == 2);   // the base's Arm and Head, untouched

    // A variant of a prefab that holds nested prefabs: edit and remove inside it.
    {
        Scene w("w");
        GameObject* wh = w.CreateGameObject("Wheel");
        GameObject* hub = w.CreateGameObject("Hub");
        hub->transform->SetParent(wh->transform, false);
        SceneSerializer::SaveObjectToFile(*wh, f.P("wheel.okayprefab"));
        std::string car = "okayscene 3\ngameobject 0 \"Car\"\n  parent -1\nend\n";
        car += "gameobject 1 \"Wheel L\"\n  nestedprefab " + Q(f.P("wheel.okayprefab")) + "\n  parent 0\nend\n";
        car += "gameobject 2 \"Wheel R\"\n  nestedprefab " + Q(f.P("wheel.okayprefab")) + "\n  parent 0\nend\n";
        Write(f.P("car.okayprefab"), car);
        // expanded car: 0 Car, 1 Wheel L, 2 Hub, 3 Wheel R, 4 Hub
        Write(f.P("racer.okayprefab"), "okayscene 3\nvariant " + Q(f.P("car.okayprefab")) + "\noverride 2 " + Q("tag \"left\"") + "\nremove 3\n");
        GameObject* racer = SceneSerializer::InstantiateFromFile(s, f.P("racer.okayprefab"), &err, &warn);
        CHECK(racer != nullptr);
        if (racer) {
            CHECK((ChildNames(racer) == std::vector<std::string>{"Wheel L"}));
            GameObject* wl = Child(racer, "Wheel L");
            CHECK(wl && wl->prefabSource == f.P("wheel.okayprefab"));             // the nested link survives
            CHECK(wl && Child(wl, "Hub") && Child(wl, "Hub")->tag == "left");
            bool tagged = false;
            if (wl) for (const auto& o : wl->prefabOverrides) if (o.object == 1 && o.line.rfind("tag", 0) == 0) tagged = true;
            CHECK(tagged);                                                          // ...and records the variant's edit
        }
    }
}


static void TestVariantWithNestedObject() {
    RUN_SUITE("a variant that adds a nested prefab");
    Files f("okay_prefab_var_g");
    MakeRobot(f.base);
    {
        Scene w("w");
        GameObject* wh = w.CreateGameObject("Wheel");
        GameObject* hub = w.CreateGameObject("Hub");
        hub->transform->SetParent(wh->transform, false);
        SceneSerializer::SaveObjectToFile(*wh, f.P("wheel.okayprefab"));
    }
    std::string t = "okayscene 3\nvariant robot.okayprefab\n";   // both paths relative to this file
    t += "gameobject 0 \"Tire\"\n  nestedprefab wheel.okayprefab\n  override 0 " + Q("tag \"tire\"") + "\n  under 2\n  parent -1\nend\n";
    t += "gameobject 1 \"Bolt\"\n  under 1\n  parent 0\nend\n";   // listed under the nested prefab, attached at its Hub
    Write(f.P("rolling.okayprefab"), t);
    Scene s("s");
    std::string err;
    std::vector<std::string> warn;
    GameObject* r = SceneSerializer::InstantiateFromFile(s, f.P("rolling.okayprefab"), &err, &warn);
    CHECK(r != nullptr);
    if (!r) { std::cerr << "  err: " << err << "\n"; return; }
    GameObject* tire = Child(Child(r, "Head"), "Tire");
    CHECK(tire && tire->tag == "tire" && tire->prefabSource == f.P("wheel.okayprefab"));
    CHECK(tire && Child(tire, "Hub") && Child(Child(tire, "Hub"), "Bolt"));
    CHECK(Live(s) == 6);   // Robot, Arm, Head, Tire, Hub, Bolt
    CHECK(PrefabTools::ValidateAsset(f.P("rolling.okayprefab")).Ok());
}

static void TestVariantErrors() {
    RUN_SUITE("variant cycles, depth and damaged files");
    Files f("okay_prefab_var_b");
    MakeRobot(f.base);
    Scene s("s");
    s.CreateGameObject("Marker");
    const std::size_t base = Live(s);
    std::string err;
    std::vector<std::string> warn;

    Write(f.P("a.okayprefab"), "okayscene 3\nvariant " + Q(f.P("b.okayprefab")) + "\n");
    Write(f.P("b.okayprefab"), "okayscene 3\nvariant " + Q(f.P("a.okayprefab")) + "\n");
    CHECK(SceneSerializer::InstantiateFromFile(s, f.P("a.okayprefab"), &err, &warn) == nullptr);
    CHECK(Contains(err, "cycle") && Contains(err, "a.okayprefab") && Contains(err, "b.okayprefab"));
    Write(f.P("self.okayprefab"), "okayscene 3\nvariant " + Q(f.P("self.okayprefab")) + "\n");
    err.clear();
    CHECK(PrefabTools::Instantiate(s, f.P("self.okayprefab"), {}, &err, &warn) == nullptr);
    CHECK(Contains(err, "cycle"));
    CHECK(Live(s) == base);

    // A chain at the limit works, one more is an error.
    auto chain = [&](int n) {   // n files: v0 is a variant of v1 ... the last is variant of the robot
        for (int i = 0; i < n; ++i)
            Write(f.P("v" + std::to_string(i) + ".okayprefab"),
                  "okayscene 3\nvariant " + Q(i + 1 < n ? f.P("v" + std::to_string(i + 1) + ".okayprefab") : f.base) + "\n");
    };
    chain(PrefabTools::kMaxDepth - 1);   // + the robot = kMaxDepth files in the chain
    GameObject* ok = PrefabTools::Instantiate(s, f.P("v0.okayprefab"), {}, &err, &warn);
    CHECK(ok != nullptr);
    const std::size_t after = Live(s);
    chain(PrefabTools::kMaxDepth + 4);
    err.clear();
    CHECK(PrefabTools::Instantiate(s, f.P("v0.okayprefab"), {}, &err, &warn) == nullptr);
    CHECK(Contains(err, "deeper than"));
    CHECK(Live(s) == after);

    // Missing base: an error naming it, the scene unchanged.
    Write(f.P("orphan.okayprefab"), "okayscene 3\nvariant " + Q(f.P("gone.okayprefab")) + "\noverride 0 " + Q("tag \"x\"") + "\n");
    err.clear();
    CHECK(SceneSerializer::InstantiateFromFile(s, f.P("orphan.okayprefab"), &err, &warn) == nullptr);
    CHECK(Contains(err, "gone.okayprefab") && Contains(err, "base"));
    CHECK(Live(s) == after);

    // A damaged variant, a damaged base, a variant with no variant name.
    const std::string good = VariantText(f.base);
    for (std::size_t cut : {good.size() - 8, good.size() / 2, (std::size_t)30, (std::size_t)5}) {
        Write(f.P("cutv.okayprefab"), good.substr(0, cut));
        err.clear();
        const std::size_t b0 = Live(s);
        GameObject* r = SceneSerializer::InstantiateFromFile(s, f.P("cutv.okayprefab"), &err, &warn);
        if (!r) CHECK(Live(s) == b0);   // never a half-built tree
    }
    Write(f.P("cutbase.okayprefab"), "okayscene 3\ngameobject 0 \"Base\"\n  transform 0 0 0 0 0");
    Write(f.P("ofcut.okayprefab"), "okayscene 3\nvariant " + Q(f.P("cutbase.okayprefab")) + "\n");
    err.clear();
    CHECK(PrefabTools::Instantiate(s, f.P("ofcut.okayprefab"), {}, &err, &warn) == nullptr);
    CHECK(Contains(err, "base") && Contains(err, "cut short"));
    CHECK(Live(s) == after);
    Write(f.P("noname.okayprefab"), "okayscene 3\nvariant\ngameobject 0 \"A\"\n  parent -1\nend\n");
    err.clear();
    PrefabTools::Instantiate(s, f.P("noname.okayprefab"), {}, &err, &warn);   // must not crash
    Write(f.P("empty.okayprefab"), "");
    CHECK(PrefabTools::Instantiate(s, f.P("empty.okayprefab"), {}, &err, &warn) == nullptr);
    Write(f.P("hdr.okayprefab"), "okayscene 0\n");
    CHECK(PrefabTools::Instantiate(s, f.P("hdr.okayprefab"), {}, &err, &warn) == nullptr);
    Write(f.P("dup.okayprefab"), "okayscene 3\ngameobject 0 \"A\"\n  parent -1\nend\ngameobject 0 \"B\"\n  parent -1\nend\n");
    CHECK(PrefabTools::Instantiate(s, f.P("dup.okayprefab"), {}, &err, &warn) == nullptr);
    CHECK(Contains(err, "duplicate"));
    // A parent loop in the file is refused, not walked forever.
    Write(f.P("loop.okayprefab"), "okayscene 3\nvariant " + Q(f.base) + "\ngameobject 0 \"A\"\n  parent 1\nend\ngameobject 1 \"B\"\n  parent 0\nend\n");
    err.clear();
    PrefabTools::Instantiate(s, f.P("loop.okayprefab"), {}, &err, &warn);   // must not hang or crash
}

static void TestVariantAuthoring() {
    RUN_SUITE("saving a variant from an instance");
    Files f("okay_prefab_var_c");
    MakeRobot(f.base);
    Scene s("s");
    GameObject* inst = SceneSerializer::InstantiateFromFile(s, f.base);
    // Edit: recolour the root, destroy the arm, tag the head, add a gun under the head.
    inst->GetComponent<SpriteRenderer>()->glyph = 'R';
    s.Destroy(Child(inst, "Arm"));
    s.FlushDestroyed();
    GameObject* head = Child(inst, "Head");
    head->tag = "head";
    head->RemoveComponent<BoxCollider2D>();
    GameObject* gun = s.CreateGameObject("Gun");
    gun->transform->SetParent(head->transform, false);
    gun->AddComponent<SpriteRenderer>()->glyph = 'g';
    GameObject* barrel = s.CreateGameObject("Barrel");
    barrel->transform->SetParent(gun->transform, false);

    std::string err;
    CHECK(PrefabTools::SaveVariant(*inst, f.P("gunner.okayprefab"), &err));
    const std::string text = Read(f.P("gunner.okayprefab"));
    CHECK(Contains(text, "variant ") && Contains(text, "robot.okayprefab"));
    CHECK(Contains(text, "remove 1"));
    CHECK(Contains(text, "override 0") && Contains(text, "override 2"));
    CHECK(Contains(text, "unset 2 \"boxcollider2d\" 0"));
    CHECK(Contains(text, "gameobject 0 \"Gun\"") && Contains(text, "under 2") && Contains(text, "gameobject 1 \"Barrel\""));
    CHECK(!fs::exists(f.P("gunner.okayprefab.tmp")));

    Scene t("t");
    std::vector<std::string> warn;
    GameObject* g = SceneSerializer::InstantiateFromFile(t, f.P("gunner.okayprefab"), &err, &warn);
    CHECK(g != nullptr);
    if (g) {
        CHECK((ChildNames(g) == std::vector<std::string>{"Head"}));
        GameObject* h2 = Child(g, "Head");
        CHECK(h2 && h2->tag == "head" && h2->GetComponent<BoxCollider2D>() == nullptr);
        CHECK(h2 && Child(h2, "Gun") && Child(Child(h2, "Gun"), "Barrel"));
        CHECK(g->GetComponent<SpriteRenderer>()->glyph == 'R');
        CHECK(Live(t) == 4);
        // The new instance matches the edited one: no differences against its own variant.
        PrefabDiff d;
        CHECK(PrefabTools::Diff(*g, d, &err) && d.Empty());
    }
    CHECK(!PrefabTools::SaveVariant(*inst, f.base, &err) && Contains(err, "base"));      // never over its own base
    CHECK(Read(f.base).find("variant") == std::string::npos);
    CHECK(!PrefabTools::SaveVariant(*s.CreateGameObject("Loose"), f.P("x.okayprefab"), &err));

    // A variant instance saved as a variant: a chain.
    GameObject* g1 = SceneSerializer::InstantiateFromFile(s, f.P("gunner.okayprefab"));
    Child(g1, "Head")->tag = "second";
    CHECK(PrefabTools::SaveVariant(*g1, f.P("gunner2.okayprefab"), &err));
    CHECK(Contains(Read(f.P("gunner2.okayprefab")), "gunner.okayprefab"));
    GameObject* g2 = SceneSerializer::InstantiateFromFile(t, f.P("gunner2.okayprefab"));
    CHECK(g2 && Child(g2, "Head")->tag == "second" && Child(Child(g2, "Head"), "Gun"));
    // Editing the base later reaches both levels (the head glyph).
    MakeRobot(f.base, 'H');
    GameObject* g3 = SceneSerializer::InstantiateFromFile(t, f.P("gunner2.okayprefab"));
    CHECK(g3 && Child(g3, "Head")->GetComponent<SpriteRenderer>()->glyph == 'H');
}

static void TestVariantApplyAndReapply() {
    RUN_SUITE("apply and re-apply with variants");
    Files f("okay_prefab_var_d");
    MakeRobot(f.base);
    Write(f.P("v1.okayprefab"), VariantText(f.base));
    const std::string baseBytes = Read(f.base);
    Scene s("s");
    std::string err;
    GameObject* v = SceneSerializer::InstantiateFromFile(s, f.P("v1.okayprefab"), &err);
    CHECK(v != nullptr);
    if (!v) return;

    // Differences against the variant file, at record and field level.
    Child(v, "Head")->tag = "changed";                                            // object 1: a record the variant overrides
    v->transform->localPosition = Vec3{2, 0, 0};                                  // object 0: a record from the base
    Child(Child(v, "Head"), "Antenna")->GetComponent<SpriteRenderer>()->color = Color{0, 1, 0, 1};   // object 2: an added object
    PrefabDiff d;
    CHECK(PrefabTools::Diff(*v, d, &err));
    CHECK(d.records.size() == 3);
    const PrefabRecordDiff* pos = d.Find(0, "transform");
    CHECK(pos && pos->kind == PrefabRecordDiff::Kind::Changed && pos->fields.size() == 1 && pos->fields[0].name == "position.x");
    CHECK(d.Find(1, "tag") && d.Find(2, "sprite"));

    // Apply the head tag: the variant file changes (its existing override), the base does not.
    CHECK(PrefabTools::ApplyToSource(*v, 1, "tag", "", 0, &err));
    std::string vt = Read(f.P("v1.okayprefab"));
    CHECK(Contains(vt, "tag \\\"changed\\\"") && !Contains(vt, "tag \\\"head\\\""));
    CHECK(Read(f.base) == baseBytes);
    // Apply the root's position: the base owns the root, so it becomes an override of the variant.
    CHECK(PrefabTools::ApplyToSource(*v, 0, "transform", "position.x", 0, &err));
    vt = Read(f.P("v1.okayprefab"));
    CHECK(Contains(vt, "override 0 \"transform 2 0 0 0 0 0 1 1 1 1\""));
    CHECK(Read(f.base) == baseBytes);
    // Apply a record of an object the variant itself adds: edited in place in that object.
    CHECK(PrefabTools::ApplyToSource(*v, 2, "sprite", "", 0, &err));
    vt = Read(f.P("v1.okayprefab"));
    CHECK(Contains(vt, "gameobject 0 \"Antenna\""));
    CHECK(vt.find("override 2 \"sprite") == std::string::npos);                    // not stored as an override
    CHECK(PrefabTools::Diff(*v, d, &err) && d.Empty());
    CHECK(v->prefabOverrides.empty());
    Scene t("t");
    GameObject* again = SceneSerializer::InstantiateFromFile(t, f.P("v1.okayprefab"), &err);
    CHECK(again != nullptr);
    if (again) {
        CHECK(Child(again, "Head")->tag == "changed");
        CHECK_NEAR(again->transform->localPosition.x, 2, 1e-5);
        CHECK(Child(Child(again, "Head"), "Antenna")->GetComponent<SpriteRenderer>()->color.g == 1);
    }
    // A record the instance lost: stored as an unset in the variant.
    Child(v, "Head")->RemoveComponent<BoxCollider2D>();
    CHECK(PrefabTools::Diff(*v, d, &err) && d.Find(1, "boxcollider2d") && d.Find(1, "boxcollider2d")->kind == PrefabRecordDiff::Kind::Removed);
    CHECK(PrefabTools::ApplyToSource(*v, 1, "boxcollider2d", "", 0, &err));
    CHECK(Contains(Read(f.P("v1.okayprefab")), "unset 2 \"boxcollider2d\" 0"));   // the variant file counts the base's objects (Head is 2 there)
    GameObject* nc = SceneSerializer::InstantiateFromFile(t, f.P("v1.okayprefab"));
    CHECK(nc && Child(nc, "Head")->GetComponent<BoxCollider2D>() == nullptr);
    CHECK(Read(f.base) == baseBytes);

    // Revert on a variant instance goes back to the variant file's state.
    Child(v, "Head")->tag = "oops";
    GameObject* rv = PrefabTools::RevertOverride(s, *v, 1, "tag", "", 0, &err);
    s.FlushDestroyed();
    CHECK(rv != nullptr && Child(rv, "Head")->tag == "changed");

    // Re-apply after the base changes: the base's edit arrives, the variant's stay.
    MakeRobot(f.base, 'Z');
    GameObject* fresh = SceneSerializer::ReapplyPrefab(s, *rv, &err);
    s.FlushDestroyed();
    CHECK(fresh != nullptr);
    if (fresh) {
        CHECK(Child(fresh, "Head")->GetComponent<SpriteRenderer>()->glyph == 'Z');
        CHECK(Child(fresh, "Head")->tag == "changed");
        CHECK(Child(Child(fresh, "Head"), "Antenna") != nullptr);
    }
}

static void TestGuidRule() {
    RUN_SUITE("instance ids");
    Files f("okay_prefab_var_e");
    MakeRobot(f.base);
    Write(f.P("v1.okayprefab"), VariantText(f.base));
    Scene s("s");
    GameObject* a = PrefabTools::Instantiate(s, f.P("v1.okayprefab"));
    GameObject* b = PrefabTools::Instantiate(s, f.P("v1.okayprefab"));
    CHECK(a && b);
    if (!a || !b) return;
    std::vector<GameObject*> wa, wb;
    Walk(a, wa); Walk(b, wb);
    std::set<std::string> all;
    for (auto* g : wa) all.insert(g->Guid());
    for (auto* g : wb) all.insert(g->Guid());
    CHECK(all.size() == wa.size() + wb.size());                               // never shared between instances

    // The overrides hold object indices, not ids, so both instances take the same list...
    std::vector<GameObject::PrefabOverride> ov{{1, "tag \"ov\""}};
    GameObject* c = PrefabTools::Instantiate(s, f.P("v1.okayprefab"), ov);
    GameObject* d = PrefabTools::Instantiate(s, f.P("v1.okayprefab"), ov);
    CHECK(c && d && c->Guid() != d->Guid() && Child(c, "Head")->tag == "ov" && Child(d, "Head")->tag == "ov");
    // ...and re-applying keeps each instance's own ids, object for object.
    std::vector<GameObject*> before;
    Walk(c, before);
    std::vector<std::string> ids;
    for (auto* g : before) ids.push_back(g->Guid());
    std::string err;
    GameObject* fresh = PrefabTools::Reapply(s, *c, &err);
    s.FlushDestroyed();
    CHECK(fresh != nullptr);
    if (fresh) {
        std::vector<GameObject*> after;
        Walk(fresh, after);
        CHECK(after.size() == ids.size());
        for (std::size_t i = 0; i < after.size() && i < ids.size(); ++i) CHECK(after[i]->Guid() == ids[i]);
        CHECK(Child(fresh, "Head")->tag == "ov");
        CHECK(s.FindByGuid(ids[0]) == fresh);
    }
    // When the prefab gains an object, the old objects keep their ids and the new one gets its own.
    std::string t = Read(f.P("v1.okayprefab"));
    t += "gameobject 2 \"Wing\"\n  under 0\n  parent -1\nend\n";
    Write(f.P("v1.okayprefab"), t);
    GameObject* grown = PrefabTools::Reapply(s, *fresh, &err);
    s.FlushDestroyed();
    CHECK(grown != nullptr);
    if (grown) {
        CHECK(grown->Guid() == ids[0]);
        CHECK(Child(grown, "Head")->Guid() == ids[1]);
        GameObject* wing = Child(grown, "Wing");
        CHECK(wing != nullptr);
        std::set<std::string> idset(ids.begin(), ids.end());
        CHECK(wing && !idset.count(wing->Guid()) && wing->Guid().size() == 32);
    }
}

static void TestValidator() {
    RUN_SUITE("asset validator (variants)");
    Files f("okay_prefab_var_f");
    MakeRobot(f.base);
    Write(f.P("ok.okayprefab"), VariantText(f.base));
    PrefabReport ok = PrefabTools::ValidateAsset(f.P("ok.okayprefab"));
    CHECK(ok.Ok() && ok.issues.empty() && ok.files.size() == 2);

    std::string st = "okayscene 3\nvariant " + Q(f.base) + "\n";
    st += "remove 9\noverride 9 " + Q("tag \"x\"") + "\nremove 0\n";
    st += "gameobject 0 \"Lost\"\n  under 9\n  parent -1\nend\n";
    Write(f.P("stale.okayprefab"), st);
    PrefabReport r = PrefabTools::ValidateAsset(f.P("stale.okayprefab"));
    CHECK(r.Ok());   // all warnings: the file still loads
    CHECK(r.Count(PrefabIssue::Severity::Warning) == 4);
    const std::string msg = r.ToString();
    CHECK(Contains(msg, "remove of object 9") && Contains(msg, "override for object 9") && Contains(msg, "'Lost'") && Contains(msg, "stale.okayprefab"));

    Write(f.P("nobase.okayprefab"), "okayscene 3\nvariant " + Q(f.P("none.okayprefab")) + "\n");
    PrefabReport nb = PrefabTools::ValidateAsset(f.P("nobase.okayprefab"));
    CHECK(!nb.Ok() && Contains(nb.ToString(), "none.okayprefab") && Contains(nb.ToString(), "base prefab"));

    Write(f.P("a.okayprefab"), "okayscene 3\nvariant " + Q(f.P("b.okayprefab")) + "\n");
    Write(f.P("b.okayprefab"), "okayscene 3\nvariant " + Q(f.P("a.okayprefab")) + "\n");
    PrefabReport cyc = PrefabTools::ValidateAsset(f.P("a.okayprefab"));
    CHECK(!cyc.Ok() && Contains(cyc.ToString(), "cycle"));

    // A base that holds a bad file is found through the chain, and named.
    Write(f.P("cut.okayprefab"), "okayscene 3\ngameobject 0 \"X\"\n  transform 0 0 0");
    Write(f.P("overcut.okayprefab"), "okayscene 3\nvariant " + Q(f.P("cut.okayprefab")) + "\n");
    PrefabReport oc = PrefabTools::ValidateAsset(f.P("overcut.okayprefab"));
    CHECK(!oc.Ok() && Contains(oc.ToString(), "cut.okayprefab") && Contains(oc.ToString(), "cut short"));
}

int main() {
    TestVariantInstantiate();
    TestVariantWithNestedObject();
    TestVariantErrors();
    TestVariantAuthoring();
    TestVariantApplyAndReapply();
    TestGuidRule();
    TestValidator();
    TEST_MAIN_RESULT();
}
