// The syntax tree: copies, printing (the tree text astOf returns) and type queries.
// Moved out of parser.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "ast.hpp"

namespace okaycc {
namespace compiler {
Param::Param() = default;
Param::Param(Param&&) noexcept = default;
Param& Param::operator=(Param&&) noexcept = default;
Param::~Param() = default;
Expr::Expr() = default;
Expr::~Expr() = default;
Type::Type() = default;
Type::~Type() = default;
Decl::Decl() = default;
Decl::~Decl() = default;
Stmt::Stmt() = default;
Stmt::~Stmt() = default;

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

// A finished class as its name and layout facts only; its members are found by its tag (canonOf).
std::unique_ptr<Type> classByName(const Type& in) {
    auto sh = std::make_unique<Type>();
    sh->kind = in.kind;
    sh->tag = in.tag;
    sh->cnst = in.cnst;
    sh->base = in.base;
    sh->nonpod = in.nonpod;
    sh->hasVptr = in.hasVptr;
    sh->needCtor = in.needCtor;
    sh->needDtor = in.needDtor;
    // (not the vtable slot names: they are read from the finished class, and copying them with
    // every named class type was a large part of compile time)
    return sh;
}
bool finishedClassCopy(const Type& t) {
    return (t.kind == Type::Kind::Struct || t.kind == Type::Kind::Union) && !t.params.empty() && !t.tag.empty() &&
           canonOf(&t) != &t;
}

std::unique_ptr<Type> cloneType(const Type& t, bool withDefaults) {
    auto n = std::make_unique<Type>();
    n->kind = t.kind;
    n->tag = t.tag;
    n->unsign = t.unsign;
    n->unsized = t.unsized;
    n->variadic = t.variadic;
    n->rref = t.rref;
    n->cnst = t.cnst;
    n->base = t.base;
    n->nonpod = t.nonpod;
    n->hasVptr = t.hasVptr;
    n->needCtor = t.needCtor;
    n->needDtor = t.needDtor;
    n->vslots = t.vslots;
    if (t.inner) {
        const Type& in = *t.inner;
        // Also the return type of a function type: a class returned by value is named, not copied
        // whole (each member function of a class returning the class copied it again: compiling a
        // file with <filesystem> took seconds in cloneType).
        bool viaRef = t.kind == Type::Kind::Pointer || t.kind == Type::Kind::Reference || t.kind == Type::Kind::Function;
        if (viaRef && finishedClassCopy(in)) {
            // A pointer or reference to a finished class keeps only the class's name and facts, not
            // its members: whoever needs the members takes the finished class by its tag (canonOf).
            // A full copy at every pointer made the types grow exponentially with nesting (a class
            // whose member functions take other classes by reference, each copying the next).
            n->inner = classByName(in);
        } else {
            n->inner = cloneType(in, withDefaults);
        }
    }
    if (t.bound) n->bound = cloneExpr(*t.bound);
    n->params.reserve(t.params.size());
    for (const Param& p : t.params) {
        Param c;
        c.name = p.name;
        c.bits = p.bits;
        c.mangled = p.mangled;
        c.vslot = p.vslot;
        c.pure = p.pure;
        c.cmethod = p.cmethod;
        if (p.type) {
            const Type& pt = *p.type;
            if (t.kind == Type::Kind::Function && finishedClassCopy(pt)) {
                // A by-value class parameter of a function type: its name and facts only.
                c.type = classByName(pt);
            } else {
                c.type = cloneType(pt, withDefaults);
            }
        }
        if (p.defArg && withDefaults) c.defArg = cloneExpr(*p.defArg);
        n->params.push_back(std::move(c));
    }
    return n;
}

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
    case Type::Kind::Float: return "float";
    case Type::Kind::Double: return "double";
    case Type::Kind::Bool: return "bool";
    case Type::Kind::Reference: return "(ref " + typeStr(*t.inner) + ")";
    case Type::Kind::Pointer: return "(ptr " + typeStr(*t.inner) + ")";
    case Type::Kind::Array: {
        std::string b = (t.unsized || !t.bound) ? "*" : exprStr(*t.bound);
        return "(arr " + typeStr(*t.inner) + " " + b + ")";
    }
    case Type::Kind::Struct:
        return t.tag.empty() ? "(struct)" : "(struct " + t.tag + ")";
    case Type::Kind::Union:
        return t.tag.empty() ? "(union)" : "(union " + t.tag + ")";
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
    case Expr::Kind::Float: return "(float " + std::to_string(e.ival) + ")";
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
    case Stmt::Kind::Goto:
        p.line(ind, "(goto " + (st.expr ? st.expr->text : "?") + ")");
        break;
    case Stmt::Kind::Label:
        p.line(ind, "(label " + (st.expr ? st.expr->text : "?") + ")");
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

bool isIntish(const Type& t) {
    return t.kind == Type::Kind::Char || t.kind == Type::Kind::Short || t.kind == Type::Kind::Int ||
           t.kind == Type::Kind::Long || t.kind == Type::Kind::Bool;
}
bool isFp(const Type& t) { return t.kind == Type::Kind::Float || t.kind == Type::Kind::Double; }
int fpWidthOf(const Type* t) {
    if (!t) return 0;
    if (t->kind == Type::Kind::Float) return 4;
    if (t->kind == Type::Kind::Double) return 8;
    return 0;
}

std::map<std::string, std::unique_ptr<Type>>& canonStructs() {
    static std::map<std::string, std::unique_ptr<Type>> m;
    return m;
}

const Type* canonOf(const Type* st) {
    if (!st || st->tag.empty()) return st;
    auto it = canonStructs().find(st->tag);
    if (it == canonStructs().end() || !it->second || it->second->params.empty()) return st;
    return it->second.get();
}
bool isPtr(const Type& t) { return t.kind == Type::Kind::Pointer; }
bool isVoidPtr(const Type& t) {
    return isPtr(t) && t.inner && t.inner->kind == Type::Kind::Void;
}
bool sameType(const Type& a, const Type& b) {
    if (a.kind != b.kind || a.unsign != b.unsign) return false;
    if (a.kind == Type::Kind::Reference && a.rref != b.rref) return false;
    if (a.kind == Type::Kind::Pointer || a.kind == Type::Kind::Array || a.kind == Type::Kind::Reference) {
        return a.inner && b.inner && sameType(*a.inner, *b.inner);
    }
    if (a.kind == Type::Kind::Function) {
        if (a.variadic != b.variadic) return false;
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

bool isSpecialName(const std::string& n) { return n == "<ctor>" || n == "<dtor>" || n == "<dtor0>"; }

const Param* findSpecial(const Type* t, const char* name) {
    if (!t) return nullptr;
    for (const Param& p : t->params)
        if (p.name == name) return &p;
    return nullptr;
}

// True when 'd' is the same class as 'b' or is derived from it (single inheritance).
bool derivedFrom(const Type& d, const Type& b) {
    if (d.kind != Type::Kind::Struct || b.kind != Type::Kind::Struct || d.tag.empty() || b.tag.empty()) return false;
    return !basePath(d.tag, b.tag).empty() || d.tag == b.tag;
}

// The members to go through from class 'from' to its base class 'to' ([class.derived]): one
// "__mb:<tag>" member per base after a primary one (parser.cpp, multiple inheritance), nothing
// for a primary base, which is at the start of the object. {""} when 'to' is reached through
// primary bases only; empty when it is not a base. Depth-first, bases in declaration order.
std::vector<std::string> basePath(const std::string& from, const std::string& to, int depth) {
    if (from == to) return {""};
    if (depth > 32 || from.empty()) return {};
    auto it = canonStructs().find(from);
    if (it == canonStructs().end() || !it->second) return {};
    const Type& c = *it->second;
    if (!c.base.empty()) {
        std::vector<std::string> p = basePath(c.base, to, depth + 1);
        if (!p.empty()) return p;
    }
    for (const Param& m : c.params) {
        if (m.name.compare(0, 5, "__mb:") != 0 || !m.type) continue;
        std::vector<std::string> p = basePath(m.type->tag, to, depth + 1);
        if (p.empty()) continue;
        if (p.size() == 1 && p[0].empty()) return {m.name};
        p.insert(p.begin(), m.name);
        return p;
    }
    return {};
}

bool hasSideEffects(const Expr& e) {
    if (e.kind == Expr::Kind::Call || e.kind == Expr::Kind::Assign || e.kind == Expr::Kind::Pre ||
        e.kind == Expr::Kind::Post)
        return true;
    for (const auto& k : e.kids)
        if (k && hasSideEffects(*k)) return true;
    return false;
}


}  // namespace compiler
}  // namespace okaycc
