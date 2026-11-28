#pragma once
// The syntax tree (types, expressions, statements, declarations), the template tables, and the declarations of
// the functions parser.cpp, check.cpp and lower.cpp share.
// Moved out of parser.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "parser.hpp"

#include "files.hpp"
#include "ir.hpp"
#include "preproc.hpp"
#include "token.hpp"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <unordered_map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <cctype>
#include <vector>

namespace okaycc {
namespace compiler {
struct Expr;
struct Type;
struct Stmt;
struct Decl;

// A type shared instead of copied (the checker's results, ET::type, and the type a checked node
// keeps, Expr::checked): copying a SharedType counts one more owner of the same type. It is read as
// const; mut() gives a type of this holder's own to change, a copy when another owner holds it
// (copy on write), and take() gives it up as a unique_ptr, copied when shared.
class SharedType {
public:
    SharedType() = default;
    SharedType(std::unique_ptr<Type> t);  // NOLINT: implicit, like the unique_ptr it replaces
    SharedType(std::nullptr_t) {}
    SharedType(const SharedType& o);
    SharedType(SharedType&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    SharedType& operator=(const SharedType& o);
    SharedType& operator=(SharedType&& o) noexcept {
        std::swap(p_, o.p_);
        return *this;
    }
    SharedType& operator=(std::unique_ptr<Type> t);
    SharedType& operator=(std::nullptr_t) {
        reset();
        return *this;
    }
    ~SharedType();
    const Type* get() const { return p_; }
    const Type& operator*() const { return *p_; }
    const Type* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    void reset();
    Type* mut();
    std::unique_ptr<Type> take();
    operator std::unique_ptr<Type>() && { return take(); }  // NOLINT: std::move(r.type) where a unique_ptr goes

private:
    void drop();
    Type* p_ = nullptr;
};

struct Param {
    std::string name;
    std::unique_ptr<Type> type;
    int bits = -1;
    std::string mangled;
    int vslot = -1;     // virtual function table slot, or -1
    bool cmethod = false;  // a const member function (mangled _ZNK...)
    bool pure = false;  // declared "= 0"
    std::unique_ptr<Expr> defArg;  // a default argument ([dcl.fct.default]), or null
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
    SharedType checked;  // the type the checker gave the node (shared with its result)
    // A number no other node made in this run has: a node made later at the address of a freed one
    // is told apart from it (the checker's kept results, Checker::memo_).
    std::uint64_t serial;
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
    bool rref = false;  // a Reference that is an rvalue reference T&& ([dcl.ref])
    bool cnst = false;  // declared const ([dcl.type.cv]); written before the type name only
    // A Pointer that is a pointer to a data member of this class (T C::*, [dcl.mptr]). Its value is
    // the member's offset plus one, so a null one is 0 (our own choice; Itanium keeps the offset and -1).
    std::string memberOf;
    std::vector<Param> params;
    // Class facts (Itanium C++ ABI layout of a single-inheritance class).
    std::string base;                // tag of the one base class, or empty
    bool nonpod = false;             // not "POD for the purpose of layout"
    bool hasVptr = false;            // has a vtable pointer at offset 0
    bool needCtor = false;           // construction must run code
    bool needDtor = false;           // destruction must run code
    std::vector<std::string> vslots; // symbol that fills each vtable slot ("" = null)
    // Owners of a type the checker shares between results (SharedType in check.cpp); 0 elsewhere.
    // cloneType does not copy it.
    int owners = 0;
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
    bool staticMember = false;  // a static member function: no 'this'
    bool isInline = false;      // inline function or class-generated code: emitted as a weak symbol
    bool fromInstance = false;  // a member function of a class template instance: checked and kept only if used
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

// A lambda expression ([expr.prim.lambda]). The parser makes its function (a static function whose
// first parameter __self points to the closure object); the checker decides the captures while it
// checks the body inside the enclosing function, gives the closure class its fields and appends
// the function to the translation unit.
struct LambdaInfo {
    std::unique_ptr<Decl> fn;
    std::string tag;     // the closure class
    int mode = 0;        // capture default: 0 none, 1 [&], 2 [=]
    std::vector<std::pair<std::string, bool>> caps;  // explicit captures: name, by reference
    // Init-captures [x = e] ([expr.prim.lambda.capture] p6): the hidden variable (also in caps, by
    // copy; the body's x is renamed to it) and e. The checker declares it where the lambda is.
    std::vector<std::pair<std::string, std::unique_ptr<Expr>>> inits;
    bool done = false;
};

struct Tu {
    std::vector<std::unique_ptr<Decl>> decls;
    // A deque: the checker adds the lambdas of each instance of a generic lambda while it holds
    // references to others (checkLambda).
    std::deque<LambdaInfo> lambdas;
    std::vector<std::string> usingNs;                 // using namespace N; (qualified names)
    std::map<std::string, std::string> usingDecl;     // using N::f;  f -> N::f
    std::string unitSfx;  // the unit's file name as a symbol part (__okay_init_<unitSfx>)
};

// withDefaults false: the default arguments of function parameters are left out (a checked
// expression's type, which lowering reads only for its shape).
std::unique_ptr<Type> cloneType(const Type& t, bool withDefaults = true);
inline SharedType::SharedType(std::unique_ptr<Type> t) : p_(t.release()) {
    if (p_) p_->owners = 1;
}
inline SharedType::SharedType(const SharedType& o) : p_(o.p_) {
    if (p_) ++p_->owners;
}
inline SharedType& SharedType::operator=(const SharedType& o) {
    SharedType n(o);
    std::swap(p_, n.p_);
    return *this;
}
inline SharedType& SharedType::operator=(std::unique_ptr<Type> t) {
    SharedType n(std::move(t));
    std::swap(p_, n.p_);
    return *this;
}
inline SharedType::~SharedType() { drop(); }
inline void SharedType::drop() {
    if (p_ && --p_->owners == 0) delete p_;
}
inline void SharedType::reset() {
    drop();
    p_ = nullptr;
}
inline Type* SharedType::mut() {
    if (p_ && p_->owners > 1) {
        Type* c = cloneType(*p_, true).release();
        --p_->owners;
        p_ = c;
        p_->owners = 1;
    }
    return p_;
}
inline std::unique_ptr<Type> SharedType::take() {
    if (!p_) return nullptr;
    std::unique_ptr<Type> r;
    if (p_->owners == 1) {
        p_->owners = 0;
        r.reset(p_);
    } else {
        r = cloneType(*p_, true);
        --p_->owners;
    }
    p_ = nullptr;
    return r;
}

std::unique_ptr<Type> classByName(const Type& in);
bool finishedClassCopy(const Type& t);
std::unique_ptr<Expr> cloneExpr(const Expr& e);
// Deep copies of a statement and of a declaration (a generic lambda's function, copied unchecked
// for each instance).
std::unique_ptr<Stmt> cloneStmt(const Stmt& s);
std::unique_ptr<Decl> cloneDecl(const Decl& d);

std::string exprStr(const Expr& e);
std::string typeStr(const Type& t);

std::string mangleNested(const std::string& qualified);
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

struct TemplateSig {
    std::vector<std::string> names;
    std::vector<bool> isType;
    std::vector<bool> hasDef;  // the parameter has a default argument (template <class T, class = void>)
    std::unique_ptr<Type> fn;  // prototype, a type parameter T appears as a struct with tag "\x02T"
};

struct InstReq {
    std::string tmpl;
    std::vector<TArg> args;
    std::string name;
};

void typecheckTu(Tu& tu, Diagnostics& diags);

// ---- Functions one part of the compiler calls in another (the definitions are in the named file) ----

// ast.cpp
std::string printTu(const Tu& tu);
bool isIntish(const Type& t);
bool isFp(const Type& t);
int fpWidthOf(const Type* t);
const Type* canonOf(const Type* st);
bool isPtr(const Type& t);
bool isVoidPtr(const Type& t);
bool isSpecialName(const std::string& n);
const Param* findSpecial(const Type* t, const char* name);
bool derivedFrom(const Type& d, const Type& b);
// Whether a class has a constructor taking one reference to its own class (a copy or move one).
bool hasCopyCtor(const Type* cls);
std::vector<std::string> basePath(const std::string& from, const std::string& to, int depth = 0);
bool hasSideEffects(const Expr& e);

// mangle.cpp
std::string mangleType(const Type& t);
std::string mangleFunc(const std::string& qualified, const Type& fn);
std::string mangleSpecial(const std::string& cls, const char* kind, const Type& fn);
std::string mangleTypeNoCv(const Type& t);
std::string mangleData(const std::string& qualified);
std::string typeInfoSym(const Type& t);  // _ZTI<type>, the type's type_info object (RTTI)
// Appends the weak _ZTS name and _ZTI object of t (and of its base classes) not yet in 'done'.
void makeTypeInfo(const Type& t, std::vector<std::unique_ptr<Decl>>& out, std::set<std::string>& done,
                  int line, int col, const std::string& file);

// templates.cpp
std::vector<TArg> cloneArgs(const std::vector<TArg>& in);
std::map<std::string, TemplateSig>& funcTemplateSigs();
std::vector<InstReq>& instRequests();
// Function template instances whose signature could not be formed for their arguments
// ([temp.deduct] p8, SFINAE), as "<template key>|<instance name>" (the overloads name#k of one
// template may give one instance name): the parser records them; overload resolution skips them.
std::set<std::string>& sfinaeFailed();
// The decltype expressions in the pattern of a class template partial specialization chosen for an
// instance (template <class T> struct S<T, void_t<decltype(e)>>), with the specialization's
// parameters replaced: the checker checks each one, and one that has no type ([temp.deduct] p8)
// puts the key ("<template>|<index>|<arguments>") in sfinaeFailed(), so the next round does not
// choose that specialization ([temp.spec.partial.match] p2). Made anew each round.
struct SpecCheck {
    std::string key;
    std::vector<std::unique_ptr<Expr>> exprs;
};
std::vector<SpecCheck>& specChecks();
// The argument for a defaulted template parameter that deduction left to the instance.
const char* const kDefaultArgTag = "__okay_default";
// The namespace each template instance (a class's tag, a function's name) was declared in: its
// tag or name is not qualified, but unqualified names in it are looked up there ([temp.res] p10).
std::map<std::string, std::string>& instNamespaces();
// The default member initializers of the classes of the file ([class.mem]), by class tag and member
// name, as the parser read them in the current round: the checker builds an aggregate from a
// braced list member by member and gives the members the list leaves out these values.
std::map<std::string, std::map<std::string, std::unique_ptr<Expr>>>& memberDefaults();
// A value made of literals only (numbers, characters, strings, braced lists and casts of them):
// one that means the same wherever it is copied to.
bool plainConstant(const Expr& e);
// The symbols of the constructors declared explicit ([class.conv.ctor] p1) in the current round:
// they make no implicit conversion.
std::set<std::string>& explicitCtors();
bool& ehActive();
bool& forceExceptions();
bool& instDirty();
// Set by frontEnd while the checker runs: makes the function template instance r at once and puts
// it at the end of the file being checked (Parser::instantiate). False: it could not be made that
// way, and the checker asks for a new round (instDirty) as before. Empty when nothing can make one.
std::function<bool(Tu&, const InstReq&)>& instantiateNow();
std::size_t mangledEnd(const std::string& s, std::size_t j);
// A pack place of a primary class template made by packs.cpp (parameter "P__pk<j>") may not take
// the filler type __okay_pack_end: that place is empty, so the copy for a longer pack must not match.
bool packPlaceGetsEnd(const std::string& param, const Type& arg);
// A prototype type with its template parameters replaced by the arguments.
std::unique_ptr<Type> substType(const Type& pat, const TemplateSig& sig, const std::vector<TArg>& args);
std::unique_ptr<Type> typeFromMangle(const std::string& m);
// Match the tag of a class template instance written with parameters (Node<T>: each parameter as
// <length>\x02<name>) against an instance's tag Q piece by piece, calling bind(name, type) for each
// parameter (false: a conflict, the match fails). strict: any other difference fails the match (a
// partial specialization's pattern); otherwise it is ignored (deduction, which checks later).
bool matchInstanceTag(const std::string& P, const std::string& Q, bool strict,
                      const std::function<bool(const std::string&, std::unique_ptr<Type>)>& bind);
std::string argMangle(const TArg& a);
std::string instFuncName(const std::string& tmpl, const TemplateSig& sig, const std::vector<TArg>& args);

// lower.cpp
// The byte offset of member 'field' in the finished class 'tag', or -1.
int memberOffset(const std::string& tag, const std::string& field);
// The size of a scalar type or a finished class, or 0 when it is not known yet.
int typeSize(const Type& t);

// parser.cpp
Tu frontEnd(const std::vector<Token>& toks, Diagnostics& diags);

}  // namespace compiler
}  // namespace okaycc
