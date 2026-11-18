#include "parser.hpp"

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
namespace {

struct Expr;
struct Type;
struct Stmt;
struct Decl;

struct Param {
    std::string name;
    std::unique_ptr<Type> type;
    int bits = -1;
    std::string mangled;
    int vslot = -1;     // virtual function table slot, or -1
    bool pure = false;  // declared "= 0"
    Param();
    Param(Param&&) noexcept;
    Param& operator=(Param&&) noexcept;
    ~Param();
};

struct Expr {
    enum class Kind {
        Int, Char, String, Name, Unary, Pre, Post, Bin, Assign, Cond, Call, Index, Member, Cast, Comma, Brace,
        Float
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
    enum class Kind {
        Void, Char, Short, Int, Long, Pointer, Array, Function, Struct, Union, Float, Double, Bool, Reference
    } kind = Kind::Int;
    std::string tag;
    bool unsign = false;
    std::unique_ptr<Type> inner;
    std::unique_ptr<Expr> bound;
    bool unsized = false;
    bool variadic = false;
    std::vector<Param> params;
    // Class facts (Itanium C++ ABI layout of a single-inheritance class).
    std::string base;                // tag of the one base class, or empty
    bool nonpod = false;             // not "POD for the purpose of layout"
    bool hasVptr = false;            // has a vtable pointer at offset 0
    bool needCtor = false;           // construction must run code
    bool needDtor = false;           // destruction must run code
    std::vector<std::string> vslots; // symbol that fills each vtable slot ("" = null)
    Type();
    ~Type();
};

struct Decl {
    std::string name;
    std::unique_ptr<Type> type;
    std::unique_ptr<Expr> init;
    std::unique_ptr<Stmt> body;
    bool function = false;
    bool isExtern = false;
    bool isStatic = false;
    std::string asmName;
    std::string methodTag;
    std::vector<std::string> vt;  // vtable image: symbol per 8-byte slot ("" = zero)
    int line = 1;
    int col = 1;
    std::string file;
    Decl();
    ~Decl();
};

struct Stmt {
    enum class Kind { Block, Expr, Empty, Return, If, While, For, Break, Continue, Decl, Switch, Case, Goto, Label } kind =
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
    n->variadic = t.variadic;
    n->base = t.base;
    n->nonpod = t.nonpod;
    n->hasVptr = t.hasVptr;
    n->needCtor = t.needCtor;
    n->needDtor = t.needDtor;
    n->vslots = t.vslots;
    if (t.inner) n->inner = cloneType(*t.inner);
    if (t.bound) n->bound = cloneExpr(*t.bound);
    for (const Param& p : t.params) {
        Param c;
        c.name = p.name;
        c.bits = p.bits;
        c.mangled = p.mangled;
        c.vslot = p.vslot;
        c.pure = p.pure;
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

std::string mangleNested(const std::string& qualified);
std::string mangleTag(const std::string& qualified) { return mangleNested(qualified); }

std::string mangleType(const Type& t) {
    switch (t.kind) {
    case Type::Kind::Void: return "v";
    case Type::Kind::Bool: return "b";
    case Type::Kind::Char: return t.unsign ? "h" : "c";
    case Type::Kind::Short: return t.unsign ? "t" : "s";
    case Type::Kind::Int: return t.unsign ? "j" : "i";
    case Type::Kind::Long: return t.unsign ? "m" : "l";
    case Type::Kind::Float: return "f";
    case Type::Kind::Double: return "d";
    case Type::Kind::Pointer: return t.inner ? "P" + mangleType(*t.inner) : "Pv";
    case Type::Kind::Reference: return t.inner ? "R" + mangleType(*t.inner) : "Rv";
    case Type::Kind::Struct:
    case Type::Kind::Union:
        if (t.tag.empty()) return "i";
        if (t.tag.find("::") == std::string::npos) return mangleTag(t.tag);
        return "N" + mangleTag(t.tag) + "E";
    default: return "i";
    }
}

// One source-level name part. A class template instance is spelled "Box<i,Pc>" where each
// argument is already mangled; it becomes <name> I <args> E (Itanium C++ ABI 5.1.5).
// Itanium C++ ABI 5.1.5 operator names (<operator-name>).
std::string operatorCode(const std::string& op) {
    static const char* const kTable[][2] = {
        {"+", "pl"}, {"-", "mi"}, {"*", "ml"}, {"/", "dv"}, {"%", "rm"}, {"&", "an"}, {"|", "or"}, {"^", "eo"},
        {"=", "aS"}, {"+=", "pL"}, {"-=", "mI"}, {"*=", "mL"}, {"/=", "dV"}, {"%=", "rM"}, {"&=", "aN"},
        {"|=", "oR"}, {"^=", "eO"}, {"<<", "ls"}, {">>", "rs"}, {"<<=", "lS"}, {">>=", "rS"}, {"==", "eq"},
        {"!=", "ne"}, {"<", "lt"}, {">", "gt"}, {"<=", "le"}, {">=", "ge"}, {"!", "nt"}, {"&&", "aa"},
        {"||", "oo"}, {"++", "pp"}, {"--", "mm"}, {",", "cm"}, {"->", "pt"}, {"()", "cl"}, {"[]", "ix"},
        {"~", "co"},
    };
    for (const auto& row : kTable)
        if (op == row[0]) return row[1];
    return "nw";
}

std::string manglePart(const std::string& part) {
    if (part.compare(0, 9, "operator ") == 0) return "cv" + part.substr(9);  // conversion function
    if (part.compare(0, 8, "operator") == 0 && part.size() > 8) return operatorCode(part.substr(8));
    std::size_t lt = part.find('<');
    if (lt == std::string::npos || part.back() != '>') return std::to_string(part.size()) + part;
    std::string name = part.substr(0, lt);
    std::string args = part.substr(lt + 1, part.size() - lt - 2);
    std::string joined;
    for (char c : args)
        if (c != ',') joined += c;
    return std::to_string(name.size()) + name + "I" + joined + "E";
}

std::string mangleNested(const std::string& qualified) {
    std::string out;
    std::size_t i = 0;
    while (i < qualified.size()) {
        std::size_t j = qualified.find("::", i);
        std::string part = qualified.substr(i, j == std::string::npos ? std::string::npos : j - i);
        out += manglePart(part);
        if (j == std::string::npos) break;
        i = j + 2;
    }
    return out;
}

std::string mangleFunc(const std::string& qualified, const Type& fn) {
    std::string params;
    if (fn.params.empty()) params = "v";
    else {
        for (const Param& p : fn.params)
            params += p.type ? mangleType(*p.type) : "i";
    }
    return "_ZN" + mangleNested(qualified) + "E" + params;
}

// Itanium C++ ABI 5.1.4: a constructor is <nested-name> C1 and a destructor D1 (complete
// object) or D0 (deleting). "ctor" is the two letters to put after the class name.
std::string mangleSpecial(const std::string& cls, const char* kind, const Type& fn) {
    std::string params;
    if (fn.params.empty()) params = "v";
    else {
        for (const Param& p : fn.params) params += p.type ? mangleType(*p.type) : "i";
    }
    return "_ZN" + mangleNested(cls) + kind + "E" + params;
}

std::string mangleData(const std::string& qualified) { return "_ZN" + mangleNested(qualified) + "E"; }

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

std::map<std::string, std::unique_ptr<Type>>& canonStructs();
bool sameType(const Type& a, const Type& b);

// ---- Templates ----
// A function or class template is kept as a range of tokens. An instance is made by parsing those
// tokens again with each template parameter bound (a type parameter becomes a typedef, a value
// parameter becomes a constant). This is the model of ISO/IEC 14882 [temp.inst]. Function
// templates called without explicit arguments are instantiated after the checker deduces the
// arguments from the call, then the whole file is parsed again with the new requests.
struct TArg {
    std::unique_ptr<Type> type;
    std::int64_t value = 0;
    bool isValue = false;
};

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

struct TemplateSig {
    std::vector<std::string> names;
    std::vector<bool> isType;
    std::unique_ptr<Type> fn;  // prototype, a type parameter T appears as a struct with tag "\x02T"
};

std::map<std::string, TemplateSig>& funcTemplateSigs() {
    static std::map<std::string, TemplateSig> m;
    return m;
}

struct InstReq {
    std::string tmpl;
    std::vector<TArg> args;
    std::string name;
};

std::vector<InstReq>& instRequests() {
    static std::vector<InstReq> v;
    return v;
}

bool& instDirty() {
    static bool d = false;
    return d;
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
std::string instFuncName(const std::string& tmpl, const TemplateSig& sig, const std::vector<TArg>& args) {
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

class Parser {
public:
    Parser(std::vector<Token> toks, Diagnostics& diags) : toks_(std::move(toks)), diags_(diags) {}

    Tu parseTu() {
        Tu tu;
        if (tscopes_.empty()) tscopes_.emplace_back();
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
        return tu;
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
        return !isTypeKw(cur()) && !isKw("if") && !isKw("else") && !isKw("while") && !isKw("for") &&
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
        if (!atEnd() && cur().kind == Tok::Ident && ctmpl_.count(cur().text) && !findTypedef(cur().text))
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
        if (B && B->needCtor) {
            MemInit* mi = take(shortName(T->base), T->base);
            std::vector<std::unique_ptr<Expr>> args;
            if (mi) args = std::move(mi->args);
            body->stmts.push_back(sExpr(ctorCall(T->base, eName("this", p), std::move(args), p), p));
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
                body->stmts.push_back(sExpr(
                    ctorCall(f.second->tag, eAddr(eArrow(eName("this", p), f.first, p), p), std::move(args), p), p));
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
        if (d.body) body->stmts.push_back(std::move(d.body));
        decl->body = std::move(body);
        return decl;
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
    void parseTemplateArgs(const TDecl& td, std::vector<TArg>& out) {
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
                err("too few template arguments");
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
        if (!structs_.count(tag) && args.size() == td.params.size()) {
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
            if (cls->needDtor && !dscopes_.empty()) dscopes_.back().push_back(DVar{name, tag, arrayCount, ""});
            return;
        }
        if (copyFrom) {
            std::vector<std::unique_ptr<Expr>> a;
            a.push_back(eAddr(eName(name, p), p));
            a.push_back(std::move(copyFrom));
            out.push_back(sExpr(eCall(eName("\x01" "cinit:" + tag, p), std::move(a), p), p));
            if (cls->needDtor && !dscopes_.empty()) dscopes_.back().push_back(DVar{name, tag, 0, ""});
            return;
        }
        if (cls->needCtor && (haveCtorArgs || !hasInit)) {
            out.push_back(sExpr(ctorCall(tag, eAddr(eName(name, p), p), std::move(ctorArgs), p), p));
        }
        if (cls->needDtor && !dscopes_.empty()) dscopes_.back().push_back(DVar{name, tag, 0, ""});
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
                if (vars[i].count > 0) {
                    out.push_back(elementLoop(vars[i].name, vars[i].count, true, p, [&](std::unique_ptr<Expr> at) {
                        return dtorCall(vars[i].tag, eAddr(std::move(at), p), p);
                    }));
                    continue;
                }
                auto call = dtorCall(vars[i].tag, eAddr(eName(vars[i].name, p), p), p);
                if (call) out.push_back(sExpr(std::move(call), p));
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
                parseTemplateArgs(td, args);
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
        if (cur().kind == Tok::Ident) {
            auto it = enums_.find(cur().text);
            if (it != enums_.end() && !nextIs("::")) {
                auto e = ex(Expr::Kind::Int, cur());
                e->ival = static_cast<std::uint64_t>(it->second);
                advance();
                return e;
            }
            if (nextIs("(")) {
                // T(args) for a class T: a temporary, constructed in a hidden local declared before
                // the statement. Its destructor does not run (see the plan: temporaries).
                const Type* td = findTypedef(cur().text);
                if (td && td->kind == Type::Kind::Struct && !td->tag.empty()) {
                    Token at = cur();
                    advance();
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
    std::string cur = d.tag;
    for (int guard = 0; guard < 64 && !cur.empty(); ++guard) {
        if (cur == b.tag) return true;
        auto it = canonStructs().find(cur);
        if (it == canonStructs().end() || !it->second) return false;
        cur = it->second->base;
    }
    return false;
}

bool hasSideEffects(const Expr& e) {
    if (e.kind == Expr::Kind::Call || e.kind == Expr::Kind::Assign || e.kind == Expr::Kind::Pre ||
        e.kind == Expr::Kind::Post)
        return true;
    for (const auto& k : e.kids)
        if (k && hasSideEffects(*k)) return true;
    return false;
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

struct Pos2 {
    int line = 1;
    int col = 1;
    std::string file;
};

class Checker {
public:
    explicit Checker(Diagnostics& diags) : diags_(diags) {}

    void check(Tu& tu) {
        scopes_.emplace_back();
        // Declare every function first, so a call may come before the definition (class members).
        for (auto& d : tu.decls) {
            if (d && d->function && d->type) add(d->name, cloneType(*d->type), true, d->line, d->col);
            else if (d && !d->vt.empty() && d->type) add(d->name, cloneType(*d->type), false, d->line, d->col);
        }
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
    const Type* method_ = nullptr;
    std::string inNs_;

    int quiet_ = 0;  // above 0: a probing check that must not report
    void err(int line, int col, const std::string& msg) {
        if (quiet_ > 0) return;
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

    ET lookup(const std::string& name, int line, int col, bool quiet = false) {
        for (int i = static_cast<int>(scopes_.size()) - 1; i >= 0; --i) {
            auto it = scopes_[static_cast<std::size_t>(i)].find(name);
            if (it == scopes_[static_cast<std::size_t>(i)].end() || !it->second.type) continue;
            ET e;
            e.type = cloneType(*it->second.type);
            e.line = line;
            e.col = col;
            if (e.type->kind == Type::Kind::Reference && e.type->inner) {
                e.type = cloneType(*e.type->inner);
                e.lvalue = true;
                e.mod = true;
                return e;
            }
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
        if (!quiet) err(line, col, "undeclared identifier '" + name + "'");
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

    bool scalar(const Type& t) const { return isIntish(t) || isPtr(t) || isFp(t); }

    bool canAssign(const Type& dst, const ET& src) {
        if (src.bad || !src.type) return true;
        if ((isIntish(dst) || isFp(dst)) && src.type && (isIntish(*src.type) || isFp(*src.type))) return true;
        if (dst.kind == Type::Kind::Struct && src.type->kind == Type::Kind::Struct && sameType(dst, *src.type))
            return true;
        if (isPtr(dst) && src.nullc) return true;
        if (isPtr(dst) && isPtr(*src.type)) {
            if (isVoidPtr(dst) || isVoidPtr(*src.type)) return true;
            if (dst.inner && src.type->inner && derivedFrom(*src.type->inner, *dst.inner)) return true;
            return dst.inner && src.type->inner && sameType(*dst.inner, *src.type->inner);
        }
        return false;
    }

    bool isConst(const Expr& e) {
        switch (e.kind) {
        case Expr::Kind::Int:
        case Expr::Kind::Char:
        case Expr::Kind::String:
        case Expr::Kind::Float:
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
        case Expr::Kind::Float:
            r.type = prim(e.litRank == 1 ? Type::Kind::Float : Type::Kind::Double);
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
        case Expr::Kind::Name: {
            if (method_) {
                const Type* st = canonOf(method_);
                if (st) {
                    for (const Param& f : st->params) {
                        if (f.name != e.text || !f.mangled.empty()) continue;
                        if (!f.type || f.type->kind == Type::Kind::Function) continue;
                        auto obj = std::make_unique<Expr>();
                        obj->kind = Expr::Kind::Name;
                        obj->text = "this";
                        obj->line = e.line;
                        obj->col = e.col;
                        auto field = std::make_unique<Expr>();
                        field->kind = Expr::Kind::Name;
                        field->text = e.text;
                        field->line = e.line;
                        field->col = e.col;
                        e.kind = Expr::Kind::Member;
                        e.text = "->";
                        e.kids.clear();
                        e.kids.push_back(std::move(obj));
                        e.kids.push_back(std::move(field));
                        return checkExprBody(e);
                    }
                }
            }
            ET hit = lookup(e.text, e.line, e.col, true);
            if (!hit.bad) return hit;
            if (e.text.find("::") == std::string::npos) {
                std::string scope = inNs_;
                while (!scope.empty()) {
                    std::string q = scope + "::" + e.text;
                    ET ns = lookup(q, e.line, e.col, true);
                    if (!ns.bad) {
                        e.text = q;
                        return ns;
                    }
                    std::size_t cut = scope.rfind("::");
                    if (cut == std::string::npos) break;
                    scope = scope.substr(0, cut);
                }
            }
            if (e.text == "malloc" || e.text == "free") {
                ET b;
                b.line = e.line;
                b.col = e.col;
                b.type = prim(Type::Kind::Function);
                if (e.text == "malloc") {
                    b.type->inner = ptrTo(prim(Type::Kind::Void));
                    Param a;
                    a.type = prim(Type::Kind::Long);
                    b.type->params.push_back(std::move(a));
                } else {
                    b.type->inner = prim(Type::Kind::Void);
                    Param a;
                    a.type = ptrTo(prim(Type::Kind::Char));
                    b.type->params.push_back(std::move(a));
                }
                return b;
            }
            return lookup(e.text, e.line, e.col);
        }
        case Expr::Kind::Unary: {
            if (!need(0)) return bad(e.line, e.col);
            if (e.text == "!" || e.text == "-" || e.text == "+" || e.text == "~") convertIfNeeded(*e.kids[0]);
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
                r.mod = isIntish(*r.type) || isPtr(*r.type) || isFp(*r.type) || r.type->kind == Type::Kind::Struct;
                return r;
            }
            ET inner = decay(checkExpr(*e.kids[0]));
            if (inner.bad) return bad(e.line, e.col);
            if (e.text == "!" && inner.type && scalar(*inner.type)) {
                r.type = prim(Type::Kind::Int);
                return r;
            }
            if ((e.text == "-" || e.text == "+") && inner.type && isFp(*inner.type)) {
                r.type = cloneType(*inner.type);
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
            if (a.lvalue && b.lvalue && a.type && b.type && a.type->kind == Type::Kind::Struct &&
                sameType(*a.type, *b.type)) {
                // c ? x : y with two objects of one class is an lvalue (ISO/IEC 14882 [expr.cond]):
                // *(c ? &x : &y).
                auto cond = mkE(Expr::Kind::Cond, e);
                cond->kids.push_back(std::move(e.kids[0]));
                cond->kids.push_back(eUnary("&", std::move(e.kids[1]), e));
                cond->kids.push_back(eUnary("&", std::move(e.kids[2]), e));
                e.kind = Expr::Kind::Unary;
                e.text = "*";
                e.kids.clear();
                e.kids.push_back(std::move(cond));
                return checkExprBody(e);
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
            r.mod = isIntish(*r.type) || isPtr(*r.type) || isFp(*r.type) || r.type->kind == Type::Kind::Struct;
            return r;
        }
        case Expr::Kind::Member: {
            if (!need(0) || !need(1)) return bad(e.line, e.col);
            ET obj = e.text == "->" ? decay(checkExpr(*e.kids[0])) : checkExpr(*e.kids[0]);
            if (obj.bad || !obj.type) return bad(e.line, e.col);
            const Type* st = obj.type.get();
            if (e.text == "->") {
                if (!isPtr(*obj.type) || !obj.type->inner ||
                    (obj.type->inner->kind != Type::Kind::Struct && obj.type->inner->kind != Type::Kind::Union)) {
                    err(e.line, e.col, "operand of '->' is not a pointer to a struct");
                    return bad(e.line, e.col);
                }
                st = obj.type->inner.get();
            } else if (st->kind != Type::Kind::Struct && st->kind != Type::Kind::Union) {
                err(e.line, e.col, "operand of '.' is not a struct");
                return bad(e.line, e.col);
            }
            st = canonOf(st);
            const std::string& fname = e.kids[1]->text;
            for (const Param& f : st->params) {
                if (f.name != fname || !f.type) continue;
                r.type = cloneType(*f.type);
                r.lvalue = true;
                r.mod = isIntish(*r.type) || isPtr(*r.type) || isFp(*r.type) || r.type->kind == Type::Kind::Struct;
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
            if (e.ival == 7 && e.ty) {
                // A T(args) temporary: destroyed after the statement unless it initializes an object.
                TempInfo ti;
                ti.name = e.text;
                ti.type = cloneType(*e.ty);
                add(ti.name, cloneType(*e.ty), false, e.line, e.col);
                if (needsDtor(*e.ty)) {
                    ti.tag = e.ty->tag;
                    ti.flag = "__cf" + std::to_string(tempN_++);
                    add(ti.flag, std::make_unique<Type>(), false, e.line, e.col);
                    scopes_.back()[ti.flag].type->kind = Type::Kind::Int;
                    lastTempFlag_ = ti.flag;
                    auto first = std::move(e.kids[0]);
                    e.kids[0] = withFlag(std::move(first), e);
                }
                e.ival = 9;
                temps_.push_back(std::move(ti));
            }
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

    // ---- temporaries made while checking (hidden locals, destroyed after the statement) ----
    struct TempInfo {
        std::string name;
        std::string tag;                 // class to destroy ("" = nothing to destroy)
        std::string flag;                // hidden int, set when the object was built (declared with it)
        std::unique_ptr<Type> type;      // set when the declaration still has to be inserted
        Pos2 pos;
    };
    std::vector<TempInfo> temps_;
    int tempN_ = 0;
    std::string lastTempFlag_;  // the destruction flag of the temporary newTemp just made, or ""
    // "flag = 1, first": the flag records that the object was built.
    std::unique_ptr<Expr> withFlag(std::unique_ptr<Expr> first, const Expr& at) {
        if (lastTempFlag_.empty()) return first;
        auto one = mkE(Expr::Kind::Int, at);
        one->ival = 1;
        auto set = mkE(Expr::Kind::Assign, at);
        set->text = "=";
        set->ival = 3;
        set->kids.push_back(mkName(lastTempFlag_, at));
        set->kids.push_back(std::move(one));
        auto c = mkE(Expr::Kind::Comma, at);
        c->kids.push_back(std::move(set));
        c->kids.push_back(std::move(first));
        return c;
    }
    std::map<const Expr*, ET> etCache_;

    static ET copyET(const ET& a) {
        ET r;
        if (a.type) r.type = cloneType(*a.type);
        r.lvalue = a.lvalue;
        r.mod = a.mod;
        r.nullc = a.nullc;
        r.bad = a.bad;
        r.line = a.line;
        r.col = a.col;
        return r;
    }
    static bool needsDtor(const Type& t) {
        if (t.kind != Type::Kind::Struct || t.tag.empty()) return false;
        auto it = canonStructs().find(t.tag);
        return it != canonStructs().end() && it->second && it->second->needDtor;
    }
    static bool hasUserCopy(const Type& t) {
        if (t.kind != Type::Kind::Struct || t.tag.empty()) return false;
        auto it = canonStructs().find(t.tag);
        if (it == canonStructs().end() || !it->second) return false;
        for (const Param& p : it->second->params) {
            if (p.name != "<ctor>" || !p.type || p.type->params.size() != 1) continue;
            const Type* a = p.type->params[0].type.get();
            if (a && a->kind == Type::Kind::Reference && a->inner && a->inner->kind == Type::Kind::Struct &&
                a->inner->tag == t.tag)
                return true;
        }
        return false;
    }
    // A hidden local of type t, declared before the statement being checked.
    std::string newTemp(const Type& t, const Expr& at, bool destroy) {
        TempInfo ti;
        ti.name = "__ct" + std::to_string(tempN_++);
        ti.type = cloneType(t);
        ti.pos.line = at.line;
        ti.pos.col = at.col;
        ti.pos.file = at.file;
        lastTempFlag_.clear();
        if (destroy && needsDtor(t)) {
            ti.tag = t.tag;
            ti.flag = "__cf" + std::to_string(tempN_++);
            lastTempFlag_ = ti.flag;
            add(ti.flag, std::make_unique<Type>(), false, at.line, at.col);
            scopes_.back()[ti.flag].type->kind = Type::Kind::Int;
        }
        add(ti.name, cloneType(t), false, at.line, at.col);
        std::string name = ti.name;
        temps_.push_back(std::move(ti));
        return name;
    }
    // The named temporary turned out to initialize an object: nothing destroys it separately.
    void eraseTemp(const std::string& name) {
        for (std::size_t i = 0; i < temps_.size(); ++i)
            if (temps_[i].name == name) {
                temps_[i].tag.clear();  // still declared (the flag is set), but not destroyed
                return;
            }
    }
    std::unique_ptr<Expr> eUnary(const char* op, std::unique_ptr<Expr> k, const Expr& at) {
        auto u = mkE(Expr::Kind::Unary, at);
        u->text = op;
        u->kids.push_back(std::move(k));
        return u;
    }
    std::unique_ptr<Expr> eAssignNode(std::unique_ptr<Expr> l, std::unique_ptr<Expr> r, const Expr& at) {
        auto a = mkE(Expr::Kind::Assign, at);
        a->text = "=";
        a->ival = 3;  // a plain copy: operator= is not used
        a->kids.push_back(std::move(l));
        a->kids.push_back(std::move(r));
        return a;
    }
    std::unique_ptr<Expr> eCommaNode(std::unique_ptr<Expr> a, std::unique_ptr<Expr> b, const Expr& at) {
        auto c = mkE(Expr::Kind::Comma, at);
        c->kids.push_back(std::move(a));
        c->kids.push_back(std::move(b));
        return c;
    }

    // An operand that is cheap and safe to check twice: a name, a member, an index or a dereference
    // built from those. (Checking a call twice is avoided; a chain would check itself too often.)
    static bool simpleOperand(const Expr& e) {
        switch (e.kind) {
        case Expr::Kind::Name: return true;
        case Expr::Kind::Member: return !e.kids.empty() && e.kids[0] && simpleOperand(*e.kids[0]);
        case Expr::Kind::Unary: return e.text == "*" && !e.kids.empty() && e.kids[0] && simpleOperand(*e.kids[0]);
        case Expr::Kind::Index:
            return e.kids.size() == 2 && e.kids[0] && e.kids[1] && simpleOperand(*e.kids[0]) &&
                   (e.kids[1]->kind == Expr::Kind::Int || e.kids[1]->kind == Expr::Kind::Name);
        default: return false;
        }
    }

    // C++ operator overloading for member operators: "a + b" on an object whose class declares
    // operator+ becomes the call a.operator+(b). Written from ISO/IEC 14882 [over.match.oper]
    // (member candidates only; no non-member operators, no conversion sequences).
    // Returns false when the expression is left alone. Only a simple left operand is looked at.
    bool rewriteOperator(Expr& e) {
        std::string op;
        std::size_t lhsAt = 0;
        bool post = false;
        switch (e.kind) {
        case Expr::Kind::Bin:
        case Expr::Kind::Assign:
            if (e.kids.size() != 2 || (e.kind == Expr::Kind::Assign && e.ival == 3)) return false;
            op = "operator" + e.text;
            break;
        case Expr::Kind::Index:
            if (e.kids.size() != 2) return false;
            op = "operator[]";
            break;
        case Expr::Kind::Pre:
            if (e.kids.size() != 1) return false;
            op = "operator" + e.text;
            break;
        case Expr::Kind::Post:
            if (e.kids.size() != 1) return false;
            op = "operator" + e.text;
            post = true;
            break;
        case Expr::Kind::Unary:
            if (e.kids.size() != 1 || (e.text != "-" && e.text != "+" && e.text != "!" && e.text != "~")) return false;
            op = "operator" + e.text;
            break;
        case Expr::Kind::Call:
            if (e.kids.empty() || !e.kids[0] || e.kids[0]->kind != Expr::Kind::Name) return false;
            op = "operator()";
            break;
        default: return false;
        }
        if (!e.kids[lhsAt]) return false;
        Expr& lhs = *e.kids[lhsAt];
        const bool simple = simpleOperand(lhs);
        // A left operand that is a call or another operator expression (a + b + c): check it once,
        // keep the result for the check that follows, and use it only for a class with the operator.
        if (!simple && e.kind != Expr::Kind::Bin && e.kind != Expr::Kind::Index) return false;
        if (!simple && lhs.kind != Expr::Kind::Call && lhs.kind != Expr::Kind::Bin && lhs.kind != Expr::Kind::Comma &&
            lhs.kind != Expr::Kind::Unary)
            return false;
        if (e.kind == Expr::Kind::Call && lhs.text.compare(0, 1, "\x01") == 0) return false;
        ++quiet_;
        ET probe = checkExpr(lhs);
        --quiet_;
        const bool usable = !probe.bad && probe.type && probe.type->kind == Type::Kind::Struct;
        const Type* st = usable ? canonOf(probe.type.get()) : nullptr;
        bool found = false;
        if (st)
            for (const Param& f : st->params)
                if (f.name == op && !f.mangled.empty()) found = true;
        if (!simple) {
            if (probe.bad) return false;
            if (!found || probe.lvalue) {
                etCache_[&lhs] = copyET(probe);
                return false;
            }
            // An object that is not an lvalue (the result of a call): put it in a hidden local and
            // call the operator on that.
            std::string tmp = newTemp(*probe.type, e, true);
            auto assign = withFlag(eAssignNode(mkName(tmp, e), std::move(e.kids[lhsAt]), e), e);
            auto member = mkE(Expr::Kind::Member, e);
            member->text = ".";
            member->kids.push_back(mkName(tmp, e));
            member->kids.push_back(mkName(op, e));
            auto call = mkE(Expr::Kind::Call, e);
            call->kids.push_back(std::move(member));
            for (std::size_t i = 1; i < e.kids.size(); ++i) call->kids.push_back(std::move(e.kids[i]));
            std::unique_ptr<Expr> last = std::move(call);
            std::unique_ptr<Expr> assignResult;
            const Type* opRet = nullptr;
            for (const Param& f : st->params)
                if (f.name == op && !f.mangled.empty() && f.type) opRet = f.type->inner.get();
            if (opRet && opRet->kind == Type::Kind::Struct) {
                // An object result is kept in a hidden local too, so the whole expression is an
                // address-bearing name like any other object.
                std::string res = newTemp(*opRet, e, false);
                assignResult = eAssignNode(mkName(res, e), std::move(last), e);
                last = mkName(res, e);
            }
            auto both = eCommaNode(std::move(assign), std::move(last), e);
            if (assignResult) both->kids.insert(both->kids.begin() + 1, std::move(assignResult));
            e.kind = Expr::Kind::Comma;
            e.text.clear();
            e.ival = 0;
            e.kids = std::move(both->kids);
            return true;
        }
        if (probe.bad || !usable || !st) return false;
        if (!found || !probe.lvalue) return false;
        auto member = mkE(Expr::Kind::Member, e);
        member->text = ".";
        member->kids.push_back(std::move(e.kids[lhsAt]));
        member->kids.push_back(mkName(op, e));
        std::vector<std::unique_ptr<Expr>> kids;
        kids.push_back(std::move(member));
        for (std::size_t i = 1; i < e.kids.size(); ++i) kids.push_back(std::move(e.kids[i]));
        if (post) {
            auto zero = mkE(Expr::Kind::Int, e);
            kids.push_back(std::move(zero));
        }
        e.kids = std::move(kids);
        e.kind = Expr::Kind::Call;
        e.text.clear();
        return true;
    }

    // An object of a class with a conversion function used where a number or pointer is needed:
    // e becomes e.operator T() (the first conversion function the class has). Only an lvalue
    // object is converted (a call result is not), and the result is not converted again.
    void convertIfNeeded(Expr& e) {
        if (!simpleOperand(e)) return;
        ++quiet_;
        ET probe = checkExpr(e);
        --quiet_;
        if (probe.bad || !probe.type || probe.type->kind != Type::Kind::Struct || !probe.lvalue) return;
        const Type* st = canonOf(probe.type.get());
        if (!st) return;
        const Param* conv = nullptr;
        for (const Param& f : st->params)
            if (f.name.compare(0, 9, "operator ") == 0 && !f.mangled.empty()) {
                conv = &f;
                break;
            }
        if (!conv) return;
        auto self = std::make_unique<Expr>();
        self->kind = e.kind;
        self->text = e.text;
        self->ival = e.ival;
        self->litUnsigned = e.litUnsigned;
        self->litRank = e.litRank;
        self->line = e.line;
        self->col = e.col;
        self->file = e.file;
        self->kids = std::move(e.kids);
        self->ty = std::move(e.ty);
        auto member = mkE(Expr::Kind::Member, e);
        member->text = ".";
        member->kids.push_back(std::move(self));
        member->kids.push_back(mkName(conv->name, e));
        e.kind = Expr::Kind::Call;
        e.text.clear();
        e.ival = 0;
        e.kids.clear();
        e.kids.push_back(std::move(member));
        e.checked.reset();
    }

    ET checkExpr(Expr& e) {
        if (!e.file.empty()) file_ = e.file;
        {
            auto cached = etCache_.find(&e);
            if (cached != etCache_.end()) {
                ET r = std::move(cached->second);
                etCache_.erase(cached);
                return r;
            }
        }
        rewriteOperator(e);
        ET r = checkExprBody(e);
        if (r.type) e.checked = cloneType(*r.type);
        return r;
    }

    ET checkBin(Expr& e) {
        if (e.kids.size() < 2 || !e.kids[0] || !e.kids[1]) return bad(e.line, e.col);
        convertIfNeeded(*e.kids[0]);
        convertIfNeeded(*e.kids[1]);
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
        int aw = fpWidthOf(a.type.get());
        int bw = fpWidthOf(b.type.get());
        if ((aw || bw) && (op == "+" || op == "-" || op == "*" || op == "/" || op == "<" || op == ">" || op == "<=" ||
                           op == ">=" || op == "==" || op == "!=")) {
            bool aok = aw || isIntish(*a.type);
            bool bok = bw || isIntish(*b.type);
            if (aok && bok) {
                if (op == "+" || op == "-" || op == "*" || op == "/") {
                    int w = (aw == 8 || bw == 8) ? 8 : 4;
                    return result(prim(w == 8 ? Type::Kind::Double : Type::Kind::Float));
                }
                return result(prim(Type::Kind::Int));
            }
        }
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
        if (!lhs.bad && lhs.type && lhs.type->kind != Type::Kind::Struct) convertIfNeeded(*e.kids[1]);
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

    std::unique_ptr<Expr> mkE(Expr::Kind k, const Expr& at) {
        auto n = std::make_unique<Expr>();
        n->kind = k;
        n->line = at.line;
        n->col = at.col;
        n->file = at.file;
        return n;
    }
    std::unique_ptr<Expr> mkName(const std::string& text, const Expr& at) {
        auto n = mkE(Expr::Kind::Name, at);
        n->text = text;
        return n;
    }

    // Score how well an argument fits a parameter: 0 exact, 1 by conversion, -1 not at all.
    int fitScore(const Type& param, const ET& arg) {
        if (arg.bad || !arg.type) return 0;
        if (param.kind == Type::Kind::Reference && param.inner) {
            if (!arg.lvalue) return -1;
            if (sameType(*param.inner, *arg.type)) return 0;
            return derivedFrom(*arg.type, *param.inner) ? 1 : -1;
        }
        if (sameType(param, *arg.type)) return 0;
        return canAssign(param, arg) ? 1 : -1;
    }

    // Choose among overloads by argument count, then by how well the argument types fit.
    // 'firstArg' is the index in call.kids of the first user argument.
    const Param* pickOverload(const std::vector<const Param*>& cands, Expr& call, std::size_t firstArg) {
        std::size_t nargs = call.kids.size() - firstArg;
        std::vector<const Param*> same;
        for (const Param* c : cands) {
            if (!c->type) continue;
            if (c->type->params.size() == nargs || (c->type->variadic && nargs >= c->type->params.size()))
                same.push_back(c);
        }
        if (same.empty()) return cands.empty() ? nullptr : cands[0];
        if (same.size() == 1) return same[0];
        std::vector<ET> ets;
        for (std::size_t i = 0; i < nargs; ++i) ets.push_back(decay(checkExpr(*call.kids[firstArg + i])));
        const Param* best = nullptr;
        int bestScore = 1 << 30;
        for (const Param* c : same) {
            int total = 0;
            bool ok = true;
            for (std::size_t i = 0; i < nargs && i < c->type->params.size(); ++i) {
                if (!c->type->params[i].type) continue;
                int sc = fitScore(*c->type->params[i].type, ets[i]);
                if (sc < 0) {
                    ok = false;
                    break;
                }
                total += sc;
            }
            if (ok && total < bestScore) {
                bestScore = total;
                best = c;
            }
        }
        return best ? best : same[0];
    }

    // ((R(*)(T*, ...))(*(void***)self)[slot]): the Itanium C++ ABI virtual call sequence.
    std::unique_ptr<Expr> vcallee(const Expr& at, std::unique_ptr<Expr> self, int slot, const std::string& tag,
                                  const Type& userFn) {
        auto cast1 = mkE(Expr::Kind::Cast, at);
        cast1->ty = ptrTo(ptrTo(ptrTo(prim(Type::Kind::Void))));
        cast1->kids.push_back(std::move(self));
        auto deref = mkE(Expr::Kind::Unary, at);
        deref->text = "*";
        deref->kids.push_back(std::move(cast1));
        auto idx = mkE(Expr::Kind::Index, at);
        idx->kids.push_back(std::move(deref));
        auto n = mkE(Expr::Kind::Int, at);
        n->ival = static_cast<std::uint64_t>(slot);
        idx->kids.push_back(std::move(n));
        auto fn = prim(Type::Kind::Function);
        fn->inner = userFn.inner ? cloneType(*userFn.inner) : prim(Type::Kind::Void);
        Param me;
        me.name = "this";
        auto st = prim(Type::Kind::Struct);
        st->tag = tag;
        me.type = ptrTo(std::move(st));
        fn->params.push_back(std::move(me));
        for (const Param& q : userFn.params) {
            Param c;
            c.name = q.name;
            if (q.type) c.type = cloneType(*q.type);
            fn->params.push_back(std::move(c));
        }
        auto cast2 = mkE(Expr::Kind::Cast, at);
        cast2->ty = ptrTo(std::move(fn));
        cast2->kids.push_back(std::move(idx));
        return cast2;
    }

    // Match a prototype type (with "\x02T" placeholders) against an argument type and bind T.
    bool deduceType(const Type& pat, const Type& arg, const TemplateSig& sig,
                    std::vector<std::unique_ptr<Type>>& bound) {
        if (pat.kind == Type::Kind::Struct && !pat.tag.empty() && pat.tag[0] == '\x02') {
            for (std::size_t i = 0; i < sig.names.size(); ++i) {
                if (sig.names[i] != pat.tag.substr(1)) continue;
                std::unique_ptr<Type> a = cloneType(arg);
                if (a->kind == Type::Kind::Array && a->inner) a = ptrTo(std::move(a->inner));
                if (!bound[i]) {
                    bound[i] = std::move(a);
                    return true;
                }
                return sameType(*bound[i], *a);
            }
            return true;
        }
        if (pat.kind == Type::Kind::Pointer && pat.inner) {
            if ((arg.kind == Type::Kind::Pointer || arg.kind == Type::Kind::Array) && arg.inner)
                return deduceType(*pat.inner, *arg.inner, sig, bound);
            return true;
        }
        if (pat.kind == Type::Kind::Reference && pat.inner) return deduceType(*pat.inner, arg, sig, bound);
        if (pat.kind == Type::Kind::Array && pat.inner) {
            if ((arg.kind == Type::Kind::Pointer || arg.kind == Type::Kind::Array) && arg.inner)
                return deduceType(*pat.inner, *arg.inner, sig, bound);
            return true;
        }
        return true;
    }

    ET checkCall(Expr& e) {
        if (e.kids.empty() || !e.kids[0]) return bad(e.line, e.col);
        Expr& c0 = *e.kids[0];
        static const std::string kCtor = "\x01" "ctor:";
        static const std::string kTmpl = "\x01" "tmpl:";
        if (c0.kind == Expr::Kind::Name && c0.text.compare(0, kTmpl.size(), kTmpl) == 0) {
            std::string name = c0.text.substr(kTmpl.size());
            auto sit = funcTemplateSigs().find(name);
            if (sit == funcTemplateSigs().end() || !sit->second.fn) {
                err(e.line, e.col, "unknown function template '" + name + "'");
                return bad(e.line, e.col);
            }
            const TemplateSig& sig = sit->second;
            if (sig.fn->params.size() != e.kids.size() - 1) {
                err(e.line, e.col, "wrong number of arguments");
                return bad(e.line, e.col);
            }
            std::vector<std::unique_ptr<Type>> bound(sig.names.size());
            for (std::size_t i = 0; i < sig.fn->params.size(); ++i) {
                ET a = checkExpr(*e.kids[i + 1]);
                if (a.bad || !a.type) return bad(e.line, e.col);
                if (sig.fn->params[i].type && !deduceType(*sig.fn->params[i].type, *a.type, sig, bound)) {
                    err(e.line, e.col, "conflicting types for a template argument");
                    return bad(e.line, e.col);
                }
            }
            std::vector<TArg> args;
            for (std::size_t i = 0; i < sig.names.size(); ++i) {
                if (!bound[i]) {
                    err(e.line, e.col, "cannot deduce template argument '" + sig.names[i] + "'");
                    return bad(e.line, e.col);
                }
                TArg a;
                a.type = std::move(bound[i]);
                args.push_back(std::move(a));
            }
            std::string inst = instFuncName(name, sig, args);
            bool known = false;
            for (const InstReq& r : instRequests())
                if (r.name == inst) known = true;
            if (!known) {
                InstReq r;
                r.tmpl = name;
                r.name = inst;
                r.args = std::move(args);
                instRequests().push_back(std::move(r));
                instDirty() = true;
            }
            c0.text = inst;
        } else if (c0.kind == Expr::Kind::Name && c0.text.compare(0, kCtor.size(), kCtor) == 0) {
            std::string tag = c0.text.substr(kCtor.size());
            auto it = canonStructs().find(tag);
            std::vector<const Param*> cands;
            if (it != canonStructs().end() && it->second)
                for (const Param& p : it->second->params)
                    if (p.name == "<ctor>") cands.push_back(&p);
            if (cands.empty()) {
                err(e.line, e.col, "'" + tag + "' has no constructor");
                return bad(e.line, e.col);
            }
            const Param* pick = pickOverload(cands, e, 2);
            c0.text = pick->mangled;
        } else if (c0.kind == Expr::Kind::Name && c0.text.compare(0, 7, "\x01" "cinit:") == 0 && e.kids.size() == 3) {
            // T x = y: the copy constructor when y is an lvalue of class T, else a plain copy of
            // the bytes (y is a temporary: the object is built in place).
            std::string tag = c0.text.substr(7);
            ET src = checkExpr(*e.kids[2]);
            if (src.bad) return bad(e.line, e.col);
            if (e.kids[2]->kind == Expr::Kind::Comma && e.kids[2]->ival == 9) {
                e.kids[2]->ival = 8;  // built in place
                eraseTemp(e.kids[2]->text);
            }
            bool useCtor = src.lvalue && src.type && src.type->kind == Type::Kind::Struct && src.type->tag == tag;
            if (useCtor) {
                e.kids[0]->text = "\x01" "ctor:" + tag;
                return checkCall(e);
            }
            auto self = std::move(e.kids[1]);
            auto rhs = std::move(e.kids[2]);
            // self is "&name": assign to the name itself.
            std::unique_ptr<Expr> target = std::move(self->kids[0]);
            e.kind = Expr::Kind::Assign;
            e.text = "=";
            e.ival = 3;  // an initialization: operator= must not be used
            e.kids.clear();
            e.kids.push_back(std::move(target));
            e.kids.push_back(std::move(rhs));
            return checkExpr(e);
        } else if (c0.kind == Expr::Kind::Name && c0.text == "\x01" "delete[]" && e.kids.size() == 2 && e.kids[1]) {
            // delete[] p: a class with a destructor goes through __vdel_ (count in front of the
            // block); anything else is just freed.
            ET a = decay(checkExpr(*e.kids[1]));
            if (a.bad || !a.type) return bad(e.line, e.col);
            const Type* cls = isPtr(*a.type) && a.type->inner ? canonOf(a.type->inner.get()) : nullptr;
            const Param* d1 = cls && cls->kind == Type::Kind::Struct ? findSpecial(cls, "<dtor>") : nullptr;
            if (d1) c0.text = "__vdel_" + mangleNested(cls->tag);
            else c0.text = "\x01" "delete";
            return checkCall(e);
        } else if (c0.kind == Expr::Kind::Name && c0.text == "\x01" "delete" && e.kids.size() == 2 && e.kids[1]) {
            ET a = decay(checkExpr(*e.kids[1]));
            if (a.bad || !a.type) return bad(e.line, e.col);
            const Type* cls = isPtr(*a.type) && a.type->inner ? canonOf(a.type->inner.get()) : nullptr;
            const Param* d1 = cls && cls->kind == Type::Kind::Struct ? findSpecial(cls, "<dtor>") : nullptr;
            auto freeCall = [&](const Expr& at, std::unique_ptr<Expr> arg) {
                auto cast = mkE(Expr::Kind::Cast, at);
                cast->ty = ptrTo(prim(Type::Kind::Char));
                cast->kids.push_back(std::move(arg));
                auto call = mkE(Expr::Kind::Call, at);
                call->kids.push_back(mkName("free", at));
                call->kids.push_back(std::move(cast));
                return call;
            };
            if (!d1) {
                c0.text = "free";
                auto cast = mkE(Expr::Kind::Cast, e);
                cast->ty = ptrTo(prim(Type::Kind::Char));
                cast->kids.push_back(std::move(e.kids[1]));
                e.kids[1] = std::move(cast);
            } else {
                if (hasSideEffects(*e.kids[1])) {
                    err(e.line, e.col, "delete of an expression with side effects is not supported");
                    return bad(e.line, e.col);
                }
                auto self = [&]() { return cloneExpr(*e.kids[1]); };
                auto work = mkE(Expr::Kind::Comma, e);
                if (d1->vslot >= 0) {
                    const Param* d0 = findSpecial(cls, "<dtor0>");
                    auto call = mkE(Expr::Kind::Call, e);
                    call->kids.push_back(vcallee(e, self(), d0->vslot, a.type->inner->tag, *d0->type));
                    call->kids.push_back(self());
                    work->kids.push_back(std::move(call));
                } else {
                    auto call = mkE(Expr::Kind::Call, e);
                    call->kids.push_back(mkName(d1->mangled, e));
                    call->kids.push_back(self());
                    work->kids.push_back(std::move(call));
                    work->kids.push_back(freeCall(e, self()));
                }
                auto zero = mkE(Expr::Kind::Int, e);
                work->kids.push_back(std::move(zero));
                auto cond = mkE(Expr::Kind::Cond, e);
                cond->kids.push_back(self());
                cond->kids.push_back(std::move(work));
                cond->kids.push_back(mkE(Expr::Kind::Int, e));
                e.kind = Expr::Kind::Cond;
                e.text.clear();
                e.kids = std::move(cond->kids);
                return checkExprBody(e);
            }
        } else if (c0.kind == Expr::Kind::Name && method_ && c0.text.find("::") == std::string::npos) {
            const Type* st = canonOf(method_);
            bool isMethod = false;
            if (st)
                for (const Param& f : st->params)
                    if (f.name == c0.text && !f.mangled.empty() && !isSpecialName(f.name)) isMethod = true;
            if (isMethod) {
                std::string fname = c0.text;
                c0.kind = Expr::Kind::Member;
                c0.text = "->";
                c0.kids.clear();
                c0.kids.push_back(mkName("this", c0));
                c0.kids.push_back(mkName(fname, c0));
            }
        } else if (c0.kind == Expr::Kind::Name && method_ && c0.text.find("::") != std::string::npos) {
            std::size_t cc = c0.text.rfind("::");
            std::string prefix = c0.text.substr(0, cc);
            std::string fname = c0.text.substr(cc + 2);
            auto it = canonStructs().find(prefix);
            if (it == canonStructs().end() && !inNs_.empty()) it = canonStructs().find(inNs_ + "::" + prefix);
            if (it != canonStructs().end() && it->second) {
                std::vector<const Param*> cands;
                for (const Param& f : it->second->params)
                    if (f.name == fname && !f.mangled.empty() && !isSpecialName(f.name)) cands.push_back(&f);
                if (!cands.empty()) {
                    const Param* pick = pickOverload(cands, e, 1);
                    c0.text = pick->mangled;
                    e.kids.insert(e.kids.begin() + 1, mkName("this", e));
                }
            }
        }
        if (e.kids[0]->kind == Expr::Kind::Member && e.kids[0]->kids.size() >= 2 && e.kids[0]->kids[0] &&
            e.kids[0]->kids[1]) {
            Expr& mem = *e.kids[0];
            ET obj = mem.text == "->" ? decay(checkExpr(*mem.kids[0])) : checkExpr(*mem.kids[0]);
            const Type* st = obj.type.get();
            if (mem.text == "->" && st && isPtr(*st) && st->inner) st = st->inner.get();
            st = canonOf(st);
            if (st) {
                std::vector<const Param*> cands;
                for (const Param& f : st->params)
                    if (f.name == mem.kids[1]->text && !f.mangled.empty() && !isSpecialName(f.name))
                        cands.push_back(&f);
                if (!cands.empty()) {
                    const Param* fp = pickOverload(cands, e, 1);
                    const Param& f = *fp;
                    std::unique_ptr<Expr> selfArg;
                    if (mem.text == "->") {
                        selfArg = std::move(mem.kids[0]);
                    } else {
                        selfArg = std::make_unique<Expr>();
                        selfArg->kind = Expr::Kind::Unary;
                        selfArg->text = "&";
                        selfArg->line = mem.line;
                        selfArg->col = mem.col;
                        selfArg->kids.push_back(std::move(mem.kids[0]));
                    }
                    std::unique_ptr<Expr> callee;
                    if (f.vslot >= 0) {
                        if (hasSideEffects(*selfArg)) {
                            err(e.line, e.col, "a virtual call on an expression with side effects is not supported");
                            return bad(e.line, e.col);
                        }
                        callee = vcallee(e, cloneExpr(*selfArg), f.vslot, st->tag, *f.type);
                    } else {
                        callee = std::make_unique<Expr>();
                        callee->kind = Expr::Kind::Name;
                        callee->text = f.mangled;
                        callee->line = e.line;
                        callee->col = e.col;
                    }
                    std::vector<std::unique_ptr<Expr>> kids;
                    kids.push_back(std::move(callee));
                    kids.push_back(std::move(selfArg));
                    for (std::size_t i = 1; i < e.kids.size(); ++i) kids.push_back(std::move(e.kids[i]));
                    e.kids = std::move(kids);
                }
            }
        }
        ET callee = decay(checkExpr(*e.kids[0]));
        if (callee.bad || !callee.type) return bad(e.line, e.col);
        if (!isPtr(*callee.type) || !callee.type->inner || callee.type->inner->kind != Type::Kind::Function) {
            err(e.line, e.col, "called expression is not a function");
            return bad(e.line, e.col);
        }
        const Type& fn = *callee.type->inner;
        std::size_t nargs = e.kids.size() - 1;
        if (fn.variadic) {
            if (nargs < fn.params.size()) {
                err(e.line, e.col, "too few arguments");
                return bad(e.line, e.col);
            }
        } else if (nargs != fn.params.size()) {
            err(e.line, e.col, "wrong number of arguments");
            return bad(e.line, e.col);
        }
        for (std::size_t i = 0; i < fn.params.size(); ++i) {
            if (fn.params[i].type && fn.params[i].type->kind == Type::Kind::Reference && fn.params[i].type->inner) {
                // A reference parameter receives the address of an lvalue of the referred type.
                if (e.kids[i + 1] && e.kids[i + 1]->ival == 1 &&
                    (e.kids[i + 1]->kind == Expr::Kind::Unary || e.kids[i + 1]->kind == Expr::Kind::Comma)) {
                    // Already wrapped by an earlier check of this call.
                    if (checkExpr(*e.kids[i + 1]).bad) return bad(e.line, e.col);
                    continue;
                }
                ET src = checkExpr(*e.kids[i + 1]);
                if (src.bad) return bad(e.line, e.col);
                const Type& want = *fn.params[i].type->inner;
                if (!src.lvalue) {
                    // A reference to const may bind to a temporary: copy the value into a hidden
                    // local and pass its address.
                    if (!src.type || !sameType(want, *src.type)) {
                        err(e.line, e.col, "a reference argument must be an lvalue");
                        return bad(e.line, e.col);
                    }
                    std::unique_ptr<Expr> first;
                    std::unique_ptr<Expr> addr;
                    if (e.kids[i + 1]->kind == Expr::Kind::Comma && e.kids[i + 1]->ival == 9) {
                        // A T(args) temporary already has a name and a destructor call: bind to it.
                        std::string existing = e.kids[i + 1]->text;
                        first = std::move(e.kids[i + 1]);
                        addr = eUnary("&", mkName(existing, e), e);
                    } else {
                        std::string tmp = newTemp(*src.type, e, true);
                        first = withFlag(eAssignNode(mkName(tmp, e), std::move(e.kids[i + 1]), e), e);
                        addr = eUnary("&", mkName(tmp, e), e);
                    }
                    auto both = eCommaNode(std::move(first), std::move(addr), e);
                    both->ival = 1;
                    e.kids[i + 1] = std::move(both);
                    if (checkExpr(*e.kids[i + 1]).bad) return bad(e.line, e.col);
                    continue;
                }
                if (src.type && !sameType(want, *src.type) && !derivedFrom(*src.type, want)) {
                    err(e.line, e.col, "cannot bind this reference argument");
                    return bad(e.line, e.col);
                }
                auto wrapped = std::make_unique<Expr>();
                wrapped->kind = Expr::Kind::Unary;
                wrapped->text = "&";
                wrapped->ival = 1;
                wrapped->line = e.kids[i + 1]->line;
                wrapped->col = e.kids[i + 1]->col;
                wrapped->file = e.kids[i + 1]->file;
                wrapped->kids.push_back(std::move(e.kids[i + 1]));
                e.kids[i + 1] = std::move(wrapped);
                ET ptr = checkExpr(*e.kids[i + 1]);
                if (ptr.bad) return bad(e.line, e.col);
                continue;
            }
            if (fn.params[i].type && fn.params[i].type->kind != Type::Kind::Struct) convertIfNeeded(*e.kids[i + 1]);
            ET arg = decay(checkExpr(*e.kids[i + 1]));
            if (arg.bad) return bad(e.line, e.col);
            if (fn.params[i].type && fn.params[i].type->kind == Type::Kind::Struct && arg.type &&
                sameType(*fn.params[i].type, *arg.type)) {
                Expr& argExpr = *e.kids[i + 1];
                if (argExpr.kind == Expr::Kind::Comma && argExpr.ival == 9) {
                    // A T(args) temporary passed by value initializes the parameter directly.
                    argExpr.ival = 8;
                    eraseTemp(argExpr.text);
                } else if (arg.lvalue && (hasUserCopy(*arg.type) || needsDtor(*arg.type))) {
                    // The caller copies the object into a hidden local (by the copy constructor, or
                    // bytewise) and destroys that copy after the statement.
                    std::string tmp = newTemp(*arg.type, e, true);
                    std::unique_ptr<Expr> made;
                    if (hasUserCopy(*arg.type)) {
                        auto ctorName = mkName("\x01" "ctor:" + arg.type->tag, e);
                        std::vector<std::unique_ptr<Expr>> kids;
                        kids.push_back(std::move(ctorName));
                        kids.push_back(eUnary("&", mkName(tmp, e), e));
                        kids.push_back(std::move(e.kids[i + 1]));
                        auto call = mkE(Expr::Kind::Call, e);
                        call->kids = std::move(kids);
                        made = std::move(call);
                    } else {
                        made = eAssignNode(mkName(tmp, e), std::move(e.kids[i + 1]), e);
                    }
                    made = withFlag(std::move(made), e);
                    e.kids[i + 1] = eCommaNode(std::move(made), mkName(tmp, e), e);
                    arg = decay(checkExpr(*e.kids[i + 1]));
                    if (arg.bad) return bad(e.line, e.col);
                }
            }
            if (fn.params[i].type && !canAssign(*fn.params[i].type, arg)) {
                err(e.line, e.col, "cannot assign argument");
                return bad(e.line, e.col);
            }
        }
        for (std::size_t i = fn.params.size(); i < nargs; ++i) {
            ET arg = decay(checkExpr(*e.kids[i + 1]));
            if (arg.bad) return bad(e.line, e.col);
            if (arg.type && (arg.type->kind == Type::Kind::Char || arg.type->kind == Type::Kind::Short))
                e.kids[i + 1]->checked = prim(Type::Kind::Int);
        }
        ET r;
        r.line = e.line;
        r.col = e.col;
        r.type = fn.inner ? cloneType(*fn.inner) : prim(Type::Kind::Int);
        if (r.type && r.type->kind == Type::Kind::Reference && r.type->inner && e.ival == 2) {
            auto ptrTy = std::make_unique<Type>();
            ptrTy->kind = Type::Kind::Pointer;
            ptrTy->inner = cloneType(*r.type->inner);
            r.type = std::move(ptrTy);
        } else if (r.type && r.type->kind == Type::Kind::Reference && r.type->inner) {
            // A call that returns a reference is the pointer it returns, dereferenced: the call
            // moves into a child and this node becomes "*call".
            auto ptrTy = std::make_unique<Type>();
            ptrTy->kind = Type::Kind::Pointer;
            ptrTy->inner = cloneType(*r.type->inner);
            auto inner = std::make_unique<Expr>();
            inner->kind = e.kind;
            inner->text = e.text;
            inner->ival = 2;
            inner->line = e.line;
            inner->col = e.col;
            inner->file = e.file;
            inner->kids = std::move(e.kids);
            inner->checked = std::move(ptrTy);
            e.kids.clear();
            e.kind = Expr::Kind::Unary;
            e.text = "*";
            e.kids.push_back(std::move(inner));
            r.type = cloneType(*r.type->inner);
            r.lvalue = true;
            r.mod = true;
        }
        return r;
    }

    void checkVar(Decl& d) {
        if (!d.type) return;
        if (!d.vt.empty()) return;  // a vtable: already declared
        if (d.type->kind == Type::Kind::Reference) {
            if (!d.init || !d.type->inner) {
                err(d.line, d.col, "a reference must be initialized");
            } else {
                ET src = checkExpr(*d.init);
                if (!src.bad && !src.lvalue) err(d.line, d.col, "a reference must bind to an lvalue");
                if (!src.bad && src.type && !sameType(*d.type->inner, *src.type) && !derivedFrom(*src.type, *d.type->inner))
                    err(d.line, d.col, "cannot bind this reference");
            }
            add(d.name, cloneType(*d.type), false, d.line, d.col);
            return;
        }
        if (d.type->kind == Type::Kind::Void ||
            (d.type->kind == Type::Kind::Array && d.type->inner && d.type->inner->kind == Type::Kind::Void)) {
            err(d.line, d.col, "variable has type void");
        }
        if (d.init && d.init->kind == Expr::Kind::Brace) {
            for (auto& k : d.init->kids)
                if (k) checkExpr(*k);
        } else if (d.init && d.init->kind == Expr::Kind::String && d.type->kind == Type::Kind::Array) {
            checkExpr(*d.init);
        } else if (d.init) {
            if (d.type->kind != Type::Kind::Struct) convertIfNeeded(*d.init);
            ET src = decay(checkExpr(*d.init));
            if (d.init->kind == Expr::Kind::Comma && d.init->ival == 9) {
                d.init->ival = 8;  // T x = T(args): built in place, the temporary is not destroyed
                eraseTemp(d.init->text);
            }
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
        const Type* savedMethod = method_;
        std::string savedNs = inNs_;
        ret_ = d.type->inner.get();
        method_ = nullptr;
        inNs_.clear();
        {
            std::size_t cut = d.name.rfind("::");
            if (cut != std::string::npos) inNs_ = d.name.substr(0, cut);
        }
        if (!d.methodTag.empty()) {
            auto it = canonStructs().find(d.methodTag);
            if (it != canonStructs().end()) method_ = it->second.get();
        }
        for (const Param& p : d.type->params) {
            if (p.name.empty() || !p.type) continue;
            add(p.name, cloneType(*p.type), false, d.line, d.col);
        }
        if (d.body->kind == Stmt::Kind::Block) {
            checkStmts(*d.body);
        } else {
            checkStmt(*d.body);
        }
        ret_ = saved;
        method_ = savedMethod;
        inNs_ = savedNs;
        scopes_.pop_back();
    }

    void checkCond(Expr& e) {
        convertIfNeeded(e);
        ET c = decay(checkExpr(e));
        if (!c.bad && c.type && !scalar(*c.type)) err(e.line, e.col, "condition is not a scalar");
    }

    // A single statement that is the body of if, while or for becomes a block, so that hidden
    // temporaries made inside it are destroyed inside it.
    static void blockify(std::unique_ptr<Stmt>& body) {
        if (!body || body->kind == Stmt::Kind::Block) return;
        auto blk = std::make_unique<Stmt>();
        blk->kind = Stmt::Kind::Block;
        blk->line = body->line;
        blk->col = body->col;
        blk->file = body->file;
        blk->stmts.push_back(std::move(body));
        body = std::move(blk);
    }

    // Declarations of the hidden locals and their flags (the flag starts at 0).
    void declareTemps(std::vector<std::unique_ptr<Stmt>>& out, std::vector<TempInfo>& temps) {
        for (TempInfo& t : temps) {
            if (t.type) {
                auto d = std::make_unique<Decl>();
                d->name = t.name;
                d->line = t.pos.line;
                d->col = t.pos.col;
                d->file = t.pos.file;
                d->type = std::move(t.type);
                auto ds = std::make_unique<Stmt>();
                ds->kind = Stmt::Kind::Decl;
                ds->line = d->line;
                ds->col = d->col;
                ds->decl = std::move(d);
                out.push_back(std::move(ds));
            }
            if (!t.flag.empty()) {
                auto d = std::make_unique<Decl>();
                d->name = t.flag;
                d->line = t.pos.line;
                d->col = t.pos.col;
                d->file = t.pos.file;
                d->type = std::make_unique<Type>();
                d->init = std::make_unique<Expr>();
                d->init->kind = Expr::Kind::Int;
                d->init->ival = 0;
                auto ds = std::make_unique<Stmt>();
                ds->kind = Stmt::Kind::Decl;
                ds->line = d->line;
                ds->col = d->col;
                ds->decl = std::move(d);
                out.push_back(std::move(ds));
            }
        }
    }
    // "if (flag) dtor(&tmp);" for each temporary that has a destructor, latest first.
    std::vector<std::unique_ptr<Stmt>> dtorStmts(std::vector<TempInfo>& temps, int line, int col,
                                                 const std::string& file) {
        std::vector<std::unique_ptr<Stmt>> dtors;
        for (std::size_t i = temps.size(); i-- > 0;) {
            if (temps[i].tag.empty()) continue;
            auto it = canonStructs().find(temps[i].tag);
            const Param* dp = it == canonStructs().end() ? nullptr : findSpecial(it->second.get(), "<dtor>");
            if (!dp) continue;
            Expr at;
            at.line = line;
            at.col = col;
            at.file = file;
            auto call = mkE(Expr::Kind::Call, at);
            call->kids.push_back(mkName(dp->mangled, at));
            call->kids.push_back(eUnary("&", mkName(temps[i].name, at), at));
            auto es = std::make_unique<Stmt>();
            es->kind = Stmt::Kind::Expr;
            es->line = line;
            es->col = col;
            es->file = file;
            es->expr = std::move(call);
            std::unique_ptr<Stmt> last = std::move(es);
            if (!temps[i].flag.empty()) {
                auto g = std::make_unique<Stmt>();
                g->kind = Stmt::Kind::If;
                g->line = line;
                g->col = col;
                g->file = file;
                g->expr = mkName(temps[i].flag, at);
                g->thenS = std::move(last);
                last = std::move(g);
            }
            checkStmt(*last);
            dtors.push_back(std::move(last));
        }
        return dtors;
    }

    // Puts the hidden locals' declarations before 'st' and the destructor calls after it (before
    // a return, with the returned value kept in another hidden local).
    void flushTemps(std::vector<std::unique_ptr<Stmt>>& out, std::unique_ptr<Stmt> st, std::vector<TempInfo>& temps) {
        declareTemps(out, temps);
        std::vector<std::unique_ptr<Stmt>> dtors = dtorStmts(temps, st->line, st->col, st->file);
        if (dtors.empty()) {
            out.push_back(std::move(st));
            return;
        }
        if (st->kind == Stmt::Kind::Return && st->expr && ret_ && ret_->kind != Type::Kind::Void &&
            ret_->kind != Type::Kind::Reference) {
            // return e; the value is computed, then the temporaries die, then the function returns.
            std::string rv = "__crv" + std::to_string(tempN_++);
            auto d = std::make_unique<Decl>();
            d->name = rv;
            d->line = st->line;
            d->col = st->col;
            d->file = st->file;
            d->type = cloneType(*ret_);
            d->init = std::move(st->expr);
            add(rv, cloneType(*ret_), false, st->line, st->col);
            auto ds = std::make_unique<Stmt>();
            ds->kind = Stmt::Kind::Decl;
            ds->line = st->line;
            ds->col = st->col;
            ds->decl = std::move(d);
            out.push_back(std::move(ds));
            for (auto& dt : dtors) out.push_back(std::move(dt));
            Expr at;
            at.line = st->line;
            at.col = st->col;
            at.file = st->file;
            st->expr = mkName(rv, at);
            st->expr->checked = cloneType(*ret_);
            out.push_back(std::move(st));
            return;
        }
        out.push_back(std::move(st));
        for (auto& dt : dtors) out.push_back(std::move(dt));
    }

    // The statements of a block, in the scope the caller opened.
    void checkStmts(Stmt& block) {
        std::vector<std::unique_ptr<Stmt>> out;
        for (auto& st : block.stmts) {
            if (!st) continue;
            std::vector<TempInfo> outer = std::move(temps_);
            temps_.clear();
            checkStmt(*st);
            std::vector<TempInfo> mine = std::move(temps_);
            temps_ = std::move(outer);
            if (mine.empty()) {
                out.push_back(std::move(st));
                continue;
            }
            flushTemps(out, std::move(st), mine);
        }
        block.stmts = std::move(out);
    }

    void checkStmt(Stmt& s) {
        if (!s.file.empty()) file_ = s.file;
        switch (s.kind) {
        case Stmt::Kind::Block: {
            scopes_.emplace_back();
            checkStmts(s);
            scopes_.pop_back();
            break;
        }
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
            } else if (ret_->kind == Type::Kind::Reference && ret_->inner) {
                ET v = checkExpr(*s.expr);
                if (!v.bad && (!v.lvalue || !v.type ||
                               (!sameType(*ret_->inner, *v.type) && !derivedFrom(*v.type, *ret_->inner))))
                    err(s.line, s.col, "return type does not match");
            } else {
                if (ret_->kind != Type::Kind::Struct) convertIfNeeded(*s.expr);
                ET v = decay(checkExpr(*s.expr));
                if (s.expr->kind == Expr::Kind::Comma && s.expr->ival == 9) {
                    s.expr->ival = 8;  // returned: it is the result object
                    eraseTemp(s.expr->text);
                }
                if (!v.bad && !canAssign(*ret_, v)) err(s.line, s.col, "return type does not match");
            }
            break;
        case Stmt::Kind::If: {
            blockify(s.thenS);
            blockify(s.elseS);
            std::vector<TempInfo> outer = std::move(temps_);
            temps_.clear();
            if (s.expr) checkCond(*s.expr);
            std::vector<TempInfo> condTemps = std::move(temps_);
            temps_ = std::move(outer);
            if (s.thenS) checkStmt(*s.thenS);
            if (s.elseS) checkStmt(*s.elseS);
            if (!condTemps.empty() && s.expr) {
                // The condition's temporaries die before either branch runs:
                // { decls; int c = cond; dtors; if (c) then else }.
                Expr at;
                at.line = s.line;
                at.col = s.col;
                at.file = s.file;
                auto inner = std::make_unique<Stmt>();
                inner->kind = Stmt::Kind::If;
                inner->line = s.line;
                inner->col = s.col;
                inner->file = s.file;
                std::string cv = "__cc" + std::to_string(tempN_++);
                inner->expr = mkName(cv, at);
                inner->expr->checked = prim(Type::Kind::Long);
                inner->thenS = std::move(s.thenS);
                inner->elseS = std::move(s.elseS);
                std::vector<std::unique_ptr<Stmt>> parts;
                declareTemps(parts, condTemps);
                auto d = std::make_unique<Decl>();
                d->name = cv;
                d->line = s.line;
                d->col = s.col;
                d->file = s.file;
                d->type = prim(Type::Kind::Long);  // wide enough for a pointer condition
                d->init = std::move(s.expr);
                auto ds = std::make_unique<Stmt>();
                ds->kind = Stmt::Kind::Decl;
                ds->line = s.line;
                ds->col = s.col;
                ds->decl = std::move(d);
                parts.push_back(std::move(ds));
                for (auto& dt : dtorStmts(condTemps, s.line, s.col, s.file)) parts.push_back(std::move(dt));
                parts.push_back(std::move(inner));
                s.kind = Stmt::Kind::Block;
                s.stmts = std::move(parts);
                s.expr.reset();
            }
            break;
        }
        case Stmt::Kind::While: {
            blockify(s.body);
            std::vector<TempInfo> outer = std::move(temps_);
            temps_.clear();
            if (s.expr) checkCond(*s.expr);
            std::vector<TempInfo> condTemps = std::move(temps_);
            temps_ = std::move(outer);
            ++loops_;
            if (s.body) checkStmt(*s.body);
            --loops_;
            if (!condTemps.empty() && s.expr && s.body) {
                // Temporaries of the condition live for one test: while (1) { decls; if (!cond) {
                // dtors; break; } dtors; body }.
                auto blk = std::make_unique<Stmt>();
                blk->kind = Stmt::Kind::Block;
                blk->line = s.line;
                blk->col = s.col;
                blk->file = s.file;
                declareTemps(blk->stmts, condTemps);
                Expr at;
                at.line = s.line;
                at.col = s.col;
                at.file = s.file;
                auto notc = eUnary("!", std::move(s.expr), at);
                notc->checked = prim(Type::Kind::Int);
                auto stop = std::make_unique<Stmt>();
                stop->kind = Stmt::Kind::If;
                stop->line = s.line;
                stop->col = s.col;
                stop->file = s.file;
                stop->expr = std::move(notc);
                auto exitBlk = std::make_unique<Stmt>();
                exitBlk->kind = Stmt::Kind::Block;
                exitBlk->line = s.line;
                exitBlk->file = s.file;
                for (auto& d : dtorStmts(condTemps, s.line, s.col, s.file)) exitBlk->stmts.push_back(std::move(d));
                auto brk = std::make_unique<Stmt>();
                brk->kind = Stmt::Kind::Break;
                brk->line = s.line;
                brk->file = s.file;
                exitBlk->stmts.push_back(std::move(brk));
                stop->thenS = std::move(exitBlk);
                blk->stmts.push_back(std::move(stop));
                for (auto& d : dtorStmts(condTemps, s.line, s.col, s.file)) blk->stmts.push_back(std::move(d));
                blk->stmts.push_back(std::move(s.body));
                s.body = std::move(blk);
                s.expr = mkE(Expr::Kind::Int, at);
                s.expr->ival = 1;
                s.expr->checked = prim(Type::Kind::Int);
            }
            break;
        }
        case Stmt::Kind::For:
            blockify(s.body);
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
        case Stmt::Kind::Goto:
        case Stmt::Kind::Label:
            break;
        }
    }
};

class Lowerer {
public:
    explicit Lowerer(Diagnostics& diags) : diags_(diags) {}

    Module run(Tu& tu) {
        for (const auto& kv : canonStructs())
            if (kv.second) layoutOf(kv.second.get());
        for (auto& d : tu.decls)
            if (d && d->function) noteFunc(*d);
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
        int bitOff = 0;
        int bitWidth = 0;
        bool bitUns = false;
        bool agg = false;
        std::string tag;
        std::vector<FieldOff> fields;
    };
    struct Val {
        Operand op;
        Operand hi;
        int step = 0;
        int inner = 0;
        bool pair = false;
        int fpw = 0;
        std::vector<FieldOff> fields;
        Val() = default;
        Val(Operand o, int s, int i) : op(std::move(o)), step(s), inner(i) {}
    };
    struct Addr {
        Operand op;
        int width = 8;
        int step = 0;
        int inner = 0;
        int bitOff = 0;
        int bitWidth = 0;
        bool bitUns = false;
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
        bool func = false;
        bool ref = false;
        bool boolean = false;
        int fpw = 0;
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
    bool retBool_ = false;
    bool retFp_ = false;
    int vaSave_ = -1;
    bool retUns_ = false;
    bool retStruct_ = false;
    bool retRef_ = false;
    int hiddenReg_ = -1;
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
        case Type::Kind::Int:
        case Type::Kind::Float: return 4;
        case Type::Kind::Double:
        case Type::Kind::Long:
        case Type::Kind::Pointer:
        case Type::Kind::Reference: return 8;
        case Type::Kind::Bool: return 1;
        case Type::Kind::Array: return alignOf(t->inner.get());
        case Type::Kind::Struct:
        case Type::Kind::Union: {
            int a = 1;
            t = canonOf(t);  // a use made before the class was complete has no fields of its own
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
        case Type::Kind::Char:
        case Type::Kind::Bool: return 1;
        case Type::Kind::Short: return 2;
        case Type::Kind::Int:
        case Type::Kind::Float: return 4;
        case Type::Kind::Double:
        case Type::Kind::Long:
        case Type::Kind::Pointer:
        case Type::Kind::Reference: return 8;
        case Type::Kind::Array: {
            int n = 0;
            if (t->bound && t->bound->kind == Expr::Kind::Int) n = static_cast<int>(t->bound->ival);
            return n * sizeOf(t->inner.get());
        }
        case Type::Kind::Struct: {
            auto fields = layoutOf(t);
            int end = 0;
            for (const FieldOff& f : fields) {
                int span = f.bitWidth > 0 ? 4 : f.width;
                int e = f.offset + span;
                if (e > end) end = e;
            }
            if (end == 0) return t->tag.empty() ? 0 : 1;
            return alignUp(end, alignOf(t));
        }
        case Type::Kind::Union: {
            int sz = 0;
            for (const Param& p : t->params) {
                int n = sizeOf(p.type.get());
                if (n > sz) sz = n;
            }
            if (sz == 0) return 0;
            return alignUp(sz, alignOf(t));
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
        if (!t || (t->kind != Type::Kind::Struct && t->kind != Type::Kind::Union)) return out;
        // A use made before the class was complete has no fields of its own; take the finished
        // type, unless this tag is being laid out right now (a pointer to itself).
        static std::set<std::string> inProgress;
        struct Mark {
            std::string tag;
            bool set = false;
            ~Mark() {
                if (set) inProgress.erase(tag);
            }
        } mark;
        if (t->params.empty() && !t->tag.empty() && !inProgress.count(t->tag)) t = canonOf(t);
        if (!t->tag.empty() && inProgress.insert(t->tag).second) {
            mark.tag = t->tag;
            mark.set = true;
        }
        if (t->kind == Type::Kind::Union) {
            for (const Param& p : t->params) {
                FieldOff f;
                f.name = p.name;
                f.offset = 0;
                f.width = sizeOf(p.type.get());
                f.step = valueStep(p.type.get());
                f.inner = innerOf(p.type.get());
                f.agg = p.type && (p.type->kind == Type::Kind::Struct || p.type->kind == Type::Kind::Union ||
                                   p.type->kind == Type::Kind::Array);
                if (p.type && (p.type->kind == Type::Kind::Struct || p.type->kind == Type::Kind::Union))
                    f.fields = layoutOf(p.type.get());
                out.push_back(std::move(f));
            }
            return out;
        }
        int off = 0;
        int bitCursor = -1;
        for (const Param& p : t->params) {
            if (!p.mangled.empty() || (p.type && p.type->kind == Type::Kind::Function)) continue;
            if (p.name == "__basepad") {
                // Itanium C++ ABI 2.4: a base that is POD for layout keeps its tail padding.
                if (bitCursor >= 0) {
                    off += 4;
                    bitCursor = -1;
                }
                off = alignUp(off, alignOf(p.type.get()));
                continue;
            }
            FieldOff f;
            f.name = p.name;
            if (p.bits >= 0) {
                int bits = p.bits;
                if (bits > 32) bits = 32;
                if (bitCursor < 0 || bitCursor + bits > 32) {
                    if (bitCursor >= 0) off += 4;
                    off = alignUp(off, 4);
                    bitCursor = 0;
                }
                f.offset = off;
                f.width = 4;
                f.bitOff = bitCursor;
                f.bitWidth = bits;
                f.bitUns = p.type && p.type->unsign;
                bitCursor += bits;
                out.push_back(std::move(f));
                continue;
            }
            if (bitCursor >= 0) {
                off += 4;
                bitCursor = -1;
            }
            off = alignUp(off, alignOf(p.type.get()));
            f.offset = off;
            f.width = sizeOf(p.type.get());
            f.step = valueStep(p.type.get());
            f.inner = innerOf(p.type.get());
            bool nested = p.type && p.type->kind == Type::Kind::Struct;
            bool arr = p.type && p.type->kind == Type::Kind::Array;
            bool pstruct = p.type && p.type->kind == Type::Kind::Pointer && p.type->inner &&
                           p.type->inner->kind == Type::Kind::Struct;
            f.agg = nested || arr;
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
        if (bitCursor >= 0) off += 4;
        if (!t->tag.empty() && (!out.empty() || !structLayouts().count(t->tag))) structLayouts()[t->tag] = out;
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
        s.zext = t->unsign || t->kind == Type::Kind::Bool;
        s.boolean = t->kind == Type::Kind::Bool;
        s.fpw = fpWidthOf(t);
        if (t->kind == Type::Kind::Reference && t->inner) {
            describe(s, t->inner.get());
            s.ref = true;
            return;
        }
        if (t->kind == Type::Kind::Array && t->inner) s.zext = t->inner->unsign;
        if (t->kind == Type::Kind::Struct || t->kind == Type::Kind::Union) {
            s.agg = true;
            s.fields = layoutOf(t);
            if (s.fields.empty() && !t->tag.empty()) {
                auto it = structLayouts().find(t->tag);
                if (it != structLayouts().end()) s.fields = it->second;
            }
        } else if (t->kind == Type::Kind::Pointer && t->inner &&
                   (t->inner->kind == Type::Kind::Struct || t->inner->kind == Type::Kind::Union)) {
            s.fields = layoutOf(t->inner.get());
            if (s.fields.empty() && !t->inner->tag.empty()) {
                auto it = structLayouts().find(t->inner->tag);
                if (it != structLayouts().end()) s.fields = it->second;
            }
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
        if (width >= 8 || width <= 0) return src;
        if (src.kind == Operand::Kind::Imm) {
            std::uint64_t mask = width >= 4 ? 0xffffffffull : width == 2 ? 0xffffull : 0xffull;
            std::uint64_t bits = static_cast<std::uint64_t>(src.imm) & mask;
            if (!uns && (bits & (1ull << (width * 8 - 1)))) bits |= ~mask;
            return Operand::number(static_cast<std::int64_t>(bits));
        }
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
    Operand asBool(Operand src, int fpw = 0) {
        if (fpw) {
            std::uint64_t mask = fpw <= 4 ? 0x7fffffffull : 0x7fffffffffffffffll;
            if (src.kind == Operand::Kind::Imm) {
                std::uint64_t bits = static_cast<std::uint64_t>(src.imm) & mask;
                return Operand::number(bits != 0 ? 1 : 0);
            }
            int mag = fresh();
            Inst in;
            in.op = Inst::Op::And;
            in.dst = mag;
            in.a = src;
            in.b = Operand::number(static_cast<std::int64_t>(mask));
            emit(in);
            src = Operand::vreg(mag);
        }
        if (src.kind == Operand::Kind::Imm) return Operand::number(src.imm != 0 ? 1 : 0);
        int dst = fresh();
        emitBin(Inst::Op::Cne, dst, src, Operand::number(0));
        return Operand::vreg(dst);
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

    void noteFunc(Decl& d) {
        if (d.name.empty()) return;
        Sym s;
        s.func = true;
        s.global = true;
        s.gname = d.asmName.empty() ? d.name : d.asmName;
        s.width = 8;
        globals_[d.name] = s;
    }

    void putAt(std::vector<unsigned char>& b, int off, std::int64_t v, int n) {
        if (n < 1) return;
        if (static_cast<int>(b.size()) < off + n) b.resize(static_cast<std::size_t>(off + n));
        auto u = static_cast<std::uint64_t>(v);
        for (int i = 0; i < n; ++i) b[static_cast<std::size_t>(off + i)] = static_cast<unsigned char>((u >> (8 * i)) & 0xffu);
    }
    void fillInit(const Type* t, const Expr* init, std::vector<unsigned char>& b, int off) {
        if (!t || !init) return;
        if (t->kind == Type::Kind::Array && init->kind == Expr::Kind::Brace && t->inner) {
            int es = sizeOf(t->inner.get());
            for (std::size_t i = 0; i < init->kids.size(); ++i)
                if (init->kids[i]) fillInit(t->inner.get(), init->kids[i].get(), b, off + static_cast<int>(i) * es);
            return;
        }
        if ((t->kind == Type::Kind::Struct || t->kind == Type::Kind::Union) && init->kind == Expr::Kind::Brace) {
            auto fields = layoutOf(t);
            for (std::size_t i = 0; i < init->kids.size() && i < fields.size() && i < t->params.size(); ++i)
                if (init->kids[i] && t->params[i].type)
                    fillInit(t->params[i].type.get(), init->kids[i].get(), b, off + fields[i].offset);
            return;
        }
        if (t->kind == Type::Kind::Array && t->inner && t->inner->kind == Type::Kind::Char &&
            init->kind == Expr::Kind::String) {
            for (std::size_t i = 0; i < init->text.size(); ++i)
                putAt(b, off + static_cast<int>(i), static_cast<unsigned char>(init->text[i]), 1);
            putAt(b, off + static_cast<int>(init->text.size()), 0, 1);
            return;
        }
        std::int64_t val = 0;
        if (init->kind == Expr::Kind::Int || init->kind == Expr::Kind::Char) val = static_cast<std::int64_t>(init->ival);
        else if (init->kind == Expr::Kind::Unary && init->text == "-" && !init->kids.empty() && init->kids[0] &&
                 init->kids[0]->kind == Expr::Kind::Int)
            val = -static_cast<std::int64_t>(init->kids[0]->ival);
        else {
            err(init->line, init->col, "global initializer is not supported");
            return;
        }
        putAt(b, off, val, sizeOf(t));
    }
    void inferBound(Decl& d) {
        if (!d.type || d.type->kind != Type::Kind::Array || !d.type->unsized || !d.init || !d.type->inner) return;
        int n = 0;
        if (d.init->kind == Expr::Kind::Brace) n = static_cast<int>(d.init->kids.size());
        else if (d.init->kind == Expr::Kind::String && d.type->inner->kind == Type::Kind::Char)
            n = static_cast<int>(d.init->text.size()) + 1;
        if (n <= 0) return;
        auto b = std::make_unique<Expr>();
        b->kind = Expr::Kind::Int;
        b->ival = static_cast<std::uint64_t>(n);
        d.type->bound = std::move(b);
        d.type->unsized = false;
    }

    void addGlobal(Decl& d) {
        if (!d.type) return;
        if (d.isExtern) {
            Sym s;
            s.global = true;
            s.gname = d.asmName.empty() ? d.name : d.asmName;
            describe(s, d.type.get());
            globals_[d.name] = s;
            return;
        }
        inferBound(d);
        Sym s;
        s.global = true;
        s.gname = d.asmName.empty() ? d.name : d.asmName;
        describe(s, d.type.get());
        Global g;
        g.name = s.gname;
        bool brace = d.init && d.init->kind == Expr::Kind::Brace;
        bool strArr = d.init && d.init->kind == Expr::Kind::String && d.type->kind == Type::Kind::Array;
        if ((s.array || s.agg) && (brace || strArr)) {
            g.size = sizeOf(d.type.get());
            if (g.size < 0) g.size = 0;
            g.bytes.assign(static_cast<std::size_t>(g.size), 0);
            fillInit(d.type.get(), d.init.get(), g.bytes, 0);
            if (static_cast<int>(g.bytes.size()) < g.size) g.bytes.resize(static_cast<std::size_t>(g.size));
        } else if (s.array || s.agg) {
            g.size = sizeOf(d.type.get());
            if (g.size <= 0) err(d.line, d.col, "array length is not a constant");
            if (d.init) err(d.line, d.col, "global initializer is not supported");
        } else if (d.init && d.init->kind == Expr::Kind::String && d.type->kind == Type::Kind::Pointer) {
            g.size = 8;
            g.reloc = addString(d.init->text);
        } else if (d.init && (d.init->kind == Expr::Kind::Int || d.init->kind == Expr::Kind::Char)) {
            g.size = s.width > 0 && s.width <= 8 ? s.width : 8;
            std::int64_t n = static_cast<std::int64_t>(d.init->ival);
            if (d.type->kind == Type::Kind::Bool) n = n != 0;
            putInt(g.bytes, n, g.size);
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
        if (!d.vt.empty()) {
            g.size = static_cast<int>(d.vt.size()) * 8;
            g.bytes.assign(static_cast<std::size_t>(g.size), 0);
            for (std::size_t k = 0; k < d.vt.size(); ++k)
                if (!d.vt[k].empty()) g.relocs.emplace_back(static_cast<int>(k) * 8, d.vt[k]);
            g.rodata = true;
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
        if (s->func) {
            int dst = fresh();
            Inst in;
            in.op = Inst::Op::LeaSym;
            in.dst = dst;
            in.func = s->gname;
            emit(in);
            return Val{Operand::vreg(dst), 0, 0};
        }
        if (s->ref) {
            int dst = fresh();
            Inst ld;
            ld.op = Inst::Op::Load;
            ld.dst = dst;
            ld.a = Operand::vreg(s->reg);
            ld.extra = s->width > 0 ? s->width : 8;
            ld.uns = s->zext;
            emit(ld);
            Val v;
            v.op = Operand::vreg(dst);
            v.step = s->step;
            v.inner = s->inner;
            v.fields = s->fields;
            v.fpw = s->fpw;
            return v;
        }
        if (s->mem) {
            Val v = loadMem(s->reg, s->width, s->zext);
            v.fpw = s->fpw;
            return v;
        }
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
            v.fpw = s->fpw;
            return v;
        }
        Val v;
        v.op = Operand::vreg(s->reg);
        v.step = s->step;
        v.inner = s->inner;
        v.fields = s->fields;
        v.fpw = s->fpw;
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
            if (s->ref) {
                a.op = Operand::vreg(s->reg);
                a.width = s->width > 0 ? s->width : 8;
                a.step = s->step;
                a.inner = s->inner;
                a.agg = s->agg;
                a.fields = s->fields;
                a.ok = true;
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
            if (e.checked && (e.checked->kind == Type::Kind::Struct || e.checked->kind == Type::Kind::Union)) {
                a.fields = layoutOf(e.checked.get());
                a.agg = true;
            }
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
            a.bitOff = field->bitOff;
            a.bitWidth = field->bitWidth;
            a.bitUns = field->bitUns;
            a.agg = field->agg;
            a.fields = fieldsFor(*field);
            a.ok = true;
            return a;
        }
        err(e.line, e.col, "not lowered");
        return a;
    }

    Val loadAddr(Addr a, int inner, bool uns = false) {
        if (a.bitWidth > 0) {
            int word = fresh();
            Inst ld;
            ld.op = Inst::Op::Load;
            ld.dst = word;
            ld.a = a.op;
            ld.extra = a.width > 0 ? a.width : 4;
            ld.uns = true;
            emit(ld);
            int sh = fresh();
            emitBin(Inst::Op::Shr, sh, Operand::vreg(word), Operand::number(a.bitOff));
            unsigned mask = a.bitWidth >= 32 ? 0xffffffffu : ((1u << a.bitWidth) - 1u);
            int masked = fresh();
            emitBin(Inst::Op::And, masked, Operand::vreg(sh), Operand::number(static_cast<std::int64_t>(mask)));
            if (a.bitUns || a.bitWidth >= 32) return Val{Operand::vreg(masked), inner, 0};
            int sign = 1 << (a.bitWidth - 1);
            int flipped = fresh();
            emitBin(Inst::Op::Xor, flipped, Operand::vreg(masked), Operand::number(sign));
            int ext = fresh();
            emitBin(Inst::Op::Sub, ext, Operand::vreg(flipped), Operand::number(sign));
            return Val{Operand::vreg(ext), inner, 0};
        }
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
    Operand addAddr(Operand base, int off) {
        if (off == 0) return base;
        int dst = fresh();
        emitBin(Inst::Op::Add, dst, base, Operand::number(off));
        return Operand::vreg(dst);
    }
    void copyMem(Operand dst, Operand src, int sz) {
        int off = 0;
        while (off < sz) {
            int n = sz - off;
            if (n > 8) n = 8;
            Operand bits = loadBits(addAddr(src, off), n);
            storeBits(addAddr(dst, off), bits, n);
            off += n;
        }
    }
    Operand addrOf(const Sym& s) {
        if (!s.global) return Operand::vreg(s.reg);
        int dst = fresh();
        Inst lea;
        lea.op = Inst::Op::LeaSym;
        lea.dst = dst;
        lea.func = s.gname;
        emit(lea);
        return Operand::vreg(dst);
    }
    Operand frameAddr(int disp) {
        int dst = fresh();
        Inst in;
        in.op = Inst::Op::LeaFrame;
        in.dst = dst;
        in.a = Operand::number(disp);
        emit(in);
        return Operand::vreg(dst);
    }
    static int structPieces(const Type* t, int& sz, bool& memClass) {
        sz = t ? sizeOf(t) : 8;
        bool rec = t && (t->kind == Type::Kind::Struct || t->kind == Type::Kind::Union);
        memClass = rec && sz > 16;
        if (memClass) return 0;
        if (rec && sz > 8) return 2;
        return 1;
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
        case Expr::Kind::Float: {
            Val v;
            v.op = Operand::number(static_cast<std::int64_t>(e.ival));
            v.fpw = e.litRank == 1 ? 4 : 8;
            return v;
        }
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
            if (e.ty->kind == Type::Kind::Bool) return Val{asBool(v.op, v.fpw), 0, 0};
            if (e.ty->kind == Type::Kind::Pointer) {
                v.step = valueStep(e.ty.get());
                v.inner = innerOf(e.ty.get());
                v.fields.clear();
                if (e.ty->inner && e.ty->inner->kind == Type::Kind::Struct) v.fields = layoutOf(e.ty->inner.get());
                return v;
            }
            if (e.ty->kind == Type::Kind::Array) return v;
            int srcW = v.fpw ? v.fpw : fpWidthOf(e.kids[0]->checked.get());
            int dstFp = fpWidthOf(e.ty.get());
            if (srcW || dstFp) {
                if (dstFp) return cvtFp(v, srcW, dstFp);
                int iw = sizeOf(e.ty.get());
                if (iw < 4) {
                    Val n = cvtFp(v, srcW, -4);
                    return Val{narrow(n.op, iw, e.ty->unsign), 0, 0};
                }
                if (iw > 8) iw = 8;
                return cvtFp(v, srcW, -iw);
            }
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
            Val loaded = loadAddr(a, inner, e.checked && e.checked->unsign);
            if (e.checked) loaded.fpw = fpWidthOf(e.checked.get());
            return loaded;
        }
        case Expr::Kind::Member: {
            Addr a = lowerAddr(e);
            if (!a.ok) return {};
            if (a.agg) {
                Val v;
                v.op = a.op;
                v.step = a.step;
                v.inner = a.inner;
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
                if (s->ref) {
                    Val v;
                    v.op = Operand::vreg(s->reg);
                    v.step = s->width;
                    return v;
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
            if (a.fpw) {
                std::uint64_t sign = a.fpw <= 4 ? 0x80000000ull : 0x8000000000000000ull;
                if (a.op.kind == Operand::Kind::Imm) {
                    Val o = a;
                    o.op = Operand::number(static_cast<std::int64_t>(static_cast<std::uint64_t>(a.op.imm) ^ sign));
                    return o;
                }
                int dst = fresh();
                Inst in;
                in.op = Inst::Op::Xor;
                in.fp = true;
                in.extra = a.fpw;
                in.dst = dst;
                in.a = a.op;
                in.b = Operand::number(static_cast<std::int64_t>(sign));
                emit(in);
                Val o;
                o.op = Operand::vreg(dst);
                o.fpw = a.fpw;
                return o;
            }
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

    Val cvtFp(Val v, int src, int dstCode) {
        if (dstCode > 0 && src == dstCode) return v;
        if (src == 0 && v.op.kind == Operand::Kind::Imm && dstCode > 0) {
            double d = static_cast<double>(v.op.imm);
            Val o;
            o.fpw = dstCode;
            if (dstCode == 4) {
                float f = static_cast<float>(d);
                std::uint32_t bits = 0;
                std::memcpy(&bits, &f, 4);
                o.op = Operand::number(bits);
            } else {
                std::uint64_t bits = 0;
                std::memcpy(&bits, &d, 8);
                o.op = Operand::number(static_cast<std::int64_t>(bits));
            }
            return o;
        }
        int t = fresh();
        Inst in;
        in.op = Inst::Op::Cvt;
        in.fp = true;
        in.dst = t;
        in.a = v.op;
        in.extra = src;
        in.b = Operand::number(dstCode);
        emit(in);
        Val o;
        o.op = Operand::vreg(t);
        o.fpw = dstCode > 0 ? dstCode : 0;
        return o;
    }

    Val lowerBin(Expr& e) {
        if (e.kids.size() < 2 || !e.kids[0] || !e.kids[1]) return {};
        Val a = lowerExpr(*e.kids[0]);
        Val b = lowerExpr(*e.kids[1]);
        int aw = a.fpw ? a.fpw : fpWidthOf(e.kids[0]->checked.get());
        int bw = b.fpw ? b.fpw : fpWidthOf(e.kids[1]->checked.get());
        if (aw || bw) {
            if (e.text == "%" || e.text == "&" || e.text == "|" || e.text == "^" || e.text == "<<" || e.text == ">>") {
                err(e.line, e.col, "not lowered");
                return {};
            }
            int dw = (aw == 8 || bw == 8) ? 8 : 4;
            if (!aw) a = cvtFp(a, 0, dw);
            else if (aw != dw) a = cvtFp(a, aw, dw);
            if (!bw) b = cvtFp(b, 0, dw);
            else if (bw != dw) b = cvtFp(b, bw, dw);
            bool cmp = e.text == "<" || e.text == ">" || e.text == "<=" || e.text == ">=" || e.text == "==" ||
                       e.text == "!=";
            int dst = fresh();
            Inst in;
            in.fp = true;
            in.extra = dw;
            in.dst = dst;
            in.a = a.op;
            in.b = b.op;
            if (cmp) in.op = binOp(e.text);
            else if (e.text == "+") in.op = Inst::Op::Add;
            else if (e.text == "-") in.op = Inst::Op::Sub;
            else if (e.text == "*") in.op = Inst::Op::Mul;
            else in.op = Inst::Op::Div;
            emit(in);
            Val r;
            r.op = Operand::vreg(dst);
            if (!cmp) r.fpw = dw;
            return r;
        }
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

    Val storeCall(Sym& dst, Expr& call) {
        int sz = dst.objSize > 0 ? dst.objSize : dst.width;
        Operand addr = addrOf(dst);
        if (sz > 16) {
            lowerCall(call, &addr);
            return {};
        }
        Val v = lowerCall(call);
        if (v.pair) {
            storeBits(addr, v.op, 8);
            storeBits(addAddr(addr, 8), v.hi, sz - 8);
        } else {
            storeBits(addr, v.op, sz);
        }
        return v;
    }

    void storeNamed(Sym& s, Operand val) {
        if (s.boolean) val = asBool(val);
        if (s.ref) {
            storeBits(Operand::vreg(s.reg), val, s.width > 0 ? s.width : 8);
            return;
        }
        if (s.global) {
            Inst in;
            in.op = Inst::Op::GStore;
            in.func = s.gname;
            in.a = val;
            in.extra = s.width > 0 ? s.width : 8;
            emit(in);
        } else if (s.mem) {
            storeMem(s.reg, val, s.width);
        } else {
            emitMov(s.reg, val, s.width, s.zext);
        }
    }

    // Store a struct value into memory. 'rhs' is the address of the source object, or for a call
    // the returned value (one register, a register pair, or the address of the hidden result).
    void storeAggregate(Operand dst, int sz, const Val& rhs, bool fromCall) {
        if (rhs.pair) {
            storeBits(dst, rhs.op, 8);
            storeBits(addAddr(dst, 8), rhs.hi, sz - 8);
        } else if (sz > 8) {
            copyMem(dst, rhs.op, sz);
        } else {
            Operand bits = fromCall ? rhs.op : loadBits(rhs.op, sz);
            storeBits(dst, bits, sz);
        }
    }

    Val lowerAssign(Expr& e) {
        if (e.kids.size() < 2 || !e.kids[0] || !e.kids[1]) {
            err(e.line, e.col, "not lowered");
            return {};
        }
        if (e.text != "=") {
            std::string op = e.text.substr(0, e.text.size() - 1);
            Expr& lhs = *e.kids[0];
            Val rhs = lowerExpr(*e.kids[1]);
            Val lhsVal;
            Addr addr;
            bool byAddr = lhs.kind != Expr::Kind::Name;
            if (!byAddr) lhsVal = readName(lhs.text, lhs.line, lhs.col);
            else {
                addr = lowerAddr(lhs);
                if (!addr.ok) return {};
                lhsVal = loadAddr(addr, 0, lhs.checked && lhs.checked->unsign);
            }
            int dst = fresh();
            emitBin(binOp(op), dst, lhsVal.op, rhs.op);
            Operand result = Operand::vreg(dst);
            if (!byAddr) {
                Sym* s = find(lhs.text);
                if (!s) {
                    err(e.line, e.col, "not lowered");
                    return {};
                }
                if (s->width > 0 && s->width < 8) result = narrow(result, s->width, s->zext);
                storeNamed(*s, result);
                return Val{Operand::vreg(s->reg), s->step, s->inner};
            }
            storeBits(addr.op, result, addr.width > 0 ? addr.width : 8);
            return Val{result, 0, 0};
        }
        Expr& lhs = *e.kids[0];
        if (lhs.kind == Expr::Kind::Name && e.kids[1]->kind == Expr::Kind::Call) {
            Sym* s = find(lhs.text);
            if (s && s->agg) return storeCall(*s, *e.kids[1]);
        }
        if (lhs.kind != Expr::Kind::Name && lhs.checked && lhs.checked->kind == Type::Kind::Struct &&
            e.kids[1]->kind != Expr::Kind::Call) {
            // struct = struct through a pointer, a field or an index: copy the bytes.
            Addr src = lowerAddr(*e.kids[1]);
            Addr dst = lowerAddr(lhs);
            if (!src.ok || !dst.ok) return {};
            copyMem(dst.op, src.op, sizeOf(lhs.checked.get()));
            return Val{dst.op, 0, 0};
        }
        Val rhs = lowerExpr(*e.kids[1]);
        if (lhs.kind == Expr::Kind::Name) {
            Sym* s = find(lhs.text);
            if (!s) {
                err(e.line, e.col, "not lowered: " + lhs.text);
                return {};
            }
            if (s->boolean) rhs.op = asBool(rhs.op, rhs.fpw);
            if (s->fpw) {
                if (rhs.fpw != s->fpw) rhs = cvtFp(rhs, rhs.fpw, s->fpw);
                if (s->global) {
                    Inst in;
                    in.op = Inst::Op::GStore;
                    in.func = s->gname;
                    in.a = rhs.op;
                    in.extra = s->fpw;
                    emit(in);
                } else {
                    emitMov(s->reg, rhs.op, 8);
                }
                Val out;
                out.op = s->global ? rhs.op : Operand::vreg(s->reg);
                out.fpw = s->fpw;
                return out;
            }
            if (s->array) {
                err(e.line, e.col, "not lowered");
                return {};
            }
            if (s->ref) {
                Operand val = narrow(rhs.op, s->width, s->zext);
                storeBits(Operand::vreg(s->reg), val, s->width > 0 ? s->width : 8);
                return Val{val, 0, 0};
            }
            if (s->agg) {
                int sz = s->objSize > 0 ? s->objSize : s->width;
                Operand addr = addrOf(*s);
                if (rhs.pair) {
                    storeBits(addr, rhs.op, 8);
                    storeBits(addAddr(addr, 8), rhs.hi, sz - 8);
                } else if (sz > 8) {
                    copyMem(addr, rhs.op, sz);
                } else {
                    Operand bits = rhs.op;
                    if (!e.kids[1] || e.kids[1]->kind != Expr::Kind::Call) bits = loadBits(rhs.op, sz);
                    storeBits(addr, bits, sz);
                }
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
        if (lhs.checked && lhs.checked->kind == Type::Kind::Struct) {
            storeAggregate(a.op, sizeOf(lhs.checked.get()), rhs, e.kids[1]->kind == Expr::Kind::Call);
            return rhs;
        }
        if (a.bitWidth > 0) {
            unsigned mask = a.bitWidth >= 32 ? 0xffffffffu : ((1u << a.bitWidth) - 1u);
            unsigned fieldMask = mask << a.bitOff;
            int word = fresh();
            Inst ld;
            ld.op = Inst::Op::Load;
            ld.dst = word;
            ld.a = a.op;
            ld.extra = a.width > 0 ? a.width : 4;
            ld.uns = true;
            emit(ld);
            int val = fresh();
            emitBin(Inst::Op::And, val, rhs.op, Operand::number(static_cast<std::int64_t>(mask)));
            int shifted = fresh();
            emitBin(Inst::Op::Shl, shifted, Operand::vreg(val), Operand::number(a.bitOff));
            int cleared = fresh();
            emitBin(Inst::Op::And, cleared, Operand::vreg(word),
                    Operand::number(static_cast<std::int64_t>(~static_cast<std::uint64_t>(fieldMask))));
            int merged = fresh();
            emitBin(Inst::Op::Or, merged, Operand::vreg(cleared), Operand::vreg(shifted));
            storeBits(a.op, Operand::vreg(merged), a.width > 0 ? a.width : 4);
            return Val{Operand::vreg(val), 0, 0};
        }
        if (lhs.checked && lhs.checked->kind == Type::Kind::Bool) rhs.op = asBool(rhs.op, rhs.fpw);
        if (lhs.checked && isFp(*lhs.checked)) {
            int w = fpWidthOf(lhs.checked.get());
            if (rhs.fpw != w) rhs = cvtFp(rhs, rhs.fpw, w);
            storeBits(a.op, rhs.op, 8);
            Val out;
            out.op = rhs.op;
            out.fpw = w;
            return out;
        }
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
        Val first = lowerExpr(*e.kids[1]);
        emitMov(dst, first.op);
        emitJmp(eL);
        startBlock(fL);
        emitMov(dst, lowerExpr(*e.kids[2]).op);
        emitJmp(eL);
        startBlock(eL);
        Val result{Operand::vreg(dst), 0, 0};
        // Two pointers to objects of one class: the result points to that class too.
        result.step = first.step;
        result.inner = first.inner;
        result.fields = first.fields;
        return result;
    }

    Val lowerVa(Expr& e, const std::string& fname) {
        if (e.kids.size() < 2 || !e.kids[1]) return {};
        Operand ap = lowerExpr(*e.kids[1]).op;
        if (fname == "__va_start") {
            int named = fn_.vaNamed < 0 ? 0 : fn_.vaNamed;
            storeBits(ap, Operand::number(named * 8), 4);
            storeBits(addAddr(ap, 4), Operand::number(48 + 16 * fn_.vaXmm), 4);
            storeBits(addAddr(ap, 8), frameAddr(16), 8);
            storeBits(addAddr(ap, 16), Operand::vreg(vaSave_), 8);
            return {};
        }
        int step = 8;
        bool fpArg = false;
        if (fname == "__va_argp" && e.kids.size() >= 3 && e.kids[2] && e.kids[2]->checked &&
            e.kids[2]->checked->kind == Type::Kind::Pointer && e.kids[2]->checked->inner &&
            isFp(*e.kids[2]->checked->inner))
            fpArg = true;
        if (!fpArg && e.kids.size() >= 3 && e.kids[2]) {
            Operand sz = lowerExpr(*e.kids[2]).op;
            if (sz.kind == Operand::Kind::Imm && sz.imm > 8) step = (static_cast<int>(sz.imm) + 7) & ~7;
        }
        int offAt = fpArg ? 4 : 0;
        int limit = fpArg ? 176 : 48;
        int bump = fpArg ? 16 : step;
        int stackBump = fpArg ? 8 : step;
        std::string id = std::to_string(tmpN_++);
        std::string regL = "va" + id + "_reg";
        std::string stkL = "va" + id + "_stk";
        std::string endL = "va" + id + "_end";
        Operand gp = loadBits(addAddr(ap, offAt), 4);
        int cmp = fresh();
        emitBin(Inst::Op::Cge, cmp, gp, Operand::number(limit), true);
        emitBr(cmp, stkL, regL);
        int result = fresh();
        startBlock(regL);
        Operand save = loadBits(addAddr(ap, 16), 8);
        int ptr = fresh();
        emitBin(Inst::Op::Add, ptr, save, gp);
        int gp2 = fresh();
        emitBin(Inst::Op::Add, gp2, gp, Operand::number(bump));
        storeBits(addAddr(ap, offAt), Operand::vreg(gp2), 4);
        emitMov(result, Operand::vreg(ptr));
        emitJmp(endL);
        startBlock(stkL);
        Operand ov = loadBits(addAddr(ap, 8), 8);
        emitMov(result, ov);
        int ov2 = fresh();
        emitBin(Inst::Op::Add, ov2, ov, Operand::number(stackBump));
        storeBits(addAddr(ap, 8), Operand::vreg(ov2), 8);
        emitJmp(endL);
        startBlock(endL);
        Val out;
        out.op = Operand::vreg(result);
        out.step = 1;
        return out;
    }

    Val lowerCall(Expr& e, const Operand* retSlotIn = nullptr) {
        if (e.kids.empty() || !e.kids[0]) {
            err(e.line, e.col, "not lowered");
            return {};
        }
        bool direct = false;
        std::string fname;
        Operand target;
        if (e.kids[0]->kind == Expr::Kind::Name) {
            Sym* s = find(e.kids[0]->text);
            if (!s || s->func) {
                direct = true;
                fname = s && !s->gname.empty() ? s->gname : e.kids[0]->text;
            }
        }
        if (direct && (fname == "__va_start" || fname == "__va_arg" || fname == "__va_argp")) return lowerVa(e, fname);
        if (!direct) target = lowerExpr(*e.kids[0]).op;
        int reg = 0;
        bool retRec = e.checked &&
                      (e.checked->kind == Type::Kind::Struct || e.checked->kind == Type::Kind::Union);
        bool retMem = retRec && sizeOf(e.checked.get()) > 16;
        bool retPair = retRec && sizeOf(e.checked.get()) > 8 && sizeOf(e.checked.get()) <= 16;
        int retSz = retRec ? sizeOf(e.checked.get()) : 0;
        std::vector<Operand> regs;
        std::vector<Operand> stack;
        Operand retSlot;
        if (retMem) {
            if (retSlotIn) {
                retSlot = *retSlotIn;
            } else {
                int slot = fresh();
                Inst loc;
                loc.op = Inst::Op::Local;
                loc.dst = slot;
                loc.a = Operand::number(retSz < 1 ? 1 : retSz);
                emit(loc);
                retSlot = Operand::vreg(slot);
            }
            regs.push_back(retSlot);
            reg = 1;
        }
        const Type* fnty = nullptr;
        if (e.kids[0]->checked) {
            const Type* t = e.kids[0]->checked.get();
            if (t->kind == Type::Kind::Pointer && t->inner && t->inner->kind == Type::Kind::Function) t = t->inner.get();
            if (t->kind == Type::Kind::Function) fnty = t;
        }
        int xmn = 0;
        std::vector<Operand> xmm;
        std::vector<int> xmmW;
        for (std::size_t i = 1; i < e.kids.size(); ++i) {
            if (!e.kids[i]) continue;
            Val v = lowerExpr(*e.kids[i]);
            const Type* t = e.kids[i]->checked.get();
            int argi = static_cast<int>(i - 1);
            const Type* declared = nullptr;
            bool varExtra = fnty && fnty->variadic && argi >= static_cast<int>(fnty->params.size());
            if (fnty && argi < static_cast<int>(fnty->params.size())) declared = fnty->params[static_cast<std::size_t>(argi)].type.get();
            int want = declared ? fpWidthOf(declared) : 0;
            if (varExtra && (v.fpw || fpWidthOf(t))) want = 8;
            if (!declared && (v.fpw || fpWidthOf(t))) want = v.fpw ? v.fpw : fpWidthOf(t);
            if (want) {
                int src = v.fpw ? v.fpw : fpWidthOf(t);
                if (src != want) v = cvtFp(v, src, want);
                if (xmn < 8) {
                    xmm.push_back(v.op);
                    xmmW.push_back(want);
                    ++xmn;
                } else {
                    stack.push_back(v.op);
                }
                continue;
            }
            int sz = 0;
            bool memClass = false;
            int nreg = structPieces(t, sz, memClass);
            bool isStruct = t && (t->kind == Type::Kind::Struct || t->kind == Type::Kind::Union);
            if (!isStruct) {
                bool decay = t && (t->kind == Type::Kind::Array || t->kind == Type::Kind::Function);
                int w = !t || decay ? 8 : sizeOf(t);
                bool uns = t && !decay && t->unsign;
                Operand val = declared && declared->kind == Type::Kind::Bool ? asBool(v.op, v.fpw)
                              : (w > 0 && w < 8) ? narrow(v.op, w, uns) : v.op;
                if (reg >= 6) stack.push_back(val);
                else {
                    regs.push_back(val);
                    ++reg;
                }
                continue;
            }
            if (memClass || reg + nreg > 6) {
                int off = 0;
                int whole = (sz + 7) & ~7;
                while (off < whole) {
                    int n = sz - off;
                    if (n <= 0) stack.push_back(Operand::number(0));
                    else {
                        if (n > 8) n = 8;
                        stack.push_back(loadBits(addAddr(v.op, off), n));
                    }
                    off += 8;
                }
            } else {
                regs.push_back(loadBits(v.op, sz >= 8 ? 8 : sz));
                if (nreg == 2) regs.push_back(loadBits(addAddr(v.op, 8), sz - 8));
                reg += nreg;
            }
        }
        while (stack.size() % 2) stack.push_back(Operand::number(0));
        int dst = fresh();
        int hi = -1;
        if (retPair) hi = fresh();
        Inst in;
        in.op = Inst::Op::Call;
        in.dst = dst;
        if (direct) in.func = std::move(fname);
        else in.a = target;
        in.args = std::move(regs);
        in.stack = std::move(stack);
        in.xmm = std::move(xmm);
        in.xmmW = std::move(xmmW);
        if (hi >= 0) in.b = Operand::vreg(hi);
        if (e.kids[0]->checked) {
            const Type* t = e.kids[0]->checked.get();
            if (t->kind == Type::Kind::Pointer && t->inner && t->inner->kind == Type::Kind::Function) t = t->inner.get();
            if (t->kind == Type::Kind::Function && t->variadic) in.va = true;
        }
        if (e.checked && isFp(*e.checked)) {
            in.fp = true;
            in.extra = fpWidthOf(e.checked.get());
        }
        emit(in);
        if (retMem) {
            Val out;
            out.op = retSlot;
            out.fields = layoutOf(e.checked.get());
            return out;
        }
        Val out{Operand::vreg(dst), 0, 0};
        if (e.checked && isFp(*e.checked)) out.fpw = fpWidthOf(e.checked.get());
        if (retPair) {
            out.pair = true;
            out.hi = Operand::vreg(hi);
        }
        if (e.checked && e.checked->kind == Type::Kind::Pointer) {
            out.step = valueStep(e.checked.get());
            out.inner = innerOf(e.checked.get());
            if (e.checked->inner && e.checked->inner->kind == Type::Kind::Struct)
                out.fields = layoutOf(e.checked->inner.get());
        }
        return out;
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
            if (s.expr && retRef_) {
                Addr a = lowerAddr(*s.expr);
                in.a = a.op;
                in.extra = 8;
                in.uns = true;
                emit(in);
                break;
            }
            if (s.expr && retStruct_ && retWidth_ > 16 && hiddenReg_ >= 0) {
                Operand hid = Operand::vreg(hiddenReg_);
                if (s.expr->kind == Expr::Kind::Call) lowerCall(*s.expr, &hid);
                else if (s.expr->kind == Expr::Kind::Name) {
                    if (Sym* sy = find(s.expr->text))
                        if (sy->agg) copyMem(hid, readName(s.expr->text, s.line, s.col).op, retWidth_);
                } else {
                    Val v = lowerExpr(*s.expr);
                    copyMem(hid, v.op, retWidth_);
                }
                in.a = hid;
                emit(in);
                break;
            }
            if (s.expr && retStruct_ && retWidth_ > 8) {
                Operand lo;
                Operand hi;
                if (s.expr->kind == Expr::Kind::Call) {
                    Val v = lowerCall(*s.expr);
                    lo = v.op;
                    hi = v.hi;
                } else if (s.expr->kind == Expr::Kind::Name && find(s.expr->text) && find(s.expr->text)->agg) {
                    Val addr = readName(s.expr->text, s.line, s.col);
                    lo = loadBits(addr.op, 8);
                    hi = loadBits(addAddr(addr.op, 8), retWidth_ - 8);
                } else {
                    Val v = lowerExpr(*s.expr);
                    lo = loadBits(v.op, 8);
                    hi = loadBits(addAddr(v.op, 8), retWidth_ - 8);
                }
                in.a = lo;
                in.b = hi;
                in.uns = true;
                emit(in);
                break;
            }
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
            if (s.expr && retStruct_ && s.expr->kind != Expr::Kind::Call && s.expr->kind != Expr::Kind::Name) {
                // Any other object expression (a temporary, *p, a[i], s.f): its address is lowered.
                Val v = lowerExpr(*s.expr);
                in.a = loadBits(v.op, retWidth_);
                in.extra = bitWidth(retWidth_);
                in.uns = true;
                emit(in);
                break;
            }
            if (s.expr) {
                Val v = lowerExpr(*s.expr);
                if (retBool_) v.op = asBool(v.op, v.fpw);
                if (retFp_ && v.fpw != retWidth_) v = cvtFp(v, v.fpw, retWidth_);
                in.a = v.op;
                in.extra = retWidth_;
                in.uns = retUns_ || retStruct_;
                in.fp = retFp_;
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
        case Stmt::Kind::Goto:
            if (s.expr) emitJmp("lab_" + s.expr->text);
            break;
        case Stmt::Kind::Label:
            if (s.expr) startBlock("lab_" + s.expr->text);
            break;
        }
    }

    void lowerLocal(Decl& d) {
        if (!d.type) return;
        if (d.isStatic) {
            std::string gname = ".Ls" + std::to_string(strN_++) + "_" + d.name;
            Global g;
            g.name = gname;
            int sz = sizeOf(d.type.get());
            if (sz < 1) sz = 1;
            g.size = sz;
            g.bytes.assign(static_cast<std::size_t>(sz), 0);
            if (d.init && (d.init->kind == Expr::Kind::Int || d.init->kind == Expr::Kind::Char)) {
                std::int64_t n = static_cast<std::int64_t>(d.init->ival);
                if (d.type->kind == Type::Kind::Bool) n = n != 0;
                putAt(g.bytes, 0, n, sz > 8 ? 8 : sz);
            }
            mod_.globals.push_back(std::move(g));
            Sym s;
            s.global = true;
            s.gname = gname;
            describe(s, d.type.get());
            if (!d.name.empty()) scopes_.back()[d.name] = s;
            return;
        }
        Sym s;
        describe(s, d.type.get());
        if (d.type->kind == Type::Kind::Array || d.type->kind == Type::Kind::Struct ||
            d.type->kind == Type::Kind::Union) {
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
            if (d.init && d.init->kind == Expr::Kind::Brace) {
                if (d.type->kind == Type::Kind::Array && d.type->inner) {
                    int es = sizeOf(d.type->inner.get());
                    for (std::size_t i = 0; i < d.init->kids.size(); ++i) {
                        if (!d.init->kids[i]) continue;
                        Val v = lowerExpr(*d.init->kids[i]);
                        int w = es > 8 ? 8 : es;
                        Operand bits = w < 8 ? narrow(v.op, w, d.type->inner->unsign) : v.op;
                        storeBits(addAddr(Operand::vreg(addr), static_cast<int>(i) * es), bits, w);
                    }
                } else if (d.type->kind == Type::Kind::Struct || d.type->kind == Type::Kind::Union) {
                    auto fields = layoutOf(d.type.get());
                    for (std::size_t i = 0; i < d.init->kids.size() && i < fields.size(); ++i) {
                        if (!d.init->kids[i]) continue;
                        Val v = lowerExpr(*d.init->kids[i]);
                        int w = fields[i].width > 8 ? 8 : fields[i].width;
                        storeBits(addAddr(Operand::vreg(addr), fields[i].offset), v.op, w);
                    }
                }
            } else if (d.init && d.type->kind == Type::Kind::Struct) {
                // A struct initialized from another struct or from a call: copy the bytes.
                Sym* me = find(d.name);
                if (d.init->kind == Expr::Kind::Call && me) {
                    storeCall(*me, *d.init);
                } else {
                    Addr src = lowerAddr(*d.init);
                    if (src.ok) copyMem(Operand::vreg(addr), src.op, bytes);
                }
            }
            return;
        }
        if (s.ref && d.init) {
            int slot = fresh();
            s.reg = slot;
            if (!d.name.empty()) scopes_.back()[d.name] = s;
            Addr a = lowerAddr(*d.init);
            emitMov(slot, a.op);
            return;
        }
        int slot = fresh();
        s.reg = slot;
        if (!d.name.empty() && addr_.count(d.name)) s.mem = true;
        if (!d.name.empty()) scopes_.back()[d.name] = s;
        if (d.init) {
            Val v = lowerExpr(*d.init);
            if (s.fpw) {
                if (v.fpw != s.fpw) v = cvtFp(v, v.fpw, s.fpw);
                emitMov(slot, v.op, 8);
            } else {
                Operand val = s.boolean ? asBool(v.op, v.fpw) : narrow(v.op, s.width, s.zext);
                if (s.mem) storeMem(slot, val, s.width);
                else emitMov(slot, val, s.width, s.zext);
            }
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
        if (s.decl) {
            scanExpr(s.decl->init.get());
            if (s.decl->type && s.decl->type->kind == Type::Kind::Reference && s.decl->init &&
                s.decl->init->kind == Expr::Kind::Name)
                addr_.insert(s.decl->init->text);
        }
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
        fn_.name = d.asmName.empty() ? d.name : d.asmName;
        fn_.localSym = d.isStatic;
        fn_.params = d.type ? static_cast<int>(d.type->params.size()) : 0;
        nextReg_ = 0;
        scopes_.clear();
        scopes_.emplace_back();
        loops_.clear();
        addr_.clear();
        retWidth_ = 8;
        retFp_ = false;
        retUns_ = false;
        retBool_ = false;
        retStruct_ = false;
        retRef_ = d.type && d.type->inner && d.type->inner->kind == Type::Kind::Reference;
        hiddenReg_ = -1;
        if (d.body) scanStmt(*d.body);
        if (d.type && d.type->inner) {
            const Type* r = d.type->inner.get();
            if (r->kind == Type::Kind::Char || r->kind == Type::Kind::Short || r->kind == Type::Kind::Int ||
                r->kind == Type::Kind::Long || r->kind == Type::Kind::Bool)
                retWidth_ = sizeOf(r);
            if (r->kind == Type::Kind::Bool) retBool_ = true;
            if (r->kind == Type::Kind::Float || r->kind == Type::Kind::Double) {
                retFp_ = true;
                retWidth_ = sizeOf(r);
            }
            if (r->kind == Type::Kind::Struct || r->kind == Type::Kind::Union) {
                retStruct_ = true;
                retWidth_ = sizeOf(r);
            }
            retUns_ = r->unsign;
        }
        struct PPlan {
            bool memory = false;
            int nreg = 1;
            int regAt = 0;
            int stackOff = 0;
            int size = 8;
            bool isStruct = false;
            bool isFp = false;
            int xmmAt = -1;
        };
        std::vector<PPlan> plans;
        int reg = 0;
        int xm = 0;
        int stack = 0;
        if (retStruct_ && retWidth_ > 16) {
            hiddenReg_ = 0;
            reg = 1;
        }
        if (d.type) {
            for (const Param& p : d.type->params) {
                PPlan plan;
                bool memClass = false;
                if (p.type && (p.type->kind == Type::Kind::Float || p.type->kind == Type::Kind::Double)) {
                    plan.isFp = true;
                    plan.size = sizeOf(p.type.get());
                    if (xm >= 8) {
                        plan.memory = true;
                        stack = alignUp(stack, 8);
                        plan.stackOff = stack;
                        stack += 8;
                    } else {
                        plan.xmmAt = xm++;
                    }
                    plans.push_back(plan);
                    continue;
                }
                plan.nreg = structPieces(p.type.get(), plan.size, memClass);
                plan.isStruct = p.type && (p.type->kind == Type::Kind::Struct || p.type->kind == Type::Kind::Union);
                if (memClass || reg + plan.nreg > 6) {
                    plan.memory = true;
                    plan.nreg = 0;
                    stack = alignUp(stack, 8);
                    plan.stackOff = stack;
                    stack += (plan.size + 7) & ~7;
                } else {
                    plan.regAt = reg;
                    reg += plan.nreg;
                }
                plans.push_back(plan);
            }
        }
        fn_.params = reg;
        fn_.vaNamed = d.type && d.type->variadic ? reg : -1;
        fn_.vaXmm = d.type && d.type->variadic ? xm : 0;
        nextReg_ = reg;
        startBlock("entry");
        if (fn_.vaNamed >= 0) {
            int save = fresh();
            Inst loc;
            loc.op = Inst::Op::Local;
            loc.dst = save;
            loc.a = Operand::number(176);
            emit(loc);
            vaSave_ = save;
        }
        if (d.type) {
            for (std::size_t i = 0; i < d.type->params.size(); ++i) {
                const Param& p = d.type->params[i];
                const PPlan& plan = plans[i];
                Sym s;
                if (p.type) describe(s, p.type.get());
                if (plan.isStruct) {
                    int addr = fresh();
                    if (plan.memory) {
                        Inst lea;
                        lea.op = Inst::Op::LeaFrame;
                        lea.dst = addr;
                        lea.a = Operand::number(16 + plan.stackOff);
                        emit(lea);
                    } else {
                        Inst loc;
                        loc.op = Inst::Op::Local;
                        loc.dst = addr;
                        loc.a = Operand::number(plan.size < 1 ? 1 : plan.size);
                        emit(loc);
                        storeBits(Operand::vreg(addr), Operand::vreg(plan.regAt),
                                  plan.size >= 8 ? 8 : plan.size);
                        if (plan.nreg == 2)
                            storeBits(addAddr(Operand::vreg(addr), 8), Operand::vreg(plan.regAt + 1),
                                      plan.size - 8);
                    }
                    s.reg = addr;
                    s.agg = true;
                } else if (plan.isFp && !plan.memory) {
                    s.reg = fresh();
                    s.fpw = plan.size;
                    s.width = plan.size;
                    if (static_cast<int>(fn_.xmmSlot.size()) <= plan.xmmAt) {
                        fn_.xmmSlot.resize(static_cast<std::size_t>(plan.xmmAt + 1), -1);
                        fn_.xmmWidth.resize(static_cast<std::size_t>(plan.xmmAt + 1), 8);
                    }
                    fn_.xmmSlot[static_cast<std::size_t>(plan.xmmAt)] = s.reg;
                    fn_.xmmWidth[static_cast<std::size_t>(plan.xmmAt)] = plan.size;
                } else if (plan.memory) {
                    int w = p.type ? sizeOf(p.type.get()) : 8;
                    bool uns = p.type && p.type->unsign;
                    Operand src = loadBits(frameAddr(16 + plan.stackOff), w);
                    int slot = fresh();
                    emitMov(slot, src, w, uns);
                    s.reg = slot;
                    if (!p.name.empty() && addr_.count(p.name)) s.mem = true;
                } else {
                    s.reg = plan.regAt;
                    if (!p.name.empty() && addr_.count(p.name)) s.mem = true;
                }
                if (s.boolean && s.reg >= 0) {
                    Operand n = asBool(Operand::vreg(s.reg));
                    emitMov(s.reg, n, 1, true);
                }
                if (!p.name.empty()) scopes_.back()[p.name] = s;
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

Module lowerProgram(const std::string& source, Diagnostics& diags, const std::string& fileName,
                     const std::vector<std::string>& includes) {
    MemoryFiles files;
    files.add(fileName, source);
    std::vector<Token> raw = preprocessTokens(files, fileName, includes, diags);
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
    Tu tu = frontEnd(toks, diags);
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
    Tu tu = frontEnd(toks, diags);
    (void)tu;
}

Module lowerSource(const std::string& source, Diagnostics& diags, const std::string& file,
                   const std::vector<std::string>& includes) {
    Module m = lowerProgram(source, diags, file, includes);
    if (diags.ok()) optimizeModule(m);
    return m;
}

}  // namespace okaycc
