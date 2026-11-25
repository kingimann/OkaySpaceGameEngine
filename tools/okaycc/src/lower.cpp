// Lowering the checked tree to the IR (class Lowerer), and lowerSource.
// Moved out of parser.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "ast.hpp"

namespace okaycc {
namespace compiler {
class Lowerer {
public:
    explicit Lowerer(Diagnostics& diags) : diags_(diags) {}

    Module run(Tu& tu) {
        for (const auto& kv : canonStructs())
            if (kv.second) layoutOf(kv.second.get());
        for (auto& d : tu.decls)
            if (d && d->function) noteFunc(*d);
        for (auto& d : tu.decls)
            if (d && !d->function) addGlobal(*d);
        for (auto& d : tu.decls)
            if (d && d->function && d->body) lowerFunc(*d);
        return std::move(mod_);
    }

private:
    struct FieldOff {
        std::string name;
        int offset = 0;
        int width = 8;
        int step = 0;
        int inner = 0;
        int bitOff = 0;
        int bitWidth = 0;
        bool bitUns = false;
        bool agg = false;
        std::string tag;
        std::vector<FieldOff> fields;
    };
    struct Val {
        Operand op;
        Operand hi;
        int step = 0;
        int inner = 0;
        bool pair = false;
        int fpw = 0;
        std::vector<FieldOff> fields;
        Val() = default;
        Val(Operand o, int s, int i) : op(std::move(o)), step(s), inner(i) {}
    };
    struct Addr {
        Operand op;
        int width = 8;
        int step = 0;
        int inner = 0;
        int bitOff = 0;
        int bitWidth = 0;
        bool bitUns = false;
        bool ok = false;
        bool agg = false;
        std::vector<FieldOff> fields;
    };
    struct Sym {
        int reg = -1;
        bool global = false;
        bool array = false;
        bool agg = false;
        bool mem = false;
        bool zext = false;
        bool func = false;
        bool ref = false;
        bool boolean = false;
        int fpw = 0;
        std::string gname;
        int width = 8;
        int step = 0;
        int inner = 0;
        int objSize = 8;
        std::vector<FieldOff> fields;
    };
    struct Loop {
        std::string cont;
        std::string brk;
        bool sw = false;
    };

    Diagnostics& diags_;
    std::string file_;
    Module mod_;
    Function fn_;
    int cur_ = -1;
    int nextReg_ = 0;
    int whileN_ = 0;
    int ifN_ = 0;
    int forN_ = 0;
    int switchN_ = 0;
    int tmpN_ = 0;
    int strN_ = 0;
    int retWidth_ = 8;
    bool retBool_ = false;
    bool retFp_ = false;
    int vaSave_ = -1;
    bool retUns_ = false;
    bool retStruct_ = false;
    bool retRef_ = false;
    int hiddenReg_ = -1;
    std::set<std::string> addr_;
    std::vector<std::map<std::string, Sym>> scopes_;
    std::map<std::string, Sym> globals_;
    std::vector<Loop> loops_;

    void err(int line, int col, const std::string& msg) {
        diags_.error(file_.empty() ? "t.c" : file_, line, col, msg);
    }
    void startBlock(const std::string& name) {
        fn_.blocks.push_back(Block{});
        fn_.blocks.back().name = name;
        cur_ = static_cast<int>(fn_.blocks.size()) - 1;
    }
    void emit(Inst in) { fn_.blocks[static_cast<std::size_t>(cur_)].code.push_back(std::move(in)); }
    int fresh() { return nextReg_++; }
    std::string lab(const std::string& prefix) { return prefix + std::to_string(tmpN_++); }

    static int alignUp(int off, int al) {
        if (al <= 1) return off;
        int m = off % al;
        return m ? off + (al - m) : off;
    }

    static int alignOf(const Type* t) {
        if (!t) return 1;
        switch (t->kind) {
        case Type::Kind::Char: return 1;
        case Type::Kind::Short: return 2;
        case Type::Kind::Int:
        case Type::Kind::Float: return 4;
        case Type::Kind::Double:
        case Type::Kind::Long:
        case Type::Kind::Pointer:
        case Type::Kind::Reference: return 8;
        case Type::Kind::Bool: return 1;
        case Type::Kind::Array: return alignOf(t->inner.get());
        case Type::Kind::Struct:
        case Type::Kind::Union: {
            int a = 1;
            t = canonOf(t);  // a use made before the class was complete has no fields of its own
            for (const Param& p : t->params) {
                int fa = alignOf(p.type.get());
                if (fa > a) a = fa;
            }
            return a;
        }
        default: return 1;
        }
    }

    static int sizeOf(const Type* t) {
        if (!t) return 4;
        switch (t->kind) {
        case Type::Kind::Char:
        case Type::Kind::Bool: return 1;
        case Type::Kind::Short: return 2;
        case Type::Kind::Int:
        case Type::Kind::Float: return 4;
        case Type::Kind::Double:
        case Type::Kind::Long:
        case Type::Kind::Pointer:
        case Type::Kind::Reference: return 8;
        case Type::Kind::Array: {
            int n = 0;
            if (t->bound && t->bound->kind == Expr::Kind::Int) n = static_cast<int>(t->bound->ival);
            return n * sizeOf(t->inner.get());
        }
        case Type::Kind::Struct: {
            auto fields = layoutOf(t);
            int end = 0;
            for (const FieldOff& f : fields) {
                int span = f.bitWidth > 0 ? 4 : f.width;
                int e = f.offset + span;
                if (e > end) end = e;
            }
            if (end == 0) return t->tag.empty() ? 0 : 1;
            return alignUp(end, alignOf(t));
        }
        case Type::Kind::Union: {
            int sz = 0;
            for (const Param& p : t->params) {
                int n = sizeOf(p.type.get());
                if (n > sz) sz = n;
            }
            if (sz == 0) return 0;
            return alignUp(sz, alignOf(t));
        }
        default: return 4;
        }
    }
    static int valueStep(const Type* t) {
        if (!t) return 0;
        if (t->kind == Type::Kind::Pointer || t->kind == Type::Kind::Array) return sizeOf(t->inner.get());
        return 0;
    }
    static int innerOf(const Type* t) {
        if (!t) return 0;
        if (t->kind == Type::Kind::Pointer || t->kind == Type::Kind::Array) return valueStep(t->inner.get());
        return 0;
    }

    static std::vector<FieldOff> layoutOf(const Type* t) {
        std::vector<FieldOff> out;
        if (!t || (t->kind != Type::Kind::Struct && t->kind != Type::Kind::Union)) return out;
        // A use made before the class was complete has no fields of its own; take the finished
        // type, unless this tag is being laid out right now (a pointer to itself).
        static std::set<std::string> inProgress;
        struct Mark {
            std::string tag;
            bool set = false;
            ~Mark() {
                if (set) inProgress.erase(tag);
            }
        } mark;
        if (t->params.empty() && !t->tag.empty() && !inProgress.count(t->tag)) t = canonOf(t);
        if (!t->tag.empty() && inProgress.insert(t->tag).second) {
            mark.tag = t->tag;
            mark.set = true;
        }
        if (t->kind == Type::Kind::Union) {
            for (const Param& p : t->params) {
                FieldOff f;
                f.name = p.name;
                f.offset = 0;
                f.width = sizeOf(p.type.get());
                f.step = valueStep(p.type.get());
                f.inner = innerOf(p.type.get());
                f.agg = p.type && (p.type->kind == Type::Kind::Struct || p.type->kind == Type::Kind::Union ||
                                   p.type->kind == Type::Kind::Array);
                if (p.type && (p.type->kind == Type::Kind::Struct || p.type->kind == Type::Kind::Union))
                    f.fields = layoutOf(p.type.get());
                out.push_back(std::move(f));
            }
            return out;
        }
        int off = 0;
        int bitCursor = -1;
        for (const Param& p : t->params) {
            if (!p.mangled.empty() || (p.type && p.type->kind == Type::Kind::Function)) continue;
            if (p.name == "__basepad") {
                // Itanium C++ ABI 2.4: a base that is POD for layout keeps its tail padding.
                if (bitCursor >= 0) {
                    off += 4;
                    bitCursor = -1;
                }
                off = alignUp(off, alignOf(p.type.get()));
                continue;
            }
            FieldOff f;
            f.name = p.name;
            if (p.bits >= 0) {
                int bits = p.bits;
                if (bits > 32) bits = 32;
                if (bitCursor < 0 || bitCursor + bits > 32) {
                    if (bitCursor >= 0) off += 4;
                    off = alignUp(off, 4);
                    bitCursor = 0;
                }
                f.offset = off;
                f.width = 4;
                f.bitOff = bitCursor;
                f.bitWidth = bits;
                f.bitUns = p.type && p.type->unsign;
                bitCursor += bits;
                out.push_back(std::move(f));
                continue;
            }
            if (bitCursor >= 0) {
                off += 4;
                bitCursor = -1;
            }
            off = alignUp(off, alignOf(p.type.get()));
            f.offset = off;
            f.width = sizeOf(p.type.get());
            f.step = valueStep(p.type.get());
            f.inner = innerOf(p.type.get());
            bool nested = p.type && p.type->kind == Type::Kind::Struct;
            bool arr = p.type && p.type->kind == Type::Kind::Array;
            bool pstruct = p.type && p.type->kind == Type::Kind::Pointer && p.type->inner &&
                           p.type->inner->kind == Type::Kind::Struct;
            f.agg = nested || arr;
            if (nested) {
                f.fields = layoutOf(p.type.get());
                f.tag = p.type->tag;
            } else if (pstruct) {
                f.tag = p.type->inner->tag;
                if (p.type->inner->tag.empty() || p.type->inner->tag != t->tag)
                    f.fields = layoutOf(p.type->inner.get());
            }
            out.push_back(std::move(f));
            off += f.width;
        }
        if (bitCursor >= 0) off += 4;
        if (!t->tag.empty() && (!out.empty() || !structLayouts().count(t->tag))) structLayouts()[t->tag] = out;
        return out;
    }

    static std::map<std::string, std::vector<FieldOff>>& structLayouts() {
        static std::map<std::string, std::vector<FieldOff>> layouts;
        return layouts;
    }

    static std::vector<FieldOff> fieldsFor(const FieldOff& f) {
        if (!f.fields.empty() || f.tag.empty()) return f.fields;
        auto it = structLayouts().find(f.tag);
        if (it == structLayouts().end()) return f.fields;
        return it->second;
    }

    static void describe(Sym& s, const Type* t) {
        if (!t) return;
        if (t->kind == Type::Kind::Array) {
            s.array = true;
            s.width = sizeOf(t->inner.get());
        } else {
            s.width = sizeOf(t);
        }
        s.step = valueStep(t);
        s.inner = innerOf(t);
        s.objSize = sizeOf(t);
        s.zext = t->unsign || t->kind == Type::Kind::Bool;
        s.boolean = t->kind == Type::Kind::Bool;
        s.fpw = fpWidthOf(t);
        if (t->kind == Type::Kind::Reference && t->inner) {
            describe(s, t->inner.get());
            s.ref = true;
            return;
        }
        if (t->kind == Type::Kind::Array && t->inner) s.zext = t->inner->unsign;
        if (t->kind == Type::Kind::Struct || t->kind == Type::Kind::Union) {
            s.agg = true;
            s.fields = layoutOf(t);
            if (s.fields.empty() && !t->tag.empty()) {
                auto it = structLayouts().find(t->tag);
                if (it != structLayouts().end()) s.fields = it->second;
            }
        } else if (t->kind == Type::Kind::Pointer && t->inner &&
                   (t->inner->kind == Type::Kind::Struct || t->inner->kind == Type::Kind::Union)) {
            s.fields = layoutOf(t->inner.get());
            if (s.fields.empty() && !t->inner->tag.empty()) {
                auto it = structLayouts().find(t->inner->tag);
                if (it != structLayouts().end()) s.fields = it->second;
            }
        }
    }

    Sym* find(const std::string& name) {
        for (int i = static_cast<int>(scopes_.size()) - 1; i >= 0; --i) {
            auto it = scopes_[static_cast<std::size_t>(i)].find(name);
            if (it != scopes_[static_cast<std::size_t>(i)].end()) return &it->second;
        }
        auto g = globals_.find(name);
        if (g != globals_.end()) return &g->second;
        return nullptr;
    }

    void emitMov(int dst, Operand src, int width = 8, bool uns = false) {
        Inst in;
        in.op = Inst::Op::Mov;
        in.dst = dst;
        in.a = src;
        in.extra = width <= 0 ? 8 : width;
        in.uns = uns;
        emit(in);
    }
    Operand narrow(Operand src, int width, bool uns = false) {
        if (width >= 8 || width <= 0) return src;
        if (src.kind == Operand::Kind::Imm) {
            std::uint64_t mask = width >= 4 ? 0xffffffffull : width == 2 ? 0xffffull : 0xffull;
            std::uint64_t bits = static_cast<std::uint64_t>(src.imm) & mask;
            if (!uns && (bits & (1ull << (width * 8 - 1)))) bits |= ~mask;
            return Operand::number(static_cast<std::int64_t>(bits));
        }
        int t = fresh();
        emitMov(t, src, width, uns);
        return Operand::vreg(t);
    }
    void storeMem(int slot, Operand val, int width) {
        int addr = fresh();
        Inst lea;
        lea.op = Inst::Op::Lea;
        lea.dst = addr;
        lea.a = Operand::vreg(slot);
        emit(lea);
        Inst st;
        st.op = Inst::Op::Store;
        st.a = Operand::vreg(addr);
        st.b = val;
        st.extra = width;
        emit(st);
    }
    Val loadMem(int slot, int width, bool uns) {
        int addr = fresh();
        Inst lea;
        lea.op = Inst::Op::Lea;
        lea.dst = addr;
        lea.a = Operand::vreg(slot);
        emit(lea);
        int dst = fresh();
        Inst ld;
        ld.op = Inst::Op::Load;
        ld.dst = dst;
        ld.a = Operand::vreg(addr);
        ld.extra = width;
        ld.uns = uns;
        emit(ld);
        return Val{Operand::vreg(dst), 0, 0};
    }
    void emitJmp(const std::string& label) {
        Inst in;
        in.op = Inst::Op::Jmp;
        in.label = label;
        emit(in);
    }
    void emitBr(int cond, const std::string& t, const std::string& f) {
        Inst in;
        in.op = Inst::Op::Br;
        in.a = Operand::vreg(cond);
        in.label = t;
        in.label2 = f;
        emit(in);
    }
    Operand asBool(Operand src, int fpw = 0) {
        if (fpw) {
            std::uint64_t mask = fpw <= 4 ? 0x7fffffffull : 0x7fffffffffffffffll;
            if (src.kind == Operand::Kind::Imm) {
                std::uint64_t bits = static_cast<std::uint64_t>(src.imm) & mask;
                return Operand::number(bits != 0 ? 1 : 0);
            }
            int mag = fresh();
            Inst in;
            in.op = Inst::Op::And;
            in.dst = mag;
            in.a = src;
            in.b = Operand::number(static_cast<std::int64_t>(mask));
            emit(in);
            src = Operand::vreg(mag);
        }
        if (src.kind == Operand::Kind::Imm) return Operand::number(src.imm != 0 ? 1 : 0);
        int dst = fresh();
        emitBin(Inst::Op::Cne, dst, src, Operand::number(0));
        return Operand::vreg(dst);
    }

    void emitBin(Inst::Op op, int dst, Operand a, Operand b, bool uns = false) {
        Inst in;
        in.op = op;
        in.dst = dst;
        in.a = a;
        in.b = b;
        in.uns = uns;
        emit(in);
    }
    int asReg(Operand o) {
        if (o.kind == Operand::Kind::Reg) return o.reg;
        int t = fresh();
        emitMov(t, o);
        return t;
    }
    int material(Val v) { return asReg(v.op); }

    // An integer constant expression of a global initializer (ISO C 6.6, C++ [expr.const]):
    // literals, unary and binary operators and casts to an integer type.
    bool constInt(const Expr& e, std::int64_t& out) {
        switch (e.kind) {
        case Expr::Kind::Int:
        case Expr::Kind::Char:
            out = static_cast<std::int64_t>(e.ival);
            return true;
        case Expr::Kind::Unary: {
            std::int64_t v = 0;
            if (e.text == "sizeof" && e.ty) {
                out = sizeOf(e.ty.get());
                return out > 0;
            }
            if (e.kids.empty() || !e.kids[0] || !constInt(*e.kids[0], v)) return false;
            if (e.text == "-") out = static_cast<std::int64_t>(0 - static_cast<std::uint64_t>(v));
            else if (e.text == "+") out = v;
            else if (e.text == "~") out = ~v;
            else if (e.text == "!") out = v == 0;
            else return false;
            return true;
        }
        case Expr::Kind::Bin: {
            std::int64_t x = 0, y = 0;
            if (e.kids.size() != 2 || !e.kids[0] || !e.kids[1]) return false;
            if (!constInt(*e.kids[0], x) || !constInt(*e.kids[1], y)) return false;
            if (e.text == "&&") { out = x && y; return true; }
            if (e.text == "||") { out = x || y; return true; }
            return fold(e.text, x, y, out);
        }
        case Expr::Kind::Cond: {
            std::int64_t c = 0, a = 0, b = 0;
            if (e.kids.size() != 3 || !e.kids[0] || !e.kids[1] || !e.kids[2]) return false;
            if (!constInt(*e.kids[0], c) || !constInt(*e.kids[1], a) || !constInt(*e.kids[2], b)) return false;
            out = c ? a : b;
            return true;
        }
        case Expr::Kind::Cast: {
            std::int64_t v = 0;
            if (e.kids.empty() || !e.kids[0] || !e.ty || !isIntish(*e.ty) || !constInt(*e.kids[0], v)) return false;
            int w = sizeOf(e.ty.get());
            if (e.ty->kind == Type::Kind::Bool) v = v != 0;
            else if (w > 0 && w < 8) {
                std::uint64_t mask = (std::uint64_t{1} << (8 * w)) - 1;
                std::uint64_t u = static_cast<std::uint64_t>(v) & mask;
                if (!e.ty->unsign && (u >> (8 * w - 1)) & 1u) u |= ~mask;
                v = static_cast<std::int64_t>(u);
            }
            out = v;
            return true;
        }
        default:
            return false;
        }
    }
    // A floating constant: a literal (ival holds its float or double bits), an integer constant,
    // a negation or a cast of one.
    bool fpConst(const Expr& e, double& out) {
        if (e.kind == Expr::Kind::Float) {
            if (e.litRank == 1) {
                auto b32 = static_cast<std::uint32_t>(e.ival);
                float f = 0;
                std::memcpy(&f, &b32, 4);
                out = f;
            } else {
                std::uint64_t b64 = e.ival;
                std::memcpy(&out, &b64, 8);
            }
            return true;
        }
        if (e.kind == Expr::Kind::Unary && e.text == "-" && !e.kids.empty() && e.kids[0]) {
            if (!fpConst(*e.kids[0], out)) return false;
            out = -out;
            return true;
        }
        if (e.kind == Expr::Kind::Cast && !e.kids.empty() && e.kids[0] && e.ty && isFp(*e.ty)) {
            if (!fpConst(*e.kids[0], out)) return false;
            if (e.ty->kind == Type::Kind::Float) out = static_cast<float>(out);
            return true;
        }
        if (e.kind == Expr::Kind::Bin && e.kids.size() == 2 && e.kids[0] && e.kids[1] && e.checked && isFp(*e.checked) &&
            (e.text == "+" || e.text == "-" || e.text == "*" || e.text == "/")) {
            // A constant expression in float or double, rounded as the run-time operation would
            // be (IEEE 754 round to nearest in the operation's type).
            double a = 0, b = 0;
            if (!fpConst(*e.kids[0], a) || !fpConst(*e.kids[1], b)) return false;
            if (e.checked->kind == Type::Kind::Float) {
                float fa = static_cast<float>(a), fb = static_cast<float>(b);
                float r = e.text == "+" ? fa + fb : e.text == "-" ? fa - fb : e.text == "*" ? fa * fb : fa / fb;
                out = r;
            } else {
                out = e.text == "+" ? a + b : e.text == "-" ? a - b : e.text == "*" ? a * b : a / b;
            }
            return true;
        }
        std::int64_t v = 0;
        if (constInt(e, v)) {
            out = static_cast<double>(v);
            return true;
        }
        return false;
    }
    void foldGlobalInit(std::unique_ptr<Expr>& init) {
        if (!init) return;
        if (init->kind == Expr::Kind::Brace) {
            for (auto& k : init->kids) foldGlobalInit(k);
            return;
        }
        if (init->kind == Expr::Kind::Int || init->kind == Expr::Kind::Char) return;
        std::int64_t v = 0;
        if (!constInt(*init, v)) return;
        auto lit = std::make_unique<Expr>();
        lit->kind = Expr::Kind::Int;
        lit->ival = static_cast<std::uint64_t>(v);
        lit->line = init->line;
        lit->col = init->col;
        lit->file = init->file;
        init = std::move(lit);
    }

    bool fold(const std::string& op, std::int64_t x, std::int64_t y, std::int64_t& out) {
        if (op == "+") {
            out = static_cast<std::int64_t>(static_cast<std::uint64_t>(x) + static_cast<std::uint64_t>(y));
            return true;
        }
        if (op == "-") {
            out = static_cast<std::int64_t>(static_cast<std::uint64_t>(x) - static_cast<std::uint64_t>(y));
            return true;
        }
        if (op == "*") {
            out = static_cast<std::int64_t>(static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(y));
            return true;
        }
        if (op == "/" || op == "%") {
            if (y == 0) return false;
            if (x == std::numeric_limits<std::int64_t>::min() && y == -1) return false;
            out = op == "/" ? x / y : x % y;
            return true;
        }
        if (op == "&") { out = x & y; return true; }
        if (op == "|") { out = x | y; return true; }
        if (op == "^") { out = x ^ y; return true; }
        if (op == "<<" || op == ">>") {
            if (y < 0 || y >= 63) return false;
            auto ux = static_cast<std::uint64_t>(x);
            unsigned s = static_cast<unsigned>(y);
            out = static_cast<std::int64_t>(op == "<<" ? (ux << s) : (ux >> s));
            return true;
        }
        if (op == "==") { out = x == y; return true; }
        if (op == "!=") { out = x != y; return true; }
        if (op == "<") { out = x < y; return true; }
        if (op == "<=") { out = x <= y; return true; }
        if (op == ">") { out = x > y; return true; }
        if (op == ">=") { out = x >= y; return true; }
        return false;
    }

    Inst::Op binOp(const std::string& op) {
        if (op == "+") return Inst::Op::Add;
        if (op == "-") return Inst::Op::Sub;
        if (op == "*") return Inst::Op::Mul;
        if (op == "/") return Inst::Op::Div;
        if (op == "%") return Inst::Op::Mod;
        if (op == "&") return Inst::Op::And;
        if (op == "|") return Inst::Op::Or;
        if (op == "^") return Inst::Op::Xor;
        if (op == "<<") return Inst::Op::Shl;
        if (op == ">>") return Inst::Op::Shr;
        if (op == "==") return Inst::Op::Ceq;
        if (op == "!=") return Inst::Op::Cne;
        if (op == "<") return Inst::Op::Clt;
        if (op == "<=") return Inst::Op::Cle;
        if (op == ">") return Inst::Op::Cgt;
        if (op == ">=") return Inst::Op::Cge;
        return Inst::Op::Add;
    }

    std::string addString(const std::string& bytes) {
        Global g;
        g.name = ".Lstr" + std::to_string(strN_++);
        g.rodata = true;
        g.size = static_cast<int>(bytes.size()) + 1;
        g.bytes.assign(bytes.begin(), bytes.end());
        g.bytes.push_back(0);
        mod_.globals.push_back(std::move(g));
        return mod_.globals.back().name;
    }

    void putInt(std::vector<unsigned char>& b, std::int64_t v, int n) {
        auto u = static_cast<std::uint64_t>(v);
        if (n < 1) n = 1;
        if (n > 8) n = 8;
        for (int i = 0; i < n; ++i) b.push_back(static_cast<unsigned char>((u >> (8 * i)) & 0xffu));
    }

    void noteFunc(Decl& d) {
        if (d.name.empty()) return;
        Sym s;
        s.func = true;
        s.global = true;
        s.gname = d.asmName.empty() ? d.name : d.asmName;
        s.width = 8;
        globals_[d.name] = s;
    }

    void putAt(std::vector<unsigned char>& b, int off, std::int64_t v, int n) {
        if (n < 1) return;
        if (static_cast<int>(b.size()) < off + n) b.resize(static_cast<std::size_t>(off + n));
        auto u = static_cast<std::uint64_t>(v);
        for (int i = 0; i < n; ++i) b[static_cast<std::size_t>(off + i)] = static_cast<unsigned char>((u >> (8 * i)) & 0xffu);
    }
    void fillInit(const Type* t, const Expr* init, std::vector<unsigned char>& b, int off) {
        if (!t || !init) return;
        if (t->kind == Type::Kind::Array && init->kind == Expr::Kind::Brace && t->inner) {
            int es = sizeOf(t->inner.get());
            for (std::size_t i = 0; i < init->kids.size(); ++i)
                if (init->kids[i]) fillInit(t->inner.get(), init->kids[i].get(), b, off + static_cast<int>(i) * es);
            return;
        }
        if ((t->kind == Type::Kind::Struct || t->kind == Type::Kind::Union) && init->kind == Expr::Kind::Brace) {
            auto fields = layoutOf(t);
            for (std::size_t i = 0; i < init->kids.size() && i < fields.size() && i < t->params.size(); ++i)
                if (init->kids[i] && t->params[i].type)
                    fillInit(t->params[i].type.get(), init->kids[i].get(), b, off + fields[i].offset);
            return;
        }
        if (t->kind == Type::Kind::Array && t->inner && t->inner->kind == Type::Kind::Char &&
            init->kind == Expr::Kind::String) {
            for (std::size_t i = 0; i < init->text.size(); ++i)
                putAt(b, off + static_cast<int>(i), static_cast<unsigned char>(init->text[i]), 1);
            putAt(b, off + static_cast<int>(init->text.size()), 0, 1);
            return;
        }
        if (isFp(*t)) {
            // A float or double element: the IEEE 754 bits of the constant.
            double fv = 0;
            if (!fpConst(*init, fv)) {
                err(init->line, init->col, "global initializer is not supported");
                return;
            }
            if (t->kind == Type::Kind::Float) {
                float f = static_cast<float>(fv);
                std::uint32_t b32 = 0;
                std::memcpy(&b32, &f, 4);
                putAt(b, off, static_cast<std::int64_t>(b32), 4);
            } else {
                std::uint64_t b64 = 0;
                std::memcpy(&b64, &fv, 8);
                putAt(b, off, static_cast<std::int64_t>(b64), 8);
            }
            return;
        }
        std::int64_t val = 0;
        if (init->kind == Expr::Kind::Int || init->kind == Expr::Kind::Char) val = static_cast<std::int64_t>(init->ival);
        else if (init->kind == Expr::Kind::Unary && init->text == "-" && !init->kids.empty() && init->kids[0] &&
                 init->kids[0]->kind == Expr::Kind::Int)
            val = -static_cast<std::int64_t>(init->kids[0]->ival);
        else {
            err(init->line, init->col, "global initializer is not supported");
            return;
        }
        putAt(b, off, val, sizeOf(t));
    }
    void inferBound(Decl& d) {
        if (!d.type || d.type->kind != Type::Kind::Array || !d.type->unsized || !d.init || !d.type->inner) return;
        int n = 0;
        if (d.init->kind == Expr::Kind::Brace) n = static_cast<int>(d.init->kids.size());
        else if (d.init->kind == Expr::Kind::String && d.type->inner->kind == Type::Kind::Char)
            n = static_cast<int>(d.init->text.size()) + 1;
        if (n <= 0) return;
        auto b = std::make_unique<Expr>();
        b->kind = Expr::Kind::Int;
        b->ival = static_cast<std::uint64_t>(n);
        d.type->bound = std::move(b);
        d.type->unsized = false;
    }

    void addGlobal(Decl& d) {
        if (!d.type) return;
        if (d.isExtern) {
            Sym s;
            s.global = true;
            s.gname = d.asmName.empty() ? d.name : d.asmName;
            describe(s, d.type.get());
            globals_[d.name] = s;
            return;
        }
        inferBound(d);
        if (d.type->kind != Type::Kind::Float && d.type->kind != Type::Kind::Double) foldGlobalInit(d.init);
        Sym s;
        s.global = true;
        s.gname = d.asmName.empty() ? d.name : d.asmName;
        describe(s, d.type.get());
        Global g;
        g.name = s.gname;
        g.localSym = d.isStatic;
        g.weakSym = d.isInline && !d.isStatic;
        bool brace = d.init && d.init->kind == Expr::Kind::Brace;
        bool strArr = d.init && d.init->kind == Expr::Kind::String && d.type->kind == Type::Kind::Array;
        double fv = 0;
        if (!s.array && !s.agg && d.init && isFp(*d.type) && fpConst(*d.init, fv)) {
            // A float or double global with a constant initializer: the IEEE 754 bits.
            if (d.type->kind == Type::Kind::Float) {
                float f = static_cast<float>(fv);
                std::uint32_t b32 = 0;
                std::memcpy(&b32, &f, 4);
                g.size = 4;
                putInt(g.bytes, static_cast<std::int64_t>(b32), 4);
            } else {
                std::uint64_t b64 = 0;
                std::memcpy(&b64, &fv, 8);
                g.size = 8;
                putInt(g.bytes, static_cast<std::int64_t>(b64), 8);
            }
            mod_.globals.push_back(std::move(g));
            globals_[d.name] = s;
            return;
        }
        if ((s.array || s.agg) && (brace || strArr)) {
            g.size = sizeOf(d.type.get());
            if (g.size < 0) g.size = 0;
            g.bytes.assign(static_cast<std::size_t>(g.size), 0);
            fillInit(d.type.get(), d.init.get(), g.bytes, 0);
            if (static_cast<int>(g.bytes.size()) < g.size) g.bytes.resize(static_cast<std::size_t>(g.size));
        } else if (s.array || s.agg) {
            g.size = sizeOf(d.type.get());
            if (g.size <= 0) err(d.line, d.col, "array length is not a constant");
            if (d.init) err(d.line, d.col, "global initializer is not supported");
        } else if (d.init && d.init->kind == Expr::Kind::String && d.type->kind == Type::Kind::Pointer) {
            g.size = 8;
            g.reloc = addString(d.init->text);
        } else if (d.init && d.init->kind == Expr::Kind::Unary && d.init->text == "&" && !d.init->kids.empty() &&
                   d.init->kids[0] && d.init->kids[0]->kind == Expr::Kind::Name && find(d.init->kids[0]->text) &&
                   find(d.init->kids[0]->text)->global) {
            // T *p = &g; an address constant: 8 bytes the linker fills with g's address.
            g.size = 8;
            g.bytes.assign(8, 0);
            Sym* target = find(d.init->kids[0]->text);
            g.relocs.emplace_back(0, target->gname.empty() ? d.init->kids[0]->text : target->gname);
        } else if (d.init && (d.init->kind == Expr::Kind::Int || d.init->kind == Expr::Kind::Char)) {
            g.size = s.width > 0 && s.width <= 8 ? s.width : 8;
            std::int64_t n = static_cast<std::int64_t>(d.init->ival);
            if (d.type->kind == Type::Kind::Bool) n = n != 0;
            putInt(g.bytes, n, g.size);
        } else if (d.init && d.init->kind == Expr::Kind::Unary && d.init->text == "-" && !d.init->kids.empty() &&
                   d.init->kids[0] && d.init->kids[0]->kind == Expr::Kind::Int) {
            g.size = s.width > 0 && s.width <= 8 ? s.width : 8;
            putInt(g.bytes, -static_cast<std::int64_t>(d.init->kids[0]->ival), g.size);
        } else if (!d.init) {
            g.size = s.array ? sizeOf(d.type.get()) : (s.width < 8 ? s.width : 8);
        } else {
            err(d.line, d.col, "global initializer is not supported");
            g.size = 8;
        }
        if (!d.vt.empty()) {
            g.size = static_cast<int>(d.vt.size()) * 8;
            g.bytes.assign(static_cast<std::size_t>(g.size), 0);
            for (std::size_t k = 0; k < d.vt.size(); ++k)
                if (!d.vt[k].empty()) g.relocs.emplace_back(static_cast<int>(k) * 8, d.vt[k]);
            g.rodata = true;
            g.weakSym = d.isInline;
        }
        mod_.globals.push_back(std::move(g));
        globals_[d.name] = s;
    }

    Val readName(const std::string& name, int line, int col) {
        Sym* s = find(name);
        if (!s) {
            err(line, col, "not lowered: " + name);
            return {};
        }
        if (s->func) {
            int dst = fresh();
            Inst in;
            in.op = Inst::Op::LeaSym;
            in.dst = dst;
            in.func = s->gname;
            emit(in);
            return Val{Operand::vreg(dst), 0, 0};
        }
        if (s->ref && (s->agg || s->array)) {
            // A reference to an object or an array: its value is the object's address (as for a
            // local object; an array then decays to a pointer to its first element).
            Val v;
            v.op = Operand::vreg(s->reg);
            v.step = s->step;
            v.inner = s->inner;
            v.fields = s->fields;
            return v;
        }
        if (s->ref) {
            int dst = fresh();
            Inst ld;
            ld.op = Inst::Op::Load;
            ld.dst = dst;
            ld.a = Operand::vreg(s->reg);
            ld.extra = s->width > 0 ? s->width : 8;
            ld.uns = s->zext;
            emit(ld);
            Val v;
            v.op = Operand::vreg(dst);
            v.step = s->step;
            v.inner = s->inner;
            v.fields = s->fields;
            v.fpw = s->fpw;
            return v;
        }
        if (s->mem) {
            Val v = loadMem(s->reg, s->width, s->zext);
            v.fpw = s->fpw;
            v.step = s->step;  // a pointer kept in memory still points to something of a size
            v.inner = s->inner;
            v.fields = s->fields;
            return v;
        }
        if (s->array || s->agg) {
            if (s->global) {
                int dst = fresh();
                Inst in;
                in.op = Inst::Op::LeaSym;
                in.dst = dst;
                in.func = s->gname;
                emit(in);
                Val v;
                v.op = Operand::vreg(dst);
                v.step = s->step;
                v.inner = s->inner;
                v.fields = s->fields;
                return v;
            }
            Val v;
            v.op = Operand::vreg(s->reg);
            v.step = s->step;
            v.inner = s->inner;
            v.fields = s->fields;
            return v;
        }
        if (s->global) {
            int dst = fresh();
            Inst in;
            in.op = Inst::Op::GLoad;
            in.dst = dst;
            in.func = s->gname;
            in.extra = s->width;
            in.uns = s->zext;
            emit(in);
            Val v;
            v.op = Operand::vreg(dst);
            v.step = s->step;
            v.inner = s->inner;
            v.fields = s->fields;
            v.fpw = s->fpw;
            return v;
        }
        Val v;
        v.op = Operand::vreg(s->reg);
        v.step = s->step;
        v.inner = s->inner;
        v.fields = s->fields;
        v.fpw = s->fpw;
        return v;
    }

    Val scale(Val idx, int step) {
        if (step == 1) return idx;
        if (idx.op.kind == Operand::Kind::Imm) return Val{Operand::number(idx.op.imm * step), 0, 0};
        int dst = fresh();
        emitBin(Inst::Op::Mul, dst, idx.op, Operand::number(step));
        return Val{Operand::vreg(dst), 0, 0};
    }

    Addr lowerAddr(Expr& e) {
        Addr a;
        if (e.kind == Expr::Kind::String) {
            // A string literal is an lvalue array ([lex.string]): its address is where its bytes are
            // (a reference to it, as a forwarding reference given "text" binds, takes this).
            a.op = lowerExpr(e).op;
            a.width = 1;
            a.agg = true;
            a.ok = true;
            return a;
        }
        if (e.kind == Expr::Kind::Comma && !e.kids.empty() && e.kids.back()) {
            // (x, y): x is evaluated for its effects, the address is y's.
            for (std::size_t i = 0; i + 1 < e.kids.size(); ++i)
                if (e.kids[i]) lowerExpr(*e.kids[i]);
            return lowerAddr(*e.kids.back());
        }
        if (e.kind == Expr::Kind::Name) {
            Sym* s = find(e.text);
            if (!s) {
                err(e.line, e.col, "not lowered: " + e.text);
                return a;
            }
            if (s->ref) {
                a.op = Operand::vreg(s->reg);
                a.width = s->width > 0 ? s->width : 8;
                a.step = s->step;
                a.inner = s->inner;
                a.agg = s->agg;
                a.fields = s->fields;
                a.ok = true;
                return a;
            }
            if (s->mem) {
                int dst = fresh();
                Inst in;
                in.op = Inst::Op::Lea;
                in.dst = dst;
                in.a = Operand::vreg(s->reg);
                emit(in);
                a.op = Operand::vreg(dst);
                a.width = s->width;
                a.ok = true;
                return a;
            }
            if (s->agg) {
                Val base = readName(e.text, e.line, e.col);
                a.op = base.op;
                a.width = s->width;
                a.agg = true;
                a.fields = s->fields;
                a.ok = true;
                return a;
            }
            if (s->array) {
                Val base = readName(e.text, e.line, e.col);
                a.op = base.op;
                a.width = s->width;
                a.ok = true;
                return a;
            }
            if (s->global) {
                int dst = fresh();
                Inst in;
                in.op = Inst::Op::LeaSym;
                in.dst = dst;
                in.func = s->gname;
                emit(in);
                a.op = Operand::vreg(dst);
                a.width = s->width;
                a.ok = true;
                return a;
            }
            int dst = fresh();
            Inst in;
            in.op = Inst::Op::Lea;
            in.dst = dst;
            in.a = Operand::vreg(s->reg);
            emit(in);
            a.op = Operand::vreg(dst);
            a.width = s->width;
            a.ok = true;
            return a;
        }
        if (e.kind == Expr::Kind::Index && e.kids.size() >= 2 && e.kids[0] && e.kids[1]) {
            Val base = lowerExpr(*e.kids[0]);
            Val idx = lowerExpr(*e.kids[1]);
            if (base.step == 0) {
                err(e.line, e.col, "not lowered");
                return a;
            }
            Val off = scale(idx, base.step);
            int dst = fresh();
            emitBin(Inst::Op::Add, dst, base.op, off.op);
            a.op = Operand::vreg(dst);
            a.width = base.step;
            if (e.checked && (e.checked->kind == Type::Kind::Struct || e.checked->kind == Type::Kind::Union)) {
                a.fields = layoutOf(e.checked.get());
                a.agg = true;
            }
            a.ok = true;
            return a;
        }
        if (e.kind == Expr::Kind::Unary && e.text == "*" && !e.kids.empty() && e.kids[0]) {
            Val p = lowerExpr(*e.kids[0]);
            a.op = p.op;
            a.width = p.step == 0 ? 8 : p.step;
            a.fields = p.fields;
            if (a.fields.empty() && e.checked && (e.checked->kind == Type::Kind::Struct || e.checked->kind == Type::Kind::Union))
                a.fields = layoutOf(e.checked.get());
            a.agg = !a.fields.empty();
            a.ok = true;
            return a;
        }
        if (e.kind == Expr::Kind::Member && e.kids.size() >= 2 && e.kids[0] && e.kids[1]) {
            Operand baseOp;
            std::vector<FieldOff> fields;
            if (e.text == "->") {
                Val p = lowerExpr(*e.kids[0]);
                baseOp = p.op;
                fields = p.fields;
            } else {
                Addr b = lowerAddr(*e.kids[0]);
                if (!b.ok) return a;
                baseOp = b.op;
                fields = std::move(b.fields);
            }
            if (fields.empty() && e.kids[0]->checked) {
                // The object's fields from its checked type (a pointer read through a returned
                // reference carries no field list of its own).
                const Type* ot = e.kids[0]->checked.get();
                if (e.text == "->" && ot->kind == Type::Kind::Pointer && ot->inner) ot = ot->inner.get();
                if (ot->kind == Type::Kind::Struct || ot->kind == Type::Kind::Union) fields = layoutOf(ot);
            }
            const std::string& fname = e.kids[1]->text;
            const FieldOff* field = nullptr;
            for (const FieldOff& f : fields) {
                if (f.name == fname) field = &f;
            }
            if (!field) {
                err(e.line, e.col, "not lowered: " + fname);
                return a;
            }
            int dst = fresh();
            emitBin(Inst::Op::Add, dst, baseOp, Operand::number(field->offset));
            a.op = Operand::vreg(dst);
            a.width = field->width;
            a.step = field->step;
            a.inner = field->inner;
            a.bitOff = field->bitOff;
            a.bitWidth = field->bitWidth;
            a.bitUns = field->bitUns;
            a.agg = field->agg;
            a.fields = fieldsFor(*field);
            a.ok = true;
            return a;
        }
        err(e.line, e.col, "not lowered");
        return a;
    }

    Val loadAddr(Addr a, int inner, bool uns = false) {
        if (a.bitWidth > 0) {
            int word = fresh();
            Inst ld;
            ld.op = Inst::Op::Load;
            ld.dst = word;
            ld.a = a.op;
            ld.extra = a.width > 0 ? a.width : 4;
            ld.uns = true;
            emit(ld);
            int sh = fresh();
            emitBin(Inst::Op::Shr, sh, Operand::vreg(word), Operand::number(a.bitOff));
            unsigned mask = a.bitWidth >= 32 ? 0xffffffffu : ((1u << a.bitWidth) - 1u);
            int masked = fresh();
            emitBin(Inst::Op::And, masked, Operand::vreg(sh), Operand::number(static_cast<std::int64_t>(mask)));
            if (a.bitUns || a.bitWidth >= 32) return Val{Operand::vreg(masked), inner, 0};
            int sign = 1 << (a.bitWidth - 1);
            int flipped = fresh();
            emitBin(Inst::Op::Xor, flipped, Operand::vreg(masked), Operand::number(sign));
            int ext = fresh();
            emitBin(Inst::Op::Sub, ext, Operand::vreg(flipped), Operand::number(sign));
            return Val{Operand::vreg(ext), inner, 0};
        }
        int dst = fresh();
        Inst in;
        in.op = Inst::Op::Load;
        in.dst = dst;
        in.a = a.op;
        in.extra = a.width;
        in.uns = uns;
        emit(in);
        return Val{Operand::vreg(dst), inner, 0};
    }
    static int bitWidth(int sz) {
        if (sz >= 8) return 8;
        if (sz >= 4) return 4;
        if (sz >= 2) return 2;
        return 1;
    }
    Operand loadBits(Operand addr, int sz) {
        int dst = fresh();
        Inst ld;
        ld.op = Inst::Op::Load;
        ld.dst = dst;
        ld.a = addr;
        ld.extra = bitWidth(sz);
        ld.uns = true;
        emit(ld);
        return Operand::vreg(dst);
    }
    void storeBits(Operand addr, Operand bits, int sz) {
        Inst st;
        st.op = Inst::Op::Store;
        st.a = addr;
        st.b = bits;
        st.extra = bitWidth(sz);
        emit(st);
    }
    Operand addAddr(Operand base, int off) {
        if (off == 0) return base;
        int dst = fresh();
        emitBin(Inst::Op::Add, dst, base, Operand::number(off));
        return Operand::vreg(dst);
    }
    void zeroMem(Operand dst, int sz) {
        int off = 0;
        while (off < sz) {
            int n = sz - off >= 8 ? 8 : 1;
            storeBits(addAddr(dst, off), Operand::number(0), n);
            off += n;
        }
    }
    // A brace initializer of a local array or struct; a nested brace initializes a nested
    // array or struct (ISO C 6.7.9).
    void initLocalBrace(Operand base, const Type* t, const Expr& init, int off) {
        if (!t) return;
        if (t->kind == Type::Kind::Array && t->inner) {
            int es = sizeOf(t->inner.get());
            for (std::size_t i = 0; i < init.kids.size(); ++i) {
                if (!init.kids[i]) continue;
                int at = off + static_cast<int>(i) * es;
                if (init.kids[i]->kind == Expr::Kind::Brace) {
                    initLocalBrace(base, t->inner.get(), *init.kids[i], at);
                    continue;
                }
                Val v = lowerExpr(*init.kids[i]);
                int w = es > 8 ? 8 : es;
                Operand bits = w < 8 ? narrow(v.op, w, t->inner->unsign) : v.op;
                storeBits(addAddr(base, at), bits, w);
            }
        } else if (t->kind == Type::Kind::Struct || t->kind == Type::Kind::Union) {
            auto fields = layoutOf(t);
            for (std::size_t i = 0; i < init.kids.size() && i < fields.size(); ++i) {
                if (!init.kids[i]) continue;
                if (init.kids[i]->kind == Expr::Kind::Brace) {
                    const Type* ft = i < t->params.size() ? t->params[i].type.get() : nullptr;
                    initLocalBrace(base, ft, *init.kids[i], off + fields[i].offset);
                    continue;
                }
                Val v = lowerExpr(*init.kids[i]);
                int w = fields[i].width > 8 ? 8 : fields[i].width;
                storeBits(addAddr(base, off + fields[i].offset), v.op, w);
            }
        }
    }
    void copyMem(Operand dst, Operand src, int sz) {
        int off = 0;
        while (off < sz) {
            int n = sz - off;
            if (n > 8) n = 8;
            Operand bits = loadBits(addAddr(src, off), n);
            storeBits(addAddr(dst, off), bits, n);
            off += n;
        }
    }
    Operand addrOf(const Sym& s) {
        if (!s.global) return Operand::vreg(s.reg);
        int dst = fresh();
        Inst lea;
        lea.op = Inst::Op::LeaSym;
        lea.dst = dst;
        lea.func = s.gname;
        emit(lea);
        return Operand::vreg(dst);
    }
    Operand frameAddr(int disp) {
        int dst = fresh();
        Inst in;
        in.op = Inst::Op::LeaFrame;
        in.dst = dst;
        in.a = Operand::number(disp);
        emit(in);
        return Operand::vreg(dst);
    }
    static int structPieces(const Type* t, int& sz, bool& memClass) {
        sz = t ? sizeOf(t) : 8;
        bool rec = t && (t->kind == Type::Kind::Struct || t->kind == Type::Kind::Union);
        memClass = rec && sz > 16;
        if (memClass) return 0;
        if (rec && sz > 8) return 2;
        return 1;
    }

    Val lowerLogic(Expr& e, bool isAnd) {
        int dst = fresh();
        std::string rhsL = lab(isAnd ? "and_rhs" : "or_rhs");
        std::string skL = lab(isAnd ? "and_false" : "or_true");
        std::string endL = lab(isAnd ? "and_end" : "or_end");
        int left = material(lowerExpr(*e.kids[0]));
        if (isAnd) emitBr(left, rhsL, skL);
        else emitBr(left, skL, rhsL);
        startBlock(rhsL);
        emitMov(dst, lowerExpr(*e.kids[1]).op);
        emitJmp(endL);
        startBlock(skL);
        emitMov(dst, Operand::number(isAnd ? 0 : 1));
        emitJmp(endL);
        startBlock(endL);
        return Val{Operand::vreg(dst), 0, 0};
    }

    Val lowerExpr(Expr& e) {
        if (!e.file.empty()) file_ = e.file;
        switch (e.kind) {
        case Expr::Kind::Int:
        case Expr::Kind::Char:
            return Val{Operand::number(static_cast<std::int64_t>(e.ival)), 0, 0};
        case Expr::Kind::Float: {
            Val v;
            v.op = Operand::number(static_cast<std::int64_t>(e.ival));
            v.fpw = e.litRank == 1 ? 4 : 8;
            return v;
        }
        case Expr::Kind::String: {
            std::string name = addString(e.text);
            int dst = fresh();
            Inst in;
            in.op = Inst::Op::LeaSym;
            in.dst = dst;
            in.func = name;
            emit(in);
            return Val{Operand::vreg(dst), 1, 0};
        }
        case Expr::Kind::Name:
            return readName(e.text, e.line, e.col);
        case Expr::Kind::Cast: {
            if (e.kids.empty() || !e.kids[0]) return {};
            Val v = lowerExpr(*e.kids[0]);
            if (!e.ty) return v;
            if (e.ty->kind == Type::Kind::Bool) return Val{asBool(v.op, v.fpw), 0, 0};
            if (e.ty->kind == Type::Kind::Pointer) {
                v.step = valueStep(e.ty.get());
                v.inner = innerOf(e.ty.get());
                v.fields.clear();
                if (e.ty->inner && e.ty->inner->kind == Type::Kind::Struct) v.fields = layoutOf(e.ty->inner.get());
                return v;
            }
            if (e.ty->kind == Type::Kind::Array) return v;
            int srcW = v.fpw ? v.fpw : fpWidthOf(e.kids[0]->checked.get());
            int dstFp = fpWidthOf(e.ty.get());
            if (srcW || dstFp) {
                if (dstFp) return cvtFp(v, srcW, dstFp);
                int iw = sizeOf(e.ty.get());
                if (iw < 4) {
                    Val n = cvtFp(v, srcW, -4);
                    return Val{narrow(n.op, iw, e.ty->unsign), 0, 0};
                }
                if (iw > 8) iw = 8;
                return cvtFp(v, srcW, -iw);
            }
            int w = sizeOf(e.ty.get());
            if (w >= 8 && !e.ty->unsign) return v;
            return Val{narrow(v.op, w, e.ty->unsign), 0, 0};
        }
        case Expr::Kind::Comma: {
            Val last{};
            for (auto& k : e.kids)
                if (k) last = lowerExpr(*k);
            return last;
        }
        case Expr::Kind::Unary:
            return lowerUnary(e);
        case Expr::Kind::Pre:
            return lowerInc(e, true);
        case Expr::Kind::Post:
            return lowerInc(e, false);
        case Expr::Kind::Bin:
            if (e.text == "&&" || e.text == "||") {
                if (e.kids.size() == 2 && e.kids[0] && e.kids[1] &&
                    (e.kids[0]->kind == Expr::Kind::Int || e.kids[0]->kind == Expr::Kind::Char) &&
                    (e.kids[1]->kind == Expr::Kind::Int || e.kids[1]->kind == Expr::Kind::Char)) {
                    std::int64_t folded = 0;
                    fold(e.text, static_cast<std::int64_t>(e.kids[0]->ival),
                         static_cast<std::int64_t>(e.kids[1]->ival), folded);
                    return Val{Operand::number(folded), 0, 0};
                }
                return lowerLogic(e, e.text == "&&");
            }
            return lowerBin(e);
        case Expr::Kind::Assign:
            return lowerAssign(e);
        case Expr::Kind::Cond:
            return lowerCond(e);
        case Expr::Kind::Call:
            return lowerCall(e);
        case Expr::Kind::Index: {
            Addr a = lowerAddr(e);
            if (!a.ok) return {};
            if (e.checked && (e.checked->kind == Type::Kind::Array || e.checked->kind == Type::Kind::Struct)) {
                Val v;
                v.op = a.op;
                v.step = valueStep(e.checked.get());
                v.inner = innerOf(e.checked.get());
                if (e.checked->kind == Type::Kind::Struct) v.fields = layoutOf(e.checked.get());
                return v;
            }
            int inner = 0;
            if (!e.kids.empty() && e.kids[0] && e.kids[0]->kind == Expr::Kind::Name) {
                if (Sym* s = find(e.kids[0]->text)) inner = s->inner;
            }
            Val loaded = loadAddr(a, inner, e.checked && e.checked->unsign);
            if (e.checked) loaded.fpw = fpWidthOf(e.checked.get());
            return loaded;
        }
        case Expr::Kind::Member: {
            Addr a = lowerAddr(e);
            if (!a.ok) return {};
            if (a.agg) {
                Val v;
                v.op = a.op;
                v.step = a.step;
                v.inner = a.inner;
                v.fields = a.fields;
                return v;
            }
            Val v = loadAddr(a, a.inner, e.checked && e.checked->unsign);
            v.step = a.step;
            v.inner = a.inner;
            v.fields = a.fields;
            if (e.checked) v.fpw = fpWidthOf(e.checked.get());  // a float or double field
            return v;
        }
        case Expr::Kind::Brace:
            err(e.line, e.col, "not lowered");
            return {};
        }
        err(e.line, e.col, "not lowered");
        return {};
    }

    std::vector<FieldOff> fieldsOf(Expr& e) {
        if (e.kind == Expr::Kind::Name) {
            Sym* s = find(e.text);
            if (s) return s->fields;
        } else if (e.kind == Expr::Kind::Member && e.kids.size() >= 2 && e.kids[0] && e.kids[1]) {
            std::vector<FieldOff> fields = fieldsOf(*e.kids[0]);
            for (const FieldOff& f : fields)
                if (f.name == e.kids[1]->text) return f.fields;
        }
        return {};
    }

    int sizeofValue(Expr& e) {
        switch (e.kind) {
        case Expr::Kind::Int:
        case Expr::Kind::Char:
            return 8;
        case Expr::Kind::String:
            return static_cast<int>(e.text.size()) + 1;
        case Expr::Kind::Name: {
            Sym* s = find(e.text);
            if (!s) {
                err(e.line, e.col, "not lowered: " + e.text);
                return 8;
            }
            return s->objSize;
        }
        case Expr::Kind::Unary:
            if (e.text == "*" && !e.kids.empty() && e.kids[0] && e.kids[0]->kind == Expr::Kind::Name) {
                Sym* s = find(e.kids[0]->text);
                if (s && s->step != 0) return s->step;
            }
            break;
        case Expr::Kind::Index:
            if (!e.kids.empty() && e.kids[0] && e.kids[0]->kind == Expr::Kind::Name) {
                Sym* s = find(e.kids[0]->text);
                if (s && s->array) return s->width;
                if (s && s->step != 0) return s->step;
            }
            break;
        case Expr::Kind::Member:
            if (e.kids.size() >= 2 && e.kids[0] && e.kids[1]) {
                std::vector<FieldOff> fields = fieldsOf(*e.kids[0]);
                for (const FieldOff& f : fields)
                    if (f.name == e.kids[1]->text) return f.width;
            }
            break;
        default:
            break;
        }
        err(e.line, e.col, "sizeof this expression is not allowed");
        return 8;
    }

    int sizeofOf(Expr& e) {
        if (e.ty) return sizeOf(e.ty.get());
        if (!e.kids.empty() && e.kids[0] && e.kids[0]->checked) return sizeOf(e.kids[0]->checked.get());
        if (e.kids.empty() || !e.kids[0]) {
            err(e.line, e.col, "sizeof this expression is not allowed");
            return 8;
        }
        return sizeofValue(*e.kids[0]);
    }

    Val lowerUnary(Expr& e) {
        if (e.text == "sizeof") return Val{Operand::number(sizeofOf(e)), 0, 0};
        if (e.kids.empty() || !e.kids[0]) return {};
        if (e.text == "&") {
            if (e.kids[0]->kind == Expr::Kind::Name) {
                Sym* s = find(e.kids[0]->text);
                if (!s) {
                    err(e.line, e.col, "not lowered: " + e.kids[0]->text);
                    return {};
                }
                if (s->ref) {
                    Val v;
                    v.op = Operand::vreg(s->reg);
                    v.step = s->width;
                    return v;
                }
                if (s->agg) {
                    Val base = readName(e.kids[0]->text, e.line, e.col);
                    base.step = s->width > 0 ? s->width : 8;
                    return base;
                }
                if (s->array || s->global) {
                    Val base = readName(e.kids[0]->text, e.line, e.col);
                    if (s->array) return base;
                    int dst = fresh();
                    Inst in;
                    in.op = Inst::Op::LeaSym;
                    in.dst = dst;
                    in.func = s->gname;
                    emit(in);
                    Val v;
                    v.op = Operand::vreg(dst);
                    v.step = s->width;
                    v.inner = s->step;
                    return v;
                }
                int dst = fresh();
                Inst in;
                in.op = Inst::Op::Lea;
                in.dst = dst;
                in.a = Operand::vreg(s->reg);
                emit(in);
                Val v;
                v.op = Operand::vreg(dst);
                v.step = s->width;
                v.inner = s->step;
                return v;
            }
            Addr a = lowerAddr(*e.kids[0]);
            Val v;
            v.op = a.op;
            v.step = a.width;
            return v;
        }
        if (e.text == "*") {
            Val p = lowerExpr(*e.kids[0]);
            Addr a;
            a.op = p.op;
            a.width = p.step == 0 ? 8 : p.step;
            a.ok = true;
            if (e.checked && (e.checked->kind == Type::Kind::Struct || e.checked->kind == Type::Kind::Union)) {
                // *p of an object is the object (its address), whatever p was lowered from (a
                // pointer from c ? &x : &y carries no field list).
                Val v;
                v.op = p.op;
                v.fields = p.fields.empty() ? layoutOf(e.checked.get()) : p.fields;
                v.step = p.step;
                v.inner = p.inner;
                return v;
            }
            if (e.checked && e.checked->kind == Type::Kind::Array) {
                // *p of an array (the result of a call returning T(&)[N]) is used through its
                // address: the array decays to its first element, so nothing is loaded.
                Val v = p;
                if (e.checked->inner) v.step = sizeOf(e.checked->inner.get());
                return v;
            }
            Val v = loadAddr(a, p.inner, e.checked && e.checked->unsign);
            if (e.checked) v.fpw = fpWidthOf(e.checked.get());  // *p of a float or double
            return v;
        }
        if (e.text == "+") return lowerExpr(*e.kids[0]);
        Val a = lowerExpr(*e.kids[0]);
        if (e.text == "-") {
            if (a.fpw) {
                std::uint64_t sign = a.fpw <= 4 ? 0x80000000ull : 0x8000000000000000ull;
                if (a.op.kind == Operand::Kind::Imm) {
                    Val o = a;
                    o.op = Operand::number(static_cast<std::int64_t>(static_cast<std::uint64_t>(a.op.imm) ^ sign));
                    return o;
                }
                int dst = fresh();
                Inst in;
                in.op = Inst::Op::Xor;
                in.fp = true;
                in.extra = a.fpw;
                in.dst = dst;
                in.a = a.op;
                in.b = Operand::number(static_cast<std::int64_t>(sign));
                emit(in);
                Val o;
                o.op = Operand::vreg(dst);
                o.fpw = a.fpw;
                return o;
            }
            if (a.op.kind == Operand::Kind::Imm && a.op.imm != std::numeric_limits<std::int64_t>::min())
                return Val{Operand::number(-a.op.imm), 0, 0};
            int dst = fresh();
            Inst in;
            in.op = Inst::Op::Neg;
            in.dst = dst;
            in.a = a.op;
            emit(in);
            return Val{Operand::vreg(dst), 0, 0};
        }
        if (e.text == "!") {
            if (a.op.kind == Operand::Kind::Imm) return Val{Operand::number(a.op.imm == 0 ? 1 : 0), 0, 0};
            int dst = fresh();
            emitBin(Inst::Op::Ceq, dst, a.op, Operand::number(0));
            return Val{Operand::vreg(dst), 0, 0};
        }
        if (e.text == "~") {
            if (a.op.kind == Operand::Kind::Imm) return Val{Operand::number(~a.op.imm), 0, 0};
            int dst = fresh();
            Inst in;
            in.op = Inst::Op::Not;
            in.dst = dst;
            in.a = a.op;
            emit(in);
            return Val{Operand::vreg(dst), 0, 0};
        }
        err(e.line, e.col, "not lowered");
        return {};
    }

    Val lowerInc(Expr& e, bool pre) {
        if (e.kids.empty() || !e.kids[0]) {
            err(e.line, e.col, "not lowered");
            return {};
        }
        Sym* simple = e.kids[0]->kind == Expr::Kind::Name ? find(e.kids[0]->text) : nullptr;
        if (!simple || simple->global || simple->array || simple->ref) {
            // Any other object (a field, an element, *p, a global, what a reference names): x++ is
            // (x += 1) - 1 and ++x is x += 1, with the object evaluated once.
            auto one = std::make_unique<Expr>();
            one->kind = Expr::Kind::Int;
            one->ival = 1;
            one->line = e.line;
            one->col = e.col;
            auto lhsType = e.kids[0]->checked ? cloneType(*e.kids[0]->checked) : nullptr;
            one->checked = std::make_unique<Type>();
            // A pointer moves by the size of what it points to ([expr.pre.incr]): the += below is
            // lowered as it is, without the checker's p = p + n scaling (++ on an int *& moved 1 byte).
            if (lhsType && lhsType->kind == Type::Kind::Pointer) {
                int st = valueStep(lhsType.get());
                if (st > 1) one->ival = static_cast<std::uint64_t>(st);
            }
            auto asg = std::make_unique<Expr>();
            asg->kind = Expr::Kind::Assign;
            asg->text = e.text == "--" ? "-=" : "+=";
            asg->line = e.line;
            asg->col = e.col;
            asg->file = e.file;
            asg->kids.push_back(std::move(e.kids[0]));
            asg->kids.push_back(std::move(one));
            if (lhsType) asg->checked = cloneType(*lhsType);
            if (pre) return lowerExpr(*asg);
            if (lhsType && lhsType->kind == Type::Kind::Pointer) {
                // p++ on a pointer: the new value less one step (the value of the += is not scaled
                // again by a pointer subtraction).
                Val after = lowerExpr(*asg);
                int st = valueStep(lhsType.get());
                int dst = fresh();
                emitBin(e.text == "--" ? Inst::Op::Add : Inst::Op::Sub, dst, after.op,
                        Operand::number(st > 1 ? st : 1));
                return Val{Operand::vreg(dst), after.step, after.inner};
            }
            auto back = std::make_unique<Expr>();
            back->kind = Expr::Kind::Bin;
            back->text = e.text == "--" ? "+" : "-";
            back->line = e.line;
            back->col = e.col;
            back->file = e.file;
            auto one2 = std::make_unique<Expr>();
            one2->kind = Expr::Kind::Int;
            one2->ival = 1;
            one2->checked = std::make_unique<Type>();
            if (lhsType) back->checked = cloneType(*lhsType);
            back->kids.push_back(std::move(asg));
            back->kids.push_back(std::move(one2));
            return lowerExpr(*back);
        }
        Sym* s = simple;
        // A pointer moves by the size of what it points to.
        Operand one = Operand::number((e.text == "--" ? -1 : 1) * (s->step > 1 ? s->step : 1));
        if (s->mem) {
            Val cur = loadMem(s->reg, s->width, s->zext);
            int dst = fresh();
            emitBin(Inst::Op::Add, dst, cur.op, one);
            Operand nxt = narrow(Operand::vreg(dst), s->width, s->zext);
            storeMem(s->reg, nxt, s->width);
            return pre ? Val{nxt, 0, 0} : cur;
        }
        if (pre) {
            emitBin(Inst::Op::Add, s->reg, Operand::vreg(s->reg), one);
            if (s->width > 0 && s->width < 8) emitMov(s->reg, Operand::vreg(s->reg), s->width, s->zext);
            return Val{Operand::vreg(s->reg), s->step, s->inner};
        }
        int old = fresh();
        emitMov(old, Operand::vreg(s->reg));
        emitBin(Inst::Op::Add, s->reg, Operand::vreg(s->reg), one);
        if (s->width > 0 && s->width < 8) emitMov(s->reg, Operand::vreg(s->reg), s->width, s->zext);
        return Val{Operand::vreg(old), s->step, s->inner};
    }

    Val cvtFp(Val v, int src, int dstCode) {
        if (dstCode > 0 && src == dstCode) return v;
        if (src == 0 && v.op.kind == Operand::Kind::Imm && dstCode > 0) {
            double d = static_cast<double>(v.op.imm);
            Val o;
            o.fpw = dstCode;
            if (dstCode == 4) {
                float f = static_cast<float>(d);
                std::uint32_t bits = 0;
                std::memcpy(&bits, &f, 4);
                o.op = Operand::number(bits);
            } else {
                std::uint64_t bits = 0;
                std::memcpy(&bits, &d, 8);
                o.op = Operand::number(static_cast<std::int64_t>(bits));
            }
            return o;
        }
        int t = fresh();
        Inst in;
        in.op = Inst::Op::Cvt;
        in.fp = true;
        in.dst = t;
        in.a = v.op;
        in.extra = src;
        in.b = Operand::number(dstCode);
        emit(in);
        Val o;
        o.op = Operand::vreg(t);
        o.fpw = dstCode > 0 ? dstCode : 0;
        return o;
    }

    Val lowerBin(Expr& e) {
        if (e.kids.size() < 2 || !e.kids[0] || !e.kids[1]) return {};
        Val a = lowerExpr(*e.kids[0]);
        Val b = lowerExpr(*e.kids[1]);
        int aw = a.fpw ? a.fpw : fpWidthOf(e.kids[0]->checked.get());
        int bw = b.fpw ? b.fpw : fpWidthOf(e.kids[1]->checked.get());
        if (aw || bw) {
            if (e.text == "%" || e.text == "&" || e.text == "|" || e.text == "^" || e.text == "<<" || e.text == ">>") {
                err(e.line, e.col, "not lowered");
                return {};
            }
            int dw = (aw == 8 || bw == 8) ? 8 : 4;
            if (!aw) a = cvtFp(a, 0, dw);
            else if (aw != dw) a = cvtFp(a, aw, dw);
            if (!bw) b = cvtFp(b, 0, dw);
            else if (bw != dw) b = cvtFp(b, bw, dw);
            bool cmp = e.text == "<" || e.text == ">" || e.text == "<=" || e.text == ">=" || e.text == "==" ||
                       e.text == "!=";
            int dst = fresh();
            Inst in;
            in.fp = true;
            in.extra = dw;
            in.dst = dst;
            in.a = a.op;
            in.b = b.op;
            if (cmp) in.op = binOp(e.text);
            else if (e.text == "+") in.op = Inst::Op::Add;
            else if (e.text == "-") in.op = Inst::Op::Sub;
            else if (e.text == "*") in.op = Inst::Op::Mul;
            else in.op = Inst::Op::Div;
            emit(in);
            Val r;
            r.op = Operand::vreg(dst);
            if (!cmp) r.fpw = dw;
            return r;
        }
        std::int64_t folded = 0;
        if (a.step == 0 && b.step == 0 && a.op.kind == Operand::Kind::Imm && b.op.kind == Operand::Kind::Imm &&
            fold(e.text, a.op.imm, b.op.imm, folded))
            return Val{Operand::number(folded), 0, 0};
        if ((e.text == "+" || e.text == "-") && a.step != 0 && b.step == 0) {
            Val off = scale(b, a.step);
            int dst = fresh();
            emitBin(e.text == "+" ? Inst::Op::Add : Inst::Op::Sub, dst, a.op, off.op);
            return Val{Operand::vreg(dst), a.step, a.inner};
        }
        if (e.text == "+" && b.step != 0 && a.step == 0) {
            Val off = scale(a, b.step);
            int dst = fresh();
            emitBin(Inst::Op::Add, dst, b.op, off.op);
            return Val{Operand::vreg(dst), b.step, b.inner};
        }
        if (e.text == "-" && a.step != 0 && a.step == b.step) {
            int diff = fresh();
            emitBin(Inst::Op::Sub, diff, a.op, b.op);
            int q = fresh();
            emitBin(Inst::Op::Div, q, Operand::vreg(diff), Operand::number(a.step));
            return Val{Operand::vreg(q), 0, 0};
        }
        bool uns = false;
        if (e.text == "<" || e.text == ">" || e.text == "<=" || e.text == ">=") {
            const Type* ta = e.kids[0]->checked.get();
            const Type* tb = e.kids[1]->checked.get();
            auto isLong = [](const Type* t) { return t && t->kind == Type::Kind::Long; };
            bool aUns = ta && ta->unsign;
            bool bUns = tb && tb->unsign;
            if ((isLong(ta) && aUns) || (isLong(tb) && bUns)) uns = true;
            else if ((isLong(ta) && !aUns && bUns) || (isLong(tb) && !bUns && aUns)) uns = false;
            else if (aUns || bUns) {
                uns = true;
                a.op = narrow(a.op, 4, true);
                b.op = narrow(b.op, 4, true);
            }
        }
        bool arith = false;
        if ((e.text == "/" || e.text == "%" || e.text == ">>") && e.checked && isIntish(*e.checked)) {
            // Unsigned division, remainder and right shift; a signed right shift is arithmetic
            // (implementation-defined in C, arithmetic in g++ and in C++20).
            if (e.checked->unsign) {
                if (e.text != ">>") uns = true;
                if (e.checked->kind != Type::Kind::Long) {
                    a.op = narrow(a.op, 4, true);
                    if (e.text != ">>") b.op = narrow(b.op, 4, true);
                }
            } else if (e.text == ">>") {
                arith = true;
            }
        }
        int dst = fresh();
        emitBin(binOp(e.text), dst, a.op, b.op, uns);
        if (arith) fn_.blocks[static_cast<std::size_t>(cur_)].code.back().arith = true;
        return Val{Operand::vreg(dst), 0, 0};
    }

    Val storeCall(Sym& dst, Expr& call) {
        int sz = dst.objSize > 0 ? dst.objSize : dst.width;
        Operand addr = addrOf(dst);
        if (sz > 16) {
            lowerCall(call, &addr);
            return {};
        }
        Val v = lowerCall(call);
        if (v.pair) {
            storeBits(addr, v.op, 8);
            storeBits(addAddr(addr, 8), v.hi, sz - 8);
        } else {
            storeBits(addr, v.op, sz);
        }
        return v;
    }

    void storeNamed(Sym& s, Operand val) {
        if (s.boolean) val = asBool(val);
        if (s.ref) {
            storeBits(Operand::vreg(s.reg), val, s.width > 0 ? s.width : 8);
            return;
        }
        if (s.global) {
            Inst in;
            in.op = Inst::Op::GStore;
            in.func = s.gname;
            in.a = val;
            in.extra = s.width > 0 ? s.width : 8;
            emit(in);
        } else if (s.mem) {
            storeMem(s.reg, val, s.width);
        } else {
            emitMov(s.reg, val, s.width, s.zext);
        }
    }

    // Store a struct value into memory. 'rhs' is the address of the source object, or for a call
    // the returned value (one register, a register pair, or the address of the hidden result).
    void storeAggregate(Operand dst, int sz, const Val& rhs, bool fromCall) {
        if (rhs.pair) {
            storeBits(dst, rhs.op, 8);
            storeBits(addAddr(dst, 8), rhs.hi, sz - 8);
        } else if (sz > 8) {
            copyMem(dst, rhs.op, sz);
        } else {
            Operand bits = fromCall ? rhs.op : loadBits(rhs.op, sz);
            storeBits(dst, bits, sz);
        }
    }

    Val lowerAssign(Expr& e) {
        if (e.kids.size() < 2 || !e.kids[0] || !e.kids[1]) {
            err(e.line, e.col, "not lowered");
            return {};
        }
        if (e.text != "=") {
            std::string op = e.text.substr(0, e.text.size() - 1);
            Expr& lhs = *e.kids[0];
            Val rhs = lowerExpr(*e.kids[1]);
            Val lhsVal;
            Addr addr;
            bool byAddr = lhs.kind != Expr::Kind::Name;
            if (!byAddr) lhsVal = readName(lhs.text, lhs.line, lhs.col);
            else {
                addr = lowerAddr(lhs);
                if (!addr.ok) return {};
                lhsVal = loadAddr(addr, 0, lhs.checked && lhs.checked->unsign);
            }
            int dst = fresh();
            const Type* lt = lhs.checked.get();
            bool opUns = false, opArith = false;
            if ((op == "/" || op == "%" || op == ">>") && lt && isIntish(*lt)) {
                if (lt->unsign) {
                    if (op != ">>") opUns = true;
                    if (lt->kind != Type::Kind::Long) {
                        lhsVal.op = narrow(lhsVal.op, 4, true);
                        if (op != ">>") rhs.op = narrow(rhs.op, 4, true);
                    }
                } else if (op == ">>") {
                    opArith = true;
                }
            }
            emitBin(binOp(op), dst, lhsVal.op, rhs.op, opUns);
            if (opArith) fn_.blocks[static_cast<std::size_t>(cur_)].code.back().arith = true;
            Operand result = Operand::vreg(dst);
            if (!byAddr) {
                Sym* s = find(lhs.text);
                if (!s) {
                    err(e.line, e.col, "not lowered");
                    return {};
                }
                if (s->width > 0 && s->width < 8) result = narrow(result, s->width, s->zext);
                storeNamed(*s, result);
                // (a reference's register holds the referent's address: the value is result)
                if (s->global || s->ref) return Val{result, s->step, s->inner};
                return Val{Operand::vreg(s->reg), s->step, s->inner};
            }
            storeBits(addr.op, result, addr.width > 0 ? addr.width : 8);
            return Val{result, 0, 0};
        }
        Expr& lhs = *e.kids[0];
        if (lhs.kind == Expr::Kind::Name && e.kids[1]->kind == Expr::Kind::Call) {
            Sym* s = find(lhs.text);
            if (s && s->agg) return storeCall(*s, *e.kids[1]);
        }
        if (lhs.kind == Expr::Kind::Name && e.kids[1]->kind != Expr::Kind::Call) {
            // A local object assigned from *p, a[i] or s.f: copy the bytes from that object's address.
            Sym* s = find(lhs.text);
            const Expr& r = *e.kids[1];
            const bool viaAddress = (r.kind == Expr::Kind::Unary && r.text == "*") || r.kind == Expr::Kind::Index ||
                                    r.kind == Expr::Kind::Member;
            if (s && s->agg && !s->ref && viaAddress) {
                Addr src = lowerAddr(*e.kids[1]);
                if (!src.ok) return {};
                int sz = s->objSize > 0 ? s->objSize : s->width;
                Operand dst = addrOf(*s);
                copyMem(dst, src.op, sz);
                return Val{dst, 0, 0};
            }
        }
        if (lhs.kind != Expr::Kind::Name && lhs.checked && lhs.checked->kind == Type::Kind::Struct &&
            e.kids[1]->kind != Expr::Kind::Call) {
            // struct = struct through a pointer, a field or an index: copy the bytes.
            Addr src = lowerAddr(*e.kids[1]);
            Addr dst = lowerAddr(lhs);
            if (!src.ok || !dst.ok) return {};
            copyMem(dst.op, src.op, sizeOf(lhs.checked.get()));
            return Val{dst.op, 0, 0};
        }
        Val rhs = lowerExpr(*e.kids[1]);
        if (lhs.kind == Expr::Kind::Name) {
            Sym* s = find(lhs.text);
            if (!s) {
                err(e.line, e.col, "not lowered: " + lhs.text);
                return {};
            }
            if (s->boolean) rhs.op = asBool(rhs.op, rhs.fpw);
            if (s->fpw) {
                if (rhs.fpw != s->fpw) rhs = cvtFp(rhs, rhs.fpw, s->fpw);
                if (s->ref) {
                    // float &r = ...; r = v: a store through the reference (the register holds the
                    // address; moving v into it lost the store and the address).
                    storeBits(Operand::vreg(s->reg), rhs.op, s->fpw);
                    Val out;
                    out.op = rhs.op;
                    out.fpw = s->fpw;
                    return out;
                }
                if (s->global) {
                    Inst in;
                    in.op = Inst::Op::GStore;
                    in.func = s->gname;
                    in.a = rhs.op;
                    in.extra = s->fpw;
                    emit(in);
                } else {
                    emitMov(s->reg, rhs.op, 8);
                }
                Val out;
                out.op = s->global ? rhs.op : Operand::vreg(s->reg);
                out.fpw = s->fpw;
                return out;
            }
            if (s->array) {
                err(e.line, e.col, "not lowered");
                return {};
            }
            if (s->ref && !s->agg) {
                Operand val = narrow(rhs.op, s->width, s->zext);
                storeBits(Operand::vreg(s->reg), val, s->width > 0 ? s->width : 8);
                return Val{val, 0, 0};
            }
            if (s->agg) {
                // An object, or a reference to one: copy the bytes into it.
                int sz = s->objSize > 0 ? s->objSize : s->width;
                Operand addr = s->ref ? Operand::vreg(s->reg) : addrOf(*s);
                if (rhs.pair) {
                    storeBits(addr, rhs.op, 8);
                    storeBits(addAddr(addr, 8), rhs.hi, sz - 8);
                } else if (sz > 8) {
                    copyMem(addr, rhs.op, sz);
                } else {
                    Operand bits = rhs.op;
                    if (!e.kids[1] || e.kids[1]->kind != Expr::Kind::Call) bits = loadBits(rhs.op, sz);
                    storeBits(addr, bits, sz);
                }
                return rhs;
            }
            if (s->global || s->mem) {
                Operand val = narrow(rhs.op, s->width, s->zext);
                if (s->global) {
                    Inst in;
                    in.op = Inst::Op::GStore;
                    in.func = s->gname;
                    in.a = val;
                    in.extra = s->width;
                    emit(in);
                } else {
                    storeMem(s->reg, val, s->width);
                }
                return Val{val, 0, 0};
            }
            emitMov(s->reg, rhs.op, s->width, s->zext);
            return Val{Operand::vreg(s->reg), s->step, s->inner};
        }
        Addr a = lowerAddr(lhs);
        if (!a.ok) return {};
        if (lhs.checked && lhs.checked->kind == Type::Kind::Struct) {
            storeAggregate(a.op, sizeOf(lhs.checked.get()), rhs, e.kids[1]->kind == Expr::Kind::Call);
            return rhs;
        }
        if (a.bitWidth > 0) {
            unsigned mask = a.bitWidth >= 32 ? 0xffffffffu : ((1u << a.bitWidth) - 1u);
            unsigned fieldMask = mask << a.bitOff;
            int word = fresh();
            Inst ld;
            ld.op = Inst::Op::Load;
            ld.dst = word;
            ld.a = a.op;
            ld.extra = a.width > 0 ? a.width : 4;
            ld.uns = true;
            emit(ld);
            int val = fresh();
            emitBin(Inst::Op::And, val, rhs.op, Operand::number(static_cast<std::int64_t>(mask)));
            int shifted = fresh();
            emitBin(Inst::Op::Shl, shifted, Operand::vreg(val), Operand::number(a.bitOff));
            int cleared = fresh();
            emitBin(Inst::Op::And, cleared, Operand::vreg(word),
                    Operand::number(static_cast<std::int64_t>(~static_cast<std::uint64_t>(fieldMask))));
            int merged = fresh();
            emitBin(Inst::Op::Or, merged, Operand::vreg(cleared), Operand::vreg(shifted));
            storeBits(a.op, Operand::vreg(merged), a.width > 0 ? a.width : 4);
            return Val{Operand::vreg(val), 0, 0};
        }
        if (lhs.checked && lhs.checked->kind == Type::Kind::Bool) rhs.op = asBool(rhs.op, rhs.fpw);
        if (lhs.checked && isFp(*lhs.checked)) {
            int w = fpWidthOf(lhs.checked.get());
            if (rhs.fpw != w) rhs = cvtFp(rhs, rhs.fpw, w);
            storeBits(a.op, rhs.op, w == 4 ? 4 : 8);  // a float is 4 bytes: 8 wrote past it
            Val out;
            out.op = rhs.op;
            out.fpw = w;
            return out;
        }
        bool destUns = lhs.checked && lhs.checked->unsign;
        Operand val = narrow(rhs.op, a.width, destUns);
        Inst in;
        in.op = Inst::Op::Store;
        in.a = a.op;
        in.b = val;
        in.extra = a.width;
        emit(in);
        return Val{val, 0, 0};
    }

    Val lowerCond(Expr& e) {
        if (e.kids.size() < 3 || !e.kids[0] || !e.kids[1] || !e.kids[2]) return {};
        int dst = fresh();
        std::string tL = lab("cond_then");
        std::string fL = lab("cond_else");
        std::string eL = lab("cond_end");
        emitBr(material(lowerExpr(*e.kids[0])), tL, fL);
        startBlock(tL);
        Val first = lowerExpr(*e.kids[1]);
        emitMov(dst, first.op);
        emitJmp(eL);
        startBlock(fL);
        emitMov(dst, lowerExpr(*e.kids[2]).op);
        emitJmp(eL);
        startBlock(eL);
        Val result{Operand::vreg(dst), 0, 0};
        if (e.checked) result.fpw = fpWidthOf(e.checked.get());
        // Two pointers to objects of one class: the result points to that class too.
        result.step = first.step;
        result.inner = first.inner;
        result.fields = first.fields;
        return result;
    }

    Val lowerVa(Expr& e, const std::string& fname) {
        if (e.kids.size() < 2 || !e.kids[1]) return {};
        Operand ap = lowerExpr(*e.kids[1]).op;
        if (fname == "__va_start") {
            int named = fn_.vaNamed < 0 ? 0 : fn_.vaNamed;
            storeBits(ap, Operand::number(named * 8), 4);
            storeBits(addAddr(ap, 4), Operand::number(48 + 16 * fn_.vaXmm), 4);
            storeBits(addAddr(ap, 8), frameAddr(16), 8);
            storeBits(addAddr(ap, 16), Operand::vreg(vaSave_), 8);
            return {};
        }
        int step = 8;
        bool fpArg = false;
        if (fname == "__va_argp" && e.kids.size() >= 3 && e.kids[2] && e.kids[2]->checked &&
            e.kids[2]->checked->kind == Type::Kind::Pointer && e.kids[2]->checked->inner &&
            isFp(*e.kids[2]->checked->inner))
            fpArg = true;
        if (!fpArg && e.kids.size() >= 3 && e.kids[2]) {
            Operand sz = lowerExpr(*e.kids[2]).op;
            if (sz.kind == Operand::Kind::Imm && sz.imm > 8) step = (static_cast<int>(sz.imm) + 7) & ~7;
        }
        int offAt = fpArg ? 4 : 0;
        int limit = fpArg ? 176 : 48;
        int bump = fpArg ? 16 : step;
        int stackBump = fpArg ? 8 : step;
        std::string id = std::to_string(tmpN_++);
        std::string regL = "va" + id + "_reg";
        std::string stkL = "va" + id + "_stk";
        std::string endL = "va" + id + "_end";
        Operand gp = loadBits(addAddr(ap, offAt), 4);
        int cmp = fresh();
        emitBin(Inst::Op::Cge, cmp, gp, Operand::number(limit), true);
        emitBr(cmp, stkL, regL);
        int result = fresh();
        startBlock(regL);
        Operand save = loadBits(addAddr(ap, 16), 8);
        int ptr = fresh();
        emitBin(Inst::Op::Add, ptr, save, gp);
        int gp2 = fresh();
        emitBin(Inst::Op::Add, gp2, gp, Operand::number(bump));
        storeBits(addAddr(ap, offAt), Operand::vreg(gp2), 4);
        emitMov(result, Operand::vreg(ptr));
        emitJmp(endL);
        startBlock(stkL);
        Operand ov = loadBits(addAddr(ap, 8), 8);
        emitMov(result, ov);
        int ov2 = fresh();
        emitBin(Inst::Op::Add, ov2, ov, Operand::number(stackBump));
        storeBits(addAddr(ap, 8), Operand::vreg(ov2), 8);
        emitJmp(endL);
        startBlock(endL);
        Val out;
        out.op = Operand::vreg(result);
        out.step = 1;
        return out;
    }

    Val lowerCall(Expr& e, const Operand* retSlotIn = nullptr) {
        if (e.kids.empty() || !e.kids[0]) {
            err(e.line, e.col, "not lowered");
            return {};
        }
        if (e.kids[0]->kind == Expr::Kind::Name && e.kids[0]->text == "\x01" "zero:" && e.kids.size() == 2 &&
            e.kids[1] && e.kids[1]->checked && e.kids[1]->checked->inner) {
            // The checker's "\x01zero:"(&object): the object's bytes set to zero (value-initialization of
            // an aggregate made from a braced list, [dcl.init.list] p3, [dcl.init] p8).
            int bytes = sizeOf(e.kids[1]->checked->inner.get());
            Val dst = lowerExpr(*e.kids[1]);
            if (bytes > 0) zeroMem(dst.op, bytes);
            return Val{Operand::number(0), 0, 0};
        }
        bool direct = false;
        std::string fname;
        Operand target;
        if (e.kids[0]->kind == Expr::Kind::Name) {
            Sym* s = find(e.kids[0]->text);
            if (!s || s->func) {
                direct = true;
                fname = s && !s->gname.empty() ? s->gname : e.kids[0]->text;
            }
        }
        if (direct && (fname == "__va_start" || fname == "__va_arg" || fname == "__va_argp")) return lowerVa(e, fname);
        if (direct && (fname == "__builtin_sqrt" || fname == "__builtin_sqrtf") && e.kids.size() == 2 && e.kids[1]) {
            int w = fname == "__builtin_sqrtf" ? 4 : 8;
            Val v = lowerExpr(*e.kids[1]);
            if (v.fpw != w) v = cvtFp(v, v.fpw, w);
            Inst in;
            in.op = Inst::Op::Sqrt;
            in.fp = true;
            in.extra = w;
            in.dst = fresh();
            in.a = v.op;
            emit(in);
            Val out;
            out.op = Operand::vreg(in.dst);
            out.fpw = w;
            return out;
        }
        if (!direct) target = lowerExpr(*e.kids[0]).op;
        int reg = 0;
        bool retRec = e.checked &&
                      (e.checked->kind == Type::Kind::Struct || e.checked->kind == Type::Kind::Union);
        bool retMem = retRec && sizeOf(e.checked.get()) > 16;
        bool retPair = retRec && sizeOf(e.checked.get()) > 8 && sizeOf(e.checked.get()) <= 16;
        int retSz = retRec ? sizeOf(e.checked.get()) : 0;
        std::vector<Operand> regs;
        std::vector<Operand> stack;
        Operand retSlot;
        if (retMem) {
            if (retSlotIn) {
                retSlot = *retSlotIn;
            } else {
                int slot = fresh();
                Inst loc;
                loc.op = Inst::Op::Local;
                loc.dst = slot;
                loc.a = Operand::number(retSz < 1 ? 1 : retSz);
                emit(loc);
                retSlot = Operand::vreg(slot);
            }
            regs.push_back(retSlot);
            reg = 1;
        }
        const Type* fnty = nullptr;
        if (e.kids[0]->checked) {
            const Type* t = e.kids[0]->checked.get();
            if (t->kind == Type::Kind::Pointer && t->inner && t->inner->kind == Type::Kind::Function) t = t->inner.get();
            if (t->kind == Type::Kind::Function) fnty = t;
        }
        int xmn = 0;
        std::vector<Operand> xmm;
        std::vector<int> xmmW;
        for (std::size_t i = 1; i < e.kids.size(); ++i) {
            if (!e.kids[i]) continue;
            Val v = lowerExpr(*e.kids[i]);
            const Type* t = e.kids[i]->checked.get();
            int argi = static_cast<int>(i - 1);
            const Type* declared = nullptr;
            bool varExtra = fnty && fnty->variadic && argi >= static_cast<int>(fnty->params.size());
            if (fnty && argi < static_cast<int>(fnty->params.size())) declared = fnty->params[static_cast<std::size_t>(argi)].type.get();
            int want = declared ? fpWidthOf(declared) : 0;
            if (varExtra && (v.fpw || fpWidthOf(t))) want = 8;
            // A float passed to "..." becomes a double (the default argument promotions, ISO C
            // 6.5.2.2 p7); without a prototype it goes as it is.
            if (!declared && !varExtra && (v.fpw || fpWidthOf(t))) want = v.fpw ? v.fpw : fpWidthOf(t);
            if (want) {
                int src = v.fpw ? v.fpw : fpWidthOf(t);
                if (src != want) v = cvtFp(v, src, want);
                if (xmn < 8) {
                    xmm.push_back(v.op);
                    xmmW.push_back(want);
                    ++xmn;
                } else {
                    stack.push_back(v.op);
                }
                continue;
            }
            int sz = 0;
            bool memClass = false;
            int nreg = structPieces(t, sz, memClass);
            bool isStruct = t && (t->kind == Type::Kind::Struct || t->kind == Type::Kind::Union);
            if (!isStruct) {
                bool decay = t && (t->kind == Type::Kind::Array || t->kind == Type::Kind::Function);
                int w = !t || decay ? 8 : sizeOf(t);
                bool uns = t && !decay && t->unsign;
                Operand val = declared && declared->kind == Type::Kind::Bool ? asBool(v.op, v.fpw)
                              : (w > 0 && w < 8) ? narrow(v.op, w, uns) : v.op;
                if (reg >= 6) stack.push_back(val);
                else {
                    regs.push_back(val);
                    ++reg;
                }
                continue;
            }
            // A struct of up to 16 bytes that is a call's result arrives in registers (rax, or
            // rax:rdx), not as an address: pass those eightbytes on as they are.
            const bool inRegs = !memClass && sz <= 16 && e.kids[i]->kind == Expr::Kind::Call;
            if (inRegs && (v.pair || nreg == 1)) {
                Operand lo = v.op;
                Operand hi = v.pair ? v.hi : Operand::number(0);
                if (reg + nreg > 6) {
                    stack.push_back(lo);
                    if (nreg == 2) stack.push_back(hi);
                } else {
                    regs.push_back(lo);
                    ++reg;
                    if (nreg == 2) {
                        regs.push_back(hi);
                        ++reg;
                    }
                }
                continue;
            }
            if (memClass || reg + nreg > 6) {
                int off = 0;
                int whole = (sz + 7) & ~7;
                while (off < whole) {
                    int n = sz - off;
                    if (n <= 0) stack.push_back(Operand::number(0));
                    else {
                        if (n > 8) n = 8;
                        stack.push_back(loadBits(addAddr(v.op, off), n));
                    }
                    off += 8;
                }
            } else {
                regs.push_back(loadBits(v.op, sz >= 8 ? 8 : sz));
                if (nreg == 2) regs.push_back(loadBits(addAddr(v.op, 8), sz - 8));
                reg += nreg;
            }
        }
        while (stack.size() % 2) stack.push_back(Operand::number(0));
        int dst = fresh();
        int hi = -1;
        if (retPair) hi = fresh();
        Inst in;
        in.op = Inst::Op::Call;
        in.dst = dst;
        if (direct) in.func = std::move(fname);
        else in.a = target;
        in.args = std::move(regs);
        in.stack = std::move(stack);
        in.xmm = std::move(xmm);
        in.xmmW = std::move(xmmW);
        if (hi >= 0) in.b = Operand::vreg(hi);
        if (e.kids[0]->checked) {
            const Type* t = e.kids[0]->checked.get();
            if (t->kind == Type::Kind::Pointer && t->inner && t->inner->kind == Type::Kind::Function) t = t->inner.get();
            if (t->kind == Type::Kind::Function && t->variadic) in.va = true;
        }
        if (e.checked && isFp(*e.checked)) {
            in.fp = true;
            in.extra = fpWidthOf(e.checked.get());
        }
        emit(in);
        if (retMem) {
            Val out;
            out.op = retSlot;
            out.fields = layoutOf(e.checked.get());
            return out;
        }
        Val out{Operand::vreg(dst), 0, 0};
        if (e.checked && isFp(*e.checked)) out.fpw = fpWidthOf(e.checked.get());
        if (retPair) {
            out.pair = true;
            out.hi = Operand::vreg(hi);
        }
        if (e.checked && e.checked->kind == Type::Kind::Pointer) {
            out.step = valueStep(e.checked.get());
            out.inner = innerOf(e.checked.get());
            if (e.checked->inner && e.checked->inner->kind == Type::Kind::Struct)
                out.fields = layoutOf(e.checked->inner.get());
        }
        return out;
    }

    // dup-allow: lowers the syntax tree to IR, not a string helper (the lint matches the word "lower")
    void lowerStmt(Stmt& s) {
        if (!s.file.empty()) file_ = s.file;
        switch (s.kind) {
        case Stmt::Kind::Block:
            scopes_.emplace_back();
            for (auto& st : s.stmts)
                if (st) lowerStmt(*st);
            scopes_.pop_back();
            break;
        case Stmt::Kind::Empty:
            break;
        case Stmt::Kind::Expr:
            if (s.expr) lowerExpr(*s.expr);
            break;
        case Stmt::Kind::Return: {
            Inst in;
            in.op = Inst::Op::Ret;
            if (s.expr && retRef_) {
                Addr a = lowerAddr(*s.expr);
                in.a = a.op;
                in.extra = 8;
                in.uns = true;
                emit(in);
                break;
            }
            if (s.expr && retStruct_ && retWidth_ > 16 && hiddenReg_ >= 0) {
                Operand hid = Operand::vreg(hiddenReg_);
                if (s.expr->kind == Expr::Kind::Call) lowerCall(*s.expr, &hid);
                else if (s.expr->kind == Expr::Kind::Name) {
                    if (Sym* sy = find(s.expr->text))
                        if (sy->agg) copyMem(hid, readName(s.expr->text, s.line, s.col).op, retWidth_);
                } else {
                    Val v = lowerExpr(*s.expr);
                    copyMem(hid, v.op, retWidth_);
                }
                in.a = hid;
                emit(in);
                break;
            }
            if (s.expr && retStruct_ && retWidth_ > 8) {
                Operand lo;
                Operand hi;
                if (s.expr->kind == Expr::Kind::Call) {
                    Val v = lowerCall(*s.expr);
                    lo = v.op;
                    hi = v.hi;
                } else if (s.expr->kind == Expr::Kind::Name && find(s.expr->text) && find(s.expr->text)->agg) {
                    Val addr = readName(s.expr->text, s.line, s.col);
                    lo = loadBits(addr.op, 8);
                    hi = loadBits(addAddr(addr.op, 8), retWidth_ - 8);
                } else {
                    Val v = lowerExpr(*s.expr);
                    lo = loadBits(v.op, 8);
                    hi = loadBits(addAddr(v.op, 8), retWidth_ - 8);
                }
                in.a = lo;
                in.b = hi;
                in.uns = true;
                emit(in);
                break;
            }
            if (s.expr && retStruct_ && s.expr->kind == Expr::Kind::Name) {
                if (Sym* sy = find(s.expr->text)) {
                    if (sy->agg) {
                        Val addr = readName(s.expr->text, s.line, s.col);
                        in.a = loadBits(addr.op, retWidth_);
                        in.extra = bitWidth(retWidth_);
                        in.uns = true;
                        emit(in);
                        break;
                    }
                }
            }
            if (s.expr && retStruct_ && s.expr->kind != Expr::Kind::Call && s.expr->kind != Expr::Kind::Name) {
                // Any other object expression (a temporary, *p, a[i], s.f): its address is lowered.
                Val v = lowerExpr(*s.expr);
                in.a = loadBits(v.op, retWidth_);
                in.extra = bitWidth(retWidth_);
                in.uns = true;
                emit(in);
                break;
            }
            if (s.expr) {
                Val v = lowerExpr(*s.expr);
                if (retBool_) v.op = asBool(v.op, v.fpw);
                if (retFp_ && v.fpw != retWidth_) v = cvtFp(v, v.fpw, retWidth_);
                in.a = v.op;
                in.extra = retWidth_;
                in.uns = retUns_ || retStruct_;
                in.fp = retFp_;
            }
            emit(in);
            break;
        }
        case Stmt::Kind::If:
            lowerIf(s);
            break;
        case Stmt::Kind::While:
            lowerWhile(s);
            break;
        case Stmt::Kind::For:
            lowerFor(s);
            break;
        case Stmt::Kind::Break:
            if (loops_.empty()) err(s.line, s.col, "break is not inside a loop or switch");
            else emitJmp(loops_.back().brk);
            break;
        case Stmt::Kind::Continue: {
            int at = -1;
            for (int i = static_cast<int>(loops_.size()) - 1; i >= 0; --i) {
                if (!loops_[static_cast<std::size_t>(i)].sw) {
                    at = i;
                    break;
                }
            }
            if (at < 0) err(s.line, s.col, "continue is not inside a loop");
            else emitJmp(loops_[static_cast<std::size_t>(at)].cont);
            break;
        }
        case Stmt::Kind::Switch:
            lowerSwitch(s);
            break;
        case Stmt::Kind::Case:
            break;
        case Stmt::Kind::Decl:
            if (s.decl) lowerLocal(*s.decl);
            break;
        case Stmt::Kind::Goto:
            if (s.expr) emitJmp("lab_" + s.expr->text);
            break;
        case Stmt::Kind::Label:
            if (s.expr) startBlock("lab_" + s.expr->text);
            break;
        }
    }

    // dup-allow: lowers the syntax tree to IR, not a string helper (the lint matches the word "lower")
    void lowerLocal(Decl& d) {
        if (!d.type) return;
        inferBound(d);  // char a[] = "text", int a[] = {1, 2}: the length comes from the initializer
        if (d.isStatic) {
            std::string gname = ".Ls" + std::to_string(strN_++) + "_" + d.name;
            Global g;
            g.name = gname;
            int sz = sizeOf(d.type.get());
            if (sz < 1) sz = 1;
            g.size = sz;
            g.bytes.assign(static_cast<std::size_t>(sz), 0);
            if (d.init && (d.init->kind == Expr::Kind::Int || d.init->kind == Expr::Kind::Char)) {
                std::int64_t n = static_cast<std::int64_t>(d.init->ival);
                if (d.type->kind == Type::Kind::Bool) n = n != 0;
                putAt(g.bytes, 0, n, sz > 8 ? 8 : sz);
            } else if (d.init && ((d.init->kind == Expr::Kind::String && d.type->kind == Type::Kind::Array) ||
                                  d.init->kind == Expr::Kind::Brace)) {
                // static char a[] = "text", static const E all[] = {E::A, E::B}: constant data, set
                // once in the image like a global's ([basic.start.static]). A value that is not a
                // constant is reported by fillInit (dynamic initialization of a static local is not
                // supported); it was silently zero before.
                fillInit(d.type.get(), d.init.get(), g.bytes, 0);
                g.bytes.resize(static_cast<std::size_t>(sz));
            } else if (d.init) {
                // static const float k = 0.5f, static double s = -1.0: the constant's bits; any
                // other value is reported by fillInit.
                fillInit(d.type.get(), d.init.get(), g.bytes, 0);
                g.bytes.resize(static_cast<std::size_t>(sz));
            }
            mod_.globals.push_back(std::move(g));
            Sym s;
            s.global = true;
            s.gname = gname;
            describe(s, d.type.get());
            if (!d.name.empty()) scopes_.back()[d.name] = s;
            return;
        }
        Sym s;
        describe(s, d.type.get());
        if (d.type->kind == Type::Kind::Array || d.type->kind == Type::Kind::Struct ||
            d.type->kind == Type::Kind::Union) {
            int bytes = sizeOf(d.type.get());
            if (bytes <= 0) {
                err(d.line, d.col, "array length is not a constant");
                bytes = 8;
            }
            int addr = fresh();
            Inst in;
            in.op = Inst::Op::Local;
            in.dst = addr;
            in.a = Operand::number(bytes);
            emit(in);
            s.reg = addr;
            if (!d.name.empty()) scopes_.back()[d.name] = s;
            if (d.init && d.init->kind == Expr::Kind::Brace) {
                // Elements and fields without an initializer are zero (ISO C 6.7.9p21).
                zeroMem(Operand::vreg(addr), bytes);
                initLocalBrace(Operand::vreg(addr), d.type.get(), *d.init, 0);
            } else if (d.init && d.init->kind == Expr::Kind::String && d.type->kind == Type::Kind::Array) {
                // char a[N] = "text" (ISO C 6.7.9p14, [dcl.init.string]): the characters, the
                // terminating zero if it fits, and zeros after; stored 8 bytes at a time.
                std::vector<unsigned char> img(static_cast<std::size_t>(bytes), 0);
                for (std::size_t i = 0; i < d.init->text.size() && i < img.size(); ++i)
                    img[i] = static_cast<unsigned char>(d.init->text[i]);
                int off = 0;
                while (off < bytes) {
                    int n = bytes - off >= 8 ? 8 : 1;
                    std::uint64_t v = 0;
                    for (int k = n - 1; k >= 0; --k) v = (v << 8) | img[static_cast<std::size_t>(off + k)];
                    storeBits(addAddr(Operand::vreg(addr), off), Operand::number(static_cast<std::int64_t>(v)), n);
                    off += n;
                }
            } else if (d.init && d.type->kind == Type::Kind::Struct) {
                // A struct initialized from another struct or from a call: copy the bytes.
                Sym* me = find(d.name);
                if (d.init->kind == Expr::Kind::Call && me) {
                    storeCall(*me, *d.init);
                } else {
                    Addr src = lowerAddr(*d.init);
                    if (src.ok) copyMem(Operand::vreg(addr), src.op, bytes);
                }
            }
            return;
        }
        if (s.ref && d.init) {
            int slot = fresh();
            s.reg = slot;
            if (!d.name.empty()) scopes_.back()[d.name] = s;
            Addr a = lowerAddr(*d.init);
            emitMov(slot, a.op);
            return;
        }
        int slot = fresh();
        s.reg = slot;
        if (!d.name.empty() && addr_.count(d.name)) s.mem = true;
        if (!d.name.empty()) scopes_.back()[d.name] = s;
        if (d.init) {
            Val v = lowerExpr(*d.init);
            if (s.fpw) {
                if (v.fpw != s.fpw) v = cvtFp(v, v.fpw, s.fpw);
                emitMov(slot, v.op, 8);
            } else {
                Operand val = s.boolean ? asBool(v.op, v.fpw) : narrow(v.op, s.width, s.zext);
                if (s.mem) storeMem(slot, val, s.width);
                else emitMov(slot, val, s.width, s.zext);
            }
        }
    }

    // dup-allow: lowers the syntax tree to IR, not a string helper (the lint matches the word "lower")
    void lowerIf(Stmt& s) {
        int id = ifN_++;
        std::string thenL = "if" + std::to_string(id) + "_then";
        std::string elseL = "if" + std::to_string(id) + "_else";
        std::string endL = "if" + std::to_string(id) + "_end";
        int cond = s.expr ? material(lowerExpr(*s.expr)) : material(Val{Operand::number(0), 0, 0});
        emitBr(cond, thenL, s.elseS ? elseL : endL);
        startBlock(thenL);
        if (s.thenS) lowerStmt(*s.thenS);
        emitJmp(endL);
        if (s.elseS) {
            startBlock(elseL);
            lowerStmt(*s.elseS);
            emitJmp(endL);
        }
        startBlock(endL);
    }

    // dup-allow: lowers the syntax tree to IR, not a string helper (the lint matches the word "lower")
    void lowerWhile(Stmt& s) {
        int id = whileN_++;
        std::string cL = "while" + std::to_string(id) + "_cond";
        std::string bL = "while" + std::to_string(id) + "_body";
        std::string eL = "while" + std::to_string(id) + "_end";
        emitJmp(cL);
        startBlock(cL);
        int cond = s.expr ? material(lowerExpr(*s.expr)) : material(Val{Operand::number(0), 0, 0});
        emitBr(cond, bL, eL);
        startBlock(bL);
        loops_.push_back(Loop{cL, eL});
        if (s.body) lowerStmt(*s.body);
        loops_.pop_back();
        emitJmp(cL);
        startBlock(eL);
    }

    // dup-allow: lowers the syntax tree to IR, not a string helper (the lint matches the word "lower")
    void lowerFor(Stmt& s) {
        int id = forN_++;
        std::string cL = "for" + std::to_string(id) + "_cond";
        std::string bL = "for" + std::to_string(id) + "_body";
        std::string sL = "for" + std::to_string(id) + "_step";
        std::string eL = "for" + std::to_string(id) + "_end";
        scopes_.emplace_back();
        if (s.init) {
            if (s.init->kind == Stmt::Kind::Block) {
                for (auto& st : s.init->stmts)
                    if (st) lowerStmt(*st);
            } else {
                lowerStmt(*s.init);
            }
        }
        emitJmp(cL);
        startBlock(cL);
        if (s.expr) emitBr(material(lowerExpr(*s.expr)), bL, eL);
        else emitJmp(bL);
        startBlock(bL);
        loops_.push_back(Loop{s.expr2 ? sL : cL, eL});
        if (s.body) lowerStmt(*s.body);
        loops_.pop_back();
        emitJmp(s.expr2 ? sL : cL);
        if (s.expr2) {
            startBlock(sL);
            lowerExpr(*s.expr2);
            emitJmp(cL);
        }
        startBlock(eL);
        scopes_.pop_back();
    }

    void scanExpr(const Expr* e) {
        if (!e) return;
        if (e->kind == Expr::Kind::Call && !e->kids.empty() && e->kids[0] && e->kids[0]->kind == Expr::Kind::Name &&
            e->kids[0]->text == "__okay_setjmp")
            hasTry_ = true;
        if (e->kind == Expr::Kind::Unary && e->text == "&" && !e->kids.empty() && e->kids[0] &&
            e->kids[0]->kind == Expr::Kind::Name)
            addr_.insert(e->kids[0]->text);
        for (const auto& k : e->kids) scanExpr(k.get());
    }
    bool hasTry_ = false;
    std::set<std::string> declNames_;
    void scanStmt(const Stmt& s) {
        if (s.decl) declNames_.insert(s.decl->name);
        scanExpr(s.expr.get());
        scanExpr(s.expr2.get());
        scanExpr(s.expr3.get());
        if (s.decl) {
            scanExpr(s.decl->init.get());
            if (s.decl->type && s.decl->type->kind == Type::Kind::Reference && s.decl->init &&
                s.decl->init->kind == Expr::Kind::Name)
                addr_.insert(s.decl->init->text);
        }
        for (const auto& st : s.stmts)
            if (st) scanStmt(*st);
        if (s.thenS) scanStmt(*s.thenS);
        if (s.elseS) scanStmt(*s.elseS);
        if (s.body) scanStmt(*s.body);
        if (s.init) scanStmt(*s.init);
    }

    // dup-allow: lowers the syntax tree to IR, not a string helper (the lint matches the word "lower")
    void lowerSwitch(Stmt& s) {
        int id = switchN_++;
        std::string endL = "sw" + std::to_string(id) + "_end";
        std::string defL = "sw" + std::to_string(id) + "_def";
        int cond = s.expr ? material(lowerExpr(*s.expr)) : material(Val{Operand::number(0), 0, 0});
        struct Arm {
            std::string label;
            bool def = false;
            std::int64_t value = 0;
            Stmt* body = nullptr;
        };
        std::vector<Arm> arms;
        bool sawDef = false;
        int n = 0;
        for (auto& cs : s.stmts) {
            if (!cs) continue;
            Arm arm;
            arm.label = "sw" + std::to_string(id) + "_" + std::to_string(n++);
            arm.body = cs->body.get();
            if (!cs->expr) {
                if (sawDef) err(cs->line, cs->col, "more than one default");
                sawDef = true;
                arm.def = true;
                arm.label = defL;
            } else {
                Val v = lowerExpr(*cs->expr);
                if (v.op.kind != Operand::Kind::Imm) {
                    err(cs->line, cs->col, "case value is not a constant");
                    v.op = Operand::number(0);
                }
                arm.value = v.op.imm;
            }
            arms.push_back(arm);
        }
        for (std::size_t i = 0; i < arms.size(); ++i) {
            if (arms[i].def) continue;
            int cmp = fresh();
            emitBin(Inst::Op::Ceq, cmp, Operand::vreg(cond), Operand::number(arms[i].value));
            std::string next = "sw" + std::to_string(id) + "_t" + std::to_string(i);
            emitBr(cmp, arms[i].label, next);
            startBlock(next);
        }
        emitJmp(sawDef ? defL : endL);
        loops_.push_back(Loop{"", endL, true});
        for (std::size_t i = 0; i < arms.size(); ++i) {
            startBlock(arms[i].label);
            if (arms[i].body) lowerStmt(*arms[i].body);
            if (i + 1 < arms.size()) emitJmp(arms[i + 1].label);
            else emitJmp(endL);
        }
        loops_.pop_back();
        startBlock(endL);
    }

    // dup-allow: lowers the syntax tree to IR, not a string helper (the lint matches the word "lower")
    void lowerFunc(Decl& d) {
        fn_ = Function();
        fn_.name = d.asmName.empty() ? d.name : d.asmName;
        fn_.localSym = d.isStatic;
        fn_.weakSym = d.isInline && !d.isStatic;
        fn_.params = d.type ? static_cast<int>(d.type->params.size()) : 0;
        nextReg_ = 0;
        scopes_.clear();
        scopes_.emplace_back();
        loops_.clear();
        addr_.clear();
        retWidth_ = 8;
        retFp_ = false;
        retUns_ = false;
        retBool_ = false;
        retStruct_ = false;
        retRef_ = d.type && d.type->inner && d.type->inner->kind == Type::Kind::Reference;
        hiddenReg_ = -1;
        hasTry_ = false;
        declNames_.clear();
        if (d.body) scanStmt(*d.body);
        if (hasTry_) {
            // A longjmp restores only the registers saved when the try block was entered, so every
            // named local of this function lives in memory.
            for (const std::string& n : declNames_) addr_.insert(n);
            if (d.type)
                for (const Param& prm : d.type->params) addr_.insert(prm.name);
        }
        if (d.type && d.type->inner) {
            const Type* r = d.type->inner.get();
            if (r->kind == Type::Kind::Char || r->kind == Type::Kind::Short || r->kind == Type::Kind::Int ||
                r->kind == Type::Kind::Long || r->kind == Type::Kind::Bool)
                retWidth_ = sizeOf(r);
            if (r->kind == Type::Kind::Bool) retBool_ = true;
            if (r->kind == Type::Kind::Float || r->kind == Type::Kind::Double) {
                retFp_ = true;
                retWidth_ = sizeOf(r);
            }
            if (r->kind == Type::Kind::Struct || r->kind == Type::Kind::Union) {
                retStruct_ = true;
                retWidth_ = sizeOf(r);
            }
            retUns_ = r->unsign;
        }
        struct PPlan {
            bool memory = false;
            int nreg = 1;
            int regAt = 0;
            int stackOff = 0;
            int size = 8;
            bool isStruct = false;
            bool isFp = false;
            int xmmAt = -1;
        };
        std::vector<PPlan> plans;
        int reg = 0;
        int xm = 0;
        int stack = 0;
        if (retStruct_ && retWidth_ > 16) {
            hiddenReg_ = 0;
            reg = 1;
        }
        if (d.type) {
            for (const Param& p : d.type->params) {
                PPlan plan;
                bool memClass = false;
                if (p.type && (p.type->kind == Type::Kind::Float || p.type->kind == Type::Kind::Double)) {
                    plan.isFp = true;
                    plan.size = sizeOf(p.type.get());
                    if (xm >= 8) {
                        plan.memory = true;
                        stack = alignUp(stack, 8);
                        plan.stackOff = stack;
                        stack += 8;
                    } else {
                        plan.xmmAt = xm++;
                    }
                    plans.push_back(plan);
                    continue;
                }
                plan.nreg = structPieces(p.type.get(), plan.size, memClass);
                plan.isStruct = p.type && (p.type->kind == Type::Kind::Struct || p.type->kind == Type::Kind::Union);
                if (memClass || reg + plan.nreg > 6) {
                    plan.memory = true;
                    plan.nreg = 0;
                    stack = alignUp(stack, 8);
                    plan.stackOff = stack;
                    stack += (plan.size + 7) & ~7;
                } else {
                    plan.regAt = reg;
                    reg += plan.nreg;
                }
                plans.push_back(plan);
            }
        }
        fn_.params = reg;
        fn_.vaNamed = d.type && d.type->variadic ? reg : -1;
        fn_.vaXmm = d.type && d.type->variadic ? xm : 0;
        nextReg_ = reg;
        startBlock("entry");
        if (fn_.vaNamed >= 0) {
            int save = fresh();
            Inst loc;
            loc.op = Inst::Op::Local;
            loc.dst = save;
            loc.a = Operand::number(176);
            emit(loc);
            vaSave_ = save;
        }
        if (d.type) {
            for (std::size_t i = 0; i < d.type->params.size(); ++i) {
                const Param& p = d.type->params[i];
                const PPlan& plan = plans[i];
                Sym s;
                if (p.type) describe(s, p.type.get());
                if (plan.isStruct) {
                    int addr = fresh();
                    if (plan.memory) {
                        Inst lea;
                        lea.op = Inst::Op::LeaFrame;
                        lea.dst = addr;
                        lea.a = Operand::number(16 + plan.stackOff);
                        emit(lea);
                    } else {
                        Inst loc;
                        loc.op = Inst::Op::Local;
                        loc.dst = addr;
                        loc.a = Operand::number(plan.size < 1 ? 1 : plan.size);
                        emit(loc);
                        storeBits(Operand::vreg(addr), Operand::vreg(plan.regAt),
                                  plan.size >= 8 ? 8 : plan.size);
                        if (plan.nreg == 2)
                            storeBits(addAddr(Operand::vreg(addr), 8), Operand::vreg(plan.regAt + 1),
                                      plan.size - 8);
                    }
                    s.reg = addr;
                    s.agg = true;
                } else if (plan.isFp && !plan.memory) {
                    s.reg = fresh();
                    s.fpw = plan.size;
                    s.width = plan.size;
                    if (static_cast<int>(fn_.xmmSlot.size()) <= plan.xmmAt) {
                        fn_.xmmSlot.resize(static_cast<std::size_t>(plan.xmmAt + 1), -1);
                        fn_.xmmWidth.resize(static_cast<std::size_t>(plan.xmmAt + 1), 8);
                    }
                    fn_.xmmSlot[static_cast<std::size_t>(plan.xmmAt)] = s.reg;
                    fn_.xmmWidth[static_cast<std::size_t>(plan.xmmAt)] = plan.size;
                } else if (plan.memory) {
                    int w = p.type ? sizeOf(p.type.get()) : 8;
                    bool uns = p.type && p.type->unsign;
                    Operand src = loadBits(frameAddr(16 + plan.stackOff), w);
                    int slot = fresh();
                    emitMov(slot, src, w, uns);
                    s.reg = slot;
                    if (!p.name.empty() && addr_.count(p.name)) s.mem = true;
                } else {
                    s.reg = plan.regAt;
                    if (!p.name.empty() && addr_.count(p.name)) s.mem = true;
                }
                if (s.boolean && !s.ref && s.reg >= 0) {
                    // A bool parameter is 0 or 1 ([conv.bool]); a bool& parameter is an address.
                    Operand n = asBool(Operand::vreg(s.reg));
                    emitMov(s.reg, n, 1, true);
                }
                if (!p.name.empty()) scopes_.back()[p.name] = s;
            }
        }
        if (d.body && d.body->kind == Stmt::Kind::Block) {
            for (auto& st : d.body->stmts)
                if (st) lowerStmt(*st);
        } else if (d.body) {
            lowerStmt(*d.body);
        }
        const auto& tail = fn_.blocks[static_cast<std::size_t>(cur_)].code;
        if (d.name == "main" && d.type && d.type->inner && d.type->inner->kind == Type::Kind::Int &&
            (tail.empty() || tail.back().op != Inst::Op::Ret)) {
            // Reaching the } of main returns 0 (C11 5.1.2.2.3, C++ [basic.start.main]/5).
            Stmt r;
            r.kind = Stmt::Kind::Return;
            r.line = d.line;
            r.col = d.col;
            r.expr = std::make_unique<Expr>();
            r.expr->kind = Expr::Kind::Int;
            r.expr->line = d.line;
            r.expr->col = d.col;
            r.expr->checked = std::make_unique<Type>();
            lowerStmt(r);
        }
        mod_.funcs.push_back(std::move(fn_));
    }
};

Module lowerProgram(const std::string& source, Diagnostics& diags, const std::string& fileName,
                     const std::vector<std::string>& includes) {
    MemoryFiles files;
    files.add(fileName, source);
    std::vector<Token> raw = preprocessTokens(files, fileName, includes, diags);
    if (!diags.ok()) return {};
    std::string file = "t.c";
    int line = 1;
    int col = 1;
    if (!raw.empty()) {
        line = raw.back().line;
        col = raw.back().column;
        if (!raw.back().file.empty()) file = raw.back().file;
    }
    std::vector<Token> toks;
    for (Token& t : raw) {
        if (t.kind != Tok::Newline) toks.push_back(std::move(t));
    }
    Token end;
    end.kind = Tok::End;
    end.file = std::move(file);
    end.line = line;
    end.column = col;
    toks.push_back(std::move(end));
    Tu tu = frontEnd(toks, diags);
    if (!diags.ok()) return {};
    Lowerer lowerer(diags);
    return lowerer.run(tu);
}


}  // namespace compiler

using namespace compiler;

Module lowerSource(const std::string& source, Diagnostics& diags, const std::string& file,
                   const std::vector<std::string>& includes) {
    Module m = lowerProgram(source, diags, file, includes);
    if (diags.ok()) optimizeModule(m);
    return m;
}

void setForceExceptions(bool on) { forceExceptions() = on; }

}  // namespace okaycc

