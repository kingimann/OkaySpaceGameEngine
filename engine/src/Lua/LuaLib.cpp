// Standard library of our own Lua 5.4 (base, string with patterns, table, math, os, io,
// coroutine). Written from chapter 6 of the Lua 5.4 reference manual, in our own words.
// Sandbox: no file access, no os.execute/exit/remove, no loading of outside code.
#include "LuaCore.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace okay::lua {

namespace {

// ------------------------------------------------------------ argument helpers
struct A {
    State& L; std::vector<Value>& a; const char* fn;
    std::size_t n() const { return a.size(); }
    Value at(std::size_t i) const { return i < a.size() ? a[i] : Value(); }
    [[noreturn]] void Bad(std::size_t i, const std::string& msg) const {
        L.Error("bad argument #" + std::to_string(i + 1) + " to '" + fn + "' (" + msg + ")");
    }
    [[noreturn]] void TypeErr(std::size_t i, const char* want) const {
        Bad(i, std::string(want) + " expected, got " + (i < a.size() ? L.TypeName(a[i]) : "no value"));
    }
    Table* Tab(std::size_t i) const { if (at(i).t != T::Table) TypeErr(i, "table"); return static_cast<Table*>(a[i].o); }
    std::int64_t Int(std::size_t i) const {
        Value v = at(i);
        if (v.t == T::Int) return v.i;
        Value n;
        if (ToNumber(v, n)) { std::int64_t r; if (ToInteger(n, r)) return r; Bad(i, "number has no integer representation"); }
        TypeErr(i, "number");
    }
    std::int64_t OptInt(std::size_t i, std::int64_t d) const { return at(i).IsNil() ? d : Int(i); }
    double Num(std::size_t i) const {
        Value n;
        if (!ToNumber(at(i), n)) TypeErr(i, "number");
        return n.t == T::Int ? (double)n.i : n.n;
    }
    Value NumV(std::size_t i) const { Value n; if (!ToNumber(at(i), n)) TypeErr(i, "number"); return n; }
    const std::string& Str(std::size_t i) {
        Value v = at(i);
        if (v.t == T::Str) return static_cast<okay::lua::Str*>(v.o)->s;
        if (v.IsNumber()) { a[i] = L.NewStr(FormatNumber(v)); return static_cast<okay::lua::Str*>(a[i].o)->s; }
        TypeErr(i, "string");
    }
    void Any(std::size_t i) const { if (i >= a.size()) Bad(i, "value expected"); }
};

#define DEF(L, tbl, name, ...) \
    tbl->Set(L, L.NewStr(name), L.Fn(name, [](State& L, std::vector<Value>& args, std::vector<Value>& r) { \
        A a{L, args, name}; (void)a; (void)r; __VA_ARGS__ }))

void SetFn(State& L, Table* t, const char* name, NativeFn fn) { t->Set(L, L.NewStr(name), L.Fn(name, std::move(fn))); }

std::int64_t PosRel(std::int64_t pos, std::size_t len) {
    if (pos >= 0) return pos;
    if ((std::size_t)(-pos) > len) return 0;
    return (std::int64_t)len + pos + 1;
}

// ------------------------------------------------------------ patterns
constexpr int kMaxCaps = 32;
constexpr int kCapUnf = -1, kCapPos = -2;

struct M {
    State& L;
    const char* src; const char* send; const char* pend;
    int level = 0, depth = 0;
    struct Cap { const char* init; std::ptrdiff_t len; } cap[kMaxCaps];
};

bool ClassMatch(int c, int cl) {
    bool res;
    switch (std::tolower(cl)) {
        case 'a': res = std::isalpha(c); break;
        case 'c': res = std::iscntrl(c); break;
        case 'd': res = std::isdigit(c); break;
        case 'g': res = std::isgraph(c); break;
        case 'l': res = std::islower(c); break;
        case 'p': res = std::ispunct(c); break;
        case 's': res = std::isspace(c); break;
        case 'u': res = std::isupper(c); break;
        case 'w': res = std::isalnum(c); break;
        case 'x': res = std::isxdigit(c); break;
        default: return cl == c;
    }
    return std::isupper(cl) ? !res : res;
}

const char* ClassEnd(M& m, const char* p) {
    if (*p == '%') {
        if (p + 1 >= m.pend) m.L.Error("malformed pattern (ends with '%')");
        return p + 2;
    }
    if (*p == '[') {
        ++p;
        if (p < m.pend && *p == '^') ++p;
        do {
            if (p >= m.pend) m.L.Error("malformed pattern (missing ']')");
            char c = *p++;
            if (c == '%') { if (p >= m.pend) m.L.Error("malformed pattern (missing ']')"); ++p; }
        } while (p >= m.pend ? (m.L.Error("malformed pattern (missing ']')"), false) : *p != ']');
        return p + 1;
    }
    return p + 1;
}

bool SetMatch(int c, const char* p, const char* ec) {   // p at '[', ec at closing ']'
    bool neg = false;
    ++p;
    if (*p == '^') { neg = true; ++p; }
    while (p < ec) {
        if (*p == '%') { ++p; if (ClassMatch(c, (unsigned char)*p)) return !neg; ++p; }
        else if (p + 2 < ec && p[1] == '-') {
            if ((unsigned char)p[0] <= c && c <= (unsigned char)p[2]) return !neg;
            p += 3;
        } else { if ((unsigned char)*p == c) return !neg; ++p; }
    }
    return neg;
}

bool Single(M& m, const char* s, const char* p, const char* ep) {
    if (s >= m.send) return false;
    int c = (unsigned char)*s;
    switch (*p) {
        case '.': return true;
        case '%': return ClassMatch(c, (unsigned char)p[1]);
        case '[': return SetMatch(c, p, ep - 1);
        default: return (unsigned char)*p == c;
    }
}

const char* DoMatch(M& m, const char* s, const char* p);

const char* MaxExpand(M& m, const char* s, const char* p, const char* ep) {
    std::ptrdiff_t i = 0;
    while (Single(m, s + i, p, ep)) ++i;
    while (i >= 0) {
        const char* r = DoMatch(m, s + i, ep + 1);
        if (r) return r;
        --i;
    }
    return nullptr;
}
const char* MinExpand(M& m, const char* s, const char* p, const char* ep) {
    for (;;) {
        const char* r = DoMatch(m, s, ep + 1);
        if (r) return r;
        if (Single(m, s, p, ep)) ++s; else return nullptr;
    }
}

int CapIndexForBackref(M& m, int l) {
    l -= '1';
    if (l < 0 || l >= m.level || m.cap[l].len == kCapUnf) m.L.Error("invalid capture index %" + std::to_string(l + 1));
    return l;
}

const char* DoMatch(M& m, const char* s, const char* p) {
    if (++m.depth > 200) m.L.Error("pattern too complex");
    struct D { M& m; ~D() { --m.depth; } } guard{m};
    for (;;) {
        if (p == m.pend) return s;
        switch (*p) {
            case '(': {
                if (m.level >= kMaxCaps) m.L.Error("too many captures");
                bool pos = (p + 1 < m.pend && p[1] == ')');
                m.cap[m.level].init = s; m.cap[m.level].len = pos ? kCapPos : kCapUnf;
                ++m.level;
                const char* r = DoMatch(m, s, pos ? p + 2 : p + 1);
                if (!r) --m.level;
                return r;
            }
            case ')': {
                int l = -1;
                for (int i = m.level - 1; i >= 0; --i) if (m.cap[i].len == kCapUnf) { l = i; break; }
                if (l < 0) m.L.Error("invalid pattern capture");
                m.cap[l].len = s - m.cap[l].init;
                const char* r = DoMatch(m, s, p + 1);
                if (!r) m.cap[l].len = kCapUnf;
                return r;
            }
            case '$':
                if (p + 1 == m.pend) return s == m.send ? s : nullptr;
                break;
            case '%':
                if (p + 1 < m.pend) {
                    char nx = p[1];
                    if (nx == 'b') {
                        if (p + 3 >= m.pend) m.L.Error("malformed pattern (missing arguments to '%b')");
                        if (s >= m.send || *s != p[2]) return nullptr;
                        int b = (unsigned char)p[2], e = (unsigned char)p[3], cont = 1;
                        const char* q = s + 1;
                        for (; q < m.send; ++q) {
                            if ((unsigned char)*q == e) { if (--cont == 0) break; }
                            else if ((unsigned char)*q == b) ++cont;
                        }
                        if (q >= m.send) return nullptr;
                        s = q + 1; p += 4; continue;
                    }
                    if (nx == 'f') {
                        p += 2;
                        if (p >= m.pend || *p != '[') m.L.Error("missing '[' after '%f' in pattern");
                        const char* ep = ClassEnd(m, p);
                        int prev = (s == m.src) ? 0 : (unsigned char)s[-1];
                        int curc = (s < m.send) ? (unsigned char)*s : 0;
                        if (!SetMatch(prev, p, ep - 1) && SetMatch(curc, p, ep - 1)) { p = ep; continue; }
                        return nullptr;
                    }
                    if (nx >= '0' && nx <= '9') {
                        int l = CapIndexForBackref(m, nx);
                        std::size_t len = (std::size_t)m.cap[l].len;
                        if ((std::size_t)(m.send - s) >= len && std::memcmp(m.cap[l].init, s, len) == 0) { s += len; p += 2; continue; }
                        return nullptr;
                    }
                }
                break;
            default: break;
        }
        const char* ep = ClassEnd(m, p);
        char epc = ep < m.pend ? *ep : '\0';
        if (epc == '?') {
            if (Single(m, s, p, ep)) { const char* r = DoMatch(m, s + 1, ep + 1); if (r) return r; }
            p = ep + 1; continue;
        }
        if (epc == '+') return Single(m, s, p, ep) ? MaxExpand(m, s + 1, p, ep) : nullptr;
        if (epc == '*') return MaxExpand(m, s, p, ep);
        if (epc == '-') return MinExpand(m, s, p, ep);
        if (!Single(m, s, p, ep)) return nullptr;
        ++s; p = ep;
    }
}

Value GetCapture(M& m, int i, const char* s, const char* e) {
    if (i >= m.level) {
        if (i == 0) return m.L.NewStr(std::string(s, e - s));
        m.L.Error("invalid capture index %" + std::to_string(i + 1));
    }
    if (m.cap[i].len == kCapUnf) m.L.Error("unfinished capture");
    if (m.cap[i].len == kCapPos) return Value::Int((std::int64_t)(m.cap[i].init - m.src) + 1);
    return m.L.NewStr(std::string(m.cap[i].init, (std::size_t)m.cap[i].len));
}
int PushCaptures(M& m, const char* s, const char* e, std::vector<Value>& out, bool wholeIfNone) {
    int n = (m.level == 0 && wholeIfNone) ? 1 : m.level;
    for (int i = 0; i < n; ++i) out.push_back(GetCapture(m, i, s, e));
    return n;
}

bool NoSpecials(const std::string& p) { return p.find_first_of("^$*+?.([%-") == std::string::npos; }

void StrFindAux(A& a, std::vector<Value>& r, bool find) {
    const std::string s = a.Str(0), p = a.Str(1);
    std::int64_t init = PosRel(a.OptInt(2, 1), s.size());
    if (init < 1) init = 1;
    if ((std::size_t)init > s.size() + 1) { r.push_back(Value()); return; }
    if (find && (a.at(3).Truthy() || NoSpecials(p))) {
        std::size_t pos = s.find(p, (std::size_t)init - 1);
        if (pos == std::string::npos) { r.push_back(Value()); return; }
        r.push_back(Value::Int((std::int64_t)pos + 1)); r.push_back(Value::Int((std::int64_t)(pos + p.size())));
        return;
    }
    M m{a.L, s.data(), s.data() + s.size(), p.data() + p.size()};
    const char* pat = p.data();
    bool anchor = !p.empty() && p[0] == '^';
    if (anchor) ++pat;
    const char* sp = s.data() + init - 1;
    do {
        m.level = 0; m.depth = 0;
        const char* e = DoMatch(m, sp, pat);
        if (e) {
            if (find) {
                r.push_back(Value::Int((std::int64_t)(sp - s.data()) + 1));
                r.push_back(Value::Int((std::int64_t)(e - s.data())));
                PushCaptures(m, nullptr, nullptr, r, false);
            } else PushCaptures(m, sp, e, r, true);
            return;
        }
    } while (sp++ < m.send && !anchor);
    r.push_back(Value());
}

void AddRepl(M& m, std::string& b, const char* s, const char* e, const Value& repl, A& a) {
    Value res;
    if (repl.t == T::Str || repl.IsNumber()) {
        const std::string rs = m.L.RawToString(repl);
        for (std::size_t i = 0; i < rs.size(); ++i) {
            if (rs[i] != '%') { b += rs[i]; continue; }
            ++i;
            if (i >= rs.size()) m.L.Error("invalid use of '%' in replacement string");
            if (rs[i] == '%') b += '%';
            else if (std::isdigit((unsigned char)rs[i])) {
                if (rs[i] == '0') b.append(s, e - s);
                else { Value c = GetCapture(m, rs[i] - '1', s, e); b += m.L.RawToString(c); }
            } else m.L.Error("invalid use of '%' in replacement string");
        }
        return;
    }
    Value first = GetCapture(m, 0, s, e);
    if (repl.t == T::Table) res = m.L.Index(repl, first);
    else {
        std::vector<Value> caps; PushCaptures(m, s, e, caps, true);
        std::vector<Value> pin = caps; m.L.pins.push_back(&pin);
        try { auto rr = m.L.Call(repl, caps, 1); res = rr.empty() ? Value() : rr[0]; }
        catch (...) { m.L.pins.pop_back(); throw; }
        m.L.pins.pop_back();
    }
    if (!res.Truthy()) b.append(s, e - s);
    else if (res.t == T::Str || res.IsNumber()) b += m.L.RawToString(res);
    else m.L.Error(std::string("invalid replacement value (a ") + m.L.TypeName(res) + ")");
    (void)a;
}

// ------------------------------------------------------------ string.format
void Format(A& a, std::vector<Value>& r) {
    State& L = a.L;
    const std::string fmt = a.Str(0);
    std::string out;
    std::size_t arg = 1;
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%') { out += fmt[i]; continue; }
        if (++i >= fmt.size()) L.Error("invalid conversion '%' to 'format'");
        if (fmt[i] == '%') { out += '%'; continue; }
        std::string spec = "%";
        while (i < fmt.size() && std::strchr("-+ #0", fmt[i])) spec += fmt[i++];
        while (i < fmt.size() && std::isdigit((unsigned char)fmt[i])) spec += fmt[i++];
        if (i < fmt.size() && fmt[i] == '.') { spec += fmt[i++]; while (i < fmt.size() && std::isdigit((unsigned char)fmt[i])) spec += fmt[i++]; }
        if (spec.size() > 20) L.Error("invalid conversion (too long) to 'format'");
        if (i >= fmt.size()) L.Error("invalid conversion '" + spec + "' to 'format'");
        char conv = fmt[i];
        char buf[512];
        switch (conv) {
            case 'd': case 'i': {
                std::int64_t v = a.Int(arg++);
                std::snprintf(buf, sizeof buf, (spec + "lld").c_str(), (long long)v); out += buf; break;
            }
            case 'u': case 'o': case 'x': case 'X': {
                std::int64_t v = a.Int(arg++);
                std::snprintf(buf, sizeof buf, (spec + "ll" + conv).c_str(), (unsigned long long)v); out += buf; break;
            }
            case 'c': out += (char)a.Int(arg++); break;
            case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A': {
                double v = a.Num(arg++);
                std::snprintf(buf, sizeof buf, (spec + conv).c_str(), v); out += buf; break;
            }
            case 's': {
                a.Any(arg);
                std::string s = L.ToStringValue(a.at(arg++));
                if (spec == "%") out += s;
                else { L.Reserve(s.size() + 512); std::vector<char> big(s.size() + 512); std::snprintf(big.data(), big.size(), (spec + "s").c_str(), s.c_str()); out += big.data(); }
                break;
            }
            case 'q': {
                a.Any(arg);
                Value v = a.at(arg++);
                if (v.t == T::Str) {
                    out += '"';
                    for (unsigned char c : static_cast<Str*>(v.o)->s) {
                        if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
                        else if (c == '\n') out += "\\n";
                        else if (c == '\r') out += "\\r";
                        else if (c == 0) out += "\\0";
                        else if (c < 32) { char t[8]; std::snprintf(t, sizeof t, "\\%d", c); out += t; }
                        else out += (char)c;
                    }
                    out += '"';
                } else out += L.RawToString(v);
                break;
            }
            default: L.Error(std::string("invalid conversion '") + spec + conv + "' to 'format'");
        }
        L.Reserve(out.size());
    }
    r.push_back(L.NewStr(out));
}

// ------------------------------------------------------------ random
struct Rng {
    std::uint64_t s[2];
    void Seed(std::uint64_t x) {
        auto mix = [](std::uint64_t& z) { z += 0x9E3779B97F4A7C15ull; std::uint64_t t = z; t = (t ^ (t >> 30)) * 0xBF58476D1CE4E5B9ull; t = (t ^ (t >> 27)) * 0x94D049BB133111EBull; return t ^ (t >> 31); };
        s[0] = mix(x); s[1] = mix(x);
        if (!s[0] && !s[1]) s[0] = 1;
    }
    std::uint64_t Next() {   // xorshift128+
        std::uint64_t a = s[0], b = s[1];
        s[0] = b; a ^= a << 23; s[1] = a ^ b ^ (a >> 17) ^ (b >> 26);
        return s[1] + b;
    }
};

}  // namespace

// ============================================================ base
void OpenBase(State& L) {
    Table* G = L.globals;
    L.stringMeta->Set(L, L.NewStr("__index"), Value());   // filled by OpenString
    G->Set(L, L.NewStr("_VERSION"), L.NewStr("Lua 5.4"));

    SetFn(L, G, "print", [](State& L, std::vector<Value>& a, std::vector<Value>&) {
        std::string s;
        for (std::size_t i = 0; i < a.size(); ++i) { if (i) s += '\t'; s += L.ToStringValue(a[i]); }
        if (L.out) L.out(s); else std::printf("%s\n", s.c_str());
    });
    DEF(L, G, "type", { a.Any(0); r.push_back(L.NewStr(L.TypeName(a.at(0)))); });
    DEF(L, G, "tostring", { a.Any(0); r.push_back(L.NewStr(L.ToStringValue(a.at(0)))); });
    DEF(L, G, "tonumber", {
        Value v = a.at(0);
        if (a.at(1).IsNil()) {
            a.Any(0);
            Value n; if (v.IsNumber()) r.push_back(v);
            else if (v.t == T::Str && ParseNumber(static_cast<Str*>(v.o)->s, n)) r.push_back(n);
            else r.push_back(Value());
        } else {
            std::int64_t base = a.Int(1);
            if (base < 2 || base > 36) a.Bad(1, "base out of range");
            if (v.t != T::Str) a.TypeErr(0, "string");
            std::string s = static_cast<Str*>(v.o)->s;
            std::size_t b = 0, e = s.size();
            while (b < e && std::isspace((unsigned char)s[b])) ++b;
            while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
            bool neg = false;
            if (b < e && s[b] == '-') { neg = true; ++b; }
            if (b >= e) { r.push_back(Value()); return; }
            std::uint64_t acc = 0; bool ok = true;
            for (; b < e; ++b) {
                int c = (unsigned char)s[b], d;
                if (std::isdigit(c)) d = c - '0'; else if (std::isalpha(c)) d = std::tolower(c) - 'a' + 10; else { ok = false; break; }
                if (d >= base) { ok = false; break; }
                acc = acc * (std::uint64_t)base + (std::uint64_t)d;
            }
            if (!ok) r.push_back(Value()); else r.push_back(Value::Int((std::int64_t)(neg ? 0 - acc : acc)));
        }
    });
    DEF(L, G, "rawget", { r.push_back(a.Tab(0)->Get(a.at(1))); });
    DEF(L, G, "rawset", {
        Table* t = a.Tab(0);
        if (a.at(1).IsNil()) L.Error("table index is nil");
        t->Set(L, a.at(1), a.at(2)); r.push_back(a.at(0));
    });
    DEF(L, G, "rawequal", { r.push_back(Value::Bool(L.RawEquals(a.at(0), a.at(1)))); });
    DEF(L, G, "rawlen", {
        if (a.at(0).t == T::Table) r.push_back(Value::Int(a.Tab(0)->Length()));
        else if (a.at(0).t == T::Str) r.push_back(Value::Int((std::int64_t)a.Str(0).size()));
        else a.Bad(0, "table or string expected");
    });
    DEF(L, G, "setmetatable", {
        Table* t = a.Tab(0);
        if (a.at(1).t != T::Table && !a.at(1).IsNil()) a.TypeErr(1, "nil or table");
        if (t->meta && !t->meta->GetStr("__metatable").IsNil()) L.Error("cannot change a protected metatable");
        t->meta = a.at(1).IsNil() ? nullptr : static_cast<Table*>(a.at(1).o);
        r.push_back(a.at(0));
    });
    DEF(L, G, "getmetatable", {
        a.Any(0);
        Table* m = L.MetaOf(a.at(0));
        if (!m) { r.push_back(Value()); return; }
        Value p = m->GetStr("__metatable");
        r.push_back(p.IsNil() ? Value::Of(m) : p);
    });
    DEF(L, G, "assert", {
        a.Any(0);
        if (!a.at(0).Truthy()) {
            if (a.n() > 1) L.ErrorValue(a.at(1));
            L.Error("assertion failed!");
        }
        r = a.a;
    });
    DEF(L, G, "error", {
        Value v = a.at(0);
        std::int64_t level = a.OptInt(1, 1);
        if (v.t == T::Str && level > 0) v = L.NewStr(L.Where((int)level) + static_cast<Str*>(v.o)->s);
        L.ErrorValue(v);
    });
    DEF(L, G, "select", {
        Value n = a.at(0);
        if (n.t == T::Str && static_cast<Str*>(n.o)->s == "#") { r.push_back(Value::Int((std::int64_t)a.n() - 1)); return; }
        std::int64_t i = a.Int(0);
        if (i < 0) i = (std::int64_t)a.n() + i; else if (i > (std::int64_t)a.n() - 1) i = (std::int64_t)a.n();
        if (i < 1) a.Bad(0, "index out of range");
        for (std::size_t k = (std::size_t)i; k < a.n(); ++k) r.push_back(a.a[k]);
    });
    {
        Native* nx = L.NewNative("next", [](State& L, std::vector<Value>& args, std::vector<Value>& r) {
            A a{L, args, "next"};
            Table* t = a.Tab(0);
            Value k, v; bool bad;
            if (t->Next(a.at(1), k, v, bad)) { r.push_back(k); r.push_back(v); }
            else if (bad) L.Error("invalid key to 'next'");
            else r.push_back(Value());
        });
        G->Set(L, L.NewStr("next"), Value::Of(nx));
        Native* np = L.NewNative("pairs", [](State& L, std::vector<Value>& args, std::vector<Value>& r) {
            A a{L, args, "pairs"};
            a.Any(0);
            Value h = L.GetMeta(a.at(0), "__pairs");
            if (!h.IsNil()) { auto rr = L.Call(h, {a.at(0)}, 3); r = rr; return; }
            a.Tab(0);
            r.push_back(L.globals->GetStr("next")); r.push_back(a.at(0)); r.push_back(Value());
        });
        G->Set(L, L.NewStr("pairs"), Value::Of(np));
        Native* it = L.NewNative("ipairs_iter", [](State& L, std::vector<Value>& args, std::vector<Value>& r) {
            std::int64_t i = (args.size() > 1 && args[1].t == T::Int ? args[1].i : 0) + 1;
            Value v = L.Index(args.empty() ? Value() : args[0], Value::Int(i));
            if (v.IsNil()) r.push_back(Value()); else { r.push_back(Value::Int(i)); r.push_back(v); }
        });
        L.extraRoots.push_back(it);
        G->Set(L, L.NewStr("ipairs"), L.Fn("ipairs", [it](State& L, std::vector<Value>& args, std::vector<Value>& r) {
            A a{L, args, "ipairs"}; a.Any(0);
            r.push_back(Value::Of(it)); r.push_back(a.at(0)); r.push_back(Value::Int(0));
        }));
    }
    {
        auto plainPcall = [](bool x) {
            return [x](State& L, std::vector<Value>& a, std::vector<Value>& r) {
                if (a.size() < (x ? 2u : 1u)) L.Error(std::string("bad argument #") + (x ? "2" : "1") + " to '" + (x ? "xpcall" : "pcall") + "' (value expected)");
                Value f = a[0], h = x ? a[1] : Value();
                std::vector<Value> rest(a.begin() + (x ? 2 : 1), a.end());
                Coro* co = L.cur; std::size_t fsz = co->frames.size(), ssz = co->st.size();
                try { L.nextCallDirect = f.t == T::Native; auto res = L.Call(f, rest, -1); r.push_back(Value::Bool(true)); for (auto& v : res) r.push_back(v); }
                catch (LuaError& e) {
                    if (e.fatal) throw;
                    co->frames.resize(fsz); co->st.resize(ssz);
                    r.clear(); r.push_back(Value::Bool(false));
                    if (x) { auto hr = L.Call(h, {e.v}, 1); r.push_back(hr.empty() ? Value() : hr[0]); } else r.push_back(e.v);
                }
            };
        };
        Native* pc = L.NewNative("pcall", plainPcall(false)); pc->special = Special::PCall;
        G->Set(L, L.NewStr("pcall"), Value::Of(pc));
        Native* xp = L.NewNative("xpcall", plainPcall(true)); xp->special = Special::XPCall;
        G->Set(L, L.NewStr("xpcall"), Value::Of(xp));
    }
    DEF(L, G, "load", {
        Value c = a.at(0);
        std::string src;
        if (c.t == T::Str) src = static_cast<Str*>(c.o)->s;
        else if (c.t == T::Func || c.t == T::Native) {
            for (;;) {
                auto rr = L.Call(c, {}, 1);
                if (rr.empty() || rr[0].IsNil() || (rr[0].t == T::Str && static_cast<Str*>(rr[0].o)->s.empty())) break;
                if (rr[0].t != T::Str) L.Error("reader function must return a string");
                src += static_cast<Str*>(rr[0].o)->s;
                L.Reserve(src.size());
            }
        } else a.TypeErr(0, "string");
        std::string chunk;
        if (a.at(1).t == T::Str) { chunk = static_cast<Str*>(a.a[1].o)->s; if (!chunk.empty() && (chunk[0] == '=' || chunk[0] == '@')) chunk.erase(0, 1); else chunk = "[string \"" + chunk + "\"]"; }
        else {
            std::string first = src.substr(0, src.find('\n'));
            bool cut = first.size() != src.size();
            if (first.size() > 40) { first.resize(40); cut = true; }
            chunk = "[string \"" + first + (cut ? "...\"]" : "\"]");
        }
        if (!a.at(3).IsNil()) { r.push_back(Value()); r.push_back(L.NewStr("load: the env parameter is not supported by this Lua")); return; }
        std::string err;
        Proto* p = L.Compile(src, chunk, &err);
        if (!p) { r.push_back(Value()); r.push_back(L.NewStr(err)); return; }
        r.push_back(L.MakeFunction(p));
    });
    DEF(L, G, "collectgarbage", {
        std::string opt = a.at(0).IsNil() ? "collect" : a.Str(0);
        if (opt == "count") { r.push_back(Value::Num((double)L.allocated / 1024.0)); }
        else if (opt == "collect") { L.Collect(); r.push_back(Value::Int(0)); }
        else r.push_back(Value::Int(0));
    });
}

// ============================================================ string
void OpenString(State& L) {
    Table* S = L.NewTable();
    L.globals->Set(L, L.NewStr("string"), Value::Of(S));
    L.stringMeta->Set(L, L.NewStr("__index"), Value::Of(S));
    DEF(L, S, "len", { r.push_back(Value::Int((std::int64_t)a.Str(0).size())); });
    DEF(L, S, "sub", {
        const std::string& s = a.Str(0);
        std::int64_t i = PosRel(a.OptInt(1, 1), s.size()), j = PosRel(a.OptInt(2, -1), s.size());
        if (i < 1) i = 1;
        if (j > (std::int64_t)s.size()) j = (std::int64_t)s.size();
        r.push_back(i > j ? L.NewStr("") : L.NewStr(s.substr((std::size_t)i - 1, (std::size_t)(j - i + 1))));
    });
    DEF(L, S, "upper", { std::string s = a.Str(0); for (auto& c : s) c = (char)std::toupper((unsigned char)c); r.push_back(L.NewStr(s)); });
    DEF(L, S, "lower", { std::string s = a.Str(0); for (auto& c : s) c = (char)std::tolower((unsigned char)c); r.push_back(L.NewStr(s)); });
    DEF(L, S, "reverse", { std::string s = a.Str(0); std::reverse(s.begin(), s.end()); r.push_back(L.NewStr(s)); });
    DEF(L, S, "rep", {
        std::string s = a.Str(0); std::int64_t n = a.Int(1);
        std::string sep = a.at(2).IsNil() ? "" : a.Str(2);
        if (n <= 0) { r.push_back(L.NewStr("")); return; }
        double total = ((double)s.size() + (double)sep.size()) * (double)n;
        if (total > 2.0e9 || (L.limits.memoryBytes && total > (double)L.limits.memoryBytes)) L.Error("not enough memory (string.rep result too large)");
        L.Reserve((std::size_t)total);
        std::string out; out.reserve((std::size_t)total);
        for (std::int64_t i = 0; i < n; ++i) { if (i) out += sep; out += s; }
        r.push_back(L.NewStr(out));
    });
    DEF(L, S, "byte", {
        const std::string& s = a.Str(0);
        std::int64_t i = PosRel(a.OptInt(1, 1), s.size());
        std::int64_t j = PosRel(a.OptInt(2, i), s.size());
        if (i < 1) i = 1;
        if (j > (std::int64_t)s.size()) j = (std::int64_t)s.size();
        for (std::int64_t k = i; k <= j; ++k) r.push_back(Value::Int((unsigned char)s[(std::size_t)k - 1]));
    });
    DEF(L, S, "char", {
        std::string s;
        for (std::size_t i = 0; i < a.n(); ++i) { std::int64_t c = a.Int(i); if (c < 0 || c > 255) a.Bad(i, "value out of range"); s += (char)c; }
        r.push_back(L.NewStr(s));
    });
    DEF(L, S, "format", { Format(a, r); });
    DEF(L, S, "find", { StrFindAux(a, r, true); });
    DEF(L, S, "match", { StrFindAux(a, r, false); });
    DEF(L, S, "gmatch", {
        std::string s = a.Str(0), p = a.Str(1);
        auto pos = std::make_shared<std::size_t>(0);
        auto lastEnd = std::make_shared<std::ptrdiff_t>(-1);
        r.push_back(L.Fn("gmatch_iter", [s, p, pos, lastEnd](State& L, std::vector<Value>&, std::vector<Value>& out) {
            M m{L, s.data(), s.data() + s.size(), p.data() + p.size()};
            for (const char* sp = s.data() + *pos; sp <= m.send; ++sp) {
                m.level = 0; m.depth = 0;
                const char* e = DoMatch(m, sp, p.data());
                if (e && (std::ptrdiff_t)(e - s.data()) != *lastEnd) {
                    *pos = (std::size_t)(e - s.data()); *lastEnd = (std::ptrdiff_t)*pos;
                    PushCaptures(m, sp, e, out, true);
                    return;
                }
            }
            *pos = s.size() + 1;
            out.push_back(Value());
        }));
    });
    DEF(L, S, "gsub", {
        const std::string s = a.Str(0), p = a.Str(1);
        Value repl = a.at(2);
        if (!(repl.t == T::Str || repl.IsNumber() || repl.t == T::Table || repl.t == T::Func || repl.t == T::Native))
            a.TypeErr(2, "string/function/table");
        std::int64_t maxN = a.at(3).IsNil() ? INT64_MAX : a.Int(3);
        const char* pat = p.data();
        bool anchor = !p.empty() && p[0] == '^';
        if (anchor) ++pat;
        M m{L, s.data(), s.data() + s.size(), p.data() + p.size()};
        const char* sp = s.data();
        std::string out; std::int64_t n = 0;
        while (n < maxN) {
            m.level = 0; m.depth = 0;
            const char* e = DoMatch(m, sp, pat);
            if (e) {
                ++n;
                AddRepl(m, out, sp, e, repl, a);
                L.Reserve(out.size());
            }
            if (e && e > sp) sp = e;
            else if (sp < m.send) out += *sp++;
            else break;
            if (anchor) break;
        }
        out.append(sp, m.send - sp);
        r.push_back(L.NewStr(out)); r.push_back(Value::Int(n));
    });
}

// ============================================================ table
void OpenTable(State& L) {
    Table* T_ = L.NewTable();
    L.globals->Set(L, L.NewStr("table"), Value::Of(T_));
    DEF(L, T_, "insert", {
        a.Tab(0);
        std::int64_t n = L.LenOf(a.at(0), "").i;
        if (a.n() == 2) L.SetIndex(a.at(0), Value::Int(n + 1), a.at(1));
        else if (a.n() == 3) {
            std::int64_t pos = a.Int(1);
            if ((std::uint64_t)(pos - 1) >= (std::uint64_t)(n + 1)) a.Bad(1, "position out of bounds");
            for (std::int64_t i = n + 1; i > pos; --i) L.SetIndex(a.at(0), Value::Int(i), L.Index(a.at(0), Value::Int(i - 1)));
            L.SetIndex(a.at(0), Value::Int(pos), a.at(2));
        } else L.Error("wrong number of arguments to 'insert'");
    });
    DEF(L, T_, "remove", {
        a.Tab(0);
        std::int64_t n = L.LenOf(a.at(0), "").i;
        std::int64_t pos = a.OptInt(1, n);
        if (a.n() > 1 && n + 1 != pos && (std::uint64_t)(pos - 1) >= (std::uint64_t)(n + 1)) a.Bad(1, "position out of bounds");
        if (a.n() <= 1 && n == 0) { r.push_back(Value()); return; }
        Value v = L.Index(a.at(0), Value::Int(pos));
        for (; pos < n; ++pos) L.SetIndex(a.at(0), Value::Int(pos), L.Index(a.at(0), Value::Int(pos + 1)));
        if (pos <= n || a.n() > 1) L.SetIndex(a.at(0), Value::Int(pos), Value());
        r.push_back(v);
    });
    DEF(L, T_, "concat", {
        a.Tab(0);
        std::string sep = a.at(1).IsNil() ? "" : a.Str(1);
        std::int64_t i = a.OptInt(2, 1), j = a.at(3).IsNil() ? L.LenOf(a.at(0), "").i : a.Int(3);
        std::string out;
        for (std::int64_t k = i; k <= j; ++k) {
            Value v = L.Index(a.at(0), Value::Int(k));
            if (v.t != T::Str && !v.IsNumber()) L.Error(std::string("invalid value (at index ") + std::to_string(k) + ") in table for 'concat'");
            out += L.RawToString(v);
            if (k < j) out += sep;
            L.Reserve(out.size());
        }
        r.push_back(L.NewStr(out));
    });
    auto unpack = [](State& L, std::vector<Value>& args, std::vector<Value>& r) {
        A a{L, args, "unpack"};
        std::int64_t i = a.OptInt(1, 1), j = a.at(2).IsNil() ? L.LenOf(a.at(0), "").i : a.Int(2);
        if (i > j) return;
        if ((std::uint64_t)j - (std::uint64_t)i >= 1000000u) L.Error("too many results to unpack");
        for (std::int64_t k = i; k <= j; ++k) r.push_back(L.Index(a.at(0), Value::Int(k)));
    };
    SetFn(L, T_, "unpack", unpack);
    DEF(L, T_, "pack", {
        Table* t = L.NewTable();
        for (std::size_t i = 0; i < a.n(); ++i) t->Set(L, Value::Int((std::int64_t)i + 1), a.a[i]);
        t->Set(L, L.NewStr("n"), Value::Int((std::int64_t)a.n()));
        r.push_back(Value::Of(t));
    });
    DEF(L, T_, "move", {
        a.Tab(0);
        std::int64_t f = a.Int(1), e = a.Int(2), t = a.Int(3);
        Value dst = a.at(4).IsNil() ? a.at(0) : a.at(4);
        if (e >= f) {
            if (t > e || t <= f || dst.o != a.at(0).o) for (std::int64_t i = 0; i <= e - f; ++i) L.SetIndex(dst, Value::Int(t + i), L.Index(a.at(0), Value::Int(f + i)));
            else for (std::int64_t i = e - f; i >= 0; --i) L.SetIndex(dst, Value::Int(t + i), L.Index(a.at(0), Value::Int(f + i)));
        }
        r.push_back(dst);
    });
    DEF(L, T_, "sort", {
        a.Tab(0);
        std::int64_t n = L.LenOf(a.at(0), "").i;
        Value cmp = a.at(1);
        if (!cmp.IsNil() && cmp.t != T::Func && cmp.t != T::Native) a.TypeErr(1, "function");
        if (n < 2) return;
        if (n > 50'000'000) a.Bad(0, "array too big");
        std::vector<Value> v((std::size_t)n), tmp((std::size_t)n);
        for (std::int64_t i = 0; i < n; ++i) v[(std::size_t)i] = L.Index(a.at(0), Value::Int(i + 1));
        L.pins.push_back(&v); L.pins.push_back(&tmp);
        struct P { State& L; ~P() { L.pins.pop_back(); L.pins.pop_back(); } } pg{L};
        auto less = [&](const Value& x, const Value& y) {
            if (cmp.IsNil()) return L.Less(x, y, false);
            auto rr = L.Call(cmp, {x, y}, 1);
            return !rr.empty() && rr[0].Truthy();
        };
        // merge sort (stable; safe even if the comparison function is inconsistent)
        for (std::size_t w = 1; w < v.size(); w *= 2) {
            for (std::size_t lo = 0; lo < v.size(); lo += 2 * w) {
                std::size_t mid = std::min(lo + w, v.size()), hi = std::min(lo + 2 * w, v.size());
                std::size_t i = lo, j = mid, k = lo;
                while (i < mid && j < hi) { if (less(v[j], v[i])) tmp[k++] = v[j++]; else tmp[k++] = v[i++]; }
                while (i < mid) tmp[k++] = v[i++];
                while (j < hi) tmp[k++] = v[j++];
            }
            v.swap(tmp);
        }
        for (std::int64_t i = 0; i < n; ++i) L.SetIndex(a.at(0), Value::Int(i + 1), v[(std::size_t)i]);
    });
}

// ============================================================ math
void OpenMath(State& L) {
    Table* M_ = L.NewTable();
    L.globals->Set(L, L.NewStr("math"), Value::Of(M_));
    M_->Set(L, L.NewStr("pi"), Value::Num(3.14159265358979323846));
    M_->Set(L, L.NewStr("huge"), Value::Num(HUGE_VAL));
    M_->Set(L, L.NewStr("maxinteger"), Value::Int(INT64_MAX));
    M_->Set(L, L.NewStr("mininteger"), Value::Int(INT64_MIN));
    DEF(L, M_, "abs", { Value v = a.NumV(0); r.push_back(v.t == T::Int ? Value::Int(v.i < 0 ? (std::int64_t)(0 - (std::uint64_t)v.i) : v.i) : Value::Num(std::fabs(v.n))); });
    auto toIntOrFloat = [](double d) { std::int64_t i; if (d >= -9223372036854775808.0 && d < 9223372036854775808.0) { i = (std::int64_t)d; return Value::Int(i); } return Value::Num(d); };
    M_->Set(L, L.NewStr("floor"), L.Fn("floor", [toIntOrFloat](State& L, std::vector<Value>& args, std::vector<Value>& r) {
        A a{L, args, "floor"}; Value v = a.NumV(0); r.push_back(v.t == T::Int ? v : toIntOrFloat(std::floor(v.n))); }));
    M_->Set(L, L.NewStr("ceil"), L.Fn("ceil", [toIntOrFloat](State& L, std::vector<Value>& args, std::vector<Value>& r) {
        A a{L, args, "ceil"}; Value v = a.NumV(0); r.push_back(v.t == T::Int ? v : toIntOrFloat(std::ceil(v.n))); }));
    DEF(L, M_, "sqrt", { r.push_back(Value::Num(std::sqrt(a.Num(0)))); });
    DEF(L, M_, "sin", { r.push_back(Value::Num(std::sin(a.Num(0)))); });
    DEF(L, M_, "cos", { r.push_back(Value::Num(std::cos(a.Num(0)))); });
    DEF(L, M_, "tan", { r.push_back(Value::Num(std::tan(a.Num(0)))); });
    DEF(L, M_, "asin", { r.push_back(Value::Num(std::asin(a.Num(0)))); });
    DEF(L, M_, "acos", { r.push_back(Value::Num(std::acos(a.Num(0)))); });
    DEF(L, M_, "atan", { r.push_back(Value::Num(std::atan2(a.Num(0), a.at(1).IsNil() ? 1.0 : a.Num(1)))); });
    DEF(L, M_, "exp", { r.push_back(Value::Num(std::exp(a.Num(0)))); });
    DEF(L, M_, "log", {
        double x = a.Num(0);
        if (a.at(1).IsNil()) r.push_back(Value::Num(std::log(x)));
        else { double b = a.Num(1); r.push_back(Value::Num(b == 2.0 ? std::log2(x) : b == 10.0 ? std::log10(x) : std::log(x) / std::log(b))); }
    });
    DEF(L, M_, "fmod", {
        Value x = a.NumV(0), y = a.NumV(1);
        if (x.t == T::Int && y.t == T::Int) {
            if (y.i == 0) a.Bad(1, "zero");
            r.push_back(Value::Int(y.i == -1 ? 0 : x.i % y.i));
        } else r.push_back(Value::Num(std::fmod(a.Num(0), a.Num(1))));
    });
    DEF(L, M_, "modf", {
        double x = a.Num(0), ip = x >= 0 ? std::floor(x) : std::ceil(x);
        std::int64_t i;
        (void)i; r.push_back(Value::Num(ip));
        r.push_back(Value::Num(std::isinf(x) ? 0.0 : x - ip));
    });
    DEF(L, M_, "tointeger", {
        Value v = a.at(0); std::int64_t i;
        if (v.t == T::Int) r.push_back(v);
        else if (v.t == T::Num && ToInteger(v, i)) r.push_back(Value::Int(i));
        else if (v.t == T::Str && ToInteger(v, i)) r.push_back(Value::Int(i));
        else r.push_back(Value());
    });
    DEF(L, M_, "type", {
        a.Any(0); Value v = a.at(0);
        if (v.t == T::Int) r.push_back(L.NewStr("integer")); else if (v.t == T::Num) r.push_back(L.NewStr("float")); else r.push_back(Value());
    });
    DEF(L, M_, "ult", { r.push_back(Value::Bool((std::uint64_t)a.Int(0) < (std::uint64_t)a.Int(1))); });
    DEF(L, M_, "max", {
        Value best = a.NumV(0);
        for (std::size_t i = 1; i < a.n(); ++i) { Value v = a.NumV(i); if (L.Less(best, v, false)) best = v; }
        r.push_back(best);
    });
    DEF(L, M_, "min", {
        Value best = a.NumV(0);
        for (std::size_t i = 1; i < a.n(); ++i) { Value v = a.NumV(i); if (L.Less(v, best, false)) best = v; }
        r.push_back(best);
    });
    auto rng = std::make_shared<Rng>();
    rng->Seed((std::uint64_t)std::chrono::steady_clock::now().time_since_epoch().count());
    M_->Set(L, L.NewStr("randomseed"), L.Fn("randomseed", [rng](State& L, std::vector<Value>& args, std::vector<Value>&) {
        A a{L, args, "randomseed"};
        rng->Seed(a.n() ? (std::uint64_t)a.Num(0) * 2654435761ull + (std::uint64_t)(a.Num(0) * 1000.0) : 0x1234567ull);
    }));
    M_->Set(L, L.NewStr("random"), L.Fn("random", [rng](State& L, std::vector<Value>& args, std::vector<Value>& r) {
        A a{L, args, "random"};
        std::uint64_t x = rng->Next();
        if (a.n() == 0) { r.push_back(Value::Num((double)(x >> 11) * (1.0 / 9007199254740992.0))); return; }
        std::int64_t lo = 1, hi;
        if (a.n() == 1) hi = a.Int(0); else { lo = a.Int(0); hi = a.Int(1); }
        if (a.n() > 2) L.Error("wrong number of arguments to 'random'");
        if (lo > hi) a.Bad(a.n() == 1 ? 0 : 1, "interval is empty");
        std::uint64_t span = (std::uint64_t)hi - (std::uint64_t)lo;
        r.push_back(Value::Int((std::int64_t)((std::uint64_t)lo + (span == UINT64_MAX ? x : x % (span + 1)))));
    }));
}

// ============================================================ os / io (sandboxed)
void OpenOs(State& L) {
    Table* O = L.NewTable();
    L.globals->Set(L, L.NewStr("os"), Value::Of(O));
    DEF(L, O, "time", {
        if (a.at(0).t == T::Table) {
            Table* t = a.Tab(0);
            auto field = [&](const char* k, int def) -> int {
                Value v = t->GetStr(k); std::int64_t i;
                if (v.IsNil()) { if (def < 0) L.Error(std::string("field '") + k + "' missing in date table"); return def; }
                if (!ToInteger(v, i)) L.Error(std::string("field '") + k + "' is not an integer");
                return (int)i;
            };
            std::tm tmv{};
            tmv.tm_year = field("year", -1) - 1900; tmv.tm_mon = field("month", -1) - 1; tmv.tm_mday = field("day", -1);
            tmv.tm_hour = field("hour", 12); tmv.tm_min = field("min", 0); tmv.tm_sec = field("sec", 0);
            tmv.tm_isdst = -1;
            r.push_back(Value::Int((std::int64_t)std::mktime(&tmv)));
        } else r.push_back(Value::Int((std::int64_t)std::time(nullptr)));
    });
    DEF(L, O, "clock", { r.push_back(Value::Num((double)std::clock() / (double)CLOCKS_PER_SEC)); });
    DEF(L, O, "difftime", { r.push_back(Value::Num(a.Num(0) - (a.at(1).IsNil() ? 0 : a.Num(1)))); });
    DEF(L, O, "getenv", { r.push_back(Value()); });
    DEF(L, O, "date", {
        std::string f = a.at(0).IsNil() ? "%c" : a.Str(0);
        std::time_t t = a.at(1).IsNil() ? std::time(nullptr) : (std::time_t)a.Int(1);
        bool utc = !f.empty() && f[0] == '!';
        if (utc) f.erase(0, 1);
        std::tm tmv = utc ? *std::gmtime(&t) : *std::localtime(&t);
        if (f.rfind("*t", 0) == 0) {
            Table* res = L.NewTable();
            auto put = [&](const char* k, int v) { res->Set(L, L.NewStr(k), Value::Int(v)); };
            put("year", tmv.tm_year + 1900); put("month", tmv.tm_mon + 1); put("day", tmv.tm_mday);
            put("hour", tmv.tm_hour); put("min", tmv.tm_min); put("sec", tmv.tm_sec);
            put("wday", tmv.tm_wday + 1); put("yday", tmv.tm_yday + 1);
            res->Set(L, L.NewStr("isdst"), Value::Bool(tmv.tm_isdst > 0));
            r.push_back(Value::Of(res));
        } else {
            char buf[512];
            std::size_t n = std::strftime(buf, sizeof buf, f.c_str(), &tmv);
            r.push_back(L.NewStr(std::string(buf, n)));
        }
    });
    // io: output only, through the same sink as print (no files in the sandbox)
    Table* IO = L.NewTable();
    L.globals->Set(L, L.NewStr("io"), Value::Of(IO));
    auto writer = [](State& L, std::vector<Value>& args, std::vector<Value>&) {
        std::string s;
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (args[i].t == T::Table && i == 0) continue;   // io.stdout:write(...)
            if (args[i].t != T::Str && !args[i].IsNumber()) L.Error(std::string("bad argument #") + std::to_string(i + 1) + " to 'write' (string expected, got " + L.TypeName(args[i]) + ")");
            s += L.RawToString(args[i]);
        }
        if (L.out) L.out(s); else std::fputs(s.c_str(), stdout);
    };
    SetFn(L, IO, "write", writer);
    Table* so = L.NewTable();
    SetFn(L, so, "write", writer);
    IO->Set(L, L.NewStr("stdout"), Value::Of(so));
    DEF(L, IO, "read", { r.push_back(Value()); });
}

// ============================================================ coroutine
void OpenCoroutine(State& L) {
    Table* C = L.NewTable();
    L.globals->Set(L, L.NewStr("coroutine"), Value::Of(C));
    DEF(L, C, "create", {
        if (a.at(0).t != T::Func && a.at(0).t != T::Native) a.TypeErr(0, "function");
        r.push_back(Value::Of(L.NewCoro(a.at(0))));
    });
    DEF(L, C, "resume", {
        if (a.at(0).t != T::Coro) a.TypeErr(0, "coroutine");
        std::vector<Value> rest(a.a.begin() + 1, a.a.end()), res;
        std::string err;
        bool ok = L.Resume(static_cast<Coro*>(a.a[0].o), rest, &res, &err);
        r.push_back(Value::Bool(ok));
        if (ok) for (auto& v : res) r.push_back(v);
        else r.push_back(res.empty() ? L.NewStr(err) : res[0]);
    });
    {
        Native* y = L.NewNative("yield", {}); y->special = Special::Yield;
        C->Set(L, L.NewStr("yield"), Value::Of(y));
    }
    DEF(L, C, "status", {
        if (a.at(0).t != T::Coro) a.TypeErr(0, "coroutine");
        Coro* c = static_cast<Coro*>(a.a[0].o);
        const char* s = "suspended";
        switch (c->status) {
            case CoStatus::Running: s = "running"; break; case CoStatus::Normal: s = "normal"; break;
            case CoStatus::Dead: s = "dead"; break; default: break;
        }
        r.push_back(L.NewStr(s));
    });
    DEF(L, C, "isyieldable", { r.push_back(Value::Bool(!L.cur->isMain && L.cur->nativeNest == 0)); });
    DEF(L, C, "running", { r.push_back(Value::Of(L.cur)); r.push_back(Value::Bool(L.cur->isMain)); });
    DEF(L, C, "close", {
        if (a.at(0).t != T::Coro) a.TypeErr(0, "coroutine");
        Coro* c = static_cast<Coro*>(a.a[0].o);
        if (c->status == CoStatus::Running || c->status == CoStatus::Normal) L.Error("cannot close a " + std::string(c->status == CoStatus::Running ? "running" : "normal") + " coroutine");
        c->status = CoStatus::Dead; c->st.clear(); c->frames.clear();
        r.push_back(Value::Bool(true));
    });
    DEF(L, C, "wrap", {
        if (a.at(0).t != T::Func && a.at(0).t != T::Native) a.TypeErr(0, "function");
        Coro* co = L.NewCoro(a.at(0));
        Native* w = L.NewNative("wrap", [](State& L, std::vector<Value>& args, std::vector<Value>& rets) {
            // the coroutine is kept alive through the native's up[0]; find ourselves via extra root slot
            (void)args; (void)rets; (void)L;
        });
        w->up.push_back(Value::Of(co));
        w->fn = [w](State& L, std::vector<Value>& args, std::vector<Value>& rets) {
            Coro* c = static_cast<Coro*>(w->up[0].o);
            std::vector<Value> res; std::string err;
            if (!L.Resume(c, args, &res, &err)) {
                if (!res.empty()) L.ErrorValue(res[0]);
                L.Error(err);
            }
            rets = res;
        };
        r.push_back(Value::Of(w));
    });
}

void State::OpenLibs() {
    OpenBase(*this); OpenString(*this); OpenTable(*this); OpenMath(*this); OpenOs(*this); OpenCoroutine(*this);
}

}  // namespace okay::lua
