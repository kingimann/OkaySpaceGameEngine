#include "codegen.hpp"

#include <cstdint>
#include <string>

namespace okaycc {
namespace {

const char* kArgReg[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
const char* kPhys[] = {"%rbx", "%r12", "%r13"};
const RegHomes* curHomes = nullptr;

int homeOf(int v) {
    if (!curHomes || v < 0 || v >= static_cast<int>(curHomes->home.size())) return -1;
    return curHomes->home[static_cast<std::size_t>(v)];
}

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
    int h = homeOf(o.reg);
    s += "  movq ";
    s += h >= 0 ? kPhys[h] : slot(o.reg);
    s += ", ";
    s += reg;
    s += "\n";
}

std::string hex64(std::uint64_t v) {
    static const char* d = "0123456789abcdef";
    std::string o = "0x";
    bool any = false;
    for (int i = 60; i >= 0; i -= 4) {
        int n = static_cast<int>((v >> i) & 0xfu);
        if (!any && n == 0 && i) continue;
        any = true;
        o.push_back(d[n]);
    }
    if (!any) o.push_back('0');
    return o;
}

void loadXmm(std::string& s, const Operand& o, int xmm, int width) {
    std::string x = "%xmm" + std::to_string(xmm);
    if (o.kind == Operand::Kind::Imm) {
        s += "  movabsq $";
        s += hex64(static_cast<std::uint64_t>(o.imm));
        s += ", %rax\n  movq %rax, ";
        s += x;
        s += "\n";
        return;
    }
    s += width <= 4 ? "  movss " : "  movsd ";
    s += slot(o.reg);
    s += ", ";
    s += x;
    s += "\n";
}

void storeXmm(std::string& s, int xmm, int dst, int width) {
    s += width <= 4 ? "  movss %xmm" : "  movsd %xmm";
    s += std::to_string(xmm);
    s += ", ";
    s += slot(dst);
    s += "\n";
}

void store(std::string& s, const char* reg, int dst) {
    int h = homeOf(dst);
    s += "  movq ";
    s += reg;
    s += ", ";
    s += h >= 0 ? kPhys[h] : slot(dst);
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
    if (in.fp && in.op == Inst::Op::Cvt) {
        int src = in.extra;
        int dstW = in.b.kind == Operand::Kind::Imm ? static_cast<int>(in.b.imm) : 8;
        if (src == 0) {
            load(s, in.a, "%rax");
            s += dstW <= 4 ? "  cvtsi2ss %rax, %xmm0\n" : "  cvtsi2sd %rax, %xmm0\n";
            storeXmm(s, 0, in.dst, dstW);
        } else if (dstW == 0) {
            loadXmm(s, in.a, 0, src);
            s += src <= 4 ? "  cvttss2si %xmm0, %rax\n" : "  cvttsd2si %xmm0, %rax\n";
            store(s, "%rax", in.dst);
        } else {
            loadXmm(s, in.a, 0, src);
            if (src <= 4 && dstW == 8) s += "  cvtss2sd %xmm0, %xmm0\n";
            else if (src == 8 && dstW <= 4) s += "  cvtsd2ss %xmm0, %xmm0\n";
            storeXmm(s, 0, in.dst, dstW);
        }
        return;
    }
    if (in.fp && (in.op == Inst::Op::Add || in.op == Inst::Op::Sub || in.op == Inst::Op::Mul || in.op == Inst::Op::Div)) {
        loadXmm(s, in.a, 0, in.extra);
        loadXmm(s, in.b, 1, in.extra);
        const char* m = "add";
        if (in.op == Inst::Op::Sub) m = "sub";
        else if (in.op == Inst::Op::Mul) m = "mul";
        else if (in.op == Inst::Op::Div) m = "div";
        s += "  ";
        s += m;
        s += in.extra <= 4 ? "ss %xmm1, %xmm0\n" : "sd %xmm1, %xmm0\n";
        storeXmm(s, 0, in.dst, in.extra);
        return;
    }
    if (in.fp && (in.op == Inst::Op::Ceq || in.op == Inst::Op::Cne || in.op == Inst::Op::Clt || in.op == Inst::Op::Cle ||
                  in.op == Inst::Op::Cgt || in.op == Inst::Op::Cge)) {
        loadXmm(s, in.a, 0, in.extra);
        loadXmm(s, in.b, 1, in.extra);
        s += in.extra <= 4 ? "  ucomiss %xmm1, %xmm0\n" : "  ucomisd %xmm1, %xmm0\n";
        if (in.op == Inst::Op::Ceq) s += "  sete %al\n  setnp %cl\n  andb %cl, %al\n";
        else if (in.op == Inst::Op::Cne) s += "  setne %al\n  setp %cl\n  orb %cl, %al\n";
        else if (in.op == Inst::Op::Clt) s += "  setb %al\n  setnp %cl\n  andb %cl, %al\n";
        else if (in.op == Inst::Op::Cle) s += "  setbe %al\n  setnp %cl\n  andb %cl, %al\n";
        else if (in.op == Inst::Op::Cgt) s += "  seta %al\n";
        else s += "  setae %al\n";
        s += "  movzbq %al, %rax\n";
        store(s, "%rax", in.dst);
        return;
    }
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
    case Inst::Op::LeaFrame:
        s += "  leaq ";
        s += std::to_string(in.a.imm);
        s += "(%rbp), %rax\n";
        store(s, "%rax", in.dst);
        break;
    case Inst::Op::Call: {
        int nstack = 0;
        if (!in.stack.empty()) {
            nstack = static_cast<int>(in.stack.size());
            for (int i = nstack - 1; i >= 0; --i) {
                load(s, in.stack[static_cast<std::size_t>(i)], "%rax");
                s += "  pushq %rax\n";
            }
        } else {
            nstack = in.args.size() > 6 ? static_cast<int>(in.args.size()) - 6 : 0;
            if (nstack % 2) s += "  subq $8, %rsp\n";
            for (int i = nstack - 1; i >= 0; --i) {
                load(s, in.args[static_cast<std::size_t>(6 + i)], "%rax");
                s += "  pushq %rax\n";
            }
        }
        int nreg = static_cast<int>(in.args.size()) < 6 ? static_cast<int>(in.args.size()) : 6;
        if (!in.stack.empty()) nreg = static_cast<int>(in.args.size());
        for (int i = 0; i < nreg && i < 6; ++i) {
            load(s, in.args[static_cast<std::size_t>(i)], "%rax");
            s += "  movq %rax, ";
            s += kArgReg[i];
            s += "\n";
        }
        if (in.func.empty()) {
            load(s, in.a, "%rax");
            if (in.va) s += "  movq %rax, %r11\n  xorl %eax, %eax\n  call *%r11\n";
            else s += "  call *%rax\n";
        } else if (in.func == "syscall") {
            // Linux syscall. The C arguments are number, a, b, c in rdi, rsi, rdx, rcx.
            s += "  movq %rdi, %rax\n  movq %rsi, %rdi\n  movq %rdx, %rsi\n  movq %rcx, %rdx\n  syscall\n";
        } else {
            for (std::size_t i = 0; i < in.xmm.size() && i < 8; ++i)
                loadXmm(s, in.xmm[i], static_cast<int>(i), i < in.xmmW.size() ? in.xmmW[i] : 8);
            if (in.va) {
                s += "  movl $";
                s += std::to_string(in.xmm.size() < 8 ? in.xmm.size() : 8);
                s += ", %eax\n";
            }
            s += "  call ";
            s += in.func;
            s += "\n";
        }
        if (nstack) {
            int bytes = in.stack.empty() ? 8 * (nstack + (nstack % 2)) : 8 * nstack;
            s += "  addq $";
            s += std::to_string(bytes);
            s += ", %rsp\n";
        }
        if (in.fp && in.dst >= 0) storeXmm(s, 0, in.dst, in.extra);
        else if (in.dst >= 0) store(s, "%rax", in.dst);
        if (in.b.kind == Operand::Kind::Reg) store(s, "%rdx", in.b.reg);
        break;
    }
    case Inst::Op::Ret:
        if (in.fp) {
            loadXmm(s, in.a, 0, in.extra);
            s += "  jmp ";
            s += f.name;
            s += "_epilogue\n";
            break;
        }
        if (in.b.kind != Operand::Kind::None) load(s, in.b, "%rdx");
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
    case Inst::Op::Cvt:
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
    case Inst::Op::Cvt:
        break;
    default:
        break;
    }
}

std::string emitFunction(const Function& f) {
    RegHomes homes = assignRegs(f);
    curHomes = &homes;
    int slots = slotCount(f);
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
    int saveBase = cursor;
    cursor += homes.count * 8;
    int memFrame = cursor;
    if (memFrame % 16) memFrame += 8;
    std::vector<int> saveDisp;
    for (int i = 0; i < homes.count; ++i) saveDisp.push_back(-(saveBase + 8 * (i + 1)));
    std::string s;
    s += ".globl ";
    s += f.name;
    s += "\n";
    s += f.name;
    s += ":\n  pushq %rbp\n  movq %rsp, %rbp\n";
    if (memFrame) {
        s += "  subq $";
        s += std::to_string(memFrame);
        s += ", %rsp\n";
    }
    for (int i = 0; i < homes.count; ++i) {
        s += "  movq ";
        s += kPhys[i];
        s += ", ";
        s += std::to_string(saveDisp[static_cast<std::size_t>(i)]);
        s += "(%rbp)\n";
    }
    int nreg = f.params < 6 ? f.params : 6;
    for (int i = 0; i < nreg; ++i) {
        s += "  movq ";
        s += kArgReg[i];
        s += ", ";
        int h = homeOf(i);
        s += h >= 0 ? kPhys[h] : slot(i);
        s += "\n";
    }
    for (int i = 6; i < f.params; ++i) {
        int disp = 16 + 8 * (i - 6);
        s += "  movq ";
        s += std::to_string(disp);
        s += "(%rbp), %rax\n";
        store(s, "%rax", i);
    }
    for (std::size_t i = 0; i < f.xmmSlot.size(); ++i) {
        int w = i < f.xmmWidth.size() ? f.xmmWidth[i] : 8;
        s += w <= 4 ? "  movss %xmm" : "  movsd %xmm";
        s += std::to_string(i);
        s += ", ";
        s += slot(f.xmmSlot[i]);
        s += "\n";
    }
    if (f.vaNamed >= 0 && !localOff.empty()) {
        int base = localOff[0];
        for (int i = 0; i < 6; ++i) {
            s += "  movq ";
            s += kArgReg[i];
            s += ", ";
            s += std::to_string(base + 8 * i);
            s += "(%rbp)\n";
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
    s += "_epilogue:\n";
    for (int i = 0; i < homes.count; ++i) {
        s += "  movq ";
        s += std::to_string(saveDisp[static_cast<std::size_t>(i)]);
        s += "(%rbp), ";
        s += kPhys[i];
        s += "\n";
    }
    s += "  movq %rbp, %rsp\n  popq %rbp\n  ret\n";
    curHomes = nullptr;
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

std::string compileToAssembly(const std::string& source, Diagnostics& diags, const std::string& file) {
    Module m = lowerSource(source, diags, file);
    if (!diags.ok()) return {};
    return emitAssembly(m);
}

}  // namespace okaycc
