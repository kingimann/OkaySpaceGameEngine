#pragma once
// The short way to describe a component's properties: point at its public
// fields and chain hints. Used by the built-in registrations (BuiltinProperties.cpp)
// and available to scripts' native components and plugins:
//
//   PropertyBuilder<Torch>("Torch", "torch", "Gameplay")
//       .AddFloat("brightness", &Torch::brightness).Range(0, 10, 0.1).Tooltip("Light output")
//       .AddEnum("mode", &Torch::mode, {"Off", "Steady", "Flicker"})
//       .AddColor("tint", &Torch::tint).Group("Look")
//       .Commit();
//
// The setters see a value that has already been checked and clamped by the
// descriptor, so a field is simply assigned.
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/GameObject.hpp"
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace okay {

/// Describes component type C (or, with C = GameObject, the pseudo component of the
/// object's own fields). Every Add* takes a pointer to a public field; the field may
/// live in a base class of C. Modifiers (Group, Tooltip, Range ...) apply to the
/// property added last.
template <class C>
class PropertyBuilder {
public:
    /// `typeName` is the C++ name as ComponentSerializerRegistry::TypeKey gives it,
    /// `recordName` the scene-file record ("" if there is none). Every component
    /// starts with an "enabled" property (Component::enabled) unless `withEnabled`
    /// is false.
    PropertyBuilder(std::string typeName, std::string recordName = {}, std::string category = {}, bool withEnabled = true) {
        m_info.name = std::move(typeName);
        m_info.recordName = std::move(recordName);
        m_info.category = std::move(category);
        m_info.type = std::type_index(typeid(C));
        if constexpr (std::is_same<C, GameObject>::value) {
            m_info.objectLevel = true;
        } else {
            static_assert(std::is_base_of<Component, C>::value, "C must derive from okay::Component (or be GameObject)");
            m_info.add = [](GameObject& go) -> Component* { return go.AddComponent<C>(); };
            if (withEnabled) {
                Prop("enabled", PropertyType::Bool,
                     [](const Component& c) { return PropertyValue::MakeBool(c.enabled); },
                     [](Component& c, const PropertyValue& v) { c.enabled = v.AsBool(); return true; });
            }
        }
    }

    // ---- Typed fields ---------------------------------------------------------
    template <class Owner> PropertyBuilder& AddBool(const char* name, bool Owner::*m) {
        return Prop(name, PropertyType::Bool,
            [m](const Component& c) { return PropertyValue::MakeBool(As<Owner>(c).*m); },
            [m](Component& c, const PropertyValue& v) { As<Owner>(c).*m = v.AsBool(); return true; });
    }
    /// Any integer field. The range starts at the field type's own limits (so a
    /// uint8 or a uint32 can never be set to something it cannot hold); narrow it with Range().
    template <class Owner, class I> PropertyBuilder& AddInt(const char* name, I Owner::*m) {
        static_assert(std::is_integral<I>::value && !std::is_same<I, bool>::value, "AddInt needs an integer field");
        Prop(name, PropertyType::Int,
            [m](const Component& c) { return PropertyValue::MakeInt(static_cast<long long>(As<Owner>(c).*m)); },
            [m](Component& c, const PropertyValue& v) { As<Owner>(c).*m = static_cast<I>(v.AsInt()); return true; });
        PropertyDesc& d = m_info.properties.back();
        d.hasRange = true;
        d.rangeMin = static_cast<double>(std::numeric_limits<I>::lowest());
        d.rangeMax = static_cast<double>(std::numeric_limits<I>::max());
        if (d.rangeMax > 9.2e18) d.rangeMax = 9.2e18;   // a long long holds no more
        return *this;
    }
    template <class Owner> PropertyBuilder& AddFloat(const char* name, float Owner::*m) {
        return Prop(name, PropertyType::Float,
            [m](const Component& c) { return PropertyValue::MakeFloat(As<Owner>(c).*m); },
            [m](Component& c, const PropertyValue& v) { As<Owner>(c).*m = v.AsFloat(); return true; });
    }
    template <class Owner> PropertyBuilder& AddString(const char* name, std::string Owner::*m) {
        return Prop(name, PropertyType::String,
            [m](const Component& c) { return PropertyValue::MakeString(As<Owner>(c).*m); },
            [m](Component& c, const PropertyValue& v) { As<Owner>(c).*m = v.AsString(); return true; });
    }
    /// A path to a file. `assetKind` hints the picker ("image", "audio", "model", ...).
    template <class Owner> PropertyBuilder& AddAssetPath(const char* name, std::string Owner::*m, const char* assetKind = "") {
        Prop(name, PropertyType::AssetPath,
            [m](const Component& c) { return PropertyValue::MakeAssetPath(As<Owner>(c).*m); },
            [m](Component& c, const PropertyValue& v) { As<Owner>(c).*m = v.AsString(); return true; });
        m_info.properties.back().assetKind = assetKind ? assetKind : "";
        return *this;
    }
    template <class Owner> PropertyBuilder& AddVec2(const char* name, Vec2 Owner::*m) {
        return Prop(name, PropertyType::Vec2,
            [m](const Component& c) { return PropertyValue::MakeVec2(As<Owner>(c).*m); },
            [m](Component& c, const PropertyValue& v) { As<Owner>(c).*m = v.AsVec2(); return true; });
    }
    template <class Owner> PropertyBuilder& AddVec3(const char* name, Vec3 Owner::*m) {
        return Prop(name, PropertyType::Vec3,
            [m](const Component& c) { return PropertyValue::MakeVec3(As<Owner>(c).*m); },
            [m](Component& c, const PropertyValue& v) { As<Owner>(c).*m = v.AsVec3(); return true; });
    }
    template <class Owner> PropertyBuilder& AddVec4(const char* name, Vec4 Owner::*m) {
        return Prop(name, PropertyType::Vec4,
            [m](const Component& c) { return PropertyValue::MakeVec4(As<Owner>(c).*m); },
            [m](Component& c, const PropertyValue& v) { As<Owner>(c).*m = v.AsVec4(); return true; });
    }
    template <class Owner> PropertyBuilder& AddQuat(const char* name, Quat Owner::*m) {
        return Prop(name, PropertyType::Quat,
            [m](const Component& c) { return PropertyValue::MakeQuat(As<Owner>(c).*m); },
            [m](Component& c, const PropertyValue& v) { As<Owner>(c).*m = v.AsQuat(); return true; });
    }
    template <class Owner> PropertyBuilder& AddColor(const char* name, Color Owner::*m) {
        return Prop(name, PropertyType::Color,
            [m](const Component& c) { return PropertyValue::MakeColor(As<Owner>(c).*m); },
            [m](Component& c, const PropertyValue& v) { As<Owner>(c).*m = v.AsColor(); return true; });
    }
    /// An `enum class` (or plain int) field whose values are 0, 1, 2 ... in the order of `options`.
    template <class Owner, class E> PropertyBuilder& AddEnum(const char* name, E Owner::*m, std::vector<std::string> options) {
        Prop(name, PropertyType::Enum,
            [m](const Component& c) { return PropertyValue::MakeEnum(static_cast<int>(As<Owner>(c).*m)); },
            [m](Component& c, const PropertyValue& v) { As<Owner>(c).*m = static_cast<E>(v.AsEnum()); return true; });
        m_info.properties.back().enumOptions = std::move(options);
        return *this;
    }

    // ---- Fields of a nested struct ---------------------------------------------------
    // A field of a struct member: `AddFloat("agentRadius", &Surface::settings, &Settings::agentRadius)`.
    template <class Owner, class Inner> PropertyBuilder& AddBool(const char* name, Inner Owner::*outer, bool Inner::*inner) {
        return Prop(name, PropertyType::Bool,
            [outer, inner](const Component& c) { return PropertyValue::MakeBool((As<Owner>(c).*outer).*inner); },
            [outer, inner](Component& c, const PropertyValue& v) { (As<Owner>(c).*outer).*inner = v.AsBool(); return true; });
    }
    template <class Owner, class Inner, class I> PropertyBuilder& AddInt(const char* name, Inner Owner::*outer, I Inner::*inner) {
        static_assert(std::is_integral<I>::value && !std::is_same<I, bool>::value, "AddInt needs an integer field");
        Prop(name, PropertyType::Int,
            [outer, inner](const Component& c) { return PropertyValue::MakeInt(static_cast<long long>((As<Owner>(c).*outer).*inner)); },
            [outer, inner](Component& c, const PropertyValue& v) { (As<Owner>(c).*outer).*inner = static_cast<I>(v.AsInt()); return true; });
        PropertyDesc& d = m_info.properties.back();
        d.hasRange = true;
        d.rangeMin = static_cast<double>(std::numeric_limits<I>::lowest());
        d.rangeMax = static_cast<double>(std::numeric_limits<I>::max());
        if (d.rangeMax > 9.2e18) d.rangeMax = 9.2e18;
        return *this;
    }
    template <class Owner, class Inner> PropertyBuilder& AddFloat(const char* name, Inner Owner::*outer, float Inner::*inner) {
        return Prop(name, PropertyType::Float,
            [outer, inner](const Component& c) { return PropertyValue::MakeFloat((As<Owner>(c).*outer).*inner); },
            [outer, inner](Component& c, const PropertyValue& v) { (As<Owner>(c).*outer).*inner = v.AsFloat(); return true; });
    }
    template <class Owner, class Inner> PropertyBuilder& AddVec3(const char* name, Inner Owner::*outer, Vec3 Inner::*inner) {
        return Prop(name, PropertyType::Vec3,
            [outer, inner](const Component& c) { return PropertyValue::MakeVec3((As<Owner>(c).*outer).*inner); },
            [outer, inner](Component& c, const PropertyValue& v) { (As<Owner>(c).*outer).*inner = v.AsVec3(); return true; });
    }

    // ---- Key bindings ------------------------------------------------------------------
    /// A key binding held in a `char` field: an Int property 0..255 holding the key code
    /// (0 = none; 1..31 are the special keys such as Shift, Ctrl and the arrows, see Input.hpp).
    /// Pass `rawCharRecord` when the file format writes the key as one raw character: white
    /// space and '-' (its "none") cannot be stored there, so those codes are refused.
    template <class Owner> PropertyBuilder& AddKey(const char* name, char Owner::*m, bool rawCharRecord = false) {
        return AddKeyImpl(name, [m](const Component& c) { return As<Owner>(c).*m; },
                          [m](Component& c, char k) { As<Owner>(c).*m = k; }, rawCharRecord);
    }
    /// The same for a `char` inside a struct member: `AddKey("dodgeKey", &C::dodge, &Dodge::key)`.
    template <class Owner, class Inner> PropertyBuilder& AddKey(const char* name, Inner Owner::*outer, char Inner::*inner, bool rawCharRecord = false) {
        return AddKeyImpl(name, [outer, inner](const Component& c) { return (As<Owner>(c).*outer).*inner; },
                          [outer, inner](Component& c, char k) { (As<Owner>(c).*outer).*inner = k; }, rawCharRecord);
    }

    // ---- Computed properties -----------------------------------------------------
    /// A property with its own accessors, for values that are not one plain field.
    /// `setter` gets a value already checked against the descriptor and may refuse.
    PropertyBuilder& Custom(const char* name, PropertyType type,
                            std::function<PropertyValue(const C&)> getter,
                            std::function<bool(C&, const PropertyValue&)> setter) {
        if (!getter) return *this;   // nothing to read: not a property (modifiers then apply to the one before)
        return Prop(name, type,
            [getter](const Component& c) { return getter(As<C>(c)); },
            [setter](Component& c, const PropertyValue& v) { return setter ? setter(As<C>(c), v) : false; });
    }
    /// Same, for an Enum with option names.
    PropertyBuilder& CustomEnum(const char* name, std::vector<std::string> options,
                                std::function<int(const C&)> getter, std::function<bool(C&, int)> setter) {
        if (!getter) return *this;
        Prop(name, PropertyType::Enum,
            [getter](const Component& c) { return PropertyValue::MakeEnum(getter(As<C>(c))); },
            [setter](Component& c, const PropertyValue& v) { return setter ? setter(As<C>(c), v.AsEnum()) : false; });
        m_info.properties.back().enumOptions = std::move(options);
        return *this;
    }

    // ---- Modifiers for the property added last ---------------------------------------
    PropertyBuilder& Group(const char* group) { Last().group = group; return *this; }
    PropertyBuilder& Tooltip(const char* text) { Last().tooltip = text; return *this; }
    /// Numeric limits (each component of a vector or color); out-of-range values are clamped.
    PropertyBuilder& Range(double minValue, double maxValue, double step = 0.0) {
        PropertyDesc& d = Last(); d.hasRange = true; d.rangeMin = minValue; d.rangeMax = maxValue; d.step = step; return *this;
    }
    /// Refuse an out-of-range value instead of clamping it.
    PropertyBuilder& RejectOutOfRange() { Last().rejectOutOfRange = true; return *this; }
    PropertyBuilder& Flags(unsigned flags) { Last().flags |= flags; return *this; }
    PropertyBuilder& ReadOnly() { return Flags(PropertyFlag::ReadOnly); }
    PropertyBuilder& Hidden() { return Flags(PropertyFlag::HideInInspector); }
    PropertyBuilder& RuntimeOnly() { return Flags(PropertyFlag::RuntimeOnly); }
    PropertyBuilder& Derived() { return Flags(PropertyFlag::Derived); }
    PropertyBuilder& Structural() { return Flags(PropertyFlag::Structural); }
    PropertyBuilder& Multiline() { Last().multiline = true; return *this; }
    PropertyBuilder& Default(const PropertyValue& v) { Last().defaultValue = v; return *this; }
    /// Properties added from here on go in this inspector section (until changed).
    PropertyBuilder& Section(const char* group) { m_section = group ? group : ""; return *this; }

    /// The component cannot be added through the registry (a Transform).
    PropertyBuilder& NoAdd() { m_info.add = nullptr; return *this; }
    /// How the registry adds this component to an object, when `AddComponent<C>()` alone is
    /// not enough (a controller that also needs its parts). Return null when it cannot be added.
    PropertyBuilder& Adder(std::function<Component*(GameObject&)> add) { m_info.add = std::move(add); return *this; }

    /// Fill in default values from a freshly made component (when C can be made
    /// without arguments), then register.
    void Commit() {
        if constexpr (std::is_base_of<Component, C>::value && std::is_default_constructible<C>::value) {
            C sample;
            for (PropertyDesc& d : m_info.properties)
                if (!d.defaultValue.IsValid() && d.getter) d.defaultValue = d.Get(sample);
        }
        PropertyRegistry::Register(std::move(m_info));
    }

private:
    template <class T> static const T& As(const Component& c) {
        if constexpr (std::is_same<T, GameObject>::value) return *c.gameObject;
        else return static_cast<const T&>(c);
    }
    template <class T> static T& As(Component& c) {
        if constexpr (std::is_same<T, GameObject>::value) return *c.gameObject;
        else return static_cast<T&>(c);
    }
    PropertyBuilder& Prop(const char* name, PropertyType type,
                          std::function<PropertyValue(const Component&)> getter,
                          std::function<bool(Component&, const PropertyValue&)> setter) {
        PropertyDesc d;
        d.name = name;
        d.type = type;
        d.group = m_section;
        d.getter = std::move(getter);
        d.setter = std::move(setter);
        m_info.properties.push_back(std::move(d));
        return *this;
    }
    template <class Get, class Set>
    PropertyBuilder& AddKeyImpl(const char* name, Get get, Set set, bool rawCharRecord) {
        Prop(name, PropertyType::Int,
            [get](const Component& c) { return PropertyValue::MakeInt(static_cast<int>(static_cast<unsigned char>(get(c)))); },
            [set, rawCharRecord](Component& c, const PropertyValue& v) {
                const int code = static_cast<int>(v.AsInt());
                if (rawCharRecord && (code == '-' || code == ' ' || (code >= 9 && code <= 13))) return false;
                set(c, static_cast<char>(code));
                return true;
            });
        PropertyDesc& d = m_info.properties.back();
        d.hasRange = true; d.rangeMin = 0.0; d.rangeMax = 255.0; d.step = 1.0;
        d.tooltip = "Key code (0 = none)";
        return *this;
    }
    // The property the modifiers apply to. Before any property was added there is none:
    // the modifier then lands on a scratch descriptor that is thrown away.
    PropertyDesc& Last() { return m_info.properties.empty() ? m_scratch : m_info.properties.back(); }

    ComponentTypeInfo m_info;
    std::string m_section;
    PropertyDesc m_scratch;
};

} // namespace okay
