// The logic behind the Hierarchy and Inspector windows (ImGui-free): the search filter, drag-and-drop
// reparenting, editing one property on several objects ("mixed" values), copy / paste / reset of a
// component's values, which prefab records an instance changed, and the single undo step for an edit
// made over several frames.
#include "test_framework.hpp"
#include "EditorState.hpp"
#include "EditUndo.hpp"
#include "InspectorModel.hpp"
#include <filesystem>
#include <fstream>
#include <set>

using namespace okay;
using namespace oked;
namespace fs = std::filesystem;
using okay::editor::EditorState;

static std::vector<std::string> KidNames(GameObject* p) {
    std::vector<std::string> n;
    for (Transform* c : p->transform->Children()) n.push_back(c->gameObject->name);
    return n;
}
static std::vector<std::string> RootNames(Scene& s) {
    std::vector<std::string> n;
    for (const auto& o : s.Objects()) if (!o->transform->Parent()) n.push_back(o->name);
    return n;
}
static std::string LowerStr(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
static GameObject* Make(Scene& s, const std::string& name, GameObject* parent = nullptr) {
    GameObject* g = s.CreateGameObject(name);
    if (parent) g->transform->SetParent(parent->transform, false);
    return g;
}

static void TestFilter() {
    RUN_SUITE("hierarchy search filter");
    Scene s("f");
    GameObject* cam = Make(s, "Eye"); cam->AddComponent<Camera>();
    GameObject* orc = Make(s, "Orc"); orc->tag = "Enemy"; orc->AddComponent<Rigidbody2D>();
    GameObject* lamp = Make(s, "Lamp"); lamp->AddComponent<Light>(); lamp->active = false;
    GameObject* crate = Make(s, "Crate"); crate->tag = "Pickup"; crate->editorLocked = true; crate->isStatic = true;
    auto hits = [&](const std::string& q) {
        std::set<std::string> r;
        HierFilter f = HierFilter::Parse(q);
        for (const auto& o : s.Objects()) if (f.Matches(*o)) r.insert(o->name);
        return r;
    };
    CHECK(HierFilter::Parse("").Empty());
    CHECK(HierFilter::Parse("   ").Empty());
    CHECK(hits("").size() == 4);                         // nothing typed: everything
    CHECK(hits("orc") == std::set<std::string>{"Orc"});  // name, any case
    CHECK(hits("camera") == std::set<std::string>{"Eye"});   // a kind word, as before
    CHECK(hits("light") == std::set<std::string>{"Lamp"});
    CHECK(hits("enemy") == std::set<std::string>{"Orc"});    // a plain word also finds the tag
    CHECK(hits("t:enemy") == std::set<std::string>{"Orc"});
    CHECK(hits("tag:pick") == std::set<std::string>{"Crate"});
    CHECK(hits("t:untagged").size() == 2);                  // Eye and Lamp have no tag
    CHECK(hits("c:rigidbody") == std::set<std::string>{"Orc"});
    CHECK(hits("c:Light") == std::set<std::string>{"Lamp"});
    CHECK(hits("comp:camera") == std::set<std::string>{"Eye"});
    CHECK(hits("is:off") == std::set<std::string>{"Lamp"});
    CHECK(hits("is:locked") == std::set<std::string>{"Crate"});
    CHECK(hits("is:static") == std::set<std::string>{"Crate"});
    CHECK(hits("-is:off").size() == 3);                     // a leading minus turns it round
    CHECK(hits("-t:enemy").size() == 3);
    CHECK(hits("c:light is:off") == std::set<std::string>{"Lamp"});   // all words must match
    CHECK(hits("c:light is:on").empty());
    CHECK(hits("t:").size() == 4);                          // half-typed prefix: no restriction yet
    CHECK(hits("nothinglikethis").empty());
    orc->layer = 5;
    CHECK(hits("l:" + LowerStr(Layers::Name(5))).count("Orc") == 1);
    // A path keeps its spaces and whole text.
    GameObject* par = Make(s, "My Parent"); GameObject* kid = Make(s, "Kid", par);
    HierFilter pf = HierFilter::Parse("My Parent/Kid");
    CHECK(pf.terms.size() == 1 && pf.PathText() == "My Parent/Kid");
    std::unordered_set<const GameObject*> ph;
    for (GameObject* g : s.FindAllByPath("My Parent/Kid")) ph.insert(g);
    CHECK(pf.Matches(*kid, &ph) && !pf.Matches(*par, &ph));
}

static void TestDrop() {
    RUN_SUITE("drag and drop in the hierarchy");
    {   // reorder roots, before and after
        Scene s("d");
        GameObject *a = Make(s, "A"), *b = Make(s, "B"), *c = Make(s, "C"), *d = Make(s, "D");
        CHECK(ApplyDrop(s, {d}, a, DropWhere::Before) == 1);
        CHECK((RootNames(s) == std::vector<std::string>{"D", "A", "B", "C"}));
        CHECK(ApplyDrop(s, {d}, c, DropWhere::After) == 1);
        CHECK((RootNames(s) == std::vector<std::string>{"A", "B", "C", "D"}));
        (void)b;
    }
    {   // inside: appended as the LAST child, world pose kept
        Scene s("d");
        GameObject *p = Make(s, "P"), *k1 = Make(s, "K1", p), *x = Make(s, "X");
        p->transform->localPosition = {10, 0, 0};
        x->transform->localPosition = {3, 0, 0};
        CHECK(ApplyDrop(s, {x}, p, DropWhere::Inside) == 1);
        CHECK((KidNames(p) == std::vector<std::string>{"K1", "X"}));
        CHECK_NEAR(x->transform->Position().x, 3.0f, 1e-4);
        CHECK_NEAR(x->transform->localPosition.x, -7.0f, 1e-4);
        (void)k1;
    }
    {   // between siblings of a parent, from another parent
        Scene s("d");
        GameObject *p = Make(s, "P"), *a = Make(s, "a", p), *b = Make(s, "b", p), *q = Make(s, "Q"), *z = Make(s, "z", q);
        CHECK(ApplyDrop(s, {z}, b, DropWhere::Before) == 1);
        CHECK((KidNames(p) == std::vector<std::string>{"a", "z", "b"}));
        CHECK(KidNames(q).empty());
        CHECK(ApplyDrop(s, {z}, nullptr, DropWhere::Inside) == 1);   // out to the top level
        CHECK(z->transform->Parent() == nullptr);
        (void)a;
    }
    {   // cycles and the target itself are refused
        Scene s("d");
        GameObject *p = Make(s, "P"), *c = Make(s, "C", p), *g = Make(s, "G", c);
        CHECK(ApplyDrop(s, {p}, g, DropWhere::Inside) == 0);
        CHECK(ApplyDrop(s, {p}, c, DropWhere::Before) == 0);
        CHECK(ApplyDrop(s, {c}, c, DropWhere::Inside) == 0);
        CHECK(p->transform->Parent() == nullptr && c->transform->Parent() == p->transform);
        CHECK(ApplyDrop(s, {p}, nullptr, DropWhere::Before) == 0);   // no target, not "inside": nothing
    }
    {   // a group keeps its order, whichever way it is picked, and a child of a dragged parent is not moved twice
        Scene s("d");
        GameObject *a = Make(s, "a"), *b = Make(s, "b"), *c = Make(s, "c"), *t = Make(s, "t");
        GameObject* bk = Make(s, "bk", b);
        CHECK(ApplyDrop(s, {c, a, bk, b}, t, DropWhere::After) == 3);   // bk travels with b
        CHECK((RootNames(s) == std::vector<std::string>{"t", "a", "b", "c"}));
        CHECK(bk->transform->Parent() == b->transform);
        CHECK(ApplyDrop(s, {c, a}, t, DropWhere::Before) == 2);
        CHECK((RootNames(s) == std::vector<std::string>{"a", "c", "t", "b"}));
        CHECK(ApplyDrop(s, {c, a}, t, DropWhere::Inside) == 2);
        CHECK((KidNames(t) == std::vector<std::string>{"a", "c"}));
    }
    {   // hooks run around each move
        Scene s("d");
        GameObject *a = Make(s, "a"), *b = Make(s, "b");
        int before = 0, after = 0;
        DropHooks h; h.before = [&](GameObject*) { ++before; }; h.after = [&](GameObject*) { ++after; };
        ApplyDrop(s, {a}, b, DropWhere::Inside, h);
        CHECK(before == 1 && after == 1);
    }
}

static void TestMixed() {
    RUN_SUITE("editing one property on several objects");
    Scene s("m");
    GameObject *a = Make(s, "a"), *b = Make(s, "b"), *c = Make(s, "c");
    a->AddComponent<Light>()->intensity = 2.0f;
    b->AddComponent<Light>()->intensity = 2.0f;
    auto* lc = c->AddComponent<Light>(); lc->intensity = 5.0f;
    auto info = PropertyRegistry::FindByName("Light");
    CHECK(info != nullptr);
    if (!info) return;
    const PropertyDesc* inten = info->Find("intensity");
    const PropertyDesc* col = info->Find("color");
    CHECK(inten && col);
    if (!inten || !col) return;
    std::vector<GameObject*> ab{a, b}, abc{a, b, c};
    FieldState st = ReadField(ab, "Light", 0, *inten);
    CHECK(st.have == 2 && !st.mixed && st.value.AsFloat() == 2.0f);
    st = ReadField(abc, "Light", 0, *inten);
    CHECK(st.have == 3 && st.mixed);
    CHECK(WriteField(abc, "Light", 0, *inten, PropertyValue::MakeFloat(7.0f)) == 3);
    st = ReadField(abc, "Light", 0, *inten);
    CHECK(!st.mixed && st.value.AsFloat() == 7.0f);
    CHECK(WriteField(abc, "Light", 0, *inten, PropertyValue::MakeFloat(7.0f)) == 0);   // same value: nothing changed
    // An object without the component is skipped, not an error.
    GameObject* d = Make(s, "d");
    st = ReadField({a, d}, "Light", 0, *inten);
    CHECK(st.have == 1 && !st.mixed);
    CHECK(WriteField({a, d}, "Light", 0, *inten, PropertyValue::MakeFloat(1.0f)) == 1);
    // A colour with one part different per object: only that part is "mixed", and editing one part keeps the rest.
    a->GetComponent<Light>()->color = Color{1.0f, 0.5f, 0.25f, 1.0f};
    b->GetComponent<Light>()->color = Color{1.0f, 0.1f, 0.25f, 1.0f};
    st = ReadField(ab, "Light", 0, *col);
    CHECK(st.mixed && st.axisMixed.size() == 4);
    CHECK(!st.axisMixed[0] && st.axisMixed[1] && !st.axisMixed[2] && !st.axisMixed[3]);
    CHECK(WriteFieldAxes(ab, "Light", 0, *col, PropertyValue::MakeColor(Color{0.0f, 0.0f, 0.0f, 1.0f}), 1u) == 2);   // red only
    CHECK(a->GetComponent<Light>()->color.r == 0.0f && a->GetComponent<Light>()->color.g == 0.5f);
    CHECK(b->GetComponent<Light>()->color.r == 0.0f && b->GetComponent<Light>()->color.g == 0.1f);
    // The Transform works the same way.
    auto tinfo = PropertyRegistry::FindByName("Transform");
    CHECK(tinfo != nullptr);
    if (tinfo) {
        const PropertyDesc* pos = tinfo->Find("position");
        CHECK(pos != nullptr);
        if (pos) {
            a->transform->localPosition = {1, 2, 3}; b->transform->localPosition = {1, 9, 3};
            FieldState ps = ReadField(ab, "Transform", 0, *pos);
            CHECK(ps.mixed && !ps.axisMixed[0] && ps.axisMixed[1] && !ps.axisMixed[2]);
            WriteFieldAxes(ab, "Transform", 0, *pos, PropertyValue::MakeVec3({5, 0, 0}), 1u);
            CHECK(a->transform->localPosition.x == 5 && a->transform->localPosition.y == 2);
            CHECK(b->transform->localPosition.x == 5 && b->transform->localPosition.y == 9);
        }
    }
    // Two lights on one object are told apart by their index.
    GameObject* e = Make(s, "e");
    e->AddComponent<Light>()->intensity = 1.0f;
    e->AddComponent<Light>()->intensity = 4.0f;
    CHECK(ComponentIndexOf(*e, *e->GetComponents<Light>()[1]) == 1);
    st = ReadField({e}, "Light", 1, *inten);
    CHECK(st.value.AsFloat() == 4.0f);
}

static void TestClipboard() {
    RUN_SUITE("copy, paste and reset a component");
    Scene s("c");
    GameObject *a = Make(s, "a"), *b = Make(s, "b");
    auto* la = a->AddComponent<Light>();
    la->intensity = 9.0f; la->range = 33.0f; la->color = Color{0, 1, 0, 1};
    auto* lb = b->AddComponent<Light>();
    ComponentClip clip = CopyComponentValues(*la);
    CHECK(clip.Valid() && clip.type == "Light");
    CHECK(PasteComponentValues(clip, *lb) > 0);
    CHECK(lb->intensity == 9.0f && lb->range == 33.0f && lb->color.g == 1.0f);
    CHECK(PasteComponentValues(clip, *lb) == 0);                // already equal
    auto* sr = a->AddComponent<SpriteRenderer>();
    CHECK(PasteComponentValues(clip, *sr) == 0);                // a different type is refused
    CHECK(!ComponentClip{}.Valid());
    CHECK(HasRegisteredProperties(*la));
    // Reset puts the factory values back.
    Light fresh;
    CHECK(ResetComponentValues(*la) > 0);
    CHECK(la->intensity == fresh.intensity && la->range == fresh.range);
    CHECK(ResetComponentValues(*la) == 0);                      // nothing left to reset
    CHECK(lb->intensity == 9.0f);                               // the other one is untouched
    // The change hook still hears about it (so a recorder sees pastes and resets).
    int heard = 0;
    int id = PropertyRegistry::AddChangeHook([&](const PropertyChange&) { ++heard; });
    PasteComponentValues(clip, *la);
    CHECK(heard > 0);
    PropertyRegistry::RemoveChangeHook(id);
}

static void TestComponentEditPropagation() {
    RUN_SUITE("a hand-drawn component edited with several objects selected");
    Scene s("p");
    GameObject *a = Make(s, "a"), *b = Make(s, "b"), *c = Make(s, "c"), *d = Make(s, "d");
    for (GameObject* g : {a, b, c}) { auto* l = g->AddComponent<Light>(); l->intensity = 2.0f; l->range = 7.0f; }
    c->GetComponent<Light>()->range = 99.0f;                 // differs, and stays different when only intensity changes
    std::vector<GameObject*> sel{a, b, c, d};
    Light* la = a->GetComponent<Light>();
    ComponentEdit e = BeginComponentEdit(sel, a, *la);
    CHECK(e.active);
    la->intensity = 5.0f;                                      // what the drawer does
    la->enabled = false;
    CHECK(PropagateComponentEdit(sel, a, *la, e) == 4);        // intensity + enabled, on b and c
    CHECK(b->GetComponent<Light>()->intensity == 5.0f && c->GetComponent<Light>()->intensity == 5.0f);
    CHECK(!b->GetComponent<Light>()->enabled && !c->GetComponent<Light>()->enabled);
    CHECK(c->GetComponent<Light>()->range == 99.0f && b->GetComponent<Light>()->range == 7.0f);   // untouched properties stay
    CHECK(d->GetComponent<Light>() == nullptr);               // an object without the component is skipped
    // Nothing changed: nothing written.
    e = BeginComponentEdit(sel, a, *la);
    CHECK(PropagateComponentEdit(sel, a, *la, e) == 0);
    // One object selected, or the object not in the selection: not active.
    CHECK(!BeginComponentEdit({a}, a, *la).active);
    CHECK(!BeginComponentEdit({b, c}, a, *la).active);
    // The second component of a type goes to the second one.
    for (GameObject* g : {a, b}) { g->AddComponent<Light>()->intensity = 1.0f; }
    Light* la2 = a->GetComponents<Light>()[1];
    e = BeginComponentEdit({a, b}, a, *la2);
    la2->intensity = 8.0f;
    CHECK(PropagateComponentEdit({a, b}, a, *la2, e) == 1);
    CHECK(b->GetComponents<Light>()[1]->intensity == 8.0f && b->GetComponents<Light>()[0]->intensity == 5.0f);
}

static void MakeLamp(const std::string& path) {
    Scene s("l");
    GameObject* lamp = s.CreateGameObject("Lamp");
    lamp->AddComponent<Light>()->intensity = 1.0f;
    lamp->AddComponent<SpriteRenderer>()->glyph = 'L';
    GameObject* shade = s.CreateGameObject("Shade");
    shade->transform->SetParent(lamp->transform, false);
    shade->AddComponent<SpriteRenderer>()->glyph = 's';
    GameObject* base = s.CreateGameObject("Base");
    base->transform->SetParent(lamp->transform, false);
    SceneSerializer::SaveObjectToFile(*lamp, path);
}

static void TestPrefabChanges() {
    RUN_SUITE("which prefab records an instance changed");
    const fs::path dir = fs::temp_directory_path() / "okay_inspector_model_prefab";
    fs::remove_all(dir); fs::create_directories(dir);
    const std::string path = (dir / "lamp.okayprefab").string();
    MakeLamp(path);
    Scene s("s");
    GameObject* inst = SceneSerializer::InstantiateFromFile(s, path);
    CHECK(inst != nullptr);
    if (!inst) return;
    CHECK(PrefabChangesOf(*inst).empty());                       // untouched: nothing highlighted
    inst->GetComponent<Light>()->intensity = 3.0f;
    GameObject* shade = nullptr;
    for (Transform* c : inst->transform->Children()) if (c->gameObject->name == "Shade") shade = c->gameObject;
    CHECK(shade != nullptr);
    shade->GetComponent<SpriteRenderer>()->glyph = 'x';
    std::vector<PrefabChange> ch = PrefabChangesOf(*inst);
    CHECK(ch.size() == 2);
    CHECK(IsChanged(ch, *inst, *inst->GetComponent<Light>()));
    CHECK(!IsChanged(ch, *inst, *inst->GetComponent<SpriteRenderer>()));   // the root's sprite is as in the prefab
    CHECK(IsChanged(ch, *shade, *shade->GetComponent<SpriteRenderer>()));
    for (const PrefabChange& c : ch) CHECK(c.object != nullptr && !c.description.empty());
    // A second instance of the prefab is its own: nothing leaks between them.
    GameObject* inst2 = SceneSerializer::InstantiateFromFile(s, path);
    CHECK(inst2 && PrefabChangesOf(*inst2).empty());
    // An object added to the instance does not shift the others.
    GameObject* extra = s.CreateGameObject("Bulb");
    extra->transform->SetParent(inst->transform, false);
    extra->transform->SetSiblingIndex(0);
    extra->AddComponent<SpriteRenderer>();
    ch = PrefabChangesOf(*inst);
    CHECK(IsChanged(ch, *shade, *shade->GetComponent<SpriteRenderer>()));
    // Not an instance: no changes, no crash.
    GameObject* plain = s.CreateGameObject("Plain");
    CHECK(PrefabChangesOf(*plain).empty());
    fs::remove_all(dir);
}

static void TestEditUndo() {
    RUN_SUITE("one undo step for an edit made over several frames");
    EditorState ed;
    ed.CreateEmpty("Thing");
    ed.dirty = false;
    auto thing = [&](const char* name = "Thing") { return ed.scene().Find(name); };
    const int base = ed.UndoDepth();
    EditUndo u;
    // A drag that changes the value over many frames: one step.
    u.Begin(ed);
    CHECK(u.IsOpen());
    thing()->transform->localPosition = {1, 0, 0};
    thing()->transform->localPosition = {2, 0, 0};
    CHECK(u.End(ed));
    CHECK(ed.UndoDepth() == base + 1 && ed.dirty);
    CHECK(ed.Undo());
    CHECK(thing() && thing()->transform->localPosition.x == 0.0f);        // the step brought the old value back
    CHECK(ed.Redo());
    CHECK(thing() && thing()->transform->localPosition.x == 2.0f);
    // Pressing a widget and letting go without changing anything records nothing.
    const int d0 = ed.UndoDepth();
    u.Begin(ed);
    CHECK(!u.End(ed));
    CHECK(ed.UndoDepth() == d0);
    // A widget that pushes its own step first: not doubled.
    u.Begin(ed);
    ed.PushUndo();
    thing()->name = "Renamed";
    CHECK(!u.End(ed));
    CHECK(ed.UndoDepth() == d0 + 1);
    // Rename through the session, then undo.
    u.Begin(ed);
    thing("Renamed")->name = "Again";
    CHECK(u.End(ed));
    CHECK(thing("Again") != nullptr);
    CHECK(ed.Undo());
    CHECK(thing("Renamed") != nullptr && thing("Again") == nullptr);
    // Begin twice keeps the first picture; End without Begin is a no-op; Cancel forgets.
    u.Begin(ed); u.Begin(ed);
    thing("Renamed")->name = "x";
    u.Cancel();
    CHECK(!u.IsOpen() && !u.End(ed));
    // Undo while a session is open (the scene was rebuilt): nothing is recorded on top of it.
    ed.dirty = false;
    u.Begin(ed);
    CHECK(ed.Undo());
    CHECK(!u.End(ed));
}

static void TestLabelsAndKinds() {
    RUN_SUITE("field labels and asset kinds");
    CHECK(PrettyName("spotAngle") == "Spot Angle");
    CHECK(PrettyName("intensity") == "Intensity");
    CHECK(PrettyName("castShadows") == "Cast Shadows");
    CHECK(PrettyName("uvScale") == "Uv Scale");
    CHECK(PrettyName("UIScale") == "UI Scale");
    CHECK(PrettyName("max_speed") == "Max Speed");
    CHECK(PrettyName("x") == "X");
    CHECK(PrettyName("") == "");
    CHECK(AssetKindAccepts("image", "/p/Assets/a.PNG"));
    CHECK(!AssetKindAccepts("image", "/p/Assets/a.wav"));
    CHECK(AssetKindAccepts("audio", "x.ogg") && AssetKindAccepts("model", "x.glb") && AssetKindAccepts("font", "x.ttf"));
    CHECK(AssetKindAccepts("prefab", "x.okayprefab") && !AssetKindAccepts("prefab", "x.okayscene"));
    CHECK(AssetKindAccepts("", "anything.bin") && AssetKindAccepts("file", "x") && AssetKindAccepts("nosuchkind", "x.y"));
    CHECK(!AssetKindAccepts("image", "noextension"));
    CHECK(!AssetKindAccepts("", ""));
    const fs::path dir = fs::temp_directory_path() / "okay_inspector_model_assets";
    fs::remove_all(dir); fs::create_directories(dir / "Sub" / "Deep"); fs::create_directories(dir / ".hidden");
    for (const char* f : {"a.png", "Sub/b.png", "Sub/Deep/cc.jpg", "Sub/s.wav", ".hidden/h.png", ".dot.png"}) {
        std::ofstream o(dir / f); o << "x";
    }
    std::vector<std::string> imgs = ListAssetsOfKind(dir.string(), "image");
    CHECK(imgs.size() == 3);
    CHECK(!imgs.empty() && imgs[0] == (dir / "a.png").string());     // shortest path first
    CHECK(ListAssetsOfKind(dir.string(), "audio").size() == 1);
    CHECK(ListAssetsOfKind(dir.string(), "").size() == 4);            // any kind: all visible files
    CHECK(ListAssetsOfKind(dir.string(), "image", 2).size() == 2);    // the limit holds
    CHECK(ListAssetsOfKind((dir / "missing").string(), "image").empty());
    CHECK(ListAssetsOfKind("", "image").empty());
    fs::remove_all(dir);
}

int main() {
    TestFilter();
    TestDrop();
    TestMixed();
    TestClipboard();
    TestComponentEditPropagation();
    TestPrefabChanges();
    TestEditUndo();
    TestLabelsAndKinds();
    TEST_MAIN_RESULT();
}
