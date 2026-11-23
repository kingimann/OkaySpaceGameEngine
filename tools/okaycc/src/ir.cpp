#include "ir.hpp"

#include <algorithm>
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
    case Inst::Op::Cvt: return "cvt";
    case Inst::Op::Sqrt: return "sqrt";
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
    case Inst::Op::Cvt:
    case Inst::Op::Sqrt:
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
    if (in.fp) return false;
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
                } else if (in.op == Inst::Op::Neg || in.op == Inst::Op::Not || in.op == Inst::Op::Sqrt) {
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

namespace {

struct Cell {
    int kind = 0;  // 0 unknown, 1 constant, 2 copy of src, 3 many values
    std::int64_t n = 0;
    int src = -1;
};

Cell joinCell(Cell a, Cell b) {
    if (a.kind == 0) return b;
    if (b.kind == 0) return a;
    if (a.kind == 3 || b.kind == 3) return Cell{3, 0, -1};
    if (a.kind == 1 && b.kind == 1 && a.n == b.n) return a;
    if (a.kind == 2 && b.kind == 2 && a.src == b.src) return a;
    return Cell{3, 0, -1};
}

void joinState(std::vector<Cell>& dst, const std::vector<Cell>& src) {
    if (dst.size() < src.size()) dst.resize(src.size());
    for (std::size_t i = 0; i < dst.size(); ++i) {
        Cell s = i < src.size() ? src[i] : Cell{};
        dst[i] = joinCell(dst[i], s);
    }
}

bool sameState(const std::vector<Cell>& a, const std::vector<Cell>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].kind != b[i].kind || a[i].n != b[i].n || a[i].src != b[i].src) return false;
    return true;
}

void defReg(std::vector<Cell>& st, int r, Cell now) {
    if (r < 0) return;
    if (r >= static_cast<int>(st.size())) st.resize(static_cast<std::size_t>(r + 1));
    for (Cell& c : st) {
        if (c.kind == 2 && c.src == r) c = Cell{3, 0, -1};
    }
    if (now.kind == 2 && now.src == r) now = Cell{3, 0, -1};
    st[static_cast<std::size_t>(r)] = now;
}

void transfer(const Inst& in, std::vector<Cell>& st) {
    if (in.dst < 0) return;
    if (in.fp) {
        defReg(st, in.dst, Cell{3, 0, -1});
        return;
    }
    if (in.op == Inst::Op::Mov && in.a.kind == Operand::Kind::Imm) {
        defReg(st, in.dst, Cell{1, in.a.imm, -1});
        return;
    }
    if (in.op == Inst::Op::Mov && in.a.kind == Operand::Kind::Reg) {
        Cell now{2, 0, in.a.reg};
        if (in.a.reg >= 0 && in.a.reg < static_cast<int>(st.size())) {
            const Cell& s = st[static_cast<std::size_t>(in.a.reg)];
            if (s.kind == 1) now = Cell{1, s.n, -1};
            else if (s.kind == 2 && s.src >= 0) now = Cell{2, 0, s.src};
        }
        defReg(st, in.dst, now);
        return;
    }
    defReg(st, in.dst, Cell{3, 0, -1});
}

void applyCell(Operand& o, const std::vector<Cell>& st, bool asValue) {
    if (o.kind != Operand::Kind::Reg || o.reg < 0 || o.reg >= static_cast<int>(st.size())) return;
    const Cell& c = st[static_cast<std::size_t>(o.reg)];
    if (asValue && c.kind == 1) {
        o = Operand::number(c.n);
        return;
    }
    if (c.kind == 2 && c.src >= 0 && c.src != o.reg) o.reg = c.src;
}

void rewriteUses(Inst& in, const std::vector<Cell>& st) {
    if (in.fp || in.op == Inst::Op::Lea) return;
    bool addrA = in.op == Inst::Op::Load || in.op == Inst::Op::Store ||
                 (in.op == Inst::Op::Call && in.func.empty());
    applyCell(in.a, st, !addrA);
    applyCell(in.b, st, true);
    for (Operand& arg : in.args) applyCell(arg, st, true);
    for (Operand& word : in.stack) applyCell(word, st, true);
    for (Operand& x : in.xmm) applyCell(x, st, true);
}

void propagateFunction(Function& f) {
    const int n = static_cast<int>(f.blocks.size());
    std::map<std::string, int> id;
    for (int i = 0; i < n; ++i) id[f.blocks[static_cast<std::size_t>(i)].name] = i;
    std::vector<std::vector<int>> preds(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const Block& b = f.blocks[static_cast<std::size_t>(i)];
        bool term = false;
        auto edge = [&](const std::string& name) {
            auto it = id.find(name);
            if (it != id.end()) preds[static_cast<std::size_t>(it->second)].push_back(i);
        };
        for (const Inst& in : b.code) {
            if (in.op == Inst::Op::Jmp) {
                edge(in.label);
                term = true;
                break;
            }
            if (in.op == Inst::Op::Br) {
                edge(in.label);
                edge(in.label2);
                term = true;
                break;
            }
            if (in.op == Inst::Op::Ret) {
                term = true;
                break;
            }
        }
        if (!term && i + 1 < n) preds[static_cast<std::size_t>(i + 1)].push_back(i);
    }

    std::vector<std::vector<Cell>> inS(static_cast<std::size_t>(n));
    std::vector<std::vector<Cell>> outS(static_cast<std::size_t>(n));
    std::vector<char> ready(static_cast<std::size_t>(n), 0);
    int guard = n * n + 4;
    bool changed = true;
    while (changed && guard-- > 0) {
        changed = false;
        for (int i = 0; i < n; ++i) {
            std::vector<Cell> joined;
            bool any = false;
            if (i == 0) {
                // Every parameter has a value on entry that is not known here: the integer ones
                // and those that arrive in xmm registers (a float parameter left "unknown" would
                // join with one branch's copy and be replaced by it).
                joined.assign(static_cast<std::size_t>(f.params), Cell{3, 0, -1});
                for (int s : f.xmmSlot) {
                    if (s < 0) continue;
                    if (s >= static_cast<int>(joined.size())) joined.resize(static_cast<std::size_t>(s + 1));
                    joined[static_cast<std::size_t>(s)] = Cell{3, 0, -1};
                }
                any = true;
            }
            for (int p : preds[static_cast<std::size_t>(i)]) {
                if (!ready[static_cast<std::size_t>(p)]) continue;
                if (!any) {
                    joined = outS[static_cast<std::size_t>(p)];
                    any = true;
                } else {
                    joinState(joined, outS[static_cast<std::size_t>(p)]);
                }
            }
            std::vector<Cell> st = joined;
            for (const Inst& in : f.blocks[static_cast<std::size_t>(i)].code) {
                transfer(in, st);
                if (in.op == Inst::Op::Jmp || in.op == Inst::Op::Br || in.op == Inst::Op::Ret) break;
            }
            if (!sameState(joined, inS[static_cast<std::size_t>(i)]) ||
                !sameState(st, outS[static_cast<std::size_t>(i)])) {
                inS[static_cast<std::size_t>(i)] = std::move(joined);
                outS[static_cast<std::size_t>(i)] = std::move(st);
                ready[static_cast<std::size_t>(i)] = 1;
                changed = true;
            } else {
                ready[static_cast<std::size_t>(i)] = 1;
            }
        }
    }

    for (int i = 0; i < n; ++i) {
        std::vector<Cell> st = inS[static_cast<std::size_t>(i)];
        for (Inst& in : f.blocks[static_cast<std::size_t>(i)].code) {
            rewriteUses(in, st);
            transfer(in, st);
            if (in.op == Inst::Op::Jmp || in.op == Inst::Op::Br || in.op == Inst::Op::Ret) break;
        }
    }
}

void foldFunction(Function& f) {
    for (Block& b : f.blocks) {
        std::map<int, std::int64_t> known;
        for (Inst& in : b.code) {
            if (in.op != Inst::Op::Lea) rewrite(in.a, known);
            rewrite(in.b, known);
            for (Operand& arg : in.args) rewrite(arg, known);
            for (Operand& word : in.stack) rewrite(word, known);
            for (Operand& x : in.xmm) rewrite(x, known);
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
}

}  // namespace

void optimizeModule(Module& m) {
    for (Function& f : m.funcs) {
        foldFunction(f);
        propagateFunction(f);
        foldFunction(f);
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
                for (const Operand& x : in.xmm) add(x);
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
                    for (const Operand& x : in.xmm) drop(x);
                    b.code.erase(b.code.begin() + i);
                    again = true;
                }
            }
        }
    }
}

namespace {

constexpr int kCalleeRegs = 5;
constexpr int kCallerRegs = 6;
constexpr int kPhysRegs = kCalleeRegs + kCallerRegs;

bool gAllocate = true;

void sizeHomes(RegHomes& h, const Function& f, int nregs) {
    h.home.assign(static_cast<std::size_t>(nregs), -1);
    h.spill.assign(f.blocks.size(), {});
    h.reload.assign(f.blocks.size(), {});
    for (std::size_t b = 0; b < f.blocks.size(); ++b) {
        std::size_t n = f.blocks[b].code.size();
        h.spill[b].assign(n, {});
        h.reload[b].assign(n, {});
    }
}

}  // namespace

void setAllocateRegisters(bool on) { gAllocate = on; }

RegHomes assignRegs(const Function& f) {
    std::vector<char> mem;
    auto markMem = [&](int r) {
        if (r < 0) return;
        if (r >= static_cast<int>(mem.size())) mem.resize(static_cast<std::size_t>(r + 1), 0);
        mem[static_cast<std::size_t>(r)] = 1;
    };
    int nregs = f.params;
    std::vector<int> calls;
    struct Site {
        int block;
        int inst;
        int index;
    };
    std::vector<Site> sites;
    auto note = [&](int r) {
        if (r >= nregs) nregs = r + 1;
    };
    int at = 0;
    for (int bi = 0; bi < static_cast<int>(f.blocks.size()); ++bi) {
        const Block& b = f.blocks[static_cast<std::size_t>(bi)];
        for (int ii = 0; ii < static_cast<int>(b.code.size()); ++ii, ++at) {
            const Inst& in = b.code[static_cast<std::size_t>(ii)];
            sites.push_back(Site{bi, ii, at});
            if (in.op == Inst::Op::Call) calls.push_back(at);
            note(in.dst);
            if (in.a.kind == Operand::Kind::Reg) note(in.a.reg);
            if (in.b.kind == Operand::Kind::Reg) note(in.b.reg);
            for (const Operand& arg : in.args)
                if (arg.kind == Operand::Kind::Reg) note(arg.reg);
            for (const Operand& word : in.stack)
                if (word.kind == Operand::Kind::Reg) note(word.reg);
            for (const Operand& x : in.xmm)
                if (x.kind == Operand::Kind::Reg) {
                    note(x.reg);
                    markMem(x.reg);  // an xmm argument is read from its stack slot (loadXmm)
                }
            if (in.op == Inst::Op::Lea && in.a.kind == Operand::Kind::Reg) markMem(in.a.reg);
            if (in.fp) {
                if (in.a.kind == Operand::Kind::Reg && in.extra != 0) markMem(in.a.reg);
                if (in.b.kind == Operand::Kind::Reg) markMem(in.b.reg);
                bool fpDst = in.op == Inst::Op::Add || in.op == Inst::Op::Sub || in.op == Inst::Op::Mul ||
                             in.op == Inst::Op::Div || in.op == Inst::Op::Mov || in.op == Inst::Op::Call ||
                             in.op == Inst::Op::Xor || in.op == Inst::Op::Sqrt;
                if (in.op == Inst::Op::Cvt && in.b.kind == Operand::Kind::Imm && (in.b.imm == 4 || in.b.imm == 8))
                    fpDst = true;
                if (fpDst) markMem(in.dst);
            }
        }
    }
    for (int s : f.xmmSlot) markMem(s);
    const int nb = static_cast<int>(f.blocks.size());
    std::map<std::string, int> id;
    std::vector<int> blockAt(static_cast<std::size_t>(nb), 0);
    std::vector<std::vector<int>> succs(static_cast<std::size_t>(nb));
    int cursor = 0;
    for (int bi = 0; bi < nb; ++bi) {
        blockAt[static_cast<std::size_t>(bi)] = cursor;
        const Block& b = f.blocks[static_cast<std::size_t>(bi)];
        id[b.name] = bi;
        cursor += static_cast<int>(b.code.size());
    }
    for (int bi = 0; bi < nb; ++bi) {
        const Block& b = f.blocks[static_cast<std::size_t>(bi)];
        bool term = false;
        auto edge = [&](const std::string& name) {
            auto it = id.find(name);
            if (it != id.end()) succs[static_cast<std::size_t>(bi)].push_back(it->second);
        };
        for (const Inst& in : b.code) {
            if (in.op == Inst::Op::Jmp) {
                edge(in.label);
                term = true;
                break;
            }
            if (in.op == Inst::Op::Br) {
                edge(in.label);
                edge(in.label2);
                term = true;
                break;
            }
            if (in.op == Inst::Op::Ret) {
                term = true;
                break;
            }
        }
        if (!term && bi + 1 < nb) succs[static_cast<std::size_t>(bi)].push_back(bi + 1);
    }
    std::vector<std::vector<char>> liveIn(static_cast<std::size_t>(nb), std::vector<char>(static_cast<std::size_t>(nregs), 0));
    std::vector<std::vector<char>> liveOut(static_cast<std::size_t>(nb), std::vector<char>(static_cast<std::size_t>(nregs), 0));
    auto useOp = [&](std::vector<char>& cur, const Operand& o) {
        if (o.kind == Operand::Kind::Reg && o.reg >= 0 && o.reg < nregs) cur[static_cast<std::size_t>(o.reg)] = 1;
    };
    auto applyUses = [&](std::vector<char>& cur, const Inst& in) {
        if (in.op != Inst::Op::Lea) useOp(cur, in.a);
        useOp(cur, in.b);
        for (const Operand& arg : in.args) useOp(cur, arg);
        for (const Operand& word : in.stack) useOp(cur, word);
        for (const Operand& x : in.xmm) useOp(cur, x);
    };
    int guard = nb * nb + 4;
    bool flow = true;
    while (flow && guard-- > 0) {
        flow = false;
        for (int bi = nb - 1; bi >= 0; --bi) {
            std::vector<char> out(static_cast<std::size_t>(nregs), 0);
            for (int s : succs[static_cast<std::size_t>(bi)]) {
                for (int v = 0; v < nregs; ++v)
                    if (liveIn[static_cast<std::size_t>(s)][static_cast<std::size_t>(v)])
                        out[static_cast<std::size_t>(v)] = 1;
            }
            const Block& b = f.blocks[static_cast<std::size_t>(bi)];
            int last = static_cast<int>(b.code.size()) - 1;
            for (int ii = 0; ii <= last; ++ii) {
                const Inst& in = b.code[static_cast<std::size_t>(ii)];
                if (in.op == Inst::Op::Jmp || in.op == Inst::Op::Br || in.op == Inst::Op::Ret) {
                    last = ii;
                    break;
                }
            }
            std::vector<char> cur = out;
            for (int ii = last; ii >= 0; --ii) {
                const Inst& in = b.code[static_cast<std::size_t>(ii)];
                if (in.dst >= 0 && in.dst < nregs) cur[static_cast<std::size_t>(in.dst)] = 0;
                applyUses(cur, in);
            }
            if (cur != liveIn[static_cast<std::size_t>(bi)] || out != liveOut[static_cast<std::size_t>(bi)]) {
                liveIn[static_cast<std::size_t>(bi)] = cur;
                liveOut[static_cast<std::size_t>(bi)] = out;
                flow = true;
            }
        }
    }
    std::vector<int> begin(static_cast<std::size_t>(nregs), -2);
    std::vector<int> end(static_cast<std::size_t>(nregs), -2);
    auto extend = [&](int r, int where) {
        if (r < 0 || r >= nregs) return;
        if (begin[static_cast<std::size_t>(r)] == -2 || where < begin[static_cast<std::size_t>(r)])
            begin[static_cast<std::size_t>(r)] = where;
        if (end[static_cast<std::size_t>(r)] == -2 || where > end[static_cast<std::size_t>(r)])
            end[static_cast<std::size_t>(r)] = where;
    };
    for (int bi = 0; bi < nb; ++bi) {
        const Block& b = f.blocks[static_cast<std::size_t>(bi)];
        int last = static_cast<int>(b.code.size()) - 1;
        for (int ii = 0; ii <= last; ++ii) {
            const Inst& in = b.code[static_cast<std::size_t>(ii)];
            if (in.op == Inst::Op::Jmp || in.op == Inst::Op::Br || in.op == Inst::Op::Ret) {
                last = ii;
                break;
            }
        }
        std::vector<char> cur = liveOut[static_cast<std::size_t>(bi)];
        for (int ii = last; ii >= 0; --ii) {
            const Inst& in = b.code[static_cast<std::size_t>(ii)];
            int where = blockAt[static_cast<std::size_t>(bi)] + ii;
            for (int v = 0; v < nregs; ++v)
                if (cur[static_cast<std::size_t>(v)]) extend(v, where);
            if (in.dst >= 0) extend(in.dst, where);
            if (in.dst >= 0 && in.dst < nregs) cur[static_cast<std::size_t>(in.dst)] = 0;
            applyUses(cur, in);
            for (int v = 0; v < nregs; ++v)
                if (cur[static_cast<std::size_t>(v)]) extend(v, where);
        }
    }
    RegHomes h;
    sizeHomes(h, f, nregs);
    if (!gAllocate) return h;

    struct Iv {
        int v;
        int b;
        int e;
    };
    std::vector<Iv> ivs;
    for (int v = 0; v < nregs; ++v) {
        if (v < static_cast<int>(mem.size()) && mem[static_cast<std::size_t>(v)]) continue;
        if (begin[static_cast<std::size_t>(v)] < -1) continue;
        ivs.push_back(Iv{v, begin[static_cast<std::size_t>(v)], end[static_cast<std::size_t>(v)]});
    }
    std::sort(ivs.begin(), ivs.end(), [](const Iv& a, const Iv& b) {
        if (a.b != b.b) return a.b < b.b;
        return a.v < b.v;
    });

    struct Act {
        int v;
        int e;
        int reg;
    };
    std::vector<Act> active;
    std::vector<int> freeCallee = {0, 1, 2, 3, 4};
    std::vector<int> freeCaller = {5, 6, 7, 8, 9, 10};
    auto take = [](std::vector<int>& bag) {
        if (bag.empty()) return -1;
        int best = 0;
        for (int i = 1; i < static_cast<int>(bag.size()); ++i)
            if (bag[static_cast<std::size_t>(i)] < bag[static_cast<std::size_t>(best)]) best = i;
        int r = bag[static_cast<std::size_t>(best)];
        bag.erase(bag.begin() + best);
        return r;
    };
    auto release = [&](int reg) {
        if (reg < kCalleeRegs) freeCallee.push_back(reg);
        else freeCaller.push_back(reg);
    };
    auto crosses = [&](const Iv& iv) {
        for (int c : calls)
            if (iv.b <= c && c < iv.e) return true;
        return false;
    };
    for (const Iv& iv : ivs) {
        std::vector<Act> keep;
        for (const Act& a : active) {
            if (a.e < iv.b) release(a.reg);
            else keep.push_back(a);
        }
        active.swap(keep);
        int reg = -1;
        bool call = crosses(iv);
        if (call) reg = take(freeCallee);
        if (reg < 0 && !call) reg = take(freeCaller);
        if (reg < 0) reg = take(freeCallee);
        if (reg < 0) reg = take(freeCaller);
        if (reg < 0 && !active.empty()) {
            int worst = 0;
            for (int i = 1; i < static_cast<int>(active.size()); ++i)
                if (active[static_cast<std::size_t>(i)].e > active[static_cast<std::size_t>(worst)].e) worst = i;
            if (active[static_cast<std::size_t>(worst)].e > iv.e) {
                reg = active[static_cast<std::size_t>(worst)].reg;
                h.home[static_cast<std::size_t>(active[static_cast<std::size_t>(worst)].v)] = -1;
                active.erase(active.begin() + worst);
            }
        }
        if (reg < 0) continue;
        h.home[static_cast<std::size_t>(iv.v)] = reg;
        active.push_back(Act{iv.v, iv.e, reg});
    }

    std::vector<char> usedPhys(static_cast<std::size_t>(kPhysRegs), 0);
    for (int home : h.home) {
        if (home >= 0 && home < kCalleeRegs) usedPhys[static_cast<std::size_t>(home)] = 1;
    }
    for (int i = 0; i < kCalleeRegs; ++i)
        if (usedPhys[static_cast<std::size_t>(i)]) h.saved.push_back(i);

    for (const Site& site : sites) {
        if (std::find(calls.begin(), calls.end(), site.index) == calls.end()) continue;
        const Inst& in = f.blocks[static_cast<std::size_t>(site.block)].code[static_cast<std::size_t>(site.inst)];
        for (int v = 0; v < nregs; ++v) {
            int home = h.home[static_cast<std::size_t>(v)];
            if (home < kCalleeRegs) continue;
            int b0 = begin[static_cast<std::size_t>(v)];
            int e0 = end[static_cast<std::size_t>(v)];
            bool redefined = v == in.dst || (in.b.kind == Operand::Kind::Reg && v == in.b.reg);
            if (!(b0 <= site.index && site.index <= e0)) continue;
            if (redefined && b0 == site.index) continue;
            h.spill[static_cast<std::size_t>(site.block)][static_cast<std::size_t>(site.inst)].push_back(v);
            if (!redefined && e0 > site.index)
                h.reload[static_cast<std::size_t>(site.block)][static_cast<std::size_t>(site.inst)].push_back(v);
        }
    }
    return h;
}

std::string irText(const std::string& source, Diagnostics& diags, const std::string& file,
                   const std::vector<std::string>& includes) {
    return printModule(lowerSource(source, diags, file, includes));
}

}  // namespace okaycc
