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
    bool rref = false;  // a Reference that is an rvalue reference T&& ([dcl.ref])
    bool cnst = false;  // declared const ([dcl.type.cv]); written before the type name only
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
    bool done = false;
};

struct Tu {
    std::vector<std::unique_ptr<Decl>> decls;
    std::vector<LambdaInfo> lambdas;
    std::vector<std::string> usingNs;                 // using namespace N; (qualified names)
    std::map<std::string, std::string> usingDecl;     // using N::f;  f -> N::f
};

std::unique_ptr<Type> cloneType(const Type& t);
std::unique_ptr<Expr> cloneExpr(const Expr& e);

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
bool hasSideEffects(const Expr& e);

// mangle.cpp
std::string mangleType(const Type& t);
std::string mangleFunc(const std::string& qualified, const Type& fn);
std::string mangleSpecial(const std::string& cls, const char* kind, const Type& fn);
std::string mangleTypeNoCv(const Type& t);
std::string mangleData(const std::string& qualified);

// templates.cpp
std::vector<TArg> cloneArgs(const std::vector<TArg>& in);
std::map<std::string, TemplateSig>& funcTemplateSigs();
std::vector<InstReq>& instRequests();
bool& ehActive();
bool& forceExceptions();
bool& instDirty();
std::size_t mangledEnd(const std::string& s, std::size_t j);
std::unique_ptr<Type> typeFromMangle(const std::string& m);
std::string argMangle(const TArg& a);
std::string instFuncName(const std::string& tmpl, const TemplateSig& sig, const std::vector<TArg>& args);

// parser.cpp
Tu frontEnd(const std::vector<Token>& toks, Diagnostics& diags);

}  // namespace compiler
}  // namespace okaycc
