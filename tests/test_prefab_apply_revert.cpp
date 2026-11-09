// Per-record and per-field apply / revert: how an instance differs from its prefab
// (records, fields, added and removed components and objects), RevertOverride,
// ApplyToSource (the prefab file is checked on disk), nested instances, damaged files.
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
static std::vector<std::string> Lines(const std::string& t) {
    std::vector<std::string> v;
    std::istringstream in(t);
    std::string l;
    while (std::getline(in, l)) v.push_back(l);
    return v;
}
static bool HasField(const PrefabRecordDiff* r, const std::string& name) {
    if (!r) return false;
    for (const auto& f : r->fields) if (f.name == name) return true;
    return false;
}

struct Files {
    fs::path dir;
    std::string lamp;
    explicit Files(const std::string& name) {
        dir = fs::temp_directory_path() / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
        lamp = (dir / "lamp.okayprefab").string();
    }
    ~Files() { std::error_code ec; fs::remove_all(dir, ec); }
    std::string P(const std::string& f) const { return (dir / f).string(); }
};

// lamp.okayprefab: 0 Lamp (light + sprite), 1 Shade (sprite + collider), 2 Base.
static void MakeLamp(const std::string& path) {
    Scene s("l");
    GameObject* lamp = s.CreateGameObject("Lamp");
    auto* li = lamp->AddComponent<Light>();
    li->color = Color{1, 1, 1, 1};
    li->intensity = 1.0f;
    lamp->AddComponent<SpriteRenderer>()->glyph = 'L';
    GameObject* shade = s.CreateGameObject("Shade");
    shade->transform->SetParent(lamp->transform, false);
    shade->AddComponent<SpriteRenderer>()->glyph = 's';
    shade->AddComponent<BoxCollider2D>();
    GameObject* base = s.CreateGameObject("Base");
    base->transform->SetParent(lamp->transform, false);
    SceneSerializer::SaveObjectToFile(*lamp, path);
}

// An instance with a spread of edits: a moved root, a brighter red light, a tag on the
// shade, a new rigidbody on the root, a collider gone from the shade, an added Bulb and
// a deleted Base.
static GameObject* EditedLamp(Scene& s, const std::string& path) {
    GameObject* inst = SceneSerializer::InstantiateFromFile(s, path);
    if (!inst) return nullptr;
    inst->transform->localPosition = Vec3{4, 0, 0};
    auto* li = inst->GetComponent<Light>();
    li->intensity = 2.5f;
    li->color = Color{1, 0, 0, 1};
    Child(inst, "Shade")->tag = "limb";
    inst->AddComponent<Rigidbody2D>();
    Child(inst, "Shade")->RemoveComponent<BoxCollider2D>();
    GameObject* bulb = s.CreateGameObject("Bulb");
    bulb->transform->SetParent(inst->transform, false);
    bulb->AddComponent<SpriteRenderer>()->glyph = 'o';   // (an empty one at Base's place would read as Base, renamed)
    s.Destroy(Child(inst, "Base"));
    s.FlushDestroyed();
    return inst;
}

static void TestDiff() {
    RUN_SUITE("differences at record and field level");
    Files f("okay_prefab_ar_a");
    MakeLamp(f.lamp);
    Scene s("s");
    GameObject* inst = EditedLamp(s, f.lamp);
    CHECK(inst != nullptr);
    if (!inst) return;
    PrefabDiff d;
    std::string err;
    CHECK(PrefabTools::Diff(*inst, d, &err));
    CHECK(!d.Empty());

    const PrefabRecordDiff* tr = d.Find(0, "transform");
    CHECK(tr && tr->kind == PrefabRecordDiff::Kind::Changed && tr->fields.size() == 1 && tr->fields[0].name == "position.x");
    if (tr && !tr->fields.empty()) {
        CHECK(tr->fields[0].prefabValue == "0" && tr->fields[0].instanceValue == "4");
        CHECK(tr->Describe() == "Transform position.x: 0 -> 4");
        CHECK(tr->objectName == "Lamp" && !tr->wholeRecordOnly);
    }
    const PrefabRecordDiff* li = d.Find(0, "light");
    CHECK(li && li->kind == PrefabRecordDiff::Kind::Changed);
    CHECK(HasField(li, "intensity") && HasField(li, "color.g") && HasField(li, "color.b"));
    CHECK(li && li->fields.size() == 3);
    CHECK(!HasField(li, "color.r") && !HasField(li, "range"));
    for (const auto& fd : li ? li->fields : std::vector<PrefabFieldDiff>{})
        if (fd.name == "intensity") CHECK(fd.prefabValue == "1" && fd.instanceValue == "2.5");

    const PrefabRecordDiff* tag = d.Find(1, "tag");
    CHECK(tag && tag->kind == PrefabRecordDiff::Kind::Added && tag->instanceLine == "tag \"limb\"" && tag->prefabLine.empty());
    const PrefabRecordDiff* rb = d.Find(0, "rigidbody2d");
    CHECK(rb && rb->kind == PrefabRecordDiff::Kind::Added);
    const PrefabRecordDiff* col = d.Find(1, "boxcollider2d");
    CHECK(col && col->kind == PrefabRecordDiff::Kind::Removed && col->instanceLine.empty() && !col->prefabLine.empty());
    CHECK(d.Find(0, "sprite") == nullptr);                       // untouched records are not listed
    CHECK(d.records.size() == 5);

    // Objects the instance gained or lost.
    int added = 0, removed = 0;
    for (const auto& o : d.objects) {
        if (o.kind == PrefabObjectDiff::Kind::Added && o.name == "Bulb" && o.path == "Lamp/Bulb" && o.parentObject == 0 && o.live) ++added;
        if (o.kind == PrefabObjectDiff::Kind::Removed && o.name == "Base" && o.object == 2) ++removed;
    }
    CHECK(added == 1 && removed == 1 && d.objects.size() == 2);

    // A record whose two lines are shaped differently can only be handled whole.
    inst->GetComponent<Light>()->castShadows = false;   // writes the optional trailing block
    PrefabDiff d2;
    CHECK(PrefabTools::Diff(*inst, d2, &err));
    const PrefabRecordDiff* li2 = d2.Find(0, "light");
    CHECK(li2 && li2->wholeRecordOnly && li2->fields.empty());
    GameObject* nul = PrefabTools::RevertOverride(s, *inst, 0, "light", "intensity", 0, &err);
    CHECK(nul == nullptr && Contains(err, "shape"));
    CHECK(!inst->IsDestroyed());

    // Not an instance, or no such difference.
    GameObject* plain = s.CreateGameObject("Plain");
    CHECK(!PrefabTools::Diff(*plain, d2, &err) && Contains(err, "not a prefab instance"));
    GameObject* fresh = SceneSerializer::InstantiateFromFile(s, f.lamp);
    PrefabDiff same;
    CHECK(PrefabTools::Diff(*fresh, same, &err) && same.Empty());
    CHECK(PrefabTools::RevertOverride(s, *fresh, 0, "light", "", 0, &err) == nullptr && Contains(err, "does not differ"));
}

static void TestRevert() {
    RUN_SUITE("revert");
    Files f("okay_prefab_ar_b");
    MakeLamp(f.lamp);
    Scene s("s");
    GameObject* holder = s.CreateGameObject("Holder");
    GameObject* inst = EditedLamp(s, f.lamp);
    inst->transform->SetParent(holder->transform, false);
    inst->name = "My lamp";
    const std::string guid = inst->Guid();
    const std::string shadeGuid = Child(inst, "Shade")->Guid();
    std::string err;
    std::vector<std::string> warn;

    // One field of the light: the intensity comes back, the colour stays red.
    GameObject* r1 = PrefabTools::RevertOverride(s, *inst, 0, "light", "intensity", 0, &err, &warn);
    s.FlushDestroyed();
    CHECK(r1 != nullptr);
    if (!r1) { std::cerr << "  err: " << err << "\n"; return; }
    CHECK_NEAR(r1->GetComponent<Light>()->intensity, 1.0, 1e-6);
    CHECK_NEAR(r1->GetComponent<Light>()->color.g, 0.0, 1e-6);
    CHECK_NEAR(r1->GetComponent<Light>()->color.r, 1.0, 1e-6);
    // Everything else the instance had is still there: place, name, ids, edits, additions, losses.
    CHECK(r1->name == "My lamp" && r1->transform->Parent() == holder->transform && r1->Guid() == guid);
    CHECK(Child(r1, "Shade") && Child(r1, "Shade")->Guid() == shadeGuid && Child(r1, "Shade")->tag == "limb");
    CHECK_NEAR(r1->transform->localPosition.x, 4, 1e-6);
    CHECK(r1->GetComponent<Rigidbody2D>() != nullptr);
    CHECK(Child(r1, "Shade")->GetComponent<BoxCollider2D>() == nullptr);          // the loss is not undone
    CHECK((ChildNames(r1) == std::vector<std::string>{"Shade", "Bulb"}));       // Base stays deleted, Bulb carried across
    PrefabDiff d;
    CHECK(PrefabTools::Diff(*r1, d, &err));
    CHECK(d.Find(0, "light") && !HasField(d.Find(0, "light"), "intensity") && HasField(d.Find(0, "light"), "color.g"));
    CHECK(r1->prefabOverrides.size() == 4);   // transform, light (colour), tag, rigidbody2d

    // The whole transform record.
    GameObject* r2 = PrefabTools::RevertOverride(s, *r1, 0, "transform", "", 0, &err);
    s.FlushDestroyed();
    CHECK(r2 && r2->transform->localPosition.x == 0 && r2->GetComponent<Rigidbody2D>() != nullptr && r2->Guid() == guid);
    // An added component goes away; a lost one comes back.
    GameObject* r3 = r2 ? PrefabTools::RevertOverride(s, *r2, 0, "rigidbody2d", "", 0, &err) : nullptr;
    s.FlushDestroyed();
    CHECK(r3 && r3->GetComponent<Rigidbody2D>() == nullptr);
    GameObject* r4 = r3 ? PrefabTools::RevertOverride(s, *r3, 1, "boxcollider2d", "", 0, &err) : nullptr;
    s.FlushDestroyed();
    CHECK(r4 && Child(r4, "Shade")->GetComponent<BoxCollider2D>() != nullptr);
    // Reverting a field of an added record is not possible.
    GameObject* nul = r4 ? PrefabTools::RevertOverride(s, *r4, 1, "tag", "value", 0, &err) : nullptr;
    CHECK(nul == nullptr && Contains(err, "cannot be reverted"));

    // Revert everything: nothing differs, the added object is dropped, the lost one is back.
    GameObject* all = r4 ? PrefabTools::RevertAll(s, *r4, &err) : nullptr;
    s.FlushDestroyed();
    CHECK(all != nullptr);
    if (all) {
        CHECK((ChildNames(all) == std::vector<std::string>{"Shade", "Base"}));
        CHECK(PrefabTools::Diff(*all, d, &err) && d.Empty());
        CHECK(all->prefabOverrides.empty());
        CHECK(all->Guid() == guid && all->name == "My lamp");
        CHECK(Live(s) == 1 + 3);   // Holder and the three lamp objects
    }
}

static void TestApply() {
    RUN_SUITE("apply to the prefab file");
    Files f("okay_prefab_ar_c");
    MakeLamp(f.lamp);
    Scene s("s");
    GameObject* other = SceneSerializer::InstantiateFromFile(s, f.lamp);   // made before any apply
    GameObject* inst = EditedLamp(s, f.lamp);
    const std::string original = Read(f.lamp);
    std::string err;

    // One field: only the intensity line changes in the file.
    CHECK(PrefabTools::ApplyToSource(*inst, 0, "light", "intensity", 0, &err));
    std::string now = Read(f.lamp);
    CHECK(now != original);
    {
        const auto a = Lines(original), b = Lines(now);
        CHECK(a.size() == b.size());
        int differing = 0;
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) if (a[i] != b[i]) { ++differing; CHECK(Contains(b[i], "light")); }
        CHECK(differing == 1);
    }
    CHECK(!fs::exists(f.lamp + ".tmp"));
    {
        Scene t("t");
        GameObject* p = SceneSerializer::InstantiateFromFile(t, f.lamp);
        CHECK(p && p->GetComponent<Light>()->intensity == 2.5f);
        CHECK(p && p->GetComponent<Light>()->color.g == 1.0f);           // the colour was not applied
        CHECK(p && p->transform->localPosition.x == 0);
    }
    // The instance's stored overrides were re-captured: the light still differs (the colour), the intensity does not.
    {
        PrefabDiff d;
        CHECK(PrefabTools::Diff(*inst, d, &err));
        CHECK(d.Find(0, "light") && !HasField(d.Find(0, "light"), "intensity") && HasField(d.Find(0, "light"), "color.g"));
        bool lightOverride = false;
        for (const auto& o : inst->prefabOverrides) if (o.object == 0 && o.line.rfind("light", 0) == 0) lightOverride = true;
        CHECK(lightOverride);
    }
    // Applying the same thing again: nothing to do, and the file stays as it is.
    const std::string afterOne = Read(f.lamp);
    CHECK(PrefabTools::ApplyToSource(*inst, 0, "light", "intensity", 0, &err));   // idempotent
    CHECK(Read(f.lamp) == afterOne);

    // A whole record, an added record, an added component and a lost component.
    CHECK(PrefabTools::ApplyToSource(*inst, 0, "transform", "", 0, &err));
    CHECK(PrefabTools::ApplyToSource(*inst, 1, "tag", "", 0, &err));
    CHECK(PrefabTools::ApplyToSource(*inst, 0, "rigidbody2d", "", 0, &err));
    CHECK(PrefabTools::ApplyToSource(*inst, 1, "boxcollider2d", "", 0, &err));
    {
        Scene t("t");
        GameObject* p = SceneSerializer::InstantiateFromFile(t, f.lamp, &err);
        CHECK(p != nullptr);
        if (p) {
            CHECK_NEAR(p->transform->localPosition.x, 4, 1e-6);
            CHECK(Child(p, "Shade")->tag == "limb");
            CHECK(p->GetComponent<Rigidbody2D>() != nullptr);
            CHECK(Child(p, "Shade")->GetComponent<BoxCollider2D>() == nullptr);
            CHECK(Child(p, "Shade")->GetComponent<SpriteRenderer>() != nullptr);   // the rest of the shade is intact
            CHECK((ChildNames(p) == std::vector<std::string>{"Shade", "Base"}));   // objects are not applied
        }
    }
    CHECK(!fs::exists(f.lamp + ".tmp"));

    // Everything else in one go: only the objects (added Bulb, deleted Base) still differ.
    CHECK(PrefabTools::ApplyAllToSource(*inst, &err));
    PrefabDiff d;
    CHECK(PrefabTools::Diff(*inst, d, &err));
    CHECK(d.records.empty() && d.objects.size() == 2);
    {
        Scene t("t");
        GameObject* p = SceneSerializer::InstantiateFromFile(t, f.lamp);
        CHECK(p && p->GetComponent<Light>()->color.g == 0.0f && p->GetComponent<Light>()->intensity == 2.5f);
    }

    // Other instances catch up when they are re-applied; their own edits stay.
    other->GetComponent<SpriteRenderer>()->glyph = 'X';
    CHECK(SceneSerializer::CapturePrefabOverrides(*other, &err));   // the file has moved on, so this includes what the apply changed
    GameObject* again = SceneSerializer::ReapplyPrefab(s, *other, &err);
    s.FlushDestroyed();
    CHECK(again && again->GetComponent<SpriteRenderer>()->glyph == 'X' && again->GetComponent<Rigidbody2D>() != nullptr);

    // Line endings: a prefab saved with CRLF is edited and written back in a form that loads.
    {
        std::string crlf;
        for (char c : original) { if (c == '\n') crlf += '\r'; crlf += c; }
        Write(f.P("crlf.okayprefab"), crlf);
        GameObject* c = SceneSerializer::InstantiateFromFile(s, f.P("crlf.okayprefab"));
        c->transform->localPosition = Vec3{7, 0, 0};
        CHECK(c && PrefabTools::ApplyToSource(*c, 0, "transform", "", 0, &err));
        const std::string t = Read(f.P("crlf.okayprefab"));
        CHECK(!Contains(t, "\r"));
        Scene u("u");
        GameObject* p = SceneSerializer::InstantiateFromFile(u, f.P("crlf.okayprefab"));
        CHECK(p && p->transform->localPosition.x == 7);
    }
}

static void TestDamagedFiles() {
    RUN_SUITE("damaged, packed and missing prefab files");
    Files f("okay_prefab_ar_d");
    MakeLamp(f.lamp);
    Scene s("s");
    GameObject* inst = EditedLamp(s, f.lamp);
    const std::size_t count = Live(s);
    const std::string good = Read(f.lamp);
    std::string err;

    // Cut off in the middle of an object (the last one: the text ends before its `end`, wherever the records of the
    // earlier objects happen to leave the halfway mark).
    const std::size_t lastEnd = good.rfind("\nend\n");
    CHECK(lastEnd != std::string::npos && lastEnd > 8);
    Write(f.lamp, good.substr(0, lastEnd - 4));
    const std::string cut = Read(f.lamp);
    PrefabDiff d;
    CHECK(!PrefabTools::Diff(*inst, d, &err) && !err.empty());
    err.clear();
    CHECK(!PrefabTools::ApplyToSource(*inst, 0, "transform", "", 0, &err) && !err.empty());
    CHECK(Read(f.lamp) == cut);                                                     // not touched
    CHECK(PrefabTools::RevertOverride(s, *inst, 0, "light", "", 0, &err) == nullptr);
    CHECK(PrefabTools::RevertAll(s, *inst, &err) == nullptr);
    CHECK(PrefabTools::Reapply(s, *inst, &err) == nullptr);
    CHECK(!PrefabTools::CaptureOverrides(*inst, &err));
    CHECK(!inst->IsDestroyed() && Live(s) == count);                                // the instance and the scene are as they were
    CHECK(!fs::exists(f.lamp + ".tmp"));

    // Garbage and an empty file.
    Write(f.lamp, std::string("\x01\x02garbage\xff", 10));
    CHECK(!PrefabTools::Diff(*inst, d, &err));
    Write(f.lamp, "");
    CHECK(!PrefabTools::ApplyToSource(*inst, 0, "transform", "", 0, &err));
    CHECK(PrefabTools::RevertAll(s, *inst, &err) == nullptr && Live(s) == count);

    // A file that is gone.
    fs::remove(f.lamp);
    CHECK(!PrefabTools::Diff(*inst, d, &err) && Contains(err, "cannot open"));
    CHECK(!PrefabTools::ApplyToSource(*inst, 0, "transform", "", 0, &err));
    CHECK(!fs::exists(f.lamp));                                                     // an apply never creates the file

    // A packed (obfuscated) prefab can be read but not rewritten.
    Write(f.lamp, DataPack::Pack(good));
    const std::string packed = Read(f.lamp);
    CHECK(PrefabTools::Diff(*inst, d, &err));
    CHECK(d.Find(0, "transform") != nullptr);
    CHECK(!PrefabTools::ApplyToSource(*inst, 0, "transform", "", 0, &err) && Contains(err, "packed"));
    CHECK(Read(f.lamp) == packed);

    // Truncation at every length never crashes and never leaves objects behind.
    for (std::size_t cutAt = 0; cutAt < good.size(); cutAt += 37) {
        Write(f.lamp, good.substr(0, cutAt));
        const std::size_t before = Live(s);
        GameObject* r = PrefabTools::Instantiate(s, f.lamp, {}, &err);
        if (r) CHECK(Live(s) > before);
        else CHECK(Live(s) == before);
        PrefabTools::Diff(*inst, d, &err);
        GameObject* rr = PrefabTools::RevertAll(s, *inst, &err);
        if (rr) { s.FlushDestroyed(); inst = rr; }
        else CHECK(!inst->IsDestroyed());
        s.FlushDestroyed();
    }
}

static void TestNestedApplyRevert() {
    RUN_SUITE("apply and revert through nested instances");
    Files f("okay_prefab_ar_e");
    const std::string wheel = f.P("wheel.okayprefab"), car = f.P("car.okayprefab");
    {
        Scene w("w");
        GameObject* wh = w.CreateGameObject("Wheel");
        GameObject* hub = w.CreateGameObject("Hub");
        hub->transform->SetParent(wh->transform, false);
        hub->AddComponent<SpriteRenderer>()->color = Color{1, 1, 1, 1};
        SceneSerializer::SaveObjectToFile(*wh, wheel);
    }
    Write(car, "okayscene 3\ngameobject 0 \"Car\"\n  transform 0 0 0 0 0 0 1 1 1 1\n  parent -1\nend\n"
               "gameobject 1 \"Wheel L\"\n  nestedprefab " + Q(wheel) + "\n  parent 0\nend\n"
               "gameobject 2 \"Wheel R\"\n  nestedprefab " + Q(wheel) + "\n  parent 0\nend\n");
    const std::string wheelBytes = Read(wheel);
    Scene s("s");
    GameObject* r = SceneSerializer::InstantiateFromFile(s, car);
    CHECK(r != nullptr);
    if (!r) return;
    // Expanded numbering: 0 Car, 1 Wheel L, 2 Hub, 3 Wheel R, 4 Hub.
    GameObject* hubR = Child(Child(r, "Wheel R"), "Hub");
    hubR->GetComponent<SpriteRenderer>()->color = Color{0, 1, 0, 1};
    std::string err;
    PrefabDiff d;
    CHECK(PrefabTools::Diff(*r, d, &err));
    const PrefabRecordDiff* sp = d.Find(4, "sprite");
    CHECK(sp && HasField(sp, "color.r") && HasField(sp, "color.b") && !HasField(sp, "color.g"));
    CHECK(d.records.size() == 1);

    // Apply through the outer instance: the car file stores it on the nested object, the wheel file is untouched.
    CHECK(PrefabTools::ApplyToSource(*r, 4, "sprite", "", 0, &err));
    const std::string carText = Read(car);
    CHECK(Contains(carText, "override 1 \"sprite"));
    CHECK(Read(wheel) == wheelBytes);
    CHECK(PrefabTools::Diff(*r, d, &err) && d.Empty());
    {
        Scene t("t");
        GameObject* p = SceneSerializer::InstantiateFromFile(t, car, &err);
        CHECK(p != nullptr);
        if (p) {
            CHECK(Child(Child(p, "Wheel R"), "Hub")->GetComponent<SpriteRenderer>()->color.g == 1);
            CHECK(Child(Child(p, "Wheel R"), "Hub")->GetComponent<SpriteRenderer>()->color.r == 0);
            CHECK(Child(Child(p, "Wheel L"), "Hub")->GetComponent<SpriteRenderer>()->color.r == 1);   // the other wheel is untouched
        }
    }

    // Apply through the nested instance: the wheel file changes, for every car that uses it.
    GameObject* wl = Child(r, "Wheel L");
    Child(wl, "Hub")->GetComponent<SpriteRenderer>()->color = Color{0, 0, 1, 1};
    CHECK(PrefabTools::ApplyToSource(*wl, 1, "sprite", "color.b", 0, &err));   // the blue channel only
    CHECK(Read(wheel) == wheelBytes);   // the blue was already 1: the file comes back byte for byte
    // A field that really changes: apply the red channel of the left hub.
    Child(wl, "Hub")->GetComponent<SpriteRenderer>()->color = Color{0.25f, 1, 1, 1};
    CHECK(PrefabTools::ApplyToSource(*wl, 1, "sprite", "color.r", 0, &err));
    {
        Scene t("t");
        GameObject* w2 = SceneSerializer::InstantiateFromFile(t, wheel);
        auto* c = Child(w2, "Hub")->GetComponent<SpriteRenderer>();
        CHECK(c && c->color.r == 0.25f && c->color.g == 1 && c->color.b == 1);
    }
    // The car's right wheel now differs from its (changed) expansion until it is re-applied.
    CHECK(wl->prefabOverrides.empty());   // the left wheel now equals its file
    CHECK(r->prefabOverrides.empty());    // and the car equals its (changed) expansion, with the right hub's own override in the car file

    // Revert one field inside a nested instance, through the outer instance.
    GameObject* hubL = Child(Child(r, "Wheel L"), "Hub");
    hubL->GetComponent<SpriteRenderer>()->color = Color{0, 0, 0, 1};
    GameObject* rr = PrefabTools::RevertOverride(s, *r, 2, "sprite", "color.g", 0, &err);
    s.FlushDestroyed();
    CHECK(rr != nullptr);
    if (rr) {
        auto* c = Child(Child(rr, "Wheel L"), "Hub")->GetComponent<SpriteRenderer>();
        CHECK(c && c->color.g == 1 && c->color.r == 0 && c->color.b == 0);
        CHECK(Child(rr, "Wheel L")->prefabSource == wheel);
    }
}


static void TestKeyValueRecordsAndNames() {
    RUN_SUITE("key=value records, quoted values and renamed objects");
    Files f("okay_prefab_ar_g");
    const std::string path = f.P("tail.okayprefab");
    {
        Scene t("t");
        GameObject* o = t.CreateGameObject("Tail");
        auto* tr = o->AddComponent<TrailRenderer>();
        tr->time = 1.5f; tr->width = 0.25f; tr->texture = "trail one.png";
        GameObject* c = t.CreateGameObject("Child");
        c->transform->SetParent(o->transform, false);
        SceneSerializer::SaveObjectToFile(*o, path);
    }
    Scene s("s");
    GameObject* inst = SceneSerializer::InstantiateFromFile(s, path);
    auto* tr = inst->GetComponent<TrailRenderer>();
    tr->width = 0.75f;
    tr->texture = "trail two.png";
    std::string err;
    PrefabDiff d;
    CHECK(PrefabTools::Diff(*inst, d, &err));
    const PrefabRecordDiff* r = d.Find(0, "trailrenderer");
    CHECK(r && r->kind == PrefabRecordDiff::Kind::Changed && !r->wholeRecordOnly);
    CHECK(HasField(r, "width") && HasField(r, "tex") && !HasField(r, "time"));
    CHECK(r && r->fields.size() == 2);
    for (const auto& fd : r ? r->fields : std::vector<PrefabFieldDiff>{})
        if (fd.name == "tex") CHECK(fd.prefabValue == "\"trail one.png\"" && fd.instanceValue == "\"trail two.png\"");

    // Rename a child, then revert one field: the rename stays (names are not overrides).
    Child(inst, "Child")->name = "Renamed";
    GameObject* rv = PrefabTools::RevertOverride(s, *inst, 0, "trailrenderer", "tex", 0, &err);
    s.FlushDestroyed();
    CHECK(rv != nullptr);
    if (rv) {
        auto* t2 = rv->GetComponent<TrailRenderer>();
        CHECK(t2 && t2->texture == "trail one.png" && t2->width == 0.75f);
        CHECK(Child(rv, "Renamed") != nullptr && Child(rv, "Child") == nullptr);
        CHECK(PrefabTools::Diff(*rv, d, &err) && d.objects.empty());   // a renamed child is not an added one
        CHECK(PrefabTools::ApplyToSource(*rv, 0, "trailrenderer", "width", 0, &err));
    }
    Scene u("u");
    GameObject* p = SceneSerializer::InstantiateFromFile(u, path);
    auto* tp = p ? p->GetComponent<TrailRenderer>() : nullptr;
    CHECK(tp && tp->width == 0.75f && tp->time == 1.5f && tp->texture == "trail one.png");
    // A field that is not there is refused with the field names listed.
    PrefabDiff d2;
    GameObject* q = SceneSerializer::InstantiateFromFile(s, path);
    q->GetComponent<TrailRenderer>()->time = 9.0f;
    CHECK(!PrefabTools::ApplyToSource(*q, 0, "trailrenderer", "nope", 0, &err) && Contains(err, "no field 'nope'"));
    // A transform field by name; an unknown one lists the names.
    q->transform->localScale = Vec3{2, 1, 1};
    CHECK(!PrefabTools::ApplyToSource(*q, 0, "transform", "position.q", 0, &err) && Contains(err, "scale.x"));
    CHECK(PrefabTools::ApplyToSource(*q, 0, "transform", "scale.x", 0, &err));
}


static void TestDisabledComponents() {
    RUN_SUITE("components switched off");
    Files f("okay_prefab_ar_h");
    const std::string path = f.P("a.okayprefab");
    {
        Scene t("t");
        GameObject* o = t.CreateGameObject("A");
        o->AddComponent<SpriteRenderer>();
        o->AddComponent<BoxCollider2D>();
        SceneSerializer::SaveObjectToFile(*o, path);
    }
    Scene s("s");
    GameObject* inst = SceneSerializer::InstantiateFromFile(s, path);
    inst->GetComponent<SpriteRenderer>()->enabled = false;
    PrefabDiff d;
    std::string err;
    CHECK(PrefabTools::Diff(*inst, d, &err));
    const PrefabRecordDiff* off = d.Find(0, "compoff");
    CHECK(off && off->kind == PrefabRecordDiff::Kind::Added && off->instanceLine == "compoff \"SpriteRenderer\" 0" && d.records.size() == 1);
    // Revert switches it back on; apply writes it into the prefab and back out again.
    GameObject* on = PrefabTools::RevertOverride(s, *inst, 0, "compoff", "", 0, &err);
    s.FlushDestroyed();
    CHECK(on && on->GetComponent<SpriteRenderer>()->enabled);
    on->GetComponent<SpriteRenderer>()->enabled = false;
    CHECK(PrefabTools::ApplyToSource(*on, 0, "compoff", "", 0, &err));
    CHECK(Contains(Read(path), "compoff \"SpriteRenderer\" 0"));
    GameObject* fresh = SceneSerializer::InstantiateFromFile(s, path);
    CHECK(fresh && !fresh->GetComponent<SpriteRenderer>()->enabled);
    fresh->GetComponent<SpriteRenderer>()->enabled = true;
    CHECK(PrefabTools::Diff(*fresh, d, &err) && d.Find(0, "compoff") && d.Find(0, "compoff")->kind == PrefabRecordDiff::Kind::Removed);
    CHECK(PrefabTools::ApplyToSource(*fresh, 0, "compoff", "", 0, &err));
    CHECK(!Contains(Read(path), "compoff"));
}

static void TestFlatParity() {
    RUN_SUITE("plain prefabs: the same results as the wave 1 functions");
    Files f("okay_prefab_ar_f");
    MakeLamp(f.lamp);
    Scene s("s");
    GameObject* a = EditedLamp(s, f.lamp);
    std::string err;
    // Capture: the same override list.
    GameObject* b = SceneSerializer::InstantiateFromFile(s, f.lamp);
    b->transform->localPosition = Vec3{4, 0, 0};
    b->GetComponent<Light>()->intensity = 2.5f;
    Child(b, "Shade")->tag = "limb";
    CHECK(SceneSerializer::CapturePrefabOverrides(*b, &err));
    const auto wave1 = b->prefabOverrides;
    CHECK(PrefabTools::CaptureOverrides(*b, &err));
    CHECK(b->prefabOverrides.size() == wave1.size());
    for (std::size_t i = 0; i < wave1.size() && i < b->prefabOverrides.size(); ++i)
        CHECK(b->prefabOverrides[i].object == wave1[i].object && b->prefabOverrides[i].line == wave1[i].line);
    // Instantiate with overrides: the same objects.
    GameObject* x = SceneSerializer::InstantiatePrefab(s, f.lamp, wave1, &err);
    GameObject* y = PrefabTools::Instantiate(s, f.lamp, wave1, &err);
    CHECK(x && y && SceneSerializer::SerializeObject(*x) == SceneSerializer::SerializeObject(*y));
    (void)a;
}

int main() {
    TestDiff();
    TestRevert();
    TestApply();
    TestDamagedFiles();
    TestNestedApplyRevert();
    TestKeyValueRecordsAndNames();
    TestDisabledComponents();
    TestFlatParity();
    TEST_MAIN_RESULT();
}
