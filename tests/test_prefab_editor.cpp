// The editor's prefab actions (editor/PrefabEdit), driven the way the Inspector, Hierarchy and Project
// panel call them: which fields an instance overrides, Revert / Apply of a field, a component, a child
// object or everything, Apply updating the other instances, nested prefabs and variants, Create Variant,
// and Undo / Redo moving the scene and the prefab files together.
#include "test_framework.hpp"
#include "EditorState.hpp"
#include "InspectorModel.hpp"
#include "PrefabEdit.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace okay;
using namespace oked;
namespace fs = std::filesystem;
using okay::editor::EditorState;

static std::string Rd(const std::string& p) { std::string s; EditorState::ReadTextFile(p, s); return s; }
static bool Has(const std::string& s, const std::string& n) { return s.find(n) != std::string::npos; }
static GameObject* Kid(GameObject* g, const std::string& n) {
    if (!g) return nullptr;
    for (Transform* c : g->transform->Children()) if (c->gameObject->name == n && !c->gameObject->IsDestroyed()) return c->gameObject;
    return nullptr;
}
static std::vector<GameObject*> RootsWithSource(Scene& s, const std::string& path) {
    std::vector<GameObject*> v;
    for (const auto& o : s.Objects()) if (!o->IsDestroyed() && o->prefabSource == path) v.push_back(o.get());
    return v;
}
static GameObject* Only(Scene& s, const std::string& path, int n = 0) {
    auto v = RootsWithSource(s, path);
    return n < (int)v.size() ? v[(std::size_t)n] : nullptr;
}

struct Dir {
    fs::path d;
    explicit Dir(const char* n) { d = fs::temp_directory_path() / n; fs::remove_all(d); fs::create_directories(d); }
    ~Dir() { std::error_code ec; fs::remove_all(d, ec); }
    std::string P(const std::string& f) const { return (d / f).string(); }
};

// 0 Lamp (light + sprite), 1 Shade (sprite), 2 Base
static void MakeLamp(const std::string& path) {
    Scene s("l");
    GameObject* lamp = s.CreateGameObject("Lamp");
    auto* li = lamp->AddComponent<Light>(); li->intensity = 1.0f; li->range = 10.0f;
    lamp->AddComponent<SpriteRenderer>()->glyph = 'L';
    GameObject* shade = s.CreateGameObject("Shade");
    shade->transform->SetParent(lamp->transform, false);
    shade->AddComponent<SpriteRenderer>()->glyph = 's';
    GameObject* base = s.CreateGameObject("Base");
    base->transform->SetParent(lamp->transform, false);
    SceneSerializer::SaveObjectToFile(*lamp, path);
}

static GameObject* Place(EditorState& ed, const std::string& path) {
    std::string err;
    GameObject* g = SceneSerializer::InstantiateFromFile(ed.scene(), path, &err);
    if (!g) std::cerr << "instantiate failed: " << err << "\n";
    return g;
}

static void TestDetect() {
    RUN_SUITE("override detection and the Inspector's property names");
    Dir dir("okay_prefab_editor_detect");
    MakeLamp(dir.P("lamp.okayprefab"));
    EditorState ed;
    GameObject* inst = Place(ed, dir.P("lamp.okayprefab"));
    CHECK(inst != nullptr);
    if (!inst) return;
    PrefabInstanceInfo info;
    CHECK(DescribePrefabInstance(*inst, info));
    CHECK(info.isRoot && info.kind == PrefabKind::Plain && info.exists && info.name == "lamp.okayprefab" && !info.isNestedInstance);
    CHECK(PrefabKindLabel(info) == "Prefab");
    CHECK(DescribePrefabInstance(*Kid(inst, "Shade"), info) && !info.isRoot && info.root == inst);
    CHECK(PrefabChangesOf(*inst).empty() && PrefabOverrideCount(*inst) == 0);

    inst->GetComponent<Light>()->intensity = 3.0f;
    inst->transform->localPosition = Vec3{5, 0, 0};
    inst->AddComponent<Rigidbody2D>();
    Kid(inst, "Shade")->RemoveComponent<SpriteRenderer>();
    ClearPrefabInfoCache();
    auto ch = PrefabChangesOf(*inst);
    CHECK(ch.size() == 4);
    const PrefabChange* lc = FindChange(ch, *inst, *inst->GetComponent<Light>());
    CHECK(lc && lc->objectName == "Lamp" && !lc->wholeRecordOnly);
    if (lc) {
        CHECK((ChangedFieldsForLabel(*lc, *inst->GetComponent<Light>(), "Intensity") == std::vector<std::string>{"intensity"}));
        CHECK(ChangedFieldsForLabel(*lc, *inst->GetComponent<Light>(), "Range").empty());   // a property that was not changed
        CHECK(ChangedFieldsForLabel(*lc, *inst->GetComponent<Light>(), "No Such Label").empty());
    }
    const PrefabChange* tc = FindChange(ch, *inst, *inst->transform);
    if (!tc) for (const auto& c : ch) if (c.record == "transform") tc = &c;
    CHECK(tc != nullptr);
    if (tc) CHECK((ChangedFieldsForLabel(*tc, *inst->GetComponent<Light>(), "Position") == std::vector<std::string>{"position.x"}));
    int added = 0, removed = 0;
    for (const auto& c : ch) { added += c.added; removed += c.removed; }
    CHECK(added == 1 && removed == 1);                         // the new rigidbody, the lost sprite
    CHECK(PrefabOverrideCount(*inst) == 4);

    GameObject* bulb = ed.scene().CreateGameObject("Bulb");
    bulb->transform->SetParent(inst->transform, false);
    bulb->AddComponent<SpriteRenderer>();
    ed.scene().Destroy(Kid(inst, "Base")); ed.scene().FlushDestroyed();
    auto objs = PrefabObjectChangesOf(*inst);
    CHECK(objs.size() == 2);
    int a = 0, r = 0;
    for (const auto& o : objs) { a += o.added; r += !o.added; CHECK(!o.description.empty()); }
    CHECK(a == 1 && r == 1);
}

static void TestRevertAndUndo() {
    RUN_SUITE("revert a field, a component and everything, with undo and redo");
    Dir dir("okay_prefab_editor_revert");
    const std::string lamp = dir.P("lamp.okayprefab");
    MakeLamp(lamp);
    EditorState ed;
    GameObject* inst = Place(ed, lamp);
    if (!inst) { CHECK(false); return; }
    inst->name = "MyLamp";
    inst->GetComponent<Light>()->intensity = 3.0f;
    inst->GetComponent<Light>()->range = 4.0f;
    inst->transform->localPosition = Vec3{5, 0, 0};
    ed.dirty = false;
    auto ch = PrefabChangesOf(*inst);
    const PrefabChange* lc = FindChange(ch, *inst, *inst->GetComponent<Light>());
    CHECK(lc != nullptr);
    if (!lc) return;
    ed.CaptureUnannouncedEdits();   // the setup edits above are their own step, not part of the action measured below
    const int depth = ed.UndoDepth();
    PrefabOpResult r = PrefabRevert(ed, *inst, {TargetOf(*lc, "intensity")});
    CHECK(r.ok && r.root);
    CHECK(ed.UndoDepth() == depth + 1 && ed.dirty);
    GameObject* now = Only(ed.scene(), lamp);
    CHECK(now && now->name == "MyLamp");                                          // the rebuilt instance keeps its own name
    CHECK(now && now->GetComponent<Light>()->intensity == 1.0f);                 // the field is back to the prefab value
    CHECK(now && now->GetComponent<Light>()->range == 4.0f);                     // the other field keeps its override
    CHECK(now && now->transform->localPosition.x == 5.0f);
    CHECK(ed.Undo());
    now = Only(ed.scene(), lamp);
    CHECK(now && now->GetComponent<Light>()->intensity == 3.0f && now->GetComponent<Light>()->range == 4.0f);
    CHECK(ed.Redo());
    now = Only(ed.scene(), lamp);
    CHECK(now && now->GetComponent<Light>()->intensity == 1.0f && now->GetComponent<Light>()->range == 4.0f);

    // Whole component.
    ch = PrefabChangesOf(*now);
    lc = FindChange(ch, *now, *now->GetComponent<Light>());
    CHECK(lc != nullptr);
    if (lc) {
        r = PrefabRevert(ed, *now, {TargetOf(*lc)});
        CHECK(r.ok);
        now = Only(ed.scene(), lamp);
        CHECK(now && now->GetComponent<Light>()->range == 10.0f);
    }
    // A component added to the instance is removed by Revert; one the instance lost comes back.
    now->AddComponent<Rigidbody2D>();
    Kid(now, "Shade")->RemoveComponent<SpriteRenderer>();
    {
        std::vector<PrefabTarget> t;
        for (const auto& c : PrefabChangesOf(*now)) if (c.added || c.removed) t.push_back(TargetOf(c));
        CHECK(t.size() == 2);
        r = PrefabRevert(ed, *now, t);
        CHECK(r.ok);
        now = Only(ed.scene(), lamp);
        CHECK(now && !now->GetComponent<Rigidbody2D>() && Kid(now, "Shade") && Kid(now, "Shade")->GetComponent<SpriteRenderer>());
    }
    // Everything.
    now->GetComponent<Light>()->intensity = 9.0f;
    GameObject* bulb = ed.scene().CreateGameObject("Bulb");
    bulb->transform->SetParent(now->transform, false);
    r = PrefabRevertAll(ed, *now);
    CHECK(r.ok);
    now = Only(ed.scene(), lamp);
    CHECK(now && PrefabChangesOf(*now).empty() && PrefabObjectChangesOf(*now).empty() && !Kid(now, "Bulb"));
    CHECK(now && now->transform->localPosition.x == 0.0f);
    CHECK(ed.Undo());
    now = Only(ed.scene(), lamp);
    CHECK(now && now->GetComponent<Light>()->intensity == 9.0f && Kid(now, "Bulb"));

    // Not an instance: a clear refusal and no undo step.
    GameObject* plain = ed.scene().CreateGameObject("Plain");
    const int d2 = ed.UndoDepth();
    CHECK(!PrefabRevert(ed, *plain, {PrefabTarget{}}).ok && !PrefabRevertAll(ed, *plain).ok && !PrefabApply(ed, *plain, {PrefabTarget{}}).ok);
    CHECK(ed.UndoDepth() == d2);
}

static void TestApplyPropagation() {
    RUN_SUITE("apply writes the prefab and updates the instances that did not override the field");
    Dir dir("okay_prefab_editor_apply");
    const std::string lamp = dir.P("lamp.okayprefab");
    MakeLamp(lamp);
    const std::string original = Rd(lamp);
    EditorState ed;
    GameObject* a = Place(ed, lamp);
    GameObject* b = Place(ed, lamp);
    GameObject* c = Place(ed, lamp);
    GameObject* d = Place(ed, lamp);
    if (!a || !b || !c || !d) { CHECK(false); return; }
    b->name = "B"; c->name = "C"; d->name = "D";
    c->GetComponent<Light>()->intensity = 7.0f;          // overrode the same field: keeps its value
    d->GetComponent<Light>()->range = 2.0f;              // overrode another field: keeps that, takes the new intensity
    a->GetComponent<Light>()->intensity = 4.0f;
    a->GetComponent<Light>()->range = 8.0f;              // not applied
    ed.dirty = false;
    auto ch = PrefabChangesOf(*a);
    const PrefabChange* lc = FindChange(ch, *a, *a->GetComponent<Light>());
    CHECK(lc != nullptr);
    if (!lc) return;
    ed.CaptureUnannouncedEdits();   // the setup edits above are their own step, not part of the action measured below
    const int depth = ed.UndoDepth();
    PrefabOpResult r = PrefabApply(ed, *a, {TargetOf(*lc, "intensity")});
    CHECK(r.ok && r.updatedInstances == 3);
    CHECK(ed.UndoDepth() == depth + 1 && ed.dirty);
    CHECK(ed.UndoName() == "Applied to lamp.okayprefab");          // one named step, shared with the file edit
    const std::string applied = Rd(lamp);
    CHECK(applied != original);
    // The file has the new intensity and the old range.
    Scene probe("p");
    GameObject* fresh = SceneSerializer::InstantiateFromFile(probe, lamp);
    CHECK(fresh && fresh->GetComponent<Light>()->intensity == 4.0f && fresh->GetComponent<Light>()->range == 10.0f);
    GameObject* na = ed.scene().Find("Lamp");
    GameObject *nb = ed.scene().Find("B"), *nc = ed.scene().Find("C"), *nd = ed.scene().Find("D");
    CHECK(na && nb && nc && nd);
    if (!(na && nb && nc && nd)) return;
    CHECK(PrefabChangesOf(*na).size() == 1);                       // only the range is still different
    CHECK(nb->GetComponent<Light>()->intensity == 4.0f && nb->GetComponent<Light>()->range == 10.0f && PrefabChangesOf(*nb).empty());
    CHECK(nc->GetComponent<Light>()->intensity == 7.0f);            // had its own value for it
    CHECK(nd->GetComponent<Light>()->intensity == 4.0f && nd->GetComponent<Light>()->range == 2.0f);
    CHECK(na->GetComponent<Light>()->range == 8.0f);

    // Undo gives the old file and the old scene; Redo applies both again.
    CHECK(ed.Undo());
    CHECK(Rd(lamp) == original);
    SceneSerializer::ClearPrefabCache();
    nb = ed.scene().Find("B"); na = ed.scene().Find("Lamp"); nc = ed.scene().Find("C");
    CHECK(nb && nb->GetComponent<Light>()->intensity == 1.0f);
    CHECK(na && na->GetComponent<Light>()->intensity == 4.0f && PrefabChangesOf(*na).size() == 1);
    CHECK(nc && nc->GetComponent<Light>()->intensity == 7.0f);
    CHECK(ed.Redo());
    CHECK(Rd(lamp) == applied);
    nb = ed.scene().Find("B"); na = ed.scene().Find("Lamp");
    CHECK(nb && nb->GetComponent<Light>()->intensity == 4.0f);
    CHECK(na && PrefabChangesOf(*na).size() == 1);

    // Reverting the same field on an instance that already took the apply does nothing harmful: nothing is left to revert.
    // Save the scene, reload it: instances, overrides and the applied prefab are all still right.
    const std::string scenePath = dir.P("level.okayscene");
    std::string err;
    CHECK(SceneSerializer::SaveToFile(ed.scene(), scenePath, &err));
    Scene again("again");
    CHECK(SceneSerializer::LoadFromFile(again, scenePath, &err));
    GameObject* la = again.Find("Lamp");
    GameObject* lc2 = again.Find("C");
    CHECK(la && la->prefabSource == lamp && la->GetComponent<Light>()->range == 8.0f);
    CHECK(lc2 && lc2->GetComponent<Light>()->intensity == 7.0f);
    CHECK(la && PrefabChangesOf(*la).size() == 1);
}

static void TestApplyComponentsAndAll() {
    RUN_SUITE("apply a whole component, an added and a lost component, then everything");
    Dir dir("okay_prefab_editor_comp");
    const std::string lamp = dir.P("lamp.okayprefab");
    MakeLamp(lamp);
    EditorState ed;
    GameObject* a = Place(ed, lamp);
    GameObject* b = Place(ed, lamp);
    if (!a || !b) { CHECK(false); return; }
    b->name = "B";
    a->GetComponent<Light>()->intensity = 5.0f;
    a->GetComponent<Light>()->range = 6.0f;
    a->AddComponent<Rigidbody2D>();
    Kid(a, "Shade")->RemoveComponent<SpriteRenderer>();
    auto ch = PrefabChangesOf(*a);
    std::vector<PrefabTarget> light;
    for (const auto& c : ch) if (c.record == "light") light.push_back(TargetOf(c));
    CHECK(light.size() == 1);
    PrefabOpResult r = PrefabApply(ed, *a, light);
    CHECK(r.ok);
    GameObject* nb = ed.scene().Find("B");
    CHECK(nb && nb->GetComponent<Light>()->intensity == 5.0f && nb->GetComponent<Light>()->range == 6.0f);
    GameObject* na = ed.scene().Find("Lamp");
    CHECK(na && PrefabChangesOf(*na).size() == 2);             // the new rigidbody and the lost sprite remain

    r = PrefabApplyAll(ed, *na);
    if (!r.ok) std::cerr << "ApplyAll: " << r.message << "\n";
    CHECK(r.ok && r.updatedInstances == 1);
    na = ed.scene().Find("Lamp"); nb = ed.scene().Find("B");
    CHECK(na && PrefabChangesOf(*na).empty());
    CHECK(nb && nb->GetComponent<Rigidbody2D>() != nullptr && Kid(nb, "Shade") && !Kid(nb, "Shade")->GetComponent<SpriteRenderer>());
    CHECK(Has(Rd(lamp), "rigidbody2d"));
    CHECK(ed.Undo());
    CHECK(!Has(Rd(lamp), "rigidbody2d"));
    nb = ed.scene().Find("B");
    CHECK(nb && nb->GetComponent<Rigidbody2D>() == nullptr);
}

static void TestObjects() {
    RUN_SUITE("child objects added to or lost from an instance");
    Dir dir("okay_prefab_editor_objs");
    const std::string lamp = dir.P("lamp.okayprefab");
    MakeLamp(lamp);
    EditorState ed;
    GameObject* a = Place(ed, lamp);
    GameObject* b = Place(ed, lamp);
    if (!a || !b) { CHECK(false); return; }
    b->name = "B";
    GameObject* bulb = ed.scene().CreateGameObject("Bulb");
    bulb->transform->SetParent(a->transform, false);
    bulb->AddComponent<SpriteRenderer>()->glyph = 'o';
    ed.scene().Destroy(Kid(a, "Base")); ed.scene().FlushDestroyed();
    auto objs = PrefabObjectChangesOf(*a);
    CHECK(objs.size() == 2);
    PrefabInstanceInfo info;
    CHECK(DescribePrefabInstance(*a, info) && ApplyObjectBlockedReason(info).empty());
    // Revert the added one: it goes. Undo brings it back.
    for (const auto& o : objs) if (o.added) { CHECK(PrefabRevertObject(ed, *a, o).ok); }
    a = ed.scene().Find("Lamp");
    CHECK(a && !Kid(a, "Bulb"));
    CHECK(ed.Undo());
    a = ed.scene().Find("Lamp");
    CHECK(a && Kid(a, "Bulb"));
    // Revert the lost one: it comes back from the prefab.
    objs = PrefabObjectChangesOf(*a);
    for (const auto& o : objs) if (!o.added) { PrefabOpResult r = PrefabRevertObject(ed, *a, o); CHECK(r.ok); }
    a = ed.scene().Find("Lamp");
    CHECK(a && Kid(a, "Base") && Kid(a, "Bulb"));
    CHECK(PrefabObjectChangesOf(*a).size() == 1);
    // Apply the added one: the prefab gets it, the other instance too.
    objs = PrefabObjectChangesOf(*a);
    CHECK(objs.size() == 1 && objs[0].added);
    if (objs.size() == 1) {
        PrefabOpResult r = PrefabApply(ed, *a, {});          // (empty target list is refused)
        CHECK(!r.ok);
        r = PrefabApplyObject(ed, *a, objs[0]);
        CHECK(r.ok && r.updatedInstances == 1);
        a = ed.scene().Find("Lamp");
        GameObject* nb = ed.scene().Find("B");
        CHECK(a && PrefabObjectChangesOf(*a).empty() && Kid(a, "Bulb"));
        CHECK(nb && Kid(nb, "Bulb") && Kid(nb, "Bulb")->GetComponent<SpriteRenderer>() && PrefabObjectChangesOf(*nb).empty());
        CHECK(Has(Rd(lamp), "Bulb"));
    }
    // Apply a lost one: the prefab drops it.
    a = ed.scene().Find("Lamp");
    ed.scene().Destroy(Kid(a, "Shade")); ed.scene().FlushDestroyed();
    objs = PrefabObjectChangesOf(*a);
    CHECK(objs.size() == 1 && !objs[0].added);
    if (objs.size() == 1) {
        PrefabOpResult ro = PrefabApplyObject(ed, *a, objs[0]);
        if (!ro.ok) std::cerr << "ApplyObject: " << ro.message << "\n";
        CHECK(ro.ok);
        GameObject* nb = ed.scene().Find("B");
        CHECK(nb && !Kid(nb, "Shade") && Kid(nb, "Bulb"));
        CHECK(!Has(Rd(lamp), "\"Shade\""));
    }
    CHECK(ed.Undo());                                           // file and scene come back together
    CHECK(Has(Rd(lamp), "\"Shade\""));
    GameObject* nb = ed.scene().Find("B");
    CHECK(nb && Kid(nb, "Shade"));
}

static void TestVariants() {
    RUN_SUITE("variants: Create Variant, apply into the variant, the base stays");
    Dir dir("okay_prefab_editor_variant");
    const std::string lamp = dir.P("lamp.okayprefab");
    MakeLamp(lamp);
    EditorState ed;
    GameObject* a = Place(ed, lamp);
    if (!a) { CHECK(false); return; }
    a->GetComponent<Light>()->intensity = 6.0f;
    Kid(a, "Shade")->tag = "limb";
    GameObject* bulb = ed.scene().CreateGameObject("Bulb");
    bulb->transform->SetParent(a->transform, false);
    const std::string vpath = DefaultVariantPath(lamp);
    CHECK(fs::path(vpath).filename() == "lamp Variant.okayprefab" && !fs::exists(vpath));
    const std::string baseText = Rd(lamp);
    ed.CaptureUnannouncedEdits();   // the setup edits above are their own step, not part of the action measured below
    const int depth = ed.UndoDepth();
    PrefabOpResult r = PrefabCreateVariant(ed, *a, vpath);
    CHECK(r.ok && fs::exists(vpath));
    CHECK(ed.UndoDepth() == depth + 1);
    CHECK(Rd(lamp) == baseText);                                    // the base is never touched
    CHECK(Has(Rd(vpath), "variant"));
    CHECK(PrefabKindOfFile(vpath) == PrefabKind::Variant);
    GameObject* v = Only(ed.scene(), vpath);
    CHECK(v != nullptr);
    if (!v) return;
    CHECK(v->GetComponent<Light>()->intensity == 6.0f && Kid(v, "Bulb"));
    PrefabInstanceInfo info;
    CHECK(DescribePrefabInstance(*v, info) && info.kind == PrefabKind::Variant && info.baseName == "lamp.okayprefab");
    CHECK(PrefabKindLabel(info) == "Prefab Variant of lamp.okayprefab");
    CHECK(PrefabChangesOf(*v).empty() && PrefabObjectChangesOf(*v).empty());   // it IS the variant now: nothing differs
    CHECK(!ApplyObjectBlockedReason(info).empty());
    CHECK(fs::path(ResolveVariantBase(info)).filename() == "lamp.okayprefab");

    // A second instance of the variant, then an apply into the variant: the base file stays.
    GameObject* v2 = Place(ed, vpath);
    CHECK(v2 != nullptr);
    if (!v2) return;
    v2->name = "V2";
    v->GetComponent<Light>()->range = 3.0f;
    auto ch = PrefabChangesOf(*v);
    const PrefabChange* lc = FindChange(ch, *v, *v->GetComponent<Light>());
    CHECK(lc != nullptr);
    if (lc) {
        r = PrefabApply(ed, *v, {TargetOf(*lc, "range")});
        CHECK(r.ok && r.updatedInstances == 1);
        CHECK(Rd(lamp) == baseText);
        CHECK(Has(Rd(vpath), "override"));
        GameObject* n2 = ed.scene().Find("V2");
        CHECK(n2 && n2->GetComponent<Light>()->range == 3.0f && n2->GetComponent<Light>()->intensity == 6.0f);
        const std::string variantApplied = Rd(vpath);
        CHECK(ed.Undo());
        CHECK(Rd(vpath) != variantApplied && !Has(Rd(vpath), "range 3"));      // the variant file went back with the scene
        CHECK(Rd(lamp) == baseText);
    }
    // A variant straight from a base file (Project panel), no differences.
    const std::string v3 = dir.P("empty.okayprefab");
    r = PrefabNewVariantFile(ed, lamp, v3);
    CHECK(r.ok && fs::exists(v3) && PrefabKindOfFile(v3) == PrefabKind::Variant);
    GameObject* e = Place(ed, v3);
    CHECK(e && e->GetComponent<Light>() && e->GetComponent<Light>()->intensity == 1.0f && PrefabChangesOf(*e).empty());
    CHECK(!PrefabNewVariantFile(ed, lamp, v3).ok);               // never overwrites
    // Undo removes the file again; Redo writes it back.
    int guard = 0;
    while (fs::exists(v3) && ed.CanUndo() && guard++ < 20) ed.Undo();
    CHECK(!fs::exists(v3));
    CHECK(ed.Redo() && fs::exists(v3));                          // Redo writes it back
}

static void TestNested() {
    RUN_SUITE("nested prefabs: an apply through the inner instance reaches every user of the inner prefab");
    Dir dir("okay_prefab_editor_nested");
    const std::string lamp = dir.P("lamp.okayprefab");
    MakeLamp(lamp);
    const std::string car = dir.P("car.okayprefab");
    {   // Car holds a Lamp instance as a part.
        Scene s("c");
        GameObject* c = s.CreateGameObject("Car");
        c->AddComponent<SpriteRenderer>();
        std::string err;
        GameObject* l = SceneSerializer::InstantiateFromFile(s, lamp, &err);
        l->transform->SetParent(c->transform, false);
        std::string e2;
        if (!PrefabTools::SavePrefab(*c, car, &e2)) std::cerr << "SavePrefab: " << e2 << "\n";
    }
    CHECK(PrefabKindOfFile(car) == PrefabKind::Nested);
    EditorState ed;
    GameObject* c1 = Place(ed, car);
    GameObject* c2 = Place(ed, car);
    GameObject* solo = Place(ed, lamp);
    if (!c1 || !c2 || !solo) { CHECK(false); return; }
    c2->name = "Car2"; solo->name = "Solo";
    GameObject* inner = nullptr;
    for (GameObject* n : PrefabTools::NestedInstances(*c1)) inner = n;
    CHECK(inner != nullptr);
    if (!inner) return;
    PrefabInstanceInfo info;
    CHECK(DescribePrefabInstance(*inner, info));
    CHECK(info.isNestedInstance && info.root == inner && info.outer == c1 && info.outerName == "car.okayprefab");
    CHECK(PrefabKindLabel(info) == "Prefab (nested in car.okayprefab)");
    CHECK(DescribePrefabInstance(*c1, info) && info.nestedCount == 1 && info.kind == PrefabKind::Nested);
    // Save As Prefab of an object that holds a prefab instance keeps the link instead of copying the objects.
    const std::string car2 = dir.P("car2.okayprefab");
    std::string saveErr;
    CHECK(SaveAsPrefabFile(*c1, car2, &saveErr));
    CHECK(Has(Rd(car2), "nestedprefab") && PrefabKindOfFile(car2) == PrefabKind::Nested);
    GameObject* plainSrc = ed.scene().CreateGameObject("PlainThing");
    plainSrc->AddComponent<Light>();
    CHECK(SaveAsPrefabFile(*plainSrc, dir.P("plain.okayprefab")) && PrefabKindOfFile(dir.P("plain.okayprefab")) == PrefabKind::Plain);
    // Edit the inner lamp inside the car, apply through the lamp: the lamp prefab changes and Solo follows.
    inner->GetComponent<Light>()->intensity = 8.0f;
    auto ch = PrefabChangesOf(*inner);
    const PrefabChange* lc = FindChange(ch, *inner, *inner->GetComponent<Light>());
    CHECK(lc != nullptr);
    if (!lc) return;
    PrefabOpResult r = PrefabApply(ed, *inner, {TargetOf(*lc, "intensity")});
    CHECK(r.ok);
    CHECK(Has(Rd(lamp), "8"));
    GameObject* nsolo = ed.scene().Find("Solo");
    CHECK(nsolo && nsolo->GetComponent<Light>()->intensity == 8.0f);
    GameObject* n2 = ed.scene().Find("Car2");
    CHECK(n2 != nullptr);
    if (n2) {
        GameObject* in2 = nullptr;
        for (GameObject* n : PrefabTools::NestedInstances(*n2)) in2 = n;
        CHECK(in2 && in2->GetComponent<Light>()->intensity == 8.0f);
    }
    CHECK(ed.Undo());
    CHECK(!Has(Rd(lamp), "intensity 8"));
    nsolo = ed.scene().Find("Solo");
    CHECK(nsolo && nsolo->GetComponent<Light>()->intensity == 1.0f);

    // Unpack: the instance becomes plain objects; Undo links it again.
    GameObject* nc = ed.scene().Find("Car2");
    CHECK(nc != nullptr);
    if (nc) {
        CHECK(PrefabUnpack(ed, *nc, false).ok);
        CHECK(nc->prefabSource.empty() && !PrefabTools::IsPrefabInstance(*nc));
        CHECK(ed.Undo());
        nc = ed.scene().Find("Car2");
        CHECK(nc && !nc->prefabSource.empty());
    }
    // Update All rebuilds the instances from the file.
    PrefabOpResult u = PrefabUpdateInstances(ed, lamp);
    CHECK(u.ok && u.updatedInstances >= 1);
    CHECK(!PrefabUpdateInstances(ed, dir.P("none.okayprefab")).ok);
}

int main() {
    TestDetect();
    TestRevertAndUndo();
    TestApplyPropagation();
    TestApplyComponentsAndAll();
    TestObjects();
    TestVariants();
    TestNested();
    TEST_MAIN_RESULT();
}
