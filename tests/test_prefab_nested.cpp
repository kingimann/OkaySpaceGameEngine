// Nested prefabs: a prefab file that holds instances of other prefab files.
// Instantiate, overrides, re-apply, cycles, depth limit, missing and damaged files,
// scene round trip, unpack, queries, save and the asset validator.
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
static GameObject* Find(Scene& s, const std::string& name) {
    for (const auto& o : s.Objects()) if (o->name == name && !o->IsDestroyed()) return o.get();
    return nullptr;
}
static GameObject* Child(GameObject* g, const std::string& name) {
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
static bool HasOverride(const GameObject* g, int object, const std::string& startsWith) {
    for (const auto& ov : g->prefabOverrides) if (ov.object == object && ov.line.rfind(startsWith, 0) == 0) return true;
    return false;
}

struct Files {
    fs::path dir;
    std::string wheel, car;
    explicit Files(const std::string& name) {
        dir = fs::temp_directory_path() / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
        wheel = (dir / "wheel.okayprefab").string();
        car = (dir / "car.okayprefab").string();
    }
    ~Files() { std::error_code ec; fs::remove_all(dir, ec); }
    std::string P(const std::string& f) const { return (dir / f).string(); }
};

// wheel.okayprefab: Wheel (red sprite) with a child Hub.
static void MakeWheel(const std::string& path) {
    Scene s("w");
    GameObject* w = s.CreateGameObject("Wheel");
    w->AddComponent<SpriteRenderer>()->color = Color{1, 0, 0, 1};
    GameObject* hub = s.CreateGameObject("Hub");
    hub->transform->SetParent(w->transform, false);
    hub->AddComponent<SpriteRenderer>()->glyph = 'h';
    SceneSerializer::SaveObjectToFile(*w, path);
}

// car.okayprefab: Car with two nested wheels, each with its own overrides.
static void MakeCar(const std::string& path, const std::string& wheel) {
    std::string t = "okayscene 3\n";
    t += "gameobject 0 \"Car\"\n  transform 0 0 0 0 0 0 1 1 1 1\n  sprite 99 0 0 1 1 2 1 \"\"\n  parent -1\nend\n";
    t += "gameobject 1 \"Wheel L\"\n  nestedprefab " + Q(wheel) + "\n  override 0 " + Q("transform -1 0 0 0 0 0 1 1 1 1") + "\n  parent 0\nend\n";
    t += "gameobject 2 \"Wheel R\"\n  nestedprefab " + Q(wheel) + "\n  override 0 " + Q("transform 1 0 0 0 0 0 1 1 1 1") +
         "\n  override 1 " + Q("tag \"rim\"") + "\n  parent 0\nend\n";
    Write(path, t);
}

static void TestNestedInstantiate() {
    RUN_SUITE("nested instantiate");
    Files f("okay_prefab_nested_a");
    MakeWheel(f.wheel);
    MakeCar(f.car, f.wheel);
    Scene s("s");
    std::string err;
    std::vector<std::string> warn;
    GameObject* car = SceneSerializer::InstantiateFromFile(s, f.car, &err, &warn);   // the loader routes it
    CHECK(car != nullptr);
    CHECK(err.empty());
    if (!car) return;
    CHECK(Live(s) == 5);
    CHECK(car->name == "Car" && car->prefabSource == f.car);
    CHECK((ChildNames(car) == std::vector<std::string>{"Wheel L", "Wheel R"}));
    GameObject* wl = Child(car, "Wheel L");
    GameObject* wr = Child(car, "Wheel R");
    CHECK(wl && wr);
    if (!wl || !wr) return;
    CHECK(Child(wl, "Hub") && Child(wr, "Hub"));
    CHECK_NEAR(wl->transform->localPosition.x, -1, 1e-5);
    CHECK_NEAR(wr->transform->localPosition.x, 1, 1e-5);
    CHECK(wl->GetComponent<SpriteRenderer>() && wl->GetComponent<SpriteRenderer>()->color.r == 1);   // from the wheel file
    CHECK(Child(wr, "Hub")->tag == "rim");
    CHECK(Child(wl, "Hub")->tag != "rim");

    // The nested instances are linked to their own files, with the outer file's overrides.
    CHECK(wl->prefabSource == f.wheel && wr->prefabSource == f.wheel);
    CHECK(HasOverride(wl, 0, "transform"));
    CHECK(HasOverride(wr, 0, "transform") && HasOverride(wr, 1, "tag"));
    CHECK(car->prefabOverrides.empty());                       // nothing differs from the car file
    CHECK(Child(wl, "Hub")->prefabSource.empty());             // only roots carry a link

    // Two instantiations never share ids.
    GameObject* car2 = SceneSerializer::InstantiateFromFile(s, f.car);
    CHECK(car2 != nullptr);
    if (car2) {
        std::set<std::string> ids;
        std::vector<GameObject*> a, b;
        std::function<void(GameObject*, std::vector<GameObject*>&)> walk = [&](GameObject* g, std::vector<GameObject*>& v) {
            v.push_back(g);
            for (Transform* c : g->transform->Children()) walk(c->gameObject, v);
        };
        walk(car, a); walk(car2, b);
        for (auto* g : a) ids.insert(g->Guid());
        for (auto* g : b) ids.insert(g->Guid());
        CHECK(ids.size() == a.size() + b.size());
        CHECK(a.size() == 5 && b.size() == 5);
    }

    // Instance level overrides use the expanded depth-first numbering: 0 Car, 1 Wheel L,
    // 2 Hub, 3 Wheel R, 4 Hub.
    std::vector<GameObject::PrefabOverride> ov{{2, "tag \"left-hub\""}};
    GameObject* car3 = SceneSerializer::InstantiatePrefab(s, f.car, ov, &err, &warn);
    CHECK(car3 != nullptr);
    if (car3) {
        CHECK(Child(Child(car3, "Wheel L"), "Hub")->tag == "left-hub");
        CHECK(car3->prefabOverrides.size() == 1);
        // ...and the nested instance sees it as a difference from its own file.
        CHECK(HasOverride(Child(car3, "Wheel L"), 1, "tag"));
    }

    // A nested object listed under a nested prefab gets attached inside it.
    {
        std::string t = Read(f.car);
        t += "gameobject 3 \"Spoke\"\n  transform 0 0.5 0 0 0 0 1 1 1 1\n  under 1\n  parent 1\nend\n";   // under Wheel L's Hub
        Write(f.P("spoked.okayprefab"), t);
        Scene s2("s2");
        GameObject* sp = SceneSerializer::InstantiateFromFile(s2, f.P("spoked.okayprefab"), &err, &warn);
        CHECK(sp != nullptr);
        GameObject* hubL = sp ? Child(Child(sp, "Wheel L"), "Hub") : nullptr;
        CHECK(hubL && Child(hubL, "Spoke"));
        CHECK(Live(s2) == 6);
    }
}

static void TestNestedReapply() {
    RUN_SUITE("nested re-apply");
    Files f("okay_prefab_nested_b");
    MakeWheel(f.wheel);
    MakeCar(f.car, f.wheel);
    Scene s("s");
    GameObject* holder = s.CreateGameObject("Holder");
    s.CreateGameObject("Before")->transform->SetParent(holder->transform, false);
    GameObject* car = SceneSerializer::InstantiateFromFile(s, f.car);
    s.CreateGameObject("After")->transform->SetParent(holder->transform, false);
    car->transform->SetParent(holder->transform, false);
    holder->transform->ReorderChild(car->transform, Find(s, "After")->transform, false);   // Before Car After
    car->name = "My car";

    // Edit the instance: tint the left hub (inside a nested instance) and move the car.
    GameObject* hubL = Child(Child(car, "Wheel L"), "Hub");
    hubL->GetComponent<SpriteRenderer>()->color = Color{0, 1, 0, 1};
    car->transform->localPosition = Vec3{3, 0, 0};
    std::string err;
    CHECK(SceneSerializer::CapturePrefabOverrides(*car, &err));
    CHECK(HasOverride(car, 0, "transform"));
    CHECK(HasOverride(car, 2, "sprite"));       // the hub is object 2 of the expanded car
    CHECK(car->prefabOverrides.size() == 2);
    // The nested instance records the same edit against its own file.
    CHECK(HasOverride(Child(car, "Wheel L"), 1, "sprite"));

    // The wheel file changes: the hub gets a tag. Re-apply keeps the overrides and the
    // ids and brings the edit. (An override is a whole record, so the hub's sprite stays
    // the way the car's instance has it.)
    {
        Scene w("w");
        GameObject* wh = w.CreateGameObject("Wheel");
        wh->AddComponent<SpriteRenderer>()->color = Color{1, 0, 0, 1};
        GameObject* hub = w.CreateGameObject("Hub");
        hub->transform->SetParent(wh->transform, false);
        hub->AddComponent<SpriteRenderer>()->glyph = 'h';
        hub->tag = "from-wheel";
        SceneSerializer::SaveObjectToFile(*wh, f.wheel);
    }
    const std::string guidCar = car->Guid();
    const std::string guidHubR = Child(Child(car, "Wheel R"), "Hub")->Guid();
    std::vector<std::string> warn;
    GameObject* fresh = SceneSerializer::ReapplyPrefab(s, *car, &err, &warn);
    CHECK(fresh != nullptr);
    s.FlushDestroyed();
    if (fresh) {
        CHECK(fresh->name == "My car");
        CHECK(fresh->Guid() == guidCar);
        CHECK(fresh->transform->Parent() == holder->transform);
        CHECK((ChildNames(holder) == std::vector<std::string>{"Before", "My car", "After"}));
        CHECK_NEAR(fresh->transform->localPosition.x, 3, 1e-5);
        GameObject* newHubL = Child(Child(fresh, "Wheel L"), "Hub");
        CHECK(newHubL && newHubL->tag == "from-wheel");                                    // the edit arrived
        CHECK_NEAR(newHubL->GetComponent<SpriteRenderer>()->color.g, 1, 1e-5);             // the override stayed
        CHECK(Child(Child(fresh, "Wheel R"), "Hub")->Guid() == guidHubR);                 // ids carried over
        CHECK(Child(fresh, "Wheel L")->prefabSource == f.wheel);                           // links rebuilt
        CHECK(fresh->prefabOverrides.size() == 2);
    }
    CHECK(Live(s) == 3 + 5);   // Holder, Before, After and the five car objects

    // An instance whose nested file went away is left alone.
    fs::remove(f.wheel);
    err.clear();
    GameObject* again = SceneSerializer::ReapplyPrefab(s, *fresh, &err, &warn);
    CHECK(again == nullptr);
    CHECK(Contains(err, "wheel"));
    CHECK(!fresh->IsDestroyed());
    CHECK(Live(s) == 3 + 5);
}

static void TestCyclesAndLimits() {
    RUN_SUITE("cycles, depth, missing and damaged files");
    Files f("okay_prefab_nested_c");
    auto holder = [&](const std::string& name, const std::string& nestedFile) {
        std::string t = "okayscene 3\ngameobject 0 \"" + name + "\"\n  transform 0 0 0 0 0 0 1 1 1 1\n  parent -1\nend\n";
        if (!nestedFile.empty())
            t += "gameobject 1 \"Inner\"\n  nestedprefab " + Q(nestedFile) + "\n  parent 0\nend\n";
        return t;
    };
    Scene s("s");
    s.CreateGameObject("Marker");
    const std::size_t base = Live(s);
    std::string err;
    std::vector<std::string> warn;

    // A holds B holds A.
    Write(f.P("a.okayprefab"), holder("A", f.P("b.okayprefab")));
    Write(f.P("b.okayprefab"), holder("B", f.P("a.okayprefab")));
    CHECK(SceneSerializer::InstantiateFromFile(s, f.P("a.okayprefab"), &err, &warn) == nullptr);
    CHECK(Contains(err, "cycle") && Contains(err, "a.okayprefab") && Contains(err, "b.okayprefab"));
    CHECK(Live(s) == base);   // nothing half built
    // A holds A.
    Write(f.P("self.okayprefab"), holder("Self", f.P("self.okayprefab")));
    err.clear();
    CHECK(PrefabTools::Instantiate(s, f.P("self.okayprefab"), {}, &err, &warn) == nullptr);
    CHECK(Contains(err, "cycle"));
    CHECK(Live(s) == base);

    // A chain at the depth limit works, one level deeper is an error that says so.
    auto chain = [&](int n) {
        for (int i = 0; i < n; ++i)
            Write(f.P("c" + std::to_string(i) + ".okayprefab"),
                  holder("C" + std::to_string(i), i + 1 < n ? f.P("c" + std::to_string(i + 1) + ".okayprefab") : ""));
    };
    chain(PrefabTools::kMaxDepth);
    err.clear();
    GameObject* ok = PrefabTools::Instantiate(s, f.P("c0.okayprefab"), {}, &err, &warn);
    CHECK(ok != nullptr);
    CHECK(Live(s) == base + (std::size_t)PrefabTools::kMaxDepth);   // one object per level: each Inner is the next file's root
    const std::size_t after = Live(s);
    chain(PrefabTools::kMaxDepth + 1);
    err.clear();
    CHECK(PrefabTools::Instantiate(s, f.P("c0.okayprefab"), {}, &err, &warn) == nullptr);
    CHECK(Contains(err, "deeper than") && Contains(err, "c0.okayprefab"));
    CHECK(Live(s) == after);

    // Wide and deep: a blow-up is stopped by the size limit, not by running out of memory.
    for (int lvl = 0; lvl < 7; ++lvl) {
        std::string t = "okayscene 3\ngameobject 0 \"L" + std::to_string(lvl) + "\"\n  transform 0 0 0 0 0 0 1 1 1 1\n  parent -1\nend\n";
        if (lvl < 6)
            for (int k = 1; k <= 12; ++k)
                t += "gameobject " + std::to_string(k) + " \"K\"\n  nestedprefab " + Q(f.P("w" + std::to_string(lvl + 1) + ".okayprefab")) + "\n  parent 0\nend\n";
        Write(f.P("w" + std::to_string(lvl) + ".okayprefab"), t);
    }
    err.clear();
    CHECK(PrefabTools::Instantiate(s, f.P("w0.okayprefab"), {}, &err, &warn) == nullptr);
    CHECK(Contains(err, "more than"));
    CHECK(Live(s) == after);

    // A missing inner file: only that inner instance is skipped, with a warning.
    MakeWheel(f.wheel);
    MakeCar(f.car, f.wheel);
    std::string car = Read(f.car);
    const std::size_t at = car.rfind(Q(f.wheel));
    car.replace(at, Q(f.wheel).size(), Q(f.P("nowhere.okayprefab")));   // Wheel R now points at nothing
    Write(f.P("car2.okayprefab"), car);
    warn.clear(); err.clear();
    const std::size_t before2 = Live(s);
    GameObject* c2 = SceneSerializer::InstantiateFromFile(s, f.P("car2.okayprefab"), &err, &warn);
    CHECK(c2 != nullptr);
    if (c2) {
        CHECK((ChildNames(c2) == std::vector<std::string>{"Wheel L"}));
        CHECK(Child(c2, "Wheel L") && Child(Child(c2, "Wheel L"), "Hub"));
        CHECK(Live(s) == before2 + 3);
        CHECK(Has(warn, "Wheel R") && Has(warn, "nowhere.okayprefab") && Has(warn, "skipped"));
    }

    // A damaged inner file (cut off in the middle of an object): the same, skip that one.
    {
        const std::string wheelText = Read(f.wheel);
        Write(f.P("cut.okayprefab"), wheelText.substr(0, wheelText.rfind("end")));   // last object has no 'end'
        std::string t = Read(f.car);
        for (std::size_t p = t.find(Q(f.wheel)); p != std::string::npos; p = t.find(Q(f.wheel)))
            t.replace(p, Q(f.wheel).size(), Q(f.P("cut.okayprefab")));
        Write(f.P("car3.okayprefab"), t);
        warn.clear(); err.clear();
        const std::size_t b3 = Live(s);
        GameObject* c3 = SceneSerializer::InstantiateFromFile(s, f.P("car3.okayprefab"), &err, &warn);
        CHECK(c3 != nullptr);
        if (c3) { CHECK(ChildNames(c3).empty()); CHECK(Live(s) == b3 + 1); CHECK(Has(warn, "cut short")); }
    }

    // A damaged top-level file is an error and leaves no half-built tree.
    {
        const std::string carText = Read(f.car);
        for (std::size_t cut : {carText.size() / 3, carText.size() / 2, carText.size() - 5, (std::size_t)10, (std::size_t)0}) {
            Write(f.P("trunc.okayprefab"), carText.substr(0, cut));
            err.clear();
            const std::size_t b4 = Live(s);
            GameObject* r = SceneSerializer::InstantiateFromFile(s, f.P("trunc.okayprefab"), &err, &warn);
            // A cut that happens to end on an object boundary is a smaller, valid prefab; any
            // other cut must fail cleanly. Either way the scene is consistent.
            if (!r) { CHECK(!err.empty()); CHECK(Live(s) == b4); }
            else CHECK(Live(s) > b4);
        }
        Write(f.P("junk.okayprefab"), "okayscene 3\ngameobject 0 \"X\"\n  nestedprefab\n  parent -1\nend\n");
        err.clear();
        const std::size_t b5 = Live(s);
        PrefabTools::Instantiate(s, f.P("junk.okayprefab"), {}, &err, &warn);   // must not crash
        CHECK(Live(s) <= b5 + 1);
        Write(f.P("junk2.okayprefab"), "not a prefab at all \x01\x02");
        CHECK(PrefabTools::Instantiate(s, f.P("junk2.okayprefab"), {}, &err, &warn) == nullptr);
        CHECK(!err.empty());
    }
}

static void TestRoundTrip() {
    RUN_SUITE("scene round trip with nested instances");
    Files f("okay_prefab_nested_d");
    MakeWheel(f.wheel);
    MakeCar(f.car, f.wheel);
    Scene a("a");
    GameObject* car = SceneSerializer::InstantiateFromFile(a, f.car);
    GameObject* hubL = Child(Child(car, "Wheel L"), "Hub");
    hubL->GetComponent<SpriteRenderer>()->color = Color{0, 0, 1, 1};
    CHECK(SceneSerializer::CapturePrefabOverrides(*car));

    std::string err;
    const std::string text = SceneSerializer::Serialize(a);
    CHECK(Contains(text, "prefab "));
    Scene b("b");
    CHECK(SceneSerializer::Deserialize(b, text, &err));
    GameObject* car2 = Find(b, "Car");
    CHECK(car2 != nullptr);
    if (!car2) return;
    CHECK(car2->prefabSource == f.car);
    CHECK(car2->prefabOverrides.size() == car->prefabOverrides.size());
    GameObject* wl = Child(car2, "Wheel L");
    GameObject* wr = Child(car2, "Wheel R");
    CHECK(wl && wr && wl->prefabSource == f.wheel && wr->prefabSource == f.wheel);
    CHECK(wl->prefabOverrides.size() == Child(car, "Wheel L")->prefabOverrides.size());
    CHECK(wr->prefabOverrides.size() == Child(car, "Wheel R")->prefabOverrides.size());
    for (std::size_t i = 0; i < wl->prefabOverrides.size(); ++i) {
        CHECK(wl->prefabOverrides[i].object == Child(car, "Wheel L")->prefabOverrides[i].object);
        CHECK(wl->prefabOverrides[i].line == Child(car, "Wheel L")->prefabOverrides[i].line);
    }
    CHECK(car2->Guid() == car->Guid());
    CHECK(Child(wl, "Hub")->Guid() == hubL->Guid());
    CHECK(Child(wl, "Hub")->GetComponent<SpriteRenderer>()->color.b == 1);
    // Saving the loaded scene again gives the same text.
    CHECK(SceneSerializer::Serialize(b) == text);

    // The loaded instance can be re-applied: the stored overrides still do their work.
    GameObject* fresh = SceneSerializer::ReapplyPrefab(b, *car2, &err);
    b.FlushDestroyed();
    CHECK(fresh != nullptr);
    if (fresh) {
        CHECK(Child(Child(fresh, "Wheel L"), "Hub")->GetComponent<SpriteRenderer>()->color.b == 1);
        CHECK(fresh->Guid() == car->Guid());
    }

    // A duplicate (the editor's paste) keeps every link and override and gets new ids.
    GameObject* dup = a.Instantiate(*car);
    CHECK(dup != nullptr);
    if (dup) {
        CHECK(dup->prefabSource == f.car && dup->prefabOverrides.size() == car->prefabOverrides.size());
        CHECK(Child(dup, "Wheel R")->prefabSource == f.wheel);
        CHECK(Child(dup, "Wheel R")->prefabOverrides.size() == Child(car, "Wheel R")->prefabOverrides.size());
        CHECK(dup->Guid() != car->Guid());
    }
}

static void TestUnpackAndQueries() {
    RUN_SUITE("unpack and queries");
    Files f("okay_prefab_nested_e");
    MakeWheel(f.wheel);
    MakeCar(f.car, f.wheel);
    Scene s("s");
    GameObject* plain = s.CreateGameObject("Plain");
    CHECK(!PrefabTools::IsPrefabInstance(*plain));
    CHECK(PrefabTools::PrefabRoot(*plain) == nullptr);
    CHECK(PrefabTools::Unpack(*plain) == 0);

    GameObject* car = SceneSerializer::InstantiateFromFile(s, f.car);
    GameObject* wl = Child(car, "Wheel L");
    GameObject* hub = Child(wl, "Hub");
    CHECK(PrefabTools::IsPrefabInstance(*car) && PrefabTools::IsPrefabInstance(*hub));
    CHECK(PrefabTools::PrefabRoot(*hub) == car);          // outermost
    CHECK(PrefabTools::NearestPrefabRoot(*hub) == wl);    // innermost
    CHECK(PrefabTools::NearestPrefabRoot(*car) == car);
    CHECK(PrefabTools::NestedInstances(*car).size() == 2);
    CHECK(PrefabTools::NestedInstances(*wl).empty());
    CHECK(PrefabTools::OverridesOf(*wl).size() == wl->prefabOverrides.size());
    CHECK(!PrefabTools::OverridesOf(*wl).empty());

    // Edit inside a nested instance, then unpack only the car.
    hub->GetComponent<SpriteRenderer>()->color = Color{0, 0, 1, 1};
    car->transform->localPosition = Vec3{9, 0, 0};
    std::vector<std::string> guids;
    std::vector<GameObject*> all;
    std::function<void(GameObject*)> walk = [&](GameObject* g) { all.push_back(g); guids.push_back(g->Guid()); for (Transform* c : g->transform->Children()) walk(c->gameObject); };
    walk(car);
    const std::size_t count = Live(s);
    CHECK(PrefabTools::Unpack(*car) == 1);
    CHECK(car->prefabSource.empty() && car->prefabOverrides.empty());
    CHECK(wl->prefabSource == f.wheel);                                   // the nested instance stays linked
    CHECK(HasOverride(wl, 1, "sprite"));                                  // and now carries the edit made through the car
    CHECK(Live(s) == count);
    for (std::size_t i = 0; i < all.size(); ++i) CHECK(all[i]->Guid() == guids[i]);   // ids stay
    CHECK(PrefabTools::PrefabRoot(*hub) == wl);
    CHECK(PrefabTools::NestedInstances(*car).size() == 2);                // still two linked descendants
    CHECK(PrefabTools::Unpack(*car) == 0);                                // already unpacked

    // Completely: the outermost instance and every nested one, from any object inside.
    GameObject* car2 = SceneSerializer::InstantiateFromFile(s, f.car);
    GameObject* hub2 = Child(Child(car2, "Wheel R"), "Hub");
    CHECK(PrefabTools::Unpack(*hub2, true) == 3);
    CHECK(car2->prefabSource.empty() && Child(car2, "Wheel R")->prefabSource.empty() && Child(car2, "Wheel L")->prefabSource.empty());
    CHECK(!PrefabTools::IsPrefabInstance(*hub2));
    CHECK(PrefabTools::NestedInstances(*car2).empty());
}

static void TestSaveNested() {
    RUN_SUITE("saving a prefab that holds instances");
    Files f("okay_prefab_nested_g");
    MakeWheel(f.wheel);
    Scene s("s");
    GameObject* garage = s.CreateGameObject("Garage");
    garage->AddComponent<SpriteRenderer>()->glyph = 'g';
    GameObject* w = SceneSerializer::InstantiateFromFile(s, f.wheel);
    w->name = "Spare";
    w->transform->SetParent(garage->transform, false);
    w->transform->localPosition = Vec3{5, 0, 0};
    Child(w, "Hub")->GetComponent<SpriteRenderer>()->color = Color{0, 0, 1, 1};
    GameObject* extra = s.CreateGameObject("Bolt");
    extra->transform->SetParent(Child(w, "Hub")->transform, false);          // added inside the nested instance
    GameObject* bench = s.CreateGameObject("Bench");
    bench->transform->SetParent(garage->transform, false);

    std::string err;
    CHECK(PrefabTools::SavePrefab(*garage, f.P("garage.okayprefab"), &err));
    const std::string text = Read(f.P("garage.okayprefab"));
    CHECK(Contains(text, "nestedprefab"));
    CHECK(Contains(text, "\"Spare\""));
    CHECK(!Contains(text, "\"Hub\""));            // the nested prefab's own objects are not copied
    CHECK(Contains(text, "\"Bolt\""));
    CHECK(!fs::exists(f.P("garage.okayprefab.tmp")));

    Scene t("t");
    std::vector<std::string> warn;
    GameObject* g2 = SceneSerializer::InstantiateFromFile(t, f.P("garage.okayprefab"), &err, &warn);
    CHECK(g2 != nullptr);
    if (g2) {
        CHECK((ChildNames(g2) == std::vector<std::string>{"Spare", "Bench"}));
        GameObject* sp = Child(g2, "Spare");
        CHECK(sp && sp->prefabSource == f.wheel);
        CHECK_NEAR(sp->transform->localPosition.x, 5, 1e-5);
        GameObject* hub = sp ? Child(sp, "Hub") : nullptr;
        CHECK(hub && hub->GetComponent<SpriteRenderer>()->color.b == 1);
        CHECK(hub && Child(hub, "Bolt") != nullptr);
        CHECK(Live(t) == 5);
    }
    // The wheel file changes: instances of the garage pick it up.
    {
        Scene w2("w2");
        GameObject* wh = w2.CreateGameObject("Wheel");
        wh->AddComponent<SpriteRenderer>()->glyph = 'Q';
        GameObject* hubx = w2.CreateGameObject("Hub");
        hubx->transform->SetParent(wh->transform, false);
        hubx->AddComponent<SpriteRenderer>();
        SceneSerializer::SaveObjectToFile(*wh, f.wheel);
        Scene u("u");
        GameObject* g3 = SceneSerializer::InstantiateFromFile(u, f.P("garage.okayprefab"));
        CHECK(g3 && Child(g3, "Spare")->GetComponent<SpriteRenderer>()->glyph == 'Q');
        CHECK(g3 && Child(Child(g3, "Spare"), "Hub")->GetComponent<SpriteRenderer>()->color.b == 1);   // the hub override still applies
    }
    // Saving a prefab over a file it contains would be a cycle: refused, nothing written.
    const std::string wheelBefore = Read(f.wheel);
    CHECK(!PrefabTools::SavePrefab(*garage, f.wheel, &err));
    CHECK(Contains(err, "cycle"));
    CHECK(Read(f.wheel) == wheelBefore);
}


static void TestNestedDeltasAndPaths() {
    RUN_SUITE("removals, unset and relative paths in nested prefabs");
    Files f("okay_prefab_nested_i");
    MakeWheel(f.wheel);
    std::string t = "okayscene 3\ngameobject 0 \"Car\"\n  transform 0 0 0 0 0 0 1 1 1 1\n  parent -1\nend\n";
    t += "gameobject 1 \"Wheel L\"\n  nestedprefab \"wheel.okayprefab\"\n  remove 1\n  parent 0\nend\n";      // relative to this file
    t += "gameobject 2 \"Wheel R\"\n  nestedprefab \"wheel.okayprefab\"\n  unset 0 \"sprite\" 0\n  parent 0\nend\n";
    Write(f.P("car.okayprefab"), t);
    // Run from another directory: the reference is found next to the file that names it.
    const fs::path cwd = fs::current_path();
    fs::current_path(fs::temp_directory_path());
    Scene s("s");
    std::string err;
    std::vector<std::string> warn;
    GameObject* car = SceneSerializer::InstantiateFromFile(s, f.P("car.okayprefab"), &err, &warn);
    fs::current_path(cwd);
    CHECK(car != nullptr);
    if (!car) return;
    GameObject* wl = Child(car, "Wheel L");
    GameObject* wr = Child(car, "Wheel R");
    CHECK(wl && wr && Child(wl, "Hub") == nullptr && Child(wr, "Hub") != nullptr);
    CHECK(wl && wl->GetComponent<SpriteRenderer>() != nullptr);
    CHECK(wr && wr->GetComponent<SpriteRenderer>() == nullptr);
    CHECK(wl && !wl->prefabSource.empty() && fs::exists(wl->prefabSource));   // the link is the path that opened
    PrefabDiff d;
    CHECK(wl && PrefabTools::Diff(*wl, d, &err));
    CHECK(d.records.empty() && d.objects.size() == 1 && d.objects[0].kind == PrefabObjectDiff::Kind::Removed && d.objects[0].name == "Hub");
    CHECK(wr && PrefabTools::Diff(*wr, d, &err));
    CHECK(d.records.size() == 1 && d.records[0].kind == PrefabRecordDiff::Kind::Removed && d.records[0].record == "sprite");

    // Saving the instance writes the removal and the unset back, and loads the same.
    CHECK(PrefabTools::SavePrefab(*car, f.P("car2.okayprefab"), &err));
    const std::string saved = Read(f.P("car2.okayprefab"));
    CHECK(Contains(saved, "remove 1") && Contains(saved, "unset 0 \"sprite\" 0"));
    Scene u("u");
    GameObject* c2 = SceneSerializer::InstantiateFromFile(u, f.P("car2.okayprefab"), &err, &warn);
    CHECK(c2 && Child(Child(c2, "Wheel L"), "Hub") == nullptr && Child(Child(c2, "Wheel R"), "Hub") != nullptr);
    CHECK(c2 && Child(c2, "Wheel R")->GetComponent<SpriteRenderer>() == nullptr);
    CHECK(Live(u) == 4);

    // Bare records on a nested object edit the nested root; a second top-level object of
    // a nested file goes under its root, with a warning.
    {
        Write(f.P("twin.okayprefab"), "okayscene 3\ngameobject 0 \"Twin\"\n  parent -1\nend\ngameobject 1 \"Extra\"\n  parent -1\nend\n");
        std::string c = "okayscene 3\ngameobject 0 \"Holder\"\n  parent -1\nend\n"
                        "gameobject 1 \"Pair\"\n  nestedprefab twin.okayprefab\n  transform -2 0 0 0 0 0 1 1 1 1\n  tag \"bare\"\n  parent 0\nend\n";
        Write(f.P("holder.okayprefab"), c);
        Scene tw("tw");
        warn.clear();
        GameObject* h = SceneSerializer::InstantiateFromFile(tw, f.P("holder.okayprefab"), &err, &warn);
        CHECK(h != nullptr);
        GameObject* pair = h ? Child(h, "Pair") : nullptr;
        CHECK(pair && pair->tag == "bare" && pair->transform->localPosition.x == -2);
        CHECK(pair && Child(pair, "Extra") != nullptr);
        CHECK(Has(warn, "Extra") && Has(warn, "no parent"));
    }

    // A nested prefab saved with a path that is relative to the saved file stays loadable
    // when the pair of files moves together (the placeholder keeps the path it was given).
    fs::create_directories(f.dir / "moved");
    fs::copy_file(f.P("car.okayprefab"), f.P("moved/car.okayprefab"));
    fs::copy_file(f.wheel, f.P("moved/wheel.okayprefab"));
    Scene m("m");
    GameObject* mc = SceneSerializer::InstantiateFromFile(m, f.P("moved/car.okayprefab"), &err, &warn);
    CHECK(mc && Child(Child(mc, "Wheel R"), "Hub") != nullptr);
    CHECK(mc && Child(mc, "Wheel R")->prefabSource == f.P("moved/wheel.okayprefab"));
}


static void TestNestedInstanceAlone() {
    RUN_SUITE("re-apply and revert one nested instance");
    Files f("okay_prefab_nested_j");
    MakeWheel(f.wheel);
    MakeCar(f.car, f.wheel);
    Scene s("s");
    GameObject* car = SceneSerializer::InstantiateFromFile(s, f.car);
    GameObject* wl = Child(car, "Wheel L");
    const std::string guid = wl->Guid();
    Child(wl, "Hub")->tag = "mine";
    std::string err;
    CHECK(SceneSerializer::CapturePrefabOverrides(*wl, &err));
    CHECK(HasOverride(wl, 1, "tag") && HasOverride(wl, 0, "transform"));   // its own edit and the car file's placement
    {
        Scene w("w");
        GameObject* wh = w.CreateGameObject("Wheel");
        wh->AddComponent<SpriteRenderer>()->glyph = 'Q';
        GameObject* hub = w.CreateGameObject("Hub");
        hub->transform->SetParent(wh->transform, false);
        hub->AddComponent<SpriteRenderer>()->glyph = 'h';
        SceneSerializer::SaveObjectToFile(*wh, f.wheel);
    }
    GameObject* nw = SceneSerializer::ReapplyPrefab(s, *wl, &err);
    s.FlushDestroyed();
    CHECK(nw != nullptr);
    if (!nw) return;
    CHECK(nw->GetComponent<SpriteRenderer>()->glyph == 'Q');                 // the wheel file's edit
    CHECK(Child(nw, "Hub")->tag == "mine");                                  // the instance's own override
    CHECK_NEAR(nw->transform->localPosition.x, -1, 1e-5);                    // and where the car put it
    CHECK(nw->name == "Wheel L" && nw->Guid() == guid && nw->prefabSource == f.wheel);
    CHECK(nw->transform->Parent() == car->transform);
    CHECK((ChildNames(car) == std::vector<std::string>{"Wheel L", "Wheel R"}));
    CHECK(Live(s) == 5);
    // Revert the tag through the nested instance itself.
    GameObject* rv = PrefabTools::RevertOverride(s, *nw, 1, "tag", "", 0, &err);
    s.FlushDestroyed();
    CHECK(rv != nullptr && Child(rv, "Hub")->tag.empty() && rv->transform->Parent() == car->transform);
    CHECK(rv && rv->prefabSource == f.wheel && Live(s) == 5);
    // The car as a whole now matches its (changed) file except where its own edits are.
    PrefabDiff d;
    CHECK(PrefabTools::Diff(*car, d, &err));
    CHECK(d.Find(1, "sprite") == nullptr);   // the left wheel root is the new one: glyph Q like the file
}

static void TestValidator() {
    RUN_SUITE("asset validator (nested)");
    Files f("okay_prefab_nested_h");
    MakeWheel(f.wheel);
    MakeCar(f.car, f.wheel);
    PrefabReport ok = PrefabTools::ValidateAsset(f.car);
    CHECK(ok.Ok());
    CHECK(ok.issues.empty());
    CHECK(ok.files.size() == 2);

    // Stale overrides, a missing nested file and a cycle, each with a readable message.
    std::string t = Read(f.car);
    t += "gameobject 3 \"Ghost\"\n  nestedprefab " + Q(f.P("ghost.okayprefab")) + "\n  parent 0\nend\n";
    t += "gameobject 4 \"Wheel X\"\n  nestedprefab " + Q(f.wheel) + "\n  override 7 " + Q("tag \"x\"") + "\n  parent 0\nend\n";
    Write(f.P("bad.okayprefab"), t);
    PrefabReport bad = PrefabTools::ValidateAsset(f.P("bad.okayprefab"));
    CHECK(!bad.Ok());
    const std::string msg = bad.ToString();
    CHECK(Contains(msg, "ghost.okayprefab") && Contains(msg, "Ghost"));
    CHECK(Contains(msg, "object 7") && Contains(msg, "no longer has it"));
    CHECK(bad.Count(PrefabIssue::Severity::Error) >= 1 && bad.Count(PrefabIssue::Severity::Warning) >= 1);

    Write(f.P("p.okayprefab"), "okayscene 3\ngameobject 0 \"P\"\n  parent -1\nend\ngameobject 1 \"Q\"\n  nestedprefab " + Q(f.P("q.okayprefab")) + "\n  parent 0\nend\n");
    Write(f.P("q.okayprefab"), "okayscene 3\ngameobject 0 \"Q\"\n  parent -1\nend\ngameobject 1 \"P\"\n  nestedprefab " + Q(f.P("p.okayprefab")) + "\n  parent 0\nend\n");
    PrefabReport cyc = PrefabTools::ValidateAsset(f.P("p.okayprefab"));
    CHECK(!cyc.Ok());
    CHECK(Contains(cyc.ToString(), "cycle"));

    // A truncated file and a file that is not there.
    Write(f.P("cut.okayprefab"), "okayscene 3\ngameobject 0 \"C\"\n  transform 0 0 0 0 0 0 1 1 1 1\n");
    PrefabReport cut = PrefabTools::ValidateAsset(f.P("cut.okayprefab"));
    CHECK(!cut.Ok() && Contains(cut.ToString(), "cut short"));
    PrefabReport none = PrefabTools::ValidateAsset(f.P("none.okayprefab"));
    CHECK(!none.Ok() && Contains(none.ToString(), "cannot open"));

    // Instances: an override that names an object the prefab no longer has.
    Scene s("s");
    GameObject* car = SceneSerializer::InstantiateFromFile(s, f.car);
    CHECK(PrefabTools::ValidateInstance(*car).issues.empty());
    car->prefabOverrides.push_back({42, "tag \"z\""});
    PrefabReport inst = PrefabTools::ValidateInstance(*car);
    CHECK(inst.issues.size() == 1 && Contains(inst.ToString(), "object 42"));
    CHECK(inst.Ok());   // stale overrides are warnings
}

int main() {
    TestNestedInstantiate();
    TestNestedReapply();
    TestCyclesAndLimits();
    TestRoundTrip();
    TestUnpackAndQueries();
    TestSaveNested();
    TestNestedDeltasAndPaths();
    TestNestedInstanceAlone();
    TestValidator();
    TEST_MAIN_RESULT();
}
