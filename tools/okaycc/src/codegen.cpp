#include "codegen.hpp"

#include <string>

namespace okaycc {
namespace {

const char* kArgReg[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};

int slotCount(const Function& f) {
    int n = f.params;
    for (const Block& b : f.blocks) {
        for (const Inst& in : b.code) {
            if (in.dst >= n) n = in.dst + 1;
            if (in.a.kind == Operand::Kind::Reg && in.a.reg >= n) n = in.a.reg + 1;
            if (in.b.kind == Operand::Kind::Reg && in.b.reg >= n) n = in.b.reg + 1;
            for (const Operand& arg : in.args) {
                if (arg.kind == Operand::Kind::Reg && arg.reg >= n) n = arg.reg + 1;
            }
        }
    }
    return n;
}

std::string slot(int reg) {
    int disp = -8 * (reg + 1);
    return std::to_string(disp) + "(%rbp)";
}

std::string blockName(const std::string& func, const std::string& block) { return func + "_" + block; }

void load(std::string& s, const Operand& o, const char* reg) {
    if (o.kind == Operand::Kind::Imm) {
        s += "  movq $";
        s += std::to_string(o.imm);
        s += ", ";
        s += reg;
        s += "\n";
        return;
    }
    s += "  movq ";
    s += slot(o.reg);
    s += ", ";
    s += reg;
    s += "\n";
}

void store(std::string& s, const char* reg, int dst) {
    s += "  movq ";
    s += reg;
    s += ", ";
    s += slot(dst);
    s += "\n";
}

void sext(std::string& s, int width, bool uns) {
    if (width != 1 && width != 2 && width != 4) return;
    if (uns && width == 4) {
        s += "  movl %eax, %eax\n";
        return;
    }
    if (uns && width == 2) {
        s += "  movzwq %ax, %rax\n";
        return;
    }
    if (uns && width == 1) {
        s += "  movzbq %al, %rax\n";
        return;
    }
    if (width == 4) s += "  movslq %eax, %rax\n";
    else if (width == 2) s += "  movswq %ax, %rax\n";
    else s += "  movsbq %al, %rax\n";
}

const char* loadOp(int width) {
    if (width == 1) return "movsbq";
    if (width == 2) return "movswq";
    if (width == 4) return "movslq";
    return "movq";
}

void memLoad(std::string& s, int width, bool uns, const std::string& addr) {
    if (uns && width == 4) {
        s += "  movl ";
        s += addr;
        s += ", %eax\n";
        return;
    }
    if (uns && width == 1) {
        s += "  movzbq ";
        s += addr;
        s += ", %rax\n";
        return;
    }
    if (uns && width == 2) {
        s += "  movzwq ";
        s += addr;
        s += ", %rax\n";
        return;
    }
    s += "  ";
    s += loadOp(width);
    s += " ";
    s += addr;
    s += ", %rax\n";
}

const char* storeOp(int width) {
    if (width == 1) return "movb";
    if (width == 2) return "movw";
    if (width == 4) return "movl";
    return "movq";
}

const char* storeReg(int width) {
    if (width == 1) return "%al";
    if (width == 2) return "%ax";
    if (width == 4) return "%eax";
    return "%rax";
}

const char* storeRegC(int width) {
    if (width == 1) return "%cl";
    if (width == 2) return "%cx";
    if (width == 4) return "%ecx";
    return "%rcx";
}

const char* setcc(Inst::Op op, bool uns) {
    if (uns) {
        switch (op) {
        case Inst::Op::Clt: return "setb";
        case Inst::Op::Cle: return "setbe";
        case Inst::Op::Cgt: return "seta";
        case Inst::Op::Cge: return "setae";
        case Inst::Op::Ceq: return "sete";
        case Inst::Op::Cne: return "setne";
        default: return "sete";
        }
    }
    switch (op) {
    case Inst::Op::Ceq: return "sete";
    case Inst::Op::Cne: return "setne";
    case Inst::Op::Clt: return "setl";
    case Inst::Op::Cle: return "setle";
    case Inst::Op::Cgt: return "setg";
    case Inst::Op::Cge: return "setge";
    default: return "sete";
    }
}

void emitInst(std::string& s, const Function& f, const Inst& in) {
    auto bin = [&](const char* mnemonic) {
        load(s, in.a, "%rax");
        if (in.b.kind == Operand::Kind::Imm && (in.op == Inst::Op::Add || in.op == Inst::Op::Sub)) {
            s += "  ";
            s += mnemonic;
            s += " $";
            s += std::to_string(in.b.imm);
            s += ", %rax\n";
        } else {
            load(s, in.b, "%rcx");
            s += "  ";
            s += mnemonic;
            s += " %rcx, %rax\n";
        }
        store(s, "%rax", in.dst);
    };
    switch (in.op) {
    case Inst::Op::Mov:
        load(s, in.a, "%rax");
        sext(s, in.extra, in.uns);
        store(s, "%rax", in.dst);
        break;
    case Inst::Op::Add:
        bin("addq");
        break;
    case Inst::Op::Sub:
        bin("subq");
        break;
    case Inst::Op::Mul:
        load(s, in.a, "%rax");
        load(s, in.b, "%rcx");
        s += "  imulq %rcx, %rax\n";
        store(s, "%rax", in.dst);
        break;
    case Inst::Op::And:
        bin("andq");
        break;
    case Inst::Op::Or:
        bin("orq");
        break;
    case Inst::Op::Xor:
        bin("xorq");
        break;
    case Inst::Op::Shl:
        load(s, in.a, "%rax");
        load(s, in.b, "%rcx");
        s += "  shlq %cl, %rax\n";
        store(s, "%rax", in.dst);
        break;
    case Inst::Op::Shr:
        load(s, in.a, "%rax");
        load(s, in.b, "%rcx");
        s += "  shrq %cl, %rax\n";
        store(s, "%rax", in.dst);
        break;
    case Inst::Op::Div:
    case Inst::Op::Mod:
        load(s, in.a, "%rax");
        s += "  cqto\n";
        load(s, in.b, "%rcx");
        s += "  idivq %rcx\n";
        store(s, in.op == Inst::Op::Div ? "%rax" : "%rdx", in.dst);
        break;
    case Inst::Op::Neg:
        load(s, in.a, "%rax");
        s += "  negq %rax\n";
        store(s, "%rax", in.dst);
        break;
    case Inst::Op::Not:
        load(s, in.a, "%rax");
        s += "  notq %rax\n";
        store(s, "%rax", in.dst);
        break;
    case Inst::Op::Ceq:
    case Inst::Op::Cne:
    case Inst::Op::Clt:
    case Inst::Op::Cle:
    case Inst::Op::Cgt:
    case Inst::Op::Cge:
        load(s, in.a, "%rax");
        load(s, in.b, "%rcx");
        s += "  cmpq %rcx, %rax\n  ";
        s += setcc(in.op, in.uns);
        s += " %al\n  movzbq %al, %rax\n";
        store(s, "%rax", in.dst);
        break;
    case Inst::Op::Jmp:
        s += "  jmp ";
        s += blockName(f.name, in.label);
        s += "\n";
        break;
    case Inst::Op::Br:
        load(s, in.a, "%rax");
        s += "  testq %rax, %rax\n  jne ";
        s += blockName(f.name, in.label);
        s += "\n  jmp ";
        s += blockName(f.name, in.label2);
        s += "\n";
        break;
    case Inst::Op::Call: {
        int nstack = in.args.size() > 6 ? static_cast<int>(in.args.size()) - 6 : 0;
        if (nstack % 2) s += "  subq $8, %rsp\n";
        for (int i = nstack - 1; i >= 0; --i) {
            load(s, in.args[static_cast<std::size_t>(6 + i)], "%rax");
            s += "  pushq %rax\n";
        }
        int nreg = static_cast<int>(in.args.size()) < 6 ? static_cast<int>(in.args.size()) : 6;
        for (int i = 0; i < nreg; ++i) {
            load(s, in.args[static_cast<std::size_t>(i)], "%rax");
            s += "  movq %rax, ";
            s += kArgReg[i];
            s += "\n";
        }
        s += "  call ";
        s += in.func;
        s += "\n";
        if (nstack) {
            int bytes = 8 * (nstack + (nstack % 2));
            s += "  addq $";
            s += std::to_string(bytes);
            s += ", %rsp\n";
        }
        if (in.dst >= 0) store(s, "%rax", in.dst);
        break;
    }
    case Inst::Op::Ret:
        if (in.a.kind != Operand::Kind::None) {
            load(s, in.a, "%rax");
            sext(s, in.extra, in.uns);
        }
        s += "  jmp ";
        s += f.name;
        s += "_epilogue\n";
        break;
    case Inst::Op::Local:
    case Inst::Op::Lea:
    case Inst::Op::LeaSym:
    case Inst::Op::Load:
    case Inst::Op::Store:
    case Inst::Op::GLoad:
    case Inst::Op::GStore:
        break;
    }
}

void emitMem(std::string& s, const Inst& in, int localOff) {
    switch (in.op) {
    case Inst::Op::Local:
        s += "  leaq ";
        s += std::to_string(localOff);
        s += "(%rbp), %rax\n";
        store(s, "%rax", in.dst);
        break;
    case Inst::Op::Lea:
        s += "  leaq ";
        s += slot(in.a.reg);
        s += ", %rax\n";
        store(s, "%rax", in.dst);
        break;
    case Inst::Op::LeaSym:
        s += "  leaq ";
        s += in.func;
        s += "(%rip), %rax\n";
        store(s, "%rax", in.dst);
        break;
    case Inst::Op::Load:
        load(s, in.a, "%rax");
        memLoad(s, in.extra, in.uns, "(%rax)");
        store(s, "%rax", in.dst);
        break;
    case Inst::Op::Store:
        load(s, in.a, "%rax");
        load(s, in.b, "%rcx");
        s += "  ";
        s += storeOp(in.extra);
        s += " ";
        s += storeRegC(in.extra);
        s += ", (%rax)\n";
        break;
    case Inst::Op::GLoad:
        memLoad(s, in.extra, in.uns, in.func + "(%rip)");
        store(s, "%rax", in.dst);
        break;
    case Inst::Op::GStore:
        load(s, in.a, "%rax");
        s += "  ";
        s += storeOp(in.extra);
        s += " ";
        s += storeReg(in.extra);
        s += ", ";
        s += in.func;
        s += "(%rip)\n";
        break;
    default:
        break;
    }
}

std::string emitFunction(const Function& f) {
    int slots = slotCount(f);
    int frame = slots * 8;
    if (frame % 16) frame += 8;
    std::string s;
    s += ".globl ";
    s += f.name;
    s += "\n";
    s += f.name;
    s += ":\n  pushq %rbp\n  movq %rsp, %rbp\n";
    if (frame) {
        s += "  subq $";
        s += std::to_string(frame);
        s += ", %rsp\n";
    }
    int nreg = f.params < 6 ? f.params : 6;
    for (int i = 0; i < nreg; ++i) {
        s += "  movq ";
        s += kArgReg[i];
        s += ", ";
        s += slot(i);
        s += "\n";
    }
    for (int i = 6; i < f.params; ++i) {
        int disp = 16 + 8 * (i - 6);
        s += "  movq ";
        s += std::to_string(disp);
        s += "(%rbp), %rax\n  movq %rax, ";
        s += slot(i);
        s += "\n";
    }
    int cursor = slots * 8;
    std::vector<int> localOff;
    for (const Block& b : f.blocks) {
        for (const Inst& in : b.code) {
            if (in.op != Inst::Op::Local) continue;
            int n = static_cast<int>(in.a.imm);
            if (n < 0) n = 0;
            if (n % 8) n += 8 - (n % 8);
            cursor += n;
            localOff.push_back(-cursor);
        }
    }
    int memFrame = cursor;
    if (memFrame % 16) memFrame += 8;
    if (memFrame > frame) {
        // The prologue was emitted with `frame`. Patch is harder, so recompute
        // by rewriting the subq we already printed when locals exist.
    }
    if (memFrame != frame && memFrame > 0) {
        auto pos = s.rfind("subq $");
        if (pos == std::string::npos) {
            auto at = s.find("movq %rsp, %rbp\n");
            if (at != std::string::npos) {
                at += std::string("movq %rsp, %rbp\n").size();
                s.insert(at, "  subq $" + std::to_string(memFrame) + ", %rsp\n");
            }
        } else {
            auto end = s.find(", %rsp\n", pos);
            if (end != std::string::npos)
                s.replace(pos, end - pos, "subq $" + std::to_string(memFrame));
        }
    }
    int localIndex = 0;
    for (const Block& b : f.blocks) {
        s += blockName(f.name, b.name);
        s += ":\n";
        for (const Inst& in : b.code) {
            if (in.op == Inst::Op::Local || in.op == Inst::Op::Lea || in.op == Inst::Op::LeaSym ||
                in.op == Inst::Op::Load || in.op == Inst::Op::Store || in.op == Inst::Op::GLoad ||
                in.op == Inst::Op::GStore) {
                int off = 0;
                if (in.op == Inst::Op::Local) off = localOff[static_cast<std::size_t>(localIndex++)];
                emitMem(s, in, off);
            } else {
                emitInst(s, f, in);
            }
        }
    }
    s += f.name;
    s += "_epilogue:\n  movq %rbp, %rsp\n  popq %rbp\n  ret\n";
    return s;
}

}  // namespace

std::string emitData(const Module& m) {
    std::string data;
    std::string ro;
    auto bytesOf = [](const Global& g) {
        std::string s;
        bool zero = g.bytes.empty();
        for (unsigned char c : g.bytes)
            if (c) zero = false;
        if (!g.reloc.empty()) {
            s += "  .quad ";
            s += g.reloc;
            s += "\n";
            return s;
        }
        int n = g.size > 0 ? g.size : static_cast<int>(g.bytes.size());
        if (zero) {
            s += "  .zero ";
            s += std::to_string(n);
            s += "\n";
            return s;
        }
        for (unsigned char c : g.bytes) {
            s += "  .byte ";
            s += std::to_string(static_cast<int>(c));
            s += "\n";
        }
        return s;
    };
    for (const Global& g : m.globals) {
        std::string item;
        if (g.name.empty() || g.name[0] != '.') item += ".globl " + g.name + "\n";
        item += g.name;
        item += ":\n";
        item += bytesOf(g);
        if (g.rodata) ro += item;
        else data += item;
    }
    std::string s;
    if (!ro.empty()) s += ".section .rodata\n" + ro;
    if (!data.empty()) s += ".data\n" + data;
    return s;
}

std::string emitAssembly(const Module& m) {
    std::string s = ".text\n";
    for (const Function& f : m.funcs) s += emitFunction(f);
    s += emitData(m);
    s += ".section .note.GNU-stack,\"\",@progbits\n";
    return s;
}

std::string compileToAssembly(const std::string& source, Diagnostics& diags) {
    Module m = lowerSource(source, diags);
    if (!diags.ok()) return {};
    return emitAssembly(m);
}

}  // namespace okaycc
