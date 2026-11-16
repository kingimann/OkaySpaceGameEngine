#include "InspectorModel.hpp"
#include "AddComponentRules.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <unordered_map>

using namespace okay;

namespace oked {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool Contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

std::vector<std::string> SplitWords(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

} // namespace

// ---- Hierarchy search -------------------------------------------------------------------

std::string HierKindWords(const GameObject& go) {
    std::string h;
    if (go.GetComponent<Camera>())          h += " camera";
    if (go.GetComponent<MeshRenderer>())    h += " mesh 3d";
    if (go.GetComponent<SpriteRenderer>())  h += " sprite 2d";
    if (go.GetComponent<Light>())           h += " light";
    if (go.GetComponent<TextRenderer>())    h += " text ui";
    if (go.GetComponent<ParticleSystem>())  h += " particles fx";
    if (go.GetComponent<AudioSource>())     h += " sound audio";
    if (!go.GetComponents<ScriptComponent>().empty()) h += " script";
    if (go.GetComponent<Rigidbody3D>() || go.GetComponent<Rigidbody2D>()) h += " rigidbody physics";
    if (go.GetComponent<Collider3D>() || go.GetComponent<Collider2D>())   h += " collider";
    if (go.GetComponent<NPCController>())   h += " npc ai enemy";
    if (go.GetComponent<Spawner>())         h += " spawner waves";
    if (go.GetComponent<Terrain>())         h += " terrain ground";
    if (go.GetComponent<Character>())       h += " character humanoid";
    if (go.GetComponent<FirstPersonController>() || go.GetComponent<ThirdPersonController>() ||
        go.GetComponent<CharacterController3D>() || go.GetComponent<ClickToMoveController>())
        h += " player controller";
    if (go.GetComponent<UIButton>() || go.GetComponent<UIPanel>() ||
        go.GetComponent<UIImage>()  || go.GetComponent<UIRawImage>() || go.GetComponent<UISlider>() ||
        go.GetComponent<UIToggle>() || go.GetComponent<UIProgressBar>())
        h += " ui widget canvas";
    return h;
}

std::string HierComponentWords(const GameObject& go) {
    std::string h = HierKindWords(go);
    for (const auto& up : go.Components()) {
        if (!up) continue;
        if (auto info = PropertyRegistry::FindFor(*up)) {
            h += " " + info->name;
            if (!info->recordName.empty()) h += " " + info->recordName;
        }
    }
    return Lower(h);
}

HierFilter HierFilter::Parse(const std::string& text) {
    HierFilter f;
    std::size_t a = text.find_first_not_of(" \t");
    if (a == std::string::npos) return f;
    const std::string trimmed = text.substr(a, text.find_last_not_of(" \t") - a + 1);
    if (trimmed.find('/') != std::string::npos) {   // a path keeps its spaces
        f.terms.push_back({Kind::Path, trimmed, false});
        return f;
    }
    for (std::string w : SplitWords(trimmed)) {
        Term t;
        if (w.size() > 1 && w[0] == '-') { t.negate = true; w.erase(0, 1); }
        std::string lw = Lower(w);
        const std::size_t colon = lw.find(':');
        t.kind = Kind::Word;
        t.text = lw;
        if (colon != std::string::npos && colon > 0) {
            const std::string key = lw.substr(0, colon), val = lw.substr(colon + 1);
            Kind k = Kind::Word;
            bool known = true;
            if (key == "t" || key == "tag")                              k = Kind::Tag;
            else if (key == "c" || key == "comp" || key == "component")  k = Kind::Component;
            else if (key == "l" || key == "layer")                       k = Kind::Layer;
            else if (key == "is")                                        k = Kind::State;
            else if (key == "n" || key == "name")                        k = Kind::Word;
            else known = false;
            if (known) {
                if (val.empty()) continue;   // "t:" while typing: no restriction yet
                t.kind = k;
                t.text = val;
            }
        }
        f.terms.push_back(t);
    }
    return f;
}

std::string HierFilter::PathText() const {
    for (const Term& t : terms) if (t.kind == Kind::Path) return t.text;
    return {};
}

bool HierFilter::Matches(const GameObject& go, const std::unordered_set<const GameObject*>* pathHits) const {
    for (const Term& t : terms) {
        bool hit = false;
        switch (t.kind) {
            case Kind::Path: {
                const std::string hay = Lower(go.name) + HierKindWords(go);
                hit = (pathHits && pathHits->count(&go)) || Contains(hay, Lower(t.text));
                break;
            }
            case Kind::Word:
                hit = Contains(Lower(go.name), t.text) || Contains(HierKindWords(go), t.text) ||
                      (!go.tag.empty() && Contains(Lower(std::string(go.tag)), t.text));
                break;
            case Kind::Tag:
                hit = Contains(go.tag.empty() ? std::string("untagged") : Lower(std::string(go.tag)), t.text);
                break;
            case Kind::Component:
                hit = Contains(HierComponentWords(go), t.text);
                break;
            case Kind::Layer:
                hit = Contains(Lower(Layers::Name(go.layer)), t.text);
                break;
            case Kind::State: {
                const std::string& s = t.text;
                if (s == "off" || s == "inactive" || s == "disabled") hit = !go.active;
                else if (s == "on" || s == "active")                  hit = go.active;
                else if (s == "locked")                               hit = go.editorLocked;
                else if (s == "static")                               hit = go.isStatic;
                else if (s == "prefab")                               hit = PrefabTools::IsPrefabInstance(go);
                else if (s == "ui")                                   hit = Contains(HierKindWords(go), "ui");
                else hit = false;
                break;
            }
        }
        if (hit == t.negate) return false;
    }
    return true;
}

// ---- Drag and drop ----------------------------------------------------------------------------

std::vector<GameObject*> HierarchyOrder(Scene& scene) {
    std::vector<GameObject*> out;
    std::function<void(GameObject*)> walk = [&](GameObject* g) {
        out.push_back(g);
        for (Transform* c : g->transform->Children())
            if (c && c->gameObject) walk(c->gameObject);
    };
    for (const auto& up : scene.Objects())
        if (up && up->transform && !up->transform->Parent()) walk(up.get());
    return out;
}

std::vector<GameObject*> PlanDrop(Scene& scene, const std::vector<GameObject*>& dragged,
                                  const GameObject* target, DropWhere where) {
    std::vector<GameObject*> moved;
    for (GameObject* g : dragged) {
        if (!g || !g->transform || g == target) continue;
        if (std::find(moved.begin(), moved.end(), g) != moved.end()) continue;
        // Never into itself or under its own child.
        bool cycle = false;
        if (target)
            for (Transform* t = target->transform; t; t = t->Parent())
                if (t == g->transform) { cycle = true; break; }
        if (cycle) continue;
        // A dragged parent already carries this one.
        bool covered = false;
        for (Transform* t = g->transform->Parent(); t && !covered; t = t->Parent())
            if (std::find(dragged.begin(), dragged.end(), t->gameObject) != dragged.end()) covered = true;
        if (covered) continue;
        moved.push_back(g);
    }
    if (moved.size() > 1) {
        const std::vector<GameObject*> order = HierarchyOrder(scene);
        auto rank = [&](GameObject* g) {
            return (int)(std::find(order.begin(), order.end(), g) - order.begin());
        };
        std::stable_sort(moved.begin(), moved.end(), [&](GameObject* a, GameObject* b) { return rank(a) < rank(b); });
    }
    (void)where;
    return moved;
}

int ApplyDrop(Scene& scene, const std::vector<GameObject*>& dragged, GameObject* target,
              DropWhere where, const DropHooks& hooks) {
    if (!target && where != DropWhere::Inside) return 0;
    const std::vector<GameObject*> moved = PlanDrop(scene, dragged, target, where);
    int n = 0;
    GameObject* afterAnchor = target;
    for (GameObject* mv : moved) {
        if (hooks.before) hooks.before(mv);
        if (where == DropWhere::Before) {
            scene.ReorderSibling(mv, target, /*after=*/false);   // each lands right before the target: group order kept
        } else if (where == DropWhere::After) {
            scene.ReorderSibling(mv, afterAnchor, /*after=*/true);
            afterAnchor = mv;                                    // chain, or the group would come out reversed
        } else {
            Transform* np = target ? target->transform : nullptr;
            mv->transform->SetParent(np, /*worldPositionStays=*/true, std::numeric_limits<int>::max());
        }
        if (hooks.after) hooks.after(mv);
        ++n;
    }
    return n;
}

// ---- Several objects at once ------------------------------------------------------------------------

std::string ComponentPath(const std::string& typeName, int index) {
    return index > 0 ? typeName + "#" + std::to_string(index) : typeName;
}

int ComponentIndexOf(const GameObject& go, const Component& c) {
    auto info = PropertyRegistry::FindFor(c);
    if (!info) return 0;
    int n = 0;
    for (const auto& up : go.Components()) {
        if (up.get() == &c) return n;
        if (up && PropertyRegistry::FindFor(*up) == info) ++n;
    }
    return 0;
}

static bool AxisEqual(const PropertyValue& a, const PropertyValue& b, int i) {
    auto get = [i](const PropertyValue& v) -> float {
        switch (v.Type()) {
            case PropertyType::Vec2:  { Vec2 x = v.AsVec2(); return i == 0 ? x.x : x.y; }
            case PropertyType::Vec3:  { Vec3 x = v.AsVec3(); return i == 0 ? x.x : i == 1 ? x.y : x.z; }
            case PropertyType::Vec4:  { Vec4 x = v.AsVec4(); return i == 0 ? x.x : i == 1 ? x.y : i == 2 ? x.z : x.w; }
            case PropertyType::Quat:  { Quat x = v.AsQuat(); return i == 0 ? x.x : i == 1 ? x.y : i == 2 ? x.z : x.w; }
            case PropertyType::Color: { Color x = v.AsColor(); return i == 0 ? x.r : i == 1 ? x.g : i == 2 ? x.b : x.a; }
            default: return 0.0f;
        }
    };
    return get(a) == get(b);
}

FieldState ReadField(const std::vector<GameObject*>& objs, const std::string& typeName, int index,
                     const PropertyDesc& desc) {
    FieldState st;
    const int comps = PropertyTypeComponentCount(desc.type);
    st.axisMixed.assign((std::size_t)comps, false);
    const std::string path = ComponentPath(typeName, index) + "." + desc.name;
    for (GameObject* g : objs) {
        if (!g) continue;
        PropertyValue v;
        if (!PropertyRegistry::TryGetProperty(*g, path, v)) continue;
        if (st.have == 0) st.value = v;
        else {
            if (v != st.value) st.mixed = true;
            for (int i = 0; i < comps; ++i)
                if (!AxisEqual(v, st.value, i)) st.axisMixed[(std::size_t)i] = true;
        }
        ++st.have;
    }
    return st;
}

int WriteField(const std::vector<GameObject*>& objs, const std::string& typeName, int index,
               const PropertyDesc& desc, const PropertyValue& value) {
    const std::string path = ComponentPath(typeName, index) + "." + desc.name;
    int n = 0;
    for (GameObject* g : objs) {
        if (!g) continue;
        if (PropertyRegistry::WriteProperty(*g, path, value).changed) ++n;
    }
    return n;
}

static PropertyValue WithAxes(const PropertyValue& base, const PropertyValue& edit, unsigned mask) {
    auto pick = [&](int i, float b, float e) { return (mask >> i) & 1u ? e : b; };
    switch (base.Type()) {
        case PropertyType::Vec2: { Vec2 b = base.AsVec2(), e = edit.AsVec2();
            return PropertyValue::MakeVec2({pick(0, b.x, e.x), pick(1, b.y, e.y)}); }
        case PropertyType::Vec3: { Vec3 b = base.AsVec3(), e = edit.AsVec3();
            return PropertyValue::MakeVec3({pick(0, b.x, e.x), pick(1, b.y, e.y), pick(2, b.z, e.z)}); }
        case PropertyType::Vec4: { Vec4 b = base.AsVec4(), e = edit.AsVec4();
            return PropertyValue::MakeVec4({pick(0, b.x, e.x), pick(1, b.y, e.y), pick(2, b.z, e.z), pick(3, b.w, e.w)}); }
        case PropertyType::Quat: { Quat b = base.AsQuat(), e = edit.AsQuat();
            return PropertyValue::MakeQuat({pick(0, b.x, e.x), pick(1, b.y, e.y), pick(2, b.z, e.z), pick(3, b.w, e.w)}); }
        case PropertyType::Color: { Color b = base.AsColor(), e = edit.AsColor();
            return PropertyValue::MakeColor({pick(0, b.r, e.r), pick(1, b.g, e.g), pick(2, b.b, e.b), pick(3, b.a, e.a)}); }
        default: return edit;
    }
}

int WriteFieldAxes(const std::vector<GameObject*>& objs, const std::string& typeName, int index,
                   const PropertyDesc& desc, const PropertyValue& value, unsigned axisMask) {
    const std::string path = ComponentPath(typeName, index) + "." + desc.name;
    int n = 0;
    for (GameObject* g : objs) {
        if (!g) continue;
        PropertyValue cur;
        if (!PropertyRegistry::TryGetProperty(*g, path, cur)) continue;
        if (PropertyRegistry::WriteProperty(*g, path, WithAxes(cur, value, axisMask)).changed) ++n;
    }
    return n;
}

ComponentEdit BeginComponentEdit(const std::vector<GameObject*>& selection, const GameObject* primary, const Component& comp) {
    ComponentEdit e;
    if (selection.size() < 2 || !primary || std::find(selection.begin(), selection.end(), primary) == selection.end()) return e;
    if (!PropertyRegistry::FindFor(comp)) return e;
    e.before = SnapshotComponent(comp);
    e.active = !e.before.Empty();
    return e;
}

int PropagateComponentEdit(const std::vector<GameObject*>& selection, GameObject* primary, Component& comp,
                           const ComponentEdit& edit) {
    if (!edit.active || !primary) return 0;
    auto info = PropertyRegistry::FindFor(comp);
    if (!info) return 0;
    const std::vector<PropertyDiff> diff = DiffProperties(edit.before, SnapshotComponent(comp));
    if (diff.empty()) return 0;
    const int idx = ComponentIndexOf(*primary, comp);
    int written = 0;
    for (GameObject* g : selection) {
        if (!g || g == primary) continue;
        Component* other = nullptr;
        int n = 0;
        for (const auto& up : g->Components())
            if (up && PropertyRegistry::FindFor(*up) == info) { if (n++ == idx) { other = up.get(); break; } }
        if (!other) continue;
        for (const PropertyDiff& d : diff)
            if (d.kind == PropertyDiffKind::Changed && PropertyRegistry::SetComponentProperty(*other, d.property, d.after)) ++written;
    }
    return written;
}

// ---- Copy, paste, reset ----------------------------------------------------------------------------------

bool HasRegisteredProperties(const Component& c) {
    auto info = PropertyRegistry::FindFor(c);
    return info && !info->properties.empty();
}

ComponentClip CopyComponentValues(const Component& c) {
    ComponentClip clip;
    auto info = PropertyRegistry::FindFor(c);
    if (!info) return clip;
    clip.type = info->name;
    clip.values = SnapshotComponent(c);
    return clip;
}

int PasteComponentValues(const ComponentClip& clip, Component& target) {
    auto info = PropertyRegistry::FindFor(target);
    if (!clip.Valid() || !info || info->name != clip.type) return 0;
    int n = 0;
    for (const auto& e : clip.values.entries) {
        PropertyValue before = PropertyRegistry::GetComponentProperty(target, e.property);
        if (before == e.value) continue;
        if (PropertyRegistry::SetComponentProperty(target, e.property, e.value)) ++n;
    }
    return n;
}

int ResetComponentValues(Component& c) {
    auto info = PropertyRegistry::FindFor(c);
    if (!info || !info->add) return 0;
    // A fresh component of the same type, on a scratch object, is the reference for "default".
    Scene scratch;
    GameObject* g = scratch.CreateGameObject("scratch");
    Component* fresh = info->add(*g);
    if (!fresh) return 0;
    return PasteComponentValues(CopyComponentValues(*fresh), c);
}

// ---- Right-click menu actions ---------------------------------------------------------------------------------------------
// Written from the Inspector menu rules in InspectorContextModel.hpp; every value goes through the property registry
// (the same path Copy Values, the Inspector's cards, Undo and the scene text use), so nothing here has its own idea of a value.

ComponentSlot SlotOf(const GameObject& go, const Component& c) {
    ComponentSlot s;
    int n = 0;
    for (const auto& up : go.Components()) {
        if (!up || up.get() == static_cast<const Component*>(go.transform)) continue;
        if (up.get() == &c) s.index = n;
        ++n;
    }
    s.count = n;
    return s;
}

bool MoveComponentTo(GameObject& go, Component& c, inspectorctx::MoveKind kind) {
    const ComponentSlot slot = SlotOf(go, c);
    const int delta = inspectorctx::MoveDelta(kind, slot.index, slot.count);
    if (delta == 0) return false;
    go.MoveComponent(&c, delta);
    return SlotOf(go, c).index != slot.index;
}

std::string TypeDisplayName(const std::string& n) {
    // "BoxCollider3D" -> "Box Collider 3D", "UIButton" -> "UI Button", "AimIK" -> "Aim IK": the way the component headers are written.
    std::string out;
    for (std::size_t i = 0; i < n.size(); ++i) {
        const unsigned char c = (unsigned char)n[i], prev = i > 0 ? (unsigned char)n[i - 1] : 0, next = i + 1 < n.size() ? (unsigned char)n[i + 1] : 0;
        bool space = false;
        if (i > 0 && std::isupper(c) && std::islower(prev)) space = true;                              // Box|Collider
        else if (i > 0 && std::isupper(c) && std::isupper(prev) && std::islower(next)) space = true;   // UI|Button
        else if (i > 0 && std::isdigit(c) && std::isalpha(prev)) space = true;                         // Collider| 3D
        if (space) out += ' ';
        out += (char)c;
    }
    return out;
}

std::string ComponentDisplayName(const Component& c) {
    auto info = PropertyRegistry::FindFor(c);
    return TypeDisplayName(info ? info->name : ComponentSerializerRegistry::TypeKey(c));
}

std::string NeededByName(const GameObject& go, const Component& c) {
    const Component* by = go.RequiredBy(&c);
    return by ? ComponentDisplayName(*by) : std::string();
}

namespace {

std::string OnlyOneReason(const std::string& typeName) {
    return "Only one per object: a second " + TypeDisplayName(typeName) + " cannot be added (the Add Component list does not offer one either).";
}

// Is the kind "only one per object": the Add Component browser's table, or a project rule (ComponentRules::SetUnique).
bool OnePerObject(const ComponentTypeInfo& info) {
    return IsOnePerObject(info.type) || ComponentRules::IsUnique(info.type);
}

int IndexInComponents(const GameObject& go, const Component& c) {
    int i = 0;
    for (const auto& up : go.Components()) { if (up.get() == &c) return i; ++i; }
    return -1;
}

// A new component of `info`'s kind on `go` holding the clip's registered values and the given on / off state (added last; the siblings a
// RequireComponent rule adds come with it). Null when the registry cannot make one. No rule is asked here.
Component* AddCopy(GameObject& go, const ComponentTypeInfo& info, const ComponentClip& clip, bool enabled) {
    if (!info.add) return nullptr;
    Component* fresh = info.add(go);
    if (!fresh) return nullptr;
    PasteComponentValues(clip, *fresh);
    fresh->enabled = enabled;
    return fresh;
}

// A new component of `source`'s kind on `go`, holding the registered values of `source` and its on / off state, right below it.
// No rule is asked here (CanAddAnother / CanPasteAsNew do that); CheckCopyIsExact uses it on scratch objects to see what a copy keeps.
Component* CloneByValues(GameObject& go, Component& source) {
    auto info = PropertyRegistry::FindFor(source);
    if (!info || !info->add) return nullptr;
    const ComponentClip clip = CopyComponentValues(source);   // taken first: adding can add required siblings
    Component* fresh = AddCopy(go, *info, clip, source.enabled);
    if (!fresh || fresh == &source) return nullptr;
    // Right below the source (adding appended it, perhaps after the siblings a RequireComponent rule added).
    const int want = IndexInComponents(go, source) + 1, have = IndexInComponents(go, *fresh);
    if (want > 0 && have > want) go.MoveComponent(fresh, want - have);
    return fresh;
}

// The lines of a scene text that hold a component's settings: not the ones that only say which component is switched off or
// where it stands in the list (those name positions, which differ between an original and its copy by design).
std::vector<std::string> SettingLines(const std::string& text) {
    std::vector<std::string> out;
    for (std::size_t at = 0; at < text.size();) {
        std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(at, nl - at);
        at = nl + 1;
        std::size_t b = 0;
        while (b < line.size() && line[b] == ' ') ++b;
        std::size_t e = b;
        while (e < line.size() && line[e] != ' ') ++e;
        const std::string word = line.substr(b, e - b);
        if (word.empty() || word == "compoff" || word == "comporder") continue;
        out.push_back(std::move(line));
    }
    return out;
}

// The lines of `a` that `b` does not have, counting repeats (a multiset difference).
std::vector<std::string> LinesNotIn(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    std::unordered_map<std::string, int> have;
    for (const std::string& l : b) ++have[l];
    std::vector<std::string> out;
    for (const std::string& l : a) {
        auto it = have.find(l);
        if (it != have.end() && it->second > 0) --it->second;
        else out.push_back(l);
    }
    return out;
}

std::string FirstWord(const std::string& line) {
    std::size_t b = 0;
    while (b < line.size() && line[b] == ' ') ++b;
    std::size_t e = b;
    while (e < line.size() && line[e] != ' ') ++e;
    return line.substr(b, e - b);
}

}   // namespace

bool CanAddAnother(const Component& c, std::string* why) {
    auto info = PropertyRegistry::FindFor(c);
    if (!info || !info->add) { if (why) *why = "The editor cannot make another of this kind of component."; return false; }
    if (OnePerObject(*info)) { if (why) *why = OnlyOneReason(info->name); return false; }
    return true;
}

CopyCheck CheckCopyIsExact(const GameObject& go, const Component& c) {
    CopyCheck r;
    auto info = PropertyRegistry::FindFor(c);
    if (!info || !info->add) { r.exact = false; r.note = "the editor cannot make another of this kind"; return r; }
    // The object alone (its children are not part of the question), as the scene file would write it.
    const std::string text = SceneSerializer::SerializeObject(go, false);
    constexpr std::size_t kMaxText = std::size_t(1) << 20;   // a menu must open at once: a bigger object is not checked
    if (text.size() > kMaxText) { r.exact = false; r.note = "its saved text is too big to compare here"; return r; }
    const int idx = ComponentIndexOf(go, c);
    const ComponentClip clip = CopyComponentValues(c);   // the values Duplicate and Paste As New would use
    // Scratch copies of the object, loaded from that text (so they hold what the file keeps, nothing more).
    struct Loaded { GameObject* object = nullptr; Component* comp = nullptr; };
    const auto load = [&](Scene& scene) {
        Loaded l;
        std::vector<std::string> quiet;   // kept here so a warning is not printed to the console for a scratch copy
        l.object = SceneSerializer::InstantiateFromText(scene, text, nullptr, &quiet);
        if (!l.object) return l;
        int n = 0;
        for (const auto& up : l.object->Components())
            if (up && PropertyRegistry::FindFor(*up) == info) { if (n++ == idx) { l.comp = up.get(); break; } }
        return l;
    };
    const auto settings = [](const GameObject& o) { return SettingLines(SceneSerializer::SerializeObject(o, false)); };
    const auto countOf = [&](const GameObject& o) {
        int n = 0;
        for (const auto& up : o.Components()) if (up && PropertyRegistry::FindFor(*up) == info) ++n;
        return n;
    };
    Scene sceneA("copy check a"), sceneB("copy check b");
    const Loaded a = load(sceneA);
    if (!a.object) { r.exact = false; r.note = "its saved text does not load back"; return r; }
    if (!a.comp) { r.saved = false; return r; }   // the scene file does not keep this kind at all: nothing is saved, so nothing can be lost
    const Loaded b = load(sceneB);
    if (!b.comp) { r.exact = false; r.note = "its saved text does not load back as the same object"; return r; }

    // (1) Is a copy the same? On object B take the original out and put a copy in its place. What the original added to the object's
    // saved text and what the copy adds must be the same records (both measured against the object without the component, so a
    // record that exists once per object, like a UI object's layout, is on both sides).
    const std::vector<std::string> withOriginal = settings(*b.object);
    const bool wasOn = b.comp->enabled;
    b.object->RemoveComponent(b.comp);
    const std::vector<std::string> without = settings(*b.object);
    if (!AddCopy(*b.object, *info, clip, wasOn)) { r.exact = false; r.note = "the editor could not add another"; return r; }
    const std::vector<std::string> own = LinesNotIn(withOriginal, without);              // what the original saves
    const std::vector<std::string> mine = LinesNotIn(settings(*b.object), without);      // what the copy saves
    if (own.empty() && mine.empty()) { r.saved = false; return r; }                      // the kind saves nothing: there is nothing to lose
    std::vector<std::string> ownSorted = own, mineSorted = mine;
    std::sort(ownSorted.begin(), ownSorted.end());
    std::sort(mineSorted.begin(), mineSorted.end());
    if (ownSorted != mineSorted) {
        // Name the first saved record that the copy lacks (or has differently).
        const std::vector<std::string> lacking = LinesNotIn(own, mine);
        r.exact = false;
        if (lacking.empty()) r.note = "the copy would save settings the original does not";
        else r.note = "its saved \"" + FirstWord(lacking.front()) + "\" settings are not all part of its copyable values (the property list does not hold them)";
        return r;
    }

    // (2) Can the file hold a second one next to the original? On object A add a copy beside it: it has to be saved, and to load back.
    const int before = countOf(*a.object);
    const std::vector<std::string> alone = settings(*a.object);
    if (!CloneByValues(*a.object, *a.comp)) { r.twoOnOne = false; r.twoNote = "the editor could not add another"; return r; }
    const std::string both = SceneSerializer::SerializeObject(*a.object, false);
    if (LinesNotIn(SettingLines(both), alone).empty()) {
        r.twoOnOne = false;
        r.twoNote = "the scene file keeps only one " + TypeDisplayName(info->name) + " per object, so a second one would not be saved";
        return r;
    }
    Scene sceneC("copy check c");
    std::vector<std::string> quiet;
    GameObject* back = SceneSerializer::InstantiateFromText(sceneC, both, nullptr, &quiet);
    if (!back || countOf(*back) < before + 1) {
        r.twoOnOne = false;
        r.twoNote = "a second " + TypeDisplayName(info->name) + " would be saved but would not load back as a second one";
    }
    return r;
}

bool CanDuplicate(const GameObject& go, const Component& source, std::string* why) {
    if (!CanAddAnother(source, why)) return false;
    const CopyCheck check = CheckCopyIsExact(go, source);
    if (!check.exact) { if (why) *why = "A copy would not be the same as the original: " + check.note + "."; return false; }
    if (!check.twoOnOne) { if (why) *why = "A copy would not be the same as the original: " + check.twoNote + "."; return false; }
    return true;
}

Component* DuplicateComponent(GameObject& go, Component& source) {
    if (!CanDuplicate(go, source)) return nullptr;
    return CloneByValues(go, source);
}

bool CanPasteAsNew(const GameObject& go, const ComponentClip& clip, std::string* why) {
    if (!clip.Valid()) { if (why) *why = "Copy a component's values first."; return false; }
    auto info = PropertyRegistry::FindByName(clip.type);
    if (!info || !info->add) { if (why) *why = "The editor cannot add a " + TypeDisplayName(clip.type) + " by itself."; return false; }
    if (!clip.exact) {
        if (why) *why = "A new " + TypeDisplayName(info->name) + " made from the copied values would not be the same as the original: " +
                        (clip.inexactNote.empty() ? std::string("part of what it saves is not one of its copyable values") : clip.inexactNote) + ".";
        return false;
    }
    if (OnePerObject(*info)) {
        bool has = HasOnePerObjectKind(go, info->type);
        for (const auto& up : go.Components()) if (up && PropertyRegistry::FindFor(*up) == info) has = true;
        if (has) { if (why) *why = "Only one per object: this object already has a " + TypeDisplayName(info->name) + "."; return false; }
    }
    return true;
}

Component* PasteComponentAsNew(GameObject& go, const ComponentClip& clip) {
    if (!CanPasteAsNew(go, clip)) return nullptr;
    auto info = PropertyRegistry::FindByName(clip.type);
    Component* fresh = info->add(go);
    if (!fresh) return nullptr;
    PasteComponentValues(clip, *fresh);
    return fresh;
}

int PasteAsNewTargets(const std::vector<GameObject*>& objs, const ComponentClip& clip, std::string* firstWhy) {
    int n = 0;
    for (GameObject* g : objs) {
        if (!g) continue;
        std::string why;
        if (CanPasteAsNew(*g, clip, &why)) ++n;
        else if (firstWhy && firstWhy->empty()) *firstWhy = why;
    }
    return n;
}

ObjectClip CopyObjectValues(const GameObject& go) {
    ObjectClip clip;
    clip.from = go.name;
    const PropertySnapshot all = SnapshotProperties(go);
    for (const PropertySnapshot::Entry& e : all.entries) {
        auto info = PropertyRegistry::FindByName(e.component);
        if (!info || info->objectLevel) continue;               // the name, tag, layer ... of the object stay where they are
        const PropertyDesc* d = info->Find(e.property);
        if (!d || (d->flags & PropertyFlag::Structural)) continue;   // Transform.parent: a paste must not move the object in the Hierarchy
        clip.values.entries.push_back(e);
    }
    return clip;
}

int PasteObjectValues(const ObjectClip& clip, GameObject& target) {
    int changed = 0;
    const bool lockScale = ScaleIsLocked(target);   // a camera cannot be scaled: the pasted scale skips it, as every other write to a scale does
    for (const PropertySnapshot::Entry& e : clip.values.entries) {   // by kind and position among that kind ("Light#1.intensity"); a value that already matches counts nothing
        if (lockScale && e.component == "Transform" && e.property == "scale") continue;
        if (PropertyRegistry::WriteProperty(target, e.Path(), e.value).changed) ++changed;
    }
    return changed;
}

bool ScaleIsLocked(const GameObject& go) { return go.GetComponent<Camera>() != nullptr; }

std::vector<GameObject*> WriteTargets(const std::vector<GameObject*>& objs, const std::string& typeName, const PropertyDesc& desc, int* skipped) {
    const bool scale = typeName == "Transform" && desc.name == "scale";
    std::vector<GameObject*> out;
    int left = 0;
    for (GameObject* g : objs) {
        if (!g) continue;
        if (scale && ScaleIsLocked(*g)) { ++left; continue; }
        out.push_back(g);
    }
    if (skipped) *skipped = left;
    return out;
}

PropertyValue DefaultValueOf(const ComponentTypeInfo& info, const PropertyDesc& desc) {
    if (desc.defaultValue.IsValid()) return desc.defaultValue;
    if (!info.add) return {};
    // A type the registry could not make a sample of: read it from a fresh component on a scratch object. Kept per type (UI thread only).
    static std::unordered_map<std::string, PropertySnapshot> s_fresh;
    auto it = s_fresh.find(info.name);
    if (it == s_fresh.end()) {
        PropertySnapshot snap;
        Scene scratch;
        GameObject* g = scratch.CreateGameObject("scratch");
        if (Component* fresh = info.add(*g)) snap = SnapshotComponent(*fresh);
        it = s_fresh.emplace(info.name, std::move(snap)).first;
    }
    for (const PropertySnapshot::Entry& e : it->second.entries)
        if (e.property == desc.name) return e.value;
    return {};
}

int ResetField(const std::vector<GameObject*>& objs, const std::string& typeName, int index, const PropertyDesc& desc) {
    auto info = PropertyRegistry::FindByName(typeName);
    if (!info) return 0;
    const PropertyValue v = DefaultValueOf(*info, desc);
    return v.IsValid() ? WriteField(objs, typeName, index, desc, v) : 0;
}

namespace {

// What a field of this kind takes, as a phrase after "not" ("the clipboard text is not a number").
std::string KindPhrase(PropertyType t) {
    switch (t) {
        case PropertyType::Bool:      return "a true / false value";
        case PropertyType::Int:       return "a whole number";
        case PropertyType::Float:     return "a number";
        case PropertyType::Vec2:      return "2 numbers";
        case PropertyType::Vec3:      return "3 numbers";
        case PropertyType::Vec4:      return "4 numbers";
        case PropertyType::Quat:      return "4 numbers (a rotation)";
        case PropertyType::Color:     return "a colour (4 numbers, or #RRGGBB)";
        case PropertyType::Enum:      return "one of this field's choices";
        case PropertyType::AssetPath: return "a file path";
        case PropertyType::ObjectRef: return "an object id";
        default:                      return "a value of this kind";
    }
}

// Why the registry would not take a value that did read as the right kind.
std::string RefusalFor(const PropertyDesc& d, const PropertyValue& v) {
    auto finite = [](float f) { return std::isfinite(f); };
    switch (d.type) {
        case PropertyType::Float:
            if (!finite(v.AsFloat())) return "A number that is nan or infinite cannot be pasted.";
            break;
        case PropertyType::Vec2: { const Vec2 a = v.AsVec2(); if (!finite(a.x) || !finite(a.y)) return "A number that is nan or infinite cannot be pasted."; break; }
        case PropertyType::Vec3: { const Vec3 a = v.AsVec3(); if (!finite(a.x) || !finite(a.y) || !finite(a.z)) return "A number that is nan or infinite cannot be pasted."; break; }
        case PropertyType::Vec4: { const Vec4 a = v.AsVec4(); if (!finite(a.x) || !finite(a.y) || !finite(a.z) || !finite(a.w)) return "A number that is nan or infinite cannot be pasted."; break; }
        case PropertyType::Quat: { const Quat a = v.AsQuat(); if (!finite(a.x) || !finite(a.y) || !finite(a.z) || !finite(a.w)) return "A number that is nan or infinite cannot be pasted."; if (a.x == 0 && a.y == 0 && a.z == 0 && a.w == 0) return "A rotation of four zeros is not a rotation."; break; }
        case PropertyType::Color: { const Color a = v.AsColor(); if (!finite(a.r) || !finite(a.g) || !finite(a.b) || !finite(a.a)) return "A number that is nan or infinite cannot be pasted."; break; }
        case PropertyType::ObjectRef: return "An object id is made of the letters a to f, digits and dashes (up to 64 characters). The clipboard text is not one.";
        case PropertyType::String:
        case PropertyType::AssetPath: return "This field takes one line of text without control characters.";
        case PropertyType::Enum: return "That number is not one of this field's choices.";
        default: break;
    }
    if (d.hasRange && d.rejectOutOfRange) return "The value is outside the range this field takes.";
    return "This field does not accept that value.";
}

}   // namespace

PasteCheck CheckPaste(const PropertyDesc& desc, const inspectorctx::ClipboardText& clip, const FieldState& current, const std::string& projectDir) {
    PasteCheck r;
    const bool multiLine = desc.type == PropertyType::String && desc.multiline;
    const inspectorctx::GateResult gate = inspectorctx::GateClipboard(clip, multiLine);
    if (gate.gate != inspectorctx::PasteGate::Ok) { r.reason = gate.reason; return r; }
    PropertyValue parsed;
    if (!desc.ParseText(gate.text, parsed)) {
        r.reason = "The clipboard text is not " + KindPhrase(desc.type) + ". Use Copy Value on a field of the same kind.";
        return r;
    }
    if (desc.type == PropertyType::AssetPath) {
        const inspectorctx::PathVerdict path = inspectorctx::CheckAssetPath(parsed.AsString(), projectDir);
        if (!path.Ok()) { r.reason = path.reason; return r; }
    }
    PropertyValue norm;
    if (!desc.Normalize(parsed, norm)) { r.reason = RefusalFor(desc, parsed); return r; }
    r.ok = true;
    r.value = norm;
    r.preview = inspectorctx::PreviewText(desc.FormatText(norm));
    r.same = current.have > 0 && !current.mixed && current.value == norm;
    return r;
}

const PropertyDesc* FindPropertyForLabel(const ComponentTypeInfo& info, const std::string& label) {
    std::string l = label.substr(0, label.find("##"));
    while (!l.empty() && l.back() == ' ') l.pop_back();
    if (l.empty()) return nullptr;
    l = Lower(l);
    for (const PropertyDesc& d : info.properties)
        if (Lower(PrettyName(d.name)) == l || Lower(d.name) == l) return &d;
    return nullptr;
}

TransformClip CopyTransform(const Transform& t) {
    TransformClip c;
    c.valid = true;
    c.position = t.localPosition; c.rotation = t.localRotation; c.scale = t.localScale;
    return c;
}

void PasteTransform(const TransformClip& clip, Transform& t, unsigned parts) {
    if (!clip.valid) return;
    if (parts & kTransformPosition) t.localPosition = clip.position;
    if (parts & kTransformRotation) t.localRotation = clip.rotation;
    if (parts & kTransformScale) t.localScale = clip.scale;
}

void ResetTransform(GameObject& go, unsigned parts) {
    Transform* t = go.transform;
    if (!t) return;
    if (parts & kTransformPosition) t->localPosition = {0, 0, 0};
    if (parts & kTransformRotation) t->localRotation = Quat::Identity;
    if ((parts & kTransformScale) && !ScaleIsLocked(go)) t->localScale = {1, 1, 1};
}

unsigned TransformAtDefault(const Transform& t) {
    unsigned at = 0;
    if (t.localPosition.x == 0.0f && t.localPosition.y == 0.0f && t.localPosition.z == 0.0f) at |= kTransformPosition;
    // "No rotation": the turn part of the quaternion is zero (down to float noise, about a hundred-thousandth of a degree) and w is 1 or -1
    // (q and -q are the same rotation). The old test (|w| > 0.999999) counted every turn up to 0.16 degrees as none and greyed Reset Rotation.
    const Quat& q = t.localRotation;
    if (std::fabs(q.x) <= 1e-7f && std::fabs(q.y) <= 1e-7f && std::fabs(q.z) <= 1e-7f && std::fabs(std::fabs(q.w) - 1.0f) <= 1e-6f) at |= kTransformRotation;
    if (t.localScale.x == 1.0f && t.localScale.y == 1.0f && t.localScale.z == 1.0f) at |= kTransformScale;
    return at;
}

// ---- Field labels and asset kinds ---------------------------------------------------------------------------------

std::string PrettyName(const std::string& n) {
    std::string out;
    for (std::size_t i = 0; i < n.size(); ++i) {
        const char c = n[i];
        if (c == '_') { if (!out.empty() && out.back() != ' ') out += ' '; continue; }
        const bool upper = std::isupper((unsigned char)c) != 0;
        if (i > 0 && upper) {
            const bool prevLower = std::islower((unsigned char)n[i - 1]) || std::isdigit((unsigned char)n[i - 1]);
            const bool acronymEnd = std::isupper((unsigned char)n[i - 1]) && i + 1 < n.size() && std::islower((unsigned char)n[i + 1]);
            if ((prevLower || acronymEnd) && !out.empty() && out.back() != ' ') out += ' ';
        }
        out += (out.empty() || out.back() == ' ') ? (char)std::toupper((unsigned char)c) : c;
    }
    return out.empty() ? n : out;
}

std::vector<std::string> AssetKindExtensions(const std::string& kind) {
    const std::string k = Lower(kind);
    if (k == "image" || k == "texture") return {".png", ".jpg", ".jpeg", ".bmp", ".tga"};
    if (k == "audio" || k == "sound")   return {".wav", ".ogg", ".mp3", ".flac"};
    if (k == "model" || k == "mesh")    return {".obj", ".fbx", ".gltf", ".glb", ".dae", ".stl", ".ply", ".3ds"};
    if (k == "font")                    return {".ttf", ".otf"};
    if (k == "prefab")                  return {".okayprefab"};
    if (k == "script")                  return {".okay", ".okayvs"};
    if (k == "material")                return {".okaymat"};
    if (k == "scene")                   return {".okayscene"};
    return {};
}

bool AssetKindAccepts(const std::string& kind, const std::string& path) {
    const std::vector<std::string> exts = AssetKindExtensions(kind);
    if (exts.empty()) return !path.empty();
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    const std::string ext = Lower(path.substr(dot));
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

std::vector<std::string> ListAssetsOfKind(const std::string& root, const std::string& kind, std::size_t limit) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return out;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end && out.size() < limit; it.increment(ec)) {
        if (it->path().filename().string().rfind('.', 0) == 0) {          // hidden folders and files
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        const std::string p = it->path().string();
        if (AssetKindAccepts(kind, p)) out.push_back(p);
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    });
    return out;
}

namespace {
struct TipRow { const char* key; const char* text; };
const TipRow kTips[] = {
    {"Transform.position", "Where the object is, relative to its parent."},
    {"Transform.rotation", "How the object is turned, in degrees."},
    {"Transform.scale", "How big the object is on each axis."},
    {"Camera.projection", "Perspective looks like the real world (things shrink with distance). Orthographic has no depth shrink, good for 2D."},
    {"Camera.nearClip", "Closest distance the camera draws. Things nearer are cut off."},
    {"Camera.farClip", "Farthest distance the camera draws."},
    {"Camera.clearFlags", "What fills the screen before drawing: the sky, a flat colour, or nothing."},
    {"Camera.backgroundColor", "The flat colour behind everything when the camera clears to a colour."},
    {"Rigidbody2D.sleeping", "True while physics has put it to rest."},
    {"Rigidbody3D.sleeping", "True while physics has put it to rest."},
    {"Rigidbody3D.groundedOnTerrain", "True while it stands on terrain."},
    {"Camera.lensShiftX", "Slides the picture sideways without turning the camera."},
    {"Camera.lensShiftY", "Slides the picture up or down without turning the camera."},
    {"Camera.pixelsPerUnit", "How many screen pixels make one world unit (pixel-perfect 2D)."},
    {"Camera.shakeAmount", "Strength of the current camera shake."},
    {"Camera.shakeAngle", "How much the shake also rolls the picture, in degrees."},
    {"Camera.shakeFrequency", "How fast the shake wobbles."},
    {"Camera.shakeRecovery", "How quickly the shake dies down."},
    {"Camera.zoom", "Extra zoom on top of the field of view."},
    {"Camera.rectY", "Where the camera's picture starts from the bottom (0 to 1). Use with Width and Height for split screen."},
    {"Camera.rectW", "Width of the camera's picture on screen (0 to 1)."},
    {"Camera.rectH", "Height of the camera's picture on screen (0 to 1)."},
    {"Camera.pixelPerfect", "Snap the 2D view to whole pixels so pixel art stays sharp."},
    {"Light.type", "Directional is the sun, Point is a bulb, Spot is a cone, Area is a soft panel."},
    {"Light.color", "The colour of the light."},
    {"Light.intensity", "How bright the light is."},
    {"Light.spotAngle", "How wide the cone is, in degrees (spot lights)."},
    {"Light.spotSoftness", "How soft the edge of the cone is."},
    {"Light.falloff", "How fast the light fades with distance."},
    {"Light.castShadows", "Objects in front of this light throw shadows."},
    {"Light.shadowStrength", "How dark the shadows are."},
    {"Light.shadowBias", "Nudges shadows to remove speckles. Raise it if you see stripes."},
    {"Light.flickerSpeed", "How fast the flicker changes."},
    {"SpriteRenderer.color", "Tint multiplied with the picture. White leaves it unchanged."},
    {"SpriteRenderer.texture", "The picture to draw. Drop an image from the Project panel."},
    {"SpriteRenderer.uvMin", "Start of the part of the picture that is used (0 to 1)."},
    {"SpriteRenderer.uvMax", "End of the part of the picture that is used (0 to 1)."},
    {"SpriteRenderer.flipX", "Mirror the picture left to right."},
    {"SpriteRenderer.flipY", "Mirror the picture upside down."},
    {"SpriteRenderer.texFilter", "Smooth blends pixels, Point keeps them sharp (pixel art)."},
    {"MeshRenderer.wireframe", "Draw only the edges of the triangles."},
    {"MeshRenderer.doubleSided", "Draw both sides of every face (for thin things like leaves)."},
    {"MeshRenderer.shader", "The look: standard, toon, gradient and so on."},
    {"MeshRenderer.emissive", "Makes the surface glow with its own light."},
    {"MeshRenderer.specular", "How strong the shiny highlight is."},
    {"MeshRenderer.shininess", "How tight the highlight is. Higher is smaller and sharper."},
    {"MeshRenderer.metallic", "0 is plastic or stone, 1 is metal."},
    {"MeshRenderer.reflectivity", "How much the surface mirrors its surroundings."},
    {"MeshRenderer.texture", "The picture on the surface. Drop an image from the Project panel."},
    {"MeshRenderer.tiling", "How many times the picture repeats across the surface."},
    {"MeshRenderer.texOffset", "Slides the picture across the surface."},
    {"MeshRenderer.texFilter", "Smooth blends pixels, Point keeps them sharp."},
    {"MeshRenderer.normalMap", "A picture that fakes small bumps."},
    {"MeshRenderer.normalStrength", "How strong the fake bumps are."},
    {"MeshRenderer.specularMap", "A picture that says which parts are shiny."},
    {"MeshRenderer.aoMap", "A picture that darkens creases."},
    {"MeshRenderer.aoStrength", "How dark the creases are."},
    {"MeshRenderer.triplanar", "Project the picture from three sides so it does not stretch."},
    {"MeshRenderer.rimStrength", "Glow around the edge seen at an angle."},
    {"MeshRenderer.rimPower", "How thin the edge glow is."},
    {"MeshRenderer.rimColor", "Colour of the edge glow."},
    {"MeshRenderer.outline", "Draw an outline around the object."},
    {"MeshRenderer.outlineColor", "Colour of the outline."},
    {"MeshRenderer.outlineWidth", "Thickness of the outline."},
    {"MeshRenderer.castShadows", "This object throws shadows."},
    {"MeshRenderer.receiveShadows", "Shadows fall on this object."},
    {"MeshRenderer.groundShadowY", "Height of the ground the blob shadow sits on."},
    {"MeshRenderer.groundShadowStrength", "How dark the blob shadow is."},
    {"MeshRenderer.gradientTop", "Colour at the top for the gradient look."},
    {"MeshRenderer.gradientBottom", "Colour at the bottom for the gradient look."},
    {"Rigidbody2D.bodyType", "Dynamic moves with physics, Kinematic moves only by script, Static never moves."},
    {"Rigidbody2D.mass", "How heavy it is. Heavier things are harder to push."},
    {"Rigidbody2D.gravityScale", "How much gravity pulls on it. 0 floats, 1 is normal."},
    {"Rigidbody2D.angularDrag", "How quickly spinning slows down."},
    {"Rigidbody2D.bounciness", "How much it bounces (0 none, 1 full)."},
    {"Rigidbody2D.friction", "How much it grips what it touches."},
    {"Rigidbody2D.freezeRotation", "Never rotate from physics."},
    {"Rigidbody2D.allowSleep", "Stop simulating it when it comes to rest, to save time."},
    {"Rigidbody2D.continuousCollision", "Catch fast objects so they do not pass through thin walls."},
    {"Rigidbody3D.bodyType", "Dynamic moves with physics, Kinematic moves only by script, Static never moves."},
    {"Rigidbody3D.mass", "How heavy it is. Heavier things are harder to push."},
    {"Rigidbody3D.gravityScale", "How much gravity pulls on it. 0 floats, 1 is normal."},
    {"Rigidbody3D.angularDrag", "How quickly spinning slows down."},
    {"Rigidbody3D.bounciness", "How much it bounces (0 none, 1 full)."},
    {"Rigidbody3D.friction", "How much it grips what it touches."},
    {"Rigidbody3D.freezeX", "Do not move along X."},
    {"Rigidbody3D.freezeY", "Do not move along Y."},
    {"Rigidbody3D.freezeZ", "Do not move along Z."},
    {"Rigidbody3D.freezeRotation", "Never rotate from physics."},
    {"Rigidbody3D.allowSleep", "Stop simulating it when it comes to rest, to save time."},
    {"Rigidbody3D.continuousCollision", "Catch fast objects so they do not pass through thin walls."},
    {"BoxCollider2D.size", "Width and height of the box."},
    {"BoxCollider3D.size", "Width, height and depth of the box."},
    {"MeshCollider3D.size", "Fallback box size, used only when there is no mesh with triangles. Otherwise static meshes use the exact triangles and a dynamic body uses the convex hull."},
    {"CircleCollider2D.radius", "How big the circle is."},
    {"SphereCollider3D.radius", "How big the sphere is."},
    {"CapsuleCollider2D.size", "Width and height of the capsule."},
    {"CapsuleCollider2D.direction", "Which way the capsule stands."},
    {"CapsuleCollider3D.radius", "How wide the capsule is."},
    {"CylinderCollider3D.radius", "How wide the cylinder is."},
    {"CylinderCollider3D.height", "How tall the cylinder is."},
    {"CylinderCollider3D.axis", "Which way the cylinder stands."},
    {"AudioSource.volume", "How loud it is (0 to 1)."},
    {"AudioSource.loop", "Start again when it ends."},
    {"AudioSource.playOnAwake", "Start playing when the game starts."},
    {"AudioSource.spatial", "Sound gets quieter and pans with distance and direction (3D sound)."},
    {"AudioSource.minDistance", "Within this distance the sound is at full volume."},
    {"AudioSource.maxDistance", "Beyond this distance the sound is silent."},
    {"AudioSource.rolloff", "How fast the sound fades between the two distances."},
};
}

std::string TooltipFor(const std::string& typeName, const PropertyDesc& d) {
    if (!d.tooltip.empty()) return d.tooltip;
    const std::string key = typeName + "." + d.name;
    for (const TipRow& r : kTips) if (key == r.key) return r.text;
    return {};
}

std::string FieldTooltip(const std::string& typeName, const std::string& label) {
    auto info = PropertyRegistry::FindByName(typeName);
    if (!info) return {};
    std::string l = label.substr(0, label.find("##"));
    while (!l.empty() && l.back() == ' ') l.pop_back();
    l = Lower(l);
    for (const PropertyDesc& d : info->properties)
        if (Lower(PrettyName(d.name)) == l || Lower(d.name) == l) return TooltipFor(typeName, d);
    return {};
}

std::vector<std::string> MixedPropertyNames(const std::vector<GameObject*>& objs, const std::string& typeName, int index) {
    std::vector<std::string> out;
    auto info = PropertyRegistry::FindByName(typeName);
    if (!info || objs.size() < 2) return out;
    for (const PropertyDesc& d : info->properties) {
        if (d.IsHidden() || d.IsRuntimeOnly() || d.name == "enabled") continue;
        const FieldState st = ReadField(objs, typeName, index, d);
        if (st.have > 1 && st.mixed) out.push_back(PrettyName(d.name));
    }
    return out;
}

RootSlot CaptureRootSlot(Scene& scene, const GameObject& go) {
    RootSlot slot;
    if (!go.transform || go.transform->Parent()) return slot;
    const GameObject* before = nullptr;
    bool found = false;
    for (const auto& up : scene.Objects()) {
        if (!up || up->IsDestroyed() || !up->transform || up->transform->Parent()) continue;
        if (up.get() == &go) { found = true; if (before) { slot.valid = true; slot.neighbourGuid = before->Guid(); slot.neighbourIsBefore = true; } continue; }
        if (!found) before = up.get();
        else if (!slot.valid) { slot.valid = true; slot.neighbourGuid = up->Guid(); slot.neighbourIsBefore = false; }
    }
    return slot;
}

void RestoreRootSlot(Scene& scene, GameObject* go, const RootSlot& slot) {
    if (!go || !slot.valid || !go->transform || go->transform->Parent()) return;
    GameObject* n = scene.FindByGuid(slot.neighbourGuid);
    if (!n || n == go || n->IsDestroyed() || !n->transform || n->transform->Parent()) return;
    scene.ReorderSibling(go, n, /*after=*/slot.neighbourIsBefore);
}

// ---- Prefab overrides ----------------------------------------------------------------------------------------

std::string RecordNameOf(const Component& c) {
    auto info = PropertyRegistry::FindFor(c);
    return info ? info->recordName : std::string();
}

std::vector<PrefabChange> PrefabChangesOf(GameObject& root, std::string* error) {
    std::vector<PrefabChange> out;
    PrefabDiff d;
    if (root.prefabSource.empty() || !PrefabTools::Diff(root, d, error)) return out;
    std::unordered_set<const GameObject*> added;
    bool anyRemoved = false;
    for (const auto& o : d.objects) {
        if (o.kind == PrefabObjectDiff::Kind::Added && o.live) added.insert(o.live);
        else if (o.kind == PrefabObjectDiff::Kind::Removed) anyRemoved = true;
    }
    // The prefab's objects in file order, as they sit in the live instance.
    std::vector<GameObject*> live;
    std::function<void(GameObject*)> walk = [&](GameObject* g) {
        if (added.count(g)) return;
        live.push_back(g);
        for (Transform* c : g->transform->Children())
            if (c && c->gameObject) walk(c->gameObject);
    };
    walk(&root);
    for (const auto& r : d.records) {
        PrefabChange ch;
        ch.record = r.record;
        ch.occurrence = r.occurrence;
        ch.prefabObject = r.object;
        ch.added = r.kind == PrefabRecordDiff::Kind::Added;
        ch.removed = r.kind == PrefabRecordDiff::Kind::Removed;
        ch.description = r.Describe();
        ch.objectName = r.objectName;
        ch.fields = r.fields;
        ch.wholeRecordOnly = r.wholeRecordOnly;
        GameObject* g = (r.object >= 0 && r.object < (int)live.size()) ? live[(std::size_t)r.object] : nullptr;
        // Positions drift when objects were lost, so then trust the name. Without lost objects the position is exact, also for
        // an object (or the instance root) that was renamed.
        if (g && anyRemoved && r.object != 0 && g->name != r.objectName) {
            GameObject* byName = nullptr;
            for (GameObject* l : live) if (l->name == r.objectName) { byName = l; break; }
            g = byName;
        }
        ch.object = g;
        out.push_back(std::move(ch));
    }
    return out;
}

const PrefabChange* FindChange(const std::vector<PrefabChange>& changes, const GameObject& go, const Component& c) {
    const std::string rec = RecordNameOf(c);
    if (rec.empty()) return nullptr;
    // The n-th component of this record type on the object is the n-th occurrence of the record.
    int occurrence = 0;
    for (const auto& up : go.Components()) {
        if (up.get() == &c) break;
        if (up && RecordNameOf(*up) == rec) ++occurrence;
    }
    for (const PrefabChange& ch : changes)
        if (ch.object == &go && ch.record == rec && ch.occurrence == occurrence) return &ch;
    return nullptr;
}

bool IsChanged(const std::vector<PrefabChange>& changes, const GameObject& go, const Component& c) {
    return FindChange(changes, go, c) != nullptr;
}

} // namespace oked
