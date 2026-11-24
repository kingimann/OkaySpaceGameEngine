#pragma once
// The OkayScript syntax tree. The parser (Parser.hpp) builds it; each node runs itself against a Runtime
// (Interpreter.hpp): expressions with Eval, statements with Exec. The node set and what each one does are the ones of
// the engine's OkayScriptVM (engine/src/Scripting/OkayScriptVM.cpp) without its engine-only nodes (classes,
// coroutines, events, scene objects). Every node keeps the source line it started on, so runtime errors can say where.
//
// Dump() prints a node as a small S-expression, e.g. `1 + 2 * 3` -> "(+ 1 (* 2 3))", so tests can check the tree's shape.
#include "okayscript/Environment.hpp"
#include "okayscript/Token.hpp"
#include "okayscript/Value.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace okayscript {

class Runtime;

struct Expr {
    int line = 0;
    virtual ~Expr() = default;
    virtual Value Eval(Runtime&) = 0;
    virtual std::string Dump() const = 0;
};
struct Stmt {
    int line = 0;
    virtual ~Stmt() = default;
    virtual void Exec(Runtime&) = 0;
    virtual std::string Dump() const = 0;
    /// Exec, and give a runtime error raised inside it this statement's line when it has none yet.
    void Run(Runtime& r);
};
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using StmtList = std::vector<StmtPtr>;

/// Control flow travels as these (not std::exceptions, so try/catch in scripts does not catch them).
struct ReturnSignal { Value value; };
struct BreakSignal {};
struct ContinueSignal {};

struct FunctionDecl {
    std::string name;
    std::vector<std::string> params;
    std::vector<unsigned char> paramKinds;   ///< `int dmg` converts / checks its argument
    std::vector<ExprPtr> defaults;           ///< null unless the parameter was written `a = expr`
    StmtList body;
    unsigned char returnKind = 0;            ///< `int Score()` converts what it returns
    int line = 0;
};

/// A parsed source file: its top-level statements and its functions (hoisted: callable before their definition).
struct Program {
    StmtList top;
    std::unordered_map<std::string, FunctionDecl> functions;
};

// dup-allow-begin: node layout of the engine VM's syntax tree (engine/src/Scripting/OkayScriptVM.cpp); one copy once the engine moves onto this library (docs/status/UNFINISHED.md "OkayScript standalone core")
// ---- Expressions -------------------------------------------------------------------------------------------------
struct NumberExpr : Expr { double v; explicit NumberExpr(double x) : v(x) {} Value Eval(Runtime&) override; std::string Dump() const override; };
struct StringExpr : Expr { std::string v; explicit StringExpr(std::string s) : v(std::move(s)) {} Value Eval(Runtime&) override; std::string Dump() const override; };
struct BoolExpr   : Expr { bool v; explicit BoolExpr(bool b) : v(b) {} Value Eval(Runtime&) override; std::string Dump() const override; };
/// $"text {expr} more"
struct InterpStringExpr : Expr {
    struct Seg { bool isExpr; std::string lit; ExprPtr expr; };
    std::vector<Seg> segs;
    void AddLiteral(const std::string& s) { if (!s.empty()) segs.push_back({false, s, nullptr}); }
    void AddExpr(ExprPtr e) { segs.push_back({true, "", std::move(e)}); }
    Value Eval(Runtime&) override; std::string Dump() const override;
};
/// A name; dotted names (`list.Count`, `Math.PI`) are one token and read a property of a variable, or a global.
struct VarExpr : Expr { std::string n; explicit VarExpr(std::string s) : n(std::move(s)) {} Value Eval(Runtime&) override; std::string Dump() const override; };
struct AssignExpr : Expr {
    std::string n; ExprPtr value;
    AssignExpr(std::string s, ExprPtr e) : n(std::move(s)), value(std::move(e)) {}
    Value Eval(Runtime&) override; std::string Dump() const override;
};
struct UnaryExpr : Expr {
    Tok op; ExprPtr e;
    UnaryExpr(Tok o, ExprPtr x) : op(o), e(std::move(x)) {}
    Value Eval(Runtime&) override; std::string Dump() const override;
};
/// && and || (the right side is only worked out when it decides the result).
struct LogicalExpr : Expr {
    Tok op; ExprPtr l, r;
    LogicalExpr(Tok o, ExprPtr a, ExprPtr b) : op(o), l(std::move(a)), r(std::move(b)) {}
    Value Eval(Runtime&) override; std::string Dump() const override;
};
/// a ?? b
struct CoalesceExpr : Expr {
    ExprPtr l, r;
    CoalesceExpr(ExprPtr a, ExprPtr b) : l(std::move(a)), r(std::move(b)) {}
    Value Eval(Runtime&) override; std::string Dump() const override;
};
/// x ??= value
struct NullishAssignExpr : Expr {
    std::string n; ExprPtr value;
    NullishAssignExpr(std::string s, ExprPtr e) : n(std::move(s)), value(std::move(e)) {}
    Value Eval(Runtime&) override; std::string Dump() const override;
};
struct BinaryExpr : Expr {
    Tok op; ExprPtr l, r;
    BinaryExpr(Tok o, ExprPtr a, ExprPtr b) : op(o), l(std::move(a)), r(std::move(b)) {}
    Value Eval(Runtime&) override; std::string Dump() const override;
};
/// expr.member (after `)` or `]`)
struct MemberExpr : Expr {
    ExprPtr obj; std::string name; bool safe = false;
    MemberExpr(ExprPtr o, std::string n, bool s = false) : obj(std::move(o)), name(std::move(n)), safe(s) {}
    Value Eval(Runtime&) override; std::string Dump() const override;
};
/// expr.member = value (a map's entry)
struct MemberAssignExpr : Expr {
    ExprPtr obj; std::string name; ExprPtr value;
    MemberAssignExpr(ExprPtr o, std::string n, ExprPtr v) : obj(std::move(o)), name(std::move(n)), value(std::move(v)) {}
    Value Eval(Runtime&) override; std::string Dump() const override;
};
/// (int)x, (float)x, (string)x, (bool)x, (char)x
struct CastExpr : Expr {
    std::string type; ExprPtr e;
    CastExpr(std::string t, ExprPtr x) : type(std::move(t)), e(std::move(x)) {}
    Value Eval(Runtime&) override; std::string Dump() const override;
};
/// expr.Method(args): a method of a string, list, map or number.
struct MethodCallExpr : Expr {
    ExprPtr obj; std::string name; std::vector<ExprPtr> args; bool safe = false;
    MethodCallExpr(ExprPtr o, std::string n, bool s = false) : obj(std::move(o)), name(std::move(n)), safe(s) {}
    Value Eval(Runtime&) override; std::string Dump() const override;
};
/// name(args) or name(param: value, ...)
struct CallExpr : Expr {
    std::string name; std::vector<ExprPtr> args;
    std::vector<std::string> argNames;   ///< parallel to args; empty string = positional
    explicit CallExpr(std::string n) : name(std::move(n)) {}
    Value Eval(Runtime&) override; std::string Dump() const override;
};
struct TernaryExpr : Expr {
    ExprPtr cond, a, b;
    TernaryExpr(ExprPtr c, ExprPtr x, ExprPtr y) : cond(std::move(c)), a(std::move(x)), b(std::move(y)) {}
    Value Eval(Runtime&) override; std::string Dump() const override;
};
/// [a, b, c] and { a, b, c }
struct ArrayExpr : Expr { std::vector<ExprPtr> elems; Value Eval(Runtime&) override; std::string Dump() const override; };
/// list[i], map["key"]
struct IndexExpr : Expr {
    ExprPtr arr, index; bool safe = false;
    IndexExpr(ExprPtr a, ExprPtr i, bool s = false) : arr(std::move(a)), index(std::move(i)), safe(s) {}
    Value Eval(Runtime&) override; std::string Dump() const override;
};
struct IndexAssignExpr : Expr {
    ExprPtr arr, index, value;
    IndexAssignExpr(ExprPtr a, ExprPtr i, ExprPtr v) : arr(std::move(a)), index(std::move(i)), value(std::move(v)) {}
    Value Eval(Runtime&) override; std::string Dump() const override;
};
/// list[i] op= value, list[i]++
struct IndexCompoundAssignExpr : Expr {
    ExprPtr arr, index, value; Tok op;
    IndexCompoundAssignExpr(ExprPtr a, ExprPtr i, Tok o, ExprPtr v)
        : arr(std::move(a)), index(std::move(i)), value(std::move(v)), op(o) {}
    Value Eval(Runtime&) override; std::string Dump() const override;
};
/// x => x * 2, (a, b) => { ... }. A lambda does not capture: it runs on top of the scopes of the code that calls it.
struct LambdaExpr : Expr {
    int id = 0;
    std::vector<std::string> params;
    std::vector<ExprPtr> defaults;
    StmtList body;
    Value Eval(Runtime&) override; std::string Dump() const override;
};

// ---- Statements --------------------------------------------------------------------------------------------------
struct ExprStmt : Stmt { ExprPtr e; explicit ExprStmt(ExprPtr x) : e(std::move(x)) {} void Exec(Runtime&) override; std::string Dump() const override; };
struct VarDeclStmt : Stmt {
    std::string n; ExprPtr init; unsigned char kind = KAny;
    VarDeclStmt(std::string s, ExprPtr e, unsigned char k = KAny) : n(std::move(s)), init(std::move(e)), kind(k) {}
    void Exec(Runtime&) override; std::string Dump() const override;
};
struct ReturnStmt : Stmt { ExprPtr e; explicit ReturnStmt(ExprPtr x) : e(std::move(x)) {} void Exec(Runtime&) override; std::string Dump() const override; };
struct BreakStmt : Stmt { void Exec(Runtime&) override; std::string Dump() const override; };
struct ContinueStmt : Stmt { void Exec(Runtime&) override; std::string Dump() const override; };
struct ThrowStmt : Stmt { ExprPtr e; explicit ThrowStmt(ExprPtr x) : e(std::move(x)) {} void Exec(Runtime&) override; std::string Dump() const override; };
/// try { ... } catch (e) { ... }: the catch variable holds the error's message.
struct TryStmt : Stmt {
    StmtList body, catchB; std::string catchVar; bool hasCatch = false;
    void Exec(Runtime&) override; std::string Dump() const override;
};
struct BlockStmt : Stmt { StmtList body; void Exec(Runtime&) override; std::string Dump() const override; };
struct IfStmt : Stmt { ExprPtr cond; StmtList thenB, elseB; void Exec(Runtime&) override; std::string Dump() const override; };
struct WhileStmt : Stmt { ExprPtr cond; StmtList body; void Exec(Runtime&) override; std::string Dump() const override; };
struct DoWhileStmt : Stmt { ExprPtr cond; StmtList body; void Exec(Runtime&) override; std::string Dump() const override; };
/// switch (x) { case 1: ...; break; default: ... } with C-style fall-through.
struct SwitchStmt : Stmt {
    struct Case { ExprPtr match; StmtList body; bool isDefault = false; };
    ExprPtr subject; std::vector<Case> cases;
    void Exec(Runtime&) override; std::string Dump() const override;
};
/// for (init; cond; step) body. The init runs in the current scope (function-level scoping).
struct ForStmt : Stmt { StmtPtr init; ExprPtr cond; ExprPtr step; StmtList body; void Exec(Runtime&) override; std::string Dump() const override; };
/// for x in list / foreach (var x in list): a list's items, or a map's keys (or {Key, Value} pairs).
struct ForEachStmt : Stmt {
    std::string var; ExprPtr iterable; StmtList body; bool pairs = false;
    void Exec(Runtime&) override; std::string Dump() const override;
};
/// int a = 1, b = 2;
struct MultiStmt : Stmt { StmtList list; void Exec(Runtime&) override; std::string Dump() const override; };

// dup-allow-end
/// The S-expression of a statement list: "(block s1 s2 ...)".
std::string DumpList(const StmtList& list);

} // namespace okayscript
