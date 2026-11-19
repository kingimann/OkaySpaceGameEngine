#include "okay/Lua/Lua.hpp"
#include "LuaCore.hpp"

namespace okay::lua {

struct Interpreter::Impl { State L; };

Interpreter::Interpreter() : m_impl(new Impl) { m_impl->L.OpenLibs(); }
Interpreter::~Interpreter() = default;

void Interpreter::SetLimits(const ScriptLimits& l) {
    m_impl->L.limits.instructions = l.instructions;
    m_impl->L.limits.seconds = l.seconds;
    m_impl->L.limits.memoryBytes = l.memoryBytes;
}
void Interpreter::SetOutput(std::function<void(const std::string&)> sink) { m_impl->L.out = std::move(sink); }

bool Interpreter::Run(const std::string& source, const std::string& chunkName, std::string* error) {
    return m_impl->L.DoString(source, chunkName, error);
}
bool Interpreter::Check(const std::string& source, const std::string& chunkName, std::string* error) {
    // Compiling adds a prototype to the state; use a throwaway state for pure checks.
    State tmp;
    return tmp.Compile(source, chunkName, error) != nullptr;
}
bool Interpreter::Eval(const std::string& expr, std::string* result, std::string* error) {
    State& L = m_impl->L;
    Proto* p = L.Compile("return " + expr, "(eval)", error);
    if (!p) return false;
    std::vector<Value> rets;
    if (!L.CallProtected(L.MakeFunction(p), {}, &rets, error)) return false;
    std::string out;
    for (std::size_t i = 0; i < rets.size(); ++i) {
        if (i) out += ", ";
        try { out += L.ToStringValue(rets[i]); } catch (LuaError& e) { if (error) *error = e.text; return false; }
    }
    if (result) *result = out;
    return true;
}
std::string Interpreter::GlobalText(const std::string& name) {
    State& L = m_impl->L;
    Value v = L.globals->GetStr(name);
    try { return L.ToStringValue(v); } catch (LuaError&) { return "?"; }
}
std::size_t Interpreter::MemoryUsed() const { return m_impl->L.allocated; }

}  // namespace okay::lua
