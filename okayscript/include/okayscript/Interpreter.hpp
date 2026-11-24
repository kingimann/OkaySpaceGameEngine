#pragma once
// Running OkayScript.
//
//   okayscript::Interpreter in;
//   okayscript::InstallStandardLibrary(in);                 // print, len, str, num, math (Stdlib.hpp)
//   in.Define("twice", [](okayscript::Args& a) { return okayscript::Value{a.Number(0) * 2}; },
//             okayscript::Arity::Exactly(1));
//   in.Run("var x = twice(21); print(x)", "demo.okay");     // prints 42
//
// Run throws SyntaxError when the source cannot be read and RuntimeError when it fails while running; both carry the
// line. Embedders that want the program's own words (Time.deltaTime, a scene, ...) give it a Host.
#include "okayscript/Ast.hpp"
#include "okayscript/Environment.hpp"
#include "okayscript/Error.hpp"
#include "okayscript/Native.hpp"
#include "okayscript/Value.hpp"
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace okayscript {

class Interpreter;

/// What the program running the interpreter adds to the language. Every hook is optional (return false = "not mine").
/// This is the seam the engine fills in for its own names (its builtins are natives; its properties come through
/// here).
class Host {
public:
    virtual ~Host() = default;
    /// A dotted name the language does not know (`Time.deltaTime`): set `out` and return true to answer it.
    virtual bool ReadName(Runtime&, const std::string& /*name*/, Value& /*out*/) { return false; }
    /// An assignment to such a name (`Time.timeScale = 0.5`): return true when handled.
    virtual bool WriteName(Runtime&, const std::string& /*name*/, const Value& /*v*/) { return false; }
    /// A call to a function that is neither a native nor a script function: set `out` and return true to answer it.
    virtual bool CallUnknown(Runtime&, const std::string& /*name*/, std::vector<Value>& /*args*/, Value& /*out*/) { return false; }
};

/// Safety limits (the engine's values by default).
struct Limits {
    int maxCallDepth = 200;                    ///< deeper script recursion is a runtime error, not a crash
    long long maxLoopIterations = 1000000;     ///< per loop run; 0 = no limit
};

/// `exit(code)` from a script: stops the program; Run lets it through so the host can end with that code.
struct ExitRequest { int code = 0; };

/// The interpreter state the syntax tree runs against: scopes, functions, natives. The AST nodes use it directly;
/// embedders normally use Interpreter instead.
class Runtime {
public:
    std::vector<Environment> scopes{1};                        ///< scopes[0] = global; one more per running call
    std::unordered_map<std::string, const FunctionDecl*> functions;
    std::unordered_map<std::string, Native> natives;
    Host* host = nullptr;
    Limits limits;
    std::ostream* out = nullptr;   ///< where print writes (std::cout by default)
    std::istream* in = nullptr;    ///< where input reads (std::cin by default)
    int callDepth = 0;

    struct LambdaBody {
        const std::vector<std::string>* params;
        const StmtList* body;
        const std::vector<ExprPtr>* defaults;
    };
    std::unordered_map<int, LambdaBody> lambdas;

    Environment& Global() { return scopes.front(); }
    void Define(const std::string& n, const Value& v);
    void DefineTyped(const std::string& n, const Value& v, unsigned char kind);
    void Assign(const std::string& n, const Value& v);
    Value Get(const std::string& n) const;
    bool Has(const std::string& n) const;

    /// Call a native, a script function, `var.Method(...)`, or a variable holding a lambda, by name.
    Value Call(const std::string& name, std::vector<Value>& args, const std::vector<std::string>* names = nullptr,
               int line = 0);
    /// Call a function value: a lambda, {"__fn": name}, or a function's name as text.
    Value CallValue(const Value& fn, std::vector<Value>& args, const std::vector<std::string>* names = nullptr);
    Value CallLambda(const Value& f, std::vector<Value>& args, const std::vector<std::string>* names = nullptr);
    static bool IsLambda(const Value& v);

    /// Dotted names: a property of a variable (`xs.Count`, `m.key`), or the host's. `ok` is false when unknown.
    Value GetDotted(const std::string& name, bool& ok);
    bool SetDotted(const std::string& name, const Value& v);

    /// Count one loop iteration against Limits::maxLoopIterations (`what` names the loop in the error).
    void CountIteration(long long& n, const char* what) const;

private:
    Value Invoke(const FunctionDecl& fn, std::vector<Value>& args, const std::vector<std::string>* names);
};

class Interpreter {
public:
    Interpreter();
    ~Interpreter();
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;

    /// Register a native function (replaces one of the same name). Natives are found before script functions.
    void Define(const std::string& name, NativeFn fn, Arity arity = Arity::Any());
    bool HasNative(const std::string& name) const;
    /// Every native's name, sorted.
    std::vector<std::string> NativeNames() const;

    /// Globals: set before a run (constants like `PI`, settings) or read after one.
    void SetGlobal(const std::string& name, const Value& v);
    Value GetGlobal(const std::string& name) const;
    bool HasGlobal(const std::string& name) const;

    /// Parse `source` (SyntaxError on failure), add its functions, then run its top-level statements (RuntimeError on
    /// failure). Errors carry the line; `file` is for the caller's messages (FormatError). The parsed program stays alive with the interpreter, so its
    /// functions and lambdas can be called afterwards. Calling Run again adds to the same globals and functions.
    void Run(const std::string& source, const std::string& file = "");
    /// Parse and add the functions without running anything (SyntaxError on failure).
    void Load(const std::string& source, const std::string& file = "");

    bool HasFunction(const std::string& name) const;
    /// Parameters of a script function (empty when there is none).
    std::vector<std::string> FunctionParams(const std::string& name) const;
    /// Call a script function or native by name (RuntimeError on failure).
    Value Call(const std::string& name, std::vector<Value> args = {});

    void SetOutput(std::ostream* out);
    void SetInput(std::istream* in);
    std::ostream& Output() const;
    std::istream& Input() const;
    void SetHost(Host* host) { m_rt.host = host; }
    Limits& GetLimits() { return m_rt.limits; }

    Runtime& Rt() { return m_rt; }

private:
    const Program& Parse(const std::string& source);
    Runtime m_rt;
    std::vector<std::unique_ptr<Program>> m_programs;
};

} // namespace okayscript
