#include "okay/VisualScript/VsDecl.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>

namespace okay::vs {

const char* VarScopeName(VarScope s) {
    switch (s) { case VarScope::Object: return "object"; case VarScope::Scene: return "scene"; case VarScope::Global: return "global"; }
    return "object";
}
bool VarScopeFromName(const std::string& in, VarScope& out) {
    std::string s; for (char c : in) s += (char)std::tolower((unsigned char)c);
    if (s == "object" || s == "graph" || s == "local") { out = VarScope::Object; return true; }
    if (s == "scene") { out = VarScope::Scene; return true; }
    if (s == "global") { out = VarScope::Global; return true; }
    return false;
}

bool VsValidIdent(const std::string& s);
namespace {
bool ValidIdent(const std::string& s) {
    if (s.empty() || !(std::isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (char c : s) if (!(std::isalnum((unsigned char)c) || c == '_')) return false;
    return true;
}
std::string Lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
bool BaseFromName(const std::string& t, PinType& out) {
    const std::string l = Lower(t);
    if (l == "any") { out = PinType::Any; return true; }
    if (l == "number" || l == "float" || l == "int") { out = PinType::Number; return true; }
    if (l == "bool" || l == "boolean") { out = PinType::Bool; return true; }
    if (l == "text" || l == "string") { out = PinType::Text; return true; }
    if (l == "vector3" || l == "vec3") { out = PinType::Vec3; return true; }
    if (l == "color" || l == "colour") { out = PinType::Color; return true; }
    if (l == "object" || l == "gameobject") { out = PinType::Object; return true; }
    return false;
}
constexpr int kMaxDepth = 16;
constexpr int kMaxItems = 100000;
constexpr std::size_t kMaxText = 1u << 20;
} // namespace

bool VsValidIdent(const std::string& s) { return ValidIdent(s); }

const StructDef* FindStruct(const std::vector<StructDef>& structs, const std::string& name) {
    for (const auto& s : structs) if (s.name == name) return &s;
    return nullptr;
}

std::string VarTypeText(const VarType& t) {
    if (t.IsStruct()) return t.structName;
    if (t.base == PinType::List || t.base == PinType::Map) {
        const char* b = t.base == PinType::List ? "List" : "Map";
        return t.elem == PinType::Any ? std::string(b) : std::string(b) + "<" + PinTypeName(t.elem) + ">";
    }
    return PinTypeName(t.base);
}

bool ParseVarType(const std::string& textIn, const std::vector<StructDef>& structs, VarType& out, std::string* err) {
    std::string text;
    for (char c : textIn) if (!std::isspace((unsigned char)c)) text += c;
    out = VarType{};
    const auto lt = text.find('<');
    if (lt != std::string::npos) {
        if (text.back() != '>') { if (err) *err = "'" + textIn + "' is missing a '>'"; return false; }
        const std::string head = Lower(text.substr(0, lt)), inner = text.substr(lt + 1, text.size() - lt - 2);
        PinType elem;
        if (!BaseFromName(inner, elem) || elem == PinType::Object) {
            // Object elements are fine, only List / Map nested inside are not typed deeper.
            if (!BaseFromName(inner, elem)) { if (err) *err = "list and map elements can be Number, Bool, Text, Vector3, Color, Object or Any, not '" + inner + "'"; return false; }
        }
        if (head == "list" || head == "array") { out.base = PinType::List; out.elem = elem; return true; }
        if (head == "map" || head == "dictionary") { out.base = PinType::Map; out.elem = elem; return true; }
        if (err) *err = "only List<T> and Map<T> take an element type";
        return false;
    }
    const std::string l = Lower(text);
    if (l == "list" || l == "array") { out.base = PinType::List; return true; }
    if (l == "map" || l == "dictionary") { out.base = PinType::Map; return true; }
    if (BaseFromName(text, out.base)) return true;
    if (FindStruct(structs, text)) { out.base = PinType::Map; out.structName = text; return true; }
    if (err) *err = "unknown type '" + textIn + "' (Number, Bool, Text, Vector3, Color, Object, List, Map, List<T>, Map<T>, or a struct name)";
    return false;
}

const char* VsKindName(const VsValue& v) {
    if (v.IsNumber()) return "Number";
    if (v.IsBool()) return "Bool";
    if (v.IsString()) return "Text";
    if (v.IsVec3()) return "Vector3";
    if (v.IsColor()) return "Color";
    if (v.IsObject()) return "Object";
    if (v.IsArray()) return "List";
    if (v.IsMap()) return "Map";
    return "Any";
}

// ---- literal text -------------------------------------------------------------
namespace {
std::string Num(float f) {
    if (!std::isfinite(f)) return "0";
    return VsValue::FormatNumber(f);
}
std::string Quote(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else o += c;
    }
    return o + '"';
}
std::string Lit(const VsValue& v, int depth) {
    if (v.IsNumber()) return Num(v.AsFloat());
    if (v.IsBool()) return v.AsBool() ? "true" : "false";
    if (v.IsString()) return Quote(v.AsString());
    if (v.IsVec3()) { const Vec3 p = v.AsVec3(); return "(" + Num(p.x) + ", " + Num(p.y) + ", " + Num(p.z) + ")"; }
    if (v.IsColor()) return VsValue::HexColor(v.AsColor());
    if (v.IsObject()) { const std::string n = v.AsString(); return v.AsObjectId() && !n.empty() ? "@" + n : "@"; }
    if (v.IsArray()) {
        auto a = v.AsArray();
        if (!a || depth >= kMaxDepth) return "[]";
        std::string s = "[";
        for (std::size_t i = 0; i < a->size(); ++i) { if (i) s += ", "; s += Lit((*a)[i], depth + 1); }
        return s + "]";
    }
    if (v.IsMap()) {
        auto m = v.AsMap();
        if (!m || depth >= kMaxDepth) return "{}";
        std::string s = "{"; bool first = true;
        for (const auto& kv : *m) {
            if (!first) s += ", ";
            first = false;
            s += (ValidIdent(kv.first) ? kv.first : Quote(kv.first)) + ": " + Lit(kv.second, depth + 1);
        }
        return s + "}";
    }
    return "0";
}

struct Parser {
    const std::string& s;
    std::size_t i = 0;
    int items = 0;
    std::string err;
    explicit Parser(const std::string& t) : s(t) {}
    void Ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
    bool Fail(const std::string& m) { if (err.empty()) err = m + " (at character " + std::to_string(i + 1) + ")"; return false; }
    bool Number(float& f) {
        const char* b = s.c_str() + i; char* e = nullptr;
        const double d = std::strtod(b, &e);
        if (e == b) return Fail("expected a number");
        i += (std::size_t)(e - b);
        if (!std::isfinite(d)) return Fail("a number is too big or not a number");
        f = (float)d; return true;
    }
    bool Str(std::string& out) {
        ++i;   // opening quote
        out.clear();
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c == '\\' && i < s.size()) {
                const char n = s[i++];
                out += n == 'n' ? '\n' : n == 't' ? '\t' : n;
            } else out += c;
            if (out.size() > kMaxText) return Fail("text is too long");
        }
        return Fail("a quote is never closed");
    }
    static int Hex(char c) { if (c >= '0' && c <= '9') return c - '0'; c = (char)std::tolower((unsigned char)c); return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
    bool Value(VsValue& out, int depth) {
        if (depth > kMaxDepth) return Fail("nested too deep");
        if (++items > kMaxItems) return Fail("too many items");
        Ws();
        if (i >= s.size()) return Fail("expected a value");
        const char c = s[i];
        if (c == '"') { std::string t; if (!Str(t)) return false; out = VsValue(t); return true; }
        if (c == '(') {
            ++i; float v[3] = {0, 0, 0};
            for (int k = 0; k < 3; ++k) {
                Ws(); if (!Number(v[k])) return false; Ws();
                if (k < 2) { if (i >= s.size() || s[i] != ',') return Fail("a vector needs three numbers (x, y, z)"); ++i; }
            }
            if (i >= s.size() || s[i] != ')') return Fail("a vector needs three numbers (x, y, z)");
            ++i; out = VsValue(Vec3{v[0], v[1], v[2]}); return true;
        }
        if (c == '#') {
            std::size_t n = 0; int h[8];
            while (i + 1 + n < s.size() && n < 9 && Hex(s[i + 1 + n]) >= 0) { h[n < 8 ? n : 0] = Hex(s[i + 1 + n]); ++n; }
            if (n != 6 && n != 8) return Fail("a colour is #rrggbb or #rrggbbaa");
            float ch[4] = {0, 0, 0, 1};
            for (std::size_t k = 0; k < n / 2; ++k) ch[k] = (float)(h[2 * k] * 16 + h[2 * k + 1]) / 255.0f;
            i += 1 + n; out = VsValue::Color(ch[0], ch[1], ch[2], ch[3]); return true;
        }
        if (c == '[') {
            ++i; auto a = std::make_shared<VsValue::Array>(); Ws();
            if (i < s.size() && s[i] == ']') { ++i; out = VsValue(a); return true; }
            for (;;) {
                VsValue e; if (!Value(e, depth + 1)) return false;
                a->push_back(std::move(e)); Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                return Fail("expected ',' or ']'");
            }
            out = VsValue(a); return true;
        }
        if (c == '{') {
            ++i; auto m = std::make_shared<VsValue::Map>(); Ws();
            if (i < s.size() && s[i] == '}') { ++i; out = VsValue(m); return true; }
            for (;;) {
                Ws(); std::string k;
                if (i < s.size() && s[i] == '"') { if (!Str(k)) return false; }
                else {
                    while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '-' || s[i] == '.')) k += s[i++];
                    if (k.empty()) return Fail("expected a key");
                }
                Ws(); if (i >= s.size() || s[i] != ':') return Fail("expected ':' after the key");
                ++i; VsValue e; if (!Value(e, depth + 1)) return false;
                (*m)[k] = std::move(e); Ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                return Fail("expected ',' or '}'");
            }
            out = VsValue(m); return true;
        }
        if (c == '@') {
            ++i; std::string n;
            while (i < s.size() && s[i] != ',' && s[i] != ']' && s[i] != '}' && s[i] != ')' && !std::isspace((unsigned char)s[i])) n += s[i++];
            out = VsValue(n); return true;   // an object by name / @guid: text; nodes resolve it
        }
        if (s.compare(i, 4, "true") == 0 && (i + 4 >= s.size() || !std::isalnum((unsigned char)s[i + 4]))) { i += 4; out = VsValue(true); return true; }
        if (s.compare(i, 5, "false") == 0 && (i + 5 >= s.size() || !std::isalnum((unsigned char)s[i + 5]))) { i += 5; out = VsValue(false); return true; }
        if (std::isdigit((unsigned char)c) || c == '-' || c == '+' || c == '.') { float f; if (!Number(f)) return false; out = VsValue(f); return true; }
        return Fail(std::string("unexpected '") + c + "'");
    }
};
} // namespace

std::string VsToLiteral(const VsValue& v, int depth) { return Lit(v, depth); }

bool VsParseLiteral(const std::string& text, VsValue& out, std::string* err) {
    if (text.size() > kMaxText) { if (err) *err = "the value is too long"; return false; }
    Parser p(text);
    VsValue v;
    if (!p.Value(v, 0)) { if (err) *err = p.err; return false; }
    p.Ws();
    if (p.i != text.size()) { if (err) *err = "unexpected text after the value (at character " + std::to_string(p.i + 1) + ")"; return false; }
    out = std::move(v);
    return true;
}

std::string VsDescribe(const VsValue& v, int maxItems) {
    std::function<std::string(const VsValue&, int)> d = [&](const VsValue& x, int depth) -> std::string {
        if (x.IsString()) { std::string t = x.AsString(); if (t.size() > 40) t = t.substr(0, 37) + "..."; return "\"" + t + "\""; }
        if (x.IsObject()) return x.AsObjectId() ? "'" + x.AsString() + "'" + (x.AsString().empty() ? " (destroyed)" : "") : "(no object)";
        if (x.IsArray()) {
            auto a = x.AsArray(); const std::size_t n = a ? a->size() : 0;
            std::string s = "list(" + std::to_string(n) + ")";
            if (!n || depth >= 3) return s;
            s += " [";
            for (std::size_t i = 0; i < n && (int)i < maxItems; ++i) { if (i) s += ", "; s += d((*a)[i], depth + 1); }
            if ((int)n > maxItems) s += ", ...";
            return s + "]";
        }
        if (x.IsMap()) {
            auto m = x.AsMap(); const std::size_t n = m ? m->size() : 0;
            std::string s = "map(" + std::to_string(n) + ")";
            if (!n || depth >= 3) return s;
            s += " {"; int k = 0;
            for (const auto& kv : *m) {
                if (k >= maxItems) { s += ", ..."; break; }
                if (k++) s += ", ";
                s += kv.first + ": " + d(kv.second, depth + 1);
            }
            return s + "}";
        }
        return x.AsString();
    };
    return d(v, 0);
}

VsValue VsDefaultFor(const VarType& t, const std::vector<StructDef>& structs) {
    if (t.IsStruct()) {
        VsValue m = VsValue::MakeMap();
        if (const StructDef* sd = FindStruct(structs, t.structName))
            for (const auto& f : sd->fields) (*m.AsMap())[f.name] = VsDeepCopy(f.def);
        return m;
    }
    switch (t.base) {
        case PinType::Number: return VsValue(0.0f);
        case PinType::Bool: return VsValue(false);
        case PinType::Text: return VsValue(std::string());
        case PinType::Vec3: return VsValue(Vec3{0, 0, 0});
        case PinType::Color: return VsValue::Color(1, 1, 1, 1);
        case PinType::Object: return VsValue::Object(0);
        case PinType::List: return VsValue::MakeArray();
        case PinType::Map: return VsValue::MakeMap();
        default: return VsValue(0.0f);
    }
}

VsValue VsDeepCopy(const VsValue& v, int depth) {
    if (depth > 64) return v.IsArray() ? VsValue::MakeArray() : v.IsMap() ? VsValue::MakeMap() : v;
    if (v.IsArray()) {
        VsValue out = VsValue::MakeArray();
        if (auto a = v.AsArray()) for (const auto& e : *a) out.AsArray()->push_back(VsDeepCopy(e, depth + 1));
        return out;
    }
    if (v.IsMap()) {
        VsValue out = VsValue::MakeMap();
        if (auto m = v.AsMap()) for (const auto& kv : *m) (*out.AsMap())[kv.first] = VsDeepCopy(kv.second, depth + 1);
        return out;
    }
    return v;
}

namespace {
bool Fit(const VarType& t, VsValue& v, const std::vector<StructDef>& structs, std::string* err, int depth) {
    auto bad = [&](const std::string& why) { if (err) *err = why; return false; };
    auto expected = [&]() { return bad(std::string("expected ") + VarTypeText(t) + ", got " + VsKindName(v)); };
    if (depth > 32) return bad("nested too deep");
    if (t.IsStruct()) {
        const StructDef* sd = FindStruct(structs, t.structName);
        if (!sd) return bad("unknown struct '" + t.structName + "'");
        if (!v.IsMap()) return expected();
        auto m = v.AsMap();
        for (const auto& kv : *m) {
            bool known = false;
            for (const auto& f : sd->fields) if (f.name == kv.first) known = true;
            if (!known) return bad("'" + t.structName + "' has no field '" + kv.first + "'");
        }
        for (const auto& f : sd->fields) {
            auto it = m->find(f.name);
            if (it == m->end()) { (*m)[f.name] = VsDeepCopy(f.def); continue; }
            std::string sub;
            if (!Fit(f.type, it->second, structs, &sub, depth + 1)) return bad("field " + f.name + ": " + sub);
        }
        return true;
    }
    switch (t.base) {
        case PinType::Any: return true;
        case PinType::Number:
            if (v.IsNumber()) return true;
            if (v.IsBool()) { v = VsValue(v.AsFloat()); return true; }
            if (v.IsString()) { float f; if (VsParseFinite(v.AsString(), f)) { v = VsValue(f); return true; } }
            return expected();
        case PinType::Bool:
            if (v.IsBool()) return true;
            if (v.IsNumber() || v.IsString()) { v = VsValue(v.AsBool()); return true; }
            return expected();
        case PinType::Text:
            if (v.IsString()) return true;
            if (v.IsNumber() || v.IsBool() || v.IsVec3() || v.IsColor()) { v = VsValue(v.AsString()); return true; }
            return expected();
        case PinType::Vec3:
            if (v.IsVec3()) return true;
            if (v.IsColor() || v.IsNumber()) { v = VsValue(v.AsVec3()); return true; }
            return expected();
        case PinType::Color:
            if (v.IsColor()) return true;
            if (v.IsVec3()) { v = VsValue(v.AsColor()); return true; }
            if (v.IsString()) { VsValue p; if (VsParseLiteral(v.AsString(), p) && p.IsColor()) { v = p; return true; } }
            return expected();
        case PinType::Object:
            if (v.IsObject() || v.IsString()) return true;
            if (v.IsNumber() && v.AsFloat() == 0.0f) { v = VsValue::Object(0); return true; }
            return expected();
        case PinType::List: {
            if (!v.IsArray()) return expected();
            auto a = v.AsArray();
            if (t.elem == PinType::Any) return true;
            VarType et; et.base = t.elem;
            for (std::size_t i = 0; i < a->size(); ++i) {
                std::string sub;
                if (!Fit(et, (*a)[i], structs, &sub, depth + 1)) return bad("item " + std::to_string(i) + ": " + sub);
            }
            return true;
        }
        case PinType::Map: {
            if (!v.IsMap()) return expected();
            auto m = v.AsMap();
            if (t.elem == PinType::Any) return true;
            VarType et; et.base = t.elem;
            for (auto& kv : *m) {
                std::string sub;
                if (!Fit(et, kv.second, structs, &sub, depth + 1)) return bad("entry '" + kv.first + "': " + sub);
            }
            return true;
        }
    }
    return true;
}
} // namespace

bool VsCoerce(const VarType& t, VsValue& v, const std::vector<StructDef>& structs, std::string* err) {
    return Fit(t, v, structs, err, 0);
}

bool VsParseTyped(const VarType& t, const std::string& text, const std::vector<StructDef>& structs, VsValue& out, std::string* err) {
    VsValue v;
    std::string perr;
    if (!VsParseLiteral(text, v, &perr)) {
        // A bare word is text when the type wants text or an object name.
        if (t.base == PinType::Text || t.base == PinType::Object) v = VsValue(text);
        else { if (err) *err = perr; return false; }
    }
    if (!VsCoerce(t, v, structs, err)) return false;
    out = std::move(v);
    return true;
}

int VsCompare(const VsValue& a, const VsValue& b) {
    auto rank = [](const VsValue& v) {
        if (v.IsNumber()) return 0; if (v.IsBool()) return 1; if (v.IsString()) return 2; if (v.IsVec3()) return 3;
        if (v.IsColor()) return 4; if (v.IsObject()) return 5; if (v.IsArray()) return 6; if (v.IsMap()) return 7; return 8;
    };
    const int ra = rank(a), rb = rank(b);
    if (ra != rb) return ra < rb ? -1 : 1;
    auto cmpF = [](float x, float y) {
        const bool nx = std::isnan(x), ny = std::isnan(y);
        if (nx || ny) return nx == ny ? 0 : (nx ? -1 : 1);
        return x < y ? -1 : x > y ? 1 : 0;
    };
    switch (ra) {
        case 0: return cmpF(a.AsFloat(), b.AsFloat());
        case 1: return (int)a.AsBool() - (int)b.AsBool();
        case 2: { const int c = a.AsString().compare(b.AsString()); return c < 0 ? -1 : c > 0 ? 1 : 0; }
        case 3: { const Vec3 x = a.AsVec3(), y = b.AsVec3();
                  if (int c = cmpF(x.x, y.x)) return c; if (int c = cmpF(x.y, y.y)) return c; return cmpF(x.z, y.z); }
        case 4: { const VsColor x = a.AsColor(), y = b.AsColor();
                  if (int c = cmpF(x.r, y.r)) return c; if (int c = cmpF(x.g, y.g)) return c;
                  if (int c = cmpF(x.b, y.b)) return c; return cmpF(x.a, y.a); }
        case 5: return a.AsObjectId() < b.AsObjectId() ? -1 : a.AsObjectId() > b.AsObjectId() ? 1 : 0;
        case 6: return cmpF((float)(a.AsArray() ? a.AsArray()->size() : 0), (float)(b.AsArray() ? b.AsArray()->size() : 0));
        case 7: return cmpF((float)(a.AsMap() ? a.AsMap()->size() : 0), (float)(b.AsMap() ? b.AsMap()->size() : 0));
    }
    return 0;
}

} // namespace okay::vs
