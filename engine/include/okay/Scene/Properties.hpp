#pragma once
// Property reflection, part 1: PropertyValue (a small tagged value that converts
// to and from text exactly) and PropertyDesc (how one named property of a
// component is read, validated and written). The registry that holds the
// descriptions, the change hook and the snapshot/diff utilities are in
// PropertyRegistry.hpp. See docs/properties.md.
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Vec4.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Render/Color.hpp"
#include <functional>
#include <string>
#include <vector>

namespace okay {

class Component;

/// What a property holds. `None` is the "no value" state of a default-constructed
/// PropertyValue (a failed lookup returns one).
enum class PropertyType { None, Bool, Int, Float, String, Vec2, Vec3, Vec4, Quat, Color, Enum, AssetPath, ObjectRef };

/// "Float", "Vec3", ... and back. The name is what the text form of a descriptor uses.
const char* PropertyTypeName(PropertyType t);
bool PropertyTypeFromName(const std::string& name, PropertyType& out);
/// How many float components a type has (Vec2 2, Vec3 3, Vec4/Quat/Color 4); 0 for the rest.
int PropertyTypeComponentCount(PropertyType t);

/// A property's value. Plain data: copy it freely. Nothing here throws; a getter
/// asked for the wrong type returns the type's zero value.
///
/// Text form (ToString / FromString), exact for every type:
///   Bool       true / false (FromString also takes 1 0 yes no on off, any case)
///   Int        decimal
///   Float      the shortest decimal that reads back to the same float; nan, inf, -inf
///   String, AssetPath, ObjectRef   the text itself, unquoted
///   Vec2/3/4, Quat, Color          components separated by one space ("1 2.5 -3")
///                                  (FromString also takes commas and one pair of
///                                  brackets; a Color also takes #RRGGBB / #RRGGBBAA)
///   Enum       the integer, then the option name when there is one ("1 Point")
/// FromString returns false (and leaves `out` alone) on anything it cannot read in
/// full: empty or extra text, a bad number, a number out of range, a wrong count.
class PropertyValue {
public:
    PropertyValue() = default;

    static PropertyValue MakeBool(bool v);
    static PropertyValue MakeInt(long long v);
    static PropertyValue MakeFloat(float v);
    static PropertyValue MakeString(std::string v);
    static PropertyValue MakeVec2(const Vec2& v);
    static PropertyValue MakeVec3(const Vec3& v);
    static PropertyValue MakeVec4(const Vec4& v);
    static PropertyValue MakeQuat(const Quat& v);
    static PropertyValue MakeColor(const Color& v);
    /// `name` is informational (the registry fills it from the descriptor's options).
    static PropertyValue MakeEnum(int value, std::string name = {});
    static PropertyValue MakeAssetPath(std::string path);
    /// `guid` is a GameObject::Guid(); empty means "no object".
    static PropertyValue MakeObjectRef(std::string guid);

    PropertyType Type() const { return m_type; }
    bool IsValid() const { return m_type != PropertyType::None; }

    bool AsBool() const { return m_type == PropertyType::Bool && m_i != 0; }
    long long AsInt() const { return m_type == PropertyType::Int ? m_i : 0; }
    float AsFloat() const { return m_type == PropertyType::Float ? m_f[0] : 0.0f; }
    /// The text of a String, AssetPath or ObjectRef (empty for other types).
    const std::string& AsString() const;
    Vec2 AsVec2() const;
    Vec3 AsVec3() const;
    Vec4 AsVec4() const;
    Quat AsQuat() const;
    Color AsColor() const;
    int AsEnum() const { return m_type == PropertyType::Enum ? static_cast<int>(m_i) : 0; }
    const std::string& EnumName() const;

    /// Exact text form (see above). An invalid value gives "".
    std::string ToString() const;
    static bool FromString(PropertyType type, const std::string& text, PropertyValue& out);

    /// One float of a Vec2/Vec3/Vec4/Quat/Color by sub-field name ("x" "y" "z" "w",
    /// or "r" "g" "b" "a" for a Color; any case). False for another type or name.
    static bool SubFieldIndex(PropertyType type, const std::string& sub, int& index);
    /// A sub-field as a Float value; false when `sub` does not name one.
    bool GetSubField(const std::string& sub, PropertyValue& out) const;
    /// Replace one sub-field with a Float value. False (unchanged) on a wrong
    /// type or name. The result is not validated: pass it through PropertyDesc::Set.
    bool WithSubField(const std::string& sub, const PropertyValue& component, PropertyValue& inOut) const;

    /// Same type and same payload. Floats compare as numbers (so 0 == -0) and
    /// two NaNs count as equal; an Enum compares its number only.
    bool operator==(const PropertyValue& o) const;
    bool operator!=(const PropertyValue& o) const { return !(*this == o); }

private:
    PropertyType m_type = PropertyType::None;
    long long m_i = 0;       // Bool, Int, Enum
    float m_f[4] = {0, 0, 0, 0};
    std::string m_s;         // String, AssetPath, ObjectRef; the Enum option name
};

/// Property flags (combine with |).
namespace PropertyFlag {
    /// Shown but not editable: Set always returns false.
    constexpr unsigned ReadOnly = 1u << 0;
    /// An inspector should not list it (it is still reachable by path).
    constexpr unsigned HideInInspector = 1u << 1;
    /// Live state, not authored data (a velocity, a sleep flag): the scene file does
    /// not keep it and snapshots leave it out unless asked.
    constexpr unsigned RuntimeOnly = 1u << 2;
    /// Computed from other properties (Transform.eulerAngles from the rotation):
    /// settable, but left out of snapshots and copies so it is not stored twice.
    constexpr unsigned Derived = 1u << 3;
    /// Changes the hierarchy, not just a value (Transform.parent). Snapshots keep
    /// it; CopyProperties skips it unless asked.
    constexpr unsigned Structural = 1u << 4;
}

/// One property of a component type: its name and type, the hints an inspector
/// needs, and how to read and write it. Built by PropertyBuilder (see
/// PropertyRegistry.hpp) or by hand.
struct PropertyDesc {
    std::string name;                 ///< "intensity"
    PropertyType type = PropertyType::None;
    std::string group;                ///< inspector section ("Shadows"); empty = none
    std::string tooltip;

    /// Numeric limits (Int, Float, and each component of Vec2/3/4 and Color).
    /// Ignored for the other types.
    bool   hasRange = false;
    double rangeMin = 0.0, rangeMax = 0.0;
    double step = 0.0;                ///< drag/step hint for UI; 0 = none
    /// Out of range: false clamps into the range, true makes Set refuse (return false).
    bool   rejectOutOfRange = false;

    /// Option names of an Enum property; the value of option i is i. Set refuses a
    /// value with no option (when there are options).
    std::vector<std::string> enumOptions;
    /// What an AssetPath points at ("image", "audio", "model", ...), for a picker.
    std::string assetKind;
    /// String properties may hold line breaks and other control characters only
    /// when this is set (a record in the scene file is one line).
    bool multiline = false;

    unsigned flags = 0;               ///< PropertyFlag bits
    /// The value of a freshly made component, when known (else invalid).
    PropertyValue defaultValue;

    /// Read the value from a component of the registered type.
    std::function<PropertyValue(const Component&)> getter;
    /// Store an already validated value. May refuse (return false), e.g. a tag
    /// the project does not allow.
    std::function<bool(Component&, const PropertyValue&)> setter;

    bool IsReadOnly() const { return (flags & PropertyFlag::ReadOnly) != 0; }
    bool IsHidden() const { return (flags & PropertyFlag::HideInInspector) != 0; }
    bool IsRuntimeOnly() const { return (flags & PropertyFlag::RuntimeOnly) != 0; }
    bool IsDerived() const { return (flags & PropertyFlag::Derived) != 0; }

    /// The current value (an Enum carries its option name). Invalid without a getter.
    PropertyValue Get(const Component& c) const;
    /// Check a value against this descriptor and give back what would be stored:
    /// the type must match exactly, floats must be finite, numbers are clamped (or
    /// refused when rejectOutOfRange), a Quat is normalised (a zero one refused), an
    /// Enum must name an option, strings may not hold control characters unless
    /// `multiline`, an ObjectRef must be empty or a guid. False leaves `out` alone.
    bool Normalize(const PropertyValue& in, PropertyValue& out) const;
    /// Normalize, then store. False when read-only, when the value is refused, or
    /// when the setter refuses; the component is then untouched.
    bool Set(Component& c, const PropertyValue& v) const;

    /// Read text in this property's type. Like PropertyValue::FromString, plus an
    /// Enum takes an option name ("Point", any case) as well as a number. For an Enum
    /// written as "number name" the two must agree: "1 Point" when option 1 is not Point
    /// is refused (a name that is no option any more leaves the number to decide).
    bool ParseText(const std::string& text, PropertyValue& out) const;
    /// The friendly text of a value: an Enum's option name, else ToString().
    std::string FormatText(const PropertyValue& v) const;

    /// Number of sub-fields ("x" "y" ...) a path may name after the property.
    int SubFieldCount() const { return PropertyTypeComponentCount(type); }
};

} // namespace okay
