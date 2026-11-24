#pragma once
// The embedding API (BasicInterpreter): natives, globals, running source, calling functions. Written for this project.
// Include this file once in the program that instantiates the core for its own value type (detail/Eval.ipp too).
#include "okayscript/Interpreter.hpp"
#include "okayscript/Parser.hpp"
#include "okayscript/detail/Eval.ipp"
#include <algorithm>
#include <iostream>

namespace okayscript {


template <class V> BasicInterpreter<V>::BasicInterpreter() {
    m_rt.out = &std::cout;
    m_rt.in = &std::cin;
    m_rt.builtins["__new_list"] = detail::NewList<V>;
    m_rt.builtins["__new_array"] = detail::NewArray<V>;
    m_rt.builtins["__new_map"] = detail::NewMap<V>;
}

template <class V> void BasicInterpreter<V>::Define(const std::string& name, BasicNativeFn<V> fn, Arity arity) {
    m_rt.natives[name] = BasicNative<V>{std::move(fn), arity};
}
template <class V> bool BasicInterpreter<V>::HasNative(const std::string& name) const { return m_rt.natives.count(name) != 0; }
template <class V> std::vector<std::string> BasicInterpreter<V>::NativeNames() const {
    std::vector<std::string> names;
    for (auto& kv : m_rt.natives) if (kv.first.rfind("__", 0) != 0) names.push_back(kv.first);
    std::sort(names.begin(), names.end());
    return names;
}

template <class V> void BasicInterpreter<V>::SetGlobal(const std::string& name, const V& v) { m_rt.Global().vars[name] = v; }
template <class V> V BasicInterpreter<V>::GetGlobal(const std::string& name) const {
    auto& g = m_rt.scopes.front().vars;
    auto it = g.find(name);
    return it == g.end() ? V{} : it->second;
}
template <class V> bool BasicInterpreter<V>::HasGlobal(const std::string& name) const {
    return m_rt.scopes.front().vars.count(name) != 0;
}

template <class V> const Program& BasicInterpreter<V>::Parse(const std::string& source) {
    auto prog = std::make_unique<Program>();
    ParseSource(source, *prog);   // SyntaxError on failure: nothing is added
    for (auto& kv : prog->functions) m_rt.functions[kv.first] = &kv.second;
    for (auto& kv : prog->classes) m_rt.classes[kv.first] = &kv.second;
    m_rt.statics = BasicRuntime<V>::SharedStatics(source);   // `static` variables are shared by every run of this source
    m_programs.push_back(std::move(prog));
    return *m_programs.back();
}

template <class V> void BasicInterpreter<V>::Load(const std::string& source, const std::string&) { Parse(source); }

template <class V> void BasicInterpreter<V>::Run(const std::string& source, const std::string&) {
    const Program& prog = Parse(source);
    try {
        for (auto& s : prog.top) m_rt.Run(*s);
    } catch (ReturnSignal<V>&) {   // `return` at the top level ends the program
    } catch (BreakSignal&) {
    } catch (ContinueSignal&) {
    } catch (const RuntimeError&) {
        m_rt.scopes.resize(1);
        throw;
    } catch (const ScriptError& e) {
        m_rt.scopes.resize(1);
        throw RuntimeError(e);
    }
}

template <class V> bool BasicInterpreter<V>::HasFunction(const std::string& name) const { return m_rt.functions.count(name) != 0; }
template <class V> std::vector<std::string> BasicInterpreter<V>::FunctionParams(const std::string& name) const {
    auto it = m_rt.functions.find(name);
    return it == m_rt.functions.end() ? std::vector<std::string>{} : it->second->params;
}

template <class V> V BasicInterpreter<V>::Call(const std::string& name, std::vector<V> args) {
    try {
        return m_rt.Call(name, args);
    } catch (const RuntimeError&) {
        m_rt.scopes.resize(1);
        throw;
    } catch (const ScriptError& e) {
        m_rt.scopes.resize(1);
        throw RuntimeError(e);
    }
}

template <class V> void BasicInterpreter<V>::SetOutput(std::ostream* out) { m_rt.out = out ? out : &std::cout; }
template <class V> void BasicInterpreter<V>::SetInput(std::istream* in) { m_rt.in = in ? in : &std::cin; }
template <class V> std::ostream& BasicInterpreter<V>::Output() const { return *m_rt.out; }
template <class V> std::istream& BasicInterpreter<V>::Input() const { return *m_rt.in; }

} // namespace okayscript
