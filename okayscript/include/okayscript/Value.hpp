#pragma once
// An OkayScript value: a number, true/false, text, a shared list or a shared map. C++17 standard library only.
//
// The conversion rules (AsNumber, AsBool, AsString) are the ones the engine's script value (vs::VsValue,
// engine/include/okay/VisualScript/VsValue.hpp) uses, written again here without its engine-only kinds (vectors,
// colours, object references). One difference: numbers are 64-bit doubles here; the engine keeps 32-bit floats.
// There is no separate null: a missing value is the number 0, exactly as in the engine (IsNullish below).
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace okayscript {

class Value {
public:
    using Array = std::vector<Value>;
    using ArrayRef = std::shared_ptr<Array>;
    using Map = std::map<std::string, Value>;
    using MapRef = std::shared_ptr<Map>;

    Value() : m_data(0.0) {}
    Value(double v) : m_data(v) {}
    Value(float v) : m_data(static_cast<double>(v)) {}
    Value(int v) : m_data(static_cast<double>(v)) {}
    Value(long long v) : m_data(static_cast<double>(v)) {}
    Value(bool v) : m_data(v) {}
    Value(std::string v) : m_data(std::move(v)) {}
    Value(const char* v) : m_data(std::string(v)) {}
    Value(ArrayRef a) : m_data(std::move(a)) {}
    Value(MapRef m) : m_data(std::move(m)) {}

    static Value MakeArray() { return Value(std::make_shared<Array>()); }
    static Value MakeMap() { return Value(std::make_shared<Map>()); }
    static Value List(std::vector<Value> items) { return Value(std::make_shared<Array>(std::move(items))); }

    bool IsNumber() const { return std::holds_alternative<double>(m_data); }
    bool IsBool() const { return std::holds_alternative<bool>(m_data); }
    bool IsString() const { return std::holds_alternative<std::string>(m_data); }
    bool IsArray() const { return std::holds_alternative<ArrayRef>(m_data); }
    bool IsMap() const { return std::holds_alternative<MapRef>(m_data); }

    /// The shared list (nullptr when this is not a list).
    ArrayRef AsArray() const;
    /// The shared map (nullptr when this is not a map).
    MapRef AsMap() const;
    /// A number: true is 1, text that is a whole number ("3", " -1.5 ") is that number, a list or map is its size,
    /// anything else 0.
    double AsNumber() const;
    /// True for true, a non-zero number, non-empty text other than "false"/"no"/"0", a non-empty list or map.
    bool AsBool() const;
    /// The value as text: numbers without trailing zeros ("3", "1.5"), lists as [a, b], maps as {k: v}.
    std::string AsString() const;

    /// "number", "bool", "string", "list", "map" or "function" (a lambda or a function reference).
    const char* TypeName() const;

    /// The engine's number format: whole numbers as integers ("3"), fractions with trailing zeros trimmed ("1.5").
    static std::string FormatNumber(double f);
    /// `s` as a finite number when the whole string (apart from surrounding spaces) is one.
    static bool ParseFinite(const std::string& s, double& out);

private:
    std::variant<double, bool, std::string, ArrayRef, MapRef> m_data;
};

/// The engine's null: the number 0, or an empty list/map reference. Text and true/false are never null.
bool IsNullish(const Value& v);
/// `==` in scripts: text compares as text when either side is text, otherwise as numbers (AsNumber). The engine's
/// rule, without its object references; note that it makes two lists of the same length equal.
bool ValuesEqual(const Value& a, const Value& b);

} // namespace okayscript
