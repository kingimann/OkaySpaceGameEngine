#include "ir.hpp"

#include <string>

namespace okaycc {
namespace {

const char* opName(Inst::Op op) {
    switch (op) {
    case Inst::Op::Mov: return "mov";
    case Inst::Op::Add: return "add";
    case Inst::Op::Sub: return "sub";
    case Inst::Op::Mul: return "mul";
    case Inst::Op::Div: return "div";
    case Inst::Op::Mod: return "mod";
    case Inst::Op::And: return "and";
    case Inst::Op::Or: return "or";
    case Inst::Op::Xor: return "xor";
    case Inst::Op::Shl: return "shl";
    case Inst::Op::Shr: return "shr";
    case Inst::Op::Neg: return "neg";
    case Inst::Op::Not: return "not";
    case Inst::Op::Ceq: return "ceq";
    case Inst::Op::Cne: return "cne";
    case Inst::Op::Clt: return "clt";
    case Inst::Op::Cle: return "cle";
    case Inst::Op::Cgt: return "cgt";
    case Inst::Op::Cge: return "cge";
    case Inst::Op::Jmp: return "jmp";
    case Inst::Op::Br: return "br";
    case Inst::Op::Call: return "call";
    case Inst::Op::Ret: return "ret";
    case Inst::Op::Local: return "local";
    case Inst::Op::Lea: return "lea";
    case Inst::Op::LeaSym: return "leasym";
    case Inst::Op::Load: return "load";
    case Inst::Op::Store: return "store";
    case Inst::Op::GLoad: return "gload";
    case Inst::Op::GStore: return "gstore";
    }
    return "?";
}

std::string opnd(const Operand& o) {
    if (o.kind == Operand::Kind::Imm) return std::to_string(o.imm);
    if (o.kind == Operand::Kind::Reg) return "v" + std::to_string(o.reg);
    return "_";
}

}  // namespace

std::string printModule(const Module& m) {
    std::string s;
    for (const Function& f : m.funcs) {
        s += "func ";
        s += f.name;
        s += "\n";
        for (const Block& b : f.blocks) {
            s += b.name;
            s += ":\n";
            for (const Inst& in : b.code) {
                s += "  ";
                if (in.op == Inst::Op::Jmp) {
                    s += "jmp ";
                    s += in.label;
                } else if (in.op == Inst::Op::Br) {
                    s += "br ";
                    s += opnd(in.a);
                    s += ", ";
                    s += in.label;
                    s += ", ";
                    s += in.label2;
                } else if (in.op == Inst::Op::Call) {
                    s += "call ";
                    s += in.dst >= 0 ? "v" + std::to_string(in.dst) : "_";
                    s += ", ";
                    s += in.func.empty() ? ("*" + opnd(in.a)) : in.func;
                    s += "(";
                    for (std::size_t i = 0; i < in.args.size(); ++i) {
                        if (i) s += ", ";
                        s += opnd(in.args[i]);
                    }
                    s += ")";
                } else if (in.op == Inst::Op::Local) {
                    s += "local v";
                    s += std::to_string(in.dst);
                    s += ", ";
                    s += opnd(in.a);
                } else if (in.op == Inst::Op::Lea) {
                    s += "lea v";
                    s += std::to_string(in.dst);
                    s += ", ";
                    s += opnd(in.a);
                } else if (in.op == Inst::Op::LeaSym) {
                    s += "leasym v";
                    s += std::to_string(in.dst);
                    s += ", ";
                    s += in.func;
                } else if (in.op == Inst::Op::Load) {
                    s += "load v";
                    s += std::to_string(in.dst);
                    s += ", ";
                    s += opnd(in.a);
                    s += ", ";
                    s += std::to_string(in.extra);
                } else if (in.op == Inst::Op::Store) {
                    s += "store ";
                    s += opnd(in.a);
                    s += ", ";
                    s += opnd(in.b);
                    s += ", ";
                    s += std::to_string(in.extra);
                } else if (in.op == Inst::Op::GLoad) {
                    s += "gload v";
                    s += std::to_string(in.dst);
                    s += ", ";
                    s += in.func;
                    s += ", ";
                    s += std::to_string(in.extra);
                } else if (in.op == Inst::Op::GStore) {
                    s += "gstore ";
                    s += in.func;
                    s += ", ";
                    s += opnd(in.a);
                    s += ", ";
                    s += std::to_string(in.extra);
                } else if (in.op == Inst::Op::Ret) {
                    s += "ret";
                    if (in.a.kind != Operand::Kind::None) {
                        s += " ";
                        s += opnd(in.a);
                    }
                } else if (in.op == Inst::Op::Neg || in.op == Inst::Op::Not) {
                    s += opName(in.op);
                    s += " v";
                    s += std::to_string(in.dst);
                    s += ", ";
                    s += opnd(in.a);
                } else if (in.op == Inst::Op::Mov) {
                    s += "mov v";
                    s += std::to_string(in.dst);
                    s += ", ";
                    s += opnd(in.a);
                } else {
                    s += opName(in.op);
                    s += " v";
                    s += std::to_string(in.dst);
                    s += ", ";
                    s += opnd(in.a);
                    s += ", ";
                    s += opnd(in.b);
                }
                s += "\n";
            }
        }
    }
    return s;
}

std::string irText(const std::string& source, Diagnostics& diags, const std::string& file) {
    return printModule(lowerSource(source, diags, file));
}

}  // namespace okaycc
