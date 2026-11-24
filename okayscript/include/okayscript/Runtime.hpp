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
    /// Run compiled code. A function's or lambda's code gets its frame's slots and their states.
    Flow RunChunk(const Chunk& c, V* slots = nullptr, unsigned char* slotState = nullptr);
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
    void BumpCalls() { callEpoch = NextVarEpoch(); }

private:
    V Invoke(const FunctionDecl& fn, std::vector<V>& args, const std::vector<std::string>* names);
};

} // namespace okayscript
