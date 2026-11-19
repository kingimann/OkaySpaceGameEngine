// Our own Lua 5.4 implementation: core types. Written from the public Lua 5.4
// reference manual (language and standard library chapters), in our own structure.
// Not derived from the Lua project or LuaJIT code.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay::lua {

enum class T : std::uint8_t { Nil, Bool, Int, Num, Str, Table, Func, Native, Coro, User, Cell };

struct Obj {
    T type;
    bool mark = false;
    Obj* next = nullptr;
    explicit Obj(T t) : type(t) {}
    virtual ~Obj() = default;
};

struct Value {
    T t = T::Nil;
    union { bool b; std::int64_t i; double n; Obj* o; };
    Value() : i(0) {}
    static Value Bool(bool v) { Value x; x.t = T::Bool; x.b = v; return x; }
    static Value Int(std::int64_t v) { Value x; x.t = T::Int; x.i = v; return x; }
    static Value Num(double v) { Value x; x.t = T::Num; x.n = v; return x; }
    static Value Of(Obj* p) { Value x; x.t = p->type; x.o = p; return x; }
    bool IsNil() const { return t == T::Nil; }
    bool Truthy() const { return !(t == T::Nil || (t == T::Bool && !b)); }
    bool IsNumber() const { return t == T::Int || t == T::Num; }
    bool IsObj() const { return t >= T::Str; }
};

struct State;
struct Coro;

struct Str : Obj {
    std::string s;
    explicit Str(std::string v) : Obj(T::Str), s(std::move(v)) {}
};

struct ValueHash { std::size_t operator()(const Value& v) const; };
struct ValueEq { bool operator()(const Value& a, const Value& b) const; };

struct Table : Obj {
    std::vector<Value> arr;                       // keys 1..arr.size()
    std::vector<std::pair<Value, Value>> ents;    // other keys, insertion order (nil value = removed)
    std::unordered_map<Value, std::size_t, ValueHash, ValueEq> idx;
    std::size_t dead = 0;
    Table* meta = nullptr;
    Table() : Obj(T::Table) {}
    Value Get(const Value& k) const;
    Value GetStr(const std::string& k) const;
    void Set(State& L, const Value& k, const Value& v);   // k must not be nil / NaN (caller checks)
    std::int64_t Length() const;
    bool Next(const Value& k, Value& nk, Value& nv, bool& badKey) const;
    void Compact();
};

struct Proto {
    std::vector<struct Instr> code;
    std::vector<int> lines;
    std::vector<std::string> hints;
    std::vector<Value> k;
    std::vector<Proto*> protos;
    struct UpDesc { bool fromLocal; int index; };
    std::vector<UpDesc> ups;
    int nparams = 0, nlocals = 0;
    bool vararg = false;
    std::string name, chunk;
    int line = 0;
};

struct Cell : Obj { Value v; Cell() : Obj(T::Cell) {} };

struct Closure : Obj {
    Proto* p;
    std::vector<Cell*> ups;
    explicit Closure(Proto* pr) : Obj(T::Func), p(pr) {}
};

using NativeFn = std::function<void(State&, std::vector<Value>& args, std::vector<Value>& rets)>;
enum class Special : std::uint8_t { None, PCall, XPCall, Yield };

struct Native : Obj {
    NativeFn fn;
    std::string name;
    Special special = Special::None;
    std::vector<Value> up;    // values this native keeps alive
    Native() : Obj(T::Native) {}
};

struct User : Obj {
    int kind = 0;
    std::uint64_t id = 0;
    std::string tag;
    Table* meta = nullptr;
    User() : Obj(T::User) {}
};

struct Frame {
    Closure* cl = nullptr;
    int pc = 0;
    int base = 0;           // index of first local
    int wantRet = -1;       // results the caller wants (-1 = all)
    int fidx = 0;           // where results go (the function slot)
    std::uint8_t prot = 0;  // 1 = pcall frame, 2 = xpcall frame
    Value handler;
    std::vector<Value> varargs;
};

enum class CoStatus : std::uint8_t { Fresh, Running, Suspended, Normal, Dead };

struct Coro : Obj {
    std::vector<Value> st;
    std::vector<Frame> frames;
    CoStatus status = CoStatus::Fresh;
    Value startFn;
    int multi = 0;
    bool yielded = false;
    std::vector<Value> transfer;
    int yieldIdx = 0, yieldWant = 0;
    int nativeNest = 0;
    Coro* resumer = nullptr;
    bool isMain = false;
    Coro() : Obj(T::Coro) {}
};

/// An error raised by a script (or by the engine on its behalf).
struct LuaError {
    Value v;
    std::string text;       // tostring(v) at throw time, for the host
    bool fatal = false;     // budget / memory errors: pcall cannot catch these
};

struct Limits {
    std::int64_t instructions = 5'000'000;   // per entry call; 0 = unlimited
    double seconds = 0.5;                    // wall clock per entry call; 0 = unlimited
    std::size_t memoryBytes = 64u << 20;     // 0 = unlimited
    int maxFrames = 20000;
};

struct State {
    Limits limits;
    Obj* all = nullptr;
    std::size_t allocated = 0, liveAfterGC = 0, threshold = 1u << 20;
    bool gcPending = false;
    Table* globals = nullptr;
    Table* stringMeta = nullptr;
    Coro* mainCo = nullptr;
    Coro* cur = nullptr;
    std::vector<std::unique_ptr<Proto>> protos;
    std::vector<std::vector<Value>*> pins;
    std::vector<Obj*> extraRoots;
    std::function<void(const std::string&)> out;      // print / io.write sink
    std::int64_t budget = 0;
    double deadline = 0;
    int cDepth = 0;
    bool nextCallDirect = false; // set by pcall before it calls a native: that call has no script caller
    bool directNative = false;   // a native called straight by pcall: errors carry no script position
    Special lastSpecial = Special::None;

    State();
    ~State();
    State(const State&) = delete;
    State& operator=(const State&) = delete;

    // ---- object creation ----
    Value NewStr(const std::string& s);
    Table* NewTable();
    Native* NewNative(const std::string& name, NativeFn fn);
    User* NewUser(int kind, std::uint64_t id, Table* meta);
    Value Fn(const std::string& name, NativeFn fn) { return Value::Of(NewNative(name, std::move(fn))); }
    void Reserve(std::size_t bytes);   // fatal error if this would pass the memory cap
    void Collect();
    void OpenLibs();

    // ---- running ----
    Proto* Compile(const std::string& src, const std::string& chunk, std::string* err);
    Value MakeFunction(Proto* p);
    Value IndexH(const Value& obj, const Value& key, const std::string& hint);
    void SetIndexH(const Value& obj, const Value& key, const Value& v, const std::string& hint);
    void RunInner(Coro* co, std::size_t stopFrames);
    bool PreCallCore(Coro* co, int fidx, int nargs, int want, std::uint8_t prot, const Value& handler);
    Value Arith(int op, const Value& a, const Value& b, const std::string& hint);
    bool Less(const Value& a, const Value& b, bool orEqual);
    Value Concat(std::vector<Value>& parts, const std::string& hint);
    Value LenOf(const Value& v, const std::string& hint);
    std::vector<Value> Call(const Value& f, std::vector<Value> args, int nret = -1);
    /// Calls with budgets reset, catching errors. Returns false and fills err.
    bool CallProtected(const Value& f, std::vector<Value> args, std::vector<Value>* rets, std::string* err);
    bool DoString(const std::string& src, const std::string& chunk, std::string* err);
    void ResetBudget();
    void CheckBudget();

    // ---- helpers used by libraries ----
    [[noreturn]] void Error(const std::string& msg);            // adds position
    [[noreturn]] void ErrorValue(const Value& v);
    [[noreturn]] void ErrorFatal(const std::string& msg);
    std::string Where(int level);
    std::string ToStringValue(const Value& v);                   // honours __tostring
    std::string RawToString(const Value& v) const;
    Value GetMeta(const Value& v, const char* event);
    Value Index(const Value& obj, const Value& key);
    void SetIndex(const Value& obj, const Value& key, const Value& v);
    bool Equals(const Value& a, const Value& b);
    bool RawEquals(const Value& a, const Value& b) const;
    const char* TypeName(const Value& v) const;
    Table* MetaOf(const Value& v) const;
    bool Resume(Coro* co, std::vector<Value> args, std::vector<Value>* results, std::string* err);
    Coro* NewCoro(const Value& fn);

    // Run loop (LuaVM.cpp)
    void Run(Coro* co, std::size_t stopFrames);
    bool PreCall(Coro* co, int fidx, int nargs, int want, std::uint8_t prot, const Value& handler);
    void Finish(Coro* co, int fidx, int want, std::vector<Value>& rets, bool prefixTrue);
    std::string Hint(const Frame& f, int pc) const;
};

bool ToNumber(const Value& v, Value& out);          // string coercion allowed
bool ParseNumber(const std::string& s, Value& out); // whole-string Lua numeral
bool ToInteger(const Value& v, std::int64_t& out);  // exact integer representation
std::string FormatNumber(const Value& v);

// Ops
enum Op : std::uint8_t {
    OP_NIL, OP_TRUE, OP_FALSE, OP_K, OP_POP, OP_DUP,
    OP_GETLOCAL, OP_SETLOCAL, OP_GETCELL, OP_SETCELL, OP_NEWCELL, OP_CELLIFY,
    OP_GETUP, OP_SETUP, OP_GETGLOBAL, OP_SETGLOBAL,
    OP_GETTABLE, OP_SETTABLE, OP_INITKV, OP_GETFIELD, OP_SETFIELD, OP_SELF,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_IDIV, OP_POW, OP_UNM, OP_NOT, OP_LEN,
    OP_BAND, OP_BOR, OP_BXOR, OP_SHL, OP_SHR, OP_BNOT,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE, OP_CONCAT,
    OP_JMP, OP_JMPF, OP_JMPFK, OP_JMPTK, OP_GFORTEST,
    OP_CALL, OP_RETURN, OP_CLOSURE, OP_VARARG, OP_NEWTABLE, OP_SETLIST,
    OP_FORPREP, OP_FORLOOP
};
struct Instr { std::uint8_t op; std::int32_t a = 0, b = 0, c = 0; };

// Library openers (LuaLib.cpp)
void OpenBase(State& L);
void OpenString(State& L);
void OpenTable(State& L);
void OpenMath(State& L);
void OpenOs(State& L);
void OpenCoroutine(State& L);
void OpenUtf8(State& L);

} // namespace okay::lua
