// OkayScript values. The conversion rules follow the engine's script value (vs::VsValue) so scripts read the same in
// both; written for this project from that behaviour (no other project's code).
#include "okayscript/Value.hpp"
#include <cctype>
#include <cmath>
#include <cstdlib>

// dup-allow-begin: the conversion rules of the engine's vs::VsValue (engine/include/okay/VisualScript/VsValue.hpp), repeated here without its engine-only kinds so the standalone language reads values the same; one copy once the engine moves onto okayscript::Value (docs/status/UNFINISHED.md "OkayScript standalone core")
namespace okayscript {

Value::ArrayRef Value::AsArray() const {
    if (auto* a = std::get_if<ArrayRef>(&m_data)) return *a;
    return nullptr;
}
Value::MapRef Value::AsMap() const {
    if (auto* m = std::get_if<MapRef>(&m_data)) return *m;
    return nullptr;
}

bool Value::ParseFinite(const std::string& s, double& out) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    if (b >= e) return false;
    const std::string t = s.substr(b, e - b);
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end != t.c_str() + t.size() || !std::isfinite(v)) return false;
    out = v;
    return true;
}

double Value::AsNumber() const {
    if (auto* f = std::get_if<double>(&m_data)) return *f;
    if (auto* b = std::get_if<bool>(&m_data)) return *b ? 1.0 : 0.0;
    if (auto* a = std::get_if<ArrayRef>(&m_data)) return *a ? static_cast<double>((*a)->size()) : 0.0;
    if (auto* m = std::get_if<MapRef>(&m_data)) return *m ? static_cast<double>((*m)->size()) : 0.0;
    if (auto* s = std::get_if<std::string>(&m_data)) {
        double n = 0.0;
        if (ParseFinite(*s, n)) return n;
    }
    return 0.0;
}

bool Value::AsBool() const {
    if (auto* b = std::get_if<bool>(&m_data)) return *b;
    if (auto* f = std::get_if<double>(&m_data)) return !std::isnan(*f) && *f != 0.0;
    if (auto* s = std::get_if<std::string>(&m_data)) {
        std::string t;
        for (char c : *s) {
            if (std::isspace(static_cast<unsigned char>(c))) continue;
            t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (t.empty() || t == "false" || t == "no") return false;
        if (t == "true" || t == "yes") return true;
        double n = 0.0;
        if (ParseFinite(*s, n)) return n != 0.0;
        return true;   // any other text is on
    }
    if (auto* a = std::get_if<ArrayRef>(&m_data)) return *a && !(*a)->empty();
    if (auto* m = std::get_if<MapRef>(&m_data)) return *m && !(*m)->empty();
    return false;
}

std::string Value::FormatNumber(double f) {
    if (std::isnan(f)) return "nan";
    if (std::isinf(f)) return f < 0 ? "-inf" : "inf";
    if (std::fabs(f) < 9.0e18 && f == static_cast<double>(static_cast<long long>(f)))
        return std::to_string(static_cast<long long>(f));
    std::string s = std::to_string(f);   // six decimals, then trim the zeros
    const std::size_t dot = s.find('.');
    if (dot != std::string::npos) {
        const std::size_t last = s.find_last_not_of('0');
        if (last > dot) s.erase(last + 1); else s.erase(dot);
    }
    if (s == "-0") s = "0";
    return s;
}

std::string Value::AsString() const {
    if (auto* s = std::get_if<std::string>(&m_data)) return *s;
    if (auto* b = std::get_if<bool>(&m_data)) return *b ? "true" : "false";
    if (auto* f = std::get_if<double>(&m_data)) return FormatNumber(*f);
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
        if ((*m)->count("__lambda") || (*m)->count("__fn")) return "<function>";
        std::string s = "{";
        bool first = true;
        for (auto& kv : **m) {
            if (!first) s += ", ";
            first = false;
            s += kv.first + ": " + kv.second.AsString();
        }
        return s + "}";
    }
    return {};
}

const char* Value::TypeName() const {
    if (IsNumber()) return "number";
    if (IsBool()) return "bool";
    if (IsString()) return "string";
    if (IsArray()) return "list";
    if (auto m = AsMap(); m && (m->count("__lambda") || m->count("__fn"))) return "function";
    return "map";
}

bool IsNullish(const Value& v) {
    if (v.IsBool() || v.IsString()) return false;
    if (v.IsArray()) return !v.AsArray();
    if (v.IsMap()) return !v.AsMap();
    return v.IsNumber() && v.AsNumber() == 0.0;
}

bool ValuesEqual(const Value& a, const Value& b) {
    if (a.IsString() || b.IsString()) return a.AsString() == b.AsString();
    return a.AsNumber() == b.AsNumber();
}

} // namespace okayscript
// dup-allow-end
