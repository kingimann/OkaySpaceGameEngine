#include "okay/Scene/Properties.hpp"
#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace okay {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// The shortest text that reads back to exactly the same float: the fewest
// significant digits that survive strtof, in plain notation for everyday sizes
// (100, 0.00012) and exponent notation for the extremes (1e+30, 1e-07).
std::string FloatText(float v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0.0f ? "-inf" : "inf";
    char sci[64];
    int digits = 1;
    for (; digits <= 9; ++digits) {
        std::snprintf(sci, sizeof sci, "%.*e", digits - 1, static_cast<double>(v));
        if (std::strtof(sci, nullptr) == v) break;
    }
    if (digits > 9) std::snprintf(sci, sizeof sci, "%.8e", static_cast<double>(v));
    const char* e = std::strchr(sci, 'e');
    const int exponent = e ? std::atoi(e + 1) : 0;
    char out[96];
    if (exponent >= -5 && exponent < 9) {
        const int after = digits - 1 - exponent;
        std::snprintf(out, sizeof out, "%.*f", after > 0 ? after : 0, static_cast<double>(v));
    } else {
        std::snprintf(out, sizeof out, "%s", sci);
    }
    for (char* p = out; *p; ++p) if (*p == ',') *p = '.';   // a comma-decimal locale
    return out;
}

bool ParseFloat(const std::string& raw, float& out) {
    const std::string tok = Trim(raw);
    if (tok.empty()) return false;
    const std::string low = Lower(tok);
    if (low == "nan" || low == "+nan" || low == "-nan") { out = std::numeric_limits<float>::quiet_NaN(); return true; }
    if (low == "inf" || low == "+inf" || low == "infinity" || low == "+infinity") { out = std::numeric_limits<float>::infinity(); return true; }
    if (low == "-inf" || low == "-infinity") { out = -std::numeric_limits<float>::infinity(); return true; }
    // Plain decimal only: no hex floats, no stray text.
    for (char c : tok)
        if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E'))
            return false;
    char* end = nullptr;
    errno = 0;
    const float v = std::strtof(tok.c_str(), &end);
    if (end != tok.c_str() + tok.size()) return false;
    if (errno == ERANGE && std::isinf(v)) return false;     // too big for a float
    out = v;
    return true;
}

bool ParseInt(const std::string& raw, long long& out) {
    const std::string tok = Trim(raw);
    if (tok.empty()) return false;
    std::size_t i = (tok[0] == '+' || tok[0] == '-') ? 1 : 0;
    if (i >= tok.size()) return false;
    for (std::size_t k = i; k < tok.size(); ++k)
        if (!std::isdigit(static_cast<unsigned char>(tok[k]))) return false;
    char* end = nullptr;
    errno = 0;
    const long long v = std::strtoll(tok.c_str(), &end, 10);
    if (errno == ERANGE || end != tok.c_str() + tok.size()) return false;
    out = v;
    return true;
}

bool ParseBool(const std::string& raw, bool& out) {
    const std::string t = Lower(Trim(raw));
    if (t == "true" || t == "1" || t == "yes" || t == "on") { out = true; return true; }
    if (t == "false" || t == "0" || t == "no" || t == "off") { out = false; return true; }
    return false;
}

// Split "1 2 3" / "1, 2, 3" / "(1, 2, 3)" into exactly `n` floats.
bool ParseFloats(const std::string& raw, int n, float* out) {
    std::string t = Trim(raw);
    if (t.size() >= 2) {
        const char a = t.front(), b = t.back();
        if ((a == '(' && b == ')') || (a == '[' && b == ']') || (a == '{' && b == '}')) t = Trim(t.substr(1, t.size() - 2));
    }
    if (t.empty()) return false;
    std::vector<std::string> parts;
    if (t.find(',') != std::string::npos) {
        std::size_t start = 0;
        for (;;) {
            const std::size_t comma = t.find(',', start);
            parts.push_back(Trim(t.substr(start, comma == std::string::npos ? std::string::npos : comma - start)));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    } else {
        std::size_t i = 0;
        while (i < t.size()) {
            while (i < t.size() && std::isspace(static_cast<unsigned char>(t[i]))) ++i;
            std::size_t j = i;
            while (j < t.size() && !std::isspace(static_cast<unsigned char>(t[j]))) ++j;
            if (j > i) parts.push_back(t.substr(i, j - i));
            i = j;
        }
    }
    if (static_cast<int>(parts.size()) != n) return false;
    float tmp[4] = {0, 0, 0, 0};
    for (int k = 0; k < n; ++k) {
        if (parts[k].empty() || !ParseFloat(parts[k], tmp[k])) return false;
    }
    for (int k = 0; k < n; ++k) out[k] = tmp[k];
    return true;
}

bool ParseHexColor(const std::string& raw, Color& out) {
    const std::string t = Trim(raw);
    if (t.size() != 7 && t.size() != 9) return false;
    if (t[0] != '#') return false;
    int v[4] = {0, 0, 0, 255};
    for (std::size_t k = 0; k < (t.size() - 1) / 2; ++k) {
        int byte = 0;
        for (int d = 0; d < 2; ++d) {
            const char c = t[1 + k * 2 + d];
            int digit;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else return false;
            byte = byte * 16 + digit;
        }
        v[k] = byte;
    }
    out = Color::FromBytes(v[0], v[1], v[2], v[3]);
    return true;
}

const std::string kEmpty;

bool HasControlChars(const std::string& s) {
    for (unsigned char c : s) if (c < 0x20 || c == 0x7F) return true;
    return false;
}

// GameObject::SetGuid's rule: 1 to 64 characters of [0-9a-f-].
bool IsGuidText(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '-')) return false;
    return true;
}

bool SameFloat(float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); }

} // namespace

// ---- PropertyType ---------------------------------------------------------

const char* PropertyTypeName(PropertyType t) {
    switch (t) {
        case PropertyType::None:      return "None";
        case PropertyType::Bool:      return "Bool";
        case PropertyType::Int:       return "Int";
        case PropertyType::Float:     return "Float";
        case PropertyType::String:    return "String";
        case PropertyType::Vec2:      return "Vec2";
        case PropertyType::Vec3:      return "Vec3";
        case PropertyType::Vec4:      return "Vec4";
        case PropertyType::Quat:      return "Quat";
        case PropertyType::Color:     return "Color";
        case PropertyType::Enum:      return "Enum";
        case PropertyType::AssetPath: return "AssetPath";
        case PropertyType::ObjectRef: return "ObjectRef";
    }
    return "None";
}

bool PropertyTypeFromName(const std::string& name, PropertyType& out) {
    const std::string n = Lower(name);
    static const PropertyType all[] = {PropertyType::None, PropertyType::Bool, PropertyType::Int, PropertyType::Float,
        PropertyType::String, PropertyType::Vec2, PropertyType::Vec3, PropertyType::Vec4, PropertyType::Quat,
        PropertyType::Color, PropertyType::Enum, PropertyType::AssetPath, PropertyType::ObjectRef};
    for (PropertyType t : all)
        if (Lower(PropertyTypeName(t)) == n) { out = t; return true; }
    return false;
}

int PropertyTypeComponentCount(PropertyType t) {
    switch (t) {
        case PropertyType::Vec2: return 2;
        case PropertyType::Vec3: return 3;
        case PropertyType::Vec4:
        case PropertyType::Quat:
        case PropertyType::Color: return 4;
        default: return 0;
    }
}

// ---- PropertyValue --------------------------------------------------------

PropertyValue PropertyValue::MakeBool(bool v) { PropertyValue p; p.m_type = PropertyType::Bool; p.m_i = v ? 1 : 0; return p; }
PropertyValue PropertyValue::MakeInt(long long v) { PropertyValue p; p.m_type = PropertyType::Int; p.m_i = v; return p; }
PropertyValue PropertyValue::MakeFloat(float v) { PropertyValue p; p.m_type = PropertyType::Float; p.m_f[0] = v; return p; }
PropertyValue PropertyValue::MakeString(std::string v) { PropertyValue p; p.m_type = PropertyType::String; p.m_s = std::move(v); return p; }
PropertyValue PropertyValue::MakeVec2(const Vec2& v) { PropertyValue p; p.m_type = PropertyType::Vec2; p.m_f[0] = v.x; p.m_f[1] = v.y; return p; }
PropertyValue PropertyValue::MakeVec3(const Vec3& v) { PropertyValue p; p.m_type = PropertyType::Vec3; p.m_f[0] = v.x; p.m_f[1] = v.y; p.m_f[2] = v.z; return p; }
PropertyValue PropertyValue::MakeVec4(const Vec4& v) { PropertyValue p; p.m_type = PropertyType::Vec4; p.m_f[0] = v.x; p.m_f[1] = v.y; p.m_f[2] = v.z; p.m_f[3] = v.w; return p; }
PropertyValue PropertyValue::MakeQuat(const Quat& v) { PropertyValue p; p.m_type = PropertyType::Quat; p.m_f[0] = v.x; p.m_f[1] = v.y; p.m_f[2] = v.z; p.m_f[3] = v.w; return p; }
PropertyValue PropertyValue::MakeColor(const Color& v) { PropertyValue p; p.m_type = PropertyType::Color; p.m_f[0] = v.r; p.m_f[1] = v.g; p.m_f[2] = v.b; p.m_f[3] = v.a; return p; }
PropertyValue PropertyValue::MakeEnum(int value, std::string name) { PropertyValue p; p.m_type = PropertyType::Enum; p.m_i = value; p.m_s = std::move(name); return p; }
PropertyValue PropertyValue::MakeAssetPath(std::string path) { PropertyValue p; p.m_type = PropertyType::AssetPath; p.m_s = std::move(path); return p; }
PropertyValue PropertyValue::MakeObjectRef(std::string guid) { PropertyValue p; p.m_type = PropertyType::ObjectRef; p.m_s = std::move(guid); return p; }

const std::string& PropertyValue::AsString() const {
    return (m_type == PropertyType::String || m_type == PropertyType::AssetPath || m_type == PropertyType::ObjectRef) ? m_s : kEmpty;
}
const std::string& PropertyValue::EnumName() const { return m_type == PropertyType::Enum ? m_s : kEmpty; }
Vec2 PropertyValue::AsVec2() const { return m_type == PropertyType::Vec2 ? Vec2{m_f[0], m_f[1]} : Vec2{}; }
Vec3 PropertyValue::AsVec3() const { return m_type == PropertyType::Vec3 ? Vec3{m_f[0], m_f[1], m_f[2]} : Vec3{}; }
Vec4 PropertyValue::AsVec4() const { return m_type == PropertyType::Vec4 ? Vec4{m_f[0], m_f[1], m_f[2], m_f[3]} : Vec4{}; }
Quat PropertyValue::AsQuat() const { return m_type == PropertyType::Quat ? Quat{m_f[0], m_f[1], m_f[2], m_f[3]} : Quat{}; }
Color PropertyValue::AsColor() const { return m_type == PropertyType::Color ? Color{m_f[0], m_f[1], m_f[2], m_f[3]} : Color{}; }

std::string PropertyValue::ToString() const {
    switch (m_type) {
        case PropertyType::None:      return std::string();
        case PropertyType::Bool:      return m_i != 0 ? "true" : "false";
        case PropertyType::Int:       return std::to_string(m_i);
        case PropertyType::Float:     return FloatText(m_f[0]);
        case PropertyType::String:
        case PropertyType::AssetPath:
        case PropertyType::ObjectRef: return m_s;
        case PropertyType::Enum:      return m_s.empty() ? std::to_string(m_i) : std::to_string(m_i) + " " + m_s;
        case PropertyType::Vec2:
        case PropertyType::Vec3:
        case PropertyType::Vec4:
        case PropertyType::Quat:
        case PropertyType::Color: {
            const int n = PropertyTypeComponentCount(m_type);
            std::string s;
            for (int i = 0; i < n; ++i) { if (i) s += ' '; s += FloatText(m_f[i]); }
            return s;
        }
    }
    return std::string();
}

bool PropertyValue::FromString(PropertyType type, const std::string& text, PropertyValue& out) {
    switch (type) {
        case PropertyType::None: return false;
        case PropertyType::Bool: {
            bool b = false;
            if (!ParseBool(text, b)) return false;
            out = MakeBool(b);
            return true;
        }
        case PropertyType::Int: {
            long long v = 0;
            if (!ParseInt(text, v)) return false;
            out = MakeInt(v);
            return true;
        }
        case PropertyType::Float: {
            float v = 0.0f;
            if (!ParseFloat(text, v)) return false;
            out = MakeFloat(v);
            return true;
        }
        case PropertyType::String:    out = MakeString(text); return true;
        case PropertyType::AssetPath: out = MakeAssetPath(text); return true;
        case PropertyType::ObjectRef: out = MakeObjectRef(text); return true;
        case PropertyType::Enum: {
            const std::string t = Trim(text);
            std::size_t split = 0;
            while (split < t.size() && !std::isspace(static_cast<unsigned char>(t[split]))) ++split;
            long long v = 0;
            if (!ParseInt(t.substr(0, split), v) || v < INT_MIN || v > INT_MAX) return false;
            out = MakeEnum(static_cast<int>(v), Trim(t.substr(split)));
            return true;
        }
        case PropertyType::Color: {
            Color hc;
            if (ParseHexColor(text, hc)) { out = MakeColor(hc); return true; }
            float f[4];
            if (!ParseFloats(text, 4, f)) return false;
            out = MakeColor(Color{f[0], f[1], f[2], f[3]});
            return true;
        }
        case PropertyType::Vec2: {
            float f[2];
            if (!ParseFloats(text, 2, f)) return false;
            out = MakeVec2(Vec2{f[0], f[1]});
            return true;
        }
        case PropertyType::Vec3: {
            float f[3];
            if (!ParseFloats(text, 3, f)) return false;
            out = MakeVec3(Vec3{f[0], f[1], f[2]});
            return true;
        }
        case PropertyType::Vec4: {
            float f[4];
            if (!ParseFloats(text, 4, f)) return false;
            out = MakeVec4(Vec4{f[0], f[1], f[2], f[3]});
            return true;
        }
        case PropertyType::Quat: {
            float f[4];
            if (!ParseFloats(text, 4, f)) return false;
            out = MakeQuat(Quat{f[0], f[1], f[2], f[3]});
            return true;
        }
    }
    return false;
}

bool PropertyValue::SubFieldIndex(PropertyType type, const std::string& sub, int& index) {
    const int n = PropertyTypeComponentCount(type);
    if (n == 0 || sub.size() != 1) return false;
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(sub[0])));
    int i = -1;
    if (type == PropertyType::Color) {
        if (c == 'r') i = 0; else if (c == 'g') i = 1; else if (c == 'b') i = 2; else if (c == 'a') i = 3;
    } else {
        if (c == 'x') i = 0; else if (c == 'y') i = 1; else if (c == 'z') i = 2; else if (c == 'w') i = 3;
    }
    if (i < 0 || i >= n) return false;
    index = i;
    return true;
}

bool PropertyValue::GetSubField(const std::string& sub, PropertyValue& out) const {
    int i = 0;
    if (!SubFieldIndex(m_type, sub, i)) return false;
    out = MakeFloat(m_f[i]);
    return true;
}

bool PropertyValue::WithSubField(const std::string& sub, const PropertyValue& component, PropertyValue& inOut) const {
    int i = 0;
    if (component.m_type != PropertyType::Float || !SubFieldIndex(m_type, sub, i)) return false;
    PropertyValue r = *this;
    r.m_f[i] = component.m_f[0];
    inOut = r;
    return true;
}

bool PropertyValue::operator==(const PropertyValue& o) const {
    if (m_type != o.m_type) return false;
    switch (m_type) {
        case PropertyType::None:      return true;
        case PropertyType::Bool:
        case PropertyType::Int:
        case PropertyType::Enum:      return m_i == o.m_i;
        case PropertyType::Float:     return SameFloat(m_f[0], o.m_f[0]);
        case PropertyType::String:
        case PropertyType::AssetPath:
        case PropertyType::ObjectRef: return m_s == o.m_s;
        default: {
            const int n = PropertyTypeComponentCount(m_type);
            for (int i = 0; i < n; ++i) if (!SameFloat(m_f[i], o.m_f[i])) return false;
            return true;
        }
    }
}

// ---- PropertyDesc ---------------------------------------------------------

PropertyValue PropertyDesc::Get(const Component& c) const {
    if (!getter) return PropertyValue();
    PropertyValue v = getter(c);
    if (v.Type() == PropertyType::Enum && type == PropertyType::Enum) {
        const int i = v.AsEnum();
        if (i >= 0 && i < static_cast<int>(enumOptions.size())) return PropertyValue::MakeEnum(i, enumOptions[static_cast<std::size_t>(i)]);
    }
    return v;
}

namespace {
// Clamp (or refuse) one number against the descriptor's range.
bool ApplyRange(const PropertyDesc& d, double& v) {
    if (!d.hasRange) return true;
    if (v < d.rangeMin) { if (d.rejectOutOfRange) return false; v = d.rangeMin; }
    else if (v > d.rangeMax) { if (d.rejectOutOfRange) return false; v = d.rangeMax; }
    return true;
}
} // namespace

bool PropertyDesc::Normalize(const PropertyValue& in, PropertyValue& out) const {
    if (type == PropertyType::None || in.Type() != type) return false;
    switch (type) {
        case PropertyType::None: return false;
        case PropertyType::Bool: out = in; return true;
        case PropertyType::Int: {
            double v = static_cast<double>(in.AsInt());
            if (hasRange) {
                if (!ApplyRange(*this, v)) return false;
                // Only values that moved are rewritten, so a big int outside double's
                // exact range is not rounded when it was already inside the limits.
                if (v != static_cast<double>(in.AsInt())) { out = PropertyValue::MakeInt(static_cast<long long>(v)); return true; }
            }
            out = in;
            return true;
        }
        case PropertyType::Float: {
            const float f = in.AsFloat();
            if (!std::isfinite(f)) return false;
            double v = f;
            if (!ApplyRange(*this, v)) return false;
            out = (static_cast<float>(v) == f) ? in : PropertyValue::MakeFloat(static_cast<float>(v));
            return true;
        }
        case PropertyType::String:
        case PropertyType::AssetPath: {
            if (!multiline && HasControlChars(in.AsString())) return false;
            out = in;
            return true;
        }
        case PropertyType::ObjectRef: {
            if (!in.AsString().empty() && !IsGuidText(in.AsString())) return false;
            out = in;
            return true;
        }
        case PropertyType::Enum: {
            const int v = in.AsEnum();
            if (!enumOptions.empty()) {
                if (v < 0 || v >= static_cast<int>(enumOptions.size())) return false;
                out = PropertyValue::MakeEnum(v, enumOptions[static_cast<std::size_t>(v)]);
            } else {
                out = PropertyValue::MakeEnum(v, std::string());
            }
            return true;
        }
        case PropertyType::Quat: {
            const Quat q = in.AsQuat();
            if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) return false;
            const double len2 = double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z + double(q.w) * q.w;
            if (!(len2 > 1e-12)) return false;
            if (std::fabs(len2 - 1.0) <= 1e-6) { out = in; return true; }   // already a unit quaternion
            const double inv = 1.0 / std::sqrt(len2);
            out = PropertyValue::MakeQuat(Quat{static_cast<float>(q.x * inv), static_cast<float>(q.y * inv),
                                               static_cast<float>(q.z * inv), static_cast<float>(q.w * inv)});
            return true;
        }
        case PropertyType::Vec2:
        case PropertyType::Vec3:
        case PropertyType::Vec4:
        case PropertyType::Color: {
            const int n = PropertyTypeComponentCount(type);
            float f[4] = {0, 0, 0, 0};
            switch (type) {
                case PropertyType::Vec2: { Vec2 v = in.AsVec2(); f[0] = v.x; f[1] = v.y; break; }
                case PropertyType::Vec3: { Vec3 v = in.AsVec3(); f[0] = v.x; f[1] = v.y; f[2] = v.z; break; }
                case PropertyType::Vec4: { Vec4 v = in.AsVec4(); f[0] = v.x; f[1] = v.y; f[2] = v.z; f[3] = v.w; break; }
                default: { Color v = in.AsColor(); f[0] = v.r; f[1] = v.g; f[2] = v.b; f[3] = v.a; break; }
            }
            bool changed = false;
            for (int i = 0; i < n; ++i) {
                if (!std::isfinite(f[i])) return false;
                double v = f[i];
                if (!ApplyRange(*this, v)) return false;
                if (static_cast<float>(v) != f[i]) { f[i] = static_cast<float>(v); changed = true; }
            }
            if (!changed) { out = in; return true; }
            switch (type) {
                case PropertyType::Vec2: out = PropertyValue::MakeVec2(Vec2{f[0], f[1]}); break;
                case PropertyType::Vec3: out = PropertyValue::MakeVec3(Vec3{f[0], f[1], f[2]}); break;
                case PropertyType::Vec4: out = PropertyValue::MakeVec4(Vec4{f[0], f[1], f[2], f[3]}); break;
                default: out = PropertyValue::MakeColor(Color{f[0], f[1], f[2], f[3]}); break;
            }
            return true;
        }
    }
    return false;
}

bool PropertyDesc::Set(Component& c, const PropertyValue& v) const {
    if (!setter || IsReadOnly()) return false;
    PropertyValue norm;
    if (!Normalize(v, norm)) return false;
    return setter(c, norm);
}

bool PropertyDesc::ParseText(const std::string& text, PropertyValue& out) const {
    if (type == PropertyType::Enum) {
        const std::string t = Trim(text);
        const std::string low = Lower(t);
        for (std::size_t i = 0; i < enumOptions.size(); ++i)
            if (Lower(enumOptions[i]) == low) { out = PropertyValue::MakeEnum(static_cast<int>(i), enumOptions[i]); return true; }
    }
    PropertyValue v;
    if (!PropertyValue::FromString(type, text, v)) return false;
    if (type == PropertyType::Enum && !v.EnumName().empty()) {
        // "number name" (what FormatText / ToString write): when the name is one of the
        // options it must be the option the number picks. A text that contradicts itself
        // (a stale "1 Orthographic" after the options were reordered) is refused rather
        // than silently taking the number. A name that is no option any more: the number decides.
        const std::string low = Lower(v.EnumName());
        for (std::size_t i = 0; i < enumOptions.size(); ++i)
            if (Lower(enumOptions[i]) == low) {
                if (static_cast<int>(i) != v.AsEnum()) return false;
                break;
            }
    }
    out = v;
    return true;
}

std::string PropertyDesc::FormatText(const PropertyValue& v) const {
    if (v.Type() == PropertyType::Enum) {
        const int i = v.AsEnum();
        if (i >= 0 && i < static_cast<int>(enumOptions.size())) return enumOptions[static_cast<std::size_t>(i)];
    }
    return v.ToString();
}

} // namespace okay
