#pragma once
// ---------------------------------------------------------------------------
// okay::lua - our own interpreter for the Lua 5.4 language.
//
// Written from the Lua 5.4 Reference Manual (the language and library
// specification) and general compiler / interpreter techniques (a one-pass
// compiler with delayed expression code, a stack-based bytecode, a mark and
// sweep collector). No code from the reference implementation or any other
// Lua implementation was used or followed.
//
// Why bytecode and not a tree-walker: coroutines must work without operating
// system threads (CLAUDE.md rule 21a). With an explicit frame stack a yield is
// "save the frames and return", and resuming continues the same loop. It also
// gives a natural place to count instructions for the run budget.
//
// What this header offers: the value type, the heap objects and the State that
// runs code. The engine glue (IScriptVM for `.lua` scripts) is LuaScriptVM.
// See docs/scripting/lua.md for the supported language and library subset.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include "okay/Core/Random.hpp"   // math.random: the engine's one seedable generator
#include <functional>
#include <string>
#include <vector>

namespace okay::lua {

/// The reserved words of Lua 5.4 (manual 3.1), in the compiler's token order. Shared by the compiler, the engine
/// glue (completion, lint) and the Script Editor's colours.
inline constexpr const char* kKeywords[] = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function",
                                            "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then",
                                            "true", "until", "while"};

enum class Type : std::uint8_t {
    Nil, Bool, Int, Float, String, Table, LFunc, NFunc, Thread, Userdata,
    Box,     ///< internal: a captured local variable (an upvalue cell)
    Proto,   ///< internal: compiled function code
};

class State;

/// Every value the collector manages starts with this header.
struct GcObj {
    GcObj* gcNext = nullptr;
    std::size_t gcSize = 0;   ///< bytes counted for this object
    Type type;
    bool marked = false;
    bool fin = false;   ///< marked for finalization (setmetatable with a __gc field), not run yet
    explicit GcObj(Type t) : type(t) {}
    virtual ~GcObj() = default;
};

struct LString;
struct Table;
struct LClosure;
struct NClosure;
struct Coroutine;
struct UData;
struct Box;
struct Proto;

/// A Lua value: nil, boolean, integer, float or a reference to a heap object.
struct Value {
    Type t = Type::Nil;
    union {
        bool b;
        std::int64_t i;
        double n;
        GcObj* o;
    };
    Value() : i(0) {}
    static Value Bool(bool v) { Value x; x.t = Type::Bool; x.b = v; return x; }
    static Value Int(std::int64_t v) { Value x; x.t = Type::Int; x.i = v; return x; }
    static Value Num(double v) { Value x; x.t = Type::Float; x.n = v; return x; }
    static Value Obj(GcObj* g) { Value x; x.t = g->type; x.o = g; return x; }
    bool IsNil() const { return t == Type::Nil; }
    bool IsNumber() const { return t == Type::Int || t == Type::Float; }
    bool IsString() const { return t == Type::String; }
    bool IsTable() const { return t == Type::Table; }
    bool IsFunction() const { return t == Type::LFunc || t == Type::NFunc; }
    bool IsGc() const { return t >= Type::String; }
    bool Truthy() const { return !(t == Type::Nil || (t == Type::Bool && !b)); }
    double AsNum() const { return t == Type::Int ? (double)i : n; }
    LString* Str() const { return reinterpret_cast<LString*>(o); }
    Table* Tab() const { return reinterpret_cast<Table*>(o); }
    LClosure* LFn() const { return reinterpret_cast<LClosure*>(o); }
    NClosure* NFn() const { return reinterpret_cast<NClosure*>(o); }
    Coroutine* Co() const { return reinterpret_cast<Coroutine*>(o); }
    UData* UD() const { return reinterpret_cast<UData*>(o); }
    Box* Bx() const { return reinterpret_cast<Box*>(o); }
};

/// Raw equality (no __eq): what rawequal and table keys use.
bool RawEquals(const Value& a, const Value& b);

struct LString : GcObj {
    std::string s;
    std::uint32_t hash = 0;
    LString() : GcObj(Type::String) {}
};

/// A table: an array part for keys 1..n and an open-addressing hash part for
/// everything else. Removing a key only clears its value; the key stays until
/// the next rehash, so next() keeps working while fields are cleared.
struct Table : GcObj {
    struct Node { Value k, v; };
    std::vector<Value> arr;        ///< arr[i] holds key i + 1
    std::vector<Node> node;        ///< size 0 or a power of two
    std::size_t used = 0;          ///< nodes with a key (live or cleared)
    Table* meta = nullptr;
    Table() : GcObj(Type::Table) {}

    Value Get(const Value& k) const;
    Value GetInt(std::int64_t k) const;
    Value GetStr(const LString* k) const;
    /// Raw set. Errors (nil or NaN key) are raised through `L`.
    void Set(State& L, const Value& k, const Value& v);
    void SetInt(State& L, std::int64_t k, const Value& v);
    /// The key after `k` (nil = first). Returns false at the end. Raises an error for a key not in the table.
    bool Next(State& L, Value& k, Value& v) const;
    /// A border (the # operator without __len).
    std::int64_t Length() const;
    std::size_t MemoryBytes() const;
private:
    const Node* FindNode(const Value& k) const;
    void Rehash(State& L, std::size_t extra);
    void MigrateFromHash();
};

/// The native function signature: arguments are L.Arg(i) for i in [0, n);
/// results are pushed with L.Push and their count returned.
using NativeFn = int (*)(State& L, int nargs);

struct NClosure : GcObj {
    NativeFn fn = nullptr;
    const char* name = "?";
    std::vector<Value> up;   ///< values the function carries with it
    NClosure() : GcObj(Type::NFunc) {}
};

struct Box : GcObj {
    Value v;
    Box() : GcObj(Type::Box) {}
};

struct LClosure : GcObj {
    Proto* p = nullptr;
    std::vector<Box*> up;
    LClosure() : GcObj(Type::LFunc) {}
};

/// Full userdata. The engine binding keeps an object id or a vector in it.
struct UData : GcObj {
    Table* meta = nullptr;
    int tag = 0;
    std::uint64_t id = 0;
    int i32 = 0;
    double v[4] = {0, 0, 0, 0};
    UData() : GcObj(Type::Userdata) {}
};

/// One active function call.
struct Frame {
    LClosure* cl = nullptr;   ///< null for a native
    NClosure* ncl = nullptr;
    int func = 0;     ///< stack index of the called function (results go here)
    int base = 0;     ///< first local slot
    int pc = 0;
    int nres = -1;    ///< results the caller wants (-1 = all)
    int vaStart = 0, vaCount = 0;
    int tbcMark = 0;  ///< coroutine tbc list size when the frame started
    bool entry = false;   ///< returning from it leaves the Execute call that started it
    bool tail = false;    ///< reached through a tail call
    bool pcallMark = false;   ///< a pcall running inside the loop: the protected function's results come back here
    bool xpcallMark = false;  ///< with pcallMark: an xpcall; the message handler is in stack[func]
    std::uint8_t post = 0;    ///< a metamethod run inside the loop: 1 = its result becomes a boolean, 2 = the negated boolean,
                              ///< 3 = a method lookup (`o:m()`): the result and the object below it swap places
    int concatLeft = 0;       ///< a CONCAT of 3+ values paused for a Lua __concat: the values still to join when it runs again
};

enum class CoStatus : std::uint8_t { Suspended, Running, Normal, Dead };

struct Coroutine : GcObj {
    std::vector<Value> stack;
    int top = 0;
    std::vector<Frame> frames;
    std::vector<int> tbc;     ///< stack slots of to-be-closed variables
    CoStatus status = CoStatus::Suspended;
    bool started = false;
    int nest = 0;             ///< native calls into Lua inside this coroutine (no yield across them)
    int yieldFunc = -1;       ///< the pending call of coroutine.yield: where its results go
    int yieldNres = -1;
    int nYield = 0;           ///< values yielded, on top of the stack
    Value fn;
    Value err;                ///< the error that killed it (for coroutine.close)
    bool errored = false;
    Coroutine() : GcObj(Type::Thread) {}
};

/// A Lua error being raised: the error value is kept in State::errorValue (so it
/// stays reachable for the collector while C++ unwinds).
struct LuaError {};
/// The run budget ran out (or memory did): pcall can not catch this one, so an
/// endless loop always stops.
struct LuaFatal { std::string message; };

/// A compiled chunk's syntax error.
struct SyntaxError {
    std::string message;   ///< "chunk:line: message near 'x'"
    int line = 0, column = 0;
    std::string bare;      ///< message without the position
};

class State {
public:
    State();
    ~State();
    State(const State&) = delete;
    State& operator=(const State&) = delete;

    // ---- libraries ----
    /// The sandboxed standard library: base, coroutine, string, table, math and
    /// a safe os (clock, time, date, difftime). No io, no loadlib, no debug.
    void OpenLibs();
    Table* Globals() const { return globals_; }
    void SetGlobal(const char* name, const Value& v);
    Value GetGlobal(const char* name);
    void Register(Table* t, const char* name, NativeFn fn);
    NClosure* NewNative(NativeFn fn, const char* name, std::vector<Value> up = {});

    // ---- compiling and running ----
    /// Compile `source` as a chunk named `chunkName` ("Player.lua"). On a syntax
    /// error returns false and fills `err`. On success pushes the function.
    bool Load(const std::string& source, const std::string& chunkName, SyntaxError* err, Table* env = nullptr);
    /// Check syntax only (nothing runs, nothing is pushed). Returns every error found
    /// (the parser stops at the first one per statement and resyncs).
    std::vector<SyntaxError> CheckSyntax(const std::string& source, const std::string& chunkName);
    /// Call the function at stack[top - nargs - 1] with nargs arguments; leaves
    /// nres results (-1 = all) in its place. Errors propagate as LuaError / LuaFatal.
    void Call(int nargs, int nres);
    /// Protected call: returns false and leaves the error message on the stack.
    /// LuaFatal is not caught here either (see PCallHost).
    bool PCall(int nargs, int nres);
    /// For the host: like PCall but also stops LuaFatal; `err` gets the message.
    bool PCallHost(int nargs, int nres, std::string* err);
    /// Load and run source; returns false with a message on error.
    bool DoString(const std::string& source, const std::string& chunkName, std::string* err);

    // ---- the stack (of the running coroutine) ----
    int Top() const { return co_->top - CurBase(); }
    void SetTop(int n);
    void Push(const Value& v);
    void PushNil() { Push(Value()); }
    void PushString(const std::string& s) { Push(Value::Obj(NewString(s))); }
    Value Pop();
    Value& At(int idx);    ///< 0-based from the current frame's base; negative counts from the top
    Value Arg(int i) const;   ///< argument i of the running native (nil when missing)
    Value& ArgRef(int i);

    // ---- objects ----
    LString* NewString(const std::string& s);
    LString* NewString(const char* s, std::size_t n);
    Table* NewTable(std::size_t narr = 0, std::size_t nhash = 0);
    UData* NewUData();
    Coroutine* NewCoroutine(const Value& fn);

    // ---- conversion and operators (with metamethods) ----
    std::string ToStringBasic(const Value& v);         ///< no __tostring
    std::string ToStringMeta(const Value& v);          ///< tostring(): uses __tostring / __name
    static bool StrToNumber(const std::string& s, Value& out);
    static std::string NumberToString(const Value& v);
    const char* TypeName(const Value& v) const;
    Table* GetMetatable(const Value& v) const;
    Value GetMetamethod(const Value& v, const char* event);
    Value Index(const Value& obj, const Value& key);
    void SetIndex(const Value& obj, const Value& key, const Value& v);
    bool Equals(const Value& a, const Value& b);
    bool LessThan(const Value& a, const Value& b);
    bool LessEqual(const Value& a, const Value& b);
    Value Arith(int op, const Value& a, const Value& b);   ///< op = an Op value (ADD..SHR)
    Value Length(const Value& v);
    Value Concat(const Value& a, const Value& b);
    /// Call a function value with arguments; returns its results (rooted while running).
    std::vector<Value> CallValue(const Value& fn, const std::vector<Value>& args, int nres = -1);

    // ---- errors ----
    [[noreturn]] void Error(const std::string& msg);              ///< adds "chunk:line:" of the running Lua code
    [[noreturn]] void ErrorValue(const Value& v);                 ///< raise any value
    [[noreturn]] void ArgError(int arg, const char* fname, const std::string& msg);
    [[noreturn]] void TypeError(int arg, const char* fname, const char* expected);
    std::string Where(int level);   ///< "chunk:line: " of the Lua function `level` frames up (1 = the caller of the native)
    Value errorValue;               ///< the value being raised

    // ---- argument helpers for natives ----
    std::int64_t CheckInt(int arg, const char* fname);
    std::int64_t OptInt(int arg, const char* fname, std::int64_t def);
    double CheckNum(int arg, const char* fname);
    LString* CheckStr(int arg, const char* fname);   ///< numbers are converted
    Table* CheckTable(int arg, const char* fname);
    void CheckAny(int arg, const char* fname);
    static bool ToInteger(const Value& v, std::int64_t& out);   ///< integer or float with an exact integer value (strings too)
    static bool ToNumber(const Value& v, Value& out);           ///< number or numeric string

    /// Run the __gc finalizers of every object still marked for finalization, in reverse order of
    /// marking (manual 2.5.3: lua_close does this). Call it before destroying the State while the
    /// natives and printSink still work (the destructor only frees). An error in a finalizer is a
    /// warning, and the run budget still stops an endless one. After Close no finalizer runs again.
    void Close();

    // ---- budget and limits ----
    /// Instructions allowed before LuaFatal("...run budget..."). 0 = no limit.
    void SetBudget(std::uint64_t instructions) { budget_ = instructions; used_ = 0; }
    std::uint64_t BudgetUsed() const { return used_; }
    void Charge(std::uint64_t n);   ///< library work (pattern steps, big copies) counts against the budget
    std::size_t memoryLimit = 256u << 20;   ///< bytes
    std::size_t MemoryInUse() const { return bytes_; }
    void CollectGarbage();
    void* userData = nullptr;   ///< for the host (the engine binding keeps its context here)
    /// Where print() output goes (default: stdout).
    std::function<void(const std::string&)> printSink;

    // ---- host roots: values the host keeps across calls ----
    int Ref(const Value& v);      ///< keep v alive; returns a handle
    Value GetRef(int ref) const;
    void Unref(int ref);
    /// Values a native keeps in C++ locals while calling Lua code (e.g. table.sort).
    std::vector<std::vector<Value>*> tempRoots;

    // ---- coroutines ----
    Coroutine* Current() const { return co_; }
    Coroutine* Main() const { return main_; }
    /// Resume `co` with nargs values from the current stack top; results are pushed.
    /// Returns false (with the error value pushed) when the coroutine raised an error.
    bool Resume(Coroutine* co, int nargs, int* nresults);
    /// Called by coroutine.yield: marks a yield of the top nargs values.
    int YieldValues(int nargs);
    bool IsYieldable() const;
    void CloseCoroutine(Coroutine* co);

    // ---- internals shared by the library files ----
    okay::Random rng;               ///< math.random / math.randomseed (Core/Random.hpp)
    LString* tmName[32] = {};       ///< metamethod names, by TM index (LuaCode.hpp)
    Table* stringMeta = nullptr;    ///< metatable of strings (__index = string)
    Table* registry = nullptr;
    int cDepth = 0;                 ///< nested native -> Lua calls (C stack guard)
    static constexpr int kMaxCDepth = 180;
    int CurBase() const;
    void CheckStack(int extra);
    std::size_t bytes_ = 0;
    void AccountAlloc(std::size_t n);
    void AccountFree(std::size_t n) { bytes_ = n > bytes_ ? 0 : bytes_ - n; }
    bool gcPending = false;
    Value GetTM(const Value& v, int tm) const;   ///< metamethod by TM index, or nil
    Value CallMeta1(const Value& f, const Value& a);
    Value CallMeta2(const Value& f, const Value& a, const Value& b);
    bool Execute();   ///< run the current coroutine's top Lua frame until an entry frame returns (false) or it yields (true)
    bool ExecuteLoop();
    bool RecoverAtPcall(std::size_t entryIdx);
    void CallAt(int func, int nres);   ///< call stack[func] with args above it (native or Lua), running Lua to completion
    /// __index without calling a Lua function: true with `out` set when resolved (a native handler is
    /// called); false when the chain ends at a Lua function: call fn(self, key) (the loop does it in place).
    bool IndexStep(const Value& obj, const Value& key, Value& out, Value& fn, Value& self);
    /// __newindex the same way: true when done; false: call fn(self, key, v).
    bool NewIndexStep(const Value& obj, const Value& key, const Value& v, Value& fn, Value& self);
    /// The Lua function metamethod an arithmetic or bitwise operation would call, or nil (numbers, or a native one).
    Value ArithLuaTM(int op, const Value& a, const Value& b);
    /// Run Lua function `f` with `nargs` arguments as a frame of the loop; its results (nres) land at `dst`.
    void PushMetaFrame(int dst, const Value& f, const Value* args, int nargs, int nres, std::uint8_t post);
    /// xpcall(f, msgh, ...) of a Lua function as a marker frame of the loop (stack[func] = xpcall).
    void PushXPcallMarker(int func, int nargs, int nres);
    /// Close the to-be-closed variables at stack index >= level (err = the error, or nil).
    void CloseTbc(int level, const Value& err, bool withErr);
    /// CloseTbc for a normal block exit inside the loop: closes pending variables at or above `level`
    /// until one has a Lua __close, which is pushed as a frame of the loop (so it can yield) and true
    /// is returned; the caller then runs its instruction again for the rest.
    bool CloseTbcInLoop(int level);
    LClosure* NewLClosure(Proto* p);
    void Unwind(std::size_t frames, int top, Value& err);   ///< drop frames after an error, closing variables
    template <class T> T* Alloc(std::size_t extraBytes = 0) {
        T* o = new T();
        o->gcNext = gcList_;
        gcList_ = o;
        o->gcSize = sizeof(T) + extraBytes;
        AccountAlloc(o->gcSize);
        return o;
    }
    void Regrow(GcObj* o, std::size_t newSize) {   ///< an object's size changed (tables, strings)
        if (newSize > o->gcSize) AccountAlloc(newSize - o->gcSize); else AccountFree(o->gcSize - newSize);
        o->gcSize = newSize;
    }
    void MaybeCollect() { if (gcPending || gcStress) CollectGarbage(); }   ///< only at safe points
    bool gcStress = false;   ///< tests: collect at every safe point (finds values a native forgot to keep reachable)
    std::string VarInfo(const Frame& f, int pc, int which = 0);
    [[noreturn]] void OpError(const char* what, const Value& v, int which);   ///< "attempt to <what> a X value (local 'y')"
    Coroutine* co_ = nullptr;
    Coroutine* main_ = nullptr;
    std::vector<Coroutine*> coStack_;   ///< coroutines waiting in Resume (roots)
    std::vector<Table*> weakTables;     ///< found while marking (collector only)
    /// Mark an object for finalization (setmetatable did it when the metatable has __gc).
    void MarkForFinalization(GcObj* o);
    void RunFinalizers();
private:
    void Mark(GcObj* o, std::vector<GcObj*>& gray);
    void MarkValue(const Value& v, std::vector<GcObj*>& gray);
    void Sweep();
    void PushLuaFrame(int func, int nargs, int nres, bool entry);
    int CallNative(int func, int nargs, int nres);   ///< results placed; returns -1 when the native yielded
    void PostCall(const Frame& f, int firstResult, int nresults);
    int CallTM(int func, int nargs);                 ///< put __call in front; returns the new nargs
    bool ForPrep(int base, int a);
    bool ForLoop(int base, int a);
    void RecoverFatal(Coroutine* co, std::size_t nframes, int func, std::size_t coDepth);
    GcObj* gcList_ = nullptr;
    Table* globals_ = nullptr;
    std::uint64_t budget_ = 0, used_ = 0;
    std::size_t gcThreshold_ = 1u << 20;
    std::vector<int> freeRefs_;
    std::vector<GcObj*> finObjs_;      ///< marked for finalization, in marking order
    std::vector<GcObj*> toFinalize_;   ///< unreachable, finalizer not run yet (roots)
    bool runningFinalizers_ = false;
    bool closed_ = false;
};

} // namespace okay::lua
