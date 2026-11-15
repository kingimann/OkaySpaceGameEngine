// What the Inspector's right-click menus do to the scene (editor/InspectorModel, ImGui-free): move a component to the top or
// bottom, duplicate it, paste a copied component as a new one, copy and paste every value of an object, reset one value,
// the Transform copy / paste / reset parts, and the check for a component another one needs. The rules for which rows
// show are in tests/test_inspector_context_model.cpp; this file checks the operations behind the rows, and that what
// they copy comes back through the property registry and the scene text unchanged.
#include "test_framework.hpp"
#include "InspectorModel.hpp"
#include <Okay.hpp>
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/SceneDiff.hpp"
#include <iostream>
#include <set>
#include <string>
#include <vector>

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

void TestDuplicate() {
    RUN_SUITE("duplicate a component");
    Scene s("d");
    GameObject* g = Make(s, "g");
    auto* li = g->AddComponent<Light>();
    g->AddComponent<Rigidbody2D>();
    li->intensity = 7.5f; li->range = 31.0f; li->color = Color{0.1f, 0.9f, 0.4f, 1.0f};
    li->enabled = false;
    std::string why;
    CHECK(CanAddAnother(*li, &why));
    Component* dup = DuplicateComponent(*g, *li);
    CHECK(dup && dup != li);
    CHECK(CountType(g, "Light") == 2);
    CHECK(Order(g) == (std::vector<std::string>{"Light", "Light", "Rigidbody2D"}));   // right below the original
    auto* dl = dynamic_cast<Light*>(dup);
    CHECK(dl && dl->intensity == 7.5f && dl->range == 31.0f && dl->color.g == 0.9f);
    CHECK(dl && !dl->enabled);                                      // the on / off state comes along
    CHECK(li->intensity == 7.5f);                                   // the original is untouched
    // The two are separate: changing one leaves the other.
    dl->intensity = 1.0f;
    CHECK(li->intensity == 7.5f);
    // Both survive a save and load, in order, with their own values.
    Scene t("d2");
    CHECK(SceneSerializer::Deserialize(t, SceneSerializer::Serialize(s)));
    GameObject* tg = t.Find("g");
    CHECK(tg && CountType(tg, "Light") == 2);
    if (tg && CountType(tg, "Light") == 2) {
        const auto lights = tg->GetComponents<Light>();
        CHECK(lights[0]->intensity == 7.5f && lights[1]->intensity == 1.0f);
    }
    // Duplicating the duplicate adds a third, again right below the one it came from.
    Component* third = DuplicateComponent(*g, *dup);
    CHECK(third && CountType(g, "Light") == 3);
    CHECK(Order(g) == (std::vector<std::string>{"Light", "Light", "Light", "Rigidbody2D"}));

    // A kind that is only allowed once is refused, with the reason.
    ComponentRules::SetUnique(typeid(Rigidbody2D));
    Component* rb = g->GetComponent<Rigidbody2D>();
    CHECK(!CanAddAnother(*rb, &why) && why.find("only one") != std::string::npos);
    CHECK(DuplicateComponent(*g, *rb) == nullptr);
    CHECK(CountType(g, "Rigidbody2D") == 1);
    ComponentRules::Clear();
    // A component the registry does not know cannot be duplicated.
    GameObject* h = Make(s, "h");
    auto* sc = h->AddComponent<ScriptComponent>();
    CHECK(!HasRegisteredProperties(*sc));
    CHECK(!CanAddAnother(*sc, &why) && !why.empty());
    CHECK(DuplicateComponent(*h, *sc) == nullptr);
}

// Every kind of component the registry can add: duplicate it with changed values, and the copy must hold exactly the
// values of the original, in memory and after a save and load.
void TestDuplicateEveryRegisteredKind() {
    RUN_SUITE("duplicate every registered kind of component");
    int kinds = 0, mutatedKinds = 0, savedKinds = 0;
    std::vector<std::string> bad;
    for (const auto& info : PropertyRegistry::All()) {
        if (!info || !info->add || info->objectLevel || info->properties.empty()) continue;
        Scene s("sweep");
        GameObject* g = Make(s, "g");
        Component* a = info->add(*g);
        if (!a) continue;
        ++kinds;
        if (Mutate(*a) > 0) ++mutatedKinds;
        Component* b = DuplicateComponent(*g, *a);
        if (!b) { bad.push_back(info->name + " (no duplicate)"); continue; }
        if (b == a || b->enabled != a->enabled || !SameValues(*a, *b)) { bad.push_back(info->name + " (values differ)"); continue; }
        // And through the scene text: two records, and loading them back gives two components with the same values.
        const std::string text = SceneSerializer::Serialize(s);
        Scene t("sweep2");
        if (!SceneSerializer::Deserialize(t, text)) { bad.push_back(info->name + " (does not load)"); continue; }
        GameObject* tg = t.Find("g");
        if (!tg) { bad.push_back(info->name + " (object lost)"); continue; }
        std::vector<const Component*> same;
        for (const auto& up : tg->Components()) if (up && PropertyRegistry::FindFor(*up) == info) same.push_back(up.get());
        if (same.size() != 2) continue;   // a kind the scene file does not keep (run-time only): checked in memory above
        ++savedKinds;
        if (!SameValues(*same[0], *same[1])) bad.push_back(info->name + " (values differ after load)");
    }
    for (const std::string& b : bad) std::cerr << "  problem: " << b << "\n";
    CHECK(bad.empty());
    CHECK(kinds > 40);
    CHECK(mutatedKinds > 30);        // most kinds had real values to copy
    CHECK(savedKinds > 30);
    std::cout << "  " << kinds << " kinds duplicated, " << mutatedKinds << " with changed values, " << savedKinds << " also through the scene text\n";
}

void TestPasteAsNew() {
    RUN_SUITE("paste a copied component as a new one");
    Scene s("p");
    GameObject *a = Make(s, "a"), *b = Make(s, "b");
    auto* la = a->AddComponent<Light>();
    la->intensity = 4.0f; la->range = 12.0f;
    const ComponentClip clip = CopyComponentValues(*la);
    std::string why;
    CHECK(CanPasteAsNew(*b, clip, &why));
    Component* nb = PasteComponentAsNew(*b, clip);
    auto* lb = dynamic_cast<Light*>(nb);
    CHECK(lb && lb->intensity == 4.0f && lb->range == 12.0f);
    CHECK(CountType(b, "Light") == 1 && b->transform);
    // The same again adds a second one (an object may hold several).
    CHECK(PasteComponentAsNew(*b, clip) != nullptr);
    CHECK(CountType(b, "Light") == 2);
    // Nothing copied: refused, with a reason.
    CHECK(!CanPasteAsNew(*b, ComponentClip{}, &why) && !why.empty());
    CHECK(PasteComponentAsNew(*b, ComponentClip{}) == nullptr);
    // A kind that is only allowed once, when the object already has it.
    GameObject* c = Make(s, "c");
    ComponentRules::SetUnique(typeid(Light));
    CHECK(CanPasteAsNew(*c, clip, &why));                          // c has none yet
    CHECK(PasteComponentAsNew(*c, clip) != nullptr);
    CHECK(!CanPasteAsNew(*c, clip, &why) && why.find("only one") != std::string::npos);
    CHECK(PasteComponentAsNew(*c, clip) == nullptr);
    CHECK(CountType(c, "Light") == 1);
    ComponentRules::Clear();
    // A clip of a kind the registry has no name for.
    ComponentClip fake; fake.type = "NoSuchComponent"; fake.values.entries.emplace_back();
    CHECK(!CanPasteAsNew(*b, fake, &why));
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

}   // namespace

int main() {
    TestNames();
    TestMoves();
    TestDuplicate();
    TestDuplicateEveryRegisteredKind();
    TestPasteAsNew();
    TestRequiredBy();
    TestObjectClip();
    TestResetOneValue();
    TestFindPropertyForLabel();
    TestTransformParts();
    TEST_MAIN_RESULT();
}
