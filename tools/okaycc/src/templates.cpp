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

bool packPlaceGetsEnd(const std::string& param, const Type& arg) {
    return param.find("__pk") != std::string::npos && arg.kind == Type::Kind::Struct && arg.tag == "__okay_pack_end";
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
    if (c == 'P' || c == 'R' || c == 'O' || c == 'K') {
        std::size_t k = mangledEnd(s, j + 1);
        return k == j + 1 ? j : k;
    }
    if (c == 'A') {
        std::size_t k = j + 1;
        while (k < s.size() && s[k] >= '0' && s[k] <= '9') ++k;
        if (k >= s.size() || s[k] != '_') return j;
        std::size_t e = mangledEnd(s, k + 1);
        return e == k + 1 ? j : e;
    }
    if (c == 'F') {
        // A function type: F <return> <parameters> E.
        std::size_t k = j + 1;
        while (k < s.size() && s[k] != 'E') {
            std::size_t e = mangledEnd(s, k);
            if (e == k) return j;
            k = e;
        }
        return k < s.size() ? k + 1 : j;
    }
    if (c == 'L') {
        std::size_t k = s.find('E', j);
        return k == std::string::npos ? j : k + 1;
    }
    if (c >= '0' && c <= '9') {
        std::size_t k = j;
        std::size_t n = 0;
        while (k < s.size() && s[k] >= '0' && s[k] <= '9') n = n * 10 + static_cast<std::size_t>(s[k++] - '0');
        if (k + n > s.size()) return j;
        k += n;
        if (k < s.size() && s[k] == 'I') {
            // A class template instance: <name> I <arguments> E (Itanium C++ ABI 5.1.5).
            ++k;
            while (k < s.size() && s[k] != 'E') {
                std::size_t e = mangledEnd(s, k);
                if (e == k) return j;
                k = e;
            }
            return k < s.size() ? k + 1 : j;
        }
        return k;
    }
    if (c == 'N') {
        // A qualified name: N <part>... E.
        std::size_t k = j + 1;
        while (k < s.size() && s[k] != 'E') {
            std::size_t e = mangledEnd(s, k);
            if (e == k) return j;
            k = e;
        }
        return k < s.size() ? k + 1 : j;
    }
    if (std::string("vbchstijlmfd").find(c) != std::string::npos) return j + 1;
    return j;
}

bool matchInstanceTag(const std::string& P, const std::string& Q, bool strict,
                      const std::function<bool(const std::string&, std::unique_ptr<Type>)>& bind) {
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < P.size() && j < Q.size()) {
        std::size_t k = i;
        std::size_t n = 0;
        while (k < P.size() && P[k] >= '0' && P[k] <= '9') n = n * 10 + static_cast<std::size_t>(P[k++] - '0');
        if (k > i && k < P.size() && P[k] == '\x02' && n >= 1) {
            std::string name = P.substr(k + 1, n - 1);
            std::size_t end = mangledEnd(Q, j);
            if (end == j) return !strict;
            auto found = typeFromMangle(Q.substr(j, end - j));
            if (!found) {
                if (strict) return false;
            } else if (!bind(name, std::move(found))) {
                return false;
            }
            i = k + n;
            j = end;
            continue;
        }
        if (P[i] != Q[j]) return !strict;
        ++i;
        ++j;
    }
    return !strict || (i == P.size() && j == Q.size());
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
    if (c == 'K') {
        auto t = typeFromMangle(m.substr(1));
        if (t) t->cnst = true;
        return t;
    }
    if (c == 'P' || c == 'R' || c == 'O') {
        auto inner = typeFromMangle(m.substr(1));
        if (!inner) return nullptr;
        auto t = make(c == 'P' ? Type::Kind::Pointer : Type::Kind::Reference);
        t->rref = c == 'O';
        t->inner = std::move(inner);
        return t;
    }
    if (c == 'F') {
        // A function type F <return> <parameters> E; a lone v stands for no parameters.
        std::vector<std::unique_ptr<Type>> parts;
        std::size_t k = 1;
        while (k < m.size() && m[k] != 'E') {
            std::size_t e = mangledEnd(m, k);
            if (e == k) return nullptr;
            auto p = typeFromMangle(m.substr(k, e - k));
            if (!p) return nullptr;
            parts.push_back(std::move(p));
            k = e;
        }
        if (parts.empty()) return nullptr;
        auto t = make(Type::Kind::Function);
        t->inner = std::move(parts[0]);
        if (!(parts.size() == 2 && parts[1]->kind == Type::Kind::Void))
            for (std::size_t n = 1; n < parts.size(); ++n) {
                Param p;
                p.type = std::move(parts[n]);
                t->params.push_back(std::move(p));
            }
        return t;
    }
    if (c == 'A') {
        std::size_t k = 1;
        std::uint64_t n = 0;
        bool sized = false;
        while (k < m.size() && m[k] >= '0' && m[k] <= '9') {
            n = n * 10 + static_cast<std::uint64_t>(m[k++] - '0');
            sized = true;
        }
        if (k >= m.size() || m[k] != '_') return nullptr;
        auto inner = typeFromMangle(m.substr(k + 1));
        if (!inner) return nullptr;
        auto t = make(Type::Kind::Array);
        t->inner = std::move(inner);
        if (sized) {
            t->bound = std::make_unique<Expr>();
            t->bound->kind = Expr::Kind::Int;
            t->bound->ival = n;
        } else {
            t->unsized = true;
        }
        return t;
    }
    if ((c >= '0' && c <= '9') || c == 'N') {
        // A class: <length><name>, with I <arguments> E for an instance; N ... E when qualified.
        // The tag is spelled as the parser spells it: "a::b<arg,arg>".
        std::string tag;
        std::size_t k = c == 'N' ? 1 : 0;
        for (;;) {
            std::size_t n = 0;
            std::size_t d = k;
            while (k < m.size() && m[k] >= '0' && m[k] <= '9') n = n * 10 + static_cast<std::size_t>(m[k++] - '0');
            if (k == d || k + n > m.size()) return nullptr;
            if (!tag.empty()) tag += "::";
            tag += m.substr(k, n);
            k += n;
            if (k < m.size() && m[k] == 'I') {
                ++k;
                tag += "<";
                bool first = true;
                while (k < m.size() && m[k] != 'E') {
                    std::size_t e = mangledEnd(m, k);
                    if (e == k) return nullptr;
                    if (!first) tag += ",";
                    tag += m.substr(k, e - k);
                    first = false;
                    k = e;
                }
                tag += ">";
                ++k;
            }
            if (c != 'N' || k >= m.size() || m[k] == 'E') break;
        }
        auto t = make(Type::Kind::Struct);
        t->tag = tag;
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
    return a.type ? (a.type->cnst ? "K" : "") + mangleType(*a.type) : "i";
}

std::unique_ptr<Type> substType(const Type& pat, const TemplateSig& sig, const std::vector<TArg>& args) {
    if (pat.kind == Type::Kind::Struct && !pat.tag.empty() && pat.tag[0] == '\x02') {
        for (std::size_t i = 0; i < sig.names.size() && i < args.size(); ++i)
            if (sig.names[i] == pat.tag.substr(1) && args[i].type) return cloneType(*args[i].type);
        return cloneType(pat);
    }
    auto n = cloneType(pat);
    if (pat.inner) n->inner = substType(*pat.inner, sig, args);
    if (n->kind == Type::Kind::Reference && n->inner && n->inner->kind == Type::Kind::Reference) {
        // Reference collapsing ([dcl.ref]/6): T& && and T&& & are T&; only T&& && stays T&&.
        bool both = n->rref && n->inner->rref;
        auto in = std::move(n->inner);
        in->rref = both;
        return in;
    }
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
