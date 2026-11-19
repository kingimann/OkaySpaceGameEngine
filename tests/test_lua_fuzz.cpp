// Fuzz tests for our own Lua: random token soup, mutated valid programs and random bytes go through
// the parser and the VM with small budgets. Nothing may crash, hang or leak past the limits. Deterministic.
#include "test_framework.hpp"
#include "okay/Lua/Lua.hpp"
#include <cstdint>
#include <string>
#include <vector>

using okay::lua::Interpreter;
using okay::lua::ScriptLimits;

namespace {
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 2862933555777941757ull + 3037000493ull) {}
    std::uint32_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (std::uint32_t)(s >> 16); }
    std::uint32_t Below(std::uint32_t n) { return n ? Next() % n : 0; }
};

const char* const kTokens[] = {
    "local", "function", "end", "if", "then", "else", "elseif", "while", "do", "for", "in", "repeat", "until", "return", "break",
    "and", "or", "not", "nil", "true", "false", "x", "y", "t", "f", "g", "i", "1", "2", "0", "3.5", "0x10", "1e308", "'s'", "\"q\"", "[[l]]",
    "+", "-", "*", "/", "//", "%", "^", "#", "&", "|", "~", "<<", ">>", "..", "...", "==", "~=", "<", "<=", ">", ">=", "=", "(", ")", "{", "}", "[", "]",
    ";", ":", ",", ".", "::", "goto", "<const>", "<close>", "math.huge", "string.rep", "table.insert", "pcall", "error", "coroutine.wrap",
    "coroutine.yield", "setmetatable", "{}", "{1,2,3}", "print", "select", "tostring", "tonumber", "next", "pairs", "ipairs", "string.format",
    "string.find", "string.gsub", "table.sort", "table.concat", "table.unpack", "load", "collectgarbage", "type", "rawset", "getmetatable",
};

const char* const kCorpus[] = {
    "local t = {} for i = 1, 10 do t[#t + 1] = i * i end return #t",
    "local function f(n) if n < 2 then return n end return f(n - 1) + f(n - 2) end return f(10)",
    "local co = coroutine.wrap(function(a) local b = coroutine.yield(a) return b end) return co(1), co(2)",
    "return string.gsub('hello world', '(%w+)', '<%1>'), string.format('%5.2f %d %s', 3.14159, 42, 'x')",
    "local mt = {__index = function(t, k) return k end, __add = function(a, b) return 1 end} return setmetatable({}, mt).x",
    "local ok, e = pcall(function() local x = nil; return x.y end) return ok, e",
    "for k, v in pairs({a = 1, b = 2, 3, 4}) do local _ = k .. tostring(v) end",
    "local s = 0 for i = 10, 1, -2 do s = s + i end while s > 0 do s = s - 7 end repeat s = s + 1 until s > 3 return s",
    "return table.concat({1, 2, 3}, ','), select('#', table.unpack({1, 2, 3})), math.floor(3.7), 7 // 2, 7 % 3, 2 ^ 10",
    "local t = {5, 3, 1, 4} table.sort(t, function(a, b) return a > b end) return t[1]",
    "return ('x'):rep(3) .. ('abc'):upper():lower():sub(2, 3) .. tostring(1 << 4 | 3 & 1 ~ 2)",
};

std::string Mutate(const std::string& src, Rng& r) {
    std::string s = src;
    int edits = 1 + (int)r.Below(4);
    for (int e = 0; e < edits && !s.empty(); ++e) {
        std::size_t p = r.Below((std::uint32_t)s.size());
        switch (r.Below(5)) {
            case 0: s.erase(p, 1 + r.Below(3)); break;
            case 1: s.insert(p, kTokens[r.Below(sizeof kTokens / sizeof *kTokens)]); break;
            case 2: s[p] = (char)r.Below(256); break;
            case 3: s.insert(p, s.substr(r.Below((std::uint32_t)s.size()), 1 + r.Below(8))); break;
            default: s.insert(p, " "); break;
        }
    }
    return s;
}

ScriptLimits Tight() { ScriptLimits l; l.instructions = 20000; l.seconds = 0; l.memoryBytes = 8u << 20; return l; }

// Check never crashes; Run (only if it parses) never crashes and always returns.
void Try(Interpreter& I, const std::string& src, int& parsed, int& ran) {
    std::string err;
    if (!I.Check(src, "fz", &err)) { CHECK(!err.empty()); return; }
    ++parsed;
    std::string e2;
    bool ok = I.Run(src, "fz", &e2);
    CHECK(ok || !e2.empty());
    ++ran;
}
}  // namespace

int main() {
    RUN_SUITE("lua_fuzz");
    int parsed = 0, ran = 0;

    // 1. token soup
    {
        Rng r(1);
        Interpreter I; I.SetLimits(Tight()); I.SetOutput([](const std::string&) {});
        for (int n = 0; n < 6000; ++n) {
            std::string src;
            int len = 1 + (int)r.Below(24);
            for (int i = 0; i < len; ++i) { src += kTokens[r.Below(sizeof kTokens / sizeof *kTokens)]; src += ' '; }
            Try(I, src, parsed, ran);
            if (n % 500 == 499) { I.~Interpreter(); new (&I) Interpreter(); I.SetLimits(Tight()); I.SetOutput([](const std::string&) {}); }
        }
    }
    // 2. mutated valid programs
    {
        Rng r(2);
        Interpreter I; I.SetLimits(Tight()); I.SetOutput([](const std::string&) {});
        for (int n = 0; n < 8000; ++n) {
            std::string src = kCorpus[r.Below(sizeof kCorpus / sizeof *kCorpus)];
            src = Mutate(src, r);
            Try(I, src, parsed, ran);
            if (n % 500 == 499) { I.~Interpreter(); new (&I) Interpreter(); I.SetLimits(Tight()); I.SetOutput([](const std::string&) {}); }
        }
    }
    // 3. random bytes (the lexer must cope with anything)
    {
        Rng r(3);
        Interpreter I; I.SetLimits(Tight());
        for (int n = 0; n < 3000; ++n) {
            std::string src;
            int len = (int)r.Below(80);
            for (int i = 0; i < len; ++i) src += (char)r.Below(256);
            std::string err;
            bool ok = I.Check(src, "fz", &err);
            CHECK(ok || !err.empty());
        }
    }
    // 4. the valid corpus really runs
    {
        Interpreter I; I.SetLimits(Tight()); I.SetOutput([](const std::string&) {});
        for (const char* c : kCorpus) { std::string e; CHECK(I.Run(c, "ok", &e)); if (!e.empty()) std::cerr << "  " << c << "\n  -> " << e << "\n"; }
    }
    // 5. pathological inputs: deep nesting, long chains, huge literals
    {
        Interpreter I; I.SetLimits(Tight());
        std::string e;
        std::string nest; for (int i = 0; i < 5000; ++i) nest += "{"; for (int i = 0; i < 5000; ++i) nest += "}";
        CHECK(!I.Check("x = " + nest, "fz", &e));
        std::string chain = "x = 1"; for (int i = 0; i < 20000; ++i) chain += " + 1";
        CHECK(I.Run(chain, "fz", &e) || !e.empty());
        std::string cc = "x = 'a'"; for (int i = 0; i < 150; ++i) cc += " .. 'b'";
        CHECK(I.Run(cc, "fz", &e));
        std::string cc2 = "x = 'a'"; for (int i = 0; i < 5000; ++i) cc2 += " .. 'b'";
        CHECK(!I.Run(cc2, "fz", &e));   // too deep for the parser: an error, not a crash
        CHECK(I.Run("local t = {} for i = 1, 100000 do t[i] = i end x = #t", "fz", &e) || !e.empty());
        std::string ifs; for (int i = 0; i < 2000; ++i) ifs += "if true then "; for (int i = 0; i < 2000; ++i) ifs += "end ";
        CHECK(!I.Check(ifs, "fz", &e));
        CHECK(!I.Run("local t = setmetatable({}, {}) getmetatable(t).__index = t return t.x", "fz", &e));
        CHECK(!I.Run("local a = setmetatable({}, {}) getmetatable(a).__newindex = a a.x = 1", "fz", &e));
        CHECK(!I.Run("local a = setmetatable({}, {__call = nil}) getmetatable(a).__call = a a()", "fz", &e));
    }
    std::cout << parsed << " programs parsed, " << ran << " ran\n";
    CHECK(parsed > 300);
    TEST_MAIN_RESULT();
}
