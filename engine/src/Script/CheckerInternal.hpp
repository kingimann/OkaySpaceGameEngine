// The script checker's class, shared by Checker.cpp and the Checker<Topic>.cpp files that hold its methods
// (docs/engine/modularization_plan.md, section 3.1). Private to engine/src/Script: not part of the script API.
#pragma once
#include "ScriptInternal.hpp"
#include <deque>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace okay::script::checker_detail {

int EditDistance(const std::string& a, const std::string& b);
std::string Suggest(const std::string& name, const std::vector<std::string>& options);

struct LocalVar {
    std::string name; Type type; int slot; SrcLoc loc; bool used = false; bool readOnly = false;
    bool captured = false;   ///< a lambda's copy of a variable from the code around it: slot = capture index
};

class Checker {
public:
    explicit Checker(Program& p) : prog_(p), api_(EngineApi::Get()) {}

    void Run();

private:
    Program& prog_;
    const EngineApi& api_;
    /// An engine binding's type in this program's terms (string[] gets the program's array type).
    Type FromApi(const Type& t);
    ClassDecl* cls_ = nullptr;
    MethodDecl* method_ = nullptr;
    std::vector<std::vector<LocalVar>> scopes_;
    int nextSlot_ = 0, maxSlot_ = 0, loopDepth_ = 0, switchDepth_ = 0;
    bool inFieldInit_ = false;
    bool inEnumValue_ = false;   ///< checking an enum member's value (fields aren't declared yet)
    std::vector<int> enumReady_;   ///< per enum: how many members have their value worked out

    // ---- Lambdas ----
    // A lambda is checked like a small method inside the code around it: its own
    // scopes and slots, plus captures for the variables it uses from outside.
    struct Outer {
        std::vector<std::vector<LocalVar>> scopes;
        std::deque<LocalVar> captures;
        LambdaDecl* lambda;
        int nextSlot, maxSlot, loopDepth, switchDepth;
        Type ret;
        bool inferRet;
        int retState;
    };
    std::vector<Outer> outer_;            ///< the code around the lambda being checked, innermost last
    LambdaDecl* lambda_ = nullptr;        ///< the lambda being checked
    std::deque<LocalVar> captures_;       ///< its captured variables (stable addresses)
    Type lambdaRet_;
    bool inferRet_ = false;               ///< its result type comes from its returns (Select, OrderBy)
    int retState_ = 0;                    ///< while inferring: 0 no return yet, 1 `return;`, 2 a value
    std::vector<Expr*> pending_;          ///< lambdas not yet given a Func / Action type

    bool HasErrors() const;
    int ClassIndex(const std::string& n) const;
    bool InComponent() const;
    int ClassId() const;
    // gameObject, transform, enabled and GetComponent belong to the object a Behaviour is on.
    bool RejectOutsideComponent(const std::string& name, SrcLoc loc);
    static std::string Values(std::size_t n);

    void DeclareMembers(ClassDecl& c);
    // The method's parameters take the call's (checked) arguments.
    bool ArgsFit(Expr& e, const MethodDecl& m);
    void Error(SrcLoc l, const std::string& m);
    void Warning(SrcLoc l, const std::string& m);
    std::string TypeName(const Type& t) const;

    int EnumIndex(const std::string& n) const;

    // Every enum's members get their values, in order: an explicit value must be a
    // fixed whole number; otherwise it is one more than the member before.
    void CheckEnums();

    // The value of a constant expression (literals, const fields, enum values and
    // operators on them), or nothing if it is worked out while playing.
    std::optional<Value> Fold(const Expr& e);
    // How a case value reads in a message.
    std::string ConstText(const Value& v, const Type& t) const;

    static bool IsSelfMemberName(const std::string& n);
    static bool HasAttr(const FieldDecl& f, const char* n);

    Type ResolveType(const TypeRef& t, bool allowVar);

    // List<T>, Dictionary<K, V> and KeyValuePair<K, V> (what foreach over a dictionary gives).
    Type ResolveGeneric(const TypeRef& t);
    static bool IsKeyType(const Type& t);
    const Program::Generic& Gen(const Type& t) const;

    void CheckLifecycleSignature(MethodDecl& m, int);

    // ---- Conversions ----
    static bool Convertible(const Type& from, const Type& to);
    std::string ConvertError(const Type& from, const Type& to, const std::string& what);
    // Check `e` can be used as `to`; mark int->float promotion.
    void Expect(Expr& e, const Type& to, const std::string& what);
    // Check a starting value, which may be a { ... } list when `to` is an array.
    void CheckInit(Expr& init, const Type& to, const std::string& what);
    void CheckItems(Expr& e, const Type& arr);

    // ---- Fields ----
    void CheckField(FieldDecl& f);
    static bool IsConstant(const Expr& e);

    // ---- Methods ----
    void CheckMethod(MethodDecl& m);
    // Lambdas that ended up somewhere without a Func or Action type.
    void FlushLambdas();

    void PushScope();
    void PopScope();
    static LocalVar* FindIn(std::vector<std::vector<LocalVar>>& scopes, std::deque<LocalVar>& captures, const std::string& n);
    // A variable by name. Inside a lambda, one from the code around it becomes a
    // capture of this lambda (and of every lambda in between).
    LocalVar* FindLocal(const std::string& n);
    LocalVar* FindAt(std::size_t level, const std::string& n);
    static LocalVar* AddCapture(LambdaDecl* l, std::deque<LocalVar>& captures, LocalVar& from);
    // Is `n` a variable here or in the code around this lambda? (Without capturing it.)
    bool NameTaken(const std::string& n);
    // The lambdas being checked use the object (its fields, methods or `this`).
    void UsesSelf();
    int Declare(const std::string& n, Type t, SrcLoc loc);
    int FieldIndex(const std::string& n) const;
    int MethodIdx(const std::string& n) const;

    // Statements; return whether execution can reach the end.
    bool CheckBlockBody(Stmt& b);
    bool CheckStmts(std::vector<StmtPtr>& body);
    bool CheckStmt(Stmt& s);

    bool CheckLambdaReturn(Stmt& s);

    // C#'s rules: the value is an int, string or enum; every case is a
    // fixed value of that type, none twice; one default at most; and no section
    // runs into the next (each ends in break, return or continue).
    bool CheckSwitch(Stmt& s);
    static bool IsTrueLiteral(const Expr& e);
    static bool ContainsBreak(const Stmt& s);
    void CheckCondition(Expr& e, const char* what);

    // ---- Expressions ----
    std::vector<std::string> VisibleNames();

    void CheckExpr(Expr& e);

    void Unpend(Expr& e);

    // A lambda used as `to` (a Func or Action).
    void CheckLambda(Expr& e, const Type& to, const std::string& what);
    // A lambda whose result type is worked out from its body (Select, OrderBy, Sum):
    // its Func type, or an error type. `who` names the method taking it.
    Type InferLambda(Expr& e, const std::vector<Type>& params, const std::string& who);
    static bool IsStatementLike(const Expr& v);
    // Check a lambda's body with these parameter types; returns its result type
    // (worked out from the body when `infer`).
    Type LambdaBody(Expr& e, const std::vector<Type>& params, Type ret, bool infer);

    // new[] { ... }: every item has the same type (ints mixed with floats make floats).
    void CheckImplicitArray(Expr& e);

    void CheckIndex(Expr& e);

    void CheckNewArray(Expr& e);

    // A static method runs without an object, so it can't use the object's own members.
    bool RejectInStatic(const std::string& name, SrcLoc loc);
    // `T` in T.member, when T is the script's class (and nothing local hides it).
    bool IsOwnClass(const Expr& e);

    // e.name as one of the current class's own members (a field, or the object's
    // gameObject / transform / enabled); false if it's neither.
    bool BindOwnMember(Expr& e);

    void CheckName(Expr& e);

    // Resolve `obj` as a member receiver: a static type, `this`, or a value.
    // Returns: 0 = value, 1 = static type (id in typeId), 2 = this, 3 = enum (id in typeId),
    // 4 = the script's own class (T.staticMember), -1 = error.
    int Receiver(Expr& obj, int& typeId);

    template <typename L>
    std::vector<std::string> Names(const std::vector<L>& v) { std::vector<std::string> n; for (auto& x : v) n.push_back(x.name); return n; }

    void CheckMember(Expr& e);

    // Pick the overload whose parameters fit the (already checked) arguments.
    const ApiMethod* PickOverload(const std::vector<ApiMethod>& ms, const std::string& owner, Expr& e);
    void BindArgs(Expr& e, const ApiMethod& m);

    // f(x) / f.Invoke(x): call the function `e.object` holds.
    void CheckInvoke(Expr& e);
    // Turn `name(...)` (or `obj.name(...)`) into calling the function stored in that variable or field.
    void AsInvoke(Expr& e, ExprPtr receiver);

    void CheckCall(Expr& e);

    void CheckGetComponent(Expr& e);

    // xs.Count, d.Keys / d.Values, kv.Key / kv.Value.
    void CheckCollectionMember(Expr& e, const Type& ot);

    // The LINQ-style methods of lists and arrays, and the List<T> methods that take
    // a lambda. False when `e` isn't one of them (the plain List methods come next).
    bool CheckLinq(Expr& e, const Type& ot, const Type& elem);

    // The methods of List<T> and Dictionary<K, V>, with the old runtime's names.
    void CheckCollectionCall(Expr& e, const Type& ot);

    // new List<int>() / new List<int> { 1, 2 } / new Dictionary<string, int>()
    void CheckNewCollection(Expr& e);

    void CheckNew(Expr& e);

    // new Node(...): the constructor whose parameters fit, or none (all fields at their starting values).
    void CheckNewObject(Expr& e, int ci);

    void CheckUnary(Expr& e);

    static bool CanBeNull(const Type& t);
    static std::string Article(const std::string& n);
    // ?. only on something that might be null: an object or an array.
    bool BadNullCond(Expr& e, int receiverKind);

    void CheckBinary(Expr& e);

    // Can `e` be assigned to? Explains why not.
    bool Assignable(Expr& e);

    void CheckAssign(Expr& e);

    void CheckIncDec(Expr& e);

    void CheckCast(Expr& e);

    void CheckConditional(Expr& e);
};

} // namespace okay::script::checker_detail
