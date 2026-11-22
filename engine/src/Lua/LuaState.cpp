// okay::lua runtime: values, tables, the collector, the bytecode loop, calls,
// errors and coroutines.
//
// Semantics come from the Lua 5.4 Reference Manual: 2.1 (values and types,
// integer and float subtypes), 2.4 (metatables and metamethods), 2.5 (garbage
// collection: we implement a stop-the-world mark and sweep; __gc and weak
// tables are not supported), 2.6 (coroutines), 3.3.5 (for statements), 3.3.8
// (to-be-closed variables), 3.4.1-3.4.4 (arithmetic, bitwise, coercions and
// comparisons) and 3.4.7 (the length operator). The implementation techniques
// (stack frames in one value array, open addressing hash tables, explicit
// gray stack marking) are textbook ones; nothing was taken from other
// interpreters.
#include "LuaCode.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>

namespace okay::lua {

const char* const kTMNames[TM_COUNT] = {
    "__index", "__newindex", "__call", "__add", "__sub", "__mul", "__mod", "__pow", "__div", "__idiv",
    "__band", "__bor", "__bxor", "__shl", "__shr", "__unm", "__bnot", "__len", "__concat", "__eq",
    "__lt", "__le", "__close", "__tostring", "__name", "__metatable", "__pairs", "__mode", "__gc"};

namespace {
constexpr int kMaxStack = 1'000'000;
constexpr std::size_t kMaxFrames = 200'000;
constexpr double kTwo63 = 9223372036854775808.0;

std::uint64_t HashInt(std::uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return x;
}

struct DepthGuard {
    int& d;
    explicit DepthGuard(int& x) : d(x) { ++d; }
    ~DepthGuard() { --d; }
};
} // namespace

std::uint32_t HashBytes(const char* s, std::size_t n) {
    // FNV-1a (a public, well-known hash function)
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < n; ++i) { h ^= (unsigned char)s[i]; h *= 16777619u; }
    return h;
}

// ------------------------------------------------------------------ values
bool RawEquals(const Value& a, const Value& b) {
    if (a.t != b.t) {
        if (a.t == Type::Int && b.t == Type::Float) {
            double f = b.n;
            return f == std::floor(f) && f >= -kTwo63 && f < kTwo63 && (std::int64_t)f == a.i;
        }
        if (a.t == Type::Float && b.t == Type::Int) return RawEquals(b, a);
        return false;
    }
    switch (a.t) {
        case Type::Nil: return true;
        case Type::Bool: return a.b == b.b;
        case Type::Int: return a.i == b.i;
        case Type::Float: return a.n == b.n;
        case Type::String: {
            if (a.o == b.o) return true;
            const LString* x = a.Str();
            const LString* y = b.Str();
            return x->hash == y->hash && x->s == y->s;
        }
        default: return a.o == b.o;
    }
}

namespace {
// a float key with an integer value is the same key as that integer
Value NormalizeKey(const Value& k) {
    if (k.t == Type::Float) {
        double f = k.n;
        if (f == std::floor(f) && f >= -kTwo63 && f < kTwo63) return Value::Int((std::int64_t)f);
    }
    return k;
}
std::uint64_t HashValue(const Value& k) {
    switch (k.t) {
        case Type::Bool: return k.b ? 1 : 2;
        case Type::Int: return HashInt((std::uint64_t)k.i);
        case Type::Float: { std::uint64_t u; std::memcpy(&u, &k.n, 8); return HashInt(u); }
        case Type::String: return HashInt(k.Str()->hash);
        default: return HashInt((std::uint64_t)(std::uintptr_t)k.o);
    }
}
} // namespace

// ------------------------------------------------------------------ tables
const Table::Node* Table::FindNode(const Value& k) const {
    if (node.empty()) return nullptr;
    std::size_t mask = node.size() - 1;
    std::size_t i = (std::size_t)HashValue(k) & mask;
    for (std::size_t n = 0; n < node.size(); ++n) {
        const Node& nd = node[i];
        if (nd.k.IsNil()) return nullptr;
        if (RawEquals(nd.k, k) && nd.k.t == k.t) return &nd;
        i = (i + 1) & mask;
    }
    return nullptr;
}

Value Table::GetInt(std::int64_t k) const {
    if (k >= 1 && (std::uint64_t)k <= arr.size()) return arr[(std::size_t)(k - 1)];
    const Node* n = FindNode(Value::Int(k));
    return n ? n->v : Value();
}

Value Table::GetStr(const LString* k) const {
    if (node.empty()) return Value();
    std::size_t mask = node.size() - 1;
    std::size_t i = (std::size_t)HashInt(k->hash) & mask;
    for (std::size_t n = 0; n < node.size(); ++n) {
        const Node& nd = node[i];
        if (nd.k.IsNil()) return Value();
        if (nd.k.t == Type::String && (nd.k.o == k || (nd.k.Str()->hash == k->hash && nd.k.Str()->s == k->s))) return nd.v;
        i = (i + 1) & mask;
    }
    return Value();
}

Value Table::Get(const Value& key) const {
    switch (key.t) {
        case Type::Nil: return Value();
        case Type::Int: return GetInt(key.i);
        case Type::String: return GetStr(key.Str());
        case Type::Float: {
            Value k = NormalizeKey(key);
            if (k.t == Type::Int) return GetInt(k.i);
            if (std::isnan(k.n)) return Value();
            const Node* n = FindNode(k);
            return n ? n->v : Value();
        }
        default: {
            const Node* n = FindNode(key);
            return n ? n->v : Value();
        }
    }
}

std::size_t Table::MemoryBytes() const {
    return sizeof(Table) + arr.capacity() * sizeof(Value) + node.size() * sizeof(Node);
}

void Table::Rehash(State& L, std::size_t extra) {
    std::size_t live = 0;
    for (const Node& n : node)
        if (!n.k.IsNil() && !n.v.IsNil()) ++live;
    std::size_t want = (live + extra) * 2;
    std::size_t cap = 4;
    while (cap < want) cap <<= 1;
    if (cap > (std::size_t(1) << 26)) L.Error("table overflow");
    std::vector<Node> old;
    old.swap(node);
    node.assign(cap, Node{});
    used = 0;
    std::size_t mask = cap - 1;
    for (const Node& n : old) {
        if (n.k.IsNil() || n.v.IsNil()) continue;
        std::size_t i = (std::size_t)HashValue(n.k) & mask;
        while (!node[i].k.IsNil()) i = (i + 1) & mask;
        node[i] = n;
        ++used;
    }
    L.Regrow(this, MemoryBytes());
}

void Table::MigrateFromHash() {
    if (node.empty()) return;
    for (;;) {
        Value k = Value::Int((std::int64_t)arr.size() + 1);
        Node* n = const_cast<Node*>(FindNode(k));
        if (!n || n->v.IsNil()) return;
        arr.push_back(n->v);
        n->v = Value();
    }
}

void Table::Set(State& L, const Value& key, const Value& v) {
    Value k = NormalizeKey(key);
    if (k.t == Type::Int) { SetInt(L, k.i, v); return; }
    if (k.IsNil()) L.Error("table index is nil");
    if (k.t == Type::Float && std::isnan(k.n)) L.Error("table index is NaN");
    if (Node* n = const_cast<Node*>(FindNode(k))) { n->v = v; return; }
    if (v.IsNil()) return;
    if ((used + 1) * 4 > node.size() * 3) Rehash(L, 1);
    std::size_t mask = node.size() - 1;
    std::size_t i = (std::size_t)HashValue(k) & mask;
    while (!node[i].k.IsNil()) i = (i + 1) & mask;
    node[i].k = k;
    node[i].v = v;
    ++used;
}

void Table::SetInt(State& L, std::int64_t k, const Value& v) {
    if (k >= 1 && (std::uint64_t)k <= arr.size()) { arr[(std::size_t)(k - 1)] = v; return; }
    if (k >= 1 && (std::uint64_t)k == arr.size() + 1) {
        if (v.IsNil()) {
            if (Node* n = const_cast<Node*>(FindNode(Value::Int(k)))) n->v = v;
            return;
        }
        std::size_t cap = arr.capacity();
        if (Node* n = const_cast<Node*>(FindNode(Value::Int(k)))) n->v = Value();
        arr.push_back(v);
        MigrateFromHash();
        if (arr.capacity() != cap) L.Regrow(this, MemoryBytes());
        return;
    }
    Value key = Value::Int(k);
    if (Node* n = const_cast<Node*>(FindNode(key))) { n->v = v; return; }
    if (v.IsNil()) return;
    if ((used + 1) * 4 > node.size() * 3) Rehash(L, 1);
    std::size_t mask = node.size() - 1;
    std::size_t i = (std::size_t)HashValue(key) & mask;
    while (!node[i].k.IsNil()) i = (i + 1) & mask;
    node[i].k = key;
    node[i].v = v;
    ++used;
}

bool Table::Next(State& L, Value& k, Value& v) const {
    std::size_t i = 0;   // position to look from: array index, then arr.size() + node index
    if (!k.IsNil()) {
        Value key = NormalizeKey(k);
        if (key.t == Type::Int && key.i >= 1 && (std::uint64_t)key.i <= arr.size()) {
            i = (std::size_t)key.i;
        } else {
            const Node* n = FindNode(key);
            if (!n) L.Error("invalid key to 'next'");
            i = arr.size() + (std::size_t)(n - node.data()) + 1;
        }
    }
    for (; i < arr.size(); ++i)
        if (!arr[i].IsNil()) { k = Value::Int((std::int64_t)i + 1); v = arr[i]; return true; }
    for (std::size_t j = i - arr.size(); j < node.size(); ++j)
        if (!node[j].k.IsNil() && !node[j].v.IsNil()) { k = node[j].k; v = node[j].v; return true; }
    return false;
}

std::int64_t Table::Length() const {
    std::size_t n = arr.size();
    if (n > 0 && arr[n - 1].IsNil()) {
        std::size_t lo = 0, hi = n;   // arr[lo-1] is non-nil (or lo == 0), arr[hi-1] is nil
        while (hi - lo > 1) {
            std::size_t m = (lo + hi) / 2;
            if (arr[m - 1].IsNil()) hi = m; else lo = m;
        }
        return (std::int64_t)lo;
    }
    if (node.empty() || GetInt((std::int64_t)n + 1).IsNil()) return (std::int64_t)n;
    // unbounded search in the hash part
    std::uint64_t i = n + 1, j = (n + 1) * 2;
    while (!GetInt((std::int64_t)j).IsNil()) {
        i = j;
        if (j > (std::uint64_t)INT64_MAX / 2) {   // a pathological table: walk linearly
            std::uint64_t x = 1;
            while (!GetInt((std::int64_t)x).IsNil()) ++x;
            return (std::int64_t)x - 1;
        }
        j *= 2;
    }
    while (j - i > 1) {
        std::uint64_t m = (i + j) / 2;
        if (GetInt((std::int64_t)m).IsNil()) j = m; else i = m;
    }
    return (std::int64_t)i;
}

// ------------------------------------------------------------------ state
State::State() {
    main_ = Alloc<Coroutine>();
    main_->status = CoStatus::Running;
    main_->started = true;
    main_->stack.resize(256);
    co_ = main_;
    globals_ = NewTable();
    registry = NewTable();
    for (int i = 0; i < TM_COUNT; ++i) tmName[i] = NewString(kTMNames[i]);
    printSink = [](const std::string& s) { std::fwrite(s.data(), 1, s.size(), stdout); std::fputc('\n', stdout); };
}

State::~State() {
    GcObj* o = gcList_;
    while (o) {
        GcObj* n = o->gcNext;
        delete o;
        o = n;
    }
}

void State::AccountAlloc(std::size_t n) {
    bytes_ += n;
    if (bytes_ > gcThreshold_) gcPending = true;
    if (bytes_ > memoryLimit) {
        gcPending = true;
        // far over the limit with no safe point in between (one huge native): stop now
        if (bytes_ > memoryLimit + memoryLimit / 2) throw LuaFatal{"not enough memory (script memory limit reached)"};
    }
}

LString* State::NewString(const char* s, std::size_t n) {
    if (n > (std::size_t(1) << 30)) Error("string too large");
    LString* str = Alloc<LString>(n);
    str->s.assign(s, n);
    str->hash = HashBytes(s, n);
    return str;
}
LString* State::NewString(const std::string& s) { return NewString(s.data(), s.size()); }

Table* State::NewTable(std::size_t narr, std::size_t nhash) {
    Table* t = Alloc<Table>();
    if (narr) t->arr.reserve(std::min<std::size_t>(narr, 1u << 20));
    if (nhash) {
        std::size_t cap = 4;
        while (cap * 3 < nhash * 4) cap <<= 1;
        if (cap <= (1u << 20)) t->node.assign(cap, Table::Node{});
    }
    if (narr || nhash) Regrow(t, t->MemoryBytes());
    return t;
}

UData* State::NewUData() { return Alloc<UData>(); }

LClosure* State::NewLClosure(Proto* p) {
    LClosure* c = Alloc<LClosure>(p->upvals.size() * sizeof(void*));
    c->p = p;
    c->up.reserve(p->upvals.size());
    return c;
}

NClosure* State::NewNative(NativeFn fn, const char* name, std::vector<Value> up) {
    NClosure* c = Alloc<NClosure>(up.size() * sizeof(Value));
    c->fn = fn;
    c->name = name;
    c->up = std::move(up);
    return c;
}

Coroutine* State::NewCoroutine(const Value& fn) {
    Coroutine* co = Alloc<Coroutine>(64 * sizeof(Value));
    co->stack.resize(64);
    co->fn = fn;
    return co;
}

void State::SetGlobal(const char* name, const Value& v) { globals_->Set(*this, Value::Obj(NewString(name)), v); }
Value State::GetGlobal(const char* name) { return globals_->Get(Value::Obj(NewString(name))); }

void State::Register(Table* t, const char* name, NativeFn fn) {
    t->Set(*this, Value::Obj(NewString(name)), Value::Obj(NewNative(fn, name)));
}

void State::OpenLibs() {
    OpenBase(*this);
    OpenString(*this);
    OpenTable(*this);
    OpenMath(*this);
    OpenCoroutine(*this);
    OpenOs(*this);
    OpenUtf8(*this);
}

// ------------------------------------------------------------------ collector
void State::Mark(GcObj* o, std::vector<GcObj*>& gray) {
    if (!o || o->marked) return;
    o->marked = true;
    if (o->type != Type::String) gray.push_back(o);
}

void State::MarkValue(const Value& v, std::vector<GcObj*>& gray) {
    if (v.IsGc()) Mark(v.o, gray);
}

namespace {
// an object a weak table may drop (strings behave as values)
bool IsWeakRef(const Value& v) { return v.IsGc() && v.t != Type::String; }
}

void State::CollectGarbage() {
    gcPending = false;
    std::vector<GcObj*> gray;
    weakTables.clear();
    Mark(globals_, gray);
    Mark(registry, gray);
    Mark(stringMeta, gray);
    Mark(main_, gray);
    Mark(co_, gray);
    for (Coroutine* c : coStack_) Mark(c, gray);
    for (LString* s : tmName) Mark(s, gray);
    MarkValue(errorValue, gray);
    for (auto* v : tempRoots)
        for (const Value& x : *v) MarkValue(x, gray);
    for (GcObj* o : toFinalize_) Mark(o, gray);   // finalizers not run yet
    auto propagate = [&]() {
        while (!gray.empty()) {
            GcObj* o = gray.back();
            gray.pop_back();
            switch (o->type) {
                case Type::Table: {
                    Table* t = static_cast<Table*>(o);
                    Mark(t->meta, gray);
                    // weak tables (manual 2.5.4): a weak part does not keep its objects alive.
                    // Strings count as values here (never removed). Weak keys are not full
                    // ephemerons: a value that refers to its own key keeps the entry.
                    bool weakK = false, weakV = false;
                    if (t->meta) {
                        Value mode = t->meta->GetStr(tmName[TM_MODE]);
                        if (mode.IsString()) {
                            weakK = mode.Str()->s.find('k') != std::string::npos;
                            weakV = mode.Str()->s.find('v') != std::string::npos;
                        }
                    }
                    if (weakK || weakV) weakTables.push_back(t);
                    for (const Value& v : t->arr) if (!weakV || !IsWeakRef(v)) MarkValue(v, gray);
                    for (const Table::Node& n : t->node) {
                        if (n.v.IsNil()) continue;   // cleared entries keep their key only as a marker
                        if (!weakK || !IsWeakRef(n.k)) MarkValue(n.k, gray);
                        if (!weakV || !IsWeakRef(n.v)) MarkValue(n.v, gray);
                    }
                    break;
                }
                case Type::LFunc: {
                    LClosure* c = static_cast<LClosure*>(o);
                    Mark(c->p, gray);
                    for (Box* b : c->up) Mark(b, gray);
                    break;
                }
                case Type::NFunc:
                    for (const Value& v : static_cast<NClosure*>(o)->up) MarkValue(v, gray);
                    break;
                case Type::Box: MarkValue(static_cast<Box*>(o)->v, gray); break;
                case Type::Proto: {
                    Proto* p = static_cast<Proto*>(o);
                    for (const Value& v : p->k) MarkValue(v, gray);
                    for (Proto* c : p->protos) Mark(c, gray);
                    break;
                }
                case Type::Thread: {
                    Coroutine* c = static_cast<Coroutine*>(o);
                    for (int i = 0; i < c->top && i < (int)c->stack.size(); ++i) MarkValue(c->stack[(std::size_t)i], gray);
                    for (const Frame& f : c->frames) { Mark(f.cl, gray); Mark(f.ncl, gray); }
                    MarkValue(c->fn, gray);
                    MarkValue(c->err, gray);
                    break;
                }
                case Type::Userdata: Mark(static_cast<UData*>(o)->meta, gray); break;
                default: break;
            }
        }
    };
    propagate();
    // finalizers (manual 2.5.3): objects marked for finalization that nothing reaches are
    // queued (reverse order of marking), their weak-value entries are cleared, then they
    // are kept alive (resurrected) for this cycle so their finalizer can see them.
    std::vector<GcObj*> found;
    {
        std::vector<GcObj*> keep;
        keep.reserve(finObjs_.size());
        for (std::size_t i = finObjs_.size(); i-- > 0;) {
            GcObj* o = finObjs_[i];
            if (o->marked) keep.push_back(o);
            else found.push_back(o);
        }
        std::reverse(keep.begin(), keep.end());
        finObjs_.swap(keep);
    }
    if (!found.empty()) {
        for (Table* t : weakTables) {
            for (Value& v : t->arr)
                if (IsWeakRef(v) && !v.o->marked) v = Value();
            for (Table::Node& n : t->node)
                if (!n.v.IsNil() && IsWeakRef(n.v) && !n.v.o->marked) n.v = Value();
        }
        for (GcObj* o : found) {
            o->fin = false;
            Mark(o, gray);
            toFinalize_.push_back(o);
        }
        propagate();
    }
    // weak tables: drop entries whose weak key or value is about to be freed
    for (Table* t : weakTables) {
        for (Value& v : t->arr)
            if (IsWeakRef(v) && !v.o->marked) v = Value();
        for (Table::Node& n : t->node) {
            if (n.v.IsNil()) continue;
            if ((IsWeakRef(n.v) && !n.v.o->marked) || (IsWeakRef(n.k) && !n.k.o->marked)) n.v = Value();
        }
    }
    weakTables.clear();
    // cleared table entries whose key is about to be freed become tombstones (the key
    // is only a position marker for next(); a dead key must not be compared later)
    for (GcObj* o = gcList_; o; o = o->gcNext) {
        if (o->type != Type::Table || !o->marked) continue;
        for (Table::Node& n : static_cast<Table*>(o)->node)
            if (n.v.IsNil() && n.k.IsGc() && n.k.o && !n.k.o->marked) {
                n.k.t = Type::Box;   // a key type no Lua value has
                n.k.o = nullptr;
            }
    }
    Sweep();
    gcThreshold_ = std::max<std::size_t>(bytes_ * 2, 1u << 20);
    if (bytes_ > memoryLimit) throw LuaFatal{"not enough memory (script memory limit reached)"};
    if (!toFinalize_.empty()) RunFinalizers();
}

void State::MarkForFinalization(GcObj* o) {
    if (!o || o->fin) return;
    o->fin = true;
    finObjs_.push_back(o);
}

// Run queued __gc metamethods (manual 2.5.3). Called at the end of a collection, which
// only happens at safe points (between instructions, or collectgarbage()). A finalizer
// can not yield; an error in one is reported to printSink as a warning and does not
// stop the program (the manual's "warning" behaviour). The run budget still applies.
void State::RunFinalizers() {
    if (runningFinalizers_) return;   // a collection inside a finalizer: the outer loop drains the queue
    runningFinalizers_ = true;
    struct Reset { bool& b; ~Reset() { b = false; } } reset{runningFinalizers_};
    std::size_t next = 0;
    while (next < toFinalize_.size()) {
        GcObj* o = toFinalize_[next++];
        Value v = Value::Obj(o);
        Value tm = GetTM(v, TM_GC);
        if (!tm.IsFunction()) continue;
        Coroutine* co = co_;
        std::size_t nframes = co->frames.size();
        CheckStack(3);
        int func = co->top;
        co->stack[(std::size_t)co->top++] = tm;
        co->stack[(std::size_t)co->top++] = v;
        // the object is on the stack now (rooted); drop it from the queue
        toFinalize_.erase(toFinalize_.begin() + (std::ptrdiff_t)(next - 1));
        --next;
        try {
            CallAt(func, 0);
            co->top = func;
        } catch (const LuaError&) {
            Value err = errorValue;
            Unwind(nframes, func, err);
            errorValue = Value();
            if (printSink) printSink("warning: error in __gc metamethod (" + ToStringBasic(err) + ")");
        }
    }
}

void State::Sweep() {
    GcObj** p = &gcList_;
    while (*p) {
        GcObj* o = *p;
        if (o->marked) {
            o->marked = false;
            p = &o->gcNext;
            continue;
        }
        *p = o->gcNext;
        AccountFree(o->gcSize);
        delete o;
    }
}

int State::Ref(const Value& v) {
    int r;
    if (!freeRefs_.empty()) { r = freeRefs_.back(); freeRefs_.pop_back(); }
    else r = (int)registry->Length() + 1;
    registry->SetInt(*this, r, v.IsNil() ? Value::Bool(false) : v);
    return r;
}
Value State::GetRef(int ref) const { return registry->GetInt(ref); }
void State::Unref(int ref) {
    if (ref <= 0) return;
    registry->SetInt(*this, ref, Value::Bool(false));
    freeRefs_.push_back(ref);
}

// ------------------------------------------------------------------ stack
int State::CurBase() const { return co_->frames.empty() ? 0 : co_->frames.back().base; }

void State::CheckStack(int extra) {
    std::size_t need = (std::size_t)co_->top + (std::size_t)extra + 8;
    if (need > co_->stack.size()) {
        if (need > (std::size_t)kMaxStack) Error("stack overflow");
        std::size_t n = std::max(need, co_->stack.size() * 2);
        if (n > (std::size_t)kMaxStack + 64) n = (std::size_t)kMaxStack + 64;
        std::size_t old = co_->stack.size();
        co_->stack.resize(n);
        Regrow(co_, sizeof(Coroutine) + n * sizeof(Value));
        (void)old;
    }
}

void State::Push(const Value& v) {
    CheckStack(1);
    co_->stack[(std::size_t)co_->top++] = v;
}
Value State::Pop() { return co_->stack[(std::size_t)--co_->top]; }
void State::SetTop(int n) {
    int b = CurBase();
    if (n >= 0) {
        CheckStack(n);
        while (co_->top < b + n) co_->stack[(std::size_t)co_->top++] = Value();
        co_->top = b + n;
    } else {
        co_->top += n + 1;
    }
}
Value& State::At(int idx) {
    if (idx < 0) return co_->stack[(std::size_t)(co_->top + idx)];
    return co_->stack[(std::size_t)(CurBase() + idx)];
}
Value State::Arg(int i) const {
    const Frame& f = co_->frames.back();
    return i < f.vaCount ? co_->stack[(std::size_t)(f.base + i)] : Value();
}
Value& State::ArgRef(int i) { return co_->stack[(std::size_t)(co_->frames.back().base + i)]; }

// ------------------------------------------------------------------ errors
std::string State::Where(int level) {
    const auto& fr = co_->frames;
    int idx = (int)fr.size() - 1 - level;
    if (idx < 0 || idx >= (int)fr.size()) return "";
    const Frame& f = fr[(std::size_t)idx];
    if (!f.cl) return "";
    const Proto* p = f.cl->p;
    int pc = f.pc > 0 ? f.pc - 1 : 0;
    int line = pc < (int)p->lines.size() ? p->lines[(std::size_t)pc] : 0;
    return p->chunk + ":" + std::to_string(line) + ": ";
}

void State::ErrorValue(const Value& v) {
    errorValue = v;
    throw LuaError{};
}

void State::Error(const std::string& msg) {
    std::string w;
    if (!co_->frames.empty()) w = Where(co_->frames.back().cl ? 0 : 1);
    ErrorValue(Value::Obj(NewString(w + msg)));
}

void State::ArgError(int arg, const char* fname, const std::string& msg) {
    Error("bad argument #" + std::to_string(arg + 1) + " to '" + fname + "' (" + msg + ")");
}

void State::TypeError(int arg, const char* fname, const char* expected) {
    Value v = Arg(arg);
    const char* got = arg >= co_->frames.back().vaCount ? "no value" : TypeName(v);
    ArgError(arg, fname, std::string(expected) + " expected, got " + got);
}

std::string State::VarInfo(const Frame& f, int pc, int which) {
    if (!f.cl) return "";
    auto it = f.cl->p->hints.find(pc);
    if (it == f.cl->p->hints.end() || it->second.empty()) return "";
    std::string h = it->second;
    std::size_t nl = h.find('\n');
    if (nl != std::string::npos) h = which == 1 ? h.substr(nl + 1) : h.substr(0, nl);
    if (h.empty()) return "";
    return " (" + h + ")";
}

void State::OpError(const char* what, const Value& v, int which) {
    std::string info;
    if (!co_->frames.empty() && co_->frames.back().cl) {
        const Frame& f = co_->frames.back();
        info = VarInfo(f, f.pc - 1, which);
    }
    Error(std::string("attempt to ") + what + " a " + TypeName(v) + " value" + info);
}

// ------------------------------------------------------------------ argument helpers
bool State::ToNumber(const Value& v, Value& out) {
    if (v.IsNumber()) { out = v; return true; }
    if (v.IsString()) return StrToNumber(v.Str()->s, out);
    return false;
}

bool State::ToInteger(const Value& v, std::int64_t& out) {
    Value n;
    if (!ToNumber(v, n)) return false;
    if (n.t == Type::Int) { out = n.i; return true; }
    double f = n.n;
    if (f == std::floor(f) && f >= -kTwo63 && f < kTwo63) { out = (std::int64_t)f; return true; }
    return false;
}

std::int64_t State::CheckInt(int arg, const char* fname) {
    Value v = Arg(arg);
    std::int64_t r;
    if (ToInteger(v, r)) return r;
    Value n;
    if (ToNumber(v, n)) ArgError(arg, fname, "number has no integer representation");
    TypeError(arg, fname, "number");
}
std::int64_t State::OptInt(int arg, const char* fname, std::int64_t def) {
    return Arg(arg).IsNil() ? def : CheckInt(arg, fname);
}
double State::CheckNum(int arg, const char* fname) {
    Value n;
    if (!ToNumber(Arg(arg), n)) TypeError(arg, fname, "number");
    return n.AsNum();
}
LString* State::CheckStr(int arg, const char* fname) {
    Value v = Arg(arg);
    if (v.IsString()) return v.Str();
    if (v.IsNumber()) {
        LString* s = NewString(NumberToString(v));
        ArgRef(arg) = Value::Obj(s);   // keep it reachable
        return s;
    }
    TypeError(arg, fname, "string");
}
Table* State::CheckTable(int arg, const char* fname) {
    Value v = Arg(arg);
    if (!v.IsTable()) TypeError(arg, fname, "table");
    return v.Tab();
}
void State::CheckAny(int arg, const char* fname) {
    if (arg >= co_->frames.back().vaCount) ArgError(arg, fname, "value expected");
}

void State::Charge(std::uint64_t n) {
    if (!budget_) return;
    used_ += n;
    if (used_ > budget_) throw LuaFatal{"script ran too long (instruction budget exceeded; endless loop?)"};
}

// ------------------------------------------------------------------ numbers and strings
std::string FormatFloat(double d) {
    if (std::isnan(d)) return std::signbit(d) ? "-nan" : "nan";
    if (std::isinf(d)) return d > 0 ? "inf" : "-inf";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.14g", d);
    std::string s = buf;
    for (char& c : s) if (c == ',') c = '.';   // a locale with a decimal comma
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s;
}

std::string State::NumberToString(const Value& v) {
    if (v.t == Type::Int) return std::to_string(v.i);
    return FormatFloat(v.n);
}

bool State::StrToNumber(const std::string& str, Value& out) {
    const char* s = str.c_str();
    const char* e = s + str.size();
    auto isws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
    while (s < e && isws(*s)) ++s;
    while (e > s && isws(e[-1])) --e;
    if (s == e) return false;
    bool neg = false;
    if (*s == '-' || *s == '+') { neg = *s == '-'; ++s; }
    if (s == e) return false;
    if (e - s >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        std::uint64_t iv = 0;
        double m = 0;
        int exp = 0;
        bool any = false, isFloat = false;
        auto hv = [](char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        while (s < e && hv(*s) >= 0) { iv = iv * 16 + (std::uint64_t)hv(*s); m = m * 16 + hv(*s); any = true; ++s; }
        if (s < e && *s == '.') {
            isFloat = true;
            ++s;
            while (s < e && hv(*s) >= 0) { m = m * 16 + hv(*s); exp -= 4; any = true; ++s; }
        }
        if (!any) return false;
        if (s < e && (*s == 'p' || *s == 'P')) {
            isFloat = true;
            ++s;
            bool eneg = false;
            if (s < e && (*s == '+' || *s == '-')) { eneg = *s == '-'; ++s; }
            if (s == e || !(*s >= '0' && *s <= '9')) return false;
            long ev = 0;
            while (s < e && *s >= '0' && *s <= '9') { if (ev < 100000) ev = ev * 10 + (*s - '0'); ++s; }
            exp += eneg ? -(int)ev : (int)ev;
        }
        if (s != e) return false;
        if (!isFloat) {
            out = Value::Int(neg ? (std::int64_t)(0ull - iv) : (std::int64_t)iv);   // wraps around
            return true;
        }
        double d = std::ldexp(m, exp);
        out = Value::Num(neg ? -d : d);
        return true;
    }
    // decimal
    const char* start = s;
    bool any = false, isFloat = false;
    while (s < e && *s >= '0' && *s <= '9') { any = true; ++s; }
    if (s < e && *s == '.') { isFloat = true; ++s; while (s < e && *s >= '0' && *s <= '9') { any = true; ++s; } }
    if (!any) return false;
    if (s < e && (*s == 'e' || *s == 'E')) {
        isFloat = true;
        ++s;
        if (s < e && (*s == '+' || *s == '-')) ++s;
        if (s == e || !(*s >= '0' && *s <= '9')) return false;
        while (s < e && *s >= '0' && *s <= '9') ++s;
    }
    if (s != e) return false;
    if (!isFloat) {
        std::uint64_t v = 0;
        bool overflow = false;
        for (const char* p = start; p < e; ++p) {
            std::uint64_t d = (std::uint64_t)(*p - '0');
            if (v > (UINT64_MAX - d) / 10) { overflow = true; break; }
            v = v * 10 + d;
        }
        std::uint64_t lim = neg ? (std::uint64_t)INT64_MAX + 1 : (std::uint64_t)INT64_MAX;
        if (!overflow && v <= lim) {
            out = Value::Int(neg ? (std::int64_t)(0ull - v) : (std::int64_t)v);
            return true;
        }
        // too big for an integer: it is a float
    }
    double d = 0;
    auto r = std::from_chars(start, e, d, std::chars_format::general);
    if (r.ec == std::errc::result_out_of_range) {
        // overflow gives infinity, underflow zero
        const char* ep = start;
        while (ep < e && *ep != 'e' && *ep != 'E') ++ep;
        bool tiny = ep < e && ep + 1 < e && ep[1] == '-';
        d = tiny ? 0.0 : HUGE_VAL;
    } else if (r.ec != std::errc() || r.ptr != e) {
        return false;
    }
    out = Value::Num(neg ? -d : d);
    return true;
}

const char* State::TypeName(const Value& v) const {
    switch (v.t) {
        case Type::Nil: return "nil";
        case Type::Bool: return "boolean";
        case Type::Int: case Type::Float: return "number";
        case Type::String: return "string";
        case Type::Table: return "table";
        case Type::LFunc: case Type::NFunc: return "function";
        case Type::Thread: return "thread";
        case Type::Userdata: return "userdata";
        default: return "internal";
    }
}

std::string State::ToStringBasic(const Value& v) {
    char buf[64];
    switch (v.t) {
        case Type::Nil: return "nil";
        case Type::Bool: return v.b ? "true" : "false";
        case Type::Int: case Type::Float: return NumberToString(v);
        case Type::String: return v.Str()->s;
        case Type::NFunc: std::snprintf(buf, sizeof buf, "builtin: %p", (void*)v.o); return std::string("function: ") + buf;
        default: std::snprintf(buf, sizeof buf, "%s: %p", TypeName(v), (void*)v.o); return buf;
    }
}

std::string State::ToStringMeta(const Value& v) {
    Value tm = GetTM(v, TM_TOSTRING);
    if (!tm.IsNil()) {
        Value r = CallMeta1(tm, v);
        if (!r.IsString()) {
            if (r.IsNumber()) return NumberToString(r);
            Error("'__tostring' must return a string");
        }
        return r.Str()->s;
    }
    Value nm = GetTM(v, TM_NAME);
    if (nm.IsString() && (v.t == Type::Table || v.t == Type::Userdata)) {
        char buf[48];
        std::snprintf(buf, sizeof buf, ": %p", (void*)v.o);
        return nm.Str()->s + buf;
    }
    return ToStringBasic(v);
}

// ------------------------------------------------------------------ metatables and operators
Table* State::GetMetatable(const Value& v) const {
    switch (v.t) {
        case Type::Table: return v.Tab()->meta;
        case Type::Userdata: return v.UD()->meta;
        case Type::String: return stringMeta;
        default: return nullptr;
    }
}

Value State::GetTM(const Value& v, int tm) const {
    Table* mt = GetMetatable(v);
    if (!mt) return Value();
    return mt->GetStr(tmName[tm]);
}

Value State::GetMetamethod(const Value& v, const char* event) {
    Table* mt = GetMetatable(v);
    if (!mt) return Value();
    return mt->Get(Value::Obj(NewString(event)));
}

Value State::CallMeta1(const Value& f, const Value& a) {
    CheckStack(3);
    int func = co_->top;
    co_->stack[(std::size_t)co_->top++] = f;
    co_->stack[(std::size_t)co_->top++] = a;
    CallAt(func, 1);
    return co_->stack[(std::size_t)--co_->top];
}

Value State::CallMeta2(const Value& f, const Value& a, const Value& b) {
    CheckStack(4);
    int func = co_->top;
    co_->stack[(std::size_t)co_->top++] = f;
    co_->stack[(std::size_t)co_->top++] = a;
    co_->stack[(std::size_t)co_->top++] = b;
    CallAt(func, 1);
    return co_->stack[(std::size_t)--co_->top];
}

Value State::Index(const Value& objIn, const Value& key) {
    Value obj = objIn;
    for (int loop = 0; loop < 100; ++loop) {
        Value tm;
        if (obj.t == Type::Table) {
            Table* t = obj.Tab();
            Value v = t->Get(key);
            if (!v.IsNil() || !t->meta) return v;
            tm = t->meta->GetStr(tmName[TM_INDEX]);
            if (tm.IsNil()) return v;
        } else {
            tm = GetTM(obj, TM_INDEX);
            if (tm.IsNil()) {
                if (loop == 0) OpError("index", obj, 0);
                Error(std::string("attempt to index a ") + TypeName(obj) + " value");
            }
        }
        if (tm.IsFunction()) return CallMeta2(tm, obj, key);
        obj = tm;
    }
    Error("'__index' chain too long; possible loop");
}

void State::SetIndex(const Value& objIn, const Value& key, const Value& v) {
    Value obj = objIn;
    for (int loop = 0; loop < 100; ++loop) {
        Value tm;
        if (obj.t == Type::Table) {
            Table* t = obj.Tab();
            if (!t->meta) { t->Set(*this, key, v); return; }
            Value cur = t->Get(key);
            if (!cur.IsNil()) { t->Set(*this, key, v); return; }
            tm = t->meta->GetStr(tmName[TM_NEWINDEX]);
            if (tm.IsNil()) { t->Set(*this, key, v); return; }
        } else {
            tm = GetTM(obj, TM_NEWINDEX);
            if (tm.IsNil()) {
                if (loop == 0) OpError("index", obj, 0);
                Error(std::string("attempt to index a ") + TypeName(obj) + " value");
            }
        }
        if (tm.IsFunction()) {
            CheckStack(5);
            int func = co_->top;
            co_->stack[(std::size_t)co_->top++] = tm;
            co_->stack[(std::size_t)co_->top++] = obj;
            co_->stack[(std::size_t)co_->top++] = key;
            co_->stack[(std::size_t)co_->top++] = v;
            CallAt(func, 0);
            return;
        }
        obj = tm;
    }
    Error("'__newindex' chain too long; possible loop");
}

namespace {
bool LtIntFloat(std::int64_t i, double f) {
    if (std::isnan(f)) return false;
    if (f >= kTwo63) return true;
    if (f <= -kTwo63) return false;
    return i < (std::int64_t)std::ceil(f);
}
bool LeIntFloat(std::int64_t i, double f) {
    if (std::isnan(f)) return false;
    if (f >= kTwo63) return true;
    if (f < -kTwo63) return false;
    return i <= (std::int64_t)std::floor(f);
}
bool LtFloatInt(double f, std::int64_t i) {
    if (std::isnan(f)) return false;
    if (f >= kTwo63) return false;
    if (f < -kTwo63) return true;
    return (std::int64_t)std::floor(f) < i;
}
bool LeFloatInt(double f, std::int64_t i) {
    if (std::isnan(f)) return false;
    if (f >= kTwo63) return false;
    if (f < -kTwo63) return true;
    return (std::int64_t)std::ceil(f) <= i;
}
bool NumLt(const Value& a, const Value& b) {
    if (a.t == Type::Int) return b.t == Type::Int ? a.i < b.i : LtIntFloat(a.i, b.n);
    return b.t == Type::Float ? a.n < b.n : LtFloatInt(a.n, b.i);
}
bool NumLe(const Value& a, const Value& b) {
    if (a.t == Type::Int) return b.t == Type::Int ? a.i <= b.i : LeIntFloat(a.i, b.n);
    return b.t == Type::Float ? a.n <= b.n : LeFloatInt(a.n, b.i);
}
int StrCmp(const LString* a, const LString* b) {
    std::size_t n = std::min(a->s.size(), b->s.size());
    int c = std::memcmp(a->s.data(), b->s.data(), n);
    if (c) return c;
    return a->s.size() < b->s.size() ? -1 : a->s.size() > b->s.size() ? 1 : 0;
}
} // namespace

bool State::Equals(const Value& a, const Value& b) {
    if (RawEquals(a, b)) return true;
    if (a.t != b.t || (a.t != Type::Table && a.t != Type::Userdata)) return false;
    Value tm = GetTM(a, TM_EQ);
    if (tm.IsNil()) tm = GetTM(b, TM_EQ);
    if (tm.IsNil()) return false;
    return CallMeta2(tm, a, b).Truthy();
}

[[noreturn]] static void CompareError(State& L, const Value& a, const Value& b) {
    const char* t1 = L.TypeName(a);
    const char* t2 = L.TypeName(b);
    if (std::strcmp(t1, t2) == 0) L.Error(std::string("attempt to compare two ") + t1 + " values");
    L.Error(std::string("attempt to compare ") + t1 + " with " + t2);
}

bool State::LessThan(const Value& a, const Value& b) {
    if (a.IsNumber() && b.IsNumber()) return NumLt(a, b);
    if (a.IsString() && b.IsString()) return StrCmp(a.Str(), b.Str()) < 0;
    Value tm = GetTM(a, TM_LT);
    if (tm.IsNil()) tm = GetTM(b, TM_LT);
    if (tm.IsNil()) CompareError(*this, a, b);
    return CallMeta2(tm, a, b).Truthy();
}

bool State::LessEqual(const Value& a, const Value& b) {
    if (a.IsNumber() && b.IsNumber()) return NumLe(a, b);
    if (a.IsString() && b.IsString()) return StrCmp(a.Str(), b.Str()) <= 0;
    Value tm = GetTM(a, TM_LE);
    if (tm.IsNil()) tm = GetTM(b, TM_LE);
    if (tm.IsNil()) CompareError(*this, a, b);
    return CallMeta2(tm, a, b).Truthy();
}

namespace {
std::int64_t IntMod(State& L, std::int64_t a, std::int64_t b) {
    if (b == 0) L.Error("attempt to perform 'n%0'");
    if (b == -1) return 0;
    std::int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}
std::int64_t IntDiv(State& L, std::int64_t a, std::int64_t b) {
    if (b == 0) L.Error("attempt to perform 'n//0'");
    if (b == -1) return (std::int64_t)(0ull - (std::uint64_t)a);
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q -= 1;
    return q;
}
double FltMod(double a, double b) {
    double m = std::fmod(a, b);
    if (m != 0 && ((m < 0) != (b < 0))) m += b;
    return m;
}
std::int64_t ShiftLeft(std::int64_t x, std::int64_t n) {
    if (n <= -64 || n >= 64) return 0;
    if (n >= 0) return (std::int64_t)((std::uint64_t)x << n);
    return (std::int64_t)((std::uint64_t)x >> (-n));
}
} // namespace

// Arithmetic on two values that are already numbers (op is ADD..SHR). Returns false for a
// bitwise op on a float without an integer value.
static bool ArithNum(State& L, int op, const Value& a, const Value& b, Value& r) {
    if (op >= BAND) {
        std::int64_t x, y;
        if (!State::ToInteger(a, x) || !State::ToInteger(b, y)) return false;
        switch (op) {
            case BAND: r = Value::Int(x & y); break;
            case BOR: r = Value::Int(x | y); break;
            case BXOR: r = Value::Int(x ^ y); break;
            case SHL: r = Value::Int(ShiftLeft(x, y)); break;
            case SHR: r = Value::Int(ShiftLeft(x, y == INT64_MIN ? 64 : -y)); break;
        }
        return true;
    }
    if (a.t == Type::Int && b.t == Type::Int && op != DIV && op != POW) {
        std::uint64_t x = (std::uint64_t)a.i, y = (std::uint64_t)b.i;
        switch (op) {
            case ADD: r = Value::Int((std::int64_t)(x + y)); break;
            case SUB: r = Value::Int((std::int64_t)(x - y)); break;
            case MUL: r = Value::Int((std::int64_t)(x * y)); break;
            case MOD: r = Value::Int(IntMod(L, a.i, b.i)); break;
            case IDIV: r = Value::Int(IntDiv(L, a.i, b.i)); break;
        }
        return true;
    }
    double x = a.AsNum(), y = b.AsNum();
    switch (op) {
        case ADD: r = Value::Num(x + y); break;
        case SUB: r = Value::Num(x - y); break;
        case MUL: r = Value::Num(x * y); break;
        case DIV: r = Value::Num(x / y); break;
        case POW: r = Value::Num(std::pow(x, y)); break;
        case MOD: r = Value::Num(FltMod(x, y)); break;
        case IDIV: r = Value::Num(std::floor(x / y)); break;
    }
    return true;
}

Value State::Arith(int op, const Value& a, const Value& b) {
    Value x, y, r;
    if (op >= BAND) {
        if (a.IsNumber() && b.IsNumber() && ArithNum(*this, op, a, b, r)) return r;
    } else if (ToNumber(a, x) && ToNumber(b, y)) {
        ArithNum(*this, op, x, y, r);
        return r;
    }
    int tm = TM_ADD + (op - ADD);
    Value f = GetTM(a, tm);
    if (f.IsNil()) f = GetTM(b, tm);
    if (!f.IsNil()) return CallMeta2(f, a, b);
    if (op >= BAND) {
        if (a.IsNumber() && b.IsNumber()) Error("number has no integer representation");
        const Value& bad = a.IsNumber() ? b : a;
        OpError("perform bitwise operation on", bad, a.IsNumber() ? 1 : 0);
    }
    Value tmp;
    bool aok = ToNumber(a, tmp);
    OpError("perform arithmetic on", aok ? b : a, aok ? 1 : 0);
}

Value State::Length(const Value& v) {
    if (v.IsString()) return Value::Int((std::int64_t)v.Str()->s.size());
    Value tm = GetTM(v, TM_LEN);
    if (!tm.IsNil()) return CallMeta1(tm, v);
    if (v.IsTable()) return Value::Int(v.Tab()->Length());
    OpError("get length of", v, 0);
}

Value State::Concat(const Value& a, const Value& b) {
    bool as = a.IsString() || a.IsNumber(), bs = b.IsString() || b.IsNumber();
    if (as && bs) {
        std::string s = a.IsString() ? a.Str()->s : NumberToString(a);
        if (b.IsString()) s += b.Str()->s; else s += NumberToString(b);
        return Value::Obj(NewString(s));
    }
    Value tm = GetTM(a, TM_CONCAT);
    if (tm.IsNil()) tm = GetTM(b, TM_CONCAT);
    if (!tm.IsNil()) return CallMeta2(tm, a, b);
    OpError("concatenate", as ? b : a, as ? 1 : 0);
}

// ------------------------------------------------------------------ calls
void State::PushLuaFrame(int func, int nargs, int nres, bool entry) {
    Coroutine* co = co_;
    if (co->frames.size() >= kMaxFrames) Error("stack overflow");
    LClosure* cl = co->stack[(std::size_t)func].LFn();
    Proto* p = cl->p;
    int np = p->numParams;
    CheckStack(nargs + np + p->maxStack + 16);
    Frame fr;
    fr.cl = cl;
    fr.func = func;
    fr.nres = nres;
    fr.entry = entry;
    fr.tbcMark = (int)co->tbc.size();
    for (int i = nargs; i < np; ++i) co->stack[(std::size_t)(func + 1 + i)] = Value();
    if (!p->isVararg) {
        fr.base = func + 1;
    } else {
        int actual = std::max(nargs, np);
        fr.vaStart = func + 1 + np;
        fr.vaCount = nargs > np ? nargs - np : 0;
        fr.base = func + 1 + actual;
        for (int i = 0; i < np; ++i) co->stack[(std::size_t)(fr.base + i)] = co->stack[(std::size_t)(func + 1 + i)];
    }
    co->top = fr.base + np;
    co->frames.push_back(fr);
}

void State::PostCall(const Frame& f, int first, int n) {
    Coroutine* co = co_;
    int func = f.func;
    if (f.nres < 0) {
        for (int i = 0; i < n; ++i) co->stack[(std::size_t)(func + i)] = co->stack[(std::size_t)(first + i)];
        co->top = func + n;
    } else {
        for (int i = 0; i < f.nres; ++i)
            co->stack[(std::size_t)(func + i)] = i < n ? co->stack[(std::size_t)(first + i)] : Value();
        co->top = func + f.nres;
    }
}

int State::CallTM(int func, int nargs) {
    Coroutine* co = co_;
    Value f = co->stack[(std::size_t)func];
    Value tm = GetTM(f, TM_CALL);
    if (tm.IsNil()) {
        // which call failed: the hint of the running instruction names the function
        OpError("call", f, 0);
    }
    CheckStack(1);
    for (int i = co->top; i > func; --i) co->stack[(std::size_t)i] = co->stack[(std::size_t)(i - 1)];
    co->top++;
    co->stack[(std::size_t)func] = tm;
    return nargs + 1;
}

int State::CallNative(int func, int nargs, int nres) {
    Coroutine* co = co_;
    if (co->frames.size() >= kMaxFrames) Error("stack overflow");
    Frame fr;
    fr.ncl = co->stack[(std::size_t)func].NFn();
    fr.func = func;
    fr.base = func + 1;
    fr.nres = nres;
    fr.vaCount = nargs;   // natives keep their argument count here
    fr.tbcMark = (int)co->tbc.size();
    co->frames.push_back(fr);
    CheckStack(32);
    int n = fr.ncl->fn(*this, nargs);
    if (n == kYieldResult) {
        co->frames.pop_back();
        return kYieldResult;
    }
    Frame done = co->frames.back();
    co->frames.pop_back();
    PostCall(done, co->top - n, n);
    return n;
}

void State::CallAt(int func, int nres) {
    if (cDepth >= kMaxCDepth) Error("C stack overflow");
    DepthGuard g1(cDepth);
    DepthGuard g2(co_->nest);
    for (;;) {
        Value f = co_->stack[(std::size_t)func];
        int nargs = co_->top - func - 1;
        if (f.t == Type::LFunc) {
            PushLuaFrame(func, nargs, nres, true);
            Execute();
            return;
        }
        if (f.t == Type::NFunc) {
            if (CallNative(func, nargs, nres) == kYieldResult) Error("attempt to yield across a C-call boundary");
            return;
        }
        CallTM(func, nargs);
    }
}

void State::Call(int nargs, int nres) { CallAt(co_->top - nargs - 1, nres); }

void State::CloseTbc(int level, const Value& err, bool withErr) {
    Coroutine* co = co_;
    while (!co->tbc.empty() && co->tbc.back() >= level) {
        int slot = co->tbc.back();
        co->tbc.pop_back();
        Value v = co->stack[(std::size_t)slot];
        Value tm = GetTM(v, TM_CLOSE);
        if (tm.IsNil()) continue;
        CheckStack(4);
        int func = co->top;
        co->stack[(std::size_t)co->top++] = tm;
        co->stack[(std::size_t)co->top++] = v;
        co->stack[(std::size_t)co->top++] = withErr ? err : Value();
        CallAt(func, 0);
    }
}

// Drop frames above `frames` after an error, closing pending to-be-closed
// variables above `top` with the error (an error while closing replaces it).
void State::Unwind(std::size_t frames, int top, Value& err) {
    Coroutine* co = co_;
    co->frames.resize(frames);
    errorValue = err;   // a root while __close handlers run
    while (!co->tbc.empty() && co->tbc.back() >= top) {
        try {
            int slot = co->tbc.back();
            if (co->top <= slot) co->top = slot + 1;
            Value e = errorValue;
            CloseTbc(slot, e, true);
        } catch (const LuaError&) {
            co->frames.resize(frames);   // errorValue now holds the newer error
        }
    }
    err = errorValue;
    co->top = top;
}

bool State::PCall(int nargs, int nres) {
    Coroutine* co = co_;
    int func = co->top - nargs - 1;
    std::size_t nframes = co->frames.size();
    try {
        CallAt(func, nres);
        return true;
    } catch (const LuaError&) {
        Value err = errorValue;
        Unwind(nframes, func, err);
        co->stack[(std::size_t)co->top++] = err;
        errorValue = Value();
        return false;
    } catch (const std::bad_alloc&) {
        Value err = Value::Obj(NewString("not enough memory"));
        Unwind(nframes, func, err);
        co->stack[(std::size_t)co->top++] = err;
        errorValue = Value();
        return false;
    }
}

bool State::PCallHost(int nargs, int nres, std::string* errOut) {
    Coroutine* co = co_;
    int func = co->top - nargs - 1;
    std::size_t nframes = co->frames.size();
    const int depth0 = cDepth, nest0 = co->nest;   // a host call may be nested inside running Lua code
    const std::size_t coDepth0 = coStack_.size();
    try {
        if (PCall(nargs, nres)) return true;
        Value e = Pop();
        if (errOut) *errOut = e.IsString() ? e.Str()->s : (e.IsNil() ? "nil" : ToStringBasic(e));
        if (errOut && !e.IsString() && !e.IsNumber()) *errOut = "(error object is a " + std::string(TypeName(e)) + " value)";
        if (errOut && e.IsNumber()) *errOut = NumberToString(e);
        return false;
    } catch (const LuaFatal& f) {
        RecoverFatal(co, nframes, func, coDepth0);
        cDepth = depth0;
        co->nest = nest0;
        if (errOut) *errOut = f.message;
        return false;
    } catch (const std::exception& e) {
        RecoverFatal(co, nframes, func, coDepth0);
        cDepth = depth0;
        co->nest = nest0;
        if (errOut) *errOut = std::string("internal error: ") + e.what();
        return false;
    }
}

// After a stop that pcall can not catch: back to the host's coroutine and frame. The
// coroutines that were running on the way are dead; to-be-closed variables are dropped.
void State::RecoverFatal(Coroutine* co, std::size_t nframes, int func, std::size_t coDepth) {
    if (co_ != co) {
        co_->status = CoStatus::Dead;
        co_->frames.clear();
        co_->top = 0;
    }
    for (std::size_t i = coDepth; i < coStack_.size(); ++i) {
        Coroutine* c = coStack_[i];
        if (c != co) { c->status = CoStatus::Dead; c->frames.clear(); c->top = 0; c->nest = 0; }
    }
    coStack_.resize(std::min(coDepth, coStack_.size()));
    co_ = co;
    co->status = CoStatus::Running;
    co->frames.resize(nframes);
    while (!co->tbc.empty() && co->tbc.back() >= func) co->tbc.pop_back();
    co->top = func;
    cDepth = 0;
    co->nest = 0;
    errorValue = Value();
}

bool State::Load(const std::string& source, const std::string& chunkName, SyntaxError* err, Table* env) {
    std::vector<SyntaxError> errs;
    Proto* p = Compile(*this, source, chunkName, errs, false);
    if (!p) {
        if (err && !errs.empty()) *err = errs.front();
        return false;
    }
    LClosure* cl = NewLClosure(p);
    Box* b = Alloc<Box>();
    b->v = Value::Obj(env ? env : globals_);
    cl->up.push_back(b);
    Push(Value::Obj(cl));
    return true;
}

std::vector<SyntaxError> State::CheckSyntax(const std::string& source, const std::string& chunkName) {
    std::vector<SyntaxError> errs;
    Compile(*this, source, chunkName, errs, true);
    return errs;
}

bool State::DoString(const std::string& source, const std::string& chunkName, std::string* err) {
    SyntaxError se;
    if (!Load(source, chunkName, &se)) {
        if (err) *err = se.message;
        return false;
    }
    return PCallHost(0, 0, err);
}

std::vector<Value> State::CallValue(const Value& fn, const std::vector<Value>& args, int nres) {
    CheckStack((int)args.size() + 2);
    int func = co_->top;
    co_->stack[(std::size_t)co_->top++] = fn;
    for (const Value& a : args) co_->stack[(std::size_t)co_->top++] = a;
    CallAt(func, nres);
    std::vector<Value> out(co_->stack.begin() + func, co_->stack.begin() + co_->top);
    co_->top = func;
    return out;
}

// ------------------------------------------------------------------ coroutines
bool State::IsYieldable() const { return co_ != main_ && co_->nest == 0; }

int State::Yield(int nargs) {
    if (co_ == main_) Error("attempt to yield from outside a coroutine");
    if (co_->nest > 0) Error("attempt to yield across a C-call boundary");
    co_->nYield = nargs;
    return kYieldResult;
}

bool State::Resume(Coroutine* co, int nargs, int* nresults) {
    Coroutine* prev = co_;
    auto fail = [&](const char* msg) {
        prev->top -= nargs;
        Push(Value::Obj(NewString(msg)));
        if (nresults) *nresults = 1;
        return false;
    };
    if (co->status == CoStatus::Dead) return fail("cannot resume dead coroutine");
    if (co->status != CoStatus::Suspended) return fail(co == prev ? "cannot resume non-suspended coroutine"
                                                                  : "cannot resume non-suspended coroutine");
    if (cDepth >= kMaxCDepth) return fail("C stack overflow");
    DepthGuard g(cDepth);
    // move the arguments over
    int from = prev->top - nargs;
    coStack_.push_back(prev);
    prev->status = CoStatus::Normal;
    co->status = CoStatus::Running;
    co_ = co;
    bool yielded = false;
    try {
        if (!co->started) {
            co->started = true;
            co->top = 0;
            CheckStack(nargs + 2);
            co->stack[(std::size_t)co->top++] = co->fn;
            for (int i = 0; i < nargs; ++i) co->stack[(std::size_t)co->top++] = prev->stack[(std::size_t)(from + i)];
            prev->top = from;
            for (;;) {
                Value f = co->stack[0];
                if (f.t == Type::LFunc) {
                    PushLuaFrame(0, co->top - 1, -1, true);
                    yielded = Execute();
                    break;
                }
                if (f.t == Type::NFunc) {
                    yielded = CallNative(0, co->top - 1, -1) == kYieldResult;
                    break;
                }
                CallTM(0, co->top - 1);
            }
        } else {
            CheckStack(nargs + 2);
            if (co->frames.empty()) {
                // a native body that yielded: the resume values are its results
                co->top = 0;
                for (int i = 0; i < nargs; ++i) co->stack[(std::size_t)co->top++] = prev->stack[(std::size_t)(from + i)];
                prev->top = from;
            } else {
                // the pending coroutine.yield call returns the resume values
                int first = co->top;
                for (int i = 0; i < nargs; ++i) co->stack[(std::size_t)co->top++] = prev->stack[(std::size_t)(from + i)];
                prev->top = from;
                Frame fake;
                fake.func = co->yieldFunc;
                fake.nres = co->yieldNres;
                PostCall(fake, first, nargs);
                yielded = Execute();
            }
        }
    } catch (const LuaError&) {
        co->status = CoStatus::Dead;
        co->errored = true;
        co->err = errorValue;
        co_ = prev;
        coStack_.pop_back();
        prev->status = CoStatus::Running;
        Push(errorValue);
        errorValue = Value();
        if (nresults) *nresults = 1;
        return false;
    } catch (const std::bad_alloc&) {
        co->status = CoStatus::Dead;
        co_ = prev;
        coStack_.pop_back();
        prev->status = CoStatus::Running;
        Push(Value::Obj(NewString("not enough memory")));
        if (nresults) *nresults = 1;
        return false;
    }
    co_ = prev;
    coStack_.pop_back();
    prev->status = CoStatus::Running;
    int n;
    int first;
    if (yielded) {
        co->status = CoStatus::Suspended;
        n = co->nYield;
        first = co->top - n;
    } else {
        co->status = CoStatus::Dead;
        n = co->top;
        first = 0;
        co->frames.clear();
    }
    CheckStack(n + 1);
    for (int i = 0; i < n; ++i) prev->stack[(std::size_t)prev->top++] = co->stack[(std::size_t)(first + i)];
    co->top = yielded ? first : 0;
    if (yielded) {
        // remember where the yield's results go: it was the native call just popped
    }
    if (nresults) *nresults = n;
    return true;
}

void State::CloseCoroutine(Coroutine* co) {
    Coroutine* prev = co_;
    co_ = co;
    coStack_.push_back(prev);
    try {
        Value e;
        CloseTbc(0, e, false);
    } catch (...) {
        co_ = prev;
        coStack_.pop_back();
        co->status = CoStatus::Dead;
        throw;
    }
    co_ = prev;
    coStack_.pop_back();
    co->status = CoStatus::Dead;
    co->frames.clear();
    co->top = 0;
}

// ------------------------------------------------------------------ the loop
bool State::ForPrep(int base, int a) {
    Coroutine* co = co_;
    Value& init = co->stack[(std::size_t)(base + a)];
    Value& lim = co->stack[(std::size_t)(base + a + 1)];
    Value& step = co->stack[(std::size_t)(base + a + 2)];
    if (init.t == Type::Int && step.t == Type::Int) {
        std::int64_t i0 = init.i, st = step.i, il;
        if (st == 0) Error("'for' step is zero");
        if (lim.t == Type::Int) il = lim.i;
        else {
            Value ln;
            if (!ToNumber(lim, ln)) Error(std::string("'for' limit must be a number"));
            if (ln.t == Type::Int) il = ln.i;
            else {
                double f = ln.n;
                if (std::isnan(f)) return false;
                double fl = st > 0 ? std::floor(f) : std::ceil(f);
                if (fl >= kTwo63) { if (st < 0) return false; il = INT64_MAX; }
                else if (fl < -kTwo63) { if (st > 0) return false; il = INT64_MIN; }
                else il = (std::int64_t)fl;
            }
        }
        if (st > 0 ? i0 > il : i0 < il) return false;
        std::uint64_t count = st > 0 ? ((std::uint64_t)il - (std::uint64_t)i0) / (std::uint64_t)st
                                     : ((std::uint64_t)i0 - (std::uint64_t)il) / ((std::uint64_t)(-(st + 1)) + 1u);
        lim = Value::Int((std::int64_t)count);
        return true;
    }
    Value i, l, s;
    if (!ToNumber(init, i)) Error("'for' initial value must be a number");
    if (!ToNumber(lim, l)) Error("'for' limit must be a number");
    if (!ToNumber(step, s)) Error("'for' step must be a number");
    double fi = i.AsNum(), fl = l.AsNum(), fs = s.AsNum();
    if (fs == 0) Error("'for' step is zero");
    init = Value::Num(fi);
    lim = Value::Num(fl);
    step = Value::Num(fs);
    return fs > 0 ? fi <= fl : fl <= fi;
}

bool State::ForLoop(int base, int a) {
    Coroutine* co = co_;
    Value& init = co->stack[(std::size_t)(base + a)];
    Value& lim = co->stack[(std::size_t)(base + a + 1)];
    Value& step = co->stack[(std::size_t)(base + a + 2)];
    if (init.t == Type::Int) {
        std::uint64_t count = (std::uint64_t)lim.i;
        if (count == 0) return false;
        lim.i = (std::int64_t)(count - 1);
        init.i = (std::int64_t)((std::uint64_t)init.i + (std::uint64_t)step.i);
        return true;
    }
    double v = init.n + step.n;
    if (step.n > 0 ? !(v <= lim.n) : !(lim.n <= v)) return false;
    init.n = v;
    return true;
}

// pcall of a Lua function runs inside the loop (a marker frame, no C++ recursion), so a
// coroutine can yield inside it. An error is caught here and handed to the nearest
// marker of this Execute call; markers of outer calls are theirs.
bool State::Execute() {
    Coroutine* const co = co_;
    std::size_t entryIdx = co->frames.empty() ? 0 : co->frames.size() - 1;
    while (entryIdx > 0 && !co->frames[entryIdx].entry) --entryIdx;
    for (;;) {
        try {
            return ExecuteLoop();
        } catch (const LuaError&) {
            if (!RecoverAtPcall(entryIdx)) throw;
        }
    }
}

bool State::RecoverAtPcall(std::size_t entryIdx) {
    Coroutine* co = co_;
    // frames above a nested call's entry frame are left over from it (it did not catch)
    std::size_t limit = co->frames.size();
    for (std::size_t i = entryIdx + 1; i < co->frames.size(); ++i)
        if (co->frames[i].entry) { limit = i; break; }
    std::size_t idx = 0;
    bool found = false;
    for (std::size_t i = limit; i-- > entryIdx + 1;)
        if (co->frames[i].pcallMark) { idx = i; found = true; break; }
    if (!found) return false;
    Frame m = co->frames[idx];
    Value err = errorValue;
    Unwind(idx, m.func + 1, err);   // drops the marker too; closes variables with the error
    errorValue = Value();
    CheckStack(2);
    co->stack[(std::size_t)m.func] = Value::Bool(false);
    co->stack[(std::size_t)(m.func + 1)] = err;
    co->top = m.func + 2;
    PostCall(m, m.func, 2);
    return true;
}

bool State::ExecuteLoop() {
    Coroutine* const co = co_;
newframe:
    if (co->frames.back().pcallMark) {
        // the protected function returned: true and its results
        Frame m = co->frames.back();
        co->frames.pop_back();
        co->stack[(std::size_t)m.func] = Value::Bool(true);
        PostCall(m, m.func, co->top - m.func);
        goto newframe;
    }
    LClosure* cl = co->frames.back().cl;
    Proto* p = cl->p;
    const Instr* code = p->code.data();
    const Value* K = p->k.data();
    const int base = co->frames.back().base;
#define S(i) co->stack[(std::size_t)(i)]
#define TOP co->top
#define FR co->frames.back()
    for (;;) {
        if (budget_ && ++used_ > budget_) throw LuaFatal{"script ran too long (instruction budget exceeded; endless loop?)"};
        const Instr in = code[FR.pc++];
        switch (in.op) {
            case NOP: break;
            case PUSHNIL: for (int i = 0; i < in.a; ++i) S(TOP++) = Value(); break;
            case PUSHTRUE: S(TOP++) = Value::Bool(true); break;
            case PUSHFALSE: S(TOP++) = Value::Bool(false); break;
            case PUSHK: S(TOP++) = K[in.a]; break;
            case PUSHINT: S(TOP++) = Value::Int(in.a); break;
            case POP: TOP -= in.a; break;
            case GETLOCAL: S(TOP) = S(base + in.a); ++TOP; break;
            case SETLOCAL: S(base + in.a) = S(--TOP); break;
            case GETBOX: S(TOP) = S(base + in.a).Bx()->v; ++TOP; break;
            case SETBOX: S(base + in.a).Bx()->v = S(--TOP); break;
            case MKBOX: {
                MaybeCollect();
                Box* b = Alloc<Box>();
                b->v = S(base + in.a);
                S(base + in.a) = Value::Obj(b);
                break;
            }
            case GETUP: S(TOP) = cl->up[(std::size_t)in.a]->v; ++TOP; break;
            case SETUP: cl->up[(std::size_t)in.a]->v = S(--TOP); break;
            case GETTABUPK: {
                Value t = cl->up[(std::size_t)in.a]->v;
                Value r;
                if (t.t == Type::Table) {
                    r = t.Tab()->GetStr(K[in.b].Str());
                    if (r.IsNil() && t.Tab()->meta) r = Index(t, K[in.b]);
                } else {
                    r = Index(t, K[in.b]);
                }
                S(TOP++) = r;
                break;
            }
            case SETTABUPK: {
                Value t = cl->up[(std::size_t)in.a]->v;
                Value v = S(TOP - 1);
                if (t.t == Type::Table && !t.Tab()->meta) t.Tab()->Set(*this, K[in.b], v);
                else SetIndex(t, K[in.b], v);
                --TOP;
                break;
            }
            case GETINDEX: {
                Value t = S(TOP - 2), k = S(TOP - 1);
                Value r;
                if (t.t == Type::Table) {
                    r = t.Tab()->Get(k);
                    if (r.IsNil() && t.Tab()->meta) r = Index(t, k);
                } else {
                    r = Index(t, k);
                }
                --TOP;
                S(TOP - 1) = r;
                break;
            }
            case GETFIELD: {
                Value t = S(TOP - 1);
                Value r;
                if (t.t == Type::Table) {
                    r = t.Tab()->GetStr(K[in.a].Str());
                    if (r.IsNil() && t.Tab()->meta) r = Index(t, K[in.a]);
                } else {
                    r = Index(t, K[in.a]);
                }
                S(TOP - 1) = r;
                break;
            }
            case SETINDEX: {
                Value t = S(TOP - 3);
                if (t.t == Type::Table && !t.Tab()->meta) t.Tab()->Set(*this, S(TOP - 2), S(TOP - 1));
                else SetIndex(t, S(TOP - 2), S(TOP - 1));
                TOP -= 3;
                break;
            }
            case SETFIELD: {
                Value t = S(TOP - 2);
                if (t.t == Type::Table && !t.Tab()->meta) t.Tab()->Set(*this, K[in.a], S(TOP - 1));
                else SetIndex(t, K[in.a], S(TOP - 1));
                TOP -= 2;
                break;
            }
            case SETINDEXAT: {
                Value t = S(base + in.a);
                if (t.t == Type::Table && !t.Tab()->meta) t.Tab()->Set(*this, S(base + in.a + 1), S(TOP - 1));
                else SetIndex(t, S(base + in.a + 1), S(TOP - 1));
                --TOP;
                break;
            }
            case SETFIELDAT: {
                Value t = S(base + in.a);
                if (t.t == Type::Table && !t.Tab()->meta) t.Tab()->Set(*this, K[in.b], S(TOP - 1));
                else SetIndex(t, K[in.b], S(TOP - 1));
                --TOP;
                break;
            }
            case SETKV: {
                S(base + in.a).Tab()->Set(*this, S(TOP - 2), S(TOP - 1));
                TOP -= 2;
                break;
            }
            case NEWTABLE: {
                MaybeCollect();
                S(TOP) = Value::Obj(NewTable((std::size_t)in.a, (std::size_t)in.b));
                ++TOP;
                break;
            }
            case SETLIST: {
                int tpos = base + in.a;
                int n = in.b < 0 ? TOP - tpos - 1 : in.b;
                Table* t = S(tpos).Tab();
                for (int i = 0; i < n; ++i) t->SetInt(*this, (std::int64_t)in.c + i, S(tpos + 1 + i));
                TOP = tpos + 1;
                break;
            }
            case SELF: {
                Value o = S(TOP - 1);
                Value f;
                if (o.t == Type::Table) {
                    f = o.Tab()->GetStr(K[in.a].Str());
                    if (f.IsNil() && o.Tab()->meta) f = Index(o, K[in.a]);
                } else {
                    f = Index(o, K[in.a]);
                }
                S(TOP - 1) = f;
                S(TOP) = o;
                ++TOP;
                break;
            }
            case ADD: case SUB: case MUL: {
                Value& a = S(TOP - 2);
                const Value& b = S(TOP - 1);
                if (a.t == Type::Int && b.t == Type::Int) {
                    std::uint64_t x = (std::uint64_t)a.i, y = (std::uint64_t)b.i;
                    a.i = (std::int64_t)(in.op == ADD ? x + y : in.op == SUB ? x - y : x * y);
                    --TOP;
                    break;
                }
                if (a.t == Type::Float && b.t == Type::Float) {
                    a.n = in.op == ADD ? a.n + b.n : in.op == SUB ? a.n - b.n : a.n * b.n;
                    --TOP;
                    break;
                }
                Value r = Arith(in.op, S(TOP - 2), S(TOP - 1));
                --TOP;
                S(TOP - 1) = r;
                break;
            }
            case MOD: case POW: case DIV: case IDIV: case BAND: case BOR: case BXOR: case SHL: case SHR: {
                Value r = Arith(in.op, S(TOP - 2), S(TOP - 1));
                --TOP;
                S(TOP - 1) = r;
                break;
            }
            case UNM: {
                Value v = S(TOP - 1);
                if (v.t == Type::Int) S(TOP - 1) = Value::Int((std::int64_t)(0ull - (std::uint64_t)v.i));
                else if (v.t == Type::Float) S(TOP - 1) = Value::Num(-v.n);
                else {
                    Value n;
                    if (ToNumber(v, n)) S(TOP - 1) = n.t == Type::Int ? Value::Int((std::int64_t)(0ull - (std::uint64_t)n.i)) : Value::Num(-n.n);
                    else {
                        Value tm = GetTM(v, TM_UNM);
                        if (tm.IsNil()) OpError("perform arithmetic on", v, 0);
                        Value r = CallMeta2(tm, v, v);
                        S(TOP - 1) = r;
                    }
                }
                break;
            }
            case BNOT: {
                Value v = S(TOP - 1);
                std::int64_t i;
                if (v.IsNumber() && ToInteger(v, i)) S(TOP - 1) = Value::Int(~i);
                else {
                    Value tm = GetTM(v, TM_BNOT);
                    if (tm.IsNil()) {
                        if (v.IsNumber()) Error("number has no integer representation");
                        OpError("perform bitwise operation on", v, 0);
                    }
                    Value r = CallMeta2(tm, v, v);
                    S(TOP - 1) = r;
                }
                break;
            }
            case NOT: S(TOP - 1) = Value::Bool(!S(TOP - 1).Truthy()); break;
            case LEN: {
                Value v = S(TOP - 1);
                if (v.t == Type::String) S(TOP - 1) = Value::Int((std::int64_t)v.Str()->s.size());
                else if (v.t == Type::Table && !v.Tab()->meta) S(TOP - 1) = Value::Int(v.Tab()->Length());
                else { Value r = Length(v); S(TOP - 1) = r; }
                break;
            }
            case CONCAT: {
                MaybeCollect();
                int n = in.a;
                bool simple = true;
                std::size_t total = 0;
                for (int i = TOP - n; i < TOP; ++i) {
                    const Value& v = S(i);
                    if (v.IsString()) total += v.Str()->s.size();
                    else if (v.IsNumber()) total += 24;
                    else { simple = false; break; }
                }
                if (simple) {
                    if (total > (std::size_t(1) << 30)) Error("string length overflow");
                    std::string s;
                    s.reserve(total);
                    for (int i = TOP - n; i < TOP; ++i) {
                        const Value& v = S(i);
                        if (v.IsString()) s += v.Str()->s; else s += NumberToString(v);
                    }
                    Charge(s.size() / 64);
                    TOP -= n - 1;
                    S(TOP - 1) = Value::Obj(NewString(s));
                } else {
                    for (int k = n; k > 1; --k) {
                        Value r = Concat(S(TOP - 2), S(TOP - 1));
                        --TOP;
                        S(TOP - 1) = r;
                    }
                }
                break;
            }
            case EQ: case NE: {
                Value a = S(TOP - 2), b = S(TOP - 1);
                bool r = (a.t == b.t && a.t != Type::Table && a.t != Type::Userdata) ? RawEquals(a, b) : Equals(a, b);
                --TOP;
                S(TOP - 1) = Value::Bool(in.op == EQ ? r : !r);
                break;
            }
            case LT: case LE: case GT: case GE: {
                Value a = S(TOP - 2), b = S(TOP - 1);
                bool r;
                if (a.t == Type::Int && b.t == Type::Int) {
                    r = in.op == LT ? a.i < b.i : in.op == LE ? a.i <= b.i : in.op == GT ? a.i > b.i : a.i >= b.i;
                } else {
                    switch (in.op) {
                        case LT: r = LessThan(a, b); break;
                        case LE: r = LessEqual(a, b); break;
                        case GT: r = LessThan(b, a); break;
                        default: r = LessEqual(b, a); break;
                    }
                }
                --TOP;
                S(TOP - 1) = Value::Bool(r);
                break;
            }
            case JMP: {
                if (in.b >= 0) {
                    int nt = base + in.b;
                    if (in.c >= 0 && !co->tbc.empty() && co->tbc.back() >= base + in.c) CloseTbc(base + in.c, Value(), false);
                    while (TOP < nt) S(TOP++) = Value();
                    TOP = nt;
                }
                FR.pc = in.a;
                break;
            }
            case JMPIFNOT: if (!S(--TOP).Truthy()) FR.pc = in.a; break;
            case JMPIF: if (S(--TOP).Truthy()) FR.pc = in.a; break;
            case ANDJ: if (!S(TOP - 1).Truthy()) FR.pc = in.a; else --TOP; break;
            case ORJ: if (S(TOP - 1).Truthy()) FR.pc = in.a; else --TOP; break;
            case CALL: {
                MaybeCollect();
                int func = base + in.a;
                int nargs = in.b < 0 ? TOP - func - 1 : in.b;
                TOP = func + 1 + nargs;
                for (;;) {
                    Value f = S(func);
                    if (f.t == Type::LFunc) {
                        PushLuaFrame(func, nargs, in.c, false);
                        goto newframe;
                    }
                    if (f.t == Type::NFunc && f.NFn()->fn == PCallNative && nargs >= 1 && S(func + 1).t == Type::LFunc) {
                        if (co->frames.size() >= 200000) Error("stack overflow");
                        Frame m;
                        m.ncl = f.NFn();
                        m.func = func;
                        m.base = func + 1;
                        m.nres = in.c;
                        m.pcallMark = true;
                        m.tbcMark = (int)co->tbc.size();
                        co->frames.push_back(m);
                        PushLuaFrame(func + 1, nargs - 1, -1, false);
                        goto newframe;
                    }
                    if (f.t == Type::NFunc) {
                        if (CallNative(func, nargs, in.c) == kYieldResult) {
                            co->yieldFunc = func;
                            co->yieldNres = in.c;
                            return true;
                        }
                        break;
                    }
                    nargs = CallTM(func, nargs);
                }
                break;
            }
            case TAILCALL: {
                MaybeCollect();
                int func = base + in.a;
                int nargs = in.b < 0 ? TOP - func - 1 : in.b;
                TOP = func + 1 + nargs;
                while (S(func).t != Type::LFunc && S(func).t != Type::NFunc) nargs = CallTM(func, nargs);
                if (S(func).t == Type::NFunc && S(func).NFn()->fn == PCallNative && nargs >= 1 && S(func + 1).t == Type::LFunc) {
                    // return pcall(f, ...): a protected call inside the loop; the RETURN after it returns the results
                    if (co->frames.size() >= 200000) Error("stack overflow");
                    Frame m;
                    m.ncl = S(func).NFn();
                    m.func = func;
                    m.base = func + 1;
                    m.nres = -1;
                    m.pcallMark = true;
                    m.tbcMark = (int)co->tbc.size();
                    co->frames.push_back(m);
                    PushLuaFrame(func + 1, nargs - 1, -1, false);
                    goto newframe;
                }
                if (S(func).t == Type::NFunc) {
                    if (CallNative(func, nargs, -1) == kYieldResult) {
                        co->yieldFunc = func;
                        co->yieldNres = -1;
                        return true;
                    }
                    break;   // the RETURN that follows returns the results
                }
                // reuse this frame: move the function and arguments down to our own function slot
                Frame old = FR;
                int dst = old.func;
                for (int i = 0; i <= nargs; ++i) S(dst + i) = S(func + i);
                TOP = dst + 1 + nargs;
                co->frames.pop_back();
                PushLuaFrame(dst, nargs, old.nres, old.entry);
                FR.tail = true;
                goto newframe;
            }
            case RETURN: {
                int first = base + in.a;
                int n = in.b < 0 ? TOP - first : in.b;
                if ((int)co->tbc.size() > FR.tbcMark) {
                    // close pending variables; the results above them stay put
                    TOP = first + n;
                    CloseTbc(base, Value(), false);
                }
                Frame done = FR;
                co->frames.pop_back();
                PostCall(done, first, n);
                if (done.entry) return false;
                goto newframe;
            }
            case CLOSURE: {
                MaybeCollect();
                Proto* np = p->protos[(std::size_t)in.a];
                LClosure* c = NewLClosure(np);
                for (const UpvalDesc& d : np->upvals) {
                    if (d.fromLocal) {
                        Value& slot = S(base + d.index);
                        if (slot.t != Type::Box) {   // defensive: never expected
                            Box* b = Alloc<Box>();
                            b->v = slot;
                            slot = Value::Obj(b);
                        }
                        c->up.push_back(slot.Bx());
                    } else {
                        c->up.push_back(cl->up[(std::size_t)d.index]);
                    }
                }
                S(TOP) = Value::Obj(c);
                ++TOP;
                break;
            }
            case VARARG: {
                int n = in.a < 0 ? FR.vaCount : in.a;
                CheckStack(n);
                int vs = FR.vaStart, vc = FR.vaCount;
                for (int i = 0; i < n; ++i) S(TOP++) = i < vc ? S(vs + i) : Value();
                break;
            }
            case FORPREP: {
                if (!ForPrep(base, in.a)) { FR.pc = in.b; break; }
                S(TOP) = S(base + in.a);
                ++TOP;
                break;
            }
            case FORLOOP: {
                if (ForLoop(base, in.a)) {
                    S(base + in.a + 3) = S(base + in.a);
                    TOP = base + in.a + 4;
                    FR.pc = in.b;
                } else {
                    TOP = base + in.a + 3;
                }
                break;
            }
            case TFORTEST: {
                if (S(base + in.a + 4).IsNil()) { TOP = base + in.a + 4; FR.pc = in.b; }
                else S(base + in.a + 2) = S(base + in.a + 4);
                break;
            }
            case TBC: {
                Value v = S(base + in.a);
                if (v.t == Type::Nil || (v.t == Type::Bool && !v.b)) break;
                if (GetTM(v, TM_CLOSE).IsNil()) Error("variable got a non-closable value");
                co->tbc.push_back(base + in.a);
                break;
            }
            case CLOSE: CloseTbc(base + in.a, Value(), false); break;
            default: Error("bad opcode");
        }
    }
#undef S
#undef TOP
#undef FR
}

} // namespace okay::lua
