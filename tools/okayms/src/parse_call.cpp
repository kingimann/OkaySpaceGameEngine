// The parser: parameter packs, folds, calls, operator functions and the vector helpers.
// Moved out of compile.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "compile_internal.hpp"
#include "parser.hpp"

namespace okayms {
namespace compiler {
// After an argument: 'rest...' stands for every parameter the pack was expanded to.
bool Parser::expand_pack_arg(Node arg, std::vector<Node>* args) {
    if (is(TEllipsis) && arg.kind == Node::Name && param_packs.count(arg.text) > 0) {
        next();
        for (const std::string& n : param_packs[arg.text]) args->push_back(node_name(n));
        return true;
    }
    args->push_back(std::move(arg));
    return true;
}

// The parameter packs named by the tokens in [from, to).
std::vector<std::string> Parser::packs_in(size_t from, size_t to) const {
    std::vector<std::string> names;
    for (size_t i = from; i < to && i < tokens.size(); ++i)
        if (tokens[i].kind == TIdent && param_packs.count(tokens[i].text) > 0) {
            bool seen = false;
            for (const std::string& n : names)
                if (n == tokens[i].text) seen = true;
            if (!seen) names.push_back(tokens[i].text);
        }
    return names;
}

// Reads the tokens [from, to) as one expression with each pack named in them replaced by its
// k-th element ([temp.variadic]: the pattern is written once per element). k == npos reads the
// range as it is. 'unary' reads a cast-expression (a fold's operand), otherwise an assignment-expression.
bool Parser::parse_pack_element(size_t from, size_t to, size_t k, bool unary, Node* out) {
    const std::vector<std::string> names = packs_in(from, to);
    std::vector<Token> saved(tokens.begin() + static_cast<std::ptrdiff_t>(from),
                             tokens.begin() + static_cast<std::ptrdiff_t>(to));
    // A type pack named in the pattern (std::forward<A>(a)...) stands for its k-th type in element k: it is a typedef of
    // that type while the element is read.
    std::map<std::string, std::vector<Type>> type_elems;
    std::map<std::string, Type> old_typedefs;
    if (k != static_cast<size_t>(-1)) {
        for (size_t i = from; i < to; ++i) {
            if (tokens[i].kind != TIdent) continue;
            if (param_packs.count(tokens[i].text) > 0) {
                tokens[i].text = param_packs[tokens[i].text][k];
                continue;
            }
            auto tp = type_packs.find(tokens[i].text);
            if (tp == type_packs.end() || k >= tp->second.size() || type_elems.count(tp->first) > 0) continue;
            if (i + 1 < to && tokens[i + 1].kind == TEllipsis) continue;  // a nested A... stays a pack
            type_elems[tp->first] = tp->second;
            auto td = typedefs.find(tp->first);
            if (td != typedefs.end()) old_typedefs[tp->first] = td->second;
        }
        for (const auto& te : type_elems) {
            typedefs[te.first] = te.second[k];
            type_packs.erase(te.first);
        }
    }
    const size_t was_at = at;
    at = from;
    const bool ok = unary ? parse_unary(out) : parse_expr(1, out);
    for (const auto& te : type_elems) {
        type_packs[te.first] = te.second;
        typedefs.erase(te.first);
        auto od = old_typedefs.find(te.first);
        if (od != old_typedefs.end()) typedefs[te.first] = od->second;
    }
    const bool whole = at == to;
    at = was_at;
    std::copy(saved.begin(), saved.end(), tokens.begin() + static_cast<std::ptrdiff_t>(from));
    if (!ok) return false;
    if (!whole) {
        error = "expected the pack expansion pattern to end before '...'";
        return false;
    }
    return true;
}

// A fold expression ([expr.prim.fold]) at '(': (xs op ...), (... op xs), (init op ... op xs) or
// (xs op ... op init). *folded tells whether one was read.
bool Parser::try_fold(Node* out, bool* folded) {
    *folded = false;
    const size_t open = at;
    size_t close = open;
    size_t e = 0;
    int depth = 0;
    for (size_t i = open; i < tokens.size() && tokens[i].kind != TEnd; ++i) {
        const Tok kind = tokens[i].kind;
        if (kind == TLParen || kind == TLBrack || kind == TLBrace) ++depth;
        else if (kind == TRParen || kind == TRBrack || kind == TRBrace) {
            if (--depth == 0) {
                close = i;
                break;
            }
        } else if (kind == TEllipsis && depth == 1 && e == 0 && !(i > open && tokens[i - 1].kind == TSizeof)) {
            e = i;
        }
    }
    if (close == open || e == 0) return true;
    const auto foldable = [this](Tok op) { return op == TComma || (prec(op) >= 3 && !is_assign_op(op)); };
    Tok op = TEnd;
    size_t pat_from = 0, pat_to = 0, init_from = 0, init_to = 0;
    bool left = false;
    bool has_init = false;
    // The operator after '...' (it may be two tokens, '>' '>') and the one before it.
    size_t after_w = 1, before_w = 1;
    const Tok after_op = e + 1 < close ? op_at(e + 1, &after_w) : TEnd;
    const Tok before_op = e >= 1 ? op_ending_at(e - 1, &before_w) : TEnd;
    if (e == open + 1 && e + 1 + after_w < close && foldable(after_op)) {
        op = after_op;
        left = true;
        pat_from = e + 1 + after_w;
        pat_to = close;
    } else if (e + 1 == close && e >= open + 2 + before_w && foldable(before_op)) {
        op = before_op;
        pat_from = open + 1;
        pat_to = e - before_w;
    } else if (e > open + 1 + before_w && e + 1 + after_w < close && foldable(before_op) && before_op == after_op) {
        op = before_op;
        has_init = true;
        if (!packs_in(e + 2, close).empty()) {
            left = true;
            init_from = open + 1;
            init_to = e - before_w;
            pat_from = e + 1 + after_w;
            pat_to = close;
        } else {
            pat_from = open + 1;
            pat_to = e - before_w;
            init_from = e + 1 + after_w;
            init_to = close;
        }
    } else {
        return true;
    }
    const std::vector<std::string> names = packs_in(pat_from, pat_to);
    if (names.empty()) {
        error = "a fold expression needs a parameter pack";
        return false;
    }
    const size_t count = param_packs[names[0]].size();
    for (const std::string& n : names)
        if (param_packs[n].size() != count) {
            error = "the packs of a fold expression differ in length";
            return false;
        }
    std::vector<Node> elems;
    for (size_t k = 0; k < count; ++k) {
        Node n;
        if (!parse_pack_element(pat_from, pat_to, k, true, &n)) return false;
        elems.push_back(std::move(n));
    }
    Node init;
    if (has_init && !parse_pack_element(init_from, init_to, static_cast<size_t>(-1), true, &init)) return false;
    const auto combine = [op](Node a, Node b) {
        Node bin;
        bin.kind = Node::Binary;
        bin.op = op;
        bin.kids.push_back(std::move(a));
        bin.kids.push_back(std::move(b));
        return bin;
    };
    if (elems.empty()) {
        if (has_init) *out = std::move(init);
        else if (op == TAndAnd || op == TOrOr) {
            out->kind = Node::IntLit;
            out->number = op == TAndAnd ? 1 : 0;
            out->type.base = 1;
        } else {
            error = "a fold over an empty pack needs an initial value";
            return false;
        }
    } else if (left) {
        Node acc = has_init ? combine(std::move(init), std::move(elems[0])) : std::move(elems[0]);
        for (size_t k = 1; k < elems.size(); ++k) acc = combine(std::move(acc), std::move(elems[k]));
        *out = std::move(acc);
    } else {
        Node acc = has_init ? combine(std::move(elems.back()), std::move(init)) : std::move(elems.back());
        for (size_t k = elems.size() - 1; k-- > 0;) acc = combine(std::move(elems[k]), std::move(acc));
        *out = std::move(acc);
    }
    at = close + 1;
    *folded = true;
    return true;
}

bool Parser::parse_call_args(std::vector<Node>* args) {
    if (accept(TRParen)) return true;
    do {
        {
            // f(g(xs)...): a pattern that is not a bare pack name is written once per element.
            const size_t start = at;
            size_t e = start;
            int depth = 0;
            for (; e < tokens.size() && tokens[e].kind != TEnd; ++e) {
                const Tok kind = tokens[e].kind;
                if (kind == TLParen || kind == TLBrack || kind == TLBrace) ++depth;
                else if (kind == TRParen || kind == TRBrack || kind == TRBrace) {
                    if (depth == 0) break;
                    --depth;
                } else if (kind == TComma && depth == 0) break;
            }
            const bool bare = e == start + 2 && tokens[start].kind == TIdent;
            if (e > start + 1 && tokens[e - 1].kind == TEllipsis && !bare) {
                const std::vector<std::string> names = packs_in(start, e - 1);
                if (!names.empty()) {
                    const size_t count = param_packs[names[0]].size();
                    for (const std::string& n : names)
                        if (param_packs[n].size() != count) {
                            error = "the packs of an expansion differ in length";
                            return false;
                        }
                    for (size_t k = 0; k < count; ++k) {
                        Node element;
                        if (!parse_pack_element(start, e - 1, k, false, &element)) return false;
                        args->push_back(std::move(element));
                    }
                    at = e;
                    continue;
                }
            }
        }
        Node arg;
        if (is(TLBrace)) {
            // A braced list as an argument, f({1, 2}) or f({}): what it makes depends on the parameter, so its
            // position is kept and adjust_args reads it once the parameter is known.
            arg.kind = Node::Block;
            arg.text = "tokbrace";
            arg.number = at;
            int depth = 0;
            do {
                if (is(TLBrace)) ++depth;
                else if (is(TRBrace)) --depth;
                else if (is(TEnd)) {
                    error = "expected '}'";
                    return false;
                }
                next();
            } while (depth > 0);
            args->push_back(std::move(arg));
            continue;
        }
        if (!parse_expr(1, &arg)) return false;
        if (!expand_pack_arg(std::move(arg), args)) return false;
    } while (accept(TComma));
    return expect(TRParen, "')'");
}

bool has_effects(const Node& node) {
    if (node.kind == Node::Call || node.kind == Node::Assign) return true;
    if (node.kind == Node::Unary && (node.op == TPlusPlus || node.op == TMinusMinus)) return true;
    for (const Node& kid : node.kids)
        if (has_effects(kid)) return true;
    return false;
}

// "Base::f(args)": a direct call of that class's function on this.
bool Parser::qualified_call(Node* out, int cls, const std::string& name, std::vector<TArg> targs) {
    if (!expect(TLParen, "'('")) return false;
    std::vector<Node> args;
    if (!parse_call_args(&args)) return false;
    std::string method_name = name;
    int method_owner = cls;
    {
        int tmpl_owner = -1;
        if (find_member_tmpl(cls, name, &tmpl_owner) != nullptr && !has_viable_method(cls, name, args)) {
            if (!instantiate_member(cls, name, std::move(targs), args, &method_name, &method_owner)) return false;
        }
    }
    const Method* m = find_method(method_owner, method_name, args.size(), &args);
    if (m == nullptr) {
        error = "no member function " + name;
        return false;
    }
    if (!pad_defaults(m->defaults, m->params.size(), &args)) return false;
    if (!adjust_args(m->params, &args)) return false;
    if (args.empty() && m->is_static && try_const_call(*m, out)) return true;
    if (cur_class < 0 && !m->is_static) {
        error = "a qualified member call needs this";
        return false;
    }
    Node call;
    call.kind = Node::Call;
    call.text = m->qname;
    call.number = 1;
    call.type = m->ret;
    if (!m->is_static) {
        Node self;
        self.kind = Node::Name;
        self.text = "this";
        call.kids.push_back(std::move(self));
    }
    for (Node& arg : args) call.kids.push_back(std::move(arg));
    *out = reference_result(std::move(call), m->ret.is_ref, m->ret.rref);
    return true;
}

// object.name(args) or object->name(args), with '(' already read. A virtual
// function reached through a pointer or reference is called through the
// object's table: the table pointer is the first field, the slot is an index.
bool Parser::method_call(Node* out, Node object, bool arrow, const std::string& name, std::vector<TArg> targs,
                         std::vector<Node>* preparsed) {
    Type ot;
    if (!static_type(object, &ot)) {
        error = "cannot tell the class of the object for " + name;
        return false;
    }
    int sid = -1;
    if (arrow && ot.pointers == 1) sid = ot.struct_id;
    if (!arrow && ot.pointers == 0 && !ot.is_array()) sid = ot.struct_id;
    if (sid < 0 || ot.base != 3) {
        error = "member call on something that is not a class object: " + name;
        return false;
    }
    if (!arrow && object.kind == Node::Call && cur_locals != nullptr) {
        // A member call on a returned object (f().g()): the object goes into a hidden local first.
        Local hidden;
        hidden.name = "__bt" + std::to_string(tmp_count++);
        hidden.type = ot;
        place_local(cur_locals, hidden);
        Node assign;
        assign.kind = Node::Assign;
        assign.op = TAssign;
        assign.number = 1;
        assign.kids.push_back(node_name(hidden.name));
        assign.kids.push_back(std::move(object));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(assign));
        stmt_pre.push_back(std::move(stmt));
        if (class_needs_dtor(structs[static_cast<size_t>(sid)])) stmt_temps.push_back(Temp{hidden.name, sid});
        object = node_name(hidden.name);
    }
    std::vector<Node> args;
    if (preparsed != nullptr) args = std::move(*preparsed);
    else if (!parse_call_args(&args)) return false;
    std::string method_name = name;
    int method_owner = sid;
    {
        int tmpl_owner = -1;
        if (find_member_tmpl(sid, name, &tmpl_owner) != nullptr && !has_viable_method(sid, name, args)) {
            if (!instantiate_member(sid, name, std::move(targs), args, &method_name, &method_owner)) return false;
        }
    }
    const Method* found_m = find_method(method_owner, method_name, args.size(), &args, ot.is_const || ot.pconst ? 1 : 0);
    if (found_m == nullptr) {
        error = "no member function " + name + " with " + std::to_string(args.size()) + " arguments";
        return false;
    }
    // A copy: converting an argument can make a constructor template instance, which adds to a class's methods and
    // moves them (the pointer would dangle).
    const Method m_copy = *found_m;
    const Method* m = &m_copy;
    if (!pad_defaults(m->defaults, m->params.size(), &args)) return false;
    if (!adjust_args(m->params, &args)) return false;
    const bool dynamic = m->is_virtual && (arrow || ot.is_ref || (object.kind == Node::Unary && object.op == TStar));
    Node self;
    if (arrow) {
        self = std::move(object);
    } else {
        self.kind = Node::Unary;
        self.op = TAmp;
        self.kids.push_back(std::move(object));
    }
    Node call;
    call.kind = Node::Call;
    call.number = 1;
    call.type = m->ret;
    if (!dynamic) {
        call.text = m->qname;
    } else {
        if (has_effects(self)) {
            error = "a virtual call needs an object expression without side effects";
            return false;
        }
        FuncType ft;
        ft.ret = m->ret;
        Type self_ptr;
        self_ptr.base = 3;
        self_ptr.struct_id = m->owner;
        self_ptr.pointers = 1;
        ft.params.push_back(self_ptr);
        // A reference parameter is passed as its address (adjust_args took it), as for a direct call: the pointer
        // call's signature says so, or a class parameter would be read as a by-value copy of that address.
        for (Type param : m->params) {
            if (param.is_ref) {
                param.is_ref = false;
                param.rref = false;
                param.pointers += 1;
            }
            ft.params.push_back(param);
        }
        Type fp;
        fp.base = 1;
        fp.pointers = 1;
        fp.func_id = static_cast<int>(func_types.size());
        func_types.push_back(ft);
        Node table;
        table.kind = Node::Field;
        table.op = TArrow;
        table.text = "__vptr";
        table.kids.push_back(self);
        Node slot;
        slot.kind = Node::IntLit;
        slot.number = static_cast<uint64_t>(m->slot);
        slot.type.base = 1;
        Node entry_addr;
        entry_addr.kind = Node::Binary;
        entry_addr.op = TPlus;
        entry_addr.kids.push_back(std::move(table));
        entry_addr.kids.push_back(std::move(slot));
        Node entry;
        entry.kind = Node::Unary;
        entry.op = TStar;
        entry.kids.push_back(std::move(entry_addr));
        Node target;
        target.kind = Node::Unary;
        target.type = fp;
        target.kids.push_back(std::move(entry));
        call.kids.push_back(std::move(target));
    }
    if (!m->is_static) call.kids.push_back(std::move(self));
    for (Node& arg : args) call.kids.push_back(std::move(arg));
    *out = reference_result(std::move(call), m->ret.is_ref, m->ret.rref);
    return true;
}

// ---- operator functions and conversion functions (ISO/IEC 14882 [over.oper], [over.match.oper], [class.conv.fct]) ----

bool Parser::operator_name(Tok op, std::string* name) const {
    const char* spelling = nullptr;
    switch (op) {
    case TPlus: spelling = "+"; break;
    case TMinus: spelling = "-"; break;
    case TStar: spelling = "*"; break;
    case TSlash: spelling = "/"; break;
    case TPercent: spelling = "%"; break;
    case TAmp: spelling = "&"; break;
    case TPipe: spelling = "|"; break;
    case TCaret: spelling = "^"; break;
    case TTilde: spelling = "~"; break;
    case TNot: spelling = "!"; break;
    case TAssign: spelling = "="; break;
    case TLt: spelling = "<"; break;
    case TGt: spelling = ">"; break;
    case TLe: spelling = "<="; break;
    case TGe: spelling = ">="; break;
    case TEq: spelling = "=="; break;
    case TNe: spelling = "!="; break;
    case TPlusPlus: spelling = "++"; break;
    case TMinusMinus: spelling = "--"; break;
    case TPlusEq: spelling = "+="; break;
    case TMinusEq: spelling = "-="; break;
    case TStarEq: spelling = "*="; break;
    case TSlashEq: spelling = "/="; break;
    case TPercentEq: spelling = "%="; break;
    case TAndEq: spelling = "&="; break;
    case TOrEq: spelling = "|="; break;
    case TXorEq: spelling = "^="; break;
    case TShl: spelling = "<<"; break;
    case TShr: spelling = ">>"; break;
    case TShlEq: spelling = "<<="; break;
    case TShrEq: spelling = ">>="; break;
    case TAndAnd: spelling = "&&"; break;
    case TOrOr: spelling = "||"; break;
    default: return false;
    }
    *name = std::string("operator") + spelling;
    return true;
}

// The node is an object of a class (not a pointer, reference-to-pointer or array).
bool Parser::class_object_type(const Node& node, Type* out) const {
    Type t;
    if (!static_type(node, &t)) return false;
    if (t.base != 3 || t.pointers != 0 || t.is_array() || t.struct_id < 0 || t.func_id >= 0) return false;
    *out = t;
    return true;
}

// A call of the free function `name` (found in any namespace) with these arguments, when one fits.
bool Parser::free_call(const std::string& name, std::vector<Node> args, Node* out, bool* found) {
    *found = false;
    const std::string picked = pick_any_ns(name, args, true);
    if (picked.empty()) return true;
    for (const FuncDecl& known : funcs) {
        if (known.name != picked) continue;
        *found = true;
        Node call;
        call.kind = Node::Call;
        call.text = picked;
        call.kids = std::move(args);
        if (!pad_func_defaults(picked, &call.kids)) return false;
        std::vector<Type> source = known.params;
        for (size_t i = 0; i < source.size() && i < known.param_ref.size(); ++i)
            if (known.param_ref[i]) {
                source[i].pointers -= 1;
                source[i].is_ref = true;
            }
        if (!adjust_args(source, &call.kids)) return false;
        *out = known.ret_ref ? reference_result(std::move(call), true, known.ret.rref) : std::move(call);
        return true;
    }
    return true;
}

// An object of a class used where a number or a pointer is wanted: its conversion function is called.
bool Parser::convert_class(Node* node, const Type& target) {
    Type t;
    if (!class_object_type(*node, &t)) return true;
    const Method* m = conv_for(t.struct_id, target);
    if (m == nullptr) return true;
    std::vector<Node> none;
    Node result;
    if (!method_call(&result, std::move(*node), false, m->name, {}, &none)) return false;
    *node = std::move(result);
    return true;
}

// left op right where an operand is an object of a class: a member of the left class, else a free function.
// `done` tells that *left now holds the call. Where no operator function fits, an operand that has a
// conversion function is converted and the built-in operator is left to the caller.
bool Parser::operator_binary(Tok op, Node* left, Node* right, bool* done) {
    *done = false;
    Type lt;
    Type rt;
    const bool lc = class_object_type(*left, &lt);
    const bool rc = class_object_type(*right, &rt);
    std::string name;
    if (!operator_name(op, &name)) return true;
    if (!lc && !rc) return true;
    if (op == TAssign && lc) {
        // Only a user-written operator= is called; without one the members are copied as before.
        std::vector<Node> args;
        args.push_back(*right);
        const Method* m = find_method(lt.struct_id, name, 1, &args);
        if (m == nullptr || m->implicit || m->is_static) return true;
        Node result;
        if (!method_call(&result, std::move(*left), false, name, {}, &args)) return false;
        *left = std::move(result);
        *done = true;
        return true;
    }
    if (op == TAssign) {
        Type target;
        if (static_type(*left, &target) && arith_type(target)) return convert_class(right, target);
        return true;
    }
    if (lc) {
        std::vector<Node> args;
        args.push_back(*right);
        const Method* m = find_method(lt.struct_id, name, 1, &args, lt.is_const || lt.pconst ? 1 : 0);
        if (m != nullptr && !m->is_static && call_score(m->params, args) >= 0) {
            Node result;
            if (!method_call(&result, std::move(*left), false, name, {}, &args)) return false;
            *left = std::move(result);
            *done = true;
            return true;
        }
    }
    {
        std::vector<Node> args;
        args.push_back(*left);
        args.push_back(*right);
        Node result;
        bool found = false;
        if (!free_call(name, std::move(args), &result, &found)) return false;
        if (found) {
            *left = std::move(result);
            *done = true;
            return true;
        }
    }
    if (is_compound_op(op)) {
        Type target;
        if (rc && static_type(*left, &target) && arith_type(target)) return convert_class(right, target);
        return true;
    }
    Type target;
    target.base = 1;
    if (op == TAndAnd || op == TOrOr) {
        target.base = 2;
        target.is_bool = true;
        target.is_unsigned = true;
    }
    if (lc) {
        Type other = target;
        Type rtype;
        if (static_type(*right, &rtype) && arith_type(rtype) && op != TAndAnd && op != TOrOr && (rtype.base >= 5 || rtype.wide)) other = rtype;
        if (!convert_class(left, other)) return false;
    }
    if (rc) {
        Type other = target;
        Type ltype;
        if (static_type(*left, &ltype) && arith_type(ltype) && op != TAndAnd && op != TOrOr && (ltype.base >= 5 || ltype.wide)) other = ltype;
        if (!convert_class(right, other)) return false;
    }
    return true;
}

bool Parser::operator_unary(Tok op, bool postfix, Node* operand, bool* done) {
    *done = false;
    Type t;
    if (!class_object_type(*operand, &t)) return true;
    std::string name;
    if (!operator_name(op, &name)) return true;
    std::vector<Node> args;
    if (postfix) {
        Node zero;
        zero.kind = Node::IntLit;
        zero.number = 0;
        zero.type.base = 1;
        args.push_back(zero);
    }
    const Method* m = find_method(t.struct_id, name, args.size(), &args, t.is_const || t.pconst ? 1 : 0);
    if (m != nullptr && !m->is_static) {
        Node result;
        if (!method_call(&result, std::move(*operand), false, name, {}, &args)) return false;
        *operand = std::move(result);
        *done = true;
        return true;
    }
    std::vector<Node> fargs;
    fargs.push_back(*operand);
    if (postfix) fargs.push_back(args[0]);
    Node result;
    bool found = false;
    if (!free_call(name, std::move(fargs), &result, &found)) return false;
    if (found) {
        *operand = std::move(result);
        *done = true;
        return true;
    }
    if (op == TMinus || op == TNot || op == TTilde) {
        Type target;
        target.base = op == TNot ? 2 : 1;
        target.is_bool = op == TNot;
        target.is_unsigned = op == TNot;
        return convert_class(operand, target);
    }
    return true;
}

bool Parser::operator_call(Node* object, std::vector<Node> args, bool* done) {
    *done = false;
    Type t;
    if (!class_object_type(*object, &t)) return true;
    const Method* m = find_method(t.struct_id, "operator()", args.size(), &args, t.is_const || t.pconst ? 1 : 0);
    int tmpl_owner = -1;
    // template <class G> T operator()(G& g) (a distribution of <random>): a member template call ([temp.mem]).
    if (m == nullptr && find_member_tmpl(t.struct_id, "operator()", &tmpl_owner) == nullptr) return true;
    Node result;
    if (!method_call(&result, std::move(*object), false, "operator()", {}, &args)) return false;
    *object = std::move(result);
    *done = true;
    return true;
}

bool Parser::operator_index(Node* object, Node index, bool* done) {
    *done = false;
    Type t;
    if (!class_object_type(*object, &t)) return true;
    std::vector<Node> args;
    args.push_back(std::move(index));
    const Method* m = find_method(t.struct_id, "operator[]", 1, &args, t.is_const || t.pconst ? 1 : 0);
    if (m == nullptr) {
        error = "no operator[] in the class";
        return false;
    }
    Node result;
    if (!method_call(&result, std::move(*object), false, "operator[]", {}, &args)) return false;
    *object = std::move(result);
    *done = true;
    return true;
}

// __vnew_C(n) and __vdel_C(p): new C[n] and delete[] p for a class with a constructor or
// destructor. The block has an 8-byte header holding the count when the destructor must run per
// element. Built as syntax trees, once per class.
bool Parser::ensure_vector_helpers(int sid) {
    const StructDef& def = structs[static_cast<size_t>(sid)];
    const std::string new_name = "__vnew_" + def.name;
    for (const FuncDecl& fn : funcs)
        if (fn.name == new_name) return true;
    const bool cookie = class_needs_dtor(def);
    Type elem;
    elem.base = 3;
    elem.struct_id = sid;
    Type elem_ptr = elem;
    elem_ptr.pointers = 1;
    Type char_ptr;
    char_ptr.base = 2;
    char_ptr.pointers = 1;
    Type int_t;
    int_t.base = 1;
    auto lit = [](int v) {
        Node n;
        n.kind = Node::IntLit;
        n.number = static_cast<uint64_t>(v);
        n.type.base = 1;
        return n;
    };
    auto cast_to = [](const Type& t, Node inner) {
        Node c;
        c.kind = Node::Unary;
        c.type = t;
        c.kids.push_back(std::move(inner));
        return c;
    };
    auto assign_stmt = [](Node l, Node r) {
        Node a;
        a.kind = Node::Assign;
        a.op = TAssign;
        a.kids.push_back(std::move(l));
        a.kids.push_back(std::move(r));
        Node st;
        st.kind = Node::ExprStmt;
        st.kids.push_back(std::move(a));
        return st;
    };
    auto bin = [](Tok op, Node l, Node r) {
        Node b;
        b.kind = Node::Binary;
        b.op = op;
        b.kids.push_back(std::move(l));
        b.kids.push_back(std::move(r));
        return b;
    };
    auto push_fn = [&](FuncDecl fn, Node body, std::vector<Local> locals) {
        finish_signature(&fn);
        fn.body = std::move(body);
        fn.locals = std::move(locals);
        fn.nparams = static_cast<int>(fn.params.size());
        fn.defined = true;
        funcs.push_back(std::move(fn));
    };
    std::vector<Local>* saved_locals = cur_locals;
    const int saved_depth = scope_depth;
    scope_depth = 0;
    {
        FuncDecl fn;
        fn.name = new_name;
        fn.ret = elem_ptr;
        fn.params.push_back(int_t);
        fn.param_names.push_back("n");
        std::vector<Local> locals;
        Local ln;
        ln.name = "n";
        ln.type = int_t;
        place_local(&locals, ln);
        Local lr;
        lr.name = "raw";
        lr.type = char_ptr;
        place_local(&locals, lr);
        Local lq;
        lq.name = "q";
        lq.type = elem_ptr;
        place_local(&locals, lq);
        cur_locals = &locals;
        Node body;
        body.kind = Node::Block;
        Node alloc;
        alloc.kind = Node::Call;
        alloc.text = "malloc";
        Node bytes = bin(TStar, node_name("n"), lit(size_of(elem)));
        if (cookie) bytes = bin(TPlus, std::move(bytes), lit(8));
        alloc.kids.push_back(std::move(bytes));
        body.kids.push_back(assign_stmt(node_name("raw"), cast_to(char_ptr, std::move(alloc))));
        if (cookie) {
            Type int_ptr = int_t;
            int_ptr.pointers = 1;
            Node count_at;
            count_at.kind = Node::Unary;
            count_at.op = TStar;
            count_at.kids.push_back(cast_to(int_ptr, node_name("raw")));
            body.kids.push_back(assign_stmt(std::move(count_at), node_name("n")));
        }
        Node base = node_name("raw");
        if (cookie) base = bin(TPlus, std::move(base), lit(8));
        body.kids.push_back(assign_stmt(node_name("q"), cast_to(elem_ptr, std::move(base))));
        if (class_needs_ctor(def)) {
            Node loop;
            const Node count_node = node_name("n");
            if (!element_loop("q", 0, false, sid, "<ctor>", &loop, &count_node)) {
                cur_locals = saved_locals;
                scope_depth = saved_depth;
                return false;
            }
            body.kids.push_back(std::move(loop));
        }
        Node ret;
        ret.kind = Node::Return;
        ret.kids.push_back(node_name("q"));
        body.kids.push_back(std::move(ret));
        push_fn(std::move(fn), std::move(body), std::move(locals));
    }
    if (class_needs_dtor(def)) {
        FuncDecl fn;
        fn.name = "__vdel_" + def.name;
        fn.ret.base = 0;
        fn.params.push_back(elem_ptr);
        fn.param_names.push_back("p");
        std::vector<Local> locals;
        Local lp;
        lp.name = "p";
        lp.type = elem_ptr;
        place_local(&locals, lp);
        Local ln;
        ln.name = "n";
        ln.type = int_t;
        place_local(&locals, ln);
        cur_locals = &locals;
        Type int_ptr = int_t;
        int_ptr.pointers = 1;
        Node header = bin(TMinus, cast_to(char_ptr, node_name("p")), lit(8));
        Node count_at;
        count_at.kind = Node::Unary;
        count_at.op = TStar;
        count_at.kids.push_back(cast_to(int_ptr, std::move(header)));
        Node work;
        work.kind = Node::Block;
        work.kids.push_back(assign_stmt(node_name("n"), std::move(count_at)));
        Node loop;
        const Node count_node = node_name("n");
        if (!element_loop("p", 0, true, sid, "<dtor>", &loop, &count_node)) {
            cur_locals = saved_locals;
            scope_depth = saved_depth;
            return false;
        }
        work.kids.push_back(std::move(loop));
        Node release;
        release.kind = Node::Call;
        release.text = "free";
        release.kids.push_back(bin(TMinus, cast_to(char_ptr, node_name("p")), lit(8)));
        Node release_stmt;
        release_stmt.kind = Node::ExprStmt;
        release_stmt.kids.push_back(std::move(release));
        work.kids.push_back(std::move(release_stmt));
        Node guard;
        guard.kind = Node::If;
        guard.kids.push_back(node_name("p"));
        guard.kids.push_back(std::move(work));
        Node body;
        body.kind = Node::Block;
        body.kids.push_back(std::move(guard));
        push_fn(std::move(fn), std::move(body), std::move(locals));
    }
    cur_locals = saved_locals;
    scope_depth = saved_depth;
    return true;
}


}  // namespace compiler
}  // namespace okayms
