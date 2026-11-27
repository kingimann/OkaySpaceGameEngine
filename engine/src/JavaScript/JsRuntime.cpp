// okay::js runtime: values, objects, properties, conversions, errors and the
// mark-and-sweep collector. Written from ECMA-262 chapter 6 (data types and
// values: 6.1.4 strings as UTF-16 code units, 6.1.6.1 Number with
// Number::toString in 6.1.6.1.20, 6.1.7 objects and property attributes),
// chapter 7 (abstract operations: 7.1 type conversion, 7.2 testing and
// comparison, 7.3 operations on objects), chapter 10.1 (ordinary object internal
// methods), 10.4.2 (array exotic objects), 10.4.3 (string exotic objects) and
// 20.5 (error objects).
#include "JsInternal.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <iterator>
#include <unordered_map>

namespace okay::js {

// ============================================================ equality (7.2.10 - 7.2.16)
bool SameValueZero(const Value& a, const Value& b) {
    if (a.t != b.t) return false;
    switch (a.t) {
        case Tag::Undefined: case Tag::Null: case Tag::Empty: return true;
        case Tag::Bool: return a.b == b.b;
        case Tag::Number: return a.n == b.n || (std::isnan(a.n) && std::isnan(b.n));
        case Tag::String: return a.p == b.p || a.S()->s == b.S()->s;
        case Tag::BigInt: return a.p == b.p || BigEquals(a, b);
        default: return a.p == b.p;
    }
}
bool SameValue(const Value& a, const Value& b) {
    if (a.IsNum() && b.IsNum() && a.n == 0 && b.n == 0) return std::signbit(a.n) == std::signbit(b.n);
    return SameValueZero(a, b);
}
bool StrictEquals(const Value& a, const Value& b) {
    if (a.IsNum() && b.IsNum()) return a.n == b.n;
    return SameValueZero(a, b);
}

std::int64_t String::ArrayIndex() const {
    if (index != -2) return index;
    index = -1;
    if (s.empty() || s.size() > 10) return -1;
    if (s[0] == u'0' && s.size() > 1) return -1;
    std::uint64_t v = 0;
    for (char16_t c : s) {
        if (c < u'0' || c > u'9') return -1;
        v = v * 10 + (std::uint64_t)(c - u'0');
    }
    if (v >= 4294967295ull) return -1;
    index = (std::int64_t)v;
    return index;
}

std::size_t KeyHash::operator()(const Value& v) const {
    switch (v.t) {
        case Tag::Number: {
            double d = v.n;
            if (d == 0) d = 0;
            if (std::isnan(d)) return 0x7ff8;
            std::uint64_t bits;
            std::memcpy(&bits, &d, 8);
            return (std::size_t)(bits ^ (bits >> 29));
        }
        case Tag::String: {
            String* s = v.S();
            if (!s->hash) {   // our own: a polynomial over the code units, then a final mix
                std::uint64_t h = s->s.size();
                for (char16_t c : s->s) h = h * 131 + c;
                h ^= h >> 29;
                h *= 0x9E3779B97F4A7C15ull;
                h ^= h >> 32;
                s->hash = (std::uint32_t)h ? (std::uint32_t)h : 1;
            }
            return s->hash;
        }
        case Tag::BigInt: return BigHash(v);
        case Tag::Bool: return v.b ? 3 : 5;
        case Tag::Undefined: return 7;
        case Tag::Null: return 11;
        default: return std::hash<const void*>()(v.p);
    }
}

// ============================================================ tracing
void Symbol::Trace(Runtime& rt) { if (desc) rt.Mark(desc); }
void Cell::Trace(Runtime& rt) { rt.MarkValue(v); }
void Object::Trace(Runtime& rt) {
    if (proto) rt.Mark(proto);
    for (const Prop& p : props) {
        rt.Mark(p.key);
        rt.MarkValue(p.v);
        if (p.get) rt.Mark(p.get);
        if (p.set) rt.Mark(p.set);
    }
}
std::size_t Object::Bytes() const { return sizeof(Object) + props.capacity() * sizeof(Prop) + (index ? index->size() * 32 : 0); }
void ArrayObj::Trace(Runtime& rt) {
    Object::Trace(rt);
    for (const Value& v : elems) rt.MarkValue(v);
}
void BoxObj::Trace(Runtime& rt) {
    Object::Trace(rt);
    rt.MarkValue(prim);
}
void NativeFunc::Trace(Runtime& rt) {
    Object::Trace(rt);
    if (name) rt.Mark(name);
    for (const Value& v : slots) rt.MarkValue(v);
}
void Closure::Trace(Runtime& rt) {
    Object::Trace(rt);
    if (code) rt.Mark(code);
    for (Cell* c : ups) if (c) rt.Mark(c);
    if (home) rt.Mark(home);
    if (fields) rt.Mark(fields);
}
void BoundFunc::Trace(Runtime& rt) {
    Object::Trace(rt);
    if (target) rt.Mark(target);
    rt.MarkValue(thisv);
    for (const Value& v : args) rt.MarkValue(v);
}
void Proto::Trace(Runtime& rt) {
    for (const Value& v : consts) rt.MarkValue(v);
    for (Proto* p : protos) rt.Mark(p);
    if (name) rt.Mark(name);
}
void GenObj::Trace(Runtime& rt) {
    Object::Trace(rt);
    for (const Value& v : saved) rt.MarkValue(v);
    if (newTarget) rt.Mark(newTarget);
    rt.MarkValue(extra);
    if (promise) rt.Mark(promise);
}
void PromiseObj::Trace(Runtime& rt) {
    Object::Trace(rt);
    rt.MarkValue(result);
    for (auto* list : {&onFulfill, &onReject})
        for (const Reaction& r : *list) {
            rt.MarkValue(r.handler);
            if (r.derived) rt.Mark(r.derived);
            if (r.awaiter) rt.Mark(r.awaiter);
        }
}
void WeakObj::Trace(Runtime& rt) {
    Object::Trace(rt);   // keys and targets are weak; WeakMap values are marked by Collect's ephemeron pass
    for (const Cell& c : cells) rt.MarkValue(c.held);
    rt.MarkValue(cleanup);
}
void MapObj::Trace(Runtime& rt) {
    Object::Trace(rt);
    for (const Entry& e : entries)
        if (e.live) {
            rt.MarkValue(e.k);
            rt.MarkValue(e.v);
        }
}
void MapObj::Compact() {
    if (iterators) return;
    std::vector<Entry> keep;
    keep.reserve(live);
    for (const Entry& e : entries) if (e.live) keep.push_back(e);
    entries.swap(keep);
    index.clear();
    for (std::uint32_t i = 0; i < entries.size(); ++i) index[entries[i].k] = i;
}
void IterObj::Trace(Runtime& rt) {
    Object::Trace(rt);
    rt.MarkValue(target);
}
void IterRec::Trace(Runtime& rt) {
    Object::Trace(rt);
    rt.MarkValue(iter);
    rt.MarkValue(next);
}
void ForInObj::Trace(Runtime& rt) {
    Object::Trace(rt);
    rt.MarkValue(obj);
    for (GcObj* k : keys) rt.Mark(k);
}

// ============================================================ own property storage
Prop* Object::FindRaw(GcObj* key) {
    if (index) {
        auto it = index->find(key);
        return it == index->end() ? nullptr : &props[it->second];
    }
    for (Prop& p : props)
        if (p.key == key) return &p;
    return nullptr;
}
Prop* Object::AddRaw(GcObj* key, const Value& v, std::uint8_t flags) {
    Prop p;
    p.key = key;
    p.v = v;
    p.flags = flags;
    props.push_back(p);
    if (index) (*index)[key] = (std::uint32_t)props.size() - 1;
    else if (props.size() > 12) {
        index = std::make_unique<std::unordered_map<GcObj*, std::uint32_t>>();
        for (std::uint32_t i = 0; i < props.size(); ++i) (*index)[props[i].key] = i;
    }
    return &props.back();
}
void Object::RemoveRaw(GcObj* key) {
    for (std::size_t i = 0; i < props.size(); ++i)
        if (props[i].key == key) {
            props.erase(props.begin() + (std::ptrdiff_t)i);
            if (index) {
                index->erase(key);
                for (auto& kv : *index)
                    if (kv.second > i) --kv.second;
            }
            return;
        }
}

// ============================================================ the runtime
void Runtime::Adopt(GcObj* o) {
    o->gcNext = gcList_;
    gcList_ = o;
    bytes_ += o->Bytes();
}

Runtime::Runtime() : vm(std::make_unique<Impl>()) {
    vm->stack = static_cast<Value*>(std::calloc(Impl::kStackValues, sizeof(Value)));
    vm->frames = static_cast<Frame*>(std::calloc((std::size_t)Impl::kMaxFrames, sizeof(Frame)));
    if (!vm->stack || !vm->frames) std::abort();
    vm->sp = vm->stack;
    vm->stackEnd = vm->stack + Impl::kStackValues - 256;
    atoms = std::make_unique<Atoms>();
    InitIntrinsics();
    scratch_.clear();
}

Runtime::~Runtime() {
    GcObj* o = gcList_;
    while (o) {
        GcObj* n = o->gcNext;
        if (o->kind == Kind::Object && static_cast<Object*>(o)->cls == Cls::Host && onHostFree) onHostFree(static_cast<HostObj*>(o));
        delete o;
        o = n;
    }
    std::free(vm->stack);
    std::free(vm->frames);
}

String* Runtime::NewString(std::u16string s) {
    if (s.size() > (std::size_t)(1u << 28)) ThrowRange("a string can be at most 2^28 characters long");
    String* o = Alloc<String>(s.size() * 2);
    o->s = std::move(s);
    return o;
}
String* Runtime::NewStringUtf8(std::string_view s) { return NewString(Utf8To16(s)); }
String* Runtime::NewStringAscii(const char* s) { return NewString(AsciiTo16(s)); }

String* Runtime::Intern(const std::u16string& s) {
    auto it = atomTable_.find(std::u16string_view(s));
    if (it != atomTable_.end()) {
        scratch_.push_back(Value::Str(it->second));
        return it->second;
    }
    String* o = NewString(s);
    o->atom = true;
    atomTable_.emplace(std::u16string_view(o->s), o);
    return o;
}
String* Runtime::Intern(const char* ascii) { return Intern(AsciiTo16(ascii)); }
String* Runtime::InternStr(String* s) {
    if (s->atom) return s;
    auto it = atomTable_.find(std::u16string_view(s->s));
    if (it != atomTable_.end()) {
        scratch_.push_back(Value::Str(it->second));
        return it->second;
    }
    s->atom = true;
    atomTable_.emplace(std::u16string_view(s->s), s);
    return s;
}

GcObj* Runtime::IndexKey(std::uint32_t i) {
    if (i < indexAtoms_.size() && indexAtoms_[i]) return indexAtoms_[i];
    String* s = Intern(NumberToU16((double)i));
    s->index = i;
    return s;
}

Symbol* Runtime::NewSymbol(String* desc) {
    Symbol* s = Alloc<Symbol>();
    s->desc = desc;
    return s;
}

Object* Runtime::NewObject(Object* proto) {
    Object* o = Alloc<Object>();
    o->cls = Cls::Ordinary;
    o->proto = proto;
    return o;
}
Object* Runtime::NewObject() { return NewObject(objectProto); }

ArrayObj* Runtime::NewArray(std::size_t reserve) {
    if (reserve > (1u << 26)) ThrowRange("this array would be too big");
    ArrayObj* a = Alloc<ArrayObj>(reserve * sizeof(Value));
    a->proto = arrayProto;
    if (reserve) a->elems.reserve(reserve);
    return a;
}

NativeFunc* Runtime::NewNative(NativeFn fn, const char* name, int length, std::vector<Value> slots) {
    NativeFunc* f = Alloc<NativeFunc>();
    f->proto = functionProto;
    f->fn = fn;
    f->name = Intern(name);
    f->length = length;
    f->slots = std::move(slots);
    f->lazy = true;
    return f;
}

HostObj* Runtime::NewHost(std::uint32_t hostClass, Object* proto) {
    HostObj* h = Alloc<HostObj>();
    h->hostClass = hostClass;
    h->proto = proto ? proto : objectProto;
    return h;
}

// ============================================================ errors
Object* Runtime::NewError(ErrorType type, const std::string& msg) {
    Charge(64);   // the stack text costs more than one instruction
    ErrorObj* e = Alloc<ErrorObj>();
    e->proto = errorProtos[(int)type];
    e->where = Where();
    if (!msg.empty() || type != ErrorType::Error) e->AddRaw(atoms->message, Value::Str(NewStringUtf8(msg)), kHidden);
    static const char* const names[] = {"Error", "TypeError", "RangeError", "SyntaxError", "ReferenceError",
                                        "EvalError", "URIError", "AggregateError"};
    std::string st = std::string(names[(int)type]) + (msg.empty() ? "" : ": " + msg) + "\n" + StackText();
    e->AddRaw(atoms->stack, Value::Str(NewStringUtf8(st)), kHidden);
    return e;
}

void Runtime::Throw(const Value& v) {
    exception = v;
    throw JsThrow{};
}
void Runtime::ThrowError(ErrorType t, const std::string& msg) { Throw(Value::Obj(NewError(t, msg))); }
static std::string WithWhere(Runtime& rt, const std::string& why) {
    std::string w = rt.Where();
    return w.empty() ? why : w + ": " + why;
}
void Runtime::Terminate(const std::string& why) { throw JsTerminate{WithWhere(*this, why)}; }
void Runtime::MemoryFull() {
    // the host (a browser dispatching an event, the engine setting a global) allocates outside any call: a throw
    // there would escape into code that does not expect one, so the stop waits for the next script call
    if (vm && vm->nframes == 0 && vm->nativeDepth == 0) return;
    bytes_ = memoryLimit;   // stays over the limit until a collection measures again
    throw JsTerminate{WithWhere(*this, "the script used more memory than allowed (" + std::to_string(memoryLimit >> 20) + " MB)")};
}
void Runtime::BudgetOut() {
    left_ = 1;
    throw JsTerminate{WithWhere(*this, "the script ran past its instruction budget (an endless loop?)")};
}
void Runtime::Charge(std::uint64_t n) {
    if (n >= left_) BudgetOut();
    left_ -= n;
}

static int LineOf(const Frame& f) {
    if (!f.fn || !f.pc) return 0;
    const Proto* p = f.fn->code;
    std::ptrdiff_t i = f.pc - p->code.data();
    if (i < 0 || i >= (std::ptrdiff_t)p->lines.size()) return p->line;
    return p->lines[(std::size_t)i];
}

std::string Runtime::Where() {
    if (vm->nframes <= 0) return "";
    const Frame& f = vm->frames[vm->nframes - 1];
    if (!f.fn) return "";
    return f.fn->code->chunk + ":" + std::to_string(LineOf(f));
}

std::string Runtime::StackText() {
    std::string out;
    int shown = 0;
    for (int i = vm->nframes - 1; i >= 0 && shown < 12; --i, ++shown) {
        const Frame& f = vm->frames[i];
        if (!f.fn) continue;
        const Proto* p = f.fn->code;
        std::string name = p->name ? Utf16To8(p->name->s) : std::string();
        if (name.empty()) name = "<anonymous>";
        out += "  in " + name + " at " + p->chunk + ":" + std::to_string(LineOf(f)) + "\n";
    }
    return out;
}

std::string Runtime::ExceptionText(const Value& exc) {
    std::string where, text;
    if (exc.IsObj()) {
        Object* o = exc.O();
        if (o->cls == Cls::Error) where = static_cast<ErrorObj*>(o)->where;
        Prop* nm = nullptr;
        for (Object* q = o; q && !nm; q = q->proto) nm = q->FindRaw(atoms->name);
        Prop* msg = nullptr;
        for (Object* q = o; q && !msg; q = q->proto) msg = q->FindRaw(atoms->message);
        if (o->cls == Cls::Error || (nm && msg)) {
            std::string n = nm && nm->v.IsStr() ? Utf16To8(nm->v.S()->s) : "Error";
            std::string m = msg && msg->v.IsStr() ? Utf16To8(msg->v.S()->s) : "";
            text = m.empty() ? n : n + ": " + m;
        } else {
            try { text = "uncaught " + Display(exc); } catch (...) { text = "uncaught exception"; }
        }
    } else {
        try { text = "uncaught " + Display(exc); } catch (...) { text = "uncaught exception"; }
    }
    if (where.empty()) where = throwWhere;
    return where.empty() ? text : where + ": " + text;
}

// ============================================================ conversions (7.1)
bool Runtime::ToBoolean(const Value& v) const {
    switch (v.t) {
        case Tag::Undefined: case Tag::Null: case Tag::Empty: return false;
        case Tag::Bool: return v.b;
        case Tag::Number: return !(v.n == 0 || std::isnan(v.n));
        case Tag::String: return !v.S()->s.empty();
        case Tag::BigInt: return !BigIsZero(v);
        default: return true;
    }
}

bool IsJsSpace(char16_t c) {
    switch (c) {
        case ' ': case '\t': case '\n': case '\r': case 0x0B: case 0x0C: case 0xA0: case 0xFEFF: case 0x1680:
        case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000:
            return true;
        default: return c >= 0x2000 && c <= 0x200A;
    }
}

// Simple case mapping for toLowerCase / toUpperCase and the RegExp i flag (22.1.3.28 / 22.1.3.30 without the
// whole Unicode table): ASCII, Latin-1, Latin Extended-A, Greek and Cyrillic.
char16_t JsLower(char16_t c) {
    if (c >= 'A' && c <= 'Z') return (char16_t)(c + 32);
    if (c < 0x80) return c;
    if ((c >= 0xC0 && c <= 0xDE && c != 0xD7)) return (char16_t)(c + 32);
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return (char16_t)(c + 32);
    if (c >= 0x410 && c <= 0x42F) return (char16_t)(c + 32);
    if (c >= 0x400 && c <= 0x40F) return (char16_t)(c + 80);
    if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F) {
        bool evenUpper = (c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177);
        if (evenUpper) return (c % 2 == 0) ? (char16_t)(c + 1) : c;
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c % 2 == 1) ? (char16_t)(c + 1) : c;
    }
    return c;
}
char16_t JsUpper(char16_t c) {
    if (c >= 'a' && c <= 'z') return (char16_t)(c - 32);
    if (c < 0x80) return c;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return (char16_t)(c - 32);
    if (c == 0xFF) return 0x178;
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return (char16_t)(c - 32);
    if (c == 0x3C2) return 0x3A3;
    if (c >= 0x430 && c <= 0x44F) return (char16_t)(c - 32);
    if (c >= 0x450 && c <= 0x45F) return (char16_t)(c - 80);
    if (c >= 0x100 && c <= 0x17F) {
        bool evenUpper = (c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177);
        if (evenUpper) return (c % 2 == 1) ? (char16_t)(c - 1) : c;
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c % 2 == 0) ? (char16_t)(c - 1) : c;
    }
    return c;
}

// 7.1.4.1.1 StringToNumber: the StringNumericLiteral grammar, checked by hand, then strtod on the ASCII text.
double Runtime::StringToNumber(const std::u16string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && IsJsSpace(s[a])) ++a;
    while (b > a && IsJsSpace(s[b - 1])) --b;
    if (a == b) return 0;
    std::string t;
    for (std::size_t i = a; i < b; ++i) {
        if (s[i] > 127) return NAN;
        t += (char)s[i];
    }
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X' || t[1] == 'o' || t[1] == 'O' || t[1] == 'b' || t[1] == 'B')) {
        int base = (t[1] | 0x20) == 'x' ? 16 : (t[1] | 0x20) == 'o' ? 8 : 2;
        double v = 0;
        for (std::size_t i = 2; i < t.size(); ++i) {
            char c = t[i];
            int d = c >= '0' && c <= '9' ? c - '0' : (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ? (c | 0x20) - 'a' + 10 : 99;
            if (d >= base) return NAN;
            v = v * base + d;
        }
        return v;
    }
    std::size_t i = 0;
    bool neg = false;
    if (t[i] == '+' || t[i] == '-') { neg = t[i] == '-'; ++i; }
    if (t.compare(i, std::string::npos, "Infinity") == 0) return neg ? -INFINITY : INFINITY;
    std::size_t digits = 0;
    while (i < t.size() && t[i] >= '0' && t[i] <= '9') { ++i; ++digits; }
    if (i < t.size() && t[i] == '.') {
        ++i;
        while (i < t.size() && t[i] >= '0' && t[i] <= '9') { ++i; ++digits; }
    }
    if (digits == 0) return NAN;
    if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
        ++i;
        if (i < t.size() && (t[i] == '+' || t[i] == '-')) ++i;
        std::size_t ed = 0;
        while (i < t.size() && t[i] >= '0' && t[i] <= '9') { ++i; ++ed; }
        if (ed == 0) return NAN;
    }
    if (i != t.size()) return NAN;
    return std::strtod(t.c_str(), nullptr);
}

// 6.1.6.1.20 Number::toString(x) with radix 10: the shortest digit string that reads back as x.
std::string Runtime::NumberToStdString(double x) {
    if (std::isnan(x)) return "NaN";
    if (x == 0) return "0";
    if (x < 0) return "-" + NumberToStdString(-x);
    if (std::isinf(x)) return "Infinity";
    if (x < 9007199254740992.0 && x == std::floor(x)) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%lld", (long long)x);
        return buf;
    }
    char buf[40];
    int k = 0, e = 0;
    std::string digits;
    for (int p = 1; p <= 17; ++p) {
        std::snprintf(buf, sizeof buf, "%.*e", p - 1, x);
        if (std::strtod(buf, nullptr) == x || p == 17) {
            const char* ep = std::strchr(buf, 'e');
            for (const char* c = buf; c < ep; ++c)
                if (*c >= '0' && *c <= '9') digits += *c;
            e = std::atoi(ep + 1);
            break;
        }
    }
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    k = (int)digits.size();
    int n = e + 1;   // x = 0.digits * 10^n
    std::string out;
    if (k <= n && n <= 21) {
        out = digits + std::string((std::size_t)(n - k), '0');
    } else if (0 < n && n <= 21) {
        out = digits.substr(0, (std::size_t)n) + "." + digits.substr((std::size_t)n);
    } else if (-6 < n && n <= 0) {
        out = "0." + std::string((std::size_t)(-n), '0') + digits;
    } else {
        int ex = n - 1;
        std::string es = (ex < 0 ? "-" : "+") + std::to_string(ex < 0 ? -ex : ex);
        if (k == 1) out = digits + "e" + es;
        else out = digits.substr(0, 1) + "." + digits.substr(1) + "e" + es;
    }
    return out;
}
std::u16string Runtime::NumberToU16(double d) { return AsciiTo16(NumberToStdString(d).c_str()); }

double Runtime::ToNumber(const Value& v) {
    switch (v.t) {
        case Tag::Number: return v.n;
        case Tag::Undefined: case Tag::Empty: return NAN;
        case Tag::Null: return 0;
        case Tag::Bool: return v.b ? 1 : 0;
        case Tag::String: return StringToNumber(v.S()->s);
        case Tag::Symbol: ThrowType("a Symbol can not be turned into a number");
        case Tag::BigInt: ThrowType("a BigInt can not be mixed with Numbers here; use Number(x) or BigInt(x)");
        case Tag::Object: {
            Value p = ToPrimitive(v, 1);
            return ToNumber(p);
        }
        default: return NAN;
    }
}

// 7.1.3 ToNumeric: a primitive's Number, or the BigInt itself
Value Runtime::ToNumeric(const Value& v) {
    if (v.IsBig()) return v;
    if (v.IsObj()) {
        Value p = ToPrimitive(v, 1);
        if (p.IsBig()) return p;
        return Value::Num(ToNumber(p));
    }
    return Value::Num(ToNumber(v));
}

double Runtime::ToIntegerOrInfinity(const Value& v) {
    double d = ToNumber(v);
    if (std::isnan(d) || d == 0) return 0;
    if (std::isinf(d)) return d;
    return std::trunc(d);
}

static std::uint32_t ToUint32D(double d) {
    if (!std::isfinite(d)) return 0;
    d = std::trunc(d);
    double m = std::fmod(d, 4294967296.0);
    if (m < 0) m += 4294967296.0;
    return (std::uint32_t)m;
}
std::int32_t Runtime::ToInt32(const Value& v) {
    if (v.IsNum() && v.n >= -2147483648.0 && v.n <= 2147483647.0) return (std::int32_t)v.n;
    return (std::int32_t)ToUint32D(ToNumber(v));
}
std::uint32_t Runtime::ToUint32(const Value& v) { return ToUint32D(ToNumber(v)); }

String* Runtime::ToString(const Value& v) {
    switch (v.t) {
        case Tag::String: return v.S();
        case Tag::Number: {
            if (v.n >= 0 && v.n < 1024 && v.n == std::floor(v.n)) return static_cast<String*>(IndexKey((std::uint32_t)v.n));
            return NewString(NumberToU16(v.n));
        }
        case Tag::Undefined: case Tag::Empty: return atoms->undefined;
        case Tag::Null: return atoms->null_;
        case Tag::Bool: return v.b ? atoms->true_ : atoms->false_;
        case Tag::Symbol: ThrowType("a Symbol can not be turned into a string automatically (use String(sym))");
        case Tag::BigInt: return NewString(BigToString(*this, v, 10));
        case Tag::Object: {
            Value p = ToPrimitive(v, 2);
            Keep(p);
            return ToString(p);
        }
        default: return atoms->empty;
    }
}
std::string Runtime::ToUtf8(const Value& v) { return Utf16To8(ToString(v)->s); }

// 7.1.1 ToPrimitive and 7.1.1.1 OrdinaryToPrimitive. hint: 0 default, 1 number, 2 string
Value Runtime::ToPrimitive(const Value& v, int hint) {
    if (!v.IsObj()) return v;
    Value exotic = Get(v, symToPrimitive);
    if (!exotic.IsNullish()) {
        if (!IsCallable(exotic)) ThrowType("Symbol.toPrimitive is not a function");
        Value h = Value::Str(hint == 0 ? atoms->default_ : hint == 1 ? atoms->number : atoms->string);
        Value r = Call(exotic, v, &h, 1);
        if (r.IsObj()) ThrowType("Symbol.toPrimitive returned an object");
        return r;
    }
    GcObj* order[2] = {atoms->valueOf, atoms->toString};
    if (hint == 2) std::swap(order[0], order[1]);
    for (GcObj* k : order) {
        Value f = Get(v, k);
        if (IsCallable(f)) {
            Value r = Call(f, v, nullptr, 0);
            if (!r.IsObj()) return r;
        }
    }
    ThrowType("this object can not be turned into a primitive value");
}

Object* Runtime::ToObject(const Value& v) {
    switch (v.t) {
        case Tag::Object: return v.O();
        case Tag::Undefined: case Tag::Null: case Tag::Empty:
            ThrowType(std::string("can not turn ") + (v.IsNull() ? "null" : "undefined") + " into an object");
        default: break;
    }
    Cls c = v.IsBool() ? Cls::BoolBox : v.IsNum() ? Cls::NumberBox : v.IsStr() ? Cls::StringBox : v.IsBig() ? Cls::BigIntBox : Cls::SymbolBox;
    BoxObj* b = Alloc<BoxObj>();
    b->cls = c;
    b->prim = v;
    b->proto = v.IsBool() ? booleanProto : v.IsNum() ? numberProto : v.IsStr() ? stringProto : v.IsBig() ? bigintProto : symbolProto;
    return b;
}

GcObj* Runtime::ToPropertyKey(const Value& v) {
    switch (v.t) {
        case Tag::String: return InternStr(v.S());
        case Tag::Symbol: return v.p;
        case Tag::Number:
            if (v.n >= 0 && v.n < 4294967295.0 && v.n == std::floor(v.n)) return IndexKey((std::uint32_t)v.n);
            return Intern(NumberToU16(v.n));
        case Tag::Object: {
            Value p = ToPrimitive(v, 2);
            Keep(p);
            return ToPropertyKey(p);
        }
        default: return InternStr(ToString(v));
    }
}

std::uint64_t Runtime::LengthOf(const Value& obj) {
    if (obj.IsObj() && obj.O()->cls == Cls::Array) return static_cast<ArrayObj*>(obj.O())->length;
    double d = ToIntegerOrInfinity(Get(obj, atoms->length));
    if (d <= 0) return 0;
    return d > 9007199254740991.0 ? 9007199254740991ull : (std::uint64_t)d;
}

bool Runtime::IsConstructor(const Value& v) const {
    if (!v.IsObj()) return false;
    Object* o = v.O();
    if (o->cls == Cls::Closure) {
        const Proto* p = static_cast<Closure*>(o)->code;
        return !(p->flags & (PF_Arrow | PF_Method | PF_Generator | PF_Async)) || (p->flags & PF_ClassCtor);
    }
    if (o->cls == Cls::Native) return static_cast<NativeFunc*>(o)->ctor;
    if (o->cls == Cls::Bound) return IsConstructor(Value::Obj(static_cast<BoundFunc*>(o)->target));
    if (o->cls == Cls::Proxy) return (o->proxyFn & 2) != 0;
    return false;
}

bool Runtime::IsArray(const Value& v) {
    if (!v.IsObj()) return false;
    Object* o = v.O();
    for (int depth = 0; o->cls == Cls::Proxy; ++depth) {   // 7.2.2 step 3: through the proxy to its target
        ProxyObj* p = static_cast<ProxyObj*>(o);
        if (!p->handler) ThrowType("Array.isArray: the proxy was revoked");
        if (depth > Impl::kMaxProxyDepth) ThrowRange("too much recursion (proxies nested too deeply)");
        o = p->target;
    }
    return o->cls == Cls::Array;
}

const char* Runtime::TypeOf(const Value& v) const {
    switch (v.t) {
        case Tag::Undefined: case Tag::Empty: return "undefined";
        case Tag::Null: return "object";
        case Tag::Bool: return "boolean";
        case Tag::Number: return "number";
        case Tag::String: return "string";
        case Tag::Symbol: return "symbol";
        case Tag::BigInt: return "bigint";
        case Tag::Object: return v.O()->IsCallable() ? "function" : "object";
        default: return "undefined";
    }
}

// 7.2.14 IsLooselyEqual
bool Runtime::LooseEquals(const Value& a, const Value& b) {
    if (a.t == b.t) return StrictEquals(a, b);
    if (a.IsNullish() && b.IsNullish()) return true;
    if (a.IsNum() && b.IsStr()) return a.n == StringToNumber(b.S()->s);
    if (a.IsStr() && b.IsNum()) return StringToNumber(a.S()->s) == b.n;
    if (a.IsBig() && b.IsStr()) {   // 7.2.14 steps 6-7: the string read as a BigInt
        Value n;
        return StringToBigInt(*this, b.S()->s, n) && BigEquals(a, n);
    }
    if (a.IsStr() && b.IsBig()) return LooseEquals(b, a);
    if (a.IsBool()) return LooseEquals(Value::Num(a.b ? 1 : 0), b);
    if (b.IsBool()) return LooseEquals(a, Value::Num(b.b ? 1 : 0));
    if (a.IsBig() && b.IsNum()) return BigCompareNum(a, b.n) == 0;
    if (a.IsNum() && b.IsBig()) return BigCompareNum(b, a.n) == 0;
    if ((a.IsNum() || a.IsStr() || a.IsSym() || a.IsBig()) && b.IsObj()) {
        Value p = ToPrimitive(b, 0);
        Keep(p);
        return LooseEquals(a, p);
    }
    if (a.IsObj() && (b.IsNum() || b.IsStr() || b.IsSym() || b.IsBig())) {
        Value p = ToPrimitive(a, 0);
        Keep(p);
        return LooseEquals(p, b);
    }
    return false;
}

// 13.10.2 InstanceofOperator and 7.3.21 OrdinaryHasInstance
bool Runtime::InstanceOf(const Value& v, const Value& target) {
    if (!target.IsObj()) ThrowType("the right side of 'instanceof' is not an object");
    Value h = Get(target, symHasInstance);
    if (!h.IsNullish()) {
        if (!IsCallable(h)) ThrowType("Symbol.hasInstance is not a function");
        Value r = Call(h, target, &v, 1);
        return ToBoolean(r);
    }
    if (!IsCallable(target)) ThrowType("the right side of 'instanceof' is not callable");
    Object* t = target.O();
    if (t->cls == Cls::Bound) return InstanceOf(v, Value::Obj(static_cast<BoundFunc*>(t)->target));
    if (!v.IsObj()) return false;
    Value p = Get(target, atoms->prototype);
    if (!p.IsObj()) ThrowType("the 'prototype' of the right side of 'instanceof' is not an object");
    int guard = 0;
    Keep(p);
    for (Object* o = GetPrototypeOf(v.O()); o; o = GetPrototypeOf(o)) {
        if (o == p.O()) return true;
        if (++guard > 100000) break;
    }
    return false;
}

// ============================================================ properties (10.1, 10.4.2, 10.4.3)
void Runtime::MakeLazyProps(Object* o) {
    if (!o->lazy) return;
    o->lazy = false;
    if (o->cls == Cls::Closure) {
        Closure* c = static_cast<Closure*>(o);
        const Proto* p = c->code;
        o->AddRaw(atoms->length, Value::Num(p->length), kConfigurable);
        o->AddRaw(atoms->name, Value::Str(p->name ? p->name : atoms->empty), kConfigurable);
        bool ctorLike = !(p->flags & (PF_Arrow | PF_Method | PF_Async | PF_ClassCtor));
        if (p->flags & PF_Generator) {
            if (!(p->flags & PF_Async)) {
                Object* proto = NewObject(generatorProto);
                o->AddRaw(atoms->prototype, Value::Obj(proto), kWritable);
            }
        } else if (ctorLike) {
            Object* proto = NewObject(objectProto);
            proto->AddRaw(atoms->constructor, Value::Obj(o), kHidden);
            o->AddRaw(atoms->prototype, Value::Obj(proto), kWritable);
        }
    } else if (o->cls == Cls::Native) {
        NativeFunc* f = static_cast<NativeFunc*>(o);
        o->AddRaw(atoms->length, Value::Num(f->length), kConfigurable);
        o->AddRaw(atoms->name, Value::Str(f->name ? f->name : atoms->empty), kConfigurable);
    } else if (o->cls == Cls::Bound) {
        BoundFunc* b = static_cast<BoundFunc*>(o);
        double len = 0;
        Value tl = Get(Value::Obj(b->target), atoms->length);
        if (tl.IsNum()) len = std::max(0.0, std::trunc(tl.n) - (double)b->args.size());
        o->AddRaw(atoms->length, Value::Num(len), kConfigurable);
        Value tn = Get(Value::Obj(b->target), atoms->name);
        std::u16string nm = u"bound " + (tn.IsStr() ? tn.S()->s : std::u16string());
        o->AddRaw(atoms->name, Value::Str(NewString(nm)), kConfigurable);
    }
}

static inline bool LazyKey(Runtime& rt, GcObj* key) {
    return key == rt.atoms->length || key == rt.atoms->name || key == rt.atoms->prototype;
}

static inline std::int64_t KeyIndex(GcObj* key) {
    if (key->kind != Kind::String) return -1;
    return static_cast<String*>(key)->ArrayIndex();
}

// The exotic own part of [[GetOwnProperty]]: true when handled (found or definitely absent there).
static bool ExoticOwn(Runtime& rt, Object* o, GcObj* key, Prop& out, bool& found) {
    switch (o->cls) {
        case Cls::Array: case Cls::Arguments: {
            ArrayObj* a = static_cast<ArrayObj*>(o);
            if (key == rt.atoms->length) {
                out.key = key;
                out.v = Value::Num(a->length);
                out.flags = a->lengthWritable ? kWritable : 0;
                if (o->cls == Cls::Arguments) out.flags |= kConfigurable;
                found = true;
                return true;
            }
            std::int64_t i = KeyIndex(key);
            if (i >= 0 && (std::uint64_t)i < a->elems.size()) {
                const Value& v = a->elems[(std::size_t)i];
                if (v.IsEmpty()) { found = false; return true; }
                out.key = key;
                out.v = v;
                out.flags = a->frozenElems ? (std::uint8_t)kEnumerable : (std::uint8_t)(a->sealedElems ? kWritable | kEnumerable : kDefault);
                found = true;
                return true;
            }
            return false;
        }
        case Cls::StringBox: {
            BoxObj* b = static_cast<BoxObj*>(o);
            const std::u16string& s = b->prim.S()->s;
            if (key == rt.atoms->length) {
                out.key = key;
                out.v = Value::Num((double)s.size());
                out.flags = 0;
                found = true;
                return true;
            }
            std::int64_t i = KeyIndex(key);
            if (i >= 0 && (std::uint64_t)i < s.size()) {
                out.key = key;
                out.v = Value::Str(rt.NewString(std::u16string(1, s[(std::size_t)i])));
                out.flags = kEnumerable;
                found = true;
                return true;
            }
            return false;
        }
        case Cls::ModuleNs: {   // 10.4.6.5 [[GetOwnProperty]]: a string key is an export (or nothing); symbols are ordinary
            if (key->kind != Kind::String) return false;
            const ModuleNsObj::Entry* e = static_cast<ModuleNsObj*>(o)->Find(key);
            found = e != nullptr;
            if (e) {
                out.key = key;
                out.v = ModuleNamespaceGet(rt, *e);
                out.get = out.set = nullptr;
                out.flags = kWritable | kEnumerable;
            }
            return true;
        }
        case Cls::TypedArray: {   // 10.4.5.1 [[GetOwnProperty]]
            double n;
            if (!CanonicalNumericKey(rt, key, n)) return false;
            TypedArrayObj* t = static_cast<TypedArrayObj*>(o);
            found = ValidIntegerIndex(t, n);
            if (found) {
                out.key = key;
                out.v = TypedArrayGet(rt, t, n);
                out.get = out.set = nullptr;
                out.flags = kDefault;
            }
            return true;
        }
        case Cls::Host: {
            HostObj* h = static_cast<HostObj*>(o);
            if (h->hostClass < rt.hostClasses.size() && rt.hostClasses[h->hostClass].get && key->kind == Kind::String) {
                Value v;
                if (rt.hostClasses[h->hostClass].get(rt, h, static_cast<String*>(key), v)) {
                    out.key = key;
                    out.v = v;
                    out.flags = kWritable | kEnumerable;
                    found = true;
                    return true;
                }
            }
            return false;
        }
        default: return false;
    }
}

bool Runtime::GetOwnProperty(Object* o, GcObj* key, Prop& out) {
    if (o->cls == Cls::Proxy) return ProxyGetOwnProperty(*this, static_cast<ProxyObj*>(o), key, out);
    bool found = false;
    if (ExoticOwn(*this, o, key, out, found)) return found;
    if (o->lazy && LazyKey(*this, key)) MakeLazyProps(o);
    Prop* p = o->FindRaw(key);
    if (!p) return false;
    out = *p;
    return true;
}

Value Runtime::Get(const Value& obj, GcObj* key) { return Get(obj, key, obj); }

Value Runtime::Get(const Value& base, GcObj* key, const Value& receiver) {
    Object* o;
    switch (base.t) {
        case Tag::Object: o = base.O(); break;
        case Tag::String: {
            const std::u16string& s = base.S()->s;
            if (key == atoms->length) return Value::Num((double)s.size());
            std::int64_t i = KeyIndex(key);
            if (i >= 0 && (std::uint64_t)i < s.size()) return Value::Str(NewString(std::u16string(1, s[(std::size_t)i])));
            o = stringProto;
            break;
        }
        case Tag::Number: o = numberProto; break;
        case Tag::Bool: o = booleanProto; break;
        case Tag::Symbol: o = symbolProto; break;
        case Tag::BigInt: o = bigintProto; break;
        default:
            ThrowType("can not read '" + KeyText(*this, key) + "' of " + (base.IsNull() ? "null" : "undefined"));
    }
    int guard = 0;
    for (; o; o = o->proto) {
        if (o->cls != Cls::Ordinary) {
            if (o->cls == Cls::TypedArray) {   // 10.4.5.4: a numeric key never reaches the prototype
                double n;
                if (CanonicalNumericKey(*this, key, n)) return TypedArrayGet(*this, static_cast<TypedArrayObj*>(o), n);
            }
            if (o->cls == Cls::Proxy) return ProxyGet(*this, static_cast<ProxyObj*>(o), key, receiver);
            Prop ex;
            bool found = false;
            if (ExoticOwn(*this, o, key, ex, found)) {
                if (found) return ex.v;
                continue;
            }
            if (o->lazy && LazyKey(*this, key)) MakeLazyProps(o);
        }
        Prop* p = o->FindRaw(key);
        if (p) {
            if (!(p->flags & kAccessor)) return p->v;
            if (!p->get) return Value();
            return Call(Value::Obj(p->get), receiver, nullptr, 0);
        }
        if (++guard > 100000) ThrowRange("the prototype chain is too long");
    }
    return Value();
}

Value Runtime::GetIndex(const Value& obj, std::uint32_t i) {
    if (obj.IsObj() && (obj.O()->cls == Cls::Array || obj.O()->cls == Cls::Arguments)) {
        ArrayObj* a = static_cast<ArrayObj*>(obj.O());
        if (i < a->elems.size() && !a->elems[i].IsEmpty()) return a->elems[i];
    }
    if (obj.IsObj() && obj.O()->cls == Cls::TypedArray) return TypedArrayGet(*this, static_cast<TypedArrayObj*>(obj.O()), (double)i);
    return Get(obj, IndexKey(i));
}

Value Runtime::GetV(const Value& obj, const Value& key) {
    if (key.IsNum() && obj.IsObj() && key.n >= 0 && key.n < 4294967295.0) {
        std::uint32_t i = (std::uint32_t)key.n;
        if ((double)i == key.n) return GetIndex(obj, i);
    }
    if (obj.IsNullish()) {
        std::string k;
        try { k = key.IsSym() ? "a symbol" : ToUtf8(key); } catch (...) { k = "?"; }
        ThrowType("can not read '" + k + "' of " + (obj.IsNull() ? "null" : "undefined"));
    }
    GcObj* k = ToPropertyKey(key);
    return Get(obj, k);
}

static void ArrayAppendFixup(Runtime& rt, ArrayObj* a) {
    // after the dense part grew: pull following index properties out of props
    while (a->sparse > 0) {
        GcObj* k = rt.IndexKey((std::uint32_t)a->elems.size());
        Prop* p = a->FindRaw(k);
        if (!p || (p->flags & kAccessor)) break;
        a->elems.push_back(p->v);
        a->RemoveRaw(k);
        --a->sparse;
    }
}

// Set element i of an array as a data property (no setters on the array itself; strict errors).
static bool ArraySetIndex(Runtime& rt, ArrayObj* a, std::uint32_t i, const Value& v, bool define) {
    if (i < a->elems.size()) {
        Value& slot = a->elems[i];
        if (slot.IsEmpty()) {
            if (!a->extensible || a->sealedElems) return false;
        } else if (a->frozenElems) return false;
        slot = v;
        return true;
    }
    if (!a->extensible || a->sealedElems) return false;
    if (i >= a->length && !a->lengthWritable) return false;
    if (a->sparse > 0) {
        GcObj* k = rt.IndexKey(i);
        if (Prop* p = a->FindRaw(k)) {
            if (p->flags & kAccessor) return false;
            if (!(p->flags & kWritable) && !define) return false;
            p->v = v;
            return true;
        }
    }
    std::size_t sz = a->elems.size();
    if (i == sz || (i < sz + 64 && i < sz * 2 + 64)) {
        if (i > sz) {
            rt.CountBytes((std::ptrdiff_t)((i - sz) * sizeof(Value)));
            a->elems.resize(i, Value::EmptyV());
        }
        rt.CountBytes(sizeof(Value));
        a->elems.push_back(v);
        if (i + 1 > a->length) a->length = i + 1;
        if (a->sparse > 0) ArrayAppendFixup(rt, a);
        return true;
    }
    a->AddRaw(rt.IndexKey(i), v, kDefault);
    ++a->sparse;
    if (i + 1 > a->length) a->length = i + 1;
    return true;
}

void SetLength(Runtime& rt, Object* o, std::uint64_t len) {
    if (o->cls == Cls::Array || o->cls == Cls::Arguments) {
        ArrayObj* a = static_cast<ArrayObj*>(o);
        if (len > 4294967295ull) rt.ThrowRange("an array length must be a whole number below 2^32");
        if (len == a->length) return;
        if (!a->lengthWritable) rt.ThrowType("the array's length is read-only");
        if (len < a->length && (a->sealedElems || a->frozenElems)) rt.ThrowType("can not remove elements of a sealed array");
        if (len < a->elems.size()) a->elems.resize((std::size_t)len);
        if (a->sparse > 0 && len < a->length) {
            std::vector<GcObj*> drop;
            for (const Prop& p : a->props) {
                std::int64_t i = KeyIndex(p.key);
                if (i >= 0 && (std::uint64_t)i >= len) drop.push_back(p.key);
            }
            for (GcObj* k : drop) { a->RemoveRaw(k); --a->sparse; }
        }
        a->length = (std::uint32_t)len;
        return;
    }
    rt.Put(Value::Obj(o), rt.atoms->length, Value::Num((double)len));
}

void Runtime::Put(const Value& base, GcObj* key, const Value& v) {
    if (base.IsNullish()) ThrowType("can not set '" + KeyText(*this, key) + "' of " + (base.IsNull() ? "null" : "undefined"));
    Object* o = base.IsObj() ? base.O() : nullptr;
    if (o) {
        // the own part
        switch (o->cls) {
            case Cls::Array: case Cls::Arguments: {
                ArrayObj* a = static_cast<ArrayObj*>(o);
                if (key == atoms->length) {
                    double d = ToNumber(v);
                    std::uint32_t n = ToUint32(Value::Num(d));
                    if ((double)n != d) ThrowRange("an array length must be a whole number below 2^32");
                    SetLength(*this, a, n);
                    return;
                }
                std::int64_t i = KeyIndex(key);
                if (i >= 0) {
                    bool hole = (std::uint64_t)i >= a->elems.size() || a->elems[(std::size_t)i].IsEmpty();
                    if (!hole || a->proto == arrayProto) {   // the common case: no setter on the chain to look for
                        if (!hole || !arrayProtoHasIndex_) {
                            if (!ArraySetIndex(*this, a, (std::uint32_t)i, v, false))
                                ThrowType("can not set element " + std::to_string(i) + " of a frozen, sealed or fixed array");
                            return;
                        }
                    }
                }
                break;
            }
            case Cls::Host: {
                HostObj* h = static_cast<HostObj*>(o);
                if (h->hostClass < hostClasses.size() && hostClasses[h->hostClass].set && key->kind == Kind::String &&
                    hostClasses[h->hostClass].set(*this, h, static_cast<String*>(key), v))
                    return;
                break;
            }
            case Cls::StringBox: {
                Prop ex;
                bool found = false;
                if (ExoticOwn(*this, o, key, ex, found) && found) ThrowType("the characters of a String are read-only");
                break;
            }
            case Cls::Proxy:
                if (!ProxySet(*this, static_cast<ProxyObj*>(o), key, v, base)) ThrowType("the proxy refused to set '" + KeyText(*this, key) + "'");
                return;
            case Cls::ModuleNs: ThrowType("can not set '" + KeyText(*this, key) + "': a module namespace is read-only");   // 10.4.6.9
            case Cls::TypedArray: {   // 10.4.5.5 [[Set]] with the typed array as the receiver: no error for a bad index
                double n;
                if (CanonicalNumericKey(*this, key, n)) {
                    TypedArraySet(*this, static_cast<TypedArrayObj*>(o), n, v);
                    return;
                }
                break;
            }
            default:
                if (o->lazy && LazyKey(*this, key)) MakeLazyProps(o);
                break;
        }
        Prop* p = o->FindRaw(key);
        if (p) {
            if (p->flags & kAccessor) {
                if (!p->set) ThrowType("'" + KeyText(*this, key) + "' has a getter but no setter");
                Value arg = v;
                Call(Value::Obj(p->set), base, &arg, 1);
                return;
            }
            if (!(p->flags & kWritable)) ThrowType("'" + KeyText(*this, key) + "' is read-only");
            p->v = v;
            return;
        }
    }
    // the prototype chain: setters and read-only properties (10.1.9.2 OrdinarySetWithOwnDescriptor)
    Object* q = o ? o->proto
                  : base.IsStr() ? stringProto : base.IsNum() ? numberProto : base.IsBool() ? booleanProto
                  : base.IsBig() ? bigintProto : symbolProto;
    int guard = 0;
    for (; q; q = q->proto) {
        if (q->cls != Cls::Ordinary) {
            if (q->cls == Cls::Proxy) {   // the rest of the chain is the proxy's [[Set]] with our receiver
                if (!ProxySet(*this, static_cast<ProxyObj*>(q), key, v, base)) ThrowType("the proxy refused to set '" + KeyText(*this, key) + "'");
                return;
            }
            if (q->cls == Cls::TypedArray) {   // 10.4.5.5 step 1.b.ii: an index that is not valid there is a quiet no-op
                double n;
                if (CanonicalNumericKey(*this, key, n) && !ValidIntegerIndex(static_cast<TypedArrayObj*>(q), n)) return;
            }
            Prop ex;
            bool found = false;
            if (ExoticOwn(*this, q, key, ex, found)) {
                if (found) {
                    if (!(ex.flags & kWritable)) ThrowType("'" + KeyText(*this, key) + "' is read-only");
                    break;
                }
                continue;
            }
            if (q->lazy && LazyKey(*this, key)) MakeLazyProps(q);
        }
        Prop* p = q->FindRaw(key);
        if (p) {
            if (p->flags & kAccessor) {
                if (!p->set) ThrowType("'" + KeyText(*this, key) + "' has a getter but no setter");
                Value arg = v;
                Call(Value::Obj(p->set), base, &arg, 1);
                return;
            }
            if (!(p->flags & kWritable)) ThrowType("'" + KeyText(*this, key) + "' is read-only");
            break;
        }
        if (++guard > 100000) ThrowRange("the prototype chain is too long");
    }
    if (!o) ThrowType("can not create the property '" + KeyText(*this, key) + "' on a primitive value");
    if (!o->extensible) ThrowType("can not add '" + KeyText(*this, key) + "': the object is not extensible");
    if (o->cls == Cls::Array || o->cls == Cls::Arguments) {
        std::int64_t i = KeyIndex(key);
        if (i >= 0) {
            if (!ArraySetIndex(*this, static_cast<ArrayObj*>(o), (std::uint32_t)i, v, false))
                ThrowType("can not set an element of a fixed array");
            return;
        }
    }
    o->AddRaw(key, v, kDefault);
    CountBytes(sizeof(Prop));
}

void Runtime::PutIndex(const Value& obj, std::uint32_t i, const Value& v) {
    if (obj.IsObj() && obj.O()->cls == Cls::Array) {
        ArrayObj* a = static_cast<ArrayObj*>(obj.O());
        if (i < a->elems.size() && !a->elems[i].IsEmpty() && !a->frozenElems) {
            a->elems[i] = v;
            return;
        }
        if (i == a->elems.size() && a->extensible && !a->sealedElems && a->sparse == 0 && !arrayProtoHasIndex_ &&
            (i < a->length || a->lengthWritable)) {
            CountBytes(sizeof(Value));
            a->elems.push_back(v);
            if (i + 1 > a->length) a->length = i + 1;
            return;
        }
    }
    if (obj.IsObj() && obj.O()->cls == Cls::TypedArray) {
        TypedArraySet(*this, static_cast<TypedArrayObj*>(obj.O()), (double)i, v);
        return;
    }
    Put(obj, IndexKey(i), v);
}

void Runtime::PutV(const Value& obj, const Value& key, const Value& v) {
    if (key.IsNum() && obj.IsObj() && key.n >= 0 && key.n < 4294967295.0) {
        std::uint32_t i = (std::uint32_t)key.n;
        if ((double)i == key.n) { PutIndex(obj, i, v); return; }
    }
    if (obj.IsNullish()) {
        std::string k;
        try { k = key.IsSym() ? "a symbol" : ToUtf8(key); } catch (...) { k = "?"; }
        ThrowType("can not set '" + k + "' of " + (obj.IsNull() ? "null" : "undefined"));
    }
    GcObj* k = ToPropertyKey(key);
    Put(obj, k, v);
}

bool Runtime::HasOwn(Object* o, GcObj* key) {
    Prop p;
    return GetOwnProperty(o, key, p);
}

bool Runtime::HasProperty(Object* o, GcObj* key) {
    int guard = 0;
    for (; o; o = o->proto) {
        if (o->cls == Cls::Proxy) return ProxyHas(*this, static_cast<ProxyObj*>(o), key);
        if (o->cls == Cls::ModuleNs && key->kind == Kind::String) return static_cast<ModuleNsObj*>(o)->Find(key) != nullptr;   // 10.4.6.7
        if (o->cls == Cls::TypedArray) {   // 10.4.5.2 [[HasProperty]]
            double n;
            if (CanonicalNumericKey(*this, key, n)) return ValidIntegerIndex(static_cast<TypedArrayObj*>(o), n);
        }
        if (HasOwn(o, key)) return true;
        if (++guard > 100000) ThrowRange("the prototype chain is too long");
    }
    return false;
}

bool Runtime::Delete(Object* o, GcObj* key) {
    switch (o->cls) {
        case Cls::Array: case Cls::Arguments: {
            ArrayObj* a = static_cast<ArrayObj*>(o);
            if (key == atoms->length) return false;
            std::int64_t i = KeyIndex(key);
            if (i >= 0 && (std::uint64_t)i < a->elems.size()) {
                if (a->elems[(std::size_t)i].IsEmpty()) return true;
                if (a->sealedElems || a->frozenElems) return false;
                if ((std::size_t)i + 1 == a->elems.size() && a->sparse == 0) a->elems.pop_back();
                else a->elems[(std::size_t)i] = Value::EmptyV();
                return true;
            }
            if (i >= 0 && a->sparse > 0) {
                Prop* p = a->FindRaw(key);
                if (!p) return true;
                if (!(p->flags & kConfigurable)) return false;
                a->RemoveRaw(key);
                --a->sparse;
                return true;
            }
            break;
        }
        case Cls::StringBox: {
            Prop ex;
            bool found = false;
            if (ExoticOwn(*this, o, key, ex, found) && found) return false;
            break;
        }
        case Cls::Host: return true;
        case Cls::Proxy: return ProxyDelete(*this, static_cast<ProxyObj*>(o), key);
        case Cls::ModuleNs:   // 10.4.6.10
            if (key->kind == Kind::String) return static_cast<ModuleNsObj*>(o)->Find(key) == nullptr;
            break;
        case Cls::TypedArray: {   // 10.4.5.6 [[Delete]]
            double n;
            if (CanonicalNumericKey(*this, key, n)) return !ValidIntegerIndex(static_cast<TypedArrayObj*>(o), n);
            break;
        }
        default:
            if (o->lazy && LazyKey(*this, key)) MakeLazyProps(o);
            break;
    }
    Prop* p = o->FindRaw(key);
    if (!p) return true;
    if (!(p->flags & kConfigurable)) return false;
    o->RemoveRaw(key);
    return true;
}

void Runtime::DefineData(Object* o, GcObj* key, const Value& v, std::uint8_t flags) {
    if (o->cls == Cls::Array || o->cls == Cls::Arguments) {
        std::int64_t i = KeyIndex(key);
        if (i >= 0 && flags == kDefault) {
            if (!ArraySetIndex(*this, static_cast<ArrayObj*>(o), (std::uint32_t)i, v, true))
                ThrowType("can not define an element of a fixed array");
            return;
        }
        if (i >= 0 || key == atoms->length) {
            Descriptor d;
            d.hasValue = d.hasWritable = d.hasEnumerable = d.hasConfigurable = true;
            d.value = v;
            d.writable = flags & kWritable;
            d.enumerable = flags & kEnumerable;
            d.configurable = flags & kConfigurable;
            if (!DefineOwnProperty(o, key, d)) ThrowType("can not define '" + KeyText(*this, key) + "'");
            return;
        }
    }
    if (o->cls == Cls::Host) {
        Put(Value::Obj(o), key, v);
        return;
    }
    double tn;
    if (o->cls == Cls::Proxy || (o->cls == Cls::TypedArray && CanonicalNumericKey(*this, key, tn))) {
        Descriptor d;   // the exotic [[DefineOwnProperty]] decides (a proxy's trap, a typed array's index rules)
        d.hasValue = d.hasWritable = d.hasEnumerable = d.hasConfigurable = true;
        d.value = v;
        d.writable = flags & kWritable;
        d.enumerable = flags & kEnumerable;
        d.configurable = flags & kConfigurable;
        if (!DefineOwnProperty(o, key, d)) ThrowType("can not define '" + KeyText(*this, key) + "'");
        return;
    }
    if (o->lazy && LazyKey(*this, key)) MakeLazyProps(o);
    Prop* p = o->FindRaw(key);
    if (p) {
        if (!(p->flags & kConfigurable) && (p->flags & kAccessor || !(p->flags & kWritable)))
            ThrowType("can not redefine '" + KeyText(*this, key) + "'");
        p->v = v;
        p->get = p->set = nullptr;
        p->flags = (p->flags & kConfigurable) ? flags : (std::uint8_t)((p->flags & ~kAccessor) | (flags & kWritable));
        return;
    }
    if (!o->extensible) ThrowType("can not add '" + KeyText(*this, key) + "': the object is not extensible");
    o->AddRaw(key, v, flags);
    CountBytes(sizeof(Prop));
}

void Runtime::DefineIndex(Object* o, std::uint32_t i, const Value& v) {
    if (o->cls == Cls::Array) {
        ArrayObj* a = static_cast<ArrayObj*>(o);
        if (i == a->elems.size() && a->sparse == 0 && a->extensible && !a->sealedElems && (i < a->length || a->lengthWritable)) {
            CountBytes(sizeof(Value));
            a->elems.push_back(v);
            if (i + 1 > a->length) a->length = i + 1;
            return;
        }
    }
    DefineData(o, IndexKey(i), v, kDefault);
}

void Runtime::DefineAccessor(Object* o, GcObj* key, Object* getter, Object* setter, std::uint8_t flags) {
    if (o->lazy && LazyKey(*this, key)) MakeLazyProps(o);
    if ((o == arrayProto || o == objectProto) && KeyIndex(key) >= 0) arrayProtoHasIndex_ = true;
    double tn;
    if (((o->cls == Cls::Array || o->cls == Cls::Arguments) && (KeyIndex(key) >= 0 || key == atoms->length)) ||
        (o->cls == Cls::TypedArray && CanonicalNumericKey(*this, key, tn)) || o->cls == Cls::Proxy) {
        Descriptor d;
        d.hasEnumerable = d.hasConfigurable = true;
        d.enumerable = flags & kEnumerable;
        d.configurable = flags & kConfigurable;
        if (getter) { d.hasGet = true; d.get = getter; }
        if (setter) { d.hasSet = true; d.set = setter; }
        if (!DefineOwnProperty(o, key, d)) ThrowType("can not define '" + KeyText(*this, key) + "'");
        return;
    }
    Prop* p = o->FindRaw(key);
    if (!p) {
        if (!o->extensible) ThrowType("can not add '" + KeyText(*this, key) + "': the object is not extensible");
        p = o->AddRaw(key, Value(), (std::uint8_t)(flags | kAccessor));
        CountBytes(sizeof(Prop));
    } else {
        if (!(p->flags & kConfigurable)) ThrowType("can not redefine '" + KeyText(*this, key) + "'");
        if (!(p->flags & kAccessor)) { p->get = p->set = nullptr; p->v = Value(); }
        p->flags = (std::uint8_t)(flags | kAccessor);
    }
    if (getter) p->get = getter;
    if (setter) p->set = setter;
}

// 10.1.6.3 ValidateAndApplyPropertyDescriptor (for ordinary objects; arrays turn the element into a property)
bool Runtime::DefineOwnProperty(Object* o, GcObj* key, const Descriptor& d) {
    if (o->cls == Cls::Proxy) return ProxyDefineOwnProperty(*this, static_cast<ProxyObj*>(o), key, d);
    if (o->cls == Cls::ModuleNs && key->kind == Kind::String) {   // 10.4.6.6: only a no-op redefinition succeeds
        Prop cur;
        if (!GetOwnProperty(o, key, cur)) return false;
        if ((d.hasConfigurable && d.configurable) || (d.hasEnumerable && !d.enumerable) || d.hasGet || d.hasSet ||
            (d.hasWritable && !d.writable))
            return false;
        return !d.hasValue || SameValue(d.value, cur.v);
    }
    if (o->lazy) MakeLazyProps(o);
    if (o->cls == Cls::TypedArray) {   // 10.4.5.3 [[DefineOwnProperty]]
        double n;
        if (CanonicalNumericKey(*this, key, n)) {
            TypedArrayObj* t = static_cast<TypedArrayObj*>(o);
            if (!ValidIntegerIndex(t, n)) return false;
            if (d.hasConfigurable && !d.configurable) return false;
            if (d.hasEnumerable && !d.enumerable) return false;
            if (d.hasGet || d.hasSet) return false;
            if (d.hasWritable && !d.writable) return false;
            if (d.hasValue) TypedArraySet(*this, t, n, d.value);
            return true;
        }
    }
    if ((o == arrayProto || o == objectProto) && KeyIndex(key) >= 0) arrayProtoHasIndex_ = true;
    bool isArr = o->cls == Cls::Array || o->cls == Cls::Arguments;
    if (isArr) {
        ArrayObj* a = static_cast<ArrayObj*>(o);
        if (key == atoms->length) {
            if (d.hasGet || d.hasSet || (d.hasConfigurable && d.configurable) || (d.hasEnumerable && d.enumerable)) return false;
            if (d.hasValue) {
                double n = ToNumber(d.value);
                std::uint32_t len = ToUint32(Value::Num(n));
                if ((double)len != n) ThrowRange("an array length must be a whole number below 2^32");
                if (!a->lengthWritable && len != a->length) return false;
                SetLength(*this, a, len);
            }
            if (d.hasWritable) {
                if (d.writable && !a->lengthWritable) return false;
                a->lengthWritable = d.writable;
            }
            return true;
        }
        std::int64_t i = KeyIndex(key);
        if (i >= 0) {
            bool plain = !d.hasGet && !d.hasSet && (!d.hasWritable || d.writable) && (!d.hasEnumerable || d.enumerable) &&
                         (!d.hasConfigurable || d.configurable);
            if (plain && (std::uint64_t)i < a->elems.size() && !a->elems[(std::size_t)i].IsEmpty()) {
                if (a->frozenElems) return d.hasValue ? SameValue(d.value, a->elems[(std::size_t)i]) : true;
                if (d.hasValue) a->elems[(std::size_t)i] = d.value;
                return true;
            }
            if (plain && d.hasValue && d.hasWritable && d.hasEnumerable && d.hasConfigurable)
                return ArraySetIndex(*this, a, (std::uint32_t)i, d.value, true);
            // anything else moves the element (and every later one) into the property part
            if ((std::uint64_t)i < a->elems.size()) {
                for (std::size_t k = (std::size_t)i; k < a->elems.size(); ++k)
                    if (!a->elems[k].IsEmpty()) {
                        a->AddRaw(IndexKey((std::uint32_t)k), a->elems[k], a->frozenElems ? kEnumerable : kDefault);
                        ++a->sparse;
                    }
                a->elems.resize((std::size_t)i);
            }
            if ((std::uint64_t)i >= a->length) {
                if (!a->lengthWritable) return false;
                a->length = (std::uint32_t)i + 1;
            }
            bool existed = a->FindRaw(key) != nullptr;
            if (!existed) ++a->sparse;
        }
    }
    Prop* p = o->FindRaw(key);
    if (!p) {
        if (!o->extensible) return false;
        std::uint8_t fl = 0;
        if (d.hasEnumerable && d.enumerable) fl |= kEnumerable;
        if (d.hasConfigurable && d.configurable) fl |= kConfigurable;
        if (d.hasGet || d.hasSet) {
            p = o->AddRaw(key, Value(), (std::uint8_t)(fl | kAccessor));
            p->get = d.get;
            p->set = d.set;
        } else {
            if (d.hasWritable && d.writable) fl |= kWritable;
            o->AddRaw(key, d.value, fl);
        }
        CountBytes(sizeof(Prop));
        return true;
    }
    bool accessor = p->flags & kAccessor;
    if (!(p->flags & kConfigurable)) {
        if (d.hasConfigurable && d.configurable) return false;
        if (d.hasEnumerable && d.enumerable != (bool)(p->flags & kEnumerable)) return false;
        if ((d.hasGet || d.hasSet) && !accessor) return false;
        if ((d.hasValue || d.hasWritable) && accessor) return false;
        if (accessor) {
            if (d.hasGet && d.get != p->get) return false;
            if (d.hasSet && d.set != p->set) return false;
        } else if (!(p->flags & kWritable)) {
            if (d.hasWritable && d.writable) return false;
            if (d.hasValue && !SameValue(d.value, p->v)) return false;
        }
    }
    if ((d.hasGet || d.hasSet) && !accessor) {
        p->flags = (std::uint8_t)((p->flags & (kEnumerable | kConfigurable)) | kAccessor);
        p->v = Value();
        p->get = p->set = nullptr;
    } else if ((d.hasValue || d.hasWritable) && accessor) {
        p->flags = (std::uint8_t)(p->flags & (kEnumerable | kConfigurable));
        p->get = p->set = nullptr;
    }
    if (d.hasValue) p->v = d.value;
    if (d.hasGet) p->get = d.get;
    if (d.hasSet) p->set = d.set;
    if (d.hasWritable) p->flags = (std::uint8_t)(d.writable ? p->flags | kWritable : p->flags & ~kWritable);
    if (d.hasEnumerable) p->flags = (std::uint8_t)(d.enumerable ? p->flags | kEnumerable : p->flags & ~kEnumerable);
    if (d.hasConfigurable) p->flags = (std::uint8_t)(d.configurable ? p->flags | kConfigurable : p->flags & ~kConfigurable);
    return true;
}

// 10.1.11.1 OrdinaryOwnPropertyKeys
std::vector<GcObj*> Runtime::OwnKeys(Object* o, bool strings, bool symbols) {
    if (o->cls == Cls::Proxy) {
        std::vector<GcObj*> all = ProxyOwnKeys(*this, static_cast<ProxyObj*>(o));
        if (strings && symbols) return all;
        std::vector<GcObj*> out;
        for (GcObj* k : all)
            if ((k->kind == Kind::String) ? strings : symbols) out.push_back(k);
        return out;
    }
    if (o->lazy) MakeLazyProps(o);
    std::vector<GcObj*> out;
    std::vector<std::pair<std::uint32_t, GcObj*>> idx;
    if (strings) {
        if (o->cls == Cls::Array || o->cls == Cls::Arguments) {
            ArrayObj* a = static_cast<ArrayObj*>(o);
            for (std::uint32_t i = 0; i < a->elems.size(); ++i)
                if (!a->elems[i].IsEmpty()) out.push_back(IndexKey(i));
        } else if (o->cls == Cls::StringBox) {
            const std::u16string& s = static_cast<BoxObj*>(o)->prim.S()->s;
            for (std::uint32_t i = 0; i < s.size(); ++i) out.push_back(IndexKey(i));
        } else if (o->cls == Cls::ModuleNs) {   // 10.4.6.11: the export names, already in order
            for (const ModuleNsObj::Entry& e : static_cast<ModuleNsObj*>(o)->entries) out.push_back(e.name);
        } else if (o->cls == Cls::TypedArray) {   // 10.4.5.7 [[OwnPropertyKeys]]: the indices first
            const std::uint64_t n = static_cast<TypedArrayObj*>(o)->Length();
            Charge(n);
            for (std::uint64_t i = 0; i < n; ++i) out.push_back(IndexKey((std::uint32_t)i));
        } else if (o->cls == Cls::Host) {
            HostObj* h = static_cast<HostObj*>(o);
            if (h->hostClass < hostClasses.size() && hostClasses[h->hostClass].keys)
                for (const std::string& k : hostClasses[h->hostClass].keys(*this, h)) out.push_back(Intern(Utf8To16(k)));
        }
        for (const Prop& p : o->props) {
            std::int64_t i = KeyIndex(p.key);
            if (i >= 0) idx.push_back({(std::uint32_t)i, p.key});
        }
        std::sort(idx.begin(), idx.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& kv : idx) out.push_back(kv.second);
        if (o->cls == Cls::Array || o->cls == Cls::Arguments || o->cls == Cls::StringBox) out.push_back(atoms->length);
        for (const Prop& p : o->props)
            if (p.key->kind == Kind::String && KeyIndex(p.key) < 0) out.push_back(p.key);
    }
    if (symbols)
        for (const Prop& p : o->props)
            if (p.key->kind == Kind::Symbol && !static_cast<Symbol*>(p.key)->priv) out.push_back(p.key);
    return out;
}

// 10.1.2.1 OrdinarySetPrototypeOf (the cycle check stops at a proxy, as step 8.c.i says)
bool Runtime::SetPrototype(Object* o, Object* proto) {
    if (o->cls == Cls::Proxy) return ProxySetPrototypeOf(*this, static_cast<ProxyObj*>(o), proto);
    if (o->proto == proto) return true;
    if (!o->extensible) return false;
    for (Object* p = proto; p; p = p->proto) {
        if (p == o) return false;
        if (p->cls == Cls::Proxy) break;
    }
    o->proto = proto;
    return true;
}

bool Runtime::PreventExtensions(Object* o) {
    if (o->cls == Cls::Proxy) return ProxyPreventExtensions(*this, static_cast<ProxyObj*>(o));
    if (o->lazy) MakeLazyProps(o);
    o->extensible = false;
    return true;
}

Object* Runtime::GetPrototypeOf(Object* o) {
    if (o->cls == Cls::Proxy) return ProxyGetPrototypeOf(*this, static_cast<ProxyObj*>(o));
    return o->proto;
}

bool Runtime::IsExtensible(Object* o) {
    if (o->cls == Cls::Proxy) return ProxyIsExtensible(*this, static_cast<ProxyObj*>(o));
    return o->extensible;
}

// 10.1.9.2 OrdinarySetWithOwnDescriptor, walking the chain with each object's own [[GetOwnProperty]]; a proxy on
// the way takes over with its [[Set]]. Put is the fast form of this for receiver == the object.
bool Runtime::SetWithReceiver(Object* o, GcObj* key, const Value& v, const Value& receiver) {
    if (o->cls == Cls::ModuleNs) return false;   // 10.4.6.9
    int guard = 0;
    Prop own;
    bool found = false;
    for (Object* q = o; q; q = q->proto) {
        if (q->cls == Cls::Proxy) return ProxySet(*this, static_cast<ProxyObj*>(q), key, v, receiver);
        if (q->cls == Cls::TypedArray) {   // 10.4.5.5
            double n;
            if (CanonicalNumericKey(*this, key, n)) {
                TypedArrayObj* t = static_cast<TypedArrayObj*>(q);
                if (receiver.IsObj() && receiver.O() == q) {
                    TypedArraySet(*this, t, n, v);
                    return true;
                }
                if (!ValidIntegerIndex(t, n)) return true;
            }
        }
        if (GetOwnProperty(q, key, own)) {
            found = true;
            break;
        }
        if (++guard > 100000) ThrowRange("the prototype chain is too long");
    }
    if (found && (own.flags & kAccessor)) {
        if (!own.set) return false;
        Value arg = v;
        Call(Value::Obj(own.set), receiver, &arg, 1);
        return true;
    }
    if (found && !(own.flags & kWritable)) return false;
    if (!receiver.IsObj()) return false;
    Object* r = receiver.O();
    Keep(r);
    Prop existing;
    if (GetOwnProperty(r, key, existing)) {
        if (existing.flags & kAccessor) return false;
        if (!(existing.flags & kWritable)) return false;
        Descriptor d;
        d.hasValue = true;
        d.value = v;
        return DefineOwnProperty(r, key, d);
    }
    Descriptor d;   // 7.3.5 CreateDataProperty
    d.hasValue = d.hasWritable = d.hasEnumerable = d.hasConfigurable = true;
    d.value = v;
    d.writable = d.enumerable = d.configurable = true;
    return DefineOwnProperty(r, key, d);
}

// ============================================================ globals and references
void Runtime::SetGlobal(const char* name, const Value& v) {
    GcObj* k = Intern(name);
    if (Prop* p = global->FindRaw(k)) p->v = v;
    else global->AddRaw(k, v, kHidden);
}
Value Runtime::GetGlobal(const char* name) {
    GcObj* k = Intern(name);
    if (Prop* p = lexicals->FindRaw(k)) return p->v.IsEmpty() ? Value() : p->v;
    if (Prop* p = global->FindRaw(k)) return (p->flags & kAccessor) ? Value() : p->v;
    return Value();
}
int Runtime::Ref(const Value& v) {
    if (!freeRefs_.empty()) {
        int r = freeRefs_.back();
        freeRefs_.pop_back();
        refs_[(std::size_t)r] = v;
        return r;
    }
    refs_.push_back(v);
    return (int)refs_.size() - 1;
}
void Runtime::Unref(int ref) {
    if (ref < 0 || ref >= (int)refs_.size()) return;
    refs_[(std::size_t)ref] = Value();
    freeRefs_.push_back(ref);
}

// ============================================================ the collector
void Runtime::Mark(GcObj* o) {
    if (!o || o->marked) return;
    o->marked = true;
    gray_.push_back(o);
}

void Runtime::Collect() {
    ++collections_;
    gray_.clear();
    for (Value* v = vm->stack; v < vm->sp; ++v) MarkValue(*v);
    for (int i = 0; i < vm->nframes; ++i) {
        const Frame& f = vm->frames[i];
        if (f.fn) Mark(f.fn);
        if (f.newTarget) Mark(f.newTarget);
        if (f.gen) Mark(f.gen);
        MarkValue(f.extra);
    }
    for (const Value& v : scratch_) MarkValue(v);
    for (const std::vector<Value>* rv : rootedVectors_)
        for (const Value& v : *rv) MarkValue(v);
    for (const Value& v : refs_) MarkValue(v);
    for (const Job& j : vm->jobs) { MarkValue(j.a); MarkValue(j.b); MarkValue(j.c); }
    MarkValue(exception);
    Mark(global);
    Mark(lexicals);
    for (Proto* p : vm->scripts) Mark(p);
    Object* intr[] = {objectProto, functionProto, arrayProto, stringProto, numberProto, booleanProto, symbolProto,
                      errorProto, iteratorProto, arrayIterProto, mapIterProto, setIterProto, stringIterProto,
                      generatorProto, generatorFnProto, asyncFnProto, mapProto, setProto, promiseProto, promiseCtor,
                      arrayCtor, objectCtor, throwTypeErrorFn, regexpProto, dateProto, bigintProto, vm->arrayValues, vm->arrayIterNext,
                      vm->stringIterator, vm->stringIterNext};
    for (Object* o : intr) if (o) Mark(o);
    for (Object* o : errorProtos) if (o) Mark(o);
    for (Symbol* s : {symIterator, symAsyncIterator, symToPrimitive, symToStringTag, symHasInstance, symSpecies}) if (s) Mark(s);
    for (Object* o : vm->moreIntrinsics) if (o) Mark(o);
    for (const auto& m : vm->moduleList) {
        if (m->proto) Mark(m->proto);
        if (m->fn) Mark(m->fn);
        if (m->ns) Mark(m->ns);
        if (m->done) Mark(m->done);
        MarkValue(m->error);
        for (const auto& kv : m->cells) Mark(kv.second);
    }
    for (auto& kv : symbolRegistry) Mark(kv.second);
    String** a = reinterpret_cast<String**>(atoms.get());
    for (std::size_t i = 0; i < sizeof(Atoms) / sizeof(String*); ++i) if (a[i]) Mark(a[i]);
    for (String* s : indexAtoms_) if (s) Mark(s);
    // WeakMap entries are ephemerons: a value is reachable when its map and its key are. Entries whose key is not
    // marked yet wait in `pending` under the key; when the key is traced, its values are marked. Each live
    // WeakMap is scanned once, so the pass is linear in the number of entries.
    std::unordered_multimap<GcObj*, Value> pending;
    std::vector<char> scanned(weakObjs.size(), 0);
    auto drain = [&]() {
        while (!gray_.empty()) {
            GcObj* o = gray_.back();
            gray_.pop_back();
            o->Trace(*this);
            if (!pending.empty()) {
                auto range = pending.equal_range(o);
                for (auto it = range.first; it != range.second; ++it) MarkValue(it->second);
                pending.erase(range.first, range.second);
            }
        }
    };
    drain();
    for (bool more = true; more;) {
        more = false;
        for (std::size_t i = 0; i < weakObjs.size(); ++i) {
            Object* w = weakObjs[i];
            if (scanned[i] || !w->marked || w->cls != Cls::WeakMap) continue;
            scanned[i] = 1;
            more = true;
            for (const auto& e : static_cast<WeakObj*>(w)->entries) {
                if (!e.second.IsGc()) continue;
                if (e.first->marked) MarkValue(e.second);
                else pending.emplace(e.first, e.second);
            }
            drain();
        }
    }
    // clear what died: dead weak objects leave the list, dead keys and targets leave the live ones
    std::size_t keep = 0;
    for (Object* w : weakObjs) {
        if (!w->marked) continue;
        WeakObj* wo = static_cast<WeakObj*>(w);
        for (auto it = wo->entries.begin(); it != wo->entries.end();)
            it = it->first->marked ? std::next(it) : wo->entries.erase(it);
        if (wo->target && !wo->target->marked) wo->target = nullptr;
        std::size_t c = 0;
        for (WeakObj::Cell& cell : wo->cells) {
            if (!cell.target->marked) continue;   // its target died: the cleanup callback is not run (9.10 allows that)
            if (cell.token && !cell.token->marked) cell.token = nullptr;
            wo->cells[c++] = cell;
        }
        wo->cells.resize(c);
        weakObjs[keep++] = w;
    }
    weakObjs.resize(keep);
    Sweep();
}

void Runtime::Sweep() {
    // phase 1: notices for objects about to go, while everything is still allocated
    for (GcObj* o = gcList_; o; o = o->gcNext) {
        if (o->marked) continue;
        if (o->kind == Kind::String && static_cast<String*>(o)->atom) atomTable_.erase(std::u16string_view(static_cast<String*>(o)->s));
        if (o->kind == Kind::Object) {
            Object* ob = static_cast<Object*>(o);
            if (ob->cls == Cls::Host && onHostFree) onHostFree(static_cast<HostObj*>(ob));
            if (ob->cls == Cls::MapIter || ob->cls == Cls::SetIter) {
                IterObj* it = static_cast<IterObj*>(ob);
                if (!it->done && it->target.IsObj() && it->target.O()->marked) {
                    MapObj* m = static_cast<MapObj*>(it->target.O());
                    if (m->iterators) --m->iterators;
                }
            }
        }
    }
    // phase 2: free
    std::size_t live = 0;
    GcObj** link = &gcList_;
    while (GcObj* o = *link) {
        if (o->marked) {
            o->marked = false;
            live += o->Bytes();
            link = &o->gcNext;
        } else {
            *link = o->gcNext;
            delete o;
        }
    }
    bytes_ = live;
    // The next run comes after the heap doubles, but never later than halfway into the room that is left, and never
    // sooner than 1/64 of the limit more: a heap close to its limit used to sit above a fixed threshold and collect at
    // every safe point (each run walks the whole heap), which looked like a hang on big pages.
    const std::size_t room = memoryLimit > live ? memoryLimit - live : 0;
    const std::size_t latest = live + std::max(room / 2, memoryLimit / 64);
    gcThreshold_ = std::min(std::max<std::size_t>(4u << 20, live * 2), latest);
}

// ============================================================ console.log formatting
static void Quote(std::string& out, const std::u16string& s) {
    out += '"';
    for (char16_t c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += Utf16To8(std::u16string(1, c));
    }
    out += '"';
}

std::string Runtime::Display(const Value& v, int depth) {
    Charge(8);
    switch (v.t) {
        case Tag::String: return Utf16To8(v.S()->s);
        case Tag::Symbol: {
            Symbol* s = v.Y();
            return "Symbol(" + (s->desc ? Utf16To8(s->desc->s) : std::string()) + ")";
        }
        case Tag::Object: break;
        case Tag::BigInt: return Utf16To8(BigToString(*this, v, 10)) + "n";
        default: return ToUtf8(v);
    }
    Object* o = v.O();
    auto inner = [&](const Value& x) {
        if (x.IsStr()) { std::string q; Quote(q, x.S()->s); return q; }
        return Display(x, depth + 1);
    };
    if (o->IsCallable()) {
        Value n = Get(v, atoms->name);
        std::string nm = n.IsStr() ? Utf16To8(n.S()->s) : "";
        bool cls = o->cls == Cls::Closure && (static_cast<Closure*>(o)->code->flags & PF_ClassCtor);
        return std::string(cls ? "[class " : "[Function") + (nm.empty() ? (cls ? "(anonymous)" : "") : (cls ? "" : ": ") + nm) + "]";
    }
    if (o->cls == Cls::Error) {
        Value n = Get(v, atoms->name), m = Get(v, atoms->message);
        std::string s = n.IsStr() ? Utf16To8(n.S()->s) : "Error";
        if (m.IsStr() && !m.S()->s.empty()) s += ": " + Utf16To8(m.S()->s);
        return s;
    }
    if (o->cls == Cls::DateBox) return DateDisplay(o);
    if (o->cls == Cls::TypedArray) return TypedArrayDisplay(*this, o, depth);
    if (o->cls == Cls::BigIntBox) return "[BigInt: " + Display(static_cast<BoxObj*>(o)->prim, depth + 1) + "]";
    if (o->cls == Cls::BoolBox || o->cls == Cls::NumberBox || o->cls == Cls::StringBox)
        return "[" + std::string(o->cls == Cls::BoolBox ? "Boolean" : o->cls == Cls::NumberBox ? "Number" : "String") + ": " +
               inner(static_cast<BoxObj*>(o)->prim) + "]";
    if (o->cls == Cls::Host && o->proto && static_cast<HostObj*>(o)->hostClass < hostClasses.size()) {
        HostObj* h = static_cast<HostObj*>(o);
        if (hostClasses[h->hostClass].toString) return hostClasses[h->hostClass].toString(*this, h);
    }
    if (depth > 2) return o->cls == Cls::Array ? "[Array]" : "[Object]";
    if (o->cls == Cls::Map || o->cls == Cls::Set) {
        MapObj* m = static_cast<MapObj*>(o);
        std::string s = std::string(o->cls == Cls::Map ? "Map(" : "Set(") + std::to_string(m->live) + ") {";
        bool first = true;
        int shown = 0;
        for (const auto& e : m->entries) {
            if (!e.live) continue;
            if (++shown > 50) { s += ", ..."; break; }
            s += first ? " " : ", ";
            first = false;
            s += inner(e.k);
            if (o->cls == Cls::Map) s += " => " + inner(e.v);
        }
        return s + (first ? "}" : " }");
    }
    if (o->cls == Cls::Promise) {
        PromiseObj* p = static_cast<PromiseObj*>(o);
        return p->state == 0 ? "Promise { <pending> }" : p->state == 1 ? "Promise { " + inner(p->result) + " }" : "Promise { <rejected> " + inner(p->result) + " }";
    }
    if (o->cls == Cls::Array) {
        ArrayObj* a = static_cast<ArrayObj*>(o);
        std::string s = "[";
        std::uint32_t n = std::min<std::uint32_t>(a->length, 100);
        for (std::uint32_t i = 0; i < n; ++i) {
            if (i) s += ", ";
            Value e = i < a->elems.size() ? a->elems[i] : Value::EmptyV();
            if (e.IsEmpty() && a->sparse) {
                Prop* p = a->FindRaw(IndexKey(i));
                if (p && !(p->flags & kAccessor)) e = p->v;
            }
            s += e.IsEmpty() ? "<empty>" : inner(e);
        }
        if (a->length > n) s += ", ... " + std::to_string(a->length - n) + " more";
        return s + "]";
    }
    std::string s = "{";
    bool first = true;
    int shown = 0;
    for (const Prop& p : o->props) {
        if (!(p.flags & kEnumerable)) continue;
        if (++shown > 50) { s += ", ..."; break; }
        s += first ? " " : ", ";
        first = false;
        if (p.key->kind == Kind::Symbol) s += Display(Value::Sym(static_cast<Symbol*>(p.key)), depth + 1);
        else s += Utf16To8(static_cast<String*>(p.key)->s);
        s += ": ";
        if (p.flags & kAccessor) s += "[Getter/Setter]";
        else s += inner(p.v);
    }
    return s + (first ? "}" : " }");
}

// ============================================================ library helpers
NativeFunc* DefNative(Runtime& rt, Object* on, const char* name, NativeFn fn, int length, std::uint8_t flags) {
    NativeFunc* f = rt.NewNative(fn, name, length);
    on->AddRaw(rt.Intern(name), Value::Obj(f), flags);
    return f;
}
NativeFunc* DefCtor(Runtime& rt, const char* name, NativeFn fn, int length, Object* proto) {
    NativeFunc* f = rt.NewNative(fn, name, length);
    f->ctor = true;
    rt.MakeLazyProps(f);
    f->AddRaw(rt.atoms->prototype, Value::Obj(proto), 0);
    proto->AddRaw(rt.atoms->constructor, Value::Obj(f), kHidden);
    rt.global->AddRaw(rt.Intern(name), Value::Obj(f), kHidden);
    return f;
}
void DefValue(Runtime& rt, Object* on, const char* name, const Value& v, std::uint8_t flags) {
    on->AddRaw(rt.Intern(name), v, flags);
}
void DefGetter(Runtime& rt, Object* on, const char* name, NativeFn fn) {
    NativeFunc* f = rt.NewNative(fn, (std::string("get ") + name).c_str(), 0);
    Prop* p = on->AddRaw(rt.Intern(name), Value(), kConfigurable | kAccessor);
    p->get = f;
}
Object* ThisObject(Runtime& rt, CallArgs& a, const char* what) {
    if (!a.thisv.IsObj()) rt.ThrowType(std::string(what) + " needs an object as 'this'");
    return a.thisv.O();
}
std::int64_t RelIndex(Runtime& rt, const Value& v, std::int64_t len, std::int64_t dflt) {
    if (v.IsUndef()) return dflt;
    double d = rt.ToIntegerOrInfinity(v);
    if (d < 0) d = std::max(0.0, (double)len + d);
    else d = std::min(d, (double)len);
    return (std::int64_t)d;
}
bool HasIndex(Runtime& rt, Object* o, std::uint64_t i) {
    if ((o->cls == Cls::Array || o->cls == Cls::Arguments) && i < static_cast<ArrayObj*>(o)->elems.size()) {
        if (!static_cast<ArrayObj*>(o)->elems[(std::size_t)i].IsEmpty()) return true;
    }
    GcObj* k = i < 4294967295ull ? rt.IndexKey((std::uint32_t)i) : rt.Intern(rt.NumberToU16((double)i));
    return rt.HasProperty(o, k);
}
void DeleteIndex(Runtime& rt, Object* o, std::uint64_t i) {
    GcObj* k = i < 4294967295ull ? rt.IndexKey((std::uint32_t)i) : rt.Intern(rt.NumberToU16((double)i));
    if (!rt.Delete(o, k)) rt.ThrowType("can not delete element " + std::to_string(i));
}
String* NameOf(Runtime& rt, Object* fn) {
    Value n = rt.Get(Value::Obj(fn), rt.atoms->name);
    return n.IsStr() ? n.S() : rt.atoms->empty;
}
std::string KeyText(Runtime& rt, GcObj* key) {
    (void)rt;
    if (!key) return "?";
    if (key->kind == Kind::String) return Utf16To8(static_cast<String*>(key)->s);
    Symbol* s = static_cast<Symbol*>(key);
    return "Symbol(" + (s->desc ? Utf16To8(s->desc->s) : std::string()) + ")";
}

// ============================================================ setup
void Runtime::InitIntrinsics() {
    Atoms& A = *atoms;
    auto at = [&](const char* s) { return Intern(s); };
    A.length = at("length"); A.prototype = at("prototype"); A.constructor = at("constructor"); A.name = at("name");
    A.message = at("message"); A.stack = at("stack"); A.value = at("value"); A.done = at("done"); A.next = at("next");
    A.toString = at("toString"); A.valueOf = at("valueOf"); A.get = at("get"); A.set = at("set");
    A.writable = at("writable"); A.enumerable = at("enumerable"); A.configurable = at("configurable");
    A.then = at("then"); A.callee = at("callee"); A.cause = at("cause"); A.toJSON = at("toJSON");
    A.ret = at("return"); A.throw_ = at("throw"); A.index = at("index"); A.input = at("input"); A.join = at("join");
    A.lastIndex = at("lastIndex"); A.errors = at("errors"); A.status = at("status"); A.reason = at("reason");
    A.empty = at(""); A.undefined = at("undefined"); A.null_ = at("null"); A.true_ = at("true"); A.false_ = at("false");
    A.object = at("object"); A.function = at("function"); A.number = at("number"); A.string = at("string");
    A.boolean = at("boolean"); A.symbol = at("symbol"); A.default_ = at("default"); A.anonymous = at("anonymous");
    A.raw = at("raw");
    indexAtoms_.resize(1024, nullptr);
    for (std::uint32_t i = 0; i < 1024; ++i) {
        String* s = Intern(NumberToU16((double)i));
        s->index = i;
        indexAtoms_[i] = s;
    }
    objectProto = NewObject(nullptr);
    functionProto = NewNative([](Runtime&, CallArgs&) { return Value(); }, "", 0);
    functionProto->proto = objectProto;
    global = NewObject(objectProto);
    lexicals = NewObject(nullptr);
    auto wk = [&](const char* d) { return NewSymbol(Intern(d)); };
    symIterator = wk("Symbol.iterator");
    symAsyncIterator = wk("Symbol.asyncIterator");
    symToPrimitive = wk("Symbol.toPrimitive");
    symToStringTag = wk("Symbol.toStringTag");
    symHasInstance = wk("Symbol.hasInstance");
    symSpecies = wk("Symbol.species");
    InstallObjectLib(*this);
    InstallFunctionLib(*this);
    InstallErrorLib(*this);
    InstallArrayLib(*this);
    InstallStringLib(*this);
    InstallNumberLib(*this);
    InstallMathLib(*this);
    InstallBigIntLib(*this);
    InstallDateLib(*this);
    InstallJsonLib(*this);
    InstallCollectionsLib(*this);
    InstallWeakLib(*this);
    InstallBinaryLib(*this);
    InstallProxyLib(*this);
    InstallTextCodingLib(*this);
    InstallStructuredClone(*this);
    InstallRegExpLib(*this);
    InstallGlobalLib(*this);
    InstallIntlLib(*this);
}

// ============================================================ compiling and running
bool Runtime::Compile(const std::string& source, const std::string& chunk, Value& fn, SyntaxError* err) {
    std::vector<Token> toks;
    SyntaxError e;
    if (!Tokenize(source, toks, e)) {
        if (err) *err = e;
        return false;
    }
    Ast ast;
    if (!Parse(toks, ast, e)) {
        if (err) *err = e;
        return false;
    }
    Proto* p = nullptr;
    try {   // the API reports every failure as false: a memory stop here (interning names) must not escape
        p = CompileScript(*this, ast, chunk, e);
    } catch (const JsTerminate& t) {
        e = SyntaxError{};
        e.line = -1;   // not a syntax error: Run reports the message as it is
        e.message = t.why;
    } catch (const JsThrow&) {
        e = SyntaxError{};
        e.line = -1;
        e.message = ExceptionText(exception);
        exception = Value();
    }
    if (!p) {
        if (err) *err = e;
        return false;
    }
    vm->scripts.push_back(p);
    Closure* c;
    try {
        c = Alloc<Closure>();
    } catch (const JsTerminate& t) {
        if (err) { *err = SyntaxError{}; err->line = -1; err->message = t.why; }
        return false;
    }
    c->code = p;
    c->proto = functionProto;
    fn = Value::Obj(c);
    return true;
}

std::vector<SyntaxError> Runtime::CheckSyntax(const std::string& source, const std::string& chunk) {
    std::vector<Token> toks;
    SyntaxError e;
    if (!Tokenize(source, toks, e)) return {e};
    Ast ast;
    if (!Parse(toks, ast, e)) return {e};
    // the compiler finds the rest (redeclarations, break targets); throw its output away
    Runtime* self = this;
    Proto* p = nullptr;
    try {
        p = CompileScript(*self, ast, chunk, e);
    } catch (const JsTerminate&) {
        return {};   // out of memory while checking: no syntax error to report
    } catch (const JsThrow&) {
        exception = Value();
        return {};
    }
    if (!p) return {e};
    return {};
}

bool Runtime::Run(const std::string& source, const std::string& chunk, std::string* err, Value* exported) {
    Value fn;
    SyntaxError se;
    if (!Compile(source, chunk, fn, &se)) {
        if (err) *err = se.line < 0 ? se.message : chunk + ":" + std::to_string(se.line) + ": SyntaxError: " + se.message;
        return false;
    }
    Value r;
    if (!CallHost(fn, Value(), {}, &r, err)) return false;
    if (exported) *exported = r;
    return true;
}

// ============================================================ modules (16.2)
// Written from ECMA-262 16.2.1.5 (Cyclic Module Records: Link and Evaluate), 16.2.1.6 (Source Text Module Records:
// ParseModule, GetExportedNames, ResolveExport, InitializeEnvironment), 16.2.1.10 GetModuleNamespace, 10.4.6 (module
// namespace exotic objects), 13.3.10 import() and 13.3.12 import.meta. Our choices: the host's resolve and fetch are
// synchronous; modules run one after another in dependency order (a module waiting on a top-level await holds up the
// ones after it, where the spec may run independent ones in between); a function a module exports is made when that
// module starts to run, not when it is linked (in a cycle, a call before that sees undefined). Loading, linking and the
// export searches are loops or bounded recursions (kMaxResolveDepth), so a long chain of modules is not a deep native
// recursion; every module and every export step charges the budget.
void ModuleNsObj::Trace(Runtime& rt) {
    Object::Trace(rt);
    for (const Entry& e : entries) {
        rt.Mark(e.name);
        if (e.cell) rt.Mark(e.cell);
    }
}
const ModuleNsObj::Entry* ModuleNsObj::Find(GcObj* key) const {
    if (key->kind != Kind::String) return nullptr;
    const std::u16string& k = static_cast<String*>(key)->s;
    std::size_t lo = 0, hi = entries.size();
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        const std::u16string& n = entries[mid].name->s;
        if (n == k) return &entries[mid];
        if (n < k) lo = mid + 1;
        else hi = mid;
    }
    return nullptr;
}

namespace {

const std::u16string kNamespaceBinding = u"*namespace*";
constexpr std::size_t kMaxModules = 20000;
constexpr int kMaxResolveDepth = 1000;

[[noreturn]] void ModuleSyntaxError(Runtime& rt, const std::string& msg) { rt.ThrowError(ErrorType::SyntaxError, msg); }

ModuleRec* NewModuleRec(Runtime& rt, const std::string& key, const std::string& source) {
    Runtime::Impl& M = *rt.vm;
    if (M.moduleList.size() >= kMaxModules) rt.ThrowRange("too many modules (" + std::to_string(kMaxModules) + ")");
    rt.Charge(64 + source.size() / 64);
    std::vector<Token> toks;
    SyntaxError e;
    Ast ast;
    if (!Tokenize(source, toks, e) || !Parse(toks, ast, e, true))
        ModuleSyntaxError(rt, key + ":" + std::to_string(e.line) + ": " + e.message);
    std::vector<ModuleExtern> externs;
    Proto* p = CompileModule(rt, ast, key, e, externs);
    if (!p) ModuleSyntaxError(rt, key + ":" + std::to_string(e.line) + ": " + e.message);
    M.scripts.push_back(p);
    auto rec = std::make_unique<ModuleRec>();
    rec->key = key;
    rec->info = std::move(*ast.module);
    rec->externs = std::move(externs);
    rec->proto = p;
    rec->index = (std::uint32_t)M.moduleList.size();
    ModuleRec* m = rec.get();
    M.moduleList.push_back(std::move(rec));
    M.moduleMap[key] = m;
    // the cells other modules link to: exported bindings (var and function start undefined, the rest not initialized)
    // and namespace imports (filled in when linked)
    for (const ModuleExtern& x : m->externs) {
        if (x.kind == 3) continue;
        bool nsImport = false;
        if (x.kind == 2) {
            for (const ModuleInfo::Import& im : m->info.imports) nsImport |= im.localName == x.name && im.importName == u"*";
            if (!nsImport) continue;
        }
        Cell* c = rt.Alloc<Cell>();
        c->v = x.kind == 1 ? Value::EmptyV() : Value();
        m->cells[x.name] = c;
    }
    return m;
}

ModuleRec* FetchModule(Runtime& rt, const std::string& key) {
    auto it = rt.vm->moduleMap.find(key);
    if (it != rt.vm->moduleMap.end()) return it->second;
    std::string source, why;
    if (!rt.modules.fetch || !rt.modules.fetch(key, source, why)) rt.ThrowType("can not load the module '" + key + "': " + why);
    return NewModuleRec(rt, key, source);
}

// 16.2.1.5.1 LoadRequestedModules, as a work list: every module of the graph is loaded once
ModuleRec* LoadGraph(Runtime& rt, const std::string& key, const std::string* source) {
    auto it = rt.vm->moduleMap.find(key);
    ModuleRec* root = it != rt.vm->moduleMap.end() ? it->second : source ? NewModuleRec(rt, key, *source) : FetchModule(rt, key);
    std::vector<ModuleRec*> work{root};
    while (!work.empty()) {
        ModuleRec* m = work.back();
        work.pop_back();
        if (m->deps.size() == m->info.requests.size()) continue;
        m->deps.clear();   // a load that failed half way is done again
        for (const std::u16string& req : m->info.requests) {
            const std::string spec = Utf16To8(req);
            std::string k, why;
            if (!rt.modules.resolve || !rt.modules.resolve(spec, m->key, k, why))
                rt.ThrowType("can not load the module '" + spec + "' (imported by " + m->key + "): " + why);
            const bool known = rt.vm->moduleMap.count(k) > 0;
            ModuleRec* d = FetchModule(rt, k);
            m->deps.push_back(d);
            if (!known) work.push_back(d);
        }
    }
    return root;
}

struct Resolution {
    ModuleRec* m = nullptr;
    std::u16string binding;
    int status = 0;   ///< 0 not found, 1 found, 2 ambiguous
};
struct ResolveSet {
    std::unordered_set<std::u16string> seen;   ///< "index:name"
};

// 16.2.1.6.3 ResolveExport
Resolution ResolveExport(Runtime& rt, ModuleRec* m, const std::u16string& name, ResolveSet& set, int depth) {
    if (depth > kMaxResolveDepth) ModuleSyntaxError(rt, "the exports of '" + m->key + "' are re-exported through too many modules");
    rt.Charge(1);
    if (!set.seen.insert(AsciiTo16(std::to_string(m->index).c_str()) + u":" + name).second) return {};   // a circular request
    for (const ModuleInfo::Export& e : m->info.exports)
        if (!e.localName.empty() && e.exportName == name) return Resolution{m, e.localName, 1};
    for (const ModuleInfo::Export& e : m->info.exports) {
        if (e.req < 0 || e.exportName.empty() || e.exportName != name) continue;
        ModuleRec* d = m->deps[(std::size_t)e.req];
        if (e.importName == u"*") return Resolution{d, kNamespaceBinding, 1};
        return ResolveExport(rt, d, e.importName, set, depth + 1);
    }
    if (name == u"default") return {};   // export * does not pass on a default
    Resolution star;
    for (const ModuleInfo::Export& e : m->info.exports) {
        if (e.req < 0 || !e.exportName.empty()) continue;
        Resolution r = ResolveExport(rt, m->deps[(std::size_t)e.req], name, set, depth + 1);
        if (r.status == 2) return r;
        if (r.status != 1) continue;
        if (star.status == 0) star = r;
        else if (r.m != star.m || r.binding != star.binding) {
            star.status = 2;
            return star;
        }
    }
    return star;
}
Resolution Resolve(Runtime& rt, ModuleRec* m, const std::u16string& name) {
    ResolveSet set;
    return ResolveExport(rt, m, name, set, 0);
}

// 16.2.1.6.2 GetExportedNames
void ExportedNames(Runtime& rt, ModuleRec* m, std::unordered_set<ModuleRec*>& visited, std::vector<std::u16string>& out,
                   std::unordered_set<std::u16string>& have, int depth) {
    if (depth > kMaxResolveDepth) ModuleSyntaxError(rt, "the exports of '" + m->key + "' are re-exported through too many modules");
    if (!visited.insert(m).second) return;
    for (const ModuleInfo::Export& e : m->info.exports) {
        rt.Charge(1);
        if (!e.exportName.empty() && have.insert(e.exportName).second) out.push_back(e.exportName);
    }
    for (const ModuleInfo::Export& e : m->info.exports) {
        if (e.req < 0 || !e.exportName.empty()) continue;
        std::vector<std::u16string> star;
        std::unordered_set<std::u16string> none;
        ExportedNames(rt, m->deps[(std::size_t)e.req], visited, star, none, depth + 1);
        for (std::u16string& n : star)
            if (n != u"default" && have.insert(n).second) out.push_back(std::move(n));
    }
}

// 16.2.1.10 GetModuleNamespace
ModuleNsObj* Namespace(Runtime& rt, ModuleRec* m) {
    if (m->ns) return m->ns;
    std::unordered_set<ModuleRec*> visited;
    std::vector<std::u16string> names;
    std::unordered_set<std::u16string> have;
    ExportedNames(rt, m, visited, names, have, 0);
    std::sort(names.begin(), names.end());
    ModuleNsObj* ns = rt.Alloc<ModuleNsObj>();
    ns->proto = nullptr;
    ns->extensible = false;
    for (const std::u16string& n : names) {
        Resolution r = Resolve(rt, m, n);
        if (r.status != 1) continue;   // ambiguous names are left out (16.2.1.10 step 3.c)
        Cell* c = nullptr;
        if (r.binding != kNamespaceBinding) {
            auto it = r.m->cells.find(r.binding);
            if (it == r.m->cells.end()) ModuleSyntaxError(rt, "internal error: no binding for the export '" + Utf16To8(n) + "'");
            c = it->second;
        }
        ns->entries.push_back(ModuleNsObj::Entry{rt.Intern(n), c, r.m});
    }
    ns->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("Module")), 0);
    m->ns = ns;
    return ns;
}

// 16.2.1.6.4 InitializeEnvironment: check the re-exports, find each import's cell, make the module function
void Initialize(Runtime& rt, ModuleRec* m) {
    for (const ModuleInfo::Export& e : m->info.exports) {
        if (e.req < 0 || e.exportName.empty() || e.importName == u"*") continue;
        Resolution r = Resolve(rt, m, e.exportName);
        if (r.status != 1)
            ModuleSyntaxError(rt, m->key + ": the re-export '" + Utf16To8(e.exportName) + "' " +
                                      (r.status == 2 ? "is ambiguous" : "is not exported by '" + m->deps[(std::size_t)e.req]->key + "'"));
    }
    std::vector<Cell*> ext;
    ext.reserve(m->externs.size());
    for (const ModuleExtern& x : m->externs) {
        if (x.kind <= 1) {
            ext.push_back(m->cells.at(x.name));
            continue;
        }
        if (x.kind == 3) {   // import.meta: an object with no prototype (13.3.12.1)
            Object* meta = rt.NewObject(nullptr);
            if (rt.modules.initMeta) rt.modules.initMeta(rt, m->key, meta);
            else rt.DefineData(meta, rt.Intern("url"), Value::Str(rt.NewStringUtf8(m->key)));
            Cell* c = rt.Alloc<Cell>();
            c->v = Value::Obj(meta);
            ext.push_back(c);
            continue;
        }
        const ModuleInfo::Import* im = nullptr;
        for (const ModuleInfo::Import& i : m->info.imports)
            if (i.localName == x.name) im = &i;
        ModuleRec* d = m->deps[(std::size_t)im->req];
        if (im->importName == u"*") {
            Cell* c = m->cells.at(x.name);
            c->v = Value::Obj(Namespace(rt, d));
            ext.push_back(c);
            continue;
        }
        Resolution r = Resolve(rt, d, im->importName);
        if (r.status != 1)
            ModuleSyntaxError(rt, m->key + ":" + std::to_string(im->line) + ": the module '" + d->key + "' " +
                                      (r.status == 2 ? "has more than one export named '" : "does not provide an export named '") +
                                      Utf16To8(im->importName) + "'");
        if (r.binding == kNamespaceBinding) {
            Cell* c = rt.Alloc<Cell>();
            c->v = Value::Obj(Namespace(rt, r.m));
            ext.push_back(c);
        } else ext.push_back(r.m->cells.at(r.binding));
    }
    Closure* fn = rt.Alloc<Closure>();
    fn->code = m->proto;
    fn->proto = rt.functionProto;
    fn->ups = std::move(ext);
    m->fn = fn;
    m->state = ModuleRec::Linked;
}

// every module reachable from root, dependencies first (a post-order walk with an explicit stack)
std::vector<ModuleRec*> PostOrder(ModuleRec* root) {
    std::vector<ModuleRec*> order;
    std::unordered_set<ModuleRec*> seen{root};
    std::vector<std::pair<ModuleRec*, std::size_t>> stack{{root, 0}};
    while (!stack.empty()) {
        auto& top = stack.back();
        if (top.second < top.first->deps.size()) {
            ModuleRec* d = top.first->deps[top.second++];
            if (seen.insert(d).second) stack.push_back({d, 0});
            continue;
        }
        order.push_back(top.first);
        stack.pop_back();
    }
    return order;
}

// 16.2.1.5.1 Link: a failure leaves the modules of this pass unlinked
void Link(Runtime& rt, ModuleRec* root) {
    std::vector<ModuleRec*> linked;
    try {
        for (ModuleRec* m : PostOrder(root)) {
            if (m->state != ModuleRec::Loaded) continue;
            Initialize(rt, m);
            linked.push_back(m);
        }
    } catch (...) {
        for (ModuleRec* m : linked) {
            m->state = ModuleRec::Loaded;
            m->fn = nullptr;
        }
        throw;
    }
}

// One evaluation (16.2.1.5.3): the modules in order; an async one (top-level await) continues the job when it settles.
struct EvalJob : Object {
    std::vector<ModuleRec*> order;
    std::size_t next = 0;
    PromiseObj* result = nullptr;
    ModuleRec* root = nullptr;
    EvalJob() : Object(Cls::Opaque) {}
    void Trace(Runtime& rt) override {
        Object::Trace(rt);
        if (result) rt.Mark(result);
    }
};

void RunEval(Runtime& rt, EvalJob* job);

void ModuleFailed(Runtime& rt, ModuleRec* m, const Value& why) {
    m->state = ModuleRec::Errored;
    m->error = why;
    if (m->done) RejectPromise(rt, m->done, why);
}

Value EvalSettled(Runtime& rt, CallArgs& a, bool ok) {
    NativeFunc* f = a.callee;
    EvalJob* job = static_cast<EvalJob*>(f->slots[0].O());
    ModuleRec* m = rt.vm->moduleList[(std::size_t)f->slots[1].n].get();
    const bool waitOnly = f->slots[2].b;
    if (!waitOnly) {
        if (ok) {
            m->state = ModuleRec::Evaluated;
            ResolvePromise(rt, m->done, Value());
            ++job->next;
        } else ModuleFailed(rt, m, a[0]);
    }
    RunEval(rt, job);
    return Value();
}

void WhenSettled(Runtime& rt, EvalJob* job, PromiseObj* p, ModuleRec* m, bool waitOnly) {
    p->handled = true;
    std::vector<Value> slots = {Value::Obj(job), Value::Num(m->index), Value::Bool(waitOnly)};
    NativeFunc* onOk = rt.NewNative([](Runtime& rt, CallArgs& a) { return EvalSettled(rt, a, true); }, "", 1, slots);
    NativeFunc* onFail = rt.NewNative([](Runtime& rt, CallArgs& a) { return EvalSettled(rt, a, false); }, "", 1, slots);
    Value then = rt.Get(Value::Obj(p), rt.atoms->then);
    rt.Call(then, Value::Obj(p), {Value::Obj(onOk), Value::Obj(onFail)});
}

void RunEval(Runtime& rt, EvalJob* job) {
    rt.Keep(job);
    while (job->next < job->order.size()) {
        ModuleRec* m = job->order[job->next];
        rt.Charge(1);
        if (m->state == ModuleRec::Evaluated) { ++job->next; continue; }
        if (m->state == ModuleRec::Errored) {
            RejectPromise(rt, job->result, m->error);
            return;
        }
        if (m->state == ModuleRec::Evaluating) {   // another import is running it (a top-level await): wait for that
            WhenSettled(rt, job, m->done, m, true);
            return;
        }
        if (m->state != ModuleRec::Linked) {
            RejectPromise(rt, job->result, Value::Obj(rt.NewError(ErrorType::TypeError, "the module '" + m->key + "' is not linked")));
            return;
        }
        bool depFailed = false;
        for (ModuleRec* d : m->deps)
            if (d->state == ModuleRec::Errored) {
                ModuleFailed(rt, m, d->error);
                depFailed = true;
                break;
            }
        if (depFailed) continue;   // the loop sees it errored and rejects
        m->state = ModuleRec::Evaluating;
        m->done = NewPromise(rt);
        m->done->handled = true;
        Value r;
        try {
            r = rt.Call(Value::Obj(m->fn), Value(), nullptr, 0);
        } catch (const JsThrow&) {
            Value e = rt.exception;
            rt.exception = Value();
            ModuleFailed(rt, m, e);
            continue;
        }
        if (!r.IsObj() || r.O()->cls != Cls::Promise) {
            m->state = ModuleRec::Evaluated;
            ResolvePromise(rt, m->done, Value());
            ++job->next;
            continue;
        }
        PromiseObj* p = static_cast<PromiseObj*>(r.O());
        if (p->state == 1) {
            m->state = ModuleRec::Evaluated;
            ResolvePromise(rt, m->done, Value());
            ++job->next;
            continue;
        }
        if (p->state == 2) {
            p->handled = true;
            ModuleFailed(rt, m, p->result);
            continue;
        }
        WhenSettled(rt, job, p, m, false);
        return;
    }
    ResolvePromise(rt, job->result, Value::Obj(Namespace(rt, job->root)));
}

void StartEval(Runtime& rt, ModuleRec* root, PromiseObj* result) {
    EvalJob* job = rt.Alloc<EvalJob>();
    job->proto = nullptr;
    job->order = PostOrder(root);
    job->result = result;
    job->root = root;
    RunEval(rt, job);
}

Value ImportJob(Runtime& rt, CallArgs& a) {   // a job: the rest of import() (16.2.1.9 ContinueDynamicImport)
    NativeFunc* f = a.callee;
    PromiseObj* p = static_cast<PromiseObj*>(f->slots[2].O());
    const std::string spec = Utf16To8(f->slots[0].S()->s), referrer = Utf16To8(f->slots[1].S()->s);
    try {
        std::string key, why;
        if (!rt.modules.resolve(spec, referrer, key, why)) rt.ThrowType("can not load the module '" + spec + "': " + why);
        ModuleRec* root = LoadGraph(rt, key, nullptr);
        Link(rt, root);
        StartEval(rt, root, p);
    } catch (const JsThrow&) {
        Value e = rt.exception;
        rt.exception = Value();
        RejectPromise(rt, p, e);
    }
    return Value();
}

} // namespace

Value ModuleNamespaceGet(Runtime& rt, const ModuleNsObj::Entry& e) {
    if (!e.cell) return Value::Obj(Namespace(rt, e.module));
    if (e.cell->v.IsEmpty()) rt.ThrowError(ErrorType::ReferenceError, "'" + Utf16To8(e.name->s) + "' is used before it is initialized");
    return e.cell->v;
}

void DynamicImport(Runtime& rt, PromiseObj* p, const std::string& specifier, const std::string& referrer) {
    NativeFunc* job = rt.NewNative(ImportJob, "", 0,
                                   {Value::Str(rt.NewStringUtf8(specifier)), Value::Str(rt.NewStringUtf8(referrer)), Value::Obj(p)});
    PromiseObj* ready = PromiseResolveValue(rt, Value());
    Value then = rt.Get(Value::Obj(ready), rt.atoms->then);
    rt.Call(then, Value::Obj(ready), {Value::Obj(job)});
}

bool Runtime::RunModule(const std::string& source, const std::string& key, std::string* err, Value* ns) {
    Impl& M = *vm;
    Value* sp0 = M.sp;
    int nf = M.nframes;
    std::size_t nh = M.handlers.size();
    int nd = M.nativeDepth;
    const bool top = M.nativeDepth == 0 && M.nframes == 0;
    try {
        ModuleRec* root = LoadGraph(*this, key, &source);
        Link(*this, root);
        PromiseObj* result = NewPromise(*this);
        result->handled = true;
        Keep(result);
        StartEval(*this, root, result);
        RunJobs();
        if (result->state == 2) {
            if (err) *err = ExceptionText(result->result);
        } else {
            if (ns && result->state == 1) *ns = result->result;
            if (top) {
                scratch_.clear();
                if (ns) Keep(*ns);
            }
            return true;
        }
    } catch (const JsThrow&) {
        if (err) *err = ExceptionText(exception);
        exception = Value();
    } catch (const JsTerminate& t) {
        if (err) *err = t.why;
        M.jobs.clear();
    }
    M.sp = sp0;
    M.nframes = nf;
    M.handlers.resize(nh);
    M.nativeDepth = nd;
    if (top) scratch_.clear();
    return false;
}

} // namespace okay::js
