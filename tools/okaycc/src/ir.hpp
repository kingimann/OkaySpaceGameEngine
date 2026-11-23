#pragma once

#include "diag.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace okaycc {

// Three-address intermediate form. The shape follows the textbook idea of a
// result, an operation, and operands (Aho, Lam, Sethi, Ullman). The operations
// are ours.

struct Operand {
    enum class Kind { None, Reg, Imm } kind = Kind::None;
    int reg = -1;
    std::int64_t imm = 0;

    static Operand none() { return {}; }
    static Operand vreg(int r) {
        Operand o;
        o.kind = Kind::Reg;
        o.reg = r;
        return o;
    }
    static Operand number(std::int64_t v) {
        Operand o;
        o.kind = Kind::Imm;
        o.imm = v;
        return o;
    }
};

struct Inst {
    enum class Op {
        Mov, Add, Sub, Mul, Div, Mod, And, Or, Xor, Shl, Shr, Neg, Not,
        Ceq, Cne, Clt, Cle, Cgt, Cge, Jmp, Br, Call, Ret,
        Local, Lea, LeaSym, LeaFrame, Load, Store, GLoad, GStore, Cvt
    } op = Op::Mov;
    int dst = -1;
    int extra = 8;  // byte width for a load, store, or narrowing move
    bool uns = false;  // zero-extend, or unsigned compare
    bool fp = false;   // floating-point op; extra is 4 (float) or 8 (double)
    Operand a;
    Operand b;
    std::string label;
    std::string label2;
    std::string func;
    std::vector<Operand> args;
    std::vector<Operand> stack;  // outgoing stack image, low address first, 8-byte chunks
    std::vector<Operand> xmm;    // SSE arguments, xmm0 onward
    std::vector<int> xmmW;       // 4 or 8 for each SSE argument
    bool va = false;             // variadic call: the caller sets al to the xmm count
};

struct Block {
    std::string name;
    std::vector<Inst> code;
};

struct Function {
    std::string name;
    int params = 0;
    int vaNamed = -1;  // named general registers before "...", or -1 when the function is not variadic
    int vaXmm = 0;     // named xmm registers before "..."
    bool localSym = false;
    bool weakSym = false;  // an inline function: every file may define it, the linker keeps one
    std::vector<int> xmmSlot;   // vreg that receives xmm0, xmm1, ...
    std::vector<int> xmmWidth;  // 4 or 8 for each of those
    std::vector<Block> blocks;
};

struct Global {
    std::string name;
    int size = 0;
    bool rodata = false;
    bool localSym = false;  // internal linkage: a static global or a constexpr static member
    bool weakSym = false;   // a vtable of a class defined in a header: one copy kept by the linker
    std::vector<unsigned char> bytes;
    std::string reloc;  // if set, the 8-byte value is the address of this symbol
    std::vector<std::pair<int, std::string>> relocs;  // (byte offset, symbol) 8-byte addresses inside bytes
};

struct Module {
    std::vector<Function> funcs;
    std::vector<Global> globals;
};

std::string printModule(const Module& m);

// Constant folding, cross-block constant and copy propagation, and dead-code
// elimination. A value that is changed in a loop is not treated as a constant.
// From the integer rules in ISO/IEC 9899 and two's-complement wrapping.
void optimizeModule(Module& m);

// Linear scan over rbx, r12–r15, rsi, rdi, and r8–r11. rax, rcx, and rdx stay
// scratch. home[v] is -1 when v stays in its stack slot, otherwise an index into
// that list (0..4 are callee-saved). saved lists the callee-saved indices the
// prologue must preserve. spill and reload name the caller-saved homes to write
// to the stack before a call and to read back after it. Address-taken values and
// floating-point values stay on the stack.
struct RegHomes {
    std::vector<int> home;
    std::vector<int> saved;
    std::vector<std::vector<std::vector<int>>> spill;
    std::vector<std::vector<std::vector<int>>> reload;
};
RegHomes assignRegs(const Function& f);

// Tests turn this off to count stack traffic with every value in its slot.
void setAllocateRegisters(bool on);

// Lower a C subset program: integers, pointers, arrays, globals, strings, and structs.
Module lowerSource(const std::string& source, Diagnostics& diags, const std::string& file = "t.c",
                   const std::vector<std::string>& includes = {});
std::string irText(const std::string& source, Diagnostics& diags, const std::string& file = "t.c");

}  // namespace okaycc
