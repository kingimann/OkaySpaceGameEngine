#pragma once
// The OkayScript syntax tree: plain data with no value type in it. The parser (Parser.hpp) builds it; the interpreter
// core (Runtime.hpp, a template over the value type) walks it by `kind`. The node set and what each one does are the ones of
// the engine's OkayScriptVM (engine/src/Scripting/OkayScriptVM.cpp) without its engine-only nodes (classes,
// coroutines, events, scene objects). Every node keeps the source line it started on, so runtime errors can say where.
//
// Dump() prints a node as a small S-expression, e.g. `1 + 2 * 3` -> "(+ 1 (* 2 3))", so tests can check the tree's shape.
#include "okayscript/Environment.hpp"
#include "okayscript/Token.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace okayscript {

enum class ExprKind {
    Number,
    String,
    Bool,
    InterpString,
    Var,
    Assign,
    Unary,
    Logical,
    Coalesce,
    NullishAssign,
    Binary,
    Member,
    MemberAssign,
    Cast,
    MethodCall,
    Call,
    Ternary,
    Array,
    Index,
    IndexAssign,
    IndexCompoundAssign,
    Lambda,
    New,
    EventOrCompound
};
enum class StmtKind {
    Expr,
    VarDecl,
    Return,
    Break,
    Continue,
    Throw,
    Try,
    Block,
    If,
    While,
    DoWhile,
    Switch,
    For,
    ForEach,
    Multi,
    Yield
};

struct Expr {
    const ExprKind kind;
    int line = 0;
    explicit Expr(ExprKind k) : kind(k) {}
    virtual ~Expr() = default;
    virtual std::string Dump() const = 0;
};
struct Stmt {
    const StmtKind kind;
    int line = 0;
    explicit Stmt(StmtKind k) : kind(k) {}
    virtual ~Stmt() = default;
    virtual std::string Dump() const = 0;
};
template <ExprKind K> struct ExprOf : Expr { ExprOf() : Expr(K) {} };
template <StmtKind K> struct StmtOf : Stmt { StmtOf() : Stmt(K) {} };
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using StmtList = std::vector<StmtPtr>;

/// How a statement ended: normally, or by return / break / continue. Control flow is a returned status, not an
/// exception, so it costs nothing on the hot path (a `return` value waits in BasicRuntime::retVal).
enum class Flow : unsigned char { Normal, Return, Break, Continue };

/// A body compiled to bytecode (Bytecode.hpp), made the first time the body runs and kept with the syntax tree.
struct Chunk;
using ChunkPtr = std::shared_ptr<const Chunk>;

struct FunctionDecl {
    std::string name;
    std::vector<std::string> params;
    std::vector<unsigned char> paramKinds;   ///< `int dmg` converts / checks its argument
    std::vector<ExprPtr> defaults;           ///< null unless the parameter was written `a = expr`
    StmtList body;
    unsigned char returnKind = 0;            ///< `int Score()` converts what it returns
    int line = 0;
    mutable ChunkPtr code;                   ///< `body` as bytecode, once it has run
    mutable std::vector<ChunkPtr> defaultCode;   ///< the defaults as bytecode, once used
};

/// A plain class (`class Item { ... }`, no base type). Instances are maps tagged {"__class": name}, so copies share
/// one object. (A class WITH a base, `class Hero : Behaviour`, is the script itself: its members are hoisted.)
struct FieldDef {
    std::string name;
    unsigned char kind = 0;
    bool isPublic = false;
    ExprPtr init;
    mutable ChunkPtr initCode;   ///< `init` as bytecode, once used
};
struct MethodDef {
    FunctionDecl decl;
    bool isPublic = false;
};
struct UserClass {
    std::string name;
    std::vector<FieldDef> fields;
    std::unordered_map<std::string, MethodDef> methods;
    std::unordered_map<int, MethodDef> ctors;   ///< one constructor per number of arguments
};

/// A parsed source file: its top-level statements, its functions (hoisted: callable before their definition) and
/// its plain classes.
struct Program {
    StmtList top;
    std::unordered_map<std::string, FunctionDecl> functions;
    std::unordered_map<std::string, UserClass> classes;
    /// Every name that can be a local variable somewhere in this program: parameters, and names declared (var, a
    /// typed declaration, foreach, catch) inside a function, method, constructor or lambda. A name that is not here
    /// can only be a global, a static or a field, so reading it skips the walk over the call stack's scopes.
    std::unordered_set<std::string> localNames;
    mutable ChunkPtr topCode;   ///< `top` as bytecode, once it has run
};
/// Fill prog.localNames (ParseSource does this).
void CollectLocalNames(Program& prog);
/// The names a function or lambda body declares (var, typed declarations, foreach, catch), in the order they first
/// appear, not looking into lambdas; `statics` gets the ones declared `static`. The compiler gives these slots.
void CollectBodyLocals(const StmtList& body, std::vector<std::string>& names, std::unordered_set<std::string>& statics);

// ---- Expressions -------------------------------------------------------------------------------------------------
struct NumberExpr : ExprOf<ExprKind::Number> { double v; explicit NumberExpr(double x) : v(x) {} std::string Dump() const override; };
struct StringExpr : ExprOf<ExprKind::String> { std::string v; explicit StringExpr(std::string s) : v(std::move(s)) {} std::string Dump() const override; };
struct BoolExpr : ExprOf<ExprKind::Bool> { bool v; explicit BoolExpr(bool b) : v(b) {} std::string Dump() const override; };
/// $"text {expr} more"
struct InterpStringExpr : ExprOf<ExprKind::InterpString> {
    struct Seg { bool isExpr; std::string lit; ExprPtr expr; };
    std::vector<Seg> segs;
    void AddLiteral(const std::string& s) { if (!s.empty()) segs.push_back({false, s, nullptr}); }
    void AddExpr(ExprPtr e) { segs.push_back({true, "", std::move(e)}); }
    std::string Dump() const override;
};
/// A name; dotted names (`list.Count`, `Math.PI`) are one token and read a property of a variable, or a global.
struct VarExpr : ExprOf<ExprKind::Var> {
    std::string n;
    bool dotted = false;      ///< has a '.' (worked out once here, not on every read)
    bool thisPrefix = false;  ///< starts with "this."
    explicit VarExpr(std::string s) : n(std::move(s)) {
        dotted = n.find('.') != std::string::npos;
        thisPrefix = n.rfind("this.", 0) == 0;
    }
    std::string Dump() const override;
};
struct AssignExpr : ExprOf<ExprKind::Assign> {
    std::string n; ExprPtr value;
    bool plain = true;   ///< no '.' and no "this." prefix (worked out once here, not on every assignment)
    AssignExpr(std::string s, ExprPtr e) : n(std::move(s)), value(std::move(e)) {
        plain = n.find('.') == std::string::npos;
    }
    std::string Dump() const override;
};
struct UnaryExpr : ExprOf<ExprKind::Unary> {
    Tok op; ExprPtr e;
    UnaryExpr(Tok o, ExprPtr x) : op(o), e(std::move(x)) {}
    std::string Dump() const override;
};
/// && and || (the right side is only worked out when it decides the result).
struct LogicalExpr : ExprOf<ExprKind::Logical> {
    Tok op; ExprPtr l, r;
    LogicalExpr(Tok o, ExprPtr a, ExprPtr b) : op(o), l(std::move(a)), r(std::move(b)) {}
    std::string Dump() const override;
};
/// a ?? b
struct CoalesceExpr : ExprOf<ExprKind::Coalesce> {
    ExprPtr l, r;
    CoalesceExpr(ExprPtr a, ExprPtr b) : l(std::move(a)), r(std::move(b)) {}
    std::string Dump() const override;
};
/// x ??= value
struct NullishAssignExpr : ExprOf<ExprKind::NullishAssign> {
    std::string n; ExprPtr value;
    NullishAssignExpr(std::string s, ExprPtr e) : n(std::move(s)), value(std::move(e)) {}
    std::string Dump() const override;
};
struct BinaryExpr : ExprOf<ExprKind::Binary> {
    Tok op; ExprPtr l, r;
    BinaryExpr(Tok o, ExprPtr a, ExprPtr b) : op(o), l(std::move(a)), r(std::move(b)) {}
    std::string Dump() const override;
};
/// expr.member (after `)` or `]`)
struct MemberExpr : ExprOf<ExprKind::Member> {
    ExprPtr obj; std::string name; bool safe = false;
    MemberExpr(ExprPtr o, std::string n, bool s = false) : obj(std::move(o)), name(std::move(n)), safe(s) {}
    std::string Dump() const override;
};
/// expr.member = value (a map's entry)
struct MemberAssignExpr : ExprOf<ExprKind::MemberAssign> {
    ExprPtr obj; std::string name; ExprPtr value;
    MemberAssignExpr(ExprPtr o, std::string n, ExprPtr v) : obj(std::move(o)), name(std::move(n)), value(std::move(v)) {}
    std::string Dump() const override;
};
/// (int)x, (float)x, (string)x, (bool)x, (char)x
struct CastExpr : ExprOf<ExprKind::Cast> {
    std::string type; ExprPtr e;
    CastExpr(std::string t, ExprPtr x) : type(std::move(t)), e(std::move(x)) {}
    std::string Dump() const override;
};
/// expr.Method(args): a method of a string, list, map or number.
struct MethodCallExpr : ExprOf<ExprKind::MethodCall> {
    ExprPtr obj; std::string name; std::vector<ExprPtr> args; bool safe = false;
    MethodCallExpr(ExprPtr o, std::string n, bool s = false) : obj(std::move(o)), name(std::move(n)), safe(s) {}
    std::string Dump() const override;
};
/// name(args) or name(param: value, ...)
struct CallExpr : ExprOf<ExprKind::Call> {
    std::string name; std::vector<ExprPtr> args;
    std::vector<std::string> argNames;   ///< parallel to args; empty string = positional
    explicit CallExpr(std::string n) : name(std::move(n)) {}
    std::string Dump() const override;
};
struct TernaryExpr : ExprOf<ExprKind::Ternary> {
    ExprPtr cond, a, b;
    TernaryExpr(ExprPtr c, ExprPtr x, ExprPtr y) : cond(std::move(c)), a(std::move(x)), b(std::move(y)) {}
    std::string Dump() const override;
};
/// [a, b, c] and { a, b, c }
struct ArrayExpr : ExprOf<ExprKind::Array> { std::vector<ExprPtr> elems; std::string Dump() const override; };
/// list[i], map["key"]
struct IndexExpr : ExprOf<ExprKind::Index> {
    ExprPtr arr, index; bool safe = false;
    IndexExpr(ExprPtr a, ExprPtr i, bool s = false) : arr(std::move(a)), index(std::move(i)), safe(s) {}
    std::string Dump() const override;
};
struct IndexAssignExpr : ExprOf<ExprKind::IndexAssign> {
    ExprPtr arr, index, value;
    IndexAssignExpr(ExprPtr a, ExprPtr i, ExprPtr v) : arr(std::move(a)), index(std::move(i)), value(std::move(v)) {}
    std::string Dump() const override;
};
/// list[i] op= value, list[i]++
struct IndexCompoundAssignExpr : ExprOf<ExprKind::IndexCompoundAssign> {
    ExprPtr arr, index, value; Tok op;
    IndexCompoundAssignExpr(ExprPtr a, ExprPtr i, Tok o, ExprPtr v)
        : arr(std::move(a)), index(std::move(i)), value(std::move(v)), op(o) {}
    std::string Dump() const override;
};
/// x => x * 2, (a, b) => { ... }. A lambda does not capture: it runs on top of the scopes of the code that calls it.
struct LambdaExpr : ExprOf<ExprKind::Lambda> {
    int id = 0;
    std::vector<std::string> params;
    std::vector<ExprPtr> defaults;
    StmtList body;
    mutable ChunkPtr code;   ///< `body` as bytecode, once it has run
    mutable std::vector<ChunkPtr> defaultCode;   ///< the defaults as bytecode, once used
    std::string Dump() const override;
};

/// new Item(args): a plain class instance.
struct NewExpr : ExprOf<ExprKind::New> {
    std::string type; std::vector<ExprPtr> args;
    explicit NewExpr(std::string t) : type(std::move(t)) {}
    std::string Dump() const override;
};
/// `name += rhs` / `name -= rhs`: subscribes / unsubscribes a function or lambda to the event `name` when rhs is one
/// (OnHit += Handler), and is plain compound assignment otherwise (score += 1).
struct EventOrCompoundExpr : ExprOf<ExprKind::EventOrCompound> {
    std::string name; ExprPtr rhs; bool remove; Tok bin;
    EventOrCompoundExpr(std::string n, ExprPtr r, bool rem, Tok b) : name(std::move(n)), rhs(std::move(r)), remove(rem), bin(b) {}
    std::string Dump() const override;
};

// ---- Statements --------------------------------------------------------------------------------------------------
struct ExprStmt : StmtOf<StmtKind::Expr> { ExprPtr e; explicit ExprStmt(ExprPtr x) : e(std::move(x)) {} std::string Dump() const override; };
struct VarDeclStmt : StmtOf<StmtKind::VarDecl> {
    std::string n; ExprPtr init; unsigned char kind = KAny;
    VarDeclStmt(std::string s, ExprPtr e, unsigned char k = KAny) : n(std::move(s)), init(std::move(e)), kind(k) {}
    std::string Dump() const override;
};
struct ReturnStmt : StmtOf<StmtKind::Return> { ExprPtr e; explicit ReturnStmt(ExprPtr x) : e(std::move(x)) {} std::string Dump() const override; };
struct BreakStmt : StmtOf<StmtKind::Break> { std::string Dump() const override; };
struct ContinueStmt : StmtOf<StmtKind::Continue> { std::string Dump() const override; };
struct ThrowStmt : StmtOf<StmtKind::Throw> { ExprPtr e; explicit ThrowStmt(ExprPtr x) : e(std::move(x)) {} std::string Dump() const override; };
/// try { ... } catch (e) { ... }: the catch variable holds the error's message.
struct TryStmt : StmtOf<StmtKind::Try> {
    StmtList body, catchB; std::string catchVar; bool hasCatch = false;
    std::string Dump() const override;
};
struct BlockStmt : StmtOf<StmtKind::Block> { StmtList body; std::string Dump() const override; };
struct IfStmt : StmtOf<StmtKind::If> { ExprPtr cond; StmtList thenB, elseB; std::string Dump() const override; };
struct WhileStmt : StmtOf<StmtKind::While> { ExprPtr cond; StmtList body; std::string Dump() const override; };
struct DoWhileStmt : StmtOf<StmtKind::DoWhile> { ExprPtr cond; StmtList body; std::string Dump() const override; };
/// switch (x) { case 1: ...; break; default: ... } with C-style fall-through.
struct SwitchStmt : StmtOf<StmtKind::Switch> {
    struct Case { ExprPtr match; StmtList body; bool isDefault = false; };
    ExprPtr subject; std::vector<Case> cases;
    std::string Dump() const override;
};
/// for (init; cond; step) body. The init runs in the current scope (function-level scoping).
struct ForStmt : StmtOf<StmtKind::For> { StmtPtr init; ExprPtr cond; ExprPtr step; StmtList body; std::string Dump() const override; };
/// for x in list / foreach (var x in list): a list's items, or a map's keys (or {Key, Value} pairs).
struct ForEachStmt : StmtOf<StmtKind::ForEach> {
    std::string var; ExprPtr iterable; StmtList body; bool pairs = false;
    std::string Dump() const override;
};
/// int a = 1, b = 2;
struct MultiStmt : StmtOf<StmtKind::Multi> { StmtList list; std::string Dump() const override; };

/// yield return <wait>; (a coroutine hands control back to its host)
struct YieldStmt : StmtOf<StmtKind::Yield> { ExprPtr e; std::string Dump() const override; };
/// The S-expression of a statement list: "(block s1 s2 ...)".
std::string DumpList(const StmtList& list);

} // namespace okayscript
