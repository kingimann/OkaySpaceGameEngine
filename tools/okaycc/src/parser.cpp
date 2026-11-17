#include "parser.hpp"

#include "files.hpp"
#include "ir.hpp"
#include "preproc.hpp"
#include "token.hpp"

#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace okaycc {
namespace {

struct Expr;
struct Type;
struct Stmt;
struct Decl;

struct Param {
    std::string name;
    std::unique_ptr<Type> type;
    Param();
    Param(Param&&) noexcept;
    ~Param();
};

struct Expr {
    enum class Kind {
        Int, Char, String, Name, Unary, Pre, Post, Bin, Assign, Cond, Call, Index, Member, Cast, Comma, Brace
    } kind = Kind::Int;
    std::string text;
    std::uint64_t ival = 0;
    bool litUnsigned = false;
    int litRank = 0;
    int line = 1;
    int col = 1;
    std::string file;
    std::vector<std::unique_ptr<Expr>> kids;
    std::unique_ptr<Type> ty;
    std::unique_ptr<Type> checked;
    Expr();
    ~Expr();
};

struct Type {
    enum class Kind { Void, Char, Short, Int, Long, Pointer, Array, Function, Struct } kind = Kind::Int;
    std::string tag;
    bool unsign = false;
    std::unique_ptr<Type> inner;
    std::unique_ptr<Expr> bound;
    bool unsized = false;
    std::vector<Param> params;
    Type();
    ~Type();
};

struct Decl {
    std::string name;
    std::unique_ptr<Type> type;
    std::unique_ptr<Expr> init;
    std::unique_ptr<Stmt> body;
    bool function = false;
    int line = 1;
    int col = 1;
    std::string file;
    Decl();
    ~Decl();
};

struct Stmt {
    enum class Kind { Block, Expr, Empty, Return, If, While, For, Break, Continue, Decl, Switch, Case } kind =
        Kind::Empty;
    int line = 1;
    int col = 1;
    std::string file;
    std::vector<std::unique_ptr<Stmt>> stmts;
    std::unique_ptr<Expr> expr;
    std::unique_ptr<Expr> expr2;
    std::unique_ptr<Expr> expr3;
    std::unique_ptr<Stmt> thenS;
    std::unique_ptr<Stmt> elseS;
    std::unique_ptr<Stmt> body;
    std::unique_ptr<Stmt> init;
    std::unique_ptr<Decl> decl;
    Stmt();
    ~Stmt();
};

struct Tu {
    std::vector<std::unique_ptr<Decl>> decls;
};

Param::Param() = default;
Param::Param(Param&&) noexcept = default;
Param::~Param() = default;
Expr::Expr() = default;
Expr::~Expr() = default;
Type::Type() = default;
Type::~Type() = default;
Decl::Decl() = default;
Decl::~Decl() = default;
Stmt::Stmt() = default;
Stmt::~Stmt() = default;

std::unique_ptr<Type> cloneType(const Type& t);
std::unique_ptr<Expr> cloneExpr(const Expr& e);

std::unique_ptr<Expr> cloneExpr(const Expr& e) {
    auto n = std::make_unique<Expr>();
    n->kind = e.kind;
    n->text = e.text;
    n->ival = e.ival;
    n->litUnsigned = e.litUnsigned;
    n->litRank = e.litRank;
    n->line = e.line;
    n->col = e.col;
    n->file = e.file;
    for (const auto& k : e.kids)
        if (k) n->kids.push_back(cloneExpr(*k));
    if (e.ty) n->ty = cloneType(*e.ty);
    if (e.checked) n->checked = cloneType(*e.checked);
    return n;
}

std::unique_ptr<Type> cloneType(const Type& t) {
    auto n = std::make_unique<Type>();
    n->kind = t.kind;
    n->tag = t.tag;
    n->unsign = t.unsign;
    n->unsized = t.unsized;
    if (t.inner) n->inner = cloneType(*t.inner);
    if (t.bound) n->bound = cloneExpr(*t.bound);
    for (const Param& p : t.params) {
        Param c;
        c.name = p.name;
        if (p.type) c.type = cloneType(*p.type);
        n->params.push_back(std::move(c));
    }
    return n;
}

std::string exprStr(const Expr& e);
std::string typeStr(const Type& t);

std::string esc(const std::string& in) {
    std::string o;
    for (unsigned char c : in) {
        if (c == '\\') o += "\\\\";
        else if (c == '"') o += "\\\"";
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c == '\r') o += "\\r";
        else if (c < 32 || c > 126) {
            char b[8];
            std::snprintf(b, sizeof b, "\\x%02x", c);
            o += b;
        } else {
            o.push_back(static_cast<char>(c));
        }
    }
    return o;
}

std::string typeStr(const Type& t) {
    switch (t.kind) {
    case Type::Kind::Void: return "void";
    case Type::Kind::Char: return "char";
    case Type::Kind::Short: return "short";
    case Type::Kind::Int: return "int";
    case Type::Kind::Long: return "long";
    case Type::Kind::Pointer: return "(ptr " + typeStr(*t.inner) + ")";
    case Type::Kind::Array: {
        std::string b = (t.unsized || !t.bound) ? "*" : exprStr(*t.bound);
        return "(arr " + typeStr(*t.inner) + " " + b + ")";
    }
    case Type::Kind::Struct:
        return t.tag.empty() ? "(struct)" : "(struct " + t.tag + ")";
    case Type::Kind::Function: {
        std::string s = "(fn " + typeStr(*t.inner);
        for (const Param& p : t.params) {
            s += " (p ";
            s += p.name.empty() ? "_" : p.name;
            s += " ";
            s += p.type ? typeStr(*p.type) : "?";
            s += ")";
        }
        s += ")";
        return s;
    }
    }
    return "?";
}

std::string exprStr(const Expr& e) {
    auto kid = [&](std::size_t i) { return i < e.kids.size() && e.kids[i] ? exprStr(*e.kids[i]) : "(int 0)"; };
    switch (e.kind) {
    case Expr::Kind::Int: return "(int " + std::to_string(e.ival) + ")";
    case Expr::Kind::Char: return "(char " + std::to_string(e.ival) + ")";
    case Expr::Kind::String: return "(string \"" + esc(e.text) + "\")";
    case Expr::Kind::Name: return "(name " + e.text + ")";
    case Expr::Kind::Unary:
        if (e.text == "sizeof" && e.ty && e.kids.empty()) return "(sizeof " + typeStr(*e.ty) + ")";
        return "(unary " + e.text + " " + kid(0) + ")";
    case Expr::Kind::Pre: return "(pre " + e.text + " " + kid(0) + ")";
    case Expr::Kind::Post: return "(post " + e.text + " " + kid(0) + ")";
    case Expr::Kind::Bin: return "(bin " + e.text + " " + kid(0) + " " + kid(1) + ")";
    case Expr::Kind::Assign: return "(assign " + e.text + " " + kid(0) + " " + kid(1) + ")";
    case Expr::Kind::Cond: return "(cond " + kid(0) + " " + kid(1) + " " + kid(2) + ")";
    case Expr::Kind::Index: return "(index " + kid(0) + " " + kid(1) + ")";
    case Expr::Kind::Member: return "(member " + e.text + " " + kid(0) + " " + kid(1) + ")";
    case Expr::Kind::Cast: return "(cast " + (e.ty ? typeStr(*e.ty) : "?") + " " + kid(0) + ")";
    case Expr::Kind::Call: {
        std::string s = "(call";
        for (const auto& k : e.kids)
            if (k) s += " " + exprStr(*k);
        s += ")";
        return s;
    }
    case Expr::Kind::Comma: {
        std::string s = "(comma";
        for (const auto& k : e.kids)
            if (k) s += " " + exprStr(*k);
        s += ")";
        return s;
    }
    case Expr::Kind::Brace: {
        std::string s = "(brace";
        for (const auto& k : e.kids)
            if (k) s += " " + exprStr(*k);
        s += ")";
        return s;
    }
    }
    return "(int 0)";
}

struct Pr {
    std::string s;
    void line(int ind, const std::string& text) {
        s.append(static_cast<std::size_t>(ind) * 2, ' ');
        s += text;
        s += "\n";
    }
};

void printStmt(Pr& p, int ind, const Stmt& st);
void printDecl(Pr& p, int ind, const Decl& d);

void printParams(Pr& p, int ind, const std::vector<Param>& params) {
    if (params.empty()) {
        p.line(ind, "(params)");
        return;
    }
    p.line(ind, "(params");
    for (const Param& param : params) {
        p.line(ind + 1, "(p " + (param.name.empty() ? "_" : param.name) + " " +
                            (param.type ? typeStr(*param.type) : "?") + ")");
    }
    p.line(ind, ")");
}

void printDecl(Pr& p, int ind, const Decl& d) {
    if (d.function) {
        const Type* ret = d.type && d.type->inner ? d.type->inner.get() : nullptr;
        p.line(ind, std::string(d.body ? "(fn " : "(fdecl ") + d.name);
        p.line(ind + 1, "(type " + (ret ? typeStr(*ret) : "?") + ")");
        if (d.type) printParams(p, ind + 1, d.type->params);
        else printParams(p, ind + 1, {});
        if (d.body) {
            p.line(ind + 1, "(body");
            printStmt(p, ind + 2, *d.body);
            p.line(ind + 1, ")");
        }
        p.line(ind, ")");
        return;
    }
    p.line(ind, "(var " + d.name);
    p.line(ind + 1, "(type " + (d.type ? typeStr(*d.type) : "?") + ")");
    if (d.init) {
        p.line(ind + 1, "(init");
        p.line(ind + 2, exprStr(*d.init));
        p.line(ind + 1, ")");
    }
    p.line(ind, ")");
}

void printStmt(Pr& p, int ind, const Stmt& st) {
    switch (st.kind) {
    case Stmt::Kind::Block:
        p.line(ind, "(block");
        for (const auto& s : st.stmts)
            if (s) printStmt(p, ind + 1, *s);
        p.line(ind, ")");
        break;
    case Stmt::Kind::Empty:
        p.line(ind, "(empty)");
        break;
    case Stmt::Kind::Expr:
        p.line(ind, "(expr");
        if (st.expr) p.line(ind + 1, exprStr(*st.expr));
        p.line(ind, ")");
        break;
    case Stmt::Kind::Return:
        p.line(ind, "(return");
        if (st.expr) p.line(ind + 1, exprStr(*st.expr));
        p.line(ind, ")");
        break;
    case Stmt::Kind::If:
        p.line(ind, "(if");
        if (st.expr) p.line(ind + 1, exprStr(*st.expr));
        if (st.thenS) printStmt(p, ind + 1, *st.thenS);
        if (st.elseS) {
            p.line(ind + 1, "(else");
            printStmt(p, ind + 2, *st.elseS);
            p.line(ind + 1, ")");
        }
        p.line(ind, ")");
        break;
    case Stmt::Kind::While:
        p.line(ind, "(while");
        if (st.expr) p.line(ind + 1, exprStr(*st.expr));
        if (st.body) printStmt(p, ind + 1, *st.body);
        p.line(ind, ")");
        break;
    case Stmt::Kind::For:
        p.line(ind, "(for");
        if (!st.init) p.line(ind + 1, "(init none)");
        else {
            p.line(ind + 1, "(init");
            printStmt(p, ind + 2, *st.init);
            p.line(ind + 1, ")");
        }
        if (!st.expr) p.line(ind + 1, "(cond none)");
        else p.line(ind + 1, "(cond " + exprStr(*st.expr) + ")");
        if (!st.expr2) p.line(ind + 1, "(step none)");
        else p.line(ind + 1, "(step " + exprStr(*st.expr2) + ")");
        if (st.body) printStmt(p, ind + 1, *st.body);
        p.line(ind, ")");
        break;
    case Stmt::Kind::Break:
        p.line(ind, "(break)");
        break;
    case Stmt::Kind::Continue:
        p.line(ind, "(continue)");
        break;
    case Stmt::Kind::Decl:
        if (st.decl) printDecl(p, ind, *st.decl);
        break;
    case Stmt::Kind::Switch:
        p.line(ind, "(switch");
        if (st.expr) p.line(ind + 1, exprStr(*st.expr));
        for (const auto& c : st.stmts)
            if (c) printStmt(p, ind + 1, *c);
        p.line(ind, ")");
        break;
    case Stmt::Kind::Case:
        if (st.expr) p.line(ind, "(case " + exprStr(*st.expr));
        else p.line(ind, "(default");
        if (st.body) printStmt(p, ind + 1, *st.body);
        p.line(ind, ")");
        break;
    }
}

std::string printTu(const Tu& tu) {
    if (tu.decls.empty()) return "(tu)\n";
    Pr p;
    p.line(0, "(tu");
    for (const auto& d : tu.decls)
        if (d) printDecl(p, 1, *d);
    p.line(0, ")");
    return p.s;
}

struct Mod {
    enum class Kind { Ptr, Arr, Func } kind = Kind::Ptr;
    std::unique_ptr<Expr> bound;
    bool unsized = false;
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
        while (!atEnd()) {
            if (isUnsupported()) {
                err("'" + cur().text + "' is not in this subset");
                syncSemi();
                continue;
            }
            if (!isTypeKw(cur())) {
                err("expected a declaration");
                syncSemi();
                continue;
            }
            parseExternal(tu);
        }
        return tu;
    }

private:
    std::vector<Token> toks_;
    Diagnostics& diags_;
    std::size_t i_ = 0;
    bool reported_ = false;
    std::map<std::string, std::unique_ptr<Type>> structs_;
    std::vector<std::map<std::string, std::unique_ptr<Type>>> tscopes_;

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
    bool atEnd() const { return i_ >= toks_.size() || toks_[i_].kind == Tok::End; }
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
    bool isKwTok(const Token& t, const char* s) const {
        return (t.kind == Tok::Keyword || t.kind == Tok::Ident) && t.text == s;
    }
    bool isKw(const char* s) const { return !atEnd() && isKwTok(cur(), s); }
    bool isTypeKw(const Token& t) const {
        if (isKwTok(t, "void") || isKwTok(t, "char") || isKwTok(t, "short") || isKwTok(t, "int") ||
            isKwTok(t, "long") || isKwTok(t, "struct") || isKwTok(t, "unsigned") || isKwTok(t, "signed") ||
            isKwTok(t, "typedef"))
            return true;
        return t.kind == Tok::Ident && findTypedef(t.text) != nullptr;
    }
    bool isUnsupported() const {
        if (atEnd() || cur().kind != Tok::Keyword) return false;
        return !isTypeKw(cur()) && !isKw("if") && !isKw("else") && !isKw("while") && !isKw("for") &&
               !isKw("return") && !isKw("break") && !isKw("continue") && !isKw("switch");
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
        const Token& t = i_ < toks_.size() ? toks_[i_] : toks_.back();
        diags_.error(t.file.empty() ? "t.c" : t.file, t.line, t.column, msg);
        reported_ = true;
    }
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
        if (isKw("struct")) return parseStructType();
        int longs = 0;
        int shorts = 0;
        bool sawInt = false;
        bool sawChar = false;
        bool sawVoid = false;
        bool uns = false;
        bool any = false;
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
            if (eatKw("void")) {
                sawVoid = true;
                any = true;
                continue;
            }
            break;
        }
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

    std::unique_ptr<Type> parseStructType() {
        Token at = cur();
        advance();
        std::string tag;
        if (!atEnd() && cur().kind == Tok::Ident) {
            tag = cur().text;
            advance();
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
        auto t = ty(Type::Kind::Struct);
        t->tag = tag;
        bool completing = false;
        if (!tag.empty() && !structs_.count(tag)) {
            auto inc = ty(Type::Kind::Struct);
            inc->tag = tag;
            structs_[tag] = std::move(inc);
            completing = true;
        }
        while (!atEnd() && !isP("}")) {
            if (!isTypeKw(cur())) {
                err("expected a field");
                break;
            }
            auto base = parseBase();
            Parsed d = parseDeclarator(true);
            Param field;
            field.name = d.name;
            field.type = apply(std::move(base), d.mods);
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
                structs_[tag] = cloneType(*t);
            }
        }
        return t;
    }

    Parsed parseDeclarator(bool needName) {
        int stars = 0;
        while (eatP("*")) ++stars;
        Parsed d = parseDirect(needName);
        for (int n = 0; n < stars; ++n) {
            Mod m;
            m.kind = Mod::Kind::Ptr;
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
                m.params = parseParams();
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

    std::vector<Param> parseParams() {
        std::vector<Param> params;
        if (eatP(")")) return params;
        if (isKw("void")) {
            std::size_t save = i_;
            advance();
            if (eatP(")")) return params;
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
            if (eatP(",")) continue;
            err("expected ')'");
            break;
        }
        return params;
    }

    std::unique_ptr<Type> apply(std::unique_ptr<Type> base, std::vector<Mod>& mods) {
        for (int n = static_cast<int>(mods.size()) - 1; n >= 0; --n) {
            auto t = std::make_unique<Type>();
            Mod& m = mods[static_cast<std::size_t>(n)];
            if (m.kind == Mod::Kind::Ptr) {
                t->kind = Type::Kind::Pointer;
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
        return decl;
    }

    void parseExternal(Tu& tu) {
        bool td = eatKw("typedef");
        auto base = parseBase();
        if (base && base->kind == Type::Kind::Struct && isP(";")) {
            advance();
            return;
        }
        if (atEnd()) {
            err("expected a declarator");
            return;
        }
        auto keep = [&](Parsed& d, std::unique_ptr<Type> type, std::unique_ptr<Expr> init) {
            if (td) addTypedef(d.name, std::move(type));
            else tu.decls.push_back(makeDecl(d, std::move(type), std::move(init)));
        };
        Parsed d = parseDeclarator(true);
        auto type = apply(cloneType(*base), d.mods);
        if (!td && type && type->kind == Type::Kind::Function && isP("{")) {
            auto decl = makeDecl(d, std::move(type), nullptr);
            decl->body = parseCompound();
            tu.decls.push_back(std::move(decl));
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
    }

    std::unique_ptr<Stmt> declStmt(std::unique_ptr<Decl> d) {
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::Decl;
        s->line = d->line;
        s->col = d->col;
        s->decl = std::move(d);
        return s;
    }

    void parseDeclInto(std::vector<std::unique_ptr<Stmt>>& out, bool forInit) {
        bool td = eatKw("typedef");
        auto base = parseBase();
        if (base && base->kind == Type::Kind::Struct && isP(";")) {
            advance();
            (void)forInit;
            return;
        }
        Parsed d = parseDeclarator(true);
        auto type = apply(cloneType(*base), d.mods);
        if (type && type->kind == Type::Kind::Function && isP("{")) {
            err("a function definition is not allowed here");
            return;
        }
        std::unique_ptr<Expr> init;
        if (eatP("=")) init = parseInitializer();
        if (td) addTypedef(d.name, std::move(type));
        else out.push_back(declStmt(makeDecl(d, std::move(type), std::move(init))));
        while (eatP(",")) {
            Parsed d2 = parseDeclarator(true);
            auto t2 = apply(cloneType(*base), d2.mods);
            std::unique_ptr<Expr> init2;
            if (eatP("=")) init2 = parseInitializer();
            if (td) addTypedef(d2.name, std::move(t2));
            else out.push_back(declStmt(makeDecl(d2, std::move(t2), std::move(init2))));
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
        auto saved = cur();
        expectP("{", "{");
        tscopes_.emplace_back();
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::Block;
        s->line = saved.line;
        s->col = saved.column;
        while (!atEnd() && !isP("}")) {
            if (isUnsupported()) {
                err("'" + cur().text + "' is not in this subset");
                syncSemi();
                continue;
            }
            if (!atEnd() && isTypeKw(cur())) {
                parseDeclInto(s->stmts, false);
                continue;
            }
            s->stmts.push_back(parseStmt());
        }
        expectP("}", "}");
        if (!tscopes_.empty()) tscopes_.pop_back();
        return s;
    }

    std::unique_ptr<Stmt> parseStmt() {
        if (isP("{")) return parseCompound();
        if (isKw("if")) return parseIf();
        if (isKw("while")) return parseWhile();
        if (isKw("for")) return parseFor();
        if (isKw("return")) return parseReturn();
        if (isKw("switch")) return parseSwitch();
        if (eatKw("break")) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Kind::Break;
            s->line = cur().line;
            expectP(";", ";");
            return s;
        }
        if (eatKw("continue")) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Kind::Continue;
            expectP(";", ";");
            return s;
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
        s->body = parseStmt();
        return s;
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
        s->body = parseStmt();
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
        return s;
    }

    std::unique_ptr<Expr> parseExpr() {
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
        if (cur().kind == Tok::Ident) {
            auto e = leafName(cur());
            advance();
            return e;
        }
        if (cur().kind == Tok::Int) {
            auto e = leafInt(cur());
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

bool isIntish(const Type& t) {
    return t.kind == Type::Kind::Char || t.kind == Type::Kind::Short || t.kind == Type::Kind::Int ||
           t.kind == Type::Kind::Long;
}
bool isPtr(const Type& t) { return t.kind == Type::Kind::Pointer; }
bool isVoidPtr(const Type& t) {
    return isPtr(t) && t.inner && t.inner->kind == Type::Kind::Void;
}
bool sameType(const Type& a, const Type& b) {
    if (a.kind != b.kind || a.unsign != b.unsign) return false;
    if (a.kind == Type::Kind::Pointer || a.kind == Type::Kind::Array) {
        return a.inner && b.inner && sameType(*a.inner, *b.inner);
    }
    if (a.kind == Type::Kind::Function) {
        if (!a.inner || !b.inner || !sameType(*a.inner, *b.inner) || a.params.size() != b.params.size())
            return false;
        for (std::size_t i = 0; i < a.params.size(); ++i) {
            if (!a.params[i].type || !b.params[i].type || !sameType(*a.params[i].type, *b.params[i].type))
                return false;
        }
    }
    if (a.kind == Type::Kind::Struct) {
        if (!a.tag.empty() || !b.tag.empty()) return a.tag == b.tag;
        if (a.params.size() != b.params.size()) return false;
        for (std::size_t i = 0; i < a.params.size(); ++i) {
            if (a.params[i].name != b.params[i].name) return false;
            if (!a.params[i].type || !b.params[i].type || !sameType(*a.params[i].type, *b.params[i].type))
                return false;
        }
    }
    return true;
}

struct ET {
    std::unique_ptr<Type> type;
    bool lvalue = false;
    bool mod = false;
    bool nullc = false;
    bool bad = false;
    int line = 1;
    int col = 1;
};

class Checker {
public:
    explicit Checker(Diagnostics& diags) : diags_(diags) {}

    void check(Tu& tu) {
        scopes_.emplace_back();
        for (auto& d : tu.decls) {
            if (!d) continue;
            if (d->function) checkFunction(*d);
            else checkVar(*d);
        }
        scopes_.pop_back();
    }

private:
    struct Sym {
        std::unique_ptr<Type> type;
        bool func = false;
    };

    Diagnostics& diags_;
    std::string file_;
    std::vector<std::map<std::string, Sym>> scopes_;
    int loops_ = 0;
    int switches_ = 0;
    const Type* ret_ = nullptr;

    void err(int line, int col, const std::string& msg) {
        diags_.error(file_.empty() ? "t.c" : file_, line, col, msg);
    }

    std::unique_ptr<Type> prim(Type::Kind k) {
        auto t = std::make_unique<Type>();
        t->kind = k;
        return t;
    }
    std::unique_ptr<Type> ptrTo(std::unique_ptr<Type> inner) {
        auto t = prim(Type::Kind::Pointer);
        t->inner = std::move(inner);
        return t;
    }

    ET bad(int line, int col) {
        ET e;
        e.type = prim(Type::Kind::Int);
        e.bad = true;
        e.line = line;
        e.col = col;
        return e;
    }

    void add(const std::string& name, std::unique_ptr<Type> type, bool func, int line, int col) {
        if (name.empty()) return;
        auto& sc = scopes_.back();
        auto it = sc.find(name);
        if (it != sc.end()) {
            if (func && it->second.func && it->second.type && type && sameType(*it->second.type, *type)) return;
            err(line, col, "redefined '" + name + "'");
            return;
        }
        Sym s;
        s.type = std::move(type);
        s.func = func;
        sc.emplace(name, std::move(s));
    }

    ET lookup(const std::string& name, int line, int col) {
        for (int i = static_cast<int>(scopes_.size()) - 1; i >= 0; --i) {
            auto it = scopes_[static_cast<std::size_t>(i)].find(name);
            if (it == scopes_[static_cast<std::size_t>(i)].end() || !it->second.type) continue;
            ET e;
            e.type = cloneType(*it->second.type);
            e.line = line;
            e.col = col;
            if (e.type->kind == Type::Kind::Function) {
                e.lvalue = false;
                e.mod = false;
            } else if (e.type->kind == Type::Kind::Array) {
                e.lvalue = true;
                e.mod = false;
            } else if (e.type->kind == Type::Kind::Struct) {
                e.lvalue = true;
                e.mod = true;
            } else {
                e.lvalue = true;
                e.mod = true;
            }
            return e;
        }
        err(line, col, "undeclared identifier '" + name + "'");
        return bad(line, col);
    }

    ET decay(ET e) {
        if (!e.type) return e;
        if (e.type->kind == Type::Kind::Array && e.type->inner) {
            e.type = ptrTo(std::move(e.type->inner));
            e.lvalue = false;
            e.mod = false;
        } else if (e.type->kind == Type::Kind::Function) {
            e.type = ptrTo(std::move(e.type));
            e.lvalue = false;
            e.mod = false;
        }
        return e;
    }

    bool scalar(const Type& t) const { return isIntish(t) || isPtr(t); }

    bool canAssign(const Type& dst, const ET& src) {
        if (src.bad || !src.type) return true;
        if (isIntish(dst) && isIntish(*src.type)) return true;
        if (dst.kind == Type::Kind::Struct && src.type->kind == Type::Kind::Struct && sameType(dst, *src.type))
            return true;
        if (isPtr(dst) && src.nullc) return true;
        if (isPtr(dst) && isPtr(*src.type)) {
            if (isVoidPtr(dst) || isVoidPtr(*src.type)) return true;
            return dst.inner && src.type->inner && sameType(*dst.inner, *src.type->inner);
        }
        return false;
    }

    bool isConst(const Expr& e) {
        switch (e.kind) {
        case Expr::Kind::Int:
        case Expr::Kind::Char:
        case Expr::Kind::String:
            return true;
        case Expr::Kind::Unary:
            return !e.kids.empty() && e.kids[0] && isConst(*e.kids[0]);
        case Expr::Kind::Bin:
            return e.kids.size() == 2 && e.kids[0] && e.kids[1] && isConst(*e.kids[0]) && isConst(*e.kids[1]);
        case Expr::Kind::Cast:
            return !e.kids.empty() && e.kids[0] && isConst(*e.kids[0]);
        case Expr::Kind::Member:
            return false;
        default:
            return false;
        }
    }

    ET checkExprBody(Expr& e) {
        ET r;
        r.line = e.line;
        r.col = e.col;
        auto need = [&](std::size_t n) {
            return e.kids.size() > n && e.kids[n] != nullptr;
        };
        switch (e.kind) {
        case Expr::Kind::Int:
            if (e.litRank > 0) r.type = prim(Type::Kind::Long);
            else if (!e.litUnsigned && e.ival > 0x7fffffffull) r.type = prim(Type::Kind::Long);
            else r.type = prim(Type::Kind::Int);
            if (e.litUnsigned || (e.litRank == 0 && e.ival > 0xffffffffull)) r.type->unsign = true;
            if (e.litUnsigned && e.ival > 0xffffffffull) r.type->kind = Type::Kind::Long;
            r.nullc = e.ival == 0;
            return r;
        case Expr::Kind::Char:
            r.type = prim(Type::Kind::Int);
            r.nullc = e.ival == 0;
            return r;
        case Expr::Kind::String: {
            r.type = prim(Type::Kind::Array);
            r.type->inner = prim(Type::Kind::Char);
            auto bound = std::make_unique<Expr>();
            bound->kind = Expr::Kind::Int;
            bound->ival = static_cast<std::uint64_t>(e.text.size()) + 1;
            r.type->bound = std::move(bound);
            r.lvalue = true;
            return r;
        }
        case Expr::Kind::Name:
            return lookup(e.text, e.line, e.col);
        case Expr::Kind::Unary: {
            if (!need(0)) return bad(e.line, e.col);
            if (e.text == "sizeof") {
                if (e.ty) {
                    if (e.ty->kind == Type::Kind::Void || e.ty->kind == Type::Kind::Function) {
                        err(e.line, e.col, "sizeof this type is not allowed");
                        return bad(e.line, e.col);
                    }
                } else {
                    if (!need(0)) return bad(e.line, e.col);
                    ET inner = checkExpr(*e.kids[0]);
                    if (inner.bad || !inner.type || inner.type->kind == Type::Kind::Void ||
                        inner.type->kind == Type::Kind::Function) {
                        err(e.line, e.col, "sizeof this expression is not allowed");
                        return bad(e.line, e.col);
                    }
                }
                r.type = prim(Type::Kind::Int);
                return r;
            }
            if (e.text == "&") {
                ET inner = checkExpr(*e.kids[0]);
                if (inner.bad) return bad(e.line, e.col);
                if (!inner.lvalue && !(inner.type && inner.type->kind == Type::Kind::Function)) {
                    err(e.line, e.col, "operand of '&' is not an lvalue");
                    return bad(e.line, e.col);
                }
                r.type = ptrTo(std::move(inner.type));
                return r;
            }
            if (e.text == "*") {
                ET inner = decay(checkExpr(*e.kids[0]));
                if (inner.bad) return bad(e.line, e.col);
                if (!inner.type || !isPtr(*inner.type) || !inner.type->inner) {
                    err(e.line, e.col, "operand is not a pointer");
                    return bad(e.line, e.col);
                }
                if (inner.type->inner->kind == Type::Kind::Void) {
                    err(e.line, e.col, "operand is not a pointer");
                    return bad(e.line, e.col);
                }
                r.type = cloneType(*inner.type->inner);
                r.lvalue = true;
                r.mod = isIntish(*r.type) || isPtr(*r.type);
                return r;
            }
            ET inner = decay(checkExpr(*e.kids[0]));
            if (inner.bad) return bad(e.line, e.col);
            if (e.text == "!" && inner.type && scalar(*inner.type)) {
                r.type = prim(Type::Kind::Int);
                return r;
            }
            if (!inner.type || !isIntish(*inner.type)) {
                err(e.line, e.col, "operand is not an integer");
                return bad(e.line, e.col);
            }
            r.type = prim(Type::Kind::Int);
            return r;
        }
        case Expr::Kind::Pre:
        case Expr::Kind::Post: {
            if (!need(0)) return bad(e.line, e.col);
            ET inner = checkExpr(*e.kids[0]);
            if (inner.bad) return bad(e.line, e.col);
            if (!inner.mod || !inner.type || !(isIntish(*inner.type) || isPtr(*inner.type))) {
                err(e.line, e.col, "operand of '" + e.text + "' is not a modifiable integer or pointer");
                return bad(e.line, e.col);
            }
            r.type = cloneType(*inner.type);
            return r;
        }
        case Expr::Kind::Bin:
            return checkBin(e);
        case Expr::Kind::Assign:
            return checkAssign(e);
        case Expr::Kind::Cond: {
            if (!need(0) || !need(1) || !need(2)) return bad(e.line, e.col);
            ET c = decay(checkExpr(*e.kids[0]));
            ET a = decay(checkExpr(*e.kids[1]));
            ET b = decay(checkExpr(*e.kids[2]));
            if (c.bad || a.bad || b.bad) return bad(e.line, e.col);
            if (!c.type || !scalar(*c.type)) {
                err(e.line, e.col, "condition is not a scalar");
                return bad(e.line, e.col);
            }
            if (a.type && b.type && isIntish(*a.type) && isIntish(*b.type)) {
                r.type = prim(Type::Kind::Int);
                return r;
            }
            if (a.nullc && b.type && isPtr(*b.type)) {
                r.type = cloneType(*b.type);
                return r;
            }
            if (b.nullc && a.type && isPtr(*a.type)) {
                r.type = cloneType(*a.type);
                return r;
            }
            if (a.type && b.type && isPtr(*a.type) && isPtr(*b.type) && canAssign(*a.type, b)) {
                r.type = cloneType(*a.type);
                return r;
            }
            err(e.line, e.col, "branches of '?:' do not have a common type");
            return bad(e.line, e.col);
        }
        case Expr::Kind::Call:
            return checkCall(e);
        case Expr::Kind::Index: {
            if (!need(0) || !need(1)) return bad(e.line, e.col);
            ET base = decay(checkExpr(*e.kids[0]));
            ET idx = decay(checkExpr(*e.kids[1]));
            if (base.bad || idx.bad) return bad(e.line, e.col);
            ET* ptr = nullptr;
            ET* off = nullptr;
            if (base.type && isPtr(*base.type) && idx.type && isIntish(*idx.type)) {
                ptr = &base;
                off = &idx;
            } else if (idx.type && isPtr(*idx.type) && base.type && isIntish(*base.type)) {
                ptr = &idx;
                off = &base;
            }
            (void)off;
            if (!ptr || !ptr->type->inner) {
                err(e.line, e.col, "operand is not a pointer");
                return bad(e.line, e.col);
            }
            r.type = cloneType(*ptr->type->inner);
            r.lvalue = true;
            r.mod = isIntish(*r.type) || isPtr(*r.type);
            return r;
        }
        case Expr::Kind::Member: {
            if (!need(0) || !need(1)) return bad(e.line, e.col);
            ET obj = e.text == "->" ? decay(checkExpr(*e.kids[0])) : checkExpr(*e.kids[0]);
            if (obj.bad || !obj.type) return bad(e.line, e.col);
            const Type* st = obj.type.get();
            if (e.text == "->") {
                if (!isPtr(*obj.type) || !obj.type->inner || obj.type->inner->kind != Type::Kind::Struct) {
                    err(e.line, e.col, "operand of '->' is not a pointer to a struct");
                    return bad(e.line, e.col);
                }
                st = obj.type->inner.get();
            } else if (st->kind != Type::Kind::Struct) {
                err(e.line, e.col, "operand of '.' is not a struct");
                return bad(e.line, e.col);
            }
            const std::string& fname = e.kids[1]->text;
            for (const Param& f : st->params) {
                if (f.name != fname || !f.type) continue;
                r.type = cloneType(*f.type);
                r.lvalue = true;
                r.mod = isIntish(*r.type) || isPtr(*r.type);
                return r;
            }
            err(e.line, e.col, "struct has no field '" + fname + "'");
            return bad(e.line, e.col);
        }
        case Expr::Kind::Cast: {
            if (!need(0) || !e.ty) return bad(e.line, e.col);
            ET inner = decay(checkExpr(*e.kids[0]));
            if (inner.bad) return bad(e.line, e.col);
            r.type = cloneType(*e.ty);
            r.nullc = inner.nullc && isIntish(*r.type);
            return r;
        }
        case Expr::Kind::Comma: {
            if (e.kids.empty()) return bad(e.line, e.col);
            ET last = bad(e.line, e.col);
            for (auto& k : e.kids)
                if (k) last = decay(checkExpr(*k));
            last.lvalue = false;
            last.mod = false;
            return last;
        }
        case Expr::Kind::Brace:
            err(e.line, e.col, "brace initializer is not an expression");
            return bad(e.line, e.col);
        }
        return bad(e.line, e.col);
    }

    ET checkExpr(Expr& e) {
        if (!e.file.empty()) file_ = e.file;
        ET r = checkExprBody(e);
        if (r.type) e.checked = cloneType(*r.type);
        return r;
    }

    ET checkBin(Expr& e) {
        if (e.kids.size() < 2 || !e.kids[0] || !e.kids[1]) return bad(e.line, e.col);
        ET a = decay(checkExpr(*e.kids[0]));
        ET b = decay(checkExpr(*e.kids[1]));
        if (a.bad || b.bad || !a.type || !b.type) return bad(e.line, e.col);
        const std::string& op = e.text;
        auto result = [&](std::unique_ptr<Type> t) {
            ET r;
            r.type = std::move(t);
            r.line = e.line;
            r.col = e.col;
            return r;
        };
        if (op == "+" ) {
            if (isIntish(*a.type) && isIntish(*b.type)) return result(prim(Type::Kind::Int));
            if (isPtr(*a.type) && isIntish(*b.type)) return result(cloneType(*a.type));
            if (isIntish(*a.type) && isPtr(*b.type)) return result(cloneType(*b.type));
        } else if (op == "-") {
            if (isIntish(*a.type) && isIntish(*b.type)) return result(prim(Type::Kind::Int));
            if (isPtr(*a.type) && isIntish(*b.type)) return result(cloneType(*a.type));
            if (isPtr(*a.type) && isPtr(*b.type) && a.type->inner && b.type->inner &&
                sameType(*a.type->inner, *b.type->inner))
                return result(prim(Type::Kind::Int));
        } else if (op == "*" || op == "/" || op == "%" || op == "&" || op == "|" || op == "^" || op == "<<" ||
                   op == ">>") {
            if (isIntish(*a.type) && isIntish(*b.type)) return result(prim(Type::Kind::Int));
        } else if (op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" || op == "!=") {
            bool arith = isIntish(*a.type) && isIntish(*b.type);
            bool ptrs = isPtr(*a.type) && isPtr(*b.type) && a.type->inner && b.type->inner &&
                        sameType(*a.type->inner, *b.type->inner);
            bool null = (isPtr(*a.type) && b.nullc) || (isPtr(*b.type) && a.nullc);
            if (arith || ptrs || null) return result(prim(Type::Kind::Int));
        } else if (op == "&&" || op == "||") {
            if (scalar(*a.type) && scalar(*b.type)) return result(prim(Type::Kind::Int));
        }
        err(e.line, e.col, "invalid operands to '" + op + "'");
        return bad(e.line, e.col);
    }

    ET checkAssign(Expr& e) {
        if (e.kids.size() < 2 || !e.kids[0] || !e.kids[1]) return bad(e.line, e.col);
        ET lhs = checkExpr(*e.kids[0]);
        ET rhs = decay(checkExpr(*e.kids[1]));
        if (lhs.bad || rhs.bad || !lhs.type) return bad(e.line, e.col);
        if (!lhs.mod) {
            err(e.line, e.col, "cannot assign to this expression");
            return bad(e.line, e.col);
        }
        if (e.text == "=") {
            if (!canAssign(*lhs.type, rhs)) {
                err(e.line, e.col, "cannot assign this value");
                return bad(e.line, e.col);
            }
        } else if (!isIntish(*lhs.type) || !rhs.type || !isIntish(*rhs.type)) {
            err(e.line, e.col, "cannot assign this value");
            return bad(e.line, e.col);
        }
        ET r;
        r.type = cloneType(*lhs.type);
        r.line = e.line;
        r.col = e.col;
        return r;
    }

    ET checkCall(Expr& e) {
        if (e.kids.empty() || !e.kids[0]) return bad(e.line, e.col);
        ET callee = decay(checkExpr(*e.kids[0]));
        if (callee.bad || !callee.type) return bad(e.line, e.col);
        if (!isPtr(*callee.type) || !callee.type->inner || callee.type->inner->kind != Type::Kind::Function) {
            err(e.line, e.col, "called expression is not a function");
            return bad(e.line, e.col);
        }
        const Type& fn = *callee.type->inner;
        if (e.kids.size() - 1 != fn.params.size()) {
            err(e.line, e.col, "wrong number of arguments");
            return bad(e.line, e.col);
        }
        for (std::size_t i = 0; i < fn.params.size(); ++i) {
            ET arg = decay(checkExpr(*e.kids[i + 1]));
            if (arg.bad) return bad(e.line, e.col);
            if (fn.params[i].type && !canAssign(*fn.params[i].type, arg)) {
                err(e.line, e.col, "cannot assign argument");
                return bad(e.line, e.col);
            }
        }
        ET r;
        r.line = e.line;
        r.col = e.col;
        r.type = fn.inner ? cloneType(*fn.inner) : prim(Type::Kind::Int);
        return r;
    }

    void checkVar(Decl& d) {
        if (!d.type) return;
        if (d.type->kind == Type::Kind::Void ||
            (d.type->kind == Type::Kind::Array && d.type->inner && d.type->inner->kind == Type::Kind::Void)) {
            err(d.line, d.col, "variable has type void");
        }
        if (d.init) {
            ET src = decay(checkExpr(*d.init));
            if (!src.bad && !canAssign(*d.type, src)) err(d.line, d.col, "cannot assign this value");
            else if (!src.bad && scopes_.size() == 1 && !isConst(*d.init))
                err(d.line, d.col, "initializer is not a constant");
        }
        add(d.name, cloneType(*d.type), false, d.line, d.col);
    }

    void checkFunction(Decl& d) {
        if (!d.type) return;
        add(d.name, cloneType(*d.type), true, d.line, d.col);
        if (!d.body) return;
        scopes_.emplace_back();
        const Type* saved = ret_;
        ret_ = d.type->inner.get();
        for (const Param& p : d.type->params) {
            if (p.name.empty() || !p.type) continue;
            add(p.name, cloneType(*p.type), false, d.line, d.col);
        }
        if (d.body->kind == Stmt::Kind::Block) {
            for (auto& s : d.body->stmts)
                if (s) checkStmt(*s);
        } else {
            checkStmt(*d.body);
        }
        ret_ = saved;
        scopes_.pop_back();
    }

    void checkCond(Expr& e) {
        ET c = decay(checkExpr(e));
        if (!c.bad && c.type && !scalar(*c.type)) err(e.line, e.col, "condition is not a scalar");
    }

    void checkStmt(Stmt& s) {
        if (!s.file.empty()) file_ = s.file;
        switch (s.kind) {
        case Stmt::Kind::Block:
            scopes_.emplace_back();
            for (auto& st : s.stmts)
                if (st) checkStmt(*st);
            scopes_.pop_back();
            break;
        case Stmt::Kind::Empty:
            break;
        case Stmt::Kind::Expr:
            if (s.expr) checkExpr(*s.expr);
            break;
        case Stmt::Kind::Return:
            if (!ret_) {
                err(s.line, s.col, "return outside a function");
                break;
            }
            if (ret_->kind == Type::Kind::Void) {
                if (s.expr) err(s.line, s.col, "return type does not match");
            } else if (!s.expr) {
                err(s.line, s.col, "return type does not match");
            } else {
                ET v = decay(checkExpr(*s.expr));
                if (!v.bad && !canAssign(*ret_, v)) err(s.line, s.col, "return type does not match");
            }
            break;
        case Stmt::Kind::If:
            if (s.expr) checkCond(*s.expr);
            if (s.thenS) checkStmt(*s.thenS);
            if (s.elseS) checkStmt(*s.elseS);
            break;
        case Stmt::Kind::While:
            if (s.expr) checkCond(*s.expr);
            ++loops_;
            if (s.body) checkStmt(*s.body);
            --loops_;
            break;
        case Stmt::Kind::For:
            scopes_.emplace_back();
            if (s.init) checkStmt(*s.init);
            if (s.expr) checkCond(*s.expr);
            if (s.expr2) checkExpr(*s.expr2);
            ++loops_;
            if (s.body) checkStmt(*s.body);
            --loops_;
            scopes_.pop_back();
            break;
        case Stmt::Kind::Break:
            if (loops_ == 0 && switches_ == 0) err(s.line, s.col, "break is not inside a loop or switch");
            break;
        case Stmt::Kind::Switch: {
            if (!s.expr) break;
            ET c = decay(checkExpr(*s.expr));
            if (!c.bad && (!c.type || !isIntish(*c.type))) err(s.line, s.col, "switch value is not an integer");
            ++switches_;
            bool sawDef = false;
            for (auto& cs : s.stmts) {
                if (!cs || cs->kind != Stmt::Kind::Case) continue;
                if (!cs->expr) {
                    if (sawDef) err(cs->line, cs->col, "more than one default");
                    sawDef = true;
                } else {
                    ET v = checkExpr(*cs->expr);
                    if (!v.bad && (!v.type || !isIntish(*v.type))) err(cs->line, cs->col, "case value is not an integer");
                }
                if (cs->body) checkStmt(*cs->body);
            }
            --switches_;
            break;
        }
        case Stmt::Kind::Continue:
            if (loops_ == 0) err(s.line, s.col, "continue is not inside a loop");
            break;
        case Stmt::Kind::Decl:
            if (s.decl) checkVar(*s.decl);
            break;
        case Stmt::Kind::Case:
            break;
        }
    }
};

class Lowerer {
public:
    explicit Lowerer(Diagnostics& diags) : diags_(diags) {}

    Module run(Tu& tu) {
        for (auto& d : tu.decls)
            if (d && !d->function) addGlobal(*d);
        for (auto& d : tu.decls)
            if (d && d->function && d->body) lowerFunc(*d);
        return std::move(mod_);
    }

private:
    struct FieldOff {
        std::string name;
        int offset = 0;
        int width = 8;
        int step = 0;
        int inner = 0;
        bool agg = false;
        std::string tag;
        std::vector<FieldOff> fields;
    };
    struct Val {
        Operand op;
        int step = 0;
        int inner = 0;
        std::vector<FieldOff> fields;
        Val() = default;
        Val(Operand o, int s, int i) : op(std::move(o)), step(s), inner(i) {}
    };
    struct Addr {
        Operand op;
        int width = 8;
        int step = 0;
        int inner = 0;
        bool ok = false;
        bool agg = false;
        std::vector<FieldOff> fields;
    };
    struct Sym {
        int reg = -1;
        bool global = false;
        bool array = false;
        bool agg = false;
        bool mem = false;
        bool zext = false;
        std::string gname;
        int width = 8;
        int step = 0;
        int inner = 0;
        int objSize = 8;
        std::vector<FieldOff> fields;
    };
    struct Loop {
        std::string cont;
        std::string brk;
        bool sw = false;
    };

    Diagnostics& diags_;
    std::string file_;
    Module mod_;
    Function fn_;
    int cur_ = -1;
    int nextReg_ = 0;
    int whileN_ = 0;
    int ifN_ = 0;
    int forN_ = 0;
    int switchN_ = 0;
    int tmpN_ = 0;
    int strN_ = 0;
    int retWidth_ = 8;
    bool retUns_ = false;
    bool retStruct_ = false;
    std::set<std::string> addr_;
    std::vector<std::map<std::string, Sym>> scopes_;
    std::map<std::string, Sym> globals_;
    std::vector<Loop> loops_;

    void err(int line, int col, const std::string& msg) {
        diags_.error(file_.empty() ? "t.c" : file_, line, col, msg);
    }
    void startBlock(const std::string& name) {
        fn_.blocks.push_back(Block{});
        fn_.blocks.back().name = name;
        cur_ = static_cast<int>(fn_.blocks.size()) - 1;
    }
    void emit(Inst in) { fn_.blocks[static_cast<std::size_t>(cur_)].code.push_back(std::move(in)); }
    int fresh() { return nextReg_++; }
    std::string lab(const std::string& prefix) { return prefix + std::to_string(tmpN_++); }

    static int alignUp(int off, int al) {
        if (al <= 1) return off;
        int m = off % al;
        return m ? off + (al - m) : off;
    }

    static int alignOf(const Type* t) {
        if (!t) return 1;
        switch (t->kind) {
        case Type::Kind::Char: return 1;
        case Type::Kind::Short: return 2;
        case Type::Kind::Int: return 4;
        case Type::Kind::Long:
        case Type::Kind::Pointer: return 8;
        case Type::Kind::Array: return alignOf(t->inner.get());
        case Type::Kind::Struct: {
            int a = 1;
            for (const Param& p : t->params) {
                int fa = alignOf(p.type.get());
                if (fa > a) a = fa;
            }
            return a;
        }
        default: return 1;
        }
    }

    static int sizeOf(const Type* t) {
        if (!t) return 4;
        switch (t->kind) {
        case Type::Kind::Char: return 1;
        case Type::Kind::Short: return 2;
        case Type::Kind::Int: return 4;
        case Type::Kind::Long:
        case Type::Kind::Pointer: return 8;
        case Type::Kind::Array: {
            int n = 0;
            if (t->bound && t->bound->kind == Expr::Kind::Int) n = static_cast<int>(t->bound->ival);
            return n * sizeOf(t->inner.get());
        }
        case Type::Kind::Struct: {
            int off = 0;
            for (const Param& p : t->params) {
                off = alignUp(off, alignOf(p.type.get()));
                off += sizeOf(p.type.get());
            }
            if (off == 0) return 0;
            return alignUp(off, alignOf(t));
        }
        default: return 4;
        }
    }
    static int valueStep(const Type* t) {
        if (!t) return 0;
        if (t->kind == Type::Kind::Pointer || t->kind == Type::Kind::Array) return sizeOf(t->inner.get());
        return 0;
    }
    static int innerOf(const Type* t) {
        if (!t) return 0;
        if (t->kind == Type::Kind::Pointer || t->kind == Type::Kind::Array) return valueStep(t->inner.get());
        return 0;
    }

    static std::vector<FieldOff> layoutOf(const Type* t) {
        std::vector<FieldOff> out;
        if (!t || t->kind != Type::Kind::Struct) return out;
        int off = 0;
        for (const Param& p : t->params) {
            off = alignUp(off, alignOf(p.type.get()));
            FieldOff f;
            f.name = p.name;
            f.offset = off;
            f.width = sizeOf(p.type.get());
            f.step = valueStep(p.type.get());
            f.inner = innerOf(p.type.get());
            bool nested = p.type && p.type->kind == Type::Kind::Struct;
            bool pstruct = p.type && p.type->kind == Type::Kind::Pointer && p.type->inner &&
                           p.type->inner->kind == Type::Kind::Struct;
            f.agg = nested;
            if (nested) {
                f.fields = layoutOf(p.type.get());
                f.tag = p.type->tag;
            } else if (pstruct) {
                f.tag = p.type->inner->tag;
                if (p.type->inner->tag.empty() || p.type->inner->tag != t->tag)
                    f.fields = layoutOf(p.type->inner.get());
            }
            out.push_back(std::move(f));
            off += f.width;
        }
        if (!t->tag.empty()) structLayouts()[t->tag] = out;
        return out;
    }

    static std::map<std::string, std::vector<FieldOff>>& structLayouts() {
        static std::map<std::string, std::vector<FieldOff>> layouts;
        return layouts;
    }

    static std::vector<FieldOff> fieldsFor(const FieldOff& f) {
        if (!f.fields.empty() || f.tag.empty()) return f.fields;
        auto it = structLayouts().find(f.tag);
        if (it == structLayouts().end()) return f.fields;
        return it->second;
    }

    static void describe(Sym& s, const Type* t) {
        if (!t) return;
        if (t->kind == Type::Kind::Array) {
            s.array = true;
            s.width = sizeOf(t->inner.get());
        } else {
            s.width = sizeOf(t);
        }
        s.step = valueStep(t);
        s.inner = innerOf(t);
        s.objSize = sizeOf(t);
        s.zext = t->unsign;
        if (t->kind == Type::Kind::Array && t->inner) s.zext = t->inner->unsign;
        if (t->kind == Type::Kind::Struct) {
            s.agg = true;
            s.fields = layoutOf(t);
        } else if (t->kind == Type::Kind::Pointer && t->inner && t->inner->kind == Type::Kind::Struct) {
            s.fields = layoutOf(t->inner.get());
        }
    }

    Sym* find(const std::string& name) {
        for (int i = static_cast<int>(scopes_.size()) - 1; i >= 0; --i) {
            auto it = scopes_[static_cast<std::size_t>(i)].find(name);
            if (it != scopes_[static_cast<std::size_t>(i)].end()) return &it->second;
        }
        auto g = globals_.find(name);
        if (g != globals_.end()) return &g->second;
        return nullptr;
    }

    void emitMov(int dst, Operand src, int width = 8, bool uns = false) {
        Inst in;
        in.op = Inst::Op::Mov;
        in.dst = dst;
        in.a = src;
        in.extra = width <= 0 ? 8 : width;
        in.uns = uns;
        emit(in);
    }
    Operand narrow(Operand src, int width, bool uns = false) {
        if ((width >= 8 || width <= 0) && !uns) return src;
        if (width >= 8) return src;
        int t = fresh();
        emitMov(t, src, width, uns);
        return Operand::vreg(t);
    }
    void storeMem(int slot, Operand val, int width) {
        int addr = fresh();
        Inst lea;
        lea.op = Inst::Op::Lea;
        lea.dst = addr;
        lea.a = Operand::vreg(slot);
        emit(lea);
        Inst st;
        st.op = Inst::Op::Store;
        st.a = Operand::vreg(addr);
        st.b = val;
        st.extra = width;
        emit(st);
    }
    Val loadMem(int slot, int width, bool uns) {
        int addr = fresh();
        Inst lea;
        lea.op = Inst::Op::Lea;
        lea.dst = addr;
        lea.a = Operand::vreg(slot);
        emit(lea);
        int dst = fresh();
        Inst ld;
        ld.op = Inst::Op::Load;
        ld.dst = dst;
        ld.a = Operand::vreg(addr);
        ld.extra = width;
        ld.uns = uns;
        emit(ld);
        return Val{Operand::vreg(dst), 0, 0};
    }
    void emitJmp(const std::string& label) {
        Inst in;
        in.op = Inst::Op::Jmp;
        in.label = label;
        emit(in);
    }
    void emitBr(int cond, const std::string& t, const std::string& f) {
        Inst in;
        in.op = Inst::Op::Br;
        in.a = Operand::vreg(cond);
        in.label = t;
        in.label2 = f;
        emit(in);
    }
    void emitBin(Inst::Op op, int dst, Operand a, Operand b, bool uns = false) {
        Inst in;
        in.op = op;
        in.dst = dst;
        in.a = a;
        in.b = b;
        in.uns = uns;
        emit(in);
    }
    int asReg(Operand o) {
        if (o.kind == Operand::Kind::Reg) return o.reg;
        int t = fresh();
        emitMov(t, o);
        return t;
    }
    int material(Val v) { return asReg(v.op); }

    bool fold(const std::string& op, std::int64_t x, std::int64_t y, std::int64_t& out) {
        if (op == "+") {
            out = static_cast<std::int64_t>(static_cast<std::uint64_t>(x) + static_cast<std::uint64_t>(y));
            return true;
        }
        if (op == "-") {
            out = static_cast<std::int64_t>(static_cast<std::uint64_t>(x) - static_cast<std::uint64_t>(y));
            return true;
        }
        if (op == "*") {
            out = static_cast<std::int64_t>(static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(y));
            return true;
        }
        if (op == "/" || op == "%") {
            if (y == 0) return false;
            if (x == std::numeric_limits<std::int64_t>::min() && y == -1) return false;
            out = op == "/" ? x / y : x % y;
            return true;
        }
        if (op == "&") { out = x & y; return true; }
        if (op == "|") { out = x | y; return true; }
        if (op == "^") { out = x ^ y; return true; }
        if (op == "<<" || op == ">>") {
            if (y < 0 || y >= 63) return false;
            auto ux = static_cast<std::uint64_t>(x);
            unsigned s = static_cast<unsigned>(y);
            out = static_cast<std::int64_t>(op == "<<" ? (ux << s) : (ux >> s));
            return true;
        }
        if (op == "==") { out = x == y; return true; }
        if (op == "!=") { out = x != y; return true; }
        if (op == "<") { out = x < y; return true; }
        if (op == "<=") { out = x <= y; return true; }
        if (op == ">") { out = x > y; return true; }
        if (op == ">=") { out = x >= y; return true; }
        return false;
    }

    Inst::Op binOp(const std::string& op) {
        if (op == "+") return Inst::Op::Add;
        if (op == "-") return Inst::Op::Sub;
        if (op == "*") return Inst::Op::Mul;
        if (op == "/") return Inst::Op::Div;
        if (op == "%") return Inst::Op::Mod;
        if (op == "&") return Inst::Op::And;
        if (op == "|") return Inst::Op::Or;
        if (op == "^") return Inst::Op::Xor;
        if (op == "<<") return Inst::Op::Shl;
        if (op == ">>") return Inst::Op::Shr;
        if (op == "==") return Inst::Op::Ceq;
        if (op == "!=") return Inst::Op::Cne;
        if (op == "<") return Inst::Op::Clt;
        if (op == "<=") return Inst::Op::Cle;
        if (op == ">") return Inst::Op::Cgt;
        if (op == ">=") return Inst::Op::Cge;
        return Inst::Op::Add;
    }

    std::string addString(const std::string& bytes) {
        Global g;
        g.name = ".Lstr" + std::to_string(strN_++);
        g.rodata = true;
        g.size = static_cast<int>(bytes.size()) + 1;
        g.bytes.assign(bytes.begin(), bytes.end());
        g.bytes.push_back(0);
        mod_.globals.push_back(std::move(g));
        return mod_.globals.back().name;
    }

    void putInt(std::vector<unsigned char>& b, std::int64_t v, int n) {
        auto u = static_cast<std::uint64_t>(v);
        if (n < 1) n = 1;
        if (n > 8) n = 8;
        for (int i = 0; i < n; ++i) b.push_back(static_cast<unsigned char>((u >> (8 * i)) & 0xffu));
    }

    void addGlobal(Decl& d) {
        if (!d.type) return;
        Sym s;
        s.global = true;
        s.gname = d.name;
        describe(s, d.type.get());
        Global g;
        g.name = d.name;
        if (s.array || s.agg) {
            g.size = sizeOf(d.type.get());
            if (g.size <= 0) err(d.line, d.col, "array length is not a constant");
            if (d.init) err(d.line, d.col, "global initializer is not supported");
        } else if (d.init && d.init->kind == Expr::Kind::String && d.type->kind == Type::Kind::Pointer) {
            g.size = 8;
            g.reloc = addString(d.init->text);
        } else if (d.init && (d.init->kind == Expr::Kind::Int || d.init->kind == Expr::Kind::Char)) {
            g.size = s.width > 0 && s.width <= 8 ? s.width : 8;
            putInt(g.bytes, static_cast<std::int64_t>(d.init->ival), g.size);
        } else if (d.init && d.init->kind == Expr::Kind::Unary && d.init->text == "-" && !d.init->kids.empty() &&
                   d.init->kids[0] && d.init->kids[0]->kind == Expr::Kind::Int) {
            g.size = s.width > 0 && s.width <= 8 ? s.width : 8;
            putInt(g.bytes, -static_cast<std::int64_t>(d.init->kids[0]->ival), g.size);
        } else if (!d.init) {
            g.size = s.array ? sizeOf(d.type.get()) : (s.width < 8 ? s.width : 8);
        } else {
            err(d.line, d.col, "global initializer is not supported");
            g.size = 8;
        }
        mod_.globals.push_back(std::move(g));
        globals_[d.name] = s;
    }

    Val readName(const std::string& name, int line, int col) {
        Sym* s = find(name);
        if (!s) {
            err(line, col, "not lowered: " + name);
            return {};
        }
        if (s->mem) return loadMem(s->reg, s->width, s->zext);
        if (s->array || s->agg) {
            if (s->global) {
                int dst = fresh();
                Inst in;
                in.op = Inst::Op::LeaSym;
                in.dst = dst;
                in.func = s->gname;
                emit(in);
                Val v;
                v.op = Operand::vreg(dst);
                v.step = s->step;
                v.inner = s->inner;
                v.fields = s->fields;
                return v;
            }
            Val v;
            v.op = Operand::vreg(s->reg);
            v.step = s->step;
            v.inner = s->inner;
            v.fields = s->fields;
            return v;
        }
        if (s->global) {
            int dst = fresh();
            Inst in;
            in.op = Inst::Op::GLoad;
            in.dst = dst;
            in.func = s->gname;
            in.extra = s->width;
            in.uns = s->zext;
            emit(in);
            Val v;
            v.op = Operand::vreg(dst);
            v.step = s->step;
            v.inner = s->inner;
            v.fields = s->fields;
            return v;
        }
        Val v;
        v.op = Operand::vreg(s->reg);
        v.step = s->step;
        v.inner = s->inner;
        v.fields = s->fields;
        return v;
    }

    Val scale(Val idx, int step) {
        if (step == 1) return idx;
        if (idx.op.kind == Operand::Kind::Imm) return Val{Operand::number(idx.op.imm * step), 0, 0};
        int dst = fresh();
        emitBin(Inst::Op::Mul, dst, idx.op, Operand::number(step));
        return Val{Operand::vreg(dst), 0, 0};
    }

    Addr lowerAddr(Expr& e) {
        Addr a;
        if (e.kind == Expr::Kind::Name) {
            Sym* s = find(e.text);
            if (!s) {
                err(e.line, e.col, "not lowered: " + e.text);
                return a;
            }
            if (s->mem) {
                int dst = fresh();
                Inst in;
                in.op = Inst::Op::Lea;
                in.dst = dst;
                in.a = Operand::vreg(s->reg);
                emit(in);
                a.op = Operand::vreg(dst);
                a.width = s->width;
                a.ok = true;
                return a;
            }
            if (s->agg) {
                Val base = readName(e.text, e.line, e.col);
                a.op = base.op;
                a.width = s->width;
                a.agg = true;
                a.fields = s->fields;
                a.ok = true;
                return a;
            }
            if (s->array) {
                Val base = readName(e.text, e.line, e.col);
                a.op = base.op;
                a.width = s->width;
                a.ok = true;
                return a;
            }
            if (s->global) {
                int dst = fresh();
                Inst in;
                in.op = Inst::Op::LeaSym;
                in.dst = dst;
                in.func = s->gname;
                emit(in);
                a.op = Operand::vreg(dst);
                a.width = s->width;
                a.ok = true;
                return a;
            }
            int dst = fresh();
            Inst in;
            in.op = Inst::Op::Lea;
            in.dst = dst;
            in.a = Operand::vreg(s->reg);
            emit(in);
            a.op = Operand::vreg(dst);
            a.width = s->width;
            a.ok = true;
            return a;
        }
        if (e.kind == Expr::Kind::Index && e.kids.size() >= 2 && e.kids[0] && e.kids[1]) {
            Val base = lowerExpr(*e.kids[0]);
            Val idx = lowerExpr(*e.kids[1]);
            if (base.step == 0) {
                err(e.line, e.col, "not lowered");
                return a;
            }
            Val off = scale(idx, base.step);
            int dst = fresh();
            emitBin(Inst::Op::Add, dst, base.op, off.op);
            a.op = Operand::vreg(dst);
            a.width = base.step;
            a.ok = true;
            return a;
        }
        if (e.kind == Expr::Kind::Unary && e.text == "*" && !e.kids.empty() && e.kids[0]) {
            Val p = lowerExpr(*e.kids[0]);
            a.op = p.op;
            a.width = p.step == 0 ? 8 : p.step;
            a.fields = p.fields;
            a.agg = !p.fields.empty();
            a.ok = true;
            return a;
        }
        if (e.kind == Expr::Kind::Member && e.kids.size() >= 2 && e.kids[0] && e.kids[1]) {
            Operand baseOp;
            std::vector<FieldOff> fields;
            if (e.text == "->") {
                Val p = lowerExpr(*e.kids[0]);
                baseOp = p.op;
                fields = p.fields;
            } else {
                Addr b = lowerAddr(*e.kids[0]);
                if (!b.ok) return a;
                baseOp = b.op;
                fields = std::move(b.fields);
            }
            const std::string& fname = e.kids[1]->text;
            const FieldOff* field = nullptr;
            for (const FieldOff& f : fields) {
                if (f.name == fname) field = &f;
            }
            if (!field) {
                err(e.line, e.col, "not lowered: " + fname);
                return a;
            }
            int dst = fresh();
            emitBin(Inst::Op::Add, dst, baseOp, Operand::number(field->offset));
            a.op = Operand::vreg(dst);
            a.width = field->width;
            a.step = field->step;
            a.inner = field->inner;
            a.agg = field->agg;
            a.fields = fieldsFor(*field);
            a.ok = true;
            return a;
        }
        err(e.line, e.col, "not lowered");
        return a;
    }

    Val loadAddr(Addr a, int inner, bool uns = false) {
        int dst = fresh();
        Inst in;
        in.op = Inst::Op::Load;
        in.dst = dst;
        in.a = a.op;
        in.extra = a.width;
        in.uns = uns;
        emit(in);
        return Val{Operand::vreg(dst), inner, 0};
    }
    static int bitWidth(int sz) {
        if (sz >= 8) return 8;
        if (sz >= 4) return 4;
        if (sz >= 2) return 2;
        return 1;
    }
    Operand loadBits(Operand addr, int sz) {
        int dst = fresh();
        Inst ld;
        ld.op = Inst::Op::Load;
        ld.dst = dst;
        ld.a = addr;
        ld.extra = bitWidth(sz);
        ld.uns = true;
        emit(ld);
        return Operand::vreg(dst);
    }
    void storeBits(Operand addr, Operand bits, int sz) {
        Inst st;
        st.op = Inst::Op::Store;
        st.a = addr;
        st.b = bits;
        st.extra = bitWidth(sz);
        emit(st);
    }

    Val lowerLogic(Expr& e, bool isAnd) {
        int dst = fresh();
        std::string rhsL = lab(isAnd ? "and_rhs" : "or_rhs");
        std::string skL = lab(isAnd ? "and_false" : "or_true");
        std::string endL = lab(isAnd ? "and_end" : "or_end");
        int left = material(lowerExpr(*e.kids[0]));
        if (isAnd) emitBr(left, rhsL, skL);
        else emitBr(left, skL, rhsL);
        startBlock(rhsL);
        emitMov(dst, lowerExpr(*e.kids[1]).op);
        emitJmp(endL);
        startBlock(skL);
        emitMov(dst, Operand::number(isAnd ? 0 : 1));
        emitJmp(endL);
        startBlock(endL);
        return Val{Operand::vreg(dst), 0, 0};
    }

    Val lowerExpr(Expr& e) {
        if (!e.file.empty()) file_ = e.file;
        switch (e.kind) {
        case Expr::Kind::Int:
        case Expr::Kind::Char:
            return Val{Operand::number(static_cast<std::int64_t>(e.ival)), 0, 0};
        case Expr::Kind::String: {
            std::string name = addString(e.text);
            int dst = fresh();
            Inst in;
            in.op = Inst::Op::LeaSym;
            in.dst = dst;
            in.func = name;
            emit(in);
            return Val{Operand::vreg(dst), 1, 0};
        }
        case Expr::Kind::Name:
            return readName(e.text, e.line, e.col);
        case Expr::Kind::Cast: {
            if (e.kids.empty() || !e.kids[0]) return {};
            Val v = lowerExpr(*e.kids[0]);
            if (!e.ty) return v;
            if (e.ty->kind == Type::Kind::Pointer) {
                v.step = valueStep(e.ty.get());
                v.inner = innerOf(e.ty.get());
                v.fields.clear();
                if (e.ty->inner && e.ty->inner->kind == Type::Kind::Struct) v.fields = layoutOf(e.ty->inner.get());
                return v;
            }
            if (e.ty->kind == Type::Kind::Array) return v;
            int w = sizeOf(e.ty.get());
            if (w >= 8 && !e.ty->unsign) return v;
            return Val{narrow(v.op, w, e.ty->unsign), 0, 0};
        }
        case Expr::Kind::Comma: {
            Val last{};
            for (auto& k : e.kids)
                if (k) last = lowerExpr(*k);
            return last;
        }
        case Expr::Kind::Unary:
            return lowerUnary(e);
        case Expr::Kind::Pre:
            return lowerInc(e, true);
        case Expr::Kind::Post:
            return lowerInc(e, false);
        case Expr::Kind::Bin:
            if (e.text == "&&" || e.text == "||") {
                if (e.kids.size() == 2 && e.kids[0] && e.kids[1] &&
                    (e.kids[0]->kind == Expr::Kind::Int || e.kids[0]->kind == Expr::Kind::Char) &&
                    (e.kids[1]->kind == Expr::Kind::Int || e.kids[1]->kind == Expr::Kind::Char)) {
                    std::int64_t folded = 0;
                    fold(e.text, static_cast<std::int64_t>(e.kids[0]->ival),
                         static_cast<std::int64_t>(e.kids[1]->ival), folded);
                    return Val{Operand::number(folded), 0, 0};
                }
                return lowerLogic(e, e.text == "&&");
            }
            return lowerBin(e);
        case Expr::Kind::Assign:
            return lowerAssign(e);
        case Expr::Kind::Cond:
            return lowerCond(e);
        case Expr::Kind::Call:
            return lowerCall(e);
        case Expr::Kind::Index: {
            Addr a = lowerAddr(e);
            if (!a.ok) return {};
            if (e.checked && (e.checked->kind == Type::Kind::Array || e.checked->kind == Type::Kind::Struct)) {
                Val v;
                v.op = a.op;
                v.step = valueStep(e.checked.get());
                v.inner = innerOf(e.checked.get());
                if (e.checked->kind == Type::Kind::Struct) v.fields = layoutOf(e.checked.get());
                return v;
            }
            int inner = 0;
            if (!e.kids.empty() && e.kids[0] && e.kids[0]->kind == Expr::Kind::Name) {
                if (Sym* s = find(e.kids[0]->text)) inner = s->inner;
            }
            return loadAddr(a, inner, e.checked && e.checked->unsign);
        }
        case Expr::Kind::Member: {
            Addr a = lowerAddr(e);
            if (!a.ok) return {};
            if (a.agg) {
                Val v;
                v.op = a.op;
                v.fields = a.fields;
                return v;
            }
            Val v = loadAddr(a, a.inner, e.checked && e.checked->unsign);
            v.step = a.step;
            v.inner = a.inner;
            v.fields = a.fields;
            return v;
        }
        case Expr::Kind::Brace:
            err(e.line, e.col, "not lowered");
            return {};
        }
        err(e.line, e.col, "not lowered");
        return {};
    }

    std::vector<FieldOff> fieldsOf(Expr& e) {
        if (e.kind == Expr::Kind::Name) {
            Sym* s = find(e.text);
            if (s) return s->fields;
        } else if (e.kind == Expr::Kind::Member && e.kids.size() >= 2 && e.kids[0] && e.kids[1]) {
            std::vector<FieldOff> fields = fieldsOf(*e.kids[0]);
            for (const FieldOff& f : fields)
                if (f.name == e.kids[1]->text) return f.fields;
        }
        return {};
    }

    int sizeofValue(Expr& e) {
        switch (e.kind) {
        case Expr::Kind::Int:
        case Expr::Kind::Char:
            return 8;
        case Expr::Kind::String:
            return static_cast<int>(e.text.size()) + 1;
        case Expr::Kind::Name: {
            Sym* s = find(e.text);
            if (!s) {
                err(e.line, e.col, "not lowered: " + e.text);
                return 8;
            }
            return s->objSize;
        }
        case Expr::Kind::Unary:
            if (e.text == "*" && !e.kids.empty() && e.kids[0] && e.kids[0]->kind == Expr::Kind::Name) {
                Sym* s = find(e.kids[0]->text);
                if (s && s->step != 0) return s->step;
            }
            break;
        case Expr::Kind::Index:
            if (!e.kids.empty() && e.kids[0] && e.kids[0]->kind == Expr::Kind::Name) {
                Sym* s = find(e.kids[0]->text);
                if (s && s->array) return s->width;
                if (s && s->step != 0) return s->step;
            }
            break;
        case Expr::Kind::Member:
            if (e.kids.size() >= 2 && e.kids[0] && e.kids[1]) {
                std::vector<FieldOff> fields = fieldsOf(*e.kids[0]);
                for (const FieldOff& f : fields)
                    if (f.name == e.kids[1]->text) return f.width;
            }
            break;
        default:
            break;
        }
        err(e.line, e.col, "sizeof this expression is not allowed");
        return 8;
    }

    int sizeofOf(Expr& e) {
        if (e.ty) return sizeOf(e.ty.get());
        if (!e.kids.empty() && e.kids[0] && e.kids[0]->checked) return sizeOf(e.kids[0]->checked.get());
        if (e.kids.empty() || !e.kids[0]) {
            err(e.line, e.col, "sizeof this expression is not allowed");
            return 8;
        }
        return sizeofValue(*e.kids[0]);
    }

    Val lowerUnary(Expr& e) {
        if (e.text == "sizeof") return Val{Operand::number(sizeofOf(e)), 0, 0};
        if (e.kids.empty() || !e.kids[0]) return {};
        if (e.text == "&") {
            if (e.kids[0]->kind == Expr::Kind::Name) {
                Sym* s = find(e.kids[0]->text);
                if (!s) {
                    err(e.line, e.col, "not lowered: " + e.kids[0]->text);
                    return {};
                }
                if (s->agg) {
                    Val base = readName(e.kids[0]->text, e.line, e.col);
                    base.step = s->width > 0 ? s->width : 8;
                    return base;
                }
                if (s->array || s->global) {
                    Val base = readName(e.kids[0]->text, e.line, e.col);
                    if (s->array) return base;
                    int dst = fresh();
                    Inst in;
                    in.op = Inst::Op::LeaSym;
                    in.dst = dst;
                    in.func = s->gname;
                    emit(in);
                    Val v;
                    v.op = Operand::vreg(dst);
                    v.step = s->width;
                    v.inner = s->step;
                    return v;
                }
                int dst = fresh();
                Inst in;
                in.op = Inst::Op::Lea;
                in.dst = dst;
                in.a = Operand::vreg(s->reg);
                emit(in);
                Val v;
                v.op = Operand::vreg(dst);
                v.step = s->width;
                v.inner = s->step;
                return v;
            }
            Addr a = lowerAddr(*e.kids[0]);
            Val v;
            v.op = a.op;
            v.step = a.width;
            return v;
        }
        if (e.text == "*") {
            Val p = lowerExpr(*e.kids[0]);
            Addr a;
            a.op = p.op;
            a.width = p.step == 0 ? 8 : p.step;
            a.ok = true;
            return loadAddr(a, p.inner, e.checked && e.checked->unsign);
        }
        if (e.text == "+") return lowerExpr(*e.kids[0]);
        Val a = lowerExpr(*e.kids[0]);
        if (e.text == "-") {
            if (a.op.kind == Operand::Kind::Imm && a.op.imm != std::numeric_limits<std::int64_t>::min())
                return Val{Operand::number(-a.op.imm), 0, 0};
            int dst = fresh();
            Inst in;
            in.op = Inst::Op::Neg;
            in.dst = dst;
            in.a = a.op;
            emit(in);
            return Val{Operand::vreg(dst), 0, 0};
        }
        if (e.text == "!") {
            if (a.op.kind == Operand::Kind::Imm) return Val{Operand::number(a.op.imm == 0 ? 1 : 0), 0, 0};
            int dst = fresh();
            emitBin(Inst::Op::Ceq, dst, a.op, Operand::number(0));
            return Val{Operand::vreg(dst), 0, 0};
        }
        if (e.text == "~") {
            if (a.op.kind == Operand::Kind::Imm) return Val{Operand::number(~a.op.imm), 0, 0};
            int dst = fresh();
            Inst in;
            in.op = Inst::Op::Not;
            in.dst = dst;
            in.a = a.op;
            emit(in);
            return Val{Operand::vreg(dst), 0, 0};
        }
        err(e.line, e.col, "not lowered");
        return {};
    }

    Val lowerInc(Expr& e, bool pre) {
        if (e.kids.empty() || !e.kids[0] || e.kids[0]->kind != Expr::Kind::Name) {
            err(e.line, e.col, "not lowered");
            return {};
        }
        Sym* s = find(e.kids[0]->text);
        if (!s || s->global || s->array) {
            err(e.line, e.col, "not lowered");
            return {};
        }
        Operand one = Operand::number(e.text == "--" ? -1 : 1);
        if (s->mem) {
            Val cur = loadMem(s->reg, s->width, s->zext);
            int dst = fresh();
            emitBin(Inst::Op::Add, dst, cur.op, one);
            Operand nxt = narrow(Operand::vreg(dst), s->width, s->zext);
            storeMem(s->reg, nxt, s->width);
            return pre ? Val{nxt, 0, 0} : cur;
        }
        if (pre) {
            emitBin(Inst::Op::Add, s->reg, Operand::vreg(s->reg), one);
            if (s->width > 0 && s->width < 8) emitMov(s->reg, Operand::vreg(s->reg), s->width, s->zext);
            return Val{Operand::vreg(s->reg), s->step, s->inner};
        }
        int old = fresh();
        emitMov(old, Operand::vreg(s->reg));
        emitBin(Inst::Op::Add, s->reg, Operand::vreg(s->reg), one);
        if (s->width > 0 && s->width < 8) emitMov(s->reg, Operand::vreg(s->reg), s->width, s->zext);
        return Val{Operand::vreg(old), s->step, s->inner};
    }

    Val lowerBin(Expr& e) {
        if (e.kids.size() < 2 || !e.kids[0] || !e.kids[1]) return {};
        Val a = lowerExpr(*e.kids[0]);
        Val b = lowerExpr(*e.kids[1]);
        std::int64_t folded = 0;
        if (a.step == 0 && b.step == 0 && a.op.kind == Operand::Kind::Imm && b.op.kind == Operand::Kind::Imm &&
            fold(e.text, a.op.imm, b.op.imm, folded))
            return Val{Operand::number(folded), 0, 0};
        if ((e.text == "+" || e.text == "-") && a.step != 0 && b.step == 0) {
            Val off = scale(b, a.step);
            int dst = fresh();
            emitBin(e.text == "+" ? Inst::Op::Add : Inst::Op::Sub, dst, a.op, off.op);
            return Val{Operand::vreg(dst), a.step, a.inner};
        }
        if (e.text == "+" && b.step != 0 && a.step == 0) {
            Val off = scale(a, b.step);
            int dst = fresh();
            emitBin(Inst::Op::Add, dst, b.op, off.op);
            return Val{Operand::vreg(dst), b.step, b.inner};
        }
        if (e.text == "-" && a.step != 0 && a.step == b.step) {
            int diff = fresh();
            emitBin(Inst::Op::Sub, diff, a.op, b.op);
            int q = fresh();
            emitBin(Inst::Op::Div, q, Operand::vreg(diff), Operand::number(a.step));
            return Val{Operand::vreg(q), 0, 0};
        }
        bool uns = false;
        if (e.text == "<" || e.text == ">" || e.text == "<=" || e.text == ">=") {
            const Type* ta = e.kids[0]->checked.get();
            const Type* tb = e.kids[1]->checked.get();
            auto isLong = [](const Type* t) { return t && t->kind == Type::Kind::Long; };
            bool aUns = ta && ta->unsign;
            bool bUns = tb && tb->unsign;
            if ((isLong(ta) && aUns) || (isLong(tb) && bUns)) uns = true;
            else if ((isLong(ta) && !aUns && bUns) || (isLong(tb) && !bUns && aUns)) uns = false;
            else if (aUns || bUns) {
                uns = true;
                a.op = narrow(a.op, 4, true);
                b.op = narrow(b.op, 4, true);
            }
        }
        int dst = fresh();
        emitBin(binOp(e.text), dst, a.op, b.op, uns);
        return Val{Operand::vreg(dst), 0, 0};
    }

    Val lowerAssign(Expr& e) {
        if (e.kids.size() < 2 || !e.kids[0] || !e.kids[1] || e.text != "=") {
            err(e.line, e.col, "not lowered");
            return {};
        }
        Val rhs = lowerExpr(*e.kids[1]);
        Expr& lhs = *e.kids[0];
        if (lhs.kind == Expr::Kind::Name) {
            Sym* s = find(lhs.text);
            if (!s) {
                err(e.line, e.col, "not lowered: " + lhs.text);
                return {};
            }
            if (s->array) {
                err(e.line, e.col, "not lowered");
                return {};
            }
            if (s->agg) {
                int sz = s->objSize > 0 ? s->objSize : s->width;
                if (sz > 8) {
                    err(e.line, e.col, "struct bigger than 8 bytes is not lowered");
                    return {};
                }
                Operand bits = rhs.op;
                if (!e.kids[1] || e.kids[1]->kind != Expr::Kind::Call)
                    bits = loadBits(rhs.op, sz);
                Operand addr = Operand::vreg(s->reg);
                if (s->global) {
                    int dst = fresh();
                    Inst lea;
                    lea.op = Inst::Op::LeaSym;
                    lea.dst = dst;
                    lea.func = s->gname;
                    emit(lea);
                    addr = Operand::vreg(dst);
                }
                storeBits(addr, bits, sz);
                return rhs;
            }
            if (s->global || s->mem) {
                Operand val = narrow(rhs.op, s->width, s->zext);
                if (s->global) {
                    Inst in;
                    in.op = Inst::Op::GStore;
                    in.func = s->gname;
                    in.a = val;
                    in.extra = s->width;
                    emit(in);
                } else {
                    storeMem(s->reg, val, s->width);
                }
                return Val{val, 0, 0};
            }
            emitMov(s->reg, rhs.op, s->width, s->zext);
            return Val{Operand::vreg(s->reg), s->step, s->inner};
        }
        Addr a = lowerAddr(lhs);
        if (!a.ok) return {};
        bool destUns = lhs.checked && lhs.checked->unsign;
        Operand val = narrow(rhs.op, a.width, destUns);
        Inst in;
        in.op = Inst::Op::Store;
        in.a = a.op;
        in.b = val;
        in.extra = a.width;
        emit(in);
        return Val{val, 0, 0};
    }

    Val lowerCond(Expr& e) {
        if (e.kids.size() < 3 || !e.kids[0] || !e.kids[1] || !e.kids[2]) return {};
        int dst = fresh();
        std::string tL = lab("cond_then");
        std::string fL = lab("cond_else");
        std::string eL = lab("cond_end");
        emitBr(material(lowerExpr(*e.kids[0])), tL, fL);
        startBlock(tL);
        emitMov(dst, lowerExpr(*e.kids[1]).op);
        emitJmp(eL);
        startBlock(fL);
        emitMov(dst, lowerExpr(*e.kids[2]).op);
        emitJmp(eL);
        startBlock(eL);
        return Val{Operand::vreg(dst), 0, 0};
    }

    Val lowerCall(Expr& e) {
        if (e.kids.empty() || !e.kids[0] || e.kids[0]->kind != Expr::Kind::Name) {
            err(e.line, e.col, "not lowered");
            return {};
        }
        std::vector<Operand> args;
        for (std::size_t i = 1; i < e.kids.size(); ++i) {
            if (!e.kids[i]) continue;
            Val v = lowerExpr(*e.kids[i]);
            if (e.kids[i]->checked && e.kids[i]->checked->kind == Type::Kind::Struct) {
                int sz = sizeOf(e.kids[i]->checked.get());
                if (sz > 8) err(e.line, e.col, "struct argument bigger than 8 bytes is not lowered");
                else args.push_back(loadBits(v.op, sz));
            } else {
                args.push_back(v.op);
            }
        }
        int dst = fresh();
        Inst in;
        in.op = Inst::Op::Call;
        in.dst = dst;
        in.func = e.kids[0]->text;
        in.args = std::move(args);
        emit(in);
        return Val{Operand::vreg(dst), 0, 0};
    }

    void lowerStmt(Stmt& s) {
        if (!s.file.empty()) file_ = s.file;
        switch (s.kind) {
        case Stmt::Kind::Block:
            scopes_.emplace_back();
            for (auto& st : s.stmts)
                if (st) lowerStmt(*st);
            scopes_.pop_back();
            break;
        case Stmt::Kind::Empty:
            break;
        case Stmt::Kind::Expr:
            if (s.expr) lowerExpr(*s.expr);
            break;
        case Stmt::Kind::Return: {
            Inst in;
            in.op = Inst::Op::Ret;
            if (s.expr && retStruct_ && s.expr->kind == Expr::Kind::Name) {
                if (Sym* sy = find(s.expr->text)) {
                    if (sy->agg) {
                        Val addr = readName(s.expr->text, s.line, s.col);
                        in.a = loadBits(addr.op, retWidth_);
                        in.extra = bitWidth(retWidth_);
                        in.uns = true;
                        emit(in);
                        break;
                    }
                }
            }
            if (s.expr) {
                in.a = lowerExpr(*s.expr).op;
                in.extra = retWidth_;
                in.uns = retUns_ || retStruct_;
            }
            emit(in);
            break;
        }
        case Stmt::Kind::If:
            lowerIf(s);
            break;
        case Stmt::Kind::While:
            lowerWhile(s);
            break;
        case Stmt::Kind::For:
            lowerFor(s);
            break;
        case Stmt::Kind::Break:
            if (loops_.empty()) err(s.line, s.col, "break is not inside a loop or switch");
            else emitJmp(loops_.back().brk);
            break;
        case Stmt::Kind::Continue: {
            int at = -1;
            for (int i = static_cast<int>(loops_.size()) - 1; i >= 0; --i) {
                if (!loops_[static_cast<std::size_t>(i)].sw) {
                    at = i;
                    break;
                }
            }
            if (at < 0) err(s.line, s.col, "continue is not inside a loop");
            else emitJmp(loops_[static_cast<std::size_t>(at)].cont);
            break;
        }
        case Stmt::Kind::Switch:
            lowerSwitch(s);
            break;
        case Stmt::Kind::Case:
            break;
        case Stmt::Kind::Decl:
            if (s.decl) lowerLocal(*s.decl);
            break;
        }
    }

    void lowerLocal(Decl& d) {
        if (!d.type) return;
        Sym s;
        describe(s, d.type.get());
        if (d.type->kind == Type::Kind::Array || d.type->kind == Type::Kind::Struct) {
            int bytes = sizeOf(d.type.get());
            if (bytes <= 0) {
                err(d.line, d.col, "array length is not a constant");
                bytes = 8;
            }
            int addr = fresh();
            Inst in;
            in.op = Inst::Op::Local;
            in.dst = addr;
            in.a = Operand::number(bytes);
            emit(in);
            s.reg = addr;
            if (!d.name.empty()) scopes_.back()[d.name] = s;
            return;
        }
        int slot = fresh();
        s.reg = slot;
        if (!d.name.empty() && addr_.count(d.name)) s.mem = true;
        if (!d.name.empty()) scopes_.back()[d.name] = s;
        if (d.init) {
            Operand val = narrow(lowerExpr(*d.init).op, s.width, s.zext);
            if (s.mem) storeMem(slot, val, s.width);
            else emitMov(slot, val, s.width, s.zext);
        }
    }

    void lowerIf(Stmt& s) {
        int id = ifN_++;
        std::string thenL = "if" + std::to_string(id) + "_then";
        std::string elseL = "if" + std::to_string(id) + "_else";
        std::string endL = "if" + std::to_string(id) + "_end";
        int cond = s.expr ? material(lowerExpr(*s.expr)) : material(Val{Operand::number(0), 0, 0});
        emitBr(cond, thenL, s.elseS ? elseL : endL);
        startBlock(thenL);
        if (s.thenS) lowerStmt(*s.thenS);
        emitJmp(endL);
        if (s.elseS) {
            startBlock(elseL);
            lowerStmt(*s.elseS);
            emitJmp(endL);
        }
        startBlock(endL);
    }

    void lowerWhile(Stmt& s) {
        int id = whileN_++;
        std::string cL = "while" + std::to_string(id) + "_cond";
        std::string bL = "while" + std::to_string(id) + "_body";
        std::string eL = "while" + std::to_string(id) + "_end";
        emitJmp(cL);
        startBlock(cL);
        int cond = s.expr ? material(lowerExpr(*s.expr)) : material(Val{Operand::number(0), 0, 0});
        emitBr(cond, bL, eL);
        startBlock(bL);
        loops_.push_back(Loop{cL, eL});
        if (s.body) lowerStmt(*s.body);
        loops_.pop_back();
        emitJmp(cL);
        startBlock(eL);
    }

    void lowerFor(Stmt& s) {
        int id = forN_++;
        std::string cL = "for" + std::to_string(id) + "_cond";
        std::string bL = "for" + std::to_string(id) + "_body";
        std::string sL = "for" + std::to_string(id) + "_step";
        std::string eL = "for" + std::to_string(id) + "_end";
        scopes_.emplace_back();
        if (s.init) {
            if (s.init->kind == Stmt::Kind::Block) {
                for (auto& st : s.init->stmts)
                    if (st) lowerStmt(*st);
            } else {
                lowerStmt(*s.init);
            }
        }
        emitJmp(cL);
        startBlock(cL);
        if (s.expr) emitBr(material(lowerExpr(*s.expr)), bL, eL);
        else emitJmp(bL);
        startBlock(bL);
        loops_.push_back(Loop{s.expr2 ? sL : cL, eL});
        if (s.body) lowerStmt(*s.body);
        loops_.pop_back();
        emitJmp(s.expr2 ? sL : cL);
        if (s.expr2) {
            startBlock(sL);
            lowerExpr(*s.expr2);
            emitJmp(cL);
        }
        startBlock(eL);
        scopes_.pop_back();
    }

    void scanExpr(const Expr* e) {
        if (!e) return;
        if (e->kind == Expr::Kind::Unary && e->text == "&" && !e->kids.empty() && e->kids[0] &&
            e->kids[0]->kind == Expr::Kind::Name)
            addr_.insert(e->kids[0]->text);
        for (const auto& k : e->kids) scanExpr(k.get());
    }
    void scanStmt(const Stmt& s) {
        scanExpr(s.expr.get());
        scanExpr(s.expr2.get());
        scanExpr(s.expr3.get());
        if (s.decl) scanExpr(s.decl->init.get());
        for (const auto& st : s.stmts)
            if (st) scanStmt(*st);
        if (s.thenS) scanStmt(*s.thenS);
        if (s.elseS) scanStmt(*s.elseS);
        if (s.body) scanStmt(*s.body);
        if (s.init) scanStmt(*s.init);
    }

    void lowerSwitch(Stmt& s) {
        int id = switchN_++;
        std::string endL = "sw" + std::to_string(id) + "_end";
        std::string defL = "sw" + std::to_string(id) + "_def";
        int cond = s.expr ? material(lowerExpr(*s.expr)) : material(Val{Operand::number(0), 0, 0});
        struct Arm {
            std::string label;
            bool def = false;
            std::int64_t value = 0;
            Stmt* body = nullptr;
        };
        std::vector<Arm> arms;
        bool sawDef = false;
        int n = 0;
        for (auto& cs : s.stmts) {
            if (!cs) continue;
            Arm arm;
            arm.label = "sw" + std::to_string(id) + "_" + std::to_string(n++);
            arm.body = cs->body.get();
            if (!cs->expr) {
                if (sawDef) err(cs->line, cs->col, "more than one default");
                sawDef = true;
                arm.def = true;
                arm.label = defL;
            } else {
                Val v = lowerExpr(*cs->expr);
                if (v.op.kind != Operand::Kind::Imm) {
                    err(cs->line, cs->col, "case value is not a constant");
                    v.op = Operand::number(0);
                }
                arm.value = v.op.imm;
            }
            arms.push_back(arm);
        }
        for (std::size_t i = 0; i < arms.size(); ++i) {
            if (arms[i].def) continue;
            int cmp = fresh();
            emitBin(Inst::Op::Ceq, cmp, Operand::vreg(cond), Operand::number(arms[i].value));
            std::string next = "sw" + std::to_string(id) + "_t" + std::to_string(i);
            emitBr(cmp, arms[i].label, next);
            startBlock(next);
        }
        emitJmp(sawDef ? defL : endL);
        loops_.push_back(Loop{"", endL, true});
        for (std::size_t i = 0; i < arms.size(); ++i) {
            startBlock(arms[i].label);
            if (arms[i].body) lowerStmt(*arms[i].body);
            if (i + 1 < arms.size()) emitJmp(arms[i + 1].label);
            else emitJmp(endL);
        }
        loops_.pop_back();
        startBlock(endL);
    }

    void lowerFunc(Decl& d) {
        fn_ = Function();
        fn_.name = d.name;
        fn_.params = d.type ? static_cast<int>(d.type->params.size()) : 0;
        nextReg_ = 0;
        scopes_.clear();
        scopes_.emplace_back();
        loops_.clear();
        addr_.clear();
        retWidth_ = 8;
        retUns_ = false;
        retStruct_ = false;
        if (d.body) scanStmt(*d.body);
        if (d.type && d.type->inner) {
            const Type* r = d.type->inner.get();
            if (r->kind == Type::Kind::Char || r->kind == Type::Kind::Short || r->kind == Type::Kind::Int ||
                r->kind == Type::Kind::Long)
                retWidth_ = sizeOf(r);
            if (r->kind == Type::Kind::Struct) {
                retStruct_ = true;
                retWidth_ = sizeOf(r);
            }
            retUns_ = r->unsign;
        }
        startBlock("entry");
        std::vector<int> structBits;
        std::vector<int> structSize;
        if (d.type) {
            for (const Param& p : d.type->params) {
                Sym s;
                s.reg = nextReg_++;
                if (p.type) {
                    describe(s, p.type.get());
                    if (p.type->kind == Type::Kind::Struct) {
                        int sz = sizeOf(p.type.get());
                        if (sz > 8) err(d.line, d.col, "struct parameter bigger than 8 bytes is not lowered");
                        structBits.push_back(s.reg);
                        structSize.push_back(sz);
                        s.agg = true;
                    } else if (!p.name.empty() && addr_.count(p.name)) {
                        s.mem = true;
                    }
                }
                if (!p.name.empty()) scopes_.back()[p.name] = s;
            }
        }
        {
            int n = 0;
            if (d.type) {
                for (const Param& p : d.type->params) {
                    if (!p.type || p.type->kind != Type::Kind::Struct || p.name.empty()) continue;
                    int addr = fresh();
                    Inst loc;
                    loc.op = Inst::Op::Local;
                    loc.dst = addr;
                    int sz = structSize[static_cast<std::size_t>(n)];
                    loc.a = Operand::number(sz < 1 ? 1 : sz);
                    emit(loc);
                    storeBits(Operand::vreg(addr), Operand::vreg(structBits[static_cast<std::size_t>(n)]), sz);
                    if (Sym* sy = find(p.name)) sy->reg = addr;
                    ++n;
                }
            }
        }
        if (d.body && d.body->kind == Stmt::Kind::Block) {
            for (auto& st : d.body->stmts)
                if (st) lowerStmt(*st);
        } else if (d.body) {
            lowerStmt(*d.body);
        }
        mod_.funcs.push_back(std::move(fn_));
    }
};

void typecheckTu(Tu& tu, Diagnostics& diags);

Module lowerProgram(const std::string& source, Diagnostics& diags, const std::string& fileName) {
    MemoryFiles files;
    files.add(fileName, source);
    std::vector<Token> raw = preprocessTokens(files, fileName, {}, diags);
    if (!diags.ok()) return {};
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
    Parser parser(std::move(toks), diags);
    Tu tu = parser.parseTu();
    if (!diags.ok()) return {};
    typecheckTu(tu, diags);
    if (!diags.ok()) return {};
    Lowerer lowerer(diags);
    return lowerer.run(tu);
}

void typecheckTu(Tu& tu, Diagnostics& diags) {
    Checker checker(diags);
    checker.check(tu);
}

}  // namespace

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
    Parser parser(std::move(toks), diags);
    Tu tu = parser.parseTu();
    if (!diags.ok()) return;
    typecheckTu(tu, diags);
}

Module lowerSource(const std::string& source, Diagnostics& diags, const std::string& file) {
    return lowerProgram(source, diags, file);
}

}  // namespace okaycc
