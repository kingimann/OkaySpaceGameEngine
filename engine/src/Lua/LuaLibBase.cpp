// okay::lua libraries: base functions, coroutine, math and a safe os subset.
//
// Behaviour follows the Lua 5.4 Reference Manual, sections 6.1 (basic
// functions), 6.2 (coroutine manipulation), 6.7 (mathematical functions) and
// 6.9 (operating system facilities, only clock / time / date / difftime).
// Sandboxed on purpose: no dofile, loadfile, require, io, os.execute, os.exit,
// os.getenv, os.remove, package.loadlib or debug. math.random uses the
// xoshiro256** generator (Blackman and Vigna's published algorithm) seeded
// through splitmix64; both written here from the algorithm descriptions.
#include "LuaCode.hpp"
#include <cmath>
#include <cstring>
#include <ctime>
#include <limits>

namespace okay::lua {

namespace {

Value Str(State& L, const std::string& s) { return Value::Obj(L.NewString(s)); }

// ------------------------------------------------------------------ base
int B_print(State& L, int n) {
    std::string out;
    for (int i = 0; i < n; ++i) {
        if (i) out += '\t';
        out += L.ToStringMeta(L.Arg(i));
    }
    if (L.printSink) L.printSink(out);
    return 0;
}

int B_type(State& L, int n) {
    (void)n;
    L.CheckAny(0, "type");
    L.Push(Str(L, L.TypeName(L.Arg(0))));
    return 1;
}

int B_tostring(State& L, int n) {
    (void)n;
    L.CheckAny(0, "tostring");
    std::string s = L.ToStringMeta(L.Arg(0));
    L.Push(Str(L, s));
    return 1;
}

int B_tonumber(State& L, int n) {
    if (n < 2 || L.Arg(1).IsNil()) {
        L.CheckAny(0, "tonumber");
        Value v = L.Arg(0), r;
        if (State::ToNumber(v, r) && (v.IsNumber() || v.IsString())) { L.Push(r); return 1; }
        L.PushNil();
        return 1;
    }
    std::int64_t base = L.CheckInt(1, "tonumber");
    Value v = L.Arg(0);
    if (!v.IsString()) L.TypeError(0, "tonumber", "string");
    if (base < 2 || base > 36) L.ArgError(1, "tonumber", "base out of range");
    const std::string& s = v.Str()->s;
    std::size_t i = 0;
    while (i < s.size() && std::strchr(" \t\n\r\f\v", s[i]) && s[i]) ++i;
    bool neg = false;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) { neg = s[i] == '-'; ++i; }
    std::uint64_t acc = 0;
    bool any = false;
    for (; i < s.size(); ++i) {
        char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else break;
        if (d >= base) { any = false; i = s.size() + 1; break; }
        acc = acc * (std::uint64_t)base + (std::uint64_t)d;
        any = true;
    }
    while (i < s.size() && std::strchr(" \t\n\r\f\v", s[i]) && s[i]) ++i;
    if (!any || i != s.size()) { L.PushNil(); return 1; }
    L.Push(Value::Int(neg ? (std::int64_t)(0ull - acc) : (std::int64_t)acc));
    return 1;
}

int B_rawequal(State& L, int n) {
    (void)n;
    L.CheckAny(0, "rawequal");
    L.CheckAny(1, "rawequal");
    L.Push(Value::Bool(RawEquals(L.Arg(0), L.Arg(1))));
    return 1;
}

int B_rawlen(State& L, int n) {
    (void)n;
    Value v = L.Arg(0);
    if (v.IsTable()) L.Push(Value::Int(v.Tab()->Length()));
    else if (v.IsString()) L.Push(Value::Int((std::int64_t)v.Str()->s.size()));
    else L.ArgError(0, "rawlen", "table or string expected");
    return 1;
}

int B_rawget(State& L, int n) {
    (void)n;
    Table* t = L.CheckTable(0, "rawget");
    L.CheckAny(1, "rawget");
    L.Push(t->Get(L.Arg(1)));
    return 1;
}

int B_rawset(State& L, int n) {
    (void)n;
    Table* t = L.CheckTable(0, "rawset");
    L.CheckAny(1, "rawset");
    L.CheckAny(2, "rawset");
    t->Set(L, L.Arg(1), L.Arg(2));
    L.Push(L.Arg(0));
    return 1;
}

int B_setmetatable(State& L, int n) {
    (void)n;
    Table* t = L.CheckTable(0, "setmetatable");
    Value mt = L.Arg(1);
    if (!mt.IsNil() && !mt.IsTable()) L.TypeError(1, "setmetatable", "nil or table");
    if (t->meta && !t->meta->GetStr(L.tmName[TM_METATABLE]).IsNil()) L.Error("cannot change a protected metatable");
    t->meta = mt.IsNil() ? nullptr : mt.Tab();
    L.Push(L.Arg(0));
    return 1;
}

int B_getmetatable(State& L, int n) {
    (void)n;
    L.CheckAny(0, "getmetatable");
    Table* mt = L.GetMetatable(L.Arg(0));
    if (!mt) { L.PushNil(); return 1; }
    Value prot = mt->GetStr(L.tmName[TM_METATABLE]);
    L.Push(prot.IsNil() ? Value::Obj(mt) : prot);
    return 1;
}

int B_assert(State& L, int n) {
    L.CheckAny(0, "assert");
    if (L.Arg(0).Truthy()) return n;   // all arguments are already on the stack top
    if (n >= 2) L.ErrorValue(L.Arg(1));
    L.Error("assertion failed!");
}

int B_error(State& L, int n) {
    Value msg = L.Arg(0);
    std::int64_t level = n >= 2 ? L.CheckInt(1, "error") : 1;
    if (msg.IsString() && level > 0) msg = Str(L, L.Where((int)level) + msg.Str()->s);
    L.ErrorValue(msg);
}

// Move the native's results (the call results) up by one and put `first` before them.
int InsertFirst(State& L, int firstResultIdx, const Value& first) {
    Coroutine* co = L.Current();
    L.CheckStack(1);
    for (int i = co->top; i > firstResultIdx; --i) co->stack[(std::size_t)i] = co->stack[(std::size_t)(i - 1)];
    co->stack[(std::size_t)firstResultIdx] = first;
    co->top++;
    return co->top - firstResultIdx;
}


int B_xpcall(State& L, int n) {
    if (n < 2) L.ArgError(1, "xpcall", "value expected");
    Coroutine* co = L.Current();
    int base = co->top - n;   // f, msgh, args...
    Value handler = co->stack[(std::size_t)(base + 1)];
    // arrange: msgh f args -> call f with args, handler stays below
    co->stack[(std::size_t)(base + 1)] = co->stack[(std::size_t)base];
    co->stack[(std::size_t)base] = handler;
    int func = base + 1;
    std::size_t nframes = co->frames.size();
    try {
        L.CallAt(func, -1);
        return InsertFirst(L, func, Value::Bool(true)) ;
    } catch (const LuaError&) {
        // the handler runs where the error happened (the frames are still there)
        Value err = L.errorValue;
        Value result = err;
        try {
            L.CheckStack(3);
            int hf = co->top;
            co->stack[(std::size_t)co->top++] = co->stack[(std::size_t)base];
            co->stack[(std::size_t)co->top++] = err;
            L.CallAt(hf, 1);
            result = co->stack[(std::size_t)(co->top - 1)];
        } catch (const LuaError&) {
            result = L.errorValue;
        }
        L.errorValue = result;
        Value e = result;
        L.Unwind(nframes, func, e);
        L.errorValue = Value();
        co->stack[(std::size_t)co->top++] = Value::Bool(false);
        co->stack[(std::size_t)co->top++] = e;
        return 2;
    }
}

int B_select(State& L, int n) {
    Value a = L.Arg(0);
    if (a.IsString() && a.Str()->s == "#") { L.Push(Value::Int(n - 1)); return 1; }
    std::int64_t i = L.CheckInt(0, "select");
    if (i < 0) i = (n - 1) + i + 1;
    else if (i > n - 1) i = n;
    if (i < 1) L.ArgError(0, "select", "index out of range");
    return n - (int)i;   // the arguments from i on are the top values
}

int B_next(State& L, int n) {
    (void)n;
    Table* t = L.CheckTable(0, "next");
    Value k = L.Arg(1), v;
    if (t->Next(L, k, v)) { L.Push(k); L.Push(v); return 2; }
    L.PushNil();
    return 1;
}

int B_pairs(State& L, int n) {
    (void)n;
    L.CheckAny(0, "pairs");
    Value v = L.Arg(0);
    Value tm = L.GetTM(v, TM_PAIRS);
    if (!tm.IsNil()) {
        int func = L.Current()->top;
        L.Push(tm);
        L.Push(v);
        L.CallAt(func, 3);
        return 3;
    }
    if (!v.IsTable()) L.TypeError(0, "pairs", "table");
    L.Push(L.GetRef(-1).IsNil() ? L.Globals()->Get(Str(L, "next")) : Value());
    if (L.Current()->stack[(std::size_t)(L.Current()->top - 1)].IsNil()) {
        L.Current()->top--;
        L.Push(Value::Obj(L.NewNative(B_next, "next")));
    }
    L.Push(v);
    L.PushNil();
    return 3;
}

int IpairsAux(State& L, int n) {
    (void)n;
    std::int64_t i = L.CheckInt(1, "ipairs") + 1;
    Value t = L.Arg(0);
    Value v = t.IsTable() && !t.Tab()->meta ? t.Tab()->GetInt(i) : L.Index(t, Value::Int(i));
    if (v.IsNil()) { L.PushNil(); return 1; }
    L.Push(Value::Int(i));
    L.Push(v);
    return 2;
}

int B_ipairs(State& L, int n) {
    (void)n;
    L.CheckAny(0, "ipairs");
    Value aux = L.registry->Get(Str(L, "ipairs_aux"));
    if (aux.IsNil()) {
        aux = Value::Obj(L.NewNative(IpairsAux, "ipairs_aux"));
        L.registry->Set(L, Str(L, "ipairs_aux"), aux);
    }
    L.Push(aux);
    L.Push(L.Arg(0));
    L.Push(Value::Int(0));
    return 3;
}

int B_load(State& L, int n) {
    Value chunk = L.Arg(0);
    std::string src;
    if (chunk.IsString()) {
        src = chunk.Str()->s;
    } else if (chunk.IsFunction()) {
        for (int guard = 0;; ++guard) {
            int func = L.Current()->top;
            L.Push(chunk);
            L.CallAt(func, 1);
            Value piece = L.Pop();
            if (piece.IsNil() || (piece.IsString() && piece.Str()->s.empty())) break;
            if (!piece.IsString()) { L.PushNil(); L.PushString("reader function must return a string"); return 2; }
            src += piece.Str()->s;
            if (src.size() > (std::size_t(64) << 20)) L.Error("chunk too large");
        }
    } else {
        L.TypeError(0, "load", "string");
    }
    std::string name = n >= 2 && L.Arg(1).IsString() ? L.Arg(1).Str()->s
                                                      : (chunk.IsString() ? "[string \"" + src.substr(0, src.find('\n')).substr(0, 40) + "\"]" : "=(load)");
    if (!name.empty() && (name[0] == '=' || name[0] == '@')) name = name.substr(1);
    std::string mode = n >= 3 && L.Arg(2).IsString() ? L.Arg(2).Str()->s : "bt";
    if (!src.empty() && src[0] == '\x1b') {
        L.PushNil();
        L.PushString("attempt to load a binary chunk (binary chunks are not supported)");
        return 2;
    }
    if (mode.find('t') == std::string::npos) {
        L.PushNil();
        L.PushString("attempt to load a text chunk (mode is '" + mode + "')");
        return 2;
    }
    Table* env = nullptr;
    bool hasEnv = n >= 4;
    if (hasEnv && L.Arg(3).IsTable()) env = L.Arg(3).Tab();
    SyntaxError se;
    if (!L.Load(src, name, &se, env)) {
        L.PushNil();
        L.PushString(se.message);
        return 2;
    }
    if (hasEnv && !L.Arg(3).IsTable()) {
        // a non-table environment (nil): the chunk's _ENV is that value
        Value f = L.At(-1);
        f.LFn()->up[0]->v = L.Arg(3);
    }
    return 1;
}

int B_collectgarbage(State& L, int n) {
    std::string opt = n >= 1 && L.Arg(0).IsString() ? L.Arg(0).Str()->s : "collect";
    if (opt == "count") {
        L.Push(Value::Num((double)L.MemoryInUse() / 1024.0));
        return 1;
    }
    if (opt == "collect") { L.CollectGarbage(); L.Push(Value::Int(0)); return 1; }
    if (opt == "step") { L.CollectGarbage(); L.Push(Value::Bool(true)); return 1; }
    if (opt == "isrunning") { L.Push(Value::Bool(true)); return 1; }
    if (opt == "incremental" || opt == "generational") { L.PushString("incremental"); return 1; }
    if (opt == "stop" || opt == "restart" || opt == "setpause" || opt == "setstepmul") { L.Push(Value::Int(0)); return 1; }
    L.ArgError(0, "collectgarbage", "invalid option '" + opt + "'");
}

// ------------------------------------------------------------------ coroutine
Coroutine* CheckCo(State& L, int arg, const char* fname) {
    Value v = L.Arg(arg);
    if (v.t != Type::Thread) L.TypeError(arg, fname, "coroutine");
    return v.Co();
}

int C_create(State& L, int n) {
    (void)n;
    Value f = L.Arg(0);
    if (!f.IsFunction()) L.TypeError(0, "create", "function");
    L.Push(Value::Obj(L.NewCoroutine(f)));
    return 1;
}

int C_resume(State& L, int n) {
    Coroutine* co = CheckCo(L, 0, "resume");
    int from = L.Current()->top - (n - 1);
    int nres = 0;
    bool ok = L.Resume(co, n - 1, &nres);
    return InsertFirst(L, from, Value::Bool(ok));
}

int C_yield(State& L, int n) { return L.Yield(n); }

int C_status(State& L, int n) {
    (void)n;
    Coroutine* co = CheckCo(L, 0, "status");
    const char* s = "dead";
    if (co == L.Current()) s = "running";
    else if (co->status == CoStatus::Suspended) s = "suspended";
    else if (co->status == CoStatus::Normal) s = "normal";
    else if (co->status == CoStatus::Running) s = "running";
    L.PushString(s);
    return 1;
}

int WrapAux(State& L, int n) {
    NClosure* self = L.Current()->frames.back().ncl;
    Coroutine* co = self->up[0].Co();
    int from = L.Current()->top - n;
    int nres = 0;
    if (!L.Resume(co, n, &nres)) {
        Value e = L.At(-1);
        if (e.IsString()) e = Str(L, L.Where(1) + e.Str()->s);
        L.ErrorValue(e);
    }
    return L.Current()->top - from;
}

int C_wrap(State& L, int n) {
    (void)n;
    Value f = L.Arg(0);
    if (!f.IsFunction()) L.TypeError(0, "wrap", "function");
    Coroutine* co = L.NewCoroutine(f);
    L.Push(Value::Obj(co));   // reachable while the closure is made
    NClosure* c = L.NewNative(WrapAux, "wrap", {Value::Obj(co)});
    L.Pop();
    L.Push(Value::Obj(c));
    return 1;
}

int C_isyieldable(State& L, int n) {
    (void)n;
    L.Push(Value::Bool(L.IsYieldable()));
    return 1;
}

int C_running(State& L, int n) {
    (void)n;
    L.Push(Value::Obj(L.Current()));
    L.Push(Value::Bool(L.Current() == L.Main()));
    return 2;
}

int C_close(State& L, int n) {
    (void)n;
    Coroutine* co = CheckCo(L, 0, "close");
    if (co == L.Current() || co->status == CoStatus::Running) L.Error("cannot close a running coroutine");
    if (co->status == CoStatus::Normal) L.Error("cannot close a normal coroutine");
    if (co->status == CoStatus::Dead) {
        if (co->errored) {
            co->errored = false;
            L.Push(Value::Bool(false));
            L.Push(co->err);
            co->err = Value();
            return 2;
        }
        L.Push(Value::Bool(true));
        return 1;
    }
    try {
        L.CloseCoroutine(co);
    } catch (const LuaError&) {
        Value e = L.errorValue;
        L.errorValue = Value();
        L.Push(Value::Bool(false));
        L.Push(e);
        return 2;
    }
    L.Push(Value::Bool(true));
    return 1;
}

// ------------------------------------------------------------------ math
Value FloatOrInt(double d) {
    if (d >= -9223372036854775808.0 && d < 9223372036854775808.0) return Value::Int((std::int64_t)d);
    return Value::Num(d);
}

int M_abs(State& L, int n) {
    (void)n;
    Value v;
    if (!State::ToNumber(L.Arg(0), v)) L.TypeError(0, "abs", "number");
    if (v.t == Type::Int) L.Push(Value::Int(v.i < 0 ? (std::int64_t)(0ull - (std::uint64_t)v.i) : v.i));
    else L.Push(Value::Num(std::fabs(v.n)));
    return 1;
}
int M_floor(State& L, int n) {
    (void)n;
    Value v;
    if (!State::ToNumber(L.Arg(0), v)) L.TypeError(0, "floor", "number");
    L.Push(v.t == Type::Int ? v : FloatOrInt(std::floor(v.n)));
    return 1;
}
int M_ceil(State& L, int n) {
    (void)n;
    Value v;
    if (!State::ToNumber(L.Arg(0), v)) L.TypeError(0, "ceil", "number");
    L.Push(v.t == Type::Int ? v : FloatOrInt(std::ceil(v.n)));
    return 1;
}
#define MATH1(NAME, EXPR) \
    int M_##NAME(State& L, int n) { (void)n; double x = L.CheckNum(0, #NAME); L.Push(Value::Num(EXPR)); return 1; }
MATH1(sqrt, std::sqrt(x))
MATH1(sin, std::sin(x))
MATH1(cos, std::cos(x))
MATH1(tan, std::tan(x))
MATH1(asin, std::asin(x))
MATH1(acos, std::acos(x))
MATH1(exp, std::exp(x))
MATH1(deg, x * (180.0 / 3.14159265358979323846))
MATH1(rad, x * (3.14159265358979323846 / 180.0))
#undef MATH1
int M_atan(State& L, int n) {
    double y = L.CheckNum(0, "atan");
    double x = n >= 2 && !L.Arg(1).IsNil() ? L.CheckNum(1, "atan") : 1.0;
    L.Push(Value::Num(std::atan2(y, x)));
    return 1;
}
int M_log(State& L, int n) {
    double x = L.CheckNum(0, "log");
    double r;
    if (n < 2 || L.Arg(1).IsNil()) r = std::log(x);
    else {
        double b = L.CheckNum(1, "log");
        if (b == 2.0) r = std::log2(x);
        else if (b == 10.0) r = std::log10(x);
        else r = std::log(x) / std::log(b);
    }
    L.Push(Value::Num(r));
    return 1;
}
int M_fmod(State& L, int n) {
    (void)n;
    Value a, b;
    if (!State::ToNumber(L.Arg(0), a)) L.TypeError(0, "fmod", "number");
    if (!State::ToNumber(L.Arg(1), b)) L.TypeError(1, "fmod", "number");
    if (a.t == Type::Int && b.t == Type::Int) {
        if (b.i == 0) L.ArgError(1, "fmod", "zero");
        if (b.i == -1) { L.Push(Value::Int(0)); return 1; }
        L.Push(Value::Int(a.i % b.i));   // C semantics: truncated
        return 1;
    }
    L.Push(Value::Num(std::fmod(a.AsNum(), b.AsNum())));
    return 1;
}
int M_modf(State& L, int n) {
    (void)n;
    double x = L.CheckNum(0, "modf");
    double ip = std::isinf(x) ? x : (x >= 0 ? std::floor(x) : std::ceil(x));
    double fp = std::isinf(x) ? 0.0 : x - ip;
    L.Push(Value::Num(ip));
    L.Push(Value::Num(fp));
    return 2;
}
int M_tointeger(State& L, int n) {
    (void)n;
    Value v = L.Arg(0);
    std::int64_t i;
    if (v.IsNumber() && State::ToInteger(v, i)) L.Push(Value::Int(i));
    else { L.CheckAny(0, "tointeger"); L.PushNil(); }
    return 1;
}
int M_type(State& L, int n) {
    (void)n;
    L.CheckAny(0, "type");
    Value v = L.Arg(0);
    if (v.t == Type::Int) L.PushString("integer");
    else if (v.t == Type::Float) L.PushString("float");
    else L.PushNil();
    return 1;
}
int M_ult(State& L, int n) {
    (void)n;
    std::uint64_t a = (std::uint64_t)L.CheckInt(0, "ult"), b = (std::uint64_t)L.CheckInt(1, "ult");
    L.Push(Value::Bool(a < b));
    return 1;
}
int MinMax(State& L, int n, bool isMax, const char* name) {
    if (n < 1) L.ArgError(0, name, "number expected, got no value");
    Value best;
    for (int i = 0; i < n; ++i) {
        Value v;
        if (!State::ToNumber(L.Arg(i), v)) L.TypeError(i, name, "number");
        if (i == 0 || (isMax ? L.LessThan(best, v) : L.LessThan(v, best))) best = v;
    }
    L.Push(best);
    return 1;
}
int M_max(State& L, int n) { return MinMax(L, n, true, "max"); }
int M_min(State& L, int n) { return MinMax(L, n, false, "min"); }

std::uint64_t Rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
std::uint64_t NextRand(std::uint64_t* s) {
    std::uint64_t result = Rotl(s[1] * 5, 7) * 9;
    std::uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = Rotl(s[3], 45);
    return result;
}
void SeedRand(std::uint64_t* s, std::uint64_t a, std::uint64_t b) {
    std::uint64_t x = a ^ (b * 0x9E3779B97F4A7C15ull);
    for (int i = 0; i < 4; ++i) {   // splitmix64 steps
        x += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = x;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        s[i] = z ^ (z >> 31);
    }
    if (!(s[0] | s[1] | s[2] | s[3])) s[0] = 1;
    for (int i = 0; i < 16; ++i) NextRand(s);
}

int M_random(State& L, int n) {
    std::uint64_t r = NextRand(L.rng);
    std::int64_t lo, hi;
    if (n == 0) {
        L.Push(Value::Num((double)(r >> 11) * (1.0 / 9007199254740992.0)));
        return 1;
    }
    if (n == 1) {
        lo = 1;
        hi = L.CheckInt(0, "random");
        if (hi == 0) { L.Push(Value::Int((std::int64_t)r)); return 1; }
    } else if (n == 2) {
        lo = L.CheckInt(0, "random");
        hi = L.CheckInt(1, "random");
    } else {
        L.Error("wrong number of arguments");
    }
    if (lo > hi) L.ArgError(n == 1 ? 0 : 1, "random", "interval is empty");
    std::uint64_t lim = (std::uint64_t)hi - (std::uint64_t)lo;
    std::uint64_t mask = lim;
    mask |= mask >> 1; mask |= mask >> 2; mask |= mask >> 4; mask |= mask >> 8; mask |= mask >> 16; mask |= mask >> 32;
    while ((r & mask) > lim) r = NextRand(L.rng);   // fewer than two tries on average
    L.Push(Value::Int((std::int64_t)((std::uint64_t)lo + (r & mask))));
    return 1;
}

int M_randomseed(State& L, int n) {
    std::uint64_t a, b = 0;
    if (n == 0) {
        a = (std::uint64_t)std::time(nullptr);
        b = (std::uint64_t)(std::uintptr_t)&L;
    } else {
        Value v = L.Arg(0);
        std::int64_t i;
        if (v.t == Type::Float && !State::ToInteger(v, i)) { std::memcpy(&a, &v.n, 8); }
        else a = (std::uint64_t)L.CheckInt(0, "randomseed");
        if (n >= 2) b = (std::uint64_t)L.CheckInt(1, "randomseed");
    }
    SeedRand(L.rng, a, b);
    L.Push(Value::Int((std::int64_t)a));
    L.Push(Value::Int((std::int64_t)b));
    return 2;
}

// ------------------------------------------------------------------ os (safe parts)
int O_clock(State& L, int n) {
    (void)n;
    L.Push(Value::Num((double)std::clock() / (double)CLOCKS_PER_SEC));
    return 1;
}

bool LocalTm(std::time_t t, std::tm& out, bool utc) {
#ifdef _WIN32
    return (utc ? gmtime_s(&out, &t) : localtime_s(&out, &t)) == 0;
#else
    return (utc ? gmtime_r(&t, &out) : localtime_r(&t, &out)) != nullptr;
#endif
}

int GetField(State& L, Table* t, const char* key, int def, bool required) {
    Value v = t->Get(Str(L, key));
    std::int64_t i;
    if (v.IsNil()) {
        if (required) L.Error(std::string("field '") + key + "' missing in date table");
        return def;
    }
    if (!State::ToInteger(v, i)) L.Error(std::string("field '") + key + "' is not an integer");
    if (i < -2000000000 || i > 2000000000) L.Error(std::string("field '") + key + "' is out-of-bound");
    return (int)i;
}

int O_time(State& L, int n) {
    if (n == 0 || L.Arg(0).IsNil()) {
        L.Push(Value::Int((std::int64_t)std::time(nullptr)));
        return 1;
    }
    Table* t = L.CheckTable(0, "time");
    std::tm tm{};
    tm.tm_year = GetField(L, t, "year", 0, true) - 1900;
    tm.tm_mon = GetField(L, t, "month", 0, true) - 1;
    tm.tm_mday = GetField(L, t, "day", 0, true);
    tm.tm_hour = GetField(L, t, "hour", 12, false);
    tm.tm_min = GetField(L, t, "min", 0, false);
    tm.tm_sec = GetField(L, t, "sec", 0, false);
    Value dst = t->Get(Str(L, "isdst"));
    tm.tm_isdst = dst.IsNil() ? -1 : dst.Truthy();
    std::time_t r = std::mktime(&tm);
    if (r == (std::time_t)-1) L.Error("time result cannot be represented in this installation");
    L.Push(Value::Int((std::int64_t)r));
    return 1;
}

int O_date(State& L, int n) {
    std::string fmt = n >= 1 && !L.Arg(0).IsNil() ? L.CheckStr(0, "date")->s : "%c";
    std::time_t t = n >= 2 && !L.Arg(1).IsNil() ? (std::time_t)L.CheckInt(1, "date") : std::time(nullptr);
    bool utc = !fmt.empty() && fmt[0] == '!';
    if (utc) fmt = fmt.substr(1);
    std::tm tm{};
    if (!LocalTm(t, tm, utc)) L.Error("date result cannot be represented in this installation");
    if (fmt.rfind("*t", 0) == 0) {
        Table* r = L.NewTable(0, 9);
        L.Push(Value::Obj(r));
        auto set = [&](const char* k, int v) { r->Set(L, Str(L, k), Value::Int(v)); };
        set("year", tm.tm_year + 1900);
        set("month", tm.tm_mon + 1);
        set("day", tm.tm_mday);
        set("hour", tm.tm_hour);
        set("min", tm.tm_min);
        set("sec", tm.tm_sec);
        set("yday", tm.tm_yday + 1);
        set("wday", tm.tm_wday + 1);
        r->Set(L, Str(L, "isdst"), Value::Bool(tm.tm_isdst > 0));
        return 1;
    }
    // each conversion is checked first: an unknown one is an error, never undefined behaviour
    static const char* const kValid = "aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%";
    std::string out;
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%') { out += fmt[i]; continue; }
        if (i + 1 >= fmt.size() || !std::strchr(kValid, fmt[i + 1]) || fmt[i + 1] == 0)
            L.ArgError(0, "date", "invalid conversion specifier '%" + fmt.substr(i + 1, 1) + "'");
        char spec[3] = {'%', fmt[i + 1], 0};
        char buf[256];
        std::size_t len = std::strftime(buf, sizeof buf, spec, &tm);
        out.append(buf, len);
        ++i;
    }
    L.PushString(out);
    return 1;
}

int O_difftime(State& L, int n) {
    double a = (double)L.CheckInt(0, "difftime");
    double b = n >= 2 ? (double)L.OptInt(1, "difftime", 0) : 0.0;
    L.Push(Value::Num(a - b));
    return 1;
}

Table* NewLib(State& L, const char* name) {
    Table* t = L.NewTable();
    L.SetGlobal(name, Value::Obj(t));
    return t;
}

} // namespace

// pcall called from native code, or of a native function (a Lua function called from
// Lua code runs inside the loop instead, so it can yield).
int PCallNative(State& L, int n) {
    L.CheckAny(0, "pcall");
    int func = L.Current()->top - n;
    bool ok = L.PCall(n - 1, -1);
    return InsertFirst(L, func, Value::Bool(ok));
}

void OpenBase(State& L) {
    Table* g = L.Globals();
    L.SetGlobal("_G", Value::Obj(g));
    L.SetGlobal("_VERSION", Value::Obj(L.NewString("Lua 5.4")));
    const struct { const char* n; NativeFn f; } fns[] = {
        {"print", B_print}, {"type", B_type}, {"tostring", B_tostring}, {"tonumber", B_tonumber},
        {"rawequal", B_rawequal}, {"rawlen", B_rawlen}, {"rawget", B_rawget}, {"rawset", B_rawset},
        {"setmetatable", B_setmetatable}, {"getmetatable", B_getmetatable}, {"assert", B_assert},
        {"error", B_error}, {"pcall", PCallNative}, {"xpcall", B_xpcall}, {"select", B_select}, {"next", B_next},
        {"pairs", B_pairs}, {"ipairs", B_ipairs}, {"load", B_load}, {"collectgarbage", B_collectgarbage},
    };
    for (const auto& f : fns) L.Register(g, f.n, f.f);
}

void OpenCoroutine(State& L) {
    Table* t = NewLib(L, "coroutine");
    L.Register(t, "create", C_create);
    L.Register(t, "resume", C_resume);
    L.Register(t, "yield", C_yield);
    L.Register(t, "status", C_status);
    L.Register(t, "wrap", C_wrap);
    L.Register(t, "isyieldable", C_isyieldable);
    L.Register(t, "running", C_running);
    L.Register(t, "close", C_close);
}

void OpenMath(State& L) {
    Table* t = NewLib(L, "math");
    const struct { const char* n; NativeFn f; } fns[] = {
        {"abs", M_abs}, {"floor", M_floor}, {"ceil", M_ceil}, {"sqrt", M_sqrt}, {"sin", M_sin}, {"cos", M_cos},
        {"tan", M_tan}, {"asin", M_asin}, {"acos", M_acos}, {"atan", M_atan}, {"exp", M_exp}, {"log", M_log},
        {"fmod", M_fmod}, {"modf", M_modf}, {"tointeger", M_tointeger}, {"type", M_type}, {"ult", M_ult},
        {"max", M_max}, {"min", M_min}, {"random", M_random}, {"randomseed", M_randomseed}, {"deg", M_deg},
        {"rad", M_rad},
    };
    for (const auto& f : fns) L.Register(t, f.n, f.f);
    t->Set(L, Value::Obj(L.NewString("pi")), Value::Num(3.14159265358979323846));
    t->Set(L, Value::Obj(L.NewString("huge")), Value::Num(HUGE_VAL));
    t->Set(L, Value::Obj(L.NewString("maxinteger")), Value::Int(INT64_MAX));
    t->Set(L, Value::Obj(L.NewString("mininteger")), Value::Int(INT64_MIN));
    SeedRand(L.rng, (std::uint64_t)std::time(nullptr), (std::uint64_t)(std::uintptr_t)&L);
}

void OpenOs(State& L) {
    Table* t = NewLib(L, "os");
    L.Register(t, "clock", O_clock);
    L.Register(t, "time", O_time);
    L.Register(t, "date", O_date);
    L.Register(t, "difftime", O_difftime);
}

} // namespace okay::lua
