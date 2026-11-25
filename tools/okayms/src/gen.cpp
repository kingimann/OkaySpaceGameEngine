// The x64 code generator: sizes, addresses, loads, stores, initializers and statements.
// Moved out of compile.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "compile_internal.hpp"
#include "gen.hpp"

namespace okayms {
namespace compiler {
int Gen::size_of(const Type& type) const {
    if (type.is_ref) return 8;
    if (type.array_len >= 0) return type.array_len * size_of(peel_array(type));
    if (type.pointers > 0) return 8;
    if (type.base == 2) return 1;
    if (type.base == 4) return 2;
    if (type.base == 5) return 4;
    if (type.base == 6) return 8;
    if (type.base == 3 && structs != nullptr && type.struct_id >= 0 &&
        type.struct_id < static_cast<int>(structs->size()))
        return (*structs)[type.struct_id].size;
    if (type.base == 0) return 0;
    return type.wide ? 8 : 4;
}

bool Gen::fits_register(const Type& type) const {
    if (type.array_len >= 0 || type.pointers > 0) return true;
    const int size = size_of(type);
    return size == 1 || size == 2 || size == 4 || size == 8;
}

Global* Gen::find_global(const std::string& name) {
    if (globals == nullptr) return nullptr;
    for (Global& global : *const_cast<std::vector<Global>*>(globals))
        if (global.name == name) return &global;
    return nullptr;
}

Type Gen::element_type(const Type& type) const { return peel_array(type); }

bool Gen::gen_addr(const Node& node) {
    if (node.kind == Node::Name) {
        if (Local* local = find_local(node.text)) {
            if (local->type.is_ref) {
                load_slot(local->offset, true);
                last_type = local->type;
                last_type.is_ref = false;
                return true;
            }
            if (local->is_static && is_tls(local->symbol)) {
                tls_addr(local->symbol);
                last_type = local->type;
                return true;
            }
            if (local->is_static) {
                b(0x48);
                b(0x8D);
                b(0x05);
                relocs.push_back({0, static_cast<uint32_t>(text.size()), local->symbol, kRelAmd64Rel32});
                u32(0);
                last_type = local->type;
                return true;
            }
            b(0x48);
            b(0x8D);
            b(0x85);
            disp(local->offset);
            last_type = local->type;
            return true;
        }
        if (Global* global = find_global(node.text)) {
            if (global->tls) {
                tls_addr(global->name);
                last_type = global->type;
                return true;
            }
            b(0x48);
            b(0x8D);
            b(0x05);
            relocs.push_back({0, static_cast<uint32_t>(text.size()), global->name, kRelAmd64Rel32});
            u32(0);
            last_type = global->type;
            return true;
        }
        error = "unknown name " + node.text;
        return false;
    }
    if (node.kind == Node::StringLit) {
        if (!gen_expr(node)) return false;
        last_type.pointers = 1;
        last_type.array_len = -1;
        return true;
    }
    if (node.kind == Node::Index) {
        bool from_pointer = false;
        if (node.kids[0].kind == Node::Name) {
            if (Local* local = find_local(node.kids[0].text))
                from_pointer = local->type.pointers > 0 && !local->type.is_array();
            else if (Global* global = find_global(node.kids[0].text))
                from_pointer = global->type.pointers > 0 && !global->type.is_array();
        }
        const Node& inner = node.kids[0];
        const bool lvalue = inner.kind == Node::Name || inner.kind == Node::Field || inner.kind == Node::Index ||
                            (inner.kind == Node::Unary && inner.op == TStar);
        if (from_pointer || !lvalue) {
            if (!gen_expr(inner)) return false;
        } else {
            if (!gen_addr(inner)) return false;
            if (last_type.pointers > 0 && !last_type.is_array()) {
                // p[i] where p is a pointer field or a pointer element: the pointer is read first.
                b(0x48);
                b(0x8B);
                b(0x00);
            }
        }
        const Type base = last_type;
        const Type elem = element_type(base);
        const int elem_size = std::max(1, size_of(elem));
        const int slot = spill();
        if (!error.empty()) return false;
        if (!gen_expr(node.kids[1])) return false;
        if (!v64(last_type) && !is_fp_t(last_type)) ext_rax(last_type.is_unsigned);
        b(0x48);
        b(0x69);
        b(0xC0);
        u32(static_cast<uint32_t>(elem_size));
        b(0x48);
        b(0x03);
        b(0x85);
        disp(slot);
        --temp_depth;
        last_type = elem;
        return true;
    }
    if (node.kind == Node::Field) {
        if (node.op == TArrow) {
            if (!gen_expr(node.kids[0])) return false;
            if (last_type.pointers < 1) {
                error = "arrow needs a pointer";
                return false;
            }
            last_type = element_type(last_type);
        } else if (!gen_addr(node.kids[0])) {
            return false;
        }
        if (last_type.base != 3 || structs == nullptr || last_type.struct_id < 0) {
            error = "field access on a non-struct";
            return false;
        }
        const StructDef& def = (*structs)[last_type.struct_id];
        const Field* field = nullptr;
        for (const Field& item : def.fields)
            if (item.name == node.text) field = &item;
        if (field == nullptr) {
            error = "unknown field " + node.text;
            return false;
        }
        if (field->offset != 0) {
            b(0x48);
            b(0x05);
            u32(static_cast<uint32_t>(field->offset));
        }
        last_type = field->type;
        return true;
    }
    if (node.kind == Node::Unary && node.op == TStar) {
        if (!gen_expr(node.kids[0])) return false;
        last_type = element_type(last_type);
        return true;
    }
    if (node.kind == Node::Ternary) {
        // c ? a : b as an lvalue (both are lvalues of one type): the address of the chosen one.
        if (!gen_expr(node.kids[0])) return false;
        test_cond();
        const size_t jz = emit_jcc(0x84);
        if (!gen_addr(node.kids[1])) return false;
        const Type first = last_type;
        const size_t jmp = emit_jmp();
        patch(jz, text.size());
        if (!gen_addr(node.kids[2])) return false;
        patch(jmp, text.size());
        last_type = first;
        return true;
    }
    if (node.kind == Node::Assign && node.op == TAssign && node.kids.size() == 2 && !has_effects(node.kids[0])) {
        // a = b as an lvalue ([expr.ass] 1: the result is the left operand), as in `x = y = v` with objects: the
        // assignment, then the left operand's address (read again; it has no effects, so none are repeated).
        if (!gen_expr(node)) return false;
        return gen_addr(node.kids[0]);
    }
    error = "not an address";
    return false;
}

void Gen::load_from_rax(int size, bool is_unsigned) {
    if (size >= 8) {
        b(0x48);
        b(0x8B);
        b(0x00);
    } else if (size == 1) {
        b(0x0F);
        b(is_unsigned ? 0xB6 : 0xBE);
        b(0x00);
    } else if (size == 2) {
        b(0x0F);
        b(is_unsigned ? 0xB7 : 0xBF);
        b(0x00);
    } else {
        b(0x8B);
        b(0x00);
    }
}

void Gen::load_typed(int32_t offset, const Type& type) {
    if (type.pointers == 0 && (type.base == 5 || type.base == 6)) {
        b(type.base == 6 ? 0xF2 : 0xF3);
        b(0x0F);
        b(0x10);
        b(0x85);
        disp(offset);
        return;
    }
    const int size = size_of(type);
    if (type.pointer() || size >= 8) load_slot(offset, true);
    else if (size == 4) load_slot(offset, false);
    else if (size == 2) {
        b(0x0F);
        b(type.is_unsigned ? 0xB7 : 0xBF);
        b(0x85);
        disp(offset);
    } else {
        b(0x0F);
        b(type.is_unsigned ? 0xB6 : 0xBE);
        b(0x85);
        disp(offset);
    }
}

void Gen::store_mem(int size) {
    if (size >= 8) {
        b(0x48);
        b(0x89);
        b(0x01);
    } else if (size == 1) {
        b(0x88);
        b(0x01);
    } else if (size == 2) {
        b(0x66);
        b(0x89);
        b(0x01);
    } else {
        b(0x89);
        b(0x01);
    }
}

void Gen::promote(Type* type) const {
    if (type->pointers > 0 || type->is_array() || type->base == 3) return;
    if (size_of(*type) < 4) {
        type->base = 1;
        type->is_bool = false;
        type->is_unsigned = false;
    }
}

// Brace elision ([dcl.init.aggr] 16): in `v.push_back({a, b, c})` for a std::array<int, 3> the list names the elements
// of the array inside the struct, not the struct's one member. The items from *pos on are given out member by member,
// a nested aggregate taking as many as it needs; an item that is itself a braced list is taken whole.
static Node elide_items(const std::vector<StructDef>* structs, const Type& type, const std::vector<Node>& items, size_t* pos) {
    Node list;
    list.kind = Node::Block;
    list.text = "init";
    auto aggregate = [&](const Type& t) { return t.array_len >= 0; };
    auto take = [&](const Type& t) {
        if (*pos >= items.size()) return;
        const Node& item = items[*pos];
        if (aggregate(t) && !(item.kind == Node::Block && item.text == "init")) {
            list.kids.push_back(elide_items(structs, t, items, pos));
        } else {
            list.kids.push_back(item);
            ++*pos;
        }
    };
    if (type.array_len >= 0) {
        const Type elem = peel_array(type);
        for (int i = 0; i < type.array_len && *pos < items.size(); ++i) take(elem);
    } else if (type.base == 3 && structs != nullptr && type.struct_id >= 0 &&
               type.struct_id < static_cast<int>(structs->size())) {
        for (const Field& field : (*structs)[static_cast<size_t>(type.struct_id)].fields) {
            if (field.name == "__vptr") continue;
            if (*pos >= items.size()) break;
            take(field.type);
        }
    }
    return list;
}

bool Gen::store_init(const Type& type, int offset, const Node& node) {
    if (node.kind == Node::Block && node.text == "init") {
        // A scalar item where a member or element is itself an aggregate: the braces were left out (elided).
        bool elided = false;
        // Own limit: only an array member is taken as elided (std::array's), not a member of class type, whose item may
        // be an object of that class.
        if (type.array_len >= 0) {
            const Type elem = peel_array(type);
            const bool agg = elem.array_len >= 0;
            for (const Node& kid : node.kids)
                if (agg && !(kid.kind == Node::Block && kid.text == "init")) elided = true;
        } else if (type.base == 3 && type.pointers == 0 && structs != nullptr && type.struct_id >= 0 &&
                   type.struct_id < static_cast<int>(structs->size())) {
            size_t k = 0;
            for (const Field& field : (*structs)[static_cast<size_t>(type.struct_id)].fields) {
                if (field.name == "__vptr") continue;
                if (k >= node.kids.size()) break;
                const bool agg = field.type.array_len >= 0;
                const Node& kid = node.kids[k++];
                if (agg && !(kid.kind == Node::Block && kid.text == "init")) elided = true;
            }
        }
        if (elided) {
            size_t pos = 0;
            const Node full = elide_items(structs, type, node.kids, &pos);
            if (pos < node.kids.size()) {
                error = "too many initializers";
                return false;
            }
            return store_init(type, offset, full);
        }
        // The elements not named are zero ([dcl.init.aggr]).
        Node zero;
        zero.kind = Node::IntLit;
        zero.type.base = 1;
        Node empty;
        empty.kind = Node::Block;
        empty.text = "init";
        if (type.array_len >= 0) {
            const Type elem = peel_array(type);
            const int step = std::max(1, size_of(elem));
            for (int i = 0; i < type.array_len; ++i) {
                const Node& item = static_cast<size_t>(i) < node.kids.size() ? node.kids[static_cast<size_t>(i)]
                                   : (elem.array_len >= 0 || (elem.base == 3 && elem.pointers == 0)) ? empty : zero;
                if (!store_init(elem, offset + i * step, item)) return false;
            }
            return true;
        }
        if (type.base == 3 && type.pointers == 0 && structs != nullptr && type.struct_id >= 0 &&
            type.struct_id < static_cast<int>(structs->size())) {
            const StructDef& def = (*structs)[static_cast<size_t>(type.struct_id)];
            for (size_t i = 0; i < def.fields.size(); ++i) {
                const Field& field = def.fields[i];
                if (field.name == "__vptr") continue;
                const bool nested = field.type.array_len >= 0 || (field.type.base == 3 && field.type.pointers == 0);
                const Node& item = i < node.kids.size() ? node.kids[i] : nested ? empty : zero;
                if (def.ref_members.count(field.name) > 0 && item.kind != Node::IntLit && item.kind != Node::Block) {
                    // A reference member made from a braced list (`struct { T& r; } s{x};`, [dcl.init.aggr]): the field
                    // holds the address of what it refers to.
                    if (!gen_addr(item)) return false;
                    b(0x48);  // mov [rbp+offset], rax
                    b(0x89);
                    b(0x85);
                    disp(offset + field.offset);
                    continue;
                }
                if (!store_init(field.type, offset + field.offset, item)) return false;
            }
            return true;
        }
        error = "initializer does not match the type";
        return false;
    }
    if (node.kind != Node::IntLit) {
        // A value computed at run time: evaluated, converted to the element type and stored.
        if ((type.base == 3 && type.pointers == 0) || type.array_len >= 0) {  // (a pointer to a class is a value)
            error = "initializer must be a constant";
            return false;
        }
        if (!gen_expr(node)) return false;
        convert(last_type, type);
        if (is_fp_t(type)) {
            b(type.base == 6 ? 0xF2 : 0xF3);
            b(0x0F);
            b(0x11);
            b(0x85);
            disp(offset);
            return true;
        }
        const int bytes = type.pointers > 0 ? 8 : std::max(1, size_of(type));
        if (bytes >= 8) b(0x48);
        else if (bytes == 2) b(0x66);
        b(bytes == 1 ? 0x88 : 0x89);
        b(0x85);
        disp(offset);
        return true;
    }
    const int width = type.pointers > 0 ? 8 : std::max(1, size_of(type));
    uint64_t value = 0;
    if (!const_convert(node, type, &value)) value = node.number;
    if (is_bool_type(type)) value = node.number != 0 ? 1u : 0u;
    if (width == 1) {
        b(0xC6);
        b(0x85);
        disp(offset);
        b(static_cast<uint8_t>(value));
    } else if (width == 2) {
        b(0x66);
        b(0xC7);
        b(0x85);
        disp(offset);
        b(static_cast<uint8_t>(value));
        b(static_cast<uint8_t>(value >> 8));
    } else {
        b(0xC7);
        b(0x85);
        disp(offset);
        u32(static_cast<uint32_t>(value));
        if (width >= 8) {
            b(0xC7);
            b(0x85);
            disp(offset + 4);
            u32(static_cast<uint32_t>(value >> 32));
        }
    }
    return true;
}

bool Gen::gen_stmt(const Node& node) {
    temp_depth = 0;
    if (debug_lines && node.line > 0 && node.kind != Node::Block)
        lines.push_back({static_cast<uint32_t>(text.size()), node.line});
    if (node.kind == Node::Block) {
        const bool scoped = node.text == "scope";
        if (scoped) push_scope();
        for (const Node& kid : node.kids)
            if (!gen_stmt(kid)) return false;
        if (scoped) pop_scope();
        return true;
    }
    if (node.kind == Node::Return) {
        if (current != nullptr && current->sret) {
            if (node.kids.empty()) {
                error = "missing return value";
                return false;
            }
            if (node.kids[0].kind == Node::Ternary && node.kids[0].kids.size() == 3) {
                // return c ? a : b; with a big struct: each branch returns its own value.
                Node branch;
                branch.kind = Node::If;
                branch.kids.push_back(node.kids[0].kids[0]);
                for (size_t side = 1; side <= 2; ++side) {
                    Node ret;
                    ret.kind = Node::Return;
                    ret.kids.push_back(node.kids[0].kids[side]);
                    branch.kids.push_back(std::move(ret));
                }
                return gen_stmt(branch);
            }
            if (node.kids[0].kind == Node::Call) {
                sret_dest_set = true;
                sret_dest_off = current->sret_off;
                sret_dest_indirect = true;
                if (!gen_expr(node.kids[0])) return false;
            } else {
                if (!gen_addr(node.kids[0])) return false;
                b(0x48);
                b(0x89);
                b(0xC3);
                b(0x48);
                b(0x89);
                b(0xDE);
                b(0x48);
                b(0x8B);
                b(0xBD);
                disp(current->sret_off);
                b(0xB9);
                u32(static_cast<uint32_t>(size_of(current->ret)));
                b(0xF3);
                b(0xA4);
            }
            b(0x48);
            b(0x8B);
            b(0x85);
            disp(current->sret_off);
            epilogue();
            return true;
        }
        if (!node.kids.empty() && !gen_expr(node.kids[0])) return false;
        if (current != nullptr && !node.kids.empty()) convert(last_type, current->ret);
        epilogue();
        return true;
    }
    if (node.kind == Node::ExprStmt) return gen_expr(node.kids[0]);
    if (node.kind == Node::If) {
        if (optimize && node.kids[0].kind == Node::IntLit) {
            if (node.kids[0].number != 0) {
                const bool ok = gen_stmt(node.kids[1]);
                if (node.kids.size() == 3) skip_scopes(node.kids[2]);
                return ok;
            }
            skip_scopes(node.kids[1]);
            if (node.kids.size() == 3) return gen_stmt(node.kids[2]);
            return true;
        }
        if (!gen_expr(node.kids[0])) return false;
        test_cond();
        const size_t jz = emit_jcc(0x84);
        if (!gen_stmt(node.kids[1])) return false;
        if (node.kids.size() == 3) {
            const size_t jmp = emit_jmp();
            patch(jz, text.size());
            if (!gen_stmt(node.kids[2])) return false;
            patch(jmp, text.size());
        } else patch(jz, text.size());
        return true;
    }
    if (node.kind == Node::While) {
        break_stack.emplace_back();
        continue_stack.emplace_back();
        const size_t start = text.size();
        if (!gen_expr(node.kids[0])) return false;
        test_cond();
        const size_t jz = emit_jcc(0x84);
        if (!gen_stmt(node.kids[1])) return false;
        for (size_t site : continue_stack.back()) patch(site, start);
        const size_t jmp = emit_jmp();
        patch(jmp, start);
        const size_t end = text.size();
        patch(jz, end);
        for (size_t site : break_stack.back()) patch(site, end);
        break_stack.pop_back();
        continue_stack.pop_back();
        return true;
    }
    if (node.kind == Node::For) {
        break_stack.emplace_back();
        continue_stack.emplace_back();
        auto skipped = [](const Node& item) { return item.kind == Node::Block && item.text == "skip"; };
        if (!skipped(node.kids[0]) && !gen_expr(node.kids[0])) return false;
        const size_t start = text.size();
        size_t jz = 0;
        bool has_cond = !skipped(node.kids[1]);
        if (has_cond) {
            if (!gen_expr(node.kids[1])) return false;
            test_cond();
            jz = emit_jcc(0x84);
        }
        if (!gen_stmt(node.kids[3])) return false;
        const size_t step_at = text.size();
        if (!skipped(node.kids[2]) && !gen_expr(node.kids[2])) return false;
        for (size_t site : continue_stack.back()) patch(site, step_at);
        const size_t jmp = emit_jmp();
        patch(jmp, start);
        const size_t end = text.size();
        if (has_cond) patch(jz, end);
        for (size_t site : break_stack.back()) patch(site, end);
        break_stack.pop_back();
        continue_stack.pop_back();
        return true;
    }
    if (node.kind == Node::Do) {
        break_stack.emplace_back();
        continue_stack.emplace_back();
        const size_t start = text.size();
        if (!gen_stmt(node.kids[0])) return false;
        const size_t cond_at = text.size();
        if (!gen_expr(node.kids[1])) return false;
        test_cond();
        const size_t jne = emit_jcc(0x85);
        patch(jne, start);
        for (size_t site : continue_stack.back()) patch(site, cond_at);
        const size_t end = text.size();
        for (size_t site : break_stack.back()) patch(site, end);
        break_stack.pop_back();
        continue_stack.pop_back();
        return true;
    }
    if (node.kind == Node::Switch) {
        if (!gen_expr(node.kids[0])) return false;
        // A long long selector is compared as 64 bits (a value above 2^32 must not match a small case).
        const bool wide_selector = is_wide_t(last_type);
        const int slot = spill();
        if (!error.empty()) return false;
        break_stack.emplace_back();
        std::vector<size_t> case_jumps;
        size_t default_jump = 0;
        bool has_default = false;
        int case_min = 0;
        int case_max = 0;
        int case_count = 0;
        for (size_t i = 1; i < node.kids.size(); ++i) {
            if (node.kids[i].op == TDefault) {
                has_default = true;
                continue;
            }
            const int value = static_cast<int>(node.kids[i].number);
            if (case_count == 0) {
                case_min = value;
                case_max = value;
            } else {
                if (value < case_min) case_min = value;
                if (value > case_max) case_max = value;
            }
            ++case_count;
        }
        const int span = case_max - case_min;
        // A dense switch is a table of jmp instructions, one per value.
        if (!wide_selector && case_count >= 4 && span >= 0 && span <= 8) {
            b(0x8B);
            b(0x85);
            disp(slot);
            if (case_min != 0) {
                b(0x2D);
                u32(static_cast<uint32_t>(case_min));
            }
            b(0x3D);
            u32(static_cast<uint32_t>(span));
            const size_t ja = emit_jcc(0x87);
            const size_t lea_disp = text.size() + 3;
            b(0x48);
            b(0x8D);
            b(0x0D);
            u32(0);
            b(0x6B);
            b(0xC0);
            b(0x05);
            b(0x48);
            b(0x01);
            b(0xC8);
            b(0xFF);
            b(0xE0);
            std::map<int, size_t> case_at;
            size_t default_at = 0;
            for (size_t i = 1; i < node.kids.size(); ++i) {
                if (node.kids[i].op == TDefault) default_at = text.size();
                else case_at[static_cast<int>(node.kids[i].number)] = text.size();
                for (const Node& stmt : node.kids[i].kids)
                    if (!gen_stmt(stmt)) return false;
            }
            const size_t table = text.size();
            const size_t hole = has_default ? default_at : 0;
            for (int v = case_min; v <= case_max; ++v) {
                size_t target = hole;
                const auto found = case_at.find(v);
                if (found != case_at.end()) target = found->second;
                b(0xE9);
                const size_t rel_at = text.size();
                u32(0);
                if (target != 0) patch(rel_at, target);
            }
            const size_t end = text.size();
            if (hole == 0) {
                for (int v = case_min; v <= case_max; ++v) {
                    if (case_at.count(v) != 0) continue;
                    // The hole jmp was left at 0. Point it at the end.
                }
                size_t cursor = table;
                for (int v = case_min; v <= case_max; ++v) {
                    if (case_at.count(v) == 0) patch(cursor + 1, end);
                    cursor += 5;
                }
            }
            patch(ja, has_default ? default_at : end);
            const int32_t lea_rel = static_cast<int32_t>(table - (lea_disp + 4));
            for (int i = 0; i < 4; ++i)
                text[lea_disp + static_cast<size_t>(i)] = static_cast<uint8_t>(static_cast<uint32_t>(lea_rel) >> (8 * i));
            for (size_t site : break_stack.back()) patch(site, end);
            break_stack.pop_back();
            --temp_depth;
            return true;
        }
        for (size_t i = 1; i < node.kids.size(); ++i) {
            if (node.kids[i].op == TDefault) {
                has_default = true;
                continue;
            }
            if (wide_selector) {
                b(0x48);  // mov rax, [slot]
                b(0x8B);
                b(0x85);
                disp(slot);
                b(0x49);  // mov r10, imm64
                b(0xBA);
                for (int k = 0; k < 8; ++k) b(static_cast<uint8_t>(node.kids[i].number >> (8 * k)));
                b(0x4C);  // cmp rax, r10
                b(0x39);
                b(0xD0);
            } else {
                b(0x8B);
                b(0x85);
                disp(slot);
                b(0x3D);
                u32(static_cast<uint32_t>(node.kids[i].number));
            }
            case_jumps.push_back(emit_jcc(0x84));
        }
        const size_t fall = emit_jmp();
        size_t case_i = 0;
        size_t default_at = 0;
        for (size_t i = 1; i < node.kids.size(); ++i) {
            if (node.kids[i].op == TDefault) default_at = text.size();
            else patch(case_jumps[case_i++], text.size());
            for (const Node& stmt : node.kids[i].kids)
                if (!gen_stmt(stmt)) return false;
        }
        const size_t end = text.size();
        patch(fall, has_default ? default_at : end);
        for (size_t site : break_stack.back()) patch(site, end);
        break_stack.pop_back();
        --temp_depth;
        (void)default_jump;
        return true;
    }
    if (node.kind == Node::Goto) {
        gotos.push_back({node.text, emit_jmp()});
        return true;
    }
    if (node.kind == Node::Label) {
        if (labels.count(node.text) != 0) {
            error = "duplicate label " + node.text;
            return false;
        }
        labels[node.text] = text.size();
        if (!node.kids.empty() && !gen_stmt(node.kids[0])) return false;
        return true;
    }
    if (node.kind == Node::Break) {
        if (break_stack.empty()) {
            error = "break is outside a loop or switch";
            return false;
        }
        break_stack.back().push_back(emit_jmp());
        return true;
    }
    if (node.kind == Node::Continue) {
        if (continue_stack.empty()) {
            error = "continue is outside a loop";
            return false;
        }
        continue_stack.back().push_back(emit_jmp());
        return true;
    }
    error = "bad statement";
    return false;
}


}  // namespace compiler
}  // namespace okayms
