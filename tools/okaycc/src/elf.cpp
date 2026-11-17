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

enum Reg { Rax = 0, Rcx = 1, Rdx = 2, Rsp = 4, Rbp = 5, Rsi = 6, Rdi = 7, R8 = 8, R9 = 9 };

const int kArg[] = {Rdi, Rsi, Rdx, Rcx, R8, R9};

enum class FixKind { Rel32, Abs64 };

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

void rel32(Image& im, const std::string& target) {
    Fix f;
    f.kind = FixKind::Rel32;
    f.at = im.bytes.size();
    f.target = target;
    im.fixes.push_back(f);
    u32(im, 0);
}

void jmpRel(Image& im, const std::string& target) {
    u8(im, 0xE9);
    rel32(im, target);
}

void jneRel(Image& im, const std::string& target) {
    u8(im, 0x0F);
    u8(im, 0x85);
    rel32(im, target);
}

void callRel(Image& im, const std::string& target) {
    u8(im, 0xE8);
    rel32(im, target);
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
    rexW(im, Rax, Rbp);
    u8(im, 0x89);
    u8(im, modrm(2, Rax, Rbp));
    i32(im, slotDisp(slot));
}

void loadRax(Image& im, const Operand& o) {
    if (o.kind == Operand::Kind::Imm) movImm(im, Rax, o.imm);
    else movRaxSlot(im, o.reg);
}

void loadRcx(Image& im, const Operand& o) {
    if (o.kind == Operand::Kind::Imm) movImm(im, Rcx, o.imm);
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
    rel32(im, target);
}

void movRaxRip(Image& im, const std::string& target) {
    rexW(im, Rax, Rbp);
    u8(im, 0x8B);
    u8(im, modrm(0, Rax, Rbp));
    rel32(im, target);
}

void movRipRax(Image& im, const std::string& target) {
    rexW(im, Rax, Rbp);
    u8(im, 0x89);
    u8(im, modrm(0, Rax, Rbp));
    rel32(im, target);
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
        rel32(im, target);
        return;
    }
    rexW(im, Rax, Rbp);
    if (!uns && width == 4) {
        u8(im, 0x63);
        u8(im, modrm(0, Rax, Rbp));
        rel32(im, target);
        return;
    }
    u8(im, 0x0F);
    u8(im, uns ? (width == 1 ? 0xB6 : 0xB7) : (width == 1 ? 0xBE : 0xBF));
    u8(im, modrm(0, Rax, Rbp));
    rel32(im, target);
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
    rel32(im, target);
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

void emitInst(Image& im, const Function& f, const Inst& in) {
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
    case Inst::Op::Call: {
        int nstack = in.args.size() > 6 ? static_cast<int>(in.args.size()) - 6 : 0;
        if (nstack % 2) aluImm(im, Rsp, 8, 5);
        for (int i = nstack - 1; i >= 0; --i) {
            loadRax(im, in.args[static_cast<std::size_t>(6 + i)]);
            pushReg(im, Rax);
        }
        int nreg = static_cast<int>(in.args.size()) < 6 ? static_cast<int>(in.args.size()) : 6;
        for (int i = 0; i < nreg; ++i) {
            loadRax(im, in.args[static_cast<std::size_t>(i)]);
            movReg(im, kArg[i], Rax);
        }
        if (in.func == "write") {
            movImm(im, Rax, 1);
            u8(im, 0x0F);
            u8(im, 0x05);
        } else {
            callRel(im, in.func);
        }
        if (nstack) {
            int bytes = 8 * (nstack + (nstack % 2));
            aluImm(im, Rsp, bytes, 0);
        }
        if (in.dst >= 0) movSlotRax(im, in.dst);
        break;
    }
    case Inst::Op::Ret:
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

void emitFunction(Image& im, const Function& f) {
    im.labels[f.name] = im.bytes.size();
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
    int used = cursor;
    if (used % 16) used += 8;
    pushReg(im, Rbp);
    movReg(im, Rbp, Rsp);
    if (used) aluImm(im, Rsp, used, 5);
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
    movReg(im, Rsp, Rbp);
    popReg(im, Rbp);
    u8(im, 0xC3);
}

void emitData(Image& im, const Module& m) {
    for (const Global& g : m.globals) {
        while (im.bytes.size() % 8) u8(im, 0);
        im.labels[g.name] = im.bytes.size();
        if (!g.reloc.empty()) {
            Fix f;
            f.kind = FixKind::Abs64;
            f.at = im.bytes.size();
            f.target = g.reloc;
            im.fixes.push_back(f);
            u64(im, 0);
            continue;
        }
        if (!g.bytes.empty()) {
            bool zero = true;
            for (unsigned char c : g.bytes)
                if (c) zero = false;
            if (!zero) {
                for (unsigned char c : g.bytes) u8(im, c);
                continue;
            }
        }
        int n = g.size > 0 ? g.size : static_cast<int>(g.bytes.size());
        for (int i = 0; i < n; ++i) u8(im, 0);
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
        if (f.kind == FixKind::Rel32) {
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

std::vector<unsigned char> compileToElf(const std::string& source, Diagnostics& diags, const std::string& file) {
    Module m = lowerSource(source, diags, file);
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
