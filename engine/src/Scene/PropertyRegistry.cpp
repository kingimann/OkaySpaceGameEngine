#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>
#include <unordered_map>

namespace okay {

namespace {

struct HookEntry { int id; PropertyRegistry::ChangeHook fn; };
using HookList = std::vector<HookEntry>;

struct Table {
    std::mutex mutex;
    std::vector<std::shared_ptr<const ComponentTypeInfo>> infos;
    std::unordered_map<std::type_index, std::size_t> byType;
    // Name lookups without scanning every type: exact type name, exact record name,
    // and the lower-cased form of both (first registered wins, as the scan did).
    std::unordered_map<std::string, std::size_t> byName, byRecord, byLower;
    // The hook list is never edited in place: Add/Remove build a new one and swap the
    // pointer, so a change that is being announced keeps walking the list it started with.
    std::shared_ptr<const HookList> hooks = std::make_shared<const HookList>();
    std::atomic<std::size_t> hookCount{0};   // size of `hooks`, readable without the lock
    int nextHook = 1;
    std::recursive_mutex builtinMutex;
    std::atomic<bool> builtinsDone{false};
};
Table& GetTable() { static Table t; return t; }

thread_local int t_suppress = 0;
// How many announcements are being delivered on this thread right now. A hook that
// writes the property it was told about (a clamp it disagrees with, two hooks that undo
// each other) would otherwise recurse until the stack ends.
thread_local int t_fireDepth = 0;
constexpr int kMaxFireDepth = 32;

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void EnsureBuiltins() {
    if (!GetTable().builtinsDone.load(std::memory_order_acquire)) RegisterBuiltinProperties();
}

// ---- Paths ------------------------------------------------------------------

struct ParsedPath {
    std::string component;
    int index = 0;
    std::string property;
    std::string sub;
};

bool SetErr(std::string* error, const std::string& msg) {
    if (error) *error = msg;
    return false;
}

bool ParsePath(const std::string& path, ParsedPath& out, std::string* error) {
    const std::size_t dot = path.find('.');
    if (dot == std::string::npos || dot == 0) return SetErr(error, "path '" + path + "' is not Component.property");
    std::string comp = path.substr(0, dot);
    const std::string rest = path.substr(dot + 1);
    int index = 0;
    const std::size_t hash = comp.find('#');
    if (hash != std::string::npos) {
        const std::string num = comp.substr(hash + 1);
        comp = comp.substr(0, hash);
        if (num.empty() || num.size() > 6) return SetErr(error, "bad component index in '" + path + "'");
        for (char c : num) if (!std::isdigit(static_cast<unsigned char>(c))) return SetErr(error, "bad component index in '" + path + "'");
        index = std::stoi(num);
    }
    if (comp.empty()) return SetErr(error, "path '" + path + "' has no component name");
    std::string prop = rest, sub;
    const std::size_t dot2 = rest.find('.');
    if (dot2 != std::string::npos) {
        prop = rest.substr(0, dot2);
        sub = rest.substr(dot2 + 1);
        if (sub.empty() || sub.find('.') != std::string::npos) return SetErr(error, "bad sub-field in '" + path + "'");
    }
    if (prop.empty()) return SetErr(error, "path '" + path + "' has no property name");
    out.component = comp;
    out.index = index;
    out.property = prop;
    out.sub = sub;
    return true;
}

// The components of `go` that belong to `info`, in stored order.
std::vector<Component*> ComponentsFor(const GameObject& go, const ComponentTypeInfo& info) {
    std::vector<Component*> out;
    if (info.objectLevel) {
        if (go.transform) out.push_back(go.transform);
        return out;
    }
    for (const auto& c : go.Components())
        if (std::type_index(typeid(*c)) == info.type) out.push_back(c.get());
    return out;
}

int IndexOfComponent(const GameObject& go, const ComponentTypeInfo& info, const Component* c) {
    if (info.objectLevel) return 0;
    int i = 0;
    for (const auto& k : go.Components()) {
        if (std::type_index(typeid(*k)) != info.type) continue;
        if (k.get() == c) return i;
        ++i;
    }
    return 0;
}

std::string MakePath(const std::string& component, int index, const std::string& property) {
    return index > 0 ? component + "#" + std::to_string(index) + "." + property : component + "." + property;
}

bool Wanted(const PropertyDesc& d, const PropertySnapshotOptions& o) {
    if (d.IsReadOnly() && !o.includeReadOnly) return false;
    if (d.IsRuntimeOnly() && !o.includeRuntime) return false;
    if (d.IsDerived() && !o.includeDerived) return false;
    return true;
}

// Still subscribed? A hook that an earlier hook of the same change removed must not run.
bool StillSubscribed(const std::shared_ptr<const HookList>& walking, int id) {
    Table& t = GetTable();
    std::lock_guard<std::mutex> lock(t.mutex);
    if (t.hooks == walking) return true;   // nothing was added or removed since the walk began
    for (const HookEntry& h : *t.hooks) if (h.id == id) return true;
    return false;
}

void Fire(const PropertyChange& ch) {
    if (t_suppress > 0 || t_fireDepth >= kMaxFireDepth) return;
    std::shared_ptr<const HookList> list;
    {
        Table& t = GetTable();
        std::lock_guard<std::mutex> lock(t.mutex);
        list = t.hooks;
    }
    if (list->empty()) return;
    struct Depth { Depth() { ++t_fireDepth; } ~Depth() { --t_fireDepth; } } depth;
    for (const HookEntry& h : *list) {
        if (!h.fn || !StillSubscribed(list, h.id)) continue;
        try { h.fn(ch); } catch (...) {}
    }
}

void FireFor(const PropertyRef& ref, const PropertyDesc& d, const PropertyValue& oldValue, const PropertyValue& newValue) {
    // Nobody listening (the usual case for a tween writing every frame): build nothing.
    if (t_suppress > 0 || t_fireDepth >= kMaxFireDepth || GetTable().hookCount.load(std::memory_order_relaxed) == 0) return;
    PropertyChange ch;
    ch.component = ref.component;
    ch.object = ref.component->gameObject;
    ch.componentInfo = ref.info;
    ch.property = &d;
    ch.componentIndex = ref.componentIndex;
    ch.path = MakePath(ref.info->name, ref.componentIndex, d.name);
    ch.oldValue = oldValue;
    ch.newValue = newValue;
    Fire(ch);
}

// Validate, store and announce one value for a resolved property.
bool StoreAndNotify(const PropertyRef& ref, const PropertyValue& value, std::string* error) {
    const PropertyDesc& d = *ref.desc;
    if (d.IsReadOnly()) return SetErr(error, "property '" + d.name + "' is read-only");
    if (!d.setter) return SetErr(error, "property '" + d.name + "' cannot be set");
    const PropertyValue oldWhole = d.Get(*ref.component);
    PropertyValue incoming = value;
    if (!ref.subField.empty()) {
        if (!oldWhole.WithSubField(ref.subField, value, incoming))
            return SetErr(error, "sub-field '" + ref.subField + "' needs a Float value");
    }
    PropertyValue stored;
    if (!d.Normalize(incoming, stored))
        return SetErr(error, "value rejected for '" + d.name + "' (" + PropertyTypeName(d.type) + ": wrong type, out of range or not allowed)");

    if (d.IsDerived()) {
        // A derived property is another view of authored ones: announce those instead
        // (writing Transform.eulerAngles announces Transform.rotation), so a subscriber
        // sees each stored value once and can restore it exactly.
        const PropertySnapshot before = SnapshotComponent(*ref.component);
        if (!d.Set(*ref.component, incoming)) return SetErr(error, "'" + d.name + "' refused the value");
        const PropertySnapshot after = SnapshotComponent(*ref.component);
        for (const PropertyDiff& df : DiffProperties(before, after)) {
            if (df.kind != PropertyDiffKind::Changed) continue;
            if (const PropertyDesc* changed = ref.info->Find(df.property)) FireFor(ref, *changed, df.before, df.after);
        }
        return true;
    }

    if (!d.Set(*ref.component, incoming)) return SetErr(error, "'" + d.name + "' refused the value");
    PropertyValue now = d.Get(*ref.component);
    if (!now.IsValid()) now = stored;
    if (now != oldWhole) FireFor(ref, d, oldWhole, now);
    return true;
}

PropertyRef MakeRef(Component* c, const std::shared_ptr<const ComponentTypeInfo>& info, const PropertyDesc* d, int index) {
    PropertyRef r;
    r.component = c;
    r.info = info;
    r.desc = d;
    r.componentIndex = index;
    return r;
}

} // namespace

// ---- ComponentTypeInfo --------------------------------------------------------

const PropertyDesc* ComponentTypeInfo::Find(const std::string& propertyName) const {
    for (const PropertyDesc& d : properties) if (d.name == propertyName) return &d;
    const std::string low = Lower(propertyName);
    for (const PropertyDesc& d : properties) if (Lower(d.name) == low) return &d;
    return nullptr;
}

// ---- Registry ----------------------------------------------------------------------

namespace {
// A name a path can address: no '.' (property and sub-field separator), no '#'
// (component index), no white space, and not empty.
bool AddressableName(const std::string& n) {
    if (n.empty()) return false;
    for (unsigned char c : n) if (c == '.' || c == '#' || std::isspace(c)) return false;
    return true;
}

// Rebuild the name tables after the entry list changed (caller holds the mutex).
void ReindexLocked(Table& t) {
    t.byType.clear(); t.byName.clear(); t.byRecord.clear(); t.byLower.clear();
    for (std::size_t i = 0; i < t.infos.size(); ++i) {
        const ComponentTypeInfo& e = *t.infos[i];
        if (e.type != std::type_index(typeid(void))) t.byType[e.type] = i;
        t.byName.emplace(e.name, i);
        if (!e.recordName.empty()) t.byRecord.emplace(e.recordName, i);
        t.byLower.emplace(Lower(e.name), i);
        if (!e.recordName.empty()) t.byLower.emplace(Lower(e.recordName), i);
    }
}
} // namespace

void PropertyRegistry::Register(ComponentTypeInfo info) {
    // A type a path cannot name could never be reached: ignore it.
    if (!AddressableName(info.name)) return;
    if (!info.recordName.empty() && !AddressableName(info.recordName)) info.recordName.clear();
    // A property needs a name a path can address, a type and a getter, and each name
    // may appear once (the first one stays); drop the ones that do not qualify.
    {
        std::vector<PropertyDesc> kept;
        kept.reserve(info.properties.size());
        for (PropertyDesc& d : info.properties) {
            if (!AddressableName(d.name) || d.type == PropertyType::None || !d.getter) continue;
            bool dup = false;
            for (const PropertyDesc& k : kept) if (k.name == d.name) { dup = true; break; }
            if (!dup) kept.push_back(std::move(d));
        }
        info.properties = std::move(kept);
    }
    auto sp = std::make_shared<const ComponentTypeInfo>(std::move(info));
    Table& t = GetTable();
    std::lock_guard<std::mutex> lock(t.mutex);
    // Replace the entry with this name, else the one for this exact type (an entry
    // with no type set, typeid(void), never matches by type).
    std::size_t at = t.infos.size();
    for (std::size_t i = 0; i < t.infos.size() && at == t.infos.size(); ++i)
        if (t.infos[i]->name == sp->name) at = i;
    if (at == t.infos.size() && sp->type != std::type_index(typeid(void)))
        for (std::size_t i = 0; i < t.infos.size() && at == t.infos.size(); ++i)
            if (t.infos[i]->type == sp->type) at = i;
    if (at == t.infos.size()) t.infos.push_back(sp);
    else t.infos[at] = sp;
    ReindexLocked(t);
}

std::shared_ptr<const ComponentTypeInfo> PropertyRegistry::FindByName(const std::string& name) {
    EnsureBuiltins();
    if (name.empty()) return nullptr;
    Table& t = GetTable();
    std::lock_guard<std::mutex> lock(t.mutex);
    auto it = t.byName.find(name);
    if (it != t.byName.end()) return t.infos[it->second];
    it = t.byRecord.find(name);
    if (it != t.byRecord.end()) return t.infos[it->second];
    it = t.byLower.find(Lower(name));
    return it == t.byLower.end() ? nullptr : t.infos[it->second];
}

std::shared_ptr<const ComponentTypeInfo> PropertyRegistry::FindByType(std::type_index type) {
    EnsureBuiltins();
    Table& t = GetTable();
    std::lock_guard<std::mutex> lock(t.mutex);
    auto it = t.byType.find(type);
    return it == t.byType.end() ? nullptr : t.infos[it->second];
}

std::shared_ptr<const ComponentTypeInfo> PropertyRegistry::FindFor(const Component& c) {
    return FindByType(std::type_index(typeid(c)));
}

std::vector<std::shared_ptr<const ComponentTypeInfo>> PropertyRegistry::All() {
    EnsureBuiltins();
    Table& t = GetTable();
    std::lock_guard<std::mutex> lock(t.mutex);
    return t.infos;
}

void PropertyRegistry::Clear() {
    Table& t = GetTable();
    std::lock_guard<std::recursive_mutex> bl(t.builtinMutex);
    {
        std::lock_guard<std::mutex> lock(t.mutex);
        t.infos.clear();
        ReindexLocked(t);
    }
    t.builtinsDone.store(false, std::memory_order_release);
}

// ---- Path access ----------------------------------------------------------------------

bool PropertyRegistry::Resolve(const GameObject& go, const std::string& path, PropertyRef& out, std::string* error) {
    ParsedPath p;
    if (!ParsePath(path, p, error)) return false;
    auto info = FindByName(p.component);
    if (!info) return SetErr(error, "unknown component '" + p.component + "'");
    const PropertyDesc* d = info->Find(p.property);
    if (!d) return SetErr(error, "component '" + info->name + "' has no property '" + p.property + "'");
    if (!p.sub.empty()) {
        int idx = 0;
        if (!PropertyValue::SubFieldIndex(d->type, p.sub, idx))
            return SetErr(error, "'" + p.sub + "' is not a sub-field of " + d->name + " (" + PropertyTypeName(d->type) + ")");
    }
    const std::vector<Component*> comps = ComponentsFor(go, *info);
    if (p.index < 0 || p.index >= static_cast<int>(comps.size()))
        return SetErr(error, "object '" + go.name + "' has no " + info->name + (p.index ? " #" + std::to_string(p.index) : std::string()));
    out = MakeRef(comps[static_cast<std::size_t>(p.index)], info, d, p.index);
    out.subField = p.sub;
    return true;
}

bool PropertyRegistry::TryGetProperty(const GameObject& go, const std::string& path, PropertyValue& out, std::string* error) {
    PropertyRef ref;
    if (!Resolve(go, path, ref, error)) return false;
    PropertyValue v = ref.desc->Get(*ref.component);
    if (!v.IsValid()) return SetErr(error, "property '" + ref.desc->name + "' cannot be read");
    if (!ref.subField.empty()) {
        PropertyValue sub;
        if (!v.GetSubField(ref.subField, sub)) return SetErr(error, "bad sub-field '" + ref.subField + "'");
        v = sub;
    }
    out = v;
    return true;
}

PropertyValue PropertyRegistry::GetProperty(const GameObject& go, const std::string& path) {
    PropertyValue v;
    if (!TryGetProperty(go, path, v)) return PropertyValue();
    return v;
}

std::string PropertyRegistry::GetPropertyText(const GameObject& go, const std::string& path) {
    PropertyRef ref;
    if (!Resolve(go, path, ref)) return std::string();
    PropertyValue v = ref.desc->Get(*ref.component);
    if (!ref.subField.empty()) {
        PropertyValue sub;
        if (!v.GetSubField(ref.subField, sub)) return std::string();
        return sub.ToString();
    }
    return ref.desc->FormatText(v);
}

bool PropertyRegistry::SetProperty(GameObject& go, const std::string& path, const PropertyValue& value, std::string* error) {
    PropertyRef ref;
    if (!Resolve(go, path, ref, error)) return false;
    return StoreAndNotify(ref, value, error);
}

bool PropertyRegistry::SetPropertyFromText(GameObject& go, const std::string& path, const std::string& text, std::string* error) {
    PropertyRef ref;
    if (!Resolve(go, path, ref, error)) return false;
    PropertyValue v;
    const bool parsed = ref.subField.empty() ? ref.desc->ParseText(text, v)
                                             : PropertyValue::FromString(PropertyType::Float, text, v);
    if (!parsed) return SetErr(error, "cannot read '" + text + "' as " + (ref.subField.empty() ? PropertyTypeName(ref.desc->type) : "Float"));
    return StoreAndNotify(ref, v, error);
}

bool PropertyRegistry::SetComponentProperty(Component& c, const std::string& property, const PropertyValue& value, std::string* error) {
    auto info = FindFor(c);
    if (!info) return SetErr(error, "component type has no registered properties");
    std::string prop = property, sub;
    const std::size_t dot = property.find('.');
    if (dot != std::string::npos) { prop = property.substr(0, dot); sub = property.substr(dot + 1); }
    const PropertyDesc* d = info->Find(prop);
    if (!d) return SetErr(error, "component '" + info->name + "' has no property '" + prop + "'");
    if (!sub.empty()) {
        int idx = 0;
        if (!PropertyValue::SubFieldIndex(d->type, sub, idx)) return SetErr(error, "'" + sub + "' is not a sub-field of " + d->name);
    }
    PropertyRef ref = MakeRef(&c, info, d, c.gameObject ? IndexOfComponent(*c.gameObject, *info, &c) : 0);
    ref.subField = sub;
    return StoreAndNotify(ref, value, error);
}

namespace {
PropertyWriteResult WriteResolved(const PropertyRef& ref, const PropertyValue& value) {
    PropertyWriteResult r;
    r.path = MakePath(ref.info->name, ref.componentIndex, ref.desc->name);
    r.componentIndex = ref.componentIndex;
    const PropertyValue before = ref.desc->Get(*ref.component);
    if (!StoreAndNotify(ref, value, &r.error)) return r;
    r.ok = true;
    r.oldValue = before;
    r.newValue = ref.desc->Get(*ref.component);
    r.changed = r.newValue != before;
    return r;
}
} // namespace

PropertyWriteResult PropertyRegistry::WriteProperty(GameObject& go, const std::string& path, const PropertyValue& value) {
    PropertyRef ref;
    std::string error;
    if (!Resolve(go, path, ref, &error)) { PropertyWriteResult r; r.error = error; return r; }
    return WriteResolved(ref, value);
}

PropertyWriteResult PropertyRegistry::WritePropertyFromText(GameObject& go, const std::string& path, const std::string& text) {
    PropertyRef ref;
    PropertyWriteResult r;
    if (!Resolve(go, path, ref, &r.error)) return r;
    PropertyValue v;
    const bool parsed = ref.subField.empty() ? ref.desc->ParseText(text, v)
                                             : PropertyValue::FromString(PropertyType::Float, text, v);
    if (!parsed) {
        r.error = "cannot read '" + text + "' as " + (ref.subField.empty() ? PropertyTypeName(ref.desc->type) : "Float");
        return r;
    }
    return WriteResolved(ref, v);
}

PropertyValue PropertyRegistry::GetComponentProperty(const Component& c, const std::string& property) {
    auto info = FindFor(c);
    if (!info) return PropertyValue();
    std::string prop = property, sub;
    const std::size_t dot = property.find('.');
    if (dot != std::string::npos) { prop = property.substr(0, dot); sub = property.substr(dot + 1); }
    const PropertyDesc* d = info->Find(prop);
    if (!d) return PropertyValue();
    PropertyValue v = d->Get(c);
    if (sub.empty()) return v;
    PropertyValue out;
    return v.GetSubField(sub, out) ? out : PropertyValue();
}

// ---- Hooks ---------------------------------------------------------------------------------

int PropertyRegistry::AddChangeHook(ChangeHook hook) {
    if (!hook) return 0;
    Table& t = GetTable();
    std::lock_guard<std::mutex> lock(t.mutex);
    const int id = t.nextHook++;
    auto next = std::make_shared<HookList>(*t.hooks);
    next->push_back({id, std::move(hook)});
    t.hookCount.store(next->size(), std::memory_order_relaxed);
    t.hooks = std::move(next);
    return id;
}

void PropertyRegistry::RemoveChangeHook(int id) {
    Table& t = GetTable();
    std::lock_guard<std::mutex> lock(t.mutex);
    auto next = std::make_shared<HookList>(*t.hooks);
    next->erase(std::remove_if(next->begin(), next->end(), [id](const HookEntry& h) { return h.id == id; }), next->end());
    t.hookCount.store(next->size(), std::memory_order_relaxed);
    t.hooks = std::move(next);
}

PropertyRegistry::ScopedHookSuppression::ScopedHookSuppression() { ++t_suppress; }
PropertyRegistry::ScopedHookSuppression::~ScopedHookSuppression() { --t_suppress; }

// ---- Builtins -------------------------------------------------------------------------------

void RegisterBuiltinProperties() {
    Table& t = GetTable();
    if (t.builtinsDone.load(std::memory_order_acquire)) return;
    // A lookup made by the registration code itself (same thread) must not recurse;
    // other threads wait on the mutex until the table is complete.
    thread_local bool registering = false;
    if (registering) return;
    std::lock_guard<std::recursive_mutex> lock(t.builtinMutex);
    if (t.builtinsDone.load(std::memory_order_acquire)) return;
    struct Guard { bool& flag; explicit Guard(bool& f) : flag(f) { flag = true; } ~Guard() { flag = false; } } guard(registering);
    detail::RegisterBuiltinPropertyTable();
    t.builtinsDone.store(true, std::memory_order_release);
}

// ---- Snapshot, diff, copy ----------------------------------------------------------------------

std::string PropertySnapshot::Entry::Path() const { return MakePath(component, index, property); }

const PropertySnapshot::Entry* PropertySnapshot::Find(const std::string& path) const {
    ParsedPath p;
    if (!ParsePath(path, p, nullptr) || !p.sub.empty()) return nullptr;
    for (const Entry& e : entries)
        if (e.index == p.index && e.component == p.component && e.property == p.property) return &e;
    return nullptr;
}

namespace {
void SnapshotInto(PropertySnapshot& snap, const ComponentTypeInfo& info, const Component& c, int index, const PropertySnapshotOptions& o) {
    for (const PropertyDesc& d : info.properties) {
        if (!Wanted(d, o)) continue;
        PropertyValue v = d.Get(c);
        if (!v.IsValid()) continue;
        PropertySnapshot::Entry e;
        e.component = info.name;
        e.index = index;
        e.property = d.name;
        e.value = std::move(v);
        snap.entries.push_back(std::move(e));
    }
}
} // namespace

PropertySnapshot SnapshotProperties(const GameObject& go, const PropertySnapshotOptions& options) {
    PropertySnapshot snap;
    if (auto objInfo = PropertyRegistry::FindByType(std::type_index(typeid(GameObject)))) {
        if (go.transform) SnapshotInto(snap, *objInfo, *go.transform, 0, options);
    }
    std::unordered_map<const ComponentTypeInfo*, int> seen;
    for (const auto& c : go.Components()) {
        auto info = PropertyRegistry::FindFor(*c);
        if (!info || info->objectLevel) continue;
        const int index = seen[info.get()]++;
        SnapshotInto(snap, *info, *c, index, options);
    }
    return snap;
}

PropertySnapshot SnapshotComponent(const Component& c, const PropertySnapshotOptions& options) {
    PropertySnapshot snap;
    if (auto info = PropertyRegistry::FindFor(c)) SnapshotInto(snap, *info, c, 0, options);
    return snap;
}

std::vector<PropertyDiff> DiffProperties(const PropertySnapshot& a, const PropertySnapshot& b) {
    std::vector<PropertyDiff> out;
    std::unordered_map<std::string, const PropertySnapshot::Entry*> inA, inB;
    for (const auto& e : a.entries) inA[e.Path()] = &e;
    for (const auto& e : b.entries) inB[e.Path()] = &e;
    for (const auto& e : b.entries) {
        const std::string path = e.Path();
        auto it = inA.find(path);
        PropertyDiff d;
        d.path = path; d.component = e.component; d.index = e.index; d.property = e.property;
        d.after = e.value;
        if (it == inA.end()) {
            d.kind = PropertyDiffKind::Added;
        } else {
            if (it->second->value == e.value) continue;
            d.kind = PropertyDiffKind::Changed;
            d.before = it->second->value;
        }
        out.push_back(std::move(d));
    }
    for (const auto& e : a.entries) {
        const std::string path = e.Path();
        if (inB.count(path)) continue;
        PropertyDiff d;
        d.path = path; d.component = e.component; d.index = e.index; d.property = e.property;
        d.kind = PropertyDiffKind::Removed;
        d.before = e.value;
        out.push_back(std::move(d));
    }
    return out;
}

int ApplyPropertySnapshot(GameObject& go, const PropertySnapshot& snapshot) {
    int written = 0;
    for (const auto& e : snapshot.entries) {
        PropertyRef ref;
        if (!PropertyRegistry::Resolve(go, e.Path(), ref)) continue;
        if (StoreAndNotify(ref, e.value, nullptr)) ++written;
    }
    return written;
}

int ApplyPropertyDiff(GameObject& go, const std::vector<PropertyDiff>& diff, bool forward) {
    int written = 0;
    for (const auto& d : diff) {
        if (d.kind != PropertyDiffKind::Changed) continue;
        PropertyRef ref;
        if (!PropertyRegistry::Resolve(go, d.path, ref)) continue;
        if (StoreAndNotify(ref, forward ? d.after : d.before, nullptr)) ++written;
    }
    return written;
}

int CopyProperties(const GameObject& from, GameObject& to, const PropertyCopyOptions& o) {
    const PropertySnapshot snap = SnapshotProperties(from, o.what);
    int written = 0;
    for (const auto& e : snap.entries) {
        auto info = PropertyRegistry::FindByName(e.component);
        if (!info) continue;
        if (info->objectLevel) { if (!o.includeObjectFields) continue; }
        else if (info->type == std::type_index(typeid(Transform)) && !o.includeTransform) continue;
        if (!o.onlyComponents.empty() && std::find(o.onlyComponents.begin(), o.onlyComponents.end(), info->name) == o.onlyComponents.end()) continue;
        const PropertyDesc* d = info->Find(e.property);
        if (!d) continue;
        if ((d->flags & PropertyFlag::Structural) && !o.includeStructural) continue;
        std::vector<Component*> comps = ComponentsFor(to, *info);
        if (e.index >= static_cast<int>(comps.size())) {
            if (!o.addMissingComponents || !info->add) continue;
            // Add components until the wanted index exists. A type that allows only one
            // per object hands back the existing one: stop then.
            for (int guard = 0; guard < 8 && static_cast<int>(comps.size()) <= e.index; ++guard) {
                const std::size_t before = comps.size();
                if (!info->add(to)) break;
                comps = ComponentsFor(to, *info);
                if (comps.size() == before) break;
            }
            if (e.index >= static_cast<int>(comps.size())) continue;
        }
        const PropertyRef ref = MakeRef(comps[static_cast<std::size_t>(e.index)], info, d, e.index);
        if (StoreAndNotify(ref, e.value, nullptr)) ++written;
    }
    return written;
}

int CopyComponentProperties(const Component& from, Component& to, const PropertySnapshotOptions& options) {
    if (std::type_index(typeid(from)) != std::type_index(typeid(to))) return 0;
    auto info = PropertyRegistry::FindFor(from);
    if (!info) return 0;
    int written = 0;
    const int index = to.gameObject ? IndexOfComponent(*to.gameObject, *info, &to) : 0;
    for (const PropertyDesc& d : info->properties) {
        if (!Wanted(d, options) || (d.flags & PropertyFlag::Structural)) continue;
        const PropertyValue v = d.Get(from);
        if (!v.IsValid()) continue;
        if (StoreAndNotify(MakeRef(&to, info, &d, index), v, nullptr)) ++written;
    }
    return written;
}

} // namespace okay
