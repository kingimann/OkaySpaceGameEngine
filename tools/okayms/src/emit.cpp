// Emitting the object file: every function's code, symbols, relocations, line table and unwind data.
// Moved out of compile.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "compile_internal.hpp"
#include "gen.hpp"

namespace okayms {
namespace compiler {
int count_args(const Node& node) {
    int m = 0;
    if (node.kind == Node::Call) m = static_cast<int>(node.kids.size());
    for (const Node& kid : node.kids) m = std::max(m, count_args(kid));
    return m;
}

void collect_locals(const Node& node, std::vector<Local>* locals) {
    if (node.kind == Node::Block || node.kind == Node::If || node.kind == Node::While || node.kind == Node::Return ||
        node.kind == Node::ExprStmt) {
        for (const Node& kid : node.kids) collect_locals(kid, locals);
    }
}

struct Sym {
    std::string name;
    uint32_t value = 0;
    int16_t section = 0;
    uint16_t type = 0;
    uint8_t storage = kClassExternal;
};

int struct_arg_space(const Node& node, const std::vector<FuncDecl>& funcs, const Gen& gen) {
    int best = 0;
    if (node.kind == Node::Call && !node.text.empty()) {
        const FuncDecl* fn = nullptr;
        for (const FuncDecl& item : funcs)
            if (item.name == node.text) fn = &item;
        if (fn != nullptr) {
            int sum = 0;
            const int n = std::min(static_cast<int>(node.kids.size()), static_cast<int>(fn->params.size()));
            for (int i = 0; i < n; ++i) {
                const Type& param = fn->params[static_cast<size_t>(i)];
                if (param.base == 3 && param.pointers == 0 && !gen.fits_register(param))
                    sum += (std::max(1, gen.size_of(param)) + 7) & ~7;
            }
            best = sum;
        }
    }
    for (const Node& kid : node.kids) best = std::max(best, struct_arg_space(kid, funcs, gen));
    return best;
}

void put32(std::vector<uint8_t>* out, uint32_t value) {
    for (int i = 0; i < 4; ++i) out->push_back(static_cast<uint8_t>(value >> (8 * i)));
}

bool emit_functions(std::vector<FuncDecl>& funcs, const std::vector<StructDef>& structs,
                    const std::vector<Global>& globals, const std::vector<FuncType>& func_types, bool optimize,
                    int opt_level, const LineSource& line_source, std::string* error, CompileOutput* out) {
    Gen gen(funcs);
    gen.debug_lines = line_source.enabled;
    gen.structs = &structs;
    gen.globals = &globals;
    gen.func_types = &func_types;
    gen.optimize = optimize;
    struct PlacedFn {
        std::string name;
        uint32_t start = 0;
        uint32_t size = 0;
        bool exported = false;
        bool is_static = false;
        std::vector<std::pair<uint32_t, int>> lines;
    };
    std::vector<PlacedFn> placed;
    std::vector<uint32_t> unwind_offs;
    if (opt_level >= 2) {
        int seq = 0;
        for (int pass = 0; pass < 4; ++pass) {
            for (FuncDecl& fn : funcs)
                if (fn.defined) walk_inline(&fn.body, &fn, funcs, gen, &seq);
            for (FuncDecl& fn : funcs)
                if (fn.defined) inline_node(&fn.body, funcs);
        }
    }
    for (FuncDecl& fn : funcs) {
        if (!fn.defined) continue;
        std::map<std::string, int> name_count;
        for (const Local& local : fn.locals) ++name_count[local.name];
        std::set<std::string> shadowed;
        for (const auto& item : name_count)
            if (item.second > 1) shadowed.insert(item.first);
        if (opt_level >= 2) {
            std::map<std::string, uint64_t> known;
            std::map<std::string, Type> ltypes;
            std::set<std::string> no_prop = shadowed;
            for (const Local& local : fn.locals) {
                ltypes[local.name] = local.type;
                if (address_taken(fn.body, local.name)) no_prop.insert(local.name);
            }
            const std::function<bool(const Node&)> sets_jmp = [&](const Node& n) {
                if (n.kind == Node::Call && n.text == "__okay_setjmp") return true;
                for (const Node& k : n.kids)
                    if (sets_jmp(k)) return true;
                return false;
            };
            if (!sets_jmp(fn.body)) prop_node(&fn.body, &known, &no_prop, &ltypes);
        }
        int max_args = count_args(fn.body);
        const int outgoing = max_args > 0 ? 32 + 8 * std::max(0, max_args - 4) : 0;
        const int local_end = fn.locals.empty() ? 0 : -fn.locals.back().offset;
        const int copy_bytes = struct_arg_space(fn.body, funcs, gen);
        int frame = local_end + 128 + copy_bytes + outgoing;
        frame = (frame + 15) & ~15;
        if (frame < 0x20) frame = 0x20;
        bool needs_nv = false;
        for (const Local& local : fn.locals) {
            if (local.type.base == 3 && local.type.pointers == 0 && !gen.fits_register(local.type)) needs_nv = true;
        }
        {
            // A function with a try block keeps its locals in memory: a restore after a throw only
            // brings back the registers saved when the try block was entered.
            const std::function<bool(const Node&)> has_setjmp = [&](const Node& n) {
                if (n.kind == Node::Call && n.text == "__okay_setjmp") return true;
                for (const Node& k : n.kids)
                    if (has_setjmp(k)) return true;
                return false;
            };
            if (has_setjmp(fn.body)) needs_nv = true;
        }
        std::vector<int> extra_regs;
        if ((optimize || opt_level >= 1) && !needs_nv) {
            std::map<std::string, std::pair<int, int>> range;
            int stamp = 0;
            const std::function<void(const Node&)> walk = [&](const Node& node) {
                if (node.kind == Node::Name) {
                    const auto found = range.find(node.text);
                    if (found == range.end()) range[node.text] = {stamp, stamp};
                    else found->second.second = stamp;
                    ++stamp;
                }
                for (const Node& kid : node.kids) walk(kid);
            };
            walk(fn.body);
            struct Span {
                int index;
                int start;
                int end;
            };
            std::vector<Span> spans;
            for (int i = 0; i < static_cast<int>(fn.locals.size()); ++i) {
                Local& local = fn.locals[static_cast<size_t>(i)];
                if (local.is_static || local.name.empty() || local.name[0] == '_' || local.type.is_ref) continue;
                if (shadowed.count(local.name) != 0) continue;
                if (local.type.is_array() || local.type.pointers != 0 || local.type.base == 3 || local.type.base == 5 ||
                    local.type.base == 6 || local.type.wide || local.type.is_bool || local.type.bits >= 0)
                    continue;
                if (address_taken(fn.body, local.name) || modified_in_place(fn.body, local.name)) continue;
                const auto found = range.find(local.name);
                if (found == range.end()) continue;
                // A parameter arrives in its register-home before the first use in the body.
                const bool is_param = i >= fn.param_base && i < fn.param_base + fn.nparams;
                spans.push_back({i, is_param ? -1 : found->second.first, found->second.second});
            }
            std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.start < b.start; });
            // Callee-saved first, then caller-saved. r10 stays the scratch
            // register for integer expressions, so it is not in this list.
            std::vector<int> pool = {3, 6, 7, 12, 13, 14, 15, 8, 9, 11, 1, 2};
            struct Active {
                int end;
                int reg;
            };
            std::vector<Active> active;
            auto callee_saved = [](int reg) { return reg == 3 || reg == 6 || reg == 7 || reg >= 12; };
            for (const Span& span : spans) {
                std::vector<Active> still;
                for (const Active& item : active) {
                    if (item.end < span.start) pool.insert(pool.begin(), item.reg);
                    else still.push_back(item);
                }
                active.swap(still);
                if (pool.empty()) continue;
                const int reg = pool.front();
                pool.erase(pool.begin());
                fn.locals[static_cast<size_t>(span.index)].reg = reg;
                if (callee_saved(reg)) {
                    bool already = false;
                    for (int have : extra_regs)
                        if (have == reg) already = true;
                    if (!already) extra_regs.push_back(reg);
                }
                active.push_back({span.end, reg});
            }
        }
        if (needs_nv) extra_regs = {3, 6, 7};
        if (((static_cast<int>(extra_regs.size()) + 1) % 2) == 0) frame += 8;
        gen.save_nv = !extra_regs.empty();
        gen.extra_pops.clear();
        gen.locals = fn.locals;
        gen.temp_base = -(((local_end + 7) & ~7) + 8);
        gen.struct_copy = -((((local_end + 7) & ~7) + 128 + copy_bytes));
        gen.frame = frame;
        const uint32_t start = static_cast<uint32_t>(gen.text.size());
        gen.lines.clear();
        if (gen.debug_lines) gen.lines.push_back({start, fn.line});
        uint8_t cursor = 0;
        auto mark = [&](uint8_t bytes) { cursor = static_cast<uint8_t>(cursor + bytes); return cursor; };
        std::vector<UnwindPush> pushes;
        for (int reg : extra_regs) {
            if (reg >= 12) {
                const uint8_t n = static_cast<uint8_t>(reg - 12);
                gen.b(0x41);
                gen.b(static_cast<uint8_t>(0x54 + n));
                pushes.push_back({static_cast<uint8_t>(reg), mark(2)});
                gen.extra_pops.insert(gen.extra_pops.begin(), static_cast<uint8_t>(0x5C + n));
                gen.extra_pops.insert(gen.extra_pops.begin(), 0x41);
            } else {
                const uint8_t push_op = reg == 3 ? 0x53 : reg == 6 ? 0x56 : 0x57;
                const uint8_t pop_op = reg == 3 ? 0x5B : reg == 6 ? 0x5E : 0x5F;
                gen.b(push_op);
                pushes.push_back({static_cast<uint8_t>(reg), mark(1)});
                gen.extra_pops.insert(gen.extra_pops.begin(), pop_op);
            }
        }
        gen.b(0x55);
        pushes.push_back({5, mark(1)});
        gen.b(0x48);
        gen.b(0x89);
        gen.b(0xE5);
        const uint8_t frame_at = mark(3);
        const bool large_alloc = frame > 127;
        if (large_alloc) {
            gen.b(0x48);
            gen.b(0x81);
            gen.b(0xEC);
            gen.u32(static_cast<uint32_t>(frame));
        } else {
            gen.b(0x48);
            gen.b(0x83);
            gen.b(0xEC);
            gen.b(static_cast<uint8_t>(frame));
        }
        if (fn.sret) {
            gen.b(0x48);
            gen.b(0x89);
            gen.b(0x8D);
            gen.disp(fn.sret_off);
        }
        if (fn.vararg) {
            gen.b(0x48);
            gen.b(0x89);
            gen.b(0x4D);
            gen.b(0x10);
            gen.b(0x48);
            gen.b(0x89);
            gen.b(0x55);
            gen.b(0x18);
            gen.b(0x4C);
            gen.b(0x89);
            gen.b(0x45);
            gen.b(0x20);
            gen.b(0x4C);
            gen.b(0x89);
            gen.b(0x4D);
            gen.b(0x28);
        }
        gen.scope_depth = 0;
        gen.scope_parent.clear();
        gen.scope_parent.push_back(0);
        gen.continue_stack.clear();
        gen.current = &fn;
        for (int p = 0; p < fn.nparams; ++p) {
            const int reg = p + (fn.sret ? 1 : 0);
            if (reg >= 4) {
                // The fifth and later arguments are on the caller's stack, above the return address, the 32-byte
                // home area and the registers this function pushed first ([x64 calling convention]).
                const Local& slot = fn.locals[static_cast<size_t>(fn.param_base + p)];
                const int from = 16 + 8 * static_cast<int>(extra_regs.size()) + 8 * reg;
                const int size = gen.size_of(slot.type);
                gen.b(0x48);  // mov rax, [rbp+from]
                gen.b(0x8B);
                gen.b(0x85);
                gen.disp(from);
                if (slot.type.base == 3 && slot.type.pointers == 0 && !gen.fits_register(slot.type)) {
                    gen.b(0x48);  // mov rsi, rax; lea rdi, [rbp+off]; mov ecx, size; rep movsb
                    gen.b(0x89);
                    gen.b(0xC6);
                    gen.b(0x48);
                    gen.b(0x8D);
                    gen.b(0xBD);
                    gen.disp(slot.offset);
                    gen.b(0xB9);
                    gen.u32(static_cast<uint32_t>(size));
                    gen.b(0xF3);
                    gen.b(0xA4);
                } else {
                    const bool wide = slot.type.pointer() || size == 8;
                    if (size == 2) gen.b(0x66);
                    else if (wide) gen.b(0x48);
                    gen.b(size == 1 ? 0x88 : 0x89);
                    gen.b(0x85);
                    gen.disp(slot.offset);
                }
                continue;
            }
            const Local& local = fn.locals[static_cast<size_t>(fn.param_base + p)];
            const Type& param = local.type;
            const int param_size = gen.size_of(param);
            const bool big = param.base == 3 && param.pointers == 0 && !gen.fits_register(param);
            const bool fp = param.pointers == 0 && !param.is_ref && (param.base == 5 || param.base == 6);
            const bool wide = param.pointer() || param_size == 8;
            if (fp) {
                const uint8_t modrm = reg == 0 ? 0x85 : reg == 1 ? 0x8D : reg == 2 ? 0x95 : 0x9D;
                gen.b(param.base == 6 ? 0xF2 : 0xF3);
                gen.b(0x0F);
                gen.b(0x11);
                gen.b(modrm);
                gen.disp(local.offset);
                continue;
            }
            if (big) {
                if (reg == 0) { gen.b(0x48); gen.b(0x89); gen.b(0xCB); }
                else if (reg == 1) { gen.b(0x48); gen.b(0x89); gen.b(0xD3); }
                else if (reg == 2) { gen.b(0x4C); gen.b(0x89); gen.b(0xC3); }
                else { gen.b(0x4C); gen.b(0x89); gen.b(0xCB); }
                gen.b(0x48); gen.b(0x89); gen.b(0xDE);
                gen.b(0x48); gen.b(0x8D); gen.b(0xBD); gen.disp(local.offset);
                gen.b(0xB9); gen.u32(static_cast<uint32_t>(param_size));
                gen.b(0xF3); gen.b(0xA4);
            } else if (param_size == 1) {
                const uint8_t rex = reg >= 2 ? 0x44 : 0;
                const uint8_t modrm = reg == 0 || reg == 3 ? 0x8D : (reg == 1 ? 0x95 : 0x85);
                if (rex) gen.b(rex);
                gen.b(0x88);
                gen.b(modrm);
                gen.disp(local.offset);
            } else if (param_size == 2) {
                const uint8_t rex = reg >= 2 ? 0x44 : 0;
                const uint8_t modrm = reg == 0 || reg == 3 ? 0x8D : (reg == 1 ? 0x95 : 0x85);
                gen.b(0x66);
                if (rex) gen.b(rex);
                gen.b(0x89);
                gen.b(modrm);
                gen.disp(local.offset);
            } else {
                const uint8_t rex = reg >= 2 ? (wide ? 0x4C : 0x44) : (wide ? 0x48 : 0);
                const uint8_t modrm = reg == 0 || reg == 3 ? 0x8D : (reg == 1 ? 0x95 : 0x85);
                if (rex) gen.b(rex);
                gen.b(0x89);
                gen.b(modrm);
                gen.disp(local.offset);
            }
        }
        for (int p = 0; p < fn.nparams; ++p) {
            const Local& local = fn.locals[static_cast<size_t>(fn.param_base + p)];
            if (local.reg == 0) continue;
            const int src = p + (fn.sret ? 1 : 0);
            if (src >= 4) {
                gen.load_reg_local(local.reg, local.offset);  // a parameter kept in a register: from its stack copy
                continue;
            }
            const int machine = src == 0 ? 1 : src == 1 ? 2 : src == 2 ? 8 : 9;
            gen.mov_regs(local.reg, machine);
        }
        if (!gen.gen_stmt(fn.body)) {
            *error = (gen.error.empty() ? std::string("codegen failed") : gen.error) + " (in " + fn.name + ")";
            return false;
        }
        if (gen.debug_lines) gen.lines.push_back({static_cast<uint32_t>(gen.text.size()), fn.end_line});
        gen.epilogue();
        placed.push_back({fn.symbol.empty() ? fn.name : fn.symbol, start, static_cast<uint32_t>(gen.text.size()) - start, fn.exported, fn.is_static, {}});
        placed.back().lines = gen.lines;
        for (const auto& site : gen.gotos) {
            const auto found = gen.labels.find(site.first);
            if (found == gen.labels.end()) {
                *error = "unknown label " + site.first;
                return false;
            }
            gen.patch(site.second, found->second);
        }
        gen.labels.clear();
        gen.gotos.clear();
        if (fn.exported) out->exports.push_back(fn.name);
        UnwindPrologue prolog;
        prolog.size = static_cast<uint8_t>(frame_at + (large_alloc ? 7 : 4));
        prolog.frame_register = 5;
        prolog.frame_at = frame_at;
        prolog.alloc = static_cast<uint32_t>(frame);
        prolog.alloc_at = prolog.size;
        prolog.pushes = pushes;
        const auto unwind = encode_unwind(prolog);
        if (!unwind.ok()) {
            *error = unwind.error;
            return false;
        }
        const uint32_t xat = static_cast<uint32_t>(gen.xdata.size());
        unwind_offs.push_back(xat);
        gen.xdata.insert(gen.xdata.end(), unwind.value.begin(), unwind.value.end());
        const std::string unw = "unwind_" + placed.back().name;
        // pdata: begin, end (addend = size), unwind info.
        const uint32_t pat = static_cast<uint32_t>(gen.pdata.size());
        for (int i = 0; i < 3; ++i) gen.pdata.push_back(0), gen.pdata.push_back(0), gen.pdata.push_back(0), gen.pdata.push_back(0);
        gen.pdata[pat + 4] = static_cast<uint8_t>(placed.back().size);
        gen.pdata[pat + 5] = static_cast<uint8_t>(placed.back().size >> 8);
        gen.pdata[pat + 6] = static_cast<uint8_t>(placed.back().size >> 16);
        gen.pdata[pat + 7] = static_cast<uint8_t>(placed.back().size >> 24);
        gen.relocs.push_back({2, pat, placed.back().name, kRelAmd64Addr32Nb});
        gen.relocs.push_back({2, pat + 4, placed.back().name, kRelAmd64Addr32Nb});
        gen.relocs.push_back({2, pat + 8, unw, kRelAmd64Addr32Nb});
        (void)xat;
        (void)unw;
    }
    if (!gen.error.empty()) {
        *error = gen.error;
        return false;
    }

    std::vector<Sym> symbols;
    auto add_sym = [&](const std::string& name, uint32_t value, int16_t section, uint8_t storage, uint16_t type) {
        symbols.push_back({name, value, section, type, storage});
    };
    for (const PlacedFn& fn : placed)
        add_sym(fn.name, fn.start, 1, fn.is_static ? kClassStatic : kClassExternal, 0x20);
    for (const FuncDecl& fn : funcs) {
        if (fn.defined) continue;
        // A call names the decorated symbol when the function has one (a namespace function).
        const std::string extern_name = fn.symbol.empty() ? fn.name : fn.symbol;
        if (fn.imported) add_sym("__imp_" + extern_name, 0, 0, kClassExternal, 0);
        else add_sym(extern_name, 0, 0, kClassExternal, 0);
    }
    std::vector<uint8_t> data_bytes;
    std::vector<uint8_t> tls_bytes;  // thread_local variables: the template each thread's copy is made from
    uint32_t bss_size = 0;
    if (gen.uses_tls) add_sym("_tls_index", 0, 0, kClassExternal, 0);
    auto align_to = [](uint32_t value, uint32_t align) {
        if (align == 0) return value;
        return (value + align - 1u) & ~(align - 1u);
    };
    int next_section = 2;
    for (const Global& global : globals) {
        if (global.vtable) {
            // Read-only: one 8-byte slot per virtual function, filled by an
            // ADDR64 relocation. A pure virtual slot stays zero.
            while (gen.rdata.size() % 8 != 0) gen.rdata.push_back(0);
            if (!global.rtti.empty()) {
                // The slot before the table: the address of the class's type information (typeid of an object).
                gen.relocs.push_back({1, static_cast<uint32_t>(gen.rdata.size()), global.rtti, kRelAmd64Addr64});
                for (int k = 0; k < 8; ++k) gen.rdata.push_back(0);
            }
            const uint32_t at = static_cast<uint32_t>(gen.rdata.size());
            for (size_t i = 0; i < global.slots.size(); ++i) {
                if (!global.slots[i].empty())
                    gen.relocs.push_back({1, at + static_cast<uint32_t>(i * 8), global.slots[i], kRelAmd64Addr64});
                for (int k = 0; k < 8; ++k) gen.rdata.push_back(0);
            }
            add_sym(global.name, at, 0, kClassExternal, 0);
            continue;
        }
        const int size = std::max(1, gen.size_of(global.type));
        const int align = global.type.pointer() ? 8 : 4;
        if (global.tls) {
            // .tls$ holds initialized bytes only (zeros for a variable without an initializer), so the whole
            // template is copied for each thread ([basic.stc.thread], PE/COFF "The .tls Section").
            const uint32_t at = align_to(static_cast<uint32_t>(tls_bytes.size()), 8);
            tls_bytes.resize(at, 0);
            if (!global.bss && !global.init_bytes.empty()) {
                tls_bytes.insert(tls_bytes.end(), global.init_bytes.begin(), global.init_bytes.end());
            } else if (!global.bss) {
                for (int k = 0; k < 4; ++k) tls_bytes.push_back(static_cast<uint8_t>(global.init >> (8 * k)));
            }
            tls_bytes.resize(std::max<size_t>(tls_bytes.size(), at + static_cast<uint32_t>(size)), 0);
            add_sym(global.name, at, 0, global.is_static ? kClassStatic : kClassExternal, 0);
            for (const auto& slot : global.addr_relocs) gen.relocs.push_back({4, at + slot.first, slot.second, kRelAmd64Addr64});
            continue;
        }
        if (global.bss) {
            bss_size = align_to(bss_size, static_cast<uint32_t>(align));
            add_sym(global.name, bss_size, 0, global.is_static ? kClassStatic : kClassExternal, 0);
            bss_size += static_cast<uint32_t>(size);
        } else {
            const uint32_t at = align_to(static_cast<uint32_t>(data_bytes.size()), static_cast<uint32_t>(align));
            data_bytes.resize(at, 0);
            if (!global.init_bytes.empty()) {
                data_bytes.insert(data_bytes.end(), global.init_bytes.begin(), global.init_bytes.end());
                if (static_cast<int>(global.init_bytes.size()) < size)
                    data_bytes.resize(at + static_cast<uint32_t>(size), 0);
            } else {
                const uint32_t value = global.init;
                data_bytes.push_back(static_cast<uint8_t>(value));
                data_bytes.push_back(static_cast<uint8_t>(value >> 8));
                data_bytes.push_back(static_cast<uint8_t>(value >> 16));
                data_bytes.push_back(static_cast<uint8_t>(value >> 24));
                if (size > 4) data_bytes.resize(at + static_cast<uint32_t>(size), 0);
            }
            add_sym(global.name, at, 0, global.is_static ? kClassStatic : kClassExternal, 0);
            for (const auto& slot : global.addr_relocs) gen.relocs.push_back({3, at + slot.first, slot.second, kRelAmd64Addr64});
        }
    }
    int data_section = 0;
    int bss_section = 0;
    int rdata_section = 0;
    int xdata_section = 0;
    int pdata_section = 0;
    int tls_section = 0;
    if (!data_bytes.empty()) data_section = next_section++;
    if (bss_size != 0) bss_section = next_section++;
    if (!gen.rdata.empty()) rdata_section = next_section++;
    if (!gen.xdata.empty()) xdata_section = next_section++;
    if (!gen.pdata.empty()) pdata_section = next_section++;
    if (!tls_bytes.empty()) tls_section = next_section++;
    for (Sym& sym : symbols) {
        for (const Global& global : globals) {
            if (sym.name != global.name) continue;
            sym.section = static_cast<int16_t>(global.vtable ? rdata_section
                                               : global.tls ? tls_section
                                               : global.bss ? bss_section
                                                            : data_section);
        }
    }
    for (const auto& str : gen.string_at) add_sym(str.first, str.second, static_cast<int16_t>(rdata_section), kClassStatic, 0);
    for (size_t i = 0; i < placed.size(); ++i) {
        add_sym("unwind_" + placed[i].name, unwind_offs[i], static_cast<int16_t>(xdata_section), kClassStatic, 0);
    }

    std::map<std::string, uint32_t> index;
    for (size_t i = 0; i < symbols.size(); ++i) index[symbols[i].name] = static_cast<uint32_t>(i);

    ObjectFile object;
    Section text;
    text.name = ".text";
    text.virtual_size = static_cast<uint32_t>(gen.text.size());
    text.data = gen.text;
    text.characteristics = kScnCntCode | kScnMemExecute | kScnMemRead | kScnAlign16;
    object.sections.push_back(std::move(text));
    auto add_data = [&](const char* name, std::vector<uint8_t> data) {
        if (data.empty()) return;
        Section section;
        section.name = name;
        section.virtual_size = static_cast<uint32_t>(data.size());
        section.data = std::move(data);
        section.characteristics = kScnCntInitData | kScnMemRead;
        object.sections.push_back(std::move(section));
    };
    if (!data_bytes.empty()) {
        Section data;
        data.name = ".data";
        data.virtual_size = static_cast<uint32_t>(data_bytes.size());
        data.data = data_bytes;
        data.characteristics = kScnCntInitData | kScnMemRead | kScnMemWrite;
        object.sections.push_back(std::move(data));
    }
    if (bss_size != 0) {
        Section bss;
        bss.name = ".bss";
        bss.virtual_size = bss_size;
        bss.characteristics = kScnCntUninitData | kScnMemRead | kScnMemWrite;
        object.sections.push_back(std::move(bss));
    }
    add_data(".rdata", gen.rdata);
    add_data(".xdata", gen.xdata);
    add_data(".pdata", gen.pdata);
    if (!tls_bytes.empty()) {
        Section tls;
        tls.name = ".tls$";
        tls.virtual_size = static_cast<uint32_t>(tls_bytes.size());
        tls.data = std::move(tls_bytes);
        tls.characteristics = kScnCntInitData | kScnMemRead | kScnMemWrite | kScnAlign8;
        object.sections.push_back(std::move(tls));
    }
    int debug_index = -1;
    if (line_source.enabled && line_source.origin != nullptr && line_source.files != nullptr) {
        // CodeView C13 subsections in .debug$S: signature 4, then one
        // DEBUG_S_LINES (0xF2) per function, DEBUG_S_STRINGTABLE (0xF3) and
        // DEBUG_S_FILECHKSMS (0xF4). A LINES header names its code range by a
        // SECREL and a SECTION relocation against the function symbol.
        std::vector<uint8_t> cv;
        std::vector<Relocation> cv_relocs;
        std::vector<std::string> used_files;
        auto table_index = [&](int file) {
            for (size_t k = 0; k < used_files.size(); ++k)
                if (used_files[k] == (*line_source.files)[static_cast<size_t>(file)]) return static_cast<uint32_t>(k);
            used_files.push_back((*line_source.files)[static_cast<size_t>(file)]);
            return static_cast<uint32_t>(used_files.size() - 1);
        };
        put32(&cv, 4);
        for (const PlacedFn& fn : placed) {
            // (file table index, offset in function, line) with same-line and same-offset entries merged.
            struct Entry {
                uint32_t file;
                uint32_t offset;
                uint32_t line;
            };
            std::vector<Entry> entries;
            for (const auto& item : fn.lines) {
                const int out_line = item.second;
                if (out_line <= 0 || static_cast<size_t>(out_line) > line_source.origin->size()) continue;
                const auto& where = (*line_source.origin)[static_cast<size_t>(out_line) - 1];
                if (where.second <= 0) continue;
                Entry entry{table_index(where.first), item.first - fn.start, static_cast<uint32_t>(where.second)};
                if (!entries.empty() && entries.back().offset == entry.offset) entries.pop_back();
                if (!entries.empty() && entries.back().file == entry.file && entries.back().line == entry.line) continue;
                entries.push_back(entry);
            }
            if (entries.empty()) continue;
            std::vector<uint8_t> payload;
            put32(&payload, 0);
            payload.push_back(0);
            payload.push_back(0);
            payload.push_back(0);
            payload.push_back(0);
            put32(&payload, fn.size);
            for (size_t k = 0; k < entries.size();) {
                size_t end = k;
                while (end < entries.size() && entries[end].file == entries[k].file) ++end;
                put32(&payload, entries[k].file * 8u);
                put32(&payload, static_cast<uint32_t>(end - k));
                put32(&payload, 12u + static_cast<uint32_t>(end - k) * 8u);
                for (size_t e = k; e < end; ++e) {
                    put32(&payload, entries[e].offset);
                    // 24 bits of line, 7 bits of extra lines, then the statement bit.
                    put32(&payload, (entries[e].line & 0x00FFFFFFu) | 0x80000000u);
                }
                k = end;
            }
            put32(&cv, 0xF2);
            put32(&cv, static_cast<uint32_t>(payload.size()));
            const auto sym = index.find(fn.name);
            if (sym == index.end()) {
                *error = "missing symbol " + fn.name;
                return false;
            }
            Relocation secrel;
            secrel.offset = static_cast<uint32_t>(cv.size());
            secrel.symbol = sym->second;
            secrel.type = kRelAmd64Secrel;
            Relocation section_rel = secrel;
            section_rel.offset += 4;
            section_rel.type = kRelAmd64Section;
            cv_relocs.push_back(secrel);
            cv_relocs.push_back(section_rel);
            cv.insert(cv.end(), payload.begin(), payload.end());
        }
        if (!used_files.empty()) {
            std::vector<uint8_t> names(1, 0);
            std::vector<uint8_t> sums;
            for (const std::string& file : used_files) {
                put32(&sums, static_cast<uint32_t>(names.size()));
                sums.push_back(0);
                sums.push_back(0);
                sums.push_back(0);
                sums.push_back(0);
                names.insert(names.end(), file.begin(), file.end());
                names.push_back(0);
            }
            while (names.size() % 4 != 0) names.push_back(0);
            put32(&cv, 0xF4);
            put32(&cv, static_cast<uint32_t>(sums.size()));
            cv.insert(cv.end(), sums.begin(), sums.end());
            put32(&cv, 0xF3);
            put32(&cv, static_cast<uint32_t>(names.size()));
            cv.insert(cv.end(), names.begin(), names.end());
            Section debug;
            debug.name = ".debug$S";
            debug.virtual_size = static_cast<uint32_t>(cv.size());
            debug.data = std::move(cv);
            debug.relocations = std::move(cv_relocs);
            // Initialized data, readable, discardable, 1-byte alignment.
            debug.characteristics = kScnCntInitData | kScnMemRead | 0x02000000u | kScnAlign1;
            object.sections.push_back(std::move(debug));
            debug_index = static_cast<int>(object.sections.size()) - 1;
        }
    }
    (void)debug_index;
    for (const Gen::NamedReloc& named : gen.relocs) {
        // section 0 text, 1 rdata (unused), 2 pdata in the Gen numbering.
        int sec = 0;
        if (named.section == 2) {
            sec = pdata_section - 1;
        } else if (named.section == 1) {
            sec = rdata_section - 1;
        } else if (named.section == 3) {
            sec = data_section - 1;
        } else if (named.section == 4) {
            sec = tls_section - 1;
        }
        const auto found = index.find(named.symbol);
        if (found == index.end()) {
            *error = "missing symbol " + named.symbol;
            return false;
        }
        Relocation reloc;
        reloc.offset = named.offset;
        reloc.symbol = found->second;
        reloc.type = named.type;
        object.sections[sec].relocations.push_back(reloc);
    }
    for (const Sym& sym : symbols) {
        Symbol out_sym;
        out_sym.name = sym.name;
        out_sym.value = sym.value;
        out_sym.section = sym.section;
        out_sym.type = sym.type;
        out_sym.storage = sym.storage;
        object.symbols.push_back(std::move(out_sym));
    }
    out->object = std::move(object);
    (void)xdata_section;
    return true;
}


}  // namespace compiler
}  // namespace okayms
