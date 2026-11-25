#pragma once
// Internal pieces of okay::js shared by the lexer, parser, compiler, bytecode machine
// and libraries. Nothing here is part of the engine API.
#include "okay/JavaScript/JavaScript.hpp"
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace okay::js {

struct RegexProg;

// ============================================================ tokens (ECMA-262 ch. 12)
enum class TT : std::uint8_t { End, Name, Num, Str, Template, Punct, PrivateName, Regex };

// Punctuators, longest first within each starting character is handled by the lexer.
enum P : std::uint16_t {
    P_None = 0,
    P_LBrace, P_RBrace, P_LParen, P_RParen, P_LBrack, P_RBrack, P_Dot, P_Ellipsis, P_Semi, P_Comma,
    P_Lt, P_Gt, P_Le, P_Ge, P_Eq2, P_Ne, P_Eq3, P_Ne2, P_Plus, P_Minus, P_Star, P_Slash, P_Percent, P_Star2,
    P_Inc, P_Dec, P_Shl, P_Sar, P_Shr, P_Amp, P_Bar, P_Caret, P_Bang, P_Tilde, P_And, P_Or, P_Nullish,
    P_Quest, P_QDot, P_Colon, P_Assign, P_PlusEq, P_MinusEq, P_StarEq, P_SlashEq, P_PercentEq, P_Star2Eq,
    P_ShlEq, P_SarEq, P_ShrEq, P_AmpEq, P_BarEq, P_CaretEq, P_AndEq, P_OrEq, P_NullishEq, P_Arrow,
    P_Count
};

struct Token {
    TT t = TT::End;
    std::uint16_t p = P_None;   ///< punctuator id
    bool nl = false;            ///< a line terminator came before this token
    bool escaped = false;       ///< a name written with an escape (not a keyword then)
    std::uint8_t tpl = 0;       ///< templates: 0 whole `...`, 1 head `...${, 2 middle }...${, 3 tail }...`
    int line = 0, col = 0;
    int match = -1;             ///< for ( [ {: the index of the closing token
    double n = 0;
    std::u16string s;           ///< name text, string value, template cooked text, regular expression body
    std::u16string flags;       ///< regular expression flags
    bool badEscape = false;     ///< a template piece with an invalid escape (only allowed when tagged)
};

/// Tokenize the whole source (templates are split into pieces; regular expression
/// literals are not supported and come out as '/' tokens the parser rejects).
bool Tokenize(const std::string& src, std::vector<Token>& out, SyntaxError& err);
/// kKeywords[0 .. kReservedCount) are the reserved words (12.7.2); the rest are contextual.
extern const int kReservedCount;
/// WhiteSpace and LineTerminator code units (12.2, 12.3): what trim and the number readers skip.
bool IsJsSpace(char16_t c);
/// Simple one-to-one case mapping (ASCII, Latin-1, Latin Extended-A, Greek, Cyrillic).
char16_t JsLower(char16_t c);
char16_t JsUpper(char16_t c);

// ============================================================ syntax tree (ch. 13-16)
enum class NK : std::uint8_t {
    // expressions
    Num, Str, Template, Ident, This, Super, NewTarget, Null, True, False, Array, Object, Func, Class,
    Unary, Update, Binary, Logical, Assign, Cond, Call, New, Member, Index, OptChain, Seq, Spread,
    Yield, Await, Hole, TaggedTemplate, Regex,
    // object literal parts
    PropInit, PropGet, PropSet, PropSpread,
    // patterns
    ArrayPat, ObjectPat, AssignPat, RestElem, PatProp,
    // statements
    VarDecl, Declarator, FuncDecl, ClassDecl, Return, If, For, ForIn, ForOf, While, DoWhile, Break, Continue,
    Throw, Try, Switch, Case, Block, Labeled, ExprStmt, Empty, ExportDefault, Debugger,
    // class parts
    ClassMethod, ClassField,
};

struct FuncInfo;
struct ClassInfo;
struct Node {
    NK k;
    int line = 0, col = 0;
    int op = 0;               ///< operator punctuator / declaration kind / method kind
    std::uint32_t flags = 0;  ///< NF_* below
    Node *a = nullptr, *b = nullptr, *c = nullptr, *d = nullptr;
    std::vector<Node*> list;
    std::u16string str;
    double num = 0;
    FuncInfo* fn = nullptr;
    ClassInfo* ci = nullptr;   ///< a class: its parts
};

enum NodeFlag : std::uint32_t {
    NF_Optional = 1,       ///< ?. on this member / call
    NF_Computed = 2,       ///< [key] in an object literal or class
    NF_Static = 4,         ///< static class member
    NF_Shorthand = 8,      ///< {a} in an object literal
    NF_Prefix = 16,        ///< ++x
    NF_Delegate = 32,      ///< yield*
    NF_Method = 64,        ///< method shorthand in a literal
    NF_CoverInit = 128,    ///< {a = 1} seen in a literal (only valid as a pattern)
    NF_Paren = 256,        ///< written in parentheses
};

enum DeclKind : int { DK_Var = 0, DK_Let = 1, DK_Const = 2 };

struct FuncInfo {
    std::vector<Node*> params;   ///< patterns (Ident, ArrayPat, ObjectPat, AssignPat, RestElem)
    Node* body = nullptr;        ///< Block, or an expression for `x => expr`
    bool exprBody = false;
    bool arrow = false, generator = false, async = false, method = false, getter = false, setter = false;
    bool classCtor = false, derived = false, fieldInit = false;
    bool hasFields = false;      ///< class constructor: the class has instance fields
    bool simpleParams = true;
    std::u16string name;
    int line = 0, col = 0;
    int length = 0;              ///< the function's "length" (params before the first default / rest)
    // what the parser saw (for capture analysis)
    std::unordered_set<std::u16string> innerRefs;   ///< names used inside nested functions
    bool usesThis = false, usesArguments = false, usesSuper = false, usesNewTarget = false;   ///< here or in nested arrows
    bool hasSuperCall = false;
    FuncInfo* fields = nullptr;  ///< class constructor: the instance field initializer function
};

struct ClassInfo {
    Node* heritage = nullptr;
    FuncInfo* ctor = nullptr;
    FuncInfo* instFields = nullptr;
    FuncInfo* staticFields = nullptr;
};

/// Owns the nodes of one parse.
struct Ast {
    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<std::unique_ptr<FuncInfo>> funcs;
    std::vector<std::unique_ptr<ClassInfo>> classes;
    FuncInfo* script = nullptr;   ///< the top level, as a function
    Node* NewNode(NK k, int line, int col) {
        nodes.push_back(std::make_unique<Node>());
        Node* n = nodes.back().get();
        n->k = k;
        n->line = line;
        n->col = col;
        return n;
    }
    FuncInfo* NewFunc() {
        funcs.push_back(std::make_unique<FuncInfo>());
        return funcs.back().get();
    }
};

bool Parse(const std::vector<Token>& toks, Ast& ast, SyntaxError& err);

// ============================================================ bytecode
enum Op : std::uint16_t {
    OP_Nop, OP_Undef, OP_Null, OP_True, OP_False, OP_PushEmpty, OP_Const, OP_Int,
    OP_Pop, OP_Dup, OP_Dup2, OP_Swap, OP_DupX1, OP_DupX2,
    OP_GetLoc, OP_SetLoc, OP_PutLoc, OP_GetLocChk, OP_IncLoc, OP_DecLoc,
    OP_GetCell, OP_GetCellChk, OP_SetCell, OP_PutCell, OP_NewCell, OP_CopyCell,
    OP_GetUp, OP_GetUpChk, OP_SetUp, OP_PutUp,
    OP_GetGlobal, OP_SetGlobal, OP_TypeofGlobal, OP_DeclVar, OP_DeclLex, OP_InitLex, OP_DeclFunc,
    OP_GetProp, OP_SetProp, OP_GetElem, OP_SetElem, OP_GetPropKeep, OP_GetElemKeep, OP_DelProp, OP_DelElem,
    OP_In, OP_InstanceOf, OP_ToKey,
    OP_Add, OP_Sub, OP_Mul, OP_Div, OP_Mod, OP_Pow, OP_BitAnd, OP_BitOr, OP_BitXor, OP_Shl, OP_Sar, OP_Shr,
    OP_Neg, OP_Pos, OP_Not, OP_BitNot, OP_Typeof, OP_Void, OP_Inc, OP_Dec, OP_ToNum, OP_ToStr,
    OP_Eq, OP_Ne, OP_StrictEq, OP_StrictNe, OP_Lt, OP_Le, OP_Gt, OP_Ge,
    OP_Jump, OP_JumpF, OP_JumpT, OP_JumpFKeep, OP_JumpTKeep, OP_JumpNotNullishKeep, OP_JumpNullish,
    OP_JumpNullishCallee, OP_JumpNotUndefKeep, OP_LtJumpF,
    OP_Call, OP_CallSpread, OP_New, OP_NewSpread, OP_SuperCall, OP_SuperCallSpread, OP_Return, OP_ReturnUndef,
    OP_Closure, OP_NewObject, OP_NewArray, OP_ArrPush, OP_ArrHole, OP_ArrSpread,
    OP_DefField, OP_DefFieldK, OP_DefGetter, OP_DefSetter, OP_DefMethod, OP_CopyProps, OP_RestObject,
    OP_Class, OP_SetFieldInit, OP_RunFields,
    OP_LoadThis, OP_LoadThisChk, OP_StoreThis, OP_LoadNewTarget, OP_LoadFunc, OP_LoadHome,
    OP_SuperGet, OP_SuperSet,
    OP_GetIter, OP_IterNext, OP_IterValue, OP_IterRest, OP_IterClose, OP_IterDone, OP_IterStep,
    OP_ForInPrep, OP_ForInNext,
    OP_Throw, OP_TryBegin, OP_TryEnd, OP_ThrowConst,
    OP_GenStart, OP_AsyncStart, OP_Yield, OP_Await, OP_ResumeCheck, OP_IterSend,
    OP_Arguments, OP_RestArgs, OP_CheckCoercible, OP_Debugger, OP_SetName, OP_NewRegExp,
    OP_Count
};

struct Instr {
    std::uint16_t op;
    std::uint16_t b;
    std::int32_t a;
};

struct UpDesc {
    bool fromLocal;        ///< the parent's local slot (a Cell there) or the parent's upvalue
    std::uint32_t index;
};

enum ProtoFlag : std::uint32_t {
    PF_Arrow = 1, PF_Generator = 2, PF_Async = 4, PF_Method = 8, PF_ClassCtor = 16, PF_Derived = 32,
    PF_NeedsExtra = 64, PF_Script = 128,
};

struct Proto : GcObj {
    std::vector<Instr> code;
    std::vector<int> lines;
    std::vector<Value> consts;
    std::vector<Proto*> protos;
    std::vector<UpDesc> ups;
    std::uint32_t nparams = 0, nslots = 0, maxStack = 0;
    std::uint32_t flags = 0;
    String* name = nullptr;
    std::string chunk;
    int line = 0;
    int length = 0;
    Proto() : GcObj(Kind::Proto) {}
    void Trace(Runtime& rt) override;
    std::size_t Bytes() const override {
        return sizeof(Proto) + code.capacity() * sizeof(Instr) + lines.capacity() * 4 + consts.capacity() * sizeof(Value);
    }
};

/// Compile a parsed script to its top-level function proto.
Proto* CompileScript(Runtime& rt, Ast& ast, const std::string& chunk, SyntaxError& err);

// ============================================================ internal objects
struct Handler {
    std::uint32_t pc;   ///< code index of the catch landing
    std::uint32_t sp;   ///< operand stack depth to restore (relative to the frame base)
};

enum GenState : std::uint8_t { GS_Start, GS_Yield, GS_Running, GS_Done };

struct PromiseObj;

/// A generator object or an async function's suspended activation (spec 27.5, 27.7).
struct GenObj : Object {
    GenState state = GS_Start;
    bool async = false;
    std::vector<Value> saved;            ///< [callee, this, slots..., operands...]
    std::vector<Handler> handlers;
    std::uint32_t pc = 0;
    std::uint32_t argc = 0;
    std::uint32_t nslots = 0;
    Object* newTarget = nullptr;
    Value extra;
    PromiseObj* promise = nullptr;       ///< async functions: the result promise
    GenObj() : Object(Cls::Generator) {}
    void Trace(Runtime& rt) override;
    std::size_t Bytes() const override { return Object::Bytes() + saved.capacity() * sizeof(Value); }
};

struct Reaction {
    Value handler;                 ///< a function, or undefined (pass through)
    PromiseObj* derived = nullptr; ///< the promise then() returned
    GenObj* awaiter = nullptr;     ///< an await waiting on this promise
};

struct PromiseObj : Object {
    std::uint8_t state = 0;        ///< 0 pending, 1 fulfilled, 2 rejected
    bool handled = false;
    Value result;
    std::vector<Reaction> onFulfill, onReject;
    PromiseObj() : Object(Cls::Promise) {}
    void Trace(Runtime& rt) override;
};

/// Map and Set (spec 24.1, 24.2): entries in insertion order with an index by SameValueZero.
struct KeyHash {
    std::size_t operator()(const Value& v) const;
};
struct KeyEq {
    bool operator()(const Value& a, const Value& b) const { return SameValueZero(a, b); }
};
struct MapObj : Object {
    struct Entry {
        Value k, v;
        bool live;
    };
    std::vector<Entry> entries;
    std::unordered_map<Value, std::uint32_t, KeyHash, KeyEq> index;
    std::uint32_t live = 0;
    std::uint32_t iterators = 0;   ///< iterators that may still run (no compaction while any)
    explicit MapObj(Cls c = Cls::Map) : Object(c) {}
    void Trace(Runtime& rt) override;
    std::size_t Bytes() const override { return Object::Bytes() + entries.capacity() * sizeof(Entry) + index.size() * 48; }
    static Value Norm(const Value& k) { return (k.IsNum() && k.n == 0) ? Value::Num(0) : k; }
    void Compact();
};

/// Array, Map, Set and String iterators (spec 23.1.5, 24.1.5, 24.2.5, 22.1.5).
struct IterObj : Object {
    Value target;
    std::uint32_t pos = 0;
    std::uint8_t kind = 0;   ///< 0 values, 1 keys, 2 entries
    bool done = false;
    explicit IterObj(Cls c = Cls::ArrayIter) : Object(c) {}
    void Trace(Runtime& rt) override;
};

/// The iterator record GetIterator makes (spec 7.4.1), with fast paths for
/// unmodified arrays and strings. Never visible to scripts.
struct IterRec : Object {
    Value iter, next;
    bool done = false;
    std::uint8_t fast = 0;   ///< 1 array, 2 string
    std::uint32_t pos = 0;
    IterRec() : Object(Cls::IterRecord) {}
    void Trace(Runtime& rt) override;
};

/// Error instances (spec 20.5): where they were made, for the host's message.
struct ErrorObj : Object {
    std::string where;   ///< "chunk:line" or ""
    ErrorObj() : Object(Cls::Error) {}
};

struct RegexProg;
/// A RegExp object (22.2.4): its compiled pattern, source and flags; lastIndex is an own property.
struct RegExpObj : Object {
    std::shared_ptr<RegexProg> prog;
    String* source = nullptr;
    String* flags = nullptr;
    RegExpObj() : Object(Cls::RegExpLike) {}
    void Trace(Runtime& rt) override;
};
/// The parser's early check of a regular expression literal (22.2.3.1).
bool RegexSyntaxOk(const std::u16string& pattern, const std::u16string& flags, std::string& err);
bool IsRegExp(const Value& v);
Value MakeRegExpLiteral(Runtime& rt, String* pattern, String* flags);
/// The String methods with a RegExp argument (JsRegExp.cpp).
Value RegExpReplace(Runtime& rt, const Value& rx, String* str, const Value& replacement, bool all);
Value RegExpSplit(Runtime& rt, const Value& rx, String* str, std::uint32_t limit);
Value RegExpMatch(Runtime& rt, const Value& rx, String* str, int mode);   ///< 0 match, 1 matchAll, 2 search

struct ForInObj : Object {
    Value obj;
    std::vector<GcObj*> keys;
    std::size_t pos = 0;
    ForInObj() : Object(Cls::ForIn) {}
    void Trace(Runtime& rt) override;
};

// ============================================================ common keys
struct Runtime::Atoms {
    String *length, *prototype, *constructor, *name, *message, *stack, *value, *done, *next, *toString, *valueOf,
        *get, *set, *writable, *enumerable, *configurable, *then, *callee, *cause, *toJSON, *ret /* "return" */,
        *throw_, *index, *input, *join, *lastIndex, *errors, *status, *reason, *empty, *undefined, *null_,
        *true_, *false_, *object, *function, *number, *string, *boolean, *symbol, *default_, *anonymous, *raw;
};

// ============================================================ machine state
enum FrameFlag : std::uint8_t { FF_Construct = 1, FF_Entry = 2 };

struct Frame {
    Closure* fn;
    const Instr* pc;
    Value* base;            ///< first parameter slot; base[-2] callee, base[-1] this
    std::uint32_t handlerBase;
    std::uint32_t argc;
    std::uint8_t flags;
    Object* newTarget;
    GenObj* gen;            ///< the generator / async activation this frame belongs to
    Value extra;            ///< arguments past the declared parameters (an array) when the function needs them
};

struct Job {
    std::uint8_t kind;      ///< 0 reaction, 1 resolve thenable, 2 resume await
    Value a, b, c;
    bool flag = false;
};

struct Runtime::Impl {
    static constexpr std::size_t kStackValues = 1u << 18;   ///< 4 MB of address space, touched as used
    static constexpr int kMaxFrames = 2400;
    static constexpr int kMaxNativeDepth = 120;
    Value* stack = nullptr;
    Value* sp = nullptr;
    Value* stackEnd = nullptr;
    Frame* frames = nullptr;
    int nframes = 0;
    std::vector<Handler> handlers;
    std::deque<Job> jobs;
    int nativeDepth = 0;
    int jobDepth = 0;
    int resumeMode = 0;     ///< how the generator now resuming was resumed: 0 next, 1 throw, 2 return
    bool yielded = false;   ///< the last Execute ended in a yield (not a return)
    NativeFunc* arrayValues = nullptr;
    NativeFunc* arrayIterNext = nullptr;
    NativeFunc* stringIterator = nullptr;
    NativeFunc* stringIterNext = nullptr;
    std::vector<Proto*> scripts;   ///< compiled scripts (kept while the runtime lives)
    std::vector<Object*> joinStack;   ///< arrays being joined (a cycle joins as "")
    std::unordered_map<std::u16string, std::shared_ptr<RegexProg>> regexCache;   ///< "flags/pattern" -> compiled
};

// the machine (JsInterpreter.cpp)
Value Execute(Runtime& rt, int entryFrame);
/// Push a frame for `fn` whose callee, this and args are already at sp - argc - 2 .. sp.
/// Returns false when fn is not a closure (the caller handles natives).
void EnterClosure(Runtime& rt, Closure* fn, int argc, std::uint8_t flags, Object* newTarget);
Value ResumeGenerator(Runtime& rt, GenObj* g, int mode, const Value& v);   ///< mode 0 next, 1 throw, 2 return
Value CallNative(Runtime& rt, NativeFunc* f, Value* argv, int argc, const Value& thisv, Object* newTarget);
Value MakeIterResult(Runtime& rt, const Value& v, bool done);

// promises (JsLibCollections.cpp)
PromiseObj* NewPromise(Runtime& rt);
void ResolvePromise(Runtime& rt, PromiseObj* p, const Value& v);
void RejectPromise(Runtime& rt, PromiseObj* p, const Value& v);
PromiseObj* PromiseResolveValue(Runtime& rt, const Value& v);
void AwaitValue(Runtime& rt, GenObj* g, const Value& v);
void RunJob(Runtime& rt, Job& j);

// iteration helpers (JsLibCollections.cpp)
IterRec* GetIterator(Runtime& rt, const Value& v);
/// The next value, or false when the iterator is done.
bool IterStep(Runtime& rt, IterRec* r, Value& out);
void IterClose(Runtime& rt, IterRec* r);

// library installers
void InstallObjectLib(Runtime& rt);
void InstallFunctionLib(Runtime& rt);
void InstallErrorLib(Runtime& rt);
void InstallArrayLib(Runtime& rt);
void InstallStringLib(Runtime& rt);
void InstallNumberLib(Runtime& rt);
void InstallMathLib(Runtime& rt);
void InstallJsonLib(Runtime& rt);
void InstallCollectionsLib(Runtime& rt);
void InstallGlobalLib(Runtime& rt);
void InstallRegExpLib(Runtime& rt);

// small helpers for library code
NativeFunc* DefNative(Runtime& rt, Object* on, const char* name, NativeFn fn, int length, std::uint8_t flags = kHidden);
NativeFunc* DefCtor(Runtime& rt, const char* name, NativeFn fn, int length, Object* proto);
void DefValue(Runtime& rt, Object* on, const char* name, const Value& v, std::uint8_t flags = kHidden);
void DefGetter(Runtime& rt, Object* on, const char* name, NativeFn fn);
Object* ThisObject(Runtime& rt, CallArgs& a, const char* what);
/// Argument i as an integer clamped into [0, len] the way slice / splice read relative indices.
std::int64_t RelIndex(Runtime& rt, const Value& v, std::int64_t len, std::int64_t dflt);
/// Array.prototype helpers over any array-like (fast for real arrays).
bool HasIndex(Runtime& rt, Object* o, std::uint64_t i);
void DeleteIndex(Runtime& rt, Object* o, std::uint64_t i);
void SetLength(Runtime& rt, Object* o, std::uint64_t len);
String* NameOf(Runtime& rt, Object* fn);   ///< a function's name for messages
std::string KeyText(Runtime& rt, GcObj* key);

} // namespace okay::js
