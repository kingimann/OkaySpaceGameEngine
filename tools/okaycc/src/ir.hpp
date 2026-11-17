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
        Local, Lea, LeaSym, LeaFrame, Load, Store, GLoad, GStore
    } op = Op::Mov;
    int dst = -1;
    int extra = 8;  // byte width for a load, store, or narrowing move
    bool uns = false;  // zero-extend, or unsigned compare
    Operand a;
    Operand b;
    std::string label;
    std::string label2;
    std::string func;
    std::vector<Operand> args;
    std::vector<Operand> stack;  // outgoing stack image, low address first, 8-byte chunks
    bool va = false;             // variadic call: the caller sets al to 0
};

struct Block {
    std::string name;
    std::vector<Inst> code;
};

struct Function {
    std::string name;
    int params = 0;
    int vaNamed = -1;  // named general registers before "...", or -1 when the function is not variadic
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

// Lower a C subset program: integers, pointers, arrays, globals, strings, and structs.
Module lowerSource(const std::string& source, Diagnostics& diags, const std::string& file = "t.c",
                   const std::vector<std::string>& includes = {});
std::string irText(const std::string& source, Diagnostics& diags, const std::string& file = "t.c");

}  // namespace okaycc
