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
    case Inst::Op::Cvt: return "cvt";
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
                joined.assign(static_cast<std::size_t>(f.params), Cell{3, 0, -1});
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
            rewrite(in.a, known);
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
            if (in.fp) {
                if (in.a.kind == Operand::Kind::Reg && in.extra != 0) markMem(in.a.reg);
                if (in.b.kind == Operand::Kind::Reg) markMem(in.b.reg);
                bool fpDst = in.op == Inst::Op::Add || in.op == Inst::Op::Sub || in.op == Inst::Op::Mul ||
                             in.op == Inst::Op::Div || in.op == Inst::Op::Mov || in.op == Inst::Op::Call ||
                             in.op == Inst::Op::Xor;
                if (in.op == Inst::Op::Cvt && (in.b.kind == Operand::Kind::Imm && (in.b.imm == 4 || in.b.imm == 8)))
                    fpDst = true;
                if (fpDst) markMem(in.dst);
            }
            for (const Operand& x : in.xmm)
                if (x.kind == Operand::Kind::Reg) markMem(x.reg);
        }
    }
    for (int s : f.xmmSlot) markMem(s);
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
