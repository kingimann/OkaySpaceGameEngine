// Scopes, typed variables, and calls (natives, script functions, lambdas, methods by dotted name).
// dup-allow-begin: ported from the engine's own OkayScriptVM Runtime (engine/src/Scripting/OkayScriptVMInternal.hpp and OkayScriptVM.cpp, written for this project) without classes, statics, coroutines and scene objects; the engine moves onto this copy in the integration step (docs/status/UNFINISHED.md "OkayScript standalone core")
// Written for this project.
#include "ValueMethods.hpp"
#include "okayscript/Interpreter.hpp"
#include "okayscript/Native.hpp"
#include <unordered_map>

namespace okayscript {

using detail::ToInt;
using detail::ToInt64;

// ---- Typed variables ---------------------------------------------------------------------------------------------
unsigned char TypeKind(const std::string& t) {
    static const std::unordered_map<std::string, unsigned char> k = {
        {"int", KInt}, {"long", KInt}, {"short", KInt}, {"byte", KInt}, {"uint", KInt},
        {"ulong", KInt}, {"ushort", KInt}, {"sbyte", KInt},
        {"float", KFloat}, {"double", KFloat}, {"decimal", KFloat},
        {"bool", KBool}, {"string", KString}};
    auto it = k.find(t);
    return it == k.end() ? (unsigned char)KAny : it->second;
}
const char* KindName(unsigned char k) {
    switch (k & 0x0F) { case KInt: return "an int"; case KFloat: return "a float"; case KBool: return "a bool";
                        case KString: return "a string"; default: return "a variable"; }
}
namespace {
const char* ValueKindName(const Value& v) {
    if (v.IsString()) return "text";
    if (v.IsBool()) return "true/false";
    if (v.IsNumber()) return "a number";
    if (v.IsArray()) return "a list";
    if (v.IsMap()) return "a map";
    return "that value";
}
} // namespace
Value CoerceToKind(unsigned char kind, const Value& v, const std::string& name) {
    auto fail = [&](const char* hint) -> Value {
        throw RuntimeError("'" + name + "' is " + KindName(kind) + ", so it can't hold " + ValueKindName(v) + hint, 0);
    };
    switch (kind & 0x0F) {
        case KInt:
            if (v.IsNumber()) return Value{(double)ToInt64(v.AsNumber())};   // drops the fraction, like (int)
            if (v.IsBool()) return Value{v.AsBool() ? 1.0 : 0.0};
            return fail(v.IsString() ? " (use int.Parse(text))" : "");
        case KFloat:
            if (v.IsNumber()) return v;
            if (v.IsBool()) return Value{v.AsBool() ? 1.0 : 0.0};
            return fail(v.IsString() ? " (use float.Parse(text))" : "");
        case KBool:
            if (v.IsBool()) return v;
            if (v.IsNumber()) return Value{v.AsNumber() != 0.0};
            return fail("");
        case KString:
            if (v.IsString()) return v;
            return fail(" (turn it into text with $\"{value}\" or .ToString())");
        default: return v;
    }
}
Value DefaultForKind(unsigned char kind) {
    switch (kind & 0x0F) { case KBool: return Value{false}; case KString: return Value{std::string()};
                           default: return Value{}; }
}

// ---- Scopes ------------------------------------------------------------------------------------------------------
void Runtime::Define(const std::string& n, const Value& v) {
    Environment& e = scopes.back();
    e.vars[n] = v;
    if (!e.kinds.empty()) e.kinds.erase(n);
}
void Runtime::DefineTyped(const std::string& n, const Value& v, unsigned char kind) {
    Environment& e = scopes.back();
    e.vars[n] = (kind & 0x0F) ? CoerceToKind(kind, v, n) : v;
    if (kind) e.kinds[n] = kind; else if (!e.kinds.empty()) e.kinds.erase(n);
}
namespace {
bool AssignIn(Environment& e, const std::string& n, const Value& v) {
    auto f = e.vars.find(n);
    if (f == e.vars.end()) return false;
    if (!e.kinds.empty()) {
        auto k = e.kinds.find(n);
        if (k != e.kinds.end()) {
            if (k->second & KConst) throw RuntimeError("'" + n + "' is const, so it can't change after it's set", 0);
            f->second = (k->second & 0x0F) ? CoerceToKind(k->second, v, n) : v;
            return true;
        }
    }
    f->second = v;
    return true;
}
} // namespace
// Every scope on the stack is searched, innermost first, then the globals (dynamic scoping, as in the engine): a
// function also sees the locals of the functions that called it, and a lambda the locals of whoever calls it.
void Runtime::Assign(const std::string& n, const Value& v) {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) if (AssignIn(*it, n, v)) return;
    Global().vars[n] = v;   // a new name becomes a global
}
Value Runtime::Get(const std::string& n) const {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto f = it->vars.find(n);
        if (f != it->vars.end()) return f->second;
    }
    return Value{};   // never set: 0
}
bool Runtime::Has(const std::string& n) const {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
        if (it->vars.count(n)) return true;
    return false;
}

void Runtime::CountIteration(long long& n, const char* what) const {
    if (limits.maxLoopIterations > 0 && ++n > limits.maxLoopIterations)
        throw RuntimeError(std::string(what) + " exceeded iteration limit (" + std::to_string(limits.maxLoopIterations) + ")", 0);
}

// ---- Dotted names ------------------------------------------------------------------------------------------------
Value Runtime::GetDotted(const std::string& name, bool& ok) {
    ok = true;
    Value out;
    if (host && host->ReadName(*this, name, out)) return out;
    const auto dot = name.rfind('.');
    if (dot != std::string::npos) {
        const std::string base = name.substr(0, dot), prop = name.substr(dot + 1);
        if (base.find('.') == std::string::npos) {
            if (Has(base) && detail::ValueProperty(Get(base), prop, out)) return out;
        } else {
            bool inner = false;
            Value bv = GetDotted(base, inner);
            if (inner && detail::ValueProperty(bv, prop, out)) return out;
        }
    }
    ok = false;
    return Value{};
}
bool Runtime::SetDotted(const std::string& name, const Value& v) {
    if (host && host->WriteName(*this, name, v)) return true;
    const auto dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    const std::string base = name.substr(0, dot), field = name.substr(dot + 1);
    Value bv;
    if (base.find('.') == std::string::npos) {
        if (!Has(base)) return false;
        bv = Get(base);
    } else {
        bool ok = false;
        bv = GetDotted(base, ok);
        if (!ok) return false;
    }
    if (auto m = bv.AsMap()) { (*m)[field] = v; return true; }   // m.key = v
    return false;
}

// ---- Calls -------------------------------------------------------------------------------------------------------
namespace {
struct CallDepthGuard {
    int& depth;
    CallDepthGuard(int& d, int max, const std::string& what) : depth(d) {
        if (depth >= max) throw RuntimeError(what + " calls itself too deeply (endless recursion?)", 0);
        ++depth;
    }
    ~CallDepthGuard() { --depth; }
};
bool AnyNamed(const std::vector<std::string>* names) {
    if (!names) return false;
    for (const std::string& n : *names) if (!n.empty()) return true;
    return false;
}
// Lay call arguments onto parameter slots. Extra positional arguments are ignored (engine rule).
void PlaceArgs(const std::vector<std::string>& params, const std::vector<Value>& args,
               const std::vector<std::string>* names, std::vector<char>& present,
               std::vector<Value>& slot, const std::string& what) {
    present.assign(params.size(), 0);
    slot.assign(params.size(), Value{});
    bool sawNamed = false;
    std::size_t next = 0;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string nm = (names && i < names->size()) ? (*names)[i] : std::string();
        if (nm.empty()) {
            if (sawNamed) throw RuntimeError(what + ": a value without a name can't follow a named one", 0);
            if (next >= params.size()) continue;
            slot[next] = args[i]; present[next] = 1; ++next;
        } else {
            sawNamed = true;
            std::size_t idx = params.size();
            for (std::size_t p = 0; p < params.size(); ++p) if (params[p] == nm) { idx = p; break; }
            if (idx == params.size()) throw RuntimeError(what + " has no parameter '" + nm + "'", 0);
            if (present[idx]) throw RuntimeError(what + ": '" + nm + "' was given twice", 0);
            slot[idx] = args[i]; present[idx] = 1;
        }
    }
}
// Errors from inside a native get the line of the call (when the native did not give one).
template <class F> Value WithLine(int line, F&& f) {
    try {
        return f();
    } catch (ScriptError& e) {
        if (e.line == 0) e.line = line;
        throw;
    }
}
} // namespace

bool Runtime::IsLambda(const Value& v) {
    auto m = v.AsMap();
    return m && m->count("__lambda");
}

Value Runtime::Invoke(const FunctionDecl& fn, std::vector<Value>& args, const std::vector<std::string>* names) {
    const std::string what = fn.name + "()";
    CallDepthGuard depthGuard(callDepth, limits.maxCallDepth, what);
    std::vector<char> present;
    std::vector<Value> slot;
    PlaceArgs(fn.params, args, names, present, slot, what);
    scopes.push_back(Environment{});
    struct PopScope { std::vector<Environment>& s; ~PopScope() { s.pop_back(); } } pop{scopes};
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        const unsigned char k = i < fn.paramKinds.size() ? fn.paramKinds[i] : 0;
        Value v;
        if (present[i]) v = slot[i];
        else if (i < fn.defaults.size() && fn.defaults[i]) v = fn.defaults[i]->Eval(*this);
        else v = k ? DefaultForKind(k) : Value{};
        if (!k) Define(fn.params[i], v);
        else DefineTyped(fn.params[i], v, k);
    }
    Value ret;
    try {
        for (auto& s : fn.body) s->Run(*this);
    } catch (ReturnSignal& rs) {
        ret = rs.value;
    } catch (BreakSignal&) {       // a stray break/continue (no loop around it) ends the function
    } catch (ContinueSignal&) {
    }
    if (fn.returnKind & 0x0F) return CoerceToKind(fn.returnKind, ret, what);
    return ret;
}

Value Runtime::Call(const std::string& name, std::vector<Value>& args, const std::vector<std::string>* names, int line) {
    return WithLine(line, [&]() -> Value {
        auto nat = natives.find(name);
        if (nat != natives.end()) {
            if (AnyNamed(names)) throw RuntimeError("'" + name + "' doesn't take named arguments", line);
            const Native& n = nat->second;
            if (!n.arity.Accepts(args.size()))
                throw RuntimeError(name + "() takes " + n.arity.Describe() + ", got " + std::to_string(args.size()), line);
            Args a(*this, name, args, line);
            try {
                return n.fn(a);
            } catch (const ScriptError&) {
                throw;
            } catch (const std::exception& e) {   // a C++ error inside a native is a script error on this line
                throw RuntimeError(name + "(): " + e.what(), line);
            }
        }
        auto f = functions.find(name);
        if (f != functions.end()) return Invoke(*f->second, args, names);

        // `name.Method(...)` on a variable: s.ToUpper(), xs.Add(3), m.ContainsKey("a").
        const auto dot = name.rfind('.');
        if (dot != std::string::npos && dot > 0) {
            const std::string base = name.substr(0, dot), method = name.substr(dot + 1);
            bool ok = false;
            Value obj;
            if (base.find('.') == std::string::npos) {
                if (Has(base)) { obj = Get(base); ok = true; }
            } else {
                obj = GetDotted(base, ok);
            }
            Value out;
            detail::LambdaInvoker inv = [this](const Value& fv, std::vector<Value>& xs) { return CallLambda(fv, xs); };
            if (ok && detail::CallValueMethod(obj, method, args, out, &inv)) return out;
            if (ok && obj.AsMap()) {   // a lambda stored in a map: m.handler(1)
                auto it = obj.AsMap()->find(method);
                if (it != obj.AsMap()->end() && IsLambda(it->second)) return CallLambda(it->second, args, names);
            }
            if (ok) throw RuntimeError("no method '" + method + "' on '" + base + "' (" + obj.TypeName() + ")", line);
        }
        if (Has(name) && IsLambda(Get(name))) return CallLambda(Get(name), args, names);   // Action f = () => ...; f();
        Value out;
        if (host && host->CallUnknown(*this, name, args, out)) return out;
        throw RuntimeError("call to undefined function '" + name + "'", line);
    });
}

Value Runtime::CallLambda(const Value& f, std::vector<Value>& args, const std::vector<std::string>* names) {
    auto m = f.AsMap();
    if (!m) throw RuntimeError("not a function", 0);
    auto it = m->find("__lambda");
    if (it == m->end()) throw RuntimeError("not a function", 0);
    auto lb = lambdas.find(ToInt(it->second.AsNumber()));
    if (lb == lambdas.end()) throw RuntimeError("lambda is no longer available", 0);
    CallDepthGuard depthGuard(callDepth, limits.maxCallDepth, "a lambda");
    const auto& ps = *lb->second.params;
    const std::vector<ExprPtr>* defs = lb->second.defaults;
    std::vector<char> present;
    std::vector<Value> slot;
    PlaceArgs(ps, args, names, present, slot, "lambda");
    scopes.push_back(Environment{});
    struct PopScope { std::vector<Environment>& s; ~PopScope() { s.pop_back(); } } pop{scopes};
    for (std::size_t i = 0; i < ps.size(); ++i) {
        Value v;
        if (present[i]) v = slot[i];
        else if (defs && i < defs->size() && (*defs)[i]) v = (*defs)[i]->Eval(*this);
        Define(ps[i], v);
    }
    Value ret;
    try { for (auto& st : *lb->second.body) st->Run(*this); }
    catch (ReturnSignal& rs) { ret = rs.value; }
    return ret;
}

Value Runtime::CallValue(const Value& fn, std::vector<Value>& args, const std::vector<std::string>* names) {
    if (IsLambda(fn)) return CallLambda(fn, args, names);
    if (auto m = fn.AsMap()) {
        auto it = m->find("__fn");
        if (it != m->end()) return Call(it->second.AsString(), args, names);
    }
    if (fn.IsString()) return Call(fn.AsString(), args, names);
    throw RuntimeError(std::string("not a function (") + fn.TypeName() + ")", 0);
}

// ---- Natives -----------------------------------------------------------------------------------------------------
std::string Arity::Describe() const {
    auto args = [](int n) { return std::to_string(n) + (n == 1 ? " argument" : " arguments"); };
    if (max < 0) return min == 0 ? std::string("any number of arguments") : "at least " + args(min);
    if (min == max) return args(min);
    return std::to_string(min) + " to " + args(max);
}

void Args::Fail(const std::string& message) const { throw RuntimeError(m_name + "(): " + message, m_line); }

void Args::Need(std::size_t i, const char* type) const {
    if (i >= m_values.size()) Fail("argument " + std::to_string(i + 1) + " is missing (" + type + " expected)");
    Fail("argument " + std::to_string(i + 1) + " must be a " + type + ", got " + m_values[i].TypeName());
}
double Args::Number(std::size_t i) const {
    if (i >= m_values.size() || !m_values[i].IsNumber()) Need(i, "number");
    return m_values[i].AsNumber();
}
std::string Args::String(std::size_t i) const {
    if (i >= m_values.size() || !m_values[i].IsString()) Need(i, "string");
    return m_values[i].AsString();
}
bool Args::Bool(std::size_t i) const {
    if (i >= m_values.size() || !m_values[i].IsBool()) Need(i, "bool");
    return m_values[i].AsBool();
}
Value::ArrayRef Args::List(std::size_t i) const {
    if (i >= m_values.size() || !m_values[i].AsArray()) Need(i, "list");
    return m_values[i].AsArray();
}
Value::MapRef Args::Map(std::size_t i) const {
    if (i >= m_values.size() || !m_values[i].AsMap()) Need(i, "map");
    return m_values[i].AsMap();
}
Value Args::Call(const Value& fn, std::vector<Value> args) const {
    try {
        return m_rt.CallValue(fn, args);
    } catch (ScriptError& e) {
        if (e.line == 0) e.line = m_line;
        throw;
    }
}

} // namespace okayscript
// dup-allow-end
