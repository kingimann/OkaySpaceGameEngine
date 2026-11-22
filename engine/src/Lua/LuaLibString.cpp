// okay::lua string library and pattern matching.
//
// Written from the Lua 5.4 Reference Manual, section 6.4 (string
// manipulation) and 6.4.1 (patterns: character classes, sets, items *, +, -,
// ?, %n back references, %b balanced matches, %f frontiers, anchors and
// captures). The matcher is a plain backtracking matcher of our own design
// with a depth limit and every step counted against the run budget, so a
// hostile pattern can not hang the game. Character classes are ASCII only
// (independent of the C locale). string.pack / unpack / packsize follow
// manual 6.4.2 (format strings): our own format reader and byte writer, with
// the sizes of this build's types (C int 4, long / lua_Integer / size_t 8,
// float 4, double / lua_Number 8, native order little-endian, native maximum
// alignment 8). string.dump is not provided (no binary chunks).
#include "LuaCode.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdio>
#include <cstring>

namespace okay::lua {

namespace {

constexpr int kMaxCaptures = 32;
constexpr int kMaxMatchDepth = 200;
constexpr std::size_t kMaxStringResult = std::size_t(1) << 30;

// ---------------------------------------------------------------- ASCII classes
bool IsAlpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool IsDigit(int c) { return c >= '0' && c <= '9'; }
bool IsLower(int c) { return c >= 'a' && c <= 'z'; }
bool IsUpper(int c) { return c >= 'A' && c <= 'Z'; }
bool IsSpace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
bool IsCntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
bool IsPunct(int c) { return c > 32 && c < 127 && !IsAlpha(c) && !IsDigit(c); }
bool IsXDigit(int c) { return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool IsGraph(int c) { return c > 32 && c < 127; }
int ToLower(int c) { return IsUpper(c) ? c + 32 : c; }
int ToUpper(int c) { return IsLower(c) ? c - 32 : c; }

// ---------------------------------------------------------------- the matcher
struct Matcher {
    State* L;
    const char* srcBegin;
    const char* srcEnd;
    const char* patEnd;
    int depth = 0;
    int level = 0;                         ///< captures opened so far
    struct Cap { const char* init; std::ptrdiff_t len; } cap[kMaxCaptures];   // len: -1 open, -2 position
    std::uint64_t steps = 0;

    [[noreturn]] void Fail(const char* msg) { L->Error(msg); }
    void Step() {
        if (++steps >= 4096) { L->Charge(steps / 2); steps = 0; }
    }

    const char* ClassEnd(const char* p) {
        char c = *p++;
        if (c == '%') {
            if (p >= patEnd) Fail("malformed pattern (ends with '%')");
            return p + 1;
        }
        if (c == '[') {
            if (p < patEnd && *p == '^') ++p;
            do {   // the first character of a set may be ']'
                if (p >= patEnd) Fail("malformed pattern (missing ']')");
                char x = *p++;
                if (x == '%') {
                    if (p >= patEnd) Fail("malformed pattern (missing ']')");
                    ++p;
                }
            } while (p >= patEnd || *p != ']');
            return p + 1;
        }
        return p;
    }

    static bool ClassMatch(int c, int cl) {
        bool r;
        switch (ToLower(cl)) {
            case 'a': r = IsAlpha(c); break;
            case 'c': r = IsCntrl(c); break;
            case 'd': r = IsDigit(c); break;
            case 'g': r = IsGraph(c); break;
            case 'l': r = IsLower(c); break;
            case 'p': r = IsPunct(c); break;
            case 's': r = IsSpace(c); break;
            case 'u': r = IsUpper(c); break;
            case 'w': r = IsAlpha(c) || IsDigit(c); break;
            case 'x': r = IsXDigit(c); break;
            default: return cl == c;
        }
        return IsUpper(cl) ? !r : r;
    }

    // p at '[', ec at the closing ']'
    static bool SetMatch(int c, const char* p, const char* ec) {
        bool neg = false;
        ++p;
        if (*p == '^') { neg = true; ++p; }
        while (p < ec) {
            if (*p == '%' && p + 1 < ec) {
                ++p;
                if (ClassMatch(c, (unsigned char)*p)) return !neg;
                ++p;
            } else if (p + 2 < ec && p[1] == '-') {
                if ((unsigned char)p[0] <= c && c <= (unsigned char)p[2]) return !neg;
                p += 3;
            } else {
                if ((unsigned char)*p == c) return !neg;
                ++p;
            }
        }
        return neg;
    }

    bool Single(const char* s, const char* p, const char* ep) const {
        if (s >= srcEnd) return false;
        int c = (unsigned char)*s;
        switch (*p) {
            case '.': return true;
            case '%': return ClassMatch(c, (unsigned char)p[1]);
            case '[': return SetMatch(c, p, ep - 1);
            default: return (unsigned char)*p == c;
        }
    }

    const char* Balance(const char* s, const char* p) {
        if (p + 1 >= patEnd) Fail("malformed pattern (missing arguments to '%b')");
        if (s >= srcEnd || *s != *p) return nullptr;
        char open = p[0], close = p[1];
        int count = 1;
        while (++s < srcEnd) {
            Step();
            if (*s == close) { if (--count == 0) return s + 1; }
            else if (*s == open) ++count;
        }
        return nullptr;
    }

    const char* MaxExpand(const char* s, const char* p, const char* ep) {
        std::ptrdiff_t i = 0;
        while (Single(s + i, p, ep)) { ++i; Step(); }
        while (i >= 0) {   // try the longest first, then shorter
            const char* r = Match(s + i, ep + 1);
            if (r) return r;
            --i;
        }
        return nullptr;
    }

    const char* MinExpand(const char* s, const char* p, const char* ep) {
        for (;;) {
            Step();
            const char* r = Match(s, ep + 1);
            if (r) return r;
            if (Single(s, p, ep)) ++s;
            else return nullptr;
        }
    }

    const char* StartCapture(const char* s, const char* p, std::ptrdiff_t what) {
        if (level >= kMaxCaptures) Fail("too many captures");
        cap[level].init = s;
        cap[level].len = what;
        ++level;
        const char* r = Match(s, p);
        if (!r) --level;
        return r;
    }

    const char* EndCapture(const char* s, const char* p) {
        int l = -1;
        for (int i = level - 1; i >= 0; --i)
            if (cap[i].len == -1) { l = i; break; }
        if (l < 0) Fail("invalid pattern capture");
        cap[l].len = s - cap[l].init;
        const char* r = Match(s, p);
        if (!r) cap[l].len = -1;
        return r;
    }

    const char* MatchBackref(const char* s, int l) {
        l -= '1';
        if (l < 0 || l >= level || cap[l].len == -1) L->Error("invalid capture index %" + std::to_string(l + 1));
        std::size_t len = (std::size_t)cap[l].len;
        if ((std::size_t)(srcEnd - s) >= len && std::memcmp(cap[l].init, s, len) == 0) return s + len;
        return nullptr;
    }

    const char* Match(const char* s, const char* p) {
        if (++depth > kMaxMatchDepth) Fail("pattern too complex");
        struct Leave { int& d; ~Leave() { --d; } } leave{depth};
        for (;;) {
            Step();
            if (p >= patEnd) return s;
            switch (*p) {
                case '(':
                    if (p + 1 < patEnd && p[1] == ')') return StartCapture(s, p + 2, -2);
                    return StartCapture(s, p + 1, -1);
                case ')': return EndCapture(s, p + 1);
                case '$':
                    if (p + 1 == patEnd) return s == srcEnd ? s : nullptr;
                    break;   // an ordinary '$'
                case '%':
                    if (p + 1 < patEnd && p[1] == 'b') {
                        s = Balance(s, p + 2);
                        if (!s) return nullptr;
                        p += 4;
                        continue;
                    }
                    if (p + 1 < patEnd && p[1] == 'f') {
                        p += 2;
                        if (p >= patEnd || *p != '[') Fail("missing '[' after '%f' in pattern");
                        const char* ep = ClassEnd(p);
                        int prev = s == srcBegin ? 0 : (unsigned char)s[-1];
                        int cur = s < srcEnd ? (unsigned char)*s : 0;
                        if (!SetMatch(prev, p, ep - 1) && SetMatch(cur, p, ep - 1)) { p = ep; continue; }
                        return nullptr;
                    }
                    if (p + 1 < patEnd && IsDigit((unsigned char)p[1])) {
                        s = MatchBackref(s, (unsigned char)p[1]);
                        if (!s) return nullptr;
                        p += 2;
                        continue;
                    }
                    break;
                default: break;
            }
            const char* ep = ClassEnd(p);
            char suffix = ep < patEnd ? *ep : '\0';
            if (!Single(s, p, ep)) {
                if (suffix == '*' || suffix == '?' || suffix == '-') { p = ep + 1; continue; }   // zero occurrences
                return nullptr;
            }
            switch (suffix) {
                case '?': {
                    const char* r = Match(s + 1, ep + 1);
                    if (r) return r;
                    p = ep + 1;
                    continue;
                }
                case '+': return MaxExpand(s + 1, p, ep);
                case '*': return MaxExpand(s, p, ep);
                case '-': return MinExpand(s, p, ep);
                default: ++s; p = ep; continue;
            }
        }
    }

    // capture i as a value (whole match when there are no captures)
    Value Capture(int i, const char* s, const char* e) {
        if (i >= level) {
            if (i != 0) L->Error("invalid capture index %" + std::to_string(i + 1));
            return Value::Obj(L->NewString(s, (std::size_t)(e - s)));
        }
        if (cap[i].len == -1) Fail("unfinished capture");
        if (cap[i].len == -2) return Value::Int((std::int64_t)(cap[i].init - srcBegin) + 1);
        return Value::Obj(L->NewString(cap[i].init, (std::size_t)cap[i].len));
    }
    int PushCaptures(const char* s, const char* e, bool wholeIfNone) {
        int n = (level == 0 && wholeIfNone) ? 1 : level;
        L->CheckStack(n);
        for (int i = 0; i < n; ++i) L->Push(Capture(i, s, e));
        return n;
    }
};

std::int64_t PosRelStart(std::int64_t pos, std::size_t len) {
    if (pos > 0) return pos;
    if (pos == 0) return 1;
    if (pos < -(std::int64_t)len) return 1;
    return (std::int64_t)len + pos + 1;
}
std::int64_t PosRelEnd(std::int64_t pos, std::size_t len) {
    if (pos > (std::int64_t)len) return (std::int64_t)len;
    if (pos >= 0) return pos;
    if (pos < -(std::int64_t)len) return 0;
    return (std::int64_t)len + pos + 1;
}

bool HasSpecials(const std::string& p) {
    return p.find_first_of("^$*+?.([%-") != std::string::npos;
}

// ---------------------------------------------------------------- functions
int S_len(State& L, int n) {
    (void)n;
    L.Push(Value::Int((std::int64_t)L.CheckStr(0, "len")->s.size()));
    return 1;
}

int S_sub(State& L, int n) {
    LString* s = L.CheckStr(0, "sub");
    std::size_t len = s->s.size();
    std::int64_t i = PosRelStart(L.OptInt(1, "sub", 1), len);
    std::int64_t j = PosRelEnd(n >= 3 ? L.OptInt(2, "sub", -1) : -1, len);
    if (i > j) L.PushString("");
    else L.Push(Value::Obj(L.NewString(s->s.data() + i - 1, (std::size_t)(j - i + 1))));
    return 1;
}

int S_upper(State& L, int n) {
    (void)n;
    std::string s = L.CheckStr(0, "upper")->s;
    for (char& c : s) c = (char)ToUpper((unsigned char)c);
    L.PushString(s);
    return 1;
}

int S_lower(State& L, int n) {
    (void)n;
    std::string s = L.CheckStr(0, "lower")->s;
    for (char& c : s) c = (char)ToLower((unsigned char)c);
    L.PushString(s);
    return 1;
}

int S_reverse(State& L, int n) {
    (void)n;
    std::string s = L.CheckStr(0, "reverse")->s;
    std::reverse(s.begin(), s.end());
    L.PushString(s);
    return 1;
}

int S_rep(State& L, int n) {
    LString* s = L.CheckStr(0, "rep");
    std::int64_t count = L.CheckInt(1, "rep");
    std::string sep = n >= 3 && !L.Arg(2).IsNil() ? L.CheckStr(2, "rep")->s : std::string();
    if (count <= 0) { L.PushString(""); return 1; }
    std::size_t unit = s->s.size() + sep.size();
    if (unit != 0 && (std::uint64_t)count > kMaxStringResult / unit) L.Error("resulting string too large");
    std::size_t total = unit * (std::size_t)count;
    L.Charge(total / 64);
    if (total > L.memoryLimit) L.Error("not enough memory");
    std::string out;
    out.reserve(total);
    for (std::int64_t k = 0; k < count; ++k) {
        out += s->s;
        if (k + 1 < count) out += sep;
    }
    L.PushString(out);
    return 1;
}

int S_byte(State& L, int n) {
    LString* s = L.CheckStr(0, "byte");
    std::size_t len = s->s.size();
    std::int64_t i = PosRelStart(L.OptInt(1, "byte", 1), len);
    std::int64_t j = PosRelEnd(n >= 3 ? L.OptInt(2, "byte", i) : i, len);
    if (i > j) return 0;
    if (j - i >= 1000000) L.Error("string slice too long");
    int cnt = (int)(j - i + 1);
    L.CheckStack(cnt);
    for (int k = 0; k < cnt; ++k) L.Push(Value::Int((unsigned char)s->s[(std::size_t)(i - 1 + k)]));
    return cnt;
}

int S_char(State& L, int n) {
    std::string out;
    for (int i = 0; i < n; ++i) {
        std::int64_t c = L.CheckInt(i, "char");
        if (c < 0 || c > 255) L.ArgError(i, "char", "value out of range");
        out += (char)c;
    }
    L.PushString(out);
    return 1;
}

int FindAux(State& L, int n, bool find) {
    const char* fname = find ? "find" : "match";
    LString* s = L.CheckStr(0, fname);
    LString* p = L.CheckStr(1, fname);
    std::size_t len = s->s.size();
    std::int64_t init = PosRelStart(L.OptInt(2, fname, 1), len);
    if (init > (std::int64_t)len + 1) { L.PushNil(); return 1; }
    bool plain = find && n >= 4 && L.Arg(3).Truthy();
    if (find && (plain || !HasSpecials(p->s))) {
        std::size_t pos = s->s.find(p->s, (std::size_t)(init - 1));
        L.Charge(len / 64);
        if (pos == std::string::npos) { L.PushNil(); return 1; }
        L.Push(Value::Int((std::int64_t)pos + 1));
        L.Push(Value::Int((std::int64_t)(pos + p->s.size())));
        return 2;
    }
    Matcher m;
    m.L = &L;
    m.srcBegin = s->s.data();
    m.srcEnd = m.srcBegin + len;
    const char* pat = p->s.data();
    m.patEnd = pat + p->s.size();
    bool anchor = !p->s.empty() && *pat == '^';
    if (anchor) ++pat;
    const char* src = m.srcBegin + init - 1;
    do {
        m.level = 0;
        m.depth = 0;
        const char* e = m.Match(src, pat);
        if (e) {
            if (find) {
                L.Push(Value::Int((std::int64_t)(src - m.srcBegin) + 1));
                L.Push(Value::Int((std::int64_t)(e - m.srcBegin)));
                return m.PushCaptures(nullptr, nullptr, false) + 2;
            }
            return m.PushCaptures(src, e, true);
        }
    } while (src++ < m.srcEnd && !anchor);
    L.PushNil();
    return 1;
}
int S_find(State& L, int n) { return FindAux(L, n, true); }
int S_match(State& L, int n) { return FindAux(L, n, false); }

// gmatch iterator: up = {string, pattern, position (0-based), last match end (-1 none)}
int GmatchAux(State& L, int n) {
    (void)n;
    NClosure* self = L.Current()->frames.back().ncl;
    LString* s = self->up[0].Str();
    LString* p = self->up[1].Str();
    Matcher m;
    m.L = &L;
    m.srcBegin = s->s.data();
    m.srcEnd = m.srcBegin + s->s.size();
    m.patEnd = p->s.data() + p->s.size();
    std::int64_t lastEnd = self->up[3].i;
    for (const char* src = m.srcBegin + self->up[2].i; src <= m.srcEnd; ++src) {
        m.level = 0;
        m.depth = 0;
        const char* e = m.Match(src, p->s.data());
        if (e && (e - m.srcBegin) != lastEnd) {
            self->up[2] = Value::Int(e - m.srcBegin);
            self->up[3] = Value::Int(e - m.srcBegin);
            return m.PushCaptures(src, e, true);
        }
    }
    self->up[2] = Value::Int((std::int64_t)s->s.size() + 1);
    return 0;
}

int S_gmatch(State& L, int n) {
    LString* s = L.CheckStr(0, "gmatch");
    LString* p = L.CheckStr(1, "gmatch");
    std::size_t len = s->s.size();
    std::int64_t init = PosRelStart(n >= 3 ? L.OptInt(2, "gmatch", 1) : 1, len);
    if (init > (std::int64_t)len + 1) init = (std::int64_t)len + 1;
    NClosure* c = L.NewNative(GmatchAux, "gmatch_iterator",
                              {Value::Obj(s), Value::Obj(p), Value::Int(init - 1), Value::Int(-1)});
    L.Push(Value::Obj(c));
    return 1;
}

void AddReplacement(State& L, Matcher& m, std::string& out, const char* s, const char* e, int replIdx) {
    Value repl = L.Arg(replIdx);
    Value v;
    if (repl.IsString() || repl.IsNumber()) {
        std::string r = repl.IsString() ? repl.Str()->s : State::NumberToString(repl);
        for (std::size_t i = 0; i < r.size(); ++i) {
            char c = r[i];
            if (c != '%') { out += c; continue; }
            ++i;
            if (i >= r.size()) L.Error("invalid use of '%' in replacement string");
            char d = r[i];
            if (d == '%') { out += '%'; continue; }
            if (!IsDigit((unsigned char)d)) L.Error("invalid use of '%' in replacement string");
            Value cv = d == '0' ? Value::Obj(L.NewString(s, (std::size_t)(e - s))) : m.Capture(d - '1', s, e);
            if (cv.IsString()) out += cv.Str()->s; else out += State::NumberToString(cv);
            if (out.size() > kMaxStringResult) L.Error("resulting string too large");
        }
        return;
    }
    Value key = m.Capture(0, s, e);
    if (repl.IsTable()) {
        L.Push(key);   // keep it reachable
        v = L.Index(repl, key);
        L.Pop();
    } else {
        int func = L.Current()->top;
        L.Push(repl);
        int nc = m.PushCaptures(s, e, true);
        (void)nc;
        L.CallAt(func, 1);
        v = L.Pop();
    }
    if (!v.Truthy()) { out.append(s, (std::size_t)(e - s)); return; }
    if (v.IsString()) out += v.Str()->s;
    else if (v.IsNumber()) out += State::NumberToString(v);
    else L.Error(std::string("invalid replacement value (a ") + L.TypeName(v) + ")");
    if (out.size() > kMaxStringResult) L.Error("resulting string too large");
}

int S_gsub(State& L, int n) {
    LString* s = L.CheckStr(0, "gsub");
    LString* p = L.CheckStr(1, "gsub");
    Value repl = L.Arg(2);
    if (!(repl.IsString() || repl.IsNumber() || repl.IsTable() || repl.IsFunction()))
        L.TypeError(2, "gsub", "string/function/table");
    std::int64_t maxN = n >= 4 && !L.Arg(3).IsNil() ? L.CheckInt(3, "gsub") : (std::int64_t)s->s.size() + 1;
    Matcher m;
    m.L = &L;
    m.srcBegin = s->s.data();
    m.srcEnd = m.srcBegin + s->s.size();
    const char* pat = p->s.data();
    m.patEnd = pat + p->s.size();
    bool anchor = !p->s.empty() && *pat == '^';
    if (anchor) ++pat;
    std::string out;
    const char* src = m.srcBegin;
    std::int64_t count = 0;
    while (count < maxN) {
        m.level = 0;
        m.depth = 0;
        const char* e = m.Match(src, pat);
        if (e) {
            ++count;
            AddReplacement(L, m, out, src, e, 2);
            // a function replacement may have run a collection; our strings are arguments, so still alive
            m.srcBegin = s->s.data();
        }
        if (e && e > src) src = e;
        else if (src < m.srcEnd) out += *src++;
        else break;
        if (anchor) break;
    }
    if (src < m.srcEnd) out.append(src, (std::size_t)(m.srcEnd - src));
    L.PushString(out);
    L.Push(Value::Int(count));
    return 2;
}

// ---------------------------------------------------------------- format
void AddQuoted(State& L, std::string& out, const Value& v) {
    if (v.IsString()) {
        const std::string& s = v.Str()->s;
        out += '"';
        for (std::size_t i = 0; i < s.size(); ++i) {
            unsigned char c = (unsigned char)s[i];
            if (c == '"' || c == '\\' || c == '\n') { out += '\\'; out += (char)c; }
            else if (c == '\r') out += "\\r";
            else if (c == 0 || IsCntrl(c)) {
                char buf[8];
                bool nextDigit = i + 1 < s.size() && IsDigit((unsigned char)s[i + 1]);
                std::snprintf(buf, sizeof buf, nextDigit ? "\\%03d" : "\\%d", c);
                out += buf;
            } else out += (char)c;
        }
        out += '"';
        return;
    }
    if (v.t == Type::Int) {
        char buf[32];
        if (v.i == INT64_MIN) std::snprintf(buf, sizeof buf, "0x%llx", (unsigned long long)v.i);
        else std::snprintf(buf, sizeof buf, "%lld", (long long)v.i);
        out += buf;
        return;
    }
    if (v.t == Type::Float) {
        if (std::isinf(v.n)) { out += v.n > 0 ? "1e9999" : "-1e9999"; return; }
        if (std::isnan(v.n)) { out += "(0/0)"; return; }
        char buf[64];
        if (v.n == std::floor(v.n) && std::fabs(v.n) < 1e15) std::snprintf(buf, sizeof buf, "%.1f", v.n);
        else std::snprintf(buf, sizeof buf, "%a", v.n);
        out += buf;
        return;
    }
    if (v.IsNil() || v.t == Type::Bool) { out += L.ToStringBasic(v); return; }
    L.Error("value has no literal form");
}

int S_format(State& L, int n) {
    const std::string fmt = L.CheckStr(0, "format")->s;
    std::string out;
    int arg = 0;
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        char c = fmt[i];
        if (c != '%') { out += c; continue; }
        if (++i >= fmt.size()) L.Error("invalid conversion '%' to 'format'");
        if (fmt[i] == '%') { out += '%'; continue; }
        // flags, width, precision (at most two digits each)
        std::string spec = "%";
        std::size_t start = i;
        while (i < fmt.size() && std::strchr("-+ #0", fmt[i]) && fmt[i]) spec += fmt[i++];
        int digits = 0;
        while (i < fmt.size() && IsDigit((unsigned char)fmt[i])) { spec += fmt[i++]; ++digits; }
        bool badWidth = digits > 2;
        bool hasPrec = false;
        if (i < fmt.size() && fmt[i] == '.') {
            hasPrec = true;
            spec += fmt[i++];
            digits = 0;
            while (i < fmt.size() && IsDigit((unsigned char)fmt[i])) { spec += fmt[i++]; ++digits; }
            if (digits > 2) badWidth = true;
        }
        if (i >= fmt.size() || badWidth || spec.size() - 1 > 6)
            L.Error("invalid conversion '%" + fmt.substr(start, std::min<std::size_t>(i - start + 1, 10)) + "' to 'format'");
        char conv = fmt[i];
        ++arg;
        if (arg >= n && conv != '%') L.ArgError(arg, "format", "no value");
        char buf[512];
        switch (conv) {
            case 'c': {
                std::int64_t v = L.CheckInt(arg, "format");
                out += (char)(unsigned char)v;
                break;
            }
            case 'd': case 'i': case 'u': {
                std::int64_t v;
                Value a = L.Arg(arg), num;
                if (!State::ToNumber(a, num)) L.TypeError(arg, "format", "number");
                if (!State::ToInteger(num, v)) L.ArgError(arg, "format", "number has no integer representation");
                std::string sp = spec + "ll" + (conv == 'u' ? 'u' : 'd');
                std::snprintf(buf, sizeof buf, sp.c_str(), (long long)v);
                out += buf;
                break;
            }
            case 'x': case 'X': case 'o': {
                std::int64_t v;
                Value a = L.Arg(arg), num;
                if (!State::ToNumber(a, num)) L.TypeError(arg, "format", "number");
                if (!State::ToInteger(num, v)) L.ArgError(arg, "format", "number has no integer representation");
                std::string sp = spec + "ll" + conv;
                std::snprintf(buf, sizeof buf, sp.c_str(), (unsigned long long)v);
                out += buf;
                break;
            }
            case 'e': case 'E': case 'f': case 'g': case 'G': case 'a': case 'A': {
                double v = L.CheckNum(arg, "format");
                std::string sp = spec + conv;
                if (conv == 'f' && std::fabs(v) >= 1e100 && !std::isinf(v)) {
                    // %f of a huge number: still bounded (99 digits of precision at most, 310 before the point)
                    std::vector<char> big(512);
                    std::snprintf(big.data(), big.size(), sp.c_str(), v);
                    out += big.data();
                } else {
                    std::snprintf(buf, sizeof buf, sp.c_str(), v);
                    out += buf;
                }
                break;
            }
            case 's': {
                std::string s = L.ToStringMeta(L.Arg(arg));
                if (spec.size() == 1) { out += s; break; }
                if (!hasPrec && s.size() >= 100) { out += s; break; }   // too long to pad: as is
                std::string sp = spec + 's';
                std::snprintf(buf, sizeof buf, sp.c_str(), s.c_str());
                out += buf;
                break;
            }
            case 'q': {
                if (spec.size() != 1) L.Error("specifier '%q' cannot have modifiers");
                AddQuoted(L, out, L.Arg(arg));
                break;
            }
            case 'p': {
                Value a = L.Arg(arg);
                const void* ptr = a.IsGc() ? (const void*)a.o : nullptr;
                if (!ptr) { out += "(null)"; break; }
                std::snprintf(buf, sizeof buf, "%p", ptr);
                out += buf;
                break;
            }
            default:
                L.Error("invalid conversion '%" + fmt.substr(start, i - start + 1) + "' to 'format'");
        }
        if (out.size() > kMaxStringResult) L.Error("resulting string too large");
    }
    L.PushString(out);
    return 1;
}

// ---------------------------------------------------------------- pack / unpack (manual 6.4.2)
enum class PackKind { Int, Uint, Float, Double, Char, String, Zstr, Pad, PadAlign, None };

struct PackItem {
    PackKind kind = PackKind::None;
    int size = 0;     ///< bytes the item takes (for String: the length prefix)
    int align = 0;    ///< padding bytes before it
};

struct PackReader {
    State& L;
    const char* fname;
    const std::string& fmt;
    std::size_t pos = 0;
    bool little = true;
    int maxAlign = 1;

    bool Digit(char c) const { return c >= '0' && c <= '9'; }
    // an optional size after an option letter; `def` when there is none
    int Size(int def) {
        if (pos >= fmt.size() || !Digit(fmt[pos])) return def;
        std::int64_t n = 0;
        while (pos < fmt.size() && Digit(fmt[pos])) {
            n = n * 10 + (fmt[pos++] - '0');
            if (n > 0x7fffffff / 10) L.ArgError(0, fname, "invalid format (size too large)");
        }
        return (int)n;
    }
    int IntSize(int def) {
        int n = Size(def);
        if (n < 1 || n > 16) L.Error(std::string("integral size (") + std::to_string(n) + ") out of limits [1,16]");
        return n;
    }
    // the next option without alignment; returns false at the end of the format
    bool Option(PackItem& it) {
        for (;;) {
            if (pos >= fmt.size()) return false;
            char c = fmt[pos++];
            it = PackItem();
            switch (c) {
                case ' ': continue;
                case '<': little = true; continue;
                case '>': little = false; continue;
                case '=': little = true; continue;   // this build's native order
                case '!': {
                    int n = Size(8);
                    if (n < 1 || n > 16) L.Error(std::string("integral size (") + std::to_string(n) + ") out of limits [1,16]");
                    maxAlign = n;
                    continue;
                }
                case 'b': it.kind = PackKind::Int; it.size = 1; return true;
                case 'B': it.kind = PackKind::Uint; it.size = 1; return true;
                case 'h': it.kind = PackKind::Int; it.size = 2; return true;
                case 'H': it.kind = PackKind::Uint; it.size = 2; return true;
                case 'l': case 'j': it.kind = PackKind::Int; it.size = 8; return true;
                case 'L': case 'J': case 'T': it.kind = PackKind::Uint; it.size = 8; return true;
                case 'i': it.kind = PackKind::Int; it.size = IntSize(4); return true;
                case 'I': it.kind = PackKind::Uint; it.size = IntSize(4); return true;
                case 'f': it.kind = PackKind::Float; it.size = 4; return true;
                case 'd': case 'n': it.kind = PackKind::Double; it.size = 8; return true;
                case 's': it.kind = PackKind::String; it.size = IntSize(8); return true;
                case 'z': it.kind = PackKind::Zstr; return true;
                case 'x': it.kind = PackKind::Pad; it.size = 1; return true;
                case 'c': {
                    if (pos >= fmt.size() || !Digit(fmt[pos])) L.Error("missing size for format option 'c'");
                    it.kind = PackKind::Char;
                    it.size = Size(0);
                    return true;
                }
                case 'X': it.kind = PackKind::PadAlign; return true;
                default: L.Error(std::string("invalid format option '") + c + "'");
            }
        }
    }
    // the next item with its alignment padding worked out (`total` = bytes so far)
    bool Next(PackItem& it, std::size_t total) {
        if (!Option(it)) return false;
        int align = it.size;
        if (it.kind == PackKind::PadAlign) {
            // Xop: align to op's size; op itself is not packed
            PackItem op;
            std::size_t save = pos;
            if (!Option(op) || op.kind == PackKind::Char || op.kind == PackKind::Zstr || op.kind == PackKind::PadAlign || op.size == 0)
                L.ArgError(0, fname, "invalid next option for option 'X'");
            (void)save;
            align = op.size;
            it.size = 0;
        }
        const bool alignable = it.kind == PackKind::Int || it.kind == PackKind::Uint || it.kind == PackKind::Float ||
                               it.kind == PackKind::Double || it.kind == PackKind::PadAlign;
        if (alignable && align > 1) {
            if (align > maxAlign) align = maxAlign;
            if ((align & (align - 1)) != 0) L.ArgError(0, fname, "format asks for alignment not power of 2");
            it.align = (int)((std::size_t)(align - (int)(total & (std::size_t)(align - 1))) & (std::size_t)(align - 1));
        }
        return true;
    }
};

void PackInt(std::string& out, std::uint64_t v, int size, bool little, bool negative) {
    unsigned char b[16];
    for (int i = 0; i < size; ++i) b[i] = i < 8 ? (unsigned char)(v >> (8 * i)) : (negative ? 0xff : 0);
    for (int i = 0; i < size; ++i) out += (char)b[little ? i : size - 1 - i];
}

std::uint64_t ReadUInt(State& L, const char* p, int size, bool little, bool isSigned) {
    std::uint64_t v = 0;
    int limit = size < 8 ? size : 8;
    for (int i = 0; i < limit; ++i) {
        unsigned char c = (unsigned char)p[little ? i : size - 1 - i];
        v |= (std::uint64_t)c << (8 * i);
    }
    if (size < 8) {
        if (isSigned) {
            std::uint64_t signBit = std::uint64_t(1) << (size * 8 - 1);
            if (v & signBit) v |= ~((signBit << 1) - 1);
        }
    } else if (size > 8) {
        // the extra bytes must be a plain sign (or zero) extension
        unsigned char fill = (isSigned && (std::int64_t)v < 0) ? 0xff : 0;
        for (int i = 8; i < size; ++i)
            if ((unsigned char)p[little ? i : size - 1 - i] != fill)
                L.Error(std::to_string(size) + "-byte integer does not fit into Lua Integer");
    }
    return v;
}

int S_pack(State& L, int n) {
    const std::string fmt = L.CheckStr(0, "pack")->s;
    PackReader rd{L, "pack", fmt};
    std::string out;
    int arg = 1;
    PackItem it;
    while (rd.Next(it, out.size())) {
        out.append((std::size_t)it.align, '\0');
        switch (it.kind) {
            case PackKind::Int: case PackKind::Uint: {
                std::int64_t v = L.CheckInt(arg, "pack");
                if (it.size < 8) {
                    if (it.kind == PackKind::Int) {
                        std::int64_t lim = std::int64_t(1) << (it.size * 8 - 1);
                        if (v < -lim || v >= lim) L.ArgError(arg, "pack", "integer overflow");
                    } else if ((std::uint64_t)v >= (std::uint64_t(1) << (it.size * 8))) {
                        L.ArgError(arg, "pack", "unsigned overflow");
                    }
                }
                PackInt(out, (std::uint64_t)v, it.size, rd.little, v < 0);
                ++arg;
                break;
            }
            case PackKind::Float: {
                float f = (float)L.CheckNum(arg++, "pack");
                std::uint32_t bits;
                std::memcpy(&bits, &f, 4);
                PackInt(out, bits, 4, rd.little, false);
                break;
            }
            case PackKind::Double: {
                double d = L.CheckNum(arg++, "pack");
                std::uint64_t bits;
                std::memcpy(&bits, &d, 8);
                PackInt(out, bits, 8, rd.little, false);
                break;
            }
            case PackKind::Char: {
                const std::string& str = L.CheckStr(arg, "pack")->s;
                if (str.size() > (std::size_t)it.size) L.ArgError(arg, "pack", "string longer than given size");
                out += str;
                out.append((std::size_t)it.size - str.size(), '\0');
                ++arg;
                break;
            }
            case PackKind::String: {
                const std::string& str = L.CheckStr(arg, "pack")->s;
                if (it.size < 8 && (std::uint64_t)str.size() >= (std::uint64_t(1) << (it.size * 8)))
                    L.ArgError(arg, "pack", "string length does not fit in given size");
                PackInt(out, (std::uint64_t)str.size(), it.size, rd.little, false);
                out += str;
                ++arg;
                break;
            }
            case PackKind::Zstr: {
                const std::string& str = L.CheckStr(arg, "pack")->s;
                if (str.find('\0') != std::string::npos) L.ArgError(arg, "pack", "string contains zeros");
                out += str;
                out += '\0';
                ++arg;
                break;
            }
            case PackKind::Pad: out += '\0'; break;
            case PackKind::PadAlign: case PackKind::None: break;
        }
        if (out.size() > kMaxStringResult) L.Error("resulting string too large");
    }
    (void)n;
    L.Charge(out.size() / 64);
    L.PushString(out);
    return 1;
}

int S_packsize(State& L, int n) {
    (void)n;
    const std::string fmt = L.CheckStr(0, "packsize")->s;
    PackReader rd{L, "packsize", fmt};
    std::size_t total = 0;
    PackItem it;
    while (rd.Next(it, total)) {
        if (it.kind == PackKind::String || it.kind == PackKind::Zstr) L.ArgError(0, "packsize", "variable-length format");
        total += (std::size_t)it.align + (std::size_t)it.size;
        if (total > 0x7fffffff) L.ArgError(0, "packsize", "format result too large");
    }
    L.Push(Value::Int((std::int64_t)total));
    return 1;
}

int S_unpack(State& L, int n) {
    const std::string fmt = L.CheckStr(0, "unpack")->s;
    LString* data = L.CheckStr(1, "unpack");
    const std::string& d = data->s;
    std::int64_t init = n >= 3 ? L.OptInt(2, "unpack", 1) : 1;
    if (init < 0) init = (std::int64_t)d.size() + init + 1;
    if (init < 1 || init - 1 > (std::int64_t)d.size()) L.ArgError(2, "unpack", "initial position out of string");
    std::size_t pos = (std::size_t)(init - 1);
    PackReader rd{L, "unpack", fmt};
    PackItem it;
    int results = 0;
    auto need = [&](std::size_t bytes) {
        if (bytes > d.size() || pos > d.size() - bytes) L.ArgError(1, "unpack", "data string too short");
    };
    while (rd.Next(it, pos)) {
        need((std::size_t)it.align + (std::size_t)it.size);
        pos += (std::size_t)it.align;
        L.CheckStack(2);
        switch (it.kind) {
            case PackKind::Int: case PackKind::Uint: {
                std::uint64_t v = ReadUInt(L, d.data() + pos, it.size, rd.little, it.kind == PackKind::Int);
                L.Push(Value::Int((std::int64_t)v));
                ++results;
                break;
            }
            case PackKind::Float: {
                std::uint32_t bits = (std::uint32_t)ReadUInt(L, d.data() + pos, 4, rd.little, false);
                float f;
                std::memcpy(&f, &bits, 4);
                L.Push(Value::Num((double)f));
                ++results;
                break;
            }
            case PackKind::Double: {
                std::uint64_t bits = ReadUInt(L, d.data() + pos, 8, rd.little, false);
                double v;
                std::memcpy(&v, &bits, 8);
                L.Push(Value::Num(v));
                ++results;
                break;
            }
            case PackKind::Char:
                L.Push(Value::Obj(L.NewString(d.data() + pos, (std::size_t)it.size)));
                ++results;
                break;
            case PackKind::String: {
                std::uint64_t len = ReadUInt(L, d.data() + pos, it.size, rd.little, false);
                pos += (std::size_t)it.size;
                if (len > d.size() - pos) L.ArgError(1, "unpack", "data string too short");
                L.Push(Value::Obj(L.NewString(d.data() + pos, (std::size_t)len)));
                pos += (std::size_t)len;
                it.size = 0;
                ++results;
                break;
            }
            case PackKind::Zstr: {
                std::size_t end = d.find('\0', pos);
                if (end == std::string::npos) L.ArgError(1, "unpack", "unfinished string for format 'z'");
                L.Push(Value::Obj(L.NewString(d.data() + pos, end - pos)));
                pos = end + 1;
                ++results;
                break;
            }
            case PackKind::Pad: case PackKind::PadAlign: case PackKind::None: break;
        }
        pos += (std::size_t)it.size;
        if (results > 200) L.CheckStack(results);
    }
    L.Push(Value::Int((std::int64_t)pos + 1));
    return results + 1;
}

} // namespace

void OpenString(State& L) {
    Table* t = L.NewTable();
    L.SetGlobal("string", Value::Obj(t));
    const struct { const char* n; NativeFn f; } fns[] = {
        {"len", S_len}, {"sub", S_sub}, {"upper", S_upper}, {"lower", S_lower}, {"reverse", S_reverse},
        {"rep", S_rep}, {"byte", S_byte}, {"char", S_char}, {"find", S_find}, {"match", S_match},
        {"gmatch", S_gmatch}, {"gsub", S_gsub}, {"format", S_format}, {"pack", S_pack},
        {"packsize", S_packsize}, {"unpack", S_unpack},
    };
    for (const auto& f : fns) L.Register(t, f.n, f.f);
    Table* mt = L.NewTable();
    mt->Set(L, Value::Obj(L.tmName[TM_INDEX]), Value::Obj(t));
    L.stringMeta = mt;
}

} // namespace okay::lua
