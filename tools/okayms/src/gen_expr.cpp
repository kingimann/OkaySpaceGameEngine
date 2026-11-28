// The x64 code generator: expressions.
// Moved out of compile.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "compile_internal.hpp"
#include "gen.hpp"

namespace okayms {
namespace compiler {
// The Microsoft interlocked intrinsics (Microsoft's documentation of _InterlockedExchange, _InterlockedExchangeAdd,
// _InterlockedCompareExchange, _InterlockedIncrement and _InterlockedDecrement, with the 8, 16 and 64 suffixes): op 1
// exchange, 2 exchange-add, 3 compare-exchange, 4 increment, 5 decrement; width in bytes (no suffix: a 32-bit long).
// They are compiled to the x64 instructions with a LOCK prefix (xchg with memory is locked by itself), as the Intel and
// AMD manuals describe XCHG, XADD and CMPXCHG, so they are atomic between threads and are full barriers.
bool interlocked_intrinsic(const std::string& name, int* op, int* width) {
    static const char* const kBase[] = {"_InterlockedExchangeAdd", "_InterlockedCompareExchange", "_InterlockedExchange",
                                        "_InterlockedIncrement", "_InterlockedDecrement"};
    static const int kOp[] = {2, 3, 1, 4, 5};
    for (int k = 0; k < 5; ++k) {
        const std::string base = kBase[k];
        if (name.compare(0, base.size(), base) != 0) continue;
        const std::string suffix = name.substr(base.size());
        int w = 0;
        if (suffix.empty()) w = 4;
        else if (suffix == "8" && kOp[k] <= 3) w = 1;
        else if (suffix == "16") w = 2;
        else if (suffix == "64") w = 8;
        else continue;
        *op = kOp[k];
        *width = w;
        return true;
    }
    return false;
}

bool Gen::gen_interlocked(const Node& node, int op, int width) {
    // The size prefixes of an instruction on [r10] with a register operand: 66 for 16 bits, REX.W for 64; REX.B for r10.
    auto prefix = [&](bool reg_r11) {
        if (width == 2) b(0x66);
        uint8_t rex = 0x41;  // B: r10
        if (width == 8) rex |= 0x08;
        if (reg_r11) rex |= 0x04;  // R: r11
        b(rex);
    };
    // A value operand is converted to the operand's width first (an int -1 given to a 64-bit form is -1 in 64 bits).
    Type operand;
    operand.base = 1;
    operand.wide = width == 8;
    if (!gen_expr(node.kids[0])) return false;  // the address
    const int ptr_slot = spill();
    if (op == 3) {
        if (!gen_expr(node.kids[1])) return false;  // the exchange value
        convert(last_type, operand);
        last_type = operand;
        const int xchg_slot = spill();
        if (!gen_expr(node.kids[2])) return false;  // the comparand, in the accumulator
        convert(last_type, operand);
        b(0x41);  // push r11 (it may hold a local)
        b(0x53);
        b(0x4C);  // mov r11, [rbp+slot]
        b(0x8B);
        b(0x9D);
        disp(xchg_slot);
        --temp_depth;
        b(0x4C);  // mov r10, [rbp+slot]: the whole 64-bit address (reload_r10 reads 32 bits)
        b(0x8B);
        b(0x95);
        disp(ptr_slot);
        --temp_depth;
        b(0xF0);  // lock cmpxchg [r10], r11
        prefix(true);
        b(0x0F);
        b(width == 1 ? 0xB0 : 0xB1);
        b(0x1A);
        b(0x41);  // pop r11
        b(0x5B);
    } else {
        if (op >= 4) {
            if (width == 8) {
                b(0x48);  // mov rax, imm32 (sign-extended)
                b(0xC7);
                b(0xC0);
                u32(op == 4 ? 1u : 0xFFFFFFFFu);
            } else {
                mov_eax_imm(op == 4 ? 1u : 0xFFFFFFFFu);
            }
        } else {
            if (!gen_expr(node.kids[1])) return false;
            convert(last_type, operand);
        }
        b(0x4C);  // mov r10, [rbp+slot]: the whole 64-bit address (reload_r10 reads 32 bits)
        b(0x8B);
        b(0x95);
        disp(ptr_slot);
        --temp_depth;
        if (op == 1) {  // xchg [r10], rax
            prefix(false);
            b(width == 1 ? 0x86 : 0x87);
            b(0x02);
        } else {  // lock xadd [r10], rax
            b(0xF0);
            prefix(false);
            b(0x0F);
            b(width == 1 ? 0xC0 : 0xC1);
            b(0x02);
        }
        if (op >= 4) {  // increment and decrement give the new value
            if (width == 2) b(0x66);
            if (width == 8) b(0x48);
            b(0xFF);
            b(op == 4 ? 0xC0 : 0xC8);
        }
    }
    // The result is the old value (the new one for increment and decrement), sign-extended like the char, short and long
    // the intrinsics return.
    if (width == 1) {
        b(0x0F);  // movsx eax, al
        b(0xBE);
        b(0xC0);
    } else if (width == 2) {
        b(0x0F);  // movsx eax, ax
        b(0xBF);
        b(0xC0);
    }
    last_type = Type{};
    last_type.base = 1;
    last_type.wide = width == 8;
    return true;
}

bool Gen::gen_expr(const Node& node) {
    if (!error.empty()) return false;
    if (node.kind == Node::IntLit) {
        if (node.type.base == 5 || node.type.base == 6) {
            b(0x48);
            b(0xB8);
            for (int i = 0; i < 8; ++i) b(static_cast<uint8_t>(node.number >> (8 * i)));
            b(0x66);
            if (node.type.base == 6) b(0x48);
            b(0x0F);
            b(0x6E);
            b(0xC0);
            last_type = Type{};
            last_type.base = node.type.base;
            return true;
        }
        last_type = Type{};
        last_type.base = 1;
        last_type.is_unsigned = node.type.is_unsigned;
        if (node.type.wide) {
            b(0x48);  // mov rax, imm64
            b(0xB8);
            for (int i = 0; i < 8; ++i) b(static_cast<uint8_t>(node.number >> (8 * i)));
            last_type.wide = true;
            return true;
        }
        mov_eax_imm(static_cast<uint32_t>(node.number));
        return true;
    }
    if (node.kind == Node::StringLit) {
        const uint32_t at = static_cast<uint32_t>(rdata.size());
        rdata.insert(rdata.end(), node.text.begin(), node.text.end());
        rdata.push_back(0);
        const std::string sym = "str_" + std::to_string(string_at.size());
        string_at.push_back({sym, at});
        b(0x48);
        b(0x8D);
        b(0x05);
        relocs.push_back({0, static_cast<uint32_t>(text.size()), sym, kRelAmd64Rel32});
        u32(0);
        last_type = Type{};
        last_type.pointers = 1;
        last_type.base = 2;
        if (node.type.pointers == 1 && (node.type.base == 4 || node.type.base == 1)) last_type = node.type;  // L"..." and U"..."
        return true;
    }
    if (node.kind == Node::Name) {
        if (Local* local = find_local(node)) {
            if (local->type.is_ref) {
                load_slot(local->offset, true);
                Type referred = local->type;
                referred.is_ref = false;
                const int size = std::max(1, size_of(referred));
                last_type = referred;
                if (load_float_from_rax(referred)) return true;
                load_from_rax(size >= 8 ? 8 : size, referred.is_unsigned);
                promote(&last_type);
                return true;
            }
            if (local->reg != 0) {
                mov_eax_from(local->reg);
                last_type = local->type;
                promote(&last_type);
                return true;
            }
            if (local->is_static && !local->type.is_array() && is_tls(local->symbol)) {
                tls_load(local->symbol, local->type);
                last_type = local->type;
                if (!is_fp_t(local->type)) promote(&last_type);
                return true;
            }
            if (local->is_static) {
                if (local->type.is_array()) {  // a static array used as a value is the address of its first element
                    if (!gen_addr(node)) return false;
                    last_type = element_type(local->type);
                    last_type.pointers += 1;
                    return true;
                }
                const int size = size_of(local->type);
                const bool uns = local->type.is_unsigned;
                if (is_fp_t(local->type)) {
                    b(local->type.base == 6 ? 0xF2 : 0xF3);  // movss/movsd xmm0, [rip+symbol]
                    b(0x0F);
                    b(0x10);
                    b(0x05);
                    relocs.push_back({0, static_cast<uint32_t>(text.size()), local->symbol, kRelAmd64Rel32});
                    u32(0);
                    last_type = local->type;
                    return true;
                }
                if (local->type.pointer() || size >= 8) {
                    b(0x48);
                    b(0x8B);
                    b(0x05);
                } else if (size == 4) {
                    b(0x8B);
                    b(0x05);
                } else if (size == 2) {
                    b(0x0F);
                    b(uns ? 0xB7 : 0xBF);
                    b(0x05);
                } else {
                    b(0x0F);
                    b(uns ? 0xB6 : 0xBE);
                    b(0x05);
                }
                relocs.push_back({0, static_cast<uint32_t>(text.size()), local->symbol, kRelAmd64Rel32});
                u32(0);
                last_type = local->type;
                promote(&last_type);
                return true;
            }
            if (local->type.is_array()) {
                if (!gen_addr(node)) return false;
                last_type = element_type(local->type);
                last_type.pointers += 1;
                return true;
            }
            load_typed(local->offset, local->type);
            last_type = local->type;
            promote(&last_type);
            return true;
        }
        if (Global* global = find_global(node.text)) {
            if (global->type.is_array()) {
                if (!gen_addr(node)) return false;
                last_type = element_type(global->type);
                last_type.pointers += 1;
                return true;
            }
            if (global->tls) {
                tls_load(global->name, global->type);
                last_type = global->type;
                if (!is_fp_t(global->type)) promote(&last_type);
                return true;
            }
            const int size = size_of(global->type);
            const bool uns = global->type.is_unsigned;
            if (is_fp_t(global->type)) {
                b(global->type.base == 6 ? 0xF2 : 0xF3);  // movss/movsd xmm0, [rip+symbol]
                b(0x0F);
                b(0x10);
                b(0x05);
                relocs.push_back({0, static_cast<uint32_t>(text.size()), global->name, kRelAmd64Rel32});
                u32(0);
                last_type = global->type;
                return true;
            }
            if (global->type.pointer() || size >= 8) {
                b(0x48);
                b(0x8B);
                b(0x05);
            } else if (size == 4) {
                b(0x8B);
                b(0x05);
            } else if (size == 2) {
                b(0x0F);
                b(uns ? 0xB7 : 0xBF);
                b(0x05);
            } else {
                b(0x0F);
                b(uns ? 0xB6 : 0xBE);
                b(0x05);
            }
            relocs.push_back({0, static_cast<uint32_t>(text.size()), global->name, kRelAmd64Rel32});
            u32(0);
            last_type = global->type;
            promote(&last_type);
            return true;
        }
        if (FuncDecl* fn = find_func(node.text)) {
            b(0x48);
            b(0x8D);
            b(0x05);
            relocs.push_back({0, static_cast<uint32_t>(text.size()), fn->symbol.empty() ? fn->name : fn->symbol, kRelAmd64Rel32});
            u32(0);
            last_type = Type{};
            last_type.pointers = 1;
            last_type.func_id = fn->func_id;
            return true;
        }
        error = "unknown name " + node.text;
        return false;
    }
    if (node.kind == Node::Index || node.kind == Node::Field) {
        if (!gen_addr(node)) return false;
        if (last_type.is_array()) {
            // An array member (or a row of a 2-D array) used as a value is the address of its first element.
            last_type = element_type(last_type);
            last_type.pointers += 1;
            return true;
        }
        const int size = std::max(1, size_of(last_type));
        const bool uns = last_type.is_unsigned;
        const int bits = last_type.bits;
        const int bit_off = last_type.bit_off;
        if (load_float_from_rax(last_type)) return true;
        load_from_rax(size, uns || bits >= 0);
        if (bits >= 0) {
            if (bit_off != 0) {
                b(0xC1);
                b(0xE8);
                b(static_cast<uint8_t>(bit_off));
            }
            const uint32_t mask = bits >= 32 ? 0xFFFFFFFFu : (1u << bits) - 1u;
            b(0x25);
            u32(mask);
        }
        promote(&last_type);
        return true;
    }
    if (node.kind == Node::Ternary) {
        if (!gen_expr(node.kids[0])) return false;
        test_cond();
        const size_t jz = emit_jcc(0x84);
        if (!gen_expr(node.kids[1])) return false;
        const Type first = last_type;
        const size_t jmp = emit_jmp();
        patch(jz, text.size());
        if (!gen_expr(node.kids[2])) return false;
        const Type second = last_type;
        // The two branches have one type after the usual arithmetic conversions ([expr.cond]); the
        // first branch is converted by a stub placed after the second.
        Type unified = first;
        std::vector<uint8_t> stub;
        if (is_fp_t(first) || is_fp_t(second)) {
            if (is_fp_t(first) && is_fp_t(second)) unified.base = first.base == 6 || second.base == 6 ? 6 : 5;
            else {
                unified = Type{};
                unified.base = (is_fp_t(first) ? first.base : second.base);
            }
        } else if (is_ptr_t(second) && is_int_t(first)) {
            unified = second;
        } else if (is_int_t(first) && is_int_t(second)) {
            unified = common_int(first, second);
            if (!is_wide_t(unified) && first.base == second.base && first.base != 1) unified = first;
        }
        if (is_int_t(first) || is_fp_t(first)) {
            std::vector<uint8_t> keep;
            keep.swap(text);
            convert(first, unified);
            stub.swap(text);
            text.swap(keep);
        }
        convert(second, unified);
        if (stub.empty()) {
            patch(jmp, text.size());
        } else {
            const size_t jend = emit_jmp();
            patch(jmp, text.size());
            text.insert(text.end(), stub.begin(), stub.end());
            patch(jend, text.size());
        }
        last_type = unified;
        return true;
    }
    if (node.kind == Node::Unary) {
        if (node.op == TEnd && !node.kids.empty()) {
            if (!gen_expr(node.kids[0])) return false;
            const Type from = last_type;
            const Type to = node.type;
            convert(from, to);
            if (is_int_t(to) && !is_bool_type(to) && (to.base == 2 || to.base == 4) && !is_fp_t(from)) {
                // The value is cut to the width of the type and extended again (movsx / movzx eax, al or ax).
                b(0x0F);
                b(to.base == 2 ? (to.is_unsigned ? 0xB6 : 0xBE) : (to.is_unsigned ? 0xB7 : 0xBF));
                b(0xC0);
            }
            last_type = to;
            if (is_int_t(to)) promote(&last_type);
            return true;
        }
        if (node.op == TPlusPlus || node.op == TMinusMinus) {
            if (!gen_addr(node.kids[0])) return false;
            const Type lvalue_type = last_type;
            const bool pointer = last_type.pointers > 0;
            const int step = pointer ? std::max(1, size_of(element_type(last_type))) : 1;
            const int width = pointer ? 8 : std::max(1, size_of(last_type));
            const bool w64 = pointer || is_wide_t(lvalue_type);
            const int addr = spill_addr();
            if (!error.empty()) return false;
            if (is_fp_t(lvalue_type)) {
                // f++ and f-- on a float or double: one is added to or taken from the value in memory.
                const bool dbl = lvalue_type.base == 6;
                b(0x48);  // mov rax, [addr]
                b(0x8B);
                b(0x85);
                disp(addr);
                b(dbl ? 0xF2 : 0xF3);  // movss/movsd xmm0, [rax]
                b(0x0F);
                b(0x10);
                b(0x00);
                int old_slot = 0;
                if (node.number != 0) {
                    last_type = lvalue_type;
                    old_slot = spill();
                    if (!error.empty()) return false;
                }
                b(0x48);  // mov rax, 1.0
                b(0xB8);
                const uint64_t one = dbl ? 0x3FF0000000000000ull : 0x3F800000ull;
                for (int i = 0; i < 8; ++i) b(static_cast<uint8_t>(one >> (8 * i)));
                b(0x66);  // movd/movq xmm1, eax/rax
                if (dbl) b(0x48);
                b(0x0F);
                b(0x6E);
                b(0xC8);
                b(dbl ? 0xF2 : 0xF3);  // addss/subss xmm0, xmm1
                b(0x0F);
                b(node.op == TPlusPlus ? 0x58 : 0x5C);
                b(0xC1);
                b(0x48);  // mov rcx, [addr]
                b(0x8B);
                b(0x8D);
                disp(addr);
                b(dbl ? 0xF2 : 0xF3);  // movss/movsd [rcx], xmm0
                b(0x0F);
                b(0x11);
                b(0x01);
                if (node.number != 0) {
                    b(dbl ? 0xF2 : 0xF3);
                    b(0x0F);
                    b(0x10);
                    b(0x85);
                    disp(old_slot);
                    --temp_depth;
                }
                --temp_depth;
                last_type = lvalue_type;
                return true;
            }
            b(0x48);
            b(0x8B);
            b(0x85);
            disp(addr);
            if (width >= 8) {
                b(0x48);
                b(0x8B);
                b(0x00);
            } else if (width == 1) {
                b(0x0F);
                b(0xBE);
                b(0x00);
            } else if (width == 2) {
                b(0x0F);
                b(0xBF);
                b(0x00);
            } else {
                b(0x8B);
                b(0x00);
            }
            int old = 0;
            if (node.number != 0) {
                old = spill();
                if (!error.empty()) return false;
            }
            if (w64) b(0x48);
            b(0x83);
            b(node.op == TPlusPlus ? 0xC0 : 0xE8);
            b(static_cast<uint8_t>(step));
            b(0x48);
            b(0x8B);
            b(0x8D);
            disp(addr);
            store_mem(width);
            if (node.number != 0) {
                load_slot(old, true);
                --temp_depth;
            }
            --temp_depth;
            last_type = lvalue_type;
            promote(&last_type);
            return true;
        }
        if (node.op == TAmp) {
            if (!gen_addr(node.kids[0])) return false;
            Type pointed = last_type;
            last_type = Type{};
            last_type.pointers = 1;
            last_type.base = pointed.base;
            last_type.struct_id = pointed.struct_id;
            if (pointed.is_array()) {
                last_type = element_type(pointed);
                last_type.pointers += 1;
            }
            return true;
        }
        if (!gen_expr(node.kids[0])) return false;
        if (node.op == TMinus) {
            if (last_type.pointers == 0 && (last_type.base == 5 || last_type.base == 6)) {
                const bool dbl = last_type.base == 6;
                b(0x48);
                b(0xB8);
                const uint64_t sign = dbl ? 0x8000000000000000ull : 0x80000000ull;
                for (int i = 0; i < 8; ++i) b(static_cast<uint8_t>(sign >> (8 * i)));
                b(0x66);
                if (dbl) b(0x48);
                b(0x0F);
                b(0x6E);
                b(0xC8);
                if (dbl) b(0x66);
                b(0x0F);
                b(0x57);
                b(0xC1);
                return true;
            }
            if (is_wide_t(last_type)) b(0x48);
            b(0xF7);
            b(0xD8);
            return true;
        }
        if (node.op == TTilde) {
            if (last_type.pointers > 0 || last_type.base == 3 || last_type.base == 5 || last_type.base == 6) {
                error = "operator ~ needs an integer";
                return false;
            }
            if (is_wide_t(last_type)) b(0x48);
            b(0xF7);  // not eax
            b(0xD0);
            promote(&last_type);
            return true;
        }
        if (node.op == TNot) {
            test_cond();
            b(0x0F);
            b(0x94);
            b(0xC0);
            b(0x0F);
            b(0xB6);
            b(0xC0);
            last_type = Type{};
            return true;
        }
        if (node.op == TStar) {
            const Type elem = element_type(last_type);
            last_type = elem;
            if (load_float_from_rax(elem)) return true;
            load_from_rax(std::max(1, size_of(elem)), elem.is_unsigned);
            promote(&last_type);
            return true;
        }
        error = "bad unary operator";
        return false;
    }
    if (node.kind == Node::Assign) {
        if (node.kids.size() == 2 && node.kids[1].kind == Node::Block && node.kids[1].text == "init" &&
            node.kids[0].kind == Node::Name) {
            if (Local* local = find_local(node.kids[0])) return store_init(local->type, local->offset, node.kids[1]);
            error = "initializer is not a local";
            return false;
        }
        if (node.kids[0].kind == Node::Name && (node.op == TAssign || node.op == TEnd)) {
            if (Local* local = find_local(node.kids[0])) {
                if (local->type.is_ref && node.number == 1) {
                    if (!gen_addr(node.kids[1])) return false;
                    const int addr = spill_addr();
                    if (!error.empty()) return false;
                    b(0x48);
                    b(0x8B);
                    b(0x85);
                    disp(addr);
                    b(0x48);
                    b(0x89);
                    b(0x85);
                    disp(local->offset);
                    --temp_depth;
                    last_type = local->type;
                    last_type.is_ref = false;
                    return true;
                }
                if (local->reg != 0 && !is_bool_type(local->type)) {
                    if (!gen_expr(node.kids[1])) return false;
                    convert(last_type, local->type);
                    // `unsigned char c = p[i];` kept in a register: the value is cut to the local's width, as a store to
                    // its slot does (Round 34; before, 0xFF read as -1 stayed -1 at /O1 and /O2: silent wrong sums).
                    const int width = slot_width(*local);
                    if (width < 4) extend_reg(0, width, local->type.is_unsigned);
                    mov_reg_eax(local->reg);
                    return true;
                }
            }
        }
        if (node.number != 1 && node.kids[0].kind == Node::Name) {
            if (Local* local = find_local(node.kids[0])) {
                if (local->is_const) {
                    error = "assignment to const " + local->name;
                    return false;
                }
            }
            if (Global* global = find_global(node.kids[0].text)) {
                if (global->type.is_const && global->type.pointers == 0) {
                    error = "assignment to const " + global->name;
                    return false;
                }
            }
        }
        if (is_compound_op(node.op)) {
            if (!gen_addr(node.kids[0])) return false;
            const Type lt = last_type;
            const int width = std::max(1, size_of(lt));
            const int addr = spill_addr();
            if (!error.empty()) return false;
            b(0x48);
            b(0x8B);
            b(0x85);
            disp(addr);
            if (is_fp_t(lt)) {
                b(lt.base == 6 ? 0xF2 : 0xF3);
                b(0x0F);
                b(0x10);
                b(0x00);
            } else {
                load_from_rax(width >= 8 ? 8 : width, lt.is_unsigned);
            }
            Type value_type = lt;
            value_type.bits = -1;
            last_type = value_type;
            const int left = spill();
            if (!error.empty()) return false;
            if (!gen_expr(node.kids[1])) return false;
            const Type rt = last_type;
            const Tok op = plain_op(node.op);
            if (is_fp_t(lt) || is_fp_t(rt)) {
                if (!fp_binary(op, lt, left)) return false;
                convert(last_type, lt);
            } else {
                const bool shift = op == TShl || op == TShr;
                const bool lptr = is_ptr_t(lt);
                Type ct = lt;
                bool W;
                if (lptr) {
                    // p += n scales n by the size of the element ([expr.add]).
                    if (!v64(rt)) ext_rax(rt.is_unsigned);
                    b(0x48);
                    b(0x69);
                    b(0xC0);
                    u32(static_cast<uint32_t>(std::max(1, size_of(element_type(lt)))));
                    W = true;
                } else if (shift) {
                    W = is_wide_t(lt);
                    ct = lt;
                    if (!W) ct.wide = false;
                } else {
                    ct = common_int(lt, rt);
                    W = ct.wide;
                    if (W && !v64(rt)) ext_rax(rt.is_unsigned);
                }
                if (W) reload_r10_64(left);
                else reload_r10(left);
                if (W && !v64(lt)) ext_r10(lt.is_unsigned);
                emit_int_op(op, W, shift ? lt.is_unsigned : ct.is_unsigned);
                if (is_bool_type(lt)) {
                    if (W) normalize_bool64();
                    else normalize_bool();
                }
            }
            b(0x48);
            b(0x8B);
            b(0x8D);
            disp(addr);
            if (is_fp_t(lt)) {
                b(lt.base == 6 ? 0xF2 : 0xF3);
                b(0x0F);
                b(0x11);
                b(0x01);
            } else {
                store_mem(width);
            }
            --temp_depth;
            last_type = value_type;
            promote(&last_type);
            return true;
        }
        if (node.kids[0].kind == Node::Name) {
            Local* local = find_local(node.kids[0]);
            if (local != nullptr && !local->is_static) {
                const bool big = local->type.base == 3 && local->type.pointers == 0 && !fits_register(local->type);
                if (big && node.kids[1].kind == Node::Call) {
                    sret_dest_set = true;
                    sret_dest_off = local->offset;
                    if (!gen_expr(node.kids[1])) return false;
                    last_type = local->type;
                    return true;
                }
                if (big) {
                    if (!gen_addr(node.kids[1])) return false;
                    b(0x48);
                    b(0x89);
                    b(0xC6);
                    b(0x48);
                    b(0x8D);
                    b(0xBD);
                    disp(local->offset);
                    b(0xB9);
                    u32(static_cast<uint32_t>(size_of(local->type)));
                    b(0xF3);
                    b(0xA4);
                    last_type = local->type;
                    return true;
                }
            }
        }
        if (node.kids[0].kind == Node::Field || node.kids[0].kind == Node::Index ||
            (node.kids[0].kind == Node::Unary && node.kids[0].op == TStar) ||
            (node.kids[0].kind == Node::Name && find_local(node.kids[0]) != nullptr &&
             (find_local(node.kids[0])->type.is_ref || find_local(node.kids[0])->is_static)) ||
            // a static local or a global (static C x = f(); C g = f();): written through its address, not a stack slot
            (node.kids[0].kind == Node::Name && find_local(node.kids[0]) == nullptr &&
             find_global(node.kids[0].text) != nullptr)) {
            // A big struct stored into a member, an element or through a pointer: the bytes are copied, or the
            // callee writes the result straight there. The address is worked out first; when the target turns
            // out to be something else, the code is dropped and the general path below runs.
            const size_t mark_text = text.size();
            const size_t mark_relocs = relocs.size();
            const int mark_depth = temp_depth;
            if (!gen_addr(node.kids[0])) return false;
            const Type target = last_type;
            // An array member copied whole (the implicit copy constructor's `this->f = o.f` for `bool f[3]`; Round 33):
            // its bytes, as for a big struct. Before, nothing useful was stored (silent wrong copies).
            const bool array_copy = target.is_array() && target.pointers == 0 && !target.is_ref && node.number == 1 &&
                                    node.kids[1].kind != Node::Call;
            if (array_copy || (target.base == 3 && target.pointers == 0 && !target.is_array() && !fits_register(target))) {
                const int dest = spill_addr();
                if (!error.empty()) return false;
                if (node.kids[1].kind == Node::Call) {
                    sret_dest_set = true;
                    sret_dest_off = dest;
                    sret_dest_indirect = true;
                    if (!gen_expr(node.kids[1])) return false;
                } else {
                    if (!gen_addr(node.kids[1])) return false;
                    spill_reg(6);
                    spill_reg(7);
                    b(0x48);  // mov rsi, rax
                    b(0x89);
                    b(0xC6);
                    b(0x48);  // mov rdi, [rbp+dest]
                    b(0x8B);
                    b(0xBD);
                    disp(dest);
                    b(0xB9);
                    u32(static_cast<uint32_t>(size_of(target)));
                    b(0xF3);
                    b(0xA4);
                    reload_reg(6);
                    reload_reg(7);
                }
                --temp_depth;
                last_type = target;
                return true;
            }
            text.resize(mark_text);
            relocs.resize(mark_relocs);
            temp_depth = mark_depth;
        }
        if (!gen_expr(node.kids[1])) return false;
        const Type rhs_type = last_type;
        const bool rhs_fp = is_fp_t(rhs_type);
        const int slot = spill();
        if (!error.empty()) return false;
        if (!gen_addr(node.kids[0])) return false;
        const Type lt = last_type;
        b(0x48);  // mov rcx, rax: the address
        b(0x89);
        b(0xC1);
        if (rhs_fp) {
            b(rhs_type.base == 6 ? 0xF2 : 0xF3);
            b(0x0F);
            b(0x10);
            b(0x85);
            disp(slot);
        } else {
            b(0x48);
            b(0x8B);
            b(0x85);
            disp(slot);
        }
        --temp_depth;
        convert(rhs_type, lt);
        if (is_fp_t(lt)) {
            b(lt.base == 6 ? 0xF2 : 0xF3);
            b(0x0F);
            b(0x11);
            b(0x01);
            last_type = lt;
            return true;
        }
        const int size = std::max(1, size_of(lt));
        if (lt.bits >= 0) {
            const int bits = lt.bits;
            const int bit_off = lt.bit_off;
            const uint32_t mask = bits >= 32 ? 0xFFFFFFFFu : (1u << static_cast<unsigned>(bits)) - 1u;
            b(0x89);
            b(0xC2);
            b(0x81);
            b(0xE2);
            u32(mask);
            if (bit_off != 0) {
                b(0xC1);
                b(0xE2);
                b(static_cast<uint8_t>(bit_off));
            }
            b(0x8B);
            b(0x01);
            b(0x25);
            u32(~(mask << bit_off));
            b(0x09);
            b(0xD0);
            b(0x89);
            b(0x01);
        } else {
            store_mem(size);
        }
        last_type = lt;
        last_type.bits = -1;
        promote(&last_type);
        return true;
    }
    if (node.kind == Node::Call) {
        int lk_op = 0;
        int lk_width = 0;
        if (interlocked_intrinsic(node.text, &lk_op, &lk_width) &&
            node.kids.size() == static_cast<size_t>(lk_op == 3 ? 3 : (lk_op >= 4 ? 1 : 2)))
            return gen_interlocked(node, lk_op, lk_width);
        if (node.text == "__okay_setjmp" && node.kids.size() == 1) {
            // The registers a restore must bring back go to the buffer: rbx rbp rdi rsi r12 to r15
            // rsp and the address after this sequence (the Windows x64 callee-saved set). The
            // sequence is entered again by __okay_longjmp with eax = 1.
            if (!gen_expr(node.kids[0])) return false;
            static const uint8_t save[] = {0x48, 0x89, 0xC1,                          // mov rcx, rax
                                           0x48, 0x89, 0x19,                          // mov [rcx], rbx
                                           0x48, 0x89, 0x69, 0x08,                    // mov [rcx+8], rbp
                                           0x48, 0x89, 0x79, 0x10,                    // mov [rcx+16], rdi
                                           0x48, 0x89, 0x71, 0x18,                    // mov [rcx+24], rsi
                                           0x4C, 0x89, 0x61, 0x20,                    // mov [rcx+32], r12
                                           0x4C, 0x89, 0x69, 0x28,                    // mov [rcx+40], r13
                                           0x4C, 0x89, 0x71, 0x30,                    // mov [rcx+48], r14
                                           0x4C, 0x89, 0x79, 0x38,                    // mov [rcx+56], r15
                                           0x48, 0x89, 0x61, 0x40,                    // mov [rcx+64], rsp
                                           0x48, 0x8D, 0x05, 0x06, 0x00, 0x00, 0x00,  // lea rax, [rip+6]
                                           0x48, 0x89, 0x41, 0x48,                    // mov [rcx+72], rax
                                           0x31, 0xC0};                               // xor eax, eax
            for (uint8_t c : save) b(c);
            last_type = Type{};
            last_type.base = 1;
            return true;
        }
        if (node.text == "__okay_longjmp" && node.kids.size() == 1) {
            if (!gen_expr(node.kids[0])) return false;
            static const uint8_t restore[] = {0x48, 0x89, 0xC1,                    // mov rcx, rax
                                              0xB8, 0x01, 0x00, 0x00, 0x00,        // mov eax, 1
                                              0x48, 0x8B, 0x19,                    // mov rbx, [rcx]
                                              0x48, 0x8B, 0x69, 0x08,              // mov rbp, [rcx+8]
                                              0x48, 0x8B, 0x79, 0x10,              // mov rdi, [rcx+16]
                                              0x48, 0x8B, 0x71, 0x18,              // mov rsi, [rcx+24]
                                              0x4C, 0x8B, 0x61, 0x20,              // mov r12, [rcx+32]
                                              0x4C, 0x8B, 0x69, 0x28,              // mov r13, [rcx+40]
                                              0x4C, 0x8B, 0x71, 0x30,              // mov r14, [rcx+48]
                                              0x4C, 0x8B, 0x79, 0x38,              // mov r15, [rcx+56]
                                              0x48, 0x8B, 0x61, 0x40,              // mov rsp, [rcx+64]
                                              0xFF, 0x61, 0x48};                   // jmp [rcx+72]
            for (uint8_t c : restore) b(c);
            last_type = Type{};
            return true;
        }
        if (node.text == "va_end") {
            mov_eax_imm(0);
            return true;
        }
        if (node.text == "va_start") {
            if (current == nullptr || node.kids.size() != 2 || node.kids[1].kind != Node::Name) {
                error = "va_start needs the last named parameter";
                return false;
            }
            int index = -1;
            for (int i = 0; i < current->nparams; ++i) {
                if (current->param_names[static_cast<size_t>(i)] == node.kids[1].text) index = i;
            }
            if (index < 0) {
                error = "va_start parameter was not found";
                return false;
            }
            const int slot = index + (current->sret ? 1 : 0);
            const int home = 16 + (slot + 1) * 8;
            if (!gen_addr(node.kids[0])) return false;
            b(0x48);
            b(0x89);
            b(0xC1);
            b(0x48);
            b(0x8D);
            b(0x85);
            disp(home);
            b(0x48);
            b(0x89);
            b(0x01);
            return true;
        }
        if (node.text == "va_arg") {
            if (node.kids.size() != 1 || node.kids[0].kind != Node::Name) {
                error = "va_arg list must be a name";
                return false;
            }
            Local* ap = find_local(node.kids[0]);
            if (ap == nullptr) {
                error = "va_arg list must be a local";
                return false;
            }
            load_typed(ap->offset, ap->type);
            const int size = std::max(1, size_of(node.type));
            b(0x48);
            b(0x89);
            b(0xC1);
            load_from_rax(size >= 8 ? 8 : size, node.type.is_unsigned || node.type.pointer());
            // The bits are in rax. A previous va_arg may have left last_type as
            // float or double, and spill would then store the old xmm0.
            last_type = Type{};
            last_type.pointers = 1;
            const int slot = spill();
            if (!error.empty()) return false;
            b(0x48);
            b(0x83);
            b(0x85);
            disp(ap->offset);
            b(0x08);
            load_slot(slot, true);
            --temp_depth;
            if (node.type.pointers == 0 && (node.type.base == 5 || node.type.base == 6)) {
                b(0x66);
                if (node.type.base == 6) b(0x48);
                b(0x0F);
                b(0x6E);
                b(0xC0);
            }
            last_type = node.type;
            promote(&last_type);
            return true;
        }
        Local* fp_local = node.text.empty() ? nullptr : find_local(node);
        if (fp_local != nullptr && fp_local->type.func_id < 0) fp_local = nullptr;
        // A global (or namespace-scope) variable that holds a function pointer, called by its name ([expr.call] 1;
        // Round 33, BehaviorTreeRunner.cpp's `g_pauseHook(*this, nodeId)`): its value is the target. Before, "unknown
        // function".
        Global* fp_global = nullptr;
        if (fp_local == nullptr && !node.text.empty() && find_func(node.text) == nullptr) {
            fp_global = find_global(node.text);
            if (fp_global != nullptr && !(fp_global->type.func_id >= 0 && fp_global->type.pointers == 1 &&
                                          !fp_global->type.is_array()))
                fp_global = nullptr;
        }
        const bool indirect = node.text.empty() || fp_local != nullptr || fp_global != nullptr;
        FuncDecl* fn = nullptr;
        FuncType sig;
        int callee_slot = 0;
        const int depth_before = temp_depth;
        size_t arg0 = 0;
        if (fp_local != nullptr) {
            load_slot(fp_local->offset, true);
            if (fp_local->type.is_ref) {
                // A reference to a function pointer (Fn& f, or F&& f bound to a lambda without captures): the slot holds
                // the pointer's address; the pointer itself is read from there (Round 30: the call went to the address
                // of the pointer, a crash).
                b(0x48);
                b(0x8B);
                b(0x00);  // mov rax, [rax]
            }
            last_type = fp_local->type;
            if (func_types == nullptr || static_cast<size_t>(fp_local->type.func_id) >= func_types->size()) {
                error = "call target is not a function";
                return false;
            }
            sig = (*func_types)[static_cast<size_t>(fp_local->type.func_id)];
            callee_slot = spill();
            if (!error.empty()) return false;
        } else if (fp_global != nullptr) {
            Node name;
            name.kind = Node::Name;
            name.text = node.text;
            if (!gen_expr(name)) return false;
            if (func_types == nullptr || static_cast<size_t>(fp_global->type.func_id) >= func_types->size()) {
                error = "call target is not a function";
                return false;
            }
            sig = (*func_types)[static_cast<size_t>(fp_global->type.func_id)];
            callee_slot = spill();
            if (!error.empty()) return false;
        } else if (indirect) {
            if (node.kids.empty() || !gen_expr(node.kids[0])) return false;
            if (last_type.func_id < 0 || func_types == nullptr ||
                static_cast<size_t>(last_type.func_id) >= func_types->size()) {
                error = "call target is not a function";
                return false;
            }
            sig = (*func_types)[static_cast<size_t>(last_type.func_id)];
            callee_slot = spill();
            if (!error.empty()) return false;
            arg0 = 1;
        } else {
            fn = find_func(node.text);
            if (!fn) {
                error = "unknown function " + node.text;
                return false;
            }
            sig.ret = fn->ret;
            sig.params = fn->params;
            sig.vararg = fn->vararg;
        }
        // A reference parameter of a pointer's signature is passed as its address (the parser took it), and a reference
        // result comes back as one.
        if (sig.ret.is_ref) {
            sig.ret.is_ref = false;
            sig.ret.rref = false;
            sig.ret.pointers += 1;
        }
        for (Type& param : sig.params)
            if (param.is_ref) {
                param.is_ref = false;
                param.rref = false;
                param.pointers += 1;
            }
        const size_t nargs = node.kids.size() - arg0;
        if (!sig.vararg && nargs != sig.params.size()) {
            error = "wrong number of arguments to " + (node.text.empty() ? std::string("function pointer") : node.text);
            return false;
        }
        if (sig.vararg && nargs < sig.params.size()) {
            error = "too few arguments to " + node.text;
            return false;
        }
        const bool sret = sig.ret.base == 3 && sig.ret.pointers == 0 && !fits_register(sig.ret);
        int hidden = 0;
        bool hidden_ptr = false;
        if (sret) {
            if (sret_dest_set) {
                hidden = sret_dest_off;
                hidden_ptr = sret_dest_indirect;
                sret_dest_set = false;
                sret_dest_indirect = false;
            } else if (current != nullptr && current->sret) {
                hidden = current->sret_off;
                hidden_ptr = true;
            } else {
                error = "large struct return has no destination";
                return false;
            }
        }
        std::vector<int> saved;
        std::vector<Type> saved_types;
        for (size_t arg_i = 0; arg_i < nargs; ++arg_i) {
            const bool named = arg_i < sig.params.size();
            const Type param = named ? sig.params[arg_i] : Type{};
            if (named && param.base == 3 && param.pointers == 0 && !fits_register(param)) {
                if (!gen_addr(node.kids[arg0 + arg_i])) return false;
            } else if (named && param.base == 3 && param.pointers == 0) {
                if (!gen_addr(node.kids[arg0 + arg_i])) return false;
                b(0x48);
                b(0x8B);
                b(0x00);
            } else if (!gen_expr(node.kids[arg0 + arg_i])) {
                return false;
            } else if (named) {
                convert(last_type, param);
                // The argument now has the parameter's type (a float parameter is passed in an xmm register).
                if (param.pointers == 0 && !param.is_array() && (param.base == 5 || param.base == 6)) last_type = param;
                else if (is_fp_t(last_type) && !is_fp_t(param) && param.pointers == 0 && param.base != 3) {
                    last_type = param;
                    last_type.is_ref = false;
                }
            } else if (is_fp_t(last_type) && last_type.base == 5) {
                Type promoted;  // a float passed through ... becomes a double ([expr.call])
                promoted.base = 6;
                convert(last_type, promoted);
                last_type = promoted;
            }
            saved_types.push_back(last_type);
            saved.push_back(spill());
            if (!error.empty()) return false;
        }
        const int reg_base = sret ? 1 : 0;
        for (int i = static_cast<int>(saved.size()) - 1; i >= 0; --i) {
            const int reg = i + reg_base;
            if (reg < 4) continue;
            b(0x48);
            b(0x8B);
            b(0x85);
            disp(saved[static_cast<size_t>(i)]);
            b(0x48);
            b(0x89);
            b(0x84);
            b(0x24);
            u32(static_cast<uint32_t>(32 + (reg - 4) * 8));
        }
        auto load_reg = [&](int reg, int spill_off) {
            if (reg == 0) {
                b(0x48);
                b(0x8B);
                b(0x8D);
            } else if (reg == 1) {
                b(0x48);
                b(0x8B);
                b(0x95);
            } else if (reg == 2) {
                b(0x4C);
                b(0x8B);
                b(0x85);
            } else if (reg == 3) {
                b(0x4C);
                b(0x8B);
                b(0x8D);
            } else {
                return;
            }
            disp(spill_off);
        };
        spill_caller();
        // rep movsb in the struct-argument copies below uses rsi and rdi, so
        // a local kept there is stored first and loaded back after the copies.
        spill_reg(6);
        spill_reg(7);
        int copy_at = struct_copy;
        for (size_t arg_i = 0; arg_i < nargs; ++arg_i) {
            const bool named = arg_i < sig.params.size();
            const Type param = named ? sig.params[arg_i] : Type{};
            if (!(named && param.base == 3 && param.pointers == 0 && !fits_register(param))) continue;
            const int bytes = std::max(1, size_of(param));
            const int step = (bytes + 7) & ~7;
            b(0x48);
            b(0x8B);
            b(0x85);
            disp(saved[arg_i]);
            b(0x48);
            b(0x89);
            b(0xC6);
            b(0x48);
            b(0x8D);
            b(0xBD);
            disp(copy_at);
            b(0xB9);
            u32(static_cast<uint32_t>(bytes));
            b(0xF3);
            b(0xA4);
            b(0x48);
            b(0x8D);
            b(0x85);
            disp(copy_at);
            b(0x48);
            b(0x89);
            b(0x85);
            disp(saved[arg_i]);
            copy_at += step;
        }
        reload_reg(6);
        reload_reg(7);
        if (sret) {
            b(0x48);
            b(hidden_ptr ? 0x8B : 0x8D);
            b(0x8D);
            disp(hidden);
        }
        for (int i = 0; i < static_cast<int>(saved.size()); ++i) {
            const int reg = i + reg_base;
            const Type& at = saved_types[static_cast<size_t>(i)];
            const bool fp = at.pointers == 0 && (at.base == 5 || at.base == 6);
            if (fp && reg >= 0 && reg < 4) {
                const uint8_t modrm = reg == 0 ? 0x85 : reg == 1 ? 0x8D : reg == 2 ? 0x95 : 0x9D;
                b(at.base == 6 ? 0xF2 : 0xF3);
                b(0x0F);
                b(0x10);
                b(modrm);
                disp(saved[static_cast<size_t>(i)]);
            } else {
                load_reg(reg, saved[static_cast<size_t>(i)]);
            }
        }
        if (sig.vararg) {
            for (int i = 0; i < static_cast<int>(saved.size()); ++i) {
                const int reg = i + reg_base;
                const Type& at = saved_types[static_cast<size_t>(i)];
                if (at.pointers != 0 || (at.base != 5 && at.base != 6) || reg < 0 || reg >= 4) continue;
                b(0x66);
                // movq r64, xmm. REX.B selects r8/r9; REX.R would select xmm8+ instead.
                b(reg >= 2 ? 0x49 : 0x48);
                b(0x0F);
                b(0x7E);
                const uint8_t modrm = reg == 0 ? 0xC1 : reg == 1 ? 0xCA : reg == 2 ? 0xD0 : 0xD9;
                b(modrm);
            }
        }
        if (sig.vararg) {
            // The caller of a varargs function writes the register arguments
            // into the 32-byte home area so the callee can walk them.
            b(0x48);
            b(0x89);
            b(0x0C);
            b(0x24);
            b(0x48);
            b(0x89);
            b(0x54);
            b(0x24);
            b(0x08);
            b(0x4C);
            b(0x89);
            b(0x44);
            b(0x24);
            b(0x10);
            b(0x4C);
            b(0x89);
            b(0x4C);
            b(0x24);
            b(0x18);
        }
        temp_depth = depth_before;
        if (indirect) {
            b(0x48);
            b(0x8B);
            b(0x85);
            disp(callee_slot);
            b(0xFF);
            b(0xD0);
        } else if (fn->imported) {
            b(0xFF);
            b(0x15);
            // The import slot of the symbol the declaration links to: an extern "C" function that is overloaded here
            // (two declarations with other parameter types) still has its plain C name.
            relocs.push_back({0, static_cast<uint32_t>(text.size()), "__imp_" + (fn->symbol.empty() ? fn->name : fn->symbol), kRelAmd64Rel32});
            u32(0);
        } else {
            b(0xE8);
            relocs.push_back({0, static_cast<uint32_t>(text.size()), fn->symbol.empty() ? fn->name : fn->symbol, kRelAmd64Rel32});
            u32(0);
        }
        reload_caller();
        last_type = sig.ret;
        if (sret) {
            b(0x48);
            b(hidden_ptr ? 0x8B : 0x8D);
            b(0x85);
            disp(hidden);
        } else {
            promote(&last_type);
        }
        return true;
    }
    if (node.kind == Node::Binary) {
        if (node.op == TAndAnd || node.op == TOrOr) {
            if (!gen_expr(node.kids[0])) return false;
            test_cond();
            const size_t j1 = emit_jcc(node.op == TAndAnd ? 0x84 : 0x85);
            if (!gen_expr(node.kids[1])) return false;
            test_cond();
            const size_t j2 = emit_jcc(node.op == TAndAnd ? 0x84 : 0x85);
            mov_eax_imm(node.op == TAndAnd ? 1 : 0);
            const size_t jmp = emit_jmp();
            const size_t other = text.size();
            mov_eax_imm(node.op == TAndAnd ? 0 : 1);
            const size_t end = text.size();
            patch(j1, other);
            patch(j2, other);
            patch(jmp, end);
            last_type = Type{};
            last_type.base = 1;
            return true;
        }
        if (!gen_expr(node.kids[0])) return false;
        const Type left_type = last_type;
        const int off = spill();
        if (!error.empty()) return false;
        if (!gen_expr(node.kids[1])) return false;
        if (node.op == TComma) {
            // a, b ([expr.comma]): the value and type of b, whatever the types (Round 31: `++x, w0 += dw0` with
            // doubles in SoftwareRenderer.hpp:302 went to the floating-point operators and was refused).
            --temp_depth;
            return true;
        }
        if ((left_type.pointers > 0 || left_type.is_array()) && node.op == TPlus) {
            const int scale = std::max(1, size_of(element_type(left_type)));
            b(0x48);
            b(0x69);
            b(0xC0);
            u32(static_cast<uint32_t>(scale));
            b(0x48);
            b(0x03);
            b(0x85);
            disp(off);
            --temp_depth;
            last_type = left_type;
            if (last_type.is_array()) {
                last_type = element_type(last_type);
                last_type.pointers += 1;
                last_type.array_len = -1;
            }
            return true;
        }
        if ((left_type.pointers > 0 || left_type.is_array()) && node.op == TMinus && last_type.pointers == 0 &&
            !last_type.is_array()) {
            // pointer - integer: the count is scaled by the element size and the result is a
            // 64-bit pointer (ISO/IEC 14882 [expr.add]).
            const int scale = std::max(1, size_of(element_type(left_type)));
            b(0x48);
            b(0x69);
            b(0xC0);
            u32(static_cast<uint32_t>(scale));
            b(0x49);  // mov r10, rax
            b(0x89);
            b(0xC2);
            b(0x48);  // mov rax, [rbp+off]
            b(0x8B);
            b(0x85);
            disp(off);
            b(0x4C);  // sub rax, r10
            b(0x29);
            b(0xD0);
            --temp_depth;
            last_type = left_type;
            if (last_type.is_array()) {
                last_type = element_type(last_type);
                last_type.pointers += 1;
                last_type.array_len = -1;
            }
            return true;
        }
        const bool left_fp = is_fp_t(left_type);
        const bool right_fp = is_fp_t(last_type);
        if (left_fp || right_fp) return fp_binary(node.op, left_type, off);
        const Type right_type = last_type;
        const bool shift = node.op == TShl || node.op == TShr;
        const bool left_ptr = is_ptr_t(left_type);
        const bool right_ptr = is_ptr_t(right_type);
        if (shift || node.op == TAmp || node.op == TPipe || node.op == TCaret) {
            if (left_ptr || right_ptr || left_type.base == 3 || right_type.base == 3) {
                error = "a bit operator needs integers";
                return false;
            }
        }
        const Type ct = shift ? left_type : common_int(left_type, right_type);
        const bool W = shift ? is_wide_t(left_type) : (v64(left_type) || v64(right_type));
        if (W && !shift && !v64(right_type)) ext_rax(right_type.is_unsigned);
        if (W) reload_r10_64(off);
        else reload_r10(off);
        if (W && !v64(left_type)) ext_r10(left_type.is_unsigned);
        const bool cmp = node.op == TEq || node.op == TNe || node.op == TLt || node.op == TGt || node.op == TLe ||
                         node.op == TGe;
        if (cmp) {
            const bool unsign = left_ptr || right_ptr || ct.is_unsigned;
            uint8_t setcc = 0x94;
            if (node.op == TEq) setcc = 0x94;
            else if (node.op == TNe) setcc = 0x95;
            else if (node.op == TLt) setcc = unsign ? 0x92 : 0x9C;
            else if (node.op == TGt) setcc = unsign ? 0x97 : 0x9F;
            else if (node.op == TLe) setcc = unsign ? 0x96 : 0x9E;
            else setcc = unsign ? 0x93 : 0x9D;
            if (W) b(0x49);
            else b(0x41);
            b(0x39);
            b(0xC2);
            b(0x0F);
            b(setcc);
            b(0xC0);
            b(0x0F);
            b(0xB6);
            b(0xC0);
            last_type = Type{};
            last_type.base = 1;
            return true;
        }
        if (node.op == TPlus && !left_ptr && right_ptr) {
            // n + p: n is scaled by the size of the element.
            b(0x4D);  // imul r10, r10, scale
            b(0x69);
            b(0xD2);
            u32(static_cast<uint32_t>(std::max(1, size_of(element_type(right_type)))));
        }
        if (node.op == TMinus && left_ptr && right_ptr) {
            // pointer - pointer: the byte difference divided by the element size, a 64-bit count.
            emit_int_op(TMinus, true, false);
            const int scale = std::max(1, size_of(element_type(left_type)));
            if (scale > 1) {
                spill_reg(2);
                b(0x49);  // mov r10, scale
                b(0xC7);
                b(0xC2);
                u32(static_cast<uint32_t>(scale));
                b(0x48);  // cqo
                b(0x99);
                b(0x49);  // idiv r10
                b(0xF7);
                b(0xFA);
                reload_reg(2);
            }
            last_type = Type{};
            last_type.base = 1;
            last_type.wide = true;
            return true;
        }
        if (node.op != TPlus && node.op != TMinus && node.op != TStar && node.op != TSlash && node.op != TPercent &&
            !bit_op(node.op)) {
            error = "operator is not supported";
            return false;
        }
        emit_int_op(node.op, W, shift ? left_type.is_unsigned : ct.is_unsigned);
        if (node.op == TPlus && !left_ptr && right_ptr) last_type = right_type;
        else if ((node.op == TPlus || node.op == TMinus) && left_ptr) last_type = left_type;
        else {
            last_type = ct;
            if (!shift) last_type.bits = -1;
        }
        return true;
    }
    error = "bad expression";
    return false;
}


}  // namespace compiler
}  // namespace okayms
