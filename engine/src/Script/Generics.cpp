// Generic classes and methods of a script (ECMA-334 15.3.2 type parameters, 15.6.1
// generic methods, 8.4 constructed types).
//
// How it works (our own design, by expansion): the parser keeps each generic class or
// method as its tokens. Before checking, every use with real types (Box<int>,
// Find<Health>()) is found in the syntax tree; for each new list of types the tokens are
// copied with the type parameters replaced and parsed again, giving an ordinary class
// called "Box<int>" (or a method called "Find<Health>"). The use is then pointed at it,
// so the checker and interpreter only ever see ordinary classes and methods.
//
// What follows from that: a generic is checked when it's used (a template nobody uses
// isn't checked); `where` constraints are read and not enforced (the filled-in code is
// checked instead); a generic method needs its types written at the call
// (Find<Health>()), because the types of the arguments aren't known yet at this point.
#include "ScriptInternal.hpp"
#include <functional>
#include <set>

namespace okay::script {

namespace {

constexpr int kMaxInstances = 200;   ///< generics made for one script, all together
constexpr int kMaxNameLength = 400;  ///< Box<Box<Box<...>>>: a type that grows without end stops here

class Expander {
public:
    explicit Expander(Program& p) : prog_(p) {}

    void Run() {
        if (prog_.genericClasses.empty() && !AnyGenericMethods()) return;
        // Classes are added while walking, and references into them are held while that
        // happens: room for every class there can be, so none of them moves.
        prog_.classes.reserve(prog_.classes.size() + (std::size_t)kMaxInstances + 1);
        for (DelegateDecl& d : prog_.delegates) {
            if (d.ret.name != "void") Type(d.ret);
            for (TypeRef& t : d.params) Type(t);
        }
        // Walk by index: classes are added while walking.
        for (std::size_t i = 0; i < prog_.classes.size(); ++i) {
            current_ = (int)i;
            WalkClass(i);
        }
    }

private:
    Program& prog_;
    int current_ = -1;   ///< the class being walked (its generic methods are the unqualified ones)
    int made_ = 0;
    bool tooMany_ = false;

    void Error(SrcLoc l, const std::string& m) { prog_.diags.push_back({l.line, m, 0, l.col}); }

    bool AnyGenericMethods() const {
        for (const ClassDecl& c : prog_.classes) if (!c.genericMethods.empty()) return true;
        return false;
    }
    const GenericTemplate* ClassTemplate(const std::string& n) const {
        for (const GenericTemplate& g : prog_.genericClasses) if (g.name == n) return &g;
        return nullptr;
    }
    int ClassIndex(const std::string& n) const {
        for (std::size_t i = 0; i < prog_.classes.size(); ++i) if (prog_.classes[i].name == n) return (int)i;
        return -1;
    }

    // ---- type names and tokens ----
    // How a (rewritten) type reads inside a constructed name: int, int[], Box<int>.
    static std::string ArgName(const TypeRef& t) {
        std::string n = t.name;
        if (!t.args.empty()) {
            n += "<";
            for (std::size_t i = 0; i < t.args.size(); ++i) n += (i ? "," : "") + ArgName(t.args[i]);
            n += ">";
        }
        for (int r = 0; r < t.rank; ++r) n += "[]";
        return n;
    }
    // The tokens that spell type `t` (a constructed class is one name token, "Box<int>").
    static void TypeTokens(const TypeRef& t, SrcLoc at, std::vector<Token>& out) {
        Token k;
        k.loc = at;
        if (t.name == "int") k.type = T::KwInt;
        else if (t.name == "float") k.type = T::KwFloat;
        else if (t.name == "bool") k.type = T::KwBool;
        else if (t.name == "string") k.type = T::KwString;
        else if (t.name == "void") k.type = T::KwVoid;
        else k.type = T::Ident;
        k.text = t.name;
        out.push_back(k);
        if (!t.args.empty()) {
            out.push_back(Token{T::Lt, "<", at});
            for (std::size_t i = 0; i < t.args.size(); ++i) {
                if (i) out.push_back(Token{T::Comma, ",", at});
                TypeTokens(t.args[i], at, out);
            }
            out.push_back(Token{T::Gt, ">", at});
        }
        for (int r = 0; r < t.rank; ++r) {
            out.push_back(Token{T::LBracket, "[", at});
            out.push_back(Token{T::RBracket, "]", at});
        }
    }
    // The template's tokens with its <T, ...> left out and each T replaced by its type.
    static std::vector<Token> Fill(const GenericTemplate& g, const std::vector<TypeRef>& args) {
        std::vector<Token> out;
        out.reserve(g.tokens.size() + 8);
        for (std::size_t i = 0; i < g.tokens.size(); ++i) {
            if (i >= g.headerFrom && i < g.headerTo) continue;
            const Token& t = g.tokens[i];
            bool replaced = false;
            if (t.type == T::Ident && !(i > 0 && g.tokens[i - 1].type == T::Dot))   // x.T is a member, not the type
                for (std::size_t p = 0; p < g.params.size(); ++p)
                    if (t.text == g.params[p]) { TypeTokens(args[p], t.loc, out); replaced = true; break; }
            if (!replaced) out.push_back(t);
        }
        Token end;
        end.type = T::End;
        end.loc = g.tokens.empty() ? SrcLoc{} : g.tokens.back().loc;
        out.push_back(end);
        return out;
    }
    bool Budget(SrcLoc at, const std::string& name) {
        if (tooMany_) return false;
        if (++made_ > kMaxInstances || (int)name.size() > kMaxNameLength) {
            tooMany_ = true;
            Error(at, "Too many generic types are made from '" + name.substr(0, 60) +
                      "' (does a generic type use itself with a bigger type, like Box<Box<T>>?)");
            return false;
        }
        return true;
    }

    // ---- classes ----
    // Box<int> (args already rewritten): the class "Box<int>", made the first time.
    std::string UseClass(const GenericTemplate& g, const std::vector<TypeRef>& args, SrcLoc at) {
        std::string name = g.name + "<";
        for (std::size_t i = 0; i < args.size(); ++i) name += (i ? "," : "") + ArgName(args[i]);
        name += ">";
        if (ClassIndex(name) >= 0 || !Budget(at, name)) return name;
        ClassDecl c;
        ParseClassTokens(Fill(g, args), prog_, c);   // the same message twice (two uses) is dropped by Compile
        for (MethodDecl& m : c.methods) if (m.isCtor) m.name = name;
        c.name = name;
        c.typeParams.clear();
        prog_.classes.push_back(std::move(c));   // walked later by Run's loop
        return name;
    }

    // ---- methods ----
    // A call M<A, B>(...) (args already rewritten) of a generic method of class k: the
    // method "M<A,B>" in that class, made the first time.
    std::string UseMethod(int k, const GenericTemplate& g, const std::vector<TypeRef>& args, SrcLoc at) {
        std::string name = g.name + "<";
        for (std::size_t i = 0; i < args.size(); ++i) name += (i ? "," : "") + ArgName(args[i]);
        name += ">";
        ClassDecl& c = prog_.classes[(std::size_t)k];
        for (const MethodDecl& m : c.methods) if (m.name == name) return name;
        if (!Budget(at, name)) return name;
        const std::size_t before = c.methods.size();
        ParseMemberTokens(Fill(g, args), prog_, c);
        for (std::size_t i = before; i < c.methods.size(); ++i) c.methods[i].name = name;
        // Walk the new method now (it may use more generics); Run's loop has already passed this class, maybe.
        const int saved = current_;
        current_ = k;
        for (std::size_t i = before; i < prog_.classes[(std::size_t)k].methods.size(); ++i) WalkMethod(k, i);
        current_ = saved;
        return name;
    }
    // Which classes have a generic method `n` taking `count` types: the unqualified call's
    // class first (and the classes it extends), else every class with one.
    std::vector<int> MethodOwners(const std::string& n, std::size_t count, bool unqualified) {
        std::vector<int> out;
        auto has = [&](int k) {
            for (const GenericTemplate& g : prog_.classes[(std::size_t)k].genericMethods)
                if (g.name == n && g.params.size() == count) return true;
            return false;
        };
        if (unqualified && current_ >= 0) {
            if (has(current_)) { out.push_back(current_); return out; }
            // a class it extends (by name; links are worked out later by the checker)
            std::set<int> seen;
            for (int k = current_; k >= 0 && seen.insert(k).second;) {
                const ClassDecl& c = prog_.classes[(std::size_t)k];
                int next = -1;
                for (const std::string& b : c.bases) if ((next = ClassIndex(b)) >= 0) break;
                if (next >= 0 && has(next)) { out.push_back(next); return out; }
                k = next;
            }
        }
        for (std::size_t k = 0; k < prog_.classes.size(); ++k) if (has((int)k)) out.push_back((int)k);
        return out;
    }
    bool IsGenericMethodName(const std::string& n) const {
        for (const ClassDecl& c : prog_.classes)
            for (const GenericTemplate& g : c.genericMethods) if (g.name == n) return true;
        return false;
    }

    // ---- the walk ----
    void Type(TypeRef& t) {
        for (TypeRef& a : t.args) Type(a);
        if (t.name.find('<') != std::string::npos) return;   // already a constructed name
        const GenericTemplate* g = ClassTemplate(t.name);
        if (!g) return;
        if (t.args.size() != g->params.size()) {
            Error(t.loc, g->name + " takes " + std::to_string(g->params.size()) + (g->params.size() == 1 ? " type" : " types") +
                         ": " + g->name + "<" + (g->params.size() == 1 ? "int" : "int, string") + ">");
            return;
        }
        t.name = UseClass(*g, t.args, t.loc);
        t.args.clear();
    }
    void WalkClass(std::size_t ci) {
        // fields, properties and method signatures, then bodies (by index: methods are added while walking)
        for (std::size_t i = 0; i < prog_.classes[ci].fields.size(); ++i) {
            FieldDecl& f = prog_.classes[ci].fields[i];
            Type(f.type);
            if (f.init) Expr_(*f.init);
            for (Attribute& a : f.attrs) for (auto& x : a.args) Expr_(*x);
        }
        for (std::size_t i = 0; i < prog_.classes[ci].props.size(); ++i) Type(prog_.classes[ci].props[i].type);
        for (std::size_t i = 0; i < prog_.classes[ci].methods.size(); ++i) WalkMethod((int)ci, i);
    }
    void WalkMethod(int ci, std::size_t mi) {
        // Copy what's needed first: walking can add classes (prog_.classes may move).
        auto method = [&]() -> MethodDecl& { return prog_.classes[(std::size_t)ci].methods[mi]; };
        if (method().ret.name != "void") Type(method().ret);
        for (std::size_t p = 0; p < method().params.size(); ++p) {
            Type(method().params[p].type);
            if (method().params[p].def) Expr_(*method().params[p].def);
        }
        for (std::size_t a = 0; a < method().baseArgs.size(); ++a) Expr_(*method().baseArgs[a]);
        if (method().body) Stmt_(*method().body);
    }
    void Stmt_(Stmt& s) {
        for (auto& c : s.body) if (c) Stmt_(*c);
        if (s.expr) Expr_(*s.expr);
        for (LocalDecl& d : s.locals) { Type(d.type); if (d.init) Expr_(*d.init); }
        if (s.then) Stmt_(*s.then);
        if (s.otherwise) Stmt_(*s.otherwise);
        if (s.init) Stmt_(*s.init);
        for (auto& x : s.step) Expr_(*x);
        if (s.loopBody) Stmt_(*s.loopBody);
        for (SwitchSection& sec : s.sections) {
            for (auto& l : sec.labels) Expr_(*l);
            for (auto& b : sec.body) Stmt_(*b);
        }
        for (CatchClause& c : s.catches) { if (c.typed) Type(c.type); if (c.body) Stmt_(*c.body); }
        if (s.finallyBody) Stmt_(*s.finallyBody);
    }
    void Expr_(Expr& e) {
        if (e.object) Expr_(*e.object);
        for (auto& a : e.args) Expr_(*a);
        if (e.lhs) Expr_(*e.lhs);
        if (e.rhs) Expr_(*e.rhs);
        if (e.third) Expr_(*e.third);
        if (e.lambda) {
            if (e.lambda->value) Expr_(*e.lambda->value);
            if (e.lambda->body) Stmt_(*e.lambda->body);
        }
        if (!e.typeArg.name.empty() && e.kind != EK::Call) Type(e.typeArg);   // new Box<int>(), (T)x, x is T, default(T)
        if (e.kind != EK::Call) return;
        for (TypeRef& t : e.typeArgs) Type(t);
        if (e.typeArgs.empty()) {   // M(...) of a generic method of this class: its types must be written
            if (e.object) return;
            const ClassDecl& c = prog_.classes[(std::size_t)current_];
            for (const MethodDecl& m : c.methods) if (m.name == e.name) return;   // a plain method of that name
            for (const GenericTemplate& g : c.genericMethods)
                if (g.name == e.name) {
                    Error(e.loc, e.name + "() is generic: say its type" + std::string(g.params.size() == 1 ? "" : "s") +
                                 ", like " + e.name + "<" + (g.params.size() == 1 ? "int" : "int, string") + ">(...)");
                    return;
                }
            return;
        }
        e.typeArg = e.typeArgs[0];
        if (e.name == "GetComponent" && !IsGenericMethodName(e.name)) return;
        const bool unqualified = !e.object || e.object->kind == EK::This;
        std::vector<int> owners = MethodOwners(e.name, e.typeArgs.size(), unqualified);
        if (owners.empty()) return;   // not one of the script's generic methods: the checker explains
        std::string name;
        for (int k : owners)
            for (const GenericTemplate& g : prog_.classes[(std::size_t)k].genericMethods)
                if (g.name == e.name && g.params.size() == e.typeArgs.size()) {
                    const GenericTemplate copy = g;   // UseMethod adds methods to the class: keep the template stable
                    name = UseMethod(k, copy, e.typeArgs, e.loc);
                    break;
                }
        e.name = name;
        e.typeArgs.clear();
        e.typeArg = TypeRef{};
    }
};

} // namespace

void InstantiateGenerics(Program& prog) {
    Expander x(prog);
    x.Run();
}

} // namespace okay::script
