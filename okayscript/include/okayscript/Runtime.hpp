#pragma once
// The OkayScript interpreter core: scopes, calls, natives and the walk over the syntax tree (Ast.hpp).
//
// It is a template over the value type V, so one core runs both the standalone okayscript::Value (Value.hpp) and the
// engine's vs::VsValue. V must have the script-value interface: constructors from float, bool, std::string and its
// vector type; MakeArray/MakeMap; IsNumber/IsBool/IsString/IsVec3/IsArray/IsMap/IsObject; AsFloat/AsBool/AsString/
// AsVec3/AsArray/AsMap/AsObjectId; NumberPtr (the number, or nullptr), SetNumber, SetBool, StringPtr. The definitions are in detail/Runtime.ipp, detail/Eval.ipp and detail/Vm.ipp; the standalone value
// is instantiated in src/Instantiate.cpp, another value type where its program includes those files once.
#include "okayscript/Ast.hpp"
#include "okayscript/Bytecode.hpp"
#include "okayscript/Environment.hpp"
#include "okayscript/Error.hpp"
#include "okayscript/Native.hpp"
#include <atomic>
#include <exception>
#include <functional>
#include <iosfwd>
#include <type_traits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace okayscript {

template <class V> class BasicRuntime;

/// A number no runtime has used before: BasicRuntime::varEpoch takes a new one whenever cached variable places may
/// have gone stale, so a cache filled by one runtime (or before a change) never matches.
inline unsigned long long NextVarEpoch() {
    static std::atomic<unsigned long long> next{0};
    return ++next;
}

/// A slot's state byte: 0 while its declaration has not run, else kDeclared with the variable's kind (VarKind).
constexpr unsigned char kDeclared = 0x80;

/// One scope. A function's or lambda's locals are slots in its frame (`slots`, named by `chunk`->slotNames); names
/// declared any other way (top-level code, a host) are in `vars`.
template <class V> struct BasicEnvironment {
    std::unordered_map<std::string, V> vars;
    std::unordered_map<std::string, unsigned char> kinds;   ///< typed / const variables only
    const Chunk* chunk = nullptr;           ///< the running body's code, when it has slots
    V* slots = nullptr;                     ///< its slot values
    unsigned char* slotState = nullptr;     ///< 0 = not declared yet, else kDeclared | kind
    /// Closures: a map of the variables a lambda made here keeps (the body's Chunk::boxNames, declared into it), whose
    /// "__up" entry is the box of the code the running lambda was made in, and so on outwards. Not a map: none.
    V env;
    const std::vector<std::string>* boxed = nullptr;   ///< the names this scope declares into `env`
    /// The slot named n, or -1.
    int SlotOf(const std::string& n) const {
        if (!chunk) return -1;
        const auto& names = chunk->slotNames;
        for (std::size_t i = 0; i < names.size(); ++i) if (names[i] == n) return (int)i;
        return -1;
    }
};

/// What the program running the interpreter adds to the language. Every hook is optional (return false = "not mine").
/// The engine answers its scene names, object references and components here.
template <class V> class BasicHost {
public:
    virtual ~BasicHost() = default;
    /// A dotted name the language does not know (`Time.deltaTime`): set `out` and return true to answer it.
    virtual bool ReadName(BasicRuntime<V>&, const std::string& /*name*/, V& /*out*/) { return false; }
    /// An assignment to such a name (`Time.timeScale = 0.5`): return true when handled.
    virtual bool WriteName(BasicRuntime<V>&, const std::string& /*name*/, const V& /*v*/) { return false; }
    /// A call to a function that is neither a native nor a script function: set `out` and return true to answer it.
    virtual bool CallUnknown(BasicRuntime<V>&, const std::string& /*name*/, std::vector<V>& /*args*/, V& /*out*/) { return false; }
    /// `value.name` on a value the language has no property for (an object reference, a component): true when answered.
    virtual bool GetMember(BasicRuntime<V>&, const V& /*value*/, const std::string& /*name*/, V& /*out*/) { return false; }
    /// Like GetMember, asked only after the language's own properties (s.Length, xs.Count) did not match.
    virtual bool GetMemberLast(BasicRuntime<V>&, const V& /*value*/, const std::string& /*name*/, V& /*out*/) { return false; }
    /// `value.name = v` on such a value: true when handled.
    virtual bool SetMember(BasicRuntime<V>&, const V& /*value*/, const std::string& /*name*/, const V& /*v*/) { return false; }
    /// `value.Method(args)` on such a value: set `out` and return true when answered.
    virtual bool CallMember(BasicRuntime<V>&, const V& /*value*/, const std::string& /*method*/, std::vector<V>& /*args*/,
                            V& /*out*/) { return false; }
    /// `yield return value;` inside a coroutine the host runs: hand control back and return true when the coroutine
    /// may go on. Without a host that runs coroutines, `yield` is a runtime error.
    virtual bool YieldValue(BasicRuntime<V>&, const V& /*value*/) { return false; }
};

/// Safety limits (the engine's values by default).
struct Limits {
    int maxCallDepth = 200;                    ///< deeper script recursion is a runtime error, not a crash
    long long maxLoopIterations = 1000000;     ///< per loop run; 0 = no limit
    bool runtimeErrorLines = true;             ///< give runtime errors the line they happened on (the engine: off)
};

/// `exit(code)` from a script: stops the program; Run lets it through so the host can end with that code.
struct ExitRequest { int code = 0; };

/// `throw value`: a RuntimeError whose message is the value as text, carrying the value itself for the catch.
template <class V> struct ThrownError : RuntimeError {
    V value;
    ThrownError(const V& v, int line) : RuntimeError(v.AsString(), line), value(v) {}
};

/// A try handler in a running chunk: where its catch starts, the stack depth to go back to, what it pushes (TryBegin b).
struct TryHandler { int pc, sp, mode; };

/// One running foreach: the collection, where it is, and a map's keys. kind 1: a generator; 2: an iterator object
/// (MoveNext / Current).
template <class V> struct ForEachState {
    V coll;
    std::size_t i = 0, n = 0;
    std::vector<std::string> keys;
    int kind = 0;
};

/// A paused generator body: its frame and everything RunChunk had in flight at the `yield` (detail/Vm.ipp).
template <class V> struct GenRun {
    bool started = false;
    int pc = 0, sp = 0;
    std::vector<V> stack;
    std::vector<TryHandler> handlers;
    std::vector<ForEachState<V>> iters;
    std::vector<long long> counters;
    std::vector<std::exception_ptr> pending;
    V value;   ///< what the last `yield` handed back
};
template <class V> struct Generator {
    ChunkPtr code;                     ///< keeps the compiled body alive
    std::vector<V> slots;
    std::vector<unsigned char> state;
    V env;                             ///< its closure box, if any
    const std::vector<std::string>* boxed = nullptr;
    V self;                            ///< a generator method's object
    std::string selfClass;
    const UserClass* methodClass = nullptr;
    GenRun<V> run;
    bool done = false, running = false;
};

/// The modules a program imported (import "file.okay"), shared by the program and every module it loads: each
/// module runs in its own runtime, so its globals and functions are its own namespace.
template <class V> struct ModuleSystem {
    /// Find `path` as written in the file `from` ("" = the main program): set the resolved path and the source.
    /// False with `error` set when it cannot be read. Unset: import is an error (the engine sets none).
    std::function<bool(const std::string& path, const std::string& from, std::string& resolved, std::string& source,
                       std::string& error)> load;
    struct Module {
        std::unique_ptr<BasicRuntime<V>> rt;
        std::unique_ptr<Program> prog;
        V exports;
        bool done = false;
        std::string path;
    };
    std::unordered_map<std::string, std::unique_ptr<Module>> byPath;
    std::vector<BasicRuntime<V>*> byId;    ///< 0 = the main program's runtime
    std::vector<std::string> loading;       ///< the import chain being loaded (resolved paths, for the cycle check)
    std::vector<std::string> loadingNames;  ///< the same as written in the imports (for messages)
};

/// The arguments of one native call.
template <class V> class BasicArgs {
public:
    BasicArgs(BasicRuntime<V>& rt, const std::string& name, std::vector<V>& values, int line)
        : m_rt(rt), m_name(name), m_values(values), m_line(line) {}

    std::size_t Size() const { return m_values.size(); }
    bool Has(std::size_t i) const { return i < m_values.size(); }
    /// Argument i, or 0 (null) when it was not given.
    V Get(std::size_t i) const { return i < m_values.size() ? m_values[i] : V{}; }
    const V& operator[](std::size_t i) const { return m_values.at(i); }
    std::vector<V>& All() { return m_values; }

    /// Typed reads: fail with "name(): argument N must be a <type>, got <type>" when argument i is something else.
    double Number(std::size_t i) const;
    std::string String(std::size_t i) const;
    bool Bool(std::size_t i) const;
    typename std::decay_t<decltype(std::declval<V>().AsArray())> List(std::size_t i) const;
    typename std::decay_t<decltype(std::declval<V>().AsMap())> Map(std::size_t i) const;

    /// Call a script function value (a lambda, or a function's name as text) with these arguments.
    V Call(const V& fn, std::vector<V> args) const;

    const std::string& Name() const { return m_name; }
    int Line() const { return m_line; }
    BasicRuntime<V>& Rt() const { return m_rt; }
    /// Throw a RuntimeError "name(): message" on the calling line.
    [[noreturn]] void Fail(const std::string& message) const;

private:
    [[noreturn]] void Need(std::size_t i, const char* type) const;
    BasicRuntime<V>& m_rt;
    const std::string& m_name;
    std::vector<V>& m_values;
    int m_line;
};

template <class V> using BasicNativeFn = std::function<V(BasicArgs<V>&)>;
template <class V> struct BasicNative {
    BasicNativeFn<V> fn;
    Arity arity;
};

template <class V> class BasicRuntime {
public:
    using Env = BasicEnvironment<V>;
    BasicRuntime() = default;
    BasicRuntime(const BasicRuntime&) = delete;
    BasicRuntime& operator=(const BasicRuntime&) = delete;
    /// The main program's runtime frees the modules it imported (they hold the module system too).
    ~BasicRuntime() {
        if (modules && moduleId == 0) {
            auto keep = modules;
            keep->byId.clear();
            keep->byPath.clear();
        }
    }
    std::vector<Env> scopes{1};                                ///< scopes[0] = global; one more per running call
    std::unordered_map<std::string, const FunctionDecl*> functions;
    std::unordered_map<std::string, BasicNative<V>> natives;
    BasicHost<V>* hooks = nullptr;   ///< the embedder's hooks (Interpreter::SetHost)
    Limits limits;
    std::ostream* out = nullptr;   ///< where print writes
    std::istream* in = nullptr;    ///< where input reads
    /// Script call depth, counted per thread (a host may run coroutines on their own threads and stacks).
    static int& CallDepth() { thread_local int depth = 0; return depth; }

    /// Raw builtins: called with the argument values only, no arity or type checks, found right after the natives
    /// (the engine registers its several hundred commands this way). Named arguments are refused unless the name is
    /// in namedArgBuiltins; while one runs, activeArgNames holds the names it was given.
    std::unordered_map<std::string, std::function<V(std::vector<V>&)>> builtins;
    /// A call by name finds the script's own functions before the builtins (the standalone interpreter, so a
    /// program's own `sum` or `map` is not hidden by the library's); natives, the embedder's, still come first. Off in
    /// the engine: its builtins come first, as they always did.
    bool scriptFunctionsFirst = false;
    std::unordered_set<std::string> namedArgBuiltins;
    const std::vector<std::string>* activeArgNames = nullptr;

    /// The line to put in a runtime error (0 when Limits::runtimeErrorLines is off).
    int Ln(int line) const { return limits.runtimeErrorLines ? line : 0; }

    struct LambdaBody {
        const std::vector<std::string>* params;
        const StmtList* body;
        const std::vector<ExprPtr>* defaults;
        ChunkPtr* code;   ///< the lambda's compiled body (LambdaExpr::code)
        std::vector<ChunkPtr>* defaultCode;   ///< its compiled defaults (LambdaExpr::defaultCode)
    };
    std::unordered_map<int, LambdaBody> lambdas;

    // ---- Plain classes (class Item { ... }, made with `new`) ----
    std::unordered_map<std::string, const UserClass*> classes;
    V self;                    ///< the class object a method or constructor is running on
    std::string selfClass;
    const UserClass* methodClass = nullptr;   ///< the class the running method is written in (for `super`)
    /// A class's own static fields (`static int count`), made the first time the class is used: ClassName.count.
    std::unordered_map<std::string, V> classStatics;
    const UserClass* FindClass(const std::string& name) const;
    const UserClass* BaseOf(const UserClass& c) const;
    /// The method `name` of class c or a class it extends; `owner` is the class that has it.
    const MethodDef* FindMethod(const UserClass* c, const std::string& name, const UserClass** owner = nullptr) const;
    const FieldDef* FindField(const UserClass* c, const std::string& name) const;
    /// The static field `name` of class c or a base: its value's place (made on first use), or nullptr.
    V* StaticField(const UserClass* c, const std::string& name);
    /// ClassName.Method(args): a static method. False when `cls` is no class or has no such static method.
    bool CallStatic(const std::string& cls, const std::string& method, std::vector<V>& args, V& out);
    /// `a op b` with a class object on either side that defines `operator op`: true with out set.
    bool ClassOperator(Tok op, const V& a, const V& b, V& out);
    /// A binary operator off the number fast path: class operators, then the language's rules (detail::Arith).
    V BinarySlow(Tok op, const V& a, const V& b);
    /// v as text: a class object with a ToString() method prints what it returns.
    std::string Text(const V& v);
    /// super(args) / super.Method(args) inside a method or constructor.
    V CallSuper(const std::string& name, std::vector<V>& args, int line);
    UserClass* ClassOf(const V& inst);
    bool SameInstance(const V& inst) const;
    bool HasMethod(const std::string& name);
    const FieldDef* SelfField(const std::string& n);
    void WriteField(const V& inst, const FieldDef& f, const V& v);
    V GetMember(const V& inst, const std::string& name, bool external);
    void SetMember(const V& inst, const std::string& name, const V& v, bool external);
    V CallMethod(const V& inst, const std::string& method, std::vector<V>& args, bool external);
    V MakeInstance(const std::string& type, std::vector<V>& args);

    // ---- `static` variables: one copy shared by every run of the same source (SharedStatics) ----
    std::shared_ptr<Env> statics;
    void DefineStatic(const std::string& n, const V& v, unsigned char kind);
    /// The statics shared by every interpreter that runs `source`.
    static std::shared_ptr<Env> SharedStatics(const std::string& source);

    // ---- Events: `OnHit += Handler` listeners, fired by FireSubs (the host, or a native) ----
    struct Sub { std::string name; V fn; };
    std::vector<Sub> subs;
    /// The host's event names that mean the same event ("OnClick" and "on_click"); the name itself by default.
    std::function<std::string(const std::string&)> canonEvent;
    void AddSub(const std::string& event, const V& fn);
    void RemoveSub(const std::string& event, const V& fn);
    bool HasSub(const std::string& event) const;
    void FireSubs(const std::string& event, std::vector<V>& args);

    Env& Global() { return scopes.front(); }
    void Define(const std::string& n, const V& v);
    void DefineTyped(const std::string& n, const V& v, unsigned char kind);
    void Assign(const std::string& n, const V& v);
    V Get(const std::string& n) const;
    bool Has(const std::string& n) const;
    /// The variable `n` in the scopes or the statics (one search), or nullptr. Not a class field. When it is found in
    /// a scope and `cache` is given, the cache is filled in (bytecode Load and Store).
    const V* Lookup(const std::string& n, NameCache* cache = nullptr) const;
    /// Assign, and fill `cache` in when the value landed in a scope with no type or const rule on it.
    void AssignCached(const std::string& n, const V& v, NameCache& cache);
    /// Changes whenever a scope opens or closes or a variable is declared: cached variable places are then stale.
    /// Code that pushes, pops or moves scopes itself (a host parking a coroutine) calls BumpVars.
    unsigned long long varEpoch = NextVarEpoch();
    void BumpVars() { varEpoch = NextVarEpoch(); }
    /// Like varEpoch, for cached globals no local can hide (a name not in localNames): those stay good across calls.
    /// Changes when a type rule may change, a static is set, a name becomes a local, or the functions are (re)loaded.
    unsigned long long globalEpoch = NextVarEpoch();
    void BumpGlobals() { globalEpoch = NextVarEpoch(); }
    /// The epoch a cache of the global `n` uses.
    unsigned long long GlobalEpochFor(const std::string& n) const { return localNames.count(n) ? varEpoch : globalEpoch; }
    /// A by-name cache is still good (epoch numbers are never reused, so the two can't be confused).
    bool CacheGood(const NameCache& c) const { return c.epoch == varEpoch || c.epoch == globalEpoch; }
    /// Names that can be locals (Program::localNames of every loaded source, plus any defined in a call's scope at
    /// run time). Lookup only walks the call stack for these; any other name is a global or a static.
    std::unordered_set<std::string> localNames;

    /// Call a native, a script function, `var.Method(...)`, or a variable holding a lambda, by name.
    V Call(const std::string& name, std::vector<V>& args, const std::vector<std::string>* names = nullptr, int line = 0);
    /// Call the script's own function `name` when it defines one, even when a builtin or native has the same name
    /// (the host calling an event handler such as `shake`); else as Call.
    V CallFunction(const std::string& name, std::vector<V>& args, const std::vector<std::string>* names = nullptr);
    /// Call a function value: a lambda, {"__fn": name}, or a function's name as text.
    V CallValue(const V& fn, std::vector<V>& args, const std::vector<std::string>* names = nullptr);
    V CallLambda(const V& f, std::vector<V>& args, const std::vector<std::string>* names = nullptr);
    static bool IsLambda(const V& v);

    /// Dotted names: a property of a variable (`xs.Count`, `m.key`), or the host's. `ok` is false when unknown.
    V GetDotted(const std::string& name, bool& ok);
    bool SetDotted(const std::string& name, const V& v);

    /// Count one loop iteration against Limits::maxLoopIterations (`what` names the loop in the error).
    void CountIteration(long long& n, const char* what) const;

    /// Run top-level code (compiled into `code` the first time).
    Flow RunBody(const StmtList& body, ChunkPtr& code) { return RunChunk(CodeFor(body, code)); }
    /// Run compiled code. A function's or lambda's code gets its frame's slots and their states. A generator's body
    /// runs with its GenRun: it starts or goes on where it stopped, and ends with Flow::Yield at a `yield value`.
    Flow RunChunk(const Chunk& c, V* slots = nullptr, unsigned char* slotState = nullptr, GenRun<V>* gen = nullptr);

    // ---- Closures: the box each lambda keeps (MakeLambda), by the lambda value's map ----
    struct ClosureRef { std::weak_ptr<const void> fn; V env; };
    std::unordered_map<const void*, ClosureRef> closures;
    std::size_t closureSweepAt = 64;
    /// The box the lambda f keeps, or 0 when it keeps none.
    V ClosureOf(const V& f) const;

    // ---- Generators: a call of a function with a `yield value` returns {"__gen": id} ----
    struct GenEntry { std::weak_ptr<const void> owner; std::unique_ptr<Generator<V>> gen; };
    std::unordered_map<long long, GenEntry> generators;
    long long nextGenerator = 1;
    std::size_t genSweepAt = 64;
    /// Make a generator for a body whose frame is ready (its arguments bound).
    V MakeGenerator(ChunkPtr code, V* slots, unsigned char* state, std::size_t n, const V& env,
                    const std::vector<std::string>* boxed);
    /// The generator a value is, or nullptr.
    Generator<V>* GeneratorOf(const V& v);
    /// Run the generator to its next `yield`: true with `out` set, false once it has finished.
    bool Resume(Generator<V>& g, V& out);
    /// g.next(), g.done(), g.to_list() on a generator; iterator objects in foreach. True when answered.
    bool GeneratorMethod(const V& v, const std::string& name, std::vector<V>& args, V& out);

    // ---- Modules (import "file.okay") ----
    std::shared_ptr<ModuleSystem<V>> modules;
    int moduleId = 0;          ///< 0: the main program; else its index in modules->byId
    std::string currentFile;   ///< the file this runtime's program came from (imports are found from it)
    /// Load (once) and return the module `path`'s exports: a map of its globals and functions.
    V Import(const std::string& path, int line);
    /// Add a parsed program's functions, classes and local names (Interpreter::Run and modules).
    void AddProgram(const Program& prog, const std::string& source);
    /// A function value {"__fn", "__mod"} or lambda of another module: run it in that module's runtime.
    BasicRuntime* ModuleOf(const V& fn);
    /// The value of one expression outside any body (a default argument, a field's starting value).
    V EvalCode(const Expr& e, ChunkPtr& code);
    V IndexRead(const V& a, const V& index);
    void IndexWrite(const V& a, const V& val, const V& index);
    V CallMethodOn(const V& v, const MethodCallExpr& e, std::vector<V>& vals);
    // What the nodes do once their operands are worked out (detail/Eval.ipp); the instructions call these.
    void LoadName(const std::string& n, NameCache* cache, V& out);
    V ReadQualified(const VarExpr& e);
    void WriteQualified(const std::string& n, const V& v);
    V ReadMember(const V& v, const MemberExpr& e);
    void WriteMember(const V& target, const MemberAssignExpr& e, const V& v);
    static V CastValue(const std::string& type, const V& v);
    V MakeLambda(const LambdaExpr& e);
    V IndexCompound(const V& a, const V& rhs, const V& index, const IndexCompoundAssignExpr& e);
    V Subscribe(const EventOrCompoundExpr& e, const V& fn);
    bool SubscribeByName(const EventOrCompoundExpr& e, V& out);
    V CompoundCurrent(const EventOrCompoundExpr& e);
    /// The line of the statement running now, and the value a `return` is carrying.
    int curLine = 0;
    V retVal;
    /// Give `e` the line of the statement that was running when it was raised, when it has none (and lines are on).
    void Stamp(ScriptError& e) const { if (e.line == 0) e.line = Ln(curLine); }

    /// Call the script function fn with argc arguments (the bytecode Call's fast path: no argument list made).
    V Invoke(const FunctionDecl& fn, const V* args, std::size_t argc, const std::vector<std::string>* names);
    /// Changes whenever the script functions are (re)defined: cached call targets are then stale (Chunk::calls).
    unsigned long long callEpoch = NextVarEpoch();
    void BumpCalls() { callEpoch = NextVarEpoch(); BumpGlobals(); }

private:
    V Invoke(const FunctionDecl& fn, std::vector<V>& args, const std::vector<std::string>* names);
};

} // namespace okayscript
