#include "parser.hpp"

#include <algorithm>

// The syntax tree and what parser.cpp shares with check.cpp and lower.cpp (docs/engine/modularization_plan.md, batch T1).
#include "ast.hpp"
#include "packs.hpp"

#include "files.hpp"
#include "ir.hpp"
#include "preproc.hpp"
#include "token.hpp"

#include <cstdio>
#include <ctime>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <cctype>
#include <vector>

namespace okaycc {
namespace compiler {
struct Mod {
    enum class Kind { Ptr, Arr, Func, Ref, RRef } kind = Kind::Ptr;
    std::unique_ptr<Expr> bound;
    bool unsized = false;
    bool variadic = false;
    std::vector<Param> params;
    std::unique_ptr<Type> trailing;  // auto f(..) -> T: the return type ([dcl.fct] p2)
    std::string memberOf;            // Ptr: T C::* ([dcl.mptr])
};

struct Parsed {
    std::string name;
    int line = 1;
    int col = 1;
    std::string file;
    std::vector<Mod> mods;
};

class Parser {
public:
    // The tokens are only read: a caller that keeps them (frontEnd, once per round) lends them,
    // which saves copying every token of the file each round.
    Parser(const std::vector<Token>& toks, Diagnostics& diags) : toks_(toks), diags_(diags) {}
    Parser(std::vector<Token>&& toks, Diagnostics& diags) : own_(std::move(toks)), toks_(own_), diags_(diags) {}
    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;

    Tu parseTu() {
        Tu tu;
        if (tscopes_.empty()) tscopes_.emplace_back();
        // char16_t, char32_t and wchar_t ([basic.fundamental] p8-p9; ISO C 7.28 and 7.19 give
        // the same names as typedefs): kept as the unsigned 16-bit and 32-bit integers and the
        // signed 32-bit int of System V x86-64. They are not distinct types for overloading or
        // mangling here.
        auto wideChar = [&](const char* name, Type::Kind k, bool uns) {
            auto w = ty(k);
            w->unsign = uns;
            tscopes_.front()[name] = std::move(w);
        };
        wideChar("char16_t", Type::Kind::Short, true);
        wideChar("char32_t", Type::Kind::Int, true);
        wideChar("wchar_t", Type::Kind::Int, false);
        eh_ = forceExceptions();
        for (const Token& t : toks_)
            if (t.kind == Tok::Keyword && (t.text == "try" || t.text == "throw")) eh_ = true;
        while (!atEnd()) {
            std::size_t before = i_;
            if (isKw("using")) parseUsing();
            else if (isKw("namespace")) parseNamespace(tu);
            else if (isKw("template")) parseTemplate();
            else if (parseStaticAssert()) {
            }
            else if (parseLinkage(tu)) {
            } else if (isUnsupported()) {
                err("'" + cur().text + "' is not in this subset");
                syncSemi();
            } else if (!isTypeKw(cur())) {
                err("expected a declaration");
                syncSemi();
            } else {
                parseExternal(tu);
            }
            if (stop_) break;
            if (i_ == before) advance();
        }
        flushExtras(tu);
        processInstances(tu);
        flushExtras(tu);
        emitGlobalObjects(tu);
        if (eh_) emitEhBuiltins(tu);
        tu.lambdas = std::move(lambdas_);
        tu.usingNs = usingNs_;
        tu.usingDecl = usingDecl_;
        lambdas_.clear();
        ehActive() = eh_;
        return tu;
    }

    // One function template instance the checker asks for while it checks the file (frontEnd,
    // instantiateNow): made now, at the end of the file as processInstances makes them ([temp.point]
    // p8: the end of the translation unit is a point of instantiation), with what it needs (class
    // instances, their members, lambdas) put at the end of tu. False when the instance cannot be
    // made the way a new round would make it: a message, no declaration of that name (a
    // substitution failure, [temp.deduct] p8), or a new object for __okay_init_/__okay_fini_ (made
    // once, at the end of parseTu). The checker then asks for a new round, as before.
    bool instantiate(Tu& tu, const InstReq& r) {
        if (stop_ || !lambdas_.empty()) return false;
        const std::size_t diagMark = diags_.items().size();
        const std::size_t mark = tu.decls.size();
        lamBase_ = tu.lambdas.size();
        InstReq c;
        c.tmpl = r.tmpl;
        c.args = cloneArgs(r.args);
        c.name = r.name;
        work_.push_back(std::move(c));
        runWork(tu);
        flushExtras(tu);
        for (auto& li : lambdas_) tu.lambdas.push_back(std::move(li));
        lambdas_.clear();
        lamBase_ = tu.lambdas.size();
        if (stop_ || diags_.items().size() != diagMark || !globalInit_.empty() || !staticObjs_.empty() ||
            !globalObjs_.empty())
            return false;
        for (std::size_t k = mark; k < tu.decls.size(); ++k)
            if (tu.decls[k] && tu.decls[k]->function && tu.decls[k]->name == r.name) return true;
        return false;
    }

    // Prototypes of the runtime functions the generated code calls (lib/eh.c).
    void emitEhBuiltins(Tu& tu) {
        struct B {
            const char* name;
            int ret;  // 0 void, 1 int, 2 long, 3 void *
            std::vector<int> params;  // 2 long, 4 long *, 5 char *
        };
        const B list[] = {
            {"__okay_eh_push", 0, {4}}, {"__okay_eh_pop", 0, {}},      {"__okay_eh_begin", 0, {4}},
            {"__okay_eh_end", 0, {}},   {"__okay_eh_match", 1, {5}},   {"__okay_eh_obj", 3, {}},
            {"__okay_eh_resume", 0, {}}, {"__okay_eh_rethrow", 0, {}}, {"__okay_throw", 0, {2, 5, 2}},
            {"__okay_setjmp", 2, {4}},  {"__okay_alloc", 3, {2}},
        };
        Pos p{1, 1, toks_.empty() ? std::string() : toks_[0].file};
        auto make = [&](int code) -> std::unique_ptr<Type> {
            switch (code) {
            case 0: return ty(Type::Kind::Void);
            case 1: return ty(Type::Kind::Int);
            case 2: return ty(Type::Kind::Long);
            case 3: return voidPtr();
            case 4: return longPtr();
            default: {
                auto cp = ty(Type::Kind::Pointer);
                cp->inner = ty(Type::Kind::Char);
                return cp;
            }
            }
        };
        for (const B& b : list) {
            std::vector<Param> params;
            for (int code : b.params) {
                Param prm;
                prm.type = make(code);
                params.push_back(std::move(prm));
            }
            auto fn = fnTypeOf(params);
            fn->inner = make(b.ret);
            auto d = std::make_unique<Decl>();
            d->name = b.name;
            d->line = 1;
            d->col = 1;
            d->file = p.file;
            d->type = std::move(fn);
            d->function = true;
            tu.decls.push_back(std::move(d));
        }
    }

    // __okay_init_<file>: the constructors of the global and static objects, in order, and
    // __okay_fini_<file>: their destructors in reverse order. The program start (our linker, or
    // compileToElf) calls them around main. Written from ISO/IEC 14882 [basic.start.init] and
    // [basic.start.term].
    void emitGlobalObjects(Tu& tu) {
        for (auto& st : staticObjs_) globalObjs_.push_back(std::move(st));
        staticObjs_.clear();
        if (globalInit_.empty() && globalObjs_.empty()) return;
        std::string sfx;
        for (char ch : unitFile()) sfx += std::isalnum(static_cast<unsigned char>(ch)) ? ch : '_';
        Pos p{1, 1, toks_.empty() ? std::string() : toks_[0].file};
        auto mk = [&](const std::string& name, std::unique_ptr<Stmt> body) {
            auto d = std::make_unique<Decl>();
            d->name = name;
            d->line = 1;
            d->col = 1;
            d->file = p.file;
            d->type = fnTypeOf({});
            d->function = true;
            d->body = std::move(body);
            tu.decls.push_back(std::move(d));
        };
        if (!globalInit_.empty()) {
            auto body = sBlock(p);
            for (auto& st : globalInit_) body->stmts.push_back(std::move(st));
            globalInit_.clear();
            mk("__okay_init_" + sfx, std::move(body));
        }
        if (!globalObjs_.empty()) {
            auto body = sBlock(p);
            for (std::size_t i = globalObjs_.size(); i-- > 0;) {
                auto call = dtorCall(globalObjs_[i].tag, eAddr(eName(globalObjs_[i].name, p), p), p);
                if (!call) continue;
                std::unique_ptr<Stmt> st = sExpr(std::move(call), p);
                if (!globalObjs_[i].guard.empty()) {
                    // Built objects only, and once: the flag is cleared, so an inline variable
                    // that every file's __okay_fini_ sees is destroyed by the first.
                    auto g = std::make_unique<Stmt>();
                    g->kind = Stmt::Kind::If;
                    g->line = p.line;
                    g->col = p.col;
                    g->file = p.file;
                    g->expr = eName(globalObjs_[i].guard, p);
                    auto thenB = sBlock(p);
                    thenB->stmts.push_back(sExpr(eAssign(eName(globalObjs_[i].guard, p), eInt(0, p), p), p));
                    thenB->stmts.push_back(std::move(st));
                    g->thenS = std::move(thenB);
                    st = std::move(g);
                }
                body->stmts.push_back(std::move(st));
            }
            globalObjs_.clear();
            mk("__okay_fini_" + sfx, std::move(body));
        }
    }

private:
    std::vector<Token> own_;
    const std::vector<Token>& toks_;
    Diagnostics& diags_;
    std::size_t i_ = 0;
    bool reported_ = false;
    bool stop_ = false;
    int depth_ = 0;
    std::map<std::string, std::unique_ptr<Type>> structs_;
    std::map<std::string, std::int64_t> enums_;
    bool enumDef_ = false;
    std::vector<std::map<std::string, std::unique_ptr<Type>>> tscopes_;
    std::vector<std::string> ns_;
    int anonNs_ = 0;
    int bodyDepth_ = 0;  // inside a function body
    bool cLink_ = false;  // inside extern "C"  // depth of unnamed namespaces around the current declaration
    std::set<std::string> implicitTags_;
    std::vector<std::unique_ptr<Decl>> extras_;

    // A type named in the source: a finished class by its name and facts only (its members stay
    // in structs_ and canonStructs, read through classOf and canonOf); anything else copied. Copying
    // every member of a class at each use of its name was most of the compile time.
    std::unique_ptr<Type> typeRef(const Type& t) const {
        if ((t.kind == Type::Kind::Struct || t.kind == Type::Kind::Union) && !t.tag.empty() && !t.params.empty()) {
            auto it = structs_.find(t.tag);
            auto ct = canonStructs().find(t.tag);
            if (it != structs_.end() && it->second && !it->second->params.empty() && ct != canonStructs().end() &&
                ct->second && !ct->second->params.empty()) {
                auto r = classByName(t);
                r->vslots = it->second->vslots;
                return r;
            }
        }
        return cloneType(t);
    }
    const Type* findTypedef(const std::string& name) const {
        for (int i = static_cast<int>(tscopes_.size()) - 1; i >= 0; --i) {
            auto it = tscopes_[static_cast<std::size_t>(i)].find(name);
            if (it != tscopes_[static_cast<std::size_t>(i)].end()) return it->second.get();
        }
        return nullptr;
    }
    void addTypedef(const std::string& name, std::unique_ptr<Type> type) {
        if (name.empty()) {
            err("typedef needs a name");
            return;
        }
        if (tscopes_.empty()) tscopes_.emplace_back();
        tscopes_.back()[name] = std::move(type);
    }

    const Token& cur() const { return toks_[i_]; }
    bool atEnd() const { return stop_ || i_ >= toks_.size() || toks_[i_].kind == Tok::End; }
    void advance() {
        if (i_ < toks_.size()) ++i_;
    }
    // A C source or header (.c, .h): C's rules for tags and unnamed structs, not C++'s.
    static bool cSourceFile(const std::string& f) {
        return f.size() >= 2 && (f.compare(f.size() - 2, 2, ".c") == 0 || f.compare(f.size() - 2, 2, ".h") == 0);
    }
    bool isP(const Token& t, const char* s) const {
        if (t.kind != Tok::Punct) return false;
        if (t.text == s) return true;
        // the digraphs ([lex.digraph]); no string is made here, this test runs for every token
        if (t.text.size() != 2 || !s[0] || s[1]) return false;
        switch (s[0]) {
        case '[': return t.text == "<:";
        case ']': return t.text == ":>";
        case '{': return t.text == "<%";
        case '}': return t.text == "%>";
        default: return false;
        }
    }
    bool isP(const char* s) const { return !atEnd() && isP(cur(), s); }
    bool nextIs(const char* s) const {
        if (i_ + 1 >= toks_.size()) return false;
        return isP(toks_[i_ + 1], s);
    }
    bool isKwTok(const Token& t, const char* s) const {
        return (t.kind == Tok::Keyword || t.kind == Tok::Ident) && t.text == s;
    }
    bool isKw(const char* s) const { return !atEnd() && isKwTok(cur(), s); }
    // Whether the '<' at token lt is closed by a '>' before a ';', '{', '}' or a bracket it is
    // inside of closes (a template argument list), looking only at the tokens.
    bool angleCloses(std::size_t lt) const {
        int depth = 0, paren = 0;
        for (std::size_t k = lt; k < toks_.size(); ++k) {
            const Token& x = toks_[k];
            if (x.kind != Tok::Punct) continue;
            if (x.text == "<") ++depth;
            else if (x.text == ">") { if (paren == 0 && --depth == 0) return true; }
            else if (x.text == ">>") { if (paren == 0 && (depth -= 2) <= 0) return true; }
            else if (x.text == "(" || x.text == "[") ++paren;
            else if (x.text == ")" || x.text == "]") { if (--paren < 0) return false; }
            else if (x.text == ";" || x.text == "{" || x.text == "}") return false;
        }
        return false;
    }
    bool isTypeKw(const Token& t) const {
        // ::T, a name looked up from the global namespace ([basic.lookup.qual] p4). okay-cc reads
        // ::T as T looked up from the current scope outward, which finds the global T unless an
        // enclosing namespace declares its own T first.
        if (isP(t, "::") && &t >= toks_.data() && &t + 1 < toks_.data() + toks_.size() && (&t + 1)->kind == Tok::Ident)
            return isTypeKw(*(&t + 1));
        if (isKwTok(t, "void") || isKwTok(t, "char") || isKwTok(t, "short") || isKwTok(t, "int") ||
            isKwTok(t, "long") || isKwTok(t, "struct") || isKwTok(t, "class") || isKwTok(t, "union") || isKwTok(t, "enum") ||
            isKwTok(t, "extern") || isKwTok(t, "float") || isKwTok(t, "double") ||
            isKwTok(t, "const") || isKwTok(t, "volatile") || isKwTok(t, "static") ||
            isKwTok(t, "unsigned") || isKwTok(t, "signed") || isKwTok(t, "bool") ||
            isKwTok(t, "typedef") || isKwTok(t, "constexpr") || isKwTok(t, "inline") || isKwTok(t, "auto") ||
            isKwTok(t, "typename") || isKwTok(t, "decltype"))
            return true;
        if (t.kind == Tok::Ident && ctmpl_.count(t.text) && isLocalName(t.text)) return false;  // auto set = [&]..
        if (t.kind == Tok::Ident && ctmpl_.count(t.text)) {
            // A class template's name starts a type when its arguments follow. Followed by '=',
            // ';', '.' and the like it is a variable or member of that name (a member 'duration'
            // beside std::chrono::duration, [basic.scope.class]).
            const bool inStream = &t >= toks_.data() && &t + 1 < toks_.data() + toks_.size();
            if (!inStream) return true;
            const Token& n = *(&t + 1);
            static const char* const kNotAType[] = {"=", ";", ".", "->", "[", "]", "+=", "-=", "*=", "/=", "++",
                                                     "--", ")", ",", ">", ">=", "<=", "==", "!=", "?", ":", "&&",
                                                     "||", "+", "-", "/", "%", "|", "^", "}"};
            bool name = false;
            for (const char* p : kNotAType)
                if (isP(n, p)) name = true;
            // duration < kMax ? a : b with a member 'duration': a '<' whose list never closes
            // before the enclosing bracket or the statement ends is a comparison.
            if (!name && isP(n, "<") && !angleCloses(static_cast<std::size_t>(&n - toks_.data()))) name = true;
            if (!name) return true;
        }
        if (t.kind == Tok::Ident && aliasTmpl_.count(t.text) && &t >= toks_.data() &&
            &t + 1 < toks_.data() + toks_.size() && isP(*(&t + 1), "<"))
            return true;
        if (t.kind == Tok::Ident && &t >= toks_.data() && &t < toks_.data() + toks_.size()) {
            std::size_t ti = tmplAfterNs(static_cast<std::size_t>(&t - toks_.data()), true);
            if (ti && ti + 1 < toks_.size() && isP(toks_[ti + 1], "<")) return true;
        }
        if (t.kind == Tok::Ident && &t >= toks_.data() && &t < toks_.data() + toks_.size()) {
            std::size_t qend = 0;
            if (qualTypeAt(static_cast<std::size_t>(&t - toks_.data()), qend)) return true;
        }
        if (t.kind != Tok::Ident || findTypedef(t.text) == nullptr) return false;
        if (isLocalName(t.text) && &t >= toks_.data() && &t + 1 < toks_.data() + toks_.size() &&
            (&t + 1)->kind != Tok::Ident && !isP(*(&t + 1), "::"))
            return false;  // a parameter or local hides a type of its name ([basic.scope.block]): float seconds
        if (!implicitTags_.count(t.text)) {
            // E::e with E an enum (or another typedef that is not a class) is an expression.
            const Token* tb = toks_.data();
            if (&t >= tb && &t + 1 < tb + toks_.size() && isP(*(&t + 1), "::")) return false;
            return true;
        }
        // A struct tag used as a plain type name (C++). Where C would see a variable of the same
        // name, only treat it as a type when what follows can continue a declaration or a type.
        const Token* base = toks_.data();
        if (&t < base || &t >= base + toks_.size()) return true;
        std::size_t idx = static_cast<std::size_t>(&t - base);
        if (idx + 1 >= toks_.size()) return true;
        const Token& n = toks_[idx + 1];
        if (n.kind == Tok::Ident) return true;
        if (n.kind == Tok::Punct && n.text == "::") {
            // C::C( and C::~C( start a definition; C::member is an expression.
            if (idx + 2 >= toks_.size()) return true;
            const Token& n2 = toks_[idx + 2];
            return isP(n2, "~") || (n2.kind == Tok::Ident && n2.text == t.text);
        }
        // (and in C++, before ',': an unnamed parameter of a class type, f(Handler h, Scoped, int p))
        return n.kind == Tok::Punct && (n.text == "*" || n.text == "&" || n.text == "&&" || n.text == ")" || n.text == ">" ||
                                        (n.text == "," && !cSourceFile(t.file)));
    }
    // A qualified type name at token idx: N::C or N::M::C naming a class, or N::T naming a typedef
    // declared while namespace N was open (typedef names are kept unqualified). end is set to the
    // token after the name.
    const Type* qualTypeAt(std::size_t idx, std::size_t& end) const {
        if (idx + 2 >= toks_.size() || toks_[idx].kind != Tok::Ident || !isP(toks_[idx + 1], "::")) return nullptr;
        std::string name = toks_[idx].text;
        std::size_t j = idx + 1;
        while (j + 1 < toks_.size() && isP(toks_[j], "::") && toks_[j + 1].kind == Tok::Ident) {
            name += "::";
            name += toks_[j + 1].text;
            j += 2;
        }
        end = j;
        for (std::size_t keep = ns_.size() + 1; keep-- > 0;) {
            std::string q;
            for (std::size_t k = 0; k < keep; ++k) q += ns_[k] + "::";
            q += name;
            auto it = structs_.find(q);
            if (it != structs_.end() && it->second) return it->second.get();
            auto mt = memberTypes_.find(q);
            if (mt != memberTypes_.end() && mt->second) return mt->second.get();
            std::size_t cut = q.rfind("::");
            if (cut != std::string::npos && knownNs_.count(q.substr(0, cut))) {
                if (const Type* td = findTypedef(q.substr(cut + 2))) return td;
            }
        }
        if (const Type* td = findTypedef(toks_[idx].text)) {
            // C::T where C names a class through a typedef or a using-declaration (using okay::Log;
            // then Log::Level): T is looked up in that class ([class.qual]).
            if (td->kind == Type::Kind::Struct && !td->tag.empty() && td->tag[0] != '\x02') {
                std::string q = td->tag + name.substr(toks_[idx].text.size());
                auto it = structs_.find(q);
                if (it != structs_.end() && it->second) return it->second.get();
                auto mt = memberTypes_.find(q);
                if (mt != memberTypes_.end() && mt->second) return mt->second.get();
            }
        }
        for (const std::string& u : usingNs_) {
            // using namespace std::chrono; then steady_clock::time_point
            auto it = structs_.find(u + "::" + name);
            if (it != structs_.end() && it->second) return it->second.get();
            auto mt = memberTypes_.find(u + "::" + name);
            if (mt != memberTypes_.end() && mt->second) return mt->second.get();
        }
        return nullptr;
    }
    std::set<std::string> knownNs_{"std"};
    std::string tmplQual_;            // the N of N::f just read, for the function template f
    // N as written, completed from the enclosing namespaces when they declare it (std stays std).
    std::string qualifyNs(const std::string& n) const {
        for (std::size_t keep = ns_.size(); keep > 0; --keep) {
            std::string pre;
            for (std::size_t k = 0; k < keep; ++k) pre += ns_[k] + "::";
            if (knownNs_.count(pre + n)) return pre + n;
        }
        return n;
    }
    // Member typedefs and aliases by qualified name ("ns::Class::name"), for Class::name outside the class.
    std::map<std::string, std::unique_ptr<Type>> memberTypes_;
    int classScopes_ = 0;         // class bodies being parsed (each has a typedef scope)
    std::size_t classOuter_ = 0;  // the typedef scope around the outermost of them
    // N::M::name where N::M is a namespace and name is a class template (classes) or a function
    // template: the index of the token 'name', or 0. Templates are kept by their bare name.
    std::size_t tmplAfterNs(std::size_t idx, bool classes) const {
        if (idx + 2 >= toks_.size() || toks_[idx].kind != Tok::Ident || !isP(toks_[idx + 1], "::")) return 0;
        std::string prefix = toks_[idx].text;
        std::size_t j = idx;
        while (j + 2 < toks_.size() && isP(toks_[j + 1], "::") && toks_[j + 2].kind == Tok::Ident) {
            const std::string& nm = toks_[j + 2].text;
            bool last = !(j + 3 < toks_.size() && isP(toks_[j + 3], "::"));
            const bool isT = classes ? (ctmpl_.count(nm) || aliasTmpl_.count(nm)) : (ftmpl_.count(nm) || varTmpl_.count(nm));
            if (last || isT) {
                if (!isT) return 0;
                bool known = knownNs_.count(prefix) > 0;
                for (std::size_t keep = ns_.size(); keep > 0 && !known; --keep) {
                    std::string q;
                    for (std::size_t k = 0; k < keep; ++k) q += ns_[k] + "::";
                    known = knownNs_.count(q + prefix) > 0;
                }
                return known ? j + 2 : 0;
            }
            prefix += "::" + nm;
            j += 2;
        }
        return 0;
    }

    bool isUnsupported() const {
        if (atEnd() || cur().kind != Tok::Keyword) return false;
        return !isTypeKw(cur()) && !isKw("if") && !isKw("else") && !isKw("while") && !isKw("for") && !isKw("do") && !isKw("try") && !isKw("throw") &&
               !isKw("return") && !isKw("break") && !isKw("continue") && !isKw("switch") && !isKw("goto") &&
               !isKw("new") && !isKw("delete") && !isKw("this");  // this->x = v; starts an expression
    }
    // noexcept or noexcept(expr) after a parameter list ([except.spec]): not enforced here.
    void skipNoexcept() {
        while (isKw("noexcept")) {
            advance();
            if (isP("(")) {
                int depth = 0;
                do {
                    if (isP("(")) ++depth;
                    else if (isP(")")) --depth;
                    advance();
                } while (!atEnd() && depth > 0);
            }
        }
    }
    bool eatP(const char* s) {
        if (!isP(s)) return false;
        advance();
        return true;
    }
    bool eatKw(const char* s) {
        if (!isKw(s)) return false;
        advance();
        return true;
    }
    void err(const std::string& msg) {
        if (stop_) return;
        if (diags_.items().size() >= 32) {
            stop_ = true;
            const Token& t = i_ < toks_.size() ? toks_[i_] : toks_.back();
            diags_.error(t.file.empty() ? "t.c" : t.file, t.line, t.column, "too many errors");
            return;
        }
        const Token& t = i_ < toks_.size() ? toks_[i_] : toks_.back();
        diags_.error(t.file.empty() ? "t.c" : t.file, t.line, t.column, msg);
        reported_ = true;
    }
    struct Guard {
        Parser& self;
        bool live;
        explicit Guard(Parser& host) : self(host), live(false) {
            if (self.stop_) return;
            if (self.depth_ >= 64) {
                self.err("nesting is too deep");
                self.stop_ = true;
                return;
            }
            ++self.depth_;
            live = true;
        }
        ~Guard() {
            if (live && self.depth_ > 0) --self.depth_;
        }
        explicit operator bool() const { return live; }
    };
    void expectP(const char* s, const char* what) {
        if (!eatP(s)) err(std::string("expected '") + what + "'");
    }
    void syncSemi() {
        while (!atEnd() && !isP(";") && !isP("}") && !isP("{")) advance();
        if (isP(";")) advance();
    }

    std::unique_ptr<Expr> ex(Expr::Kind k, const Token& at) {
        auto e = std::make_unique<Expr>();
        e->kind = k;
        e->line = at.line;
        e->col = at.column;
        e->file = at.file;
        return e;
    }
    std::unique_ptr<Expr> leafName(const Token& t) {
        auto e = ex(Expr::Kind::Name, t);
        e->text = t.text;
        return e;
    }
    std::unique_ptr<Expr> leafInt(const Token& t) {
        auto e = ex(Expr::Kind::Int, t);
        e->ival = t.uval;
        e->litUnsigned = t.isUnsigned;
        e->litRank = t.longRank;
        return e;
    }
    std::unique_ptr<Expr> zeroAt() {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Int;
        return e;
    }

    std::unique_ptr<Type> ty(Type::Kind k) {
        auto t = std::make_unique<Type>();
        t->kind = k;
        return t;
    }

    // using X = T;  using namespace N;  using N::name;  ([dcl.typedef], [namespace.udir],
    // [namespace.udecl]). An alias is a typedef. A using-directive is kept for the rest of the file
    // (not only to the end of the block: a simplification) and searched after the scopes that
    // contain a name; the checker searches it too. A using-declaration of a type is a typedef; of
    // anything else, the checker maps the short name to the qualified one.
    std::vector<std::string> usingNs_;
    std::map<std::string, std::string> usingDecl_;
    void parseUsing() {
        advance();  // using
        if (eatKw("namespace")) {
            std::string name;
            while (!atEnd() && cur().kind == Tok::Ident) {
                name += cur().text;
                advance();
                if (!eatP("::")) break;
                name += "::";
            }
            expectP(";", ";");
            std::string full = name;
            for (std::size_t keep = ns_.size() + 1; keep-- > 0;) {
                std::string q;
                for (std::size_t k = 0; k < keep; ++k) q += ns_[k] + "::";
                if (knownNs_.count(q + name)) {
                    full = q + name;
                    break;
                }
            }
            if (!knownNs_.count(full)) err("unknown namespace '" + name + "'");
            else if (std::find(usingNs_.begin(), usingNs_.end(), full) == usingNs_.end()) usingNs_.push_back(full);
            return;
        }
        if (!atEnd() && cur().kind == Tok::Ident && nextIs("=")) {
            std::string name = cur().text;
            advance();
            advance();
            noteEnumAlias(name);
            auto t = parseTypeName();
            expectP(";", ";");
            if (bodyDepth_ == 0 && !classStack_.empty() && classStack_.back() && !classStack_.back()->tag.empty() && t)
                memberTypes_[classStack_.back()->tag + "::" + name] = cloneType(*t);  // a member alias
            addTypedef(name, std::move(t));
            return;
        }
        eatKw("typename");
        std::size_t qend = 0;
        if (!atEnd() && cur().kind == Tok::Ident && nextIs("::")) {
            if (const Type* qt = qualTypeAt(i_, qend)) {
                std::string last = toks_[qend - 1].text;
                auto t = cloneType(*qt);
                i_ = qend;
                expectP(";", ";");
                addTypedef(last, std::move(t));
                return;
            }
        }
        std::string chain;
        std::string last;
        while (!atEnd() && cur().kind == Tok::Ident) {
            last = cur().text;
            chain += last;
            advance();
            if (!eatP("::")) break;
            chain += "::";
        }
        if (last == "operator") {
            // using Base::operator[]; in a class ([namespace.udecl]): the base's operators stay
            // found through the base, so the declaration adds nothing here.
            while (!atEnd() && !isP(";")) advance();
            expectP(";", ";");
            return;
        }
        expectP(";", ";");
        if (!last.empty() && chain != last) {
            usingDecl_[last] = chain;
            if (!ns_.empty()) usingDecl_[qualify(last)] = chain;  // util::twice -> geo::twice
        }
    }

    // using C = E; with E an enumeration (an int here, so the type keeps no name): C::e names E's
    // enumerators ([namespace.udecl], [dcl.typedef], [expr.prim.id.qual]). At the aliased type.
    std::map<std::string, std::string> enumAlias_;  // alias -> the enumeration's qualified name
    std::string deferredClass_;  // the class whose member function bodies readDeferred is reading
    void noteEnumAlias(const std::string& name) {
        std::string chain;
        std::size_t j = i_;
        if (j < toks_.size() && isKwTok(toks_[j], "typename")) ++j;
        while (j < toks_.size() && toks_[j].kind == Tok::Ident) {
            chain += toks_[j].text;
            if (j + 2 < toks_.size() && isP(toks_[j + 1], "::") && toks_[j + 2].kind == Tok::Ident) {
                chain += "::";
                j += 2;
            } else {
                ++j;
                break;
            }
        }
        if (chain.empty() || j >= toks_.size() || !isP(toks_[j], ";")) return;
        auto it = enumAlias_.find(chain);
        if (it != enumAlias_.end()) chain = it->second;
        std::vector<std::string> prefixes;  // the classes around, the namespaces around, the ones used
        // (using I = Transition::Interrupt; in a member function: the class's own member types first)
        std::string c = !bodyClass_.empty() ? bodyClass_ : deferredClass_;
        if (c.empty() && !classStack_.empty() && classStack_.back()) c = classStack_.back()->tag;
        while (!c.empty()) {
            prefixes.push_back(c + "::");
            std::size_t cut = c.rfind("::");
            c = cut == std::string::npos ? std::string() : c.substr(0, cut);
        }
        for (std::size_t keep = ns_.size() + 1; keep-- > 0;) {
            std::string q;
            for (std::size_t k = 0; k < keep; ++k) q += ns_[k] + "::";
            prefixes.push_back(q);
        }
        for (const std::string& u : usingNs_) prefixes.push_back(u + "::");
        for (const std::string& q : prefixes) {
            auto hit = enums_.lower_bound(q + chain + "::");
            if (hit != enums_.end() && hit->first.compare(0, q.size() + chain.size() + 2, q + chain + "::") == 0) {
                enumAlias_[name] = q + chain;
                // a member alias (struct B { using K = ParamDef::Kind; }): also B::K::e ([class.qual])
                if (bodyDepth_ == 0 && !classStack_.empty() && classStack_.back() && !classStack_.back()->tag.empty())
                    enumAlias_[classStack_.back()->tag + "::" + name] = q + chain;
                return;
            }
        }
    }

    // [captures](params) mutable -> ret { body }. See LambdaInfo in ast.hpp.
    std::deque<LambdaInfo> lambdas_;
    std::size_t lamBase_ = 0;  // the index of lambdas_[0] in the file's list (Tu::lambdas)
    // The parameters of each lambda's function (with __self), by closure class: kept after the
    // checker takes the lambdas (is_invocable in an instance made on demand).
    std::map<std::string, std::size_t> lamArity_;
    std::unique_ptr<Expr> parseLambda() {
        Token at = cur();
        Pos pos = here();
        advance();  // [
        LambdaInfo li;
        while (!atEnd() && !isP("]")) {
            std::size_t before = i_;
            if (eatP("&")) {
                if (!atEnd() && cur().kind == Tok::Ident) {
                    li.caps.emplace_back(cur().text, true);
                    advance();
                } else {
                    li.mode = 1;
                }
            } else if (eatP("=")) {
                li.mode = 2;
            } else if (isKw("this")) {
                li.caps.emplace_back("this", false);  // [this]: the pointer, by copy
                advance();
            } else if (!atEnd() && cur().kind == Tok::Ident) {
                li.caps.emplace_back(cur().text, false);
                advance();
            }
            if (!eatP(",") && !isP("]")) {
                err("expected ']'");
                break;
            }
            if (i_ == before) advance();
        }
        expectP("]", "]");
        std::vector<Param> params;
        if (eatP("(")) parseParams(params);
        while (isKw("mutable") || isKw("noexcept") || isKw("constexpr")) advance();
        std::unique_ptr<Type> ret = eatP("->") ? parseTypeName() : autoType();
        std::string sfx;
        for (char ch : unitFile()) sfx += std::isalnum(static_cast<unsigned char>(ch)) ? ch : '_';
        // Reserve this lambda's slot before its body is read: a lambda inside the body takes the
        // next one (the index names the closure class and the function).
        const std::size_t slot = lambdas_.size();
        const std::size_t idx = lamBase_ + slot;  // (the file's lambdas before an instance made on demand)
        lambdas_.emplace_back();
        li.tag = "__closure_" + sfx + "_" + std::to_string(idx);
        auto fn = std::make_unique<Decl>();
        fn->name = "__lambda_" + sfx + "_" + std::to_string(idx);
        fn->function = true;
        fn->isStatic = true;
        fn->line = pos.line;
        fn->col = pos.col;
        fn->file = pos.file;
        auto ft = ty(Type::Kind::Function);
        ft->inner = std::move(ret);
        Param self;
        self.name = "__self";
        auto cls = ty(Type::Kind::Struct);
        cls->tag = li.tag;
        self.type = ty(Type::Kind::Pointer);
        self.type->inner = std::move(cls);
        ft->params.push_back(std::move(self));
        for (auto& q : params) ft->params.push_back(std::move(q));
        fn->type = std::move(ft);
        if (!isP("{")) {
            err("expected a lambda body");
            lambdas_[slot] = std::move(li);
            return zeroAt();
        }
        fn->body = parseBody(fn->type->inner.get());
        lamArity_[li.tag] = fn->type->params.size();
        li.fn = std::move(fn);
        lambdas_[slot] = std::move(li);
        auto e = ex(Expr::Kind::Name, at);
        e->text = "\x01" "lambda:" + std::to_string(idx);
        return e;
    }

    // auto ([dcl.spec.auto]): a placeholder the checker replaces with the type of the initializer.
    std::unique_ptr<Type> autoType() {
        auto t = ty(Type::Kind::Struct);
        t->tag = "\x03" "auto";
        return t;
    }

    // The base type with a leading const recorded on it (const B, const char): overload ranking
    // and template deduction need it ([over.ics.rank], [temp.deduct.call]).
    std::unique_ptr<Type> parseBase() {
        // typename X<T>::type ([temp.res] p3): the word only says a type follows.
        if (isKw("typename") && i_ + 1 < toks_.size() && (toks_[i_ + 1].kind == Tok::Ident || isP(toks_[i_ + 1], "::")))
            advance();
        if (isP("::") && i_ + 1 < toks_.size() && toks_[i_ + 1].kind == Tok::Ident) advance();  // ::T (see isTypeKw)
        std::size_t start = i_;
        auto t = parseBase0();
        if (t) {
            int depth = 0;
            for (std::size_t k = start; k < i_ && k < toks_.size(); ++k) {
                if (isP(toks_[k], "<")) ++depth;
                else if (isP(toks_[k], ">")) --depth;
                else if (isP(toks_[k], ">>")) depth -= 2;
                else if (depth == 0 && (isKwTok(toks_[k], "const") || isKwTok(toks_[k], "constexpr"))) t->cnst = true;
            }
        }
        return t;
    }

    std::unique_ptr<Type> parseBase0() {
        if (eatKw("auto")) return autoType();
        if (isKw("decltype") && nextIs("(")) {
            // decltype(auto) ([dcl.type.auto.deduct] p5): deduced from the initializer or the
            // returns; a return of an lvalue that is not a plain name keeps the reference (the
            // checker, marked by 'variadic' on the auto type). decltype(e) ([dcl.type.decltype]):
            // the type of e, worked out by the checker where the declaration is checked.
            advance();
            advance();
            if (isKw("auto") && nextIs(")")) {
                advance();
                advance();
                auto a = autoType();
                a->variadic = true;  // decltype(auto): a reference result is kept (the checker)
                return a;
            }
            auto t = ty(Type::Kind::Struct);
            t->tag = "\x03" "decltype";
            t->bound = parseExpr();
            expectP(")", ")");
            if (collectDecltype_ && t->bound) collectDecltype_->push_back(cloneExpr(*t->bound));
            return t;
        }
        if (isKw("struct") || isKw("union") || isKw("class")) return parseStructType();
        if (isKw("enum")) return parseEnum();
        if (eatKw("bool")) {
            auto t = ty(Type::Kind::Bool);
            t->unsign = true;
            return t;
        }
        int longs = 0;
        int shorts = 0;
        bool sawInt = false;
        bool sawChar = false;
        bool sawVoid = false;
        bool uns = false;
        bool any = false;
        bool qualOnly = false;  // const or volatile seen; a class or typedef name may follow
        while (!atEnd()) {
            if (eatKw("unsigned")) {
                uns = true;
                any = true;
                continue;
            }
            if (eatKw("signed")) {
                any = true;
                continue;
            }
            if (eatKw("long")) {
                ++longs;
                any = true;
                continue;
            }
            if (eatKw("short")) {
                ++shorts;
                any = true;
                continue;
            }
            if (eatKw("int")) {
                sawInt = true;
                any = true;
                continue;
            }
            if (eatKw("char")) {
                sawChar = true;
                any = true;
                continue;
            }
            if (eatKw("const") || eatKw("volatile") || eatKw("constexpr") || eatKw("inline")) {
                qualOnly = true;
                continue;
            }
            if (!any && isKw("decltype") && nextIs("(")) {
                auto t = parseBase0();  // const decltype(x)
                if (t && qualOnly) t->cnst = true;
                return t;
            }
            // const typename X<T>::type: the word only says a type follows ([temp.res] p3).
            if (!any && qualOnly && isKw("typename") && i_ + 1 < toks_.size() &&
                (toks_[i_ + 1].kind == Tok::Ident || isP(toks_[i_ + 1], "::"))) {
                advance();
                if (isP("::") && i_ + 1 < toks_.size() && toks_[i_ + 1].kind == Tok::Ident) advance();
                continue;
            }
            if (!any && isKw("auto")) {
                advance();
                return autoType();
            }
            if (!any && isKw("bool")) {
                advance();
                auto t = ty(Type::Kind::Bool);
                t->unsign = true;
                return t;
            }
            if (eatKw("void")) {
                sawVoid = true;
                any = true;
                continue;
            }
            if (eatKw("float")) return ty(Type::Kind::Float);
            if (eatKw("double")) return ty(Type::Kind::Double);
            break;
        }
        if (qualOnly && !any && (isKw("struct") || isKw("union") || isKw("class"))) return parseStructType();
        if (qualOnly && !any && isKw("enum")) return parseEnum();
        if (qualOnly && !any && !atEnd() && !(cur().kind == Tok::Ident)) any = true;
        if (any) {
            std::unique_ptr<Type> t;
            if (sawVoid && !longs && !shorts && !sawInt && !sawChar && !uns) t = ty(Type::Kind::Void);
            else if (sawChar && !longs && !shorts && !sawInt && !sawVoid) t = ty(Type::Kind::Char);
            else if (shorts && !longs && !sawChar && !sawVoid) t = ty(Type::Kind::Short);
            else if (longs && !shorts && !sawChar && !sawVoid) t = ty(Type::Kind::Long);
            else if ((sawInt || uns) && !longs && !shorts && !sawChar && !sawVoid) t = ty(Type::Kind::Int);
            if (!t) {
                err("unsupported type");
                return ty(Type::Kind::Int);
            }
            t->unsign = uns;
            return t;
        }
        if (!atEnd() && cur().kind == Tok::Ident && nextIs("::")) {
            std::size_t ti = tmplAfterNs(i_, true);
            if (ti && ti + 1 < toks_.size() && isP(toks_[ti + 1], "<")) i_ = ti;  // std::vector<int>
        }
        if (!atEnd() && cur().kind == Tok::Ident && aliasTmpl_.count(cur().text) && nextIs("<")) return useAliasTemplate();
        if (!atEnd() && cur().kind == Tok::Ident && ctmpl_.count(cur().text) &&
            (!findTypedef(cur().text) || nextIs("<"))) {
            auto inst = instantiateClass(cur().text);
            // X<T>::name: a member type of the instance ([temp.res] p2, [class.qual]):
            // std::vector<int>::iterator, std::map<K, V>::const_iterator.
            while (inst && inst->kind == Type::Kind::Struct && isP("::") && i_ + 1 < toks_.size() &&
                   toks_[i_ + 1].kind == Tok::Ident) {
                std::string q = inst->tag + "::" + toks_[i_ + 1].text;
                auto mt = memberTypes_.find(q);
                auto st = structs_.find(q);
                if (mt != memberTypes_.end() && mt->second) inst = typeRef(*mt->second);
                else if (st != structs_.end() && st->second) inst = typeRef(*st->second);
                else if (inst->tag.find('\x02') != std::string::npos || protoDepth_ > 0) {
                    // A member of an instance that still depends on template parameters
                    // (typename X<I, tuple<T...>>::type in a function template's prototype,
                    // [temp.dep.type]): known only in each instance, so it stands for itself here,
                    // a placeholder that deduction does not bind. In a prototype a value parameter
                    // reads as 0, so an instance with no such member is also left to the instances
                    // (the copy of get<I> for an empty pack names __tuple_at<I, tuple<>>::type).
                    inst = ty(Type::Kind::Struct);
                    inst->tag = std::string("\x02") + "?" + toks_[i_ + 1].text;
                } else break;
                advance();
                advance();
            }
            return inst;
        }
        if (!atEnd() && cur().kind == Tok::Ident && nextIs("::")) {
            std::size_t qend = 0;
            if (const Type* qt = qualTypeAt(i_, qend)) {
                i_ = qend;
                return typeRef(*qt);
            }
        }
        if (!atEnd() && cur().kind == Tok::Ident) {
            const Type* td = findTypedef(cur().text);
            if (td) {
                advance();
                return typeRef(*td);
            }
        }
        if (!atEnd() && cur().kind == Tok::Keyword) {
            err("'" + cur().text + "' is not in this subset");
            advance();
        } else {
            err("expected a type");
        }
        return ty(Type::Kind::Int);
    }

    // enum E { ... }, and the C++ forms enum class E : base { ... } (ISO/IEC 14882 [dcl.enum]). An
    // enumeration is an int here. Its name is a type name. The enumerators of an unscoped enum are
    // names of the enclosing scope and also E::e; those of a scoped enum (enum class) are only E::e.
    // The classes whose unscoped member enumerations declare each bare enumerator name ("" for a
    // namespace): BtParamDef::Number is seen as Number only in BtParamDef and the classes derived
    // from it ([class.member.lookup], [basic.lookup.unqual]); BtValue::Number(f) elsewhere is a call.
    std::map<std::string, std::set<std::string>> enumOwners_;
    std::set<std::string> enumListNames_;  // the enumerators of the scoped enumeration being read
    bool enumeratorVisible(const std::string& name) const {
        if (enumListNames_.count(name)) return true;
        auto it = enumOwners_.find(name);
        if (it == enumOwners_.end() || it->second.count("")) return true;
        std::vector<std::string> ctx;
        for (const Type* c : classStack_)
            if (c && !c->tag.empty()) ctx.push_back(c->tag);
        if (!bodyClass_.empty()) ctx.push_back(bodyClass_);
        if (!memberTag_.empty()) ctx.push_back(memberTag_);
        for (std::string t : ctx) {
            for (int guard = 0; !t.empty() && guard < 64; ++guard) {
                if (it->second.count(t)) return true;
                // a nested class sees its enclosing class's members
                for (const std::string& o : it->second)
                    if (t.compare(0, o.size() + 2, o + "::") == 0) return true;
                auto st = structs_.find(t);
                if (st == structs_.end() || !st->second) break;
                t = st->second->base;
            }
        }
        return false;
    }
    std::unique_ptr<Type> parseEnum() {
        advance();
        bool scoped = eatKw("class") || eatKw("struct");
        std::string ename;
        if (!atEnd() && cur().kind == Tok::Ident) {
            ename = cur().text;
            advance();
        }
        if (eatP(":")) parseBase();  // the underlying type: read, the enum stays an int
        if (!ename.empty()) addTypedef(ename, ty(Type::Kind::Int));
        if (!ename.empty() && bodyDepth_ == 0 && !classStack_.empty() && classStack_.back() &&
            !classStack_.back()->tag.empty())
            memberTypes_[classStack_.back()->tag + "::" + ename] = ty(Type::Kind::Int);  // C::E ([class.qual])
        if (!eatP("{")) return ty(Type::Kind::Int);
        std::int64_t next = 0;
        std::vector<EnumSave> scopedSaved;
        while (!atEnd() && !isP("}")) {
            if (cur().kind != Tok::Ident) {
                err("expected an enumerator");
                break;
            }
            std::string name = cur().text;
            advance();
            if (eatP("=")) {
                auto e = parseAssign();
                long v = 0;  // any integral constant expression ([dcl.enum] p2): 'A', 1 << 3, Other + 1
                if (e && evalConst(*e, v)) next = static_cast<std::int64_t>(v);
                else err("enumerator is not a constant");
            }
            if (!scoped) {
                enums_[name] = next;
                enums_[qualify(name)] = next;
                if (bodyDepth_ == 0 && !classStack_.empty() && classStack_.back() && !classStack_.back()->tag.empty())
                    enumOwners_[name].insert(classStack_.back()->tag);
                else
                    enumOwners_[name].insert("");
            } else {
                // In its own list a scoped enumerator is seen by its bare name ([dcl.enum] p11 /
                // [basic.scope.enum]): C = A | B. The name goes away after the '}'.
                if (std::none_of(scopedSaved.begin(), scopedSaved.end(), [&](const EnumSave& es) { return es.name == name; })) {
                    EnumSave es;
                    es.name = name;
                    auto it = enums_.find(name);
                    if (it != enums_.end()) {
                        es.had = true;
                        es.old = it->second;
                    }
                    scopedSaved.push_back(es);
                }
                enums_[name] = next;
                enumListNames_.insert(name);
            }
            if (!ename.empty()) {
                enums_[ename + "::" + name] = next;
                enums_[qualify(ename) + "::" + name] = next;
            }
            if (bodyDepth_ == 0 && !classStack_.empty() && classStack_.back() && !classStack_.back()->tag.empty()) {
                // A member enumeration: C::E::e and (unscoped) C::e ([class.qual], [dcl.enum] p11).
                const std::string& ct = classStack_.back()->tag;
                if (!ename.empty()) enums_[ct + "::" + ename + "::" + name] = next;
                if (!scoped) enums_[ct + "::" + name] = next;
            }
            ++next;
            if (!eatP(",")) break;
        }
        for (const EnumSave& es : scopedSaved) {
            if (es.had) enums_[es.name] = es.old;
            else enums_.erase(es.name);
            enumListNames_.erase(es.name);
        }
        expectP("}", "}");
        enumDef_ = true;
        return ty(Type::Kind::Int);
    }

    // ---- C++ classes: single inheritance, constructors, destructors, virtual functions ----
    // Written from ISO/IEC 14882 (class, constructor and destructor rules) and the Itanium C++
    // ABI (data layout, vtable layout, name mangling).
    struct Pos {
        int line = 1;
        int col = 1;
        std::string file;
    };
    Pos here() const {
        Pos p;
        if (i_ < toks_.size()) {
            p.line = toks_[i_].line;
            p.col = toks_[i_].column;
            p.file = toks_[i_].file;
        }
        return p;
    }
    struct MemInit {
        std::string name;
        std::vector<std::unique_ptr<Expr>> args;
    };
    struct CtorDef {
        std::vector<Param> params;
        std::vector<MemInit> inits;
        std::vector<std::unique_ptr<Stmt>> pre;  // hidden locals the initializers use
        std::unique_ptr<Stmt> body;
        bool hasBody = false;
        bool defaulted = false;  // C(..) = default;
        Pos pos;
    };
    struct DefaultedAssign {  // C &operator=(..) = default; in the class: made when it is finished
        std::string mangled;
        std::unique_ptr<Type> fn;
        Pos pos;
    };
    struct ClassCtx {
        std::string tag;
        Type* t = nullptr;
        std::vector<CtorDef> ctors;
        std::vector<DefaultedAssign> defaultAssigns;
        bool dtorDeclared = false;
        bool dtorVirtual = false;
        std::unique_ptr<Stmt> dtorBody;
        Pos dtorPos;
        bool dtorInline = false;  // a body for the complete-object destructor is emitted at class end
        bool cxx = false;
        bool tmplCtor = false;  // has a constructor template
    };
    struct DVar {
        std::string name;
        std::string tag;
        std::uint64_t count = 0;  // above 0: an array of that many objects
        std::string guard;        // a static local: destroyed only if this flag is set
        int kind = 0;             // 0 object, 1 try frame (popped), 2 handler (ended)
        // A hidden pointer to the object, made where a local of an inner block takes its name
        // (shadowLocals): while that pointer is in scope, the object is destroyed through it.
        std::string alias{};
    };
    std::vector<std::vector<DVar>> dscopes_;
    std::size_t dbase_ = 0;
    std::vector<std::size_t> loopBases_;
    std::vector<std::size_t> brkBases_;
    const Type* curRet_ = nullptr;
    std::vector<std::unique_ptr<Stmt>> preStmts_;
    int newN_ = 0;
    std::vector<DVar> staticObjs_;                    // static locals: built at run time, after every global
    std::vector<DVar> globalObjs_;                    // globals with destructors, in declaration order
    std::vector<std::unique_ptr<Stmt>> globalInit_;   // their constructor calls, run first in main
    int globalCount_ = 0;

    Type* classOf(const std::string& tag) {
        auto it = structs_.find(tag);
        return it == structs_.end() ? nullptr : it->second.get();
    }
    // A member whose type is a lambda's closure class (a class template argument, F f;). The
    // checker makes that class (and its copy constructor and destructor, when it owns a class
    // object) after the parse, so such a member is copied with "\x01cinit:" and destroyed with
    // "\x01dtorif:", which the checker resolves against the finished closure class.
    static bool closureField(const Type* t) {
        return t && t->kind == Type::Kind::Struct && t->tag.compare(0, 10, "__closure_") == 0;
    }
    static std::string shortName(const std::string& tag) {
        std::size_t c = tag.rfind("::");
        return c == std::string::npos ? tag : tag.substr(c + 2);
    }
    static std::vector<Param> cloneParams(const std::vector<Param>& ps) {
        std::vector<Param> out;
        for (const Param& p : ps) {
            Param c;
            c.name = p.name;
            c.bits = p.bits;
            c.mangled = p.mangled;
            c.vslot = p.vslot;
            c.cmethod = p.cmethod;
            c.pure = p.pure;
            if (p.type) c.type = cloneType(*p.type);
            if (p.defArg) c.defArg = cloneExpr(*p.defArg);  // a constructor's default arguments
            out.push_back(std::move(c));
        }
        return out;
    }
    static const Param* special(const Type* t, const char* name) {
        if (!t) return nullptr;
        for (const Param& p : t->params)
            if (p.name == name) return &p;
        return nullptr;
    }
    static bool isSpecialName(const std::string& n) { return n == "<ctor>" || n == "<dtor>" || n == "<dtor0>"; }
    static bool isDataField(const Param& p) {
        return p.mangled.empty() && p.type && p.type->kind != Type::Kind::Function && p.name != "__vptr" &&
               p.name != "__basepad";
    }
    static std::string vtName(const std::string& tag) {
        if (tag.find("::") == std::string::npos) return "_ZTV" + mangleNested(tag);
        return "_ZTVN" + mangleNested(tag) + "E";
    }

    std::unique_ptr<Expr> eName(const std::string& n, const Pos& p) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Name;
        e->text = n;
        e->line = p.line;
        e->col = p.col;
        e->file = p.file;
        return e;
    }
    std::unique_ptr<Expr> eInt(std::uint64_t v, const Pos& p) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Int;
        e->ival = v;
        e->line = p.line;
        e->col = p.col;
        e->file = p.file;
        return e;
    }
    std::unique_ptr<Expr> eCall(std::unique_ptr<Expr> callee, std::vector<std::unique_ptr<Expr>> args,
                                const Pos& p) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Call;
        e->line = p.line;
        e->col = p.col;
        e->file = p.file;
        e->kids.push_back(std::move(callee));
        for (auto& a : args) e->kids.push_back(std::move(a));
        return e;
    }
    std::unique_ptr<Expr> eAddr(std::unique_ptr<Expr> x, const Pos& p) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Unary;
        e->text = "&";
        e->line = p.line;
        e->col = p.col;
        e->file = p.file;
        e->kids.push_back(std::move(x));
        return e;
    }
    std::unique_ptr<Expr> eArrow(std::unique_ptr<Expr> obj, const std::string& field, const Pos& p) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Member;
        e->text = "->";
        e->line = p.line;
        e->col = p.col;
        e->file = p.file;
        e->kids.push_back(std::move(obj));
        e->kids.push_back(eName(field, p));
        return e;
    }
    std::unique_ptr<Expr> eAssign(std::unique_ptr<Expr> l, std::unique_ptr<Expr> r, const Pos& p) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Assign;
        e->text = "=";
        e->line = p.line;
        e->col = p.col;
        e->file = p.file;
        e->kids.push_back(std::move(l));
        e->kids.push_back(std::move(r));
        return e;
    }
    std::unique_ptr<Stmt> sExpr(std::unique_ptr<Expr> x, const Pos& p) {
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::Expr;
        s->line = p.line;
        s->col = p.col;
        s->file = p.file;
        s->expr = std::move(x);
        return s;
    }
    std::unique_ptr<Stmt> sBlock(const Pos& p) {
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::Block;
        s->line = p.line;
        s->col = p.col;
        s->file = p.file;
        return s;
    }
    std::unique_ptr<Type> voidPtr() {
        auto p = ty(Type::Kind::Pointer);
        p->inner = ty(Type::Kind::Void);
        return p;
    }
    std::unique_ptr<Type> ptrToTag(const std::string& tag) {
        auto inner = ty(Type::Kind::Struct);
        inner->tag = tag;
        auto ptr = ty(Type::Kind::Pointer);
        ptr->inner = std::move(inner);
        return ptr;
    }
    std::unique_ptr<Type> fnTypeOf(const std::vector<Param>& ps) {
        auto ft = ty(Type::Kind::Function);
        ft->inner = ty(Type::Kind::Void);
        ft->params = cloneParams(ps);
        return ft;
    }

    // A call to a constructor. The checker picks the overload from the arguments.
    std::unique_ptr<Expr> ctorCall(const std::string& tag, std::unique_ptr<Expr> self,
                                   std::vector<std::unique_ptr<Expr>> args, const Pos& p) {
        std::vector<std::unique_ptr<Expr>> all;
        all.push_back(std::move(self));
        for (auto& a : args) all.push_back(std::move(a));
        return eCall(eName("\x01" "ctor:" + tag, p), std::move(all), p);
    }
    // T{args} (ISO/IEC 14882 [expr.type.conv], [dcl.init.list]): a temporary. A class with a
    // constructor of its own takes the arguments as T(args). An aggregate runs its implicit
    // constructor (default member initializers) if it has one, then gets each argument in field
    // order; without one, the fields that get no argument are zero.
    // Whether the class has a constructor taking one std::initializer_list ([dcl.init.list] p3).
    static bool hasListCtor(const Type& T) {
        for (const Param& q : T.params)
            if (q.name == "<ctor>" && q.type && q.type->params.size() == 1 && q.type->params[0].type) {
                const Type* pt = q.type->params[0].type.get();
                if (pt->kind == Type::Kind::Reference && pt->inner) pt = pt->inner.get();
                if (pt->kind == Type::Kind::Struct && (pt->tag.compare(0, 22, "std::initializer_list<") == 0 ||
                                                       pt->tag.compare(0, 17, "initializer_list<") == 0))
                    return true;
            }
        return false;
    }
    std::set<std::string> userCtors_;
    // Names of functions declared at namespace scope: a function hides a class of the same name
    // ([class.name] p2), so sigaction(..) calls the function, not struct sigaction's constructor.
    std::set<std::string> fnNames_;
    std::set<std::string> deletedFns_;  // symbols of special members declared "= delete"
    std::vector<Type*> classStack_;  // classes whose body is being parsed, innermost last
    std::unique_ptr<Expr> braceTemp(const Type& td, std::vector<std::unique_ptr<Expr>> args, const Pos& pos) {
        std::string tag = td.tag;
        Type* T = classOf(tag);
        if (T && T->params.empty())
            for (auto it = classStack_.rbegin(); it != classStack_.rend(); ++it)
                if (*it && (*it)->tag == tag) T = *it;  // a member function of the class being defined
        std::string tmp = "__tmp" + std::to_string(newN_++);
        auto mk = [&](Expr::Kind k) {
            auto x = std::make_unique<Expr>();
            x->kind = k;
            x->line = pos.line;
            x->col = pos.col;
            x->file = pos.file;
            return x;
        };
        auto comma = mk(Expr::Kind::Comma);
        comma->ival = 7;
        comma->text = tmp;
        comma->ty = cloneType(td);
        bool listCtor = T && hasListCtor(*T);
        if (T && userCtors_.count(tag) && listCtor) {
            // An initializer-list constructor takes the whole list (the checker builds it).
            auto br = mk(Expr::Kind::Brace);
            br->ival = 5;
            br->kids = std::move(args);
            std::vector<std::unique_ptr<Expr>> one;
            one.push_back(std::move(br));
            comma->kids.push_back(ctorCall(tag, eAddr(eName(tmp, pos), pos), std::move(one), pos));
        } else if (T && userCtors_.count(tag)) {
            comma->kids.push_back(ctorCall(tag, eAddr(eName(tmp, pos), pos), std::move(args), pos));
        } else if (T && [&] {
                       // An aggregate whose list needs brace elision ([dcl.init.aggr] p15: more values
                       // than fields, or an array field): the checker builds it, as it knows the types.
                       std::size_t n = 0;
                       bool arr = false;
                       for (const Param& f : T->params)
                           if (isDataField(f)) {
                               ++n;
                               if (f.type && f.type->kind == Type::Kind::Array) arr = true;
                           }
                       return args.size() > n || (arr && !args.empty());
                   }()) {
            auto br = mk(Expr::Kind::Brace);
            br->ival = 11;
            br->ty = cloneType(td);
            br->kids = std::move(args);
            return br;
        } else {
            std::unique_ptr<Expr> seq;
            auto addE = [&](std::unique_ptr<Expr> x) {
                if (!seq) {
                    seq = std::move(x);
                    return;
                }
                auto c = mk(Expr::Kind::Comma);
                c->kids.push_back(std::move(seq));
                c->kids.push_back(std::move(x));
                seq = std::move(c);
            };
            if (T && T->needCtor) addE(ctorCall(tag, eAddr(eName(tmp, pos), pos), {}, pos));
            std::size_t i = 0;
            if (T)
                for (const Param& f : T->params) {
                    if (!isDataField(f)) continue;
                    std::unique_ptr<Expr> v;
                    if (i < args.size()) v = std::move(args[i]);
                    else if (!T->needCtor && f.type && f.type->kind != Type::Kind::Struct &&
                             f.type->kind != Type::Kind::Union && f.type->kind != Type::Kind::Array)
                        v = mk(Expr::Kind::Int);
                    ++i;
                    if (v) {
                        // tmp.field = v: a plain member, so a class field's operator= is found
                        // (name = "text" for a std::string field).
                        auto field = eArrow(eName(tmp, pos), f.name, pos);
                        field->text = ".";
                        if (f.type && f.type->kind == Type::Kind::Reference) {
                            // A reference member is bound to the value, not assigned through
                            // ([dcl.init.aggr] p4, [dcl.init.ref]): its slot (ival 11) takes the
                            // address. (It wrote through the slot's garbage: a crash.)
                            field->ival = 11;
                            v = eAddr(std::move(v), pos);
                        }
                        addE(eAssign(std::move(field), std::move(v), pos));
                    }
                }
            if (i < args.size()) err("too many initializers");
            if (!seq) seq = mk(Expr::Kind::Int);
            comma->kids.push_back(std::move(seq));
        }
        comma->kids.push_back(eName(tmp, pos));
        return comma;
    }

    // A class whose body is being parsed: its own inline member functions see it complete
    // ([class.mem]/6), so what it needs (constructors, a destructor) is decided by the checker.
    bool inProgress(const std::string& tag) const {
        for (const Type* c : classStack_)
            if (c && c->tag == tag) return true;
        return false;
    }
    std::unique_ptr<Expr> dtorCall(const std::string& tag, std::unique_ptr<Expr> self, const Pos& p) {
        if (tag == "\x03" "auto") {
            // A local declared "auto x = e;": the checker knows its class; it runs that class's
            // destructor, or nothing.
            std::vector<std::unique_ptr<Expr>> args;
            args.push_back(std::move(self));
            return eCall(eName("\x01" "dtorauto:", p), std::move(args), p);
        }
        const Param* d = special(classOf(tag), "<dtor>");
        if (!d && inProgress(tag)) {
            std::vector<std::unique_ptr<Expr>> args;
            args.push_back(std::move(self));
            return eCall(eName("\x01" "dtorif:" + tag, p), std::move(args), p);
        }
        if (!d) return nullptr;
        std::vector<std::unique_ptr<Expr>> args;
        args.push_back(std::move(self));
        return eCall(eName(d->mangled, p), std::move(args), p);
    }
    // this->__vptr = &vtable[index], or this->sub.__vptr for the subobject member sub of a base after
    // the primary one (its part of the vtable group, Itanium C++ ABI 2.5.2).
    std::unique_ptr<Stmt> vptrStore(const std::string& tag, const Pos& p, const std::string& sub = "", int index = 2) {
        auto cast = std::make_unique<Expr>();
        cast->kind = Expr::Kind::Cast;
        cast->line = p.line;
        cast->col = p.col;
        cast->file = p.file;
        auto pp = ty(Type::Kind::Pointer);
        pp->inner = voidPtr();
        cast->ty = std::move(pp);
        cast->kids.push_back(eName(vtName(tag), p));
        auto sum = std::make_unique<Expr>();
        sum->kind = Expr::Kind::Bin;
        sum->text = "+";
        sum->line = p.line;
        sum->col = p.col;
        sum->file = p.file;
        sum->kids.push_back(std::move(cast));
        sum->kids.push_back(eInt(index, p));
        if (sub.empty()) return sExpr(eAssign(eArrow(eName("this", p), "__vptr", p), std::move(sum), p), p);
        auto dot = std::make_unique<Expr>();
        dot->kind = Expr::Kind::Member;
        dot->text = ".";
        dot->line = p.line;
        dot->col = p.col;
        dot->file = p.file;
        dot->kids.push_back(eArrow(eName("this", p), sub, p));
        dot->kids.push_back(eName("__vptr", p));
        return sExpr(eAssign(std::move(dot), std::move(sum), p), p);
    }
    // The vtable pointers of the subobjects of the bases after the primary one: each points into this
    // class's vtable group, past the primary part (see emitClassDecls).
    void otherVptrStores(const Type& T, const std::string& tag, std::vector<std::unique_ptr<Stmt>>& out, const Pos& p) {
        int at = 2 + static_cast<int>(T.vslots.size());
        for (const std::string& S : otherBases(T)) {
            const Type* B = classOf(S);
            if (!B || !B->hasVptr) continue;
            out.push_back(vptrStore(tag, p, "__mb:" + S, at + 2));
            at += 2 + static_cast<int>(B->vslots.size());
        }
    }

    int newSlot(ClassCtx& c, const Token& at) {
        Type& T = *c.t;
        if (!T.hasVptr) {
            if (!T.base.empty()) {
                diags_.error(at.file.empty() ? "t.c" : at.file, at.line, at.column,
                             "a virtual function in a class whose base is not polymorphic is not supported");
            } else {
                Param v;
                v.name = "__vptr";
                v.type = voidPtr();
                T.params.insert(T.params.begin(), std::move(v));
                T.hasVptr = true;
            }
        }
        T.vslots.push_back("");
        return static_cast<int>(T.vslots.size()) - 1;
    }

    void registerDtor(ClassCtx& c, bool virt, const Token& at) {
        Type& T = *c.t;
        Param d;
        d.name = "<dtor>";
        auto ft = fnTypeOf({});
        d.mangled = mangleSpecial(c.tag, "D1", *ft);
        d.type = cloneType(*ft);
        Param d0;
        d0.name = "<dtor0>";
        d0.mangled = mangleSpecial(c.tag, "D0", *ft);
        d0.type = cloneType(*ft);
        if (virt) {
            const Param* bd = T.base.empty() ? nullptr : special(classOf(T.base), "<dtor>");
            int s;
            if (bd && bd->vslot >= 0) {
                s = bd->vslot;
            } else {
                s = newSlot(c, at);
                newSlot(c, at);
            }
            T.vslots[static_cast<std::size_t>(s)] = d.mangled;
            T.vslots[static_cast<std::size_t>(s) + 1] = d0.mangled;
            d.vslot = s;
            d0.vslot = s + 1;
        }
        T.params.push_back(std::move(d));
        T.params.push_back(std::move(d0));
    }

    // ---- Multiple inheritance ([class.mi]; Itanium C++ ABI 2.4, 2.5.2, 3.1.4 thunks) ----
    // A base after the primary one is the member "__mb:<tag>" (see parseStructType); these are the
    // tags of those subobjects, in layout order.
    static std::vector<std::string> otherBases(const Type& t) {
        std::vector<std::string> out;
        for (const Param& p : t.params)
            if (p.name.compare(0, 5, "__mb:") == 0 && p.type) out.push_back(p.type->tag);
        return out;
    }
    // The virtual function of base class S (or of S's own bases at its start) that a member
    // function name(type) const? of a derived class overrides ([class.virtual] p2), or null.
    const Param* virtualIn(const std::string& S, const std::string& name, const Type& fn, bool cmethod) {
        const Type* B = classOf(S);
        if (!B) return nullptr;
        for (const Param& q : B->params)
            if (q.name == name && q.vslot >= 0 && q.type && !isSpecialName(q.name) && q.cmethod == cmethod &&
                sameParamTypes(*q.type, fn))
                return &q;
        return nullptr;
    }
    bool overridesOtherBase(const Type& t, const std::string& name, const Type& fn, bool cmethod) {
        for (const std::string& S : otherBases(t))
            if (virtualIn(S, name, fn, cmethod)) return true;
        return false;
    }
    // The parts of the vtable group for the bases after the primary one (Itanium C++ ABI 2.5.2):
    // for each, offset-to-top (minus the subobject's offset), the type_info, then the base's slots;
    // a slot this class overrides holds a thunk that moves 'this' back to the whole object and
    // jumps to the overrider (3.1.4: _ZThn<offset>_<overrider>).
    void otherVtables(const std::string& tag, const Type& T, const std::string& ti, std::vector<std::string>& vt,
                      std::vector<std::unique_ptr<Decl>>& thunks, const Pos& p) {
        const Type* P0 = T.base.empty() ? nullptr : classOf(T.base);
        for (const std::string& S : otherBases(T)) {
            const Type* B = classOf(S);
            if (!B || !B->hasVptr) continue;
            const int off = memberOffset(tag, "__mb:" + S);
            if (off < 0) continue;
            // A subobject this class has through its primary base (class E : Anim): the primary
            // base's functions are overriders of S's too.
            bool viaPrimary = false;
            if (P0)
                for (const Param& r : P0->params)
                    if (r.name == "__mb:" + S) viaPrimary = true;
            vt.push_back("\x01#" + std::to_string(-off));
            vt.push_back(ti);
            for (std::size_t k = 0; k < B->vslots.size(); ++k) {
                const Param* bf = nullptr;
                for (const Param& q : B->params)
                    if (q.vslot == static_cast<int>(k)) bf = &q;
                const Param* over = nullptr;
                if (bf && (bf->name == "<dtor>" || bf->name == "<dtor0>")) {
                    over = special(&T, bf->name.c_str());
                    if (over && over->vslot < 0) over = nullptr;
                } else if (bf && bf->type) {
                    for (const Param& q : T.params) {
                        if (q.name != bf->name || q.mangled.empty() || !q.type || q.cmethod != bf->cmethod ||
                            !sameParamTypes(*q.type, *bf->type))
                            continue;
                        bool inherited = false;  // the primary base's function is not an overrider of S's
                        if (P0 && !viaPrimary)
                            for (const Param& r : P0->params)
                                if (r.mangled == q.mangled) inherited = true;
                        if (!inherited) over = &q;
                    }
                }
                if (!over || over->mangled.size() < 3) {
                    vt.push_back(B->vslots[k]);
                    continue;
                }
                std::string name = "_ZThn" + std::to_string(off) + "_" + over->mangled.substr(2);
                vt.push_back(name);
                const bool isDtor = over->name == "<dtor>" || over->name == "<dtor0>";
                auto fn = isDtor ? fnTypeOf({}) : cloneType(*over->type);
                std::vector<Param> ps;
                for (std::size_t a = 0; a < fn->params.size(); ++a) {
                    Param q;
                    q.name = "__t" + std::to_string(a);
                    if (fn->params[a].type) q.type = cloneType(*fn->params[a].type);
                    ps.push_back(std::move(q));
                }
                auto decl = newMethodDecl(S, name, *fn, ps, p);
                decl->isInline = true;
                // (T *)((char *)this - off)
                auto toChar = std::make_unique<Expr>();
                toChar->kind = Expr::Kind::Cast;
                toChar->line = p.line;
                toChar->col = p.col;
                toChar->file = p.file;
                toChar->ty = ty(Type::Kind::Pointer);
                toChar->ty->inner = ty(Type::Kind::Char);
                toChar->kids.push_back(eName("this", p));
                auto minus = std::make_unique<Expr>();
                minus->kind = Expr::Kind::Bin;
                minus->text = "-";
                minus->line = p.line;
                minus->col = p.col;
                minus->file = p.file;
                minus->kids.push_back(std::move(toChar));
                minus->kids.push_back(eInt(static_cast<std::uint64_t>(off), p));
                auto whole = std::make_unique<Expr>();
                whole->kind = Expr::Kind::Cast;
                whole->line = p.line;
                whole->col = p.col;
                whole->file = p.file;
                whole->ty = ptrToTag(tag);
                whole->kids.push_back(std::move(minus));
                std::vector<std::unique_ptr<Expr>> args;
                args.push_back(std::move(whole));
                for (const Param& q : ps) args.push_back(eName(q.name, p));
                auto call = eCall(eName(over->mangled, p), std::move(args), p);
                auto body = sBlock(p);
                if (fn->inner && fn->inner->kind != Type::Kind::Void) {
                    auto r = std::make_unique<Stmt>();
                    r->kind = Stmt::Kind::Return;
                    r->line = p.line;
                    r->col = p.col;
                    r->file = p.file;
                    r->expr = std::move(call);
                    body->stmts.push_back(std::move(r));
                } else {
                    body->stmts.push_back(sExpr(std::move(call), p));
                }
                decl->body = std::move(body);
                thunks.push_back(std::move(decl));
            }
        }
    }
    // A destructor is virtual when a base's is ([class.dtor] p13).
    bool basesVirtualDtor(const Type& t) {
        std::vector<std::string> all = otherBases(t);
        if (!t.base.empty()) all.push_back(t.base);
        for (const std::string& b : all) {
            const Type* B = classOf(b);
            const Param* d = B ? special(B, "<dtor>") : nullptr;
            if (d && d->vslot >= 0) return true;
        }
        return false;
    }

    bool sameParamTypes(const Type& a, const Type& b) {
        if (a.params.size() != b.params.size()) return false;
        for (std::size_t i = 0; i < a.params.size(); ++i) {
            if (!a.params[i].type || !b.params[i].type || !sameType(*a.params[i].type, *b.params[i].type))
                return false;
        }
        return true;
    }

    // The data members a class declares itself: those after the copies of its base's members,
    // which come first in the same order (a member of the same name as a base's is the class's own).
    std::vector<std::pair<std::string, const Type*>> ownFields(const Type& T) {
        std::vector<std::pair<std::string, const Type*>> out;
        const Type* B = T.base.empty() ? nullptr : classOf(T.base);
        std::size_t skip = 0;
        if (B)
            for (const Param& bp : B->params)
                if (isDataField(bp)) ++skip;
        for (const Param& p : T.params) {
            if (!isDataField(p)) continue;
            if (skip > 0) {
                --skip;
                continue;
            }
            out.emplace_back(p.name, p.type.get());
        }
        return out;
    }
    // A data member with the name of a base's member hides it ([class.member.lookup]): the base's
    // copy among the class's members is renamed, so a name finds the class's own member; the base's
    // own functions still reach theirs through the base class.
    void hideBaseFields(Type& T) {
        const Type* B = T.base.empty() ? nullptr : classOf(T.base);
        if (!B) return;
        std::size_t inherited = 0;
        for (const Param& bp : B->params)
            if (isDataField(bp)) ++inherited;
        std::set<std::string> own;
        for (const auto& f : ownFields(T)) own.insert(f.first);
        std::size_t k = 0;
        for (Param& p : T.params) {
            if (!isDataField(p)) continue;
            if (k++ >= inherited) break;
            if (own.count(p.name)) p.name += "\x05" + T.base;
        }
    }

    std::unique_ptr<Decl> newMethodDecl(const std::string& tag, const std::string& mangled, const Type& userFn,
                                        const std::vector<Param>& params, const Pos& p) {
        auto decl = std::make_unique<Decl>();
        decl->name = mangled;
        decl->function = true;
        decl->methodTag = tag;
        decl->line = p.line;
        decl->col = p.col;
        decl->file = p.file;
        auto ft = ty(Type::Kind::Function);
        ft->inner = userFn.inner ? cloneType(*userFn.inner) : ty(Type::Kind::Void);
        Param self;
        self.name = "this";
        self.type = ptrToTag(tag);
        ft->params.push_back(std::move(self));
        for (const Param& q : params) {
            Param c;
            c.name = q.name;
            if (q.type) c.type = cloneType(*q.type);
            if (q.defArg) c.defArg = cloneExpr(*q.defArg);
            ft->params.push_back(std::move(c));
        }
        decl->type = std::move(ft);
        return decl;
    }

    // A constructor body: base constructor, vtable pointer, members in declaration order, then the
    // user's own statements (ISO C++ [class.base.init]).
    // A constructor that takes no arguments (every parameter has a default) or none declared.
    static bool defaultConstructible(const Type& cls) {
        bool any = false;
        for (const Param& q : cls.params) {
            if (q.name != "<ctor>" || !q.type) continue;
            any = true;
            bool all = true;
            for (const Param& a : q.type->params)
                if (!a.defArg) all = false;
            if (all) return true;
        }
        return !any;
    }
    std::unique_ptr<Decl> buildCtor(const std::string& tag, CtorDef& d, const std::string& symbol = "") {
        Type* T = classOf(tag);
        if (!T) return nullptr;
        Pos p = d.pos;
        auto fn = fnTypeOf(d.params);
        std::string mangled = symbol.empty() ? mangleSpecial(tag, "C1", *fn) : symbol;
        auto decl = newMethodDecl(tag, mangled, *fn, d.params, p);
        auto body = sBlock(p);
        for (auto& st : d.pre) body->stmts.push_back(std::move(st));
        d.pre.clear();
        auto take = [&](const std::string& n, const std::string& alt) -> MemInit* {
            for (MemInit& m : d.inits)
                if (m.name == n || (!alt.empty() && m.name == alt)) return &m;
            return nullptr;
        };
        {
            // A delegating constructor, C() : C(args) {} ([class.base.init] p6): the target
            // constructor builds the whole object, then this body runs.
            std::string own = shortName(tag);
            std::size_t lt = own.find('<');
            std::string ownBare = lt == std::string::npos ? own : own.substr(0, lt);
            if (d.inits.size() == 1 && (d.inits[0].name == own || d.inits[0].name == ownBare || d.inits[0].name == tag)) {
                body->stmts.push_back(sExpr(ctorCall(tag, eName("this", p), std::move(d.inits[0].args), p), p));
                if (d.body) body->stmts.push_back(std::move(d.body));
                decl->body = std::move(body);
                return decl;
            }
        }
        Type* B = T->base.empty() ? nullptr : classOf(T->base);
        int ehHeld = 0;  // sub-objects recorded for unwinding while the constructor runs
        if (B && B->needCtor) {
            MemInit* mi = take(shortName(T->base), T->base);
            std::vector<std::unique_ptr<Expr>> args;
            if (mi) args = std::move(mi->args);
            body->stmts.push_back(sExpr(ctorCall(T->base, eName("this", p), std::move(args), p), p));
            if (eh_ && B->needDtor) {
                ehPushObject(body->stmts, eName("this", p), T->base, 0, p);
                ++ehHeld;
            }
        }
        if (T->hasVptr) body->stmts.push_back(vptrStore(tag, p));
        for (auto& f : ownFields(*T)) {
            // A base after the primary one is initialized by its class name (Backend(), ns::Backend(x)).
            const bool subBase = f.first.compare(0, 5, "__mb:") == 0;
            MemInit* mi = subBase ? take(shortName(f.first.substr(5)), f.first.substr(5)) : take(f.first, "");
            Type* ft = f.second && f.second->kind == Type::Kind::Struct && !f.second->tag.empty()
                           ? classOf(f.second->tag)
                           : nullptr;
            const Expr* dm = mi ? nullptr : memberDefault(tag, f.first);
            if (ft && ft->needCtor) {
                std::vector<std::unique_ptr<Expr>> args;
                bool listArg = false;  // the one argument is the whole braced list
                if (mi) args = std::move(mi->args);
                else if (dm && dm->kind == Expr::Kind::Brace && !dm->kids.empty() && hasListCtor(*ft)) {
                    // std::vector<std::string> items{"a", "b"}; an initializer-list constructor
                    // takes the whole list ([dcl.init.list] p3.6), as for a local
                    auto br = std::make_unique<Expr>();
                    br->kind = Expr::Kind::Brace;
                    br->ival = 5;
                    br->line = dm->line;
                    br->col = dm->col;
                    br->file = dm->file;
                    for (const auto& k : dm->kids) br->kids.push_back(cloneExpr(*k));
                    args.push_back(std::move(br));
                    listArg = true;
                } else if (dm && dm->kind == Expr::Kind::Brace)
                    for (const auto& k : dm->kids) args.push_back(cloneExpr(*k));
                else if (dm) args.push_back(cloneExpr(*dm));
                if (args.empty() && userCtors_.count(f.second->tag) && !defaultConstructible(*ft)) {
                    // A member with no default constructor ([class.default.ctor] p2: the class's own
                    // default constructor is deleted): the class is only made by a braced list
                    // (struct R { std::type_index t; ..} r{typeid(X), ..}), which assigns the member.
                    continue;
                }
                if (args.size() == 1 && !listArg) {
                    // One argument: an object of the class is copied, anything else goes to a constructor.
                    std::vector<std::unique_ptr<Expr>> a;
                    a.push_back(eAddr(eArrow(eName("this", p), f.first, p), p));
                    a.push_back(std::move(args[0]));
                    body->stmts.push_back(sExpr(eCall(eName("\x01" "cinit:" + f.second->tag, p), std::move(a), p), p));
                    if (eh_ && ft->needDtor) {
                        ehPushObject(body->stmts, eAddr(eArrow(eName("this", p), f.first, p), p), f.second->tag, 0, p);
                        ++ehHeld;
                    }
                    continue;
                }
                body->stmts.push_back(sExpr(
                    ctorCall(f.second->tag, eAddr(eArrow(eName("this", p), f.first, p), p), std::move(args), p), p));
                if (eh_ && ft->needDtor) {
                    ehPushObject(body->stmts, eAddr(eArrow(eName("this", p), f.first, p), p), f.second->tag, 0, p);
                    ++ehHeld;
                }
            } else if (!mi && !dm && f.second && f.second->kind == Type::Kind::Array && arrayElemClass(f.second) &&
                       classOf(arrayElemClass(f.second)->tag) && classOf(arrayElemClass(f.second)->tag)->needCtor) {
                // Ring lines[8]: each element by its default constructor ([class.base.init]/9).
                std::string et = arrayElemClass(f.second)->tag;
                body->stmts.push_back(fieldLoop(f.first, f.second->bound->ival, false, p, [&](std::unique_ptr<Expr> at) {
                    return ctorCall(et, std::move(at), {}, p);
                }));
            } else if (mi && mi->args.size() == 1 && closureField(f.second)) {
                std::vector<std::unique_ptr<Expr>> a;
                a.push_back(eAddr(eArrow(eName("this", p), f.first, p), p));
                a.push_back(std::move(mi->args[0]));
                body->stmts.push_back(sExpr(eCall(eName("\x01" "cinit:" + f.second->tag, p), std::move(a), p), p));
            } else if (mi && mi->args.size() == 1 && f.second && f.second->kind == Type::Kind::Reference) {
                // A reference member is bound, not assigned ([class.base.init] p8, [dcl.init.ref]):
                // it holds the address of its referent. ival 11 names the reference itself.
                auto slot = eArrow(eName("this", p), f.first, p);
                slot->ival = 11;
                body->stmts.push_back(sExpr(eAssign(std::move(slot), eAddr(std::move(mi->args[0]), p), p), p));
            } else if (mi && mi->args.size() == 1) {
                body->stmts.push_back(
                    sExpr(eAssign(eArrow(eName("this", p), f.first, p), std::move(mi->args[0]), p), p));
            } else if (dm && f.second && f.second->kind == Type::Kind::Array && dm->kind == Expr::Kind::Brace) {
                // int a[4] = {1, 2}; float m[3][3] = {{1, 0, 0}, ...}: each named element, the rest zero.
                arrayInit(body->stmts, *eArrow(eName("this", p), f.first, p), f.second, dm, p);
            } else if (dm) {
                std::unique_ptr<Expr> v;
                if (dm->kind == Expr::Kind::Brace && f.second && f.second->kind == Type::Kind::Struct) {
                    // std::array<int, 3> row{}; an aggregate member: this->row = {...} ([expr.ass] p9).
                    v = cloneExpr(*dm);
                } else if (dm->kind == Expr::Kind::Brace) {
                    if (!dm->kids.empty() && dm->kids[0]) v = cloneExpr(*dm->kids[0]);
                    else {
                        v = std::make_unique<Expr>();
                        v->kind = Expr::Kind::Int;  // int x{}; is zero
                    }
                } else {
                    v = cloneExpr(*dm);
                }
                body->stmts.push_back(sExpr(eAssign(eArrow(eName("this", p), f.first, p), std::move(v), p), p));
            }
        }
        otherVptrStores(*T, tag, body->stmts, p);
        // A member of an anonymous union or struct member (union { bool b; int64_t i; }; .. : i(0)),
        // [class.union.anon] p1: its members are members of the class. It is assigned here.
        for (std::size_t k = 0; k < d.inits.size();) {
            MemInit& m = d.inits[k];
            bool inAnon = false;
            for (auto& f : ownFields(*T)) {
                if (f.first.compare(0, 6, "__anon") != 0 || !f.second || f.second->tag.size() ||
                    (f.second->kind != Type::Kind::Union && f.second->kind != Type::Kind::Struct))
                    continue;
                for (const Param& q : f.second->params)
                    if (q.name == m.name) inAnon = true;
            }
            if (!inAnon || m.args.size() != 1) {
                ++k;
                continue;
            }
            body->stmts.push_back(sExpr(eAssign(eArrow(eName("this", p), m.name, p), std::move(m.args[0]), p), p));
            d.inits.erase(d.inits.begin() + static_cast<std::ptrdiff_t>(k));
        }
        for (MemInit& m : d.inits) {
            bool ok = (B && (m.name == shortName(T->base) || m.name == T->base));
            for (auto& f : ownFields(*T))
                if (f.first == m.name || (f.first.compare(0, 5, "__mb:") == 0 &&
                                          (m.name == f.first.substr(5) || m.name == shortName(f.first.substr(5)))))
                    ok = true;
            if (!ok) diags_.error(p.file.empty() ? "t.c" : p.file, p.line, p.col, "'" + m.name + "' is not a member or base");
        }
        if (d.body) {
            if (ehHeld > 0 && hasReturn(*d.body))
                diags_.error(p.file.empty() ? "t.c" : p.file, p.line, p.col,
                             "a return in a constructor body is not supported with exceptions");
            body->stmts.push_back(std::move(d.body));
        }
        for (int k = 0; k < ehHeld; ++k) body->stmts.push_back(sExpr(ehCall("__okay_eh_pop", {}, p), p));
        decl->body = std::move(body);
        return decl;
    }

    static bool hasReturn(const Stmt& st) {
        if (st.kind == Stmt::Kind::Return) return true;
        for (const auto& k : st.stmts)
            if (k && hasReturn(*k)) return true;
        for (const Stmt* k : {st.thenS.get(), st.elseS.get(), st.body.get(), st.init.get()})
            if (k && hasReturn(*k)) return true;
        return false;
    }

    std::unique_ptr<Decl> buildDtor(const std::string& tag, std::unique_ptr<Stmt> user, const Pos& p, bool deleting) {
        Type* T = classOf(tag);
        if (!T) return nullptr;
        auto fn = fnTypeOf({});
        auto decl = newMethodDecl(tag, mangleSpecial(tag, deleting ? "D0" : "D1", *fn), *fn, {}, p);
        auto body = sBlock(p);
        if (deleting) {
            std::vector<std::unique_ptr<Expr>> a1;
            a1.push_back(eName("this", p));
            body->stmts.push_back(sExpr(eCall(eName(special(T, "<dtor>")->mangled, p), std::move(a1), p), p));
            auto cast = std::make_unique<Expr>();
            cast->kind = Expr::Kind::Cast;
            cast->line = p.line;
            cast->col = p.col;
            cast->file = p.file;
            auto cp = ty(Type::Kind::Pointer);
            cp->inner = ty(Type::Kind::Char);
            cast->ty = std::move(cp);
            cast->kids.push_back(eName("this", p));
            std::vector<std::unique_ptr<Expr>> a2;
            a2.push_back(std::move(cast));
            body->stmts.push_back(sExpr(eCall(eName("free", p), std::move(a2), p), p));
            decl->body = std::move(body);
            return decl;
        }
        if (T->hasVptr) body->stmts.push_back(vptrStore(tag, p));
        if (user) body->stmts.push_back(std::move(user));
        auto fields = ownFields(*T);
        for (int i = static_cast<int>(fields.size()) - 1; i >= 0; --i) {
            const Type* ft = fields[static_cast<std::size_t>(i)].second;
            if (ft && ft->kind == Type::Kind::Array && arrayElemClass(ft) && classOf(arrayElemClass(ft)->tag) &&
                classOf(arrayElemClass(ft)->tag)->needDtor) {
                std::string et = arrayElemClass(ft)->tag;
                body->stmts.push_back(fieldLoop(fields[static_cast<std::size_t>(i)].first, ft->bound->ival, true, p,
                                                [&](std::unique_ptr<Expr> at) { return dtorCall(et, std::move(at), p); }));
                continue;
            }
            if (closureField(ft) && !classOf(ft->tag)) {
                std::vector<std::unique_ptr<Expr>> args;
                args.push_back(eAddr(eArrow(eName("this", p), fields[static_cast<std::size_t>(i)].first, p), p));
                body->stmts.push_back(sExpr(eCall(eName("\x01" "dtorif:" + ft->tag, p), std::move(args), p), p));
                continue;
            }
            if (ft && ft->kind == Type::Kind::Struct && !ft->tag.empty()) {
                Type* c = classOf(ft->tag);
                if (c && c->needDtor) {
                    auto call = dtorCall(ft->tag,
                                         eAddr(eArrow(eName("this", p), fields[static_cast<std::size_t>(i)].first, p), p), p);
                    if (call) body->stmts.push_back(sExpr(std::move(call), p));
                }
            }
        }
        Type* B = T->base.empty() ? nullptr : classOf(T->base);
        if (B && B->needDtor) {
            auto call = dtorCall(T->base, eName("this", p), p);
            if (call) body->stmts.push_back(sExpr(std::move(call), p));
        }
        decl->body = std::move(body);
        return decl;
    }

    // Assignments that fill an array from a braced list, element by element (nested lists for the
    // inner arrays); what the list does not name is zero.
    void arrayInit(std::vector<std::unique_ptr<Stmt>>& out, const Expr& base, const Type* t, const Expr* list,
                   const Pos& p) {
        std::uint64_t n = t->bound && t->bound->kind == Expr::Kind::Int ? t->bound->ival : 0;
        for (std::uint64_t k = 0; k < n; ++k) {
            auto idx = std::make_unique<Expr>();
            idx->kind = Expr::Kind::Int;
            idx->ival = k;
            auto el = std::make_unique<Expr>();
            el->kind = Expr::Kind::Index;
            el->line = p.line;
            el->col = p.col;
            el->file = p.file;
            el->kids.push_back(cloneExpr(base));
            el->kids.push_back(std::move(idx));
            const Expr* item = list && k < list->kids.size() ? list->kids[k].get() : nullptr;
            if (t->inner && t->inner->kind == Type::Kind::Array) {
                arrayInit(out, *el, t->inner.get(), item && item->kind == Expr::Kind::Brace ? item : nullptr, p);
                continue;
            }
            std::unique_ptr<Expr> v;
            if (t->inner && (t->inner->kind == Type::Kind::Struct || t->inner->kind == Type::Kind::Union)) {
                // S a[4]{} or {{..}, {..}}: each element from its own braced list, the rest from {}
                // (value-initialized, [dcl.init.aggr] p5); the element is an object, not a number.
                if (item) {
                    v = cloneExpr(*item);
                } else {
                    v = std::make_unique<Expr>();
                    v->kind = Expr::Kind::Brace;
                    v->line = p.line;
                    v->col = p.col;
                    v->file = p.file;
                }
                out.push_back(sExpr(eAssign(std::move(el), std::move(v), p), p));
                continue;
            }
            if (item && item->kind != Expr::Kind::Brace) v = cloneExpr(*item);
            else if (item && !item->kids.empty() && item->kids[0]) v = cloneExpr(*item->kids[0]);
            else {
                v = std::make_unique<Expr>();
                v->kind = Expr::Kind::Int;
            }
            out.push_back(sExpr(eAssign(std::move(el), std::move(v), p), p));
        }
    }

    // A default member initializer: int x = 1; int a[2] = {1, 2}; int y{3}; ([class.mem]). It is
    // used by every constructor whose initializer list does not name the member, and it makes
    // the implicit default constructor do work.
    std::map<std::string, std::map<std::string, std::unique_ptr<Expr>>> dmi_;
    bool memberInit(const std::string& tag, const std::string& name) {
        if (!isP("=") && !isP("{")) return false;
        eatP("=");
        auto init = parseInitializer();
        if (tag.empty() || name.empty() || !init) return false;
        dmi_[tag][name] = std::move(init);
        return true;
    }
    const Expr* memberDefault(const std::string& tag, const std::string& name) const {
        auto it = dmi_.find(tag);
        if (it == dmi_.end()) return nullptr;
        auto jt = it->second.find(name);
        return jt == it->second.end() ? nullptr : jt->second.get();
    }

    // The implicit copy assignment operator ([class.copy.assign]): a class with no operator= of its
    // own, whose base or a member has one, assigns the base and each member in turn (an array
    // member element by element) and returns *this. Without such a part, assignment stays a copy
    // of the bytes, which is the same thing.
    static bool hasAssign(const Type* cls) {
        if (!cls) return false;
        for (const Param& q : cls->params)
            if (q.name == "operator=" && !q.mangled.empty()) return true;
        return false;
    }
    void implicitAssign(ClassCtx& c, Type& T, Type* B, const Pos& p) {
        if (hasAssign(&T)) return;
        bool parts = B && hasAssign(B);
        for (auto& f : ownFields(T)) {
            const Type* ft = f.second;
            while (ft && ft->kind == Type::Kind::Array && ft->inner) ft = ft->inner.get();
            if (ft && ft->kind == Type::Kind::Struct && !ft->tag.empty() && hasAssign(classOf(ft->tag))) parts = true;
        }
        if (!parts) return;
        auto refT = [&]() {
            auto r = ty(Type::Kind::Reference);
            r->inner = ty(Type::Kind::Struct);
            r->inner->tag = c.tag;
            return r;
        };
        auto fn = ty(Type::Kind::Function);
        fn->inner = refT();
        Param op;
        op.name = "__o";
        op.type = refT();
        fn->params.push_back(std::move(op));
        Param m;
        m.name = "operator=";
        m.mangled = mangleFunc(c.tag + "::operator=", *fn);
        m.type = cloneType(*fn);
        T.params.push_back(std::move(m));
        auto decl = newMethodDecl(c.tag, T.params.back().mangled, *fn, fn->params, p);
        decl->body = memberwiseAssign(T, false, p);
        extras_.push_back(std::move(decl));
    }

    // std::move(x) written out: *&x marked as an xvalue (ival 5, as the checker marks the result
    // of a call returning T&&).
    std::unique_ptr<Expr> eXvalue(std::unique_ptr<Expr> x, const Pos& p) {
        auto d = std::make_unique<Expr>();
        d->kind = Expr::Kind::Unary;
        d->text = "*";
        d->ival = 5;
        d->line = p.line;
        d->col = p.col;
        d->file = p.file;
        d->kids.push_back(eAddr(std::move(x), p));
        return d;
    }
    // __o.field: a part of the object copied or moved from.
    std::unique_ptr<Expr> eOther(const std::string& field, const Pos& p) {
        auto dot = std::make_unique<Expr>();
        dot->kind = Expr::Kind::Member;
        dot->text = ".";
        dot->line = p.line;
        dot->col = p.col;
        dot->file = p.file;
        dot->kids.push_back(eName("__o", p));
        dot->kids.push_back(eName(field, p));
        return dot;
    }
    // this->field = __o.field, an array member element by element; for a move, a part of class
    // type is moved.
    // construct: the parts are built, not assigned (a copy or move constructor's array members):
    // an element of a class with constructors gets one (T(__o.a[i])); assigning would run its
    // operator= on an object never built (a std::vector member freed what was never allocated).
    void assignPart(Stmt& body, const std::string& field, const Type* ft, bool move, const Pos& p,
                    bool construct = false) {
        auto from = [&](std::unique_ptr<Expr> x, const Type* t) {
            return move && t && t->kind == Type::Kind::Struct ? eXvalue(std::move(x), p) : std::move(x);
        };
        if (ft && ft->kind == Type::Kind::Array) {
            // Element by element, through every dimension (bool has[5][4]: has[i][j] = __o.has[i][j]).
            std::vector<std::uint64_t> dims;
            const Type* el = ft;
            for (; el && el->kind == Type::Kind::Array; el = el->inner.get())
                dims.push_back(el->bound && el->bound->kind == Expr::Kind::Int ? el->bound->ival : 0);
            std::vector<std::uint64_t> at(dims.size(), 0);
            for (std::uint64_t d : dims)
                if (d == 0) return;
            for (;;) {
                auto idx = [&](std::unique_ptr<Expr> base) {
                    for (std::uint64_t k : at) {
                        auto ix = std::make_unique<Expr>();
                        ix->kind = Expr::Kind::Index;
                        ix->line = p.line;
                        ix->col = p.col;
                        ix->file = p.file;
                        ix->kids.push_back(std::move(base));
                        ix->kids.push_back(eInt(k, p));
                        base = std::move(ix);
                    }
                    return base;
                };
                Type* ec = construct && el && el->kind == Type::Kind::Struct && !el->tag.empty() ? classOf(el->tag)
                                                                                                    : nullptr;
                bool hasCtor = false;
                if (ec)
                    for (const Param& q : ec->params)
                        if (q.name == "<ctor>") hasCtor = true;
                if (hasCtor) {
                    std::vector<std::unique_ptr<Expr>> a;
                    a.push_back(from(idx(eOther(field, p)), el));
                    body.stmts.push_back(sExpr(ctorCall(el->tag, eAddr(idx(eArrow(eName("this", p), field, p)), p), std::move(a), p), p));
                } else {
                    body.stmts.push_back(
                        sExpr(eAssign(idx(eArrow(eName("this", p), field, p)), from(idx(eOther(field, p)), el), p), p));
                }
                std::size_t q = at.size();
                while (q > 0 && ++at[q - 1] == dims[q - 1]) at[--q] = 0;
                if (q == 0) break;
            }
            return;
        }
        body.stmts.push_back(sExpr(eAssign(eArrow(eName("this", p), field, p), from(eOther(field, p), ft), p), p));
    }
    // The body of an implicit or defaulted copy or move assignment ([class.copy.assign] p12): the
    // base part (*(B *)this = *(B *)&__o, with the base's own operator= when it has one), then
    // each member in turn, then return *this. A move assignment moves each part.
    std::unique_ptr<Stmt> memberwiseAssign(const Type& T, bool move, const Pos& p) {
        auto body = sBlock(p);
        if (!T.base.empty() && classOf(T.base)) {
            auto castTo = [&](std::unique_ptr<Expr> x) {
                auto cst = std::make_unique<Expr>();
                cst->kind = Expr::Kind::Cast;
                cst->line = p.line;
                cst->col = p.col;
                cst->file = p.file;
                cst->ty = ty(Type::Kind::Pointer);
                cst->ty->inner = ty(Type::Kind::Struct);
                cst->ty->inner->tag = T.base;
                cst->kids.push_back(std::move(x));
                auto d = std::make_unique<Expr>();
                d->kind = Expr::Kind::Unary;
                d->text = "*";
                d->line = p.line;
                d->col = p.col;
                d->file = p.file;
                d->kids.push_back(std::move(cst));
                return d;
            };
            auto src = castTo(eAddr(eName("__o", p), p));
            if (move) src = eXvalue(std::move(src), p);
            body->stmts.push_back(sExpr(eAssign(castTo(eName("this", p)), std::move(src), p), p));
        }
        for (auto& f : ownFields(T)) assignPart(*body, f.first, f.second, move, p);
        auto ret = std::make_unique<Stmt>();
        ret->kind = Stmt::Kind::Return;
        ret->line = p.line;
        ret->col = p.col;
        ret->file = p.file;
        auto self = std::make_unique<Expr>();
        self->kind = Expr::Kind::Unary;
        self->text = "*";
        self->line = p.line;
        self->col = p.col;
        self->file = p.file;
        self->kids.push_back(eName("this", p));
        ret->expr = std::move(self);
        body->stmts.push_back(std::move(ret));
        return body;
    }
    // The member initializers of an implicit or defaulted copy or move constructor ([class.copy.ctor]
    // p14): the base from __o, then each member from __o's (moved for a move). An array member is
    // copied element by element in the body. The constructor's one parameter becomes __o.
    void memberwiseInits(CtorDef& d, const Type& T, bool move, const Pos& p) {
        if (d.params.size() != 1) return;
        d.params[0].name = "__o";
        d.inits.clear();
        d.body = sBlock(p);
        d.hasBody = true;
        if (!T.base.empty()) {
            MemInit mi;
            mi.name = shortName(T.base);
            mi.args.push_back(move ? eXvalue(eName("__o", p), p) : eName("__o", p));
            d.inits.push_back(std::move(mi));
        }
        for (auto& f : ownFields(T)) {
            const Type* ft = f.second;
            if (ft && ft->kind == Type::Kind::Array) {
                assignPart(*d.body, f.first, ft, move, p, true);
                continue;
            }
            MemInit mi;
            mi.name = f.first;
            auto from = eOther(f.first, p);
            mi.args.push_back(move && ft && ft->kind == Type::Kind::Struct ? eXvalue(std::move(from), p) : std::move(from));
            d.inits.push_back(std::move(mi));
        }
    }
    // A defaulted constructor whose one parameter is a reference to its own class: a copy or move
    // constructor; it gets the implicit one's member initializers ([dcl.fct.def.default] p5).
    void defaultCopyCtor(CtorDef& d, const std::string& tag, const Type& T) {
        if (d.params.size() != 1 || !d.params[0].type) return;
        const Type* a = d.params[0].type.get();
        if (a->kind != Type::Kind::Reference || !a->inner || a->inner->kind != Type::Kind::Struct || a->inner->tag != tag)
            return;
        memberwiseInits(d, T, a->rref, d.pos);
    }
    // A defaulted operator= whose parameter is a reference to its own class: the copy or move
    // assignment, with the implicit one's body. Returns the definition (or null when it is not one).
    std::unique_ptr<Decl> defaultAssign(const std::string& tag, const std::string& mangled, const Type& fn,
                                        const Pos& p) {
        const Type* T = classOf(tag);
        if (!T || fn.params.size() != 1 || !fn.params[0].type) return nullptr;
        const Type* a = fn.params[0].type.get();
        if (a->kind != Type::Kind::Reference || !a->inner || a->inner->kind != Type::Kind::Struct || a->inner->tag != tag)
            return nullptr;
        auto f = cloneType(fn);
        f->params[0].name = "__o";
        auto decl = newMethodDecl(tag, mangled, *f, f->params, p);
        decl->body = memberwiseAssign(*T, a->rref, p);
        return decl;
    }

    // The class of a field: the field's own type, or the element type of a one-level array of a
    // class with a known length (Ring lines[8]).
    const Type* arrayElemClass(const Type* t) {
        if (!t) return nullptr;
        if (t->kind == Type::Kind::Struct && !t->tag.empty()) return t;
        if (t->kind == Type::Kind::Array && t->inner && t->inner->kind == Type::Kind::Struct && !t->inner->tag.empty() &&
            t->bound && t->bound->kind == Expr::Kind::Int)
            return t->inner.get();
        return nullptr;
    }
    // for (i = 0; i < n; ++i) call(&this->field[i])  (from n - 1 down when reverse)
    template <class F>
    std::unique_ptr<Stmt> fieldLoop(const std::string& field, std::uint64_t n, bool reverse, const Pos& p, F call) {
        return indexLoop([&] { return eArrow(eName("this", p), field, p); }, n, reverse, p,
                         [&](std::unique_ptr<Expr> at) { return call(eAddr(std::move(at), p)); });
    }

    void finishClass(ClassCtx& c) {
        Type& T = *c.t;
        Type* B = T.base.empty() ? nullptr : classOf(T.base);
        bool needCtor = !c.ctors.empty() || c.tmplCtor || T.hasVptr || (B && B->needCtor);
        if (dmi_.count(c.tag) && !dmi_[c.tag].empty()) needCtor = true;
        bool needDtor = c.dtorDeclared || (B && B->needDtor);
        for (auto& f : ownFields(T)) {
            if (closureField(f.second)) needDtor = true;  // (its destructor, if any, is the checker's)
            const Type* el = arrayElemClass(f.second);
            if (el) {
                Type* m = classOf(el->tag);
                if (m && m->needCtor) needCtor = true;
                if (m && m->needDtor) needDtor = true;
                if (m && (m->nonpod || m->needCtor || m->needDtor)) T.nonpod = true;
            }
        }
        T.needCtor = needCtor;
        T.needDtor = needDtor;
        if (!c.ctors.empty() || c.tmplCtor || c.dtorDeclared || T.hasVptr || needCtor || needDtor) T.nonpod = true;
        Token at;
        at.line = c.dtorPos.line;
        at.column = c.dtorPos.col;
        at.file = c.dtorPos.file;
        Pos p = c.dtorPos;
        if (c.ctors.empty() && needCtor) {
            CtorDef d;
            d.hasBody = true;
            d.pos = p;
            Param cp;
            cp.name = "<ctor>";
            auto fn = fnTypeOf({});
            cp.mangled = mangleSpecial(c.tag, "C1", *fn);
            cp.type = std::move(fn);
            T.params.push_back(std::move(cp));
            c.ctors.push_back(std::move(d));
        }
        for (CtorDef& d : c.ctors)
            if (d.defaulted) defaultCopyCtor(d, c.tag, T);
        // The implicit copy constructor: a class with no copy constructor of its own whose base or
        // member has one copies the base and each member with them, an array member element by
        // element (memberwiseInits). (Not made when the base has no copy constructor.)
        {
            bool deletedPart = false;  // a base or member whose copy constructor is deleted
            auto hasCopy = [&](const Type* cls) {
                if (!cls) return false;
                bool found = false;
                for (const Param& q : cls->params) {
                    if (q.name != "<ctor>" || !q.type || q.type->params.size() != 1) continue;
                    const Type* a = q.type->params[0].type.get();
                    if (a && a->kind == Type::Kind::Reference && a->inner && a->inner->kind == Type::Kind::Struct &&
                        a->inner->tag == cls->tag)
                        found = true;
                }
                for (const Param& q : cls->params)
                    if (q.name == "<ctor>" && deletedFns_.count(q.mangled) && q.type && q.type->params.size() == 1 &&
                        q.type->params[0].type && !q.type->params[0].type->rref)
                        deletedPart = true;
                return found;
            };
            bool userCopy = false;
            for (const Param& q : T.params) {
                if (q.name != "<ctor>" || !q.type || q.type->params.size() != 1) continue;
                const Type* a = q.type->params[0].type.get();
                if (a && a->kind == Type::Kind::Reference && a->inner && a->inner->tag == c.tag) userCopy = true;
            }
            bool parts = B && hasCopy(B);
            bool ok = !B || hasCopy(B);
            for (auto& f : ownFields(T)) {
                if (!f.second) continue;
                if (f.second->kind == Type::Kind::Array) {
                    // Curve loc[3] with Curve copied by its copy constructor: each element is
                    // (it was a copy of the bytes, two objects owning one buffer).
                    const Type* el = f.second;
                    while (el && el->kind == Type::Kind::Array && el->inner) el = el->inner.get();
                    if (el && el->kind == Type::Kind::Struct && !el->tag.empty() && hasCopy(classOf(el->tag)))
                        parts = true;
                    continue;
                }
                if (closureField(f.second)) parts = true;
                if (f.second->kind == Type::Kind::Struct && !f.second->tag.empty()) {
                    Type* m = classOf(f.second->tag);
                    if (m && hasCopy(m)) parts = true;
                    // A member class with constructors but no copy constructor is copied by its
                    // implicit copy, the bytes ([class.copy.ctor] p14), as in T x = y.
                }
            }
            // With a part whose copy constructor is deleted the class's own copy constructor is
            // deleted too ([class.copy.ctor] p10); its implicit move constructor is made instead,
            // moving each part (it used to be a copy constructor calling the deleted one).
            const bool asMove = deletedPart;
            if (!userCopy && parts && ok) {
                CtorDef d;
                d.pos = p;
                Param op;
                op.name = "__o";
                auto refT = ty(Type::Kind::Reference);
                refT->rref = asMove;
                refT->inner = ty(Type::Kind::Struct);
                refT->inner->tag = c.tag;
                // C(const C &): a const object is copied too ([class.copy.ctor] p7)
                refT->inner->cnst = !asMove;
                op.type = std::move(refT);
                d.params.push_back(std::move(op));
                memberwiseInits(d, T, asMove, p);
                Param cp;
                cp.name = "<ctor>";
                auto fn = fnTypeOf(d.params);
                cp.mangled = mangleSpecial(c.tag, "C1", *fn);
                cp.type = std::move(fn);
                T.params.push_back(std::move(cp));
                c.ctors.push_back(std::move(d));
            }
        }
        implicitAssign(c, T, B, p);
        bool baseVirt = basesVirtualDtor(T);
        if (!c.dtorDeclared && needDtor) {
            registerDtor(c, baseVirt, at);
            c.dtorInline = true;
        }
    }

    // __vnew_<T>(n) and __vdel_<T>(p): new T[n] and delete[] p for a class with a constructor or
    // destructor. The block has an 8-byte count in front when the destructor must run per element.
    //   T *__vnew_T(long n) { char *raw; long i; T *p; raw = malloc(n * sizeof(T) + 8); *(long *)raw = n;
    //     p = (T *)(raw + 8); for (i = 0; i < n; i = i + 1) ctor(&p[i]); return p; }
    //   void __vdel_T(T *p) { long i; if (p) { for (i = *(long *)((char *)p - 8) - 1; i >= 0; i = i - 1)
    //     dtor(&p[i]); free((char *)p - 8); } }
    void emitVectorHelpers(ClassCtx& c, Type* T) {
        if (!T->needCtor && !T->needDtor) return;
        Pos p = c.dtorPos;
        auto longT = [&]() { return ty(Type::Kind::Long); };
        auto charPtr = [&]() {
            auto cp = ty(Type::Kind::Pointer);
            cp->inner = ty(Type::Kind::Char);
            return cp;
        };
        auto cast = [&](std::unique_ptr<Type> to, std::unique_ptr<Expr> x) {
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::Cast;
            e->line = p.line;
            e->col = p.col;
            e->file = p.file;
            e->ty = std::move(to);
            e->kids.push_back(std::move(x));
            return e;
        };
        auto deref = [&](std::unique_ptr<Expr> x) {
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::Unary;
            e->text = "*";
            e->line = p.line;
            e->col = p.col;
            e->file = p.file;
            e->kids.push_back(std::move(x));
            return e;
        };
        auto index = [&](const std::string& arr, const std::string& i) {
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::Index;
            e->line = p.line;
            e->col = p.col;
            e->file = p.file;
            e->kids.push_back(eName(arr, p));
            e->kids.push_back(eName(i, p));
            return e;
        };
        auto local = [&](const std::string& n, std::unique_ptr<Type> t) {
            auto d = std::make_unique<Decl>();
            d->name = n;
            d->line = p.line;
            d->col = p.col;
            d->file = p.file;
            d->type = std::move(t);
            return declStmt(std::move(d));
        };
        auto loop = [&](bool down, std::unique_ptr<Expr> start, std::unique_ptr<Expr> call) {
            auto l = std::make_unique<Stmt>();
            l->kind = Stmt::Kind::For;
            l->line = p.line;
            l->col = p.col;
            l->file = p.file;
            l->init = sExpr(eAssign(eName("i", p), std::move(start), p), p);
            l->expr = down ? eBin(">=", eName("i", p), eInt(0, p), p) : eBin("<", eName("i", p), eName("n", p), p);
            l->expr2 = eAssign(eName("i", p), eBin(down ? "-" : "+", eName("i", p), eInt(1, p), p), p);
            l->body = sExpr(std::move(call), p);
            return l;
        };
        const std::string mn = mangleNested(c.tag);
        const bool cookie = T->needDtor;
        auto elemPtr = [&]() { return ptrToTag(c.tag); };
        auto mk = [&](const std::string& name, std::vector<Param> params, std::unique_ptr<Type> ret,
                      std::unique_ptr<Stmt> body) {
            auto fn = fnTypeOf(params);
            fn->inner = std::move(ret);
            auto d = std::make_unique<Decl>();
            d->name = name;
            d->line = p.line;
            d->col = p.col;
            d->file = p.file;
            d->type = std::move(fn);
            d->function = true;
            d->body = std::move(body);
            extras_.push_back(std::move(d));
        };
        bool defaultCtor = !T->needCtor;
        for (const Param& q : T->params)
            if (q.name == "<ctor>" && q.type && q.type->params.empty()) defaultCtor = true;
        if (defaultCtor) {
            auto body = sBlock(p);
            body->stmts.push_back(local("raw", charPtr()));
            body->stmts.push_back(local("i", longT()));
            body->stmts.push_back(local("q", elemPtr()));
            auto size = std::make_unique<Expr>();
            size->kind = Expr::Kind::Unary;
            size->text = "sizeof";
            size->line = p.line;
            size->col = p.col;
            size->file = p.file;
            size->ty = ty(Type::Kind::Struct);
            size->ty->tag = c.tag;
            std::unique_ptr<Expr> bytes = eBin("*", eName("n", p), std::move(size), p);
            if (cookie) bytes = eBin("+", std::move(bytes), eInt(8, p), p);
            std::vector<std::unique_ptr<Expr>> margs;
            margs.push_back(std::move(bytes));
            body->stmts.push_back(sExpr(eAssign(eName("raw", p), cast(charPtr(), eCall(eName("malloc", p), std::move(margs), p)), p), p));
            if (cookie) body->stmts.push_back(sExpr(eAssign(deref(cast([&]() { auto lp = ty(Type::Kind::Pointer); lp->inner = longT(); return lp; }(), eName("raw", p))), eName("n", p), p), p));
            std::unique_ptr<Expr> base = eName("raw", p);
            if (cookie) base = eBin("+", std::move(base), eInt(8, p), p);
            body->stmts.push_back(sExpr(eAssign(eName("q", p), cast(elemPtr(), std::move(base)), p), p));
            if (T->needCtor)
                body->stmts.push_back(loop(false, eInt(0, p),
                                           ctorCall(c.tag, eAddr(index("q", "i"), p), {}, p)));
            auto ret = std::make_unique<Stmt>();
            ret->kind = Stmt::Kind::Return;
            ret->line = p.line;
            ret->col = p.col;
            ret->file = p.file;
            ret->expr = eName("q", p);
            body->stmts.push_back(std::move(ret));
            Param np;
            np.name = "n";
            np.type = longT();
            std::vector<Param> params;
            params.push_back(std::move(np));
            mk("__vnew_" + mn, std::move(params), elemPtr(), std::move(body));
        }
        if (T->needDtor) {
            auto body = sBlock(p);
            body->stmts.push_back(local("i", longT()));
            auto work = sBlock(p);
            auto lp = ty(Type::Kind::Pointer);
            lp->inner = longT();
            auto countAt = deref(cast(std::move(lp), eBin("-", cast(charPtr(), eName("p", p)), eInt(8, p), p)));
            work->stmts.push_back(loop(true, eBin("-", std::move(countAt), eInt(1, p), p),
                                       dtorCall(c.tag, eAddr(index("p", "i"), p), p)));
            std::vector<std::unique_ptr<Expr>> fargs;
            fargs.push_back(eBin("-", cast(charPtr(), eName("p", p)), eInt(8, p), p));
            work->stmts.push_back(sExpr(eCall(eName("free", p), std::move(fargs), p), p));
            auto guard = std::make_unique<Stmt>();
            guard->kind = Stmt::Kind::If;
            guard->line = p.line;
            guard->col = p.col;
            guard->file = p.file;
            guard->expr = eName("p", p);
            guard->thenS = std::move(work);
            body->stmts.push_back(std::move(guard));
            Param pp;
            pp.name = "p";
            pp.type = elemPtr();
            std::vector<Param> params;
            params.push_back(std::move(pp));
            mk("__vdel_" + mn, std::move(params), ty(Type::Kind::Void), std::move(body));
        }
    }

    // Runs after the finished class is stored, so the builders can look it up by tag.
    void emitClassDecls(ClassCtx& c) {
        Type* T = classOf(c.tag);
        if (!T) return;
        if (const Param* dp = special(T, "<dtor>"); dp && !c.dtorInline) {
            // ~C(); with the body elsewhere: a prototype, so the array helpers can call it.
            auto fn = fnTypeOf({});
            extras_.push_back(newMethodDecl(c.tag, dp->mangled, *fn, {}, c.dtorPos));
        }
        for (CtorDef& d : c.ctors) {
            if (d.hasBody) continue;
            // C(); with the body elsewhere: a prototype, so the array helpers can call it.
            auto fn = fnTypeOf(d.params);
            extras_.push_back(newMethodDecl(c.tag, mangleSpecial(c.tag, "C1", *fn), *fn, d.params, d.pos));
        }
        for (DefaultedAssign& da : c.defaultAssigns)
            if (auto decl = defaultAssign(c.tag, da.mangled, *da.fn, da.pos)) extras_.push_back(std::move(decl));
        emitVectorHelpers(c, T);
        for (CtorDef& d : c.ctors) {
            if (!d.hasBody) continue;
            auto decl = buildCtor(c.tag, d);
            if (decl) extras_.push_back(std::move(decl));
        }
        const Param* d1 = special(T, "<dtor>");
        if (d1 && c.dtorInline) {
            auto decl = buildDtor(c.tag, std::move(c.dtorBody), c.dtorPos, false);
            if (decl) extras_.push_back(std::move(decl));
        }
        if (d1 && d1->vslot >= 0) {
            auto decl = buildDtor(c.tag, nullptr, c.dtorPos, true);
            if (decl) extras_.push_back(std::move(decl));
        }
        if (T->hasVptr) {
            auto vd = std::make_unique<Decl>();
            vd->name = vtName(c.tag);
            vd->line = c.dtorPos.line;
            vd->col = c.dtorPos.col;
            vd->file = c.dtorPos.file;
            auto arr = ty(Type::Kind::Array);
            arr->inner = voidPtr();
            auto bound = std::make_unique<Expr>();
            bound->kind = Expr::Kind::Int;
            bound->ival = T->vslots.size() + 2;
            arr->bound = std::move(bound);
            vd->type = std::move(arr);
            // offset-to-top, then the class's type_info object ([expr.typeid], Itanium ABI 2.9.5)
            Type cls;
            cls.kind = Type::Kind::Struct;
            cls.tag = c.tag;
            vd->vt.push_back("");
            vd->vt.push_back(typeInfoSym(cls));
            for (const std::string& sname : T->vslots) vd->vt.push_back(sname);
            std::vector<std::unique_ptr<Decl>> thunks;
            otherVtables(c.tag, *T, typeInfoSym(cls), vd->vt, thunks, c.dtorPos);
            vd->type->bound->ival = vd->vt.size();
            makeTypeInfo(cls, extras_, tiDone_, vd->line, vd->col, vd->file);
            extras_.push_back(std::move(vd));
            for (auto& th : thunks) extras_.push_back(std::move(th));
        }
    }

    std::unique_ptr<Type> parseStructType() {
        struct Depth {
            int& d;
            explicit Depth(int& x) : d(x) { ++d; }
            ~Depth() { --d; }
        } depth(classBodies_);
        Token at = cur();
        bool isUnion = at.text == "union";
        bool isClass = at.text == "class";
        advance();
        std::string tag;
        if (!atEnd() && cur().kind == Tok::Ident) {
            tag = cur().text;
            advance();
            while (eatP("::")) {
                if (atEnd() || cur().kind != Tok::Ident) {
                    err("expected a struct name");
                    break;
                }
                tag += "::";
                tag += cur().text;
                advance();
            }
            if (specSkipTo_ && i_ == specSkipFrom_) i_ = specSkipTo_;  // L<float> of a specialization
            if (tag.find("::") == std::string::npos) {
                // A class defined in a class is a member of it ([class.nest]): Outer::Inner, so its
                // member functions find the outer class's names.
                // Only a definition (struct Inner { or struct Inner :) or a name already declared
                // there is nested; struct Outer *next inside Outer is the outer class itself.
                Type* outer = classStack_.empty() ? nullptr : classStack_.back();
                bool nest = outer && !outer->tag.empty() && instOverrideName_.empty() && outer->tag != tag &&
                            shortName(outer->tag) != tag &&
                            ((!atEnd() && (isP("{") || isP(":") || isKw("final") || (isP(";") && bodyDepth_ == 0))) ||
                             structs_.count(outer->tag + "::" + tag));
                tag = nest ? outer->tag + "::" + tag : qualify(tag);
            } else if (!structs_.count(tag) && structs_.count(qualify(tag))) {
                tag = qualify(tag);  // struct Dec::Impl { .. } inside Dec's namespace: n::Dec::Impl
            } else if (!structs_.count(tag) && !ns_.empty() && (isP("{") || isP(":"))) {
                // class P::It { .. } in P's namespace after "class It;" in P: n::P::It.
                std::string outer = classPrefix(tag.substr(0, tag.rfind("::")));
                if (!outer.empty()) tag = outer + tag.substr(tag.rfind("::"));
            }
            if (tag.find("::") != std::string::npos && (isP("{") || isP(":")) && classScopes_ == 0) {
                // struct Jobs::Impl { ErrorHandler h; }: a nested class defined after its class
                // sees the enclosing class's member types ([class.nest], [basic.lookup.unqual] p7).
                std::string outer = classPrefix(tag.substr(0, tag.rfind("::")));
                if (!outer.empty()) enterMemberScope(outer);
            }
        }
        std::string tplShort;
        if (!tag.empty() && bodyDepth_ > 0 && classStack_.empty() && (isP("{") || isP(":")) && structs_.count(tag) &&
            structs_[tag] && !structs_[tag]->params.empty()) {
            // A local class ([class.local]) whose name another function's local class already has
            // (each instance of a template defines its own): a tag of its own, the name as written.
            tplShort = shortName(tag);
            tag += "__local" + std::to_string(localClassN_++);
        }
        if (tag.empty() && !isUnion && (bodyDepth_ > 0 || !classStack_.empty()) && isP("{") && [&] {
                // only one with a declarator after its body (an anonymous member, struct { .. };,
                // keeps its members in the enclosing class)
                int braces = 0;
                for (std::size_t k = i_; k < toks_.size(); ++k) {
                    if (isP(toks_[k], "{")) ++braces;
                    else if (isP(toks_[k], "}") && --braces == 0)
                        return k + 1 < toks_.size() && (toks_[k + 1].kind == Tok::Ident || isP(toks_[k + 1], "*") ||
                                                         isP(toks_[k + 1], "&"));
                }
                return false;
            }()) {
            // An unnamed class defined in a C++ function (struct { T &r; } cv{x}; in
            // Render/Mesh.hpp) or class (struct { int a; } s{1}; a member): a hidden name, so it is a class like any other and a braced
            // initializer initializes its members ([dcl.init.aggr]). (In C, unnamed structs keep
            // the C rules.)
            const std::string& f = at.file;
            bool cSource = cSourceFile(f);
            if (!cSource && !f.empty()) tag = "__unnamed" + std::to_string(localClassN_++);
        }
        if (!instOverrideName_.empty() && (tag == instOverrideName_ || shortName(tag) == instOverrideName_)) {
            tplShort = instOverrideName_;
            tag = instOverrideTag_;
            instOverrideName_.clear();
        }
        if (!tag.empty() && isKw("final")) {
            finalTags_.insert(tag);  // class C final : B (not enforced; std::is_final reads it)
            advance();
        }
        std::string baseTag;
        bool preDeclared = false;
        if (!tag.empty() && !isUnion && isP(":") && !structs_.count(tag)) {
            // The class name is declared before its base clause ([basic.scope.pdecl] p6), so a base
            // may name it: struct T : std::enable_shared_from_this<T>.
            auto inc = ty(Type::Kind::Struct);
            inc->tag = tag;
            structs_[tag] = cloneType(*inc);
            if (namesNoClassHere(tag)) addTypedef(shortName(tag), cloneType(*inc));
            preDeclared = true;
        }
        std::vector<std::string> moreBases;  // the bases after the first ([class.mi])
        if (!tag.empty() && !isUnion && isP(":")) {
          advance();
          for (;;) {
            while (isKw("public") || isKw("private") || isKw("protected") || isKw("virtual")) advance();
            std::string bn;
            std::size_t bnAt = 0;
            std::string one;
            if (!atEnd() && cur().kind == Tok::Ident && nextIs("::")) {
                // N::B<T> as a base (std::enable_shared_from_this<C>): the template after its namespaces.
                std::size_t ti = tmplAfterNs(i_, true);
                if (ti && ti + 1 < toks_.size() && isP(toks_[ti + 1], "<")) i_ = ti;
            }
            if (!atEnd() && cur().kind == Tok::Ident && ctmpl_.count(cur().text) && nextIs("<")) {
                auto bt = instantiateClass(cur().text);
                if (bt && !bt->tag.empty() && classOf(bt->tag)) one = bt->tag;
                else err("unknown base class");
            } else if (!atEnd() && cur().kind == Tok::Ident) {
                bnAt = i_;
                bn = cur().text;
                advance();
                while (eatP("::")) {
                    if (atEnd() || cur().kind != Tok::Ident) break;
                    bn += "::";
                    bn += cur().text;
                    advance();
                }
            }
            const Type* td = findTypedef(bn);
            std::size_t qend = 0;
            if (!td && bn.find("::") != std::string::npos) td = qualTypeAt(bnAt, qend);  // std::true_type
            if (!one.empty()) {
            } else if (classOf(qualify(bn))) one = qualify(bn);
            else if (classOf(bn)) one = bn;
            else if (td && !td->tag.empty() && classOf(td->tag)) one = td->tag;
            else if (!classPrefix(bn).empty()) one = classPrefix(bn);  // Character::PoseSource, a nested class
            else err("unknown base class '" + bn + "'");
            if (!one.empty()) {
                if (baseTag.empty()) baseTag = one;
                else moreBases.push_back(one);
            }
            if (!eatP(",")) break;
          }
          // Multiple bases (Itanium C++ ABI 2.4 II): the primary base is the first one with a vtable
          // (else the first); it is the start of the object, its members are this class's. Each
          // other base is a subobject, a member named "__mb:<tag>" placed after it in order of
          // declaration; its members are reached through that member.
          if (!moreBases.empty() && classOf(baseTag) && !classOf(baseTag)->hasVptr) {
              for (std::size_t k = 0; k < moreBases.size(); ++k)
                  if (classOf(moreBases[k]) && classOf(moreBases[k])->hasVptr) {
                      std::swap(baseTag, moreBases[k]);
                      std::rotate(moreBases.begin(), moreBases.begin() + static_cast<std::ptrdiff_t>(k),
                                  moreBases.begin() + static_cast<std::ptrdiff_t>(k) + 1);
                      break;
                  }
          }
        }
        if (!eatP("{")) {
            if (tag.empty()) {
                err("expected a struct name");
                return ty(Type::Kind::Int);
            }
            auto it = structs_.find(tag);
            if (it == structs_.end() || !it->second) {
                // An elaborated type specifier that names no class yet declares one
                // ([dcl.type.elab], [basic.scope.pdecl]): `class P;` or `struct P *p`. The class is
                // incomplete until its definition; uses by pointer or reference need only the tag
                // (the checker finds the finished class by tag). In C++ the name is also a type name.
                auto inc = ty(isUnion ? Type::Kind::Union : Type::Kind::Struct);
                inc->tag = tag;
                structs_[tag] = cloneType(*inc);
                const std::string& f = at.file;
                bool cSource = cSourceFile(f);
                if (!isUnion && (isClass || !ns_.empty() || !cSource) && namesNoClassHere(tag))
                    addTypedef(shortName(tag), cloneType(*inc));
                return inc;
            }
            {
                // The class was first named where its name was not left behind as a type name (in
                // a parameter list: void f(class Camera *c); [basic.scope.pdecl] p7): `class
                // Camera;` here makes the name a type name in this scope.
                const std::string& f = at.file;
                bool cSource = cSourceFile(f);
                if (!isUnion && !cSource && isP(";") && !findTypedef(shortName(tag)))
                    addTypedef(shortName(tag), cloneType(*it->second));
            }
            return cloneType(*it->second);
        }
        auto t = ty(isUnion ? Type::Kind::Union : Type::Kind::Struct);
        t->tag = tag;
        bool completing = preDeclared;
        if (!tag.empty() && !structs_.count(tag)) {
            auto inc = ty(isUnion ? Type::Kind::Union : Type::Kind::Struct);
            inc->tag = tag;
            structs_[tag] = std::move(inc);
            completing = true;
        }
        // Everything made from the class body (member functions defined in it, implicit
        // constructors and destructors, the vtable) is inline ([class.mfct], [dcl.inline]): a
        // header with the class may be included by many files, so these are weak symbols.
        std::size_t inlineMark = extras_.size();
        struct MarkInline {
            std::vector<std::unique_ptr<Decl>>& v;
            std::size_t from;
            ~MarkInline() {
                for (std::size_t k = from; k < v.size(); ++k)
                    if (v[k]) v[k]->isInline = true;
            }
        } markInline{extras_, inlineMark};
        struct ClassScope {
            std::vector<Type*>& v;
            ~ClassScope() { v.pop_back(); }
        } classScope{classStack_};
        classStack_.push_back(nullptr);
        ClassCtx cx;
        cx.tag = tag;
        cx.t = t.get();
        classStack_.back() = t.get();
        cx.dtorPos.line = at.line;
        cx.dtorPos.col = at.column;
        cx.dtorPos.file = at.file;
        bool classy = !tag.empty() && !isUnion;
        std::string shortTag = tplShort.empty() ? shortName(tag) : tplShort;
        bool pub = !isClass;
        // Member function bodies defined in the class are read after the class is complete
        // ([class.mem] p7: a function body is a complete-class context), so they see members and
        // nested types declared after them, and the class's own constructors (new C(..) in a
        // member function of C). Each is remembered by where its '{' is and read at the end.
        std::vector<DeferredBody> deferred;
        auto deferBody = [&](int kind, std::size_t index, Decl* decl, const Type* ret) {
            DeferredBody db;
            db.at = i_;
            db.kind = kind;
            db.index = index;
            db.decl = decl;
            db.ret = ret ? cloneType(*ret) : ty(Type::Kind::Void);
            db.params = lastParams_;
            lastParams_.clear();
            skipBraces();
            deferred.push_back(std::move(db));
        };
        // Static member function bodies are deferred the same way (staticMember).
        struct DeferGuard {
            std::vector<DeferredBody>*& p;
            std::vector<DeferredBody>* old;
            ~DeferGuard() { p = old; }
        } deferGuard{staticDefer_, staticDefer_};
        staticDefer_ = &deferred;
        if (classy) cx.cxx = true;
        // The class's own scope for its member typedefs and aliases ([basic.scope.class]): they
        // are found inside the class and as Class::name ([class.qual]), not after the class by
        // their bare name. The scope is popped when the class is done.
        if (classScopes_ == 0) classOuter_ = tscopes_.empty() ? 0 : tscopes_.size() - 1;
        if (tscopes_.empty()) tscopes_.emplace_back();
        tscopes_.emplace_back();
        ++classScopes_;
        struct ClassTypes {
            Parser& p;
            ~ClassTypes() {
                if (!p.tscopes_.empty()) p.tscopes_.pop_back();
                --p.classScopes_;
            }
        } classTypes{*this};
        std::vector<std::string> allBases;
        if (!baseTag.empty()) allBases.push_back(baseTag);
        allBases.insert(allBases.end(), moreBases.begin(), moreBases.end());
        for (const std::string& baseOne : allBases) {
            if (tag.empty()) break;
            // The base class's member types are members of this class too.
            std::string bp = baseOne + "::";
            std::vector<std::pair<std::string, std::unique_ptr<Type>>> inherited;
            for (auto it = memberTypes_.lower_bound(bp); it != memberTypes_.end() && it->first.compare(0, bp.size(), bp) == 0; ++it)
                if (it->second && it->first.find("::", bp.size()) == std::string::npos)
                    inherited.emplace_back(it->first.substr(bp.size()), cloneType(*it->second));
            for (auto& in : inherited) {
                memberTypes_[tag + "::" + in.first] = cloneType(*in.second);
                tscopes_.back()[in.first] = std::move(in.second);
            }
        }
        if (!baseTag.empty()) {
            Type* B = classOf(baseTag);
            for (const Param& p : B->params) {
                if (isSpecialName(p.name)) continue;
                Param c;
                c.name = p.name;
                c.bits = p.bits;
                c.mangled = p.mangled;
                c.vslot = p.vslot;
                c.cmethod = p.cmethod;  // a const virtual is overridden only by a const one
                c.pure = p.pure;
                if (p.type) c.type = cloneType(*p.type);
                t->params.push_back(std::move(c));
            }
            if (!B->nonpod) {
                Param padp;
                padp.name = "__basepad";
                padp.type = cloneType(*B);
                t->params.push_back(std::move(padp));
            }
            t->base = baseTag;
            t->nonpod = true;
            t->hasVptr = B->hasVptr;
            t->vslots = B->vslots;
            t->needCtor = B->needCtor;
            t->needDtor = B->needDtor;
        }
        for (const std::string& mb : moreBases) {
            Type* S = classOf(mb);
            if (!S) continue;
            Param sub;
            sub.name = "__mb:" + mb;
            sub.type = ty(Type::Kind::Struct);
            sub.type->tag = mb;
            t->params.push_back(std::move(sub));
            t->nonpod = true;
            t->needCtor = t->needCtor || S->needCtor || S->hasVptr;
            t->needDtor = t->needDtor || S->needDtor;
            cx.cxx = true;
        }
        if (cx.cxx) {
            auto ph = ty(Type::Kind::Struct);
            ph->tag = tag;
            addTypedef(shortTag, std::move(ph));
            implicitTags_.insert(shortTag);
        }
        while (!atEnd() && !isP("}")) {
            if (isKw("using") && inheritCtors(cx, *t)) continue;
            if (isKw("using")) {
                parseUsing();
                continue;
            }
            if (isKw("typedef")) {
                // A member typedef ([dcl.typedef], [class.mem]): typedef pair<const K, V> value_type;
                // the name is known in the rest of the class, as a 'using' alias is.
                advance();
                auto base = parseBase();
                for (;;) {
                    Parsed d = parseDeclarator(true);
                    auto mt = apply(cloneType(*base), d.mods);
                    if (!tag.empty()) memberTypes_[tag + "::" + d.name] = cloneType(*mt);
                    addTypedef(d.name, std::move(mt));
                    if (!eatP(",")) break;
                }
                expectP(";", ";");
                continue;
            }
            if (isKw("template")) {
                std::string savedTag = memberTag_;
                std::string savedShort = memberShort_;
                Type* savedCls = memberCls_;
                memberTag_ = tag;
                memberShort_ = shortTag;
                memberCls_ = t.get();
                if (parseTemplate()) {
                    userCtors_.insert(tag);
                    cx.tmplCtor = true;
                }
                memberTag_ = savedTag;
                memberShort_ = savedShort;
                memberCls_ = savedCls;
                cx.cxx = true;
                continue;
            }
            if (parseStaticAssert()) continue;
            if ((isKw("public") || isKw("private") || isKw("protected")) && nextIs(":")) {
                pub = isKw("public");
                advance();
                advance();
                continue;
            }
            bool isStaticMember = false;
            for (;;) {
                if (eatKw("static")) isStaticMember = true;
                else if (eatKw("inline") || eatKw("constexpr") || eatKw("explicit") || eatKw("mutable")) {
                } else break;
            }
            bool isVirtual = false;
            if (classy && isKw("virtual")) {
                isVirtual = true;
                advance();
                cx.cxx = true;
            }
            if (classy && isP("~")) {
                Token dt = cur();
                advance();
                if (!atEnd() && cur().kind == Tok::Ident && cur().text == shortTag) advance();
                else err("expected the class name after '~'");
                expectP("(", "(");
                expectP(")", ")");
                while (isKw("noexcept") || isKw("override") || isKw("final")) advance();
                cx.cxx = true;
                if (cx.dtorDeclared) err("a class has one destructor");
                cx.dtorDeclared = true;
                cx.dtorPos.line = dt.line;
                cx.dtorPos.col = dt.column;
                cx.dtorPos.file = dt.file;
                bool baseVirt = basesVirtualDtor(*t);
                registerDtor(cx, isVirtual || baseVirt, dt);
                if (isP("{")) {
                    deferBody(0, 0, nullptr, nullptr);
                    cx.dtorInline = true;
                } else if (eatP("=")) {
                    if (isKw("default")) cx.dtorInline = true;
                    advance();
                    expectP(";", ";");
                    if (cx.dtorInline && !cx.dtorBody) cx.dtorBody = sBlock(cx.dtorPos);
                } else {
                    expectP(";", ";");
                }
                continue;
            }
            if (classy && !atEnd() && cur().kind == Tok::Ident && cur().text == shortTag && nextIs("(")) {
                CtorDef cd;
                cd.pos = here();
                advance();
                advance();
                parseParams(cd.params);
                cx.cxx = true;
                skipNoexcept();  // C(C&&) noexcept : ... { }
                parseInitList(cd);
                Param cp;
                cp.name = "<ctor>";
                auto fn = fnTypeOf(cd.params);
                cp.mangled = mangleSpecial(tag, "C1", *fn);
                cp.type = std::move(fn);
                t->params.push_back(std::move(cp));
                if (isP("{")) {
                    deferBody(1, cx.ctors.size(), nullptr, nullptr);
                    cd.hasBody = true;
                } else if (eatP("=")) {
                    if (isKw("default")) {
                        cd.hasBody = true;
                        cd.defaulted = true;
                        cd.body = sBlock(cd.pos);
                    } else if (isKw("delete")) {
                        deletedFns_.insert(t->params.back().mangled);  // C(const C&) = delete
                    }
                    advance();
                    expectP(";", ";");
                } else {
                    expectP(";", ";");
                }
                cx.ctors.push_back(std::move(cd));
                userCtors_.insert(tag);
                continue;
            }
            Parsed d;
            std::unique_ptr<Type> fieldType;
            std::unique_ptr<Type> fieldBase;  // for "int a, b;": the type before the first declarator
            if (!atEnd() && cur().kind == Tok::Ident && cur().text == "operator" && i_ + 1 < toks_.size() &&
                isTypeKw(toks_[i_ + 1])) {
                // A conversion function: operator int() const { ... } (ISO/IEC 14882 [class.conv.fct]).
                d.line = cur().line;
                d.col = cur().column;
                d.file = cur().file;
                advance();
                auto target = parseBase();
                while (isP("*") || isP("&")) {
                    // operator T *() and operator T &() ([class.conv.fct]: a conversion-type-id
                    // may end in pointer and reference operators).
                    auto w = ty(isP("*") ? Type::Kind::Pointer : Type::Kind::Reference);
                    advance();
                    while (eatKw("const")) {
                    }
                    w->inner = std::move(target);
                    target = std::move(w);
                }
                expectP("(", "(");
                expectP(")", ")");
                d.name = "operator " + mangleType(*target);
                fieldType = fnTypeOf({});
                fieldType->inner = std::move(target);
            } else {
                if (!isTypeKw(cur())) {
                    err("expected a field");
                    break;
                }
                auto base = parseBase();
                if (enumDef_ && isP(";")) {
                    // enum E { ... }; as a member: only its names
                    enumDef_ = false;
                    advance();
                    continue;
                }
                if (base && (base->kind == Type::Kind::Struct || base->kind == Type::Kind::Union) && isP(";") &&
                    base->tag.empty() && !base->params.empty()) {
                    // An anonymous union or struct member, union { bool b; int64_t i; }; ([class.union.anon]
                    // p1, C11 6.7.2.1 p13): a member with a hidden name, whose members the checker finds
                    // as members of the class.
                    advance();
                    Param anon;
                    anon.name = "__anon" + std::to_string(newN_++);
                    anon.type = std::move(base);
                    t->params.push_back(std::move(anon));
                    continue;
                }
                if (base && (base->kind == Type::Kind::Struct || base->kind == Type::Kind::Union) && isP(";")) {
                    // A nested class: struct Inner { ... }; (its name is kept at namespace scope)
                    advance();
                    continue;
                }
                enumDef_ = false;
                if (base) fieldBase = cloneType(*base);
                d = parseDeclarator(true);
                fieldType = apply(std::move(base), d.mods);
            }
            if (isStaticMember && !tag.empty() && !isUnion) {
                staticMember(tag, d, std::move(fieldType), fieldBase.get());
                continue;
            }
            if (fieldType && fieldType->kind == Type::Kind::Function && !tag.empty() && !isUnion) {
                cx.cxx = true;
                if (cx.cxx && !classy) classy = true;
                bool pure = false;
                bool cmethod = false;
                bool defaulted = false;
                while (isKw("const") || isKw("override") || isKw("final") || isKw("noexcept")) {
                    if (isKw("const")) cmethod = true;
                    if (isKw("noexcept")) skipNoexcept();  // noexcept(true) too
                    else advance();
                }
                if (isP("=")) {
                    advance();
                    if (!atEnd() && cur().kind == Tok::Int && cur().uval == 0) pure = true;
                    if (isKw("default")) defaulted = true;
                    advance();
                }
                Param field;
                field.name = d.name;
                field.mangled = constMangle(mangleFunc(tag + "::" + d.name, *fieldType), cmethod);
                field.cmethod = cmethod;
                field.type = cloneType(*fieldType);
                int slot = -1;
                bool virt = isVirtual;
                for (std::size_t j = 0; j < t->params.size(); ++j) {
                    const Param& ex = t->params[j];
                    if (ex.name != d.name || ex.mangled.empty() || isSpecialName(ex.name) || !ex.type) continue;
                    if (!sameParamTypes(*ex.type, *fieldType) || ex.cmethod != cmethod) continue;
                    if (ex.vslot >= 0) {
                        slot = ex.vslot;
                        virt = true;
                    }
                    t->params.erase(t->params.begin() + static_cast<std::ptrdiff_t>(j));
                    break;
                }
                if (!virt && overridesOtherBase(*t, d.name, *fieldType, cmethod)) virt = true;
                if (virt) {
                    if (slot < 0) slot = newSlot(cx, at);
                    if (slot >= 0) t->vslots[static_cast<std::size_t>(slot)] = pure ? "" : field.mangled;
                }
                field.vslot = slot;
                field.pure = pure;
                t->params.push_back(std::move(field));
                if (isP("{")) {
                    auto decl = newMethodDecl(tag, t->params.back().mangled, *fieldType, fieldType->params,
                                              Pos{d.line, d.col, d.file});
                    deferBody(2, 0, decl.get(), fieldType->inner.get());
                    extras_.push_back(std::move(decl));
                } else if (defaulted && d.name == "operator=") {
                    // C &operator=(const C &) = default: memberwise, made when the class is finished.
                    expectP(";", ";");
                    cx.defaultAssigns.push_back(
                        DefaultedAssign{t->params.back().mangled, cloneType(*fieldType), Pos{d.line, d.col, d.file}});
                } else {
                    expectP(";", ";");
                    if (!pure) {
                        // Declared only: a prototype, so that calls can be checked; the definition
                        // may be later in this file or in another file.
                        auto decl = newMethodDecl(tag, t->params.back().mangled, *fieldType, fieldType->params,
                                                  Pos{d.line, d.col, d.file});
                        extras_.push_back(std::move(decl));
                    }
                }
                continue;
            }
            Param field;
            field.name = d.name;
            field.type = std::move(fieldType);
            if (eatP(":")) {
                auto w = parseAssign();
                if (!w || w->kind != Expr::Kind::Int) err("bitfield width is not a constant");
                else field.bits = static_cast<int>(w->ival);
            }
            if (!pub) t->nonpod = true;
            if (memberInit(tag, d.name)) cx.cxx = true;
            {
                // A member of a class that constructs, destroys or assigns: this class must too.
                const Type* ft = field.type.get();
                while (ft && ft->kind == Type::Kind::Array && ft->inner) ft = ft->inner.get();
                Type* mc = ft && ft->kind == Type::Kind::Struct && !ft->tag.empty() ? classOf(ft->tag) : nullptr;
                if (mc && (mc->needCtor || mc->needDtor || hasAssign(mc))) cx.cxx = true;
            }
            t->params.push_back(std::move(field));
            while (fieldBase && eatP(",")) {
                // More declarators of the same type: int a, *b, c[4];
                Parsed d2 = parseDeclarator(true);
                Param more;
                more.name = d2.name;
                more.type = apply(cloneType(*fieldBase), d2.mods);
                if (eatP(":")) {
                    auto w = parseAssign();
                    if (!w || w->kind != Expr::Kind::Int) err("bitfield width is not a constant");
                    else more.bits = static_cast<int>(w->ival);
                }
                if (memberInit(tag, d2.name)) cx.cxx = true;
                t->params.push_back(std::move(more));
            }
            expectP(";", ";");
        }
        expectP("}", "}");
        if (!tag.empty()) {
            auto it = structs_.find(tag);
            bool already = it != structs_.end() && it->second && !it->second->params.empty() && !completing;
            if (already) {
                diags_.error(at.file.empty() ? "t.c" : at.file, at.line, at.column, "redefined struct '" + tag + "'");
            } else {
                hideBaseFields(*t);
                if (cx.cxx) finishClass(cx);
                structs_[tag] = cloneType(*t);
                canonStructs()[tag] = cloneType(*t);
                if (cx.cxx) {
                    // The class name: in the class's own scope (the injected-class-name) and in the
                    // scope around the outermost class being defined, where all class names were
                    // kept before class scopes existed.
                    addTypedef(shortTag, cloneType(*t));
                    // (not a template instance's name: less<int> must not make "less" a type name
                    // everywhere, where a parameter called less is called as less(a, b))
                    // A class nested in another is not found by its short name outside that class
                    // ([class.nest] p1): UISkeleton::Bar made gx::Bar(..) a cast, Random::State
                    // took the place of lua::State.
                    const bool nested = classStack_.size() > 1 && classStack_[classStack_.size() - 2];
                    if (tplShort.empty() && !nested && classOuter_ < tscopes_.size())
                        tscopes_[classOuter_][shortTag] = cloneType(*t);
                    else if (tplShort.empty() && nested && tscopes_.size() >= 2) {
                        tscopes_[tscopes_.size() - 2][shortTag] = cloneType(*t);  // the enclosing class's scope
                        // a member type of the enclosing class: found in its derived classes and
                        // in its member functions defined outside it
                        const Type* outerCls = classStack_[classStack_.size() - 2];
                        if (!outerCls->tag.empty()) memberTypes_[outerCls->tag + "::" + shortTag] = classByName(*t);
                    }
                }
                readDeferred(deferred, cx);
                if (cx.cxx) emitClassDecls(cx);
            }
        }
        if (!deferred.empty()) readDeferred(deferred, cx);  // an unnamed or redefined class
        return t;
    }

    // A class named by "A::B::C" where some leading part is a typedef or a member type: the class.
    // Returns null when the name is an ordinary class or namespace path, or names no class.
    const Type* classThroughTypedefs(const std::string& qname) const {
        std::vector<std::string> parts;
        for (std::size_t i = 0;;) {
            std::size_t j = qname.find("::", i);
            parts.push_back(qname.substr(i, j == std::string::npos ? std::string::npos : j - i));
            if (j == std::string::npos) break;
            i = j + 2;
        }
        auto named = [&](const std::string& q, bool& viaAlias) -> const Type* {
            viaAlias = false;
            for (std::size_t keep = ns_.size() + 1; keep-- > 0;) {
                std::string pre;
                for (std::size_t k = 0; k < keep; ++k) pre += ns_[k] + "::";
                auto it = structs_.find(pre + q);
                if (it != structs_.end() && it->second) return it->second.get();
                auto mt = memberTypes_.find(pre + q);
                if (mt != memberTypes_.end() && mt->second) {
                    viaAlias = true;
                    return mt->second.get();
                }
            }
            std::size_t cut = q.rfind("::");
            const Type* td = nullptr;
            if (cut == std::string::npos) td = findTypedef(q);
            else if (knownNs_.count(q.substr(0, cut))) td = findTypedef(q.substr(cut + 2));
            if (td) viaAlias = true;
            return td;
        };
        for (std::size_t n = parts.size(); n > 0; --n) {
            std::string q;
            for (std::size_t k = 0; k < n; ++k) q += (k ? "::" : "") + parts[k];
            bool alias = false;
            const Type* t = named(q, alias);
            if (!t) continue;
            for (std::size_t k = n; t && k < parts.size(); ++k) {
                if (t->kind != Type::Kind::Struct || t->tag.empty()) return nullptr;
                std::string m = t->tag + "::" + parts[k];
                auto mt = memberTypes_.find(m);
                if (mt != memberTypes_.end() && mt->second) {
                    t = mt->second.get();
                    alias = true;
                    continue;
                }
                auto st = structs_.find(m);
                t = st != structs_.end() ? st->second.get() : nullptr;
            }
            if (!alias || !t || t->kind != Type::Kind::Struct || t->tag.empty()) return nullptr;
            return t;
        }
        return nullptr;
    }

    // The file this translation unit was compiled from: the last token's (the first token may come
    // from a header). It names the unit's __okay_init_/__okay_fini_ functions and its closures, which
    // must differ between the files of one program.
    std::string unitFile() const {
        for (std::size_t k = toks_.size(); k-- > 0;)
            if (!toks_[k].file.empty()) return toks_[k].file;
        return "t";
    }

    // A member function body whose reading waits for the end of its class (parseStructType).
    struct DeferredBody {
        std::size_t at = 0;           // index of its '{'
        int kind = 0;                 // 0 destructor, 1 constructor 'index', 2 member function 'decl'
        std::size_t index = 0;
        Decl* decl = nullptr;
        std::unique_ptr<Type> ret;
        std::vector<std::string> params;  // its parameter names (locals of the body)
    };
    std::vector<DeferredBody>* staticDefer_ = nullptr;  // the bodies of the class being parsed
    int localClassN_ = 0;  // local classes renamed apart (parseStructType)
    // ---- Structured bindings ([dcl.struct.bind]) ----
    // auto [a, b] = e; and for (auto &[k, v] : m): a hidden variable __sbN holds e (or refers to it),
    // and each name stands for its i-th element; the checker makes that the i-th data member (or
    // array element) of __sbN once it knows the type.
    struct SBind {
        std::string name;
        std::string var;
        int index = 0;
        std::size_t pos = 0;  // its place in localNames_ (gone when the scope is)
        int classDepth = 0;
    };
    std::vector<SBind> sbinds_;
    bool atBindings() const {
        std::size_t j = i_;
        if (j < toks_.size() && (isP(toks_[j], "&") || isP(toks_[j], "&&"))) ++j;
        return j + 1 < toks_.size() && isP(toks_[j], "[") && toks_[j + 1].kind == Tok::Ident;
    }
    std::vector<std::string> readBindings(bool& isRef) {
        isRef = eatP("&") || eatP("&&");
        std::vector<std::string> names;
        expectP("[", "[");
        while (!atEnd() && cur().kind == Tok::Ident) {
            names.push_back(cur().text);
            advance();
            if (!eatP(",")) break;
        }
        expectP("]", "]");
        return names;
    }
    void bindNames(const std::vector<std::string>& names, const std::string& var) {
        for (std::size_t k = 0; k < names.size(); ++k) {
            SBind b;
            b.name = names[k];
            b.var = var;
            b.index = static_cast<int>(k);
            b.pos = localNames_.size();
            b.classDepth = classBodies_;
            localNames_.push_back(names[k]);
            sbinds_.push_back(b);
        }
    }
    const SBind* boundName(const std::string& n) const {
        // the innermost local of that name must be the binding itself (a later local hides it), and
        // a class body read meanwhile (an instance made inside the function) does not see it
        std::size_t last = localNames_.size();
        for (std::size_t k = localNames_.size(); k-- > 0;)
            if (localNames_[k] == n) {
                last = k;
                break;
            }
        for (auto it = sbinds_.rbegin(); it != sbinds_.rend(); ++it)
            if (it->name == n && it->pos == last && it->classDepth == classBodies_) return &*it;
        return nullptr;
    }
    int classBodies_ = 0;  // class bodies (and their deferred member bodies) being read
    std::set<std::string> finalTags_;  // classes declared final
    std::set<std::string> tiDone_;  // type_info objects emitted for vtables (emitClassDecls)
    void skipBraces() {
        int depth = 0;
        while (!atEnd()) {
            if (isP("{")) ++depth;
            else if (isP("}") && --depth == 0) {
                advance();
                return;
            }
            advance();
        }
    }
    // The members of a class and of its bases that have the name of a function or variable
    // template (data members) or of a type (data members and member functions): inside a member
    // function the member hides it ([class.member.lookup], [basic.lookup.unqual] p8), so `max < 0`
    // there compares the member `max`, and `Value(i)` calls the member function Value, not a
    // class okay::uivars::Value. They are put among the local names while the body is read; the
    // caller cuts localNames_ back to the returned mark.
    std::size_t pushMemberNames(const Type* cls) {
        std::size_t mark = localNames_.size();
        const std::string own = cls ? shortName(cls->tag) : std::string();
        for (int depth = 0; cls && depth < 64; ++depth) {
            for (const Param& p : cls->params) {
                if (!p.type || p.name.empty() || p.name == own || !(std::isalpha(static_cast<unsigned char>(p.name[0])) || p.name[0] == '_'))
                    continue;
                const bool fn = p.type->kind == Type::Kind::Function;
                if ((!fn && (ftmpl_.count(p.name) || varTmpl_.count(p.name))) ||
                    (p.name.compare(0, 8, "operator") != 0 && findTypedef(p.name)))
                    localNames_.push_back(p.name);
            }
            auto sf = staticFns_.find(cls->tag);  // static member functions (Cable::Curve beside a class Curve)
            if (sf != staticFns_.end())
                for (const std::string& n : sf->second)
                    if (n != own && n.compare(0, 8, "operator") != 0 && findTypedef(n)) localNames_.push_back(n);
            cls = cls->base.empty() ? nullptr : classOf(cls->base);
        }
        return mark;
    }
    void readDeferred(std::vector<DeferredBody>& bodies, ClassCtx& cx) {
        std::size_t saved = i_;
        bool savedHalf = halfGt_;
        halfGt_ = false;
        const std::size_t savedFloor = localClassFloor_;
        const std::string savedDeferredClass = deferredClass_;
        if (cx.t) deferredClass_ = cx.t->tag;
        for (DeferredBody& db : bodies) {
            i_ = db.at;
            lastParams_ = db.params;
            // (a local class: the names from here are its own, the ones before the function's)
            if (bodyDepth_ > 0) localClassFloor_ = localNames_.size();
            std::size_t memberMark = pushMemberNames(cx.t);
            auto body = parseBody(db.ret.get());
            localNames_.resize(memberMark);
            localClassFloor_ = savedFloor;
            if (db.kind == 0) cx.dtorBody = std::move(body);
            else if (db.kind == 1 && db.index < cx.ctors.size()) cx.ctors[db.index].body = std::move(body);
            else if (db.kind == 2 && db.decl) db.decl->body = std::move(body);
        }
        deferredClass_ = savedDeferredClass;
        bodies.clear();
        lastParams_.clear();
        i_ = saved;
        halfGt_ = savedHalf;
    }

    // Parse a function body, noting the return type for destructor-before-return handling.
    std::unique_ptr<Stmt> parseBody(const Type* ret, std::size_t outerScopes = 0) {
        const Type* savedRet = curRet_;
        std::size_t savedBase = dbase_;
        auto savedLoops = std::move(loopBases_);
        auto savedBrks = std::move(brkBases_);
        loopBases_.clear();
        brkBases_.clear();
        curRet_ = ret;
        dbase_ = dscopes_.size() - outerScopes;
        ++bodyDepth_;
        std::size_t localMark = localNames_.size();
        for (const std::string& n : lastParams_) localNames_.push_back(n);
        lastParams_.clear();
        auto b = parseCompound();
        localNames_.resize(localMark);
        --bodyDepth_;
        if (bodyDepth_ == 0) fnStatics_.clear();
        curRet_ = savedRet;
        dbase_ = savedBase;
        loopBases_ = std::move(savedLoops);
        brkBases_ = std::move(savedBrks);
        return b;
    }

    Parsed parseDeclarator(bool needName) {
        Guard nest(*this);
        if (!nest) return Parsed();
        std::vector<Mod::Kind> prefix;
        std::vector<std::string> memberOf;  // the class of each T C::* in prefix
        while (!atEnd()) {
            if (eatKw("const") || eatKw("volatile")) continue;
            if (eatP("*")) {
                prefix.push_back(Mod::Kind::Ptr);
                continue;
            }
            if (eatP("&")) {
                prefix.push_back(Mod::Kind::Ref);
                continue;
            }
            if (eatP("&&")) {
                prefix.push_back(Mod::Kind::RRef);  // T&&: an rvalue (or forwarding) reference
                continue;
            }
            if (!atEnd() && cur().kind == Tok::Ident && nextIs("::")) {
                // T C::*name: a pointer to a data member of class C ([dcl.mptr]).
                std::size_t j = i_;
                std::string q;
                while (j + 1 < toks_.size() && toks_[j].kind == Tok::Ident && isP(toks_[j + 1], "::")) {
                    q += (q.empty() ? "" : "::") + toks_[j].text;
                    j += 2;
                }
                std::string cls = j < toks_.size() && isP(toks_[j], "*") ? classPrefix(q) : "";
                if (cls.empty() && protoDepth_ > 0 && j < toks_.size() && isP(toks_[j], "*"))
                    if (const Type* tp = findTypedef(q))  // bool Owner::*m with Owner a template parameter
                        if (tp->kind == Type::Kind::Struct && tp->tag.find('\x02') != std::string::npos) cls = tp->tag;
                if (!cls.empty()) {
                    i_ = j + 1;
                    prefix.push_back(Mod::Kind::Ptr);
                    memberOf.resize(prefix.size());
                    memberOf.back() = cls;
                    continue;
                }
            }
            break;
        }
        Parsed d = parseDirect(needName);
        for (int n = static_cast<int>(prefix.size()) - 1; n >= 0; --n) {
            Mod m;
            m.kind = prefix[static_cast<std::size_t>(n)];
            if (static_cast<std::size_t>(n) < memberOf.size()) m.memberOf = memberOf[static_cast<std::size_t>(n)];
            d.mods.push_back(std::move(m));
        }
        return d;
    }

    Parsed parseDirect(bool needName) {
        Parsed d;
        // In an abstract declarator a '(' before ')' or a type starts a parameter list, not a
        // grouping ([dcl.ambig.res] p2): the int(int, char) of function<int(int, char)>.
        bool params = !needName && isP("(") && i_ + 1 < toks_.size() &&
                      (isP(toks_[i_ + 1], ")") || isP(toks_[i_ + 1], "...") || isTypeKw(toks_[i_ + 1]));
        if (!params && eatP("(")) {
            d = parseDeclarator(needName);
            expectP(")", ")");
        } else if (!atEnd() && cur().kind == Tok::Ident) {
            d.name = cur().text;
            d.line = cur().line;
            d.col = cur().column;
            d.file = cur().file;
            advance();
            auto operatorSymbol = [&]() {
                // operator+, operator==, operator[], operator() and the other overloadable symbols.
                std::string op = cur().text;
                advance();
                if (op == "(" && eatP(")")) op = "()";
                else if (op == "[" && eatP("]")) op = "[]";
                d.name += op;
            };
            if (d.name == "operator" && !atEnd() && cur().kind == Tok::Punct) operatorSymbol();
            if (d.name == "operator" && !atEnd() && cur().kind == Tok::StringLit && cur().decoded.empty() &&
                i_ + 1 < toks_.size() && toks_[i_ + 1].kind == Tok::Ident) {
                // operator""_sfx, a literal operator ([over.literal]); mangled li<name>.
                advance();
                d.name += "\"\"" + cur().text;
                advance();
            }
            while (eatP("::")) {
                if (atEnd() || cur().kind != Tok::Ident) {
                    err("expected a name");
                    break;
                }
                d.name += "::";
                d.name += cur().text;
                advance();
                // C::operator=(..) defined outside its class ([class.mfct] p2).
                if (d.name.size() >= 10 && d.name.compare(d.name.size() - 10, 10, "::operator") == 0 && !atEnd() &&
                    cur().kind == Tok::Punct && !isP("::"))
                    operatorSymbol();
            }
            if (isP("(") && classScopes_ == 0 && localNames_.empty()) {
                std::size_t cc = d.name.rfind("::");
                if (cc != std::string::npos) enterMemberScope(classPrefix(d.name.substr(0, cc)));
            }
        } else if (needName) {
            err("expected a name");
            d.line = atEnd() ? 1 : cur().line;
            d.col = atEnd() ? 1 : cur().column;
        }
        while (true) {
            if (eatP("[")) {
                Mod m;
                m.kind = Mod::Kind::Arr;
                if (!isP("]")) {
                    m.bound = parseAssign();
                    if (m.bound) boundConsts(*m.bound);  // T a[kLines] with kLines a class constant
                }
                else m.unsized = true;
                expectP("]", "]");
                d.mods.push_back(std::move(m));
                continue;
            }
            if (eatP("(")) {
                Mod m;
                m.kind = Mod::Kind::Func;
                m.variadic = parseParams(m.params);
                std::size_t back = i_;
                skipNoexcept();
                if (eatP("->")) m.trailing = parseTypeName();  // auto f(int a) -> decltype(a * 2)
                else i_ = back;
                d.mods.push_back(std::move(m));
                continue;
            }
            break;
        }
        return d;
    }

    std::unique_ptr<Type> adjustParam(std::unique_ptr<Type> t) {
        if (!t) return t;
        if (t->kind == Type::Kind::Array) {
            auto p = ty(Type::Kind::Pointer);
            p->inner = std::move(t->inner);
            return p;
        }
        if (t->kind == Type::Kind::Function) {
            auto p = ty(Type::Kind::Pointer);
            p->inner = std::move(t);
            return p;
        }
        return t;
    }

    bool parseParams(std::vector<Param>& params) {
        bool variadic = false;
        if (isP("...")) {
            advance();
            expectP(")", ")");
            return true;
        }
        if (eatP(")")) return false;
        if (isKw("void")) {
            std::size_t save = i_;
            advance();
            if (eatP(")")) return false;
            i_ = save;
        }
        while (!atEnd()) {
            if (!isTypeKw(cur())) {
                err("expected ')'");
                if (isP(")")) advance();
                break;
            }
            auto base = parseBase();
            Parsed d = parseDeclarator(false);
            Param p;
            p.name = d.name;
            p.type = adjustParam(apply(std::move(base), d.mods));
            if (eatP("=")) {
                // A default argument. {} or {a, b} for a class parameter is a temporary T{...}.
                p.defArg = parseInitializer();
                const Type* pt = p.type.get();
                if (pt && pt->kind == Type::Kind::Reference && pt->inner) pt = pt->inner.get();
                if (p.defArg && p.defArg->kind == Expr::Kind::Brace && pt && pt->kind == Type::Kind::Struct)
                    p.defArg = braceTemp(*pt, std::move(p.defArg->kids), here());
            }
            params.push_back(std::move(p));
            if (eatP(")")) break;
            if (!eatP(",")) {
                err("expected ')'");
                break;
            }
            if (isP("...")) {
                advance();
                expectP(")", ")");
                variadic = true;
                break;
            }
        }
        lastParams_.clear();
        for (const Param& q : params)
            if (!q.name.empty()) lastParams_.push_back(q.name);
        return variadic;
    }
    // The parameter names of the last parameter list read: they are locals of the body that follows
    // ([basic.scope.param]), so a parameter named count hides std::count there.
    std::vector<std::string> lastParams_;

    std::unique_ptr<Type> apply(std::unique_ptr<Type> base, std::vector<Mod>& mods) {
        for (int n = static_cast<int>(mods.size()) - 1; n >= 0; --n) {
            auto t = std::make_unique<Type>();
            Mod& m = mods[static_cast<std::size_t>(n)];
            if (m.kind == Mod::Kind::Ptr) {
                t->kind = Type::Kind::Pointer;
                t->memberOf = m.memberOf;
                t->inner = std::move(base);
            } else if (m.kind == Mod::Kind::Ref || m.kind == Mod::Kind::RRef) {
                if (base && base->kind == Type::Kind::Reference) {
                    // Reference collapsing ([dcl.ref] p6): T& && is T&, T&& && is T&&.
                    if (m.kind == Mod::Kind::Ref) base->rref = false;
                    continue;  // base stays the (collapsed) reference
                }
                t->kind = Type::Kind::Reference;
                t->rref = m.kind == Mod::Kind::RRef;
                t->inner = std::move(base);
            } else if (m.kind == Mod::Kind::Arr) {
                t->kind = Type::Kind::Array;
                t->inner = std::move(base);
                t->bound = std::move(m.bound);
                t->unsized = m.unsized || !t->bound;
            } else {
                t->kind = Type::Kind::Function;
                bool autoBase = base && base->kind == Type::Kind::Struct && base->tag == "\x03" "auto";
                t->inner = m.trailing && autoBase ? std::move(m.trailing) : std::move(base);
                t->params = std::move(m.params);
                t->variadic = m.variadic;
            }
            base = std::move(t);
        }
        return base;
    }

    std::unique_ptr<Decl> makeDecl(Parsed& d, std::unique_ptr<Type> type, std::unique_ptr<Expr> init) {
        auto decl = std::make_unique<Decl>();
        decl->name = d.name;
        decl->line = d.line;
        decl->col = d.col;
        decl->file = d.file;
        decl->type = std::move(type);
        decl->init = std::move(init);
        decl->function = decl->type && decl->type->kind == Type::Kind::Function;
        if (bodyDepth_ == 0) decl->name = qualify(decl->name);  // a local is never a namespace member
        if (!instFnName_.empty() && decl->function) {
            decl->name = instFnName_;
            instFnName_.clear();
            return decl;
        }
        if (decl->name.find("::") != std::string::npos && decl->type && cLink_) {
            // extern "C" inside a namespace: the name is qualified for lookup, the symbol is plain
            // ([dcl.link]: two declarations of a C function in different namespaces are one function).
            decl->asmName = shortName(decl->name);
        } else if (decl->name.find("::") != std::string::npos && decl->type) {
            if (decl->function) decl->asmName = mangleFunc(decl->name, *decl->type);
            else decl->asmName = mangleData(decl->name);
        }
        return decl;
    }

    // A const member function: Itanium 5.1.5 puts K (the cv-qualifier of 'this') after N.
    static std::string constMangle(const std::string& m, bool isConst) {
        if (!isConst || m.compare(0, 3, "_ZN") != 0) return m;
        return "_ZNK" + m.substr(3);
    }

    // The class tag a qualified prefix (C, N::C, a typedef of a class) names, or "".
    // The parameters and body of a member function defined outside its class see the class's
    // member types by their bare names ([basic.lookup.unqual] p8, [class.mfct]): R C::f(Type t).
    // A typedef scope with them is pushed here and popped when the declaration is done.
    long memberScope_ = -1;  // index in tscopes_ of that scope, or -1
    void enterMemberScope(const std::string& tag) {
        if (tag.empty() || memberScope_ >= 0) return;
        std::map<std::string, std::unique_ptr<Type>> names;
        // The class's member types, then those of the classes it is nested in, and each of those
        // classes' own names ([basic.lookup.unqual] p8, [class.nest]): in Paths::Env::Current()
        // "Env" is the class itself (nested class names are not kept outside their class).
        for (std::string cls = tag; !cls.empty() && classOf(cls);) {
            std::string pre = cls + "::";
            for (auto it = memberTypes_.lower_bound(pre); it != memberTypes_.end() && it->first.compare(0, pre.size(), pre) == 0; ++it)
                if (it->second && it->first.find("::", pre.size()) == std::string::npos &&
                    !names.count(it->first.substr(pre.size())))
                    names[it->first.substr(pre.size())] = cloneType(*it->second);
            for (auto it = structs_.lower_bound(pre); it != structs_.end() && it->first.compare(0, pre.size(), pre) == 0; ++it)
                if (it->second && it->first.find("::", pre.size()) == std::string::npos && !it->second->tag.empty() &&
                    !names.count(it->first.substr(pre.size()))) {
                    auto t = ty(Type::Kind::Struct);
                    t->tag = it->second->tag;
                    names[it->first.substr(pre.size())] = std::move(t);
                }
            const std::size_t cut = cls.rfind("::");
            if (cls.find('<') == std::string::npos && !names.count(shortName(cls))) {
                auto self = ty(Type::Kind::Struct);
                self->tag = cls;
                names[shortName(cls)] = std::move(self);
            }
            if (cut == std::string::npos) break;
            cls = cls.substr(0, cut);
        }
        if (names.empty()) return;
        tscopes_.emplace_back();
        memberScope_ = static_cast<long>(tscopes_.size()) - 1;
        for (auto& n : names) tscopes_.back()[n.first] = std::move(n.second);
    }
    struct MemberScopeGuard {
        Parser& p;
        long saved;
        ~MemberScopeGuard() {
            if (p.memberScope_ != saved && p.memberScope_ >= 0 &&
                static_cast<std::size_t>(p.memberScope_) < p.tscopes_.size())
                p.tscopes_.erase(p.tscopes_.begin() + p.memberScope_);
            p.memberScope_ = saved;
        }
    };
    std::string classPrefix(const std::string& prefix) {
        if (prefix.empty()) return {};
        if (classOf(qualify(prefix))) return qualify(prefix);
        // A::B (a nested class) named inside namespaces: tried in each enclosing namespace,
        // innermost first ([basic.lookup.qual]).
        for (std::size_t n = ns_.size(); n > 0; --n) {
            std::string q;
            for (std::size_t k = 0; k < n; ++k) q += ns_[k] + "::";
            if (classOf(q + prefix)) return q + prefix;
        }
        if (classOf(prefix)) return prefix;
        if (const Type* td2 = findTypedef(prefix))
            if (!td2->tag.empty() && classOf(td2->tag)) return td2->tag;
        return {};
    }

    // Whether "class C;" (or C's base clause) makes C's short name a type name here: when it names
    // nothing yet, or names a class nested in another class (class names of nested classes are kept
    // in the scope around the outermost class, but outside that class they are not found by an
    // unqualified name, [class.nest] p1; "class State;" in okay::lua named okay::Random::State).
    bool namesNoClassHere(const std::string& tag) const {
        const Type* old = findTypedef(shortName(tag));
        if (!old) return true;
        if ((old->kind != Type::Kind::Struct && old->kind != Type::Kind::Union) || old->tag == tag) return false;
        const std::size_t cut = old->tag.rfind("::");
        return cut != std::string::npos && structs_.count(old->tag.substr(0, cut)) && !inProgress(old->tag.substr(0, cut));
    }
    std::string qualify(const std::string& name) const {
        if (name.empty() || ns_.empty() || name.find("::") != std::string::npos) return name;
        std::string q;
        for (const std::string& part : ns_) {
            if (!q.empty()) q += "::";
            q += part;
        }
        q += "::";
        q += name;
        return q;
    }

    // A static member (ISO/IEC 14882 [class.static]). A static member function is an ordinary
    // function named Class::f (no 'this'); its symbol is the Itanium nested name. A static data
    // member is a global named Class::x: declared here (extern) unless it has an initializer in
    // the class (constexpr or const), which defines it in this file with internal linkage. C++17
    // makes such a member an inline variable with one address in the program; here each file has
    // its own copy (see the plan).
    std::map<std::string, std::set<std::string>> staticFns_;
    std::vector<std::unique_ptr<Decl>> staticData_;
    std::map<std::string, long> classConsts_;  // Class::N for static integer constants
    // C::m as a constant: C's own static constant, else one of its base classes' (is_same<T, T>
    // takes value from integral_constant<bool, true>).
    bool memberConst(const std::string& q, long& out, int depth = 0) const {
        auto it = classConsts_.find(q);
        if (it != classConsts_.end()) {
            out = it->second;
            return true;
        }
        std::size_t cut = q.rfind("::");
        if (cut == std::string::npos || depth > 16) return false;
        std::string cls = q.substr(0, cut);
        auto st = structs_.find(cls);
        if (st == structs_.end() || !st->second || st->second->base.empty()) return false;
        return memberConst(st->second->base + q.substr(cut), out, depth + 1);
    }
    // A constant expression evaluated while parsing ([expr.const]): integers, bools, the static
    // constants of classes (trait values), !, -, ~, the binary operators, ?: and casts. False when
    // 'e' is not one the parser can work out (the checker sees it then).
    bool evalConst(const Expr& e, long& out) {
        int width = 8;
        bool uns = false;
        return evalConstTyped(e, out, width, uns);
    }
    // A value of the given width (4 or 8 bytes after the integral promotions) and signedness, held
    // sign- or zero-extended in a long.
    static long fitConst(long v, int width, bool uns) {
        if (width >= 8) return v;
        const unsigned long bits = static_cast<unsigned long>(v) & 0xffffffffUL;
        if (!uns && (bits & 0x80000000UL)) return static_cast<long>(bits | ~0xffffffffUL);
        return static_cast<long>(bits);
    }
    // The constant value of e, with its type's width and signedness ([expr.const]; the usual
    // arithmetic conversions, [expr.arith.conv]): (unsigned long)-1 < 0ul is false, not true, so
    // numeric_limits<unsigned long>::digits is 64.
    bool evalConstTyped(const Expr& e, long& out, int& width, bool& uns) {
        width = 4;
        uns = false;
        switch (e.kind) {
        case Expr::Kind::Int:
        case Expr::Kind::Char:
            out = static_cast<long>(e.ival);
            uns = e.litUnsigned;
            if (e.litRank > 0 || (uns ? e.ival > 0xffffffffULL : e.ival > 0x7fffffffULL)) width = 8;
            if (!uns && e.ival > 0x7fffffffffffffffULL) uns = true;
            return true;
        case Expr::Kind::Name: {
            width = 8;
            if (e.text.find("::") == std::string::npos && classConst(e.text, out)) return true;
            if (memberConst(e.text, out)) return true;
            for (std::size_t keep = ns_.size(); keep > 0; --keep) {
                std::string q;
                for (std::size_t k = 0; k < keep; ++k) q += ns_[k] + "::";
                if (memberConst(q + e.text, out)) return true;
            }
            return false;
        }
        case Expr::Kind::Unary: {
            if (e.text == "sizeof" && e.ty && e.kids.empty()) {
                // sizeof(type) ([expr.sizeof]): of a scalar or a finished class, as laid out.
                const int n = typeSize(*e.ty);
                if (n <= 0) return false;
                out = n;
                width = 8;
                uns = true;
                return true;
            }
            long v = 0;
            if (e.kids.empty() || !e.kids[0] || !evalConstTyped(*e.kids[0], v, width, uns)) return false;
            if (e.text == "!") {
                out = !v;
                width = 4;
                uns = false;
                return true;
            }
            if (e.text == "-") out = static_cast<long>(0UL - static_cast<unsigned long>(v));
            else if (e.text == "~") out = ~v;
            else if (e.text == "+") out = v;
            else return false;
            out = fitConst(out, width, uns);
            return true;
        }
        case Expr::Kind::Cast: {
            long v = 0;
            if (e.kids.empty() || !e.kids[0] || !evalConstTyped(*e.kids[0], v, width, uns)) return false;
            const Type* t = e.ty.get();
            if (t && t->kind == Type::Kind::Bool) {
                out = v != 0;
                width = 4;
                uns = false;
                return true;
            }
            if (t && (t->kind == Type::Kind::Char || t->kind == Type::Kind::Short || t->kind == Type::Kind::Int ||
                      t->kind == Type::Kind::Long)) {
                // to a narrower type: its bits, then the promotion to int
                const int w = t->kind == Type::Kind::Char ? 1 : t->kind == Type::Kind::Short ? 2 : t->kind == Type::Kind::Int ? 4 : 8;
                unsigned long bits = static_cast<unsigned long>(v);
                if (w < 8) {
                    const unsigned long mask = (1UL << (8 * w)) - 1;
                    bits &= mask;
                    if (!t->unsign && (bits >> (8 * w - 1)) & 1UL) bits |= ~mask;
                }
                out = static_cast<long>(bits);
                width = w < 4 ? 4 : w;
                uns = t->unsign && w >= 4;
                return true;
            }
            out = v;
            return true;
        }
        case Expr::Kind::Cond: {
            long c = 0;
            int cw = 4;
            bool cu = false;
            if (e.kids.size() != 3 || !e.kids[0] || !evalConstTyped(*e.kids[0], c, cw, cu)) return false;
            return e.kids[c ? 1 : 2] && evalConstTyped(*e.kids[c ? 1 : 2], out, width, uns);
        }
        case Expr::Kind::Bin: {
            long a = 0;
            long b = 0;
            int wa = 4, wb = 4;
            bool ua = false, ub = false;
            if (e.kids.size() != 2 || !e.kids[0] || !e.kids[1] || !evalConstTyped(*e.kids[0], a, wa, ua)) return false;
            if (e.text == "&&" && !a) return (out = 0, true);
            if (e.text == "||" && a) return (out = 1, true);
            if (!evalConstTyped(*e.kids[1], b, wb, ub)) return false;
            const std::string& o = e.text;
            if (o == "&&" || o == "||") {
                out = b != 0;
                return true;
            }
            if (o == "<<" || o == ">>") {
                // the type of the promoted left operand
                width = wa;
                uns = ua;
                if (b < 0 || b >= 8 * width) return false;
                if (o == "<<") out = static_cast<long>(static_cast<unsigned long>(a) << b);
                else out = uns ? static_cast<long>(static_cast<unsigned long>(fitConst(a, width, true)) >> b) : a >> b;
                out = fitConst(out, width, uns);
                return true;
            }
            // the usual arithmetic conversions: the wider type; at one width, unsigned if either is
            width = wa > wb ? wa : wb;
            uns = wa == wb ? (ua || ub) : (wa > wb ? ua : ub);
            a = fitConst(a, width, uns);
            b = fitConst(b, width, uns);
            const unsigned long xa = static_cast<unsigned long>(a), xb = static_cast<unsigned long>(b);
            bool cmp = true;
            if (o == "==") out = a == b;
            else if (o == "!=") out = a != b;
            else if (o == "<") out = uns ? xa < xb : a < b;
            else if (o == ">") out = uns ? xa > xb : a > b;
            else if (o == "<=") out = uns ? xa <= xb : a <= b;
            else if (o == ">=") out = uns ? xa >= xb : a >= b;
            else {
                cmp = false;
                if (o == "+") out = static_cast<long>(xa + xb);
                else if (o == "-") out = static_cast<long>(xa - xb);
                else if (o == "*") out = static_cast<long>(xa * xb);
                else if (o == "/" && b) out = uns ? static_cast<long>(xa / xb) : a / b;
                else if (o == "%" && b) out = uns ? static_cast<long>(xa % xb) : a % b;
                else if (o == "&") out = a & b;
                else if (o == "|") out = a | b;
                else if (o == "^") out = a ^ b;
                else return false;
                out = fitConst(out, width, uns);
            }
            if (cmp) {
                width = 4;
                uns = false;
            }
            return true;
        }
        default: return false;
        }
    }
    // static_assert(cond, "text"); ([dcl.pre] p10): a condition the parser can work out must hold;
    // one it cannot is left unchecked.
    bool parseStaticAssert() {
        if (!isKw("static_assert")) return false;
        Pos p = here();
        advance();
        expectP("(", "(");
        auto cond = parseAssign();
        std::string msg;
        if (eatP(",")) {
            while (!atEnd() && cur().kind == Tok::StringLit) {
                std::string t = cur().text;
                if (t.size() >= 2 && t.front() == '"' && t.back() == '"') t = t.substr(1, t.size() - 2);
                msg += t;
                advance();
            }
        }
        expectP(")", ")");
        expectP(";", ";");
        long v = 1;
        if (cond && evalConst(*cond, v) && v == 0)
            diags_.error(p.file.empty() ? "t.c" : p.file, p.line, p.col,
                         "static assertion failed" + (msg.empty() ? std::string() : ": " + msg));
        return true;
    }
    std::string bodyClass_;                    // the class of the out-of-class member function being parsed
    // N inside a class or one of its member functions: Class::N, then the enclosing classes.
    bool classConst(const std::string& name, long& out) {
        std::vector<std::string> tags;
        for (auto it = classStack_.rbegin(); it != classStack_.rend(); ++it)
            if (*it && !(*it)->tag.empty()) tags.push_back((*it)->tag);
        for (std::string t = bodyClass_; !t.empty();) {
            tags.push_back(t);
            std::size_t cut = t.rfind("::");
            if (cut == std::string::npos) break;
            t = t.substr(0, cut);
        }
        for (const std::string& t : tags) {
            auto it = classConsts_.find(t + "::" + name);
            if (it != classConsts_.end()) {
                out = it->second;
                return true;
            }
        }
        return false;
    }
    // constexpr int kN = 8; in a block: its value, for the length of an array of objects (the
    // parser does not keep const on a local, so this is the initializer's value).
    std::map<std::size_t, std::pair<std::string, long>> localConstAt_;
    bool localConst(const std::string& n, long& out) const {
        for (std::size_t k = localNames_.size(); k-- > 0;)
            if (localNames_[k] == n) {
                auto it = localConstAt_.find(k);
                if (it == localConstAt_.end() || it->second.first != n) return false;
                out = it->second.second;
                return true;
            }
        return false;
    }
    void boundConsts(Expr& e) {
        long cv = 0;
        if (e.kind == Expr::Kind::Name && localConst(e.text, cv)) {
            e.kind = Expr::Kind::Int;
            e.text.clear();
            e.ival = static_cast<std::uint64_t>(cv);
            return;
        }
        if (e.kind == Expr::Kind::Name && isLocalName(e.text)) return;  // a local hides the class's name
        if (e.kind == Expr::Kind::Name && e.text.find("::") == std::string::npos && classConst(e.text, cv)) {
            e.kind = Expr::Kind::Int;
            e.text.clear();
            e.ival = static_cast<std::uint64_t>(cv);
            return;
        }
        if (e.kind == Expr::Kind::Name) {
            auto it = classConsts_.find(e.text);  // R::kLines written out
            if (it == classConsts_.end()) it = classConsts_.find(qualify(e.text));
            if (it != classConsts_.end()) {
                e.kind = Expr::Kind::Int;
                e.text.clear();
                e.ival = static_cast<std::uint64_t>(it->second);
            }
            return;
        }
        for (auto& k : e.kids)
            if (k) boundConsts(*k);
    }
    void staticMember(const std::string& tag, Parsed& d, std::unique_ptr<Type> type, const Type* base = nullptr) {
        if (!type || d.name.empty()) {
            syncSemi();
            return;
        }
        std::string q = tag + "::" + d.name;
        auto decl = std::make_unique<Decl>();
        decl->name = q;
        decl->line = d.line;
        decl->col = d.col;
        decl->file = d.file;
        if (type->kind == Type::Kind::Function) {
            while (isKw("const") || isKw("noexcept") || isKw("override") || isKw("final")) advance();
            decl->function = true;
            decl->staticMember = true;
            decl->methodTag = tag;
            decl->asmName = mangleFunc(q, *type);
            const Type* ret = type->inner.get();
            decl->type = std::move(type);
            staticFns_[tag].insert(d.name);
            if (isP("{") && staticDefer_) {
                // Read when the class is complete ([class.mem] p7), like the other member functions.
                DeferredBody db;
                db.at = i_;
                db.kind = 2;
                db.decl = decl.get();
                db.ret = ret ? cloneType(*ret) : ty(Type::Kind::Void);
                db.params = lastParams_;
                lastParams_.clear();
                skipBraces();
                staticDefer_->push_back(std::move(db));
                // A prototype first, so the class's other functions may call it before its body.
                auto proto = std::make_unique<Decl>();
                proto->name = decl->name;
                proto->line = decl->line;
                proto->col = decl->col;
                proto->file = decl->file;
                proto->function = true;
                proto->staticMember = true;
                proto->methodTag = tag;
                proto->asmName = decl->asmName;
                proto->type = cloneType(*decl->type);
                staticData_.push_back(std::move(proto));
            } else if (isP("{")) {
                decl->body = parseBody(ret);
            } else {
                if (eatP("=")) advance();  // = delete / = default
                expectP(";", ";");
            }
            extras_.push_back(std::move(decl));
            return;
        }
        decl->type = std::move(type);
        decl->asmName = mangleData(q);
        if (eatP("=")) decl->init = parseInitializer();
        else if (isP("{")) decl->init = parseInitializer();
        if (decl->init) decl->isStatic = true;
        else decl->isExtern = true;
        if (decl->init && decl->init->kind != Expr::Kind::Brace && decl->type && decl->type->kind == Type::Kind::Struct &&
            !decl->type->tag.empty() && classOf(decl->type->tag) && !classOf(decl->type->tag)->needDtor &&
            bodyDepth_ == 0) {
            // static inline Vec3 listener = Vec3::Zero; and = Vec3(1, 2, 3) (a class without a
            // destructor, from another object): copied when the program starts, by its copy or
            // converting constructor if it has one ([basic.start.dynamic]), as a namespace one is.
            // The copy is made in a static member function of the class (weak, as the class may be
            // in every file), so the initializer's names are looked up from the class outward
            // ([class.static.data] p4: as in a member function): Vec3::Zero in namespace okay.
            Pos gp{d.line, d.col, d.file};
            std::vector<std::unique_ptr<Expr>> a;
            a.push_back(eAddr(eName(q, gp), gp));
            a.push_back(std::move(decl->init));
            decl->init.reset();
            auto fnT = ty(Type::Kind::Function);
            fnT->inner = ty(Type::Kind::Void);
            auto init = std::make_unique<Decl>();
            init->name = mangleFunc(tag + "::__okay_sinit_" + d.name, *fnT);
            init->function = true;
            init->staticMember = true;
            init->isInline = true;
            init->methodTag = tag;
            init->line = d.line;
            init->col = d.col;
            init->file = d.file;
            init->type = std::move(fnT);
            init->body = sBlock(gp);
            init->body->stmts.push_back(sExpr(eCall(eName("\x01" "cinit:" + decl->type->tag, gp), std::move(a), gp), gp));
            globalInit_.push_back(sExpr(eCall(eName(init->name, gp), {}, gp), gp));
            extras_.push_back(std::move(init));
        }
        if (decl->init && decl->type && (decl->type->kind == Type::Kind::Int || decl->type->kind == Type::Kind::Long ||
                                         decl->type->kind == Type::Kind::Short || decl->type->kind == Type::Kind::Char ||
                                         decl->type->kind == Type::Kind::Bool)) {
            // static constexpr int N = 8;: an integer constant the class's own code (array
            // lengths, case labels) can use as a constant expression ([expr.const]).
            const Expr* v = decl->init.get();
            if (v->kind == Expr::Kind::Brace && v->kids.size() == 1 && v->kids[0]) v = v->kids[0].get();
            long cv = 0;
            if (evalConst(*v, cv)) {
                // (also A::value && !B::value, the traits' own expressions: folded to the number)
                if (decl->type->kind == Type::Kind::Bool) cv = cv != 0;
                classConsts_[q] = cv;
                auto lit = std::make_unique<Expr>();
                lit->kind = Expr::Kind::Int;
                lit->ival = static_cast<std::uint64_t>(cv < 0 ? -cv : cv);
                lit->line = v->line;
                lit->col = v->col;
                lit->file = v->file;
                if (cv < 0) {
                    auto neg = std::make_unique<Expr>();
                    neg->kind = Expr::Kind::Unary;
                    neg->text = "-";
                    neg->line = lit->line;
                    neg->col = lit->col;
                    neg->file = lit->file;
                    neg->kids.push_back(std::move(lit));
                    lit = std::move(neg);
                }
                decl->init = std::move(lit);
            }
        }
        if (base && eatP(",")) {
            // static constexpr float a = 1, b = 2;: more static members of the same type.
            beforeDecls_.push_back(std::move(decl));
            Parsed d2 = parseDeclarator(true);
            staticMember(tag, d2, apply(cloneType(*base), d2.mods), base);
            return;
        }
        expectP(";", ";");
        // Before the function being parsed: a class template instance made inside a function body
        // has its static members declared ahead of that function.
        beforeDecls_.push_back(std::move(decl));
    }

    void flushExtras(Tu& tu) {
        if (!preStmts_.empty()) {
            err("'new' with a constructor or an initial value is only supported inside a function");
            preStmts_.clear();
        }
        flushBefore(tu);
        for (auto& d : staticData_) tu.decls.push_back(std::move(d));
        staticData_.clear();
        for (auto& d : extras_) tu.decls.push_back(std::move(d));
        extras_.clear();
    }
    // Globals made for static locals go in before the function that uses them.
    std::vector<std::unique_ptr<Decl>> beforeDecls_;
    // The static locals of the function being read (by the place of their name in localNames_), for
    // the member functions of a local class in it, which may use them ([class.local] p1): a static
    // local that stays a local (decl) gets a global name when one of them does (enclosingStatic).
    struct FnStatic {
        std::string name;
        Decl* decl = nullptr;
        std::string global;
        std::size_t at = 0;
    };
    std::vector<FnStatic> fnStatics_;
    std::size_t localClassFloor_ = static_cast<std::size_t>(-1);  // localNames_ from here: a local class's own
    std::string enclosingStatic(const std::string& name) {
        for (std::size_t k = localClassFloor_; k < localNames_.size(); ++k)
            if (localNames_[k] == name) return name;  // its own member, parameter or local
        for (std::size_t k = fnStatics_.size(); k-- > 0;) {
            FnStatic& s = fnStatics_[k];
            if (s.name != name || s.at >= localClassFloor_ || s.at >= localNames_.size() || localNames_[s.at] != name)
                continue;
            if (s.global.empty() && s.decl && s.decl->type) {
                s.global = "__slc" + std::to_string(newN_++) + "_" + name;
                s.decl->asmName = s.global;
                auto g = std::make_unique<Decl>();
                g->name = s.global;
                g->line = s.decl->line;
                g->col = s.decl->col;
                g->file = s.decl->file;
                g->type = cloneType(*s.decl->type);
                g->isExtern = true;
                beforeDecls_.push_back(std::move(g));
            }
            return s.global.empty() ? name : s.global;
        }
        return name;
    }
    void flushBefore(Tu& tu) {
        for (auto& d : beforeDecls_) tu.decls.push_back(std::move(d));
        beforeDecls_.clear();
    }

    // ---- template parsing ----
    struct TParam {
        std::string name;
        bool isType = true;
        bool hasDef = false;
        std::size_t defStart = 0;
    };
    struct TDecl {
        std::vector<TParam> params;
        std::size_t start = 0;
        std::size_t end = 0;
        std::string ns;  // the namespace it is declared in (N::M), for names its instances use
        // A last parameter named __okay_drop.. (our <vector>'s allocator): an argument for it is read
        // and left out, so vector<T, A> is vector<T>.
        bool dropOne = false;
    };
    std::map<std::string, TDecl> ftmpl_;
    std::map<std::string, TDecl> ctmpl_;
    struct AliasTmpl {
        TDecl td;
        std::size_t at = 0;  // the first token after '='
    };
    std::map<std::string, AliasTmpl> aliasTmpl_;  // template <..> using X = type;
    std::map<std::string, AliasTmpl> varTmpl_;    // template <..> constexpr T x = value;
    // X<args> of an alias template at the name: the type it stands for.
    static bool hasPlaceholderTag(const std::vector<TArg>& args) {
        for (const TArg& a : args)
            if (a.type && mangleType(*a.type).find('\x02') != std::string::npos) return true;
        return false;
    }
    std::unique_ptr<Type> useAliasTemplate() {
        AliasTmpl at = aliasTmpl_[cur().text];
        advance();
        std::vector<TArg> args;
        parseTemplateArgs(at.td, args);
        std::size_t after = i_;
        bool savedHalf = halfGt_;
        halfGt_ = false;
        auto es = bindParams(at.td, args);
        i_ = at.at;
        eatKw("typename");
        auto t = parseTypeName();
        // The alias names a member type its instance does not have (enable_if_t<false>): an error
        // here, a substitution failure in a function template's instance ([temp.alias] p2).
        if (!isP(";") && protoDepth_ == 0 && !hasPlaceholderTag(args)) err("the alias template cannot be formed");
        unbindParams(es);
        i_ = after;
        halfGt_ = savedHalf;
        return t;
    }
    // Our own type-trait builtins for <type_traits> ([meta.unary.prop], [meta.rel]), worked out from
    // what the parser knows of the types: __okay_is_base_of(B, D), __okay_is_class(T),
    // __okay_is_polymorphic(T), __okay_is_abstract(T), __okay_is_final(T), __okay_is_empty(T),
    // __okay_is_trivially_destructible(T), __okay_is_default_constructible(T),
    // __okay_is_trivially_copyable(T), __okay_is_array(T). Enumerations are ints in okay-cc, so there is
    // no is_enum.
    std::unique_ptr<Expr> typeTrait() {
        Token at0 = cur();
        std::string which = cur().text.substr(10);
        advance();
        advance();  // (
        std::vector<std::unique_ptr<Type>> ts;
        while (!atEnd() && !isP(")")) {
            ts.push_back(parseTypeName());
            if (!eatP(",")) break;
        }
        expectP(")", ")");
        auto clsOf = [&](const Type* t) -> const Type* {
            if (!t || t->kind != Type::Kind::Struct || t->tag.empty()) return nullptr;
            auto it = structs_.find(t->tag);
            return it != structs_.end() && it->second ? it->second.get() : canonOf(t);
        };
        long v = 0;
        const Type* t0 = ts.empty() ? nullptr : ts[0].get();
        const Type* c0 = clsOf(t0);
        if (which == "base_of" && ts.size() == 2) {
            const Type* b = clsOf(ts[0].get());
            const Type* d = clsOf(ts[1].get());
            if (b && d) {
                v = b->tag == d->tag;
                for (std::string t = d->base; !v && !t.empty();) {
                    if (t == b->tag) v = 1;
                    auto it = structs_.find(t);
                    t = it != structs_.end() && it->second ? it->second->base : std::string();
                }
            }
        } else if (which == "class") {
            v = c0 != nullptr;
        } else if (which == "invocable" && t0) {
            // is_invocable<F, A...> ([meta.rel]): F callable with as many arguments (a lambda's
            // closure, a class with operator(), a function or a pointer to one). The argument types
            // are not compared with the parameters here.
            const Type* f = t0;
            while (f && (f->kind == Type::Kind::Reference || f->kind == Type::Kind::Pointer) && f->inner) f = f->inner.get();
            std::size_t n = 0;
            for (std::size_t k = 1; k < ts.size(); ++k)
                if (!(ts[k] && ts[k]->kind == Type::Kind::Struct && ts[k]->tag == "std::__okay_no_arg")) ++n;
            if (f && f->kind == Type::Kind::Function) {
                v = f->params.size() == n || (f->variadic && f->params.size() <= n);
            } else if (f && f->kind == Type::Kind::Struct && f->tag.compare(0, 10, "__closure_") == 0) {
                auto la = lamArity_.find(f->tag);
                if (la != lamArity_.end()) v = la->second == n + 1;
            } else if (const Type* c = clsOf(f)) {
                for (const Param& q : c->params)
                    if (q.name == "operator()" && q.type && q.type->kind == Type::Kind::Function &&
                        q.type->params.size() == n)
                        v = 1;
            }
        } else if (which == "array") {
            v = t0 && t0->kind == Type::Kind::Array;  // [meta.unary.cat]
        } else if (which == "integral") {
            // bool, the character types and the integer types ([basic.fundamental]); an
            // enumeration is an int to okay-cc, so it counts too here.
            v = t0 && (t0->kind == Type::Kind::Bool || t0->kind == Type::Kind::Char || t0->kind == Type::Kind::Short ||
                       t0->kind == Type::Kind::Int || t0->kind == Type::Kind::Long);
        } else if (which == "polymorphic") {
            v = c0 && c0->hasVptr;
        } else if (which == "abstract") {
            if (c0)
                for (const std::string& slot : c0->vslots)
                    if (slot.empty()) v = 1;  // a pure virtual function left without an overrider
        } else if (which == "final") {
            v = c0 && finalTags_.count(c0->tag);
        } else if (which == "empty") {
            v = c0 && !c0->hasVptr;
            if (c0)
                for (const Param& f : c0->params)
                    if (f.mangled.empty() && f.type && f.type->kind != Type::Kind::Function && !f.name.empty() &&
                        f.name[0] != '<')
                        v = 0;
        } else if (which == "trivially_destructible") {
            v = !c0 || !c0->needDtor;
            if (t0 && t0->kind == Type::Kind::Void) v = 0;
        } else if (which == "trivially_copyable") {
            v = !c0 || (!c0->needDtor && !userCtors_.count(c0->tag));
        } else if (which == "default_constructible") {
            if (t0 && (t0->kind == Type::Kind::Reference || t0->kind == Type::Kind::Void)) v = 0;
            else if (!c0) v = 1;
            else {
                bool anyCtor = false;
                for (const Param& f : c0->params)
                    if (f.name == "<ctor>" && f.type) {
                        anyCtor = true;
                        bool all = true;
                        for (const Param& q : f.type->params)
                            if (!q.defArg) all = false;
                        if (all) v = 1;
                    }
                if (!anyCtor) v = 1;
                for (const std::string& slot : c0->vslots)
                    if (slot.empty()) v = 0;  // abstract
            }
        } else {
            diags_.error(at0.file.empty() ? "t.c" : at0.file, at0.line, at0.column, "unknown type trait '" + at0.text + "'");
        }
        auto lit = ex(Expr::Kind::Int, at0);
        lit->ival = static_cast<std::uint64_t>(v);
        lit->text = v ? "true" : "false";  // typed bool
        return lit;
    }
    // X<args> of a variable template: its value, worked out now.
    std::unique_ptr<Expr> useVarTemplate() {
        Token at0 = cur();
        AliasTmpl vt = varTmpl_[cur().text];
        advance();
        std::vector<TArg> args;
        parseTemplateArgs(vt.td, args);
        std::size_t after = i_;
        bool savedHalf = halfGt_;
        halfGt_ = false;
        auto es = bindParams(vt.td, args);
        i_ = vt.at;
        auto e = parseAssign();
        unbindParams(es);
        i_ = after;
        halfGt_ = savedHalf;
        long v = 0;
        if (!e || !evalConst(*e, v)) {
            diags_.error(at0.file.empty() ? "t.c" : at0.file, at0.line, at0.column,
                         "variable template '" + at0.text + "' is not a constant");
            v = 0;
        }
        auto lit = ex(Expr::Kind::Int, at0);
        lit->ival = static_cast<std::uint64_t>(v);
        if (v < 0) {
            lit->ival = static_cast<std::uint64_t>(-v);
            auto neg = ex(Expr::Kind::Unary, at0);
            neg->text = "-";
            neg->kids.push_back(std::move(lit));
            return neg;
        }
        return lit;
    }
    struct Spec {
        std::size_t start = 0;     // the 'struct' of template <> struct L<float> {
        std::size_t skipFrom = 0;  // its '<'
        std::size_t skipTo = 0;    // just after its '>'
    };
    std::map<std::string, Spec> specs_;  // explicit specializations by instance tag
    struct Partial {
        TDecl td;                  // its own parameters
        std::size_t start = 0;     // the 'class' keyword
        std::size_t skipFrom = 0;  // the '<' of its pattern
        std::size_t skipTo = 0;    // just after the pattern's '>'
    };
    std::map<std::string, std::vector<Partial>> partials_;
    std::vector<std::unique_ptr<Expr>>* collectDecltype_ = nullptr;  // decltype(e) read now go here
    int protoDepth_ = 0;  // reading a function template's prototype (parameters stand for themselves)

    // Match a pattern type (parameters as "\x02P" placeholders) against an argument type, binding
    // the parameters ([temp.deduct.type]). Instance tags are compared whole.
    bool matchPattern(const Type& pat, const Type& arg, const TDecl& td, std::vector<std::unique_ptr<Type>>& bound) {
        if (pat.kind == Type::Kind::Struct && !pat.tag.empty() && pat.tag[0] == '\x02') {
            for (std::size_t k = 0; k < td.params.size(); ++k) {
                if (td.params[k].name != pat.tag.substr(1)) continue;
                if (packPlaceGetsEnd(td.params[k].name, arg)) return false;
                auto a = cloneType(arg);
                if (pat.cnst) {
                    if (!a->cnst) return false;
                    a->cnst = false;
                }
                if (!bound[k]) {
                    bound[k] = std::move(a);
                    return true;
                }
                return sameType(*bound[k], *a);
            }
            return false;
        }
        if (pat.kind != arg.kind || pat.cnst != arg.cnst) return false;
        if (pat.kind == Type::Kind::Struct && pat.tag.find('\x02') != std::string::npos) {
            // A class template instance in the pattern (tuple_size<tuple<T...>>): its tag holds the
            // parameters; match it against the argument's tag piece by piece ([temp.deduct.type] p8).
            return matchInstanceTag(pat.tag, arg.tag, true, [&](const std::string& name, std::unique_ptr<Type> a) {
                for (std::size_t k = 0; k < td.params.size(); ++k) {
                    if (td.params[k].name != name) continue;
                    if (packPlaceGetsEnd(name, *a)) return false;
                    if (!bound[k]) {
                        bound[k] = std::move(a);
                        return true;
                    }
                    return sameType(*bound[k], *a);
                }
                return false;
            });
        }
        if (pat.kind == Type::Kind::Array && pat.unsized != arg.unsized) return false;
        if (pat.kind == Type::Kind::Reference && pat.rref != arg.rref) return false;
        if (pat.kind == Type::Kind::Function) {
            // R(A...) against a function type ([temp.deduct.type] p8): the return type and each
            // parameter type in turn.
            if (pat.variadic != arg.variadic || pat.params.size() != arg.params.size()) return false;
            if (!pat.inner || !arg.inner || !matchPattern(*pat.inner, *arg.inner, td, bound)) return false;
            for (std::size_t k = 0; k < pat.params.size(); ++k)
                if (!pat.params[k].type || !arg.params[k].type ||
                    !matchPattern(*pat.params[k].type, *arg.params[k].type, td, bound))
                    return false;
            return true;
        }
        if (pat.inner || arg.inner) {
            if (!pat.inner || !arg.inner) return false;
            return matchPattern(*pat.inner, *arg.inner, td, bound);
        }
        return sameType(pat, arg);
    }

    // The first partial specialization of 'name' whose pattern matches 'args': its index, and its
    // own arguments in 'own'; or -1.
    int pickPartial(const std::string& name, const TDecl& primary, const std::vector<TArg>& args,
                    std::vector<TArg>& own) {
        auto it = partials_.find(name);
        if (it == partials_.end()) return -1;
        for (std::size_t n = 0; n < it->second.size(); ++n) {
            const Partial ps = it->second[n];
            bool typesOnly = true;
            for (const TParam& tp : ps.td.params)
                if (!tp.isType) typesOnly = false;
            if (!typesOnly) continue;
            // The pattern, read with each of the specialization's parameters standing for itself.
            std::size_t saved = i_;
            bool savedHalf = halfGt_;
            halfGt_ = false;
            tscopes_.emplace_back();
            for (const TParam& tp : ps.td.params) {
                auto ph = ty(Type::Kind::Struct);
                ph->tag = "\x02" + tp.name;
                tscopes_.back()[tp.name] = std::move(ph);
            }
            i_ = ps.skipFrom;
            std::vector<TArg> pat;
            parseTemplateArgs(primary, pat, true);
            tscopes_.pop_back();
            i_ = saved;
            halfGt_ = savedHalf;
            if (pat.size() != args.size()) continue;
            std::vector<std::unique_ptr<Type>> bound(ps.td.params.size());
            bool ok = true;
            for (std::size_t k = 0; k < pat.size() && ok; ++k) {
                if (pat[k].isValue || args[k].isValue) ok = pat[k].isValue && args[k].isValue && pat[k].value == args[k].value;
                else if (!pat[k].type || !args[k].type) ok = false;
                else ok = matchPattern(*pat[k].type, *args[k].type, ps.td, bound);
            }
            for (auto& b : bound)
                if (!b) ok = false;
            if (!ok) continue;
            std::vector<TArg> mine;
            for (auto& b : bound) {
                TArg a;
                a.type = std::move(b);
                mine.push_back(std::move(a));
            }
            bool hasDecltype = false;
            for (std::size_t k = ps.skipFrom; k < ps.skipTo && k < toks_.size(); ++k)
                if (isKwTok(toks_[k], "decltype")) hasDecltype = true;
            if (hasDecltype) {
                // SFINAE in the pattern ([temp.spec.partial.match] p2, [temp.deduct] p8): the
                // pattern with these arguments must be formed. The decltype expressions in it are
                // given to the checker (specChecks); one that had no type in an earlier round has
                // put this key in sfinaeFailed(), and the specialization does not match.
                std::string key = name + "|" + std::to_string(n) + "|";
                for (const TArg& a : args) key += argMangle(a) + ",";
                if (sfinaeFailed().count(key)) continue;
                bool known = false;
                for (const SpecCheck& c : specChecks())
                    if (c.key == key) known = true;
                if (!known) {
                    SpecCheck chk;
                    chk.key = key;
                    std::vector<std::unique_ptr<Expr>>* savedCollect = collectDecltype_;
                    collectDecltype_ = &chk.exprs;
                    std::size_t at = i_;
                    bool half = halfGt_;
                    halfGt_ = false;
                    auto es = bindParams(ps.td, mine);
                    i_ = ps.skipFrom;
                    std::vector<TArg> formed;
                    parseTemplateArgs(primary, formed, true);
                    unbindParams(es);
                    i_ = at;
                    halfGt_ = half;
                    collectDecltype_ = savedCollect;
                    specChecks().push_back(std::move(chk));
                }
            }
            own = std::move(mine);
            return static_cast<int>(n);
        }
        return -1;
    }
    std::size_t specSkipFrom_ = 0, specSkipTo_ = 0;
    std::vector<InstReq> work_;
    std::set<std::string> instDone_;
    std::set<std::string> instInProgress_;
    std::string instOverrideName_;
    std::string instOverrideTag_;
    std::string instFnName_;
    bool halfGt_ = false;

    std::size_t skipDecl(std::size_t from) const {
        std::size_t k = from;
        int paren = 0;
        while (k < toks_.size() && toks_[k].kind != Tok::End) {
            const Token& t = toks_[k];
            if (isP(t, "(")) ++paren;
            else if (isP(t, ")")) --paren;
            else if (isP(t, "{") && paren == 0) {
                int depth = 0;
                while (k < toks_.size() && toks_[k].kind != Tok::End) {
                    if (isP(toks_[k], "{")) ++depth;
                    else if (isP(toks_[k], "}") && --depth == 0) break;
                    ++k;
                }
                ++k;
                if (k < toks_.size() && isP(toks_[k], ";")) ++k;
                return k;
            } else if (isP(t, ";") && paren == 0) {
                return k + 1;
            }
            ++k;
        }
        return k;
    }

    bool isAngleClose() const { return halfGt_ || isP(">") || isP(">>"); }
    void closeAngle() {
        if (halfGt_) {
            halfGt_ = false;
            advance();
            return;
        }
        if (isP(">")) advance();
        else if (isP(">>")) halfGt_ = true;
        else err("expected '>'");
    }

    std::int64_t constOf(const Expr& e) {
        if (e.kind == Expr::Kind::Int || e.kind == Expr::Kind::Char) return static_cast<std::int64_t>(e.ival);
        if (e.kind == Expr::Kind::Unary && e.text == "-" && !e.kids.empty() && e.kids[0])
            return -constOf(*e.kids[0]);
        // In a function template's prototype a value that depends on the parameters
        // (is_integral<T>::value) is known only in each instance ([temp.dep.constexpr]).
        if (protoDepth_ == 0) err("template argument is not a constant");
        return 0;
    }

    TArg parseOneArg(const TParam& tp) {
        TArg a;
        if (tp.isType) {
            auto b = parseBase();
            for (;;) {
                if (eatP("*")) {
                    auto ptr = ty(Type::Kind::Pointer);
                    ptr->inner = std::move(b);
                    b = std::move(ptr);
                    while (eatKw("const")) {
                    }
                } else if (isP("&") || isP("&&")) {
                    auto r = ty(Type::Kind::Reference);
                    r->rref = isP("&&");
                    advance();
                    r->inner = std::move(b);
                    b = std::move(r);
                } else {
                    break;
                }
            }
            if (isP("(")) {
                // A function type R(A, B) or a pointer to one R(*)(A) ([dcl.fct], [temp.arg.type]):
                // function<void(int)>.
                Parsed d = parseDeclarator(false);
                b = apply(std::move(b), d.mods);
            }
            if (isP("[")) {
                // T[] and T[N] ([dcl.array]): unique_ptr<int[]>.
                advance();
                auto arr = ty(Type::Kind::Array);
                if (!isP("]")) arr->bound = parseAssign();
                else arr->unsized = true;
                expectP("]", "]");
                arr->inner = std::move(b);
                b = std::move(arr);
            }
            a.type = std::move(b);
        } else {
            // A constant expression ([temp.arg.nontype]): N, -1, Trait<T>::value, !B, A && B, a == b
            // (a '>' would end the list, [temp.names] p3).
            auto e = parseAdd();
            while (isP("==") || isP("!=") || isP("&&") || isP("||") || isP("&") || isP("|")) {
                auto b = ex(Expr::Kind::Bin, cur());
                b->text = cur().text;
                advance();
                b->kids.push_back(std::move(e));
                b->kids.push_back(parseAdd());
                e = std::move(b);
            }
            a.isValue = true;
            long v = 0;
            if (e && evalConst(*e, v)) a.value = v;
            else a.value = constOf(*e);
        }
        return a;
    }

    // At '<': read the template arguments up to the closing '>', then fill in defaults.
    void parseTemplateArgs(const TDecl& td, std::vector<TArg>& out, bool partial = false) {
        advance();
        std::size_t idx = 0;
        while (!atEnd() && !isAngleClose()) {
            if (idx >= td.params.size()) {
                if (td.dropOne && idx == td.params.size()) {
                    TParam any;
                    (void)parseOneArg(any);  // (see TDecl::dropOne)
                    ++idx;
                    if (!eatP(",")) break;
                    continue;
                }
                err("too many template arguments");
                break;
            }
            out.push_back(parseOneArg(td.params[idx]));
            ++idx;
            if (!eatP(",")) break;
        }
        closeAngle();
        for (; idx < td.params.size(); ++idx) {
            if (!td.params[idx].hasDef) {
                // A function template may be given only its first arguments; the rest are deduced.
                if (!partial) err("too few template arguments");
                break;
            }
            // A default may name the earlier parameters (class C = vector<T>, [temp.param] p10):
            // they are bound while it is read.
            std::size_t saved = i_;
            bool savedHalf = halfGt_;
            halfGt_ = false;
            auto es = bindParams(td, out);
            i_ = td.params[idx].defStart;
            TArg a = parseOneArg(td.params[idx]);
            unbindParams(es);
            out.push_back(std::move(a));
            i_ = saved;
            halfGt_ = savedHalf;
        }
    }

    struct EnumSave {
        std::string name;
        bool had = false;
        std::int64_t old = 0;
    };
    std::vector<EnumSave> bindParams(const TDecl& td, const std::vector<TArg>& args) {
        std::vector<EnumSave> saved;
        tscopes_.emplace_back();
        for (std::size_t i = 0; i < td.params.size() && i < args.size(); ++i) {
            const TParam& tp = td.params[i];
            if (tp.isType && args[i].type) {
                tscopes_.back()[tp.name] = cloneType(*args[i].type);
            } else if (!tp.isType) {
                EnumSave es;
                es.name = tp.name;
                auto it = enums_.find(tp.name);
                if (it != enums_.end()) {
                    es.had = true;
                    es.old = it->second;
                }
                saved.push_back(es);
                enums_[tp.name] = args[i].value;
            }
        }
        return saved;
    }
    void unbindParams(const std::vector<EnumSave>& saved) {
        for (const EnumSave& es : saved) {
            if (es.had) enums_[es.name] = es.old;
            else enums_.erase(es.name);
        }
        if (!tscopes_.empty()) tscopes_.pop_back();
    }

    // Returns true when it declared a constructor template of the class being parsed.
    bool parseTemplate() {
        advance();
        if (!eatP("<")) {
            err("expected '<' after 'template'");
            syncSemi();
            return false;
        }
        TDecl td;
        for (const std::string& n : ns_) td.ns += (td.ns.empty() ? "" : "::") + n;
        bool closedByShift = false;  // "class D = default_delete<T>>": the '>>' also ends the list
        while (!atEnd() && !isP(">") && !closedByShift) {
            TParam tp;
            if (isKw("typename") || isKw("class")) {
                tp.isType = true;
                advance();
            } else {
                // A value parameter: "int N", "std::size_t N", "T v" (of a type parameter before it,
                // [temp.param] p4): the name is the last identifier before ',', '>' or '='.
                tp.isType = false;
                std::size_t k = i_;
                while (k < toks_.size() && toks_[k].kind != Tok::End && !isP(toks_[k], ",") && !isP(toks_[k], ">") &&
                       !isP(toks_[k], "=") && !isP(toks_[k], ">>"))
                    ++k;
                if (k > i_ && toks_[k - 1].kind == Tok::Ident) i_ = k - 1;
                else
                    while (!atEnd() && cur().kind != Tok::Ident && !isP(",") && !isP(">")) advance();
            }
            if (!atEnd() && cur().kind == Tok::Ident) {
                tp.name = cur().text;
                advance();
            } else if (!atEnd() && (isP("=") || isP(",") || isP(">"))) {
                // A parameter with no name ([temp.param] p1: template <class T, class = void>).
                tp.name = "__okay_unnamed" + std::to_string(td.params.size());
            } else {
                err("expected a template parameter name");
                break;
            }
            if (eatP("=")) {
                tp.hasDef = true;
                tp.defStart = i_;
                int depth = 0;
                while (!atEnd()) {
                    if (isP("<")) ++depth;
                    else if (isP(">")) {
                        if (depth == 0) break;
                        --depth;
                    } else if (isP(">>") && depth <= 1) {
                        closedByShift = depth == 1;
                        break;
                    } else if (isP(">>")) {
                        depth -= 2;
                    } else if (isP(",") && depth == 0) break;
                    advance();
                }
            }
            // A parameter of our library named __okay_drop..: its argument is read and left out
            // (parseTemplateArgs), so it is not one of the template's parameters
            // (std::vector<T, Alloc>: the elements are allocated the one way our vector has).
            if (tp.isType && tp.name.compare(0, 11, "__okay_drop") == 0) td.dropOne = true;
            else td.params.push_back(tp);
            if (closedByShift || !eatP(",")) break;
        }
        if (closedByShift) advance();
        else expectP(">", ">");
        td.start = i_;
        td.end = skipDecl(i_);
        if (td.params.empty() && (isKw("class") || isKw("struct")) && i_ + 2 < toks_.size() &&
            toks_[i_ + 1].kind == Tok::Ident && isP(toks_[i_ + 2], "<") && ctmpl_.count(toks_[i_ + 1].text)) {
            // template <> struct L<float> { ... }: an explicit specialization ([temp.expl.spec]). Its
            // arguments are read as the primary template's; the instance with exactly these
            // arguments is made from this body instead.
            std::string name = toks_[i_ + 1].text;
            TDecl primary = ctmpl_[name];
            Spec sp;
            sp.start = td.start;
            i_ = td.start + 2;
            sp.skipFrom = i_;
            std::vector<TArg> args;
            parseTemplateArgs(primary, args);
            sp.skipTo = i_;
            std::string tag = name + "<";
            for (std::size_t k = 0; k < args.size(); ++k) {
                if (k) tag += ",";
                tag += argMangle(args[k]);
            }
            tag += ">";
            specs_[tag] = sp;
            i_ = td.end;
            return false;
        }
        if ((isKw("class") || isKw("struct")) && i_ + 2 < toks_.size() && toks_[i_ + 1].kind == Tok::Ident &&
            isP(toks_[i_ + 2], "<") && ctmpl_.count(toks_[i_ + 1].text)) {
            // template <class T, class D> class unique_ptr<T[], D> { ... }: a partial specialization
            // ([temp.spec.partial]). Its pattern is matched against the arguments of each instance.
            Partial ps;
            ps.td = td;
            ps.start = td.start;
            ps.skipFrom = i_ + 2;
            std::size_t gt = i_ + 2;
            int depth = 0;
            for (; gt < td.end; ++gt) {
                if (isP(toks_[gt], "<")) ++depth;
                else if (isP(toks_[gt], ">") && --depth == 0) break;
                else if (isP(toks_[gt], ">>") && (depth -= 2) <= 0) break;
            }
            ps.skipTo = gt + 1;
            partials_[toks_[i_ + 1].text].push_back(ps);
            i_ = td.end;
            return false;
        }
        if (isKw("class") || isKw("struct")) {
            if (i_ + 1 < toks_.size() && toks_[i_ + 1].kind == Tok::Ident) ctmpl_[toks_[i_ + 1].text] = td;
            else err("expected a class template name");
            i_ = td.end;
            return false;
        }
        if (isKw("using") && i_ + 2 < toks_.size() && toks_[i_ + 1].kind == Tok::Ident && isP(toks_[i_ + 2], "=")) {
            // An alias template ([temp.alias]): template <class T> using X_t = typename X<T>::type;
            // a use X_t<A> reads the type after '=' with the parameters bound.
            AliasTmpl at;
            at.td = td;
            at.at = i_ + 3;
            aliasTmpl_[toks_[i_ + 1].text] = at;
            i_ = td.end;
            return false;
        }
        {
            // A variable template ([temp.pre] p1): template <class T> inline constexpr bool X_v = ...;
            // made a constant at each use X_v<A> (only constant ones are supported).
            int paren = 0;
            std::size_t eq = 0;
            for (std::size_t k = td.start; k < td.end; ++k) {
                if (isP(toks_[k], "(")) ++paren;
                else if (isP(toks_[k], ")")) --paren;
                else if (paren == 0 && isP(toks_[k], "{")) break;
                else if (paren == 0 && isP(toks_[k], "=")) {
                    eq = k;
                    break;
                }
            }
            if (eq > td.start && toks_[eq - 1].kind == Tok::Ident && toks_[eq - 1].text != "operator" && paren == 0) {
                AliasTmpl vt;
                vt.td = td;
                vt.at = eq + 1;
                varTmpl_[toks_[eq - 1].text] = vt;
                i_ = td.end;
                return false;
            }
        }
        std::string name;
        std::size_t nameIdx = 0;
        int paren = 0;
        for (std::size_t k = td.start; k < td.end; ++k) {
            if (paren == 0 && toks_[k].kind == Tok::Ident && toks_[k].text == "operator" && k + 1 < td.end &&
                toks_[k + 1].kind == Tok::Punct) {
                // An operator template: template <class U> P &operator=(const P<U> &o) ([over.oper]).
                std::string op = toks_[k + 1].text;
                if ((op == "(" || op == "[") && k + 2 < td.end && isP(toks_[k + 2], op == "(" ? ")" : "]"))
                    op += op == "(" ? ")" : "]";
                name = "operator" + op;
                break;
            }
            if (isP(toks_[k], "(")) {
                if (paren == 0 && k > td.start && toks_[k - 1].kind == Tok::Ident && toks_[k - 1].text != "decltype") {
                    name = toks_[k - 1].text;
                    nameIdx = k - 1;
                    break;
                }
                ++paren;
            } else if (isP(toks_[k], ")") && paren > 0) {
                --paren;  // decltype(auto) f(..): the name follows the closed group
            }
        }
        if (name.empty()) {
            err("expected a function template");
            i_ = td.end;
            return false;
        }
        if (!memberCls_ && nameIdx >= td.start + 2 && isP(toks_[nameIdx - 1], "::")) {
            // template <class T> T *C::f() const { .. }: the definition of a member function template
            // declared in class C ([temp.mem] p1). Its instances are read from here.
            std::size_t b = nameIdx - 1;
            std::string prefix;
            while (b >= td.start + 1 && isP(toks_[b], "::") && toks_[b - 1].kind == Tok::Ident) {
                prefix = toks_[b - 1].text + (prefix.empty() ? "" : "::" + prefix);
                if (b < td.start + 2) break;
                b -= 2;
            }
            std::string cls = classPrefix(prefix);
            if (!cls.empty()) {
                std::string key = cls + "::" + name;
                for (int k = 1; memberTmpl_.count(key); ++k) {
                    MemberTmpl& mt = memberTmpl_[key];
                    bool body = false;
                    for (std::size_t q = mt.td.start; q < mt.td.end && !body; ++q) body = isP(toks_[q], "{");
                    if (!body && !mt.hasDef) {
                        mt.hasDef = true;
                        mt.def = td;
                        i_ = td.end;
                        return false;
                    }
                    key = cls + "::" + name + "#" + std::to_string(k);
                }
            }
        }
        // The prototype, with each type parameter standing for itself, is what deduction matches.
        TemplateSig sig;
        std::vector<EnumSave> saved;
        tscopes_.emplace_back();
        for (const TParam& tp : td.params) {
            sig.names.push_back(tp.name);
            sig.isType.push_back(tp.isType);
            sig.hasDef.push_back(tp.hasDef);
            if (tp.isType) {
                auto ph = ty(Type::Kind::Struct);
                ph->tag = "\x02" + tp.name;
                tscopes_.back()[tp.name] = std::move(ph);
            } else {
                EnumSave es;
                es.name = tp.name;
                auto it = enums_.find(tp.name);
                if (it != enums_.end()) {
                    es.had = true;
                    es.old = it->second;
                }
                saved.push_back(es);
                enums_[tp.name] = 0;
            }
        }
        // A constructor template ([temp.mem] p1, [class.ctor]): "template <class U> C(U *p) : ...".
        std::size_t nameAt = td.start;
        while (nameAt < td.end && (isKwTok(toks_[nameAt], "explicit") || isKwTok(toks_[nameAt], "constexpr") ||
                                   isKwTok(toks_[nameAt], "inline")))
            ++nameAt;
        bool isStaticTmpl = false;
        bool isCtor = memberCls_ && !memberShort_.empty() && nameAt + 1 < td.end &&
                      toks_[nameAt].kind == Tok::Ident && toks_[nameAt].text == memberShort_ &&
                      isP(toks_[nameAt + 1], "(");
        if (isCtor) {
            i_ = nameAt + 2;
            std::vector<Param> ps;
            parseParams(ps);
            sig.fn = fnTypeOf(ps);
            name = "<ctor>";
        } else {
            // A static member function template ([temp.mem], [class.static.mfct]): "template <class T>
            // static void f(T)". It is called without an object, so it is kept like a function
            // template whose name is qualified by its class.
            i_ = td.start;
            while (isKw("static") || isKw("inline") || isKw("constexpr")) {
                if (isKw("static") && memberCls_) isStaticTmpl = true;
                advance();
            }
            ++protoDepth_;
            auto base = parseBase();
            Parsed d = parseDeclarator(true);
            --protoDepth_;
            sig.fn = apply(std::move(base), d.mods);
        }
        for (const EnumSave& es : saved) {
            if (es.had) enums_[es.name] = es.old;
            else enums_.erase(es.name);
        }
        tscopes_.pop_back();
        i_ = td.end;
        // More than one function template may have this name ([temp.over]): the others are kept as
        // name#1, name#2, ...; the checker deduces against each.
        if (memberCls_) {
            // A member function template ([temp.mem]): kept with its class; the class gets a
            // member whose symbol is "\x01mtmpl:key", which the checker deduces and instantiates.
            std::string mkey = memberTag_ + "::" + name;
            for (int k = 1; memberTmpl_.count(mkey); ++k) mkey = memberTag_ + "::" + name + "#" + std::to_string(k);
            MemberTmpl mt;
            mt.td = td;
            mt.tag = memberTag_;
            mt.isCtor = isCtor;
            mt.isStatic = isStaticTmpl;
            // The names the class sees: its template parameters' bindings (the scope the outermost
            // class was begun in, unless that is the file's own scope) and the class scopes.
            std::size_t from = classScopes_ > 0 ? (classOuter_ > 0 ? classOuter_ : 1) : (tscopes_.empty() ? 0 : tscopes_.size() - 1);
            for (std::size_t k = from; k < tscopes_.size(); ++k)
                for (auto& kv : tscopes_[k]) mt.scope.emplace_back(kv.first, cloneType(*kv.second));
            if (isStaticTmpl) {
                // Found by its name (Name(..) in the class, C::Name(..) outside it); the checker
                // looks the qualified key up from the class or namespace it is in.
                staticTmplShort_.insert(name);
            } else {
                Param m;
                m.name = name;
                m.mangled = "\x01" "mtmpl:" + mkey;
                m.type = cloneType(*sig.fn);
                memberCls_->params.push_back(std::move(m));
            }
            funcTemplateSigs()[mkey] = std::move(sig);
            memberTmpl_[mkey] = std::move(mt);
            return isCtor;
        }
        std::string key = name;
        for (int k = 1; ftmpl_.count(key); ++k) key = name + "#" + std::to_string(k);
        funcTemplateSigs()[key] = std::move(sig);
        ftmpl_[key] = td;
        return false;
    }

    std::unique_ptr<Type> instantiateClass(const std::string& name) {
        TDecl td = ctmpl_[name];
        advance();
        std::vector<TArg> args;
        if (isP("<")) parseTemplateArgs(td, args);
        else err("a class template needs its arguments in '<' '>'");
        std::string tag = name + "<";
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i) tag += ",";
            tag += argMangle(args[i]);
        }
        tag += ">";
        if (tag.find('\x02') != std::string::npos && !structs_.count(tag)) {
            // An instance that depends on template parameters (tuple<T &...> in tie's prototype,
            // tuple<H, T...> in a partial specialization's pattern): only the tag is needed, for
            // deduction and matching; its members are made per real instance. (Making each such
            // class, with the tail classes it holds, for every pack length took seconds a file.)
            auto dep = ty(Type::Kind::Struct);
            dep->tag = tag;
            return dep;
        }
        if (!td.ns.empty()) instNamespaces()[tag] = td.ns;
        if (instInProgress_.count(tag)) {
            // The class names itself (Node<T> *next inside Node<T>): the tag stands for it.
            auto self = ty(Type::Kind::Struct);
            self->tag = tag;
            return self;
        }
        if (!structs_.count(tag) && specs_.count(tag)) {
            const Spec sp = specs_[tag];
            instInProgress_.insert(tag);
            std::size_t saved = i_;
            auto savedPre = std::move(preStmts_);
            auto savedLocals = std::move(localNames_);  // a class body does not see the function's locals
            localNames_.clear();
            preStmts_.clear();
            bool savedHalf = halfGt_;
            halfGt_ = false;
            instOverrideName_ = name;
            instOverrideTag_ = tag;
            specSkipFrom_ = sp.skipFrom;
            specSkipTo_ = sp.skipTo;
            i_ = sp.start;
            parseBase();
            instOverrideName_.clear();
            specSkipFrom_ = specSkipTo_ = 0;
            i_ = saved;
            preStmts_ = std::move(savedPre);
            localNames_ = std::move(savedLocals);
            halfGt_ = savedHalf;
            instInProgress_.erase(tag);
        }
        std::vector<TArg> own;
        int pi = structs_.count(tag) || args.size() != td.params.size() ? -1 : pickPartial(name, td, args, own);
        if (pi >= 0) {
            const Partial ps = partials_[name][static_cast<std::size_t>(pi)];
            instInProgress_.insert(tag);
            std::size_t saved = i_;
            auto savedPre = std::move(preStmts_);
            auto savedLocals = std::move(localNames_);  // a class body does not see the function's locals
            localNames_.clear();
            preStmts_.clear();
            bool savedHalf = halfGt_;
            halfGt_ = false;
            auto es = bindParams(ps.td, own);
            instOverrideName_ = name;
            instOverrideTag_ = tag;
            std::size_t savedFrom = specSkipFrom_, savedTo = specSkipTo_;
            specSkipFrom_ = ps.skipFrom;
            specSkipTo_ = ps.skipTo;
            i_ = ps.start;
            std::size_t mark = extras_.size();
            parseBase();
            for (std::size_t k = mark; k < extras_.size(); ++k)
                if (extras_[k] && extras_[k]->function && extras_[k]->body) extras_[k]->fromInstance = true;
            instOverrideName_.clear();
            specSkipFrom_ = savedFrom;
            specSkipTo_ = savedTo;
            unbindParams(es);
            i_ = saved;
            preStmts_ = std::move(savedPre);
            localNames_ = std::move(savedLocals);
            halfGt_ = savedHalf;
            instInProgress_.erase(tag);
        }
        if (!structs_.count(tag) && args.size() == td.params.size()) {
            instInProgress_.insert(tag);
            std::size_t saved = i_;
            auto savedPre = std::move(preStmts_);
            auto savedLocals = std::move(localNames_);  // a class body does not see the function's locals
            localNames_.clear();
            preStmts_.clear();
            bool savedHalf = halfGt_;
            halfGt_ = false;
            auto es = bindParams(td, args);
            instOverrideName_ = name;
            instOverrideTag_ = tag;
            i_ = td.start;
            std::size_t mark = extras_.size();
            parseBase();
            // The member functions of an instance are made only if they are used ([temp.inst] p4):
            // the checker checks one only once something refers to it, and drops the rest.
            for (std::size_t k = mark; k < extras_.size(); ++k)
                if (extras_[k] && extras_[k]->function && extras_[k]->body) extras_[k]->fromInstance = true;
            instOverrideName_.clear();
            unbindParams(es);
            i_ = saved;
            preStmts_ = std::move(savedPre);
            localNames_ = std::move(savedLocals);
            halfGt_ = savedHalf;
            instInProgress_.erase(tag);
        }
        auto it = structs_.find(tag);
        if (it == structs_.end() || !it->second) return ty(Type::Kind::Int);
        return typeRef(*it->second);
    }

    struct MemberTmpl {
        TDecl td;
        std::string tag;
        bool isCtor = false;  // a constructor template
        bool isStatic = false;  // a static member function template (no 'this')
        bool hasDef = false;    // defined after the class: def is that definition
        TDecl def;
        std::vector<std::pair<std::string, std::unique_ptr<Type>>> scope;  // the class's own bindings
    };
    std::set<std::string> staticTmplShort_;  // bare names of static member function templates
    // The template arguments given at a call travel as carriers (casts holding a type or a value);
    // the checker deduces the rest.
    void addCarriers(Expr& e, const std::vector<TArg>& args, const Token& at) {
        for (const TArg& a : args) {
            auto carrier = ex(Expr::Kind::Cast, at);
            if (a.isValue) {
                carrier->text = "v";
                carrier->ival = static_cast<std::uint64_t>(a.value);
            } else if (a.type) {
                carrier->ty = cloneType(*a.type);
            }
            e.kids.push_back(std::move(carrier));
        }
    }
    // The key of the static member function template that the qualified name 'chain' (C::f, N::C::f
    // or f) names, seen from the class or namespace being parsed; empty if none.
    std::string staticTmplKey(const std::string& chain) const {
        std::vector<std::string> scopes;
        for (std::string t = memberTag_; !t.empty();) {
            scopes.push_back(t);
            std::size_t cut = t.rfind("::");
            if (cut == std::string::npos) break;
            t = t.substr(0, cut);
        }
        for (std::size_t keep = ns_.size(); keep > 0; --keep) {
            std::string q;
            for (std::size_t k = 0; k < keep; ++k) q += (k ? "::" : "") + ns_[k];
            scopes.push_back(q);
        }
        scopes.push_back("");
        for (const std::string& sc : scopes) {
            std::string key = sc.empty() ? chain : sc + "::" + chain;
            auto it = memberTmpl_.find(key);
            if (it != memberTmpl_.end() && it->second.isStatic) return key;
        }
        std::string tail = "::" + chain;
        for (const auto& kv : memberTmpl_)
            if (kv.second.isStatic && kv.first.size() > tail.size() &&
                kv.first.compare(kv.first.size() - tail.size(), tail.size(), tail) == 0)
                return kv.first;
        return {};
    }
    std::map<std::string, MemberTmpl> memberTmpl_;
    // A non-static member function template of some class with this name (the first one).
    const MemberTmpl* memberTmplNamed(const std::string& name) const {
        std::string tail = "::" + name;
        for (const auto& kv : memberTmpl_) {
            if (kv.second.isStatic || kv.second.isCtor) continue;
            std::string k = kv.first;
            std::size_t hash = k.find('#');
            if (hash != std::string::npos) k.resize(hash);
            if (k.size() > tail.size() && k.compare(k.size() - tail.size(), tail.size(), tail) == 0) return &kv.second;
        }
        return nullptr;
    }
    std::string memberTag_;
    std::string memberShort_;  // the class's own name, as its constructors are written
    Type* memberCls_ = nullptr;

    // An instance of a member function template: its tokens read again inside the class's scope
    // (the class template's parameters bound as they were), with its own parameters bound.
    void replayMember(Tu& tu, const InstReq& r, MemberTmpl& mt) {
        std::size_t saved = i_;
        MemberScopeGuard memberScope{*this, memberScope_};
        tscopes_.emplace_back();
        for (auto& kv : mt.scope) tscopes_.back()[kv.first] = cloneType(*kv.second);
        {
            // and the class's nested classes, also those declared after the template ([class.mem]
            // p7: a member function body is a complete-class context)
            const std::string pre = mt.tag + "::";
            for (auto it = memberTypes_.lower_bound(pre); it != memberTypes_.end() && it->first.compare(0, pre.size(), pre) == 0; ++it)
                if (it->second && it->first.find("::", pre.size()) == std::string::npos &&
                    !tscopes_.back().count(it->first.substr(pre.size())))
                    tscopes_.back()[it->first.substr(pre.size())] = cloneType(*it->second);
        }
        const TDecl& from = mt.hasDef ? mt.def : mt.td;  // the definition after the class, if any
        std::string savedBody = bodyClass_;
        bodyClass_ = mt.tag;
        struct BodyBack {
            std::string& b;
            std::string v;
            ~BodyBack() { b = v; }
        } bodyBack{bodyClass_, savedBody};
        auto es = bindParams(from, r.args);
        // Names in the instance are looked up from the namespace the template was declared in
        // ([temp.res] p10): savefmt::FacadeGuard inside okay::Save::Locked is okay::savefmt::...
        struct NsBack {
            std::vector<std::string>& ns;
            std::vector<std::string> v;
            ~NsBack() { ns = std::move(v); }
        } nsBack{ns_, ns_};
        if (ns_.empty() && !from.ns.empty()) {
            for (std::size_t a = 0;;) {
                std::size_t b = from.ns.find("::", a);
                ns_.push_back(from.ns.substr(a, b == std::string::npos ? std::string::npos : b - a));
                if (b == std::string::npos) break;
                a = b + 2;
            }
        }
        i_ = from.start;
        Pos pos = here();
        if (mt.isCtor) {
            // An instance of a constructor template: built like a constructor written outside its class.
            while (isKw("explicit") || isKw("constexpr") || isKw("inline")) advance();
            CtorDef cd;
            cd.pos = here();
            advance();
            advance();
            parseParams(cd.params);
            parseInitList(cd);
            if (isP("{")) {
                auto vt = ty(Type::Kind::Void);
                cd.body = parseBody(vt.get());
                cd.hasBody = true;
                auto decl = buildCtor(mt.tag, cd, r.name);
                flushExtras(tu);
                if (decl) {
                    decl->isInline = true;
                    tu.decls.push_back(std::move(decl));
                }
            } else {
                err("a constructor template must be defined in its class");
            }
            unbindParams(es);
            tscopes_.pop_back();
            i_ = saved;
            return;
        }
        while (isKw("static") || isKw("inline") || isKw("constexpr")) advance();
        auto base = parseBase();
        Parsed d = parseDeclarator(true);
        auto type = apply(std::move(base), d.mods);
        while (isKw("const") || isKw("noexcept")) advance();
        if (mt.isStatic && type && type->kind == Type::Kind::Function && isP("{")) {
            // An instance of a static member function template: a function with no 'this' whose
            // names are looked up in its class ([class.static.mfct]).
            auto decl = std::make_unique<Decl>();
            decl->name = r.name;
            decl->function = true;
            decl->staticMember = true;
            decl->methodTag = mt.tag;
            decl->isInline = true;
            decl->line = pos.line;
            decl->col = pos.col;
            decl->file = pos.file;
            const Type* ret = type->inner.get();
            for (const Param& q : type->params) lastParams_.push_back(q.name);
            std::string savedTag = memberTag_;
            memberTag_ = mt.tag;
            decl->body = parseBody(ret);
            memberTag_ = savedTag;
            decl->type = std::move(type);
            flushExtras(tu);
            tu.decls.push_back(std::move(decl));
        } else if (type && type->kind == Type::Kind::Function && isP("{")) {
            auto decl = newMethodDecl(mt.tag, r.name, *type, type->params, pos);
            decl->isInline = true;
            decl->body = parseBody(type->inner.get());
            flushExtras(tu);
            tu.decls.push_back(std::move(decl));
        } else {
            err("a member function template must be defined in its class");
        }
        unbindParams(es);
        tscopes_.pop_back();
        i_ = saved;
    }

    void replayFunction(Tu& tu, const InstReq& r) {
        auto mit = memberTmpl_.find(r.tmpl);
        if (mit != memberTmpl_.end()) {
            replayMember(tu, r, mit->second);
            return;
        }
        auto it = ftmpl_.find(r.tmpl);
        if (it == ftmpl_.end()) {
            err("unknown function template '" + r.tmpl + "'");
            return;
        }
        TDecl td = it->second;
        std::size_t saved = i_;
        // The template's own arguments: a defaulted parameter that deduction left (kDefaultArgTag)
        // gets its default, read with the parameters before it bound ([temp.deduct] p5).
        std::vector<TArg> args = cloneArgs(r.args);
        const std::size_t diagMark = diags_.items().size();
        const bool savedStop = stop_;
        for (std::size_t k = 0; k < args.size() && k < td.params.size(); ++k) {
            if (!args[k].type || args[k].type->kind != Type::Kind::Struct || args[k].type->tag != kDefaultArgTag ||
                !td.params[k].hasDef)
                continue;
            bool savedHalf = halfGt_;
            halfGt_ = false;
            auto des = bindParams(td, args);
            i_ = td.params[k].defStart;
            args[k] = parseOneArg(td.params[k]);
            // (a member type the instance does not have, enable_if<false>::type, stops the read
            // before the end of the default)
            if (!isP(",") && !isP(">") && !isP(">>") && !halfGt_) err("the default template argument cannot be formed");
            unbindParams(des);
            halfGt_ = savedHalf;
        }
        auto es = bindParams(td, args);
        if (diags_.items().size() == diagMark) {
            // The signature, read on its own first: a type that cannot be formed there
            // (enable_if<false>::type) is a substitution failure, not an error ([temp.deduct] p8).
            i_ = td.start;
            while (isKw("static") || isKw("inline") || isKw("constexpr") || isKw("extern")) advance();
            std::vector<std::string> savedParams = lastParams_;
            const bool savedReported = reported_;
            auto base = parseBase();
            Parsed d = parseDeclarator(true);
            (void)base;
            (void)d;
            lastParams_ = std::move(savedParams);
            if (diags_.items().size() != diagMark) reported_ = savedReported;
        }
        if (diags_.items().size() != diagMark) {
            diags_.truncate(diagMark);
            stop_ = savedStop;
            sfinaeFailed().insert(r.tmpl + "|" + r.name);
            instDone_.erase(r.name);  // another overload may make an instance of this name
            // The other templates of this name with the same parameters (the enable_if pairs) are
            // tried with the same arguments now, so overload resolution finds the one that fits
            // in the next round without a round of its own.
            const std::string base = r.tmpl.substr(0, r.tmpl.find('#'));
            for (int k = 0;; ++k) {
                std::string other = k == 0 ? base : base + "#" + std::to_string(k);
                auto ot = ftmpl_.find(other);
                if (ot == ftmpl_.end()) break;
                if (other == r.tmpl || ot->second.params.size() != r.args.size()) continue;
                bool same = true;
                for (std::size_t q = 0; q < r.args.size(); ++q)
                    if (ot->second.params[q].isType == r.args[q].isValue) same = false;
                auto os = funcTemplateSigs().find(other);
                if (!same || os == funcTemplateSigs().end()) continue;
                InstReq o;
                o.tmpl = other;
                o.args = cloneArgs(r.args);
                o.name = instFuncName(other, os->second, o.args);
                if (sfinaeFailed().count(other + "|" + o.name)) continue;
                bool known = false;
                for (const InstReq& q : instRequests())
                    if (q.tmpl == o.tmpl && q.name == o.name) known = true;
                if (known) continue;
                InstReq copy;
                copy.tmpl = o.tmpl;
                copy.args = cloneArgs(o.args);
                copy.name = o.name;
                instRequests().push_back(std::move(copy));
                work_.push_back(std::move(o));
            }
            unbindParams(es);
            i_ = saved;
            return;
        }
        if (!td.ns.empty()) instNamespaces()[r.name] = td.ns;
        instFnName_ = r.name;
        i_ = td.start;
        std::size_t mark = tu.decls.size();
        parseExternal(tu);
        // A template instance may be made in every file that uses it ([temp.inst]): weak.
        for (std::size_t k = mark; k < tu.decls.size(); ++k)
            if (tu.decls[k] && tu.decls[k]->function && tu.decls[k]->name == r.name) tu.decls[k]->isInline = true;
        instFnName_.clear();
        unbindParams(es);
        i_ = saved;
    }

    void processInstances(Tu& tu) {
        for (const InstReq& r : instRequests()) {
            InstReq c;
            c.tmpl = r.tmpl;
            c.args = cloneArgs(r.args);
            c.name = r.name;
            work_.push_back(std::move(c));
        }
        runWork(tu);
    }

    void runWork(Tu& tu) {
        // A member template of a class instance that only another instance's body makes (the
        // constructor template of shared_ptr<D> used inside make_shared<D>) waits until that body
        // has been read; it is an error only when a whole pass makes nothing new.
        std::vector<InstReq> waiting;
        for (;;) {
            bool progress = false;
            while (!work_.empty() && !stop_) {
                InstReq r = std::move(work_.back());
                work_.pop_back();
                if (instDone_.count(r.name)) continue;
                if (!memberTmpl_.count(r.tmpl) && !ftmpl_.count(r.tmpl)) {
                    waiting.push_back(std::move(r));
                    continue;
                }
                instDone_.insert(r.name);
                replayFunction(tu, r);
                progress = true;
            }
            if (waiting.empty() || stop_) break;
            if (!progress) {
                for (const InstReq& r : waiting) err("unknown function template '" + r.tmpl + "'");
                break;
            }
            for (auto& r : waiting) work_.push_back(std::move(r));
            waiting.clear();
        }
    }

    // extern "C" declaration  /  extern "C" { declarations }  (ISO C++ [dcl.link]). C linkage means
    // the name is not mangled, which is already how plain names are emitted.
    bool parseLinkage(Tu& tu) {
        if (!isKw("extern") || i_ + 1 >= toks_.size() || toks_[i_ + 1].kind != Tok::StringLit) return false;
        advance();
        bool savedC = cLink_;
        cLink_ = cur().text == "\"C\"" || cur().text == "C";
        struct Restore {
            bool& flag;
            bool saved;
            ~Restore() { flag = saved; }
        } restore{cLink_, savedC};
        advance();
        if (eatP("{")) {
            while (!atEnd() && !isP("}")) {
                std::size_t before = i_;
                if (isUnsupported()) {
                    err("'" + cur().text + "' is not in this subset");
                    syncSemi();
                } else if (!isTypeKw(cur())) {
                    err("expected a declaration");
                    syncSemi();
                } else {
                    parseExternal(tu);
                }
                if (stop_) break;
                if (i_ == before) advance();
            }
            expectP("}", "}");
        } else if (!atEnd() && (isTypeKw(cur()))) {
            parseExternal(tu);
        } else {
            err("expected a declaration");
        }
        return true;
    }

    // namespace N { }, namespace A::B { } (C++17 [namespace.def]) and the unnamed namespace
    // namespace { }. Names in an unnamed namespace are looked up as names of the enclosing
    // namespace and get internal linkage (they are emitted as static).
    void parseNamespace(Tu& tu) {
        advance();
        std::size_t pushed = 0;
        bool unnamed = false;
        if (isP("{")) {
            unnamed = true;
            ++anonNs_;
        } else {
            if (atEnd() || cur().kind != Tok::Ident) {
                err("expected a namespace name");
                return;
            }
            ns_.push_back(cur().text);
            ++pushed;
            advance();
            while (eatP("::")) {
                if (atEnd() || cur().kind != Tok::Ident) {
                    err("expected a namespace name");
                    break;
                }
                ns_.push_back(cur().text);
                ++pushed;
                advance();
            }
            std::string full;
            for (const std::string& part : ns_) full += (full.empty() ? "" : "::") + part;
            knownNs_.insert(full);
        }
        expectP("{", "{");
        while (!atEnd() && !isP("}")) {
            std::size_t before = i_;
            if (isKw("using")) parseUsing();
            else if (isKw("namespace")) parseNamespace(tu);
            else if (isKw("template")) parseTemplate();
            else if (parseStaticAssert()) {
            }
            else if (parseLinkage(tu)) {
            } else if (isUnsupported()) {
                err("'" + cur().text + "' is not in this subset");
                syncSemi();
            } else if (!isTypeKw(cur())) {
                err("expected a declaration");
                syncSemi();
            } else {
                parseExternal(tu);
            }
            if (stop_) break;
            if (i_ == before) advance();
        }
        if (isP("}")) advance();
        for (std::size_t k = 0; k < pushed && !ns_.empty(); ++k) ns_.pop_back();
        if (unnamed && anonNs_ > 0) --anonNs_;
    }

    void parseInitList(CtorDef& cd) {
        if (!eatP(":")) return;
        while (!atEnd() && !isP("{") && !isP(";")) {
            MemInit mi;
            if (cur().kind == Tok::Ident && ctmpl_.count(cur().text) && nextIs("<")) {
                auto bt = instantiateClass(cur().text);
                mi.name = bt ? bt->tag : "";
            } else if (cur().kind == Tok::Ident) {
                mi.name = cur().text;
                advance();
                while (eatP("::")) {
                    if (atEnd() || cur().kind != Tok::Ident) break;
                    mi.name += "::";
                    mi.name += cur().text;
                    advance();
                }
            } else {
                err("expected a member name");
                break;
            }
            if (eatP("(")) {
                if (!eatP(")")) {
                    mi.args.push_back(parseAssign());
                    while (eatP(",")) mi.args.push_back(parseAssign());
                    expectP(")", ")");
                }
            } else if (isP("{")) {
                // m{a, b} ([class.base.init] p3, [dcl.init.list]): the values as for m(a, b)
                auto list = parseInitializer();
                if (list && list->kind == Expr::Kind::Brace)
                    for (auto& k : list->kids) mi.args.push_back(std::move(k));
                else if (list)
                    mi.args.push_back(std::move(list));
            } else {
                err("expected '(' after a member initializer");
                break;
            }
            cd.inits.push_back(std::move(mi));
            if (!eatP(",")) break;
        }
        // 'new T(args)' in an initializer declares a hidden pointer: it goes first in the constructor.
        for (auto& st : preStmts_) cd.pre.push_back(std::move(st));
        preStmts_.clear();
    }

    // using B::B; in a class whose base is B: B's constructors are constructors of this class too
    // ([namespace.udecl] p3, [class.inhctor.init]). Each becomes C(params) : B(params) {}, with B's
    // default arguments; B's copy and move constructors are not inherited ([over.match.funcs] p8).
    bool inheritCtors(ClassCtx& cx, Type& T) {
        if (T.base.empty()) return false;
        std::size_t j = i_ + 1;
        std::string q;
        while (j + 1 < toks_.size() && toks_[j].kind == Tok::Ident && isP(toks_[j + 1], "::")) {
            q += (q.empty() ? "" : "::") + toks_[j].text;
            j += 2;
        }
        if (q.empty() || j + 1 >= toks_.size() || toks_[j].kind != Tok::Ident || !isP(toks_[j + 1], ";")) return false;
        if (classPrefix(q) != T.base || toks_[j].text != shortName(T.base)) return false;
        const Type* B = classOf(T.base);
        if (!B) return false;
        i_ = j + 2;
        Pos p = here();
        for (const Param& bc : B->params) {
            if (bc.name != "<ctor>" || !bc.type || bc.type->params.empty() || deletedFns_.count(bc.mangled)) continue;
            const Type* a0 = bc.type->params[0].type.get();
            if (bc.type->params.size() == 1 && a0 && a0->kind == Type::Kind::Reference && a0->inner &&
                a0->inner->tag == T.base)
                continue;
            auto fnT = cloneType(*bc.type, true);
            CtorDef cd;
            cd.pos = p;
            MemInit mi;
            mi.name = shortName(T.base);
            for (std::size_t k = 0; k < fnT->params.size(); ++k) {
                Param& prm = fnT->params[k];
                prm.name = "__ia" + std::to_string(k);
                std::unique_ptr<Expr> arg = eName(prm.name, p);
                // (a T&& parameter is passed on as an xvalue, as std::forward would)
                if (prm.type && prm.type->kind == Type::Kind::Reference && prm.type->rref) arg = eXvalue(std::move(arg), p);
                mi.args.push_back(std::move(arg));
                cd.params.push_back(std::move(prm));
            }
            cd.inits.push_back(std::move(mi));
            cd.body = sBlock(p);
            cd.hasBody = true;
            Param cp;
            cp.name = "<ctor>";
            auto fn = fnTypeOf(cd.params);
            cp.mangled = mangleSpecial(cx.tag, "C1", *fn);
            cp.type = std::move(fn);
            bool dup = false;  // a constructor the class declares itself hides the inherited one
            for (const Param& q2 : T.params)
                if (q2.name == "<ctor>" && q2.mangled == cp.mangled) dup = true;
            if (dup) continue;
            T.params.push_back(std::move(cp));
            cx.ctors.push_back(std::move(cd));
            userCtors_.insert(cx.tag);
        }
        cx.cxx = true;
        return true;
    }

    // "S::S(...) : init { }" and "S::~S() { }" written outside the class.
    bool tryOutOfClassSpecial() {
        // (inline C::C(..) {..}: the word is skipped; a definition in a header is weak anyway)
        if (isKw("inline") && i_ + 3 < toks_.size() && toks_[i_ + 1].kind == Tok::Ident && isP(toks_[i_ + 2], "::")) {
            std::size_t keep = i_;
            advance();
            ooInline_ = true;
            bool done = tryOutOfClassSpecial();
            ooInline_ = false;
            if (done) return true;
            i_ = keep;
            return false;
        }
        if (atEnd() || cur().kind != Tok::Ident || !nextIs("::") || i_ + 2 >= toks_.size()) return false;
        const Type* td = findTypedef(cur().text);
        if (!td || td->tag.empty() || !classOf(td->tag)) return false;
        std::string tag = td->tag;
        const Token& third = toks_[i_ + 2];
        bool isCtor = third.kind == Tok::Ident && third.text == cur().text && i_ + 3 < toks_.size() &&
                      isP(toks_[i_ + 3], "(");
        bool isDtor = isP(third, "~");
        if (!isCtor && !isDtor) return false;
        Type* T = classOf(tag);
        Pos pos = here();
        if (isCtor) {
            advance();
            advance();
            advance();
            advance();
            enterMemberScope(tag);  // C::C(Type t): the class's member types ([basic.lookup.unqual] p8)
            CtorDef cd;
            cd.pos = pos;
            parseParams(cd.params);
            skipNoexcept();  // C::C(C &&) noexcept = default; C::C() noexcept : x(1) {}
            parseInitList(cd);
            auto fn = fnTypeOf(cd.params);
            std::string mangled = mangleSpecial(tag, "C1", *fn);
            bool declared = false;
            for (const Param& p : T->params)
                if (p.name == "<ctor>" && p.mangled == mangled) declared = true;
            if (!declared) err("no constructor with these parameters is declared in the class");
            auto vt = ty(Type::Kind::Void);
            if (isP("=") && i_ + 2 < toks_.size() && isKwTok(toks_[i_ + 1], "default") && isP(toks_[i_ + 2], ";")) {
                i_ += 3;  // S::S() = default;
                cd.body = sBlock(pos);
                cd.hasBody = true;
                defaultCopyCtor(cd, tag, *T);  // S::S(S &&) = default: memberwise
                auto decl = buildCtor(tag, cd);
                if (decl) {
                    decl->isInline = decl->isInline || ooInline_;
                    extras_.push_back(std::move(decl));
                }
            } else if (isP("{")) {
                cd.body = parseBody(vt.get());
                cd.hasBody = true;
                auto decl = buildCtor(tag, cd);
                if (decl) {
                    decl->isInline = decl->isInline || ooInline_;
                    extras_.push_back(std::move(decl));
                }
            } else {
                err("expected a constructor body");
            }
            return true;
        }
        advance();
        advance();
        advance();
        advance();
        expectP("(", "(");
        expectP(")", ")");
        skipNoexcept();
        if (!special(T, "<dtor>")) err("the class does not declare a destructor");
        auto vt = ty(Type::Kind::Void);
        if (isP("{")) {
            auto body = parseBody(vt.get());
            auto decl = buildDtor(tag, std::move(body), pos, false);
            if (decl) {
                    decl->isInline = decl->isInline || ooInline_;
                    extras_.push_back(std::move(decl));
                }
        } else if (isP("=") && i_ + 2 < toks_.size() && isKwTok(toks_[i_ + 1], "default") && isP(toks_[i_ + 2], ";")) {
            // S::~S() = default; ([dcl.fct.def.default]): an empty body; members are destroyed as usual.
            i_ += 3;
            auto decl = buildDtor(tag, sBlock(pos), pos, false);
            if (decl) {
                    decl->isInline = decl->isInline || ooInline_;
                    extras_.push_back(std::move(decl));
                }
        } else {
            err("expected a destructor body");
        }
        return true;
    }

    bool ooInline_ = false;  // reading "inline C::C(..)" (a weak definition)
    void parseExternal(Tu& tu) {
        Guard nest(*this);
        if (!nest) return;
        MemberScopeGuard memberScope{*this, memberScope_};
        if (parseStaticAssert()) return;
        if (tryOutOfClassSpecial()) {
            flushExtras(tu);
            return;
        }
        bool ext = false, st = anonNs_ > 0, td = false, inl = false, inlVar = false, cexpr = false;
        for (;;) {
            if (eatKw("extern")) ext = true;
            else if (eatKw("static")) st = true;
            else if (eatKw("typedef")) td = true;
            else if (eatKw("inline")) inl = inlVar = true;
            else if (eatKw("constexpr")) inl = cexpr = true;
            else break;
        }
        auto base = parseBase();
        if (base && (base->kind == Type::Kind::Struct || base->kind == Type::Kind::Union) && isP(";")) {
            advance();
            flushExtras(tu);
            return;
        }
        if (enumDef_ && isP(";")) {
            enumDef_ = false;
            advance();
            flushExtras(tu);
            return;
        }
        enumDef_ = false;
        if (atEnd()) {
            err("expected a declarator");
            flushExtras(tu);
            return;
        }
        auto keep = [&](Parsed& d, std::unique_ptr<Type> type, std::unique_ptr<Expr> init) {
            if (td) addTypedef(d.name, std::move(type));
            else {
                std::size_t dc = d.name.rfind("::");
                if (dc != std::string::npos) {
                    // The definition of a static data member: T C::x = v;
                    std::string cls = classPrefix(d.name.substr(0, dc));
                    if (!cls.empty()) d.name = cls + "::" + d.name.substr(dc + 2);
                }
                if (init && type && (cexpr || type->cnst) && isIntish(*type)) {
                    // A namespace-scope integer constant: array lengths that name it are known while
                    // parsing (an array of objects with constructors needs its length then).
                    const Expr* v = init.get();
                    if (v->kind == Expr::Kind::Int) classConsts_[qualify(d.name)] = static_cast<long>(v->ival);
                }
                auto decl = makeDecl(d, std::move(type), std::move(init));
                if (ext && !decl->init) decl->isExtern = true;
                // A constexpr variable at namespace scope has internal linkage ([basic.link] p3); an
                // inline variable has one copy in the program, kept by the linker ([dcl.inline]).
                decl->isStatic = st || (cexpr && !inlVar && !decl->function);
                decl->isInline = inlVar && !decl->isStatic;
                tu.decls.push_back(std::move(decl));
            }
        };
        // A global object of a class with a constructor or destructor: "G g(args);" or "G g;".
        // Its constructor runs at the start of main and its destructor when main returns
        // (only for a main defined later in this file, see the plan).
        // (Also "T C::x = e;", the definition of a static data member of class type.)
        std::size_t gEnd = i_;
        std::string gQual;
        if (!td && !atEnd() && cur().kind == Tok::Ident) {
            std::size_t j = i_;
            std::string prefix;
            while (j + 2 < toks_.size() && isP(toks_[j + 1], "::") && toks_[j + 2].kind == Tok::Ident) {
                prefix += (prefix.empty() ? "" : "::") + toks_[j].text;
                j += 2;
            }
            if (j > i_) {
                std::string cls = classPrefix(prefix);
                if (!cls.empty()) {
                    gEnd = j;
                    gQual = cls + "::" + toks_[j].text;
                    // R C::f(Type t): the class's member types decide that '(' starts parameters.
                    if (j + 1 < toks_.size() && isP(toks_[j + 1], "(")) enterMemberScope(cls);
                }
            }
        }
        // (also G g = e; for a class without constructors when e is no braced list: g = V::Zero
        // copies another object when the program starts, [basic.start.dynamic])
        const bool copyOfObject = gEnd + 2 < toks_.size() && isP(toks_[gEnd + 1], "=") && !isP(toks_[gEnd + 2], "{") &&
                                  base && base->kind == Type::Kind::Struct && !base->tag.empty() && classOf(base->tag) &&
                                  !classOf(base->tag)->params.empty() && bodyDepth_ == 0;
        if (!td && !ext && base && base->kind == Type::Kind::Struct && !base->tag.empty() && classOf(base->tag) &&
            (classOf(base->tag)->needCtor || classOf(base->tag)->needDtor || copyOfObject) && !atEnd() &&
            cur().kind == Tok::Ident && gEnd + 1 < toks_.size() &&
            (isP(toks_[gEnd + 1], ";") || (isP(toks_[gEnd + 1], "=") && gEnd + 2 < toks_.size() && !isP(toks_[gEnd + 2], "{")) ||
             (isP(toks_[gEnd + 1], "(") && gEnd + 2 < toks_.size() && !isP(toks_[gEnd + 2], ")") && !isTypeKw(toks_[gEnd + 2])) ||
             // G g = {..} and G g{..} for a class with constructors of its own ([dcl.init.list] p3):
             // a constructor call (round 24 stored the list as bytes: std::vector<int> g = {1, 2}
             // crashed at its first use).
             (userCtors_.count(base->tag) &&
              (isP(toks_[gEnd + 1], "{") ||
               (isP(toks_[gEnd + 1], "=") && gEnd + 2 < toks_.size() && isP(toks_[gEnd + 2], "{")))))) {
            Parsed gd;
            gd.name = gQual.empty() ? cur().text : gQual;
            gd.line = cur().line;
            gd.col = cur().column;
            gd.file = cur().file;
            i_ = gEnd + 1;
            std::vector<std::unique_ptr<Expr>> args;
            std::unique_ptr<Expr> copyInit;  // G g = e; (copy-initialization, [dcl.init] p17.6)
            if (isP("{") || (isP("=") && i_ + 1 < toks_.size() && isP(toks_[i_ + 1], "{"))) {
                eatP("=");
                auto list = parseInitializer();
                bool listCtor = hasListCtor(*classOf(base->tag));
                if (list && list->kind == Expr::Kind::Brace && listCtor && !list->kids.empty()) {
                    list->ival = 5;  // the whole list to the initializer-list constructor
                    args.push_back(std::move(list));
                } else if (list && list->kind == Expr::Kind::Brace) {
                    args = std::move(list->kids);
                } else if (list) {
                    copyInit = std::move(list);
                }
            } else if (eatP("(")) {
                if (!eatP(")")) {
                    args.push_back(parseAssign());
                    while (eatP(",")) args.push_back(parseAssign());
                    expectP(")", ")");
                }
            } else if (eatP("=")) {
                copyInit = parseAssign();
            }
            expectP(";", ";");
            Pos gp{gd.line, gd.col, gd.file};
            std::string tag = base->tag;
            auto decl = makeDecl(gd, cloneType(*base), nullptr);
            decl->isStatic = st;
            gd.name = decl->name;  // namespace-qualified: the init and fini functions are at file scope
            std::string guard;
            if (inlVar && !st) {
                // An inline variable of class type ([dcl.inline] p6): one object in the program
                // (a weak symbol), built by the first file's __okay_init_ to run, which sets a
                // weak guard flag (Itanium C++ ABI 2.8 guard variables, our own spelling).
                decl->isInline = true;
                guard = "__okay_ig_";
                for (char ch : decl->name) guard += std::isalnum(static_cast<unsigned char>(ch)) ? ch : '_';
                auto gf = std::make_unique<Decl>();
                gf->name = guard;
                gf->line = gd.line;
                gf->col = gd.col;
                gf->file = gd.file;
                gf->type = ty(Type::Kind::Int);
                gf->isInline = true;
                tu.decls.push_back(std::move(gf));
            }
            tu.decls.push_back(std::move(decl));
            if (!guard.empty()) {
                auto thenB = sBlock(gp);
                thenB->stmts.push_back(sExpr(eAssign(eName(guard, gp), eInt(1, gp), gp), gp));
                if (copyInit) {
                    std::vector<std::unique_ptr<Expr>> a;
                    a.push_back(eAddr(eName(gd.name, gp), gp));
                    a.push_back(std::move(copyInit));
                    thenB->stmts.push_back(sExpr(eCall(eName("\x01" "cinit:" + tag, gp), std::move(a), gp), gp));
                } else if (classOf(tag)->needCtor) {
                    thenB->stmts.push_back(sExpr(ctorCall(tag, eAddr(eName(gd.name, gp), gp), std::move(args), gp), gp));
                }
                auto notG = eName(guard, gp);
                auto iff = std::make_unique<Stmt>();
                iff->kind = Stmt::Kind::If;
                iff->line = gp.line;
                iff->col = gp.col;
                iff->file = gp.file;
                auto neg = std::make_unique<Expr>();
                neg->kind = Expr::Kind::Unary;
                neg->text = "!";
                neg->line = gp.line;
                neg->col = gp.col;
                neg->file = gp.file;
                neg->kids.push_back(std::move(notG));
                iff->expr = std::move(neg);
                iff->thenS = std::move(thenB);
                globalInit_.push_back(std::move(iff));
            } else if (copyInit) {
                // The checker chooses: the copy constructor for an object of the class, else the
                // constructor that takes the value ("\x01cinit:", as for a local "T x = e;").
                std::vector<std::unique_ptr<Expr>> a;
                a.push_back(eAddr(eName(gd.name, gp), gp));
                a.push_back(std::move(copyInit));
                globalInit_.push_back(sExpr(eCall(eName("\x01" "cinit:" + tag, gp), std::move(a), gp), gp));
            } else if (classOf(tag)->needCtor) {
                globalInit_.push_back(sExpr(ctorCall(tag, eAddr(eName(gd.name, gp), gp), std::move(args), gp), gp));
            }
            if (classOf(tag)->needDtor) globalObjs_.push_back(DVar{gd.name, tag, 0, guard});
            ++globalCount_;
            flushExtras(tu);
            return;
        }
        Parsed d = parseDeclarator(true);
        auto type = apply(cloneType(*base), d.mods);
        bool defConst = false;
        if (type && type->kind == Type::Kind::Function)
            while (isKw("const") || isKw("noexcept")) {
                if (isKw("const")) defConst = true;
                if (isKw("noexcept")) skipNoexcept();  // noexcept(expr) too
                else advance();
            }
        if (!td && type && type->kind == Type::Kind::Function && isP("=") && i_ + 2 < toks_.size() &&
            isKwTok(toks_[i_ + 1], "default") && isP(toks_[i_ + 2], ";")) {
            // C &C::operator=(C &&) noexcept = default; ([dcl.fct.def.default]): the implicit one's
            // body, memberwise.
            std::size_t cc = d.name.rfind("::");
            std::string clsTag = cc == std::string::npos ? "" : classPrefix(d.name.substr(0, cc));
            if (!clsTag.empty() && d.name.substr(cc + 2) == "operator=") {
                i_ += 3;
                std::string mangled;
                for (const Param& p : classOf(clsTag)->params)
                    if (p.name == "operator=" && !p.mangled.empty() && p.type && sameParamTypes(*p.type, *type))
                        mangled = p.mangled;
                std::unique_ptr<Decl> decl;
                if (!mangled.empty()) decl = defaultAssign(clsTag, mangled, *type, Pos{d.line, d.col, d.file});
                if (!decl) err("only a copy or move assignment declared in the class can be defaulted here");
                else {
                    decl->isInline = inl;
                    extras_.push_back(std::move(decl));
                }
                flushExtras(tu);
                return;
            }
        }
        if (!td && type && type->kind == Type::Kind::Function && d.name.find("::") == std::string::npos)
            fnNames_.insert(d.name);
        if (!td && type && type->kind == Type::Kind::Function && isP("{")) {
            std::size_t cc = d.name.rfind("::");
            std::string prefix = cc == std::string::npos ? "" : d.name.substr(0, cc);
            std::string clsTag = classPrefix(prefix);
            if (!clsTag.empty() && staticFns_[clsTag].count(d.name.substr(cc + 2))) {
                // A static member function defined outside its class.
                std::string q = clsTag + "::" + d.name.substr(cc + 2);
                auto decl = std::make_unique<Decl>();
                decl->name = q;
                decl->line = d.line;
                decl->col = d.col;
                decl->file = d.file;
                decl->function = true;
                decl->staticMember = true;
                decl->methodTag = clsTag;
                decl->asmName = mangleFunc(q, *type);
                decl->isInline = inl;
                const Type* ret = type->inner.get();
                decl->type = std::move(type);
                std::size_t memberMark = pushMemberNames(classOf(clsTag));
                decl->body = parseBody(ret);
                localNames_.resize(memberMark);
                extras_.push_back(std::move(decl));
                flushExtras(tu);
                return;
            }
            if (!clsTag.empty()) {
                // A member function defined outside its class.
                std::string fname = d.name.substr(cc + 2);
                std::string mangled;
                for (const Param& p : classOf(clsTag)->params)
                    if (p.name == fname && !p.mangled.empty() && !isSpecialName(p.name) && p.type &&
                        sameParamTypes(*p.type, *type) && p.cmethod == defConst)
                        mangled = p.mangled;
                if (mangled.empty()) {
                    err("'" + fname + "' is not declared in class '" + clsTag + "' with these parameters");
                } else {
                    auto decl = newMethodDecl(clsTag, mangled, *type, type->params, Pos{d.line, d.col, d.file});
                    decl->isInline = inl;
                    std::string savedBody = bodyClass_;
                    bodyClass_ = clsTag;
                    std::size_t memberMark = pushMemberNames(classOf(clsTag));
                    decl->body = parseBody(type->inner.get());
                    localNames_.resize(memberMark);
                    bodyClass_ = savedBody;
                    extras_.push_back(std::move(decl));
                }
                flushExtras(tu);
                return;
            }
            auto decl = makeDecl(d, std::move(type), nullptr);
            decl->isStatic = st;
            decl->isInline = inl;
            if (false) {
                // The globals declared so far are constructed first thing in main and destroyed
                // when it returns, latest first.
                dscopes_.emplace_back(globalObjs_);
                decl->body = parseBody(decl->type ? decl->type->inner.get() : nullptr, 1);
                Pos mp{decl->line, decl->col, decl->file};
                if (decl->body && decl->body->kind == Stmt::Kind::Block) {
                    auto ds = pendingDtors(dscopes_.size() - 1, mp);
                    for (auto& st2 : ds) decl->body->stmts.push_back(std::move(st2));
                    decl->body->stmts.insert(decl->body->stmts.begin(), std::make_move_iterator(globalInit_.begin()),
                                             std::make_move_iterator(globalInit_.end()));
                }
                globalInit_.clear();
                dscopes_.pop_back();
                globalCount_ = 0;
            } else {
                decl->body = parseBody(decl->type ? decl->type->inner.get() : nullptr);
            }
            flushBefore(tu);
            tu.decls.push_back(std::move(decl));
            flushExtras(tu);
            return;
        }
        std::unique_ptr<Expr> init;
        if (eatP("=")) init = parseInitializer();
        else if (isP("{") && type && type->kind != Type::Kind::Function) init = parseInitializer();  // T x{...};
        keep(d, std::move(type), std::move(init));
        while (eatP(",")) {
            Parsed d2 = parseDeclarator(true);
            auto t2 = apply(cloneType(*base), d2.mods);
            std::unique_ptr<Expr> init2;
            if (eatP("=")) init2 = parseInitializer();
            keep(d2, std::move(t2), std::move(init2));
        }
        expectP(";", ";");
        flushExtras(tu);
    }

    std::unique_ptr<Stmt> declStmt(std::unique_ptr<Decl> d) {
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::Decl;
        s->line = d->line;
        s->col = d->col;
        s->decl = std::move(d);
        return s;
    }


    // A local of class type: run its constructor right after the declaration and remember it so
    // that its destructor runs when the scope ends.
    // A static local (an object, or a scalar with a dynamic initializer): a global __slN_name (zero at
    // first) and a guard __sgN declared before the function, and the local name as a reference to
    // the global. Returns the block that runs once (it sets the guard; the caller adds the
    // initialization).
    std::unique_ptr<Stmt> staticGuarded(std::vector<std::unique_ptr<Stmt>>& out, const std::string& name,
                                        const Type& type, const Pos& p, std::string& gname, std::string& guard) {
        int n = newN_++;
        gname = "__sl" + std::to_string(n) + "_" + name;
        guard = "__sg" + std::to_string(n);
        if (!localNames_.empty() && localNames_.back() == name)
            fnStatics_.push_back(FnStatic{name, nullptr, gname, localNames_.size() - 1});
        auto g = std::make_unique<Decl>();
        g->name = gname;
        g->line = p.line;
        g->col = p.col;
        g->file = p.file;
        g->type = cloneType(type);
        g->type->cnst = false;
        beforeDecls_.push_back(std::move(g));
        auto gf = std::make_unique<Decl>();
        gf->name = guard;
        gf->line = p.line;
        gf->col = p.col;
        gf->file = p.file;
        gf->type = ty(Type::Kind::Int);
        beforeDecls_.push_back(std::move(gf));
        auto refDecl = std::make_unique<Decl>();
        refDecl->name = name;
        refDecl->line = p.line;
        refDecl->col = p.col;
        refDecl->file = p.file;
        auto rt = ty(Type::Kind::Reference);
        rt->inner = cloneType(type);
        refDecl->type = std::move(rt);
        refDecl->init = eName(gname, p);
        out.push_back(declStmt(std::move(refDecl)));
        auto thenB = sBlock(p);
        thenB->stmts.push_back(sExpr(eAssign(eName(guard, p), eInt(1, p), p), p));
        return thenB;
    }
    // if (!guard) { ... }
    std::unique_ptr<Stmt> guardIf(const std::string& guard, std::unique_ptr<Stmt> thenB, const Pos& p) {
        auto notG = std::make_unique<Expr>();
        notG->kind = Expr::Kind::Unary;
        notG->text = "!";
        notG->line = p.line;
        notG->col = p.col;
        notG->file = p.file;
        notG->kids.push_back(eName(guard, p));
        auto iff = std::make_unique<Stmt>();
        iff->kind = Stmt::Kind::If;
        iff->line = p.line;
        iff->col = p.col;
        iff->file = p.file;
        iff->expr = std::move(notG);
        iff->thenS = std::move(thenB);
        return iff;
    }
    // An initializer the object file can hold as bytes: a literal, maybe negated or cast.
    static bool constantInit(const Expr& e) {
        switch (e.kind) {
        case Expr::Kind::Int:
        case Expr::Kind::Char:
        case Expr::Kind::Float:
        case Expr::Kind::String: return true;
        case Expr::Kind::Unary:
            if (e.text == "&") return true;  // an address constant (&g; as before)
            return (e.text == "-" || e.text == "+" || e.text == "~" || e.text == "!") && !e.kids.empty() && e.kids[0] &&
                   constantInit(*e.kids[0]);
        case Expr::Kind::Cast: return !e.kids.empty() && e.kids[0] && constantInit(*e.kids[0]);
        case Expr::Kind::Brace: return true;  // (left as before)
        default: return false;
        }
    }
    static bool deepConstant(const Expr& e) {
        if (e.kind != Expr::Kind::Brace) return constantInit(e) || e.kind == Expr::Kind::Name;  // (names as before)
        for (const auto& k : e.kids)
            if (k && !deepConstant(*k)) return false;
        return true;
    }
    // An array whose elements are numbers, pointers or classes that need no constructor or destructor.
    bool plainElements(const Type& t) {
        const Type* e = &t;
        while (e->kind == Type::Kind::Array && e->inner) e = e->inner.get();
        if (e->kind != Type::Kind::Struct && e->kind != Type::Kind::Union) return true;
        const Type* c = e->tag.empty() ? nullptr : classOf(e->tag);
        return c && !c->needCtor && !c->needDtor;
    }
    // A local of an inner block that takes the name of an object with a destructor of an enclosing
    // block ([basic.scope.block]; std::vector v, then if (auto *v = ..)): a return or break from
    // inside the inner block destroyed `v`, which there names the new local. The object gets a
    // hidden pointer, declared just before the new local, and is destroyed through it while that
    // pointer is in scope (pendingDtors).
    void shadowLocals(std::vector<std::unique_ptr<Stmt>>& out, const std::string& name, int line, int col,
                      const std::string& file) {
        if (name.empty() || dscopes_.size() < 2 || !isLocalName(name)) return;
        for (std::size_t sc = dscopes_.size() - 1; sc-- > dbase_;)
            for (DVar& v : dscopes_[sc]) {
                if (v.kind != 0 || v.count != 0 || v.name != name || !v.guard.empty() || v.tag.empty() ||
                    v.tag[0] == '\x03' || (!v.alias.empty() && isLocalName(v.alias)))
                    continue;
                auto d = std::make_unique<Decl>();
                d->name = "__dal" + std::to_string(newN_++);
                d->line = line;
                d->col = col;
                d->file = file;
                auto cls = ty(Type::Kind::Struct);
                cls->tag = v.tag;
                d->type = ty(Type::Kind::Pointer);
                d->type->inner = std::move(cls);
                Pos p{line, col, file};
                d->init = eAddr(eName(name, p), p);
                v.alias = d->name;
                localNames_.push_back(d->name);
                out.push_back(declStmt(std::move(d)));
            }
    }
    // The type of a new-expression as the parser writes it, (__newN = (T *)malloc(..), ..): T *.
    static const Type* newExprType(const Expr& e) {
        if (e.kind != Expr::Kind::Comma || e.kids.empty() || !e.kids[0]) return nullptr;
        const Expr& a = *e.kids[0];
        if (a.kind != Expr::Kind::Assign || a.kids.size() != 2 || !a.kids[1] || a.kids[1]->kind != Expr::Kind::Cast)
            return nullptr;
        const Type* t = a.kids[1]->ty.get();
        return t && t->kind == Type::Kind::Pointer ? t : nullptr;
    }
    void addLocal(std::vector<std::unique_ptr<Stmt>>& out, std::unique_ptr<Decl> decl,
                  std::vector<std::unique_ptr<Expr>> ctorArgs, bool haveCtorArgs) {
        std::string name = decl->name;
        localConstAt_.erase(localNames_.size());
        if (decl->type && decl->init && !decl->isStatic &&
            (decl->type->kind == Type::Kind::Int || decl->type->kind == Type::Kind::Long ||
             decl->type->kind == Type::Kind::Short || decl->type->kind == Type::Kind::Char)) {
            const Expr* v = decl->init.get();
            if (v->kind == Expr::Kind::Brace && v->kids.size() == 1 && v->kids[0]) v = v->kids[0].get();
            long cv = 0;
            if (evalConst(*v, cv)) localConstAt_[localNames_.size()] = {name, cv};
        }
        // static const T table[] = {{"up", KeyUp}, ..}; in a member function, KeyUp a static constant
        // of the class: its value, so the table is constant data (Input/Input.hpp).
        if (decl->isStatic && decl->init && decl->init->kind == Expr::Kind::Brace && bodyDepth_ > 0)
            boundConsts(*decl->init);
        shadowLocals(out, name, decl->line, decl->col, decl->file);
        localNames_.push_back(name);
        Pos p;
        p.line = decl->line;
        p.col = decl->col;
        p.file = decl->file;
        if (decl->isStatic && decl->type && decl->type->kind == Type::Kind::Struct && !decl->type->tag.empty() &&
            classOf(decl->type->tag) && (classOf(decl->type->tag)->needCtor || classOf(decl->type->tag)->needDtor) &&
            !name.empty()) {
            // A static local object: a real global plus a guard flag. The local name is a reference
            // to the global; the constructor runs the first time control passes the declaration and
            // the destructor runs at program end (if it was built).
            // static T x = e; and static T x{a, b}; ([stmt.dcl] p3): built the first time through,
            // by the constructor the initializer chooses.
            std::unique_ptr<Expr> copyInit;
            if (decl->init && decl->init->kind == Expr::Kind::Brace) {
                for (auto& k : decl->init->kids) ctorArgs.push_back(std::move(k));
                decl->init.reset();
            } else if (decl->init) {
                copyInit = std::move(decl->init);
            }
            std::string tag = decl->type->tag;
            std::string gname;
            std::string guard;
            auto thenB = staticGuarded(out, name, *decl->type, p, gname, guard);
            if (copyInit) {
                std::vector<std::unique_ptr<Expr>> a;
                a.push_back(eAddr(eName(gname, p), p));
                a.push_back(std::move(copyInit));
                thenB->stmts.push_back(sExpr(eCall(eName("\x01" "cinit:" + tag, p), std::move(a), p), p));
            } else if (classOf(tag)->needCtor || !ctorArgs.empty()) {
                thenB->stmts.push_back(sExpr(ctorCall(tag, eAddr(eName(gname, p), p), std::move(ctorArgs), p), p));
            }
            out.push_back(guardIf(guard, std::move(thenB), p));
            if (classOf(tag)->needDtor) staticObjs_.push_back(DVar{gname, tag, 0, guard});
            return;
        }
        if (decl->isStatic && decl->init && decl->init->kind == Expr::Kind::Brace && decl->type &&
            decl->type->kind == Type::Kind::Array && !name.empty() && bodyDepth_ > 0 && !deepConstant(*decl->init) &&
            plainElements(*decl->type)) {
            // static const T table[] = {{"f1", KeyF(1)}, ..}; with an element that is not a constant
            // (a call of a constexpr function): filled element by element the first time control
            // passes the declaration ([stmt.dcl] p3), as for a scalar below.
            if (!decl->type->bound) {
                decl->type->bound = eInt(decl->init->kids.size(), p);
                decl->type->unsized = false;
            }
            std::string gname;
            std::string guard;
            auto thenB = staticGuarded(out, name, *decl->type, p, gname, guard);
            arrayInit(thenB->stmts, *eName(gname, p), decl->type.get(), decl->init.get(), p);
            out.push_back(guardIf(guard, std::move(thenB), p));
            return;
        }
        if (decl->isStatic && decl->init && decl->type && bodyDepth_ > 0) {
            // static auto *s = new T(..); the global made for it needs the type now: T *.
            const Type* a = decl->type.get();
            if (a->kind == Type::Kind::Pointer && a->inner) a = a->inner.get();
            if (a->kind == Type::Kind::Struct && a->tag == "\x03" "auto")
                if (const Type* nt = newExprType(*decl->init)) decl->type = cloneType(*nt);
        }
        if (decl->isStatic && decl->init && decl->type && !name.empty() && bodyDepth_ > 0 &&
            (decl->type->kind == Type::Kind::Bool || decl->type->kind == Type::Kind::Char ||
             decl->type->kind == Type::Kind::Short || decl->type->kind == Type::Kind::Int ||
             decl->type->kind == Type::Kind::Long || decl->type->kind == Type::Kind::Float ||
             decl->type->kind == Type::Kind::Double || decl->type->kind == Type::Kind::Pointer) &&
            !constantInit(*decl->init)) {
            // static T x = e; with e not a constant (a call, a lambda called at once): the object is
            // zero first ([basic.start.static] p2) and takes e's value the first time control
            // passes the declaration ([stmt.dcl] p3), under a guard flag as for a static object.
            std::string gname;
            std::string guard;
            auto thenB = staticGuarded(out, name, *decl->type, p, gname, guard);
            thenB->stmts.push_back(sExpr(eAssign(eName(gname, p), std::move(decl->init), p), p));
            out.push_back(guardIf(guard, std::move(thenB), p));
            return;
        }
        const Type* ty0 = decl->type.get();
        std::uint64_t arrayCount = 0;
        if (ty0 && ty0->kind == Type::Kind::Array && ty0->inner && ty0->inner->kind == Type::Kind::Struct &&
            !ty0->inner->tag.empty() && classOf(ty0->inner->tag) &&
            (classOf(ty0->inner->tag)->needCtor || classOf(ty0->inner->tag)->needDtor)) {
            if (ty0->bound && ty0->bound->kind != Expr::Kind::Int) {
                boundConsts(*decl->type->bound);
                long cv = 0;
                if (decl->type->bound->kind != Expr::Kind::Int && evalConst(*decl->type->bound, cv) && cv >= 0)
                    decl->type->bound = eInt(static_cast<std::uint64_t>(cv), p);  // kMax * 2
            }
            std::size_t given = decl->init && decl->init->kind == Expr::Kind::Brace ? decl->init->kids.size() : 0;
            if (!ty0->bound && given > 0) {
                // T a[] = {x, y}: the length is the number of initializers ([dcl.array] p3).
                decl->type->bound = eInt(given, p);
                decl->type->unsized = false;
            }
            if (ty0->bound && ty0->bound->kind == Expr::Kind::Int && (!decl->init || given > 0) &&
                given <= ty0->bound->ival) {
                arrayCount = ty0->bound->ival;
                ty0 = ty0->inner.get();
                if (decl->init) {
                    // T a[3] = {x, y}: element i is copy-initialized from initializer i and the rest
                    // are value-initialized ([dcl.init.aggr] p4-5): x and y are copied, a[2] is T().
                    std::vector<std::unique_ptr<Expr>> elems = std::move(decl->init->kids);
                    decl->init.reset();
                    std::string etag = ty0->tag;
                    Type* ecls = classOf(etag);
                    out.push_back(declStmt(std::move(decl)));
                    for (std::uint64_t k = 0; k < arrayCount; ++k) {
                        auto at = eAddr(eIndex(name, k, p), p);
                        if (k < elems.size() && elems[k] && elems[k]->kind == Expr::Kind::Brace) {
                            out.push_back(sExpr(ctorCall(etag, std::move(at), std::move(elems[k]->kids), p), p));
                        } else if (k < elems.size()) {
                            std::vector<std::unique_ptr<Expr>> a;
                            a.push_back(std::move(at));
                            a.push_back(std::move(elems[k]));
                            out.push_back(sExpr(eCall(eName("\x01" "cinit:" + etag, p), std::move(a), p), p));
                        } else if (ecls && ecls->needCtor) {
                            out.push_back(sExpr(ctorCall(etag, std::move(at), {}, p), p));
                        }
                    }
                    if (ecls && ecls->needDtor && !dscopes_.empty()) {
                        if (eh_) ehPushObject(out, eName(name, p), etag, arrayCount, p);
                        dscopes_.back().push_back(DVar{name, etag, arrayCount, "", 0});
                    }
                    return;
                }
            } else {
                err("an array of objects with constructors needs a constant size and no initializer");
            }
        }
        Type* cls = ty0 && ty0->kind == Type::Kind::Struct && !ty0->tag.empty() ? classOf(ty0->tag) : nullptr;
        std::string tag = ty0 ? ty0->tag : "";
        bool hasInit = decl->init != nullptr;
        std::unique_ptr<Expr> copyFrom;
        if (cls && hasInit && !haveCtorArgs && decl->init->kind != Expr::Kind::Brace &&
            (hasCopyCtor(cls) || cls->needCtor || cls->needDtor || inProgress(tag))) {
            // "T x = y;" runs the copy constructor when y is an object (decided by the checker).
            copyFrom = std::move(decl->init);
            hasInit = false;
        }
        const bool autoLocal = !cls && ty0 && ty0->kind == Type::Kind::Struct && ty0->tag == "\x03" "auto" && hasInit &&
                               !decl->isStatic && bodyDepth_ > 0 && !name.empty();
        if (decl->isStatic && bodyDepth_ > 0 && !name.empty() && !localNames_.empty() && localNames_.back() == name)
            fnStatics_.push_back(FnStatic{name, decl.get(), "", localNames_.size() - 1});
        out.push_back(declStmt(std::move(decl)));
        if (autoLocal && !dscopes_.empty()) dscopes_.back().push_back(DVar{name, "\x03" "auto", 0, "", 0});
        if (!cls || name.empty()) return;
        if (arrayCount > 0) {
            if (cls->needCtor)
                out.push_back(elementLoop(name, arrayCount, false, p, [&](std::unique_ptr<Expr> at) {
                    return ctorCall(tag, eAddr(std::move(at), p), {}, p);
                }));
            if (cls->needDtor && !dscopes_.empty()) {
                if (eh_) ehPushObject(out, eName(name, p), tag, arrayCount, p);
                dscopes_.back().push_back(DVar{name, tag, arrayCount, "", 0});
            }
            return;
        }
        if (copyFrom) {
            std::vector<std::unique_ptr<Expr>> a;
            a.push_back(eAddr(eName(name, p), p));
            a.push_back(std::move(copyFrom));
            out.push_back(sExpr(eCall(eName("\x01" "cinit:" + tag, p), std::move(a), p), p));
            if ((cls->needDtor || (inProgress(tag) && !eh_)) && !dscopes_.empty()) {
                if (eh_) ehPushObject(out, eAddr(eName(name, p), p), tag, 0, p);
                dscopes_.back().push_back(DVar{name, tag, 0, "", 0});
            }
            return;
        }
        const bool open = inProgress(tag);
        if ((cls->needCtor || open) && (haveCtorArgs || !hasInit)) {
            out.push_back(sExpr(ctorCall(tag, eAddr(eName(name, p), p), std::move(ctorArgs), p), p));
        }
        if ((cls->needDtor || (open && !eh_)) && !dscopes_.empty()) {
            if (eh_) ehPushObject(out, eAddr(eName(name, p), p), tag, 0, p);
            dscopes_.back().push_back(DVar{name, tag, 0, "", 0});
        }
    }

    void parseDeclInto(std::vector<std::unique_ptr<Stmt>>& out, bool forInit, bool condition = false) {
        bool st = eatKw("static");
        bool td = eatKw("typedef");
        auto base = parseBase();
        if (base && (base->kind == Type::Kind::Struct || base->kind == Type::Kind::Union) && isP(";")) {
            advance();
            (void)forInit;
            return;
        }
        if (enumDef_ && isP(";")) {
            enumDef_ = false;
            advance();
            (void)forInit;
            return;
        }
        enumDef_ = false;
        if (!td && atBindings()) {
            // auto [a, b] = e; (also auto &[a, b] = e; and {e}) ([dcl.struct.bind])
            bool isRef = false;
            auto names = readBindings(isRef);
            Parsed d;
            d.name = "__sb" + std::to_string(newN_++);
            Pos bp = here();
            d.line = bp.line;
            d.col = bp.col;
            d.file = bp.file;
            std::unique_ptr<Type> type;
            if (isRef) {
                type = ty(Type::Kind::Reference);
                type->inner = std::move(base);
            } else {
                type = std::move(base);
            }
            std::unique_ptr<Expr> init;
            if (eatP("=")) {
                init = parseInitializer();
            } else if (isP("{") || isP("(")) {
                bool paren = eatP("(");
                if (paren) {
                    init = parseAssign();
                    expectP(")", ")");
                } else {
                    auto list = parseInitializer();
                    if (list && list->kids.size() == 1) init = std::move(list->kids[0]);
                }
            }
            if (!init) err("a structured binding needs an initializer");
            auto decl = makeDecl(d, std::move(type), nullptr);
            decl->init = std::move(init);
            addLocal(out, std::move(decl), {}, false);
            bindNames(names, d.name);
            if (!condition) expectP(";", ";");
            return;
        }
        bool classBase = !td && base && base->kind == Type::Kind::Struct && !base->tag.empty() && classOf(base->tag) &&
                         (classOf(base->tag)->needCtor || classOf(base->tag)->needDtor || inProgress(base->tag));
        for (;;) {
            // "T x(args);" for a class: constructor arguments, not a function declarator.
            if (classBase && !atEnd() && cur().kind == Tok::Ident && nextIs("(") && i_ + 2 < toks_.size() &&
                !isP(toks_[i_ + 2], ")") && (!isTypeKw(toks_[i_ + 2]) || typeThenValue(i_ + 2))) {
                Parsed d;
                d.name = cur().text;
                d.line = cur().line;
                d.col = cur().column;
                d.file = cur().file;
                advance();
                advance();
                std::vector<std::unique_ptr<Expr>> args;
                args.push_back(parseAssign());
                while (eatP(",")) args.push_back(parseAssign());
                expectP(")", ")");
                auto decl = makeDecl(d, cloneType(*base), nullptr);
                decl->isStatic = st;
                addLocal(out, std::move(decl), std::move(args), true);
            } else {
                Parsed d = parseDeclarator(true);
                auto type = apply(cloneType(*base), d.mods);
                if (type && type->kind == Type::Kind::Function && isP("{")) {
                    err("a function definition is not allowed here");
                    return;
                }
                std::unique_ptr<Expr> init;
                if (eatP("=")) {
                    Pos bp = here();
                    // The name is in scope in its own initializer ([basic.scope.pdecl] p1): a
                    // std::function named visit calls itself, not std::visit.
                    if (!td && ftmpl_.count(d.name)) localNames_.push_back(d.name);
                    init = parseInitializer();
                    // T x = {a, b}; for a class with constructors: copy-list-initialization runs a
                    // constructor as T x{a, b} does ([dcl.init.list]/3).
                    if (init && init->kind == Expr::Kind::Brace && type && type->kind == Type::Kind::Struct &&
                        !type->tag.empty() && userCtors_.count(type->tag))
                        init = braceTemp(*type, std::move(init->kids), bp);
                } else if (isP("{") && type && type->kind != Type::Kind::Function) {
                    // T x{a, b}; ([dcl.init.list]): a class from a T{a, b} temporary built in place,
                    // a scalar from its one value, an array from the list.
                    Pos bp = here();
                    auto list = parseInitializer();
                    if (type->kind == Type::Kind::Struct && !type->tag.empty())
                        init = braceTemp(*type, std::move(list->kids), bp);
                    else if (type->kind != Type::Kind::Array && list->kids.size() == 1)
                        init = std::move(list->kids[0]);
                    else if (type->kind != Type::Kind::Array && list->kids.empty())
                        init = zeroAt();
                    else
                        init = std::move(list);
                }
                if (td) {
                    addTypedef(d.name, std::move(type));
                } else {
                    auto decl = makeDecl(d, std::move(type), nullptr);
                    decl->isStatic = st;
                    bool direct = false;
                    std::vector<std::unique_ptr<Expr>> args;
                    // "T x = T(args);" runs the constructor directly.
                    if (init && classBase && decl->type && decl->type->kind == Type::Kind::Struct &&
                        init->kind == Expr::Kind::Call && !init->kids.empty() && init->kids[0] &&
                        init->kids[0]->kind == Expr::Kind::Name &&
                        (init->kids[0]->text == decl->type->tag || init->kids[0]->text == shortName(decl->type->tag))) {
                        for (std::size_t k = 1; k < init->kids.size(); ++k) args.push_back(std::move(init->kids[k]));
                        direct = true;
                        init.reset();
                    }
                    decl->init = std::move(init);
                    addLocal(out, std::move(decl), std::move(args), direct);
                }
            }
            if (condition || !eatP(",")) break;
        }
        if (!condition) expectP(";", ";");
        (void)forInit;
    }

    // `if (T x = e)`: a condition that declares a variable ([stmt.select] p2, [stmt.pre]). It is
    // told from an expression by a type at the start and `name =` or `name {` at the top level
    // before the closing parenthesis.
    bool conditionIsDecl() const {
        if (atEnd() || !isTypeKw(cur())) return false;
        int depth = 0;
        for (std::size_t j = i_; j < toks_.size(); ++j) {
            const Token& t = toks_[j];
            if (t.kind == Tok::End) return false;
            if (isP(t, "(") || isP(t, "[")) ++depth;
            else if (isP(t, ")") || isP(t, "]")) {
                if (depth == 0) return false;
                --depth;
            } else if (depth == 0 && (isP(t, "=") || isP(t, "{")) && j > i_ && toks_[j - 1].kind == Tok::Ident &&
                       j - 1 > i_) {
                return true;
            } else if (depth == 0 && isP(t, ";")) {
                return false;
            }
        }
        return false;
    }

    std::unique_ptr<Expr> parseInitializer() {
        if (eatP("{")) {
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::Brace;
            if (!eatP("}")) {
                e->kids.push_back(parseInitializer());
                while (eatP(",")) {
                    if (isP("}")) break;
                    e->kids.push_back(parseInitializer());
                }
                expectP("}", "}");
            }
            return e;
        }
        return parseAssign();
    }

    // Names of the local variables declared so far in the enclosing blocks of the function being
    // parsed ([basic.scope.block]): such a name hides a namespace function template of that name,
    // so `count < 5` with a local count is a comparison, not count<...>.
    std::vector<std::string> localNames_;
    bool isLocalName(const std::string& n) const {
        for (const std::string& l : localNames_)
            if (l == n) return true;
        return false;
    }

    std::unique_ptr<Stmt> parseCompound() {
        Guard nest(*this);
        if (!nest) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Kind::Block;
            return s;
        }
        auto saved = cur();
        expectP("{", "{");
        tscopes_.emplace_back();
        std::size_t localMark = localNames_.size();
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::Block;
        s->line = saved.line;
        s->col = saved.column;
        dscopes_.emplace_back();
        while (!atEnd() && !isP("}")) {
            std::size_t before = i_;
            std::size_t at0 = s->stmts.size();
            if (isUnsupported()) {
                err("'" + cur().text + "' is not in this subset");
                syncSemi();
            } else if (isKw("using")) {
                parseUsing();
            } else if (!atEnd() && isTypeKw(cur()) && !typeThenExprStmt(i_)) {
                parseDeclInto(s->stmts, false);
            } else {
                s->stmts.push_back(parseStmt());
            }
            flushPre(s->stmts, at0);
            if (stop_) break;
            if (i_ == before) advance();
        }
        Pos endPos = here();
        expectP("}", "}");
        {
            auto ds = pendingDtors(dscopes_.size() - 1, endPos);
            for (auto& d : ds) s->stmts.push_back(std::move(d));
        }
        if (!dscopes_.empty()) dscopes_.pop_back();
        if (!tscopes_.empty()) tscopes_.pop_back();
        localNames_.resize(localMark);
        return s;
    }

    // Statements that must run before the statement being parsed (temporaries for 'new').
    void flushPre(std::vector<std::unique_ptr<Stmt>>& out, std::size_t at) {
        if (preStmts_.empty()) return;
        if (at > out.size()) at = out.size();
        for (auto& p : preStmts_) {
            out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), std::move(p));
            ++at;
        }
        preStmts_.clear();
    }

    std::unique_ptr<Expr> eBin(const char* op, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r, const Pos& p) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Bin;
        e->text = op;
        e->line = p.line;
        e->col = p.col;
        e->file = p.file;
        e->kids.push_back(std::move(l));
        e->kids.push_back(std::move(r));
        return e;
    }
    // { int i; for (i = 0; i < n; i = i + 1) call(name[i]); } (or counting down). 'call' builds the
    // call from the element expression. Used for constructors and destructors of an array.
    template <class F>
    std::unique_ptr<Stmt> elementLoop(const std::string& name, std::uint64_t n, bool reverse, const Pos& p, F call) {
        return indexLoop([&] { return eName(name, p); }, n, reverse, p, call);
    }
    // for (i = 0; i < n; ++i) call(base()[i]), from n - 1 down when reverse.
    template <class B, class F>
    std::unique_ptr<Stmt> indexLoop(B base, std::uint64_t n, bool reverse, const Pos& p, F call) {
        auto blk = sBlock(p);
        std::string iv = "__i" + std::to_string(newN_++);
        auto decl = std::make_unique<Decl>();
        decl->name = iv;
        decl->line = p.line;
        decl->col = p.col;
        decl->file = p.file;
        decl->type = ty(Type::Kind::Int);
        blk->stmts.push_back(declStmt(std::move(decl)));
        auto loop = std::make_unique<Stmt>();
        loop->kind = Stmt::Kind::For;
        loop->line = p.line;
        loop->col = p.col;
        loop->file = p.file;
        loop->init = sExpr(eAssign(eName(iv, p), eInt(reverse ? n - 1 : 0, p), p), p);
        loop->expr = reverse ? eBin(">=", eName(iv, p), eInt(0, p), p)
                             : eBin("<", eName(iv, p), eInt(n, p), p);
        loop->expr2 = eAssign(eName(iv, p), eBin(reverse ? "-" : "+", eName(iv, p), eInt(1, p), p), p);
        auto idx = std::make_unique<Expr>();
        idx->kind = Expr::Kind::Index;
        idx->line = p.line;
        idx->col = p.col;
        idx->file = p.file;
        idx->kids.push_back(base());
        idx->kids.push_back(eName(iv, p));
        loop->body = sExpr(call(std::move(idx)), p);
        blk->stmts.push_back(std::move(loop));
        return blk;
    }

    // Destructor calls for the objects declared in scopes from 'base' up, innermost first.
    std::vector<std::unique_ptr<Stmt>> pendingDtors(std::size_t base, const Pos& p, const std::string& keep = "") {
        std::vector<std::unique_ptr<Stmt>> out;
        bool kept = keep.empty();
        for (std::size_t sc = dscopes_.size(); sc-- > base;) {
            auto& vars = dscopes_[sc];
            for (std::size_t i = vars.size(); i-- > 0;) {
                if (!kept && vars[i].kind == 0 && vars[i].count == 0 && vars[i].name == keep) {
                    kept = true;  // the returned object itself (see parseReturn)
                    continue;
                }
                if (vars[i].kind == 1) {
                    out.push_back(sExpr(ehCall("__okay_eh_pop", {}, p), p));
                    continue;
                }
                if (vars[i].kind == 2) {
                    out.push_back(sExpr(ehCall("__okay_eh_end", {}, p), p));
                    continue;
                }
                if (vars[i].count > 0) {
                    out.push_back(elementLoop(vars[i].name, vars[i].count, true, p, [&](std::unique_ptr<Expr> at) {
                        return dtorCall(vars[i].tag, eAddr(std::move(at), p), p);
                    }));
                    if (eh_) out.push_back(sExpr(ehCall("__okay_eh_pop", {}, p), p));
                    continue;
                }
                const bool viaAlias = !vars[i].alias.empty() && isLocalName(vars[i].alias);  // (shadowLocals)
                auto call = dtorCall(vars[i].tag, viaAlias ? eName(vars[i].alias, p) : eAddr(eName(vars[i].name, p), p), p);
                const bool had = call != nullptr;
                if (had) out.push_back(sExpr(std::move(call), p));
                // (An "auto x = e;" local has no unwinding record: no pop for it. Popping one
                // anyway dropped the record of the object declared before it, so a later return
                // unwound the stack of records past the function's own.)
                if (had && eh_ && vars[i].guard.empty() && vars[i].tag != "\x03" "auto")
                    out.push_back(sExpr(ehCall("__okay_eh_pop", {}, p), p));
            }
        }
        return out;
    }

    std::unique_ptr<Stmt> wrapExit(std::unique_ptr<Stmt> st, bool have, std::size_t base) {
        if (!have) return st;
        Pos p;
        p.line = st->line;
        p.col = st->col;
        p.file = st->file;
        auto ds = pendingDtors(base, p);
        if (ds.empty()) return st;
        auto blk = sBlock(p);
        for (auto& d : ds) blk->stmts.push_back(std::move(d));
        blk->stmts.push_back(std::move(st));
        return blk;
    }

    std::unique_ptr<Stmt> parseStmt() {
        Guard nest(*this);
        if (!nest) return std::make_unique<Stmt>();
        if (!atEnd() && cur().kind == Tok::Ident && i_ + 1 < toks_.size() && toks_[i_ + 1].kind == Tok::Punct &&
            toks_[i_ + 1].text == ":") {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Kind::Label;
            s->line = cur().line;
            s->col = cur().column;
            s->file = cur().file;
            s->expr = leafName(cur());
            advance();
            advance();
            return s;
        }
        if (isKw("goto")) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Kind::Goto;
            s->line = cur().line;
            s->col = cur().column;
            advance();
            if (!atEnd() && cur().kind == Tok::Ident) {
                s->expr = leafName(cur());
                advance();
            } else {
                err("expected a label");
            }
            expectP(";", ";");
            return s;
        }
        if (isP("{")) return parseCompound();
        if (parseStaticAssert()) return std::make_unique<Stmt>();
        if (isKw("if")) return parseIf();
        if (isKw("while")) return parseWhile();
        if (isKw("do")) return parseDo();
        if (isKw("try")) return parseTry();
        if (isKw("for")) return parseFor();
        if (isKw("return")) return parseReturn();
        if (isKw("switch")) return parseSwitch();
        if (eatKw("break")) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Kind::Break;
            s->line = cur().line;
            expectP(";", ";");
            return wrapExit(std::move(s), !brkBases_.empty(), brkBases_.empty() ? 0 : brkBases_.back());
        }
        if (eatKw("continue")) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Kind::Continue;
            s->line = cur().line;
            expectP(";", ";");
            return wrapExit(std::move(s), !loopBases_.empty(), loopBases_.empty() ? 0 : loopBases_.back());
        }
        if (isUnsupported()) {
            err("'" + cur().text + "' is not in this subset");
            advance();
            return std::make_unique<Stmt>();
        }
        if (eatP(";")) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Kind::Empty;
            return s;
        }
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::Expr;
        s->line = atEnd() ? 1 : cur().line;
        s->expr = parseExpr();
        expectP(";", ";");
        return s;
    }

    // At the token after "if (": a ';' before the matching ')' means an init-statement ([stmt.if]).
    bool hasInitStatement() const {
        int depth = 0;
        for (std::size_t k = i_; k < toks_.size() && toks_[k].kind != Tok::End; ++k) {
            if (isP(toks_[k], "(") || isP(toks_[k], "[") || isP(toks_[k], "{")) ++depth;
            else if (isP(toks_[k], ")") || isP(toks_[k], "]") || isP(toks_[k], "}")) {
                if (depth == 0) return false;
                --depth;
            } else if (isP(toks_[k], ";") && depth == 0) {
                return true;
            }
        }
        return false;
    }

    // { declarations; if (cond) A else B } for if (init; cond) and if (T x = e): beginIfBlock opens
    // the block's scopes; endIfBlock, once cond is read, adds the branches, then the destructors of
    // what the block declared, and closes the scopes.
    std::unique_ptr<Stmt> beginIfBlock(const Stmt& s) {
        tscopes_.emplace_back();
        dscopes_.emplace_back();
        auto block = std::make_unique<Stmt>();
        block->kind = Stmt::Kind::Block;
        block->line = s.line;
        block->col = s.col;
        return block;
    }
    std::unique_ptr<Stmt> endIfBlock(std::unique_ptr<Stmt> block, std::unique_ptr<Stmt> s, std::size_t localMark) {
        s->thenS = parseStmt();
        if (eatKw("else")) s->elseS = parseStmt();
        block->stmts.push_back(std::move(s));
        auto ds = pendingDtors(dscopes_.size() - 1, here());
        for (auto& d : ds) block->stmts.push_back(std::move(d));
        dscopes_.pop_back();
        tscopes_.pop_back();
        localNames_.resize(localMark);
        return block;
    }

    std::unique_ptr<Stmt> parseIf() {
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::If;
        s->line = cur().line;
        s->col = cur().column;
        advance();
        const bool constIf = eatKw("constexpr");
        expectP("(", "(");
        if (hasInitStatement()) {
            // if (init; cond) A else B  is  { init; if (cond) A else B } ([stmt.if] p3): what init
            // declares is in scope in both branches and is destroyed after the whole statement.
            std::size_t localMark = localNames_.size();
            auto block = beginIfBlock(*s);
            std::size_t at0 = 0;
            if (isTypeKw(cur())) {
                parseDeclInto(block->stmts, false);
            } else {
                auto st = std::make_unique<Stmt>();
                st->kind = Stmt::Kind::Expr;
                st->line = cur().line;
                st->expr = parseExpr();
                expectP(";", ";");
                block->stmts.push_back(std::move(st));
            }
            flushPre(block->stmts, at0);
            std::size_t at1 = block->stmts.size();
            s->expr = parseExpr();
            flushPre(block->stmts, at1);
            expectP(")", ")");
            return endIfBlock(std::move(block), std::move(s), localMark);
        }
        if (conditionIsDecl()) {
            // if (T x = e) A else B  is  { T x = e; if (x) A else B }: x is in scope in both
            // branches and is destroyed after the whole statement.
            Pos at = here();
            std::size_t localMark = localNames_.size();
            auto block = beginIfBlock(*s);
            std::size_t at0 = 0;
            parseDeclInto(block->stmts, false, true);
            flushPre(block->stmts, at0);
            std::string var = localNames_.size() > localMark ? localNames_.back() : std::string();
            expectP(")", ")");
            s->expr = eName(var, at);
            return endIfBlock(std::move(block), std::move(s), localMark);
        }
        s->expr = parseExpr();
        expectP(")", ")");
        s->thenS = parseStmt();
        if (eatKw("else")) s->elseS = parseStmt();
        long cv = 0;
        if (constIf && s->expr && evalConst(*s->expr, cv)) {
            // if constexpr ([stmt.if] p2): the branch not taken is discarded (read, never checked).
            std::unique_ptr<Stmt> taken = cv ? std::move(s->thenS) : std::move(s->elseS);
            return taken ? std::move(taken) : std::make_unique<Stmt>();
        }
        return s;
    }

    std::unique_ptr<Stmt> parseWhile() {
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::While;
        s->line = cur().line;
        advance();
        expectP("(", "(");
        s->expr = parseExpr();
        expectP(")", ")");
        loopBases_.push_back(dscopes_.size());
        brkBases_.push_back(dscopes_.size());
        s->body = parseStmt();
        loopBases_.pop_back();
        brkBases_.pop_back();
        return s;
    }

    // ---- exceptions (written from ISO/IEC 14882 [except]; not the Itanium ABI) ----
    // A stack of records in the runtime (lib/eh.c): an object with a destructor pushes a record when
    // it is built and pops it when it is destroyed; a try block pushes a frame holding the saved
    // registers (__okay_setjmp); throw walks the records, destroying objects, until it reaches a try
    // frame, which jumps to the handlers. The handlers test the thrown type and continue unwinding
    // when none fits.
    bool eh_ = false;
    std::unique_ptr<Expr> ehCall(const std::string& fn, std::vector<std::unique_ptr<Expr>> args, const Pos& p) {
        return eCall(eName(fn, p), std::move(args), p);
    }
    std::unique_ptr<Expr> eStr(const std::string& text, const Pos& p) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::String;
        e->text = text;
        e->line = p.line;
        e->col = p.col;
        e->file = p.file;
        return e;
    }
    std::unique_ptr<Expr> eCastTo(std::unique_ptr<Type> t, std::unique_ptr<Expr> x, const Pos& p) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Cast;
        e->line = p.line;
        e->col = p.col;
        e->file = p.file;
        e->ty = std::move(t);
        e->kids.push_back(std::move(x));
        return e;
    }
    std::unique_ptr<Expr> eIndex(const std::string& arr, std::uint64_t i, const Pos& p) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Index;
        e->line = p.line;
        e->col = p.col;
        e->file = p.file;
        e->kids.push_back(eName(arr, p));
        e->kids.push_back(eInt(i, p));
        return e;
    }
    std::unique_ptr<Stmt> ehRecordDecl(const std::string& name, const Pos& p) {
        auto d = std::make_unique<Decl>();
        d->name = name;
        d->line = p.line;
        d->col = p.col;
        d->file = p.file;
        auto arr = ty(Type::Kind::Array);
        arr->inner = ty(Type::Kind::Long);
        arr->bound = eInt(14, p);
        d->type = std::move(arr);
        return declStmt(std::move(d));
    }
    // After an object of class 'tag' is built: record it so that unwinding destroys it.
    void ehPushObject(std::vector<std::unique_ptr<Stmt>>& out, std::unique_ptr<Expr> objAddr, const std::string& tag,
                      std::uint64_t count, const Pos& p) {
        Type* c = classOf(tag);
        const Param* d = c ? special(c, "<dtor>") : nullptr;
        if (!d) return;
        std::string rec = "__eh" + std::to_string(newN_++);
        out.push_back(ehRecordDecl(rec, p));
        out.push_back(sExpr(eAssign(eIndex(rec, 1, p), eInt(count ? 1 : 0, p), p), p));
        out.push_back(sExpr(eAssign(eIndex(rec, 2, p), eCastTo(ty(Type::Kind::Long), std::move(objAddr), p), p), p));
        out.push_back(sExpr(eAssign(eIndex(rec, 3, p), eCastTo(ty(Type::Kind::Long), eName(d->mangled, p), p), p), p));
        if (count) {
            out.push_back(sExpr(eAssign(eIndex(rec, 4, p), eInt(count, p), p), p));
            auto sz = std::make_unique<Expr>();
            sz->kind = Expr::Kind::Unary;
            sz->text = "sizeof";
            sz->line = p.line;
            sz->col = p.col;
            sz->file = p.file;
            sz->ty = ty(Type::Kind::Struct);
            sz->ty->tag = tag;
            out.push_back(sExpr(eAssign(eIndex(rec, 5, p), std::move(sz), p), p));
        }
        std::vector<std::unique_ptr<Expr>> a;
        a.push_back(eName(rec, p));
        out.push_back(sExpr(ehCall("__okay_eh_push", std::move(a), p), p));
    }

    std::unique_ptr<Type> longPtr() {
        auto pt = ty(Type::Kind::Pointer);
        pt->inner = ty(Type::Kind::Long);
        return pt;
    }

    // try { body } catch (T e) { ... } catch (...) { ... }
    std::unique_ptr<Stmt> parseTry() {
        Pos p = here();
        advance();
        const int n = newN_++;
        const std::string tr = "__tr" + std::to_string(n);
        auto blk = sBlock(p);
        blk->stmts.push_back(ehRecordDecl(tr, p));
        blk->stmts.push_back(sExpr(eAssign(eIndex(tr, 1, p), eInt(2, p), p), p));
        {
            std::vector<std::unique_ptr<Expr>> a;
            a.push_back(eName(tr, p));
            blk->stmts.push_back(sExpr(ehCall("__okay_eh_push", std::move(a), p), p));
        }
        // The try body, with the frame popped by every way out of it.
        dscopes_.emplace_back();
        DVar frame;
        frame.kind = 1;
        dscopes_.back().push_back(frame);
        auto body = parseCompound();
        dscopes_.pop_back();
        body->stmts.push_back(sExpr(ehCall("__okay_eh_pop", {}, p), p));
        struct Handler {
            bool all = false;
            std::string name;
            std::unique_ptr<Type> type;
            std::string var;
            std::unique_ptr<Stmt> block;
        };
        std::vector<Handler> handlers;
        if (!isKw("catch")) err("a try block needs a handler");
        while (isKw("catch")) {
            Handler h;
            advance();
            expectP("(", "(");
            Pos hp = here();
            dscopes_.emplace_back();
            DVar hv;
            hv.kind = 2;
            dscopes_.back().push_back(hv);
            auto hb = sBlock(hp);
            const std::string cr = "__ch" + std::to_string(newN_++);
            hb->stmts.push_back(ehRecordDecl(cr, hp));
            {
                std::vector<std::unique_ptr<Expr>> a;
                a.push_back(eName(cr, hp));
                hb->stmts.push_back(sExpr(ehCall("__okay_eh_begin", std::move(a), hp), hp));
            }
            if (isP("...")) {
                advance();
                h.all = true;
            } else {
                auto base = parseBase();
                Parsed d = parseDeclarator(false);
                auto type = apply(std::move(base), d.mods);
                h.name = mangleTypeNoCv(type->kind == Type::Kind::Reference && type->inner ? *type->inner : *type);
                if (!d.name.empty()) {
                    // The caught object: a copy for a type, the object itself for a reference.
                    auto decl = std::make_unique<Decl>();
                    decl->name = d.name;
                    decl->line = d.line;
                    decl->col = d.col;
                    decl->file = d.file;
                    const Type* want = type->kind == Type::Kind::Reference && type->inner ? type->inner.get() : type.get();
                    auto ptr = ty(Type::Kind::Pointer);
                    ptr->inner = cloneType(*want);
                    auto objp = eCastTo(std::move(ptr), ehCall("__okay_eh_obj", {}, hp), hp);
                    auto deref = std::make_unique<Expr>();
                    deref->kind = Expr::Kind::Unary;
                    deref->text = "*";
                    deref->line = hp.line;
                    deref->col = hp.col;
                    deref->file = hp.file;
                    deref->kids.push_back(std::move(objp));
                    decl->type = std::move(type);
                    decl->init = std::move(deref);
                    addLocal(hb->stmts, std::move(decl), {}, false);
                }
            }
            expectP(")", ")");
            auto hbody = parseCompound();
            hb->stmts.push_back(std::move(hbody));
            auto ds = pendingDtors(dscopes_.size() - 1, hp);
            for (auto& dd : ds) hb->stmts.push_back(std::move(dd));
            dscopes_.pop_back();
            h.block = std::move(hb);
            handlers.push_back(std::move(h));
        }
        // The chain: the first handler whose type fits, else keep unwinding.
        std::unique_ptr<Stmt> chain;
        {
            auto resume = sExpr(ehCall("__okay_eh_resume", {}, p), p);
            chain = std::move(resume);
        }
        for (std::size_t i = handlers.size(); i-- > 0;) {
            auto iff = std::make_unique<Stmt>();
            iff->kind = Stmt::Kind::If;
            iff->line = p.line;
            iff->col = p.col;
            iff->file = p.file;
            if (handlers[i].all) {
                iff->expr = eInt(1, p);
            } else {
                std::vector<std::unique_ptr<Expr>> a;
                a.push_back(eStr(handlers[i].name, p));
                iff->expr = ehCall("__okay_eh_match", std::move(a), p);
            }
            iff->thenS = std::move(handlers[i].block);
            iff->elseS = std::move(chain);
            chain = std::move(iff);
        }
        // if (!setjmp(frame)) body else handlers
        auto top = std::make_unique<Stmt>();
        top->kind = Stmt::Kind::If;
        top->line = p.line;
        top->col = p.col;
        top->file = p.file;
        {
            std::vector<std::unique_ptr<Expr>> a;
            a.push_back(eAddr(eIndex(tr, 6, p), p));
            auto call = ehCall("__okay_setjmp", std::move(a), p);
            auto notc = std::make_unique<Expr>();
            notc->kind = Expr::Kind::Unary;
            notc->text = "!";
            notc->line = p.line;
            notc->col = p.col;
            notc->file = p.file;
            notc->kids.push_back(std::move(call));
            top->expr = std::move(notc);
        }
        top->thenS = std::move(body);
        top->elseS = std::move(chain);
        blk->stmts.push_back(std::move(top));
        return blk;
    }

    // do body while (cond); as { first = 1; while (first || cond) { first = 0; body } }. A
    // continue in the body goes to the test, which then evaluates cond (first is 0 by then).
    std::unique_ptr<Stmt> parseDo() {
        Pos p = here();
        advance();
        std::string first = "__do" + std::to_string(newN_++);
        loopBases_.push_back(dscopes_.size());
        brkBases_.push_back(dscopes_.size());
        auto body = parseStmt();
        loopBases_.pop_back();
        brkBases_.pop_back();
        if (!isKw("while")) {
            err("expected 'while' after the body of 'do'");
            return body;
        }
        advance();
        expectP("(", "(");
        auto cond = parseExpr();
        expectP(")", ")");
        expectP(";", ";");
        auto blk = sBlock(p);
        auto d = std::make_unique<Decl>();
        d->name = first;
        d->line = p.line;
        d->col = p.col;
        d->file = p.file;
        d->type = ty(Type::Kind::Int);
        d->init = eInt(1, p);
        blk->stmts.push_back(declStmt(std::move(d)));
        auto w = std::make_unique<Stmt>();
        w->kind = Stmt::Kind::While;
        w->line = p.line;
        w->col = p.col;
        w->file = p.file;
        w->expr = eBin("||", eName(first, p), std::move(cond), p);
        auto inner = sBlock(p);
        inner->stmts.push_back(sExpr(eAssign(eName(first, p), eInt(0, p), p), p));
        inner->stmts.push_back(std::move(body));
        w->body = std::move(inner);
        blk->stmts.push_back(std::move(w));
        return blk;
    }

    // for (decl : range) body ([stmt.ranged]) becomes
    //   { auto &__rg = range; auto __rb = begin(__rg); auto __re = end(__rg);
    //     for (; __rb != __re; ++__rb) { decl = *__rb; body } }
    // where begin and end are the array bounds or the class's begin() and end() (the checker
    // chooses). A range that is not an lvalue is held by value.
    bool isRangeFor() const {
        int depth = 0;
        int conds = 0;  // '?' seen at depth 0 whose ':' has not come yet
        for (std::size_t j = i_; j < toks_.size() && toks_[j].kind != Tok::End; ++j) {
            const Token& t = toks_[j];
            if (t.kind != Tok::Punct) continue;
            if (t.text == "(" || t.text == "[" || t.text == "{") ++depth;
            else if (t.text == ")" || t.text == "]" || t.text == "}") {
                if (depth == 0) return false;
                --depth;
            } else if (depth == 0 && t.text == ";") return false;
            else if (depth == 0 && t.text == "?") ++conds;
            else if (depth == 0 && t.text == ":") {
                if (conds > 0) --conds;  // the ':' of a ?: (int i = c ? 1 : 0)
                else return true;
            }
        }
        return false;
    }
    std::unique_ptr<Stmt> parseRangeFor() {
        Pos pos = here();
        int n = newN_++;
        std::string rg = "__rg" + std::to_string(n), rb = "__rb" + std::to_string(n), re = "__re" + std::to_string(n);
        auto base = parseBase();
        Parsed d;
        std::unique_ptr<Type> elemType;
        std::vector<std::string> sbNames;
        if (atBindings()) {
            // for (auto &[k, v] : m) ([dcl.struct.bind], [stmt.ranged])
            bool isRef = false;
            sbNames = readBindings(isRef);
            d.name = "__sb" + std::to_string(newN_++);
            if (isRef) {
                elemType = ty(Type::Kind::Reference);
                elemType->inner = std::move(base);
            } else {
                elemType = std::move(base);
            }
        } else {
            d = parseDeclarator(true);
            elemType = apply(std::move(base), d.mods);
        }
        expectP(":", ":");
        std::unique_ptr<Expr> range;
        std::unique_ptr<Type> listArray;
        if (isP("{")) {
            // for (T x : {a, b, c}): the list is a std::initializer_list<T> ([stmt.ranged] p1,
            // [dcl.init.list] p5), an array of the elements that lives through the loop. Here it is
            // a hidden array of the element type the loop names (without its reference or const).
            range = parseInitializer();
            const Type* et = elemType.get();
            while (et && et->kind == Type::Kind::Reference && et->inner) et = et->inner.get();
            if (!et || (et->kind == Type::Kind::Struct && et->tag == "\x03" "auto")) {
                err("a range-for over a braced list needs a named element type here");
            } else {
                listArray = ty(Type::Kind::Array);
                listArray->inner = cloneType(*et);
                listArray->inner->cnst = false;
                auto bound = std::make_unique<Expr>();
                bound->kind = Expr::Kind::Int;
                bound->ival = range->kids.empty() ? 1 : range->kids.size();
                listArray->bound = std::move(bound);
            }
        } else {
            range = parseExpr();
        }
        expectP(")", ")");
        auto mkDecl = [&](const std::string& name, std::unique_ptr<Type> type, std::unique_ptr<Expr> init) {
            auto dd = std::make_unique<Decl>();
            dd->name = name;
            dd->type = std::move(type);
            dd->init = std::move(init);
            dd->line = pos.line;
            dd->col = pos.col;
            dd->file = pos.file;
            return dd;
        };
        auto mkE = [&](Expr::Kind k, const char* text) {
            auto x = std::make_unique<Expr>();
            x->kind = k;
            x->text = text;
            x->line = pos.line;
            x->col = pos.col;
            x->file = pos.file;
            return x;
        };
        auto outer = sBlock(pos);
        if (listArray) {
            std::string rl = "__rl" + std::to_string(n);
            outer->stmts.push_back(declStmt(mkDecl(rl, std::move(listArray), std::move(range))));
            range = eName(rl, pos);
        }
        auto refAuto = ty(Type::Kind::Reference);
        refAuto->inner = autoType();
        outer->stmts.push_back(declStmt(mkDecl(rg, std::move(refAuto), std::move(range))));
        for (int k = 0; k < 2; ++k) {
            std::vector<std::unique_ptr<Expr>> a;
            a.push_back(eName(rg, pos));
            auto call = eCall(eName(k == 0 ? "\x01" "begin" : "\x01" "end", pos), std::move(a), pos);
            outer->stmts.push_back(declStmt(mkDecl(k == 0 ? rb : re, autoType(), std::move(call))));
        }
        auto loop = std::make_unique<Stmt>();
        loop->kind = Stmt::Kind::For;
        loop->line = pos.line;
        loop->col = pos.col;
        loop->file = pos.file;
        auto cmp = mkE(Expr::Kind::Bin, "!=");
        cmp->kids.push_back(eName(rb, pos));
        cmp->kids.push_back(eName(re, pos));
        loop->expr = std::move(cmp);
        auto inc = mkE(Expr::Kind::Pre, "++");
        inc->kids.push_back(eName(rb, pos));
        loop->expr2 = std::move(inc);
        loopBases_.push_back(dscopes_.size());
        brkBases_.push_back(dscopes_.size());
        dscopes_.emplace_back();
        tscopes_.emplace_back();
        auto body = sBlock(pos);
        auto deref = mkE(Expr::Kind::Unary, "*");
        deref->kids.push_back(eName(rb, pos));
        addLocal(body->stmts, mkDecl(d.name, std::move(elemType), std::move(deref)), {}, false);
        std::size_t sbMark = localNames_.size();
        if (!sbNames.empty()) bindNames(sbNames, d.name);
        body->stmts.push_back(parseStmt());
        if (!sbNames.empty()) localNames_.resize(sbMark);
        {
            auto ds = pendingDtors(dscopes_.size() - 1, pos);
            for (auto& st : ds) body->stmts.push_back(std::move(st));
        }
        dscopes_.pop_back();
        tscopes_.pop_back();
        loopBases_.pop_back();
        brkBases_.pop_back();
        loop->body = std::move(body);
        outer->stmts.push_back(std::move(loop));
        return outer;
    }

    std::unique_ptr<Stmt> parseFor() {
        if (i_ + 1 < toks_.size() && isP(toks_[i_ + 1], "(")) {
            std::size_t save = i_;
            i_ += 2;
            if (isRangeFor()) return parseRangeFor();
            i_ = save;
        }
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::For;
        s->line = cur().line;
        // An init-statement that is more than one statement (two declarators, or an object whose
        // construction is a statement of its own) goes in a block around the loop: [stmt.for] p1
        // defines the for statement as { init-statement while (condition) { ... } }.
        std::vector<std::unique_ptr<Stmt>> outer;
        Pos forPos = here();
        advance();
        expectP("(", "(");
        // The init-statement's objects are destroyed when the loop ends, not at the end of the
        // enclosing block (where their names are out of scope): a destructor scope of their own.
        dscopes_.emplace_back();
        if (eatP(";")) {
            s->init = nullptr;
        } else if (!atEnd() && isTypeKw(cur())) {
            std::vector<std::unique_ptr<Stmt>> decls;
            parseDeclInto(decls, true);
            if (decls.size() == 1) s->init = std::move(decls[0]);
            else outer = std::move(decls);  // "for (int i = 0, n = 4; ...)", "for (It it = f(); ...)"
        } else {
            auto st = std::make_unique<Stmt>();
            st->kind = Stmt::Kind::Expr;
            st->expr = parseExpr();
            expectP(";", ";");
            s->init = std::move(st);
        }
        if (!eatP(";")) {
            s->expr = parseExpr();
            expectP(";", ";");
        }
        if (!isP(")")) s->expr2 = parseExpr();
        expectP(")", ")");
        loopBases_.push_back(dscopes_.size());
        brkBases_.push_back(dscopes_.size());
        s->body = parseStmt();
        loopBases_.pop_back();
        brkBases_.pop_back();
        auto ds = pendingDtors(dscopes_.size() - 1, forPos);
        dscopes_.pop_back();
        if (outer.empty() && ds.empty()) return s;
        if (outer.empty() && s->init) {
            // for (std::string t; ...): the declaration moves into the block so that the
            // destructor calls after the loop still see it.
            outer.push_back(std::move(s->init));
        }
        auto block = std::make_unique<Stmt>();
        block->kind = Stmt::Kind::Block;
        block->line = s->line;
        block->stmts = std::move(outer);
        block->stmts.push_back(std::move(s));
        for (auto& st : ds) block->stmts.push_back(std::move(st));
        return block;
    }

    std::unique_ptr<Stmt> parseSwitch() {
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::Switch;
        s->line = cur().line;
        s->col = cur().column;
        s->file = cur().file;
        advance();
        expectP("(", "(");
        s->expr = parseExpr();
        expectP(")", ")");
        expectP("{", "{");
        brkBases_.push_back(dscopes_.size());
        while (!atEnd() && !isP("}")) {
            auto c = std::make_unique<Stmt>();
            c->kind = Stmt::Kind::Case;
            c->line = cur().line;
            c->col = cur().column;
            c->file = cur().file;
            if (eatKw("case")) {
                c->expr = parseExpr();
                expectP(":", ":");
            } else if (eatKw("default")) {
                expectP(":", ":");
            } else {
                err("expected 'case' or 'default'");
                break;
            }
            auto body = std::make_unique<Stmt>();
            body->kind = Stmt::Kind::Block;
            body->line = c->line;
            body->col = c->col;
            body->file = c->file;
            while (!atEnd() && !isKw("case") && !isKw("default") && !isP("}"))
                body->stmts.push_back(parseStmt());
            c->body = std::move(body);
            s->stmts.push_back(std::move(c));
        }
        brkBases_.pop_back();
        expectP("}", "}");
        return s;
    }

    std::unique_ptr<Stmt> parseReturn() {
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::Return;
        s->line = cur().line;
        s->col = cur().column;
        advance();
        if (isP("{") && curRet_ && curRet_->kind == Type::Kind::Struct && !curRet_->tag.empty()) {
            // return {a, b}; initializes the returned object from the list ([stmt.return] p2).
            Pos bp = here();
            auto list = parseInitializer();
            s->expr = braceTemp(*curRet_, std::move(list->kids), bp);
        } else if (!isP(";")) {
            s->expr = parseExpr();
        }
        expectP(";", ";");
        Pos p;
        p.line = s->line;
        p.col = s->col;
        p.file = s->file;
        // return x; for a local object x of the returned class: x itself becomes the returned
        // object (the copy elision of [class.copy.elision]/1.1, which g++ always does): its bytes
        // move to the result and its destructor does not run on this path.
        std::string nrvo;
        if (!eh_ && s->expr && s->expr->kind == Expr::Kind::Name && curRet_ && curRet_->kind == Type::Kind::Struct &&
            !curRet_->tag.empty()) {
            bool found = false;
            for (std::size_t sc = dscopes_.size(); sc-- > dbase_ && !found;) {
                const auto& vars = dscopes_[sc];
                for (std::size_t i = vars.size(); i-- > 0 && !found;)
                    if (vars[i].kind == 0 && vars[i].name == s->expr->text) {
                        found = true;
                        if (vars[i].count == 0 && vars[i].guard.empty() &&
                            (vars[i].tag == curRet_->tag || vars[i].tag == "\x03" "auto"))
                            nrvo = vars[i].name;
                    }
            }
        }
        auto ds = pendingDtors(dbase_, p, nrvo);
        if (ds.empty() || !curRet_) return s;
        auto blk = sBlock(p);
        if (!s->expr) {
            for (auto& d : ds) blk->stmts.push_back(std::move(d));
            blk->stmts.push_back(std::move(s));
        } else if (curRet_->kind == Type::Kind::Void) {
            blk->stmts.push_back(sExpr(std::move(s->expr), p));
            for (auto& d : ds) blk->stmts.push_back(std::move(d));
            blk->stmts.push_back(std::move(s));
        } else {
            auto decl = std::make_unique<Decl>();
            decl->name = "__rv" + std::to_string(newN_++);
            decl->line = p.line;
            decl->col = p.col;
            decl->file = p.file;
            decl->type = cloneType(*curRet_);
            std::string nm = decl->name;
            Type* cls = curRet_->kind == Type::Kind::Struct && !curRet_->tag.empty() ? classOf(curRet_->tag) : nullptr;
            if (nrvo.empty() && cls && (hasCopyCtor(cls) || cls->needCtor || cls->needDtor || inProgress(curRet_->tag))) {
                // The returned object is copied with its copy constructor before the locals are
                // destroyed ([stmt.return], [class.copy.ctor]); a bytewise copy would share what
                // a local's destructor is about to free.
                blk->stmts.push_back(declStmt(std::move(decl)));
                std::vector<std::unique_ptr<Expr>> a;
                a.push_back(eAddr(eName(nm, p), p));
                a.push_back(std::move(s->expr));
                blk->stmts.push_back(sExpr(eCall(eName("\x01" "cinit:" + curRet_->tag, p), std::move(a), p), p));
            } else {
                decl->init = std::move(s->expr);
                blk->stmts.push_back(declStmt(std::move(decl)));
            }
            for (auto& d : ds) blk->stmts.push_back(std::move(d));
            s->expr = eName(nm, p);
            blk->stmts.push_back(std::move(s));
        }
        return blk;
    }

    std::unique_ptr<Expr> parseExpr() {
        Guard nest(*this);
        if (!nest) return zeroAt();
        auto e = parseAssign();
        if (!isP(",")) return e;
        auto c = std::make_unique<Expr>();
        c->kind = Expr::Kind::Comma;
        c->line = e->line;
        c->col = e->col;
        c->kids.push_back(std::move(e));
        while (eatP(",")) c->kids.push_back(parseAssign());
        return c;
    }

    bool isAssignOp() const {
        if (atEnd() || cur().kind != Tok::Punct) return false;
        const std::string& t = cur().text;
        return t == "=" || t == "+=" || t == "-=" || t == "*=" || t == "/=" || t == "%=" || t == "&=" ||
               t == "|=" || t == "^=" || t == "<<=" || t == ">>=";
    }

    std::unique_ptr<Expr> parseAssign() {
        if (isKw("throw")) {
            // throw e  /  throw  (rethrow): the checker knows the type of e and builds the throw.
            Token at = cur();
            advance();
            auto call = ex(Expr::Kind::Call, at);
            call->kids.push_back(eName("\x01" "throw", Pos{at.line, at.column, at.file}));
            if (!isP(";") && !isP(")") && !isP(",")) call->kids.push_back(parseAssign());
            return call;
        }
        auto e = parseCond();
        if (!isAssignOp()) return e;
        Token op = cur();
        advance();
        // x = {a, b}: a braced list on the right of = ([expr.ass] p9); the checker makes it an
        // object of the left side's class, or the one value of a scalar.
        auto r = op.text == "=" && isP("{") ? parseInitializer() : parseAssign();
        auto a = ex(Expr::Kind::Assign, op);
        a->text = op.text;
        a->kids.push_back(std::move(e));
        a->kids.push_back(std::move(r));
        return a;
    }

    std::unique_ptr<Expr> parseCond() {
        auto e = parseLor();
        if (!eatP("?")) return e;
        auto a = parseExpr();
        expectP(":", ":");
        auto b = parseCond();
        auto c = std::make_unique<Expr>();
        c->kind = Expr::Kind::Cond;
        c->line = e->line;
        c->kids.push_back(std::move(e));
        c->kids.push_back(std::move(a));
        c->kids.push_back(std::move(b));
        return c;
    }

    std::unique_ptr<Expr> parseLeft(std::unique_ptr<Expr> (Parser::*next)(),
                                    std::initializer_list<const char*> ops) {
        auto e = (this->*next)();
        for (;;) {
            const char* found = nullptr;
            for (const char* op : ops) {
                if (isP(op)) {
                    found = op;
                    break;
                }
            }
            if (!found) break;
            Token op = cur();
            advance();
            auto r = (this->*next)();
            auto b = ex(Expr::Kind::Bin, op);
            b->text = found;
            b->kids.push_back(std::move(e));
            b->kids.push_back(std::move(r));
            e = std::move(b);
        }
        return e;
    }

    std::unique_ptr<Expr> parseLor() { return parseLeft(&Parser::parseLand, {"||"}); }
    std::unique_ptr<Expr> parseLand() { return parseLeft(&Parser::parseBor, {"&&"}); }
    std::unique_ptr<Expr> parseBor() { return parseLeft(&Parser::parseXor, {"|"}); }
    std::unique_ptr<Expr> parseXor() { return parseLeft(&Parser::parseBand, {"^"}); }
    std::unique_ptr<Expr> parseBand() { return parseLeft(&Parser::parseEq, {"&"}); }
    std::unique_ptr<Expr> parseEq() { return parseLeft(&Parser::parseRel, {"==", "!="}); }
    std::unique_ptr<Expr> parseRel() { return parseLeft(&Parser::parseShift, {"<", ">", "<=", ">="}); }
    std::unique_ptr<Expr> parseShift() { return parseLeft(&Parser::parseAdd, {"<<", ">>"}); }
    std::unique_ptr<Expr> parseAdd() { return parseLeft(&Parser::parseMul, {"+", "-"}); }
    std::unique_ptr<Expr> parseMul() { return parseLeft(&Parser::parsePm, {"*", "/", "%"}); }
    // a.*pm and p->*pm ([expr.mptr.oper]): the checker makes them a dereference.
    std::unique_ptr<Expr> parsePm() { return parseLeft(&Parser::parseCast, {".*", "->*"}); }

    // The index just past the type name (N::T, T<args>, N::T<A>::U) at toks_[k]; toks_.size() if
    // a ';' comes inside its template arguments.
    std::size_t afterTypeName(std::size_t k) const {
        while (k < toks_.size() && toks_[k].kind == Tok::Ident) {
            ++k;
            if (k < toks_.size() && isP(toks_[k], "<")) {
                int depth = 0;
                for (; k < toks_.size(); ++k) {
                    if (isP(toks_[k], "<")) ++depth;
                    else if (isP(toks_[k], ">") && --depth == 0) break;
                    else if (isP(toks_[k], ">>") && (depth -= 2) <= 0) break;
                    else if (isP(toks_[k], ";")) return toks_.size();
                }
                ++k;
            }
            if (k < toks_.size() && isP(toks_[k], "::")) ++k;
            else break;
        }
        return k;
    }
    // At toks_[k] a statement that starts with a type name but is an expression ([stmt.ambig]: it
    // cannot be a declaration): T().f(..), T(x).f(..), T{..}.f(..), T();
    // std::vector<int>().swap(v) empties v.
    bool typeThenExprStmt(std::size_t k) const {
        if (k >= toks_.size() || toks_[k].kind != Tok::Ident) return false;
        k = afterTypeName(k);
        if (k + 1 >= toks_.size() || !(isP(toks_[k], "(") || isP(toks_[k], "{"))) return false;
        const bool paren = isP(toks_[k], "(");
        if (paren && isP(toks_[k + 1], ")")) return true;
        int depth = 0;
        for (; k < toks_.size(); ++k) {
            if (isP(toks_[k], "(") || isP(toks_[k], "{") || isP(toks_[k], "[")) ++depth;
            else if ((isP(toks_[k], ")") || isP(toks_[k], "}") || isP(toks_[k], "]")) && --depth == 0) break;
        }
        return k + 1 < toks_.size() && (isP(toks_[k + 1], ".") || isP(toks_[k + 1], "->"));
    }

    // At toks_[k] a type name (N::T, T<args>) followed by '(' and a literal or by '{': a temporary
    // of that type, so W a(std::string("x")); declares an object, not a function
    // ([dcl.ambig.res] p1: it cannot be a parameter declaration).
    bool typeThenValue(std::size_t k) const {
        k = afterTypeName(k);
        if (k + 1 >= toks_.size()) return false;
        if (isP(toks_[k], "{")) return true;
        if (!isP(toks_[k], "(")) return false;
        Tok n = toks_[k + 1].kind;
        return n == Tok::Int || n == Tok::Float || n == Tok::CharLit || n == Tok::StringLit;
    }

    bool looksLikeCast() const {
        if (!isP("(")) return false;
        for (std::size_t k = i_ + 1; k < toks_.size(); ++k) {
            if (toks_[k].kind == Tok::Newline) continue;
            if (!isTypeKw(toks_[k])) return false;
            // A qualified type (N::T): look at what follows the whole name.
            std::size_t qend = 0;
            if (k + 1 < toks_.size() && isP(toks_[k + 1], "::") && qualTypeAt(k, qend) && qend > k) k = qend - 1;
            else if (std::size_t tn = k + 1 < toks_.size() && isP(toks_[k + 1], "::") ? tmplAfterNs(k, true) : 0) k = tn;
            if (k + 1 < toks_.size() && isP(toks_[k + 1], "<")) {
                // (X<A, B>(x)): the template arguments of a class template name come first.
                int depth = 0;
                std::size_t j = k + 1;
                for (; j < toks_.size(); ++j) {
                    if (isP(toks_[j], "<")) ++depth;
                    else if (isP(toks_[j], ">") && --depth == 0) break;
                    else if (isP(toks_[j], ">>") && (depth -= 2) <= 0) break;
                    else if (isP(toks_[j], ";") || isP(toks_[j], "{")) break;
                }
                if (j < toks_.size() && depth <= 0) k = j;
                if (k + 2 < toks_.size() && isP(toks_[k + 1], "::") && toks_[k + 2].kind == Tok::Ident) {
                    // (X<T>::name ...): a member type is a cast only where a type may end, and
                    // before ')' only when an operand follows; (is_same<U, T>::value ? 1 : 0) is
                    // an expression.
                    while (k + 2 < toks_.size() && isP(toks_[k + 1], "::") && toks_[k + 2].kind == Tok::Ident) k += 2;
                    if (k + 1 >= toks_.size()) return false;
                    const Token& n = toks_[k + 1];
                    if (isP(n, "*") || isP(n, "&") || isP(n, "&&")) return true;
                    if (!isP(n, ")") || k + 2 >= toks_.size()) return false;
                    const Token& a = toks_[k + 2];
                    return a.kind == Tok::Ident || a.kind == Tok::Int || a.kind == Tok::Float || isP(a, "(") ||
                           isKwTok(a, "this");
                }
            }
            // (T(x) ...) and (T{...} ...) are a functional cast or a temporary inside parentheses,
            // not a cast; (T(*)(int)) is still a cast to a pointer to function.
            if (k + 2 < toks_.size() && isP(toks_[k + 1], "{")) return false;
            if (k + 2 < toks_.size() && isP(toks_[k + 1], "(") && !isP(toks_[k + 2], "*") && !isP(toks_[k + 2], "&"))
                return false;
            return true;
        }
        return false;
    }

    std::unique_ptr<Type> parseTypeName() {
        auto base = parseBase();
        if (isP("*") || isP("(") || isP("[") || isP("&") || isP("&&")) {  // (& and &&: -> const T & of a lambda)
            Parsed d = parseDeclarator(false);
            return apply(std::move(base), d.mods);
        }
        return base;
    }

    std::unique_ptr<Expr> parseCast() {
        if (!looksLikeCast()) return parseUnary();
        Token at = cur();
        eatP("(");
        auto tyName = parseTypeName();
        expectP(")", ")");
        auto inner = parseCast();
        auto e = ex(Expr::Kind::Cast, at);
        e->ty = std::move(tyName);
        e->kids.push_back(std::move(inner));
        return e;
    }

    // &C::m for a non-static data member m ([expr.unary.op] p3): a pointer to member, T C::*. It is
    // kept as the member's offset plus one: (T C::*)((long)&((C *)0)->m + 1).
    std::unique_ptr<Expr> tryMemberPointer() {
        std::size_t j = i_ + 1;
        std::string prefix;
        while (j + 2 < toks_.size() && toks_[j].kind == Tok::Ident && isP(toks_[j + 1], "::") &&
               toks_[j + 2].kind == Tok::Ident) {
            prefix += (prefix.empty() ? "" : "::") + toks_[j].text;
            j += 2;
        }
        if (prefix.empty() || j + 1 >= toks_.size() || isP(toks_[j + 1], "(") || isP(toks_[j + 1], "<")) return nullptr;
        std::string cls = classPrefix(prefix);
        const Type* T = cls.empty() ? nullptr : classOf(cls);
        if (!T) return nullptr;
        const Param* f = nullptr;
        for (const Param& q : T->params)
            if (isDataField(q) && q.name == toks_[j].text) f = &q;
        if (!f || f->bits >= 0 || f->type->kind == Type::Kind::Reference) return nullptr;
        Pos p = here();
        std::string field = f->name;
        auto pmT = ty(Type::Kind::Pointer);
        pmT->inner = cloneType(*f->type);
        pmT->memberOf = cls;
        i_ = j + 1;
        auto cptr = ty(Type::Kind::Pointer);
        cptr->inner = ty(Type::Kind::Struct);
        cptr->inner->tag = cls;
        auto at = eAddr(eArrow(eCastTo(std::move(cptr), eInt(0, p), p), field, p), p);
        auto plus = std::make_unique<Expr>();
        plus->kind = Expr::Kind::Bin;
        plus->text = "+";
        plus->line = p.line;
        plus->col = p.col;
        plus->file = p.file;
        plus->kids.push_back(eCastTo(ty(Type::Kind::Long), std::move(at), p));
        plus->kids.push_back(eInt(1, p));
        return eCastTo(std::move(pmT), std::move(plus), p);
    }

    std::unique_ptr<Expr> parseUnary() {
        if (atEnd()) {
            err("expected an expression");
            return zeroAt();
        }
        if (isP("&"))
            if (auto pm = tryMemberPointer()) return pm;
        if (isP("::") && i_ + 1 < toks_.size() &&
            (toks_[i_ + 1].kind == Tok::Ident || isKwTok(toks_[i_ + 1], "new") || isKwTok(toks_[i_ + 1], "delete")))
            advance();  // ::f(), ::new: the global name (see isTypeKw)
        if (isP("++") || isP("--")) {
            Token op = cur();
            advance();
            auto inner = parseUnary();
            auto e = ex(Expr::Kind::Pre, op);
            e->text = op.text;
            e->kids.push_back(std::move(inner));
            return e;
        }
        if (isKw("sizeof")) {
            Token op = cur();
            advance();
            auto e = ex(Expr::Kind::Unary, op);
            e->text = "sizeof";
            if (eatP("(")) {
                if (!atEnd() && isTypeKw(cur())) e->ty = parseTypeName();
                else e->kids.push_back(parseExpr());
                expectP(")", ")");
                return e;
            }
            e->kids.push_back(parseUnary());
            return e;
        }
        if (isKw("new") && i_ + 1 < toks_.size() && isP(toks_[i_ + 1], "(")) {
            // Placement new: new (p) T(args), new (p) T{...}, new (p) T ([expr.new] p14): the object
            // is built at p, and the value is p as a T *. p is used twice, so it must have no
            // side effects.
            Token at = cur();
            Pos pp = here();
            advance();
            advance();
            auto place = parseExpr();
            expectP(")", ")");
            auto tyName = parseBase();
            while (eatP("*")) {
                auto ptr = ty(Type::Kind::Pointer);
                ptr->inner = std::move(tyName);
                tyName = std::move(ptr);
            }
            std::vector<std::unique_ptr<Expr>> args;
            bool braced = false;
            if (eatP("(")) {
                if (!eatP(")")) {
                    args.push_back(parseAssign());
                    while (eatP(",")) args.push_back(parseAssign());
                    expectP(")", ")");
                }
            } else if (isP("{")) {
                braced = true;
                auto list = parseInitializer();
                args = std::move(list->kids);
            }
            if (hasSideEffects(*place)) err("placement new needs an address without side effects");
            auto ptrT = ty(Type::Kind::Pointer);
            ptrT->inner = cloneType(*tyName);
            auto at1 = eCastTo(cloneType(*ptrT), cloneExpr(*place), pp);
            auto at2 = eCastTo(cloneType(*ptrT), std::move(place), pp);
            auto deref = std::make_unique<Expr>();
            deref->kind = Expr::Kind::Unary;
            deref->text = "*";
            deref->line = pp.line;
            deref->col = pp.col;
            deref->file = pp.file;
            deref->kids.push_back(std::move(at1));
            std::unique_ptr<Expr> build;
            Type* cls = tyName->kind == Type::Kind::Struct && !tyName->tag.empty() ? classOf(tyName->tag) : nullptr;
            if (cls && braced) {
                auto tmp = braceTemp(*tyName, std::move(args), pp);
                std::vector<std::unique_ptr<Expr>> a;
                a.push_back(eAddr(std::move(deref), pp));
                a.push_back(std::move(tmp));
                build = eCall(eName("\x01" "cinit:" + tyName->tag, pp), std::move(a), pp);
            } else if (cls && args.size() == 1) {
                std::vector<std::unique_ptr<Expr>> a;
                a.push_back(eAddr(std::move(deref), pp));
                a.push_back(std::move(args[0]));
                build = eCall(eName("\x01" "cinit:" + tyName->tag, pp), std::move(a), pp);
            } else if (cls) {
                build = ctorCall(tyName->tag, eAddr(std::move(deref), pp), std::move(args), pp);
            } else {
                std::unique_ptr<Expr> v = args.empty() ? zeroAt() : std::move(args[0]);
                build = eAssign(std::move(deref), std::move(v), pp);
            }
            auto comma = ex(Expr::Kind::Comma, at);
            comma->kids.push_back(std::move(build));
            comma->kids.push_back(std::move(at2));
            return comma;
        }
        if (isKw("new")) {
            Token at = cur();
            advance();
            auto tyName = parseBase();
            while (eatP("*")) {
                auto ptr = ty(Type::Kind::Pointer);
                ptr->inner = std::move(tyName);
                tyName = std::move(ptr);
            }
            std::vector<std::unique_ptr<Expr>> args;
            bool paren = false;
            if (isP("[")) {
                // new T[n]: a block of n objects (ISO/IEC 14882 [expr.new]). A class with a
                // constructor or destructor goes through the generated __vnew_ helper (which
                // stores the count in front of the block when the destructor must run per element).
                advance();
                auto count = parseExpr();
                expectP("]", "]");
                Pos vp;
                vp.line = at.line;
                vp.col = at.column;
                vp.file = at.file;
                Type* vc = tyName && tyName->kind == Type::Kind::Struct && !tyName->tag.empty() ? classOf(tyName->tag)
                                                                                                 : nullptr;
                if (vc && (vc->needCtor || vc->needDtor)) {
                    bool defaultCtor = !vc->needCtor;
                    for (const Param& q : vc->params)
                        if (q.name == "<ctor>" && q.type && q.type->params.empty()) defaultCtor = true;
                    if (!defaultCtor) err("new[] needs a default constructor for '" + tyName->tag + "'");
                    std::vector<std::unique_ptr<Expr>> a;
                    a.push_back(std::move(count));
                    return eCall(eName("__vnew_" + mangleNested(tyName->tag), vp), std::move(a), vp);
                }
                auto size = ex(Expr::Kind::Unary, at);
                size->text = "sizeof";
                size->ty = cloneType(*tyName);
                auto bytes = eBin("*", std::move(count), std::move(size), vp);
                std::vector<std::unique_ptr<Expr>> a;
                a.push_back(std::move(bytes));
                auto cast = ex(Expr::Kind::Cast, at);
                auto pt = ty(Type::Kind::Pointer);
                pt->inner = cloneType(*tyName);
                cast->ty = std::move(pt);
                cast->kids.push_back(eCall(eName("malloc", vp), std::move(a), vp));
                return cast;
            }
            if (isP("(")) {
                advance();
                paren = true;
                if (!eatP(")")) {
                    args.push_back(parseAssign());
                    while (eatP(",")) args.push_back(parseAssign());
                    expectP(")", ")");
                }
            }
            Pos pos;
            pos.line = at.line;
            pos.col = at.column;
            pos.file = at.file;
            auto call = ex(Expr::Kind::Call, at);
            auto name = std::make_unique<Expr>();
            name->kind = Expr::Kind::Name;
            name->text = "malloc";
            name->line = at.line;
            name->col = at.column;
            auto sz = ex(Expr::Kind::Unary, at);
            sz->text = "sizeof";
            sz->ty = tyName ? cloneType(*tyName) : ty(Type::Kind::Int);
            call->kids.push_back(std::move(name));
            call->kids.push_back(std::move(sz));
            auto e = ex(Expr::Kind::Cast, at);
            auto ptr = ty(Type::Kind::Pointer);
            ptr->inner = cloneType(*tyName);
            e->ty = std::move(ptr);
            e->kids.push_back(std::move(call));
            Type* cls = tyName && tyName->kind == Type::Kind::Struct && !tyName->tag.empty()
                            ? classOf(tyName->tag)
                            : nullptr;
            bool scalarInit = paren && args.size() == 1 && !cls;
            if ((cls && cls->needCtor) || scalarInit) {
                std::string tmp = "__new" + std::to_string(newN_++);
                auto decl = std::make_unique<Decl>();
                decl->name = tmp;
                decl->line = at.line;
                decl->col = at.column;
                decl->file = at.file;
                auto tp = ty(Type::Kind::Pointer);
                tp->inner = cloneType(*tyName);
                decl->type = std::move(tp);
                preStmts_.push_back(declStmt(std::move(decl)));
                auto comma = ex(Expr::Kind::Comma, at);
                comma->kids.push_back(eAssign(eName(tmp, pos), std::move(e), pos));
                if (cls && cls->needCtor) {
                    comma->kids.push_back(ctorCall(tyName->tag, eName(tmp, pos), std::move(args), pos));
                } else {
                    auto deref = ex(Expr::Kind::Unary, at);
                    deref->text = "*";
                    deref->kids.push_back(eName(tmp, pos));
                    comma->kids.push_back(eAssign(std::move(deref), std::move(args[0]), pos));
                }
                comma->kids.push_back(eName(tmp, pos));
                return comma;
            }
            return e;
        }
        if (isKw("delete")) {
            Token at = cur();
            advance();
            bool vec = false;
            if (isP("[") && nextIs("]")) {
                advance();
                advance();
                vec = true;
            }
            auto arg = parseCast();
            Pos pos;
            pos.line = at.line;
            pos.col = at.column;
            pos.file = at.file;
            std::vector<std::unique_ptr<Expr>> args;
            args.push_back(std::move(arg));
            return eCall(eName(vec ? "\x01" "delete[]" : "\x01" "delete", pos), std::move(args), pos);
        }
        if (isP("&") || isP("*") || isP("+") || isP("-") || isP("~") || isP("!")) {
            Token op = cur();
            advance();
            auto inner = parseCast();
            auto e = ex(Expr::Kind::Unary, op);
            e->text = op.text;
            e->kids.push_back(std::move(inner));
            return e;
        }
        return parsePostfix();
    }

    std::unique_ptr<Expr> parsePostfix() {
        auto e = parsePrimary();
        while (!atEnd()) {
            if (eatP("[")) {
                Token at = cur();
                // m[{a, b}]: a braced list as the subscript ([expr.sub] p1), given to operator[].
                auto idx = isP("{") ? parseInitializer() : parseExpr();
                expectP("]", "]");
                auto n = ex(Expr::Kind::Index, at);
                n->kids.push_back(std::move(e));
                n->kids.push_back(std::move(idx));
                e = std::move(n);
                continue;
            }
            if (eatP("(")) {
                auto n = std::make_unique<Expr>();
                n->kind = Expr::Kind::Call;
                n->line = e->line;
                n->col = e->col;
                n->kids.push_back(std::move(e));
                if (!eatP(")")) {
                    // An argument may be a braced list; the checker makes it an object of the
                    // parameter's class.
                    n->kids.push_back(isP("{") ? parseInitializer() : parseAssign());
                    while (eatP(",")) n->kids.push_back(isP("{") ? parseInitializer() : parseAssign());
                    expectP(")", ")");
                }
                e = std::move(n);
                continue;
            }
            if (isP("++") || isP("--")) {
                Token op = cur();
                advance();
                auto n = ex(Expr::Kind::Post, op);
                n->text = op.text;
                n->kids.push_back(std::move(e));
                e = std::move(n);
                continue;
            }
            if (isP(".") || isP("->")) {
                Token op = cur();
                advance();
                if (isP("~") && i_ + 3 < toks_.size() && toks_[i_ + 1].kind == Tok::Ident && isP(toks_[i_ + 2], "(") &&
                    isP(toks_[i_ + 3], ")")) {
                    // p->~T() and x.~T(): the destructor, or nothing for a type without one (a
                    // scalar T in a template: a pseudo-destructor, [expr.prim.id.dtor]).
                    std::string nm = toks_[i_ + 1].text;
                    Pos pp = here();
                    i_ += 4;
                    const Type* td = findTypedef(nm);
                    std::unique_ptr<Expr> self = op.text == "->" ? std::move(e) : eAddr(std::move(e), pp);
                    std::unique_ptr<Expr> call;
                    if (td && td->kind == Type::Kind::Struct && !td->tag.empty() && classOf(td->tag))
                        call = dtorCall(td->tag, std::move(self), pp);
                    if (!call) call = zeroAt();
                    e = std::move(call);
                    continue;
                }
                eatKw("template");  // x.template f<T>() ([temp.names] p4)
                if (atEnd() || cur().kind != Tok::Ident) {
                    err("expected a field name");
                    continue;
                }
                auto n = ex(Expr::Kind::Member, op);
                n->text = op.text;
                n->kids.push_back(std::move(e));
                n->kids.push_back(leafName(cur()));
                Token at = cur();
                advance();
                if (isP("<")) {
                    // obj.f<T>(..) with f a member function template ([temp.arg.explicit]): the
                    // arguments travel as carriers on the name; the checker deduces the rest.
                    if (const MemberTmpl* mt = memberTmplNamed(at.text); mt && angleCloses(i_)) {
                        std::vector<TArg> args;
                        TDecl td = mt->td;
                        parseTemplateArgs(td, args, true);
                        addCarriers(*n->kids[1], args, at);
                    }
                }
                e = std::move(n);
                continue;
            }
            break;
        }
        return e;
    }

    std::unique_ptr<Expr> parsePrimary() {
        if (atEnd()) {
            err("expected an expression");
            return zeroAt();
        }
        if (isKw("this")) {
            auto e = leafName(cur());
            advance();
            return e;
        }
        if (isP("[")) return parseLambda();
        if (cur().kind == Tok::Ident && nextIs("<") && !isLocalName(cur().text)) {
            // f<T>(..) in a member function of a class with a member function template f: this->f<T>(..)
            // ([class.mfct.non.static] p2, [temp.arg.explicit]).
            std::string cls = bodyClass_;
            for (auto it = classStack_.rbegin(); cls.empty() && it != classStack_.rend(); ++it)
                if (*it) cls = (*it)->tag;
            auto mit = cls.empty() ? memberTmpl_.end() : memberTmpl_.find(cls + "::" + cur().text);
            if (mit != memberTmpl_.end() && !mit->second.isStatic && !mit->second.isCtor && angleCloses(i_ + 1)) {
                Token at = cur();
                auto n = ex(Expr::Kind::Member, at);
                n->text = "->";
                n->kids.push_back(leafName(at));
                n->kids.back()->text = "this";
                n->kids.push_back(leafName(at));
                advance();
                std::vector<TArg> args;
                TDecl td = mit->second.td;
                parseTemplateArgs(td, args, true);
                addCarriers(*n->kids[1], args, at);
                return n;
            }
        }
        if (cur().kind == Tok::Ident && !sbinds_.empty() && !nextIs("::")) {
            if (const SBind* b = boundName(cur().text)) {
                auto e = leafName(cur());
                e->text = "\x01" "sb:" + b->var + ":" + std::to_string(b->index);
                advance();
                return e;
            }
        }
        if (cur().kind == Tok::Ident && !staticTmplShort_.empty()) {
            // f(..), C::f(..), N::C::f<T>(..) where f is a static member function template.
            std::size_t j = i_;
            std::string chain = cur().text;
            while (j + 2 < toks_.size() && isP(toks_[j + 1], "::") && toks_[j + 2].kind == Tok::Ident) {
                chain += "::" + toks_[j + 2].text;
                j += 2;
            }
            std::string key;
            if (staticTmplShort_.count(toks_[j].text) && j + 1 < toks_.size() &&
                (isP(toks_[j + 1], "(") || isP(toks_[j + 1], "<")) && !(j == i_ && isLocalName(chain)))
                key = staticTmplKey(chain);
            if (!key.empty()) {
                Token at = cur();
                if (j > i_) {
                    // Log::f after "using okay::Log;": the class the first name stands for.
                    const Type* td = findTypedef(toks_[i_].text);
                    if (td && td->kind == Type::Kind::Struct && !td->tag.empty() &&
                        memberTmpl_.count(td->tag + chain.substr(toks_[i_].text.size())))
                        chain = td->tag + chain.substr(toks_[i_].text.size());
                }
                i_ = j + 1;
                auto e = ex(Expr::Kind::Name, at);
                e->text = "\x01" "tmpl:" + chain;
                if (isP("<")) {
                    // Explicit arguments travel as carriers; the checker deduces the rest.
                    std::vector<TArg> args;
                    parseTemplateArgs(memberTmpl_[key].td, args, true);
                    addCarriers(*e, args, at);
                }
                return e;
            }
        }
        if (cur().kind == Tok::Ident && nextIs("::")) {
            // std::find(...), std::vector<int>(...): the template's bare name.
            std::size_t ti = tmplAfterNs(i_, false);
            if (ti) {
                // N::f(...): the namespace part travels with the template name, so the checker can
                // take N's own ordinary function f when no template f fits the call
                // (std::filesystem::remove(p) beside the algorithm std::remove).
                tmplQual_.clear();
                for (std::size_t k = i_; k + 1 < ti; ++k) tmplQual_ += toks_[k].text;
                if (tmplQual_.size() >= 2 && tmplQual_.compare(tmplQual_.size() - 2, 2, "::") == 0) tmplQual_.resize(tmplQual_.size() - 2);
            }
            if (!ti) {
                ti = tmplAfterNs(i_, true);
                if (ti && !(ti + 1 < toks_.size() && isP(toks_[ti + 1], "<"))) ti = 0;
            }
            if (ti) i_ = ti;
        }
        if (cur().kind == Tok::Ident && aliasTmpl_.count(cur().text) && nextIs("<") && !isLocalName(cur().text)) {
            // conditional_t<B, X, Y>::value: a static member of the class the alias names.
            Token at = cur();
            auto t = useAliasTemplate();
            if (t && t->kind == Type::Kind::Struct && isP("::") && i_ + 1 < toks_.size() && toks_[i_ + 1].kind == Tok::Ident) {
                advance();
                auto e = leafName(cur());
                e->text = t->tag + "::" + cur().text;
                advance();
                long cv = 0;
                if (memberConst(e->text, cv)) {
                    auto lit = ex(Expr::Kind::Int, at);
                    lit->ival = static_cast<std::uint64_t>(cv);
                    return lit;
                }
                return e;
            }
            err("expected '::' after an alias template in an expression");
            return zeroAt();
        }
        if (cur().kind == Tok::Ident && varTmpl_.count(cur().text) && nextIs("<") && !isLocalName(cur().text)) {
            tmplQual_.clear();
            return useVarTemplate();
        }
        if (cur().kind == Tok::Ident && cur().text.compare(0, 10, "__okay_is_") == 0 && nextIs("(")) return typeTrait();
        if (isKw("nullptr")) {
            auto e = ex(Expr::Kind::Int, cur());
            e->ival = 0;
            advance();
            return e;
        }
        if (nextIs("(") && (isKw("int") || isKw("double") || isKw("float") || isKw("char") || isKw("bool") ||
                            isKw("long") || isKw("short") || isKw("unsigned") || isKw("signed") ||
                            (cur().kind == Tok::Ident && findTypedef(cur().text) &&
                             findTypedef(cur().text)->kind != Type::Kind::Struct &&
                             findTypedef(cur().text)->kind != Type::Kind::Union))) {
            // A functional cast T(e) of a scalar type ([expr.type.conv]); T() is zero.
            Token at = cur();
            auto tyName = parseBase();
            expectP("(", "(");
            std::unique_ptr<Expr> inner;
            if (isP(")")) {
                inner = leafInt(at);
                inner->ival = 0;
            }
            else inner = parseExpr();
            expectP(")", ")");
            auto e = ex(Expr::Kind::Cast, at);
            e->ty = std::move(tyName);
            e->kids.push_back(std::move(inner));
            return e;
        }
        if (cur().kind == Tok::Ident && nextIs("::")) {
            std::size_t qend = 0;
            std::size_t start = i_;
            const Type* qt = qualTypeAt(i_, qend);
            if (qt && qend < toks_.size() && (isP(toks_[qend], "(") || isP(toks_[qend], "{"))) {
                // N::T(e) or N::T{...}
                Token at = cur();
                Pos pos = here();
                i_ = qend;
                auto tyName = cloneType(*qt);
                if (tyName->kind == Type::Kind::Struct && isP("{")) {
                    auto list = parseInitializer();
                    return braceTemp(*tyName, std::move(list->kids), pos);
                }
                if (tyName->kind != Type::Kind::Struct && tyName->kind != Type::Kind::Union && eatP("(")) {
                    std::unique_ptr<Expr> inner;
                    if (isP(")")) {
                inner = leafInt(at);
                inner->ival = 0;
            }
                    else inner = parseExpr();
                    expectP(")", ")");
                    auto e = ex(Expr::Kind::Cast, at);
                    e->ty = std::move(tyName);
                    e->kids.push_back(std::move(inner));
                    return e;
                }
                if (tyName->kind == Type::Kind::Struct && !tyName->tag.empty() && eatP("(")) {
                    // N::T(args): a temporary of the class, as for T(args) below.
                    std::vector<std::unique_ptr<Expr>> args;
                    if (!eatP(")")) {
                        args.push_back(parseAssign());
                        while (eatP(",")) args.push_back(parseAssign());
                        expectP(")", ")");
                    }
                    std::string tmp = "__tmp" + std::to_string(newN_++);
                    auto comma = ex(Expr::Kind::Comma, at);
                    comma->ival = 7;
                    comma->text = tmp;
                    comma->ty = cloneType(*tyName);
                    comma->kids.push_back(ctorCall(tyName->tag, eAddr(eName(tmp, pos), pos), std::move(args), pos));
                    comma->kids.push_back(eName(tmp, pos));
                    return comma;
                }
                i_ = start;
            }
        }
        if (isKw("typeid") && nextIs("(")) {
            // typeid(type) and typeid(expression) ([expr.typeid]): the checker picks the type_info
            // object, from the static type or, for an object of a class with virtual functions, from
            // its vtable.
            Token at = cur();
            advance();
            advance();
            auto e = ex(Expr::Kind::Call, at);
            e->kids.push_back(leafName(at));
            e->kids.back()->text = "\x01" "typeid";
            if (isTypeKw(cur())) e->ty = parseTypeName();
            else e->kids.push_back(parseExpr());
            expectP(")", ")");
            return e;
        }
        if (isKw("dynamic_cast") && nextIs("<")) {
            // dynamic_cast<T *>(p) ([expr.dynamic.cast]): checked against the object's type_info chain.
            Token at = cur();
            advance();
            advance();
            auto tyName = parseTypeName();
            closeAngle();
            expectP("(", "(");
            auto inner = parseExpr();
            expectP(")", ")");
            auto e = ex(Expr::Kind::Call, at);
            e->kids.push_back(leafName(at));
            e->kids.back()->text = "\x01" "dyncast";
            e->kids.push_back(std::move(inner));
            e->ty = std::move(tyName);
            return e;
        }
        if ((isKw("static_cast") || isKw("reinterpret_cast") || isKw("const_cast")) && nextIs("<")) {
            // static_cast<T>(e), reinterpret_cast<T>(e), const_cast<T>(e) (ISO/IEC 14882 [expr.static.cast],
            // [expr.reinterpret.cast], [expr.const.cast]). The subset checks them like a C cast (T)e.
            Token at = cur();
            advance();
            advance();
            auto tyName = parseTypeName();
            expectP(">", ">");
            expectP("(", "(");
            auto inner = parseExpr();
            expectP(")", ")");
            auto e = ex(Expr::Kind::Cast, at);
            e->ty = std::move(tyName);
            e->kids.push_back(std::move(inner));
            return e;
        }
        // A function template's name is a template only where it is called or given template
        // arguments; `bool forward` or `count = 0` is an ordinary name (a variable of that name).
        if (cur().kind == Tok::Ident && ftmpl_.count(cur().text) && (nextIs("(") || nextIs("<")) &&
            !isLocalName(cur().text)) {
            std::string name = cur().text;
            Token at = cur();
            TDecl td = ftmpl_[name];
            advance();
            auto e = ex(Expr::Kind::Name, at);
            if (isP("<") && ftmpl_.count(name + "#1")) {
                // Several templates of this name (get<0>(tuple) beside get<T>(variant)): each
                // argument is a type or a value as it reads, and the checker picks the template
                // whose parameters take them ([temp.arg.explicit], [temp.over]).
                advance();
                std::vector<TArg> args;
                while (!atEnd() && !isAngleClose()) {
                    TParam any;
                    any.isType = isTypeKw(cur());
                    args.push_back(parseOneArg(any));
                    if (!eatP(",")) break;
                }
                closeAngle();
                e->text = "\x01" "tmpl:" + name;
                if (!tmplQual_.empty()) e->text += "\x04" + qualifyNs(tmplQual_);
                addCarriers(*e, args, at);
                tmplQual_.clear();
                return e;
            }
            if (isP("<")) {
                std::vector<TArg> args;
                parseTemplateArgs(td, args, true);
                if (args.size() < td.params.size()) {
                    // Only the first template arguments are given: the checker deduces the rest from
                    // the call and finishes the instance. The given ones travel as carriers.
                    e->text = "\x01" "tmpl:" + name;
                    addCarriers(*e, args, at);
                    return e;
                }
                const TemplateSig& sig = funcTemplateSigs()[name];
                InstReq r;
                r.tmpl = name;
                r.name = instFuncName(name, sig, args);
                r.args = cloneArgs(args);
                e->text = r.name;
                work_.push_back(std::move(r));
            } else {
                e->text = "\x01" "tmpl:" + name;
                if (!tmplQual_.empty()) e->text += "\x04" + qualifyNs(tmplQual_);
            }
            tmplQual_.clear();
            return e;
        }
        if (cur().kind == Tok::Ident && (cur().text == "true" || cur().text == "false") && !nextIs("::")) {
            auto e = ex(Expr::Kind::Int, cur());
            e->ival = cur().text == "true" ? 1 : 0;
            e->text = cur().text;  // the checker types it bool
            advance();
            return e;
        }
        if (cur().kind == Tok::Ident) {
            auto it = enums_.find(cur().text);
            if (it != enums_.end() && !nextIs("::") && enumeratorVisible(cur().text)) {
                auto e = ex(Expr::Kind::Int, cur());
                e->ival = static_cast<std::uint64_t>(it->second);
                advance();
                return e;
            }

            if (nextIs("::")) {
                // A qualified enumerator: E::e, N::E::e, N::e.
                std::string chain = cur().text;
                std::size_t j = i_ + 1;
                while (j + 1 < toks_.size() && isP(toks_[j], "::") && toks_[j + 1].kind == Tok::Ident) {
                    chain += "::" + toks_[j + 1].text;
                    j += 2;
                }
                std::vector<std::string> prefixes;
                for (std::size_t keep = ns_.size() + 1; keep-- > 0;) {
                    std::string q;
                    for (std::size_t k = 0; k < keep; ++k) q += ns_[k] + "::";
                    prefixes.push_back(q);
                }
                for (const std::string& u : usingNs_) prefixes.push_back(u + "::");
                auto ea = enumAlias_.find(cur().text);
                if (ea != enumAlias_.end() && enums_.count(ea->second + chain.substr(cur().text.size()))) {
                    auto e = ex(Expr::Kind::Int, cur());
                    e->ival = static_cast<std::uint64_t>(enums_[ea->second + chain.substr(cur().text.size())]);
                    i_ = j;
                    return e;
                }
                if (std::size_t cut = chain.rfind("::"); cut != std::string::npos && chain.find("::") != cut) {
                    // C::K::e with K a member alias of an enumeration in class C
                    const std::string head = chain.substr(0, cut);
                    for (const std::string& q : prefixes) {
                        auto ma = enumAlias_.find(q + head);
                        if (ma != enumAlias_.end() && enums_.count(ma->second + chain.substr(cut))) {
                            auto e = ex(Expr::Kind::Int, cur());
                            e->ival = static_cast<std::uint64_t>(enums_[ma->second + chain.substr(cut)]);
                            i_ = j;
                            return e;
                        }
                    }
                }
                std::string viaClass;  // Log::Level::Info after "using okay::Log;" ([class.qual])
                if (const Type* td = findTypedef(cur().text))
                    if (td->kind == Type::Kind::Struct && !td->tag.empty() && td->tag[0] != '\x02')
                        viaClass = td->tag + chain.substr(cur().text.size());
                if (!viaClass.empty() && enums_.count(viaClass)) {
                    auto e = ex(Expr::Kind::Int, cur());
                    e->ival = static_cast<std::uint64_t>(enums_[viaClass]);
                    i_ = j;
                    return e;
                }
                for (const std::string& q : prefixes) {
                    auto hit = enums_.find(q + chain);
                    if (hit == enums_.end()) continue;
                    auto e = ex(Expr::Kind::Int, cur());
                    e->ival = static_cast<std::uint64_t>(hit->second);
                    i_ = j;
                    return e;
                }
            }
            std::unique_ptr<Type> instTd;
            if (ctmpl_.count(cur().text) && nextIs("<") && !isLocalName(cur().text) && angleCloses(i_ + 1)) {
                // Name<args>(...) with a class template: the instance, then a temporary of it.
                std::size_t saved = i_;
                instTd = instantiateClass(cur().text);
                if (instTd->kind == Type::Kind::Struct && isP("::") && i_ + 1 < toks_.size() &&
                    toks_[i_ + 1].kind == Tok::Ident) {
                    // Box<int>::member: a static member of the instance.
                    advance();
                    auto e = leafName(cur());
                    e->text = instTd->tag + "::" + cur().text;
                    advance();
                    return e;
                }
                if (isP("{") && instTd->kind == Type::Kind::Struct && !instTd->tag.empty()) {
                    // Name<args>{...}: a temporary of the instance, list-initialized ([expr.type.conv]).
                    Pos pos = here();
                    auto list = parseInitializer();
                    return braceTemp(*instTd, std::move(list->kids), pos);
                }
                if (!isP("(") || instTd->kind != Type::Kind::Struct) {
                    i_ = saved;
                    instTd.reset();
                }
            }
            if (!instTd && nextIs("{")) {
                const Type* td = findTypedef(cur().text);
                if (td && td->kind == Type::Kind::Struct && !td->tag.empty()) {
                    Pos pos = here();
                    advance();
                    auto list = parseInitializer();
                    return braceTemp(*td, std::move(list->kids), pos);
                }
            }
            if (instTd || nextIs("(")) {
                // T(args) for a class T: a temporary, constructed in a hidden local declared before
                // the statement. Its destructor does not run (see the plan: temporaries).
                const Type* td = instTd ? instTd.get() : findTypedef(cur().text);
                if (td && !instTd && (fnNames_.count(cur().text) || isLocalName(cur().text))) td = nullptr;  // (a local or member hides it)
                if (td && td->kind == Type::Kind::Struct && !td->tag.empty()) {
                    Token at = cur();
                    if (!instTd) advance();
                    advance();
                    std::vector<std::unique_ptr<Expr>> args;
                    if (!eatP(")")) {
                        args.push_back(parseAssign());
                        while (eatP(",")) args.push_back(parseAssign());
                        expectP(")", ")");
                    }
                    Pos pos;
                    pos.line = at.line;
                    pos.col = at.column;
                    pos.file = at.file;
                    std::string tmp = "__tmp" + std::to_string(newN_++);
                    auto decl = std::make_unique<Decl>();
                    decl->name = tmp;
                    decl->line = at.line;
                    decl->col = at.column;
                    decl->file = at.file;
                    decl->type = cloneType(*td);
                    std::string tag = td->tag;
                    decl.reset();  // the checker declares the hidden local (it knows where it must live)
                    auto comma = ex(Expr::Kind::Comma, at);
                    comma->ival = 7;  // a temporary: the checker destroys it unless it initializes an object
                    comma->text = tmp;
                    comma->ty = cloneType(*td);
                    comma->kids.push_back(ctorCall(tag, eAddr(eName(tmp, pos), pos), std::move(args), pos));
                    comma->kids.push_back(eName(tmp, pos));
                    return comma;
                }
            }
            auto e = leafName(cur());
            advance();
            while (eatP("::")) {
                if (atEnd() || cur().kind != Tok::Ident) {
                    err("expected a name");
                    break;
                }
                e->text += "::";
                e->text += cur().text;
                advance();
                if (e->text.size() >= 10 && e->text.compare(e->text.size() - 10, 10, "::operator") == 0 && !atEnd() &&
                    cur().kind == Tok::Punct && !isP("::")) {
                    // Base::operator[](i): a base class's operator called by its name ([over.oper] p7).
                    std::string op = cur().text;
                    advance();
                    if (op == "(" && eatP(")")) op = "()";
                    else if (op == "[" && eatP("]")) op = "[]";
                    e->text += op;
                }
            }
            std::size_t last = e->text.rfind("::");
            if (last != std::string::npos) {
                // T::x where T names a class through a typedef or a member type (std::ios::app with
                // typedef ios_base ios; fs::file_time_type::clock::now): the class's own name
                // ([dcl.typedef] p2, [basic.lookup.qual]).
                std::string prefix = e->text.substr(0, last);
                const Type* td = classThroughTypedefs(prefix);
                if (td && td->tag != prefix) e->text = td->tag + e->text.substr(last);
            } else if (localClassFloor_ != static_cast<std::size_t>(-1) && !fnStatics_.empty()) {
                e->text = enclosingStatic(e->text);  // a static local of the function around a local class
            }
            return e;
        }
        if (cur().kind == Tok::Int) {
            auto e = leafInt(cur());
            advance();
            return e;
        }
        if (cur().kind == Tok::Float) {
            auto e = ex(Expr::Kind::Float, cur());
            double dv = cur().fval;
            if (cur().floatRank == 1) {
                float fv = static_cast<float>(dv);
                std::uint32_t bits = 0;
                std::memcpy(&bits, &fv, 4);
                e->ival = bits;
                e->litRank = 1;
            } else {
                std::uint64_t bits = 0;
                std::memcpy(&bits, &dv, 8);
                e->ival = bits;
                e->litRank = 0;
            }
            advance();
            return e;
        }
        if (cur().kind == Tok::CharLit) {
            auto e = ex(Expr::Kind::Char, cur());
            e->ival = cur().charValue;
            advance();
            return e;
        }
        if (cur().kind == Tok::StringLit) {
            auto e = ex(Expr::Kind::String, cur());
            e->text = cur().decoded;
            advance();
            // Adjacent string literals are one ([lex.phases] phase 6, ISO C 5.1.1.2): "a" "b" is "ab".
            while (!atEnd() && cur().kind == Tok::StringLit) {
                e->text += cur().decoded;
                advance();
            }
            if (!atEnd() && cur().kind == Tok::Ident && !cur().leadSpace && cur().text.size() > 1 &&
                cur().text[0] == '_') {
                // "text"_sfx, a user-defined string literal ([lex.ext] p5): operator""_sfx("text", length).
                auto call = ex(Expr::Kind::Call, cur());
                auto fn = ex(Expr::Kind::Name, cur());
                fn->text = "operator\"\"" + cur().text;
                advance();
                auto len = ex(Expr::Kind::Int, toks_[i_ - 1]);
                len->ival = e->text.size();
                len->litUnsigned = true;
                len->litRank = 2;
                call->kids.push_back(std::move(fn));
                call->kids.push_back(std::move(e));
                call->kids.push_back(std::move(len));
                return call;
            }
            return e;
        }
        if (eatP("(")) {
            auto e = parseExpr();
            expectP(")", ")");
            return e;
        }
        if (cur().kind == Tok::Keyword) {
            err("'" + cur().text + "' is not in this subset");
            advance();
            return zeroAt();
        }
        err("expected an expression");
        advance();
        return zeroAt();
    }

};


// Token tests shared by the token-stream passes below (namespace aliases, thread_local, friends).
static bool punct(const Token& t, const char* s) { return t.kind == Tok::Punct && t.text == s; }
static bool word(const Token& t, const char* s) { return t.isName() && t.text == s; }

// Namespace aliases ([namespace.alias]): "namespace fs = std::filesystem;" is taken out, and every
// later "fs ::" (a name followed by '::', not after '.', '->' or '::') becomes "std :: filesystem ::".
// The alias holds from its declaration to the end of the file, wherever it was declared (a
// simplification: block scope is not kept).
// inline namespace N { .. } ([namespace.def] p7): its members are members of the enclosing
// namespace for every lookup, so okay-cc reads them as such. "inline namespace N {" and its '}'
// are dropped, and so is N where it names the namespace in a qualified name (A::N::f is A::f,
// using namespace A::N; is using namespace A;, using namespace N; inside A is dropped). Our own
// choice: the symbols of its members leave N out (g++'s keep it); no other compiler's objects are
// linked with okay-cc's.
// Attributes ([dcl.attr.grammar]): [[noreturn]], [[nodiscard]], [[deprecated("..")]],
// [[fallthrough]] and the rest change no meaning okay-cc gives a program, so the whole
// specifier is dropped here too. Two '[' in a row only ever start one (p7).
static std::vector<Token> dropAttributesAndInlineNamespaces(const std::vector<Token>& in) {
    std::set<std::string> names;
    std::vector<int> drop;  // for each open '{': 1 if it opens an inline namespace
    std::vector<Token> out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (punct(in[i], "[") && i + 1 < in.size() && punct(in[i + 1], "[")) {
            std::size_t k = i + 2;
            int depth = 2;
            for (; k < in.size() && in[k].kind != Tok::End; ++k) {
                if (punct(in[k], "[")) ++depth;
                else if (punct(in[k], "]") && --depth == 0) break;
            }
            if (k < in.size() && depth == 0) {
                i = k;
                continue;
            }
        }
        if (word(in[i], "inline") && i + 3 < in.size() && word(in[i + 1], "namespace") && in[i + 2].kind == Tok::Ident &&
            punct(in[i + 3], "{")) {
            names.insert(in[i + 2].text);
            drop.push_back(1);
            i += 3;
            continue;
        }
        if (punct(in[i], "{")) drop.push_back(0);
        if (punct(in[i], "}") && !drop.empty()) {
            int d = drop.back();
            drop.pop_back();
            if (d) continue;
        }
        if (!names.empty() && in[i].kind == Tok::Ident && names.count(in[i].text)) {
            const bool after = !out.empty() && punct(out.back(), "::");
            const bool before = i + 1 < in.size() && punct(in[i + 1], "::");
            if (before && (after || (!out.empty() && !punct(out.back(), ".") && !punct(out.back(), "->")))) {
                ++i;  // A::N::f, N::f: the N and its '::'
                continue;
            }
            if (after && i + 1 < in.size() && punct(in[i + 1], ";")) {
                out.pop_back();  // using namespace A::N;
                continue;
            }
            if (!out.empty() && word(out.back(), "namespace") && out.size() >= 2 && word(out[out.size() - 2], "using") &&
                i + 1 < in.size() && punct(in[i + 1], ";")) {
                out.pop_back();
                out.pop_back();
                ++i;  // using namespace N; (its own enclosing namespace)
                continue;
            }
        }
        out.push_back(in[i]);
    }
    return out;
}

static std::vector<Token> expandNamespaceAliases(const std::vector<Token>& in) {
    std::map<std::string, std::vector<Token>> alias;
    std::vector<Token> out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (word(in[i], "namespace") && i + 3 < in.size() && in[i + 1].kind == Tok::Ident && punct(in[i + 2], "=")) {
            std::vector<Token> target;
            std::size_t k = i + 3;
            if (k < in.size() && punct(in[k], "::")) ++k;  // namespace a = ::b;
            for (; k < in.size() && !punct(in[k], ";"); ++k) target.push_back(in[k]);
            // the target may itself start with an alias
            if (!target.empty() && alias.count(target[0].text) && target.size() > 1 && punct(target[1], "::")) {
                std::vector<Token> full = alias[target[0].text];
                full.insert(full.end(), target.begin() + 2, target.end());
                target = full;
            } else if (target.size() == 1 && alias.count(target[0].text)) {
                target = alias[target[0].text];
            }
            alias[in[i + 1].text] = target;
            i = k;  // past the ';'
            continue;
        }
        if (!alias.empty() && in[i].kind == Tok::Ident && i + 1 < in.size() && punct(in[i + 1], "::") &&
            alias.count(in[i].text) &&
            !(i > 0 && (punct(in[i - 1], ".") || punct(in[i - 1], "->") || punct(in[i - 1], "::")))) {
            for (const Token& t : alias[in[i].text]) {
                Token c = t;
                c.line = in[i].line;
                c.column = in[i].column;
                c.file = in[i].file;
                out.push_back(c);
            }
            continue;  // the '::' after the alias follows
        }
        // using namespace fs; names the aliased namespace.
        if (!alias.empty() && word(in[i], "namespace") && i > 0 && word(in[i - 1], "using") && i + 1 < in.size() &&
            in[i + 1].kind == Tok::Ident && alias.count(in[i + 1].text) && i + 2 < in.size() && punct(in[i + 2], ";")) {
            out.push_back(in[i]);
            for (const Token& t : alias[in[i + 1].text]) out.push_back(t);
            ++i;
            continue;
        }
        out.push_back(in[i]);
    }
    return out;
}

// thread_local ([basic.stc.thread], [dcl.stc] p4) on the token stream. okay-cc programs have one
// thread, so a thread-local variable has one object, as a static one has: inside a function or a
// class "thread_local" means "static" (block scope: [dcl.stc] p4 makes it static there too); at
// namespace scope, and next to "static" or "extern", the word is dropped.
static std::vector<Token> threadLocalsAsStatic(const std::vector<Token>& in) {
    std::vector<bool> nsBrace;  // for each open '{': is it a namespace's (or extern "C"'s)?
    std::vector<Token> out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        const Token& t = in[i];
        if (punct(t, "{")) {
            bool ns = false;
            for (std::size_t k = i; k-- > 0 && i - k <= 6;) {
                if (word(in[k], "namespace") || (in[k].kind == Tok::StringLit && k > 0 && word(in[k - 1], "extern"))) {
                    ns = true;
                    break;
                }
                if (!(in[k].kind == Tok::Ident || punct(in[k], "::"))) break;
            }
            nsBrace.push_back(ns);
        } else if (punct(t, "}") && !nsBrace.empty()) {
            nsBrace.pop_back();
        }
        if (word(t, "thread_local")) {
            bool nsScope = true;
            for (bool b : nsBrace) nsScope = nsScope && b;
            bool near = (i > 0 && (word(in[i - 1], "static") || word(in[i - 1], "extern"))) ||
                        (i + 1 < in.size() && (word(in[i + 1], "static") || word(in[i + 1], "extern")));
            if (nsScope || near) continue;
            Token st = t;
            st.kind = isKeywordSpelling("static") ? Tok::Keyword : Tok::Ident;
            st.text = "static";
            out.push_back(st);
            continue;
        }
        out.push_back(t);
    }
    return out;
}

// friend declarations ([class.friend]) on the token stream. okay-cc does not enforce access, so a
// friend that only declares (friend class Scene; friend void f(int);) is dropped. A friend function
// defined in a class at namespace scope (friend bool operator==(A a, A b) { .. }, a "hidden
// friend") is a namespace-scope function ([class.friend] p6-p7): it is moved, as an inline
// function, to just after the class's closing "};". (Name lookup differs from the standard's: the
// moved function is found by ordinary lookup too, not only through its arguments' classes.)
static std::vector<Token> hoistFriends(const std::vector<Token>& in) {
    // kinds of open braces: 'n' namespace or extern "C", 'c' class at namespace scope, 'C' a class
    // in a template or another class, 'o' other
    std::vector<char> kinds;
    std::vector<std::vector<Token>> pending;  // per open class: the friend definitions to emit
    std::vector<Token> out;
    out.reserve(in.size());
    bool afterTemplate = false;  // "template <...>" seen before a class head
    for (std::size_t i = 0; i < in.size(); ++i) {
        const Token& t = in[i];
        if (word(t, "template")) afterTemplate = true;
        if (punct(t, ";")) afterTemplate = false;
        if (punct(t, "{")) {
            char k = 'o';
            bool nsScope = true;
            for (char c : kinds) nsScope = nsScope && c == 'n';
            for (std::size_t b = i; b-- > 0 && i - b <= 64;) {
                if (punct(in[b], ";") || punct(in[b], "}") || punct(in[b], "{")) break;
                if (word(in[b], "namespace") || (in[b].kind == Tok::StringLit && b > 0 && word(in[b - 1], "extern"))) {
                    k = 'n';
                    break;
                }
                if ((word(in[b], "class") || word(in[b], "struct")) && !(b > 0 && word(in[b - 1], "enum"))) {
                    k = nsScope && !afterTemplate ? 'c' : 'C';
                    break;
                }
                if (punct(in[b], ")") || punct(in[b], "=")) break;  // a function body or an initializer
            }
            kinds.push_back(k);
            pending.emplace_back();
            afterTemplate = false;
            out.push_back(t);
            continue;
        }
        if (punct(t, "}") && !kinds.empty()) {
            char k = kinds.back();
            std::vector<Token> moved = std::move(pending.back());
            kinds.pop_back();
            pending.pop_back();
            out.push_back(t);
            if (k == 'c' && !moved.empty()) {
                if (i + 1 < in.size() && punct(in[i + 1], ";")) out.push_back(in[++i]);
                for (const Token& m : moved) out.push_back(m);
            }
            continue;
        }
        if (word(t, "friend") && !kinds.empty() && (kinds.back() == 'c' || kinds.back() == 'C')) {
            // the declaration: up to ';' at depth 0, or a body's closing '}'
            std::size_t j = i + 1;
            int paren = 0;
            bool body = false;
            for (; j < in.size() && in[j].kind != Tok::End; ++j) {
                if (punct(in[j], "(")) ++paren;
                else if (punct(in[j], ")")) --paren;
                else if (paren == 0 && punct(in[j], ";")) break;
                else if (paren == 0 && punct(in[j], "{")) {
                    body = true;
                    int depth = 0;
                    for (; j < in.size(); ++j) {
                        if (punct(in[j], "{")) ++depth;
                        else if (punct(in[j], "}") && --depth == 0) break;
                    }
                    break;
                }
            }
            if (j >= in.size()) {
                out.push_back(t);
                continue;
            }
            if (!body) {
                i = j;  // dropped with its ';'
                continue;
            }
            if (kinds.back() != 'c') {
                out.push_back(t);  // (in a template or a nested class: left as it is)
                continue;
            }
            Token inl = t;
            inl.text = "inline";
            inl.kind = isKeywordSpelling("inline") ? Tok::Keyword : Tok::Ident;
            pending.back().push_back(inl);
            for (std::size_t k = i + 1; k <= j; ++k) pending.back().push_back(in[k]);
            i = j;
            continue;
        }
        out.push_back(t);
    }
    return out;
}

// Parse and check. A function template called without explicit arguments is only known after the
// checker has deduced them, so the file is parsed again with the new instance requests until none
// are new (at most eight rounds).
Tu frontEnd(const std::vector<Token>& raw, Diagnostics& diags) {
    instRequests().clear();
    const std::vector<Token> toks = expandPacks(hoistFriends(threadLocalsAsStatic(expandNamespaceAliases(dropAttributesAndInlineNamespaces(raw)))));
    Tu result;
    for (int round = 0; round < 8; ++round) {
        Diagnostics local;
        instDirty() = false;
        specChecks().clear();
        const std::clock_t t0 = std::clock();
        Parser parser(toks, local);
        Tu tu = parser.parseTu();
        const std::clock_t t1 = std::clock();
        if (local.ok()) {
            // Function template instances the checker deduces are made in this round when they
            // can be (Parser::instantiate), so a file needs another round only for the rest.
            if (!std::getenv("OKAYCC_NO_ONDEMAND"))
                instantiateNow() = [&parser](Tu& t, const InstReq& r) { return parser.instantiate(t, r); };
            typecheckTu(tu, local);
            instantiateNow() = nullptr;
        }
        if (std::getenv("OKAYCC_ROUNDS"))
            std::fprintf(stderr, "round %d: %zu requests, dirty %d, %zu diags, parse %.2f s, check %.2f s\n", round,
                         instRequests().size(), instDirty() ? 1 : 0, local.items().size(),
                         double(t1 - t0) / CLOCKS_PER_SEC, double(std::clock() - t1) / CLOCKS_PER_SEC);
        if (instDirty() && round < 7) continue;
        for (const Diag& d : local.items()) {
            if (d.warning) diags.warning(d.file, d.line, d.column, d.message);
            else diags.error(d.file, d.line, d.column, d.message);
        }
        result = std::move(tu);
        break;
    }
    return result;
}

}  // namespace compiler

using namespace compiler;

std::string astFromRaw(std::vector<Token> raw, Diagnostics& diags) {
    std::string file = "t.c";
    int line = 1;
    int col = 1;
    if (!raw.empty()) {
        line = raw.back().line;
        col = raw.back().column;
        if (!raw.back().file.empty()) file = raw.back().file;
    }
    std::vector<Token> toks;
    for (Token& t : raw) {
        if (!t.file.empty()) file = t.file;
        if (t.kind != Tok::Newline) toks.push_back(std::move(t));
    }
    Token end;
    end.kind = Tok::End;
    end.file = file;
    end.line = line;
    end.column = col;
    toks.push_back(std::move(end));
    Parser parser(std::move(toks), diags);
    Tu tu = parser.parseTu();
    return printTu(tu);
}

std::string astOf(const std::string& source, Diagnostics& diags) {
    MemoryFiles files;
    files.add("t.c", source);
    return astFromRaw(preprocessTokens(files, "t.c", {}, diags), diags);
}

std::string astOfFile(FileStore& files, const std::string& path, const std::vector<std::string>& includePaths,
                      Diagnostics& diags) {
    return astFromRaw(preprocessTokens(files, path, includePaths, diags), diags);
}

void checkSource(const std::string& source, Diagnostics& diags, const std::string& fileName,
                 const std::vector<std::string>& includes) {
    MemoryFiles files;
    files.add(fileName, source);
    std::vector<Token> raw = preprocessTokens(files, fileName, includes, diags);
    if (!diags.ok()) return;
    std::string file = "t.c";
    int line = 1;
    int col = 1;
    if (!raw.empty()) {
        line = raw.back().line;
        col = raw.back().column;
        if (!raw.back().file.empty()) file = raw.back().file;
    }
    std::vector<Token> toks;
    for (Token& t : raw) {
        if (t.kind != Tok::Newline) toks.push_back(std::move(t));
    }
    Token end;
    end.kind = Tok::End;
    end.file = std::move(file);
    end.line = line;
    end.column = col;
    toks.push_back(std::move(end));
    Tu tu = frontEnd(toks, diags);
    (void)tu;
}

}  // namespace okaycc
