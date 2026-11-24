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
// BasicInterpreter is the same for any value type (Runtime.hpp); Interpreter is the standalone one.
#include "okayscript/Ast.hpp"
#include "okayscript/Runtime.hpp"
#include "okayscript/Value.hpp"
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace okayscript {

template <class V> class BasicInterpreter {
public:
    BasicInterpreter();
    BasicInterpreter(const BasicInterpreter&) = delete;
    BasicInterpreter& operator=(const BasicInterpreter&) = delete;

    /// Register a native function (replaces one of the same name). Natives are found before script functions.
    void Define(const std::string& name, BasicNativeFn<V> fn, Arity arity = Arity::Any());
    bool HasNative(const std::string& name) const;
    /// Every native's name, sorted.
    std::vector<std::string> NativeNames() const;

    /// Globals: set before a run (constants like `PI`, settings) or read after one.
    void SetGlobal(const std::string& name, const V& v);
    V GetGlobal(const std::string& name) const;
    bool HasGlobal(const std::string& name) const;

    /// Parse `source` (SyntaxError on failure), add its functions, then run its top-level statements (RuntimeError on
    /// failure). Errors carry the line; `file` is for the caller's messages (FormatError). The parsed program
    /// stays alive with the interpreter, so its functions and lambdas can be called afterwards. Calling Run again adds
    /// to the same globals and functions.
    void Run(const std::string& source, const std::string& file = "");
    /// Parse and add the functions without running anything (SyntaxError on failure).
    void Load(const std::string& source, const std::string& file = "");

    bool HasFunction(const std::string& name) const;
    /// Parameters of a script function (empty when there is none).
    std::vector<std::string> FunctionParams(const std::string& name) const;
    /// Call a script function or native by name (RuntimeError on failure).
    V Call(const std::string& name, std::vector<V> args = {});

    void SetOutput(std::ostream* out);
    void SetInput(std::istream* in);
    std::ostream& Output() const;
    std::istream& Input() const;
    void SetHost(BasicHost<V>* host) { m_rt.hooks = host; }
    Limits& GetLimits() { return m_rt.limits; }

    BasicRuntime<V>& Rt() { return m_rt; }

private:
    const Program& Parse(const std::string& source);
    BasicRuntime<V> m_rt;
    std::vector<std::unique_ptr<Program>> m_programs;
};

using Interpreter = BasicInterpreter<Value>;
using Runtime = BasicRuntime<Value>;
using Host = BasicHost<Value>;
using Args = BasicArgs<Value>;
using NativeFn = BasicNativeFn<Value>;
using Native = BasicNative<Value>;
using Environment = BasicEnvironment<Value>;

extern template class BasicRuntime<Value>;
extern template class BasicArgs<Value>;
extern template class BasicInterpreter<Value>;

} // namespace okayscript
