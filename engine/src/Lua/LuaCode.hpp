#pragma once
// okay::lua internals: the bytecode, compiled functions and the compiler entry.
// Our own design (a stack machine whose bottom slots are the locals), written
// from general interpreter techniques; see Lua.hpp for the overview.
#include "okay/Lua/Lua.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace okay::lua {

// Stack effects are given as (pops -> pushes). Slot operands are relative to
// the frame base. "K[x]" is constant x of the function.
enum Op : std::uint8_t {
    NOP,
    PUSHNIL,     // a: push a nils
    PUSHTRUE, PUSHFALSE,
    PUSHK,       // push K[a]
    PUSHINT,     // push integer a
    POP,         // drop a values
    GETLOCAL, SETLOCAL,      // slot a
    GETBOX, SETBOX,          // slot a holds a Box
    MKBOX,                   // slot a: wrap its value in a fresh Box
    GETUP, SETUP,            // upvalue a
    GETTABUPK, SETTABUPK,    // upvalue a indexed by K[b] (globals)
    GETINDEX,    // t k -> t[k]
    GETFIELD,    // t -> t[K[a]]
    SETINDEX,    // t k v ->
    SETFIELD,    // t v -> ; key K[a]
    SETINDEXAT,  // v -> ; stack[a][stack[a+1]] = v
    SETFIELDAT,  // v -> ; stack[a][K[b]] = v
    SETKV,       // k v -> ; stack[a][k] = v (table constructors)
    NEWTABLE,    // push a table (a array, b hash hint)
    SETLIST,     // table at slot a, values above it (b of them, -1 = to top) go to keys c, c+1, ...
    SELF,        // o -> o[K[a]] o
    ADD, SUB, MUL, MOD, POW, DIV, IDIV, BAND, BOR, BXOR, SHL, SHR,   // a b -> r (order matters: Arith uses op - ADD)
    UNM, BNOT, NOT, LEN,     // a -> r
    CONCAT,      // a values -> 1
    EQ, NE, LT, LE, GT, GE,  // a b -> bool
    JMP,         // pc = a
    JMPIFNOT,    // v -> ; if falsy pc = a
    JMPIF,
    ANDJ,        // if top falsy: pc = a (keep it), else pop
    ORJ,         // if top truthy: pc = a (keep it), else pop
    CALL,        // function at slot a, b args (-1 = to top), c results (-1 = all)
    TAILCALL,    // same, returning the results
    RETURN,      // values from slot a, b of them (-1 = to top)
    CLOSURE,     // push a closure of child function a
    VARARG,      // push a varargs (-1 = all)
    FORPREP,     // numeric for with hidden slots a, a+1, a+2: no iteration -> pc = b; else push the start value
    FORLOOP,     // next step: continue -> slot a+3 = value, pc = b; done -> pop the loop variable
    TFORTEST,    // generic for: slot a+4 nil -> top = a+4, pc = b; else slot a+2 = slot a+4
    TBC,         // slot a becomes a to-be-closed variable
    CLOSE,       // close to-be-closed variables at slot >= a
    CHECKCONST,  // (compile-time only marker, never executed)
};

/// Metamethod names, indexes into State::tmName. TM_ADD..TM_SHR follow ADD..SHR.
enum TM : int {
    TM_INDEX, TM_NEWINDEX, TM_CALL,
    TM_ADD, TM_SUB, TM_MUL, TM_MOD, TM_POW, TM_DIV, TM_IDIV, TM_BAND, TM_BOR, TM_BXOR, TM_SHL, TM_SHR,
    TM_UNM, TM_BNOT, TM_LEN, TM_CONCAT, TM_EQ, TM_LT, TM_LE, TM_CLOSE, TM_TOSTRING, TM_NAME, TM_METATABLE,
    TM_PAIRS, TM_MODE, TM_GC, TM_COUNT
};
extern const char* const kTMNames[TM_COUNT];
/// Value a native returns to say "I yielded" (coroutine.yield).
constexpr int kYieldResult = -1;

struct Instr {
    Op op = NOP;
    std::int32_t a = 0, b = 0, c = 0;
};

struct UpvalDesc {
    bool fromLocal = false;   ///< a local slot of the enclosing function (holding a Box), else its upvalue
    int index = 0;
    std::string name;
};

struct Proto : GcObj {
    std::vector<Instr> code;
    std::vector<int> lines;
    std::vector<Value> k;          ///< constants (numbers and strings)
    std::vector<Proto*> protos;
    std::vector<UpvalDesc> upvals;
    int numParams = 0;
    bool isVararg = false;
    int maxStack = 0;
    int lineDefined = 0;
    std::string name;              ///< "function 'Update'" etc. (for messages)
    std::string chunk;             ///< chunk name
    /// What an operand was, by pc, for error messages: "global 'x'", "local 'y'", "field 'z'", "method 'm'".
    std::unordered_map<int, std::string> hints;
    Proto() : GcObj(Type::Proto) {}
};

/// Compile a chunk. Returns the main function's proto (vararg, upvalue 0 = _ENV),
/// or nullptr with `errs` filled. With `collectAll`, keeps going after an error
/// (statement resync) to report more of them; the result is then always null.
Proto* Compile(State& L, const std::string& src, const std::string& chunk, std::vector<SyntaxError>& errs,
               bool collectAll);

/// Floating point to string the way Lua prints numbers ("%.14g", "1.0" for whole floats).
std::string FormatFloat(double d);
/// Hash used for strings.
std::uint32_t HashBytes(const char* s, std::size_t n);

/// The base library's pcall (the loop runs pcall of a Lua function itself, see State::Execute).
int PCallNative(State& L, int nargs);
/// The base library's xpcall (the loop runs xpcall of a Lua function itself, like pcall).
int XPCallNative(State& L, int nargs);

/// Library openers (one per file).
void OpenBase(State& L);
void OpenString(State& L);
void OpenTable(State& L);
void OpenMath(State& L);
void OpenCoroutine(State& L);
void OpenOs(State& L);
void OpenUtf8(State& L);

/// Lua string pattern matching (LuaLibString.cpp), shared with the tests.
struct PatternCapture { std::ptrdiff_t start = 0, len = 0; bool position = false; };

/// Append code point `cp` as UTF-8, with the original 5- and 6-byte forms the manual allows up to 2^31
/// (3.1 "\u{XXX}", 6.5 utf8.char). LuaLibUtf8.cpp; the compiler uses it for string escapes.
void AppendUtf8(std::string& out, std::uint32_t cp);

} // namespace okay::lua
