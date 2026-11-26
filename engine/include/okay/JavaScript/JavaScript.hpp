#pragma once
// okay::js - our own JavaScript engine (ECMAScript, ECMA-262), written from the
// specification only. Feature map 13.9, record: docs/scripting/javascript.md.
//
// The pieces (each says which spec chapters it was written from):
//   JsLexer.cpp        source text -> tokens            (ECMA-262 ch. 12, lexical grammar)
//   JsParser.cpp       tokens -> syntax tree            (ch. 13-16, the syntactic grammar)
//   JsCompiler.cpp     syntax tree -> our own bytecode  (ch. 9 environments, ch. 14 statement semantics)
//   JsInterpreter.cpp  the bytecode machine             (ch. 10 function objects, 27.5 generators, 27.7 async)
//   JsRuntime.cpp      values, objects, properties,     (ch. 6 types, 7 abstract operations, 10.1 ordinary
//                      conversions, collector            objects, 10.4 exotic objects)
//   JsLib*.cpp         the library                      (ch. 19-28)
//
// Values are numbers (doubles), strings (UTF-16 code units, as the spec defines
// them), booleans, null, undefined, symbols and objects. Memory is managed by a
// mark-and-sweep collector that runs only at safe points of the bytecode
// machine (see Runtime::Collect for the rooting rules). A run budget counts
// bytecode instructions and library work; when it runs out the script stops
// with an error JavaScript cannot catch, so a game script can not freeze the game.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace okay::js {

struct GcObj;
struct String;
struct Symbol;
struct Object;
struct Proto;
struct Cell;
struct BigIntV;
class Runtime;

enum class Tag : std::uint8_t {
    Undefined = 0,   ///< zeroed memory is undefined (the value stack relies on it)
    Null,
    Bool,
    Number,
    String,
    Symbol,
    Object,
    Empty,      ///< internal: an array hole or a let/const binding not initialized yet
    Internal,   ///< internal: a Cell or an iterator record on the value stack
    BigInt,     ///< a BigInt (6.1.6.2): an immutable BigIntV
};

struct Value {
    Tag t;
    union {
        double n;
        bool b;
        GcObj* p;
    };
    Value() : t(Tag::Undefined), n(0) {}
    static Value Undef() { return Value(); }
    static Value Null() { Value v; v.t = Tag::Null; return v; }
    static Value Bool(bool x) { Value v; v.t = Tag::Bool; v.n = 0; v.b = x; return v; }
    static Value Num(double x) { Value v; v.t = Tag::Number; v.n = x; return v; }
    static Value Str(String* s) { Value v; v.t = Tag::String; v.p = reinterpret_cast<GcObj*>(s); return v; }
    static Value Sym(Symbol* s) { Value v; v.t = Tag::Symbol; v.p = reinterpret_cast<GcObj*>(s); return v; }
    static Value Obj(Object* o) { Value v; v.t = Tag::Object; v.p = reinterpret_cast<GcObj*>(o); return v; }
    static Value EmptyV() { Value v; v.t = Tag::Empty; return v; }
    static Value Internal(GcObj* o) { Value v; v.t = Tag::Internal; v.p = o; return v; }
    static Value Big(BigIntV* b) { Value v; v.t = Tag::BigInt; v.p = reinterpret_cast<GcObj*>(b); return v; }

    bool IsUndef() const { return t == Tag::Undefined; }
    bool IsNull() const { return t == Tag::Null; }
    bool IsNullish() const { return t == Tag::Undefined || t == Tag::Null; }
    bool IsBool() const { return t == Tag::Bool; }
    bool IsNum() const { return t == Tag::Number; }
    bool IsStr() const { return t == Tag::String; }
    bool IsSym() const { return t == Tag::Symbol; }
    bool IsObj() const { return t == Tag::Object; }
    bool IsEmpty() const { return t == Tag::Empty; }
    bool IsBig() const { return t == Tag::BigInt; }
    bool IsGc() const { return t == Tag::String || t == Tag::Symbol || t == Tag::Object || t == Tag::Internal || t == Tag::BigInt; }
    String* S() const { return reinterpret_cast<String*>(p); }
    Symbol* Y() const { return reinterpret_cast<Symbol*>(p); }
    Object* O() const { return reinterpret_cast<Object*>(p); }
};

/// SameValue (spec 7.2.10) and SameValueZero (7.2.11) and IsStrictlyEqual (7.2.16).
bool SameValue(const Value& a, const Value& b);
bool SameValueZero(const Value& a, const Value& b);
bool StrictEquals(const Value& a, const Value& b);

enum class Kind : std::uint8_t { String, Symbol, Object, Cell, Proto, BigInt };

struct GcObj {
    GcObj* gcNext = nullptr;
    Kind kind;
    bool marked = false;
    explicit GcObj(Kind k) : kind(k) {}
    virtual ~GcObj() = default;
    virtual void Trace(Runtime&) {}
    virtual std::size_t Bytes() const { return 32; }
};

struct String : GcObj {
    std::u16string s;
    mutable std::uint32_t hash = 0;
    mutable std::int64_t index = -2;   ///< as an array index: -2 not computed, -1 not one
    bool atom = false;                 ///< in the runtime's key table
    String() : GcObj(Kind::String) {}
    std::size_t Bytes() const override { return sizeof(String) + s.capacity() * 2; }
    std::int64_t ArrayIndex() const;   ///< the canonical uint32 index < 2^32-1 this string names, or -1
};

struct Symbol : GcObj {
    String* desc = nullptr;     ///< nullptr = no description
    bool registered = false;    ///< made by Symbol.for
    Symbol() : GcObj(Kind::Symbol) {}
    void Trace(Runtime& rt) override;
};

/// A captured variable (the spec's binding in an environment record that a closure keeps alive).
struct Cell : GcObj {
    Value v;
    Cell() : GcObj(Kind::Cell) {}
    void Trace(Runtime& rt) override;
};

/// Property attributes (spec 6.1.7.1).
enum PropFlag : std::uint8_t {
    kWritable = 1,
    kEnumerable = 2,
    kConfigurable = 4,
    kAccessor = 8,
    kDefault = kWritable | kEnumerable | kConfigurable,   ///< a plain assignment's attributes
    kHidden = kWritable | kConfigurable,                  ///< built-in methods: not enumerable
};

struct Prop {
    GcObj* key;          ///< an atom String or a Symbol
    Value v;             ///< data property value
    Object* get = nullptr;
    Object* set = nullptr;
    std::uint8_t flags = kDefault;
};

/// What kind of object (the spec's ordinary and exotic objects, plus our internal ones).
enum class Cls : std::uint8_t {
    Ordinary, Array, Arguments, Closure, Native, Bound, Error, BoolBox, NumberBox, StringBox, SymbolBox,
    Map, Set, Generator, Promise, ArrayIter, MapIter, SetIter, StringIter, RegExpLike, Host, IterRecord, ForIn,
    DateBox,   ///< a Date (21.4): a BoxObj whose prim is the time value
    WeakMap, WeakSet, WeakRef, FinalizationRegistry,
    BigIntBox,   ///< Object(1n): a BoxObj around a BigInt
    Intl,        ///< an Intl.NumberFormat / DateTimeFormat / ... object (JsLibNumber.cpp)   ///< WeakObj (26.1, 26.2, 24.3, 24.4)
    ArrayBuffer, ///< an ArrayBuffer or SharedArrayBuffer (25.1, 25.2): a BufferObj
    TypedArray,  ///< an integer-indexed exotic object (10.4.5, 23.2): a TypedArrayObj
    DataView,    ///< a DataView (25.3): a DataViewObj
    Proxy,       ///< a proxy exotic object (10.5): a ProxyObj
    Opaque,      ///< an internal object with no script-visible behaviour (TextEncoder / TextDecoder state, module records)
};

struct Object : GcObj {
    Cls cls;
    bool extensible = true;
    bool lazy = false;   ///< a function whose name / length / prototype are made on first look
    std::uint8_t proxyFn = 0;   ///< a proxy: 1 its target is callable, 2 also a constructor (10.5.15 ProxyCreate)
    Object* proto = nullptr;
    std::vector<Prop> props;
    std::unique_ptr<std::unordered_map<GcObj*, std::uint32_t>> index;   ///< key -> props position, for big objects
    explicit Object(Cls c = Cls::Ordinary) : GcObj(Kind::Object), cls(c) {}
    void Trace(Runtime& rt) override;
    std::size_t Bytes() const override;
    bool IsCallable() const { return cls == Cls::Closure || cls == Cls::Native || cls == Cls::Bound || proxyFn != 0; }
    /// Own property by key (no lazy step, no exotic part), or nullptr.
    Prop* FindRaw(GcObj* key);
    const Prop* FindRaw(GcObj* key) const { return const_cast<Object*>(this)->FindRaw(key); }
    Prop* AddRaw(GcObj* key, const Value& v, std::uint8_t flags);
    void RemoveRaw(GcObj* key);
};

/// Arrays and argument objects: a dense element part plus the length (spec 10.4.2).
/// Elements at or past elems.size() but under length live in props under their string keys.
struct ArrayObj : Object {
    std::vector<Value> elems;   ///< Tag::Empty = a hole
    std::uint32_t length = 0;
    std::uint32_t sparse = 0;   ///< index properties kept in props (past the dense part)
    bool lengthWritable = true;
    bool frozenElems = false;   ///< Object.freeze: elements are read-only
    bool sealedElems = false;   ///< Object.seal / preventExtensions: no element is added or removed
    explicit ArrayObj(Cls c = Cls::Array) : Object(c) {}
    void Trace(Runtime& rt) override;
    std::size_t Bytes() const override { return Object::Bytes() + elems.capacity() * sizeof(Value); }
};

/// Primitive wrappers (Boolean, Number, String, Symbol objects) and other one-value objects.
struct BoxObj : Object {
    Value prim;
    explicit BoxObj(Cls c = Cls::BoolBox) : Object(c) {}
    void Trace(Runtime& rt) override;
};

struct CallArgs;
/// A library function. Arguments are a[i]; `this` is a.thisv; a.newTarget is set for `new`.
using NativeFn = Value (*)(Runtime& rt, CallArgs& a);

struct NativeFunc : Object {
    NativeFn fn = nullptr;
    String* name = nullptr;
    int length = 0;
    bool ctor = false;            ///< may be called with new
    std::vector<Value> slots;     ///< values the function carries (resolve functions, bound engine method ids)
    NativeFunc() : Object(Cls::Native) {}
    void Trace(Runtime& rt) override;
};

struct Closure : Object {
    Proto* code = nullptr;
    std::vector<Cell*> ups;
    Object* home = nullptr;       ///< [[HomeObject]] of a method (super lookups)
    Closure* fields = nullptr;    ///< a class constructor's instance field initializer
    bool classCtor = false;
    bool derived = false;
    Closure() : Object(Cls::Closure) {}
    void Trace(Runtime& rt) override;
};

struct BoundFunc : Object {
    Object* target = nullptr;
    Value thisv;
    std::vector<Value> args;
    BoundFunc() : Object(Cls::Bound) {}
    void Trace(Runtime& rt) override;
};

/// Engine objects (the game binding): what they are is up to their host class.
struct HostObj : Object {
    std::uint32_t hostClass = 0;
    std::uint64_t id = 0;
    std::int32_t i32 = 0;
    std::uint32_t u32 = 0;
    double v[3] = {0, 0, 0};
    HostObj() : Object(Cls::Host) {}
};

/// How engine objects behave: get / set return true when they handled the key.
struct HostClass {
    std::string name;
    bool (*get)(Runtime& rt, HostObj* self, String* key, Value& out) = nullptr;
    bool (*set)(Runtime& rt, HostObj* self, String* key, const Value& v) = nullptr;
    std::string (*toString)(Runtime& rt, HostObj* self) = nullptr;
    std::vector<std::string> (*keys)(Runtime& rt, HostObj* self) = nullptr;   ///< for JSON / Object.keys
};

struct CallArgs {
    Value* argv = nullptr;
    int argc = 0;
    Value thisv;
    Object* newTarget = nullptr;
    NativeFunc* callee = nullptr;
    Value operator[](int i) const { return i < argc ? argv[i] : Value(); }
};

/// A JavaScript exception is being thrown; the value is Runtime::exception.
struct JsThrow {};
/// The run stopped (budget, memory, too deep): try/catch can not catch this.
struct JsTerminate {
    std::string why;
};

struct SyntaxError {
    int line = 0, column = 0;
    std::string message;   ///< "message" (no position)
};

enum class ErrorType : std::uint8_t { Error, TypeError, RangeError, SyntaxError, ReferenceError, EvalError, URIError, AggregateError };

/// A JavaScript engine instance: its own heap, globals and budget.
class Runtime {
public:
    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // ---- running code
    /// Compile a script. On success `fn` is the script's top-level function. Never throws: a memory stop while
    /// compiling comes back as false with err->line = -1 and the stop's message.
    bool Compile(const std::string& source, const std::string& chunk, Value& fn, SyntaxError* err);
    /// Every syntax error the parser reports (it stops at the first one).
    std::vector<SyntaxError> CheckSyntax(const std::string& source, const std::string& chunk);
    /// Compile and run a script, then the pending promise jobs. On failure `err` is
    /// "chunk:line: TypeError: message" (or the stop reason). `exported` gets `export default`'s value.
    bool Run(const std::string& source, const std::string& chunk, std::string* err, Value* exported = nullptr);
    /// Call from the host: false on a thrown error or a stop, with the message in `err`.
    bool CallHost(const Value& fn, const Value& thisv, const std::vector<Value>& args, Value* result, std::string* err);
    /// new fn(...args) from the host, with the same error handling as CallHost.
    bool ConstructHost(const Value& fn, const std::vector<Value>& args, Value* result, std::string* err);
    void* userData = nullptr;   ///< for the host (the game-script glue keeps its state here)
    /// Classic page scripts (the browser): code without "use strict" (and outside classes) is sloppy about one thing,
    /// assigning an undeclared name creates a global property (6.2.5.6 PutValue step 5.b). Off by default: game
    /// scripts stay fully strict.
    bool sloppyScripts = false;
    /// Call from library code: throws JsThrow / JsTerminate. The result is kept for the current native call.
    Value Call(const Value& fn, const Value& thisv, const Value* args, int argc);
    Value Call(const Value& fn, const Value& thisv, std::initializer_list<Value> args) {
        return Call(fn, thisv, args.begin(), (int)args.size());
    }
    Value Construct(const Value& fn, const Value* args, int argc, Object* newTarget = nullptr);
    /// Run promise jobs (the spec's job queue, 9.5) until none are left.
    void RunJobs();
    /// The text for the pending exception ("TypeError: message" plus "chunk:line: " where known).
    std::string ExceptionText(const Value& exc);

    // ---- budget and limits
    void SetBudget(std::uint64_t instructions) { budget_ = instructions; left_ = instructions ? instructions : ~0ull; }
    std::uint64_t BudgetUsed() const { return budget_ ? budget_ - left_ : 0; }
    void Charge(std::uint64_t n);   ///< library work counts against the budget
    std::size_t memoryLimit = 256u << 20;
    bool gcStress = false;          ///< collect at every safe point (tests: finds values that are not kept)
    std::size_t BytesInUse() const { return bytes_; }
    std::uint64_t Collections() const { return collections_; }   ///< how many times the collector has run
    std::function<void(const std::string&)> printSink;   ///< console.log; nullptr = nowhere
    std::function<void(HostObj*)> onHostFree;            ///< an engine object is about to be freed
    /// import(specifier): return a promise for the module namespace. Unset (game scripts): the promise is
    /// rejected with a TypeError, so `try { await import(x) } catch {}` code still runs.
    std::function<Value(Runtime&, const std::string& specifier)> importHook;

    // ---- values
    String* NewString(std::u16string s);
    String* NewStringUtf8(std::string_view s);
    String* NewStringAscii(const char* s);
    String* Intern(const std::u16string& s);
    String* Intern(const char* ascii);
    String* InternStr(String* s);
    Symbol* NewSymbol(String* desc);
    Object* NewObject(Object* proto);
    Object* NewObject();   ///< with Object.prototype
    ArrayObj* NewArray(std::size_t reserve = 0);
    Object* NewError(ErrorType type, const std::string& msg);
    NativeFunc* NewNative(NativeFn fn, const char* name, int length, std::vector<Value> slots = {});
    HostObj* NewHost(std::uint32_t hostClass, Object* proto = nullptr);
    std::uint32_t AddHostClass(HostClass c) { hostClasses.push_back(std::move(c)); return (std::uint32_t)hostClasses.size() - 1; }
    std::vector<HostClass> hostClasses;

    // ---- the abstract operations of spec chapter 7
    bool ToBoolean(const Value& v) const;
    double ToNumber(const Value& v);
    double ToIntegerOrInfinity(const Value& v);
    std::int32_t ToInt32(const Value& v);
    std::uint32_t ToUint32(const Value& v);
    String* ToString(const Value& v);
    std::u16string ToU16(const Value& v) { return ToString(v)->s; }
    std::string ToUtf8(const Value& v);
    Value ToPrimitive(const Value& v, int hint);   ///< hint 0 default, 1 number, 2 string
    Value ToNumeric(const Value& v);   ///< 7.1.3: a Number or a BigInt
    Object* ToObject(const Value& v);
    GcObj* ToPropertyKey(const Value& v);          ///< an atom or a symbol
    std::uint64_t LengthOf(const Value& obj);      ///< LengthOfArrayLike
    bool IsCallable(const Value& v) const { return v.IsObj() && v.O()->IsCallable(); }
    bool IsConstructor(const Value& v) const;
    /// 7.2.2 IsArray: sees through proxies (a revoked one is a TypeError).
    bool IsArray(const Value& v);
    bool LooseEquals(const Value& a, const Value& b);
    const char* TypeOf(const Value& v) const;
    bool InstanceOf(const Value& v, const Value& target);
    static std::string NumberToStdString(double d);
    std::u16string NumberToU16(double d);
    static double StringToNumber(const std::u16string& s);
    /// How console.log shows a value (strings as they are, objects one level deep).
    std::string Display(const Value& v, int depth = 0);

    // ---- properties (spec 7.3, 10.1)
    Value Get(const Value& obj, GcObj* key);
    Value Get(const Value& obj, GcObj* key, const Value& receiver);
    Value GetIndex(const Value& obj, std::uint32_t i);
    Value GetV(const Value& obj, const Value& key);   ///< obj[key]
    Value GetStr(const Value& obj, const char* key) { return Get(obj, Intern(key)); }
    /// [[Set]]; throws in strict mode when it fails (all our code is strict).
    void Put(const Value& obj, GcObj* key, const Value& v);
    void PutV(const Value& obj, const Value& key, const Value& v);
    void PutIndex(const Value& obj, std::uint32_t i, const Value& v);
    bool HasProperty(Object* o, GcObj* key);
    bool HasOwn(Object* o, GcObj* key);
    bool Delete(Object* o, GcObj* key);   ///< false if it is not configurable
    /// CreateDataProperty and DefinePropertyOrThrow for a plain value.
    void DefineData(Object* o, GcObj* key, const Value& v, std::uint8_t flags = kDefault);
    void DefineIndex(Object* o, std::uint32_t i, const Value& v);
    void DefineAccessor(Object* o, GcObj* key, Object* getter, Object* setter, std::uint8_t flags);
    /// [[GetOwnProperty]]: false when there is none.
    bool GetOwnProperty(Object* o, GcObj* key, Prop& out);
    /// [[DefineOwnProperty]] with a descriptor (has* say which fields were given).
    struct Descriptor {
        bool hasValue = false, hasWritable = false, hasGet = false, hasSet = false, hasEnumerable = false, hasConfigurable = false;
        Value value;
        Object* get = nullptr;
        Object* set = nullptr;
        bool writable = false, enumerable = false, configurable = false;
    };
    bool DefineOwnProperty(Object* o, GcObj* key, const Descriptor& d);
    /// [[OwnPropertyKeys]] in spec order: indices ascending, then strings, then symbols.
    std::vector<GcObj*> OwnKeys(Object* o, bool strings = true, bool symbols = false);
    bool SetPrototype(Object* o, Object* proto);
    bool PreventExtensions(Object* o);
    /// [[GetPrototypeOf]] and [[IsExtensible]] (proxies run their traps).
    Object* GetPrototypeOf(Object* o);
    bool IsExtensible(Object* o);
    /// [[Set]] with a receiver (10.1.9.2 OrdinarySetWithOwnDescriptor); false when it was refused (no throw for that).
    bool SetWithReceiver(Object* o, GcObj* key, const Value& v, const Value& receiver);
    void MakeLazyProps(Object* o);   ///< a function's name, length and prototype
    GcObj* IndexKey(std::uint32_t i);   ///< the atom for an index

    // ---- errors
    [[noreturn]] void Throw(const Value& v);
    [[noreturn]] void ThrowError(ErrorType t, const std::string& msg);
    [[noreturn]] void ThrowType(const std::string& msg) { ThrowError(ErrorType::TypeError, msg); }
    [[noreturn]] void ThrowRange(const std::string& msg) { ThrowError(ErrorType::RangeError, msg); }
    [[noreturn]] void Terminate(const std::string& why);
    Value exception;   ///< the value being thrown (while a JsThrow is in flight)
    std::string throwWhere;   ///< "chunk:line" of the last throw statement (for values that are not errors)
    /// "chunk:line" of the code running now, or "".
    std::string Where();
    /// The call stack, innermost first, as "  at name (chunk:line)" lines.
    std::string StackText();

    // ---- globals and host references
    Object* global = nullptr;
    Object* lexicals = nullptr;   ///< top-level let / const / class (the global declarative record)
    void SetGlobal(const char* name, const Value& v);
    Value GetGlobal(const char* name);   ///< undefined when missing
    int Ref(const Value& v);              ///< keep a value alive for the host; returns a handle
    Value GetRef(int ref) const { return refs_[(std::size_t)ref]; }
    void Unref(int ref);
    /// Keep a value alive until the current library call returns (or the next safe point of this call level).
    void Keep(const Value& v) { if (v.IsGc()) scratch_.push_back(v); }
    void Keep(GcObj* o) { if (o) scratch_.push_back(Value::Internal(o)); }

    // ---- the collector
    /// Mark and sweep. Roots: the value stack, the frames, the globals, the intrinsics, the host refs,
    /// the job queue, the scratch list (everything the current library calls made or were handed) and
    /// the registered vectors. Runs only at safe points of the bytecode machine (backward jumps,
    /// calls), never inside an allocation, so library code only has to keep what it holds across a
    /// call back into JavaScript (Keep / RootedVector).
    void Collect();
    void Mark(GcObj* o);
    void Adopt(GcObj* o);   ///< link an object made with new into the heap
    void MarkValue(const Value& v) { if (v.IsGc() && v.p && !v.p->marked) Mark(v.p); }
    /// A vector of values the collector sees while it exists (sort's copy of the elements).
    struct RootedVector {
        Runtime& rt;
        std::vector<Value> v;
        explicit RootedVector(Runtime& r) : rt(r) { rt.rootedVectors_.push_back(&v); }
        ~RootedVector() { rt.rootedVectors_.pop_back(); }
    };
    /// Drop scratch entries made inside a loop step (library loops that call back).
    struct ScratchScope {
        Runtime& rt;
        std::size_t mark;
        explicit ScratchScope(Runtime& r) : rt(r), mark(r.scratch_.size()) {}
        ~ScratchScope() { if (rt.scratch_.size() > mark) rt.scratch_.resize(mark); }
    };

    // ---- intrinsics (spec 6.1.7.4)
    Object *objectProto = nullptr, *functionProto = nullptr, *arrayProto = nullptr, *stringProto = nullptr,
           *numberProto = nullptr, *booleanProto = nullptr, *symbolProto = nullptr, *errorProto = nullptr,
           *iteratorProto = nullptr, *arrayIterProto = nullptr, *mapIterProto = nullptr, *setIterProto = nullptr,
           *stringIterProto = nullptr, *generatorProto = nullptr, *generatorFnProto = nullptr, *asyncFnProto = nullptr,
           *mapProto = nullptr, *setProto = nullptr, *promiseProto = nullptr, *promiseCtor = nullptr,
           *arrayCtor = nullptr, *objectCtor = nullptr, *throwTypeErrorFn = nullptr, *regexpProto = nullptr,
           *dateProto = nullptr, *bigintProto = nullptr;
    Object* errorProtos[8] = {};
    Symbol *symIterator = nullptr, *symAsyncIterator = nullptr, *symToPrimitive = nullptr, *symToStringTag = nullptr,
           *symHasInstance = nullptr, *symSpecies = nullptr;
    struct Atoms;
    std::unique_ptr<Atoms> atoms;   ///< common keys ("length", "prototype", ...)
    std::unordered_map<std::u16string, Symbol*> symbolRegistry;
    /// WeakMap / WeakSet / WeakRef / FinalizationRegistry objects: the collector clears their dead keys.
    std::vector<Object*> weakObjs;

    struct Impl;   ///< the bytecode machine's state (JsInterpreter.cpp)
    std::unique_ptr<Impl> vm;

private:
    void Sweep();
    void InitIntrinsics();
    GcObj* gcList_ = nullptr;
    std::size_t bytes_ = 0, gcThreshold_ = 4u << 20;
    std::uint64_t collections_ = 0;
    std::uint64_t budget_ = 0, left_ = ~0ull;
    std::unordered_map<std::u16string_view, String*> atomTable_;
    std::vector<Value> refs_;
    std::vector<int> freeRefs_;
    std::vector<Value> scratch_;
    std::vector<std::vector<Value>*> rootedVectors_;
    std::vector<String*> indexAtoms_;   ///< "0".."1023" cached
    std::vector<GcObj*> gray_;
public:
    /// Allocate a heap object (kept in the scratch list until the current call level's next safe point).
    template <class T> T* Alloc(std::size_t extra = 0) {
        T* o = new T();
        o->gcNext = gcList_;
        gcList_ = o;
        bytes_ += sizeof(T) + extra;
        scratch_.push_back(Value::Internal(o));
        if (bytes_ > memoryLimit) MemoryFull();
        return o;
    }
    bool arrayProtoHasIndex_ = false;   ///< an index accessor on Array.prototype / Object.prototype (slow path for holes)
private:
public:
    // used by the bytecode machine and the libraries
    std::vector<Value>& Scratch() { return scratch_; }
    bool GcDue() const { return gcStress || bytes_ > gcThreshold_; }
    void CountBytes(std::ptrdiff_t n) { bytes_ = (std::size_t)((std::ptrdiff_t)bytes_ + n); if (bytes_ > memoryLimit) MemoryFull(); }
    /// Over the memory limit: stops the running script (JsTerminate). When no script is running (the host making
    /// values between calls) it returns, and the next call stops at its first allocation instead.
    void MemoryFull();
    std::uint64_t& BudgetLeft() { return left_; }
    [[noreturn]] void BudgetOut();
};

/// UTF-8 <-> UTF-16 (lone surrogates become U+FFFD in UTF-8).
std::u16string Utf8To16(std::string_view s);
std::string Utf16To8(const std::u16string& s);
std::u16string AsciiTo16(const char* s);

/// Reserved words (for the editor's colours).
extern const char* const kKeywords[];
extern const int kKeywordCount;

} // namespace okay::js
