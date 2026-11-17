#include "ir.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

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
    case Inst::Op::LeaFrame: return "leaframe";
    case Inst::Op::Load: return "load";
    case Inst::Op::Store: return "store";
    case Inst::Op::GLoad: return "gload";
    case Inst::Op::GStore: return "gstore";
    }
    return "?";
}

bool pure(Inst::Op op) {
    switch (op) {
    case Inst::Op::Mov:
    case Inst::Op::Add:
    case Inst::Op::Sub:
    case Inst::Op::Mul:
    case Inst::Op::And:
    case Inst::Op::Or:
    case Inst::Op::Xor:
    case Inst::Op::Shl:
    case Inst::Op::Shr:
    case Inst::Op::Neg:
    case Inst::Op::Not:
    case Inst::Op::Ceq:
    case Inst::Op::Cne:
    case Inst::Op::Clt:
    case Inst::Op::Cle:
    case Inst::Op::Cgt:
    case Inst::Op::Cge:
    case Inst::Op::Lea:
    case Inst::Op::LeaSym:
    case Inst::Op::LeaFrame:
    case Inst::Op::Local:
        return true;
    default:
        return false;
    }
}

std::int64_t fitWidth(std::int64_t v, int width, bool uns) {
    if (width >= 8 || width <= 0) return v;
    std::uint64_t mask = width >= 4 ? 0xffffffffull : width == 2 ? 0xffffull : 0xffull;
    std::uint64_t bits = static_cast<std::uint64_t>(v) & mask;
    if (!uns && (bits & (1ull << (width * 8 - 1)))) bits |= ~mask;
    return static_cast<std::int64_t>(bits);
}

bool asImm(const Operand& o, std::int64_t& v) {
    if (o.kind != Operand::Kind::Imm) return false;
    v = o.imm;
    return true;
}

bool cmpFold(Inst::Op op, std::uint64_t x, std::uint64_t y, bool uns, bool& out) {
    std::int64_t sx = static_cast<std::int64_t>(x);
    std::int64_t sy = static_cast<std::int64_t>(y);
    switch (op) {
    case Inst::Op::Ceq: out = x == y; return true;
    case Inst::Op::Cne: out = x != y; return true;
    case Inst::Op::Clt: out = uns ? x < y : sx < sy; return true;
    case Inst::Op::Cle: out = uns ? x <= y : sx <= sy; return true;
    case Inst::Op::Cgt: out = uns ? x > y : sx > sy; return true;
    case Inst::Op::Cge: out = uns ? x >= y : sx >= sy; return true;
    default: return false;
    }
}

bool foldInst(const Inst& in, std::int64_t& out) {
    std::int64_t x = 0;
    std::int64_t y = 0;
    if (in.op == Inst::Op::Mov && asImm(in.a, x)) {
        out = fitWidth(x, in.extra, in.uns);
        return true;
    }
    if (in.op == Inst::Op::Neg && asImm(in.a, x)) {
        out = static_cast<std::int64_t>(0 - static_cast<std::uint64_t>(x));
        return true;
    }
    if (in.op == Inst::Op::Not && asImm(in.a, x)) {
        out = static_cast<std::int64_t>(~static_cast<std::uint64_t>(x));
        return true;
    }
    if (!asImm(in.a, x) || !asImm(in.b, y)) return false;
    switch (in.op) {
    case Inst::Op::Add:
        out = static_cast<std::int64_t>(static_cast<std::uint64_t>(x) + static_cast<std::uint64_t>(y));
        return true;
    case Inst::Op::Sub:
        out = static_cast<std::int64_t>(static_cast<std::uint64_t>(x) - static_cast<std::uint64_t>(y));
        return true;
    case Inst::Op::Mul:
        out = static_cast<std::int64_t>(static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(y));
        return true;
    case Inst::Op::And: out = x & y; return true;
    case Inst::Op::Or: out = x | y; return true;
    case Inst::Op::Xor: out = x ^ y; return true;
    case Inst::Op::Shl:
    case Inst::Op::Shr:
        if (y < 0 || y >= 63) return false;
        {
            auto ux = static_cast<std::uint64_t>(x);
            unsigned s = static_cast<unsigned>(y);
            out = static_cast<std::int64_t>(in.op == Inst::Op::Shl ? (ux << s) : (ux >> s));
        }
        return true;
    case Inst::Op::Div:
    case Inst::Op::Mod:
        if (y == 0 || (x == INT64_MIN && y == -1)) return false;
        out = in.op == Inst::Op::Div ? x / y : x % y;
        return true;
    default:
        break;
    }
    bool bit = false;
    if (!cmpFold(in.op, static_cast<std::uint64_t>(x), static_cast<std::uint64_t>(y), in.uns, bit)) return false;
    out = bit ? 1 : 0;
    return true;
}

void rewrite(Operand& o, const std::map<int, std::int64_t>& known) {
    if (o.kind != Operand::Kind::Reg) return;
    auto it = known.find(o.reg);
    if (it == known.end()) return;
    o = Operand::number(it->second);
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

void optimizeModule(Module& m) {
    for (Function& f : m.funcs) {
        for (Block& b : f.blocks) {
            std::map<int, std::int64_t> known;
            for (Inst& in : b.code) {
                rewrite(in.a, known);
                rewrite(in.b, known);
                for (Operand& arg : in.args) rewrite(arg, known);
                for (Operand& word : in.stack) rewrite(word, known);
                std::int64_t folded = 0;
                if (in.dst >= 0 && foldInst(in, folded)) {
                    in.op = Inst::Op::Mov;
                    in.a = Operand::number(folded);
                    in.b = Operand::none();
                    in.extra = 8;
                    in.uns = false;
                    in.args.clear();
                    in.stack.clear();
                    in.func.clear();
                    known[in.dst] = folded;
                } else if (in.dst >= 0) {
                    known.erase(in.dst);
                }
            }
        }
        std::map<int, int> uses;
        auto add = [&](const Operand& o) {
            if (o.kind == Operand::Kind::Reg) uses[o.reg] += 1;
        };
        for (const Block& b : f.blocks) {
            for (const Inst& in : b.code) {
                add(in.a);
                add(in.b);
                for (const Operand& arg : in.args) add(arg);
                for (const Operand& word : in.stack) add(word);
            }
        }
        bool again = true;
        while (again) {
            again = false;
            for (Block& b : f.blocks) {
                for (int i = static_cast<int>(b.code.size()) - 1; i >= 0; --i) {
                    Inst& in = b.code[static_cast<std::size_t>(i)];
                    if (!pure(in.op) || in.dst < 0 || uses[in.dst] != 0) continue;
                    auto drop = [&](const Operand& o) {
                        if (o.kind == Operand::Kind::Reg && uses[o.reg] > 0) uses[o.reg] -= 1;
                    };
                    drop(in.a);
                    drop(in.b);
                    for (const Operand& arg : in.args) drop(arg);
                    for (const Operand& word : in.stack) drop(word);
                    b.code.erase(b.code.begin() + i);
                    again = true;
                }
            }
        }
    }
}

RegHomes assignRegs(const Function& f) {
    std::vector<char> used;
    std::vector<char> mem;
    auto touch = [&](int r) {
        if (r < 0) return;
        if (r >= static_cast<int>(used.size())) used.resize(static_cast<std::size_t>(r + 1), 0);
        used[static_cast<std::size_t>(r)] = 1;
    };
    auto markMem = [&](int r) {
        touch(r);
        if (r >= static_cast<int>(mem.size())) mem.resize(static_cast<std::size_t>(r + 1), 0);
        mem[static_cast<std::size_t>(r)] = 1;
    };
    int n = f.params;
    if (n > 0) used.assign(static_cast<std::size_t>(n), 1);
    for (const Block& b : f.blocks) {
        for (const Inst& in : b.code) {
            touch(in.dst);
            if (in.a.kind == Operand::Kind::Reg) touch(in.a.reg);
            if (in.b.kind == Operand::Kind::Reg) touch(in.b.reg);
            for (const Operand& arg : in.args)
                if (arg.kind == Operand::Kind::Reg) touch(arg.reg);
            for (const Operand& word : in.stack)
                if (word.kind == Operand::Kind::Reg) touch(word.reg);
            if (in.op == Inst::Op::Lea && in.a.kind == Operand::Kind::Reg) markMem(in.a.reg);
        }
    }
    RegHomes h;
    h.home.assign(used.size(), -1);
    for (int i = 0; i < static_cast<int>(used.size()) && h.count < 3; ++i) {
        if (!used[static_cast<std::size_t>(i)]) continue;
        if (i < static_cast<int>(mem.size()) && mem[static_cast<std::size_t>(i)]) continue;
        h.home[static_cast<std::size_t>(i)] = h.count++;
    }
    return h;
}

std::string irText(const std::string& source, Diagnostics& diags, const std::string& file) {
    return printModule(lowerSource(source, diags, file));
}

}  // namespace okaycc
