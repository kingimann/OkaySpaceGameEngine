#pragma once
// The x64 code generator: machine code, the frame, relocations and the unwind data of one object.
// Moved out of compile.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "compile_internal.hpp"

namespace okayms {
namespace compiler {
// The interlocked intrinsics compiled inline (gen_expr.cpp): op and width of a name, or false.
bool interlocked_intrinsic(const std::string& name, int* op, int* width);

struct Gen {
    std::vector<FuncDecl>& funcs;
    std::string error;
    std::vector<uint8_t> text;
    std::vector<uint8_t> rdata;
    std::vector<uint8_t> xdata;
    std::vector<uint8_t> pdata;
    struct NamedReloc {
        int section;
        uint32_t offset;
        std::string symbol;
        uint16_t type;
    };
    std::vector<NamedReloc> relocs;
    std::vector<Local> locals;
    std::vector<std::pair<std::string, uint32_t>> string_at;
    int temp_depth = 0;
    int temp_base = 0;
    int frame = 0x20;
    // First byte of the caller-made copies of big struct arguments.
    int struct_copy = 0;
    const std::vector<StructDef>* structs = nullptr;
    const std::vector<Global>* globals = nullptr;
    const std::vector<FuncType>* func_types = nullptr;
    const FuncDecl* current = nullptr;
    bool sret_dest_set = false;
    int sret_dest_off = 0;
    // The destination slot holds a pointer (the function's own hidden result pointer) instead of being the object.
    bool sret_dest_indirect = false;
    bool save_nv = false;
    bool optimize = false;
    // Offset and line (of the preprocessed text) for each statement of the
    // function being generated, when line records are wanted.
    bool debug_lines = false;
    std::vector<std::pair<uint32_t, int>> lines;
    // The line (of the preprocessed text) of the statement being generated, for error messages (Round 30).
    int stmt_line = 0;
    std::vector<uint8_t> extra_pops;
    std::map<std::string, size_t> labels;
    std::vector<std::pair<std::string, size_t>> gotos;
    std::vector<std::vector<size_t>> break_stack;
    std::vector<std::vector<size_t>> continue_stack;
    Type last_type{};
    int scope_depth = 0;
    std::vector<int> scope_parent{0};

    void push_scope() {
        const int id = static_cast<int>(scope_parent.size());
        scope_parent.push_back(scope_depth);
        scope_depth = id;
    }
    void pop_scope() { scope_depth = scope_parent[static_cast<size_t>(scope_depth)]; }
    bool scope_visible(int local_scope) const {
        int walk = scope_depth;
        while (walk != local_scope) {
            if (walk == 0) return false;
            walk = scope_parent[static_cast<size_t>(walk)];
        }
        return true;
    }

    explicit Gen(std::vector<FuncDecl>& functions) : funcs(functions) {}

    void b(uint8_t v) { text.push_back(v); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) text.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
    void disp(int32_t offset) { u32(static_cast<uint32_t>(offset)); }

    // The first function of this name (Round 33: through an index, built again when the list changed size, or once when
    // the entry it names no longer has the name; it was a scan of every function at each call, a third of a heavy file's
    // compile time).
    std::unordered_map<std::string, size_t> func_index_{};
    size_t func_index_size_ = static_cast<size_t>(-1);
    FuncDecl* find_func(const std::string& name) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (func_index_size_ != funcs.size() || attempt == 1) {
                func_index_.clear();
                for (size_t i = 0; i < funcs.size(); ++i) func_index_.emplace(funcs[i].name, i);
                func_index_size_ = funcs.size();
            }
            const auto it = func_index_.find(name);
            if (it == func_index_.end()) return nullptr;
            if (it->second < funcs.size() && funcs[it->second].name == name) return &funcs[it->second];
        }
        return nullptr;
    }
    // How many blocks out from the current one a local's block is, or -1 when it is not visible here.
    int scope_distance(int local_scope) const {
        int walk = scope_depth;
        int steps = 0;
        while (walk != local_scope) {
            if (walk == 0) return -1;
            walk = scope_parent[static_cast<size_t>(walk)];
            ++steps;
        }
        return steps;
    }
    // The local a name means here: of the visible locals of that name, the one of the innermost block (Round 29: a
    // local of an enclosing block declared later in the text no longer hides the inner block's own one; before, the
    // last declared visible local was taken). Within one block the last declared.
    // vis (Node::vis, Round 30): only locals with seq below it were declared when the name was read; one of those is
    // taken when there is one, so `int d; { use(d); int d; }` reads the outer d before the inner one is declared.
    Local* find_local(const std::string& name, int vis = -1) {
        Local* best = nullptr;
        int best_steps = -1;
        for (int pass = vis >= 0 ? 0 : 1; pass < 2 && best == nullptr; ++pass) {
            for (int i = static_cast<int>(locals.size()) - 1; i >= 0; --i) {
                Local& local = locals[static_cast<size_t>(i)];
                if (local.name != name) continue;
                if (pass == 0 && local.seq >= vis) continue;
                const int steps = scope_distance(local.scope);
                if (steps < 0) continue;
                if (best == nullptr || steps < best_steps) {
                    best = &local;
                    best_steps = steps;
                }
            }
        }
        return best;
    }
    Local* find_local(const Node& name) { return find_local(name.text, name.vis); }

    void load_slot(int32_t offset, bool pointer) {
        if (pointer) {
            b(0x48);
            b(0x8B);
        } else b(0x8B);
        b(0x85);
        disp(offset);
    }
    void store_slot(int32_t offset, bool pointer) {
        if (pointer) {
            b(0x48);
            b(0x89);
        } else b(0x89);
        b(0x85);
        disp(offset);
    }
    void mov_eax_imm(uint32_t value) {
        b(0xB8);
        u32(value);
    }
    // Room for spilled values below the locals (Round 30: 16 before; a call with 35 arguments, ThirdPersonController's
    // PlaceThirdPersonOrbit, needed more). emit.cpp sizes the frame with it.
    static constexpr int kTempSlots = 48;
    int spill() {
        if (temp_depth >= kTempSlots) {
            error = "expression is too deep";
            return 0;
        }
        const int off = temp_base - temp_depth * 8;
        ++temp_depth;
        if (is_fp_t(last_type)) {
            b(last_type.base == 6 ? 0xF2 : 0xF3);
            b(0x0F);
            b(0x11);
            b(0x85);
            disp(off);
            return off;
        }
        store_slot(off, true);
        return off;
    }
    // Spills the address in rax (the last value was an lvalue of any type, possibly a float).
    int spill_addr() {
        const Type keep = last_type;
        last_type = Type{};
        last_type.pointers = 1;
        const int off = spill();
        last_type = keep;
        return off;
    }
    void reload_r10(int off) {
        // The left operand of an integer operation lives in r10, so rcx can
        // hold a local. r10 is not given to a local.
        b(0x44);
        b(0x8B);
        b(0x95);
        disp(off);
        --temp_depth;
    }

    bool gen_expr(const Node& node);
    bool gen_interlocked(const Node& node, int op, int width);
    bool store_init(const Type& type, int offset, const Node& node);
    bool gen_stmt(const Node& node);
    // A statement that is not generated (a branch with a constant condition) still takes the scope numbers its blocks
    // would have taken, so the scopes of what follows keep the numbers the parser gave them.
    void skip_scopes(const Node& node) {
        const bool scoped = node.kind == Node::Block && node.text == "scope";
        if (scoped) push_scope();
        for (const Node& kid : node.kids) skip_scopes(kid);
        if (scoped) pop_scope();
    }
    bool gen_addr(const Node& node);
    int size_of(const Type& type) const;
    bool fits_register(const Type& type) const;
    bool bytes_copyable(int sid) const;
    Global* find_global(const std::string& name);
    // thread_local variables ([basic.stc.thread]), Microsoft x64 TLS: the address of `sym` in this thread's copy is
    // ThreadLocalStoragePointer[_tls_index] (the TEB's TLS array is at gs:[0x58]) plus sym's offset in the .tls section
    // (a SECREL relocation). rax gets the address; rcx is kept.
    bool is_tls(const std::string& sym) {
        const Global* g = find_global(sym);
        return g != nullptr && g->tls;
    }
    bool uses_tls = false;
    void tls_addr(const std::string& sym) {
        uses_tls = true;
        b(0x51);  // push rcx
        b(0x8B);  // mov ecx, [rip+_tls_index]
        b(0x0D);
        relocs.push_back({0, static_cast<uint32_t>(text.size()), "_tls_index", kRelAmd64Rel32});
        u32(0);
        const uint8_t gs_load[] = {0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00};  // mov rax, gs:[0x58]
        for (uint8_t x : gs_load) b(x);
        b(0x48);  // mov rax, [rax+rcx*8]
        b(0x8B);
        b(0x04);
        b(0xC8);
        b(0x59);  // pop rcx
        b(0x48);  // lea rax, [rax+disp32]
        b(0x8D);
        b(0x80);
        relocs.push_back({0, static_cast<uint32_t>(text.size()), sym, kRelAmd64Secrel});
        u32(0);
    }
    // The value of a thread_local variable of type t, read through its address.
    void tls_load(const std::string& sym, const Type& t) {
        tls_addr(sym);
        if (load_float_from_rax(t)) return;
        const int size = size_of(t);
        load_from_rax(t.pointer() || size >= 8 ? 8 : size, t.is_unsigned);
    }
    Type element_type(const Type& type) const;
    void load_from_rax(int size, bool is_unsigned);
    // A float or double at [rax] goes to xmm0 (where a floating value lives), anything else to eax/rax.
    bool load_float_from_rax(const Type& t) {
        if (t.pointers != 0 || t.array_len >= 0 || (t.base != 5 && t.base != 6)) return false;
        b(t.base == 6 ? 0xF2 : 0xF3);
        b(0x0F);
        b(0x10);
        b(0x00);
        return true;
    }
    void promote(Type* type) const;
    void load_typed(int32_t offset, const Type& type);
    void store_mem(int size);
    void mov_eax_from(int reg) {
        const int low = reg & 7;
        if (reg >= 8) b(0x44);
        b(0x89);
        b(static_cast<uint8_t>(0xC0 | (low << 3)));
    }
    void mov_reg_eax(int reg) {
        const int low = reg & 7;
        if (reg >= 8) b(0x41);
        b(0x89);
        b(static_cast<uint8_t>(0xC0 | low));
    }
    void mov_regs(int dest, int src) {
        uint8_t rex = 0;
        if (src >= 8) rex = static_cast<uint8_t>(rex | 0x44);
        if (dest >= 8) rex = static_cast<uint8_t>(rex | 0x41);
        if (rex != 0) b(rex);
        b(0x89);
        b(static_cast<uint8_t>(0xC0 | ((src & 7) << 3) | (dest & 7)));
    }
    static bool caller_saved(int reg) { return reg == 1 || reg == 2 || (reg >= 8 && reg <= 11); }
    void store_reg_local(int reg, int offset) {
        if (reg >= 8) b(0x44);
        b(0x89);
        b(static_cast<uint8_t>(0x85 | ((reg & 7) << 3)));
        disp(offset);
    }
    void load_reg_local(int reg, int offset) {
        if (reg >= 8) b(0x44);
        b(0x8B);
        b(static_cast<uint8_t>(0x85 | ((reg & 7) << 3)));
        disp(offset);
    }
    void spill_caller() {
        for (const Local& local : locals)
            if (caller_saved(local.reg)) store_reg_local(local.reg, local.offset);
    }
    void reload_caller() {
        for (const Local& local : locals)
            if (caller_saved(local.reg)) load_reg_local(local.reg, local.offset);
    }
    void spill_reg(int reg) {
        for (const Local& local : locals)
            if (local.reg == reg) store_reg_local(reg, local.offset);
    }
    void reload_reg(int reg) {
        for (const Local& local : locals)
            if (local.reg == reg) load_reg_local(reg, local.offset);
    }
    void epilogue() {
        b(0x48);
        b(0x89);
        b(0xEC);
        b(0x5D);
        for (uint8_t op : extra_pops) b(op);
        b(0xC3);
    }
    size_t emit_jcc(uint8_t cc) {
        b(0x0F);
        b(cc);
        const size_t at = text.size();
        u32(0);
        return at;
    }
    size_t emit_jmp() {
        b(0xE9);
        const size_t at = text.size();
        u32(0);
        return at;
    }
    void patch(size_t at, size_t target) {
        const int32_t rel = static_cast<int32_t>(target - (at + 4));
        for (int i = 0; i < 4; ++i) text[at + i] = static_cast<uint8_t>(static_cast<uint32_t>(rel) >> (8 * i));
    }
    // The operator an assignment form stands for (+= is +).
    static Tok plain_op(Tok op) {
        switch (op) {
        case TPlusEq: return TPlus;
        case TMinusEq: return TMinus;
        case TStarEq: return TStar;
        case TSlashEq: return TSlash;
        case TPercentEq: return TPercent;
        case TAndEq: return TAmp;
        case TOrEq: return TPipe;
        case TXorEq: return TCaret;
        case TShlEq: return TShl;
        case TShrEq: return TShr;
        default: return op;
        }
    }
    // rax = r10 op rax for + - * / % & | ^ << >>, 64 bits wide when W, else 32. The left operand is in r10
    // and the right in rax, as for every integer operator. `uns` picks unsigned division and remainder, and
    // for >> a logical (rather than arithmetic) shift. A shift count goes through cl, so rcx is saved around
    // it (a local may live there); the count is the low bits of the right operand.
    void emit_int_op(Tok raw, bool W, bool uns) {
        const Tok op = plain_op(raw);
        const uint8_t rex_reg = W ? 0x4C : 0x44;  // REX.R (+W): r10 in the reg field
        const uint8_t rex_rm = W ? 0x49 : 0x41;   // REX.B (+W): r10 in the r/m field
        if (op == TPlus) {
            b(rex_reg);
            b(0x01);
            b(0xD0);
        } else if (op == TMinus) {
            b(rex_rm);
            b(0x29);
            b(0xC2);
            b(rex_reg);
            b(0x89);
            b(0xD0);
        } else if (op == TStar) {
            b(rex_rm);
            b(0x0F);
            b(0xAF);
            b(0xC2);
        } else if (op == TSlash || op == TPercent) {
            spill_reg(2);
            b(rex_rm);  // xchg rax, r10: rax = the left operand, r10 = the divisor
            b(0x92);
            if (uns) {
                b(0x31);  // xor edx, edx
                b(0xD2);
            } else {
                if (W) b(0x48);
                b(0x99);  // cdq / cqo
            }
            b(rex_rm);
            b(0xF7);
            b(uns ? 0xF2 : 0xFA);  // div / idiv r10
            if (op == TPercent) {
                if (W) b(0x48);
                b(0x89);  // mov rax, rdx
                b(0xD0);
            }
            reload_reg(2);
        } else if (op == TAmp || op == TPipe || op == TCaret) {
            b(rex_reg);
            b(op == TAmp ? 0x21 : op == TPipe ? 0x09 : 0x31);
            b(0xD0);
        } else {  // TShl, TShr
            spill_reg(1);
            b(0x89);  // mov ecx, eax
            b(0xC1);
            b(rex_reg);  // mov rax, r10
            b(0x89);
            b(0xD0);
            if (W) b(0x48);
            b(0xD3);  // shl / shr / sar rax, cl
            b(op == TShl ? 0xE0 : uns ? 0xE8 : 0xF8);
            reload_reg(1);
        }
    }
    // The floating-point binary operators. The left operand was spilled at `off` with type lt, the right is
    // in xmm0 or rax with type last_type. Both go to the wider floating type; a comparison gives an int.
    bool fp_binary(Tok op, const Type& lt, int off) {
        const Type rt = last_type;
        Type ct;
        ct.base = ((is_fp_t(lt) && lt.base == 6) || (is_fp_t(rt) && rt.base == 6)) ? 6 : 5;
        const bool use_double = ct.base == 6;
        const bool cmp = op == TEq || op == TNe || op == TLt || op == TGt || op == TLe || op == TGe;
        if (!cmp && op != TPlus && op != TMinus && op != TStar && op != TSlash) {
            error = "operator is not supported";
            return false;
        }
        convert(rt, ct);
        b(use_double ? 0xF2 : 0xF3);  // movss/movsd xmm1, xmm0
        b(0x0F);
        b(0x10);
        b(0xC8);
        if (is_fp_t(lt)) {
            b(lt.base == 6 ? 0xF2 : 0xF3);
            b(0x0F);
            b(0x10);
            b(0x85);
            disp(off);
        } else {
            b(0x48);
            b(0x8B);
            b(0x85);
            disp(off);
        }
        --temp_depth;
        convert(lt, ct);
        if (cmp) {
            if (use_double) b(0x66);
            b(0x0F);
            b(0x2E);
            b(0xC1);
            uint8_t setcc = 0x94;
            if (op == TNe) setcc = 0x95;
            else if (op == TLt) setcc = 0x92;
            else if (op == TGt) setcc = 0x97;
            else if (op == TLe) setcc = 0x96;
            else if (op == TGe) setcc = 0x93;
            b(0x0F);
            b(setcc);
            b(0xC0);
            // An unordered compare (a NaN) sets ZF, PF and CF: == < <= must be false and != true ([expr.eq],
            // [expr.rel], IEEE-754 unordered).
            if (op == TEq || op == TLt || op == TLe || op == TNe) {
                b(0x41);  // setnp r10b / setp r10b
                b(0x0F);
                b(op == TNe ? 0x9A : 0x9B);
                b(0xC2);
                b(0x44);  // and / or al, r10b
                b(op == TNe ? 0x08 : 0x20);
                b(0xD0);
            }
            b(0x0F);
            b(0xB6);
            b(0xC0);
            last_type = Type{};
            last_type.base = 1;
        } else {
            b(use_double ? 0xF2 : 0xF3);
            b(0x0F);
            b(op == TMinus ? 0x5C : op == TStar ? 0x59 : op == TSlash ? 0x5E : 0x58);
            b(0xC1);
            last_type = ct;
        }
        return true;
    }
    static bool bit_op(Tok op) {
        return op == TAmp || op == TPipe || op == TCaret || op == TShl || op == TShr || op == TAndEq || op == TOrEq ||
               op == TXorEq || op == TShlEq || op == TShrEq;
    }

    // eax = (eax != 0): the value a conversion to bool gives ([conv.bool]).
    void normalize_bool() {
        b(0x85);  // test eax, eax
        b(0xC0);
        b(0x0F);  // setne al
        b(0x95);
        b(0xC0);
        b(0x0F);  // movzx eax, al
        b(0xB6);
        b(0xC0);
    }
    static bool is_bool_type(const Type& t) { return t.is_bool && t.pointers == 0 && !t.is_ref && !t.is_array(); }
    // What a value in a register is. A float or double lives in xmm0; a long long, a pointer or an array
    // address lives in all of rax; anything else in eax.
    static bool is_fp_t(const Type& t) {
        return t.pointers == 0 && !t.is_array() && !t.is_ref && (t.base == 5 || t.base == 6);
    }
    static bool is_wide_t(const Type& t) { return t.pointers == 0 && !t.is_array() && !t.is_ref && t.base == 1 && t.wide; }
    static bool is_ptr_t(const Type& t) { return (t.pointers > 0 || t.is_array()) && !t.is_ref; }
    static bool v64(const Type& t) { return is_wide_t(t) || is_ptr_t(t); }
    static bool is_int_t(const Type& t) {
        return t.pointers == 0 && !t.is_array() && !t.is_ref && (t.base == 1 || t.base == 2 || t.base == 4);
    }
    // The type of an arithmetic operation on two integer or pointer values (the usual arithmetic
    // conversions, [expr.arith.conv]): the 64-bit operand wins, unsigned wins over signed of the same width.
    static Type common_int(const Type& a, const Type& b) {
        Type r;
        r.base = 1;
        const bool wa = v64(a);
        const bool wb = v64(b);
        const bool ua = is_ptr_t(a) || a.is_unsigned;
        const bool ub = is_ptr_t(b) || b.is_unsigned;
        if (wa || wb) {
            r.wide = true;
            r.is_unsigned = (wa && wb) ? (ua || ub) : (wa ? ua : ub);
        } else {
            r.is_unsigned = ua || ub;
        }
        return r;
    }
    // rax = the 64-bit value of the int in eax (sign-extended, or zero-extended when it is unsigned).
    void ext_rax(bool uns) {
        if (uns) {
            b(0x89);
            b(0xC0);
        } else {
            b(0x48);
            b(0x63);
            b(0xC0);
        }
    }
    void ext_r10(bool uns) {
        if (uns) {
            b(0x45);
            b(0x89);
            b(0xD2);
        } else {
            b(0x4D);
            b(0x63);
            b(0xD2);
        }
    }
    void reload_r10_64(int off) {
        b(0x4C);
        b(0x8B);
        b(0x95);
        disp(off);
        --temp_depth;
    }
    // Sets the flags so that jz is taken when the value in last_type is zero (a condition).
    void test_cond() {
        if (is_fp_t(last_type)) {
            // A NaN is true in a condition ([conv.bool]: it is not equal to zero): cmpneqss gives all ones when the
            // value is unordered or not equal to zero, and the result is tested as an integer.
            b(0x0F);  // xorps xmm1, xmm1
            b(0x57);
            b(0xC9);
            b(last_type.base == 6 ? 0xF2 : 0xF3);  // cmpneqss / cmpneqsd xmm0, xmm1
            b(0x0F);
            b(0xC2);
            b(0xC1);
            b(0x04);
            if (last_type.base == 6) {  // movq rax, xmm0 ; test rax, rax
                b(0x66);
                b(0x48);
                b(0x0F);
                b(0x7E);
                b(0xC0);
                b(0x48);
            } else {  // movd eax, xmm0 ; test eax, eax
                b(0x66);
                b(0x0F);
                b(0x7E);
                b(0xC0);
            }
            b(0x85);
            b(0xC0);
            return;
        }
        if (v64(last_type)) b(0x48);
        b(0x85);
        b(0xC0);
    }
    void normalize_bool64() {
        b(0x48);
        b(0x85);
        b(0xC0);
        b(0x0F);
        b(0x95);
        b(0xC0);
        b(0x0F);
        b(0xB6);
        b(0xC0);
    }
    // Converts the value in rax or xmm0 from type `from` to type `to` ([conv.integral], [conv.fpint],
    // [conv.double], [conv.bool]). Only r10 is used besides rax and xmm0.
    void convert(const Type& from, const Type& to) {
        const bool from_fp = is_fp_t(from);
        const bool to_fp = is_fp_t(to);
        if (is_bool_type(to) && !from_fp) {
            if (v64(from)) normalize_bool64();
            else normalize_bool();
            return;
        }
        if (from_fp && to_fp) {
            if (from.base != to.base) {
                b(to.base == 6 ? 0xF3 : 0xF2);
                b(0x0F);
                b(0x5A);
                b(0xC0);
            }
            return;
        }
        if (!from_fp && to_fp) {
            if (!is_int_t(from)) return;
            const uint8_t prefix = to.base == 6 ? 0xF2 : 0xF3;
            if (is_wide_t(from) && from.is_unsigned) {
                // Above 2^63 the value is halved (keeping the low bit so rounding stays right), converted
                // and doubled.
                b(0x48);  // test rax, rax
                b(0x85);
                b(0xC0);
                const size_t js = emit_jcc(0x88);
                b(prefix);
                b(0x48);
                b(0x0F);
                b(0x2A);
                b(0xC0);
                const size_t jmp = emit_jmp();
                patch(js, text.size());
                b(0x49);  // mov r10, rax
                b(0x89);
                b(0xC2);
                b(0x48);  // shr rax, 1
                b(0xD1);
                b(0xE8);
                b(0x41);  // and r10d, 1
                b(0x83);
                b(0xE2);
                b(0x01);
                b(0x4C);  // or rax, r10
                b(0x09);
                b(0xD0);
                b(prefix);
                b(0x48);
                b(0x0F);
                b(0x2A);
                b(0xC0);
                b(prefix);  // addss / addsd xmm0, xmm0
                b(0x0F);
                b(0x58);
                b(0xC0);
                patch(jmp, text.size());
                return;
            }
            if (is_wide_t(from)) {
                b(prefix);
                b(0x48);
                b(0x0F);
                b(0x2A);
                b(0xC0);
                return;
            }
            if (from.is_unsigned) {
                b(0x89);  // mov eax, eax: an unsigned int is converted as a 64-bit value
                b(0xC0);
                b(prefix);
                b(0x48);
                b(0x0F);
                b(0x2A);
                b(0xC0);
                return;
            }
            b(prefix);
            b(0x0F);
            b(0x2A);
            b(0xC0);
            return;
        }
        if (from_fp && !to_fp) {
            if (!is_int_t(to)) return;
            const bool w = is_wide_t(to) || (to.base == 1 && to.is_unsigned);
            b(from.base == 6 ? 0xF2 : 0xF3);
            if (w) b(0x48);
            b(0x0F);
            b(0x2C);
            b(0xC0);
            if (is_bool_type(to)) normalize_bool();
            return;
        }
        if (is_wide_t(to) && is_int_t(from) && !is_wide_t(from)) ext_rax(from.is_unsigned);
    }

    void cmp_set(uint8_t setcc) {
        b(0x41);
        b(0x39);
        b(0xC2);
        b(0x0F);
        b(setcc);
        b(0xC0);
        b(0x0F);
        b(0xB6);
        b(0xC0);
    }
};


}  // namespace compiler
}  // namespace okayms
