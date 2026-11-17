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
        Ceq, Cne, Clt, Cle, Cgt, Cge, Jmp, Br, Call, Ret
    } op = Op::Mov;
    int dst = -1;
    Operand a;
    Operand b;
    std::string label;
    std::string label2;
    std::string func;
    std::vector<Operand> args;
};

struct Block {
    std::string name;
    std::vector<Inst> code;
};

struct Function {
    std::string name;
    int params = 0;
    std::vector<Block> blocks;
};

struct Module {
    std::vector<Function> funcs;
};

std::string printModule(const Module& m);

// Lower a type-checked integer program. Pointers, arrays, and globals are refused.
Module lowerSource(const std::string& source, Diagnostics& diags);
std::string irText(const std::string& source, Diagnostics& diags);

}  // namespace okaycc
