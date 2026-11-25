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

namespace okay::js {

// ============================================================ equality (7.2.10 - 7.2.16)
bool SameValueZero(const Value& a, const Value& b) {
    if (a.t != b.t) return false;
    switch (a.t) {
        case Tag::Undefined: case Tag::Null: case Tag::Empty: return true;
        case Tag::Bool: return a.b == b.b;
        case Tag::Number: return a.n == b.n || (std::isnan(a.n) && std::isnan(b.n));
        case Tag::String: return a.p == b.p || a.S()->s == b.S()->s;
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
        case Tag::Object: {
            Value p = ToPrimitive(v, 1);
            return ToNumber(p);
        }
        default: return NAN;
    }
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
    Cls c = v.IsBool() ? Cls::BoolBox : v.IsNum() ? Cls::NumberBox : v.IsStr() ? Cls::StringBox : Cls::SymbolBox;
    BoxObj* b = Alloc<BoxObj>();
    b->cls = c;
    b->prim = v;
    b->proto = v.IsBool() ? booleanProto : v.IsNum() ? numberProto : v.IsStr() ? stringProto : symbolProto;
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
    return false;
}

const char* Runtime::TypeOf(const Value& v) const {
    switch (v.t) {
        case Tag::Undefined: case Tag::Empty: return "undefined";
        case Tag::Null: return "object";
        case Tag::Bool: return "boolean";
        case Tag::Number: return "number";
        case Tag::String: return "string";
        case Tag::Symbol: return "symbol";
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
    if (a.IsBool()) return LooseEquals(Value::Num(a.b ? 1 : 0), b);
    if (b.IsBool()) return LooseEquals(a, Value::Num(b.b ? 1 : 0));
    if ((a.IsNum() || a.IsStr() || a.IsSym()) && b.IsObj()) {
        Value p = ToPrimitive(b, 0);
        Keep(p);
        return LooseEquals(a, p);
    }
    if (a.IsObj() && (b.IsNum() || b.IsStr() || b.IsSym())) {
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
    for (Object* o = v.O()->proto; o; o = o->proto) {
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
        default:
            ThrowType("can not read '" + KeyText(*this, key) + "' of " + (base.IsNull() ? "null" : "undefined"));
    }
    int guard = 0;
    for (; o; o = o->proto) {
        if (o->cls != Cls::Ordinary) {
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
                  : base.IsStr() ? stringProto : base.IsNum() ? numberProto : base.IsBool() ? booleanProto : symbolProto;
    int guard = 0;
    for (; q; q = q->proto) {
        if (q->cls != Cls::Ordinary) {
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
    if ((o->cls == Cls::Array || o->cls == Cls::Arguments) && (KeyIndex(key) >= 0 || key == atoms->length)) {
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
    if (o->lazy) MakeLazyProps(o);
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
            if (p.key->kind == Kind::Symbol) out.push_back(p.key);
    return out;
}

bool Runtime::SetPrototype(Object* o, Object* proto) {
    if (o->proto == proto) return true;
    if (!o->extensible) return false;
    for (Object* p = proto; p; p = p->proto)
        if (p == o) return false;
    o->proto = proto;
    return true;
}

bool Runtime::PreventExtensions(Object* o) {
    if (o->lazy) MakeLazyProps(o);
    o->extensible = false;
    return true;
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
                      arrayCtor, objectCtor, throwTypeErrorFn, vm->arrayValues, vm->arrayIterNext,
                      vm->stringIterator, vm->stringIterNext};
    for (Object* o : intr) if (o) Mark(o);
    for (Object* o : errorProtos) if (o) Mark(o);
    for (Symbol* s : {symIterator, symAsyncIterator, symToPrimitive, symToStringTag, symHasInstance}) if (s) Mark(s);
    for (auto& kv : symbolRegistry) Mark(kv.second);
    String** a = reinterpret_cast<String**>(atoms.get());
    for (std::size_t i = 0; i < sizeof(Atoms) / sizeof(String*); ++i) if (a[i]) Mark(a[i]);
    for (String* s : indexAtoms_) if (s) Mark(s);
    while (!gray_.empty()) {
        GcObj* o = gray_.back();
        gray_.pop_back();
        o->Trace(*this);
    }
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
    gcThreshold_ = std::max<std::size_t>(4u << 20, live * 2);
    if (gcThreshold_ > memoryLimit) gcThreshold_ = memoryLimit - memoryLimit / 8;
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
    InstallObjectLib(*this);
    InstallFunctionLib(*this);
    InstallErrorLib(*this);
    InstallArrayLib(*this);
    InstallStringLib(*this);
    InstallNumberLib(*this);
    InstallMathLib(*this);
    InstallJsonLib(*this);
    InstallCollectionsLib(*this);
    InstallGlobalLib(*this);
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
    Proto* p = CompileScript(*this, ast, chunk, e);
    if (!p) {
        if (err) *err = e;
        return false;
    }
    vm->scripts.push_back(p);
    Closure* c = Alloc<Closure>();
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
    Proto* p = CompileScript(*self, ast, chunk, e);
    if (!p) return {e};
    return {};
}

bool Runtime::Run(const std::string& source, const std::string& chunk, std::string* err, Value* exported) {
    Value fn;
    SyntaxError se;
    if (!Compile(source, chunk, fn, &se)) {
        if (err) *err = chunk + ":" + std::to_string(se.line) + ": SyntaxError: " + se.message;
        return false;
    }
    Value r;
    if (!CallHost(fn, Value(), {}, &r, err)) return false;
    if (exported) *exported = r;
    return true;
}

} // namespace okay::js
