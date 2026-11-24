// The parser: initializers, blocks, statements, local declarations and range-for.
// Moved out of compile.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "compile_internal.hpp"
#include "parser.hpp"

namespace okayms {
namespace compiler {
bool Parser::parse_init(Node* out) {
    if (is(TLBrace)) {
        next();
        out->kind = Node::Block;
        out->text = "init";
        while (!is(TRBrace) && !is(TEnd)) {
            Node item;
            if (!parse_init(&item)) return false;
            out->kids.push_back(std::move(item));
            if (!accept(TComma)) break;
        }
        return expect(TRBrace, "'}'");
    }
    return parse_expr(1, out);
}

bool Parser::flatten_init(const Type& type, const Node& node_given, std::vector<uint8_t>* bytes, int at) {
    auto ensure = [&](int n) {
        if (static_cast<int>(bytes->size()) < n) bytes->resize(static_cast<size_t>(n), 0);
    };
    const Node regular = node_given.kind == Node::Block && node_given.text == "init" && type.array_len != 0
                             ? elide_braces(type, node_given)
                             : node_given;
    const Node& node = regular;
    if (node.kind == Node::Block && node.text == "init") {
        if (type.array_len >= 0) {
            const Type elem = peel_array(type);
            const int step = std::max(1, size_of(elem));
            for (size_t i = 0; i < node.kids.size(); ++i) {
                if (!flatten_init(elem, node.kids[i], bytes, at + static_cast<int>(i) * step)) return false;
            }
            ensure(at + type.array_len * step);
            return true;
        }
        if (type.base == 3 && type.pointers == 0 && type.struct_id >= 0 &&
            type.struct_id < static_cast<int>(structs.size())) {
            const StructDef& def = structs[static_cast<size_t>(type.struct_id)];
            for (size_t i = 0; i < node.kids.size() && i < def.fields.size(); ++i) {
                const Field& field = def.fields[i];
                if (!flatten_init(field.type, node.kids[i], bytes, at + field.offset)) return false;
            }
            ensure(at + def.size);
            return true;
        }
        error = "initializer does not match the type";
        return false;
    }
    if (node.kind == Node::StringLit && type.array_len < 0 && type.pointers == 1 && type.base == 2 && init_relocs != nullptr) {
        // const char *p = "text": the text becomes a hidden global and the pointer an ADDR64 slot.
        Global text;
        text.name = "__gstr" + std::to_string(gstr_count++);
        text.is_static = true;
        text.bss = false;
        text.type.base = 2;
        text.type.array_len = static_cast<int>(node.text.size()) + 1;
        text.init_bytes.assign(node.text.begin(), node.text.end());
        text.init_bytes.push_back(0);
        globals.push_back(text);
        ensure(at + 8);
        init_relocs->push_back({static_cast<uint32_t>(at), globals.back().name});
        return true;
    }
    if (node.kind == Node::StringLit && type.array_len >= 0 && type.base == 2 && type.pointers == 0) {
        // char s[N] = "text"
        ensure(at + type.array_len);
        for (size_t i = 0; i < node.text.size() && static_cast<int>(i) < type.array_len; ++i)
            (*bytes)[static_cast<size_t>(at) + i] = static_cast<uint8_t>(node.text[i]);
        return true;
    }
    if (node.kind != Node::IntLit) {
        if (dyn_leaves != nullptr && type.bits < 0 && !type.is_array() && !type.is_ref) {
            // Not a constant: zero here, stored at run time ([basic.start.dynamic]).
            ensure(at + std::max(1, size_of(type)));
            DynLeaf leaf;
            leaf.at = at;
            leaf.type = type;
            leaf.value = node;
            dyn_leaves->push_back(std::move(leaf));
            return true;
        }
        error = "global initializer must be a constant";
        return false;
    }
    const int width = std::max(1, size_of(type));
    ensure(at + width);
    uint64_t value = type.is_bool && type.pointers == 0 ? (node.number != 0 ? 1u : 0u) : node.number;
    if (!(type.is_bool && type.pointers == 0)) const_convert(node, type, &value);
    for (int i = 0; i < width && i < 8; ++i)
        (*bytes)[static_cast<size_t>(at + i)] = static_cast<uint8_t>(value >> (8 * i));
    return true;
}

bool Parser::parse_block(Node* out, std::vector<Local>* locals) {
    if (!expect(TLBrace, "'{'")) return false;
    out->kind = Node::Block;
    out->text = "scope";
    push_scope();
    dscopes.emplace_back();
    while (!is(TRBrace) && !is(TEnd)) {
        Node stmt;
        if (!parse_stmt(&stmt, locals)) return false;
        out->kids.push_back(std::move(stmt));
    }
    {
        // Destroy this block's objects, latest first.
        std::vector<Node> dt;
        if (!dtor_nodes(dscopes.size() - 1, &dt)) return false;
        for (Node& n : dt) out->kids.push_back(std::move(n));
        dscopes.pop_back();
    }
    pop_scope();
    return expect(TRBrace, "'}'");
}

// The guard of a static local with a dynamic initializer ([stmt.dcl]): a hidden flag, and the block that runs the first
// time control reaches the declaration. The block starts with what the initializer needs before it (hidden temporaries
// made for reference arguments), so those are made only once too; they are destroyed at the end of the block.
bool Parser::static_guard_block(const std::string& symbol, Node* then_block) {
    Global guard;
    guard.name = symbol + "_guard";
    guard.type.base = 1;
    guard.bss = true;
    guard.is_static = true;
    globals.push_back(guard);
    then_block->kind = Node::Block;
    for (Node& n : stmt_pre) then_block->kids.push_back(std::move(n));
    stmt_pre.clear();
    return true;
}

void Parser::finish_static_guard(Node* then_block, Node init_stmt, const std::string& symbol, Node* out) {
    if (!(init_stmt.kind == Node::Block && init_stmt.kids.empty())) then_block->kids.push_back(std::move(init_stmt));
    for (size_t i = stmt_temps.size(); i-- > 0;) {
        Node call;
        if (special_call(stmt_temps[i].sid, "<dtor>", node_addr(node_name(stmt_temps[i].name)), {}, &call))
            then_block->kids.push_back(std::move(call));
    }
    stmt_temps.clear();
    // The flag is set once the value is there ([stmt.dcl] 4).
    Node set;
    set.kind = Node::Assign;
    set.op = TAssign;
    set.kids.push_back(node_name(symbol + "_guard"));
    Node one;
    one.kind = Node::IntLit;
    one.number = 1;
    one.type.base = 1;
    set.kids.push_back(std::move(one));
    Node set_stmt;
    set_stmt.kind = Node::ExprStmt;
    set_stmt.kids.push_back(std::move(set));
    then_block->kids.push_back(std::move(set_stmt));
    Node not_g;
    not_g.kind = Node::Unary;
    not_g.op = TNot;
    not_g.kids.push_back(node_name(symbol + "_guard"));
    Node test;
    test.kind = Node::If;
    test.kids.push_back(std::move(not_g));
    test.kids.push_back(std::move(*then_block));
    out->kids.push_back(std::move(test));
}

// A statement, preceded by the assignments that give hidden locals their values for references
// bound to temporaries (a reference parameter given an expression that is not an object).
bool Parser::parse_stmt(Node* out, std::vector<Local>* locals) {
    std::vector<Node> outer = std::move(stmt_pre);
    stmt_pre.clear();
    bool loop = is(TWhile) || is(TFor) || is(TDo);
    if (is(TFor) && at + 1 < tokens.size() && tokens[at + 1].kind == TLParen) {
        // A range for evaluates its range once, before the loop: what that needs can go before the statement.
        const size_t save = at;
        at += 2;
        if (range_for_ahead()) loop = false;
        at = save;
    }
    const bool ok = parse_stmt_core(out, locals);
    std::vector<Node> mine = std::move(stmt_pre);
    stmt_pre = std::move(outer);
    if (!ok || mine.empty()) return ok;
    if (loop) {
        error = "a reference bound to a temporary in a loop condition is not supported";
        return false;
    }
    Node block;
    block.kind = Node::Block;
    block.line = out->line;
    for (Node& n : mine) block.kids.push_back(std::move(n));
    block.kids.push_back(std::move(*out));
    *out = std::move(block);
    return true;
}

bool Parser::parse_stmt_core(Node* out, std::vector<Local>* locals) {
    const int stmt_line = cur().line;
    std::vector<Temp> outer = std::move(stmt_temps);
    stmt_temps.clear();
    const bool ok = parse_stmt_inner(out, locals, stmt_line);
    out->line = stmt_line;
    std::vector<Temp> mine = std::move(stmt_temps);
    stmt_temps = std::move(outer);
    if (!ok || mine.empty()) return ok;
    if ((out->kind == Node::If && out->kids.size() >= 2) || (out->kind == Node::While && out->kids.size() == 2)) {
        // The copies made in a condition die once the condition is evaluated: the value is kept
        // in a hidden local, the copies are destroyed, then the branch (or, for a loop, the body
        // or the exit) is chosen. A loop becomes while (1) { c = cond; dtors; if (!c) break; body }.
        Local hidden;
        hidden.name = "__cv" + std::to_string(rv_count++);
        Type ct;
        ct.base = 1;
        if (!static_type(out->kids[0], &ct) || ct.base == 3 || ct.is_array()) {
            ct = Type{};
            ct.base = 1;
        }
        ct.is_ref = false;
        hidden.type = ct;
        place_local(locals, hidden);
        Node assign;
        assign.kind = Node::Assign;
        assign.op = TAssign;
        assign.kids.push_back(node_name(hidden.name));
        assign.kids.push_back(std::move(out->kids[0]));
        Node assign_stmt;
        assign_stmt.kind = Node::ExprStmt;
        assign_stmt.kids.push_back(std::move(assign));
        Node block;
        block.kind = Node::Block;
        block.line = stmt_line;
        block.kids.push_back(std::move(assign_stmt));
        for (size_t i = mine.size(); i-- > 0;) {
            Node call;
            if (!special_call(mine[i].sid, "<dtor>", node_addr(node_name(mine[i].name)), {}, &call)) return false;
            block.kids.push_back(std::move(call));
        }
        if (out->kind == Node::If) {
            out->kids[0] = node_name(hidden.name);
            block.kids.push_back(std::move(*out));
        } else {
            Node inner;
            inner.kind = Node::Block;
            Node first;
            first = std::move(block);
            // first holds: assign, dtors. Add the exit test and the original body.
            Node not_c;
            not_c.kind = Node::Unary;
            not_c.op = TNot;
            not_c.kids.push_back(node_name(hidden.name));
            Node stop;
            stop.kind = Node::If;
            stop.kids.push_back(std::move(not_c));
            Node brk;
            brk.kind = Node::Break;
            stop.kids.push_back(std::move(brk));
            first.kids.push_back(std::move(stop));
            first.kids.push_back(std::move(out->kids[1]));
            Node forever;
            forever.kind = Node::While;
            Node one;
            one.kind = Node::IntLit;
            one.number = 1;
            one.type.base = 1;
            forever.kids.push_back(std::move(one));
            forever.kids.push_back(std::move(first));
            forever.line = stmt_line;
            *out = std::move(forever);
            return true;
        }
        *out = std::move(block);
        return true;
    }
    // Hidden copies made for this statement's arguments are destroyed after it, latest first.
    Node wrapped;
    wrapped.kind = Node::Block;
    wrapped.line = stmt_line;
    wrapped.kids.push_back(std::move(*out));
    for (size_t i = mine.size(); i-- > 0;) {
        Node call;
        if (!special_call(mine[i].sid, "<dtor>", node_addr(node_name(mine[i].name)), {}, &call)) return false;
        wrapped.kids.push_back(std::move(call));
    }
    *out = std::move(wrapped);
    return true;
}

bool Parser::parse_stmt_inner(Node* out, std::vector<Local>* locals, int stmt_line) {
    cur_locals = locals;
    if (is(TLBrace)) return parse_block(out, locals);
    if (is(TSemi)) {
        // An empty statement ([stmt.expr]).
        next();
        out->kind = Node::Block;
        return true;
    }
    if (is(TIdent) && cur().text == "try" && at + 1 < tokens.size() && tokens[at + 1].kind == TLBrace)
        return parse_try(out, locals, stmt_line);
    if (is(TIdent) && cur().text == "throw") return parse_throw(out, locals);
    if (is(TGoto)) {
        next();
        if (!is(TIdent)) {
            error = "expected a label";
            return false;
        }
        out->kind = Node::Goto;
        out->text = cur().text;
        next();
        return expect(TSemi, "';'");
    }
    if (is(TIdent) && at + 1 < tokens.size() && tokens[at + 1].kind == TColon &&
        typedefs.find(cur().text) == typedefs.end()) {
        out->kind = Node::Label;
        out->text = cur().text;
        next();
        next();
        Node stmt;
        if (!parse_stmt(&stmt, locals)) return false;
        out->kids.push_back(std::move(stmt));
        return true;
    }
    if (is(TIf)) {
        next();
        out->kind = Node::If;
        if (!expect(TLParen, "'('")) return false;
        Node cond;
        Node cond_decl;
        bool cond_scoped = false;
        if (!parse_condition(&cond, &cond_decl, &cond_scoped, locals)) return false;
        Node then_s;
        if (!parse_stmt(&then_s, locals)) return false;
        out->kids.push_back(std::move(cond));
        out->kids.push_back(std::move(then_s));
        if (accept(TElse)) {
            Node else_s;
            if (!parse_stmt(&else_s, locals)) return false;
            out->kids.push_back(std::move(else_s));
        }
        close_condition_scope(out, std::move(cond_decl), cond_scoped);
        return true;
    }
    if (is(TWhile)) {
        next();
        out->kind = Node::While;
        if (!expect(TLParen, "'('")) return false;
        Node cond;
        Node cond_decl;
        bool cond_scoped = false;
        if (!parse_condition(&cond, &cond_decl, &cond_scoped, locals)) return false;
        Node body;
        brk_dbase.push_back(dscopes.size());
        cont_dbase.push_back(dscopes.size());
        const bool body_ok = parse_stmt(&body, locals);
        brk_dbase.pop_back();
        cont_dbase.pop_back();
        if (!body_ok) return false;
        out->kids.push_back(std::move(cond));
        out->kids.push_back(std::move(body));
        if (cond_scoped) {
            error = "a declaration in a while condition is not supported";  // the variable would need to be made again each pass
            return false;
        }
        return true;
    }
    if (is(TFor)) {
        next();
        out->kind = Node::For;
        if (!expect(TLParen, "'('")) return false;
        if (range_for_ahead()) return parse_range_for(out, locals, stmt_line);
        Node init;
        init.kind = Node::Block;
        init.text = "skip";
        // for (int i = 0; ...): the declaration lives in a scope around the whole loop ([stmt.for]).
        Node decl_init;
        bool decl_scope = false;
        if (is_decl_start() && !is(TStatic)) {
            decl_scope = true;
            push_scope();
            dscopes.emplace_back();
            if (!parse_stmt(&decl_init, locals)) return false;
        } else {
            if (!is(TSemi)) {
                if (!parse_expr(0, &init)) return false;
            }
            if (!expect(TSemi, "';'")) return false;
        }
        Node cond;
        cond.kind = Node::Block;
        cond.text = "skip";
        if (!is(TSemi)) {
            if (!parse_expr(0, &cond)) return false;
            if (!convert_class(&cond, bool_type())) return false;
        }
        if (!expect(TSemi, "';'")) return false;
        Node step;
        step.kind = Node::Block;
        step.text = "skip";
        if (!is(TRParen)) {
            if (!parse_expr(0, &step)) return false;
        }
        if (!expect(TRParen, "')'")) return false;
        Node body;
        brk_dbase.push_back(dscopes.size());
        cont_dbase.push_back(dscopes.size());
        const bool body_ok = parse_stmt(&body, locals);
        brk_dbase.pop_back();
        cont_dbase.pop_back();
        if (!body_ok) return false;
        out->kids.push_back(std::move(init));
        out->kids.push_back(std::move(cond));
        out->kids.push_back(std::move(step));
        out->kids.push_back(std::move(body));
        if (decl_scope) {
            Node loop = std::move(*out);
            *out = Node{};
            out->kind = Node::Block;
            out->text = "scope";
            out->kids.push_back(std::move(decl_init));
            out->kids.push_back(std::move(loop));
            std::vector<Node> dt;
            if (!dtor_nodes(dscopes.size() - 1, &dt)) return false;
            for (Node& n : dt) out->kids.push_back(std::move(n));
            dscopes.pop_back();
            pop_scope();
        }
        return true;
    }
    if (is(TDo)) {
        next();
        out->kind = Node::Do;
        Node body;
        brk_dbase.push_back(dscopes.size());
        cont_dbase.push_back(dscopes.size());
        const bool body_ok = parse_stmt(&body, locals);
        brk_dbase.pop_back();
        cont_dbase.pop_back();
        if (!body_ok) return false;
        if (!is(TWhile)) {
            error = "expected while";
            return false;
        }
        next();
        if (!expect(TLParen, "'('")) return false;
        Node cond;
        if (!parse_expr(0, &cond)) return false;
        if (!convert_class(&cond, bool_type())) return false;
        if (!expect(TRParen, "')'")) return false;
        if (!expect(TSemi, "';'")) return false;
        out->kids.push_back(std::move(body));
        out->kids.push_back(std::move(cond));
        return true;
    }
    if (is(TSwitch)) {
        next();
        out->kind = Node::Switch;
        if (!expect(TLParen, "'('")) return false;
        Node cond;
        if (!parse_expr(0, &cond)) return false;
        if (!expect(TRParen, "')'")) return false;
        if (!expect(TLBrace, "'{'")) return false;
        out->kids.push_back(std::move(cond));
        brk_dbase.push_back(dscopes.size());
        struct PopBrk {
            std::vector<size_t>& v;
            ~PopBrk() { v.pop_back(); }
        } pop_brk{brk_dbase};
        while (!is(TRBrace) && !is(TEnd)) {
            Node arm;
            arm.kind = Node::Case;
            if (is(TCase)) {
                next();
                if (!parse_const(&arm.number, "a case value")) return false;
                if (!expect(TColon, "':'")) return false;
            } else if (is(TDefault)) {
                next();
                arm.op = TDefault;
                if (!expect(TColon, "':'")) return false;
            } else {
                error = "expected case or default";
                return false;
            }
            while (!is(TCase) && !is(TDefault) && !is(TRBrace) && !is(TEnd)) {
                Node stmt;
                if (!parse_stmt(&stmt, locals)) return false;
                arm.kids.push_back(std::move(stmt));
            }
            out->kids.push_back(std::move(arm));
        }
        return expect(TRBrace, "'}'");
    }
    if (is(TIdent) && cur().text == "delete") return parse_delete(out);
    if (is(TBreak) || is(TContinue)) {
        const bool is_break = is(TBreak);
        next();
        std::vector<Node> dt;
        const std::vector<size_t>& bases = is_break ? brk_dbase : cont_dbase;
        if (!bases.empty() && !dtor_nodes(bases.back(), &dt)) return false;
        Node jump;
        jump.kind = is_break ? Node::Break : Node::Continue;
        if (dt.empty()) {
            *out = std::move(jump);
        } else {
            out->kind = Node::Block;
            for (Node& n : dt) out->kids.push_back(std::move(n));
            out->kids.push_back(std::move(jump));
        }
        return expect(TSemi, "';'");
    }
    if (is(TReturn)) {
        next();
        std::vector<Node> dt;
        Node ret;
        ret.kind = Node::Return;
        Node value;
        bool has_value = false;
        bool braced_ret = false;
        bool dt_ready = false;
        if (is(TLBrace) && !cur_ret_ref && cur_ret.base == 3 && cur_ret.pointers == 0 && !cur_ret.is_ref &&
            cur_ret.struct_id >= 0) {
            // return {a, b}; ([stmt.return], [dcl.init.list]): the object is made in a hidden local, by a
            // constructor taking the list, or member by member for an aggregate, and that local is returned.
            Local hidden;
            hidden.name = "__br" + std::to_string(rv_count++);
            hidden.type = cur_ret;
            place_local(locals, hidden);
            if (obj_needs_ctor(cur_ret)) {
                next();
                std::vector<Node> cargs;
                if (!is(TRBrace)) {
                    do {
                        Node a;
                        if (!parse_expr(1, &a)) return false;
                        cargs.push_back(std::move(a));
                    } while (accept(TComma));
                }
                if (!expect(TRBrace, "'}'")) return false;
                Node call;
                if (!special_call(cur_ret.struct_id, "<ctor>", node_addr(node_name(hidden.name)), std::move(cargs), &call))
                    return false;
                stmt_pre.push_back(std::move(call));
            } else {
                Node init;
                if (!parse_init(&init)) return false;
                Node assign;
                assign.kind = Node::Assign;
                assign.op = TAssign;
                assign.number = 1;
                assign.kids.push_back(node_name(hidden.name));
                assign.kids.push_back(std::move(init));
                Node stmt;
                stmt.kind = Node::ExprStmt;
                stmt.kids.push_back(std::move(assign));
                stmt_pre.push_back(std::move(stmt));
            }
            value = node_name(hidden.name);
            braced_ret = true;
        }
        if (braced_ret || !accept(TSemi)) {
            if (!braced_ret && !parse_expr(0, &value)) return false;
            if (lam_probe && !lam_probe_seen && !braced_ret) {
                Type seen;
                if (static_type(value, &seen)) {
                    seen.is_ref = false;
                    seen.pconst = false;
                    if (seen.is_array()) {
                        seen = peel_array(seen);
                        seen.pointers += 1;
                    }
                    lam_probe_type = seen;
                    lam_probe_seen = true;
                }
            }
            if (!braced_ret && cur_ret.base != 3 && cur_ret.base != 0 && !cur_ret_ref && !convert_class(&value, cur_ret))
                return false;
            has_value = true;
            if (!expect(TSemi, "';'")) return false;
            dtor_skip.clear();
            if (value.kind == Node::Name && !cur_ret_ref && obj_needs_dtor(cur_ret)) {
                // return x; where x is a local object of the result class, or a temporary made for this
                // statement: the object itself becomes the result, so its destructor does not run here.
                if (value.text.compare(0, 4, "__bt") == 0 || value.text.compare(0, 4, "__br") == 0) {
                    for (size_t ti = 0; ti < stmt_temps.size(); ++ti)
                        if (stmt_temps[ti].name == value.text) {
                            stmt_temps.erase(stmt_temps.begin() + static_cast<std::ptrdiff_t>(ti));
                            break;
                        }
                } else {
                    for (const auto& scope : dscopes)
                        for (const DObj& d : scope)
                            if (d.kind == 0 && d.count == 0 && d.name == value.text && d.sid == cur_ret.struct_id)
                                dtor_skip = value.text;
                }
            }
            if (!dtor_nodes(fn_dbase, &dt)) return false;
            dtor_skip.clear();
            dt_ready = true;
            if (cur_ret_ref) value = node_addr(std::move(value));  // a reference result is the address
            // Hidden argument copies of the return expression go first.
            std::vector<Node> tdt;
            for (size_t i = stmt_temps.size(); i-- > 0;) {
                Node call;
                if (!special_call(stmt_temps[i].sid, "<dtor>", node_addr(node_name(stmt_temps[i].name)), {}, &call))
                    return false;
                tdt.push_back(std::move(call));
            }
            stmt_temps.clear();
            dt.insert(dt.begin(), std::make_move_iterator(tdt.begin()), std::make_move_iterator(tdt.end()));
        } else if (cur_special == 1) {
            value = node_name("this");
            has_value = true;
        }
        if (!dt_ready && !dtor_nodes(fn_dbase, &dt)) return false;
        if (dt.empty()) {
            if (has_value) ret.kids.push_back(std::move(value));
            *out = std::move(ret);
            return true;
        }
        // The value is computed first, then the objects are destroyed, then the function returns.
        out->kind = Node::Block;
        if (has_value) {
            Local hidden;
            hidden.name = "__rv" + std::to_string(rv_count++);
            hidden.type = cur_ret;
            place_local(locals, hidden);
            Node assign;
            assign.kind = Node::Assign;
            assign.op = TAssign;
            assign.number = 1;
            assign.kids.push_back(node_name(hidden.name));
            assign.kids.push_back(std::move(value));
            Node stmt;
            stmt.kind = Node::ExprStmt;
            stmt.kids.push_back(std::move(assign));
            out->kids.push_back(std::move(stmt));
            for (Node& n : dt) out->kids.push_back(std::move(n));
            ret.kids.push_back(node_name(hidden.name));
        } else {
            for (Node& n : dt) out->kids.push_back(std::move(n));
        }
        out->kids.push_back(std::move(ret));
        return true;
    }
    if (is_decl_start()) {
        bool is_typedef = false;
        bool is_static = false;
        bool is_const = false;
        while (is(TTypedef) || is(TStatic) || is(TConst)) {
            if (accept(TTypedef)) is_typedef = true;
            else if (accept(TStatic)) is_static = true;
            else if (accept(TConst)) is_const = true;
        }
        Type type;
        bool imp = false;
        bool exp = false;
        if (!is_typedef && is(TIdent) && cur().text == "auto") {
            // auto x = e; ([dcl.spec.auto]): the type of e, without a reference, a top-level const or an array.
            next();
            const size_t at_declarator = at;
            size_t k = at;
            while (k < tokens.size() && (tokens[k].kind == TAmp || tokens[k].kind == TStar || tokens[k].kind == TConst)) ++k;
            if (!(k + 1 < tokens.size() && tokens[k].kind == TIdent && tokens[k + 1].kind == TAssign)) {
                error = "auto needs a name and an initializer";
                return false;
            }
            at = k + 2;
            Node init;
            if (!parse_init(&init)) return false;
            const size_t after_init = at;
            Type t;
            if (!static_type(init, &t)) {
                error = "cannot work out the type of the auto variable";
                return false;
            }
            t.is_ref = false;
            t.pconst = false;
            if (t.is_array()) {
                t = peel_array(t);
                t.pointers += 1;
            }
            if (tokens[at_declarator].kind == TStar && t.pointers > 0) t.pointers -= 1;
            at = at_declarator;
            type = t;
            has_preset = true;
            preset_init = std::move(init);
            preset_end = after_init;
        } else if (!parse_type(&type, &imp, &exp)) {
            return false;
        }
        if (!is_typedef && is(TSemi)) {
            next();
            out->kind = Node::Block;
            return true;
        }
        Type declared_type = type;
        bool more = true;
        Node combined;
        combined.kind = Node::Block;
        size_t count = 0;
        while (more) {
            Node one;
            if (!parse_local_decl_one(declared_type, is_typedef, is_static, is_const, &one, locals, stmt_line, &more))
                return false;
            ++count;
            if (!more && count == 1) {
                *out = std::move(one);
                return true;
            }
            combined.kids.push_back(std::move(one));
        }
        *out = std::move(combined);
        return true;
    }
    out->kind = Node::ExprStmt;
    Node expr;
    if (!parse_expr(0, &expr)) return false;
    {
        // f(); where f returns a class in memory: the result goes into a hidden local that is destroyed after the statement.
        Type result;
        if (expr.kind == Node::Call && cur_locals != nullptr && static_type(expr, &result) && result.base == 3 &&
            result.pointers == 0 && !result.is_ref && !result.is_array() && result.struct_id >= 0 && !fits_register(result)) {
            Local hidden;
            hidden.name = "__bt" + std::to_string(tmp_count++);
            hidden.type = result;
            place_local(cur_locals, hidden);
            Node assign;
            assign.kind = Node::Assign;
            assign.op = TAssign;
            assign.number = 1;
            assign.kids.push_back(node_name(hidden.name));
            assign.kids.push_back(std::move(expr));
            expr = std::move(assign);
            if (class_needs_dtor(structs[static_cast<size_t>(result.struct_id)]))
                stmt_temps.push_back(Temp{hidden.name, result.struct_id});
        }
    }
    out->kids.push_back(std::move(expr));
    return expect(TSemi, "';'");
}

// One declarator of a local declaration `T a = 1, *b;` ([dcl.dcl]): type is the part before the first
// declarator. *more tells whether a ',' followed, so another declarator of the same type comes next.
bool Parser::parse_local_decl_one(const Type& type, bool is_typedef, bool is_static, bool is_const, Node* out,
                                  std::vector<Local>* locals, int stmt_line, bool* more) {
    Declarator decl;
    std::vector<Node> ctor_args;
    bool have_ctor_args = false;
    Node paren_init;
    bool have_paren_init = false;
    if (!is_typedef && obj_needs_ctor(type) && is(TIdent) && at + 1 < tokens.size() &&
        (tokens[at + 1].kind == TLParen || tokens[at + 1].kind == TLBrace)) {
        // C x(args); and C x{args}; the arguments go to a constructor.
        decl.name = cur().text;
        decl.type = type;
        next();
        const bool braced_args = is(TLBrace);
        Type list_type;
        if (braced_args && tokens[at + 1].kind != TRBrace && has_init_list_ctor(type.struct_id, &list_type)) {
            Node list;
            if (!init_list_temp(list_type, &list)) return false;
            ctor_args.push_back(std::move(list));
            have_ctor_args = true;
        } else {
        next();
        if (braced_args) {
            if (!is(TRBrace)) {
                do {
                    Node a;
                    if (!parse_expr(1, &a)) return false;
                    ctor_args.push_back(std::move(a));
                } while (accept(TComma));
            }
            if (!expect(TRBrace, "'}'")) return false;
        } else if (!parse_call_args(&ctor_args)) {
            return false;
        }
        have_ctor_args = true;
        }
    } else if (!is_typedef && !obj_needs_ctor(type) && !type.is_array() &&
               [&]() {
                   // `int x(5);` and `int* p(&v);` ([dcl.init]): direct-initialisation of a scalar or an aggregate, when
                   // what is in the parentheses is an expression and not a parameter list.
                   size_t k = at;
                   while (k < tokens.size() && tokens[k].kind == TStar) ++k;
                   if (!(k + 2 < tokens.size() && tokens[k].kind == TIdent && tokens[k + 1].kind == TLParen &&
                         tokens[k + 2].kind != TRParen))
                       return false;
                   const size_t save = at;
                   at = k + 2;
                   const bool is_type = is_decl_start();
                   at = save;
                   return !is_type;
               }()) {
        decl.type = type;
        while (accept(TStar)) ++decl.type.pointers;
        decl.name = cur().text;
        next();
        next();
        if (!parse_expr(1, &paren_init)) return false;
        if (!expect(TRParen, "')'")) return false;
        have_paren_init = true;
    } else if (!parse_declarator(type, &decl)) {
        return false;
    }
    if (is_typedef) {
        decl.type.is_const = is_const;
        typedefs[decl.name] = decl.type;
        return end_decl(more);
    }
    if (decl.is_function) {
        error = "a function cannot be nested";
        return false;
    }
    decl.type.is_const = is_const;
    if (decl.type.base == 3 && decl.type.pointers == 0 && !decl.type.is_ref && decl.type.struct_id >= 0 &&
        structs[static_cast<size_t>(decl.type.struct_id)].incomplete) {
        error = "variable " + decl.name + " has the incomplete type " + structs[static_cast<size_t>(decl.type.struct_id)].name;
        return false;
    }
    Local local;
    local.name = decl.name;
    local.type = decl.type;
    local.is_const = is_const && decl.type.pointers == 0;  // `const T* p` is a pointer to const: p itself may change
    local.is_static = is_static;
    Node init;
    bool has_init = false;
    const bool preset = has_preset && (is(TAssign) || is(TColon));
    Type list_type_local;
    if (preset || have_paren_init || accept(TAssign) || (is(TLBrace) && !have_ctor_args)) {
        if (preset) {
            init = std::move(preset_init);
            at = preset_end;
            has_preset = false;
        } else if (have_paren_init) {
            init = std::move(paren_init);
        } else if (is(TLBrace) && !decl.type.is_array() && obj_needs_ctor(decl.type) &&
                   tokens[at + 1].kind != TRBrace && has_init_list_ctor(decl.type.struct_id, &list_type_local)) {
            // C x = {a, b}; for a class with an initializer-list constructor.
            Node list;
            if (!init_list_temp(list_type_local, &list)) return false;
            ctor_args.push_back(std::move(list));
            have_ctor_args = true;
            has_init = false;
            init = Node{};
            init.kind = Node::Block;
            init.text = "skipped";
        } else if (!parse_init(&init)) {
            return false;
        }
        if (init.kind == Node::Block && init.text == "skipped") has_init = false; else
        has_init = true;
        if (!decl.type.is_array() && decl.type.base != 3 && decl.type.base != 0 && !convert_class(&init, decl.type))
            return false;
        if (init.kind == Node::Block && init.text == "init" && decl.type.array_len != 0) init = elide_braces(decl.type, init);
        if (!fix_unsized(&decl.type, init)) return false;
        local.type = decl.type;
        if (!is_static && wants_copy(decl.type, init)) {
            // C x = y; with y an object of the class: the copy constructor.
            ctor_args.push_back(std::move(init));
            have_ctor_args = true;
            has_init = false;
        }        else if (!is_static && !decl.type.is_array() && obj_needs_ctor(decl.type) && init.kind != Node::Block) {
            // C x = value; with a value that is not an object of a class (std::string s = "abc"; V v = 5;): a converting
            // constructor makes x from it ([dcl.init], copy-initialisation, with the copy elided).
            Type from;
            if (static_type(init, &from) && !(from.base == 3 && from.pointers == 0 && !from.is_array())) {
                ctor_args.push_back(std::move(init));
                have_ctor_args = true;
                has_init = false;
            }
        }
    }
    bool static_dyn = false;
    std::vector<DynLeaf> static_leaves;
    if (is_static) {
        Global global;
        global.name = current_fn + "_" + decl.name + "_" + std::to_string(globals.size());
        global.type = decl.type;
        global.is_static = true;
        global.bss = true;
        if (has_init && obj_needs_ctor(decl.type) && !decl.type.is_array() && !(init.kind == Node::Block && init.text == "init")) {
            static_dyn = true;  // static C x = value; : a constructor under the guard below
        } else if (has_init) {
            global.bss = false;
            // A static array of string pointers: the addresses are relocations. A part that is not a constant
            // (static T v = f();) is stored the first time control passes the declaration, under a guard ([stmt.dcl]).
            if (!flatten_dyn(decl.type, init, &global, &static_leaves)) return false;
            has_init = false;
        }
        local.symbol = global.name;
        globals.push_back(global);
    }
    place_local(locals, local);
    out->kind = Node::Block;
    Node static_prvalue;  // static C x = f(); with f returning a C: f writes x itself ([dcl.init], [class.copy.elision])
    if (static_dyn && obj_needs_ctor(decl.type) && !decl.type.is_array() && !(init.kind == Node::Block && init.text == "init")) {
        if (class_prvalue(decl.type, init)) {
            static_prvalue.kind = Node::Assign;
            static_prvalue.op = TAssign;
            static_prvalue.number = 1;
            static_prvalue.kids.push_back(node_name(decl.name));
            static_prvalue.kids.push_back(std::move(init));
        } else {
            // static C x = value; : the constructor that takes the value, under the guard below.
            ctor_args.push_back(std::move(init));
            have_ctor_args = true;
        }
        has_init = false;
    }
    if (!static_leaves.empty()) {
        if (obj_needs_ctor(decl.type) || obj_needs_dtor(decl.type)) {
            error = "a static local object with a destructor and an initializer that is not constant is not supported";
            return false;
        }
        Node then_block;
        if (!static_guard_block(local.symbol, &then_block)) return false;
        Node stores;
        stores.kind = Node::Block;
        stores.line = stmt_line;
        stores.kids = dyn_leaf_stmts(decl.name, decl.type, &static_leaves);
        finish_static_guard(&then_block, std::move(stores), local.symbol, out);
        return end_decl(more);
    }
    if (decl.type.base == 3 && decl.type.pointers == 0 && !decl.type.is_ref && decl.type.struct_id >= 0 &&
        structs[static_cast<size_t>(decl.type.struct_id)].poly) {
        // The table pointer is set where the object is declared (an array or static object gets
        // it from its constructors).
        if (!decl.type.is_array() && !is_static) {
        const StructDef& cls = structs[static_cast<size_t>(decl.type.struct_id)];
        Node table_name;
        table_name.kind = Node::Name;
        table_name.text = cls.vtable_sym;
        Node table_addr;
        table_addr.kind = Node::Unary;
        table_addr.op = TAmp;
        table_addr.kids.push_back(std::move(table_name));
        Node as_ptr;
        as_ptr.kind = Node::Unary;
        as_ptr.type.base = 0;
        as_ptr.type.pointers = 2;
        as_ptr.kids.push_back(std::move(table_addr));
        Node object;
        object.kind = Node::Name;
        object.text = decl.name;
        Node field;
        field.kind = Node::Field;
        field.op = TDot;
        field.text = "__vptr";
        field.kids.push_back(std::move(object));
        Node store;
        store.kind = Node::Assign;
        store.op = TAssign;
        store.kids.push_back(std::move(field));
        store.kids.push_back(std::move(as_ptr));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.line = stmt_line;
        stmt.kids.push_back(std::move(store));
        out->kids.push_back(std::move(stmt));
        }
    }
    if (obj_needs_ctor(decl.type) || obj_needs_dtor(decl.type)) {
        const int sid = decl.type.struct_id;
        if (decl.type.is_array() && !decl.type.more.empty()) {
            error = "an array of objects needs one dimension";
            return false;
        }
        if (is_static) {
            // A static local object: built the first time control reaches the declaration (a
            // guard flag in a hidden global), destroyed by the program end function.
            if (has_init || decl.type.is_array()) {
                error = "a static local object takes no braced initializer and is not an array";
                return false;
            }
            Node then_block;
            if (!static_guard_block(local.symbol, &then_block)) return false;
            Node call;
            if (static_prvalue.kind == Node::Assign) {
                call.kind = Node::ExprStmt;
                call.kids.push_back(std::move(static_prvalue));
            } else if (obj_needs_ctor(decl.type)) {
                if (!special_call(sid, "<ctor>", node_addr(node_name(local.symbol)), std::move(ctor_args), &call))
                    return false;
            }
            finish_static_guard(&then_block, std::move(call), local.symbol, out);
            if (obj_needs_dtor(decl.type)) {
                GlobalObj g;
                g.name = local.symbol;
                g.sid = sid;
                g.guard = local.symbol + "_guard";
                static_objs.push_back(std::move(g));
            }
            return end_decl(more);
        }
        if (decl.type.is_array()) {
            if (has_init) {
                std::vector<Node> calls;
                if (!array_init_calls(decl.name, decl.type, init, &calls)) return false;
                for (Node& c : calls) out->kids.push_back(std::move(c));
                has_init = false;
            } else if (obj_needs_ctor(decl.type)) {
                Node loop;
                if (!element_loop(decl.name, decl.type.array_len, false, sid, "<ctor>", &loop)) return false;
                out->kids.push_back(std::move(loop));
            }
            if (obj_needs_dtor(decl.type)) {
                std::vector<Node> rec_nodes;
                eh_push_object(&rec_nodes, node_name(decl.name), sid, decl.type.array_len, locals);
                for (Node& n : rec_nodes) out->kids.push_back(std::move(n));
                DObj obj;
                obj.name = decl.name;
                obj.sid = sid;
                obj.count = decl.type.array_len;
                if (dscopes.empty()) dscopes.emplace_back();
                dscopes.back().push_back(std::move(obj));
            }
            return end_decl(more);
        }
        if (obj_needs_ctor(decl.type) && !has_init) {
            Node call;
            if (!special_call(decl.type.struct_id, "<ctor>", node_addr(node_name(decl.name)),
                              std::move(ctor_args), &call))
                return false;
            call.line = stmt_line;
            out->kids.push_back(std::move(call));
        } else if (have_ctor_args) {
            error = "constructor arguments together with an initializer";
            return false;
        }
        if (obj_needs_dtor(decl.type)) {
            std::vector<Node> rec_nodes;
            eh_push_object(&rec_nodes, node_addr(node_name(decl.name)), decl.type.struct_id, 0, locals);
            for (Node& n : rec_nodes) out->kids.push_back(std::move(n));
            DObj obj;
            obj.name = decl.name;
            obj.sid = decl.type.struct_id;
            if (dscopes.empty()) dscopes.emplace_back();
            dscopes.back().push_back(std::move(obj));
        }
    }
    if (has_init) {
        Node assign;
        assign.kind = Node::Assign;
        assign.number = 1;
        Node name;
        name.kind = Node::Name;
        name.text = decl.name;
        assign.kids.push_back(std::move(name));
        assign.kids.push_back(std::move(init));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.line = stmt_line;
        stmt.kids.push_back(std::move(assign));
        out->kids.push_back(std::move(stmt));
    }
    return end_decl(more);
}



// The condition of `if` and `while`, after the '(' and up to and including the ')'. A declaration with an initializer
// (`if (Finger* f = Find(id))`, [stmt.select]) is made in a scope around the statement and its name is the condition.
bool Parser::parse_condition(Node* cond, Node* decl, bool* scoped, std::vector<Local>* locals) {
    *scoped = false;
    if (is_decl_start() && !is(TStatic) && !is(TTypedef)) {
        size_t rp = at;
        int depth = 0;
        size_t assign = 0;
        for (; rp < tokens.size() && tokens[rp].kind != TEnd; ++rp) {
            const Tok k = tokens[rp].kind;
            if (k == TLParen || k == TLBrack || k == TLBrace) ++depth;
            else if (k == TRParen || k == TRBrack || k == TRBrace) {
                if (depth == 0) break;
                --depth;
            } else if (depth == 0 && k == TAssign && assign == 0) assign = rp;
        }
        if (assign > at && tokens[assign - 1].kind == TIdent && rp < tokens.size() && tokens[rp].kind == TRParen) {
            const std::string name = tokens[assign - 1].text;
            *scoped = true;
            push_scope();
            dscopes.emplace_back();
            tokens[rp].kind = TSemi;  // the declaration ends at the ')'
            const bool ok = parse_stmt(decl, locals);
            tokens[rp].kind = TRParen;
            if (!ok) return false;
            *cond = node_name(name);
            return convert_class(cond, bool_type());
        }
    }
    if (!parse_expr(0, cond)) return false;
    if (!convert_class(cond, bool_type())) return false;
    return expect(TRParen, "')'");
}

void Parser::close_condition_scope(Node* stmt, Node decl, bool scoped) {
    if (!scoped) return;
    Node inner = std::move(*stmt);
    *stmt = Node{};
    stmt->kind = Node::Block;
    stmt->text = "scope";
    stmt->kids.push_back(std::move(decl));
    stmt->kids.push_back(std::move(inner));
    std::vector<Node> dt;
    if (dtor_nodes(dscopes.size() - 1, &dt))
        for (Node& n : dt) stmt->kids.push_back(std::move(n));
    dscopes.pop_back();
    pop_scope();
}

// for (T x : array) body ([stmt.ranged]) over an array whose length is known: an index runs over it and the
// variable is declared, in each pass, from the element (a reference for T&).
bool Parser::parse_range_for(Node* out, std::vector<Local>* locals, int stmt_line) {
    const size_t decl_start = at;
    size_t colon = at;
    int depth = 0;
    for (; colon < tokens.size() && tokens[colon].kind != TEnd; ++colon) {
        const Tok k = tokens[colon].kind;
        if (k == TLParen || k == TLBrack || k == TLBrace) ++depth;
        else if (k == TRParen || k == TRBrack || k == TRBrace) --depth;
        else if (depth == 0 && k == TColon) break;
    }
    at = colon + 1;
    Node range;
    Node list_init;
    bool braced_range = false;
    Type range_type;
    if (is(TLBrace)) {
        // for (T x : {a, b, c}): the list is stored in a hidden array that the loop runs over ([stmt.ranged]).
        if (!parse_init(&list_init)) return false;
        if (!expect(TRParen, "')'")) return false;
        if (list_init.kids.empty()) {
            error = "a range for over an empty list is not supported";
            return false;
        }
        const size_t after_list = at;
        at = decl_start;
        while (accept(TConst)) {}
        Type elem_type;
        if (is(TIdent) && cur().text == "auto") {
            if (!static_type(list_init.kids[0], &elem_type)) {
                error = "cannot work out the type of the range elements";
                return false;
            }
            elem_type.is_ref = false;
            if (elem_type.is_array()) {
                elem_type = peel_array(elem_type);
                elem_type.pointers += 1;
            }
        } else {
            bool imp = false;
            bool exp = false;
            if (!parse_type(&elem_type, &imp, &exp)) return false;
            while (accept(TStar)) ++elem_type.pointers;
        }
        elem_type.is_ref = false;
        at = after_list;
        Local array_local;
        array_local.name = "__rl" + std::to_string(range_count++);
        array_local.type = elem_type;
        array_local.type.array_len = static_cast<int>(list_init.kids.size());
        place_local(locals, array_local);
        range = node_name(array_local.name);
        range_type = array_local.type;
        braced_range = true;
    } else if (!parse_expr(0, &range)) {
        return false;
    } else if (!expect(TRParen, "')'")) {
        return false;
    }
    const size_t after_range = at;
    if (!braced_range && !static_type(range, &range_type)) {
        error = "a range for needs an array of known length";
        return false;
    }
    if (!braced_range && range.kind == Node::Call && range_type.base == 3 && range_type.pointers == 0 &&
        !range_type.is_array() && range_type.struct_id >= 0 && cur_locals != nullptr) {
        // for (x : f()): the object the call returns goes into a hidden local before the loop and is destroyed after it.
        Local hidden;
        hidden.name = "__bt" + std::to_string(tmp_count++);
        hidden.type = range_type;
        place_local(cur_locals, hidden);
        Node assign;
        assign.kind = Node::Assign;
        assign.op = TAssign;
        assign.number = 1;
        assign.kids.push_back(node_name(hidden.name));
        assign.kids.push_back(std::move(range));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(assign));
        stmt_pre.push_back(std::move(stmt));
        if (class_needs_dtor(structs[static_cast<size_t>(range_type.struct_id)]))
            stmt_temps.push_back(Temp{hidden.name, range_type.struct_id});
        range = node_name(hidden.name);
    }
    // A class with begin() and end() that return pointers: the pointer runs from one to the other ([stmt.ranged]).
    bool class_range = false;
    Type iter_type;
    Node begin_call, end_call;
    if (range_type.base == 3 && range_type.pointers == 0 && !range_type.is_array() && range_type.struct_id >= 0 &&
        (range.kind == Node::Name || range.kind == Node::Field || range.kind == Node::Index ||
         (range.kind == Node::Unary && range.op == TStar))) {
        const Method* mb = find_method(range_type.struct_id, "begin", 0);
        const Method* me = find_method(range_type.struct_id, "end", 0);
        if (mb == nullptr || me == nullptr || mb->ret.pointers < 1 || me->ret.pointers != mb->ret.pointers) {
            error = "a range for over a class needs begin() and end() that return pointers";
            return false;
        }
        class_range = true;
        iter_type = mb->ret;
        for (int w = 0; w < 2; ++w) {
            const Method* m = w == 0 ? mb : me;
            Node call;
            call.kind = Node::Call;
            call.text = m->qname;
            call.number = 1;
            call.type = m->ret;
            call.kids.push_back(node_addr(range));
            (w == 0 ? begin_call : end_call) = std::move(call);
        }
    } else if (!range_type.is_array() || range_type.array_len < 0) {
        error = "a range for needs an array of known length";
        return false;
    }
    const int count = class_range ? 0 : range_type.array_len;
    Type elem = class_range ? iter_type : peel_array(range_type);
    if (class_range) elem.pointers -= 1;
    elem.is_const = false;
    at = decl_start;
    bool is_const = false;
    while (accept(TConst)) is_const = true;
    Type declared;
    if (is(TIdent) && cur().text == "auto") {
        next();
        declared = elem;
        if (is(TStar) && declared.pointers > 0) declared.pointers -= 1;
    } else {
        bool imp = false;
        bool exp = false;
        if (!parse_type(&declared, &imp, &exp)) return false;
    }
    const int saved_depth = scope_depth;
    push_scope();
    dscopes.emplace_back();
    Local index;
    index.name = "__ri" + std::to_string(range_count++);
    index.type.base = 1;
    Local range_end;
    range_end.name = "__re" + std::to_string(range_count++);
    if (class_range) {
        index.type = iter_type;
        range_end.type = iter_type;
    }
    place_local(locals, index);
    if (class_range) place_local(locals, range_end);
    Node element;
    if (class_range) {
        element.kind = Node::Unary;
        element.op = TStar;
        element.kids.push_back(node_name(index.name));
    } else {
        element.kind = Node::Index;
        element.kids.push_back(range);
        element.kids.push_back(node_name(index.name));
    }
    has_preset = true;
    preset_init = std::move(element);
    preset_end = after_range;
    range_decl = true;
    Node decl_node;
    bool more = false;
    const bool decl_ok = parse_local_decl_one(declared, false, false, is_const, &decl_node, locals, stmt_line, &more);
    range_decl = false;
    has_preset = false;
    if (!decl_ok) return false;
    at = after_range;
    Node body;
    brk_dbase.push_back(dscopes.size());
    cont_dbase.push_back(dscopes.size());
    const bool body_ok = parse_stmt(&body, locals);
    brk_dbase.pop_back();
    cont_dbase.pop_back();
    if (!body_ok) return false;
    Node init;
    init.kind = Node::Assign;
    init.op = TAssign;
    init.number = 1;
    init.kids.push_back(node_name(index.name));
    if (class_range) {
        init.kids.push_back(std::move(begin_call));
    } else {
        Node zero;
        zero.kind = Node::IntLit;
        zero.type.base = 1;
        init.kids.push_back(std::move(zero));
    }
    Node cond;
    cond.kind = Node::Binary;
    cond.op = class_range ? TNe : TLt;
    cond.kids.push_back(node_name(index.name));
    if (class_range) {
        cond.kids.push_back(node_name(range_end.name));
    } else {
        Node limit;
        limit.kind = Node::IntLit;
        limit.number = static_cast<uint64_t>(count);
        limit.type.base = 1;
        cond.kids.push_back(std::move(limit));
    }
    Node step;
    step.kind = Node::Unary;
    step.op = TPlusPlus;
    step.number = 0;
    step.kids.push_back(node_name(index.name));
    Node iteration;
    iteration.kind = Node::Block;
    iteration.kids.push_back(std::move(decl_node));
    iteration.kids.push_back(std::move(body));
    std::vector<Node> dt;
    if (!dtor_nodes(dscopes.size() - 1, &dt)) return false;
    for (Node& n : dt) iteration.kids.push_back(std::move(n));
    dscopes.pop_back();
    pop_scope();
    scope_depth = saved_depth;
    Node loop;
    loop.kind = Node::For;
    loop.kids.push_back(std::move(init));
    loop.kids.push_back(std::move(cond));
    loop.kids.push_back(std::move(step));
    loop.kids.push_back(std::move(iteration));
    // The loop's variables live in a scope around it, as for `for (int i = ...)`.
    *out = Node{};
    out->kind = Node::Block;
    out->text = "scope";
    if (braced_range) {
        Node store;
        store.kind = Node::Assign;
        store.number = 1;
        store.kids.push_back(range);
        store.kids.push_back(std::move(list_init));
        Node store_stmt;
        store_stmt.kind = Node::ExprStmt;
        store_stmt.kids.push_back(std::move(store));
        out->kids.push_back(std::move(store_stmt));
    }
    if (class_range) {
        // The end pointer is taken once, before the loop.
        Node assign;
        assign.kind = Node::Assign;
        assign.op = TAssign;
        assign.number = 1;
        assign.kids.push_back(node_name(range_end.name));
        assign.kids.push_back(std::move(end_call));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(assign));
        out->kids.push_back(std::move(stmt));
    }
    out->kids.push_back(std::move(loop));
    return true;
}


}  // namespace compiler
}  // namespace okayms
