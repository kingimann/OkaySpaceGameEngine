// Values, tables, garbage collector, and the virtual machine of our own Lua 5.4.
// Written from the Lua 5.4 reference manual (basic concepts, metatables and the
// semantics of operators and coroutines), in our own structure.
#include "LuaCore.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace okay::lua {

static double NowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ============================================================ numbers
std::string FormatNumber(const Value& v) {
    char buf[64];
    if (v.t == T::Int) { std::snprintf(buf, sizeof buf, "%lld", (long long)v.i); return buf; }
    double d = v.n;
    if (std::isnan(d)) return std::signbit(d) ? "-nan" : "nan";
    if (std::isinf(d)) return d < 0 ? "-inf" : "inf";
    std::snprintf(buf, sizeof buf, "%.14g", d);
    if (!std::strpbrk(buf, ".eEn")) std::strcat(buf, ".0");
    return buf;
}

bool ParseNumber(const std::string& str, Value& out) {
    std::size_t b = 0, e = str.size();
    while (b < e && std::isspace((unsigned char)str[b])) ++b;
    while (e > b && std::isspace((unsigned char)str[e - 1])) --e;
    if (b >= e) return false;
    std::string s = str.substr(b, e - b);
    std::size_t i = 0;
    bool neg = false;
    if (s[i] == '-') { neg = true; ++i; } else if (s[i] == '+') ++i;
    if (i >= s.size()) return false;
    bool hex = (s.size() > i + 1 && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X'));
    if (hex) {
        std::size_t j = i + 2;
        bool isFloat = false;
        for (std::size_t q = j; q < s.size(); ++q) if (s[q] == '.' || s[q] == 'p' || s[q] == 'P') isFloat = true;
        if (!isFloat) {
            if (j >= s.size()) return false;
            std::uint64_t v = 0;
            for (; j < s.size(); ++j) {
                char c = s[j]; int d;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else return false;
                v = v * 16 + (unsigned)d;
            }
            out = Value::Int((std::int64_t)(neg ? 0 - v : v));
            return true;
        }
        for (std::size_t q = i + 2; q < s.size(); ++q) {
            char c = s[q];
            if (!(std::isxdigit((unsigned char)c) || c == '.' || c == 'p' || c == 'P' || c == '+' || c == '-')) return false;
        }
        char* end = nullptr;
        double d = std::strtod(s.c_str(), &end);
        if (*end) return false;
        out = Value::Num(d); return true;
    }
    bool isInt = true;
    for (std::size_t q = i; q < s.size(); ++q) {
        char c = s[q];
        if (c >= '0' && c <= '9') continue;
        isInt = false;
        if (!(c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')) return false;
    }
    if (isInt) {
        std::uint64_t v = 0; bool overflow = false;
        for (std::size_t q = i; q < s.size(); ++q) {
            unsigned d = (unsigned)(s[q] - '0');
            if (v > (UINT64_MAX - d) / 10) { overflow = true; break; }
            v = v * 10 + d;
        }
        if (!overflow && (neg ? v <= (std::uint64_t)INT64_MAX + 1 : v <= (std::uint64_t)INT64_MAX)) {
            out = Value::Int((std::int64_t)(neg ? 0 - v : v)); return true;
        }
    }
    char* end = nullptr;
    double d = std::strtod(s.c_str(), &end);
    if (*end || end == s.c_str()) return false;
    out = Value::Num(d);
    return true;
}

bool ToNumber(const Value& v, Value& out) {
    if (v.IsNumber()) { out = v; return true; }
    if (v.t == T::Str) return ParseNumber(static_cast<Str*>(v.o)->s, out);
    return false;
}

static bool FloatToInt(double d, std::int64_t& out) {
    if (!(d >= -9223372036854775808.0 && d < 9223372036854775808.0)) return false;
    double f = std::floor(d);
    if (f != d) return false;
    out = (std::int64_t)f; return true;
}

bool ToInteger(const Value& v, std::int64_t& out) {
    if (v.t == T::Int) { out = v.i; return true; }
    if (v.t == T::Num) return FloatToInt(v.n, out);
    if (v.t == T::Str) { Value n; if (ParseNumber(static_cast<Str*>(v.o)->s, n)) return ToInteger(n, out); }
    return false;
}

// ============================================================ tables
std::size_t ValueHash::operator()(const Value& v) const {
    switch (v.t) {
        case T::Nil: return 0;
        case T::Bool: return v.b ? 1 : 2;
        case T::Int: return std::hash<std::int64_t>()(v.i);
        case T::Num: return std::hash<double>()(v.n);
        case T::Str: return std::hash<std::string>()(static_cast<Str*>(v.o)->s);
        default: return std::hash<const void*>()(v.o);
    }
}
bool ValueEq::operator()(const Value& a, const Value& b) const {
    if (a.t != b.t) return false;
    switch (a.t) {
        case T::Nil: return true;
        case T::Bool: return a.b == b.b;
        case T::Int: return a.i == b.i;
        case T::Num: return a.n == b.n;
        case T::Str: return a.o == b.o || static_cast<Str*>(a.o)->s == static_cast<Str*>(b.o)->s;
        default: return a.o == b.o;
    }
}

static Value NormKey(const Value& k) {
    if (k.t == T::Num) { std::int64_t i; if (FloatToInt(k.n, i)) return Value::Int(i); }
    return k;
}

Value Table::Get(const Value& k0) const {
    if (k0.t == T::Int) {
        if (k0.i >= 1 && (std::uint64_t)k0.i <= arr.size()) return arr[(std::size_t)k0.i - 1];
    } else if (k0.t == T::Nil) return Value();
    Value k = NormKey(k0);
    if (k.t == T::Int && k.i >= 1 && (std::uint64_t)k.i <= arr.size()) return arr[(std::size_t)k.i - 1];
    if (idx.empty()) return Value();
    auto it = idx.find(k);
    return it == idx.end() ? Value() : ents[it->second].second;
}

Value Table::GetStr(const std::string& s) const {
    if (idx.empty()) return Value();
    Str tmp(s);
    Value k; k.t = T::Str; k.o = &tmp;
    auto it = idx.find(k);
    return it == idx.end() ? Value() : ents[it->second].second;
}

void Table::Compact() {
    std::vector<std::pair<Value, Value>> n;
    n.reserve(ents.size() - dead);
    for (auto& e : ents) if (!e.second.IsNil()) n.push_back(e);
    ents.swap(n); dead = 0;
    idx.clear();
    for (std::size_t i = 0; i < ents.size(); ++i) idx[ents[i].first] = i;
}

void Table::Set(State& L, const Value& k0, const Value& v) {
    Value k = NormKey(k0);
    if (k.t == T::Int && k.i >= 1) {
        std::uint64_t u = (std::uint64_t)k.i;
        if (u <= arr.size()) {
            arr[u - 1] = v;
            if (v.IsNil() && u == arr.size()) { while (!arr.empty() && arr.back().IsNil()) arr.pop_back(); }
            return;
        }
        if (u == arr.size() + 1 && !v.IsNil()) {
            L.allocated += 16; if (L.allocated > L.threshold) L.gcPending = true;
            arr.push_back(v);
            if (!idx.empty()) {   // pull following keys out of the hash part
                for (;;) {
                    Value nk = Value::Int((std::int64_t)arr.size() + 1);
                    auto it = idx.find(nk);
                    if (it == idx.end() || ents[it->second].second.IsNil()) break;
                    arr.push_back(ents[it->second].second);
                    ents[it->second].second = Value(); ++dead;
                    idx.erase(it);
                }
            }
            return;
        }
    }
    auto it = idx.find(k);
    if (it != idx.end()) {
        bool wasNil = ents[it->second].second.IsNil();
        ents[it->second].second = v;
        if (v.IsNil() && !wasNil) ++dead;
        else if (!v.IsNil() && wasNil) --dead;
        return;
    }
    if (v.IsNil()) return;
    if (dead > 8 && dead * 2 > ents.size()) Compact();
    L.allocated += 48; if (L.allocated > L.threshold) L.gcPending = true;
    ents.emplace_back(k, v);
    idx[k] = ents.size() - 1;
}

std::int64_t Table::Length() const { return (std::int64_t)arr.size(); }

bool Table::Next(const Value& k0, Value& nk, Value& nv, bool& bad) const {
    bad = false;
    std::size_t ai = 0, ei = 0;
    Value k = NormKey(k0);
    if (k.IsNil()) { ai = 0; }
    else if (k.t == T::Int && k.i >= 1 && (std::uint64_t)k.i <= arr.size()) ai = (std::size_t)k.i;
    else {
        auto it = idx.find(k);
        if (it == idx.end()) {
            if (k.t == T::Int && k.i >= 1) { ai = arr.size(); ei = 0; }   // array shrank during traversal
            else { bad = true; return false; }
        } else { ai = arr.size(); ei = it->second + 1; }
    }
    for (; ai < arr.size(); ++ai)
        if (!arr[ai].IsNil()) { nk = Value::Int((std::int64_t)ai + 1); nv = arr[ai]; return true; }
    for (; ei < ents.size(); ++ei)
        if (!ents[ei].second.IsNil()) { nk = ents[ei].first; nv = ents[ei].second; return true; }
    return false;
}

// ============================================================ state, objects, GC
State::State() {
    mainCo = new Coro(); mainCo->isMain = true; mainCo->status = CoStatus::Running;
    mainCo->next = all; all = mainCo; cur = mainCo;
    globals = NewTable();
    stringMeta = NewTable();
    globals->Set(*this, NewStr("_G"), Value::Of(globals));
}

State::~State() {
    Obj* o = all;
    while (o) { Obj* n = o->next; delete o; o = n; }
}

static std::size_t ObjSize(Obj* o) {
    switch (o->type) {
        case T::Str: return 40 + static_cast<Str*>(o)->s.size();
        case T::Table: { auto* t = static_cast<Table*>(o); return 72 + t->arr.capacity() * 16 + t->ents.capacity() * 40 + t->idx.size() * 40; }
        case T::Func: return 40 + 8 * static_cast<Closure*>(o)->ups.size();
        case T::Cell: return 32;
        case T::Coro: { auto* c = static_cast<Coro*>(o); return 160 + c->st.capacity() * 16 + c->frames.capacity() * sizeof(Frame); }
        default: return 96;
    }
}

static void Track(State& L, Obj* o, std::size_t bytes) {
    o->next = L.all; L.all = o;
    L.allocated += bytes;
    if (L.allocated > L.threshold) L.gcPending = true;
}

Value State::NewStr(const std::string& s) {
    Reserve(s.size());
    auto* o = new Str(s); Track(*this, o, 40 + s.size());
    return Value::Of(o);
}
Table* State::NewTable() { auto* o = new Table(); Track(*this, o, 72); return o; }
Native* State::NewNative(const std::string& name, NativeFn fn) {
    auto* o = new Native(); o->name = name; o->fn = std::move(fn); Track(*this, o, 96); return o;
}
User* State::NewUser(int kind, std::uint64_t id, Table* meta) {
    auto* o = new User(); o->kind = kind; o->id = id; o->meta = meta; Track(*this, o, 48); return o;
}
Coro* State::NewCoro(const Value& fn) {
    auto* c = new Coro(); c->startFn = fn; Track(*this, c, 200); return c;
}
Value State::MakeFunction(Proto* p) {
    auto* c = new Closure(p);
    Track(*this, c, 48);
    for (std::size_t i = 0; i < p->ups.size(); ++i) c->ups.push_back(nullptr);
    return Value::Of(c);
}

void State::Reserve(std::size_t bytes) {
    if (limits.memoryBytes && allocated + bytes > limits.memoryBytes + (limits.memoryBytes >> 3)) {
        if (bytes > limits.memoryBytes)
            Error("not enough memory (script limit is " + std::to_string(limits.memoryBytes >> 20) + " MB)");
        gcPending = true;
    }
}

void State::Collect() {
    gcPending = false;
    std::vector<Obj*> gray;
    auto markObj = [&](Obj* o) { if (o && !o->mark) { o->mark = true; gray.push_back(o); } };
    auto markV = [&](const Value& v) { if (v.IsObj()) markObj(v.o); };
    markObj(globals); markObj(stringMeta); markObj(mainCo); markObj(cur);
    for (Coro* c = cur; c; c = c->resumer) markObj(c);
    for (auto* p : pins) for (auto& v : *p) markV(v);
    for (Obj* o : extraRoots) markObj(o);
    for (auto& p : protos) for (auto& v : p->k) markV(v);
    while (!gray.empty()) {
        Obj* o = gray.back(); gray.pop_back();
        switch (o->type) {
            case T::Table: {
                auto* t = static_cast<Table*>(o);
                for (auto& v : t->arr) markV(v);
                for (auto& e : t->ents) { if (e.second.IsNil()) continue; markV(e.first); markV(e.second); }
                markObj(t->meta); break;
            }
            case T::Func: for (Cell* c : static_cast<Closure*>(o)->ups) markObj(c); break;
            case T::Cell: markV(static_cast<Cell*>(o)->v); break;
            case T::Native: for (auto& v : static_cast<Native*>(o)->up) markV(v); break;
            case T::User: markObj(static_cast<User*>(o)->meta); break;
            case T::Coro: {
                auto* c = static_cast<Coro*>(o);
                for (auto& v : c->st) markV(v);
                for (auto& f : c->frames) { markObj(f.cl); markV(f.handler); for (auto& v : f.varargs) markV(v); }
                markV(c->startFn);
                for (auto& v : c->transfer) markV(v);
                markObj(c->resumer);
                break;
            }
            default: break;
        }
    }
    std::size_t live = 0;
    Obj** link = &all;
    while (*link) {
        Obj* o = *link;
        if (o->mark) { o->mark = false; live += ObjSize(o); link = &o->next; }
        else { *link = o->next; delete o; }
    }
    allocated = liveAfterGC = live;
    threshold = std::max<std::size_t>(1u << 20, live * 2);
    if (limits.memoryBytes) {
        if (live > limits.memoryBytes)
            ErrorFatal("not enough memory (script limit is " + std::to_string(limits.memoryBytes >> 20) + " MB)");
        threshold = std::min(threshold, limits.memoryBytes);
    }
}

void State::ResetBudget() {
    budget = limits.instructions ? limits.instructions : INT64_MAX;
    deadline = limits.seconds > 0 ? NowSeconds() + limits.seconds : 0;
}

// ============================================================ errors / strings
std::string State::Where(int level) {
    if (level == 1 && directNative) return "";   // called straight from pcall: no script line
    Coro* co = cur;
    int n = (int)co->frames.size() - level;
    if (level < 1 || n < 0) return "";
    const Frame& f = co->frames[n];
    int pc = f.pc > 0 ? f.pc - 1 : 0;
    int line = pc < (int)f.cl->p->lines.size() ? f.cl->p->lines[pc] : 0;
    return f.cl->p->chunk + ":" + std::to_string(line) + ": ";
}

void State::ErrorValue(const Value& v) {
    LuaError e; e.v = v;
    if (v.t == T::Str) e.text = static_cast<Str*>(v.o)->s;
    else if (v.IsNumber()) e.text = FormatNumber(v);
    else e.text = std::string("(error object is a ") + TypeName(v) + " value)";
    throw e;
}
void State::Error(const std::string& msg) {
    Value s = NewStr(Where(1) + msg);
    ErrorValue(s);
}
void State::ErrorFatal(const std::string& msg) {
    LuaError e; e.fatal = true;
    e.text = Where(1) + msg;
    e.v = Value();
    throw e;
}

const char* State::TypeName(const Value& v) const {
    switch (v.t) {
        case T::Nil: return "nil"; case T::Bool: return "boolean";
        case T::Int: case T::Num: return "number"; case T::Str: return "string";
        case T::Table: return "table"; case T::Func: case T::Native: return "function";
        case T::Coro: return "thread"; default: return "userdata";
    }
}

std::string State::RawToString(const Value& v) const {
    char buf[64];
    switch (v.t) {
        case T::Nil: return "nil";
        case T::Bool: return v.b ? "true" : "false";
        case T::Int: case T::Num: return FormatNumber(v);
        case T::Str: return static_cast<Str*>(v.o)->s;
        default: std::snprintf(buf, sizeof buf, "%s: %p", TypeName(v), (void*)v.o); return buf;
    }
}

Table* State::MetaOf(const Value& v) const {
    if (v.t == T::Table) return static_cast<Table*>(v.o)->meta;
    if (v.t == T::User) return static_cast<User*>(v.o)->meta;
    if (v.t == T::Str) return stringMeta;
    return nullptr;
}
Value State::GetMeta(const Value& v, const char* ev) {
    Table* m = MetaOf(v);
    return m ? m->GetStr(ev) : Value();
}

std::string State::ToStringValue(const Value& v) {
    Value h = GetMeta(v, "__tostring");
    if (!h.IsNil()) {
        auto r = Call(h, {v}, 1);
        if (r.empty() || r[0].t != T::Str) {
            if (!r.empty() && r[0].IsNumber()) return FormatNumber(r[0]);
            Error("'__tostring' must return a string");
        }
        return static_cast<Str*>(r[0].o)->s;
    }
    return RawToString(v);
}

bool State::RawEquals(const Value& a, const Value& b) const {
    if (a.t != b.t) {
        std::int64_t j;
        if (a.t == T::Int && b.t == T::Num) return FloatToInt(b.n, j) && a.i == j;
        if (a.t == T::Num && b.t == T::Int) return FloatToInt(a.n, j) && b.i == j;
        return false;
    }
    return ValueEq()(a, b);
}
bool State::Equals(const Value& a, const Value& b) {
    if (RawEquals(a, b)) return true;
    if (a.t != b.t || (a.t != T::Table && a.t != T::User)) return false;
    Value h = GetMeta(a, "__eq");
    if (h.IsNil()) h = GetMeta(b, "__eq");
    if (h.IsNil()) return false;
    auto r = Call(h, {a, b}, 1);
    return !r.empty() && r[0].Truthy();
}

// ============================================================ operators
static std::string HintPart(const std::string& hint, int which) {
    std::size_t bar = hint.find('|');
    if (bar == std::string::npos) return which == 0 ? hint : std::string();
    return which == 0 ? hint.substr(0, bar) : hint.substr(bar + 1);
}
static std::string WithHint(const std::string& h) { return h.empty() || h == "constant" ? std::string() : " (" + h + ")"; }

Value State::IndexH(const Value& obj0, const Value& key, const std::string& hint) {
    Value obj = obj0;
    for (int loop = 0; loop < 100; ++loop) {
        Value h;
        if (obj.t == T::Table) {
            auto* t = static_cast<Table*>(obj.o);
            Value v = t->Get(key);
            if (!v.IsNil()) return v;
            if (!t->meta) return Value();
            h = t->meta->GetStr("__index");
            if (h.IsNil()) return Value();
        } else {
            h = GetMeta(obj, "__index");
            if (h.IsNil()) {
                std::string kd = key.t == T::Str ? " (field '" + static_cast<Str*>(key.o)->s + "')" : "";
                (void)kd;
                Error(std::string("attempt to index a ") + TypeName(obj) + " value" +
                      (loop == 0 ? WithHint(hint) : std::string()));
            }
        }
        if (h.t == T::Func || h.t == T::Native) {
            auto r = Call(h, {obj, key}, 1);
            return r.empty() ? Value() : r[0];
        }
        obj = h;
    }
    Error("'__index' chain too long; possible loop");
}
Value State::Index(const Value& obj, const Value& key) { return IndexH(obj, key, ""); }

void State::SetIndexH(const Value& obj0, const Value& key, const Value& v, const std::string& hint) {
    Value obj = obj0;
    for (int loop = 0; loop < 100; ++loop) {
        Value h;
        if (obj.t == T::Table) {
            auto* t = static_cast<Table*>(obj.o);
            Table* m = t->meta;
            if (!m || !t->Get(key).IsNil() || (h = m->GetStr("__newindex")).IsNil()) {
                if (key.IsNil()) Error("table index is nil");
                if (key.t == T::Num && std::isnan(key.n)) Error("table index is NaN");
                t->Set(*this, key, v);
                return;
            }
        } else {
            h = GetMeta(obj, "__newindex");
            if (h.IsNil()) Error(std::string("attempt to index a ") + TypeName(obj) + " value" + (loop == 0 ? WithHint(hint) : std::string()));
        }
        if (h.t == T::Func || h.t == T::Native) { Call(h, {obj, key, v}, 0); return; }
        obj = h;
    }
    Error("'__newindex' chain too long; possible loop");
}
void State::SetIndex(const Value& obj, const Value& key, const Value& v) { SetIndexH(obj, key, v, ""); }

static const char* const kArithEvents[] = {"__add", "__sub", "__mul", "__div", "__mod", "__idiv", "__pow", "__unm", "", "", "__band", "__bor", "__bxor", "__shl", "__shr", "__bnot"};

Value State::Arith(int op, const Value& a, const Value& b, const std::string& hint) {
    bool bitwise = (op == OP_BAND || op == OP_BOR || op == OP_BXOR || op == OP_SHL || op == OP_SHR || op == OP_BNOT);
    Value x, y;
    if (ToNumber(a, x) && ToNumber(b, y)) {
        if (bitwise) {
            std::int64_t i, j;
            if (!ToInteger(x, i) || !ToInteger(y, j)) Error("number has no integer representation");
            std::uint64_t ui = (std::uint64_t)i;
            switch (op) {
                case OP_BAND: return Value::Int(i & j);
                case OP_BOR: return Value::Int(i | j);
                case OP_BXOR: return Value::Int(i ^ j);
                case OP_BNOT: return Value::Int(~i);
                case OP_SHL: case OP_SHR: {
                    std::int64_t n = op == OP_SHL ? j : (j == INT64_MIN ? INT64_MAX : -j);
                    if (n <= -64 || n >= 64) return Value::Int(0);
                    return Value::Int((std::int64_t)(n >= 0 ? ui << n : ui >> (-n)));
                }
            }
        }
        if (x.t == T::Int && y.t == T::Int) {
            std::int64_t i = x.i, j = y.i;
            std::uint64_t ui = (std::uint64_t)i, uj = (std::uint64_t)j;
            switch (op) {
                case OP_ADD: return Value::Int((std::int64_t)(ui + uj));
                case OP_SUB: return Value::Int((std::int64_t)(ui - uj));
                case OP_MUL: return Value::Int((std::int64_t)(ui * uj));
                case OP_UNM: return Value::Int((std::int64_t)(0 - ui));
                case OP_IDIV: {
                    if (j == 0) Error("attempt to perform 'n//0'");
                    if (j == -1) return Value::Int((std::int64_t)(0 - ui));
                    std::int64_t q = i / j;
                    if ((i % j != 0) && ((i ^ j) < 0)) --q;
                    return Value::Int(q);
                }
                case OP_MOD: {
                    if (j == 0) Error("attempt to perform 'n%%0'");
                    if (j == -1) return Value::Int(0);
                    std::int64_t r = i % j;
                    if (r != 0 && (r ^ j) < 0) r += j;
                    return Value::Int(r);
                }
                default: break;
            }
        }
        double p = x.t == T::Int ? (double)x.i : x.n, q = y.t == T::Int ? (double)y.i : y.n;
        switch (op) {
            case OP_ADD: return Value::Num(p + q);
            case OP_SUB: return Value::Num(p - q);
            case OP_MUL: return Value::Num(p * q);
            case OP_DIV: return Value::Num(p / q);
            case OP_POW: return Value::Num(std::pow(p, q));
            case OP_UNM: return Value::Num(-p);
            case OP_IDIV: return Value::Num(std::floor(p / q));
            case OP_MOD: {
                double m = std::fmod(p, q);
                if ((m > 0) ? q < 0 : (m < 0 && q != m)) m += q;
                return Value::Num(m);
            }
            default: break;
        }
    }
    const char* ev = kArithEvents[op - OP_ADD];
    Value h = GetMeta(a, ev);
    if (h.IsNil()) h = GetMeta(b, ev);
    if (!h.IsNil()) { auto r = Call(h, {a, b}, 1); return r.empty() ? Value() : r[0]; }
    bool leftBad = !ToNumber(a, x);
    const Value& bad = leftBad ? a : b;
    std::string hh = WithHint(HintPart(hint, leftBad ? 0 : 1));
    if (bitwise) {
        if (bad.IsNumber() || (bad.t == T::Str)) Error("number has no integer representation");
        Error(std::string("attempt to perform bitwise operation on a ") + TypeName(bad) + " value" + hh);
    }
    Error(std::string("attempt to perform arithmetic on a ") + TypeName(bad) + " value" + hh);
}

bool State::Less(const Value& a, const Value& b, bool orEq) {
    if (a.IsNumber() && b.IsNumber()) {
        if (a.t == T::Int && b.t == T::Int) return orEq ? a.i <= b.i : a.i < b.i;
        constexpr double k63 = 9223372036854775808.0;
        if (a.t == T::Int) {          // integer against float, exactly
            const std::int64_t i = a.i; const double d = b.n;
            if (std::isnan(d)) return false;
            if (!orEq) { if (d >= k63) return true; return d > -k63 ? i < (std::int64_t)std::ceil(d) : false; }
            if (d >= k63) return true; return d >= -k63 ? i <= (std::int64_t)std::floor(d) : false;
        }
        if (b.t == T::Int) {          // float against integer
            const double d = a.n; const std::int64_t i = b.i;
            if (std::isnan(d)) return false;
            if (!orEq) { if (d >= k63) return false; return d >= -k63 ? (std::int64_t)std::floor(d) < i : true; }
            if (d >= k63) return false; return d >= -k63 ? (std::int64_t)std::ceil(d) <= i : true;
        }
        return orEq ? a.n <= b.n : a.n < b.n;
    }
    if (a.t == T::Str && b.t == T::Str) {
        int c = static_cast<Str*>(a.o)->s.compare(static_cast<Str*>(b.o)->s);
        return orEq ? c <= 0 : c < 0;
    }
    const char* ev = orEq ? "__le" : "__lt";
    Value h = GetMeta(a, ev);
    if (h.IsNil()) h = GetMeta(b, ev);
    if (!h.IsNil()) { auto r = Call(h, {a, b}, 1); return !r.empty() && r[0].Truthy(); }
    const char* ta = TypeName(a); const char* tb = TypeName(b);
    if (std::strcmp(ta, tb) == 0) Error(std::string("attempt to compare two ") + ta + " values");
    Error(std::string("attempt to compare ") + ta + " with " + tb);
}

Value State::LenOf(const Value& v, const std::string& hint) {
    if (v.t == T::Str) return Value::Int((std::int64_t)static_cast<Str*>(v.o)->s.size());
    Value h = GetMeta(v, "__len");
    if (!h.IsNil()) { auto r = Call(h, {v}, 1); return r.empty() ? Value() : r[0]; }
    if (v.t == T::Table) return Value::Int(static_cast<Table*>(v.o)->Length());
    Error(std::string("attempt to get length of a ") + TypeName(v) + " value" + WithHint(hint));
}

Value State::Concat(std::vector<Value>& parts, const std::string& hint) {
    bool simple = true;
    std::size_t total = 0;
    for (auto& p : parts) {
        if (p.t == T::Str) total += static_cast<Str*>(p.o)->s.size();
        else if (p.IsNumber()) total += 24;
        else { simple = false; break; }
    }
    if (simple) {
        Reserve(total);
        std::string out; out.reserve(total);
        for (auto& p : parts) out += p.t == T::Str ? static_cast<Str*>(p.o)->s : FormatNumber(p);
        return NewStr(out);
    }
    // right to left, pairwise
    Value acc = parts.back();
    for (std::size_t i = parts.size() - 1; i-- > 0;) {
        const Value& l = parts[i];
        if ((l.t == T::Str || l.IsNumber()) && (acc.t == T::Str || acc.IsNumber()))
            acc = NewStr(RawToString(l) + RawToString(acc));
        else {
            Value h = GetMeta(l, "__concat");
            if (h.IsNil()) h = GetMeta(acc, "__concat");
            if (h.IsNil()) {
                bool leftBad = !(l.t == T::Str || l.IsNumber());
                Error(std::string("attempt to concatenate a ") + TypeName(leftBad ? l : acc) + " value" +
                      WithHint(i == 0 && parts.size() == 2 ? HintPart(hint, leftBad ? 0 : 1) : std::string()));
            }
            auto r = Call(h, {l, acc}, 1);
            acc = r.empty() ? Value() : r[0];
        }
    }
    return acc;
}

std::string State::Hint(const Frame& f, int pc) const {
    if (pc < 0 || pc >= (int)f.cl->p->hints.size()) return "";
    return f.cl->p->hints[pc];
}

// ============================================================ calls
static void Place(State& L, Coro* co, int fidx, int want, const std::vector<Value>& vals) {
    auto& st = co->st;
    st.resize(fidx);
    for (auto& v : vals) st.push_back(v);
    if (want < 0) co->multi = (int)vals.size();
    else st.resize(fidx + want);   // pads with nil / truncates
    (void)L;
}

void State::Finish(Coro* co, int fidx, int want, std::vector<Value>& rets, bool prefixTrue) {
    if (prefixTrue) rets.insert(rets.begin(), Value::Bool(true));
    Place(*this, co, fidx, want, rets);
}

bool State::PreCall(Coro* co, int fidx, int nargs, int want, std::uint8_t prot, const Value& handler) {
    if (!prot) return PreCallCore(co, fidx, nargs, want, 0, handler);
    const std::size_t fsz = co->frames.size();
    try { return PreCallCore(co, fidx, nargs, want, prot, handler); }
    catch (LuaError& e) {
        if (e.fatal) throw;
        std::vector<Value> pin{e.v};
        pins.push_back(&pin);
        std::vector<Value> rets{Value::Bool(false), e.v};
        if (prot == 2) {
            try { auto r = Call(handler, {e.v}, 1); rets[1] = r.empty() ? Value() : r[0]; }
            catch (LuaError& e2) { if (e2.fatal) { pins.pop_back(); throw; } rets[1] = e2.v; }
        }
        pins.pop_back();
        co->frames.resize(fsz);
        Place(*this, co, fidx, want, rets);
        return false;
    }
}

bool State::PreCallCore(Coro* co, int fidx, int nargs, int want, std::uint8_t prot, const Value& handler) {
    for (int guard = 0; guard < 100; ++guard) {
        Value f = co->st[fidx];
        if (f.t == T::Func) {
            auto* cl = static_cast<Closure*>(f.o);
            Proto* p = cl->p;
            if ((int)co->frames.size() >= limits.maxFrames || co->st.size() > 1'000'000)
                Error("stack overflow (too many nested calls)");
            Frame fr;
            fr.cl = cl; fr.pc = 0; fr.base = fidx + 1; fr.wantRet = want; fr.fidx = fidx; fr.prot = prot; fr.handler = handler;
            if (nargs > p->nparams && p->vararg)
                fr.varargs.assign(co->st.begin() + fidx + 1 + p->nparams, co->st.begin() + fidx + 1 + nargs);
            co->st.resize(fidx + 1 + std::min(nargs, p->nparams));
            co->st.resize(fidx + 1 + p->nlocals);
            co->frames.push_back(std::move(fr));
            return true;
        }
        if (f.t == T::Native) {
            auto* nat = static_cast<Native*>(f.o);
            switch (nat->special) {
                case Special::PCall:
                    if (prot) break;   // pcall called through another pcall: plain native below
                    if (nargs < 1) Error("bad argument #1 to 'pcall' (value expected)");
                    co->st.erase(co->st.begin() + fidx);
                    return PreCall(co, fidx, nargs - 1, want, 1, Value());
                case Special::XPCall: {
                    if (prot) break;
                    if (nargs < 2) Error("bad argument #2 to 'xpcall' (value expected)");
                    Value h = co->st[fidx + 2];
                    co->st.erase(co->st.begin() + fidx + 2);
                    co->st.erase(co->st.begin() + fidx);
                    return PreCall(co, fidx, nargs - 2, want, 2, h);
                }
                case Special::Yield: {
                    if (co->isMain) Error("attempt to yield from outside a coroutine");
                    if (co->nativeNest > 0) Error("attempt to yield across a native call boundary");
                    co->transfer.assign(co->st.begin() + fidx + 1, co->st.begin() + fidx + 1 + nargs);
                    co->yielded = true; co->yieldIdx = fidx; co->yieldWant = want;
                    return false;
                }
                default: break;
            }
            std::vector<Value> args(co->st.begin() + fidx + 1, co->st.begin() + fidx + 1 + nargs), rets;
            struct DN { State& s; bool saved; ~DN() { s.directNative = saved; } } dn{*this, directNative};
            directNative = directNative || prot != 0;
            nat->fn(*this, args, rets);
            Finish(co, fidx, want, rets, prot != 0);
            return false;
        }
        Value h = GetMeta(f, "__call");
        if (h.IsNil()) {
            if (prot) ErrorValue(NewStr(std::string("attempt to call a ") + TypeName(f) + " value"));
            std::string hint = co->frames.empty() ? "" : Hint(co->frames.back(), co->frames.back().pc - 1);
            Error(std::string("attempt to call a ") + TypeName(f) + " value" + WithHint(hint));
        }
        co->st.insert(co->st.begin() + fidx, h);
        ++nargs;
    }
    Error("'__call' chain too long");
}

std::vector<Value> State::Call(const Value& f, std::vector<Value> args, int nret) {
    Coro* co = cur;
    if (!(f.t == T::Func || f.t == T::Native) && GetMeta(f, "__call").IsNil())
        Error(std::string("attempt to call a ") + TypeName(f) + " value");
    if (cDepth >= 190) Error("stack overflow (native calls nested too deeply)");
    int fidx = (int)co->st.size();
    co->st.push_back(f);
    for (auto& a : args) co->st.push_back(a);
    std::size_t depth = co->frames.size();
    ++co->nativeNest; ++cDepth;
    struct G { Coro* c; State* s; bool dn; ~G() { --c->nativeNest; --s->cDepth; s->directNative = dn; } } g{co, this, directNative};
    directNative = (f.t == T::Native);
    if (PreCall(co, fidx, (int)args.size(), nret, 0, Value())) Run(co, depth);
    std::vector<Value> out(co->st.begin() + fidx, co->st.end());
    co->st.resize(fidx);
    return out;
}

bool State::CallProtected(const Value& f, std::vector<Value> args, std::vector<Value>* rets, std::string* err) {
    if (cDepth == 0) ResetBudget();
    Coro* co = cur;
    std::size_t fsz = co->frames.size(), ssz = co->st.size();
    try {
        auto r = Call(f, std::move(args), -1);
        if (rets) *rets = std::move(r);
        return true;
    } catch (LuaError& e) {
        co->frames.resize(fsz); co->st.resize(ssz);
        if (err) *err = e.text;
    } catch (std::bad_alloc&) {
        co->frames.resize(fsz); co->st.resize(ssz);
        if (err) *err = "not enough memory";
    }
    return false;
}

bool State::DoString(const std::string& src, const std::string& chunk, std::string* err) {
    Proto* p = Compile(src, chunk, err);
    if (!p) return false;
    return CallProtected(MakeFunction(p), {}, nullptr, err);
}

void State::CheckBudget() {
    if (budget <= 0) ErrorFatal("script used too many instructions (limit " + std::to_string(limits.instructions) + "); possible endless loop");
    if (deadline > 0 && NowSeconds() > deadline)
        ErrorFatal("script ran too long (limit " + std::to_string(limits.seconds) + " seconds); possible endless loop");
}

// ============================================================ coroutines
bool State::Resume(Coro* co, std::vector<Value> args, std::vector<Value>* results, std::string* err) {
    if (co->status == CoStatus::Dead) { *err = "cannot resume dead coroutine"; return false; }
    if (co->status != CoStatus::Fresh && co->status != CoStatus::Suspended) { *err = "cannot resume non-suspended coroutine"; return false; }
    if (cDepth >= 190) { *err = "C stack overflow"; return false; }
    Coro* prev = cur;
    bool fresh = co->status == CoStatus::Fresh;
    co->resumer = prev; prev->status = CoStatus::Normal; co->status = CoStatus::Running; cur = co; ++cDepth;
    struct G { State* s; Coro* prev; Coro* co; ~G() { s->cur = prev; prev->status = CoStatus::Running; --s->cDepth; co->resumer = nullptr; } } g{this, prev, co};
    try {
        if (fresh) {
            co->st.clear();
            co->st.push_back(co->startFn);
            for (auto& a : args) co->st.push_back(a);
            if (PreCall(co, 0, (int)args.size(), -1, 0, Value())) Run(co, 0);
        } else {
            co->yielded = false;
            Finish(co, co->yieldIdx, co->yieldWant, args, false);
            Run(co, 0);
        }
        if (co->yielded) { co->status = CoStatus::Suspended; *results = std::move(co->transfer); co->transfer.clear(); }
        else { co->status = CoStatus::Dead; *results = co->st; co->st.clear(); co->frames.clear(); }
        return true;
    } catch (LuaError& e) {
        co->status = CoStatus::Dead;
        co->st.clear(); co->frames.clear();
        if (e.fatal) throw;
        *err = e.text;
        results->assign(1, e.v);
        return false;
    }
}

// ============================================================ the run loop
void State::Run(Coro* co, std::size_t stopFrames) {
    for (;;) {
        try { RunInner(co, stopFrames); return; }
        catch (LuaError& e) {
            if (e.fatal) throw;
            int pf = -1;
            for (int i = (int)co->frames.size() - 1; i >= (int)stopFrames; --i) if (co->frames[i].prot) { pf = i; break; }
            if (pf < 0) throw;
            Frame f = std::move(co->frames[pf]);
            co->frames.resize(pf);
            co->st.resize(f.fidx);
            std::vector<Value> pin{e.v};
            pins.push_back(&pin);
            std::vector<Value> rets{Value::Bool(false), e.v};
            if (f.prot == 2) {
                try { auto r = Call(f.handler, {e.v}, 1); rets[1] = r.empty() ? Value() : r[0]; }
                catch (LuaError& e2) { if (e2.fatal) { pins.pop_back(); throw; } rets[1] = e2.v; }
            }
            pins.pop_back();
            Place(*this, co, f.fidx, f.wantRet, rets);
        }
    }
}

#define POP() (co->st.pop_back())
#define TOP() (co->st.back())

void State::RunInner(Coro* co, std::size_t stopFrames) {
    auto& st = co->st;
    std::int64_t tick = 0;
    while (co->frames.size() > stopFrames) {
        if (gcPending) Collect();
        Frame* fr = &co->frames.back();
        const Proto* P = fr->cl->p;
        const Instr& in = P->code[fr->pc++];
        const int base = fr->base;
        if (--budget <= 0 || ((++tick & 2047) == 0 && deadline > 0)) CheckBudget();
        switch ((Op)in.op) {
            case OP_NIL: st.emplace_back(); break;
            case OP_TRUE: st.push_back(Value::Bool(true)); break;
            case OP_FALSE: st.push_back(Value::Bool(false)); break;
            case OP_K: st.push_back(P->k[in.a]); break;
            case OP_POP: POP(); break;
            case OP_DUP: st.push_back(st.back()); break;
            case OP_GETLOCAL: st.push_back(st[base + in.a]); break;
            case OP_SETLOCAL: st[base + in.a] = TOP(); POP(); break;
            case OP_GETCELL: st.push_back(static_cast<Cell*>(st[base + in.a].o)->v); break;
            case OP_SETCELL: static_cast<Cell*>(st[base + in.a].o)->v = TOP(); POP(); break;
            case OP_NEWCELL: {
                auto* c = new Cell(); Track(*this, c, 32); c->v = TOP(); POP();
                st[base + in.a] = Value::Of(c); break;
            }
            case OP_CELLIFY: {
                auto* c = new Cell(); Track(*this, c, 32); c->v = st[base + in.a];
                st[base + in.a] = Value::Of(c); break;
            }
            case OP_GETUP: st.push_back(fr->cl->ups[in.a]->v); break;
            case OP_SETUP: fr->cl->ups[in.a]->v = TOP(); POP(); break;
            case OP_GETGLOBAL: {
                Value k = P->k[in.a];
                Value v = globals->Get(k);
                if (v.IsNil() && globals->meta) v = Index(Value::Of(globals), k);
                st.push_back(v); break;
            }
            case OP_SETGLOBAL: {
                Value k = P->k[in.a]; Value v = TOP();
                if (globals->meta) SetIndex(Value::Of(globals), k, v); else globals->Set(*this, k, v);
                POP(); break;
            }
            case OP_GETTABLE: {
                Value k = st[st.size() - 1], o = st[st.size() - 2];
                Value r;
                if (o.t == T::Table && !static_cast<Table*>(o.o)->meta) { r = static_cast<Table*>(o.o)->Get(k); }
                else r = IndexH(o, k, P->hints[fr->pc - 1]);
                st.resize(st.size() - 2); st.push_back(r); break;
            }
            case OP_GETFIELD: {
                Value o = TOP(); Value r;
                if (o.t == T::Table && !static_cast<Table*>(o.o)->meta) r = static_cast<Table*>(o.o)->Get(P->k[in.a]);
                else r = IndexH(o, P->k[in.a], P->hints[fr->pc - 1]);
                POP(); st.push_back(r); break;
            }
            case OP_SELF: {
                Value o = TOP();
                Value f = IndexH(o, P->k[in.a], P->hints[fr->pc - 1]);
                st.back() = f; st.push_back(o); break;
            }
            case OP_SETTABLE: {   // stack: v, t, k
                Value k = st[st.size() - 1], o = st[st.size() - 2], v = st[st.size() - 3];
                if (gcPending) { /* collected at the next instruction */ }
                SetIndexH(o, k, v, P->hints[fr->pc - 1]);
                st.resize(st.size() - 3); break;
            }
            case OP_SETFIELD: {   // stack: v, t
                Value o = st[st.size() - 1], v = st[st.size() - 2];
                SetIndexH(o, P->k[in.a], v, P->hints[fr->pc - 1]);
                st.resize(st.size() - 2); break;
            }
            case OP_INITKV: {     // stack: T, k, v
                Value v = st[st.size() - 1], k = st[st.size() - 2];
                if (k.IsNil()) Error("table index is nil");
                if (k.t == T::Num && std::isnan(k.n)) Error("table index is NaN");
                static_cast<Table*>(st[st.size() - 3].o)->Set(*this, k, v);
                st.resize(st.size() - 2); break;
            }
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: case OP_IDIV: case OP_POW:
            case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SHR: {
                Value a = st[st.size() - 2], b = st[st.size() - 1];
                Value r;
                if (in.op == OP_ADD && a.t == T::Int && b.t == T::Int) r = Value::Int((std::int64_t)((std::uint64_t)a.i + (std::uint64_t)b.i));
                else if (in.op == OP_ADD && a.t == T::Num && b.t == T::Num) r = Value::Num(a.n + b.n);
                else if (in.op == OP_SUB && a.t == T::Num && b.t == T::Num) r = Value::Num(a.n - b.n);
                else if (in.op == OP_MUL && a.t == T::Num && b.t == T::Num) r = Value::Num(a.n * b.n);
                else r = Arith(in.op, a, b, P->hints[fr->pc - 1]);
                st.resize(st.size() - 2); st.push_back(r); break;
            }
            case OP_UNM: { Value a = TOP(); Value r = Arith(OP_UNM, a, a, P->hints[fr->pc - 1]); st.back() = r; break; }
            case OP_BNOT: { Value a = TOP(); Value r = Arith(OP_BNOT, a, a, P->hints[fr->pc - 1]); st.back() = r; break; }
            case OP_NOT: st.back() = Value::Bool(!TOP().Truthy()); break;
            case OP_LEN: { Value a = TOP(); Value r = LenOf(a, P->hints[fr->pc - 1]); st.back() = r; break; }
            case OP_CONCAT: {
                std::vector<Value> parts(st.end() - in.a, st.end());
                Value r = Concat(parts, P->hints[fr->pc - 1]);
                st.resize(st.size() - in.a); st.push_back(r); break;
            }
            case OP_EQ: case OP_NE: {
                Value a = st[st.size() - 2], b = st[st.size() - 1];
                bool r = Equals(a, b);
                st.resize(st.size() - 2); st.push_back(Value::Bool(in.op == OP_EQ ? r : !r)); break;
            }
            case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
                Value a = st[st.size() - 2], b = st[st.size() - 1];
                bool r;
                switch ((Op)in.op) {
                    case OP_LT: r = Less(a, b, false); break;
                    case OP_LE: r = Less(a, b, true); break;
                    case OP_GT: r = Less(b, a, false); break;
                    default: r = Less(b, a, true); break;
                }
                st.resize(st.size() - 2); st.push_back(Value::Bool(r)); break;
            }
            case OP_JMP: fr->pc = in.a; break;
            case OP_JMPF: { bool t = TOP().Truthy(); POP(); if (!t) fr->pc = in.a; break; }
            case OP_JMPFK: if (!TOP().Truthy()) fr->pc = in.a; else POP(); break;
            case OP_JMPTK: if (TOP().Truthy()) fr->pc = in.a; else POP(); break;
            case OP_GFORTEST:
                if (TOP().IsNil()) { POP(); fr->pc = in.a; }
                else st[base + in.b] = TOP();
                break;
            case OP_CALL: {
                int n = in.a + (in.c ? co->multi : 0);
                int fidx = (int)st.size() - n - 1;
                PreCall(co, fidx, n, in.b, 0, Value());
                if (co->yielded) return;
                break;
            }
            case OP_RETURN: {
                int n = in.a + (in.c ? co->multi : 0);
                Frame f = std::move(co->frames.back());
                co->frames.pop_back();
                int src = (int)st.size() - n, dst = f.fidx;
                int count = n;
                if (f.prot) { st[dst++] = Value::Bool(true); ++count; }
                for (int i = 0; i < n; ++i) st[dst + i] = st[src + i];
                int start = f.fidx;
                st.resize(start + count);
                if (f.wantRet < 0) co->multi = count;
                else st.resize(start + f.wantRet);    // pads with nil or truncates
                break;
            }
            case OP_CLOSURE: {
                Proto* np = P->protos[in.a];
                Value cv = MakeFunction(np);
                auto* cl = static_cast<Closure*>(cv.o);
                for (std::size_t i = 0; i < np->ups.size(); ++i) {
                    const auto& u = np->ups[i];
                    cl->ups[i] = u.fromLocal ? static_cast<Cell*>(st[base + u.index].o) : fr->cl->ups[u.index];
                }
                st.push_back(cv); break;
            }
            case OP_VARARG: {
                const auto& va = fr->varargs;
                if (in.b < 0) { for (auto& v : va) st.push_back(v); co->multi = (int)va.size(); }
                else for (int i = 0; i < in.b; ++i) st.push_back(i < (int)va.size() ? va[i] : Value());
                break;
            }
            case OP_NEWTABLE: {
                Table* t = NewTable();
                if (in.a > 0) t->arr.reserve((std::size_t)in.a);
                st.push_back(Value::Of(t)); break;
            }
            case OP_SETLIST: {
                int n = in.a + (in.c ? co->multi : 0);
                auto* t = static_cast<Table*>(st[st.size() - n - 1].o);
                for (int i = 0; i < n; ++i) t->Set(*this, Value::Int(in.b + i), st[st.size() - n + i]);
                st.resize(st.size() - n); break;
            }
            case OP_FORPREP: {
                int s0 = base + in.b;
                Value a = st[s0], lim = st[s0 + 1], step = st[s0 + 2];
                if (!a.IsNumber()) Error("'for' initial value must be a number");
                if (!lim.IsNumber()) Error("'for' limit must be a number");
                if (!step.IsNumber()) Error("'for' step must be a number");
                if (a.t == T::Int && step.t == T::Int) {
                    if (step.i == 0) Error("'for' step is zero");
                    std::int64_t limI;
                    bool skip = false;
                    if (lim.t == T::Int) limI = lim.i;
                    else {
                        double l = lim.n;
                        if (std::isnan(l)) { skip = true; limI = 0; }
                        else if (l >= 9223372036854775808.0) { if (step.i < 0) skip = true; limI = INT64_MAX; }
                        else if (l < -9223372036854775808.0) { if (step.i > 0) skip = true; limI = INT64_MIN; }
                        else limI = (std::int64_t)(step.i > 0 ? std::floor(l) : std::ceil(l));
                    }
                    if (!skip) skip = step.i > 0 ? a.i > limI : a.i < limI;
                    if (skip) { fr->pc = in.a; break; }
                    std::uint64_t count;
                    if (step.i > 0) count = ((std::uint64_t)limI - (std::uint64_t)a.i) / (std::uint64_t)step.i;
                    else count = ((std::uint64_t)a.i - (std::uint64_t)limI) / ((std::uint64_t)(-(step.i + 1)) + 1u);
                    st[s0 + 1] = Value::Int((std::int64_t)count);
                    st[s0 + 3] = a;
                } else {
                    double p = a.t == T::Int ? (double)a.i : a.n, l = lim.t == T::Int ? (double)lim.i : lim.n,
                           s = step.t == T::Int ? (double)step.i : step.n;
                    if (s == 0) Error("'for' step is zero");
                    if (s > 0 ? !(p <= l) : !(p >= l)) { fr->pc = in.a; break; }
                    st[s0] = Value::Num(p); st[s0 + 1] = Value::Num(l); st[s0 + 2] = Value::Num(s); st[s0 + 3] = Value::Num(p);
                }
                break;
            }
            case OP_FORLOOP: {
                int s0 = base + in.b;
                if (st[s0].t == T::Int) {
                    std::uint64_t count = (std::uint64_t)st[s0 + 1].i;
                    if (count > 0) {
                        st[s0 + 1].i = (std::int64_t)(count - 1);
                        st[s0].i = (std::int64_t)((std::uint64_t)st[s0].i + (std::uint64_t)st[s0 + 2].i);
                        st[s0 + 3] = st[s0];
                        fr->pc = in.a;
                    }
                } else {
                    double idx = st[s0].n + st[s0 + 2].n;
                    if (st[s0 + 2].n > 0 ? idx <= st[s0 + 1].n : idx >= st[s0 + 1].n) {
                        st[s0].n = idx; st[s0 + 3] = Value::Num(idx); fr->pc = in.a;
                    }
                }
                break;
            }
        }
    }
}

}  // namespace okay::lua
