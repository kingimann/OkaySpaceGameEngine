#pragma once
#include "okay/Math/Vec3.hpp"
#include <cctype>
#include <cstdint>
#include <map>
#include <memory>
#include <cmath>
#include <cstdlib>
#include <string>
#include <variant>
#include <vector>

namespace okay::vs {

/// `s` as a finite number when the whole string (aside from surrounding spaces)
/// is one. "3" and "-1.5" count; "3abc", "nan" and "" do not.
inline bool VsParseFinite(const std::string& s, float& out) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    if (b >= e) return false;
    char* end = nullptr;
    const float v = std::strtof(s.c_str() + b, &end);
    if (end != s.c_str() + e || !std::isfinite(v)) return false;
    out = v;
    return true;
}

/// A reference to a GameObject flowing on a wire (Self, Other, a spawned object,
/// a raycast hit...). Holds the object's instance id, never a pointer, so it is
/// safe after the object is destroyed (it then resolves to nothing).
struct VsObject { std::uint64_t id = 0; };
/// An RGBA colour, each channel 0..1 (not clamped on store; Make Color and the
/// literal parser clamp). Prints as #rrggbbaa.
struct VsColor { float r = 0, g = 0, b = 0, a = 1; };

/// Read 1..`maxN` finite numbers from text like "0,5,0", "0 5 0" or "(0, 5, 0)":
/// separated by a comma, by spaces or by both, optionally inside one pair of round
/// brackets. Returns how many were read, or 0 when the text isn't such a list (an
/// empty field like "1,,2", a trailing comma, a word, nan / inf, more than maxN).
/// The one parser behind typed Vector3 / Color values (issue #888 finding 30).
inline int VsParseNumberList(const std::string& s, float* out, int maxN) {
    auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    std::size_t b = 0, e = s.size();
    while (b < e && space(s[b])) ++b;
    while (e > b && space(s[e - 1])) --e;
    const bool open = b < e && s[b] == '(', close = b < e && s[e - 1] == ')';
    if (open != close) return 0;
    if (open) { if (e - b < 2) return 0; ++b; --e; }
    const std::string body = s.substr(b, e - b);
    const char* p = body.c_str();
    const char* end = p + body.size();
    int n = 0;
    for (;;) {
        while (p < end && space(*p)) ++p;
        if (p >= end) return 0;   // nothing, or nothing after a comma
        char* q = nullptr;
        const float v = std::strtof(p, &q);
        if (q == p || !std::isfinite(v) || n >= maxN) return 0;
        out[n++] = v;
        p = q;
        bool gap = false;
        while (p < end && space(*p)) { ++p; gap = true; }
        if (p >= end) return n;
        if (*p == ',') { ++p; continue; }
        if (!gap) return 0;   // "1a", "1;2"
    }
}

/// A Vector3 typed as text: "x,y,z", "x y z" or "(x, y, z)"; "x,y" gives z = 0.
/// False for anything else (a single number is the caller's choice: AsVec3 makes it (n, n, n)).
inline bool VsParseVec3Text(const std::string& s, Vec3& out) {
    float v[3] = {0, 0, 0};
    const int n = VsParseNumberList(s, v, 3);
    if (n < 2) return false;
    out = Vec3{v[0], v[1], n == 3 ? v[2] : 0.0f};
    return true;
}

/// A colour typed as text: "#rrggbb" / "#rrggbbaa", or "r,g,b" / "r,g,b,a" (any of the
/// Vector3 separators). Numbers are 0..1, but if ANY of them is above 1 all of them
/// (alpha too) are 0..255. Channels are clamped to 0..1; alpha is 1 when left out.
inline bool VsParseColorText(const std::string& s, VsColor& out) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    float ch[4] = {0, 0, 0, 1};
    if (b < e && s[b] == '#') {
        const std::size_t n = e - b - 1;
        if (n != 6 && n != 8) return false;
        for (std::size_t k = 0; k < n; k += 2) {
            int byte = 0;
            for (std::size_t j = 0; j < 2; ++j) {
                const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[b + 1 + k + j])));
                const int h = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
                if (h < 0) return false;
                byte = byte * 16 + h;
            }
            ch[k / 2] = static_cast<float>(byte) / 255.0f;
        }
    } else {
        const int n = VsParseNumberList(s, ch, 4);
        if (n < 3) return false;
        bool bytes = false;
        for (int k = 0; k < n; ++k) if (ch[k] > 1.0f) bytes = true;
        if (bytes) for (int k = 0; k < n; ++k) ch[k] /= 255.0f;
        for (float& c : ch) c = std::fmin(std::fmax(c, 0.0f), 1.0f);
    }
    out = VsColor{ch[0], ch[1], ch[2], ch[3]};
    return true;
}
/// The name of a referenced object ("" when it no longer exists). NodeGraph.cpp.
std::string VsObjectName(std::uint64_t id);
/// True while the referenced object exists. NodeGraph.cpp.
bool VsObjectAlive(std::uint64_t id);

/// A dynamically-typed value flowing between graph nodes (the data carried on
/// the wires of a visual script). Can also hold a shared, mutable array or map
/// of VsValues (used by OkayScript lists and dictionaries).
class VsValue {
public:
    using Array = std::vector<VsValue>;
    using ArrayRef = std::shared_ptr<Array>;
    using Map = std::map<std::string, VsValue>;
    using MapRef = std::shared_ptr<Map>;

    VsValue() : m_data(0.0f) {}
    VsValue(float v) : m_data(v) {}
    VsValue(double v) : m_data(static_cast<float>(v)) {}
    VsValue(int v) : m_data(static_cast<float>(v)) {}
    VsValue(bool v) : m_data(v) {}
    VsValue(const Vec3& v) : m_data(v) {}
    VsValue(std::string v) : m_data(std::move(v)) {}
    VsValue(const char* v) : m_data(std::string(v)) {}
    VsValue(ArrayRef a) : m_data(std::move(a)) {}
    VsValue(MapRef m) : m_data(std::move(m)) {}
    VsValue(VsObject o) : m_data(o) {}
    VsValue(VsColor c) : m_data(c) {}

    /// A colour value.
    static VsValue Color(float r, float g, float b, float a = 1.0f) { return VsValue(VsColor{r, g, b, a}); }
    bool IsColor() const { return std::holds_alternative<VsColor>(m_data); }
    /// The colour. A Vector3 gives its x, y, z as r, g, b; a number n (clamped to 0..1) is
    /// the grey (n, n, n); text is read by VsParseColorText ("#ff8000", "1,0.5,0",
    /// "255,128,0") or as one number. Anything else is opaque black.
    VsColor AsColor() const {
        if (auto* c = std::get_if<VsColor>(&m_data)) return *c;
        if (auto* v = std::get_if<Vec3>(&m_data)) return VsColor{v->x, v->y, v->z, 1.0f};
        auto grey = [](float f) { const float g = std::fmin(std::fmax(f, 0.0f), 1.0f); return VsColor{g, g, g, 1.0f}; };
        if (auto* f = std::get_if<float>(&m_data)) return std::isnan(*f) ? VsColor{} : grey(*f);
        if (auto* s = std::get_if<std::string>(&m_data)) {
            VsColor c;
            if (VsParseColorText(*s, c)) return c;
            float n = 0.0f;
            if (VsParseFinite(*s, n)) return grey(n);
        }
        return VsColor{};
    }

    /// An object reference value (0 = no object).
    static VsValue Object(std::uint64_t instanceId) { return VsValue(VsObject{instanceId}); }
    bool IsObject() const { return std::holds_alternative<VsObject>(m_data); }
    /// The referenced object's instance id (0 when this isn't an object).
    std::uint64_t AsObjectId() const {
        if (auto* o = std::get_if<VsObject>(&m_data)) return o->id;
        return 0;
    }

    /// Make a new (empty) array value.
    static VsValue MakeArray() { return VsValue(std::make_shared<Array>()); }
    bool IsArray() const { return std::holds_alternative<ArrayRef>(m_data); }
    /// The shared array, or nullptr if this isn't an array.
    ArrayRef AsArray() const {
        if (auto* a = std::get_if<ArrayRef>(&m_data)) return *a;
        return nullptr;
    }
    /// Make a new (empty) map value.
    static VsValue MakeMap() { return VsValue(std::make_shared<Map>()); }
    bool IsMap() const { return std::holds_alternative<MapRef>(m_data); }
    /// The shared map, or nullptr if this isn't a map.
    MapRef AsMap() const {
        if (auto* m = std::get_if<MapRef>(&m_data)) return *m;
        return nullptr;
    }

    float AsFloat() const {
        if (auto* f = std::get_if<float>(&m_data)) return *f;
        if (auto* b = std::get_if<bool>(&m_data)) return *b ? 1.0f : 0.0f;
        if (auto* v = std::get_if<Vec3>(&m_data)) return v->Magnitude();
        if (auto* c = std::get_if<VsColor>(&m_data)) return c->a;
        if (auto* a = std::get_if<ArrayRef>(&m_data)) return *a ? (float)(*a)->size() : 0.0f;
        if (auto* m = std::get_if<MapRef>(&m_data)) return *m ? (float)(*m)->size() : 0.0f;
        if (auto* s = std::get_if<std::string>(&m_data)) {
            float n = 0.0f;
            if (VsParseFinite(*s, n)) return n;
        }
        return 0.0f;
    }
    bool AsBool() const {
        if (auto* b = std::get_if<bool>(&m_data)) return *b;
        if (auto* f = std::get_if<float>(&m_data)) return !std::isnan(*f) && *f != 0.0f;
        if (auto* s = std::get_if<std::string>(&m_data)) {
            std::string t;
            t.reserve(s->size());
            for (char c : *s) {
                if (std::isspace(static_cast<unsigned char>(c))) continue;
                t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            if (t.empty() || t == "false" || t == "no") return false;
            if (t == "true" || t == "yes") return true;
            float n = 0.0f;
            if (VsParseFinite(*s, n)) return n != 0.0f;
            return true;   // any other text is on
        }
        if (auto* c = std::get_if<VsColor>(&m_data)) return c->a > 0.0f;
        if (auto* a = std::get_if<ArrayRef>(&m_data)) return *a && !(*a)->empty();
        if (auto* m = std::get_if<MapRef>(&m_data)) return *m && !(*m)->empty();
        if (auto* o = std::get_if<VsObject>(&m_data)) return o->id != 0 && VsObjectAlive(o->id);
        return false;
    }
    Vec3 AsVec3() const {
        if (auto* v = std::get_if<Vec3>(&m_data)) return *v;
        if (auto* f = std::get_if<float>(&m_data)) return Vec3{*f};
        if (auto* c = std::get_if<VsColor>(&m_data)) return Vec3{c->r, c->g, c->b};
        if (auto* s = std::get_if<std::string>(&m_data)) {   // typed into a pin: "0,5,0", "(0, 5, 0)", "5"
            Vec3 v;
            if (VsParseVec3Text(*s, v)) return v;
            float n = 0.0f;
            if (VsParseFinite(*s, n)) return Vec3{n};
        }
        return Vec3::Zero;
    }
    bool IsString() const { return std::holds_alternative<std::string>(m_data); }
    bool IsVec3() const { return std::holds_alternative<Vec3>(m_data); }
    bool IsBool() const { return std::holds_alternative<bool>(m_data); }
    bool IsNumber() const { return std::holds_alternative<float>(m_data); }
    std::string AsString() const {
        if (auto* s = std::get_if<std::string>(&m_data)) return *s;
        if (auto* b = std::get_if<bool>(&m_data)) return *b ? "true" : "false";
        if (auto* f = std::get_if<float>(&m_data)) return FormatNumber(*f);
        if (auto* v = std::get_if<Vec3>(&m_data))
            return "(" + FormatNumber(v->x) + ", " + FormatNumber(v->y) +
                   ", " + FormatNumber(v->z) + ")";
        if (auto* c = std::get_if<VsColor>(&m_data)) return HexColor(*c);
        if (auto* a = std::get_if<ArrayRef>(&m_data)) {
            if (!*a) return "[]";
            std::string s = "[";
            for (std::size_t i = 0; i < (*a)->size(); ++i) {
                if (i) s += ", ";
                s += (**a)[i].AsString();
            }
            return s + "]";
        }
        if (auto* m = std::get_if<MapRef>(&m_data)) {
            if (!*m) return "{}";
            std::string s = "{";
            bool first = true;
            for (auto& kv : **m) {
                if (!first) s += ", ";
                first = false;
                s += kv.first + ": " + kv.second.AsString();
            }
            return s + "}";
        }
        if (auto* o = std::get_if<VsObject>(&m_data)) return o->id ? VsObjectName(o->id) : std::string();
        return {};
    }

    /// "#rrggbbaa" for a colour (channels clamped to 0..1, rounded to 8 bits).
    static std::string HexColor(const VsColor& c) {
        auto byte = [](float f) { return (unsigned)(std::lround(std::fmin(std::fmax(std::isnan(f) ? 0.0f : f, 0.0f), 1.0f) * 255.0f)); };
        static const char* hex = "0123456789abcdef";
        std::string s = "#";
        for (unsigned b : {byte(c.r), byte(c.g), byte(c.b), byte(c.a)}) { s += hex[b >> 4]; s += hex[b & 15]; }
        return s;
    }

    /// Format a float without trailing-zero noise: whole numbers print as
    /// integers ("3"), fractions trim trailing zeros ("1.5"). Keeps script
    /// string concatenation (e.g. score counters) clean.
    static std::string FormatNumber(float f) {
        // NaN / inf and numbers past long long's range can't take the integer
        // path (that cast is undefined): print them as they are.
        if (std::isnan(f)) return "nan";
        if (std::isinf(f)) return f < 0 ? "-inf" : "inf";
        if (std::fabs(f) < 9.0e18f && f == static_cast<float>(static_cast<long long>(f)))
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
    std::variant<float, bool, Vec3, std::string, ArrayRef, MapRef, VsObject, VsColor> m_data;
};

} // namespace okay::vs
