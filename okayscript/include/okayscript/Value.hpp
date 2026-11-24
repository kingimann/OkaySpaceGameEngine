#pragma once
// The standalone OkayScript value: a number, true/false, text, a 3-number vector, a colour, a handle (a reference to
// something the host owns, by id), a shared list or a shared map. C++17 standard library only.
//
// It has the same interface and the same conversion rules as the engine's script value (vs::VsValue,
// engine/include/okay/VisualScript/VsValue.hpp), so the one interpreter core (Runtime.hpp, a template over the value
// type) runs either. Numbers are 32-bit floats (`Number`), the engine's choice. There is no separate null: a missing
// value is the number 0.
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace okayscript {

/// The one number type of the language: 32-bit float, the engine's choice (so 16777217 is not representable).
using Number = float;

/// A 3-number vector (Vector3 in scripts).
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit constexpr Vec3(float s) : x(s), y(s), z(s) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float Magnitude() const { return std::sqrt(x * x + y * y + z * z); }
    static const Vec3 Zero;
};
inline const Vec3 Vec3::Zero{};

/// A reference to an object the host owns (an instance id; 0 = none). The host says what it is called and whether
/// it still exists (SetHandleHooks); without hooks a handle has no name and is alive while its id is not 0.
struct Handle { std::uint64_t id = 0; };
/// An RGBA colour, each channel 0..1. Prints as #rrggbbaa.
struct Color4 { float r = 0, g = 0, b = 0, a = 1; };

using HandleNameFn = std::string (*)(std::uint64_t);
using HandleAliveFn = bool (*)(std::uint64_t);
void SetHandleHooks(HandleNameFn name, HandleAliveFn alive);
std::string HandleName(std::uint64_t id);
bool HandleAlive(std::uint64_t id);

class Value {
public:
    using Array = std::vector<Value>;
    using ArrayRef = std::shared_ptr<Array>;
    using Map = std::map<std::string, Value>;
    using MapRef = std::shared_ptr<Map>;

    Value() : m_data(Number{0}) {}
    Value(float v) : m_data(v) {}
    Value(double v) : m_data(static_cast<Number>(v)) {}
    Value(int v) : m_data(static_cast<Number>(v)) {}
    Value(bool v) : m_data(v) {}
    Value(const Vec3& v) : m_data(v) {}
    Value(std::string v) : m_data(std::move(v)) {}
    Value(const char* v) : m_data(std::string(v)) {}
    Value(ArrayRef a) : m_data(std::move(a)) {}
    Value(MapRef m) : m_data(std::move(m)) {}
    Value(Handle o) : m_data(o) {}
    Value(Color4 c) : m_data(c) {}

    static Value Color(float r, float g, float b, float a = 1.0f) { return Value(Color4{r, g, b, a}); }
    static Value Object(std::uint64_t id) { return Value(Handle{id}); }
    static Value MakeArray() { return Value(std::make_shared<Array>()); }
    static Value MakeMap() { return Value(std::make_shared<Map>()); }
    static Value List(std::vector<Value> items) { return Value(std::make_shared<Array>(std::move(items))); }

    bool IsNumber() const { return std::holds_alternative<Number>(m_data); }
    bool IsBool() const { return std::holds_alternative<bool>(m_data); }
    bool IsString() const { return std::holds_alternative<std::string>(m_data); }
    bool IsVec3() const { return std::holds_alternative<Vec3>(m_data); }
    bool IsArray() const { return std::holds_alternative<ArrayRef>(m_data); }
    bool IsMap() const { return std::holds_alternative<MapRef>(m_data); }
    bool IsObject() const { return std::holds_alternative<Handle>(m_data); }
    bool IsColor() const { return std::holds_alternative<Color4>(m_data); }

    ArrayRef AsArray() const;
    MapRef AsMap() const;
    std::uint64_t AsObjectId() const;
    Color4 AsColor() const;
    /// A number: true is 1, a vector its length, a colour its alpha, a list or map its size, numeric text that number.
    float AsFloat() const;
    double AsNumber() const { return AsFloat(); }
    /// True for true, a non-zero number, a non-empty list or map, a live handle, and text other than ""/"false"/"no"/"0".
    bool AsBool() const;
    Vec3 AsVec3() const;
    /// Numbers without trailing zeros ("3", "1.5"), vectors "(1, 2, 3)", colours "#rrggbbaa", lists "[a, b]",
    /// maps "{k: v}", a handle its name.
    std::string AsString() const;
    /// "number", "bool", "string", "vector", "colour", "object", "list", "map" or "function".
    const char* TypeName() const;

    static std::string HexColor(const Color4& c);
    static std::string FormatNumber(float f);
    /// `s` as a finite number when the whole string (apart from surrounding spaces) is one.
    static bool ParseFinite(const std::string& s, float& out);

private:
    std::variant<Number, bool, Vec3, std::string, ArrayRef, MapRef, Handle, Color4> m_data;
};

} // namespace okayscript
