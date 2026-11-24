// The optimizer: constant propagation and inlining.
// Moved out of compile.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "compile_internal.hpp"
#include "gen.hpp"

namespace okayms {
namespace compiler {
bool address_taken(const Node& node, const std::string& name) {
    if (node.kind == Node::Unary && node.op == TAmp && !node.kids.empty() && node.kids[0].kind == Node::Name &&
        node.kids[0].text == name)
        return true;
    for (const Node& kid : node.kids)
        if (address_taken(kid, name)) return true;
    return false;
}

// True when the name is incremented, decremented or changed by a compound assignment: those go through the
// variable's address, so it cannot live in a register.
bool modified_in_place(const Node& node, const std::string& name) {
    if (node.kind == Node::Unary && (node.op == TPlusPlus || node.op == TMinusMinus) && !node.kids.empty() &&
        node.kids[0].kind == Node::Name && node.kids[0].text == name)
        return true;
    if (node.kind == Node::Assign && node.op != TAssign && node.op != TEnd && !node.kids.empty() &&
        node.kids[0].kind == Node::Name && node.kids[0].text == name)
        return true;
    for (const Node& kid : node.kids)
        if (modified_in_place(kid, name)) return true;
    return false;
}

bool expr_uses(const Node& node, const std::string& name) {
    if (node.kind == Node::Name && node.text == name) return true;
    for (const Node& kid : node.kids)
        if (expr_uses(kid, name)) return true;
    return false;
}

Node* statement_value(Node* node) {
    if (node->kind == Node::ExprStmt && node->kids.size() == 1) return &node->kids[0];
    return node;
}

// Forgets every name that is assigned, incremented or decremented anywhere inside the node.
void kill_assigned(const Node& node, std::map<std::string, uint64_t>* known) {
    if (node.kind == Node::Assign && !node.kids.empty() && node.kids[0].kind == Node::Name) known->erase(node.kids[0].text);
    if (node.kind == Node::Unary && (node.op == TPlusPlus || node.op == TMinusMinus) && !node.kids.empty() &&
        node.kids[0].kind == Node::Name)
        known->erase(node.kids[0].text);
    for (const Node& kid : node.kids) kill_assigned(kid, known);
}

// /O2 constant propagation: a local int or long long that was given a constant keeps that value until
// anything can change it. A loop forgets what its body assigns before the loop is read (the second round
// sees the new value), a branch works on a copy, a label or a case forgets everything (it can be reached
// from elsewhere), and the right side of && || ?: only runs sometimes.
void prop_node(Node* node, std::map<std::string, uint64_t>* known, const std::set<std::string>* shadowed,
               const std::map<std::string, Type>* ltypes) {
    using Known = std::map<std::string, uint64_t>;
    if (node->kind == Node::While || node->kind == Node::Do) {
        kill_assigned(*node, known);
        Known inner = *known;
        for (Node& kid : node->kids) prop_node(&kid, &inner, shadowed, ltypes);
        return;
    }
    if (node->kind == Node::For && node->kids.size() == 4) {
        prop_node(&node->kids[0], known, shadowed, ltypes);
        kill_assigned(*node, known);
        Known inner = *known;
        for (size_t i = 1; i < node->kids.size(); ++i) prop_node(&node->kids[i], &inner, shadowed, ltypes);
        return;
    }
    if (node->kind == Node::If && !node->kids.empty()) {
        prop_node(&node->kids[0], known, shadowed, ltypes);
        for (size_t i = 1; i < node->kids.size(); ++i) {
            Known inner = *known;
            prop_node(&node->kids[i], &inner, shadowed, ltypes);
        }
        kill_assigned(*node, known);
        return;
    }
    if (node->kind == Node::Ternary || (node->kind == Node::Binary && (node->op == TAndAnd || node->op == TOrOr))) {
        if (!node->kids.empty()) prop_node(&node->kids[0], known, shadowed, ltypes);
        for (size_t i = 1; i < node->kids.size(); ++i) {
            Known inner = *known;
            prop_node(&node->kids[i], &inner, shadowed, ltypes);
        }
        kill_assigned(*node, known);
        return;
    }
    if (node->kind == Node::Switch) {
        if (!node->kids.empty()) prop_node(&node->kids[0], known, shadowed, ltypes);
        kill_assigned(*node, known);
        Known inner = *known;
        for (size_t i = 1; i < node->kids.size(); ++i) prop_node(&node->kids[i], &inner, shadowed, ltypes);
        return;
    }
    if (node->kind == Node::Case || node->kind == Node::Label) known->clear();
    if (node->kind == Node::Block || node->kind == Node::Label || node->kind == Node::Case) {
        if (node->kind == Node::Block) {
            for (size_t i = 0; i + 1 < node->kids.size(); ++i) {
                Node* cur = statement_value(&node->kids[i]);
                Node* nxt = statement_value(&node->kids[i + 1]);
                // x = a; x = b; : the first store is dead unless b reads x or a does something else.
                if (cur->kind == Node::Assign && nxt->kind == Node::Assign && cur->kids.size() == 2 && nxt->kids.size() == 2 &&
                    cur->kids[0].kind == Node::Name && nxt->kids[0].kind == Node::Name && cur->kids[0].text == nxt->kids[0].text &&
                    (cur->op == TAssign || cur->op == TEnd) && (nxt->op == TAssign || nxt->op == TEnd) &&
                    !expr_uses(nxt->kids[1], cur->kids[0].text) && !has_effects(cur->kids[1])) {
                    node->kids[i].kind = Node::Block;
                    node->kids[i].kids.clear();
                }
            }
        }
        for (Node& kid : node->kids) prop_node(&kid, known, shadowed, ltypes);
        return;
    }
    if (node->kind == Node::Assign && !node->kids.empty() && node->kids[0].kind == Node::Name &&
        (node->op == TAssign || node->op == TEnd)) {
        prop_node(&node->kids[1], known, shadowed, ltypes);
        if (shadowed != nullptr && shadowed->count(node->kids[0].text) != 0) {
            known->erase(node->kids[0].text);
            return;
        }
        // Only an int or long long local keeps a known value, cut to its type (a char or a bool would need
        // its own conversion, a float its own bits).
        const auto lt = ltypes != nullptr ? ltypes->find(node->kids[0].text) : std::map<std::string, Type>::const_iterator();
        const bool plain = ltypes != nullptr && lt != ltypes->end() && lt->second.base == 1 && lt->second.pointers == 0 &&
                           !lt->second.is_array() && !lt->second.is_ref && !lt->second.is_bool && lt->second.bits < 0;
        if (plain && node->kids[1].kind == Node::IntLit && node->kids[1].type.base < 5)
            (*known)[node->kids[0].text] = const_norm(node->kids[1].number, lt->second);
        else known->erase(node->kids[0].text);
        return;
    }
    if (node->kind == Node::Assign && !node->kids.empty() && node->kids[0].kind == Node::Name) {
        // x += e: x changes, and is not an operand that can be replaced by its old value.
        for (size_t i = 1; i < node->kids.size(); ++i) prop_node(&node->kids[i], known, shadowed, ltypes);
        known->erase(node->kids[0].text);
        return;
    }
    if (node->kind == Node::Unary && (node->op == TPlusPlus || node->op == TMinusMinus) && !node->kids.empty() &&
        node->kids[0].kind == Node::Name) {
        known->erase(node->kids[0].text);
        return;
    }
    if (node->kind == Node::Unary && node->op == TAmp && !node->kids.empty() && node->kids[0].kind == Node::Name) return;
    if (node->kind == Node::Name && known->count(node->text) != 0 &&
        (shadowed == nullptr || shadowed->count(node->text) == 0)) {
        const uint64_t value = (*known)[node->text];
        const auto lt = ltypes != nullptr ? ltypes->find(node->text) : std::map<std::string, Type>::const_iterator();
        *node = Node{};
        node->kind = Node::IntLit;
        node->number = value;
        node->type.base = 1;
        if (ltypes != nullptr && lt != ltypes->end()) {
            node->type.wide = lt->second.wide;
            node->type.is_unsigned = lt->second.is_unsigned;
        }
        return;
    }
    for (Node& kid : node->kids) prop_node(&kid, known, shadowed, ltypes);
    if (node->kind == Node::Binary && node->kids.size() == 2 && node->kids[0].kind == Node::IntLit &&
        node->kids[1].kind == Node::IntLit && node->kids[0].type.base < 5 && node->kids[1].type.base < 5) {
        Node folded_node;
        std::string fold_error;
        if (fold_int_binary(node->op, node->kids[0], node->kids[1], &folded_node, &fold_error)) *node = std::move(folded_node);
    }
}

bool contains_call(const Node& node) {
    if (node.kind == Node::Call) return true;
    for (const Node& kid : node.kids)
        if (contains_call(kid)) return true;
    return false;
}

const Node* single_return(const Node& body) {
    if (body.kind != Node::Block || body.kids.size() != 1) return nullptr;
    const Node& stmt = body.kids[0];
    if (stmt.kind != Node::Return || stmt.kids.size() != 1) return nullptr;
    if (contains_call(stmt.kids[0])) return nullptr;
    return &stmt.kids[0];
}

void subst_names(Node* node, const std::map<std::string, Node>& repl) {
    if (node->kind == Node::Name) {
        const auto found = repl.find(node->text);
        if (found != repl.end()) *node = found->second;
        return;
    }
    for (Node& kid : node->kids) subst_names(&kid, repl);
}

void inline_node(Node* node, const std::vector<FuncDecl>& funcs) {
    if (node->kind == Node::Call && !node->text.empty()) {
        const FuncDecl* fn = nullptr;
        for (const FuncDecl& item : funcs)
            if (item.name == node->text && item.defined) fn = &item;
        const bool simple = fn != nullptr && !fn->vararg && !fn->sret &&
                            (fn->param_names.empty() || fn->param_names[0] != "this") &&
                            static_cast<int>(node->kids.size()) == fn->nparams &&
                            static_cast<int>(fn->locals.size()) == fn->nparams;
        bool args_ok = simple;
        if (simple) {
            for (const Node& arg : node->kids) {
                if (arg.kind != Node::IntLit && arg.kind != Node::Name) args_ok = false;
            }
        }
        const Node* expr = simple && args_ok ? single_return(fn->body) : nullptr;
        if (expr != nullptr) {
            // The call converts each argument to its parameter type and the result to the return type;
            // the inlined expression keeps that with casts ([expr.call], [stmt.return]).
            const auto arithmetic = [](const Type& t) {
                return t.pointers == 0 && !t.is_array() && !t.is_ref && (t.base == 1 || t.base == 2 || t.base == 4 || t.base == 5 || t.base == 6);
            };
            const auto cast_to = [](const Type& t, Node inner) {
                Node cast;
                cast.kind = Node::Unary;
                cast.op = TEnd;
                cast.type = t;
                cast.kids.push_back(std::move(inner));
                return cast;
            };
            std::map<std::string, Node> repl;
            for (int i = 0; i < fn->nparams; ++i) {
                Node arg = node->kids[static_cast<size_t>(i)];
                if (arithmetic(fn->params[static_cast<size_t>(i)])) arg = cast_to(fn->params[static_cast<size_t>(i)], std::move(arg));
                repl[fn->param_names[static_cast<size_t>(i)]] = std::move(arg);
            }
            Node next = *expr;
            subst_names(&next, repl);
            if (arithmetic(fn->ret)) next = cast_to(fn->ret, std::move(next));
            *node = std::move(next);
            inline_node(node, funcs);
            return;
        }
    }
    for (Node& kid : node->kids) inline_node(&kid, funcs);
}

bool no_loop(const Node& node) {
    if (node.kind == Node::While || node.kind == Node::For || node.kind == Node::Do || node.kind == Node::Switch)
        return false;
    for (const Node& kid : node.kids)
        if (!no_loop(kid)) return false;
    return true;
}

void rename_names(Node* node, const std::map<std::string, std::string>& ren) {
    if (node->kind == Node::Name) {
        const auto found = ren.find(node->text);
        if (found != ren.end()) node->text = found->second;
    }
    for (Node& kid : node->kids) rename_names(&kid, ren);
}

void place_extra(FuncDecl* fn, Local local, const Gen& gen) {
    const int used = fn->locals.empty() ? 0 : -fn->locals.back().offset;
    int size = std::max(1, gen.size_of(local.type));
    int align = size >= 8 ? 8 : size >= 4 ? 4 : size >= 2 ? 2 : 1;
    const int aligned = (used + align - 1) & ~(align - 1);
    local.offset = -(aligned + size);
    local.scope = 0;
    local.reg = 0;
    fn->locals.push_back(local);
}

Node assign_name(const std::string& dest, Node rhs, uint64_t init_store = 0) {
    Node as;
    as.kind = Node::Assign;
    as.number = init_store;  // 1: the initializing store of a declaration or of a const global at startup
    Node lhs;
    lhs.kind = Node::Name;
    lhs.text = dest;
    as.kids.push_back(std::move(lhs));
    as.kids.push_back(std::move(rhs));
    Node stmt;
    stmt.kind = Node::ExprStmt;
    stmt.kids.push_back(std::move(as));
    return stmt;
}

bool try_inline_stmt(Node* stmt, FuncDecl* caller, const std::vector<FuncDecl>& funcs, Gen& gen, int* seq) {
    Node* call = nullptr;
    std::string dest;
    bool is_return = false;
    bool drop = false;
    uint64_t dest_init = 0;
    if (stmt->kind == Node::Return && stmt->kids.size() == 1 && stmt->kids[0].kind == Node::Call) {
        call = &stmt->kids[0];
        is_return = true;
    } else if (stmt->kind == Node::ExprStmt && !stmt->kids.empty() && stmt->kids[0].kind == Node::Assign &&
               stmt->kids[0].kids.size() == 2 && stmt->kids[0].kids[1].kind == Node::Call &&
               stmt->kids[0].kids[0].kind == Node::Name) {
        call = &stmt->kids[0].kids[1];
        dest = stmt->kids[0].kids[0].text;
        dest_init = stmt->kids[0].number;  // `const int k = f();`: still an initialization after inlining
    } else if (stmt->kind == Node::ExprStmt && !stmt->kids.empty() && stmt->kids[0].kind == Node::Call) {
        call = &stmt->kids[0];
        drop = true;
    } else {
        return false;
    }
    if (call->text.empty() || call->text == caller->name) return false;
    const FuncDecl* fn = nullptr;
    for (const FuncDecl& item : funcs)
        if (item.name == call->text && item.defined) fn = &item;
    if (fn == nullptr || fn->vararg || fn->sret || static_cast<int>(call->kids.size()) != fn->nparams) return false;
    // Member functions are not inlined: their body names this->field.
    if (!fn->param_names.empty() && fn->param_names[0] == "this") return false;
    // A reference parameter or result is not a plain value: the body would read the address as the value.
    if (fn->ret_ref) return false;
    for (bool is_ref : fn->param_ref)
        if (is_ref) return false;
    // A static local keeps its value between calls: it cannot become a local of the caller.
    for (const Local& static_local : fn->locals)
        if (static_local.is_static) return false;
    // A hidden local (__rv, __br, __tmp) is not copied into the caller, so such a body is not inlined.
    for (const Local& hidden_local : fn->locals) {
        bool is_param_name = false;
        for (const std::string& pn : fn->param_names)
            if (pn == hidden_local.name) is_param_name = true;
        if (!is_param_name && !hidden_local.name.empty() && hidden_local.name[0] == '_') return false;
    }
    if (!no_loop(fn->body) || contains_call(fn->body)) return false;
    std::map<std::string, std::string> ren;
    Node block;
    block.kind = Node::Block;
    for (int i = 0; i < fn->nparams; ++i) {
        const std::string tmp = "_arg" + std::to_string((*seq)++);
        Local local;
        local.name = tmp;
        local.type = fn->params[static_cast<size_t>(i)];
        place_extra(caller, local, gen);
        block.kids.push_back(assign_name(tmp, call->kids[static_cast<size_t>(i)]));
        if (i < static_cast<int>(fn->param_names.size())) ren[fn->param_names[static_cast<size_t>(i)]] = tmp;
    }
    for (const Local& local : fn->locals) {
        bool param = false;
        for (const std::string& pn : fn->param_names)
            if (pn == local.name) param = true;
        if (param || local.name.empty() || local.name[0] == '_') continue;
        const std::string tmp = "_loc" + std::to_string((*seq)++);
        Local copy = local;
        copy.name = tmp;
        copy.is_static = false;
        copy.symbol.clear();
        place_extra(caller, copy, gen);
        ren[local.name] = tmp;
    }
    const std::function<void(const Node&)> copy_stmts = [&](const Node& node) {
        if (node.kind == Node::Block) {
            for (const Node& kid : node.kids) copy_stmts(kid);
            return;
        }
        if (node.kind == Node::Return) {
            if (drop || node.kids.empty()) return;
            Node expr = node.kids[0];
            rename_names(&expr, ren);
            if (is_return) {
                Node ret;
                ret.kind = Node::Return;
                ret.kids.push_back(std::move(expr));
                block.kids.push_back(std::move(ret));
            } else {
                block.kids.push_back(assign_name(dest, std::move(expr), dest_init));
            }
            return;
        }
        Node cloned = node;
        rename_names(&cloned, ren);
        // The callee's blocks are not blocks of the caller: a "scope" block would take a scope number that the
        // caller's own locals (numbered when the caller was parsed) do not expect. Its locals were renamed and
        // placed in the caller already.
        const std::function<void(Node*)> flatten_scopes = [&](Node* n) {
            if (n->kind == Node::Block && n->text == "scope") n->text.clear();
            for (Node& kid : n->kids) flatten_scopes(&kid);
        };
        flatten_scopes(&cloned);
        block.kids.push_back(std::move(cloned));
    };
    copy_stmts(fn->body);
    *stmt = std::move(block);
    return true;
}

void walk_inline(Node* node, FuncDecl* caller, const std::vector<FuncDecl>& funcs, Gen& gen, int* seq) {
    if (node->kind != Node::Block && node->kind != Node::If && node->kind != Node::While && node->kind != Node::For &&
        node->kind != Node::Do && node->kind != Node::Label)
        return;
    for (size_t i = 0; i < node->kids.size(); ++i) {
        if (!try_inline_stmt(&node->kids[i], caller, funcs, gen, seq)) walk_inline(&node->kids[i], caller, funcs, gen, seq);
        else walk_inline(&node->kids[i], caller, funcs, gen, seq);
    }
}


}  // namespace compiler
}  // namespace okayms
