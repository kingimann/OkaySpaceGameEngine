// What the Inspector's right-click menus do to the scene (editor/InspectorModel, ImGui-free): move a component to the top or
// bottom, duplicate it, paste a copied component as a new one, copy and paste every value of an object, reset one value,
// the Transform copy / paste / reset parts, the check for a component another one needs, and what Paste Value does with the
// clipboard text. The rules for which rows show are in tests/test_inspector_context_model.cpp; this file checks the operations
// behind the rows, and that what they copy comes back through the scene text unchanged. A copy is judged by the TEXT THE SCENE
// FILE SAVES for the original and for the copy, never by comparing registry snapshots with themselves.
#include "test_framework.hpp"
#include "AddComponentRules.hpp"
#include "InspectorModel.hpp"
#include <Okay.hpp>
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/PropertyBuilder.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/SceneDiff.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <new>
#include <set>
#include <string>
#include <vector>

// Count what is allocated while a measurement is switched on (ParseText must not copy or split a huge clipboard text).
namespace {
std::size_t g_allocated = 0;
bool g_counting = false;
}   // namespace
void* operator new(std::size_t n) {
    if (g_counting) g_allocated += n;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace okay;
using namespace oked;
using inspectorctx::MoveKind;

namespace {

GameObject* Make(Scene& s, const std::string& name, GameObject* parent = nullptr) {
    GameObject* g = s.CreateGameObject(name);
    if (parent) g->transform->SetParent(parent->transform, false);
    return g;
}

std::vector<std::string> Order(const GameObject* g) {
    std::vector<std::string> out;
    for (const auto& c : g->Components()) {
        if (c.get() == static_cast<const Component*>(g->transform)) continue;
        out.push_back(ComponentSerializerRegistry::TypeKey(*c));
    }
    return out;
}

int CountType(const GameObject* g, const std::string& type) {
    int n = 0;
    for (const auto& c : g->Components()) if (ComponentSerializerRegistry::TypeKey(*c) == type) ++n;
    return n;
}

// Change every authored value of a component that can be changed to something else. Returns how many changed.
int Mutate(Component& c) {
    auto info = PropertyRegistry::FindFor(c);
    if (!info) return 0;
    int changed = 0;
    for (const PropertyDesc& d : info->properties) {
        if (d.IsReadOnly() || d.IsHidden() || d.IsRuntimeOnly() || d.IsDerived() || (d.flags & PropertyFlag::Structural)) continue;
        if (d.name == "enabled") continue;
        const PropertyValue cur = PropertyRegistry::GetComponentProperty(c, d.name);
        PropertyValue next;
        switch (d.type) {
            case PropertyType::Bool:  next = PropertyValue::MakeBool(!cur.AsBool()); break;
            case PropertyType::Int:   next = PropertyValue::MakeInt(cur.AsInt() + 1); break;
            case PropertyType::Float: next = PropertyValue::MakeFloat(cur.AsFloat() + 0.25f); break;
            case PropertyType::Vec2:  { Vec2 v = cur.AsVec2(); v.x += 0.5f; next = PropertyValue::MakeVec2(v); break; }
            case PropertyType::Vec3:  { Vec3 v = cur.AsVec3(); v.x += 0.5f; next = PropertyValue::MakeVec3(v); break; }
            case PropertyType::Vec4:  { Vec4 v = cur.AsVec4(); v.x += 0.5f; next = PropertyValue::MakeVec4(v); break; }
            case PropertyType::Color: { Color v = cur.AsColor(); v.r = v.r > 0.5f ? v.r - 0.25f : v.r + 0.25f; next = PropertyValue::MakeColor(v); break; }
            case PropertyType::Enum:  if (!d.enumOptions.empty()) { const int i = (cur.AsEnum() + 1) % (int)d.enumOptions.size(); next = PropertyValue::MakeEnum(i, d.enumOptions[(std::size_t)i]); } break;
            default: break;
        }
        if (next.IsValid() && PropertyRegistry::SetComponentProperty(c, d.name, next)) ++changed;
    }
    return changed;
}

bool SameValues(const Component& a, const Component& b) {
    return DiffProperties(SnapshotComponent(a), SnapshotComponent(b)).empty();
}

void TestNames() {
    RUN_SUITE("component names as the headers write them");
    CHECK(TypeDisplayName("Rigidbody2D") == "Rigidbody 2D");
    CHECK(TypeDisplayName("BoxCollider3D") == "Box Collider 3D");
    CHECK(TypeDisplayName("SpriteRenderer") == "Sprite Renderer");
    CHECK(TypeDisplayName("UIButton") == "UI Button");
    CHECK(TypeDisplayName("AimIK") == "Aim IK");
    CHECK(TypeDisplayName("Light") == "Light");
    Scene s("n");
    GameObject* g = Make(s, "g");
    CHECK(ComponentDisplayName(*g->AddComponent<Light>()) == "Light");
    CHECK(ComponentDisplayName(*g->AddComponent<Rigidbody2D>()) == "Rigidbody 2D");
}

void TestMoves() {
    RUN_SUITE("move a component to the top or bottom");
    Scene s("m");
    GameObject* g = Make(s, "g");
    auto* sr = g->AddComponent<SpriteRenderer>();
    auto* rb = g->AddComponent<Rigidbody2D>();
    auto* bc = g->AddComponent<BoxCollider2D>();
    auto* li = g->AddComponent<Light>();
    CHECK(Order(g) == (std::vector<std::string>{"SpriteRenderer", "Rigidbody2D", "BoxCollider2D", "Light"}));
    ComponentSlot sl = SlotOf(*g, *bc);
    CHECK(sl.index == 2 && sl.count == 4);
    CHECK(SlotOf(*g, *g->transform).index == -1);                  // the Transform is not in the list
    CHECK(MoveComponentTo(*g, *bc, MoveKind::Top));
    CHECK(Order(g) == (std::vector<std::string>{"BoxCollider2D", "SpriteRenderer", "Rigidbody2D", "Light"}));
    CHECK(g->Components()[0].get() == static_cast<const Component*>(g->transform));   // the Transform stays first
    CHECK(!MoveComponentTo(*g, *bc, MoveKind::Top));               // already there: nothing moves
    CHECK(!MoveComponentTo(*g, *bc, MoveKind::Up));
    CHECK(MoveComponentTo(*g, *sr, MoveKind::Bottom));
    CHECK(Order(g) == (std::vector<std::string>{"BoxCollider2D", "Rigidbody2D", "Light", "SpriteRenderer"}));
    CHECK(!MoveComponentTo(*g, *sr, MoveKind::Bottom));
    CHECK(!MoveComponentTo(*g, *sr, MoveKind::Down));
    CHECK(MoveComponentTo(*g, *rb, MoveKind::Down));
    CHECK(Order(g) == (std::vector<std::string>{"BoxCollider2D", "Light", "Rigidbody2D", "SpriteRenderer"}));
    CHECK(MoveComponentTo(*g, *li, MoveKind::Up));
    CHECK(Order(g) == (std::vector<std::string>{"Light", "BoxCollider2D", "Rigidbody2D", "SpriteRenderer"}));
    CHECK(g->HasCustomComponentOrder());                           // the new order is kept in the scene file
    // The order survives a save and load.
    Scene t("m2");
    CHECK(SceneSerializer::Deserialize(t, SceneSerializer::Serialize(s)));
    GameObject* tg = t.Find("g");
    CHECK(tg && Order(tg) == Order(g));
}

bool Contains(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

// What the scene file saves for the object alone (its components, no children).
std::string Saved(const GameObject& g) { return SceneSerializer::SerializeObject(g, false); }

// The scene text of `g` loaded into `scene`; null when it does not load.
GameObject* Reload(Scene& scene, const GameObject& g) { return SceneSerializer::InstantiateFromText(scene, Saved(g)); }

// The n-th component of a kind (by registry entry) on an object.
Component* Nth(GameObject& g, const std::shared_ptr<const ComponentTypeInfo>& info, int n) {
    int i = 0;
    for (const auto& up : g.Components()) if (up && PropertyRegistry::FindFor(*up) == info) { if (i++ == n) return up.get(); }
    return nullptr;
}

void TestDuplicate() {
    RUN_SUITE("duplicate a component (a kind the Add Component list offers again and again)");
    // Volume is one the browser never hides, so it may be duplicated. Everything else of the old test (Light, Rigidbody 2D ...) is
    // "only one per object" now: see TestOnePerObject.
    Scene s("d");
    GameObject* g = Make(s, "g");
    auto* vol = g->AddComponent<Volume>();
    g->AddComponent<Rigidbody2D>();
    vol->size = {3.0f, 4.0f, 5.0f}; vol->eventName = "a"; vol->forceStrength = 7.5f;
    vol->enabled = false;
    std::string why;
    CHECK(CanAddAnother(*vol, &why) && why.empty());
    CHECK(CanDuplicate(*g, *vol, &why));
    Component* dup = DuplicateComponent(*g, *vol);
    CHECK(dup && dup != vol);
    CHECK(CountType(g, "Volume") == 2);
    CHECK(Order(g) == (std::vector<std::string>{"Volume", "Volume", "Rigidbody2D"}));   // right below the original
    auto* dv = dynamic_cast<Volume*>(dup);
    CHECK(dv && dv->size.x == 3.0f && dv->size.z == 5.0f && dv->eventName == "a" && dv->forceStrength == 7.5f);
    CHECK(dv && !dv->enabled);                                      // the on / off state comes along
    // The two are separate objects in memory: editing the copy leaves the original alone, and the other way round.
    dv->size = {9.0f, 9.0f, 9.0f}; dv->eventName = "copy";
    CHECK(vol->size.x == 3.0f && vol->eventName == "a");
    vol->forceStrength = -1.0f;
    CHECK(dv->forceStrength == 7.5f);
    // Both survive a save and load, in order, with their own values.
    Scene t("d2");
    CHECK(SceneSerializer::Deserialize(t, SceneSerializer::Serialize(s)));
    GameObject* tg = t.Find("g");
    CHECK(tg && CountType(tg, "Volume") == 2);
    if (tg && CountType(tg, "Volume") == 2) {
        const auto vols = tg->GetComponents<Volume>();
        CHECK(vols[0]->eventName == "a" && vols[1]->eventName == "copy" && vols[0]->size.x == 3.0f && vols[1]->size.x == 9.0f);
    }
    // Duplicating the duplicate adds a third, again right below the one it came from.
    Component* third = DuplicateComponent(*g, *dup);
    CHECK(third && CountType(g, "Volume") == 3);
    CHECK(Order(g) == (std::vector<std::string>{"Volume", "Volume", "Volume", "Rigidbody2D"}));
    // A component the registry does not know cannot be duplicated, and says why.
    GameObject* h = Make(s, "h");
    auto* sc = h->AddComponent<ScriptComponent>();
    CHECK(!HasRegisteredProperties(*sc));
    CHECK(!CanAddAnother(*sc, &why) && !why.empty());
    CHECK(DuplicateComponent(*h, *sc) == nullptr);
}

// The Add Component browser hides a kind once the object has one, and Duplicate and Paste As New must refuse it too (finding 1).
void TestOnePerObject() {
    RUN_SUITE("kinds the Add Component list offers once per object are refused by Duplicate and Paste As New");
    Scene s("o");
    GameObject* g = Make(s, "g");
    auto* rb = g->AddComponent<Rigidbody2D>();
    auto* li = g->AddComponent<Light>();
    auto* bc = g->AddComponent<BoxCollider3D>();
    auto* au = g->AddComponent<AudioSource>();
    auto* ps = g->AddComponent<ParticleSystem>();
    std::string why;
    // The browser's own test, and the menus', give the same answer for the same kind.
    for (Component* c : std::vector<Component*>{rb, li, bc, au, ps}) {
        const std::string name = ComponentSerializerRegistry::TypeKey(*c);
        auto info = PropertyRegistry::FindFor(*c);
        CHECK(info && IsOnePerObject(info->type));
        CHECK(HasOnePerObjectKind(*g, info->type));
        why.clear();
        CHECK(!CanAddAnother(*c, &why));
        CHECK(Contains(why, "Only one per object") && Contains(why, TypeDisplayName(name)));
        why.clear();
        CHECK(!CanDuplicate(*g, *c, &why) && Contains(why, "Only one per object"));
        const std::size_t before = g->Components().size();
        CHECK(DuplicateComponent(*g, *c) == nullptr);
        CHECK(g->Components().size() == before);                    // nothing was added
        // Paste As New of the same kind onto an object that already has one: refused. Onto one that has none: fine.
        const ComponentClip clip = CopyComponentValues(*c);
        why.clear();
        CHECK(!CanPasteAsNew(*g, clip, &why) && Contains(why, "Only one per object"));
        CHECK(PasteComponentAsNew(*g, clip) == nullptr && g->Components().size() == before);
        GameObject* other = Make(s, "other " + name);
        CHECK(CanPasteAsNew(*other, clip, &why));
        CHECK(PasteComponentAsNew(*other, clip) != nullptr);
        CHECK(!CanPasteAsNew(*other, clip, &why));                  // and now it has one
        CHECK(CountType(other, name) == 1);
    }
    // The Add Component test itself, the template the browser's rows call, agrees: true while there is none, false once there is one.
    GameObject* bare = Make(s, "bare");
    CHECK(CanAddOne<Rigidbody2D>(*bare) && CanAddOne<Light>(*bare) && CanAddOne<BoxCollider3D>(*bare));
    bare->AddComponent<Rigidbody2D>(); bare->AddComponent<Light>();
    CHECK(!CanAddOne<Rigidbody2D>(*bare) && !CanAddOne<Light>(*bare) && CanAddOne<BoxCollider3D>(*bare));
    // A kind the browser offers repeatedly is not in the table.
    CHECK(!IsOnePerObject(std::type_index(typeid(Volume))));
    CHECK(!IsOnePerObject(std::type_index(typeid(ScriptComponent))));
    CHECK(!HasOnePerObjectKind(*g, std::type_index(typeid(Volume))));
    // A project rule (DisallowMultipleComponent) is respected as well, for a kind that is not in the table.
    GameObject* u = Make(s, "u");
    auto* v1 = u->AddComponent<Volume>();
    ComponentRules::SetUnique(typeid(Volume));
    CHECK(!CanAddAnother(*v1, &why) && Contains(why, "Only one per object"));
    CHECK(DuplicateComponent(*u, *v1) == nullptr && CountType(u, "Volume") == 1);
    ComponentRules::Clear();
    CHECK(CanAddAnother(*v1));
}

// A component that saves more than its registered values: a copy made from the registered values is not the same (finding 2).
struct HalfRegistered : Component {
    int shown = 0;      // registered: a copy keeps it
    int hidden = 0;     // saved in the scene file but not a registered property: a copy loses it
};
struct OneRecord : Component {
    int value = 0;
};

void RegisterTestKinds() {
    static bool done = false;
    if (done) return;
    done = true;
    PropertyBuilder<HalfRegistered>("HalfRegistered", "halfregistered", "Test").AddInt("shown", &HalfRegistered::shown).Commit();
    PropertyBuilder<OneRecord>("OneRecord", "onerecord", "Test").AddInt("value", &OneRecord::value).Commit();
    ComponentSerializerRegistry::Entry half;
    half.name = "halfregistered";
    half.write = [](const GameObject& go, std::ostream& out) {
        for (auto* c : go.GetComponents<HalfRegistered>()) out << "  halfregistered " << c->shown << " " << c->hidden << "\n";
    };
    half.read = [](GameObject& go, std::istream& in) { auto* c = go.AddComponent<HalfRegistered>(); in >> c->shown >> c->hidden; };
    ComponentSerializerRegistry::Register(half);
    ComponentSerializerRegistry::Entry one;   // a writer that saves the first component only: a second one would never reach the file
    one.name = "onerecord";
    one.write = [](const GameObject& go, std::ostream& out) { if (auto* c = go.GetComponent<OneRecord>()) out << "  onerecord " << c->value << "\n"; };
    one.read = [](GameObject& go, std::istream& in) { int v = 0; in >> v; go.AddComponent<OneRecord>()->value = v; };
    ComponentSerializerRegistry::Register(one);
}

void TestCopyExactness() {
    RUN_SUITE("a copy is only offered when it saves as the same scene text");
    RegisterTestKinds();
    std::string why;
    // 1. Everything the file keeps is a registered value: exact.
    {
        Scene s("x");
        GameObject* g = Make(s, "g");
        auto* v = g->AddComponent<Volume>();
        v->size = {2, 3, 4}; v->eventName = "z";
        const CopyCheck c = CheckCopyIsExact(*g, *v);
        CHECK(c.exact && c.saved && c.note.empty());
        CHECK(CanDuplicate(*g, *v, &why) && DuplicateComponent(*g, *v) != nullptr);
    }
    // 2. The file keeps more than the registry holds: not exact; Duplicate is refused with the saved record named; Paste As New too.
    {
        Scene s("x");
        GameObject* g = Make(s, "g");
        auto* h = g->AddComponent<HalfRegistered>();
        h->shown = 5; h->hidden = 9;
        const CopyCheck c = CheckCopyIsExact(*g, *h);
        CHECK(!c.exact && c.saved && Contains(c.note, "halfregistered"));
        why.clear();
        CHECK(!CanDuplicate(*g, *h, &why) && Contains(why, "would not be the same") && Contains(why, "halfregistered"));
        CHECK(DuplicateComponent(*g, *h) == nullptr && CountType(g, "HalfRegistered") == 1);
        ComponentClip clip = CopyComponentValues(*h);
        clip.exact = c.exact; clip.inexactNote = c.note;           // what Copy Values stores
        GameObject* other = Make(s, "other");
        why.clear();
        CHECK(!CanPasteAsNew(*other, clip, &why) && Contains(why, "would not be the same") && Contains(why, "halfregistered"));
        CHECK(PasteComponentAsNew(*other, clip) == nullptr && CountType(other, "HalfRegistered") == 0);
        // The same copy WITHOUT the gate really is partial (this is what used to be offered): the hidden part is lost in the saved text.
        auto info = PropertyRegistry::FindFor(*h);
        Component* partial = info->add(*other);
        PasteComponentValues(clip, *partial);
        CHECK(static_cast<HalfRegistered*>(partial)->shown == 5 && static_cast<HalfRegistered*>(partial)->hidden == 0);
        CHECK(Saved(*g) != Saved(*other));
        // With nothing hidden set, the copy is the same after all (the object really holds only registered state).
        h->hidden = 0;
        CHECK(CheckCopyIsExact(*g, *h).exact);
    }
    // 3. A writer that saves one record per object: a second component would never be saved, so Duplicate is refused.
    {
        Scene s("x");
        GameObject* g = Make(s, "g");
        auto* o = g->AddComponent<OneRecord>();
        o->value = 4;
        const CopyCheck c = CheckCopyIsExact(*g, *o);
        CHECK(!c.exact && Contains(c.note, "only one") && Contains(c.note, "not be saved"));
        CHECK(!CanDuplicate(*g, *o, &why) && DuplicateComponent(*g, *o) == nullptr);
    }
    // 4. The real one: a particle system's gradient and size curve are saved but are not registered values.
    {
        Scene s("x");
        GameObject* g = Make(s, "g");
        auto* ps = g->AddComponent<ParticleSystem>();
        ps->emissionRate = 33.0f;
        const CopyCheck plain = CheckCopyIsExact(*g, *ps);
        ps->colorGradient.Add(0.0f, Color{1, 0, 0, 1});
        ps->colorGradient.Add(1.0f, Color{0, 0, 1, 1});
        ps->sizeCurve.Add(0.0f, 0.5f);
        ps->sizeCurve.Add(1.0f, 2.0f);
        const CopyCheck edited = CheckCopyIsExact(*g, *ps);
        std::cout << "  particle system, defaults: exact=" << plain.exact << "; with a gradient and a curve: exact=" << edited.exact << " (" << edited.note << ")\n";
        CHECK(!edited.exact && edited.saved);
        // What the old code did: copy the registered values onto a fresh component. The saved text differs, which is the whole point.
        GameObject* other = Make(s, "other");
        ComponentClip clip = CopyComponentValues(*ps);
        auto info = PropertyRegistry::FindFor(*ps);
        PasteComponentValues(clip, *info->add(*other));
        CHECK(other->GetComponent<ParticleSystem>()->emissionRate == 33.0f);
        CHECK(!other->GetComponent<ParticleSystem>()->colorGradient.Active());                 // the gradient did not come along
        // Paste As New is refused for a clip taken from it, and says what is lost.
        clip.exact = edited.exact; clip.inexactNote = edited.note;
        GameObject* third = Make(s, "third");
        why.clear();
        CHECK(!CanPasteAsNew(*third, clip, &why) && Contains(why, "would not be the same"));
    }
    // 5. A kind the scene file does not keep at all: nothing is saved, so nothing is lost; the check says so instead of guessing.
    {
        Scene s("x");
        GameObject* g = Make(s, "g");
        auto* sc = g->AddComponent<ScriptComponent>();
        CHECK(!CheckCopyIsExact(*g, *sc).exact);                    // no registry entry: cannot be copied at all
    }
}

// Every registered kind, three ways: (1) the menu's verdict, (2) an independent comparison of the saved text of the original and of a
// copy made from its registered values, (3) for the kinds the menu allows, a real Duplicate whose saved text is compared (finding 2).
void TestEveryRegisteredKind() {
    RUN_SUITE("every registered kind: the verdict matches what the scene file saves");
    RegisterTestKinds();
    int kinds = 0, enabled = 0, onceOnly = 0, inexact = 0, unsavedKinds = 0;
    std::vector<std::string> bad, inexactKinds, enabledKinds, notTwo, tooBigKinds;
    for (const auto& info : PropertyRegistry::All()) {
        if (!info || !info->add || info->objectLevel || info->properties.empty()) continue;
        if (info->name == "HalfRegistered" || info->name == "OneRecord") continue;   // made by this test, checked in TestCopyExactness
        Scene s("sweep");
        GameObject* g = Make(s, "g");
        Component* a = info->add(*g);
        if (!a) continue;
        ++kinds;
        Mutate(*a);
        const CopyCheck verdict = CheckCopyIsExact(*g, *a);
        if (!verdict.saved) ++unsavedKinds;
        // (2) a copy made from the registered values on another object, compared as saved text with the original's.
        Scene s2("sweep copy");
        GameObject* g2 = Make(s2, "g");
        Component* copy = info->add(*g2);
        if (copy) { PasteComponentValues(CopyComponentValues(*a), *copy); copy->enabled = a->enabled; }
        const bool sameText = copy && Saved(*g) == Saved(*g2);
        const bool tooBig = !verdict.exact && verdict.note.find("too big") != std::string::npos;   // the menu does not compare an object whose text is over 1 MB
        if (tooBig) tooBigKinds.push_back(info->name);
        else if (verdict.exact != sameText) bad.push_back(info->name + " (verdict exact=" + std::to_string(verdict.exact) + " but saved text equal=" + std::to_string(sameText) + "): " + verdict.note);
        if (!verdict.exact) inexactKinds.push_back(info->name + ": " + verdict.note);
        // (1) what the menu says.
        std::string why;
        const bool can = CanDuplicate(*g, *a, &why);
        if (!can) {
            if (Contains(why, "Only one per object")) { ++onceOnly; CHECK(IsOnePerObject(info->type) || ComponentRules::IsUnique(info->type)); }
            else { ++inexact; CHECK(!verdict.exact && Contains(why, "would not be the same")); }
            CHECK(DuplicateComponent(*g, *a) == nullptr);
            continue;
        }
        // (3) enabled: duplicate for real and compare the saved text of the original and the copy.
        ++enabled;
        enabledKinds.push_back(info->name);
        Component* b = DuplicateComponent(*g, *a);
        if (!b || b == a || b->enabled != a->enabled) { bad.push_back(info->name + " (no duplicate)"); continue; }
        const std::string both = Saved(*g);
        Scene r1("one"), r2("two");
        GameObject* o1 = SceneSerializer::InstantiateFromText(r1, both);
        GameObject* o2 = SceneSerializer::InstantiateFromText(r2, both);
        if (!o1 || !o2) { bad.push_back(info->name + " (the saved text does not load)"); continue; }
        Component* first1 = Nth(*o1, info, 0); Component* second1 = Nth(*o1, info, 1);
        Component* first2 = Nth(*o2, info, 0); Component* second2 = Nth(*o2, info, 1);
        if (!first1 || !second1 || !first2 || !second2) { notTwo.push_back(info->name); continue; }   // listed below: the file keeps one per object
        o1->RemoveComponent(second1);      // only the original is left on o1 ...
        o2->RemoveComponent(first2);       // ... and only the copy on o2
        if (Saved(*o1) != Saved(*o2)) bad.push_back(info->name + " (the copy saves different text from the original)");
    }
    for (const std::string& b : bad) std::cerr << "  problem: " << b << "\n";
    CHECK(bad.empty());
    std::cout << "  " << kinds << " registered kinds: " << onceOnly << " only one per object, " << inexact << " not copyable exactly, " << enabled
              << " offered (" << unsavedKinds << " kinds save nothing)\n";
    std::cout << "  offered:";
    for (const std::string& n : enabledKinds) std::cout << " " << n;
    std::cout << "\n";
    for (const std::string& n : inexactKinds) std::cout << "  not exact: " << n << "\n";
    for (const std::string& n : notTwo) std::cout << "  not reloaded as two: " << n << "\n";
    for (const std::string& n : tooBigKinds) std::cout << "  saved text too big to compare (Duplicate is refused for it): " << n << "\n";
    CHECK(kinds > 80);
    CHECK(onceOnly > 60);                                           // nearly everything is a kind the browser hides once present
    CHECK(enabled >= 1);
    CHECK(std::find(enabledKinds.begin(), enabledKinds.end(), "Volume") != enabledKinds.end());   // the kind the other tests duplicate
    CHECK(notTwo.empty());                                          // a kind that is offered must come back from the file as two
}

void TestPasteAsNew() {
    RUN_SUITE("paste a copied component as a new one");
    Scene s("p");
    GameObject *a = Make(s, "a"), *b = Make(s, "b");
    auto* la = a->AddComponent<Light>();
    la->intensity = 4.0f; la->range = 12.0f;
    ComponentClip clip = CopyComponentValues(*la);
    std::string why;
    CHECK(CanPasteAsNew(*b, clip, &why));
    Component* nb = PasteComponentAsNew(*b, clip);
    auto* lb = dynamic_cast<Light*>(nb);
    CHECK(lb && lb->intensity == 4.0f && lb->range == 12.0f);
    CHECK(CountType(b, "Light") == 1 && b->transform);
    // A Light is a kind the Add Component list offers once: a second Paste As New onto the same object is refused.
    CHECK(!CanPasteAsNew(*b, clip, &why) && Contains(why, "Only one per object"));
    CHECK(PasteComponentAsNew(*b, clip) == nullptr);
    CHECK(CountType(b, "Light") == 1);
    // A kind the browser offers repeatedly can be pasted again and again.
    GameObject* v = Make(s, "v");
    auto* vol = v->AddComponent<Volume>();
    vol->eventName = "sig";
    const ComponentClip vclip = CopyComponentValues(*vol);
    CHECK(CanPasteAsNew(*v, vclip, &why) && PasteComponentAsNew(*v, vclip) != nullptr);
    CHECK(CanPasteAsNew(*v, vclip, &why) && PasteComponentAsNew(*v, vclip) != nullptr);
    CHECK(CountType(v, "Volume") == 3);
    for (auto* x : v->GetComponents<Volume>()) CHECK(x->eventName == "sig");
    // Nothing copied: refused, with a reason.
    CHECK(!CanPasteAsNew(*b, ComponentClip{}, &why) && !why.empty());
    CHECK(PasteComponentAsNew(*b, ComponentClip{}) == nullptr);
    // A project rule: allowed until the object has one.
    GameObject* c = Make(s, "c");
    ComponentRules::SetUnique(typeid(Volume));
    CHECK(CanPasteAsNew(*c, vclip, &why));
    CHECK(PasteComponentAsNew(*c, vclip) != nullptr);
    CHECK(!CanPasteAsNew(*c, vclip, &why) && Contains(why, "Only one per object"));
    CHECK(PasteComponentAsNew(*c, vclip) == nullptr);
    CHECK(CountType(c, "Volume") == 1);
    ComponentRules::Clear();
    // A clip of a kind the registry has no name for.
    ComponentClip fake; fake.type = "NoSuchComponent"; fake.values.entries.emplace_back();
    CHECK(!CanPasteAsNew(*b, fake, &why));
}

// "Paste As New X (N objects)": N is the objects it really adds to (finding 5).
void TestPasteAsNewTargets() {
    RUN_SUITE("Paste As New counts the selected objects it can really add to");
    Scene s("t");
    GameObject *src = Make(s, "src"), *a = Make(s, "a"), *b = Make(s, "b"), *c = Make(s, "c");
    auto* l = src->AddComponent<Light>();
    l->intensity = 3.0f;
    const ComponentClip clip = CopyComponentValues(*l);
    b->AddComponent<Light>();                                       // b has one already, and a Light is "only one per object"
    std::string why;
    CHECK(PasteAsNewTargets({a, b, c}, clip, &why) == 2);           // a and c; the number the menu names, not the number that have a Light
    CHECK(why.find("Only one per object") != std::string::npos);
    CHECK(PasteAsNewTargets({a, c}, clip) == 2 && PasteAsNewTargets({b}, clip) == 0 && PasteAsNewTargets({}, clip) == 0);
    CHECK(PasteAsNewTargets({a, nullptr, c}, clip) == 2);           // a null is skipped
    // The action adds to exactly those.
    int made = 0;
    for (GameObject* g : {a, b, c}) if (PasteComponentAsNew(*g, clip)) ++made;
    CHECK(made == 2 && CountType(a, "Light") == 1 && CountType(b, "Light") == 1 && CountType(c, "Light") == 1);
    // A clip that is not exact reaches nobody, and the first reason comes back.
    ComponentClip inexact = clip; inexact.exact = false; inexact.inexactNote = "its gradient";
    GameObject* d = Make(s, "d");
    why.clear();
    CHECK(PasteAsNewTargets({d}, inexact, &why) == 0 && why.find("its gradient") != std::string::npos);
    // The menu state built from those numbers names the right count (the menu text itself is in test_inspector_context_model.cpp).
    inspectorctx::ComponentMenuState st;
    st.typeName = "Light"; st.hasValues = true; st.selectionCount = 1; st.clipType = "Light"; st.clipCanAdd = true;
    st.pasteNewObjects = PasteAsNewTargets({a, b, c, d}, clip);     // d has none: a, c and b already have one now -> only d
    CHECK(st.pasteNewObjects == 1);
    GameObject* e = Make(s, "e");
    st.pasteNewObjects = PasteAsNewTargets({d, e}, clip);
    CHECK(st.pasteNewObjects == 2);
    CHECK(inspectorctx::Find(inspectorctx::ComponentMenu(st), "comp.pasteNew")->label == "Paste As New Light (2 objects)");
}

void TestRequiredBy() {
    RUN_SUITE("a component another one needs");
    Scene s("r");
    GameObject* g = Make(s, "g");
    auto* rb = g->AddComponent<Rigidbody2D>();
    auto* bc = g->AddComponent<BoxCollider2D>();
    CHECK(NeededByName(*g, *rb).empty());                          // no rule yet: nothing is needed
    RequireComponent<BoxCollider2D, Rigidbody2D>();                // a Box Collider 2D needs a Rigidbody 2D
    CHECK(NeededByName(*g, *rb) == "Box Collider 2D");
    CHECK(NeededByName(*g, *bc).empty());                          // nothing needs the collider
    // A second provider means the first can go.
    auto* rb2 = g->AddComponent<Rigidbody2D>();
    CHECK(NeededByName(*g, *rb).empty() && NeededByName(*g, *rb2).empty());
    g->RemoveComponent(rb2);
    CHECK(NeededByName(*g, *rb) == "Box Collider 2D");
    // Remove the dependant and the block is gone.
    g->RemoveComponent(bc);
    CHECK(NeededByName(*g, *rb).empty());
    ComponentRules::Clear();
}

void TestObjectClip() {
    RUN_SUITE("copy and paste every value of an object");
    Scene s("o");
    GameObject* p1 = Make(s, "P1");
    GameObject* p2 = Make(s, "P2");
    GameObject* a = Make(s, "A", p1);
    GameObject* b = Make(s, "B", p2);
    a->tag = "Enemy"; a->transform->localPosition = {1, 2, 3}; a->transform->localScale = {2, 2, 2};
    auto* la = a->AddComponent<Light>(); la->intensity = 6.0f; la->range = 40.0f;
    a->AddComponent<Rigidbody2D>();
    b->transform->localPosition = {9, 9, 9};
    auto* lb = b->AddComponent<Light>();
    b->AddComponent<SpriteRenderer>();                             // a has none: untouched
    const ObjectClip clip = CopyObjectValues(*a);
    CHECK(clip.Valid() && clip.from == "A");
    // The clip holds no object-level fields and no parent.
    for (const auto& e : clip.values.entries) {
        CHECK(e.component != "GameObject");
        CHECK(!(e.component == "Transform" && e.property == "parent"));
    }
    CHECK(PasteObjectValues(clip, *b) > 0);
    CHECK(b->transform->localPosition.x == 1 && b->transform->localPosition.y == 2 && b->transform->localPosition.z == 3);
    CHECK(b->transform->localScale.x == 2);
    CHECK(lb->intensity == 6.0f && lb->range == 40.0f);
    CHECK(b->name == "B" && b->tag != "Enemy");                    // name and tag stay
    CHECK(b->transform->Parent() == p2->transform);                // and so does its place in the Hierarchy
    CHECK(CountType(b, "Rigidbody2D") == 0);                       // components it lacks are not added
    CHECK(PasteObjectValues(clip, *b) == 0);                       // already the same
    CHECK(PasteObjectValues(ObjectClip{}, *b) == 0);               // nothing copied
    // The copy is a picture: changing the source afterwards does not change the clip.
    la->intensity = 1.0f;
    GameObject* c = Make(s, "C");
    auto* lc = c->AddComponent<Light>();
    PasteObjectValues(clip, *c);
    CHECK(lc->intensity == 6.0f);
    // The scene text of the object matches what was copied.
    CHECK(SameValues(*lb, *lc));
}

void TestResetOneValue() {
    RUN_SUITE("reset one value to its default");
    Scene s("v");
    GameObject *a = Make(s, "a"), *b = Make(s, "b");
    auto* la = a->AddComponent<Light>();
    auto* lb = b->AddComponent<Light>();
    Light fresh;
    la->intensity = 9.0f; lb->intensity = 3.0f; la->range = 77.0f;
    auto info = PropertyRegistry::FindByName("Light");
    CHECK(info != nullptr);
    const PropertyDesc* intensity = info ? info->Find("intensity") : nullptr;
    CHECK(intensity != nullptr);
    if (!info || !intensity) return;
    const PropertyValue d = DefaultValueOf(*info, *intensity);
    CHECK(d.IsValid() && d.AsFloat() == fresh.intensity);
    CHECK(ResetField({a, b}, "Light", 0, *intensity) == 2);        // both objects
    CHECK(la->intensity == fresh.intensity && lb->intensity == fresh.intensity);
    CHECK(la->range == 77.0f);                                     // only that value
    CHECK(ResetField({a, b}, "Light", 0, *intensity) == 0);        // nothing left to reset
    // A read-only value is refused.
    for (const PropertyDesc& pd : info->properties) {
        if (!pd.IsReadOnly()) continue;
        CHECK(ResetField({a}, "Light", 0, pd) == 0);
        break;
    }
    // The second Light of an object is addressed by its position.
    auto* la2 = a->AddComponent<Light>();
    la2->intensity = 5.0f; la->intensity = 8.0f;
    CHECK(ResetField({a}, "Light", 1, *intensity) == 1);
    CHECK(la2->intensity == fresh.intensity && la->intensity == 8.0f);
    CHECK(ResetField({a}, "NoSuchType", 0, *intensity) == 0);
}

void TestFindPropertyForLabel() {
    RUN_SUITE("which property a labelled field shows");
    auto info = PropertyRegistry::FindByName("Light");
    CHECK(info != nullptr);
    if (!info) return;
    const PropertyDesc* d = FindPropertyForLabel(*info, "Intensity##light");
    CHECK(d && d->name == "intensity");
    CHECK(FindPropertyForLabel(*info, "intensity") == d);
    CHECK(FindPropertyForLabel(*info, "INTENSITY  ") == d);        // any case, trailing spaces
    CHECK(FindPropertyForLabel(*info, "##name") == nullptr);       // a hidden-label widget
    CHECK(FindPropertyForLabel(*info, "") == nullptr);
    CHECK(FindPropertyForLabel(*info, "Not A Property##x") == nullptr);
    auto tf = PropertyRegistry::FindByName("Transform");
    CHECK(tf && FindPropertyForLabel(*tf, "Position") && FindPropertyForLabel(*tf, "Rotation") && FindPropertyForLabel(*tf, "Scale"));
}

void TestTransformParts() {
    RUN_SUITE("copy, paste and reset the Transform");
    Scene s("t");
    GameObject *a = Make(s, "a"), *b = Make(s, "b"), *cam = Make(s, "cam");
    cam->AddComponent<Camera>();
    a->transform->localPosition = {1, 2, 3};
    a->transform->localRotation = Quat::Euler(Vec3{0, 90, 0});
    a->transform->localScale = {2, 3, 4};
    CHECK(TransformAtDefault(*b->transform) == kTransformAll);
    CHECK(TransformAtDefault(*a->transform) == 0u);
    const TransformClip clip = CopyTransform(*a->transform);
    CHECK(clip.valid && !TransformClip{}.valid);
    PasteTransform(clip, *b->transform, kTransformPosition);       // only the position
    CHECK(b->transform->localPosition.y == 2 && b->transform->localScale.x == 1);
    CHECK((TransformAtDefault(*b->transform) & kTransformPosition) == 0u && (TransformAtDefault(*b->transform) & kTransformScale) != 0u);
    PasteTransform(clip, *b->transform, kTransformRotation | kTransformScale);
    CHECK(b->transform->localScale.z == 4);
    CHECK(Quat::Dot(b->transform->localRotation, a->transform->localRotation) > 0.9999f);
    CHECK(TransformAtDefault(*b->transform) == 0u);
    PasteTransform(TransformClip{}, *b->transform, kTransformAll);  // an empty clip does nothing
    CHECK(b->transform->localScale.z == 4);
    ResetTransform(*b, kTransformRotation);
    CHECK(Quat::Dot(b->transform->localRotation, Quat::Identity) > 0.9999f && b->transform->localPosition.x == 1);
    ResetTransform(*b, kTransformAll);
    CHECK(TransformAtDefault(*b->transform) == kTransformAll);
    // A camera keeps its scale.
    cam->transform->localScale = {2, 2, 2}; cam->transform->localPosition = {5, 5, 5};
    ResetTransform(*cam, kTransformAll);
    CHECK(cam->transform->localPosition.x == 0 && cam->transform->localScale.x == 2);
}


// A camera cannot be scaled: no write that reaches several objects may scale it (finding 4).
void TestCameraScaleLock() {
    RUN_SUITE("a camera keeps its scale when several objects are edited");
    Scene s("c");
    GameObject *cam = Make(s, "cam"), *a = Make(s, "a"), *b = Make(s, "b");
    cam->AddComponent<Camera>();
    cam->transform->localScale = {2, 2, 2};
    a->transform->localScale = {3, 3, 3};
    b->transform->localScale = {4, 4, 4};
    CHECK(ScaleIsLocked(*cam) && !ScaleIsLocked(*a));
    auto tf = PropertyRegistry::FindByName("Transform");
    const PropertyDesc* scale = tf ? tf->Find("scale") : nullptr;
    const PropertyDesc* position = tf ? tf->Find("position") : nullptr;
    CHECK(scale && position);
    if (!scale || !position) return;
    int skipped = 0;
    std::vector<GameObject*> t = WriteTargets({cam, a, b}, "Transform", *scale, &skipped);
    CHECK(t.size() == 2 && skipped == 1 && t[0] == a && t[1] == b);
    CHECK(WriteTargets({cam}, "Transform", *scale, &skipped).empty() && skipped == 1);
    t = WriteTargets({cam, a}, "Transform", *position, &skipped);   // any other property reaches the camera
    CHECK(t.size() == 2 && skipped == 0);
    t = WriteTargets({cam, a}, "Light", *scale, &skipped);          // and a property called scale on another kind
    CHECK(t.size() == 2 && skipped == 0);
    CHECK(WriteTargets({nullptr, a}, "Transform", *scale).size() == 1);
    // The field menu writes to those targets only: Set to One and Reset to Default.
    const std::vector<GameObject*> targets = WriteTargets({cam, a, b}, "Transform", *scale);
    CHECK(WriteField(targets, "Transform", 0, *scale, PropertyValue::MakeVec3({1, 1, 1})) == 2);
    CHECK(cam->transform->localScale.x == 2 && a->transform->localScale.x == 1 && b->transform->localScale.x == 1);
    a->transform->localScale = {5, 5, 5};
    CHECK(ResetField(targets, "Transform", 0, *scale) == 1 && cam->transform->localScale.x == 2 && a->transform->localScale.x == 1);
    // Paste All Component Values: the camera keeps its own scale, takes the rest.
    GameObject* src = Make(s, "src");
    src->transform->localPosition = {7, 8, 9}; src->transform->localScale = {6, 6, 6};
    const ObjectClip clip = CopyObjectValues(*src);
    CHECK(PasteObjectValues(clip, *cam) > 0);
    CHECK(cam->transform->localPosition.x == 7 && cam->transform->localScale.x == 2);
    CHECK(PasteObjectValues(clip, *a) > 0 && a->transform->localScale.x == 6);
    // Reset Transform (the old, working path) agrees.
    ResetTransform(*cam, kTransformAll);
    CHECK(cam->transform->localPosition.x == 0 && cam->transform->localScale.x == 2);
}

// "Reset Rotation" is only greyed for no rotation at all, not for a turn of a fraction of a degree (finding 7).
void TestRotationDefault() {
    RUN_SUITE("Reset Rotation is only greyed when there is no rotation");
    Scene s("r");
    GameObject* g = Make(s, "g");
    CHECK((TransformAtDefault(*g->transform) & kTransformRotation) != 0);
    g->transform->localRotation = Quat::Identity;
    CHECK((TransformAtDefault(*g->transform) & kTransformRotation) != 0);
    // q and -q are one rotation.
    g->transform->localRotation = Quat{0.0f, 0.0f, 0.0f, -1.0f};
    CHECK((TransformAtDefault(*g->transform) & kTransformRotation) != 0);
    // 0.16 degrees (the old test called anything up to this "no rotation"), 0.05, 0.01 and 0.001 degrees all count as a rotation.
    for (const float deg : {0.2f, 0.16f, 0.05f, 0.01f, 0.001f}) {
        g->transform->localRotation = Quat::Euler(Vec3{0, deg, 0});
        CHECK((TransformAtDefault(*g->transform) & kTransformRotation) == 0);
        g->transform->localRotation = Quat::Euler(Vec3{deg, 0, 0});
        CHECK((TransformAtDefault(*g->transform) & kTransformRotation) == 0);
        g->transform->localRotation = Quat::Euler(Vec3{0, 0, -deg});
        CHECK((TransformAtDefault(*g->transform) & kTransformRotation) == 0);
    }
    // After Reset Rotation it is none again.
    ResetTransform(*g, kTransformRotation);
    CHECK((TransformAtDefault(*g->transform) & kTransformRotation) != 0);
    // Position and scale keep their exact tests.
    g->transform->localPosition = {0.0f, 1e-6f, 0.0f};
    const unsigned at = TransformAtDefault(*g->transform);
    CHECK((at & kTransformPosition) == 0 && (at & kTransformScale) != 0);
    g->transform->localScale = {1.0f, 1.0f, 1.001f};
    CHECK((TransformAtDefault(*g->transform) & kTransformScale) == 0);
}

// Paste Value: what the clipboard may put into a field (finding 6). The text-level rules (size, lines, paths) are in
// tests/test_inspector_clipboard_rules.cpp; this is the registry's side and the two together.
PasteCheck Paste(const PropertyDesc& d, const std::string& text, const FieldState& cur = FieldState{}, const std::string& project = "/home/me/Game") {
    return CheckPaste(d, inspectorctx::ReadClipboard(text.c_str()), cur, project);
}

void TestCheckPaste() {
    RUN_SUITE("Paste Value takes only a value the field accepts, with a reason and a preview");
    auto light = PropertyRegistry::FindByName("Light");
    auto tf = PropertyRegistry::FindByName("Transform");
    auto sprite = PropertyRegistry::FindByName("SpriteRenderer");
    CHECK(light && tf && sprite);
    if (!light || !tf || !sprite) return;
    const PropertyDesc* intensity = light->Find("intensity");
    const PropertyDesc* position = tf->Find("position");
    const PropertyDesc* rotation = tf->Find("rotation");
    const PropertyDesc* color = light->Find("color");
    const PropertyDesc* texture = sprite->Find("texture");
    auto cam = PropertyRegistry::FindByName("Camera");
    const PropertyDesc* projection = cam ? cam->Find("projection") : nullptr;
    auto go = PropertyRegistry::FindByName("GameObject");
    const PropertyDesc* name = go ? go->Find("name") : nullptr;
    CHECK(intensity && position && rotation && color && texture && projection && name);
    if (!intensity || !position || !rotation || !color || !texture || !projection || !name) return;

    // A number.
    PasteCheck p = Paste(*intensity, "2.5");
    CHECK(p.ok && p.value.AsFloat() == 2.5f && p.preview == "2.5" && !p.same && p.reason.empty());
    p = Paste(*intensity, " 2.5\r\n");
    CHECK(p.ok && p.value.AsFloat() == 2.5f);                       // a line end from an editor does no harm
    // nan and infinity read as numbers but the registry refuses them: Paste Value is greyed (not an empty undo step) and says why.
    for (const char* bad : {"nan", "NaN", "-nan", "inf", "-inf", "Infinity"}) {
        p = Paste(*intensity, bad);
        CHECK(!p.ok && Contains(p.reason, "nan or infinite"));
    }
    p = Paste(*position, "1 nan 3");
    CHECK(!p.ok && Contains(p.reason, "nan or infinite"));
    p = Paste(*rotation, "0 0 0 0");
    CHECK(!p.ok && !p.reason.empty());                               // not a rotation
    // Not this kind of value, several lines, an empty or blank clipboard.
    p = Paste(*intensity, "hello");
    CHECK(!p.ok && Contains(p.reason, "not a number"));
    p = Paste(*position, "1 2");
    CHECK(!p.ok && Contains(p.reason, "3 numbers"));
    p = Paste(*position, "1 2 3\n4 5 6");
    CHECK(!p.ok && Contains(p.reason, "several lines"));
    p = Paste(*intensity, "");
    CHECK(!p.ok && Contains(p.reason, "no text"));
    p = Paste(*intensity, "  \n ");
    CHECK(!p.ok && Contains(p.reason, "blank"));
    // A vector with its own formats; the preview is the value as the field would show it.
    p = Paste(*position, "(1.5, 2, -3)");
    CHECK(p.ok && p.value.AsVec3().x == 1.5f && p.value.AsVec3().z == -3.0f && p.preview == "1.5 2 -3");
    // A colour: hex and numbers.
    p = Paste(*color, "#FF8000");
    CHECK(p.ok && p.value.AsColor().r == 1.0f);
    p = Paste(*color, "0.1 0.2 0.3 1");
    CHECK(p.ok);
    // An enum: by name or number.
    p = Paste(*projection, "Perspective");
    CHECK(p.ok && p.value.AsEnum() == 1);
    p = Paste(*projection, "17");
    CHECK(!p.ok && !p.reason.empty());                               // not one of the choices
    // "Already holds it": pasting would change nothing, so the row is greyed instead of pushing an empty undo step.
    FieldState cur;
    cur.have = 2; cur.value = PropertyValue::MakeFloat(2.5f);
    CHECK(Paste(*intensity, "2.5", cur).same && Paste(*intensity, "2.5", cur).ok);
    cur.mixed = true;
    CHECK(!Paste(*intensity, "2.5", cur).same);                      // the selected objects differ: the paste does change something
    cur.mixed = false; cur.value = PropertyValue::MakeFloat(1.0f);
    CHECK(!Paste(*intensity, "2.5", cur).same);

    // Too long: refused before anything parses it (64 KiB cap) with a message that names the limit; a long text is not cut and pasted.
    const std::string big(inspectorctx::kMaxClipboardChars + 1, '1');
    p = Paste(*intensity, big);
    CHECK(!p.ok && Contains(p.reason, "64 KB"));
    p = Paste(*name, big);
    CHECK(!p.ok && Contains(p.reason, "64 KB"));
    // A text just under the cap: a one-line String field takes it, a vector refuses it.
    const std::string longName(inspectorctx::kMaxClipboardChars, 'n');
    p = Paste(*name, longName);
    CHECK(p.ok && p.value.AsString().size() == inspectorctx::kMaxClipboardChars && p.preview.size() == 48 + 3);
    p = Paste(*position, longName);
    CHECK(!p.ok);
    // A one-line String field refuses several lines (it used to look "fine" and then write nothing but still push an undo step).
    p = Paste(*name, "first\nsecond");
    CHECK(!p.ok && Contains(p.reason, "several lines"));
    p = Paste(*name, "Boss");
    CHECK(p.ok && p.value.AsString() == "Boss");

    // File paths stay inside the project.
    p = Paste(*texture, "Assets/hero.png");
    CHECK(p.ok && p.value.AsString() == "Assets/hero.png");
    p = Paste(*texture, "/home/me/Game/Assets/hero.png");           // the form the Project panel stores
    CHECK(p.ok);
    for (const char* bad : {"../secret.png", "Assets/../../x.png", "/etc/passwd", "/home/me/Game2/a.png", "C:\\Windows\\a.png", "\\\\server\\share\\a.png", "//server/a.png"}) {
        p = Paste(*texture, bad);
        CHECK(!p.ok && !p.reason.empty());
    }
    p = Paste(*texture, "../secret.png");
    CHECK(Contains(p.reason, ".."));
    p = Paste(*texture, "\\\\server\\share\\a.png");
    CHECK(Contains(p.reason, "network"));
    p = Paste(*texture, "/etc/passwd");
    CHECK(Contains(p.reason, "outside the project"));
    p = Paste(*texture, "/home/me/Game/Assets/hero.png", FieldState{}, "");   // no project open: nothing absolute can be inside it
    CHECK(!p.ok && Contains(p.reason, "Open a project"));
    p = Paste(*texture, "Assets/hero.png", FieldState{}, "");
    CHECK(p.ok);
}

// The engine's own number parsers must not copy or split a huge text either (a clipboard can hold a whole file).
void TestParseTextIsBounded() {
    RUN_SUITE("a huge text is refused without being copied or cut into pieces");
    auto tf = PropertyRegistry::FindByName("Transform");
    auto light = PropertyRegistry::FindByName("Light");
    auto spawner = PropertyRegistry::FindByName("Spawner");
    CHECK(tf && light && spawner);
    if (!tf || !light || !spawner) return;
    const PropertyDesc* position = tf->Find("position");
    const PropertyDesc* rotation = tf->Find("rotation");
    const PropertyDesc* intensity = light->Find("intensity");
    const PropertyDesc* color = light->Find("color");
    const PropertyDesc* whole = spawner->Find("maxAlive");            // a whole number
    const PropertyDesc* yesNo = spawner->Find("autoStart");           // a true / false value
    CHECK(position && rotation && intensity && color && whole && yesNo);
    if (!position || !rotation || !intensity || !color || !whole || !yesNo) return;
    CHECK(whole->type == PropertyType::Int && yesNo->type == PropertyType::Bool);
    std::string huge;
    for (int i = 0; i < 16 * 1024 * 1024; ++i) huge += "1 ";            // 32 MB of numbers
    PropertyValue v;
    for (const PropertyDesc* d : {position, rotation, intensity, color, whole, yesNo}) {
        g_allocated = 0; g_counting = true;
        const bool parsed = d->ParseText(huge, v);
        g_counting = false;
        CHECK(!parsed);
        CHECK(g_allocated < 4096);                                      // it never made a 32 MB copy or a list of 16 million pieces
    }
    // A text that fits the size limit but holds hundreds of numbers: refused after the first few, not after splitting all of them.
    std::string many;
    for (int i = 0; i < 480; ++i) many += "1 ";
    g_allocated = 0; g_counting = true;
    const bool parsedMany = position->ParseText(many, v);
    g_counting = false;
    CHECK(!parsedMany && g_allocated < 4096);
    std::string manyCommas;
    for (int i = 0; i < 480; ++i) manyCommas += "1,";
    g_allocated = 0; g_counting = true;
    const bool parsedCommas = position->ParseText(manyCommas, v);
    g_counting = false;
    CHECK(!parsedCommas && g_allocated < 4096);
    // Normal text still parses.
    CHECK(position->ParseText("1 2 3", v) && v.AsVec3().z == 3.0f);
    CHECK(position->ParseText("(1, 2, 3)", v) && position->ParseText("1,2,3", v) && position->ParseText("  1\t2   3  ", v));
    CHECK(!position->ParseText("1 2 3 4", v) && !position->ParseText("1 2", v) && !position->ParseText("1,2,3,", v));
    CHECK(rotation->ParseText("0 0 0 1", v) && color->ParseText("#ff00ff", v) && color->ParseText("1 1 1 1", v));
    CHECK(intensity->ParseText("2.5", v) && !intensity->ParseText(std::string(300, '1'), v));
}

// The scene serializer can write one object without the objects below it (what the copy check uses).
void TestSerializeObjectAlone() {
    RUN_SUITE("SerializeObject can leave the children out");
    Scene s("o");
    GameObject* parent = Make(s, "Parent");
    GameObject* kid = Make(s, "Kid", parent);
    kid->AddComponent<Light>();
    parent->AddComponent<Rigidbody2D>();
    const std::string withKids = SceneSerializer::SerializeObject(*parent);
    const std::string alone = SceneSerializer::SerializeObject(*parent, false);
    CHECK(withKids.find("Kid") != std::string::npos && alone.find("Kid") == std::string::npos);
    CHECK(alone.find("Parent") != std::string::npos && alone.find("rigidbody2d") != std::string::npos && alone.find("light") == std::string::npos);
    CHECK(SceneSerializer::SerializeObject(*parent, true) == withKids);   // the default is unchanged
    // It loads back as one object with its component.
    Scene t("t");
    GameObject* back = SceneSerializer::InstantiateFromText(t, alone);
    CHECK(back && back->name == "Parent" && back->GetComponent<Rigidbody2D>() && t.Objects().size() == 1);
    // A leaf gives the same text either way.
    CHECK(SceneSerializer::SerializeObject(*kid, false) == SceneSerializer::SerializeObject(*kid, true));
}

}   // namespace

int main() {
    TestNames();
    TestMoves();
    TestDuplicate();
    TestOnePerObject();
    TestCopyExactness();
    TestEveryRegisteredKind();
    TestPasteAsNew();
    TestPasteAsNewTargets();
    TestRequiredBy();
    TestObjectClip();
    TestResetOneValue();
    TestFindPropertyForLabel();
    TestTransformParts();
    TestCameraScaleLock();
    TestRotationDefault();
    TestCheckPaste();
    TestParseTextIsBounded();
    TestSerializeObjectAlone();
    TEST_MAIN_RESULT();
}
