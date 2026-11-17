#pragma once

#include "diag.hpp"

#include <cstdint>
#include <string>
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
    std::vector<int> xmmSlot;   // vreg that receives xmm0, xmm1, ...
    std::vector<int> xmmWidth;  // 4 or 8 for each of those
    std::vector<Block> blocks;
};

struct Global {
    std::string name;
    int size = 0;
    bool rodata = false;
    std::vector<unsigned char> bytes;
    std::string reloc;  // if set, the 8-byte value is the address of this symbol
};

struct Module {
    std::vector<Function> funcs;
    std::vector<Global> globals;
};

std::string printModule(const Module& m);

// Constant folding and dead-code elimination. Folding stays inside one block, so a
// value that changes in a loop is not treated as a constant. From the integer rules
// in ISO/IEC 9899 and two's-complement wrapping.
void optimizeModule(Module& m);

// Up to three callee-saved registers (rbx, r12, r13) for values whose address
// is not taken. home[v] is -1 when v stays in its stack slot, otherwise 0, 1, or 2.
struct RegHomes {
    std::vector<int> home;
    int count = 0;
};
RegHomes assignRegs(const Function& f);

// Lower a C subset program: integers, pointers, arrays, globals, strings, and structs.
Module lowerSource(const std::string& source, Diagnostics& diags, const std::string& file = "t.c",
                   const std::vector<std::string>& includes = {});
std::string irText(const std::string& source, Diagnostics& diags, const std::string& file = "t.c");

}  // namespace okaycc
