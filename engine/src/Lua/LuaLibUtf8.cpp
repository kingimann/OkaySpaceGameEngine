// okay::lua utf8 library.
//
// Written from the Lua 5.4 Reference Manual, section 6.5 (UTF-8 support):
// char, charpattern, codes, codepoint, len and offset, strict by default
// (no surrogates, nothing above U+10FFFF, no overlong forms) and lax on
// request (up to 2^31 - 1, the original six-byte UTF-8 form).
#include "LuaCode.hpp"
#include <cstring>

namespace okay::lua {

void AppendUtf8(std::string& out, std::uint32_t cp) {
    // The manual allows code points up to 2^31 using the original UTF-8 scheme (up to 6 bytes).
    if (cp < 0x80) { out += (char)cp; return; }
    int n;
    if (cp < 0x800) n = 2;
    else if (cp < 0x10000) n = 3;
    else if (cp < 0x200000) n = 4;
    else if (cp < 0x4000000) n = 5;
    else n = 6;
    static const unsigned char first[] = {0, 0, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC};
    char buf[6];
    for (int i = n - 1; i > 0; --i) { buf[i] = (char)(0x80 | (cp & 0x3F)); cp >>= 6; }
    buf[0] = (char)(first[n] | cp);
    out.append(buf, (std::size_t)n);
}

namespace {

// Decode one character at s[0..n): its length, or 0 when the bytes are not valid UTF-8.
int Decode(const unsigned char* s, std::size_t n, std::uint32_t& cp, bool strict) {
    if (n == 0) return 0;
    unsigned c = s[0];
    if (c < 0x80) { cp = c; return 1; }
    int len;
    std::uint32_t v;
    if (c >= 0xC0 && c < 0xE0) { len = 2; v = c & 0x1F; }
    else if (c >= 0xE0 && c < 0xF0) { len = 3; v = c & 0x0F; }
    else if (c >= 0xF0 && c < 0xF8) { len = 4; v = c & 0x07; }
    else if (c >= 0xF8 && c < 0xFC) { len = 5; v = c & 0x03; }
    else if (c >= 0xFC && c < 0xFE) { len = 6; v = c & 0x01; }
    else return 0;
    if ((std::size_t)len > n) return 0;
    for (int i = 1; i < len; ++i) {
        if ((s[i] & 0xC0) != 0x80) return 0;
        v = (v << 6) | (s[i] & 0x3F);
    }
    static const std::uint32_t kMin[] = {0, 0, 0x80, 0x800, 0x10000, 0x200000, 0x4000000};
    if (v < kMin[len]) return 0;   // overlong
    if (strict && (v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF))) return 0;
    cp = v;
    return len;
}

std::int64_t Rel(std::int64_t pos, std::size_t len) {
    if (pos >= 0) return pos;
    // count back from the end; 0 - pos in unsigned arithmetic so math.mininteger is safe
    if ((std::uint64_t)0 - (std::uint64_t)pos > (std::uint64_t)len) return 0;
    return (std::int64_t)len + pos + 1;
}

bool IsCont(unsigned char c) { return (c & 0xC0) == 0x80; }

int U_char(State& L, int n) {
    std::string out;
    for (int i = 0; i < n; ++i) {
        std::int64_t c = L.CheckInt(i, "char");
        if (c < 0 || c > 0x7FFFFFFF) L.ArgError(i, "char", "code point must be from 0 to 0x7FFFFFFF");
        AppendUtf8(out, (std::uint32_t)c);
    }
    L.PushString(out);
    return 1;
}

int U_codepoint(State& L, int n) {
    LString* s = L.CheckStr(0, "codepoint");
    std::size_t len = s->s.size();
    std::int64_t i = Rel(L.OptInt(1, "codepoint", 1), len);
    std::int64_t j = Rel(n >= 3 && !L.Arg(2).IsNil() ? L.CheckInt(2, "codepoint") : i, len);
    bool lax = n >= 4 && L.Arg(3).Truthy();
    if (i < 1) L.ArgError(1, "codepoint", "start is before the string");
    if (j > (std::int64_t)len) L.ArgError(2, "codepoint", "end is past the string");
    if (i > j) return 0;
    if (j - i >= 1000000) L.Error("utf8.codepoint: asked for more than a million code points at once");
    L.Charge((std::uint64_t)(j - i) + 1);   // at most one value pushed per byte
    const unsigned char* p = (const unsigned char*)s->s.data();
    int count = 0;
    std::size_t pos = (std::size_t)(i - 1);
    while ((std::int64_t)pos < j) {
        std::uint32_t cp;
        int l = Decode(p + pos, len - pos, cp, !lax);
        if (!l) L.Error("the string is not valid UTF-8");
        L.CheckStack(1);
        L.Push(Value::Int(cp));
        ++count;
        pos += (std::size_t)l;
    }
    return count;
}

int U_len(State& L, int n) {
    LString* s = L.CheckStr(0, "len");
    std::size_t len = s->s.size();
    std::int64_t i = Rel(L.OptInt(1, "len", 1), len);
    std::int64_t j = Rel(n >= 3 && !L.Arg(2).IsNil() ? L.CheckInt(2, "len") : -1, len);
    bool lax = n >= 4 && L.Arg(3).Truthy();
    if (i < 1 || i > (std::int64_t)len + 1) L.ArgError(1, "len", "start is outside the string");
    if (j > (std::int64_t)len) L.ArgError(2, "len", "end is past the string");
    if (j >= i) L.Charge((std::uint64_t)(j - i) / 16 + 1);   // the bytes decoded
    const unsigned char* p = (const unsigned char*)s->s.data();
    std::int64_t count = 0;
    std::size_t pos = (std::size_t)(i - 1);
    while ((std::int64_t)pos < j) {
        std::uint32_t cp;
        int l = Decode(p + pos, len - pos, cp, !lax);
        if (!l) {
            L.PushNil();
            L.Push(Value::Int((std::int64_t)pos + 1));
            return 2;
        }
        pos += (std::size_t)l;
        ++count;
    }
    L.Push(Value::Int(count));
    return 1;
}

int U_offset(State& L, int n) {
    LString* s = L.CheckStr(0, "offset");
    std::size_t len = s->s.size();
    std::int64_t k = L.CheckInt(1, "offset");
    std::int64_t i = n >= 3 && !L.Arg(2).IsNil() ? Rel(L.CheckInt(2, "offset"), len) : (k >= 0 ? 1 : (std::int64_t)len + 1);
    if (i < 1 || i > (std::int64_t)len + 1) L.ArgError(2, "offset", "position is outside the string");
    const unsigned char* p = (const unsigned char*)s->s.data();
    std::int64_t pos = i - 1;   // 0-based
    auto cont = [&](std::int64_t at) { return at < (std::int64_t)len && IsCont(p[at]); };
    if (k == 0) {
        while (pos > 0 && cont(pos)) --pos;   // back to the start of the character
        L.Charge((std::uint64_t)((i - 1) - pos) / 16 + 1);
        L.Push(Value::Int(pos + 1));
        return 1;
    }
    if (cont(pos)) L.Error("utf8.offset: the start position is in the middle of a character");
    if (k < 0) {
        while (k < 0 && pos > 0) {
            do { --pos; } while (pos > 0 && cont(pos));
            ++k;
        }
    } else {
        --k;
        while (k > 0 && pos < (std::int64_t)len) {
            do { ++pos; } while (cont(pos));
            --k;
        }
    }
    // the bytes walked over
    L.Charge((std::uint64_t)(pos > i - 1 ? pos - (i - 1) : (i - 1) - pos) / 16 + 1);
    if (k == 0) L.Push(Value::Int(pos + 1));
    else L.PushNil();
    return 1;
}

// iterator for utf8.codes: (s, position) -> next position, code point
int CodesAux(State& L, bool lax) {
    LString* s = L.CheckStr(0, "codes");
    std::int64_t i = L.CheckInt(1, "codes");
    std::size_t len = s->s.size();
    const unsigned char* p = (const unsigned char*)s->s.data();
    std::size_t pos = i > 0 ? (std::size_t)i - 1 : 0;
    if (i > 0) {   // skip the current character
        ++pos;
        while (pos < len && IsCont(p[pos])) ++pos;
    }
    if (pos >= len) return 0;
    std::uint32_t cp;
    int l = Decode(p + pos, len - pos, cp, !lax);
    if (!l || (pos + (std::size_t)l < len && IsCont(p[pos + (std::size_t)l]))) L.Error("the string is not valid UTF-8");
    L.Push(Value::Int((std::int64_t)pos + 1));
    L.Push(Value::Int(cp));
    return 2;
}
int CodesStrict(State& L, int) { return CodesAux(L, false); }
int CodesLax(State& L, int) { return CodesAux(L, true); }

int U_codes(State& L, int n) {
    LString* s = L.CheckStr(0, "codes");
    bool lax = n >= 2 && L.Arg(1).Truthy();
    if (!s->s.empty() && IsCont((unsigned char)s->s[0])) L.Error("the string is not valid UTF-8");
    L.Push(Value::Obj(L.NewNative(lax ? CodesLax : CodesStrict, "codes_iterator")));
    L.Push(L.Arg(0));
    L.Push(Value::Int(0));
    return 3;
}

} // namespace

void OpenUtf8(State& L) {
    Table* t = L.NewTable();
    L.SetGlobal("utf8", Value::Obj(t));
    L.Register(t, "char", U_char);
    L.Register(t, "codepoint", U_codepoint);
    L.Register(t, "len", U_len);
    L.Register(t, "offset", U_offset);
    L.Register(t, "codes", U_codes);
    const char pat[] = "[\0-\x7F\xC2-\xFD][\x80-\xBF]*";
    t->Set(L, Value::Obj(L.NewString("charpattern")), Value::Obj(L.NewString(pat, sizeof(pat) - 1)));
}

} // namespace okay::lua
