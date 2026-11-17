#include "elf.hpp"

#include "ir.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace okaycc {
namespace {

constexpr std::uint64_t kBase = 0x400000;
constexpr std::size_t kHeader = 64;
constexpr std::size_t kPhdr = 56;
constexpr std::size_t kLoadOff = kHeader + kPhdr;

enum Reg { Rax = 0, Rcx = 1, Rdx = 2, Rbx = 3, Rsp = 4, Rbp = 5, Rsi = 6, Rdi = 7, R8 = 8, R9 = 9, R12 = 12, R13 = 13 };

const int kArg[] = {Rdi, Rsi, Rdx, Rcx, R8, R9};
const int kPhys[] = {Rbx, R12, R13};
const RegHomes* curHomes = nullptr;

int homeOf(int v) {
    if (!curHomes || v < 0 || v >= static_cast<int>(curHomes->home.size())) return -1;
    return curHomes->home[static_cast<std::size_t>(v)];
}

enum class FixKind { Rel32, Plt32, Pc32, Abs64 };

struct Fix {
    FixKind kind;
    std::size_t at = 0;
    std::string target;
};

struct Image {
    std::vector<unsigned char> bytes;
    std::map<std::string, std::size_t> labels;
    std::vector<Fix> fixes;
    bool bad = false;
    std::string why;
    std::size_t textEnd = 0;
    std::size_t roStart = 0;
    std::size_t roEnd = 0;
    std::size_t dataStart = 0;
    std::size_t dataEnd = 0;
    struct Pub {
        std::string name;
        std::size_t off = 0;
        int sec = 1;
        int size = 0;
        bool func = false;
        bool global = true;
    };
    std::vector<Pub> pubs;
};

void u8(Image& im, unsigned v) { im.bytes.push_back(static_cast<unsigned char>(v)); }

void u32(Image& im, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) u8(im, (v >> (8 * i)) & 0xffu);
}

void u64(Image& im, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) u8(im, static_cast<unsigned>(v >> (8 * i)) & 0xffu);
}

void i32(Image& im, std::int32_t v) { u32(im, static_cast<std::uint32_t>(v)); }

void rexW(Image& im, int reg, int rm) {
    int x = 0x48;
    if (reg & 8) x |= 0x4;
    if (rm & 8) x |= 0x1;
    u8(im, x);
}

int modrm(int mod, int reg, int rm) { return (mod << 6) | ((reg & 7) << 3) | (rm & 7); }

void opReg(Image& im, int opcode, int dstRm, int srcReg) {
    rexW(im, srcReg, dstRm);
    u8(im, opcode);
    u8(im, modrm(3, srcReg, dstRm));
}

void movReg(Image& im, int dst, int src) { opReg(im, 0x89, dst, src); }

void sext(Image& im, int width, bool uns) {
    if (width != 1 && width != 2 && width != 4) return;
    if (uns && width == 4) {
        u8(im, 0x89);
        u8(im, modrm(3, Rax, Rax));
        return;
    }
    rexW(im, Rax, Rax);
    if (!uns && width == 4) {
        u8(im, 0x63);
    } else {
        u8(im, 0x0F);
        u8(im, uns ? (width == 1 ? 0xB6 : 0xB7) : (width == 1 ? 0xBE : 0xBF));
    }
    u8(im, modrm(3, Rax, Rax));
}

void movImm(Image& im, int dst, std::int64_t v) {
    rexW(im, 0, dst);
    u8(im, 0xB8 + (dst & 7));
    u64(im, static_cast<std::uint64_t>(v));
}

void aluReg(Image& im, int opcode, int dst, int src) { opReg(im, opcode, dst, src); }

void aluImm(Image& im, int dst, std::int32_t imm, int ext) {
    rexW(im, ext, dst);
    u8(im, 0x81);
    u8(im, modrm(3, ext, dst));
    i32(im, imm);
}

void imulReg(Image& im, int dst, int src) {
    rexW(im, dst, src);
    u8(im, 0x0F);
    u8(im, 0xAF);
    u8(im, modrm(3, dst, src));
}

void shiftCl(Image& im, int dst, int ext) {
    rexW(im, ext, dst);
    u8(im, 0xD3);
    u8(im, modrm(3, ext, dst));
}

void unaryF7(Image& im, int dst, int ext) {
    rexW(im, ext, dst);
    u8(im, 0xF7);
    u8(im, modrm(3, ext, dst));
}

void setcc(Image& im, int cc) {
    u8(im, 0x0F);
    u8(im, cc);
    u8(im, modrm(3, 0, Rax));
}

void movzxAl(Image& im) {
    rexW(im, Rax, Rax);
    u8(im, 0x0F);
    u8(im, 0xB6);
    u8(im, modrm(3, Rax, Rax));
}

void rel32(Image& im, const std::string& target, FixKind kind = FixKind::Rel32) {
    Fix f;
    f.kind = kind;
    f.at = im.bytes.size();
    f.target = target;
    im.fixes.push_back(f);
    u32(im, 0);
}

void jmpRel(Image& im, const std::string& target) {
    u8(im, 0xE9);
    rel32(im, target, FixKind::Rel32);
}

void jneRel(Image& im, const std::string& target) {
    u8(im, 0x0F);
    u8(im, 0x85);
    rel32(im, target, FixKind::Rel32);
}

void callRel(Image& im, const std::string& target) {
    u8(im, 0xE8);
    rel32(im, target, FixKind::Plt32);
}

void pushReg(Image& im, int reg) {
    if (reg >= 8) u8(im, 0x41);
    u8(im, 0x50 + (reg & 7));
}

void popReg(Image& im, int reg) {
    if (reg >= 8) u8(im, 0x41);
    u8(im, 0x58 + (reg & 7));
}

int slotDisp(int reg) { return -8 * (reg + 1); }

void movRaxSlot(Image& im, int slot) {
    rexW(im, Rax, Rbp);
    u8(im, 0x8B);
    u8(im, modrm(2, Rax, Rbp));
    i32(im, slotDisp(slot));
}

void movRcxSlot(Image& im, int slot) {
    rexW(im, Rcx, Rbp);
    u8(im, 0x8B);
    u8(im, modrm(2, Rcx, Rbp));
    i32(im, slotDisp(slot));
}

void movSlotRax(Image& im, int slot) {
    int h = homeOf(slot);
    if (h >= 0) {
        movReg(im, kPhys[h], Rax);
        return;
    }
    rexW(im, Rax, Rbp);
    u8(im, 0x89);
    u8(im, modrm(2, Rax, Rbp));
    i32(im, slotDisp(slot));
}

void loadRax(Image& im, const Operand& o) {
    if (o.kind == Operand::Kind::Imm) movImm(im, Rax, o.imm);
    else if (homeOf(o.reg) >= 0) movReg(im, Rax, kPhys[homeOf(o.reg)]);
    else movRaxSlot(im, o.reg);
}

void loadRcx(Image& im, const Operand& o) {
    if (o.kind == Operand::Kind::Imm) movImm(im, Rcx, o.imm);
    else if (homeOf(o.reg) >= 0) movReg(im, Rcx, kPhys[homeOf(o.reg)]);
    else movRcxSlot(im, o.reg);
}

void leaRaxRbp(Image& im, std::int32_t disp) {
    rexW(im, Rax, Rbp);
    u8(im, 0x8D);
    u8(im, modrm(2, Rax, Rbp));
    i32(im, disp);
}

void leaRaxRip(Image& im, const std::string& target) {
    rexW(im, Rax, Rbp);
    u8(im, 0x8D);
    u8(im, modrm(0, Rax, Rbp));
    rel32(im, target, FixKind::Pc32);
}

void movRaxRip(Image& im, const std::string& target) {
    rexW(im, Rax, Rbp);
    u8(im, 0x8B);
    u8(im, modrm(0, Rax, Rbp));
    rel32(im, target, FixKind::Pc32);
}

void movRipRax(Image& im, const std::string& target) {
    rexW(im, Rax, Rbp);
    u8(im, 0x89);
    u8(im, modrm(0, Rax, Rbp));
    rel32(im, target, FixKind::Pc32);
}

void movRaxMemRax(Image& im) {
    rexW(im, Rax, Rax);
    u8(im, 0x8B);
    u8(im, modrm(0, Rax, Rax));
}

void movMemRaxRcx(Image& im) {
    rexW(im, Rcx, Rax);
    u8(im, 0x89);
    u8(im, modrm(0, Rcx, Rax));
}

void loadMemRax(Image& im, int width, bool uns) {
    if (width == 8 || width == 0) {
        movRaxMemRax(im);
        return;
    }
    if (uns && width == 4) {
        u8(im, 0x8B);
        u8(im, modrm(0, Rax, Rax));
        return;
    }
    rexW(im, Rax, Rax);
    if (!uns && width == 4) {
        u8(im, 0x63);
        u8(im, modrm(0, Rax, Rax));
        return;
    }
    u8(im, 0x0F);
    u8(im, uns ? (width == 1 ? 0xB6 : 0xB7) : (width == 1 ? 0xBE : 0xBF));
    u8(im, modrm(0, Rax, Rax));
}

void loadRipRax(Image& im, const std::string& target, int width, bool uns) {
    if (width == 8 || width == 0) {
        movRaxRip(im, target);
        return;
    }
    if (uns && width == 4) {
        u8(im, 0x8B);
        u8(im, modrm(0, Rax, Rbp));
        rel32(im, target, FixKind::Pc32);
        return;
    }
    rexW(im, Rax, Rbp);
    if (!uns && width == 4) {
        u8(im, 0x63);
        u8(im, modrm(0, Rax, Rbp));
        rel32(im, target, FixKind::Pc32);
        return;
    }
    u8(im, 0x0F);
    u8(im, uns ? (width == 1 ? 0xB6 : 0xB7) : (width == 1 ? 0xBE : 0xBF));
    u8(im, modrm(0, Rax, Rbp));
    rel32(im, target, FixKind::Pc32);
}

void storeMemRcx(Image& im, int width) {
    if (width == 8 || width == 0) {
        movMemRaxRcx(im);
        return;
    }
    if (width == 2) u8(im, 0x66);
    u8(im, width == 1 ? 0x88 : 0x89);
    u8(im, modrm(0, Rcx, Rax));
}

void storeRipRax(Image& im, const std::string& target, int width) {
    if (width == 8 || width == 0) {
        movRipRax(im, target);
        return;
    }
    if (width == 2) u8(im, 0x66);
    u8(im, width == 1 ? 0x88 : 0x89);
    u8(im, modrm(0, Rax, Rbp));
    rel32(im, target, FixKind::Pc32);
}

std::string blockName(const std::string& func, const std::string& block) { return func + "_" + block; }

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

int ccByte(Inst::Op op, bool uns) {
    if (uns) {
        switch (op) {
        case Inst::Op::Clt: return 0x92;
        case Inst::Op::Cge: return 0x93;
        case Inst::Op::Cle: return 0x96;
        case Inst::Op::Cgt: return 0x97;
        case Inst::Op::Ceq: return 0x94;
        case Inst::Op::Cne: return 0x95;
        default: return 0x94;
        }
    }
    switch (op) {
    case Inst::Op::Ceq: return 0x94;
    case Inst::Op::Cne: return 0x95;
    case Inst::Op::Clt: return 0x9C;
    case Inst::Op::Cle: return 0x9E;
    case Inst::Op::Cgt: return 0x9F;
    case Inst::Op::Cge: return 0x9D;
    default: return 0x94;
    }
}

void emitBin(Image& im, const Inst& in) {
    loadRax(im, in.a);
    if (in.b.kind == Operand::Kind::Imm && (in.op == Inst::Op::Add || in.op == Inst::Op::Sub) &&
        in.b.imm >= -2147483648LL && in.b.imm <= 2147483647LL) {
        aluImm(im, Rax, static_cast<std::int32_t>(in.b.imm), in.op == Inst::Op::Add ? 0 : 5);
    } else {
        loadRcx(im, in.b);
        int opcode = 0x01;
        if (in.op == Inst::Op::Sub) opcode = 0x29;
        else if (in.op == Inst::Op::And) opcode = 0x21;
        else if (in.op == Inst::Op::Or) opcode = 0x09;
        else if (in.op == Inst::Op::Xor) opcode = 0x31;
        aluReg(im, opcode, Rax, Rcx);
    }
    movSlotRax(im, in.dst);
}

void xmmMem(Image& im, int xmm, int disp, int width, bool store) {
    u8(im, width <= 4 ? 0xF3 : 0xF2);
    u8(im, 0x0F);
    u8(im, store ? 0x11 : 0x10);
    u8(im, modrm(2, xmm, Rbp));
    i32(im, disp);
}

void movqXmmRax(Image& im, int xmm, bool toXmm) {
    u8(im, 0x66);
    u8(im, 0x48);
    u8(im, 0x0F);
    u8(im, 0x6E);
    // 6E is movq xmm, r/m when toXmm. 7E is movq r/m, xmm.
    if (!toXmm) im.bytes.back() = 0x7E;
    u8(im, modrm(3, xmm, Rax));
}

void loadXmm(Image& im, const Operand& o, int xmm, int width) {
    if (o.kind == Operand::Kind::Imm) {
        movImm(im, Rax, o.imm);
        movqXmmRax(im, xmm, true);
        return;
    }
    xmmMem(im, xmm, slotDisp(o.reg), width, false);
}

void storeXmm(Image& im, int xmm, int dst, int width) { xmmMem(im, xmm, slotDisp(dst), width, true); }

void fpBin(Image& im, int width, int opcode) {
    u8(im, width <= 4 ? 0xF3 : 0xF2);
    u8(im, 0x0F);
    u8(im, opcode);
    u8(im, modrm(3, 0, 1));
}

void emitInst(Image& im, const Function& f, const Inst& in) {
    if (in.fp && in.op == Inst::Op::Cvt) {
        int src = in.extra;
        int code = in.b.kind == Operand::Kind::Imm ? static_cast<int>(in.b.imm) : 8;
        int dstW = code < 0 ? -code : code;
        if (src == 0) {
            loadRax(im, in.a);
            u8(im, code <= 4 ? 0xF3 : 0xF2);
            u8(im, 0x48);
            u8(im, 0x0F);
            u8(im, 0x2A);
            u8(im, modrm(3, 0, Rax));
            storeXmm(im, 0, in.dst, dstW);
        } else if (code <= 0) {
            loadXmm(im, in.a, 0, src);
            u8(im, src <= 4 ? 0xF3 : 0xF2);
            if (dstW > 4) u8(im, 0x48);
            u8(im, 0x0F);
            u8(im, 0x2C);
            u8(im, modrm(3, Rax, 0));
            if (dstW <= 4) {
                u8(im, 0x48);
                u8(im, 0x63);
                u8(im, modrm(3, Rax, Rax));
            }
            movSlotRax(im, in.dst);
        } else {
            loadXmm(im, in.a, 0, src);
            if (src <= 4 && dstW == 8) {
                u8(im, 0xF3);
                u8(im, 0x0F);
                u8(im, 0x5A);
                u8(im, modrm(3, 0, 0));
            } else if (src == 8 && dstW <= 4) {
                u8(im, 0xF2);
                u8(im, 0x0F);
                u8(im, 0x5A);
                u8(im, modrm(3, 0, 0));
            }
            storeXmm(im, 0, in.dst, dstW);
        }
        return;
    }
    if (in.fp && (in.op == Inst::Op::Add || in.op == Inst::Op::Sub || in.op == Inst::Op::Mul || in.op == Inst::Op::Div)) {
        loadXmm(im, in.a, 0, in.extra);
        loadXmm(im, in.b, 1, in.extra);
        int op = 0x58;
        if (in.op == Inst::Op::Sub) op = 0x5C;
        else if (in.op == Inst::Op::Mul) op = 0x59;
        else if (in.op == Inst::Op::Div) op = 0x5E;
        fpBin(im, in.extra, op);
        storeXmm(im, 0, in.dst, in.extra);
        return;
    }
    if (in.fp && (in.op == Inst::Op::Ceq || in.op == Inst::Op::Cne || in.op == Inst::Op::Clt || in.op == Inst::Op::Cle ||
                  in.op == Inst::Op::Cgt || in.op == Inst::Op::Cge)) {
        loadXmm(im, in.a, 0, in.extra);
        loadXmm(im, in.b, 1, in.extra);
        if (in.extra <= 4) u8(im, 0x0F);
        else {
            u8(im, 0x66);
            u8(im, 0x0F);
        }
        u8(im, 0x2E);
        u8(im, modrm(3, 0, 1));
        auto setOne = [&](int cc) {
            u8(im, 0x0F);
            u8(im, cc);
            u8(im, modrm(3, 0, Rax));
        };
        if (in.op == Inst::Op::Ceq || in.op == Inst::Op::Cne || in.op == Inst::Op::Clt || in.op == Inst::Op::Cle) {
            int cc = in.op == Inst::Op::Ceq ? 0x94 : in.op == Inst::Op::Cne ? 0x95 : in.op == Inst::Op::Clt ? 0x92 : 0x96;
            setOne(cc);
            u8(im, 0x0F);
            u8(im, in.op == Inst::Op::Cne ? 0x9A : 0x9B);
            u8(im, modrm(3, 0, Rcx));
            u8(im, in.op == Inst::Op::Cne ? 0x08 : 0x20);
            u8(im, modrm(3, Rcx, Rax));
        } else {
            setOne(in.op == Inst::Op::Cgt ? 0x97 : 0x93);
        }
        u8(im, 0x48);
        u8(im, 0x0F);
        u8(im, 0xB6);
        u8(im, modrm(3, Rax, Rax));
        movSlotRax(im, in.dst);
        return;
    }
    switch (in.op) {
    case Inst::Op::Mov:
        loadRax(im, in.a);
        sext(im, in.extra, in.uns);
        movSlotRax(im, in.dst);
        break;
    case Inst::Op::Add:
    case Inst::Op::Sub:
    case Inst::Op::And:
    case Inst::Op::Or:
    case Inst::Op::Xor:
        emitBin(im, in);
        break;
    case Inst::Op::Mul:
        loadRax(im, in.a);
        loadRcx(im, in.b);
        imulReg(im, Rax, Rcx);
        movSlotRax(im, in.dst);
        break;
    case Inst::Op::Shl:
    case Inst::Op::Shr:
        loadRax(im, in.a);
        loadRcx(im, in.b);
        shiftCl(im, Rax, in.op == Inst::Op::Shl ? 4 : 5);
        movSlotRax(im, in.dst);
        break;
    case Inst::Op::Div:
    case Inst::Op::Mod:
        loadRax(im, in.a);
        u8(im, 0x48);
        u8(im, 0x99);
        loadRcx(im, in.b);
        unaryF7(im, Rcx, 7);
        if (in.op == Inst::Op::Div) movSlotRax(im, in.dst);
        else {
            movReg(im, Rax, Rdx);
            movSlotRax(im, in.dst);
        }
        break;
    case Inst::Op::Neg:
        loadRax(im, in.a);
        unaryF7(im, Rax, 3);
        movSlotRax(im, in.dst);
        break;
    case Inst::Op::Not:
        loadRax(im, in.a);
        unaryF7(im, Rax, 2);
        movSlotRax(im, in.dst);
        break;
    case Inst::Op::Ceq:
    case Inst::Op::Cne:
    case Inst::Op::Clt:
    case Inst::Op::Cle:
    case Inst::Op::Cgt:
    case Inst::Op::Cge:
        loadRax(im, in.a);
        loadRcx(im, in.b);
        aluReg(im, 0x39, Rax, Rcx);
        setcc(im, ccByte(in.op, in.uns));
        movzxAl(im);
        movSlotRax(im, in.dst);
        break;
    case Inst::Op::Jmp:
        jmpRel(im, blockName(f.name, in.label));
        break;
    case Inst::Op::Br:
        loadRax(im, in.a);
        aluReg(im, 0x85, Rax, Rax);
        jneRel(im, blockName(f.name, in.label));
        jmpRel(im, blockName(f.name, in.label2));
        break;
    case Inst::Op::LeaFrame:
        leaRaxRbp(im, static_cast<std::int32_t>(in.a.imm));
        movSlotRax(im, in.dst);
        break;
    case Inst::Op::Call: {
        int nstack = 0;
        if (!in.stack.empty()) {
            nstack = static_cast<int>(in.stack.size());
            for (int i = nstack - 1; i >= 0; --i) {
                loadRax(im, in.stack[static_cast<std::size_t>(i)]);
                pushReg(im, Rax);
            }
        } else {
            nstack = in.args.size() > 6 ? static_cast<int>(in.args.size()) - 6 : 0;
            if (nstack % 2) aluImm(im, Rsp, 8, 5);
            for (int i = nstack - 1; i >= 0; --i) {
                loadRax(im, in.args[static_cast<std::size_t>(6 + i)]);
                pushReg(im, Rax);
            }
        }
        int nreg = static_cast<int>(in.args.size()) < 6 ? static_cast<int>(in.args.size()) : 6;
        if (!in.stack.empty()) nreg = static_cast<int>(in.args.size());
        for (int i = 0; i < nreg && i < 6; ++i) {
            loadRax(im, in.args[static_cast<std::size_t>(i)]);
            movReg(im, kArg[i], Rax);
        }
        if (in.func.empty()) {
            loadRax(im, in.a);
            if (in.va) {
                u8(im, 0x49);
                u8(im, 0x89);
                u8(im, 0xC3);
                u8(im, 0x31);
                u8(im, 0xC0);
                u8(im, 0x41);
                u8(im, 0xFF);
                u8(im, 0xD3);
            } else {
                u8(im, 0xFF);
                u8(im, 0xD0);
            }
        } else if (in.func == "syscall") {
            // Linux syscall: C call syscall(number, a, b, c) is already in rdi, rsi, rdx, rcx.
            movReg(im, Rax, Rdi);
            movReg(im, Rdi, Rsi);
            movReg(im, Rsi, Rdx);
            movReg(im, Rdx, Rcx);
            u8(im, 0x0F);
            u8(im, 0x05);
        } else {
            for (std::size_t i = 0; i < in.xmm.size() && i < 8; ++i)
                loadXmm(im, in.xmm[i], static_cast<int>(i), i < in.xmmW.size() ? in.xmmW[i] : 8);
            if (in.va) {
                int n = static_cast<int>(in.xmm.size() < 8 ? in.xmm.size() : 8);
                u8(im, 0xB8);
                u32(im, static_cast<std::uint32_t>(n));
            }
            callRel(im, in.func);
        }
        if (nstack) {
            int bytes = in.stack.empty() ? 8 * (nstack + (nstack % 2)) : 8 * nstack;
            aluImm(im, Rsp, bytes, 0);
        }
        if (in.fp && in.dst >= 0) storeXmm(im, 0, in.dst, in.extra);
        else if (in.dst >= 0) movSlotRax(im, in.dst);
        if (in.b.kind == Operand::Kind::Reg) {
            int h = homeOf(in.b.reg);
            if (h >= 0) movReg(im, kPhys[h], Rdx);
            else {
                rexW(im, Rdx, Rbp);
                u8(im, 0x89);
                u8(im, modrm(2, Rdx, Rbp));
                i32(im, slotDisp(in.b.reg));
            }
        }
        break;
    }
    case Inst::Op::Ret:
        if (in.fp) {
            loadXmm(im, in.a, 0, in.extra);
            jmpRel(im, f.name + "_epilogue");
            break;
        }
        if (in.b.kind != Operand::Kind::None) {
            if (in.b.kind == Operand::Kind::Imm) movImm(im, Rdx, in.b.imm);
            else if (homeOf(in.b.reg) >= 0) movReg(im, Rdx, kPhys[homeOf(in.b.reg)]);
            else {
                rexW(im, Rdx, Rbp);
                u8(im, 0x8B);
                u8(im, modrm(2, Rdx, Rbp));
                i32(im, slotDisp(in.b.reg));
            }
        }
        if (in.a.kind != Operand::Kind::None) {
            loadRax(im, in.a);
            sext(im, in.extra, in.uns);
        }
        jmpRel(im, f.name + "_epilogue");
        break;
    default:
        break;
    }
}

void emitMem(Image& im, const Inst& in, int localOff) {
    switch (in.op) {
    case Inst::Op::Local:
        leaRaxRbp(im, localOff);
        movSlotRax(im, in.dst);
        break;
    case Inst::Op::Lea:
        leaRaxRbp(im, slotDisp(in.a.reg));
        movSlotRax(im, in.dst);
        break;
    case Inst::Op::LeaSym:
        leaRaxRip(im, in.func);
        movSlotRax(im, in.dst);
        break;
    case Inst::Op::Load:
        loadRax(im, in.a);
        loadMemRax(im, in.extra, in.uns);
        movSlotRax(im, in.dst);
        break;
    case Inst::Op::Store:
        loadRax(im, in.a);
        loadRcx(im, in.b);
        storeMemRcx(im, in.extra);
        break;
    case Inst::Op::GLoad:
        loadRipRax(im, in.func, in.extra, in.uns);
        movSlotRax(im, in.dst);
        break;
    case Inst::Op::GStore:
        loadRax(im, in.a);
        storeRipRax(im, in.func, in.extra);
        break;
    default:
        break;
    }
}

void movDisp(Image& im, int disp, int reg, bool store) {
    rexW(im, reg, Rbp);
    u8(im, store ? 0x89 : 0x8B);
    u8(im, modrm(2, reg, Rbp));
    i32(im, disp);
}

void emitFunction(Image& im, const Function& f) {
    RegHomes homes = assignRegs(f);
    curHomes = &homes;
    std::size_t start = im.bytes.size();
    im.labels[f.name] = start;
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
    int used = cursor;
    if (used % 16) used += 8;
    std::vector<int> saveDisp;
    for (int i = 0; i < homes.count; ++i) saveDisp.push_back(-(saveBase + 8 * (i + 1)));
    pushReg(im, Rbp);
    movReg(im, Rbp, Rsp);
    if (used) aluImm(im, Rsp, used, 5);
    for (int i = 0; i < homes.count; ++i) movDisp(im, saveDisp[static_cast<std::size_t>(i)], kPhys[i], true);
    int nreg = f.params < 6 ? f.params : 6;
    for (int i = 0; i < nreg; ++i) {
        movReg(im, Rax, kArg[i]);
        movSlotRax(im, i);
    }
    for (int i = 6; i < f.params; ++i) {
        int disp = 16 + 8 * (i - 6);
        rexW(im, Rax, Rbp);
        u8(im, 0x8B);
        u8(im, modrm(2, Rax, Rbp));
        i32(im, disp);
        movSlotRax(im, i);
    }
    for (std::size_t i = 0; i < f.xmmSlot.size(); ++i) {
        int w = i < f.xmmWidth.size() ? f.xmmWidth[i] : 8;
        xmmMem(im, static_cast<int>(i), slotDisp(f.xmmSlot[i]), w, true);
    }
    if (f.vaNamed >= 0 && !localOff.empty()) {
        int base = localOff[0];
        for (int i = 0; i < 8; ++i) {
            u8(im, 0xF3);
            u8(im, 0x0F);
            u8(im, 0x7F);
            u8(im, modrm(2, i, Rbp));
            i32(im, base + 48 + 16 * i);
        }
    }
    if (f.vaNamed >= 0 && !localOff.empty()) {
        int base = localOff[0];
        for (int i = 0; i < 6; ++i) {
            rexW(im, kArg[i], Rbp);
            u8(im, 0x89);
            u8(im, modrm(2, kArg[i], Rbp));
            i32(im, base + 8 * i);
        }
    }
    int localIndex = 0;
    for (const Block& b : f.blocks) {
        im.labels[blockName(f.name, b.name)] = im.bytes.size();
        for (const Inst& in : b.code) {
            bool mem = in.op == Inst::Op::Local || in.op == Inst::Op::Lea || in.op == Inst::Op::LeaSym ||
                       in.op == Inst::Op::Load || in.op == Inst::Op::Store || in.op == Inst::Op::GLoad ||
                       in.op == Inst::Op::GStore;
            if (mem) {
                int off = 0;
                if (in.op == Inst::Op::Local) off = localOff[static_cast<std::size_t>(localIndex++)];
                emitMem(im, in, off);
            } else {
                emitInst(im, f, in);
            }
        }
    }
    im.labels[f.name + "_epilogue"] = im.bytes.size();
    for (int i = 0; i < homes.count; ++i) movDisp(im, saveDisp[static_cast<std::size_t>(i)], kPhys[i], false);
    movReg(im, Rsp, Rbp);
    popReg(im, Rbp);
    u8(im, 0xC3);
    curHomes = nullptr;
    Image::Pub p;
    p.name = f.name;
    p.off = start;
    p.sec = 1;
    p.size = static_cast<int>(im.bytes.size() - start);
    p.func = true;
    p.global = true;
    im.pubs.push_back(p);
}

void emitData(Image& im, const Module& m) {
    for (const Global& g : m.globals) {
        bool ro = g.rodata;
        if (ro) {
            if (im.roStart == im.roEnd) im.roStart = im.bytes.size();
            else
                while (im.bytes.size() % 8) u8(im, 0);
        } else {
            if (im.dataStart == im.dataEnd) im.dataStart = im.bytes.size();
            else
                while (im.bytes.size() % 8) u8(im, 0);
        }
        im.labels[g.name] = im.bytes.size();
        std::size_t item = im.bytes.size();
        if (!g.reloc.empty()) {
            Fix f;
            f.kind = FixKind::Abs64;
            f.at = im.bytes.size();
            f.target = g.reloc;
            im.fixes.push_back(f);
            u64(im, 0);
        } else if (!g.bytes.empty()) {
            bool zero = true;
            for (unsigned char c : g.bytes)
                if (c) zero = false;
            if (!zero) {
                for (unsigned char c : g.bytes) u8(im, c);
            } else {
                int n = g.size > 0 ? g.size : static_cast<int>(g.bytes.size());
                for (int i = 0; i < n; ++i) u8(im, 0);
            }
        } else {
            int n = g.size > 0 ? g.size : static_cast<int>(g.bytes.size());
            for (int i = 0; i < n; ++i) u8(im, 0);
        }
        if (ro) im.roEnd = im.bytes.size();
        else im.dataEnd = im.bytes.size();
        Image::Pub p;
        p.name = g.name;
        p.off = item - (ro ? im.roStart : im.dataStart);
        p.sec = ro ? 2 : 3;
        p.size = static_cast<int>(im.bytes.size() - item);
        p.func = false;
        p.global = g.name.empty() || g.name[0] != '.';
        im.pubs.push_back(p);
    }
}

bool patch(Image& im) {
    for (const Fix& f : im.fixes) {
        auto it = im.labels.find(f.target);
        if (it == im.labels.end()) {
            im.bad = true;
            im.why = "missing symbol " + f.target;
            return false;
        }
        if (f.kind == FixKind::Rel32 || f.kind == FixKind::Plt32 || f.kind == FixKind::Pc32) {
            auto rel = static_cast<std::int64_t>(it->second) - static_cast<std::int64_t>(f.at + 4);
            if (rel < -2147483648LL || rel > 2147483647LL) {
                im.bad = true;
                im.why = "jump is too far";
                return false;
            }
            auto u = static_cast<std::uint32_t>(static_cast<std::int32_t>(rel));
            for (int i = 0; i < 4; ++i) im.bytes[f.at + static_cast<std::size_t>(i)] = static_cast<unsigned char>((u >> (8 * i)) & 0xffu);
        } else {
            std::uint64_t v = kBase + kLoadOff + it->second;
            for (int i = 0; i < 8; ++i)
                im.bytes[f.at + static_cast<std::size_t>(i)] = static_cast<unsigned char>((v >> (8 * i)) & 0xffu);
        }
    }
    return true;
}

bool patchLocal(Image& im) {
    for (const Fix& f : im.fixes) {
        if (f.kind != FixKind::Rel32) continue;
        auto it = im.labels.find(f.target);
        if (it == im.labels.end()) {
            im.bad = true;
            im.why = "missing label " + f.target;
            return false;
        }
        auto rel = static_cast<std::int64_t>(it->second) - static_cast<std::int64_t>(f.at + 4);
        auto u = static_cast<std::uint32_t>(static_cast<std::int32_t>(rel));
        for (int i = 0; i < 4; ++i)
            im.bytes[f.at + static_cast<std::size_t>(i)] = static_cast<unsigned char>((u >> (8 * i)) & 0xffu);
    }
    return true;
}

struct StrTab {
    std::string bytes = std::string(1, '\0');
    std::uint32_t add(const std::string& s) {
        auto at = static_cast<std::uint32_t>(bytes.size());
        bytes += s;
        bytes.push_back('\0');
        return at;
    }
};

void au32(std::vector<unsigned char>& o, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xffu));
}
void au64(std::vector<unsigned char>& o, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) o.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xffu));
}
void ai64(std::vector<unsigned char>& o, std::int64_t v) { au64(o, static_cast<std::uint64_t>(v)); }

void shdr(std::vector<unsigned char>& o, std::uint32_t name, std::uint32_t type, std::uint64_t flags,
          std::uint64_t off, std::uint64_t size, std::uint32_t link, std::uint32_t info, std::uint64_t align,
          std::uint64_t entsize) {
    au32(o, name);
    au32(o, type);
    au64(o, flags);
    au64(o, 0);
    au64(o, off);
    au64(o, size);
    au32(o, link);
    au32(o, info);
    au64(o, align);
    au64(o, entsize);
}

std::vector<unsigned char> writeObject(Image& im) {
    std::vector<unsigned char> text(im.bytes.begin(), im.bytes.begin() + static_cast<std::ptrdiff_t>(im.textEnd));
    std::vector<unsigned char> ro;
    std::vector<unsigned char> data;
    if (im.roEnd > im.roStart)
        ro.assign(im.bytes.begin() + static_cast<std::ptrdiff_t>(im.roStart),
                  im.bytes.begin() + static_cast<std::ptrdiff_t>(im.roEnd));
    if (im.dataEnd > im.dataStart)
        data.assign(im.bytes.begin() + static_cast<std::ptrdiff_t>(im.dataStart),
                    im.bytes.begin() + static_cast<std::ptrdiff_t>(im.dataEnd));

    StrTab strs;
    std::vector<unsigned char> syms;
    auto addSym = [&](std::uint32_t name, unsigned bind, unsigned type, std::uint16_t shndx, std::uint64_t value,
                      std::uint64_t size) {
        au32(syms, name);
        syms.push_back(static_cast<unsigned char>((bind << 4) | (type & 0xf)));
        syms.push_back(0);
        syms.push_back(static_cast<unsigned char>(shndx & 0xff));
        syms.push_back(static_cast<unsigned char>((shndx >> 8) & 0xff));
        au64(syms, value);
        au64(syms, size);
    };
    addSym(0, 0, 0, 0, 0, 0);
    addSym(0, 0, 3, 1, 0, 0);
    addSym(0, 0, 3, 2, 0, 0);
    addSym(0, 0, 3, 3, 0, 0);
    std::map<std::string, std::uint32_t> idx;
    for (const Image::Pub& p : im.pubs) {
        if (p.global) continue;
        idx[p.name] = static_cast<std::uint32_t>(syms.size() / 24);
        addSym(strs.add(p.name), 0, p.func ? 2 : 1, static_cast<std::uint16_t>(p.sec), p.off, static_cast<std::uint64_t>(p.size));
    }
    auto firstGlobal = static_cast<std::uint32_t>(syms.size() / 24);
    for (const Image::Pub& p : im.pubs) {
        if (!p.global) continue;
        idx[p.name] = static_cast<std::uint32_t>(syms.size() / 24);
        addSym(strs.add(p.name), 1, p.func ? 2 : 1, static_cast<std::uint16_t>(p.sec), p.off, static_cast<std::uint64_t>(p.size));
    }
    auto ensure = [&](const std::string& name) {
        if (idx.count(name)) return idx[name];
        auto id = static_cast<std::uint32_t>(syms.size() / 24);
        idx[name] = id;
        addSym(strs.add(name), 1, 0, 0, 0, 0);
        return id;
    };
    std::vector<unsigned char> relaText, relaRo, relaData;
    auto addRela = [&](std::vector<unsigned char>& r, std::uint64_t off, std::uint32_t sym, std::uint32_t type,
                       std::int64_t addend) {
        au64(r, off);
        au64(r, (static_cast<std::uint64_t>(sym) << 32) | type);
        ai64(r, addend);
    };
    for (const Fix& f : im.fixes) {
        if (f.kind == FixKind::Rel32) continue;
        std::uint32_t sym = ensure(f.target);
        if (f.kind == FixKind::Abs64) {
            if (f.at >= im.dataStart && f.at < im.dataEnd)
                addRela(relaData, f.at - im.dataStart, sym, 1, 0);
            else if (f.at >= im.roStart && f.at < im.roEnd)
                addRela(relaRo, f.at - im.roStart, sym, 1, 0);
        } else if (f.at < im.textEnd) {
            std::uint32_t type = f.kind == FixKind::Plt32 ? 4u : 2u;
            addRela(relaText, f.at, sym, type, -4);
        }
    }
    StrTab sh;
    auto nText = sh.add(".text");
    auto nRo = sh.add(".rodata");
    auto nData = sh.add(".data");
    auto nRelaT = sh.add(".rela.text");
    auto nRelaR = sh.add(".rela.rodata");
    auto nRelaD = sh.add(".rela.data");
    auto nSym = sh.add(".symtab");
    auto nStr = sh.add(".strtab");
    auto nSh = sh.add(".shstrtab");
    auto align8 = [](std::size_t n) { return (n + 7) & ~std::size_t{7}; };
    std::size_t off = 64;
    auto place = [&](std::size_t n) {
        off = align8(off);
        std::size_t at = off;
        off += n;
        return at;
    };
    std::size_t oText = place(text.size());
    std::size_t oRo = place(ro.size());
    std::size_t oData = place(data.size());
    std::size_t oRelaT = place(relaText.size());
    std::size_t oRelaR = place(relaRo.size());
    std::size_t oRelaD = place(relaData.size());
    std::size_t oSym = place(syms.size());
    std::size_t oStr = place(strs.bytes.size());
    std::size_t oSh = place(sh.bytes.size());
    off = align8(off);
    std::size_t oShdr = off;
    std::vector<unsigned char> out(oShdr + 10 * 64, 0);
    out[0] = 0x7f;
    out[1] = 'E';
    out[2] = 'L';
    out[3] = 'F';
    out[4] = 2;
    out[5] = 1;
    out[6] = 1;
    auto poke = [&](std::size_t at, std::uint64_t v, int n) {
        for (int i = 0; i < n; ++i) out[at + static_cast<std::size_t>(i)] = static_cast<unsigned char>((v >> (8 * i)) & 0xffu);
    };
    poke(16, 1, 2);
    poke(18, 62, 2);
    poke(20, 1, 4);
    poke(40, oShdr, 8);
    poke(52, 64, 2);
    poke(58, 64, 2);
    poke(60, 10, 2);
    poke(62, 9, 2);
    auto copyAt = [&](std::size_t at, const std::vector<unsigned char>& b) {
        for (std::size_t i = 0; i < b.size(); ++i) out[at + i] = b[i];
    };
    auto copyStr = [&](std::size_t at, const std::string& s) {
        for (std::size_t i = 0; i < s.size(); ++i) out[at + i] = static_cast<unsigned char>(s[i]);
    };
    copyAt(oText, text);
    copyAt(oRo, ro);
    copyAt(oData, data);
    copyAt(oRelaT, relaText);
    copyAt(oRelaR, relaRo);
    copyAt(oRelaD, relaData);
    copyAt(oSym, syms);
    copyStr(oStr, strs.bytes);
    copyStr(oSh, sh.bytes);
    std::vector<unsigned char> headers;
    shdr(headers, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    shdr(headers, nText, 1, 6, oText, text.size(), 0, 0, 16, 0);
    shdr(headers, nRo, 1, 2, oRo, ro.size(), 0, 0, 8, 0);
    shdr(headers, nData, 1, 3, oData, data.size(), 0, 0, 8, 0);
    shdr(headers, nRelaT, 4, 0, oRelaT, relaText.size(), 7, 1, 8, 24);
    shdr(headers, nRelaR, 4, 0, oRelaR, relaRo.size(), 7, 2, 8, 24);
    shdr(headers, nRelaD, 4, 0, oRelaD, relaData.size(), 7, 3, 8, 24);
    shdr(headers, nSym, 2, 0, oSym, syms.size(), 8, firstGlobal, 8, 24);
    shdr(headers, nStr, 3, 0, oStr, strs.bytes.size(), 0, 0, 1, 0);
    shdr(headers, nSh, 3, 0, oSh, sh.bytes.size(), 0, 0, 1, 0);
    for (std::size_t i = 0; i < headers.size(); ++i) out[oShdr + i] = headers[i];
    return out;
}

void put(std::vector<unsigned char>& o, std::size_t at, std::uint64_t v, int n) {
    for (int i = 0; i < n; ++i) o[at + static_cast<std::size_t>(i)] = static_cast<unsigned char>((v >> (8 * i)) & 0xffu);
}

std::vector<unsigned char> wrapElf(const std::vector<unsigned char>& payload, std::size_t entryOff) {
    std::uint64_t filesz = kLoadOff + payload.size();
    std::uint64_t entry = kBase + kLoadOff + entryOff;
    std::vector<unsigned char> o(kLoadOff + payload.size(), 0);
    o[0] = 0x7f;
    o[1] = 'E';
    o[2] = 'L';
    o[3] = 'F';
    o[4] = 2;
    o[5] = 1;
    o[6] = 1;
    put(o, 16, 2, 2);
    put(o, 18, 62, 2);
    put(o, 20, 1, 4);
    put(o, 24, entry, 8);
    put(o, 32, kHeader, 8);
    put(o, 52, 64, 2);
    put(o, 54, 56, 2);
    put(o, 56, 1, 2);
    put(o, 64, 1, 4);
    put(o, 68, 7, 4);
    put(o, 72, 0, 8);
    put(o, 80, kBase, 8);
    put(o, 88, kBase, 8);
    put(o, 96, filesz, 8);
    put(o, 104, filesz, 8);
    put(o, 112, 0x1000, 8);
    for (std::size_t i = 0; i < payload.size(); ++i) o[kLoadOff + i] = payload[i];
    return o;
}

}  // namespace

std::vector<unsigned char> compileToObject(const std::string& source, Diagnostics& diags, const std::string& file,
                                      const std::vector<std::string>& includes) {
    Module m = lowerSource(source, diags, file, includes);
    if (!diags.ok()) return {};
    Image im;
    for (const Function& f : m.funcs) emitFunction(im, f);
    im.textEnd = im.bytes.size();
    emitData(im, m);
    if (!patchLocal(im)) {
        diags.error(file, 1, 1, im.why);
        return {};
    }
    return writeObject(im);
}

std::vector<unsigned char> compileToElf(const std::string& source, Diagnostics& diags, const std::string& file,
                                   const std::vector<std::string>& includes) {
    Module m = lowerSource(source, diags, file, includes);
    if (!diags.ok()) return {};
    Image im;
    for (const Function& f : m.funcs) emitFunction(im, f);
    if (!im.labels.count("main")) {
        diags.error(file, 1, 1, "missing main");
        return {};
    }
    std::size_t entry = im.bytes.size();
    im.labels["_start"] = entry;
    callRel(im, "main");
    movReg(im, Rdi, Rax);
    movImm(im, Rax, 60);
    u8(im, 0x0F);
    u8(im, 0x05);
    emitData(im, m);
    if (!patch(im)) {
        diags.error(file, 1, 1, im.why);
        return {};
    }
    return wrapElf(im.bytes, entry);
}

}  // namespace okaycc
