#include "parser.hpp"

// The syntax tree and what parser.cpp shares with check.cpp and lower.cpp (docs/engine/modularization_plan.md, batch T1).
#include "ast.hpp"

#include "files.hpp"
#include "ir.hpp"
#include "preproc.hpp"
#include "token.hpp"

#include <cstdio>
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
    enum class Kind { Ptr, Arr, Func, Ref } kind = Kind::Ptr;
    std::unique_ptr<Expr> bound;
    bool unsized = false;
    bool variadic = false;
    std::vector<Param> params;
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
    Parser(std::vector<Token> toks, Diagnostics& diags) : toks_(std::move(toks)), diags_(diags) {}

    Tu parseTu() {
        Tu tu;
        if (tscopes_.empty()) tscopes_.emplace_back();
        eh_ = forceExceptions();
        for (const Token& t : toks_)
            if (t.kind == Tok::Keyword && (t.text == "try" || t.text == "throw")) eh_ = true;
        while (!atEnd()) {
            std::size_t before = i_;
            if (isKw("namespace")) parseNamespace(tu);
            else if (isKw("template")) parseTemplate();
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
        ehActive() = eh_;
        return tu;
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
        for (char ch : (toks_.empty() ? std::string("t") : toks_[0].file)) sfx += std::isalnum(static_cast<unsigned char>(ch)) ? ch : '_';
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
                    auto g = std::make_unique<Stmt>();
                    g->kind = Stmt::Kind::If;
                    g->line = p.line;
                    g->col = p.col;
                    g->file = p.file;
                    g->expr = eName(globalObjs_[i].guard, p);
                    g->thenS = std::move(st);
                    st = std::move(g);
                }
                body->stmts.push_back(std::move(st));
            }
            globalObjs_.clear();
            mk("__okay_fini_" + sfx, std::move(body));
        }
    }

private:
    std::vector<Token> toks_;
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
    std::set<std::string> implicitTags_;
    std::vector<std::unique_ptr<Decl>> extras_;

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
    bool isP(const Token& t, const char* s) const {
        if (t.kind != Tok::Punct) return false;
        if (t.text == s) return true;
        if (std::string(s) == "[" && t.text == "<:") return true;
        if (std::string(s) == "]" && t.text == ":>") return true;
        if (std::string(s) == "{" && t.text == "<%") return true;
        if (std::string(s) == "}" && t.text == "%>") return true;
        return false;
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
    bool isTypeKw(const Token& t) const {
        if (isKwTok(t, "void") || isKwTok(t, "char") || isKwTok(t, "short") || isKwTok(t, "int") ||
            isKwTok(t, "long") || isKwTok(t, "struct") || isKwTok(t, "class") || isKwTok(t, "union") || isKwTok(t, "enum") ||
            isKwTok(t, "extern") || isKwTok(t, "float") || isKwTok(t, "double") ||
            isKwTok(t, "const") || isKwTok(t, "volatile") || isKwTok(t, "static") ||
            isKwTok(t, "unsigned") || isKwTok(t, "signed") || isKwTok(t, "bool") ||
            isKwTok(t, "typedef"))
            return true;
        if (t.kind == Tok::Ident && ctmpl_.count(t.text)) return true;
        if (t.kind != Tok::Ident || findTypedef(t.text) == nullptr) return false;
        if (!implicitTags_.count(t.text)) return true;
        // A struct tag used as a plain type name (C++). Where C would see a variable of the same
        // name, only treat it as a type when what follows can continue a declaration or a type.
        const Token* base = toks_.data();
        if (&t < base || &t >= base + toks_.size()) return true;
        std::size_t idx = static_cast<std::size_t>(&t - base);
        if (idx + 1 >= toks_.size()) return true;
        const Token& n = toks_[idx + 1];
        if (n.kind == Tok::Ident) return true;
        return n.kind == Tok::Punct && (n.text == "*" || n.text == "&" || n.text == ")" || n.text == "::" || n.text == ">");
    }
    bool isUnsupported() const {
        if (atEnd() || cur().kind != Tok::Keyword) return false;
        return !isTypeKw(cur()) && !isKw("if") && !isKw("else") && !isKw("while") && !isKw("for") && !isKw("do") && !isKw("try") && !isKw("throw") &&
               !isKw("return") && !isKw("break") && !isKw("continue") && !isKw("switch") && !isKw("goto") &&
               !isKw("new") && !isKw("delete");
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

    std::unique_ptr<Type> parseBase() {
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
            if (eatKw("const") || eatKw("volatile")) {
                qualOnly = true;
                continue;
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
        if (!atEnd() && cur().kind == Tok::Ident && ctmpl_.count(cur().text) &&
            (!findTypedef(cur().text) || nextIs("<")))
            return instantiateClass(cur().text);
        if (!atEnd() && cur().kind == Tok::Ident) {
            const Type* td = findTypedef(cur().text);
            if (td) {
                advance();
                return cloneType(*td);
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

    std::unique_ptr<Type> parseEnum() {
        advance();
        if (!atEnd() && cur().kind == Tok::Ident) advance();
        if (!eatP("{")) return ty(Type::Kind::Int);
        std::int64_t next = 0;
        while (!atEnd() && !isP("}")) {
            if (cur().kind != Tok::Ident) {
                err("expected an enumerator");
                break;
            }
            std::string name = cur().text;
            advance();
            if (eatP("=")) {
                auto e = parseAssign();
                if (e && e->kind == Expr::Kind::Int) next = static_cast<std::int64_t>(e->ival);
                else if (e && e->kind == Expr::Kind::Unary && e->text == "-" && !e->kids.empty() && e->kids[0] &&
                         e->kids[0]->kind == Expr::Kind::Int)
                    next = -static_cast<std::int64_t>(e->kids[0]->ival);
                else err("enumerator is not a constant");
            }
            enums_[name] = next;
            ++next;
            if (!eatP(",")) break;
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
        std::unique_ptr<Stmt> body;
        bool hasBody = false;
        Pos pos;
    };
    struct ClassCtx {
        std::string tag;
        Type* t = nullptr;
        std::vector<CtorDef> ctors;
        bool dtorDeclared = false;
        bool dtorVirtual = false;
        std::unique_ptr<Stmt> dtorBody;
        Pos dtorPos;
        bool dtorInline = false;  // a body for the complete-object destructor is emitted at class end
        bool cxx = false;
    };
    struct DVar {
        std::string name;
        std::string tag;
        std::uint64_t count = 0;  // above 0: an array of that many objects
        std::string guard;        // a static local: destroyed only if this flag is set
        int kind = 0;             // 0 object, 1 try frame (popped), 2 handler (ended)
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
            c.pure = p.pure;
            if (p.type) c.type = cloneType(*p.type);
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
    std::unique_ptr<Expr> dtorCall(const std::string& tag, std::unique_ptr<Expr> self, const Pos& p) {
        const Param* d = special(classOf(tag), "<dtor>");
        if (!d) return nullptr;
        std::vector<std::unique_ptr<Expr>> args;
        args.push_back(std::move(self));
        return eCall(eName(d->mangled, p), std::move(args), p);
    }
    std::unique_ptr<Stmt> vptrStore(const std::string& tag, const Pos& p) {
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
        sum->kids.push_back(eInt(2, p));
        return sExpr(eAssign(eArrow(eName("this", p), "__vptr", p), std::move(sum), p), p);
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

    bool sameParamTypes(const Type& a, const Type& b) {
        if (a.params.size() != b.params.size()) return false;
        for (std::size_t i = 0; i < a.params.size(); ++i) {
            if (!a.params[i].type || !b.params[i].type || !sameType(*a.params[i].type, *b.params[i].type))
                return false;
        }
        return true;
    }

    std::vector<std::pair<std::string, const Type*>> ownFields(const Type& T) {
        std::vector<std::pair<std::string, const Type*>> out;
        const Type* B = T.base.empty() ? nullptr : classOf(T.base);
        for (const Param& p : T.params) {
            if (!isDataField(p)) continue;
            bool inherited = false;
            if (B)
                for (const Param& bp : B->params)
                    if (isDataField(bp) && bp.name == p.name) inherited = true;
            if (!inherited) out.emplace_back(p.name, p.type.get());
        }
        return out;
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
            ft->params.push_back(std::move(c));
        }
        decl->type = std::move(ft);
        return decl;
    }

    // A constructor body: base constructor, vtable pointer, members in declaration order, then the
    // user's own statements (ISO C++ [class.base.init]).
    std::unique_ptr<Decl> buildCtor(const std::string& tag, CtorDef& d) {
        Type* T = classOf(tag);
        if (!T) return nullptr;
        Pos p = d.pos;
        auto fn = fnTypeOf(d.params);
        std::string mangled = mangleSpecial(tag, "C1", *fn);
        auto decl = newMethodDecl(tag, mangled, *fn, d.params, p);
        auto body = sBlock(p);
        auto take = [&](const std::string& n, const std::string& alt) -> MemInit* {
            for (MemInit& m : d.inits)
                if (m.name == n || (!alt.empty() && m.name == alt)) return &m;
            return nullptr;
        };
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
            MemInit* mi = take(f.first, "");
            Type* ft = f.second && f.second->kind == Type::Kind::Struct && !f.second->tag.empty()
                           ? classOf(f.second->tag)
                           : nullptr;
            if (ft && ft->needCtor) {
                std::vector<std::unique_ptr<Expr>> args;
                if (mi) args = std::move(mi->args);
                if (args.size() == 1) {
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
            } else if (mi && mi->args.size() == 1) {
                body->stmts.push_back(
                    sExpr(eAssign(eArrow(eName("this", p), f.first, p), std::move(mi->args[0]), p), p));
            }
        }
        for (MemInit& m : d.inits) {
            bool ok = (B && (m.name == shortName(T->base) || m.name == T->base));
            for (auto& f : ownFields(*T))
                if (f.first == m.name) ok = true;
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

    void finishClass(ClassCtx& c) {
        Type& T = *c.t;
        Type* B = T.base.empty() ? nullptr : classOf(T.base);
        bool needCtor = !c.ctors.empty() || T.hasVptr || (B && B->needCtor);
        bool needDtor = c.dtorDeclared || (B && B->needDtor);
        for (auto& f : ownFields(T)) {
            if (f.second && f.second->kind == Type::Kind::Struct && !f.second->tag.empty()) {
                Type* m = classOf(f.second->tag);
                if (m && m->needCtor) needCtor = true;
                if (m && m->needDtor) needDtor = true;
                if (m && (m->nonpod || m->needCtor || m->needDtor)) T.nonpod = true;
            }
        }
        T.needCtor = needCtor;
        T.needDtor = needDtor;
        if (!c.ctors.empty() || c.dtorDeclared || T.hasVptr || needCtor || needDtor) T.nonpod = true;
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
        // The implicit copy constructor: a class with no copy constructor of its own whose base or
        // member has one copies the base and each member with them. (Not made when a part could not
        // be copied that way: an array member, a base without a copy constructor.)
        {
            auto hasCopy = [&](const Type* cls) {
                if (!cls) return false;
                for (const Param& q : cls->params) {
                    if (q.name != "<ctor>" || !q.type || q.type->params.size() != 1) continue;
                    const Type* a = q.type->params[0].type.get();
                    if (a && a->kind == Type::Kind::Reference && a->inner && a->inner->kind == Type::Kind::Struct &&
                        a->inner->tag == cls->tag)
                        return true;
                }
                return false;
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
                if (f.second->kind == Type::Kind::Array) ok = false;
                if (f.second->kind == Type::Kind::Struct && !f.second->tag.empty()) {
                    Type* m = classOf(f.second->tag);
                    if (m && hasCopy(m)) parts = true;
                    else if (m && m->needCtor) ok = false;
                }
            }
            if (!userCopy && parts && ok) {
                CtorDef d;
                d.hasBody = true;
                d.pos = p;
                Param op;
                op.name = "__o";
                auto refT = ty(Type::Kind::Reference);
                refT->inner = ty(Type::Kind::Struct);
                refT->inner->tag = c.tag;
                op.type = std::move(refT);
                d.params.push_back(std::move(op));
                if (B) {
                    MemInit mi;
                    mi.name = shortName(T.base);
                    mi.args.push_back(eName("__o", p));
                    d.inits.push_back(std::move(mi));
                }
                for (auto& f : ownFields(T)) {
                    MemInit mi;
                    mi.name = f.first;
                    auto dot = std::make_unique<Expr>();
                    dot->kind = Expr::Kind::Member;
                    dot->text = ".";
                    dot->line = p.line;
                    dot->col = p.col;
                    dot->file = p.file;
                    dot->kids.push_back(eName("__o", p));
                    dot->kids.push_back(eName(f.first, p));
                    mi.args.push_back(std::move(dot));
                    d.inits.push_back(std::move(mi));
                }
                Param cp;
                cp.name = "<ctor>";
                auto fn = fnTypeOf(d.params);
                cp.mangled = mangleSpecial(c.tag, "C1", *fn);
                cp.type = std::move(fn);
                T.params.push_back(std::move(cp));
                c.ctors.push_back(std::move(d));
            }
        }
        bool baseVirt = B && special(B, "<dtor>") && special(B, "<dtor>")->vslot >= 0;
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
            vd->vt.push_back("");
            vd->vt.push_back("");
            for (const std::string& sname : T->vslots) vd->vt.push_back(sname);
            extras_.push_back(std::move(vd));
        }
    }

    std::unique_ptr<Type> parseStructType() {
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
            if (tag.find("::") == std::string::npos) tag = qualify(tag);
        }
        std::string tplShort;
        if (!instOverrideName_.empty() && (tag == instOverrideName_ || shortName(tag) == instOverrideName_)) {
            tplShort = instOverrideName_;
            tag = instOverrideTag_;
            instOverrideName_.clear();
        }
        std::string baseTag;
        if (!tag.empty() && !isUnion && isP(":")) {
            advance();
            while (isKw("public") || isKw("private") || isKw("protected") || isKw("virtual")) advance();
            std::string bn;
            if (!atEnd() && cur().kind == Tok::Ident && ctmpl_.count(cur().text) && nextIs("<")) {
                auto bt = instantiateClass(cur().text);
                if (bt && !bt->tag.empty() && classOf(bt->tag)) baseTag = bt->tag;
                else err("unknown base class");
            } else if (!atEnd() && cur().kind == Tok::Ident) {
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
            if (!baseTag.empty()) {
            } else if (classOf(qualify(bn))) baseTag = qualify(bn);
            else if (classOf(bn)) baseTag = bn;
            else if (td && !td->tag.empty() && classOf(td->tag)) baseTag = td->tag;
            else err("unknown base class '" + bn + "'");
            if (isP(",")) err("multiple inheritance is not supported");
        }
        if (!eatP("{")) {
            if (tag.empty()) {
                err("expected a struct name");
                return ty(Type::Kind::Int);
            }
            auto it = structs_.find(tag);
            if (it == structs_.end() || !it->second) {
                err("unknown struct '" + tag + "'");
                return ty(Type::Kind::Int);
            }
            return cloneType(*it->second);
        }
        auto t = ty(isUnion ? Type::Kind::Union : Type::Kind::Struct);
        t->tag = tag;
        bool completing = false;
        if (!tag.empty() && !structs_.count(tag)) {
            auto inc = ty(isUnion ? Type::Kind::Union : Type::Kind::Struct);
            inc->tag = tag;
            structs_[tag] = std::move(inc);
            completing = true;
        }
        ClassCtx cx;
        cx.tag = tag;
        cx.t = t.get();
        cx.dtorPos.line = at.line;
        cx.dtorPos.col = at.column;
        cx.dtorPos.file = at.file;
        bool classy = !tag.empty() && !isUnion;
        std::string shortTag = tplShort.empty() ? shortName(tag) : tplShort;
        bool pub = !isClass;
        if (classy) cx.cxx = true;
        if (!baseTag.empty()) {
            Type* B = classOf(baseTag);
            for (const Param& p : B->params) {
                if (isSpecialName(p.name)) continue;
                Param c;
                c.name = p.name;
                c.bits = p.bits;
                c.mangled = p.mangled;
                c.vslot = p.vslot;
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
        if (cx.cxx) {
            auto ph = ty(Type::Kind::Struct);
            ph->tag = tag;
            addTypedef(shortTag, std::move(ph));
            implicitTags_.insert(shortTag);
        }
        while (!atEnd() && !isP("}")) {
            if ((isKw("public") || isKw("private") || isKw("protected")) && nextIs(":")) {
                pub = isKw("public");
                advance();
                advance();
                continue;
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
                bool baseVirt = !baseTag.empty() && special(classOf(baseTag), "<dtor>") &&
                                special(classOf(baseTag), "<dtor>")->vslot >= 0;
                registerDtor(cx, isVirtual || baseVirt, dt);
                if (isP("{")) {
                    auto vt = ty(Type::Kind::Void);
                    cx.dtorBody = parseBody(vt.get());
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
                parseInitList(cd);
                Param cp;
                cp.name = "<ctor>";
                auto fn = fnTypeOf(cd.params);
                cp.mangled = mangleSpecial(tag, "C1", *fn);
                cp.type = std::move(fn);
                t->params.push_back(std::move(cp));
                if (isP("{")) {
                    auto vt = ty(Type::Kind::Void);
                    cd.body = parseBody(vt.get());
                    cd.hasBody = true;
                } else if (eatP("=")) {
                    if (isKw("default")) {
                        cd.hasBody = true;
                        cd.body = sBlock(cd.pos);
                    }
                    advance();
                    expectP(";", ";");
                } else {
                    expectP(";", ";");
                }
                cx.ctors.push_back(std::move(cd));
                continue;
            }
            Parsed d;
            std::unique_ptr<Type> fieldType;
            if (!atEnd() && cur().kind == Tok::Ident && cur().text == "operator" && i_ + 1 < toks_.size() &&
                isTypeKw(toks_[i_ + 1])) {
                // A conversion function: operator int() const { ... } (ISO/IEC 14882 [class.conv.fct]).
                d.line = cur().line;
                d.col = cur().column;
                d.file = cur().file;
                advance();
                auto target = parseBase();
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
                d = parseDeclarator(true);
                fieldType = apply(std::move(base), d.mods);
            }
            if (fieldType && fieldType->kind == Type::Kind::Function && !tag.empty() && !isUnion) {
                cx.cxx = true;
                if (cx.cxx && !classy) classy = true;
                bool pure = false;
                while (isKw("const") || isKw("override") || isKw("final") || isKw("noexcept")) advance();
                if (isP("=")) {
                    advance();
                    if (!atEnd() && cur().kind == Tok::Int && cur().uval == 0) pure = true;
                    advance();
                }
                Param field;
                field.name = d.name;
                field.mangled = mangleFunc(tag + "::" + d.name, *fieldType);
                field.type = cloneType(*fieldType);
                int slot = -1;
                bool virt = isVirtual;
                for (std::size_t j = 0; j < t->params.size(); ++j) {
                    const Param& ex = t->params[j];
                    if (ex.name != d.name || ex.mangled.empty() || isSpecialName(ex.name) || !ex.type) continue;
                    if (!sameParamTypes(*ex.type, *fieldType)) continue;
                    if (ex.vslot >= 0) {
                        slot = ex.vslot;
                        virt = true;
                    }
                    t->params.erase(t->params.begin() + static_cast<std::ptrdiff_t>(j));
                    break;
                }
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
                    decl->body = parseBody(fieldType->inner.get());
                    extras_.push_back(std::move(decl));
                } else {
                    expectP(";", ";");
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
            t->params.push_back(std::move(field));
            expectP(";", ";");
        }
        expectP("}", "}");
        if (!tag.empty()) {
            auto it = structs_.find(tag);
            bool already = it != structs_.end() && it->second && !it->second->params.empty() && !completing;
            if (already) {
                diags_.error(at.file.empty() ? "t.c" : at.file, at.line, at.column, "redefined struct '" + tag + "'");
            } else {
                if (cx.cxx) finishClass(cx);
                structs_[tag] = cloneType(*t);
                canonStructs()[tag] = cloneType(*t);
                if (cx.cxx) {
                    addTypedef(shortTag, cloneType(*t));
                    emitClassDecls(cx);
                }
            }
        }
        return t;
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
        auto b = parseCompound();
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
            break;
        }
        Parsed d = parseDirect(needName);
        for (int n = static_cast<int>(prefix.size()) - 1; n >= 0; --n) {
            Mod m;
            m.kind = prefix[static_cast<std::size_t>(n)];
            d.mods.push_back(std::move(m));
        }
        return d;
    }

    Parsed parseDirect(bool needName) {
        Parsed d;
        if (eatP("(")) {
            d = parseDeclarator(needName);
            expectP(")", ")");
        } else if (!atEnd() && cur().kind == Tok::Ident) {
            d.name = cur().text;
            d.line = cur().line;
            d.col = cur().column;
            d.file = cur().file;
            advance();
            if (d.name == "operator" && !atEnd() && cur().kind == Tok::Punct) {
                // operator+, operator==, operator[], operator() and the other overloadable symbols.
                std::string op = cur().text;
                advance();
                if (op == "(" && eatP(")")) op = "()";
                else if (op == "[" && eatP("]")) op = "[]";
                d.name += op;
            }
            while (eatP("::")) {
                if (atEnd() || cur().kind != Tok::Ident) {
                    err("expected a name");
                    break;
                }
                d.name += "::";
                d.name += cur().text;
                advance();
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
                if (!isP("]")) m.bound = parseAssign();
                else m.unsized = true;
                expectP("]", "]");
                d.mods.push_back(std::move(m));
                continue;
            }
            if (eatP("(")) {
                Mod m;
                m.kind = Mod::Kind::Func;
                m.variadic = parseParams(m.params);
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
        return variadic;
    }

    std::unique_ptr<Type> apply(std::unique_ptr<Type> base, std::vector<Mod>& mods) {
        for (int n = static_cast<int>(mods.size()) - 1; n >= 0; --n) {
            auto t = std::make_unique<Type>();
            Mod& m = mods[static_cast<std::size_t>(n)];
            if (m.kind == Mod::Kind::Ptr) {
                t->kind = Type::Kind::Pointer;
                t->inner = std::move(base);
            } else if (m.kind == Mod::Kind::Ref) {
                t->kind = Type::Kind::Reference;
                t->inner = std::move(base);
            } else if (m.kind == Mod::Kind::Arr) {
                t->kind = Type::Kind::Array;
                t->inner = std::move(base);
                t->bound = std::move(m.bound);
                t->unsized = m.unsized || !t->bound;
            } else {
                t->kind = Type::Kind::Function;
                t->inner = std::move(base);
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
        decl->name = qualify(decl->name);
        if (!instFnName_.empty() && decl->function) {
            decl->name = instFnName_;
            instFnName_.clear();
            return decl;
        }
        if (decl->name.find("::") != std::string::npos && decl->type) {
            if (decl->function) decl->asmName = mangleFunc(decl->name, *decl->type);
            else decl->asmName = mangleData(decl->name);
        }
        return decl;
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

    void flushExtras(Tu& tu) {
        if (!preStmts_.empty()) {
            err("'new' with a constructor or an initial value is only supported inside a function");
            preStmts_.clear();
        }
        flushBefore(tu);
        for (auto& d : extras_) tu.decls.push_back(std::move(d));
        extras_.clear();
    }
    // Globals made for static locals go in before the function that uses them.
    std::vector<std::unique_ptr<Decl>> beforeDecls_;
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
    };
    std::map<std::string, TDecl> ftmpl_;
    std::map<std::string, TDecl> ctmpl_;
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
        err("template argument is not a constant");
        return 0;
    }

    TArg parseOneArg(const TParam& tp) {
        TArg a;
        if (tp.isType) {
            auto b = parseBase();
            while (eatP("*")) {
                auto ptr = ty(Type::Kind::Pointer);
                ptr->inner = std::move(b);
                b = std::move(ptr);
            }
            a.type = std::move(b);
        } else {
            auto e = parseAdd();
            a.isValue = true;
            a.value = constOf(*e);
        }
        return a;
    }

    // At '<': read the template arguments up to the closing '>', then fill in defaults.
    void parseTemplateArgs(const TDecl& td, std::vector<TArg>& out, bool partial = false) {
        advance();
        std::size_t idx = 0;
        while (!atEnd() && !isAngleClose()) {
            if (idx >= td.params.size()) {
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
            std::size_t saved = i_;
            i_ = td.params[idx].defStart;
            out.push_back(parseOneArg(td.params[idx]));
            i_ = saved;
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

    void parseTemplate() {
        advance();
        if (!eatP("<")) {
            err("expected '<' after 'template'");
            syncSemi();
            return;
        }
        TDecl td;
        while (!atEnd() && !isP(">")) {
            TParam tp;
            if (isKw("typename") || isKw("class")) {
                tp.isType = true;
                advance();
            } else {
                tp.isType = false;
                while (!atEnd() && cur().kind != Tok::Ident && !isP(",") && !isP(">")) advance();
            }
            if (!atEnd() && cur().kind == Tok::Ident) {
                tp.name = cur().text;
                advance();
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
                    } else if (isP(",") && depth == 0) break;
                    advance();
                }
            }
            td.params.push_back(tp);
            if (!eatP(",")) break;
        }
        expectP(">", ">");
        td.start = i_;
        td.end = skipDecl(i_);
        if (isKw("class") || isKw("struct")) {
            if (i_ + 1 < toks_.size() && toks_[i_ + 1].kind == Tok::Ident) ctmpl_[toks_[i_ + 1].text] = td;
            else err("expected a class template name");
            i_ = td.end;
            return;
        }
        std::string name;
        int paren = 0;
        for (std::size_t k = td.start; k < td.end; ++k) {
            if (isP(toks_[k], "(")) {
                if (paren == 0 && k > td.start && toks_[k - 1].kind == Tok::Ident) {
                    name = toks_[k - 1].text;
                    break;
                }
                ++paren;
            }
        }
        if (name.empty()) {
            err("expected a function template");
            i_ = td.end;
            return;
        }
        // The prototype, with each type parameter standing for itself, is what deduction matches.
        TemplateSig sig;
        std::vector<EnumSave> saved;
        tscopes_.emplace_back();
        for (const TParam& tp : td.params) {
            sig.names.push_back(tp.name);
            sig.isType.push_back(tp.isType);
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
        i_ = td.start;
        auto base = parseBase();
        Parsed d = parseDeclarator(true);
        sig.fn = apply(std::move(base), d.mods);
        for (const EnumSave& es : saved) {
            if (es.had) enums_[es.name] = es.old;
            else enums_.erase(es.name);
        }
        tscopes_.pop_back();
        i_ = td.end;
        funcTemplateSigs()[name] = std::move(sig);
        ftmpl_[name] = td;
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
        if (instInProgress_.count(tag)) {
            // The class names itself (Node<T> *next inside Node<T>): the tag stands for it.
            auto self = ty(Type::Kind::Struct);
            self->tag = tag;
            return self;
        }
        if (!structs_.count(tag) && args.size() == td.params.size()) {
            instInProgress_.insert(tag);
            std::size_t saved = i_;
            auto savedPre = std::move(preStmts_);
            preStmts_.clear();
            bool savedHalf = halfGt_;
            halfGt_ = false;
            auto es = bindParams(td, args);
            instOverrideName_ = name;
            instOverrideTag_ = tag;
            i_ = td.start;
            parseBase();
            instOverrideName_.clear();
            unbindParams(es);
            i_ = saved;
            preStmts_ = std::move(savedPre);
            halfGt_ = savedHalf;
            instInProgress_.erase(tag);
        }
        auto it = structs_.find(tag);
        if (it == structs_.end() || !it->second) return ty(Type::Kind::Int);
        return cloneType(*it->second);
    }

    void replayFunction(Tu& tu, const InstReq& r) {
        auto it = ftmpl_.find(r.tmpl);
        if (it == ftmpl_.end()) {
            err("unknown function template '" + r.tmpl + "'");
            return;
        }
        TDecl td = it->second;
        std::size_t saved = i_;
        auto es = bindParams(td, r.args);
        instFnName_ = r.name;
        i_ = td.start;
        parseExternal(tu);
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
        while (!work_.empty() && !stop_) {
            InstReq r = std::move(work_.back());
            work_.pop_back();
            if (!instDone_.insert(r.name).second) continue;
            replayFunction(tu, r);
        }
    }

    // extern "C" declaration  /  extern "C" { declarations }  (ISO C++ [dcl.link]). C linkage means
    // the name is not mangled, which is already how plain names are emitted.
    bool parseLinkage(Tu& tu) {
        if (!isKw("extern") || i_ + 1 >= toks_.size() || toks_[i_ + 1].kind != Tok::StringLit) return false;
        advance();
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

    void parseNamespace(Tu& tu) {
        advance();
        if (atEnd() || cur().kind != Tok::Ident) {
            err("expected a namespace name");
            return;
        }
        ns_.push_back(cur().text);
        advance();
        expectP("{", "{");
        while (!atEnd() && !isP("}")) {
            std::size_t before = i_;
            if (isKw("namespace")) parseNamespace(tu);
            else if (isKw("template")) parseTemplate();
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
        if (!ns_.empty()) ns_.pop_back();
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
            } else {
                err("expected '(' after a member initializer");
                break;
            }
            cd.inits.push_back(std::move(mi));
            if (!eatP(",")) break;
        }
    }

    // "S::S(...) : init { }" and "S::~S() { }" written outside the class.
    bool tryOutOfClassSpecial() {
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
            CtorDef cd;
            cd.pos = pos;
            parseParams(cd.params);
            parseInitList(cd);
            auto fn = fnTypeOf(cd.params);
            std::string mangled = mangleSpecial(tag, "C1", *fn);
            bool declared = false;
            for (const Param& p : T->params)
                if (p.name == "<ctor>" && p.mangled == mangled) declared = true;
            if (!declared) err("no constructor with these parameters is declared in the class");
            auto vt = ty(Type::Kind::Void);
            if (isP("{")) {
                cd.body = parseBody(vt.get());
                cd.hasBody = true;
                auto decl = buildCtor(tag, cd);
                if (decl) extras_.push_back(std::move(decl));
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
        if (!special(T, "<dtor>")) err("the class does not declare a destructor");
        auto vt = ty(Type::Kind::Void);
        if (isP("{")) {
            auto body = parseBody(vt.get());
            auto decl = buildDtor(tag, std::move(body), pos, false);
            if (decl) extras_.push_back(std::move(decl));
        } else {
            err("expected a destructor body");
        }
        return true;
    }

    void parseExternal(Tu& tu) {
        Guard nest(*this);
        if (!nest) return;
        if (tryOutOfClassSpecial()) {
            flushExtras(tu);
            return;
        }
        bool ext = eatKw("extern");
        bool st = eatKw("static");
        bool td = eatKw("typedef");
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
                auto decl = makeDecl(d, std::move(type), std::move(init));
                if (ext && !decl->init) decl->isExtern = true;
                decl->isStatic = st;
                tu.decls.push_back(std::move(decl));
            }
        };
        // A global object of a class with a constructor or destructor: "G g(args);" or "G g;".
        // Its constructor runs at the start of main and its destructor when main returns
        // (only for a main defined later in this file, see the plan).
        if (!td && !ext && base && base->kind == Type::Kind::Struct && !base->tag.empty() && classOf(base->tag) &&
            (classOf(base->tag)->needCtor || classOf(base->tag)->needDtor) && !atEnd() &&
            cur().kind == Tok::Ident &&
            (nextIs(";") || (nextIs("(") && i_ + 2 < toks_.size() && !isP(toks_[i_ + 2], ")") &&
                             !isTypeKw(toks_[i_ + 2])))) {
            Parsed gd;
            gd.name = cur().text;
            gd.line = cur().line;
            gd.col = cur().column;
            gd.file = cur().file;
            advance();
            std::vector<std::unique_ptr<Expr>> args;
            if (eatP("(")) {
                if (!eatP(")")) {
                    args.push_back(parseAssign());
                    while (eatP(",")) args.push_back(parseAssign());
                    expectP(")", ")");
                }
            }
            expectP(";", ";");
            Pos gp{gd.line, gd.col, gd.file};
            std::string tag = base->tag;
            auto decl = makeDecl(gd, cloneType(*base), nullptr);
            decl->isStatic = st;
            tu.decls.push_back(std::move(decl));
            if (classOf(tag)->needCtor)
                globalInit_.push_back(sExpr(ctorCall(tag, eAddr(eName(gd.name, gp), gp), std::move(args), gp), gp));
            if (classOf(tag)->needDtor) globalObjs_.push_back(DVar{gd.name, tag, 0, ""});
            ++globalCount_;
            flushExtras(tu);
            return;
        }
        Parsed d = parseDeclarator(true);
        auto type = apply(cloneType(*base), d.mods);
        if (type && type->kind == Type::Kind::Function)
            while (isKw("const") || isKw("noexcept")) advance();
        if (!td && type && type->kind == Type::Kind::Function && isP("{")) {
            std::size_t cc = d.name.rfind("::");
            std::string prefix = cc == std::string::npos ? "" : d.name.substr(0, cc);
            std::string clsTag;
            if (!prefix.empty()) {
                if (classOf(qualify(prefix))) clsTag = qualify(prefix);
                else if (classOf(prefix)) clsTag = prefix;
                else if (const Type* td2 = findTypedef(prefix)) {
                    if (!td2->tag.empty() && classOf(td2->tag)) clsTag = td2->tag;
                }
            }
            if (!clsTag.empty()) {
                // A member function defined outside its class.
                std::string fname = d.name.substr(cc + 2);
                std::string mangled;
                for (const Param& p : classOf(clsTag)->params)
                    if (p.name == fname && !p.mangled.empty() && !isSpecialName(p.name) && p.type &&
                        sameParamTypes(*p.type, *type))
                        mangled = p.mangled;
                if (mangled.empty()) {
                    err("'" + fname + "' is not declared in class '" + clsTag + "' with these parameters");
                } else {
                    auto decl = newMethodDecl(clsTag, mangled, *type, type->params, Pos{d.line, d.col, d.file});
                    decl->body = parseBody(type->inner.get());
                    extras_.push_back(std::move(decl));
                }
                flushExtras(tu);
                return;
            }
            auto decl = makeDecl(d, std::move(type), nullptr);
            decl->isStatic = st;
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

    static bool hasCopyCtor(const Type* cls) {
        if (!cls) return false;
        for (const Param& p : cls->params) {
            if (p.name != "<ctor>" || !p.type || p.type->params.size() != 1) continue;
            const Type* a = p.type->params[0].type.get();
            if (a && a->kind == Type::Kind::Reference && a->inner && a->inner->kind == Type::Kind::Struct &&
                a->inner->tag == cls->tag)
                return true;
        }
        return false;
    }

    // A local of class type: run its constructor right after the declaration and remember it so
    // that its destructor runs when the scope ends.
    void addLocal(std::vector<std::unique_ptr<Stmt>>& out, std::unique_ptr<Decl> decl,
                  std::vector<std::unique_ptr<Expr>> ctorArgs, bool haveCtorArgs) {
        std::string name = decl->name;
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
            if (decl->init) err("a static local object cannot have an initializer");
            std::string tag = decl->type->tag;
            int n = newN_++;
            std::string gname = "__sl" + std::to_string(n) + "_" + name;
            std::string guard = "__sg" + std::to_string(n);
            auto g = std::make_unique<Decl>();
            g->name = gname;
            g->line = p.line;
            g->col = p.col;
            g->file = p.file;
            g->type = cloneType(*decl->type);
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
            rt->inner = cloneType(*decl->type);
            refDecl->type = std::move(rt);
            refDecl->init = eName(gname, p);
            out.push_back(declStmt(std::move(refDecl)));
            auto thenB = sBlock(p);
            thenB->stmts.push_back(sExpr(eAssign(eName(guard, p), eInt(1, p), p), p));
            if (classOf(tag)->needCtor)
                thenB->stmts.push_back(sExpr(ctorCall(tag, eAddr(eName(gname, p), p), std::move(ctorArgs), p), p));
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
            out.push_back(std::move(iff));
            if (classOf(tag)->needDtor) staticObjs_.push_back(DVar{gname, tag, 0, guard});
            return;
        }
        const Type* ty0 = decl->type.get();
        std::uint64_t arrayCount = 0;
        if (ty0 && ty0->kind == Type::Kind::Array && ty0->inner && ty0->inner->kind == Type::Kind::Struct &&
            !ty0->inner->tag.empty() && classOf(ty0->inner->tag) &&
            (classOf(ty0->inner->tag)->needCtor || classOf(ty0->inner->tag)->needDtor)) {
            if (ty0->bound && ty0->bound->kind == Expr::Kind::Int && !decl->init) {
                arrayCount = ty0->bound->ival;
                ty0 = ty0->inner.get();
            } else {
                err("an array of objects with constructors needs a constant size and no initializer");
            }
        }
        Type* cls = ty0 && ty0->kind == Type::Kind::Struct && !ty0->tag.empty() ? classOf(ty0->tag) : nullptr;
        std::string tag = ty0 ? ty0->tag : "";
        bool hasInit = decl->init != nullptr;
        std::unique_ptr<Expr> copyFrom;
        if (cls && hasInit && !haveCtorArgs && decl->init->kind != Expr::Kind::Brace &&
            (hasCopyCtor(cls) || cls->needCtor || cls->needDtor)) {
            // "T x = y;" runs the copy constructor when y is an object (decided by the checker).
            copyFrom = std::move(decl->init);
            hasInit = false;
        }
        out.push_back(declStmt(std::move(decl)));
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
            if (cls->needDtor && !dscopes_.empty()) {
                if (eh_) ehPushObject(out, eAddr(eName(name, p), p), tag, 0, p);
                dscopes_.back().push_back(DVar{name, tag, 0, "", 0});
            }
            return;
        }
        if (cls->needCtor && (haveCtorArgs || !hasInit)) {
            out.push_back(sExpr(ctorCall(tag, eAddr(eName(name, p), p), std::move(ctorArgs), p), p));
        }
        if (cls->needDtor && !dscopes_.empty()) {
            if (eh_) ehPushObject(out, eAddr(eName(name, p), p), tag, 0, p);
            dscopes_.back().push_back(DVar{name, tag, 0, "", 0});
        }
    }

    void parseDeclInto(std::vector<std::unique_ptr<Stmt>>& out, bool forInit) {
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
        bool classBase = !td && base && base->kind == Type::Kind::Struct && !base->tag.empty() && classOf(base->tag) &&
                         (classOf(base->tag)->needCtor || classOf(base->tag)->needDtor);
        for (;;) {
            // "T x(args);" for a class: constructor arguments, not a function declarator.
            if (classBase && !atEnd() && cur().kind == Tok::Ident && nextIs("(") && i_ + 2 < toks_.size() &&
                !isP(toks_[i_ + 2], ")") && !isTypeKw(toks_[i_ + 2])) {
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
                if (eatP("=")) init = parseInitializer();
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
            if (!eatP(",")) break;
        }
        expectP(";", ";");
        (void)forInit;
    }

    std::unique_ptr<Expr> parseInitializer() {
        if (eatP("{")) {
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::Brace;
            if (!eatP("}")) {
                e->kids.push_back(parseAssign());
                while (eatP(",")) {
                    if (isP("}")) break;
                    e->kids.push_back(parseAssign());
                }
                expectP("}", "}");
            }
            return e;
        }
        return parseAssign();
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
            } else if (!atEnd() && isTypeKw(cur())) {
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
        idx->kids.push_back(eName(name, p));
        idx->kids.push_back(eName(iv, p));
        loop->body = sExpr(call(std::move(idx)), p);
        blk->stmts.push_back(std::move(loop));
        return blk;
    }

    // Destructor calls for the objects declared in scopes from 'base' up, innermost first.
    std::vector<std::unique_ptr<Stmt>> pendingDtors(std::size_t base, const Pos& p) {
        std::vector<std::unique_ptr<Stmt>> out;
        for (std::size_t sc = dscopes_.size(); sc-- > base;) {
            auto& vars = dscopes_[sc];
            for (std::size_t i = vars.size(); i-- > 0;) {
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
                auto call = dtorCall(vars[i].tag, eAddr(eName(vars[i].name, p), p), p);
                const bool had = call != nullptr;
                if (had) out.push_back(sExpr(std::move(call), p));
                if (had && eh_ && vars[i].guard.empty()) out.push_back(sExpr(ehCall("__okay_eh_pop", {}, p), p));
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

    std::unique_ptr<Stmt> parseIf() {
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::If;
        s->line = cur().line;
        s->col = cur().column;
        advance();
        expectP("(", "(");
        s->expr = parseExpr();
        expectP(")", ")");
        s->thenS = parseStmt();
        if (eatKw("else")) s->elseS = parseStmt();
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
                h.name = mangleType(type->kind == Type::Kind::Reference && type->inner ? *type->inner : *type);
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

    std::unique_ptr<Stmt> parseFor() {
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::For;
        s->line = cur().line;
        advance();
        expectP("(", "(");
        if (eatP(";")) {
            s->init = nullptr;
        } else if (!atEnd() && isTypeKw(cur())) {
            std::vector<std::unique_ptr<Stmt>> decls;
            parseDeclInto(decls, true);
            if (decls.size() == 1) s->init = std::move(decls[0]);
            else {
                auto block = std::make_unique<Stmt>();
                block->kind = Stmt::Kind::Block;
                block->stmts = std::move(decls);
                s->init = std::move(block);
            }
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
        return s;
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
        if (!isP(";")) s->expr = parseExpr();
        expectP(";", ";");
        Pos p;
        p.line = s->line;
        p.col = s->col;
        p.file = s->file;
        auto ds = pendingDtors(dbase_, p);
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
            decl->init = std::move(s->expr);
            std::string nm = decl->name;
            blk->stmts.push_back(declStmt(std::move(decl)));
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
        auto r = parseAssign();
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
    std::unique_ptr<Expr> parseMul() { return parseLeft(&Parser::parseCast, {"*", "/", "%"}); }

    bool looksLikeCast() const {
        if (!isP("(")) return false;
        for (std::size_t k = i_ + 1; k < toks_.size(); ++k) {
            if (toks_[k].kind == Tok::Newline) continue;
            return isTypeKw(toks_[k]);
        }
        return false;
    }

    std::unique_ptr<Type> parseTypeName() {
        auto base = parseBase();
        if (isP("*") || isP("(") || isP("[")) {
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

    std::unique_ptr<Expr> parseUnary() {
        if (atEnd()) {
            err("expected an expression");
            return zeroAt();
        }
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
                auto idx = parseExpr();
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
                    n->kids.push_back(parseAssign());
                    while (eatP(",")) n->kids.push_back(parseAssign());
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
                if (atEnd() || cur().kind != Tok::Ident) {
                    err("expected a field name");
                    continue;
                }
                auto n = ex(Expr::Kind::Member, op);
                n->text = op.text;
                n->kids.push_back(std::move(e));
                n->kids.push_back(leafName(cur()));
                advance();
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
        if (isKw("nullptr")) {
            auto e = ex(Expr::Kind::Int, cur());
            e->ival = 0;
            advance();
            return e;
        }
        if (cur().kind == Tok::Ident && ftmpl_.count(cur().text)) {
            std::string name = cur().text;
            Token at = cur();
            TDecl td = ftmpl_[name];
            advance();
            auto e = ex(Expr::Kind::Name, at);
            if (isP("<")) {
                std::vector<TArg> args;
                parseTemplateArgs(td, args, true);
                if (args.size() < td.params.size()) {
                    // Only the first template arguments are given: the checker deduces the rest from
                    // the call and finishes the instance. The given ones travel as carriers.
                    e->text = "\x01" "tmpl:" + name;
                    for (const TArg& a : args) {
                        auto carrier = ex(Expr::Kind::Cast, at);
                        if (a.isValue) {
                            carrier->text = "v";
                            carrier->ival = static_cast<std::uint64_t>(a.value);
                        } else if (a.type) {
                            carrier->ty = cloneType(*a.type);
                        }
                        e->kids.push_back(std::move(carrier));
                    }
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
            }
            return e;
        }
        if (cur().kind == Tok::Ident && (cur().text == "true" || cur().text == "false") && !nextIs("::")) {
            auto e = ex(Expr::Kind::Int, cur());
            e->ival = cur().text == "true" ? 1 : 0;
            advance();
            return e;
        }
        if (cur().kind == Tok::Ident) {
            auto it = enums_.find(cur().text);
            if (it != enums_.end() && !nextIs("::")) {
                auto e = ex(Expr::Kind::Int, cur());
                e->ival = static_cast<std::uint64_t>(it->second);
                advance();
                return e;
            }
            std::unique_ptr<Type> instTd;
            if (ctmpl_.count(cur().text) && nextIs("<")) {
                // Name<args>(...) with a class template: the instance, then a temporary of it.
                std::size_t saved = i_;
                instTd = instantiateClass(cur().text);
                if (!isP("(") || instTd->kind != Type::Kind::Struct) {
                    i_ = saved;
                    instTd.reset();
                }
            }
            if (instTd || nextIs("(")) {
                // T(args) for a class T: a temporary, constructed in a hidden local declared before
                // the statement. Its destructor does not run (see the plan: temporaries).
                const Type* td = instTd ? instTd.get() : findTypedef(cur().text);
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


// Parse and check. A function template called without explicit arguments is only known after the
// checker has deduced them, so the file is parsed again with the new instance requests until none
// are new (at most eight rounds).
Tu frontEnd(const std::vector<Token>& toks, Diagnostics& diags) {
    instRequests().clear();
    Tu result;
    for (int round = 0; round < 8; ++round) {
        Diagnostics local;
        instDirty() = false;
        Parser parser(toks, local);
        Tu tu = parser.parseTu();
        if (local.ok()) typecheckTu(tu, local);
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

void checkSource(const std::string& source, Diagnostics& diags, const std::string& fileName) {
    MemoryFiles files;
    files.add(fileName, source);
    std::vector<Token> raw = preprocessTokens(files, fileName, {}, diags);
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
