// The embedding API: natives, globals, running source, calling functions. Written for this project.
#include "okayscript/Interpreter.hpp"
#include "ValueMethods.hpp"
#include "okayscript/Parser.hpp"
#include <algorithm>
#include <iostream>

namespace okayscript {

namespace {
// dup-allow-begin: the engine VM's __new_list / __new_array / __new_map (engine/src/Scripting/OkayScriptVM_Alias.cpp), here as core natives; one copy once the engine moves onto this library (docs/status/UNFINISHED.md "OkayScript standalone core")
// What `new List<T>()`, `new Dictionary<K, V>()` and `new T[n]` turn into (the parser lowers them to these calls).
void InstallCoreNatives(Runtime& rt) {
    rt.natives["__new_list"] = Native{[](Args& a) {
        Value v = Value::MakeArray();
        if (a.Size() > 0) if (auto src = a[0].AsArray()) *v.AsArray() = *src;   // initializer / copy
        return v;
    }, Arity::Between(0, 1)};
    rt.natives["__new_array"] = Native{[](Args& a) {
        Value v = Value::MakeArray();
        const int n = a.Size() > 0 ? detail::ToInt(a[0].AsNumber()) : 0;
        if (n > (1 << 20)) a.Fail("an array can have at most 1048576 items");
        if (n > 0) v.AsArray()->assign((std::size_t)n, Value{});
        return v;
    }, Arity::Between(0, 1)};
    rt.natives["__new_map"] = Native{[](Args& a) {
        Value v = Value::MakeMap();
        if (a.Size() == 0) return v;
        if (auto src = a[0].AsMap()) { *v.AsMap() = *src; return v; }
        if (auto pairs = a[0].AsArray())                          // { {"a", 1}, {"b", 2} }
            for (auto& pr : *pairs)
                if (auto kv = pr.AsArray(); kv && kv->size() >= 2) (*v.AsMap())[(*kv)[0].AsString()] = (*kv)[1];
        return v;
    }, Arity::Between(0, 1)};
}
// dup-allow-end
} // namespace

Interpreter::Interpreter() {
    m_rt.out = &std::cout;
    m_rt.in = &std::cin;
    InstallCoreNatives(m_rt);
}
Interpreter::~Interpreter() = default;

void Interpreter::Define(const std::string& name, NativeFn fn, Arity arity) {
    m_rt.natives[name] = Native{std::move(fn), arity};
}
bool Interpreter::HasNative(const std::string& name) const { return m_rt.natives.count(name) != 0; }
std::vector<std::string> Interpreter::NativeNames() const {
    std::vector<std::string> names;
    for (auto& kv : m_rt.natives) if (kv.first.rfind("__", 0) != 0) names.push_back(kv.first);
    std::sort(names.begin(), names.end());
    return names;
}

void Interpreter::SetGlobal(const std::string& name, const Value& v) { m_rt.Global().vars[name] = v; }
Value Interpreter::GetGlobal(const std::string& name) const {
    auto& g = m_rt.scopes.front().vars;
    auto it = g.find(name);
    return it == g.end() ? Value{} : it->second;
}
bool Interpreter::HasGlobal(const std::string& name) const { return m_rt.scopes.front().vars.count(name) != 0; }

const Program& Interpreter::Parse(const std::string& source) {
    auto prog = std::make_unique<Program>();
    ParseSource(source, *prog);   // SyntaxError on failure: nothing is added
    for (auto& kv : prog->functions) m_rt.functions[kv.first] = &kv.second;
    m_programs.push_back(std::move(prog));
    return *m_programs.back();
}

void Interpreter::Load(const std::string& source, const std::string&) { Parse(source); }

void Interpreter::Run(const std::string& source, const std::string&) {
    const Program& prog = Parse(source);
    try {
        for (auto& s : prog.top) s->Run(m_rt);
    } catch (ReturnSignal&) {     // `return` at the top level ends the program
    } catch (BreakSignal&) {
    } catch (ContinueSignal&) {
    } catch (const RuntimeError&) {
        m_rt.scopes.resize(1);
        m_rt.callDepth = 0;
        throw;
    } catch (const ScriptError& e) {
        m_rt.scopes.resize(1);
        m_rt.callDepth = 0;
        throw RuntimeError(e);
    }
}

bool Interpreter::HasFunction(const std::string& name) const { return m_rt.functions.count(name) != 0; }
std::vector<std::string> Interpreter::FunctionParams(const std::string& name) const {
    auto it = m_rt.functions.find(name);
    return it == m_rt.functions.end() ? std::vector<std::string>{} : it->second->params;
}

Value Interpreter::Call(const std::string& name, std::vector<Value> args) {
    try {
        return m_rt.Call(name, args);
    } catch (const RuntimeError&) {
        m_rt.scopes.resize(1);
        m_rt.callDepth = 0;
        throw;
    } catch (const ScriptError& e) {
        m_rt.scopes.resize(1);
        m_rt.callDepth = 0;
        throw RuntimeError(e);
    }
}

void Interpreter::SetOutput(std::ostream* out) { m_rt.out = out ? out : &std::cout; }
void Interpreter::SetInput(std::istream* in) { m_rt.in = in ? in : &std::cin; }
std::ostream& Interpreter::Output() const { return *m_rt.out; }
std::istream& Interpreter::Input() const { return *m_rt.in; }

} // namespace okayscript
