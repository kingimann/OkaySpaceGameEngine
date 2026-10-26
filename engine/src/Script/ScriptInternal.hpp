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
using ArrayRef = std::shared_ptr<ArrayObj>;
using Value = std::variant<std::monostate, bool, std::int32_t, float, std::string, Vec3, ObjRef, ArrayRef>;
/// A T[] at runtime. Arrays are references: copies share the items.
struct ArrayObj {
    std::vector<Value> items;
    std::string typeName;   ///< "int[]", printed when the array itself is turned into text
};

std::string ValueToString(const Value& v);

/// A runtime failure inside a script (destroyed object, division by zero, ...),
/// reported with the line it happened on.
struct RuntimeError : std::runtime_error {
    SrcLoc loc;
    RuntimeError(const std::string& m, SrcLoc l) : std::runtime_error(m), loc(l) {}
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
    std::string generic;  ///< reserved (List<T> later)
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
    ArrayLit,      ///< { a, b } after `T[] x =`: the type comes from the declaration
};

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

/// What a checked name/member refers to (filled by the Checker).
enum class Bind : std::uint8_t {
    None, Local, Field, Method, SelfMember /*gameObject/transform/enabled*/,
    ApiProp, ApiStaticProp, ApiMethod, ApiStaticMethod, StaticType,
    VecField /*x/y/z*/, VecProp /*magnitude/normalized*/, GetComponent, Destroy, StaticField,
    EnumValue /*State.Idle: ival*/, ArrayLength,
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
};

enum class SK : std::uint8_t { Block, Expr, Local, If, While, For, Return, Break, Continue, Switch, Foreach };
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
struct Stmt {
    SK kind;
    SrcLoc loc;
    std::vector<StmtPtr> body;          ///< Block
    ExprPtr expr;                       ///< Expr / If cond / While cond / For cond / Return value
    std::vector<LocalDecl> locals;      ///< Local
    StmtPtr then, otherwise;            ///< If
    StmtPtr init;                       ///< For init (Local or Expr stmt)
    std::vector<ExprPtr> step;          ///< For step expressions
    StmtPtr loopBody;                   ///< While / For / Foreach
    bool promoteReturn = false;
    std::vector<SwitchSection> sections; ///< Switch (expr = the value switched on)
    // Foreach: locals[0] is the loop variable, expr the array.
};

struct Attribute { std::string name; std::vector<ExprPtr> args; SrcLoc loc; };

struct FieldDecl {
    std::vector<Attribute> attrs;
    bool isPublic = false, isConst = false, isStatic = false;   ///< static: one value shared by every object running the script
    TypeRef type;
    std::string name;
    SrcLoc loc;
    ExprPtr init;
    // checked
    Type resolved;
    int index = -1;
    bool promote = false;
    bool read = false;
};
struct Param { TypeRef type; std::string name; SrcLoc loc; Type resolved; };
struct MethodDecl {
    bool isPublic = false, isStatic = false;   ///< static: can't use the object's own fields, methods or members
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
    std::string name, base;
    SrcLoc loc, baseLoc;
    std::vector<FieldDecl> fields;
    std::vector<MethodDecl> methods;
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
    int component = -1;            ///< the class that is the component
    std::vector<ScriptDiagnostic> diags;
    bool ok() const;
    const ClassDecl* Component() const { return component >= 0 ? &classes[(std::size_t)component] : nullptr; }
    int MethodIndex(const std::string& name) const;
    /// The array type with elements of `elem` (made on first use).
    Type ArrayOf(const Type& elem);
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
};

} // namespace okay::script
