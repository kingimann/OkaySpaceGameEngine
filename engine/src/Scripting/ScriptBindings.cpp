// The hand-written half of the script binding layer (okay/Scripting/ScriptBindings.hpp): the class table, the
// handles, the type checks and the overload choice. The per-class method entries are generated into
// ScriptBindingsGenerated.cpp by scripts/gen_script_bindings.py; properties come from the PropertyRegistry.
// Written for this engine from its own headers; no code from another project.
#include "okay/Scripting/ScriptBindings.hpp"
#include "okay/Core/StringUtil.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Quat.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

namespace okay::scriptbind {
namespace {

using V = vs::VsValue;



struct Table {
    std::vector<ClassBinding> classes;
    std::unordered_map<std::string, std::size_t> byName;    // exact and lower-case names
    std::unordered_map<std::type_index, std::size_t> byType;
};

// Filled once, on first use, and never changed after: readers need no lock once it is built.
std::vector<ClassBinding>* g_building = nullptr;

Table& BuildTable() {
    static Table* t = [] {
        auto* table = new Table();
        std::vector<ClassBinding> list;
        g_building = &list;
        RegisterGeneratedBindings();
        g_building = nullptr;
        table->classes = std::move(list);
        for (std::size_t i = 0; i < table->classes.size(); ++i) {
            const ClassBinding& c = table->classes[i];
            table->byName.emplace(c.name, i);
            table->byName.emplace(str::LowerAscii(c.name), i);
            table->byType.emplace(c.type, i);
        }
        // The script names the engine has always used for these classes (the hand-written builtins took them).
        static const char* const kAliases[][2] = {
            {"Rigidbody", "Rigidbody3D"}, {"Collider", "Collider3D"}, {"BoxCollider", "BoxCollider3D"},
            {"SphereCollider", "SphereCollider3D"}, {"CapsuleCollider", "CapsuleCollider3D"},
            {"MeshCollider", "MeshCollider3D"},
        };
        for (const auto& a : kAliases) {
            auto it = table->byName.find(a[1]);
            if (it != table->byName.end()) { table->byName.emplace(a[0], it->second); table->byName.emplace(str::LowerAscii(a[0]), it->second); }
        }
        return table;
    }();
    return *t;
}

const ClassBinding* ClassOfChecked(const Component& c) {
    Table& t = BuildTable();
    auto it = t.byType.find(std::type_index(typeid(c)));
    return it == t.byType.end() ? nullptr : &t.classes[it->second];
}

bool Nullish(const V& v) { return v.IsNumber() && v.AsFloat() == 0.0f; }

const char* KindOf(const V& v) {
    if (v.IsComponentHandle()) return "a component";
    if (v.IsObject()) return "an object";
    if (v.IsString()) return "text";
    if (v.IsBool()) return "true/false";
    if (v.IsNumber()) return "a number";
    if (v.IsVec3()) return "a vector";
    if (v.IsColor()) return "a colour";
    if (v.IsArray()) return "a list";
    if (v.IsMap()) return "a map";
    return "that value";
}

// Can a script value stand for an argument of this kind?
bool Fits(ArgKind k, const V& v) {
    switch (k) {
        case ArgKind::Bool: case ArgKind::Int: case ArgKind::Float: case ArgKind::Enum:
            return v.IsNumber() || v.IsBool();
        case ArgKind::String: return v.IsString();
        case ArgKind::Vec2: case ArgKind::Vec3: case ArgKind::Vec4: case ArgKind::Quat:
            return v.IsVec3();
        case ArgKind::Color: return v.IsColor() || v.IsVec3();
        case ArgKind::Object: return v.IsObject() || Nullish(v);
        default: return false;
    }
}

std::string Plural(int n) { return n == 1 ? "1 argument" : std::to_string(n) + " arguments"; }

std::string PropertyKindName(PropertyType t) {
    switch (t) {
        case PropertyType::Bool: return "true/false";
        case PropertyType::Int: case PropertyType::Float: return "a number";
        case PropertyType::Enum: return "an option name or number";
        case PropertyType::Vec2: case PropertyType::Vec3: return "a vector";
        case PropertyType::Quat: return "a rotation (euler angles as a vector)";
        case PropertyType::Color: return "a colour";
        case PropertyType::Vec4: return "a vector or colour";
        default: return "text";
    }
}

const PropertyDesc* FindProperty(const Component& c, const std::string& name, std::shared_ptr<const ComponentTypeInfo>& keep) {
    keep = PropertyRegistry::FindFor(c);
    if (!keep) return nullptr;
    return keep->Find(name);
}

} // namespace

const char* ArgKindName(ArgKind k) {
    switch (k) {
        case ArgKind::Void: return "nothing";
        case ArgKind::Bool: return "true/false";
        case ArgKind::Int: case ArgKind::Float: return "a number";
        case ArgKind::Enum: return "a number (an option)";
        case ArgKind::String: return "text";
        case ArgKind::Vec2: case ArgKind::Vec3: case ArgKind::Vec4: return "a vector";
        case ArgKind::Quat: return "a rotation (euler angles as a vector)";
        case ArgKind::Color: return "a colour";
        case ArgKind::Object: return "an object";
        case ArgKind::Component: return "a component";
    }
    return "a value";
}

void AddClass(ClassBinding c) {
    if (g_building) g_building->push_back(std::move(c));
}

const std::vector<ClassBinding>& AllClasses() { return BuildTable().classes; }

const ClassBinding* FindClass(const std::string& name) {
    Table& t = BuildTable();
    auto it = t.byName.find(name);
    if (it == t.byName.end()) it = t.byName.find(str::LowerAscii(name));
    if (it != t.byName.end()) return &t.classes[it->second];
    if (auto info = PropertyRegistry::FindByName(name)) {   // a scene-record name ("rigidbody2d")
        auto ti = t.byType.find(info->type);
        if (ti != t.byType.end()) return &t.classes[ti->second];
    }
    return nullptr;
}

const ClassBinding* ClassOf(const Component& c) { return ClassOfChecked(c); }

const ClassBinding* ClassByKey(std::uint32_t key) {
    Table& t = BuildTable();
    return key == 0 || key > t.classes.size() ? nullptr : &t.classes[key - 1];
}

std::uint32_t KeyOf(const ClassBinding& c) {
    Table& t = BuildTable();
    return static_cast<std::uint32_t>(&c - t.classes.data()) + 1;
}

Component* FindComponent(const GameObject& go, const ClassBinding& cls, int index) {
    int k = 0;
    for (const auto& up : go.Components())
        if (up && cls.is(*up) && k++ == index) return up.get();
    return nullptr;
}

V MakeHandle(Component* c, const ClassBinding* cls) {
    if (!c || !c->gameObject) return V{};
    if (!cls) cls = ClassOfChecked(*c);
    if (!cls || !cls->is(*c)) return V{};
    int index = 0;
    for (const auto& up : c->gameObject->Components()) {
        if (up.get() == c) break;
        if (up && cls->is(*up)) ++index;
    }
    return V::ComponentHandle(c->gameObject->InstanceId(), KeyOf(*cls), static_cast<std::uint32_t>(index));
}

bool IsHandle(const V& v) { return v.IsComponentHandle(); }

Component* Resolve(const V& v, const ClassBinding** clsOut) {
    if (!v.IsComponentHandle()) return nullptr;
    const vs::VsObject o = v.AsObjectRef();
    const ClassBinding* cls = ClassByKey(o.comp);
    if (clsOut) *clsOut = cls;
    GameObject* g = o.id ? GameObject::FromInstanceId(o.id) : nullptr;
    if (!g || !cls) return nullptr;
    return FindComponent(*g, *cls, static_cast<int>(o.index));
}

// ---- Property values ----------------------------------------------------------------------------------------------

V PropertyToScript(const PropertyValue& v) {
    switch (v.Type()) {
        case PropertyType::Bool:   return V(v.AsBool());
        case PropertyType::Int:    return V(static_cast<float>(v.AsInt()));
        case PropertyType::Float:  return V(v.AsFloat());
        case PropertyType::Enum:   return V(v.EnumName().empty() ? std::to_string(v.AsEnum()) : v.EnumName());
        case PropertyType::Vec2:   return V(Vec3{v.AsVec2().x, v.AsVec2().y, 0});
        case PropertyType::Vec3:   return V(v.AsVec3());
        case PropertyType::Color:  return V(Vec3{v.AsColor().r, v.AsColor().g, v.AsColor().b});
        default:                   return V(v.ToString());
    }
}

bool ScriptToProperty(const PropertyDesc& d, const V& v, PropertyValue& out, std::string& error) {
    auto bad = [&]() {
        error = d.name + " wants " + PropertyKindName(d.type) + ", got " + KindOf(v);
        return false;
    };
    const bool num = v.IsNumber() || v.IsBool();
    switch (d.type) {
        case PropertyType::Bool:  if (!num) return bad(); out = PropertyValue::MakeBool(v.AsBool()); return true;
        case PropertyType::Int:   if (!num) return bad(); out = PropertyValue::MakeInt(std::llround(static_cast<double>(v.AsFloat()))); return true;
        case PropertyType::Float: if (!num) return bad(); out = PropertyValue::MakeFloat(v.AsFloat()); return true;
        case PropertyType::Enum:
            if (num) { out = PropertyValue::MakeEnum(static_cast<int>(std::lround(v.AsFloat()))); return true; }
            if (v.IsString() && d.ParseText(v.AsString(), out)) return true;
            if (v.IsString()) { error = d.name + " has no option '" + v.AsString() + "'"; return false; }
            return bad();
        case PropertyType::Vec2:  if (!v.IsVec3()) return bad(); out = PropertyValue::MakeVec2(Vec2{v.AsVec3().x, v.AsVec3().y}); return true;
        case PropertyType::Vec3:  if (!v.IsVec3()) return bad(); out = PropertyValue::MakeVec3(v.AsVec3()); return true;
        case PropertyType::Quat:  if (!v.IsVec3()) return bad(); out = PropertyValue::MakeQuat(Quat::Euler(v.AsVec3())); return true;
        case PropertyType::Vec4:
            if (v.IsColor()) { auto c = v.AsColor(); out = PropertyValue::MakeVec4(Vec4{c.r, c.g, c.b, c.a}); return true; }
            if (v.IsVec3()) { auto c = v.AsVec3(); out = PropertyValue::MakeVec4(Vec4{c.x, c.y, c.z, 0.0f}); return true; }
            return bad();
        case PropertyType::Color:
            if (v.IsColor()) { auto c = v.AsColor(); out = PropertyValue::MakeColor(Color{c.r, c.g, c.b, c.a}); return true; }
            if (v.IsVec3()) { auto c = v.AsVec3(); out = PropertyValue::MakeColor(Color{c.x, c.y, c.z, 1.0f}); return true; }
            return bad();
        default:
            if (v.IsArray() || v.IsMap()) return bad();
            if (!d.ParseText(v.AsString(), out)) { error = d.name + " can't hold '" + v.AsString() + "'"; return false; }
            return true;
    }
}

// ---- Members ------------------------------------------------------------------------------------------------------

namespace {
std::string NullError(const ClassBinding* cls, const std::string& what) {
    return "can't " + what + ": the " + (cls ? cls->name : std::string("component")) +
           " is null (its object was destroyed or the component was removed)";
}
} // namespace

Result GetMember(const V& handle, const std::string& name, V& out, std::string& error) {
    const ClassBinding* cls = nullptr;
    Component* c = Resolve(handle, &cls);
    if (!c) { error = NullError(cls, "read '" + name + "'"); return Result::Error; }
    if (name == "gameObject") { out = V::Object(c->gameObject ? c->gameObject->InstanceId() : 0); return Result::Ok; }
    if (name == "enabled") { out = V{c->enabled}; return Result::Ok; }
    std::shared_ptr<const ComponentTypeInfo> info;
    if (const PropertyDesc* d = FindProperty(*c, name, info)) {
        out = PropertyToScript(d->Get(*c));
        return Result::Ok;
    }
    return Result::NotFound;   // the host may answer the object's own members (transform, name, tag)
}

Result SetMember(const V& handle, const std::string& name, const V& v, std::string& error) {
    const ClassBinding* cls = nullptr;
    Component* c = Resolve(handle, &cls);
    if (!c) { error = NullError(cls, "set '" + name + "'"); return Result::Error; }
    std::shared_ptr<const ComponentTypeInfo> info;
    const PropertyDesc* d = FindProperty(*c, name, info);
    if (!d) {
        if (name == "enabled") { c->enabled = v.AsBool(); return Result::Ok; }
        return Result::NotFound;
    }
    PropertyValue pv;
    if (!ScriptToProperty(*d, v, pv, error)) { error = cls->name + "." + error; return Result::Error; }
    if (d->IsReadOnly()) { error = cls->name + "." + d->name + " is read-only"; return Result::Error; }
    std::string why;
    if (!PropertyRegistry::SetComponentProperty(*c, d->name, pv, &why)) {
        error = "can't set " + cls->name + "." + d->name + (why.empty() ? std::string() : ": " + why);
        return Result::Error;
    }
    return Result::Ok;
}

Result CallOn(Component& c, const ClassBinding& cls, const std::string& method, const std::vector<V>& args, V& out,
              std::string& error) {
    const MethodBinding* firstNamed = nullptr;
    const int argc = static_cast<int>(args.size());
    for (int pass = 0; pass < 2; ++pass) {   // the exact name first, then any case
        for (const MethodBinding& m : cls.methods) {
            const bool same = pass == 0 ? method == m.name : str::EqualsNoCaseAscii(method, m.name);
            if (!same) continue;
            if (!firstNamed) firstNamed = &m;
            if (argc < m.required || argc > static_cast<int>(m.params.size())) continue;
            bool ok = true;
            for (int i = 0; i < argc && ok; ++i) ok = Fits(m.params[static_cast<std::size_t>(i)], args[static_cast<std::size_t>(i)]);
            if (!ok) continue;
            out = m.fn(c, args.data(), argc);
            return Result::Ok;
        }
        if (firstNamed) break;
    }
    if (!firstNamed) return Result::NotFound;
    // Say why the nearest overload did not fit.
    const MethodBinding& m = *firstNamed;
    const std::string sig = cls.name + "." + m.name + "()";
    if (argc < m.required || argc > static_cast<int>(m.params.size())) {
        const int lo = m.required, hi = static_cast<int>(m.params.size());
        error = sig + " takes " + (lo == hi ? Plural(lo) : std::to_string(lo) + " to " + Plural(hi)) + ", got " + std::to_string(argc);
        return Result::Error;
    }
    for (int i = 0; i < argc; ++i) {
        if (!Fits(m.params[static_cast<std::size_t>(i)], args[static_cast<std::size_t>(i)])) {
            error = sig + ": argument " + std::to_string(i + 1) + " must be " + ArgKindName(m.params[static_cast<std::size_t>(i)]) +
                    ", got " + KindOf(args[static_cast<std::size_t>(i)]);
            return Result::Error;
        }
    }
    error = sig + ": no version of it takes these arguments";
    return Result::Error;
}

Result CallMethod(const V& handle, const std::string& method, const std::vector<V>& args, V& out, std::string& error) {
    const ClassBinding* cls = nullptr;
    Component* c = Resolve(handle, &cls);
    if (!c) { error = NullError(cls, "call '" + method + "'"); return Result::Error; }
    // The class the handle was made as, then the component's own (a Collider3D handle to a BoxCollider3D).
    Result r = CallOn(*c, *cls, method, args, out, error);
    if (r != Result::NotFound) return r;
    if (const ClassBinding* own = ClassOfChecked(*c); own && own != cls) return CallOn(*c, *own, method, args, out, error);
    return r;
}

// ---- Helpers for the generated code -------------------------------------------------------------------------------

long long ArgInt(const V& v) {
    const double d = static_cast<double>(v.AsFloat());
    if (!std::isfinite(d)) return 0;
    return std::llround(std::max(-9.0e18, std::min(9.0e18, d)));
}
Vec2 ArgVec2(const V& v) { const Vec3 p = v.AsVec3(); return Vec2{p.x, p.y}; }
Vec4 ArgVec4(const V& v) {
    if (v.IsColor()) { auto c = v.AsColor(); return Vec4{c.r, c.g, c.b, c.a}; }
    const Vec3 p = v.AsVec3();
    return Vec4{p.x, p.y, p.z, 0.0f};
}
Quat ArgQuat(const V& v) { return Quat::Euler(v.AsVec3()); }
Color ArgColor(const V& v) { auto c = v.AsColor(); return Color{c.r, c.g, c.b, c.a}; }
GameObject* ArgObject(const V& v) {
    if (!v.IsObject()) return nullptr;
    const std::uint64_t id = v.AsObjectId();
    return id ? GameObject::FromInstanceId(id) : nullptr;
}
V RetVec2(const Vec2& v) { return V{Vec3{v.x, v.y, 0.0f}}; }
V RetVec4(const Vec4& v) { return V::Color(v.x, v.y, v.z, v.w); }
V RetQuat(const Quat& q) { return V{q.ToEuler()}; }
V RetColor(const Color& c) { return V::Color(c.r, c.g, c.b, c.a); }
V RetObject(const GameObject* g) { return g ? V::Object(g->InstanceId()) : V{}; }
V RetComponent(const Component* c) { return MakeHandle(const_cast<Component*>(c)); }

} // namespace okay::scriptbind

namespace okay::vs {
bool VsComponentAlive(const VsObject& o) {
    return scriptbind::Resolve(VsValue::ComponentHandle(o.id, o.comp, o.index)) != nullptr;
}
std::string VsComponentName(const VsObject& o) {
    const scriptbind::ClassBinding* cls = scriptbind::ClassByKey(o.comp);
    GameObject* g = o.id ? GameObject::FromInstanceId(o.id) : nullptr;
    return (cls ? cls->name : std::string("Component")) + "(" + (g ? g->name : std::string()) + ")";
}
} // namespace okay::vs
