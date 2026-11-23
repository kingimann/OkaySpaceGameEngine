// Templates: copies of arguments, the instance requests, mangled types and instance names.
// Moved out of parser.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "ast.hpp"

namespace okaycc {
namespace compiler {
std::vector<TArg> cloneArgs(const std::vector<TArg>& in) {
    std::vector<TArg> out;
    for (const TArg& a : in) {
        TArg c;
        if (a.type) c.type = cloneType(*a.type);
        c.value = a.value;
        c.isValue = a.isValue;
        out.push_back(std::move(c));
    }
    return out;
}

std::map<std::string, TemplateSig>& funcTemplateSigs() {
    static std::map<std::string, TemplateSig> m;
    return m;
}

std::vector<InstReq>& instRequests() {
    static std::vector<InstReq> v;
    return v;
}

bool& ehActive() {
    static bool on = false;
    return on;
}

bool& forceExceptions() {
    static bool force = false;
    return force;
}

bool& instDirty() {
    static bool d = false;
    return d;
}

// Where the Itanium-style mangled type that starts at s[j] ends: a builtin letter, P/R and a type,
// <length><name> (a class, possibly an instance "Box<...>" counted whole), or Li<n>E. Returns j when
// there is no type there.
std::size_t mangledEnd(const std::string& s, std::size_t j) {
    if (j >= s.size()) return j;
    char c = s[j];
    if (c == 'P' || c == 'R' || c == 'O') {
        std::size_t k = mangledEnd(s, j + 1);
        return k == j + 1 ? j : k;
    }
    if (c == 'L') {
        std::size_t k = s.find('E', j);
        return k == std::string::npos ? j : k + 1;
    }
    if (c >= '0' && c <= '9') {
        std::size_t k = j;
        std::size_t n = 0;
        while (k < s.size() && s[k] >= '0' && s[k] <= '9') n = n * 10 + static_cast<std::size_t>(s[k++] - '0');
        return k + n <= s.size() ? k + n : j;
    }
    if (std::string("vbchstijlmfd").find(c) != std::string::npos) return j + 1;
    return j;
}

// The type a mangled string names (builtin letters, pointers and class names).
std::unique_ptr<Type> typeFromMangle(const std::string& m) {
    auto make = [](Type::Kind k) {
        auto t = std::make_unique<Type>();
        t->kind = k;
        return t;
    };
    if (m.empty()) return nullptr;
    char c = m[0];
    if (c == 'P' || c == 'R' || c == 'O') {
        auto inner = typeFromMangle(m.substr(1));
        if (!inner) return nullptr;
        auto t = make(c == 'P' ? Type::Kind::Pointer : Type::Kind::Reference);
        t->rref = c == 'O';
        t->inner = std::move(inner);
        return t;
    }
    if (c >= '0' && c <= '9') {
        std::size_t k = 0;
        std::size_t n = 0;
        while (k < m.size() && m[k] >= '0' && m[k] <= '9') n = n * 10 + static_cast<std::size_t>(m[k++] - '0');
        auto t = make(Type::Kind::Struct);
        t->tag = m.substr(k, n);
        return t;
    }
    switch (c) {
    case 'v': return make(Type::Kind::Void);
    case 'b': return make(Type::Kind::Bool);
    case 'c': return make(Type::Kind::Char);
    case 'h': { auto t = make(Type::Kind::Char); t->unsign = true; return t; }
    case 's': return make(Type::Kind::Short);
    case 't': { auto t = make(Type::Kind::Short); t->unsign = true; return t; }
    case 'i': return make(Type::Kind::Int);
    case 'j': { auto t = make(Type::Kind::Int); t->unsign = true; return t; }
    case 'l': return make(Type::Kind::Long);
    case 'm': { auto t = make(Type::Kind::Long); t->unsign = true; return t; }
    case 'f': return make(Type::Kind::Float);
    case 'd': return make(Type::Kind::Double);
    default: return nullptr;
    }
}

std::string argMangle(const TArg& a) {
    if (a.isValue) return "Li" + std::to_string(a.value) + "E";
    return a.type ? mangleType(*a.type) : "i";
}

std::unique_ptr<Type> substType(const Type& pat, const TemplateSig& sig, const std::vector<TArg>& args) {
    if (pat.kind == Type::Kind::Struct && !pat.tag.empty() && pat.tag[0] == '\x02') {
        for (std::size_t i = 0; i < sig.names.size() && i < args.size(); ++i)
            if (sig.names[i] == pat.tag.substr(1) && args[i].type) return cloneType(*args[i].type);
        return cloneType(pat);
    }
    auto n = cloneType(pat);
    if (pat.inner) n->inner = substType(*pat.inner, sig, args);
    for (std::size_t i = 0; i < pat.params.size(); ++i)
        if (pat.params[i].type) n->params[i].type = substType(*pat.params[i].type, sig, args);
    return n;
}

// Name of a function template instance: _Z <name> I <arguments> E <parameter types>.
std::string instFuncName(const std::string& key, const TemplateSig& sig, const std::vector<TArg>& args) {
    // An overload of a function template is kept as "name#k": the name is what is mangled.
    std::string tmpl = key.substr(0, key.find('#'));
    std::string out = "_Z" + mangleNested(tmpl) + "I";
    for (const TArg& a : args) out += argMangle(a);
    out += "E";
    if (!sig.fn || sig.fn->params.empty()) return out + "v";
    for (const Param& p : sig.fn->params) {
        auto t = p.type ? substType(*p.type, sig, args) : nullptr;
        out += t ? mangleType(*t) : "i";
    }
    return out;
}


}  // namespace compiler
}  // namespace okaycc
