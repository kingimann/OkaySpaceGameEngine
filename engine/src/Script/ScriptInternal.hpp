#pragma once
// ---------------------------------------------------------------------------
// OkayScript: a C#-style scripting language that runs on OkaySpace's engine.
//
// Pipeline: source -> Lexer (tokens with line/column) -> Parser (syntax tree)
// -> Checker (every name resolved, every type known, diagnostics) -> Interpreter
// (walks the checked tree). Nothing reaches the interpreter unless it passed the
// checker, so a script either works or says why it won't.
//
// Engine access goes through one registry of bindings (EngineApi.cpp): each
// type, property and method there is backed by a real engine call. The checker
// uses the registry to resolve members and the editor uses it for autocomplete,
// so a name that autocompletes is a name that runs.
// ---------------------------------------------------------------------------
#include "okay/Math/Vec3.hpp"
#include "okay/Scripting/ScriptVM.hpp"   // ScriptDiagnostic
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace okay {
class GameObject;
class Scene;
class Component;
}

namespace okay::script {

struct SrcLoc { int line = 0, col = 0; };

// ---- Types -----------------------------------------------------------------
enum class TK : std::uint8_t {
    Error,    ///< couldn't be worked out (an error was already reported)
    Void,
    Null,     ///< the type of the `null` literal
    Bool, Int, Float, String,
    Vector3,
    Object,   ///< an engine reference type (GameObject, Transform, ...): id = ApiType index
    Any,      ///< parameter that accepts every type (Debug.Log)
    Enum,     ///< a script's enum: id = Program::enums index; an int at runtime
    Array,    ///< T[]: id = Program::arrays index (the element type)
    Class,    ///< a plain class from the script: id = Program::classes index
    List,     ///< List<T>: id = Program::generics index (a = T)
    Dict,     ///< Dictionary<K, V>: id = Program::generics index (a = K, b = V)
    Pair,     ///< KeyValuePair<K, V> (foreach over a dictionary): id as Dict
    Func,     ///< Func<..., R> / Action<...>: id = Program::funcs index
    Lambda,   ///< a lambda whose Func / Action type comes from where it's used (checker only)
};
struct Type {
    TK  kind = TK::Error;
    int id = -1;
    bool operator==(const Type& o) const { return kind == o.kind && id == o.id; }
    bool operator!=(const Type& o) const { return !(*this == o); }
    bool IsNumeric() const { return kind == TK::Int || kind == TK::Float; }
    static Type Of(TK k) { return Type{k, -1}; }
    static Type Obj(int apiType) { return Type{TK::Object, apiType}; }
};

// ---- Runtime values -------------------------------------------------------
/// A reference to an engine object by id: the GameObject, plus which of its
/// components (an ApiType index; the GameObject itself uses its own index).
/// Resolved on every use, so a destroyed object reads as "destroyed", never as
/// freed memory.
struct ObjRef {
    std::uint64_t go = 0;
    int type = -1;
    bool operator==(const ObjRef& o) const { return go == o.go && type == o.type; }
};
struct ArrayObj;
struct ScriptObj;
struct DictObj;
struct FuncObj;
struct Cell;
using ArrayRef = std::shared_ptr<ArrayObj>;
using ObjPtr = std::shared_ptr<ScriptObj>;
using DictRef = std::shared_ptr<DictObj>;
using FuncRef = std::shared_ptr<FuncObj>;
using CellRef = std::shared_ptr<Cell>;
/// CellRef only appears in a method's local slots (a local that a lambda captured); reading the local gives what's inside.
using Value = std::variant<std::monostate, bool, std::int32_t, float, std::string, Vec3, ObjRef, ArrayRef, ObjPtr, DictRef, FuncRef, CellRef>;
/// A T[], a List<T> or a KeyValuePair (items = key, value) at runtime. Arrays
/// and lists are references: copies share the items.
struct ArrayObj {
    std::vector<Value> items;
    std::string typeName;      ///< "int[]", "List<int>": printed when it's turned into text
    std::uint32_t version = 0; ///< a list's adds and removes (foreach notices changes)
    ~ArrayObj();
};
/// Orders dictionary keys (int, string, bool, enum, object references).
struct KeyLess { bool operator()(const Value& a, const Value& b) const; };
/// A Dictionary<K, V>: entries in the order they were added, with an index by key.
struct DictObj {
    std::vector<std::pair<Value, Value>> entries;
    std::map<Value, std::size_t, KeyLess> index;   ///< key -> position in entries
    std::string typeName;
    std::uint32_t version = 0;
    ~DictObj();
};
/// An object of one of the script's plain classes (new Node(...)): a reference, like an array.
struct ScriptObj {
    int cls = -1;                ///< Program::classes index
    std::string className;       ///< printed when the object is turned into text
    std::vector<Value> fields;
    ~ScriptObj();                ///< frees long chains (linked lists) without deep recursion
};

/// A local variable shared between a method and the lambdas that use it.
struct Cell { Value v; };

struct Program;
struct LambdaDecl;
class Instance;
/// A lambda made while running: its code, the variables it captured and the object it runs for.
struct FuncObj {
    const LambdaDecl* decl = nullptr;
    std::shared_ptr<const Program> prog;   ///< keeps decl alive
    std::vector<CellRef> captures;
    Instance* inst = nullptr;              ///< the script object that made it; only used while `alive` holds
    std::weak_ptr<char> alive;
    ObjPtr self;                           ///< made inside a plain class's method: that object
    int cls = -1;
    std::string typeName;
    /// a += b: the functions it calls, in order (decl is null). Never nested.
    std::vector<FuncRef> list;
};

std::string ValueToString(const Value& v);

/// A runtime failure inside a script (destroyed object, division by zero, ...),
/// reported with the line it happened on.
struct RuntimeError : std::runtime_error {
    SrcLoc loc;
    bool fatal = false;   ///< an endless loop or recursion: catch can't stop it
    RuntimeError(const std::string& m, SrcLoc l, bool isFatal = false) : std::runtime_error(m), loc(l), fatal(isFatal) {}
};
/// A script's `throw`: the Exception object. Uncaught, it's reported like any runtime error.
struct ScriptThrow : RuntimeError {
    ObjPtr ex;
    ScriptThrow(const std::string& m, SrcLoc l, ObjPtr e) : RuntimeError(m, l), ex(std::move(e)) {}
};

// ---- Engine bindings -------------------------------------------------------
/// What a binding needs from the running script: the object it's attached to.
struct CallContext {
    GameObject* self = nullptr;
    Component* component = nullptr;   ///< the Script component running this code
    float deltaTime = 0.0f;
    float fixedDeltaTime = 0.02f;
    SrcLoc loc;
};

struct ApiProperty {
    std::string name;
    Type type;
    std::function<Value(const Value& self, CallContext&)> get;
    std::function<void(const Value& self, const Value& v, CallContext&)> set;   ///< empty = read-only
    std::string doc;
};
struct ApiMethod {
    std::string name;
    std::vector<Type> params;
    std::vector<std::string> paramNames;
    Type ret;
    std::function<Value(const Value& self, std::vector<Value>& args, CallContext&)> fn;
    std::string doc;
    /// Optional compile-time check of literal arguments (key names, axis names):
    /// returns an error message, or "" when fine. `literal` is empty for non-literals.
    std::function<std::string(int argIndex, const std::string& literal)> checkLiteral;
    /// No side effects (maths, queries): the visual-script node generated from it
    /// is a value node instead of a step in the flow.
    bool pure = false;
};
struct ApiType {
    std::string name;
    bool isComponent = false;   ///< reachable through GetComponent<T>()
    bool isStatic = false;      ///< only static members (Input, Time, Debug, Mathf)
    bool isValueType = false;   ///< Vector3
    std::vector<ApiProperty> props, staticProps;
    std::vector<ApiMethod> methods, staticMethods;
    std::string doc;
    /// Components only: the component of this type on `go`, or nullptr.
    std::function<Component*(GameObject* go)> find;
};

/// The engine binding registry (built once).
struct EngineApi {
    std::vector<ApiType> types;
    int gameObject = -1, transform = -1, vector3 = -1, rigidbody = -1;
    int Find(const std::string& name) const;
    static const EngineApi& Get();
};

/// The GameObject behind a reference, or throws a RuntimeError at `loc`.
GameObject* Resolve(const ObjRef& r, SrcLoc loc);
Value MakeRef(GameObject* go, int apiType);

// ---- Tokens ----------------------------------------------------------------
enum class T : std::uint8_t {
    End, Ident, IntLit, FloatLit, StringLit,
    LParen, RParen, LBrace, RBrace, LBracket, RBracket, Comma, Dot, Semicolon, Colon, Question,
    Plus, Minus, Star, Slash, Percent, Not, Lt, Gt, Le, Ge, EqEq, NotEq, AndAnd, OrOr,
    Assign, PlusEq, MinusEq, StarEq, SlashEq, PercentEq, PlusPlus, MinusMinus,
    QuestionDot, QuestionQuestion, QuestionQuestionEq, Arrow /* => */,
    // keywords
    KwClass, KwPublic, KwPrivate, KwVoid, KwInt, KwFloat, KwBool, KwString, KwVar,
    KwIf, KwElse, KwWhile, KwFor, KwReturn, KwBreak, KwContinue, KwTrue, KwFalse, KwNull,
    KwNew, KwThis, KwConst,
    // $"..." (see Lex): InterpBegin, then StringLit parts and InterpHole <tokens> [InterpFormat] InterpHoleEnd, then InterpEnd
    InterpBegin, InterpHole, InterpFormat, InterpHoleEnd, InterpEnd,
};
struct Token {
    T type = T::End;
    std::string text;
    SrcLoc loc;
};
/// Tokenize; lexical errors (bad character, unterminated string) go to `diags`.
std::vector<Token> Lex(const std::string& src, std::vector<ScriptDiagnostic>& diags);
const char* TokenName(T t);

// ---- Syntax tree -----------------------------------------------------------
struct TypeRef {
    std::string name;     ///< "int", "float", "Vector3", "GameObject", "var", "void", ...
    std::vector<TypeRef> args;   ///< List<int>: {int}; Dictionary<string, int>: {string, int}
    int rank = 0;         ///< number of [] after the name: int[] = 1, int[][] = 2
    SrcLoc loc;
};

enum class EK : std::uint8_t {
    IntLit, FloatLit, StringLit, BoolLit, NullLit, This,
    Name,          ///< bare identifier (local, field, method group, type)
    Member,        ///< a.b
    Call,          ///< f(args) / a.f(args) / GetComponent<T>()
    New,           ///< new Vector3(...)
    Unary, Binary, Assign, IncDec, Cast, Conditional,
    Index,         ///< a[i]: object = the array, lhs = the index
    NewArray,      ///< new T[n] / new T[] { ... }: typeArg = the array type, lhs = n (may be null), args = items
    Interp,        ///< $"...": args = the parts (text literals and { } values), joined as text
    Format,        ///< {value:F2} in $"...": lhs = the number, sval = "F2", ival = decimals (checked)
    NullCond,      ///< a chain with ?. in it: lhs = the chain, null if a ?. found null; name = what was null
    ArrayLit,      ///< { a, b } after `T[] x =`: the type comes from the declaration
    Is,            ///< lhs is T: typeArg = T
    As,            ///< lhs as T: typeArg = T
    Lambda,        ///< x => ..., (a, b) => { ... }: `lambda` holds the parameters and body
};

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;
struct LambdaDecl;

/// What a checked name/member refers to (filled by the Checker).
enum class Bind : std::uint8_t {
    None, Local, Field, Method, SelfMember /*gameObject/transform/enabled*/,
    ApiProp, ApiStaticProp, ApiMethod, ApiStaticMethod, StaticType,
    VecField /*x/y/z*/, VecProp /*magnitude/normalized*/, GetComponent, Destroy, StaticField,
    EnumValue /*State.Idle: ival*/, ArrayLength,
    CollCount /*list / dictionary .Count*/, CollMethod /*xs.Add(...): member = CollOp*/,
    DictKeys, DictValues, PairKey, PairValue,
    Captured /*a variable of the method around a lambda: slot = capture index*/,
    Invoke /*f(x), f.Invoke(x): object = the function*/,
    NewCollection /*new List<int>(), new Dictionary<K, V>(); args = { ... } items*/,
    ObjField /*obj.field: slot = field (in the object's fields, bases first)*/,
    ObjMethod /*obj.M(), and M() inside a plain class: slot = the method's name id, run as the object's own class has it*/,
    NewObject /*new Node(): slot = class, member = constructor (-1 = none)*/,
    BaseMethod /*base.M(): slot = method, member = the class that has it*/,
    BaseCtor /*: base(...) of a constructor: slot = the base class, member = its constructor (-1 = none)*/,
};
/// Methods of List<T> and Dictionary<K, V> (Bind::CollMethod's member).
enum class CollOp : std::uint8_t {
    ListAdd, ListAddRange, ListInsert, ListRemove, ListRemoveAt, ListClear, ListContains, ListIndexOf,
    ListSort, ListReverse, ListToArray,
    DictAdd, DictTryAdd, DictRemove, DictContainsKey, DictContainsValue, DictClear, DictGetValueOrDefault,
    // LINQ-style, on lists and arrays (a lambda or nothing as the argument)
    Where, Select, OrderBy, OrderByDescending, First, FirstOrDefault, Last, LastOrDefault, Any, All, Count,
    Sum, Min, Max, Average, ToList,
    // List<T> methods that take a lambda
    Find, FindAll, FindIndex, Exists, RemoveAll, ForEach, SortBy,
};

struct Expr {
    EK kind;
    SrcLoc loc;
    // Literal payloads
    std::int32_t ival = 0; float fval = 0; bool bval = false; std::string sval;
    // Name / member / call
    std::string name;
    ExprPtr object;                 ///< Member/Call receiver (null = unqualified)
    std::vector<ExprPtr> args;
    TypeRef typeArg;                ///< GetComponent<T>, new T, (T)cast
    // Operators
    T op = T::End;                  ///< Unary/Binary/Assign/IncDec operator
    bool prefix = false;            ///< IncDec
    ExprPtr lhs, rhs, third;        ///< Binary lhs/rhs; Assign target/value; Conditional cond/a/b
    // ---- filled by the Checker ----
    Type type;
    Bind bind = Bind::None;
    int slot = -1;                  ///< local slot / field index / method index / api type
    int member = -1;                ///< api prop / method index, vec field 0..2
    bool isStaticRecv = false;
    bool promoteL = false, promoteR = false;   ///< int -> float before the operator
    bool toFloat = false;           ///< convert this (int) result to float where it's used
    int enumStr = -1;               ///< an enum (Program::enums index) used as text: print the member's name
    bool hasInit = false;           ///< NewArray: has a { ... } list
    bool nullCond = false;          ///< Member/Call reached with ?. (its receiver may be null)
    std::unique_ptr<LambdaDecl> lambda;   ///< Lambda
};

enum class SK : std::uint8_t { Block, Expr, Local, If, While, For, Return, Break, Continue, Switch, Foreach, DoWhile, Try, Throw };
struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;
struct LocalDecl { TypeRef type; std::string name; SrcLoc loc; ExprPtr init; int slot = -1; Type resolved; bool promote = false; };
/// One `case a: case b: ... statements` group of a switch.
struct SwitchSection {
    std::vector<ExprPtr> labels;        ///< the case values
    std::vector<SrcLoc> labelLocs;      ///< where each `case` is
    bool isDefault = false;
    SrcLoc loc, defaultLoc;
    std::vector<StmtPtr> body;
    std::vector<Value> values;          ///< checked: the labels' constant values
};
/// catch (T name) { ... } of a try: T and name are optional.
struct CatchClause {
    TypeRef type;
    bool typed = false;
    std::string name;
    SrcLoc loc, nameLoc;
    StmtPtr body;
    // checked
    Type resolved;                      ///< the Exception class it catches
    int slot = -1;                      ///< the variable's local slot, -1 = none
};
struct Stmt {
    SK kind;
    SrcLoc loc;
    std::vector<StmtPtr> body;          ///< Block
    ExprPtr expr;                       ///< Expr / If cond / While cond / For cond / Return value
    std::vector<LocalDecl> locals;      ///< Local
    StmtPtr then, otherwise;            ///< If
    StmtPtr init;                       ///< For init (Local or Expr stmt)
    std::vector<ExprPtr> step;          ///< For step expressions
    StmtPtr loopBody;                   ///< While / For / Foreach / DoWhile
    bool promoteReturn = false;
    std::vector<SwitchSection> sections; ///< Switch (expr = the value switched on)
    // Foreach: locals[0] is the loop variable, expr the array.
    std::vector<CatchClause> catches;   ///< Try (then = the try block)
    StmtPtr finallyBody;                ///< Try
    // Throw: expr = the exception (null: `throw;` again in a catch).
};

struct Attribute { std::string name; std::vector<ExprPtr> args; SrcLoc loc; };

struct FieldDecl {
    std::vector<Attribute> attrs;
    bool isPublic = false, isProtected = false, isConst = false, isStatic = false;   ///< static: one value shared by every object running the script
    bool isEvent = false;   ///< outside its class it can only be added to (+=) or removed from (-=)
    TypeRef type;
    std::string name;
    SrcLoc loc;
    ExprPtr init;
    // checked
    Type resolved;
    int index = -1;       ///< slot in an object's fields: the classes it extends come first
    bool promote = false;
    bool read = false;
};
struct Param { TypeRef type; std::string name; SrcLoc loc; Type resolved; };
struct LambdaDecl {
    std::vector<Param> params;
    ExprPtr value;                  ///< x => value
    std::unique_ptr<Stmt> body;     ///< x => { body }
    // checked
    Type ret;                       ///< void for an Action
    int localCount = 0;
    /// Where each captured variable comes from when the lambda is made: a local
    /// slot of the code around it (>= 0), or that code's own capture -1 - i.
    std::vector<int> captureFrom;
    bool usesSelf = false;          ///< uses the object's fields, methods or `this`
    /// Made from a method's name (Died += OnDied): that method, so -= OnDied finds it.
    int method = -1;
};
struct MethodDecl {
    bool isPublic = false, isProtected = false, isStatic = false;   ///< static: can't use the object's own fields, methods or members
    bool isCtor = false;                       ///< a plain class's constructor (name = the class)
    bool isVirtual = false, isOverride = false, isAbstract = false;   ///< abstract (and interface) methods have no body
    std::vector<ExprPtr> baseArgs;             ///< a constructor's : base(...)
    bool hasBaseCall = false;
    SrcLoc baseLoc;
    TypeRef ret;
    std::string name;
    SrcLoc loc;
    std::vector<Param> params;
    std::unique_ptr<Stmt> body;
    // checked
    Type retType;
    int localCount = 0;
    bool called = false;
    int lifecycle = -1;   ///< index into the lifecycle table, -1 = an ordinary method
    int nameId = -1;      ///< Program::methodNames index (dispatch by name)
    ExprPtr baseCall;     ///< constructor of a class that extends another: the base constructor call (Bind::BaseCtor)
};
struct EnumMember { std::string name; SrcLoc loc; ExprPtr value; std::int32_t v = 0; };
struct EnumDecl {
    std::string name;
    SrcLoc loc;
    std::vector<EnumMember> members;
    /// The member's name for `v`, or "" if no member has that value.
    const std::string* NameOf(std::int32_t v) const {
        for (auto& m : members) if (m.v == v) return &m.name;
        return nullptr;
    }
};

struct ClassDecl {
    std::string name;
    std::vector<std::string> bases;   ///< after ':': Behaviour, or a class and / or interfaces
    std::vector<SrcLoc> baseLocs;
    bool isAbstract = false, isInterface = false;
    bool builtin = false;            ///< Exception, which every script has
    SrcLoc loc;
    std::vector<FieldDecl> fields;
    std::vector<MethodDecl> methods;
    // checked
    int baseClass = -1;              ///< the class it extends
    std::vector<int> interfaces;     ///< the interfaces it names
    int fieldCount = 0;              ///< fields of an object of this class, the bases' included
    std::vector<char> isA;           ///< by class index: this class is (extends or implements) that one
    /// By method name id: the class and method an object of this class runs ({-1, -1}: none).
    std::vector<std::pair<int, int>> dispatch;
};

/// `static` fields' values: one set shared by every object running the same
/// script in a scene, the first of them setting the starting values.
struct StaticStore {
    std::vector<Value> values;   ///< by field index (instance fields' slots stay unused)
    bool ready = false;
};
/// The store for scripts with this exact source in `scene` (made on first use).
/// It lasts until the scene is cleared or destroyed (ForgetSceneStatics).
std::shared_ptr<StaticStore> SceneStatics(const Scene* scene, const std::string& source);
void ForgetSceneStatics(const Scene* scene);

/// A checked, runnable script.
struct Program {
    std::vector<ClassDecl> classes;
    std::vector<EnumDecl> enums;   ///< every enum in the file (top level or inside a class)
    std::vector<Type> arrays;      ///< element type of each array type (Type{Array, i})
    /// List / Dictionary / KeyValuePair types: their type arguments.
    struct Generic { TK kind; Type a, b; };
    std::vector<Generic> generics;
    /// Func / Action types: parameter types and result (void for an Action).
    struct FuncSig { std::vector<Type> params; Type ret; };
    std::vector<FuncSig> funcs;
    std::vector<std::string> methodNames;   ///< every method name in the file (MethodDecl::nameId)
    int component = -1;            ///< the class that is the component
    int exceptionClass = -1;       ///< Exception (built in, or the script's own class of that name)
    std::vector<ScriptDiagnostic> diags;
    bool ok() const;
    const ClassDecl* Component() const { return component >= 0 ? &classes[(std::size_t)component] : nullptr; }
    int MethodIndex(const std::string& name) const;
    /// The array type with elements of `elem` (made on first use).
    Type ArrayOf(const Type& elem);
    /// The List<a> / Dictionary<a, b> / KeyValuePair<a, b> type (made on first use).
    Type GenericOf(TK kind, const Type& a, const Type& b = Type{});
    /// The Func<params..., ret> (or Action<params...> when ret is void) type (made on first use).
    Type FuncOf(const std::vector<Type>& params, const Type& ret);
    /// TypeName that also knows this program's enums and arrays.
    std::string NameOf(const Type& t) const;
};

/// Parse + check `src`. The program is runnable only if it has no errors.
std::shared_ptr<Program> Compile(const std::string& src);
/// Parser entry (used by Compile).
void Parse(const std::vector<Token>& toks, Program& out);
/// Checker entry (used by Compile).
void Check(Program& prog);

/// The lifecycle methods the engine calls, by name.
struct Lifecycle { const char* name; const char* when; };
const std::vector<Lifecycle>& LifecycleMethods();

std::string TypeName(const Type& t);

// ---- Running ---------------------------------------------------------------
/// One running instance of a component class.
class Instance {
public:
    Instance(std::shared_ptr<Program> prog, GameObject* self);
    /// Run the field initializers. Throws RuntimeError.
    void Init();
    /// Share `static` fields through `store` from now on, setting their starting
    /// values if it's new. Until then the instance has statics of its own.
    void AttachStatics(std::shared_ptr<StaticStore> store);
    /// Call a method by index with args. Throws RuntimeError.
    Value Call(int method, std::vector<Value> args);
    bool HasMethod(const std::string& name) const { return prog_->MethodIndex(name) >= 0; }
    Value GetField(const std::string& name) const;
    bool SetField(const std::string& name, const Value& v);
    CallContext& Context() { return ctx_; }
    const Program& program() const { return *prog_; }
    const std::shared_ptr<Program>& programPtr() const { return prog_; }
    /// Expires when this instance is destroyed (lambdas it made check it before running).
    std::weak_ptr<char> AliveToken() const { return alive_; }
    std::vector<Value>& Fields() { return fields_; }
    std::vector<Value>& Statics() { return statics_->values; }
    /// Statements executed in the current callback (a guard against endless loops).
    std::uint64_t steps = 0;
    std::uint64_t stepLimit = 50'000'000;
private:
    void InitStatics();
    std::shared_ptr<Program> prog_;
    std::vector<Value> fields_;
    std::shared_ptr<StaticStore> statics_;
    CallContext ctx_;
    std::shared_ptr<char> alive_ = std::make_shared<char>(0);
};

} // namespace okay::script
