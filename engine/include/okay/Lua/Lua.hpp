#pragma once
// Our own Lua 5.4 interpreter (written from the public Lua 5.4 reference manual; no code
// from the Lua project or LuaJIT). This is the plain language core: no engine types.
// The engine binding (GameObject, Transform, Input ...) is okay::LuaScriptVM.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace okay::lua {

struct ScriptLimits {
    std::int64_t instructions = 5'000'000;   ///< VM instructions per entry call (0 = unlimited)
    double       seconds      = 0.5;         ///< wall-clock seconds per entry call (0 = unlimited)
    std::size_t  memoryBytes  = 64u << 20;   ///< memory cap for the script (0 = unlimited)
};

class Interpreter {
public:
    Interpreter();
    ~Interpreter();
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;

    void SetLimits(const ScriptLimits& l);
    /// Where print() and io.write() send text (default: standard output).
    void SetOutput(std::function<void(const std::string&)> sink);
    /// Compile and run a chunk. Returns false and fills `error` ("name:line: message").
    bool Run(const std::string& source, const std::string& chunkName, std::string* error);
    /// Check the syntax only; nothing runs.
    bool Check(const std::string& source, const std::string& chunkName, std::string* error);
    /// Evaluate an expression list and return tostring() of each result joined by ", ".
    bool Eval(const std::string& expression, std::string* result, std::string* error);
    /// tostring() of a global ("nil" when unset).
    std::string GlobalText(const std::string& name);
    /// Estimated bytes the script's objects use right now.
    std::size_t MemoryUsed() const;

    struct Impl;
    Impl& GetImpl() { return *m_impl; }
private:
    std::unique_ptr<Impl> m_impl;
};

}  // namespace okay::lua
