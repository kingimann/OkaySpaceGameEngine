// The standalone OkayScript value. Written for this project.
// dup-allow-begin: the conversion rules of the engine's vs::VsValue (engine/include/okay/VisualScript/VsValue.hpp), repeated so the standalone value behaves the same; one value type for both is open (docs/status/UNFINISHED.md "OkayScript standalone core")
#include "okayscript/Value.hpp"
#include <cctype>
#include <cstdlib>
#include <vector>

namespace okayscript {

namespace {
HandleNameFn g_handleName = nullptr;
HandleAliveFn g_handleAlive = nullptr;
} // namespace
void SetHandleHooks(HandleNameFn name, HandleAliveFn alive) { g_handleName = name; g_handleAlive = alive; }
std::string HandleName(std::uint64_t id) { return g_handleName ? g_handleName(id) : std::string(); }
bool HandleAlive(std::uint64_t id) { return g_handleAlive ? g_handleAlive(id) : id != 0; }

bool Value::ParseFinite(const std::string& s, float& out) {
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

Value::ArrayRef Value::AsArray() const {
    if (auto* a = std::get_if<ArrayRef>(&m_data)) return *a;
    return nullptr;
}
Value::MapRef Value::AsMap() const {
    if (auto* m = std::get_if<MapRef>(&m_data)) return *m;
    return nullptr;
}
std::uint64_t Value::AsObjectId() const {
    if (auto* o = std::get_if<Handle>(&m_data)) return o->id;
    return 0;
}
Color4 Value::AsColor() const {
    if (auto* c = std::get_if<Color4>(&m_data)) return *c;
    if (auto* v = std::get_if<Vec3>(&m_data)) return Color4{v->x, v->y, v->z, 1.0f};
    return Color4{};
}

float Value::AsFloat() const {
    if (auto* f = std::get_if<Number>(&m_data)) return *f;
    if (auto* i = std::get_if<Int64>(&m_data)) return (float)i->v;
    if (auto* b = std::get_if<bool>(&m_data)) return *b ? 1.0f : 0.0f;
    if (auto* v = std::get_if<Vec3>(&m_data)) return v->Magnitude();
    if (auto* c = std::get_if<Color4>(&m_data)) return c->a;
    if (auto* a = std::get_if<ArrayRef>(&m_data)) return *a ? (float)(*a)->size() : 0.0f;
    if (auto* m = std::get_if<MapRef>(&m_data)) return *m ? (float)(*m)->size() : 0.0f;
    if (auto* s = std::get_if<std::string>(&m_data)) {
        float n = 0.0f;
        if (ParseFinite(*s, n)) return n;
    }
    return 0.0f;
}

bool Value::AsBool() const {
    if (auto* b = std::get_if<bool>(&m_data)) return *b;
    if (auto* f = std::get_if<Number>(&m_data)) return !std::isnan(*f) && *f != 0.0f;
    if (auto* i = std::get_if<Int64>(&m_data)) return i->v != 0;
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
        if (ParseFinite(*s, n)) return n != 0.0f;
        return true;   // any other text is on
    }
    if (auto* c = std::get_if<Color4>(&m_data)) return c->a > 0.0f;
    if (auto* a = std::get_if<ArrayRef>(&m_data)) return *a && !(*a)->empty();
    if (auto* m = std::get_if<MapRef>(&m_data)) return *m && !(*m)->empty();
    if (auto* o = std::get_if<Handle>(&m_data)) return o->id != 0 && HandleAlive(o->id);
    return false;
}

Vec3 Value::AsVec3() const {
    if (auto* v = std::get_if<Vec3>(&m_data)) return *v;
    if (auto* f = std::get_if<Number>(&m_data)) return Vec3{*f};
    if (auto* i = std::get_if<Int64>(&m_data)) return Vec3{(float)i->v};
    if (auto* c = std::get_if<Color4>(&m_data)) return Vec3{c->r, c->g, c->b};
    return Vec3::Zero;
}

namespace {
// The lists and maps being printed now (outermost first): one that contains itself prints as [...] / {...}, and
// nesting deeper than kMaxPrintDepth stops there, so printing never overflows the stack.
thread_local std::vector<const void*> t_printing;
constexpr std::size_t kMaxPrintDepth = 200;
struct Printing {
    bool cycle = false;
    explicit Printing(const void* p) {
        cycle = t_printing.size() >= kMaxPrintDepth;
        for (const void* q : t_printing) if (q == p) cycle = true;
        if (!cycle) t_printing.push_back(p);
    }
    ~Printing() { if (!cycle) t_printing.pop_back(); }
};
} // namespace

std::string Value::AsString() const {
    if (auto* s = std::get_if<std::string>(&m_data)) return *s;
    if (auto* b = std::get_if<bool>(&m_data)) return *b ? "true" : "false";
    if (auto* f = std::get_if<Number>(&m_data)) return FormatNumber(*f);
    if (auto* i = std::get_if<Int64>(&m_data)) return std::to_string(i->v);
    if (auto* v = std::get_if<Vec3>(&m_data))
        return "(" + FormatNumber(v->x) + ", " + FormatNumber(v->y) + ", " + FormatNumber(v->z) + ")";
    if (auto* c = std::get_if<Color4>(&m_data)) return HexColor(*c);
    if (auto* a = std::get_if<ArrayRef>(&m_data)) {
        if (!*a) return "[]";
        Printing guard(a->get());
        if (guard.cycle) return "[...]";
        std::string s = "[";
        for (std::size_t i = 0; i < (*a)->size(); ++i) {
            if (i) s += ", ";
            s += (**a)[i].AsString();
        }
        return s + "]";
    }
    if (auto* m = std::get_if<MapRef>(&m_data)) {
        if (!*m) return "{}";
        Printing guard(m->get());
        if (guard.cycle) return "{...}";
        std::string s = "{";
        bool first = true;
        for (auto& kv : **m) {
            if (!first) s += ", ";
            first = false;
            s += kv.first + ": " + kv.second.AsString();
        }
        return s + "}";
    }
    if (auto* o = std::get_if<Handle>(&m_data)) return o->id ? HandleName(o->id) : std::string();
    return {};
}

const char* Value::TypeName() const {
    if (IsInt()) return "int";
    if (IsNumber()) return "number";
    if (IsBool()) return "bool";
    if (IsString()) return "string";
    if (IsVec3()) return "vector";
    if (IsColor()) return "colour";
    if (IsObject()) return "object";
    if (IsArray()) return "list";
    if (auto m = AsMap(); m && (m->count("__lambda") || m->count("__fn"))) return "function";
    return "map";
}

std::string Value::HexColor(const Color4& c) {
    auto byte = [](float f) { return (unsigned)(std::lround(std::fmin(std::fmax(std::isnan(f) ? 0.0f : f, 0.0f), 1.0f) * 255.0f)); };
    static const char* hex = "0123456789abcdef";
    std::string s = "#";
    for (unsigned b : {byte(c.r), byte(c.g), byte(c.b), byte(c.a)}) { s += hex[b >> 4]; s += hex[b & 15]; }
    return s;
}

std::string Value::FormatNumber(float f) {
    if (std::isnan(f)) return "nan";
    if (std::isinf(f)) return f < 0 ? "-inf" : "inf";
    if (std::fabs(f) < 9.0e18f && f == static_cast<float>(static_cast<long long>(f)))
        return std::to_string(static_cast<long long>(f));
    std::string s = std::to_string(f);
    const std::size_t dot = s.find('.');
    if (dot != std::string::npos) {
        const std::size_t last = s.find_last_not_of('0');
        if (last > dot) s.erase(last + 1); else s.erase(dot);
    }
    return s;
}

} // namespace okayscript
// dup-allow-end
