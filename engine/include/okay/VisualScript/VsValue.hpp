#pragma once
#include "okay/Math/Vec3.hpp"
#include <string>
#include <variant>

namespace okay::vs {

/// A dynamically-typed value flowing between graph nodes (the data carried on
/// the wires of a visual script).
class VsValue {
public:
    VsValue() : m_data(0.0f) {}
    VsValue(float v) : m_data(v) {}
    VsValue(double v) : m_data(static_cast<float>(v)) {}
    VsValue(int v) : m_data(static_cast<float>(v)) {}
    VsValue(bool v) : m_data(v) {}
    VsValue(const Vec3& v) : m_data(v) {}
    VsValue(std::string v) : m_data(std::move(v)) {}
    VsValue(const char* v) : m_data(std::string(v)) {}

    float AsFloat() const {
        if (auto* f = std::get_if<float>(&m_data)) return *f;
        if (auto* b = std::get_if<bool>(&m_data)) return *b ? 1.0f : 0.0f;
        if (auto* v = std::get_if<Vec3>(&m_data)) return v->Magnitude();
        return 0.0f;
    }
    bool AsBool() const {
        if (auto* b = std::get_if<bool>(&m_data)) return *b;
        if (auto* f = std::get_if<float>(&m_data)) return *f != 0.0f;
        if (auto* s = std::get_if<std::string>(&m_data)) return !s->empty();
        return false;
    }
    Vec3 AsVec3() const {
        if (auto* v = std::get_if<Vec3>(&m_data)) return *v;
        if (auto* f = std::get_if<float>(&m_data)) return Vec3{*f};
        return Vec3::Zero;
    }
    bool IsString() const { return std::holds_alternative<std::string>(m_data); }
    std::string AsString() const {
        if (auto* s = std::get_if<std::string>(&m_data)) return *s;
        if (auto* b = std::get_if<bool>(&m_data)) return *b ? "true" : "false";
        if (auto* f = std::get_if<float>(&m_data)) return FormatNumber(*f);
        if (auto* v = std::get_if<Vec3>(&m_data))
            return "(" + FormatNumber(v->x) + ", " + FormatNumber(v->y) +
                   ", " + FormatNumber(v->z) + ")";
        return {};
    }

    /// Format a float without trailing-zero noise: whole numbers print as
    /// integers ("3"), fractions trim trailing zeros ("1.5"). Keeps script
    /// string concatenation (e.g. score counters) clean.
    static std::string FormatNumber(float f) {
        if (f == static_cast<float>(static_cast<long long>(f)))
            return std::to_string(static_cast<long long>(f));
        std::string s = std::to_string(f);
        std::size_t dot = s.find('.');
        if (dot != std::string::npos) {
            std::size_t last = s.find_last_not_of('0');
            if (last > dot) s.erase(last + 1); else s.erase(dot);
        }
        return s;
    }

private:
    std::variant<float, bool, Vec3, std::string> m_data;
};

} // namespace okay::vs
