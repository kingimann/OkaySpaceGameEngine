#pragma once
// The OkayScript interpreter core: scopes, calls, natives and the walk over the syntax tree (Ast.hpp).
//
// It is a template over the value type V, so one core runs both the standalone okayscript::Value (Value.hpp) and the
// engine's vs::VsValue. V must have the script-value interface: constructors from float, bool, std::string and its
// vector type; MakeArray/MakeMap; IsNumber/IsBool/IsString/IsVec3/IsArray/IsMap/IsObject; AsFloat/AsBool/AsString/
// AsVec3/AsArray/AsMap/AsObjectId. The definitions are in detail/Runtime.ipp and detail/Eval.ipp; the standalone value
// is instantiated in src/Instantiate.cpp, another value type where its program includes those files once.
#include "okayscript/Ast.hpp"
#include "okayscript/Environment.hpp"
#include "okayscript/Error.hpp"
#include "okayscript/Native.hpp"
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

template <class V> struct BasicEnvironment {
    std::unordered_map<std::string, V> vars;
    std::unordered_map<std::string, unsigned char> kinds;   ///< typed / const variables only
};

/// `return` carries its value as this (not a std::exception, so try/catch in scripts does not catch it).
template <class V> struct ReturnSignal { V value; };

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
    virtual bool Yield(BasicRuntime<V>&, const V& /*value*/) { return false; }
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

    /// Call a native, a script function, `var.Method(...)`, or a variable holding a lambda, by name.
    V Call(const std::string& name, std::vector<V>& args, const std::vector<std::string>* names = nullptr, int line = 0);
    /// Call a function value: a lambda, {"__fn": name}, or a function's name as text.
    V CallValue(const V& fn, std::vector<V>& args, const std::vector<std::string>* names = nullptr);
    V CallLambda(const V& f, std::vector<V>& args, const std::vector<std::string>* names = nullptr);
    static bool IsLambda(const V& v);

    /// Dotted names: a property of a variable (`xs.Count`, `m.key`), or the host's. `ok` is false when unknown.
    V GetDotted(const std::string& name, bool& ok);
    bool SetDotted(const std::string& name, const V& v);

    /// Count one loop iteration against Limits::maxLoopIterations (`what` names the loop in the error).
    void CountIteration(long long& n, const char* what) const;

    /// Work out an expression / run a statement (Run also gives an error raised inside it the statement's line).
    V Eval(const Expr& e);
    void Exec(const Stmt& s);
    void Run(const Stmt& s);
    void RunList(const StmtList& list);

private:
    V Invoke(const FunctionDecl& fn, std::vector<V>& args, const std::vector<std::string>* names);
};

} // namespace okayscript
