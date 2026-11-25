// The parser: braced temporaries, lambdas, static types, new and delete, unary, postfix and binary expressions.
// Moved out of compile.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "compile_internal.hpp"
#include "parser.hpp"

namespace okayms {
namespace compiler {
// `{a, b, c}` for a parameter of type std::initializer_list<T> ([dcl.init.list]): the elements go into a hidden array of
// T in this function's frame and the list is a hidden object that points at the array. The current token is the '{'.
bool Parser::init_list_temp(const Type& list_type, Node* out) {
    const StructDef& lsd = structs[static_cast<size_t>(list_type.struct_id)];
    if (lsd.targs.empty() || lsd.targs[0].is_value || cur_locals == nullptr) {
        error = "cannot make an initializer_list here";
        return false;
    }
    const Type elem = lsd.targs[0].type;
    const bool class_elems = elem.base == 3 && elem.pointers == 0 && !elem.is_array() && elem.struct_id >= 0 &&
                             (obj_needs_ctor(elem) || obj_needs_dtor(elem));
    next();  // '{'
    if (class_elems) {
        // {"a", "b"} for an initializer_list<std::string>: each element is a hidden object made from its item by a
        // constructor (copy-initialization, [dcl.init.list] 5) and destroyed after the full-expression, like other
        // temporaries. Own simplification: the array does not live longer than the statement, so a list kept past it
        // (a range for over a braced list of strings) is not supported. The elements are placed from the last to the
        // first: locals grow downward, so element 0 is at the lowest address and the elements are contiguous (a class's
        // size is a multiple of its alignment).
        std::vector<Node> items;
        while (!is(TRBrace) && !is(TEnd)) {
            Node item;
            if (is(TLBrace)) {
                if (!braced_temp(elem.struct_id, &item)) return false;
            } else if (!parse_expr(1, &item)) {
                return false;
            }
            items.push_back(std::move(item));
            if (!accept(TComma)) break;
        }
        if (!expect(TRBrace, "'}'")) return false;
        const size_t n = items.empty() ? 1 : items.size();
        const std::string base = "__il" + std::to_string(tmp_count++) + "_";
        for (size_t i = n; i-- > 0;) {
            Local e;
            e.name = base + std::to_string(i);
            e.type = elem;
            e.type.is_const = false;
            place_local(cur_locals, e);
        }
        const bool has_dtor = obj_needs_dtor(elem);
        for (size_t i = 0; i < items.size(); ++i) {
            const std::string name = base + std::to_string(i);
            std::vector<Node> cargs;
            cargs.push_back(std::move(items[i]));
            Node ctor_stmt;
            if (!special_call(elem.struct_id, "<ctor>", node_addr(node_name(name)), std::move(cargs), &ctor_stmt))
                return false;
            stmt_pre.push_back(std::move(ctor_stmt));
            if (has_dtor) stmt_temps.push_back(Temp{name, elem.struct_id});
        }
        Local hidden;
        hidden.name = "__ilo" + std::to_string(tmp_count++);
        hidden.type = list_type;
        hidden.type.is_ref = false;
        place_local(cur_locals, hidden);
        Node count;
        count.kind = Node::IntLit;
        count.type.base = 1;
        count.type.wide = true;
        count.number = items.size();
        std::vector<Node> largs;
        largs.push_back(node_addr(node_name(base + "0")));
        largs.push_back(std::move(count));
        Node list_ctor;
        if (!special_call(list_type.struct_id, "<ctor>", node_addr(node_name(hidden.name)), std::move(largs), &list_ctor))
            return false;
        stmt_pre.push_back(std::move(list_ctor));
        *out = node_name(hidden.name);
        return true;
    }
    std::vector<Node> items;
    while (!is(TRBrace) && !is(TEnd)) {
        Node item;
        if (is(TLBrace) && elem.base == 3 && elem.pointers == 0 && elem.struct_id >= 0) {
            if (!braced_temp(elem.struct_id, &item)) return false;
        } else {
            if (!parse_expr(1, &item)) return false;
            if (elem.base != 3 && elem.base != 0 && elem.pointers == 0 && !convert_class(&item, elem)) return false;
        }
        items.push_back(std::move(item));
        if (!accept(TComma)) break;
    }
    if (!expect(TRBrace, "'}'")) return false;
    Local array;
    array.name = "__il" + std::to_string(tmp_count++);
    array.type = elem;
    array.type.array_len = items.empty() ? 1 : static_cast<int>(items.size());
    place_local(cur_locals, array);
    for (size_t i = 0; i < items.size(); ++i) {
        Node idx;
        idx.kind = Node::IntLit;
        idx.type.base = 1;
        idx.number = i;
        Node slot;
        slot.kind = Node::Index;
        slot.kids.push_back(node_name(array.name));
        slot.kids.push_back(std::move(idx));
        Node assign;
        assign.kind = Node::Assign;
        assign.op = TAssign;
        assign.number = 1;
        assign.kids.push_back(std::move(slot));
        assign.kids.push_back(std::move(items[i]));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(assign));
        stmt_pre.push_back(std::move(stmt));
    }
    Node zero;
    zero.kind = Node::IntLit;
    zero.type.base = 1;
    Node first;
    first.kind = Node::Index;
    first.kids.push_back(node_name(array.name));
    first.kids.push_back(std::move(zero));
    Node count;
    count.kind = Node::IntLit;
    count.type.base = 1;
    count.type.wide = true;
    count.number = items.size();
    Local hidden;
    hidden.name = "__ilo" + std::to_string(tmp_count++);
    hidden.type = list_type;
    hidden.type.is_ref = false;
    place_local(cur_locals, hidden);
    std::vector<Node> cargs;
    cargs.push_back(node_addr(std::move(first)));
    cargs.push_back(std::move(count));
    Node ctor_stmt;
    if (!special_call(list_type.struct_id, "<ctor>", node_addr(node_name(hidden.name)), std::move(cargs), &ctor_stmt))
        return false;
    stmt_pre.push_back(std::move(ctor_stmt));
    *out = node_name(hidden.name);
    return true;
}

// C{args} ([expr.type.conv], [dcl.init.list]): a temporary object of class bsid made from the braced list at the
// current token, by a constructor, or member by member for an aggregate. Its construction goes before the
// statement and its destructor runs after it.
bool Parser::braced_temp(int bsid, Node* out) {
        Type bt;
        bt.base = 3;
        bt.struct_id = bsid;
        Local hidden;
        hidden.name = "__bt" + std::to_string(tmp_count++);
        hidden.type = bt;
        place_local(cur_locals, hidden);
        Type list_type;
        if (obj_needs_ctor(bt) && tokens[at + 1].kind != TRBrace && has_init_list_ctor(bsid, &list_type)) {
            // An initializer-list constructor is preferred when the list is not empty ([over.match.list]).
            Node list;
            if (!init_list_temp(list_type, &list)) return false;
            std::vector<Node> bargs;
            bargs.push_back(std::move(list));
            Node ctor_stmt;
            if (!special_call(bsid, "<ctor>", node_addr(node_name(hidden.name)), std::move(bargs), &ctor_stmt))
                return false;
            stmt_pre.push_back(std::move(ctor_stmt));
        } else if (obj_needs_ctor(bt) && tokens[at + 1].kind != TRBrace && has_nested_braces_ahead() &&
                   aggregate_with_defaults(bsid, std::vector<Node>(2))) {
            // C{a, {b, c}} for an aggregate with members that need construction: the default constructor, then each
            // member set from its item (a nested list makes the member's own temporary).
            next();  // '{'
            const StructDef& agg = structs[static_cast<size_t>(bsid)];
            const Method* dflt = find_method(bsid, "<ctor>", 0);
            if (dflt != nullptr) {
                Node call;
                call.kind = Node::Call;
                call.text = dflt->qname;
                call.number = 1;
                call.type = dflt->ret;
                call.kids.push_back(node_addr(node_name(hidden.name)));
                Node st;
                st.kind = Node::ExprStmt;
                st.kids.push_back(std::move(call));
                stmt_pre.push_back(std::move(st));
            }
            size_t k = 0;
            for (const Field& f : agg.fields) {
                if (f.name.compare(0, 6, "__vptr") == 0) continue;
                if (is(TRBrace)) break;
                Node item;
                if (is(TLBrace) && f.type.base == 3 && f.type.pointers == 0 && !f.type.is_array() && f.type.struct_id >= 0) {
                    if (!braced_temp(f.type.struct_id, &item)) return false;
                } else if (is(TLBrace)) {
                    error = "a nested list for a member of this type is not supported";
                    return false;
                } else {
                    if (!parse_expr(1, &item)) return false;
                    if (f.type.base != 3 && f.type.base != 0 && f.type.pointers == 0 && !f.type.is_array() &&
                        !convert_class(&item, f.type))
                        return false;
                }
                Node fn;
                fn.kind = Node::Field;
                fn.op = TDot;
                fn.text = f.name;
                fn.kids.push_back(node_name(hidden.name));
                Node assign;
                assign.kind = Node::Assign;
                assign.op = TAssign;
                assign.number = obj_needs_ctor(f.type) || obj_needs_dtor(f.type) ? 0 : 1;
                assign.kids.push_back(std::move(fn));
                assign.kids.push_back(std::move(item));
                Node st;
                st.kind = Node::ExprStmt;
                st.kids.push_back(std::move(assign));
                stmt_pre.push_back(std::move(st));
                ++k;
                if (!accept(TComma)) break;
            }
            if (!expect(TRBrace, "'}'")) return false;
        } else if (obj_needs_ctor(bt)) {
            next();
            std::vector<Node> bargs;
            if (!is(TRBrace)) {
                do {
                    Node a;
                    if (!parse_expr(1, &a)) return false;
                    bargs.push_back(std::move(a));
                } while (accept(TComma));
            }
            if (!expect(TRBrace, "'}'")) return false;
            Node ctor_stmt;
            if (!special_call(bsid, "<ctor>", node_addr(node_name(hidden.name)), std::move(bargs), &ctor_stmt))
                return false;
            stmt_pre.push_back(std::move(ctor_stmt));
        } else {
            Node binit;
            if (!parse_init(&binit)) return false;
            Node assign;
            assign.kind = Node::Assign;
            assign.op = TAssign;
            assign.number = 1;
            assign.kids.push_back(node_name(hidden.name));
            assign.kids.push_back(std::move(binit));
            Node stmt;
            stmt.kind = Node::ExprStmt;
            stmt.kids.push_back(std::move(assign));
            stmt_pre.push_back(std::move(stmt));
        }
        if (class_needs_dtor(structs[static_cast<size_t>(bsid)])) stmt_temps.push_back(Temp{hidden.name, bsid});
    *out = node_name(hidden.name);
    return true;
}

// A lambda expression ([expr.prim.lambda]) becomes a class with an operator(). One that captures nothing is
// built with a static operator() and its value is the function's address (a function pointer); one that
// captures gets a field for each captured variable (a copy, or its address for &x), and its value is a hidden
// local of that class whose fields are filled before the statement. The return type is the written one, or the
// type of the first `return e;` (the body is read once to find it), or void.
bool Parser::parse_lambda(Node* out) {
    next();  // '['
    struct Cap {
        std::string name;
        bool ref = false;
    };
    std::vector<Cap> caps;
    bool default_value = false;
    bool default_ref = false;
    bool cap_this = false;
    while (!is(TRBrack)) {
        if (is(TAssign)) {
            default_value = true;
            next();
        } else if (is(TAmp)) {
            next();
            if (is(TIdent)) {
                caps.push_back(Cap{cur().text, true});
                next();
            } else {
                default_ref = true;
            }
        } else if (is(TIdent) && cur().text == "this") {
            cap_this = true;
            next();
        } else if (is(TStar) && at + 1 < tokens.size() && tokens[at + 1].kind == TIdent && tokens[at + 1].text == "this") {
            error = "a lambda that captures *this is not supported";
            return false;
        } else if (is(TIdent)) {
            Cap c;
            c.name = cur().text;
            next();
            if (is(TAssign)) {
                error = "an init-capture is not supported";
                return false;
            }
            caps.push_back(c);
        } else {
            error = "unsupported lambda capture";
            return false;
        }
        if (!accept(TComma)) break;
    }
    if (!expect(TRBrack, "']'")) return false;
    Declarator decl;
    // A generic lambda ([expr.prim.lambda.closure] 3): a parameter declared `auto` makes the call operator a member
    // function template, one invented type parameter per `auto`. Its parameter tokens are kept with each `auto` replaced
    // by the invented name, and the call operator is registered as a member template of the closure class below.
    std::vector<Token> generic_params;
    std::vector<std::string> generic_names;
    if (is(TLParen)) {
        int depth = 0;
        bool any_auto = false;
        size_t close = at;
        for (; close < tokens.size() && tokens[close].kind != TEnd; ++close) {
            if (tokens[close].kind == TLParen) ++depth;
            else if (tokens[close].kind == TRParen && --depth == 0) break;
            else if (depth == 1 && tokens[close].kind == TIdent && tokens[close].text == "auto") any_auto = true;
        }
        if (any_auto) {
            for (size_t k = at + 1; k < close; ++k) {
                Token t = tokens[k];
                if (t.kind == TIdent && t.text == "auto") {
                    if (k + 1 < close && tokens[k + 1].kind == TEllipsis) {
                        error = "a generic lambda with a parameter pack is not supported";
                        return false;
                    }
                    t.text = "__gl" + std::to_string(generic_names.size());
                    generic_names.push_back(t.text);
                }
                generic_params.push_back(t);
                // The parameter's name: the identifier before ',' or ')' or '=' at the top level.
                if (t.kind == TIdent && k + 1 <= close &&
                    (tokens[k + 1].kind == TComma || tokens[k + 1].kind == TRParen || tokens[k + 1].kind == TAssign))
                    decl.param_names.push_back(t.text);
            }
            at = close + 1;
        }
    }
    if (generic_names.empty() && accept(TLParen) && !parse_param_list(&decl)) return false;
    if (is(TIdent) && cur().text == "mutable") next();
    Type ret;
    ret.base = 0;
    bool has_ret = false;
    size_t ret_from = 0;
    size_t ret_to = 0;
    if (accept(TArrow)) {
        bool imp = false;
        bool exp = false;
        ret_from = at;
        if (!parse_type(&ret, &imp, &exp)) return false;
        while (accept(TStar)) ++ret.pointers;
        if (accept(TAmp) || accept(TAndAnd)) ret.is_ref = true;
        has_ret = true;
        ret_to = at;
    }
    if (!is(TLBrace)) {
        error = "expected the lambda body";
        return false;
    }
    const size_t body_at = at;
    size_t body_end = at;
    {
        int depth = 0;
        for (; body_end < tokens.size() && tokens[body_end].kind != TEnd; ++body_end) {
            if (tokens[body_end].kind == TLBrace) ++depth;
            else if (tokens[body_end].kind == TRBrace && --depth == 0) break;
        }
    }
    // The captured variables: the named ones, then (for = or &) every visible local the body mentions.
    std::vector<Cap> list;
    for (const Cap& c : caps)
        if (local_visible(c.name)) list.push_back(c);
    if (default_value || default_ref) {
        for (size_t i = body_at; i < body_end; ++i) {
            if (tokens[i].kind != TIdent || !local_visible(tokens[i].text) || tokens[i].text == "this") continue;
            if (i > 0 && (tokens[i - 1].kind == TDot || tokens[i - 1].kind == TArrow || tokens[i - 1].kind == TColonColon))
                continue;
            bool is_param = false;
            for (const std::string& pn : decl.param_names)
                if (pn == tokens[i].text) is_param = true;
            bool known = false;
            for (const Cap& c : list)
                if (c.name == tokens[i].text) known = true;
            if (!is_param && !known) list.push_back(Cap{tokens[i].text, default_ref});
        }
    }
    // `this` is captured when named, or by a default capture ([=] or [&]) in a member function ([expr.prim.lambda.capture]).
    if ((default_value || default_ref) && cur_class >= 0 && local_visible("this")) cap_this = true;
    if (cap_this && (cur_class < 0 || !local_visible("this"))) {
        error = "a lambda captures this outside a member function";
        return false;
    }
    if (cur_locals == nullptr && (!list.empty() || cap_this)) {
        error = "a lambda that captures needs a function around it";
        return false;
    }
    StructDef def;
    def.name = "__lam" + std::to_string(lambda_count++);
    def.cxx = true;
    // The class around: the member function's class, or for a lambda in a lambda the class around that one.
    def.lam_scope = cur_class;
    if (cur_class >= 0 && structs[static_cast<size_t>(cur_class)].name.compare(0, 5, "__lam") == 0 && !cap_this)
        def.lam_scope = structs[static_cast<size_t>(cur_class)].lam_scope;
    int offset = 0;
    int align = 1;
    if (cap_this) {
        Field field;
        field.name = "__this";
        field.type.base = 3;
        field.type.struct_id = cur_class;
        field.type.pointers = 1;
        field.offset = 0;
        offset = 8;
        align = 8;
        def.fields.push_back(field);
        def.lam_outer = cur_class;
    }
    for (const Cap& c : list) {
        Node probe = node_name(c.name);
        Type vt;
        if (!static_type(probe, &vt)) {
            error = "cannot tell the type of the captured variable " + c.name;
            return false;
        }
        Field field;
        field.name = c.name;
        field.type = vt;
        field.type.is_ref = false;
        field.type.pconst = false;
        if (c.ref) {
            if (vt.is_array()) {
                field.type = peel_array(field.type);
                field.type.pointers += 1;
            } else {
                field.type.pointers += 1;
                def.ref_caps.insert(c.name);
            }
        } else if (vt.is_array()) {
            error = "a lambda cannot capture an array by value";
            return false;
        }
        const int fa = std::max(1, align_of(field.type));
        const int fs = std::max(1, size_of(field.type));
        offset = (offset + fa - 1) & ~(fa - 1);
        field.offset = offset;
        offset += fs;
        align = std::max(align, fa);
        def.fields.push_back(field);
    }
    def.align = align;
    def.size = std::max(1, (offset + align - 1) & ~(align - 1));
    const int sid = static_cast<int>(structs.size());
    structs.push_back(def);
    {
        // A closure that holds objects with a copy constructor or a destructor (a shared_ptr captured by value) has the
        // implicit copy constructor and destructor of a class with those members ([expr.prim.lambda.closure] 14, 15):
        // they copy and destroy the captured objects.
        bool part_copies = false;
        for (const Field& f : structs[static_cast<size_t>(sid)].fields)
            if (f.type.base == 3 && f.type.pointers == 0 && !f.type.is_array() && f.type.struct_id >= 0 &&
                class_has_copy(f.type.struct_id))
                part_copies = true;
        std::vector<size_t> made;
        if (part_copies) {
            Method cm;
            cm.name = "<ctor>";
            cm.ret.base = 3;
            cm.ret.struct_id = sid;
            cm.ret.pointers = 1;
            Type self_ref;
            self_ref.base = 3;
            self_ref.struct_id = sid;
            self_ref.is_ref = true;
            cm.params.push_back(self_ref);
            cm.param_names.push_back("__o");
            cm.implicit = true;
            cm.implicit_copy = true;
            cm.has_body = true;
            cm.body_at = 1;
            if (!add_method(&structs[static_cast<size_t>(sid)], sid, std::move(cm))) return false;
            made.push_back(structs[static_cast<size_t>(sid)].methods.size() - 1);
        }
        if (class_needs_dtor(structs[static_cast<size_t>(sid)])) {
            Method dm;
            dm.name = "<dtor>";
            dm.ret.base = 0;
            dm.implicit = true;
            dm.has_body = true;
            dm.body_at = 1;
            if (!add_method(&structs[static_cast<size_t>(sid)], sid, std::move(dm))) return false;
            made.push_back(structs[static_cast<size_t>(sid)].methods.size() - 1);
        }
        for (size_t mi : made) {
            std::vector<Node> keep_pre = std::move(stmt_pre);
            std::vector<Temp> keep_temps = std::move(stmt_temps);
            stmt_pre.clear();
            stmt_temps.clear();
            const size_t keep_at = at;
            const bool ok = define_method(sid, mi);
            at = keep_at;
            stmt_pre = std::move(keep_pre);
            stmt_temps = std::move(keep_temps);
            if (!ok) return false;
        }
    }
    if (!generic_names.empty()) {
        // The call operator template, as the tokens `R operator()(params) body` after the end of the file (R is the
        // written return type or `auto`), instantiated for each call like any member function template.
        if (cur_locals == nullptr) {
            error = "a generic lambda needs a function around it";
            return false;
        }
        Token tmpl = tokens[body_at];
        auto tok = [&tmpl](Tok kind, const std::string& text) {
            Token t = tmpl;
            t.kind = kind;
            t.text = text;
            return t;
        };
        std::vector<Token> extra;
        if (has_ret) {
            for (size_t k = ret_from; k < ret_to; ++k) extra.push_back(tokens[k]);
        } else {
            extra.push_back(tok(TIdent, "auto"));
        }
        const size_t name_at = extra.size();
        extra.push_back(tok(TIdent, "operator()"));
        extra.push_back(tok(TLParen, "("));
        for (const Token& t : generic_params) extra.push_back(t);
        extra.push_back(tok(TRParen, ")"));
        for (size_t k = body_at; k <= body_end && k < tokens.size(); ++k) extra.push_back(tokens[k]);
        extra.push_back(tok(TEnd, ""));
        if (tokens.capacity() < tokens.size() + extra.size()) {
            // The token vector must not move: callers hold references into it.
            error = "too many generic lambdas in one file";
            return false;
        }
        const size_t start = tokens.size();
        for (Token& t : extra) tokens.push_back(std::move(t));
        MemberTmpl mt;
        for (const std::string& gn : generic_names) {
            TParam tp;
            tp.name = gn;
            mt.td.params.push_back(tp);
        }
        mt.td.start = start;
        mt.td.end = tokens.size() - 1;  // the TEnd after the body
        mt.owner = def.name;
        if (!inst_base_name.empty()) {  // a lambda in a member of a class template instance: its arguments stay bound
            mt.ctx = inst_td;
            mt.ctx_args = inst_bind_args();
        }
        {
            std::map<std::string, size_t> values;
            const auto saved = bind_params(mt.td, make_markers(mt.td));
            const size_t was_at = at;
            at = start + name_at + 2;
            Declarator pd;
            pat_values = &values;
            const bool ok = parse_param_list(&pd);
            pat_values = nullptr;
            mt.td.proto = pd.params;
            at = was_at;
            unbind_params(saved);
            if (!ok) return false;
        }
        member_tmpl_all[std::make_pair(sid, std::string("operator()"))].push_back(mt);
        member_tmpls[std::make_pair(sid, std::string("operator()"))] = std::move(mt);
    }
    std::string qname;
    if (generic_names.empty()) {
        Method m;
        m.name = "operator()";
        m.ret = ret;
        if (!has_ret) m.ret.base = 0;
        m.params = decl.params;
        m.param_names = decl.param_names;
        m.defaults = decl.param_defaults;
        m.is_static = list.empty() && !cap_this;
        m.has_body = true;
        m.body_at = body_at;
        // Does the body return a value?
        bool returns_value = false;
        for (size_t i = body_at; i < body_end; ++i)
            if (tokens[i].kind == TReturn && i + 1 < tokens.size() && tokens[i + 1].kind != TSemi) returns_value = true;
        if (!add_method(&structs[static_cast<size_t>(sid)], sid, m)) return false;
        const size_t mi = structs[static_cast<size_t>(sid)].methods.size() - 1;
        const size_t resume = body_end + 1;
        // The locals of the function around stay visible to decltype in the body (an unevaluated operand names a local
        // that is not captured: [expr.prim.lambda.capture] 7, [dcl.type.decltype]).
        struct OuterGuard {
            std::vector<const std::vector<Local>*>& stack;
            ~OuterGuard() { stack.pop_back(); }
        } outer_guard{lam_outer_locals_};
        lam_outer_locals_.push_back(cur_locals);
        if (!has_ret && returns_value) {
            const bool saved_probe = lam_probe;
            const bool saved_seen = lam_probe_seen;
            const Type saved_type = lam_probe_type;
            const std::string saved_probe_fn = lam_probe_fn;
            lam_probe_fn = structs[static_cast<size_t>(sid)].methods[mi].qname;
            lam_probe = true;
            lam_probe_seen = false;
            lam_probe_type = Type{};
            lam_probe_type.base = 1;
            structs[static_cast<size_t>(sid)].methods[mi].ret = Type{};
            structs[static_cast<size_t>(sid)].methods[mi].ret.base = 1;
            const size_t funcs_before = funcs.size();
            std::vector<Node> keep_pre = std::move(stmt_pre);
            stmt_pre.clear();
            std::vector<Temp> keep_temps = std::move(stmt_temps);
            stmt_temps.clear();
            const size_t probe_at = at;
            const bool probe_ok = define_method(sid, mi);
            at = probe_at;
            stmt_pre = std::move(keep_pre);
            stmt_temps = std::move(keep_temps);
            const Type found = lam_probe_type;
            lam_probe = saved_probe;
            lam_probe_seen = saved_seen;
            lam_probe_type = saved_type;
            lam_probe_fn = saved_probe_fn;
            if (!probe_ok) return false;
            // Only the probe's own body goes: instances made while it was read (a member template called in it) are kept,
            // because their classes already record them as made and they would not be made again.
            {
                const std::string own = structs[static_cast<size_t>(sid)].methods[mi].qname;
                for (size_t fi = funcs.size(); fi-- > funcs_before;)
                    if (funcs[fi].name == own) funcs.erase(funcs.begin() + static_cast<std::ptrdiff_t>(fi));
            }
            Method& mm = structs[static_cast<size_t>(sid)].methods[mi];
            mm.ret = found;
            mm.body_at = body_at;
            mm.symbol = cxx_method_symbol(structs, structs[static_cast<size_t>(sid)], mm);
        }
        {
            std::vector<Node> keep_pre = std::move(stmt_pre);
            stmt_pre.clear();
            std::vector<Temp> keep_temps = std::move(stmt_temps);
            stmt_temps.clear();
            const size_t keep_at = at;
            const bool ok = define_method(sid, mi);
            at = keep_at;
            stmt_pre = std::move(keep_pre);
            stmt_temps = std::move(keep_temps);
            if (!ok) return false;
        }
        at = resume;
        qname = structs[static_cast<size_t>(sid)].methods[mi].qname;
    } else {
        at = body_end + 1;
    }
    if (generic_names.empty() && list.empty() && !cap_this) {
        *out = node_name(qname);
        return true;
    }
    Local hidden;
    hidden.name = "__bt" + std::to_string(tmp_count++);
    hidden.type.base = 3;
    hidden.type.struct_id = sid;
    place_local(cur_locals, hidden);
    if (class_needs_dtor(structs[static_cast<size_t>(sid)])) stmt_temps.push_back(Temp{hidden.name, sid});
    if (cap_this) {
        Node target;
        target.kind = Node::Field;
        target.op = TDot;
        target.text = "__this";
        target.kids.push_back(node_name(hidden.name));
        Node assign;
        assign.kind = Node::Assign;
        assign.op = TAssign;
        assign.number = 1;
        assign.kids.push_back(std::move(target));
        assign.kids.push_back(node_name("this"));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(assign));
        stmt_pre.push_back(std::move(stmt));
    }
    for (const Cap& c : list) {
        Node target;
        target.kind = Node::Field;
        target.op = TDot;
        target.text = c.name;
        target.kids.push_back(node_name(hidden.name));
        Node value = node_name(c.name);
        Type vt;
        static_type(value, &vt);
        if (c.ref && !vt.is_array()) value = node_addr(std::move(value));
        if (!c.ref && vt.base == 3 && vt.pointers == 0 && !vt.is_array() && vt.struct_id >= 0 && class_has_copy(vt.struct_id)) {
            // A captured object of a class with a copy constructor is copied by it ([expr.prim.lambda.capture] 15).
            std::vector<Node> cargs;
            cargs.push_back(std::move(value));
            Node call;
            if (!special_call(vt.struct_id, "<ctor>", node_addr(std::move(target)), std::move(cargs), &call)) return false;
            stmt_pre.push_back(std::move(call));
            continue;
        }
        Node assign;
        assign.kind = Node::Assign;
        assign.op = TAssign;
        assign.number = 1;
        assign.kids.push_back(std::move(target));
        assign.kids.push_back(std::move(value));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(assign));
        stmt_pre.push_back(std::move(stmt));
    }
    *out = node_name(hidden.name);
    return true;
}

bool Parser::local_visible(const std::string& name) const {
    if (cur_locals == nullptr) return false;
    for (int i = static_cast<int>(cur_locals->size()) - 1; i >= 0; --i) {
        const Local& local = (*cur_locals)[static_cast<size_t>(i)];
        if (local.name == name && scope_visible(local.scope)) return true;
    }
    return false;
}

// The static type of an expression, for the forms that name an object:
// names, fields, elements, dereferences, address-of, casts and calls.
bool Parser::static_type(const Node& node, Type* out) const {
    switch (node.kind) {
    case Node::Name:
        if (cur_locals != nullptr) {
            for (int i = static_cast<int>(cur_locals->size()) - 1; i >= 0; --i) {
                const Local& local = (*cur_locals)[static_cast<size_t>(i)];
                if (local.name == node.text && scope_visible(local.scope)) {
                    *out = local.type;
                    return true;
                }
            }
        }
        for (const Global& global : globals)
            if (global.name == node.text) {
                *out = global.type;
                return true;
            }
        for (const FuncDecl& fn : funcs)
            if (fn.name == node.text) {
                // The name of a function is a pointer to it.
                *out = Type{};
                out->base = 1;
                out->pointers = 1;
                out->func_id = fn.func_id;
                return true;
            }
        return false;
    case Node::Assign:
        return !node.kids.empty() && static_type(node.kids[0], out);
    case Node::Field: {
        Type base;
        if (node.kids.empty() || !static_type(node.kids[0], &base)) return false;
        if (node.op == TArrow) {
            if (base.pointers < 1) return false;
            base = peel_array(base);
        }
        if (base.base != 3 || base.struct_id < 0 || base.struct_id >= static_cast<int>(structs.size())) return false;
        // The last field of the name: a member of the class hides a member of a base with the same name
        // ([class.member.lookup]; Component::m_live and UIDamageNumbers::m_live), as the code generator reads it.
        const std::vector<Field>& fs = structs[static_cast<size_t>(base.struct_id)].fields;
        for (size_t k = fs.size(); k-- > 0;)
            if (fs[k].name == node.text) {
                *out = fs[k].type;
                return true;
            }
        return false;
    }
    case Node::Index: {
        Type base;
        if (node.kids.empty() || !static_type(node.kids[0], &base)) return false;
        *out = peel_array(base);
        return true;
    }
    case Node::Unary: {
        if (node.op == TEnd) {
            *out = node.type;
            return true;
        }
        Type inner;
        if (node.kids.empty() || !static_type(node.kids[0], &inner)) return false;
        if (node.op == TStar) {
            inner.is_ref = false;
            *out = peel_array(inner);
            return true;
        }
        if (node.op == TAmp) {
            inner.is_ref = false;
            if (inner.is_array()) {
                inner = peel_array(inner);
            }
            ++inner.pointers;
            *out = inner;
            return true;
        }
        if ((node.op == TMinus || node.op == TTilde) && inner.pointers == 0 && !inner.is_array() && inner.base != 3 &&
            inner.base != 0) {
            // -x and ~x ([expr.unary.op] 8, 10): the promoted type of x (std::max(-limit, x) deduces float).
            inner.is_ref = false;
            inner.is_const = false;
            if (inner.base == 2 || inner.base == 4 || inner.is_bool) {
                inner = Type{};
                inner.base = 1;
            }
            *out = inner;
            return true;
        }
        return false;
    }
    case Node::IntLit:
    case Node::StringLit:
        *out = node.type;
        return true;
    case Node::Binary: {
        if (node.kids.size() != 2) return false;
        if (node.op == TEq || node.op == TNe || node.op == TLt || node.op == TGt || node.op == TLe ||
            node.op == TGe || node.op == TAndAnd || node.op == TOrOr) {
            *out = Type{};
            out->base = 1;
            return true;
        }
        Type left;
        if (!static_type(node.kids[0], &left)) return false;
        Type right;
        const bool have_right = static_type(node.kids[1], &right);
        left.is_ref = false;
        const bool left_ptr = left.pointers > 0 || left.is_array();
        const bool right_ptr = have_right && (right.pointers > 0 || right.is_array());
        if (left_ptr && right_ptr && node.op == TMinus) {
            *out = Type{};
            out->base = 1;
            out->wide = true;
            return true;
        }
        const auto arith = [](const Type& t) { return t.pointers == 0 && !t.is_array() && t.base != 3 && t.base != 0; };
        if (!left_ptr && have_right && !right_ptr && arith(left) && arith(right)) {
            right.is_ref = false;
            if (left.base == 6 || right.base == 6) {
                *out = Type{};
                out->base = 6;
                return true;
            }
            if (left.base == 5 || right.base == 5) {
                *out = Type{};
                out->base = 5;
                return true;
            }
            Type pl = left;
            pl.base = 1;
            pl.is_bool = false;
            if (left.base == 1) pl.is_unsigned = left.is_unsigned;
            else pl.is_unsigned = false;
            Type pr = right;
            pr.base = 1;
            pr.is_bool = false;
            if (right.base != 1) pr.is_unsigned = false;
            if (node.op == TShl || node.op == TShr) {
                *out = pl;
                out->bits = -1;
                return true;
            }
            *out = const_common(pl, pr);
            return true;
        }
        *out = left;
        return true;
    }
    case Node::Ternary: {
        if (node.kids.size() != 3 || !static_type(node.kids[1], out)) return false;
        if (out->base == 0 && out->pointers == 0) return static_type(node.kids[2], out);  // c ? throw x : y
        return true;
    }
    case Node::Call:
        if (node.number == 1) {
            *out = node.type;
            return true;
        }
        if (!node.text.empty()) {
            for (const FuncDecl& fn : funcs)
                if (fn.name == node.text) {
                    *out = fn.ret;
                    return true;
                }
            // A call through a variable that holds a function pointer.
            Node callee;
            callee.kind = Node::Name;
            callee.text = node.text;
            Type ct;
            if (static_type(callee, &ct) && ct.func_id >= 0 && ct.func_id < static_cast<int>(func_types.size())) {
                *out = func_types[static_cast<size_t>(ct.func_id)].ret;
                if (out->is_ref) {  // the call gives the address (fp_reference_result reads through it)
                    out->is_ref = false;
                    out->rref = false;
                    out->pointers += 1;
                }
                return true;
            }
        } else if (!node.kids.empty()) {
            Type ct;
            if (static_type(node.kids[0], &ct) && ct.func_id >= 0 && ct.func_id < static_cast<int>(func_types.size())) {
                *out = func_types[static_cast<size_t>(ct.func_id)].ret;
                if (out->is_ref) {
                    out->is_ref = false;
                    out->rref = false;
                    out->pointers += 1;
                }
                return true;
            }
        }
        return false;
    default:
        return false;
    }
}

// new T, new T(args): malloc, then the constructor (which returns its object). The program
// declares malloc and free itself (the runtime provides them). Written from ISO/IEC 14882
// [expr.new].
// malloc(sizeof(T)): the storage of one new object.
Node Parser::malloc_of(const Type& ty) const {
    Node size;
    size.kind = Node::IntLit;
    size.number = static_cast<uint64_t>(size_of(ty));
    size.type.base = 1;
    Node alloc;
    alloc.kind = Node::Call;
    alloc.text = "malloc";
    alloc.kids.push_back(std::move(size));
    return alloc;
}

bool Parser::parse_new(Node* out) {
    next();
    // new (place) T(args) ([expr.new]): construct in storage that is already there; nothing is allocated.
    bool has_place = false;
    Node place;
    if (is(TLParen)) {
        const size_t save = at;
        next();
        if (!is_decl_start()) {
            if (!parse_expr(1, &place)) return false;
            if (!expect(TRParen, "')'")) return false;
            has_place = true;
        } else {
            at = save;
        }
    }
    Type ty;
    bool imp = false;
    bool exp = false;
    if (!parse_type(&ty, &imp, &exp)) return false;
    while (accept(TStar)) ++ty.pointers;
    if (has_place && is(TLBrack)) {
        error = "placement new of an array is not supported";
        return false;
    }
    if (accept(TLBrack)) {
        // new T[n]: n objects. A class with a constructor or destructor goes through __vnew_C
        // (a count in front of the block when destructors must run per element).
        Node count;
        if (!parse_expr(0, &count)) return false;
        if (!expect(TRBrack, "']'")) return false;
        const bool vobj = ty.base == 3 && ty.pointers == 0 && ty.struct_id >= 0;
        if (vobj && (class_needs_ctor(structs[static_cast<size_t>(ty.struct_id)]) ||
                     class_needs_dtor(structs[static_cast<size_t>(ty.struct_id)]))) {
            if (!ensure_vector_helpers(ty.struct_id)) return false;
            Node call;
            call.kind = Node::Call;
            call.text = "__vnew_" + structs[static_cast<size_t>(ty.struct_id)].name;
            call.type = ty;
            call.type.pointers = 1;
            call.kids.push_back(std::move(count));
            *out = std::move(call);
            return true;
        }
        Node size;
        size.kind = Node::IntLit;
        size.number = static_cast<uint64_t>(size_of(ty));
        size.type.base = 1;
        Node bytes;
        bytes.kind = Node::Binary;
        bytes.op = TStar;
        bytes.kids.push_back(std::move(count));
        bytes.kids.push_back(std::move(size));
        Node alloc;
        alloc.kind = Node::Call;
        alloc.text = "malloc";
        alloc.kids.push_back(std::move(bytes));
        Node cast;
        cast.kind = Node::Unary;
        cast.type = ty;
        cast.type.pointers += 1;
        cast.kids.push_back(std::move(alloc));
        *out = std::move(cast);
        return true;
    }
    std::vector<Node> args;
    if (accept(TLParen)) {
        if (!parse_call_args(&args)) return false;
    } else if (is(TLBrace) && !has_place) {
        // new T{a, b} ([expr.new], [dcl.init.list]): a class with a constructor takes the list as constructor
        // arguments; an aggregate gets its members in order, and the members after the last one given are zero; a
        // scalar gets its one value (or zero for {}).
        next();
        std::vector<Node> items;
        while (!is(TRBrace) && !is(TEnd)) {
            Node item;
            if (!parse_expr(1, &item)) return false;
            items.push_back(std::move(item));
            if (!accept(TComma)) break;
        }
        if (!expect(TRBrace, "'}'")) return false;
        const bool cls = ty.base == 3 && ty.pointers == 0 && ty.struct_id >= 0;
        if (cls && class_needs_ctor(structs[static_cast<size_t>(ty.struct_id)])) {
            args = std::move(items);
        } else {
            Node cast;
            cast.kind = Node::Unary;
            cast.type = ty;
            cast.type.pointers += 1;
            cast.kids.push_back(malloc_of(ty));
            Local hidden;
            hidden.name = "__pn" + std::to_string(tmp_count++);
            hidden.type = cast.type;
            place_local(cur_locals, hidden);
            auto add_stmt = [&](Node e) {
                Node st;
                st.kind = Node::ExprStmt;
                st.kids.push_back(std::move(e));
                stmt_pre.push_back(std::move(st));
            };
            Node set_ptr;
            set_ptr.kind = Node::Assign;
            set_ptr.op = TAssign;
            set_ptr.kids.push_back(node_name(hidden.name));
            set_ptr.kids.push_back(std::move(cast));
            add_stmt(std::move(set_ptr));
            auto zero = []() {
                Node z;
                z.kind = Node::IntLit;
                z.number = 0;
                z.type.base = 1;
                return z;
            };
            auto deref = [&]() {
                Node d;
                d.kind = Node::Unary;
                d.op = TStar;
                d.kids.push_back(node_name(hidden.name));
                return d;
            };
            if (!cls) {
                if (items.size() > 1) {
                    error = "too many values in new T{...}";
                    return false;
                }
                Node store;
                store.kind = Node::Assign;
                store.op = TAssign;
                store.kids.push_back(deref());
                store.kids.push_back(items.empty() ? zero() : std::move(items[0]));
                add_stmt(std::move(store));
            } else {
                const StructDef def = structs[static_cast<size_t>(ty.struct_id)];
                size_t next_item = 0;
                for (const Field& f : def.fields) {
                    if (f.name.compare(0, 6, "__vptr") == 0) continue;
                    const bool scalar = f.type.pointers > 0 || (f.type.base != 3 && !f.type.is_array());
                    if (next_item >= items.size()) {
                        if (!scalar) {
                            error = "new T{...} leaves out a member that is not a number or pointer (" + f.name + ")";
                            return false;
                        }
                    } else if (!scalar) {
                        error = "new T{...} of a member that is an array or class is not supported (" + f.name + ")";
                        return false;
                    }
                    Node field;
                    field.kind = Node::Field;
                    field.op = TDot;
                    field.text = f.name;
                    field.kids.push_back(deref());
                    Node store;
                    store.kind = Node::Assign;
                    store.op = TAssign;
                    store.kids.push_back(std::move(field));
                    store.kids.push_back(next_item < items.size() ? std::move(items[next_item]) : zero());
                    ++next_item;
                    add_stmt(std::move(store));
                }
                if (next_item < items.size()) {
                    error = "too many values in new T{...}";
                    return false;
                }
            }
            *out = node_name(hidden.name);
            return true;
        }
    }
    const bool obj = ty.base == 3 && ty.pointers == 0 && ty.struct_id >= 0;
    bool plain_copy = false;  // new C(other) where C has constructors but no copy constructor: the bytes are copied
    if (obj && args.size() == 1 && class_needs_ctor(structs[static_cast<size_t>(ty.struct_id)]) &&
        !class_has_copy(ty.struct_id)) {
        Type from;
        plain_copy = static_type(args[0], &from) && from.base == 3 && from.pointers == 0 && !from.is_array() &&
                     from.struct_id == ty.struct_id;
    }
    const bool ctor = obj && class_needs_ctor(structs[static_cast<size_t>(ty.struct_id)]) && !plain_copy;
    if (!args.empty() && !ctor && !(args.size() == 1 && (has_place || plain_copy || !obj))) {
        error = "new with arguments needs a class with a constructor";
        return false;
    }
    Node cast;
    cast.kind = Node::Unary;
    cast.type = ty;
    cast.type.pointers += 1;
    cast.kids.push_back(has_place ? std::move(place) : malloc_of(ty));
    if (!ctor && !args.empty()) {
        // new (p) int(v): the value is stored in a statement before this one, and the pointer is the result.
        Local hidden;
        hidden.name = "__pn" + std::to_string(tmp_count++);
        hidden.type = cast.type;
        place_local(cur_locals, hidden);
        Node set_ptr;
        set_ptr.kind = Node::Assign;
        set_ptr.op = TAssign;
        set_ptr.kids.push_back(node_name(hidden.name));
        set_ptr.kids.push_back(std::move(cast));
        Node target;
        target.kind = Node::Unary;
        target.op = TStar;
        target.kids.push_back(node_name(hidden.name));
        Node store;
        store.kind = Node::Assign;
        store.op = TAssign;
        store.kids.push_back(std::move(target));
        store.kids.push_back(std::move(args[0]));
        Node set_stmt;
        set_stmt.kind = Node::ExprStmt;
        set_stmt.kids.push_back(std::move(set_ptr));
        Node store_stmt;
        store_stmt.kind = Node::ExprStmt;
        store_stmt.kids.push_back(std::move(store));
        stmt_pre.push_back(std::move(set_stmt));
        stmt_pre.push_back(std::move(store_stmt));
        *out = node_name(hidden.name);
        return true;
    }
    if (!ctor) {
        *out = std::move(cast);
        return true;
    }
    std::string tctor;
    if (!ensure_ctor_template(ty.struct_id, args, &tctor)) return false;
    const Method* found_m = find_ctor(ty.struct_id, args);
    if (!tctor.empty()) found_m = find_ctor_instance(ty.struct_id, tctor);
    if (found_m == nullptr) {
        error = "no matching constructor in " + structs[static_cast<size_t>(ty.struct_id)].name;
        return false;
    }
    const Method m_copy = *found_m;  // adjust_args can move the class's methods (constructor template instances)
    const Method* m = &m_copy;
    if (!pad_defaults(m->defaults, m->params.size(), &args)) return false;
    if (!adjust_args(m->params, &args)) return false;
    Node call;
    call.kind = Node::Call;
    call.text = m->qname;
    call.number = 1;
    call.type = m->ret;
    call.kids.push_back(std::move(cast));
    for (Node& arg : args) call.kids.push_back(std::move(arg));
    *out = std::move(call);
    return true;
}

// delete p; the destructor, then free. A virtual destructor is reached through the table slot of
// the scalar deleting destructor, which frees the memory itself. A null pointer does nothing.
bool Parser::parse_delete(Node* out) {
    next();
    bool vec = false;
    if (is(TLBrack) && at + 1 < tokens.size() && tokens[at + 1].kind == TRBrack) {
        next();
        next();
        vec = true;
    }
    Node target;
    if (!parse_unary(&target)) return false;
    if (!expect(TSemi, "';'")) return false;
    Type ot;
    if (!static_type(target, &ot) || ot.pointers != 1) {
        error = "delete needs a pointer";
        return false;
    }
    if (has_effects(target)) {
        error = "delete needs a pointer expression without side effects";
        return false;
    }
    if (vec && ot.base == 3 && ot.struct_id >= 0 && class_needs_dtor(structs[static_cast<size_t>(ot.struct_id)])) {
        // delete[] p for objects with destructors: __vdel_C runs them (last to first) and frees.
        Node call;
        call.kind = Node::Call;
        call.text = "__vdel_" + structs[static_cast<size_t>(ot.struct_id)].name;
        call.kids.push_back(std::move(target));
        out->kind = Node::ExprStmt;
        out->kids.push_back(std::move(call));
        return true;
    }
    Node then_block;
    then_block.kind = Node::Block;
    const bool cls = ot.base == 3 && !vec && ot.struct_id >= 0 && class_needs_dtor(structs[static_cast<size_t>(ot.struct_id)]);
    const Method* dm = cls ? find_method(ot.struct_id, "<dtor>", 0) : nullptr;
    if (dm != nullptr && dm->is_virtual) {
        FuncType ft;
        ft.ret.base = 0;
        ft.ret.pointers = 1;
        Type self_ptr;
        self_ptr.base = 3;
        self_ptr.struct_id = dm->owner;
        self_ptr.pointers = 1;
        ft.params.push_back(self_ptr);
        Type flag;
        flag.base = 1;
        flag.is_unsigned = true;
        ft.params.push_back(flag);
        Type fp;
        fp.base = 1;
        fp.pointers = 1;
        fp.func_id = static_cast<int>(func_types.size());
        func_types.push_back(ft);
        Node table;
        table.kind = Node::Field;
        table.op = TArrow;
        table.text = "__vptr";
        table.kids.push_back(target);
        Node slot;
        slot.kind = Node::IntLit;
        slot.number = static_cast<uint64_t>(dm->slot);
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
        Node fn_target;
        fn_target.kind = Node::Unary;
        fn_target.type = fp;
        fn_target.kids.push_back(std::move(entry));
        Node call;
        call.kind = Node::Call;
        call.number = 1;
        call.type = ft.ret;
        call.kids.push_back(std::move(fn_target));
        call.kids.push_back(target);
        Node one;
        one.kind = Node::IntLit;
        one.number = 1;
        one.type.base = 1;
        call.kids.push_back(std::move(one));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(call));
        then_block.kids.push_back(std::move(stmt));
    } else {
        if (dm != nullptr) {
            Node call;
            if (!special_call(ot.struct_id, "<dtor>", target, {}, &call)) return false;
            then_block.kids.push_back(std::move(call));
        }
        Node release;
        release.kind = Node::Call;
        release.text = "free";
        release.kids.push_back(target);
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(release));
        then_block.kids.push_back(std::move(stmt));
    }
    out->kind = Node::If;
    out->kids.push_back(std::move(target));
    out->kids.push_back(std::move(then_block));
    return true;
}

bool Parser::parse_unary(Node* out) {
    if (is(TIdent) && cur().text == "new") {
        if (!parse_new(out)) return false;
        return parse_postfix(out);
    }
    if (is(TSizeof) && at + 1 < tokens.size() && tokens[at + 1].kind == TEllipsis) {
        // sizeof...(Ts) or sizeof...(rest): the number of elements of a pack ([expr.sizeof]).
        next();
        next();
        if (!expect(TLParen, "'('")) return false;
        if (!is(TIdent)) {
            error = "expected a pack name";
            return false;
        }
        long long count = -1;
        auto tp = type_packs.find(cur().text);
        if (tp != type_packs.end()) count = static_cast<long long>(tp->second.size());
        auto pp = param_packs.find(cur().text);
        if (count < 0 && pp != param_packs.end()) count = static_cast<long long>(pp->second.size());
        if (count < 0) {
            error = "sizeof... of something that is not a parameter pack";
            return false;
        }
        next();
        if (!expect(TRParen, "')'")) return false;
        out->kind = Node::IntLit;
        out->number = static_cast<uint64_t>(count);
        out->type.base = 1;
        return true;
    }
    if (is(TIdent) && cur().text == "typeid" && at + 1 < tokens.size() && tokens[at + 1].kind == TLParen)
        return parse_typeid(out) && parse_postfix(out);
    if (is(TIdent) && cur().text.compare(0, 5, "__is_") == 0 && at + 1 < tokens.size() && tokens[at + 1].kind == TLParen)
        return parse_type_trait(out);
    if (is(TSizeof)) {
        next();
        // sizeof unary-expression has no parentheses ([expr.sizeof]); sizeof ( type ) has them.
        const bool sizeof_paren = is(TLParen);
        if (sizeof_paren) next();
        int bytes = 0;
        if (sizeof_paren && is_decl_start() && !is(TStatic)) {
            Type ty;
            bool imp = false;
            bool exp = false;
            if (!parse_type(&ty, &imp, &exp)) return false;
            while (accept(TStar)) ++ty.pointers;
            if (!parse_brackets(&ty)) return false;
            if (ty.is_ref) {  // sizeof(T&) is the size of T ([expr.sizeof] 2)
                ty.is_ref = false;
                ty.rref = false;
            }
            bytes = size_of(ty);
        } else {
            Node inner;
            if (sizeof_paren ? !parse_expr(1, &inner) : !parse_unary(&inner)) return false;
            if (inner.kind == Node::Name) {
                if (cur_locals != nullptr) {
                    for (int i = static_cast<int>(cur_locals->size()) - 1; i >= 0; --i) {
                        const Local& local = (*cur_locals)[static_cast<size_t>(i)];
                        if (local.name == inner.text && scope_visible(local.scope)) {
                            Type lt = local.type;  // a reference names the object it refers to ([expr.sizeof] 2)
                            lt.is_ref = false;
                            lt.rref = false;
                            bytes = size_of(lt);
                            break;
                        }
                    }
                }
                if (bytes == 0) {
                    for (const Global& global : globals) {
                        if (global.name == inner.text) bytes = size_of(global.type);
                    }
                }
            } else if (inner.kind == Node::StringLit) {
                bytes = static_cast<int>(inner.text.size()) + 1;
            } else if (inner.kind == Node::IntLit) {
                bytes = inner.type.base == 6 ? 8 : inner.type.wide ? 8 : 4;
            }
            if (bytes == 0) {
                // sizeof of any other expression: the size of its type, if the parser can tell it.
                Type et;
                if (static_type(inner, &et)) {
                    et.is_ref = false;
                    et.rref = false;
                    bytes = size_of(et);
                }
            }
            if (bytes == 0) {
                error = "sizeof needs a type or a name";
                return false;
            }
        }
        if (sizeof_paren && !expect(TRParen, "')'")) return false;
        out->kind = Node::IntLit;
        out->number = static_cast<uint64_t>(bytes);
        // sizeof gives a size_t: unsigned and 64 bits wide ([expr.sizeof]).
        out->type.base = 1;
        out->type.wide = true;
        out->type.is_unsigned = true;
        return true;
    }
    if (is(TIdent) && cur().text == "throw" && eh_) {
        next();
        out->kind = Node::Call;
        out->number = 1;
        out->type = Type{};
        out->type.base = 0;
        if (is(TSemi) || is(TColon) || is(TRParen) || is(TComma)) {  // throw;
            out->text = "__okay_eh_rethrow";
            return true;
        }
        Node value;
        if (!parse_expr(1, &value)) return false;
        Type vt;
        if (!static_type(value, &vt)) {
            error = "cannot tell the type of the thrown value";
            return false;
        }
        if (vt.is_array()) {
            vt = peel_array(vt);
            vt.pointers += 1;
            vt.array_len = -1;
        }
        vt.is_ref = false;
        std::string helper;
        const bool temp_operand = value.kind == Node::Name && value.text.compare(0, 4, "__bt") == 0;
        if (!ensure_throw_helper(vt, temp_operand, &helper)) return false;
        out->text = helper;
        out->kids.push_back(std::move(value));
        for (const FuncDecl& known : funcs) {
            if (known.name != helper) continue;
            std::vector<Type> source = known.params;
            for (size_t i = 0; i < source.size() && i < known.param_ref.size(); ++i)
                if (known.param_ref[i]) {
                    source[i].pointers -= 1;
                    source[i].is_ref = true;
                }
            if (!adjust_args(source, &out->kids)) return false;
            break;
        }
        return true;
    }
    if (is(TPlusPlus) || is(TMinusMinus)) {
        out->kind = Node::Unary;
        out->op = cur().kind;
        out->number = 0;
        next();
        Node inner;
        if (!parse_unary(&inner)) return false;
        {
            bool done = false;
            if (!operator_unary(out->op, false, &inner, &done)) return false;
            if (done) {
                *out = std::move(inner);
                return parse_postfix(out);
            }
        }
        out->kids.push_back(std::move(inner));
        return true;
    }
    if (is(TNumber)) {
        out->kind = Node::IntLit;
        out->number = cur().number;
        out->type.base = cur().real != 0 ? cur().real : 1;
        if (cur().real == 0) {
            out->type.wide = cur().ity >= 2;
            out->type.is_unsigned = cur().ity == 1 || cur().ity == 3;
            // A number is kept in the form its type gives: sign-extended int, zero-extended unsigned int.
            if (cur().ity == 0) out->number = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(out->number)));
            if (cur().ity == 5 || cur().ity == 6) {
                out->type.wide = false;
                out->type.is_unsigned = false;
                // ([lex.ccon], [lex.bool]): 'a' is a char and true a bool, so f(char) and f(bool) are chosen over f(int)
                out->text = cur().ity == 5 ? "char" : "bool";
            }
        }
        next();
    } else if (is(TString)) {
        out->kind = Node::StringLit;
        out->text = cur().text;
        out->type.pointers = 1;
        out->type.base = 2;
        if (cur().ity == 16 || cur().ity == 32) {
            // L"..." is an array of wchar_t (here unsigned short), U"..." of char32_t (unsigned int): the code unit
            // bytes, and the zero unit's bytes after them (the generator adds the last zero byte).
            out->type.base = cur().ity == 16 ? 4 : 1;
            out->type.is_unsigned = true;
            out->text.append(static_cast<size_t>(cur().ity == 16 ? 1 : 3), '\0');
        }
        next();
    } else if (is(TIdent) && cur().text == "dynamic_cast" && at + 1 < tokens.size() && tokens[at + 1].kind == TLt) {
        // dynamic_cast<T*>(e) ([expr.dynamic.cast]): for a class T with virtual functions, a call of
        // std::__okay_dynamic_cast (<typeinfo>) with e and T's type information, read back as a T*. A cast to a base
        // (or the same class) needs no check. Own limits: pointers only, no virtual bases.
        next();
        next();
        Type ty;
        bool imp = false;
        bool exp = false;
        if (!parse_type(&ty, &imp, &exp)) return false;
        while (accept(TStar)) ++ty.pointers;
        if (is(TAmp) || is(TAndAnd)) {
            error = "dynamic_cast to a reference is not supported";
            return false;
        }
        if (!expect(TGt, "'>'") || !expect(TLParen, "'('")) return false;
        Node inner;
        if (!parse_expr(0, &inner)) return false;
        if (!expect(TRParen, "')'")) return false;
        Type from;
        if (!static_type(inner, &from)) {
            error = "dynamic_cast of an expression whose type is not known";
            return false;
        }
        if (ty.pointers != 1 || ty.base != 3 || ty.struct_id < 0) {
            error = "dynamic_cast needs a pointer to a class";
            return false;
        }
        if (from.pointers == 1 && from.base == 3 && from.struct_id >= 0 && derives_from(from.struct_id, ty.struct_id)) {
            class_ptr_convert(&inner, ty);  // the base subobject (Round 29)
            *out = cast_node(ty, std::move(inner));  // to a base or the same class: no check ([expr.dynamic.cast] 5)
            return parse_postfix(out);
        }
        if (!names_function("std::__okay_dynamic_cast")) {
            error = "dynamic_cast needs #include <typeinfo>";
            return false;
        }
        const std::string sym = type_info_symbol(ty.struct_id, structs[static_cast<size_t>(ty.struct_id)].symname());
        const int ti_sid = find_struct("type_info");
        Type ti_ptr;
        ti_ptr.base = 3;
        ti_ptr.struct_id = ti_sid;
        ti_ptr.pointers = 1;
        std::vector<Node> cargs;
        cargs.push_back(cast_node(void_ptr(), std::move(inner)));
        cargs.push_back(cast_node(ti_ptr, node_addr(node_name(sym))));
        Node call;
        bool found = false;
        if (!free_call("std::__okay_dynamic_cast", std::move(cargs), &call, &found)) return false;
        if (!found) {
            error = "dynamic_cast needs #include <typeinfo>";
            return false;
        }
        *out = cast_node(ty, std::move(call));
        return parse_postfix(out);
    } else if (is(TIdent) || (is(TColonColon) && at + 1 < tokens.size() && tokens[at + 1].kind == TIdent)) {
        const bool from_global = is(TColonColon);
        if (from_global) next();  // ::name: looked up at global scope, not in the namespaces around the call
        std::string name = cur().text;
        // A member function of the class being read hides a namespace function of the same name in a call
        // ([basic.lookup.unqual] 1, [class.member.lookup]): `Text(s)` in a member calls the member Text, even when it is
        // declared further down the class and a free Text exists.
        const bool member_call = !from_global && !cur().nsq && at + 1 < tokens.size() &&
                                 tokens[at + 1].kind == TLParen && member_hides(name, TLParen);
        next();
        if (!global_alias.empty() && !is(TLParen) && !local_visible(name)) {
            const auto alias = global_alias.find(name);
            if (alias != global_alias.end()) name = alias->second;
        }
        if (is(TLt) && !local_visible(name)) {
            // std::is_same_v<A, B>: a variable template (kept by its bare name, like function templates).
            std::string bare = name;
            const size_t first = name.find("::");
            if (first != std::string::npos && namespaces.count(name.substr(0, first)) > 0)
                bare = name.substr(name.rfind("::") + 2);
            const auto vt = var_templates.find(bare);
            if (vt != var_templates.end()) {
                if (!parse_var_template_use(vt->second, out)) return false;
                return parse_postfix(out);
            }
        }
        int scope_sid = -1;
        if (is(TLt) && class_templates.count(name) > 0 && !local_visible(name) && !(!tokens[at - 1].nsq && member_hides(name, cur().kind))) {
            // Box<int>::member in an expression.
            std::vector<TArg> targs;
            if (!parse_template_args(class_templates[name], &targs, false)) return false;
            Type inst;
            if (!instantiate_class(name, targs, &inst)) return false;
            if ((is(TLParen) || is(TLBrace)) && inst.base == 3 && inst.struct_id >= 0) {
                // Box<int>(args): a temporary of the instance, made like any class name followed by an argument list.
                name = structs[static_cast<size_t>(inst.struct_id)].name;
            } else if (!is(TColonColon) || at + 1 >= tokens.size() || tokens[at + 1].kind != TIdent) {
                error = "expected :: and a member name after a class template instance";
                return false;
            } else {
                next();
                scope_sid = inst.struct_id;
                name = "<inst>::" + cur().text;
                next();
            }
        } else {
            while (accept(TColonColon)) {  // a::b::c: namespaces, then a name
                if (!is(TIdent)) {
                    error = "expected a name after ::";
                    return false;
                }
                name += "::" + cur().text;
                next();
            }
        }
        bool exact_function = false;
        if (name.find("::") != std::string::npos)
            for (const FuncDecl& fd : funcs)
                if (fd.name == name) exact_function = true;
        if (!exact_function && !from_global && !member_call && !current_ns.empty() && is(TLParen) && !local_visible(name)) {
            // A bare name inside a namespace finds the functions of that namespace and of the ones around
            // it ([basic.lookup.unqual]), innermost first.
            std::string ns = current_ns;
            for (;;) {
                const std::string candidate = ns + "::" + name;
                bool known = false;
                for (const FuncDecl& fd : funcs)
                    if (fd.name == candidate) known = true;
                if (known) {
                    name = candidate;
                    break;
                }
                const size_t cut = ns.rfind("::");
                if (cut == std::string::npos) break;
                ns = ns.substr(0, cut);
            }
        }
        if (name.find("::") == std::string::npos && is(TLParen) && !member_call && !local_visible(name) &&
            !using_namespaces.empty()) {
            // A bare call that names no function of its own: look in the namespaces of `using namespace`.
            bool own = false;
            for (const FuncDecl& fd : funcs)
                if (fd.name == name) own = true;
            if (!own) {
                // `using namespace detail;` inside namespace okay names okay::detail ([namespace.udir] 2): the
                // namespace is looked for from the namespace being read outward.
                bool found = false;
                for (const std::string& ns : using_namespaces) {
                    std::string open = current_ns;
                    for (;;) {
                        const std::string candidate = (open.empty() ? std::string() : open + "::") + ns + "::" + name;
                        for (const FuncDecl& fd : funcs)
                            if (fd.name == candidate) found = true;
                        if (found) {
                            name = candidate;
                            break;
                        }
                        if (open.empty()) break;
                        const size_t cut = open.rfind("::");
                        open = cut == std::string::npos ? std::string() : open.substr(0, cut);
                    }
                    if (found) break;
                }
            }
        }
        if (name.find("::") != std::string::npos && is(TLParen) && !local_visible(name)) {
            // Easing::Linear(x) where Easing is a namespace inside one that a `using namespace` names (or that is open
            // here): the function is registered with the whole path.
            bool own = false;
            for (const FuncDecl& fd : funcs)
                if (fd.name == name) own = true;
            if (!own) {
                std::vector<std::string> outer = using_namespaces;
                std::string open = current_ns;
                while (!open.empty()) {
                    outer.push_back(open);
                    const size_t cut = open.rfind("::");
                    open = cut == std::string::npos ? std::string() : open.substr(0, cut);
                }
                for (const std::string& ns : outer) {
                    const std::string candidate = ns + "::" + name;
                    bool known = false;
                    for (const FuncDecl& fd : funcs)
                        if (fd.name == candidate) known = true;
                    if (known) {
                        name = candidate;
                        break;
                    }
                }
            }
        }
        if (name.find("::") != std::string::npos && (is(TLParen) || is(TLt))) {
            // std::min(a, b): a function template of a namespace is registered by its bare name.
            const size_t first = name.find("::");
            const size_t last = name.rfind("::");
            const std::string suffix = name.substr(last + 2);
            // An ordinary function of that full name (std::filesystem::remove beside the algorithm std::remove) is the
            // one called ([namespace.qual]); okay-ms keeps templates by their bare names only.
            // std::remove(path) is <cstdio>'s function and std::remove(first, last, v) the algorithm template: when an
            // ordinary function of the name exists, the template is used only for a call none of the functions can take
            // and one of the template's overloads can (by the number of arguments, [over.match.viable] 2).
            bool use_template = namespaces.count(name.substr(0, first)) > 0 && func_templates.count(suffix) > 0;
            if (use_template && ovl.count(name) > 0) {
                use_template = false;
                if (is(TLParen)) {
                    const size_t n = call_arg_count_at(at);
                    bool plain_takes = false;
                    for (const OvFn& o : ovl[name])
                        if (o.required <= n && (o.params.size() >= n || o.vararg)) plain_takes = true;
                    use_template = !plain_takes && func_template_takes(suffix, n);
                }
            }
            if (use_template) name = suffix;
        }
        // std::filesystem::path(p) names the class even where a local `path` is visible ([basic.lookup.qual]).
        bool ns_class = false;
        if (name.find("::") != std::string::npos && (is(TLParen) || is(TLBrace))) {
            // std::string("abc"): a class of a namespace is registered by its bare name, as in a declaration.
            const size_t first = name.find("::");
            const std::string suffix = name.substr(name.rfind("::") + 2);
            bool is_function = false;
            for (const FuncDecl& fd : funcs)
                if (fd.name == name) is_function = true;
            if (!is_function && namespaces.count(name.substr(0, first)) > 0 && scope_class(name.substr(0, first)) < 0 &&
                (class_name_text(suffix) || (typedefs.count(suffix) == 0 && find_struct(suffix) >= 0) ||
                 (typedefs.count(suffix) > 0 && typedefs[suffix].base == 3))) {
                name = suffix;  // also std::monostate(): a class with no members or constructors ([expr.type.conv] 2)
                ns_class = true;
            }
        }
        size_t colons = name.find("::");
        if (building != nullptr && colons == std::string::npos && !is(TLParen) && !local_visible(name)) {
            const auto found = building->statics.find(name);
            if (found != building->statics.end() && found->second.has_const) {
                const_node(found->second, out);
                return parse_postfix(out);
            }
        }
        if (scope_sid < 0 && colons != std::string::npos) scope_sid = scope_class(name.substr(0, colons));
        if (scope_sid >= 0 && colons != std::string::npos && (is(TLParen) || is(TLBrace))) {
            // Outer::Nested(args) and Outer::Nested{args}: a temporary of a nested class (std::thread::id()), when the
            // class has no member function of that name. Nested classes are kept under their plain name.
            const std::string member = name.substr(colons + 2);
            if (is(TLBrace) && cur_locals != nullptr) {
                // Outer::Inner{args} and Outer::Mid::Inner{args} (Round 29: Inventory::Slot{...}): the class nested in
                // the named class, found by its parent (another class of the same plain name elsewhere is not taken).
                int walk = scope_sid;
                std::string rest = member;
                while (walk >= 0) {
                    const size_t step = rest.find("::");
                    const std::string part = rest.substr(0, step);
                    int nested = -1;
                    for (size_t k = 0; k < structs.size(); ++k)
                        if (structs[k].name == part && structs[k].parent == walk && !structs[k].incomplete)
                            nested = static_cast<int>(k);
                    walk = nested;
                    if (step == std::string::npos) break;
                    rest = rest.substr(step + 2);
                }
                if (walk >= 0) {
                    if (!braced_temp(walk, out)) return false;
                    return parse_postfix(out);
                }
            }
            bool member_fn = false;
            for (const Method& m : structs[static_cast<size_t>(scope_sid)].methods)
                if (m.name == member) member_fn = true;
            int tmpl_owner = -1;
            if (find_member_tmpl(scope_sid, member, &tmpl_owner) != nullptr) member_fn = true;
            if (!member_fn && member.find("::") == std::string::npos && find_struct(member) >= 0 &&
                typedefs.count(member) == 0) {
                name = member;
                colons = std::string::npos;
                scope_sid = -1;
            }
        }
        if (scope_sid >= 0 && colons != std::string::npos) {
            std::string member = name.substr(colons + 2);
            // Outer::Inner::name: step into the nested class for each further name ([class.qual]); an enumerator of
            // an enumeration in it (Scene::RenderSettings::SkyProcedural) is then a member of the innermost class.
            for (size_t step = member.find("::"); step != std::string::npos; step = member.find("::")) {
                const std::string part = member.substr(0, step);
                int nested = -1;
                for (size_t k = 0; k < structs.size(); ++k)
                    if (structs[k].name == part && structs[k].parent == scope_sid) nested = static_cast<int>(k);
                if (nested < 0) {
                    // A member type that names a class (file_time_type::clock::now(), Round 30): its class.
                    const auto td = structs[static_cast<size_t>(scope_sid)].tdefs.find(part);
                    if (td != structs[static_cast<size_t>(scope_sid)].tdefs.end() && td->second.base == 3 &&
                        td->second.pointers == 0 && td->second.struct_id >= 0)
                        nested = td->second.struct_id;
                }
                if (nested < 0) break;
                scope_sid = nested;
                member = member.substr(step + 2);
            }
            if (member.find("::") == std::string::npos && !is(TLParen) && !is(TLt) && enum_values.count(member) > 0 &&
                !static_member_expr(scope_sid, member, out)) {
                error.clear();
                *out = Node{};
                out->kind = Node::IntLit;
                out->number = enum_values[member];
                out->type.base = 1;
                out->type.enum_id = enum_id_of(member);
                return parse_postfix(out);
            }
            if ((!is(TLParen) || static_object_call(scope_sid, member)) && static_member_expr(scope_sid, member, out))
                return parse_postfix(out);
            if (!is(TLParen) && !is(TLt) && static_method_ref(scope_sid, member, out)) return parse_postfix(out);
            std::vector<TArg> qtargs;
            if (is(TLt)) {
                int tmpl_owner = -1;
                const MemberTmpl* mt = find_member_tmpl(scope_sid, member, &tmpl_owner);
                if (mt != nullptr && !parse_template_args(mt->td, &qtargs, true)) return false;
            }
            return qualified_call(out, scope_sid, member, std::move(qtargs)) && parse_postfix(out);
        }
        if (cur_class >= 0 && colons == std::string::npos && structs[static_cast<size_t>(cur_class)].lam_scope >= 0 &&
            ((name == "this" && structs[static_cast<size_t>(cur_class)].lam_outer >= 0) ||
             (name != "this" && !local_visible(name) && find_field_named(structs[static_cast<size_t>(cur_class)], name) < 0))) {
            // In the body of a lambda written in a member function: the class's static members are found by name; when
            // the lambda captured `this`, `this` is the captured pointer and the other members are reached through it
            // (also those of a class around an enclosing lambda).
            const bool has_self = structs[static_cast<size_t>(cur_class)].lam_outer >= 0;
            Node self;
            self.kind = Node::Field;
            self.op = TArrow;
            self.text = "__this";
            self.kids.push_back(node_name("this"));
            int cls = has_self ? structs[static_cast<size_t>(cur_class)].lam_outer : structs[static_cast<size_t>(cur_class)].lam_scope;
            if (name == "this") {
                *out = std::move(self);
                return parse_postfix(out);
            }
            while (cls >= 0) {
                const StructDef& od = structs[static_cast<size_t>(cls)];
                bool is_field = find_field_named(od, name) >= 0;
                bool is_method = false;
                bool any_object_fn = false;
                for (const Method& m : od.methods)
                    if (m.name == name) {
                        is_method = true;
                        if (!m.is_static) any_object_fn = true;
                    }
                int tmpl_owner = -1;
                if (find_member_tmpl(cls, name, &tmpl_owner) != nullptr) {
                    is_method = true;
                    any_object_fn = true;
                }
                if ((!is(TLParen) || static_object_call(cls, name)) && static_member_expr(cls, name, out))
                    return parse_postfix(out);
                if (is_method && is(TLParen) && !any_object_fn) return qualified_call(out, cls, name, {}) && parse_postfix(out);
                if (!has_self) break;
                if (is_field) {
                    Node field;
                    field.kind = Node::Field;
                    field.op = TArrow;
                    field.text = name;
                    field.kids.push_back(std::move(self));
                    *out = std::move(field);
                    if (od.ref_caps.count(name) > 0) {
                        Node inner = std::move(*out);
                        *out = Node{};
                        out->kind = Node::Unary;
                        out->op = TStar;
                        out->kids.push_back(std::move(inner));
                    }
                    return parse_postfix(out);
                }
                if (is_method && is(TLParen)) {
                    if (!any_object_fn) return qualified_call(out, cls, name, {}) && parse_postfix(out);
                    next();
                    return method_call(out, std::move(self), true, name) && parse_postfix(out);
                }
                if (od.lam_outer < 0) break;
                Node up;
                up.kind = Node::Field;
                up.op = TArrow;
                up.text = "__this";
                up.kids.push_back(std::move(self));
                self = std::move(up);
                cls = od.lam_outer;
            }
        }
        auto enclosing = [this](int c) {
            const StructDef& d = structs[static_cast<size_t>(c)];
            return d.parent >= 0 ? d.parent : d.local_in;
        };
        if (cur_class >= 0 && !local_visible(name) && colons == std::string::npos && enclosing(cur_class) >= 0 &&
            find_field_named(structs[static_cast<size_t>(cur_class)], name) < 0) {
            // In a member function of a nested class: a static member of an enclosing class is found by its plain name
            // ([class.nest] 1, [basic.lookup.unqual] 8), after the nested class's own members.
            bool own = false;
            for (const Method& m : structs[static_cast<size_t>(cur_class)].methods)
                if (m.name == name) own = true;
            int tmpl_owner = -1;
            if (find_member_tmpl(cur_class, name, &tmpl_owner) != nullptr) own = true;
            if (!own && structs[static_cast<size_t>(cur_class)].statics.count(name) == 0)
                for (int outer = enclosing(cur_class); outer >= 0; outer = enclosing(outer)) {
                    bool static_fn = false;
                    for (const Method& m : structs[static_cast<size_t>(outer)].methods)
                        if (m.name == name && m.is_static) static_fn = true;
                    if (is(TLParen) && static_fn) return qualified_call(out, outer, name, {}) && parse_postfix(out);
                    if (!is(TLParen) && !is(TLt) && structs[static_cast<size_t>(outer)].statics.count(name) > 0 &&
                        static_member_expr(outer, name, out))
                        return parse_postfix(out);
                }
        }
        if (cur_class >= 0 && !local_visible(name) && colons == std::string::npos) {
            const StructDef& def = structs[static_cast<size_t>(cur_class)];
            if ((!is(TLParen) || static_object_call(cur_class, name)) && static_member_expr(cur_class, name, out))
                return parse_postfix(out);
            bool is_field = false;
            for (const Field& field : def.fields)
                if (field.name == name) is_field = true;
            if (!is_field && !is(TLParen) && !is(TLt) && static_method_ref(cur_class, name, out)) return parse_postfix(out);
            Node self;
            self.kind = Node::Name;
            self.text = "this";
            if (is_field) {
                out->kind = Node::Field;
                out->op = TArrow;
                out->text = name;
                out->kids.push_back(std::move(self));
                if (def.ref_caps.count(name) > 0) {
                    // A variable a lambda captured by reference: the field holds its address.
                    Node field = std::move(*out);
                    *out = Node{};
                    out->kind = Node::Unary;
                    out->op = TStar;
                    out->kids.push_back(std::move(field));
                }
                return parse_postfix(out);
            }
            if (is(TLt) && !is_field) {
                // Get<T>() in a member function: a member function template of the class called on this with explicit
                // template arguments ([temp.names], [class.mfct.non.static] 2), as this->Get<T>() is.
                int tmpl_owner = -1;
                const MemberTmpl* mt = find_member_tmpl(cur_class, name, &tmpl_owner);
                const bool own_name = name == def.name || (!def.tmpl.empty() && name == def.tmpl);
                if (mt != nullptr && !own_name) {
                    const size_t saved = at;
                    std::vector<TArg> targs;
                    if (!parse_template_args(mt->td, &targs, true)) return false;
                    if (is(TLParen)) {
                        if (mt->is_static || !local_visible("this"))
                            return qualified_call(out, cur_class, name, std::move(targs)) && parse_postfix(out);
                        next();
                        return method_call(out, std::move(self), true, name, std::move(targs)) && parse_postfix(out);
                    }
                    at = saved;
                }
            }
            if (is(TLParen) && !def.methods.empty()) {
                bool is_method = false;
                for (const Method& m : def.methods)
                    if (m.name == name) is_method = true;
                int tmpl_owner = -1;
                // A constructor template (template <class It> string(It, It)) is not a member function: string(p, n)
                // inside the class is a new object ([class.ctor] 2, [expr.type.conv]).
                const bool own_name = name == def.name || (!def.tmpl.empty() && name == def.tmpl);
                if (!own_name && find_member_tmpl(cur_class, name, &tmpl_owner) != nullptr) is_method = true;
                bool any_object_fn = false;
                for (const Method& m : def.methods)
                    if (m.name == name && !m.is_static) any_object_fn = true;
                // In a static member function there is no this: a member template named there is a static one
                // (Log::Error calls Emit), called like the static functions.
                const bool no_this = !local_visible("this");
                if (is_method && !any_object_fn && (no_this || find_member_tmpl(cur_class, name, &tmpl_owner) == nullptr)) {
                    // Only static functions have this name: there is no object to call it on.
                    return qualified_call(out, cur_class, name, {}) && parse_postfix(out);
                }
                if (is_method) {
                    next();
                    return method_call(out, std::move(self), true, name) && parse_postfix(out);
                }
            }
        }
        if (is(TLBrace) && colons == std::string::npos && (ns_class || !local_visible(name)) && cur_locals != nullptr) {
            // C{args}: a temporary object of a class made from a braced list ([expr.type.conv], [dcl.init.list]):
            // by a constructor, or member by member for an aggregate.
            int bsid = -1;
            auto btd = typedefs.find(name);
            if (btd != typedefs.end()) {
                if (btd->second.base == 3 && btd->second.pointers == 0 && btd->second.struct_id >= 0)
                    bsid = btd->second.struct_id;
            } else if (class_name_text(name) || find_struct(name) >= 0) {
                bsid = find_struct(name);
            }
            bool named_function = false;
            for (const FuncDecl& known : funcs)
                if (known.name == name) named_function = true;
            if (bsid >= 0 && !named_function) {
                if (!braced_temp(bsid, out)) return false;
                return parse_postfix(out);
            }
            if (btd != typedefs.end() && !named_function && btd->second.base != 3 && !btd->second.is_array()) {
                // T{} and T{v} for a scalar or enumeration type: zero, or v converted ([expr.type.conv] 2, [dcl.init.list]).
                const Type st = btd->second;
                next();
                Node value;
                if (accept(TRBrace)) {
                    value.kind = Node::IntLit;
                    value.number = 0;
                    value.type.base = 1;
                } else {
                    if (!parse_expr(1, &value)) return false;
                    if (!expect(TRBrace, "'}'")) return false;
                }
                out->kind = Node::Unary;
                out->type = st;
                out->kids.push_back(std::move(value));
                return parse_postfix(out);
            }
        }
        if (is(TLParen) && colons == std::string::npos && (ns_class || !local_visible(name)) && cur_locals != nullptr) {
            // C(args): a temporary object of a class built by a constructor ([expr.type.conv]). It is
            // made before the statement that uses it and destroyed after it.
            int csid = -1;
            auto td = typedefs.find(name);
            if (td != typedefs.end()) {
                if (td->second.base == 3 && td->second.pointers == 0 && td->second.struct_id >= 0)
                    csid = td->second.struct_id;
            } else if (class_name_text(name) || find_struct(name) >= 0) {
                csid = find_struct(name);
            }
            bool is_function = false;
            for (const FuncDecl& known : funcs)
                if (known.name == name) is_function = true;
            if (csid >= 0 && !is_function) {
                next();
                std::vector<Node> cargs;
                if (!parse_call_args(&cargs)) return false;
                Type ct;
                ct.base = 3;
                ct.struct_id = csid;
                Local hidden;
                hidden.name = "__bt" + std::to_string(tmp_count++);
                hidden.type = ct;
                place_local(cur_locals, hidden);
                if (!class_needs_ctor(structs[static_cast<size_t>(csid)]) && cargs.size() <= 1) {
                    // P() and P(other) for a class without constructors: zero, or a copy ([dcl.init]).
                    if (cargs.empty()) {
                        if (!lower_aggr(ct, node_name(hidden.name), nullptr, &stmt_pre)) return false;
                    } else {
                        Node copy;
                        copy.kind = Node::Assign;
                        copy.op = TAssign;
                        copy.number = 1;
                        copy.kids.push_back(node_name(hidden.name));
                        copy.kids.push_back(std::move(cargs[0]));
                        Node copy_stmt;
                        copy_stmt.kind = Node::ExprStmt;
                        copy_stmt.kids.push_back(std::move(copy));
                        stmt_pre.push_back(std::move(copy_stmt));
                    }
                    *out = node_name(hidden.name);
                    return parse_postfix(out);
                }
                Node ctor_stmt;
                if (!special_call(csid, "<ctor>", node_addr(node_name(hidden.name)), std::move(cargs), &ctor_stmt))
                    return false;
                stmt_pre.push_back(std::move(ctor_stmt));
                if (class_needs_dtor(structs[static_cast<size_t>(csid)])) stmt_temps.push_back(Temp{hidden.name, csid});
                *out = node_name(hidden.name);
                return parse_postfix(out);
            }
        }
        const bool plain_beats_template = is(TLParen) && func_templates.count(name) > 0 && ovl.count(name) > 0 &&
                                          !func_template_takes(name, call_arg_count_at(at));  // see std::remove above
        if (func_templates.count(name) > 0 && (is(TLt) || is(TLParen)) && !local_visible(name) && !plain_beats_template) {
            std::vector<TArg> targs;
            if (is(TLt) && !parse_func_template_args(name, &targs)) return false;
            if (!expect(TLParen, "'('")) return false;
            std::vector<Node> cargs;
            if (!parse_call_args(&cargs)) return false;
            std::string inst;
            if (!instantiate_function(name, std::move(targs), cargs, &inst)) return false;
            out->kind = Node::Call;
            out->text = inst;
            out->kids = std::move(cargs);
            for (const FuncDecl& known : funcs) {
                if (known.name != inst) continue;
                if (!pad_defaults(known.defaults, known.params.size(), &out->kids)) return false;
                std::vector<Type> source = known.params;
                for (size_t i = 0; i < source.size() && i < known.param_ref.size(); ++i)
                    if (known.param_ref[i]) {
                        source[i].pointers -= 1;
                        source[i].is_ref = true;
                    }
                if (!adjust_args(source, &out->kids)) return false;
                if (known.ret_ref) *out = reference_result(std::move(*out), true, known.ret.rref);
                break;
            }
            return parse_postfix(out);
        }
        bool var_object = false;
        if (is(TLParen) && colons == std::string::npos) {
            // obj(args) where obj is a variable of a class: its operator() (read in parse_postfix).
            Type vt;
            Node probe;
            probe.kind = Node::Name;
            probe.text = name;
            var_object = static_type(probe, &vt) && vt.base == 3 && vt.pointers == 0 && !vt.is_array() && vt.struct_id >= 0 &&
                         (local_visible(name) || [&]() {
                             for (const FuncDecl& fd : funcs)
                                 if (fd.name == name) return false;
                             return true;
                         }());
        }
        if (name == "va_arg" && accept(TLParen)) {
            Node ap;
            if (!parse_expr(1, &ap)) return false;
            if (!expect(TComma, "','")) return false;
            Type ty;
            bool imp = false;
            bool exp = false;
            if (!parse_type(&ty, &imp, &exp)) return false;
            while (accept(TStar)) ++ty.pointers;
            if (!expect(TRParen, "')'")) return false;
            out->kind = Node::Call;
            out->text = "va_arg";
            out->type = ty;
            out->kids.push_back(std::move(ap));
        } else if (is(TLParen) && !local_visible(name) && functional_cast_type(name).base >= 0) {
            // T(x) for a type name T of a scalar type is the cast (T)(x) ([expr.type.conv]).
            Type ty = functional_cast_type(name);
            next();
            Node inner;
            // The list may be a pack expansion, T(a...) with T bound to a scalar ([temp.variadic]): it must give one
            // value, or none.
            std::vector<Node> list;
            if (!parse_call_args(&list)) return false;
            if (list.empty()) {
                // T(): a value-initialised scalar is zero ([expr.type.conv]).
                inner.kind = Node::IntLit;
                inner.type.base = 1;
            } else if (list.size() == 1) {
                inner = std::move(list[0]);
            } else {
                error = "a cast to " + name + " takes one value";
                return false;
            }
            if (!convert_class(&inner, ty)) return false;
            if (inner.kind == Node::IntLit && ty.pointers == 0 && ty.base == 1 && !ty.is_bool) {
                inner.number = const_norm(inner.number, ty);
                inner.type = Type{};
                inner.type.base = 1;
                inner.type.wide = ty.wide;
                inner.type.is_unsigned = ty.is_unsigned;
                *out = std::move(inner);
                return parse_postfix(out);
            }
            out->kind = Node::Unary;
            out->type = ty;
            out->kids.push_back(std::move(inner));
        } else if (!var_object && accept(TLParen)) {
            out->kind = Node::Call;
            out->text = name;
            if (!parse_call_args(&out->kids)) return false;
            if (local_visible(name)) {
                Type fpt;
                if (static_type(node_name(name), &fpt)) {
                    if (!adjust_fp_args(fpt, &out->kids, 0)) return false;
                    fp_reference_result(fpt, out);
                }
            }
            if (!local_visible(name)) {
                const std::string picked = pick_free(name, out->kids);
                if (!picked.empty()) {
                    name = picked;
                    out->text = picked;
                }
            }
            if (!pad_func_defaults(name, &out->kids)) return false;
            for (const FuncDecl& known : funcs) {
                if (known.name != name) continue;
                std::vector<Type> source = known.params;
                for (size_t i = 0; i < source.size() && i < known.param_ref.size(); ++i)
                    if (known.param_ref[i]) {
                        source[i].pointers -= 1;
                        source[i].is_ref = true;
                    }
                if (!adjust_args(source, &out->kids)) return false;
                if (known.ret_ref) *out = reference_result(std::move(*out), true, known.ret.rref);
                break;
            }
        } else if (enum_values.find(name) != enum_values.end()) {
            out->kind = Node::IntLit;
            out->number = enum_values[name];
            out->type.base = 1;
            out->type.enum_id = enum_id_of(name);
            {
                // A template value parameter of a 64-bit type (mt19937_64's a = 0xb5026f5aa96619e9): a value that does
                // not fit in an int keeps all 64 bits.
                const long long v = static_cast<long long>(out->number);
                if (v < -2147483647LL - 1 || v > 2147483647LL) out->type.wide = true;
            }
        } else if (class_enumerator(name, &out->number)) {
            // In::P1: an enumerator of an unscoped enumeration declared in class In ([class.member.lookup]); enumerators are
            // kept in one table here.
            out->kind = Node::IntLit;
            out->type.base = 1;
            out->type.enum_id = enum_id_of(name.substr(name.rfind("::") + 2));
        } else if (!no_const_subst && !local_visible(name) && !is_assign_op(cur().kind) && !is(TPlusPlus) &&
                   !is(TMinusMinus) && global_const_node(name, out)) {
            // A const global with a constant initializer is a constant ([expr.const]).
        } else if (!no_const_subst && local_const_node(name, out)) {
            // A const local integer with a constant initializer is a constant too (`constexpr int kMax = 8; T a[kMax];`).
        } else {
            out->kind = Node::Name;
            out->text = name;
            out->vis = local_seq + 1;
            if (!local_visible(name) && !names_function(name)) {
                // std::hex used as a value: the namespace qualifier was dropped (namespaces are flat for names that
                // are not called), so the function is found by its last name part when exactly one has it.
                bool is_global = false;
                for (const Global& g : globals)
                    if (g.name == name) is_global = true;
                if (!is_global) {
                    std::string only;
                    int count = 0;
                    const std::string tail = "::" + name;
                    for (const FuncDecl& f : funcs)
                        if (f.name.size() > tail.size() && f.name.compare(f.name.size() - tail.size(), tail.size(), tail) == 0 &&
                            f.name != only) {
                            only = f.name;
                            ++count;
                        }
                    if (count == 1) out->text = only;
                }
            }
        }
    } else if (is(TLParen)) {
        const Tok ahead = at + 1 < tokens.size() ? tokens[at + 1].kind : TEnd;
        const bool type_ahead = ahead == TInt || ahead == TConst || ahead == TChar || ahead == TVoid || ahead == TShort || ahead == TUnsigned ||
                                ahead == TSigned || ahead == TLong || ahead == TFloat || ahead == TDouble || ahead == TStruct ||
                                ahead == TUnion || ahead == TEnum ||
                                (ahead == TIdent && typedefs.find(tokens[at + 1].text) != typedefs.end() && !qualified_value_at(at + 1) &&
                                 (tokens[at + 1].nsq || (!local_visible(tokens[at + 1].text) && !member_hides(tokens[at + 1].text, at + 2 < tokens.size() ? tokens[at + 2].kind : TEnd)))) ||  // a parameter `seconds` hides chrono's type
                                (ahead == TIdent && class_name_at(at + 1) && !qualified_value_at(at + 1) &&
                                 (tokens[at + 1].nsq || (!local_visible(tokens[at + 1].text) && !member_hides(tokens[at + 1].text, at + 2 < tokens.size() ? tokens[at + 2].kind : TEnd)))) ||
                                // (std::vector<int>*)p, and static_cast<function<void()>*>(p), which is read as that
                                (ahead == TIdent && is_template_name_at(at + 1) && !local_visible(tokens[at + 1].text) &&
                                 !member_hides(tokens[at + 1].text, at + 2 < tokens.size() ? tokens[at + 2].kind : TEnd) && template_id_cast_at(at + 1)) ||
                                // (decltype(x)*)p, and static_cast<decltype(f)*>(p) ([dcl.type.decltype])
                                (ahead == TIdent && tokens[at + 1].text == "decltype" && at + 2 < tokens.size() &&
                                 tokens[at + 2].kind == TLParen);
        // (double(k) * 2) and (T(x) + 1): a type written like a call inside parentheses is a functional cast in an
        // expression, not a C cast ([expr.type.conv]). A function pointer type is (int (*)(int)), with '*' after '('.
        bool functional = false;
        if (type_ahead) {
            size_t k = at + 1;
            if (tokens[k].kind == TIdent && tokens[k].text == "decltype") {
                int depth = 0;  // past decltype( ... ): (decltype(x)(y) + 1) is a functional cast
                for (++k; k < tokens.size() && tokens[k].kind != TEnd; ++k) {
                    if (tokens[k].kind == TLParen) ++depth;
                    else if (tokens[k].kind == TRParen && --depth == 0) {
                        ++k;
                        break;
                    }
                }
            } else if (tokens[k].kind == TIdent && is_template_name_at(k)) {
                int angle = 0;  // past the template's argument list: (Box<int>(x) + 1) is a functional cast
                for (++k; k < tokens.size() && tokens[k].kind != TEnd; ++k) {
                    if (tokens[k].kind == TLt) ++angle;
                    else if (tokens[k].kind == TGt && --angle == 0) {
                        ++k;
                        break;
                    }
                }
            } else if (tokens[k].kind == TIdent) {
                ++k;
            } else {
                k = after_type_keywords(k);
            }
            functional = k > at + 1 && k + 1 < tokens.size() && tokens[k].kind == TLParen && tokens[k + 1].kind != TStar;
            // (Vec3{0, 0, 0} - v): a braced temporary ([expr.type.conv] 2) in parentheses, not a cast (Round 30).
            if (k > at + 1 && k < tokens.size() && tokens[k].kind == TLBrace) functional = true;
        }
        if (type_ahead && !functional) {
            const bool reinterp = cur().text == "(reinterpret";
            next();
            Type ty;
            bool imp = false;
            bool exp = false;
            if (!parse_type(&ty, &imp, &exp)) return false;
            while (accept(TStar)) ++ty.pointers;
            if (is(TAmp) || is(TAndAnd)) {
                // (T&)x, (const T&)x, (T&&)x, and static_cast<const Base&>(derived), which is read as that: the object x
                // seen as a T ([expr.static.cast] 2, [expr.cast]), here *(T*)&x. When x already has type T (and is not
                // an lvalue, such as a call result) it is x itself.
                next();
                if (!expect(TRParen, "')'")) return false;
                Node inner;
                if (!parse_unary(&inner)) return false;
                Type from;
                const bool known = static_type(inner, &from);
                if (known && from.base == ty.base && from.pointers == ty.pointers && from.struct_id == ty.struct_id &&
                    !(inner.kind == Node::Name || inner.kind == Node::Field || inner.kind == Node::Index ||
                      (inner.kind == Node::Unary && inner.op == TStar))) {
                    *out = std::move(inner);
                    return parse_postfix(out);
                }
                Type pt = ty;
                pt.is_ref = false;
                pt.rref = false;
                pt.is_const = false;
                pt.pointers += 1;
                Node addr = node_addr(std::move(inner));
                if (!reinterp) class_ptr_convert(&addr, pt, true);  // a base subobject, or the derived object around it
                Node cast;
                cast.kind = Node::Unary;
                cast.type = pt;
                cast.kids.push_back(std::move(addr));
                *out = Node{};
                out->kind = Node::Unary;
                out->op = TStar;
                out->kids.push_back(std::move(cast));
                return parse_postfix(out);
            }
            if (!expect(TRParen, "')'")) return false;
            Node inner;
            if (!parse_unary(&inner)) return false;
            if (ty.base != 3 && ty.base != 0 && !ty.is_array() && !convert_class(&inner, ty)) return false;
            if (inner.kind == Node::IntLit && ty.pointers == 0 && !ty.is_array() && !ty.is_ref && ty.base >= 5 &&
                ty.base <= 6) {
                // (float)3 and (double)1.5f are constants: the literal is converted at compile time.
                uint64_t bits = 0;
                if (const_convert(inner, ty, &bits)) {
                    out->kind = Node::IntLit;
                    out->number = bits;
                    out->type = Type{};
                    out->type.base = ty.base;
                    return parse_postfix(out);
                }
            }
            if (inner.kind == Node::IntLit && inner.type.base >= 5 && ty.pointers == 0 && !ty.is_array() && !ty.is_ref &&
                (ty.base == 1 || ty.base == 2 || ty.base == 4) && !ty.is_bool) {
                // (int)2.5f is a constant: truncated toward zero.
                uint64_t bits = 0;
                if (const_convert(inner, ty, &bits)) {
                    Type nt = Type{};
                    nt.base = 1;
                    if (ty.base == 1) {
                        nt.wide = ty.wide;
                        nt.is_unsigned = ty.is_unsigned;
                    }
                    uint64_t v = bits;
                    if (ty.base == 2) v = ty.is_unsigned ? (v & 0xFFu) : static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(v)));
                    else if (ty.base == 4) v = ty.is_unsigned ? (v & 0xFFFFu) : static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(v)));
                    else v = const_norm(v, nt);
                    out->kind = Node::IntLit;
                    out->number = v;
                    out->type = nt;
                    return parse_postfix(out);
                }
            }
            if (inner.kind == Node::IntLit && inner.type.base < 5 && ty.pointers == 0 && !ty.is_array() && !ty.is_ref &&
                (ty.base == 1 || ty.base == 2 || ty.base == 4) && ty.bits < 0) {
                // A cast of a constant to an integer type is a constant ([expr.const]): the value is
                // cut to the width of the type and, for a signed type, extended again.
                uint64_t v = inner.number;
                if (ty.is_bool) v = v != 0 ? 1 : 0;
                else if (ty.base == 2) v = ty.is_unsigned ? (v & 0xFFu) : static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(v)));
                else if (ty.base == 4) v = ty.is_unsigned ? (v & 0xFFFFu) : static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(v)));
                else if (!ty.wide) v = ty.is_unsigned ? (v & 0xFFFFFFFFu) : static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(v)));
                out->kind = Node::IntLit;
                out->number = v;
                out->type = Type{};
                out->type.base = 1;
                if (ty.base == 1) {
                    out->type.wide = ty.wide;
                    out->type.is_unsigned = ty.is_unsigned;
                }
                return parse_postfix(out);
            }
            // (Base*)p and static_cast<Derived*>(b) with several bases: the address moves to the subobject or back.
            if (!reinterp) class_ptr_convert(&inner, ty, true);
            out->kind = Node::Unary;
            out->type = ty;
            out->kids.push_back(std::move(inner));
        } else {
            bool folded = false;
            if (!try_fold(out, &folded)) return false;
            if (!folded) {
                next();
                if (!parse_expr(0, out)) return false;
                if (!expect(TRParen, "')'")) return false;
            }
        }
    } else if (is(TPlus)) {
        // +x ([expr.unary.op] 7): the value of x, promoted (a char, short or bool becomes an int); an operator+ of a class
        // with one operand is called.
        next();
        Node inner;
        if (!parse_unary(&inner)) return false;
        bool done = false;
        if (!operator_unary(TPlus, false, &inner, &done)) return false;
        Type it;
        if (!done && static_type(inner, &it) && it.pointers == 0 && !it.is_array() && !it.is_ref &&
            (it.base == 2 || it.base == 4)) {
            Type promoted;
            promoted.base = 1;
            inner = cast_node(promoted, std::move(inner));
        }
        *out = std::move(inner);
        return parse_postfix(out);
    } else if (is(TAmp) && member_pointer_ahead()) {
        return parse_member_pointer(out) && parse_postfix(out);
    } else if (is(TStar) || is(TAmp) || is(TMinus) || is(TNot) || is(TTilde)) {
        out->kind = Node::Unary;
        out->op = cur().kind;
        next();
        Node inner;
        const bool saved_subst = no_const_subst;
        if (out->op == TAmp) no_const_subst = true;
        const bool inner_ok = parse_unary(&inner);
        no_const_subst = saved_subst;
        if (!inner_ok) return false;
        const Tok uop = out->op;
        if (uop == TAmp && ((inner.kind == Node::Name && inner.kids.empty() && names_function(inner.text)) ||
                            (inner.kind == Node::Unary && inner.op == TEnd && inner.text == "<static-fn>"))) {
            // &f of a function ([expr.unary.op] 3): the function's address, which the name alone already is ([conv.func]).
            *out = std::move(inner);
            return parse_postfix(out);
        }
        if (uop == TMinus || uop == TNot || uop == TTilde || uop == TStar) {
            bool done = false;
            if (!operator_unary(uop, false, &inner, &done)) return false;
            if (done) {
                *out = std::move(inner);
                return parse_postfix(out);
            }
        }
        if ((uop == TMinus || uop == TTilde || uop == TNot) && inner.kind == Node::IntLit && inner.type.base < 5) {
            // A constant operand is folded, so -16, ~0 and !1 can be used where a constant is needed.
            const uint64_t v = inner.number;
            out->kind = Node::IntLit;
            out->op = TEnd;
            out->type = Type{};
            out->type.base = 1;
            if (uop == TNot) out->number = v == 0 ? 1 : 0;
            else {
                out->type.wide = inner.type.wide;
                out->type.is_unsigned = inner.type.is_unsigned;
                out->number = const_norm(uop == TMinus ? 0 - v : ~v, out->type);
            }
            return parse_postfix(out);
        }
        if ((uop == TMinus || uop == TNot) && inner.kind == Node::IntLit && inner.type.base >= 5) {
            // -1.5f and !0.0 are constants too: the sign bit flips, or the value is compared with zero.
            const int fbase = inner.type.base;
            const uint64_t v = inner.number;
            out->kind = Node::IntLit;
            out->op = TEnd;
            out->type = Type{};
            if (uop == TMinus) {
                out->type.base = fbase;
                out->number = fbase == 5 ? ((v ^ 0x80000000ull) & 0xFFFFFFFFull) : (v ^ 0x8000000000000000ull);
            } else {
                out->type.base = 1;
                out->number = lit_double(inner) == 0 ? 1 : 0;
            }
            return parse_postfix(out);
        }
        out->kids.push_back(std::move(inner));
    } else if ((is(TInt) || is(TFloat) || is(TDouble) || is(TChar) || is(TShort) || is(TUnsigned) || is(TSigned) ||
                is(TLong)) &&
               [&]() {
                   // int(x), unsigned(x), float(x): a type name of keywords written like a call.
                   const size_t k = after_type_keywords(at);
                   return k < tokens.size() && tokens[k].kind == TLParen;
               }()) {
        Type ty;
        bool imp = false;
        bool exp = false;
        if (!parse_type(&ty, &imp, &exp)) return false;
        if (!expect(TLParen, "'('")) return false;
        Node inner;
        if (!parse_expr(0, &inner)) return false;
        if (!expect(TRParen, "')'")) return false;
        if (!convert_class(&inner, ty)) return false;
        if (inner.kind == Node::IntLit && inner.type.base < 5 && ty.base == 1) {
            inner.number = const_norm(inner.number, ty);
            inner.type = Type{};
            inner.type.base = 1;
            inner.type.wide = ty.wide;
            inner.type.is_unsigned = ty.is_unsigned;
            *out = std::move(inner);
            return parse_postfix(out);
        }
        out->kind = Node::Unary;
        out->type = ty;
        out->kids.push_back(std::move(inner));
    } else if (is(TLBrack)) {
        if (!parse_lambda(out)) return false;
    } else {
        error = "expected an expression";
        return false;
    }
    return parse_postfix(out);
}

bool Parser::parse_postfix(Node* out) {
    while (is(TLBrack) || is(TDot) || is(TArrow) || is(TLParen) || is(TPlusPlus) || is(TMinusMinus)) {
        if ((is(TDot) || is(TArrow)) && at + 1 < tokens.size() && tokens[at + 1].kind == TStar) {
            // obj.*pm and p->*pm (Round 30). Their operand binds as a cast-expression ([expr.mptr.oper]); what follows
            // is left to the binary operators, so `(p->*m).f()` needs its parentheses, as in C++.
            const bool arrow = is(TArrow);
            next();
            next();
            Node result;
            if (!apply_member_pointer(std::move(*out), arrow, &result)) return false;
            *out = std::move(result);
            return true;
        }
        if (is(TPlusPlus) || is(TMinusMinus)) {
            {
                bool done = false;
                Node operand = std::move(*out);
                if (!operator_unary(cur().kind, true, &operand, &done)) return false;
                *out = std::move(operand);
                if (done) {
                    next();
                    continue;
                }
            }
            Node wrapped;
            wrapped.kind = Node::Unary;
            wrapped.op = cur().kind;
            wrapped.number = 1;
            next();
            wrapped.kids.push_back(std::move(*out));
            *out = std::move(wrapped);
            continue;
        }
        if (is(TLParen)) {
            next();
            {
                Node probe;
                Type ct;
                if (class_object_type(*out, &ct)) {
                    std::vector<Node> cargs;
                    if (!parse_call_args(&cargs)) return false;
                    bool done = false;
                    if (!operator_call(out, std::move(cargs), &done)) return false;
                    if (!done) {
                        error = "no operator() in the class";
                        return false;
                    }
                    continue;
                }
            }
            Node call;
            call.kind = Node::Call;
            call.kids.push_back(std::move(*out));
            {
                // A call through a function pointer: the arguments as for any call (a pack expands, fn(args...)).
                std::vector<Node> pargs;
                if (!parse_call_args(&pargs)) return false;
                for (Node& a : pargs) call.kids.push_back(std::move(a));
                Type fpt;
                if (static_type(call.kids[0], &fpt)) {
                    if (!adjust_fp_args(fpt, &call.kids, 1)) return false;
                    fp_reference_result(fpt, &call);
                }
            }
            *out = std::move(call);
            continue;
        }
        if (is(TLBrack)) {
            next();
            Node index;
            Type sub_t;
            if (is(TLBrace) && class_object_type(*out, &sub_t) && sub_t.struct_id >= 0) {
                // m[{a, b}] ([expr.sub] 1, [over.sub]): the list makes a temporary of the class operator[] takes.
                int key_sid = -1;
                for (const Method& m : structs[static_cast<size_t>(sub_t.struct_id)].methods)
                    if (m.name == "operator[]" && m.params.size() == 1 && m.params[0].base == 3 && m.params[0].pointers == 0 &&
                        m.params[0].struct_id >= 0)
                        key_sid = m.params[0].struct_id;
                if (key_sid < 0) {
                    error = "a braced list in [] needs an operator[] that takes a class";
                    return false;
                }
                if (!braced_temp(key_sid, &index)) return false;
            } else if (!parse_expr(0, &index)) {
                return false;
            }
            if (!expect(TRBrack, "']'")) return false;
            {
                bool done = false;
                Node probe;
                Type ct;
                if (class_object_type(*out, &ct)) {
                    if (!operator_index(out, std::move(index), &done)) return false;
                    continue;
                }
            }
            Node wrapped;
            wrapped.kind = Node::Index;
            wrapped.kids.push_back(std::move(*out));
            wrapped.kids.push_back(std::move(index));
            *out = std::move(wrapped);
        } else {
            const Tok op = cur().kind;
            next();
            if (op == TArrow) {
                // obj->member for a class object with operator->: its result (a pointer) is what -> then applies to.
                Type object_type;
                if (class_object_type(*out, &object_type) && find_method(object_type.struct_id, "operator->", 0) != nullptr) {
                    Node result;
                    std::vector<Node> no_args;
                    if (!method_call(&result, std::move(*out), false, "operator->", {}, &no_args)) return false;
                    *out = std::move(result);
                }
            }
            // s.template add<int>(1): 'template' only says that a '<' after the name opens arguments.
            if (is(TIdent) && cur().text == "template" && at + 1 < tokens.size() && tokens[at + 1].kind == TIdent) next();
            if (is(TTilde) && at + 1 < tokens.size() && tokens[at + 1].kind == TIdent) {
                // p->~T() and o.~T(): an explicit destructor call ([class.dtor]); for a type without a destructor
                // (a scalar, a plain struct) it does nothing ([expr.prim.id.dtor]).
                next();
                const std::string dtor_name = cur().text;
                next();
                if (!expect(TLParen, "'('") || !expect(TRParen, "')'")) return false;
                Type ot;
                Node call_expr;
                call_expr.kind = Node::IntLit;
                call_expr.type.base = 1;
                int dsid = -1;
                if (static_type(*out, &ot) && ot.base == 3 && ot.struct_id >= 0 && ot.pointers == (op == TArrow ? 1 : 0)) {
                    dsid = ot.struct_id;
                } else {
                    // The object's type is not known here (static_cast<H*>(p)->~H()): the name after ~ says the class.
                    auto dtd = typedefs.find(dtor_name);
                    if (dtd != typedefs.end()) {
                        if (dtd->second.base == 3 && dtd->second.pointers == 0) dsid = dtd->second.struct_id;
                    } else {
                        dsid = find_struct(dtor_name);
                    }
                }
                if (dsid >= 0 && class_needs_dtor(structs[static_cast<size_t>(dsid)])) {
                    Node self = op == TArrow ? std::move(*out) : node_addr(std::move(*out));
                    Node stmt;
                    if (!special_call(dsid, "<dtor>", std::move(self), {}, &stmt)) return false;
                    call_expr = std::move(stmt.kids[0]);
                }
                *out = std::move(call_expr);
                continue;
            }
            if (!is(TIdent)) {
                error = "expected a field name";
                return false;
            }
            Node wrapped;
            wrapped.kind = Node::Field;
            wrapped.op = op;
            wrapped.text = cur().text;
            next();
            if (!is(TLParen)) {
                Type object_type;
                if (static_type(*out, &object_type) && object_type.base == 3 && object_type.struct_id >= 0 &&
                    static_member_expr(object_type.struct_id, wrapped.text, &wrapped)) {
                    *out = std::move(wrapped);  // obj.count: the object is not evaluated
                    continue;
                }
            }
            std::vector<TArg> member_targs;
            if (is(TLt) || is(TLParen)) {
                Type object_type;
                if (static_type(*out, &object_type) && object_type.base == 3 && object_type.struct_id >= 0) {
                    const int sid = object_type.struct_id;
                    bool is_method = false;
                    for (const Method& m : structs[static_cast<size_t>(sid)].methods)
                        if (m.name == wrapped.text) is_method = true;
                    int tmpl_owner = -1;
                    const MemberTmpl* mt = find_member_tmpl(sid, wrapped.text, &tmpl_owner);
                    if (mt != nullptr) {
                        is_method = true;
                        if (is(TLt) && !parse_template_args(mt->td, &member_targs, true)) return false;
                    }
                    if (is_method && is(TLParen)) {
                        next();
                        Node object = std::move(*out);
                        if (!method_call(out, std::move(object), op == TArrow, wrapped.text, std::move(member_targs)))
                            return false;
                        continue;
                    }
                }
            }
            {
                // f().x: the object a call returns is not in memory yet; it goes into a hidden local first
                // ([expr.ref], [class.temporary]) and is destroyed after the statement.
                Type call_type;
                if (op == TDot && out->kind == Node::Call && cur_locals != nullptr && static_type(*out, &call_type) &&
                    call_type.base == 3 && call_type.pointers == 0 && !call_type.is_array() && call_type.struct_id >= 0) {
                    Local hidden;
                    hidden.name = "__bt" + std::to_string(tmp_count++);
                    hidden.type = call_type;
                    place_local(cur_locals, hidden);
                    Node assign;
                    assign.kind = Node::Assign;
                    assign.op = TAssign;
                    assign.number = 1;
                    assign.kids.push_back(node_name(hidden.name));
                    assign.kids.push_back(std::move(*out));
                    Node stmt;
                    stmt.kind = Node::ExprStmt;
                    stmt.kids.push_back(std::move(assign));
                    stmt_pre.push_back(std::move(stmt));
                    if (class_needs_dtor(structs[static_cast<size_t>(call_type.struct_id)]))
                        stmt_temps.push_back(Temp{hidden.name, call_type.struct_id});
                    *out = node_name(hidden.name);
                }
            }
            {
                // u.LowPart where LowPart is a member of an anonymous struct or union inside u: go through the hidden
                // field(s) that hold it ([class.union.anon]).
                Type holder;
                std::vector<std::string> path;
                if (static_type(*out, &holder) && holder.base == 3 && holder.struct_id >= 0 &&
                    find_field_named(structs[static_cast<size_t>(holder.struct_id)], wrapped.text) < 0 &&
                    anon_path(holder.struct_id, wrapped.text, &path)) {
                    Tok step_op = op;
                    for (const std::string& hidden : path) {
                        Node step;
                        step.kind = Node::Field;
                        step.op = step_op;
                        step.text = hidden;
                        step.kids.push_back(std::move(*out));
                        *out = std::move(step);
                        step_op = TDot;
                    }
                    wrapped.op = TDot;
                }
            }
            wrapped.kids.push_back(std::move(*out));
            *out = std::move(wrapped);
            {
                // o.r for a reference member: the field holds the address of what it refers to ([dcl.ref]).
                Type holder;
                if (static_type(out->kids[0], &holder) && holder.base == 3 && holder.struct_id >= 0 &&
                    structs[static_cast<size_t>(holder.struct_id)].ref_caps.count(out->text) > 0) {
                    Node field = std::move(*out);
                    *out = Node{};
                    out->kind = Node::Unary;
                    out->op = TStar;
                    out->kids.push_back(std::move(field));
                }
            }
        }
    }
    return true;
}

bool Parser::parse_expr(int min_prec, Node* out) {
    if (!parse_unary(out)) return false;
    while (true) {
        size_t op_width = 1;
        const Tok op = op_at(at, &op_width);
        const int p = prec(op);
        if (p < min_prec) break;
        for (size_t w = 0; w < op_width; ++w) next();
        if (op == TQuestion) {
            Node mid;
            Node els;
            if (!parse_expr(1, &mid)) return false;
            if (!expect(TColon, "':'")) return false;
            if (!parse_expr(p, &els)) return false;
            Node tern;
            tern.kind = Node::Ternary;
            if (!convert_class(out, bool_type())) return false;
            tern.kids.push_back(std::move(*out));
            {
                // c ? a : b with objects of one class where a branch is not an lvalue (a call result, a temporary):
                // a hidden local gets the chosen value, in a statement before this one ([expr.cond]).
                Type ta, tb;
                const auto lvalue = [](const Node& n) {
                    return n.kind == Node::Name || n.kind == Node::Field || n.kind == Node::Index ||
                           (n.kind == Node::Unary && n.op == TStar);
                };
                const bool typed = static_type(mid, &ta) && static_type(els, &tb);
                ta.is_ref = false;  // a reference names its object ([expr.type] 1)
                tb.is_ref = false;
                if (typed && ta.base == 3 && ta.pointers == 0 && !ta.is_array() && tb.base == 3 && tb.pointers == 0 &&
                    ta.struct_id >= 0 && ta.struct_id == tb.struct_id && lvalue(mid) && lvalue(els)) {
                    // c ? a : b with two objects of one class is an lvalue ([expr.cond] 4): *(c ? &a : &b).
                    tern.kids.push_back(node_addr(std::move(mid)));
                    tern.kids.push_back(node_addr(std::move(els)));
                    *out = Node{};
                    out->kind = Node::Unary;
                    out->op = TStar;
                    out->kids.push_back(std::move(tern));
                    continue;
                }
                const auto class_obj = [](const Type& t) {
                    return t.base == 3 && t.pointers == 0 && !t.is_array() && t.struct_id >= 0;
                };
                // c ? "text" : std::string(...): one operand of a class and one that is not; the other is converted to
                // the class ([expr.cond] 4.3, a converting constructor), so the result is an object of that class
                // (Round 30: LocalPlayerInput.hpp's `kb ? "keyboard" : "pad" + std::to_string(n)`).
                const bool mixed = cur_locals != nullptr && typed && class_obj(ta) != class_obj(tb) &&
                                   (class_obj(ta) ? tb : ta).memptr < 0 &&
                                   ((class_obj(ta) ? tb : ta).pointers > 0 || (class_obj(ta) ? tb : ta).is_array()) &&
                                   structs[static_cast<size_t>((class_obj(ta) ? ta : tb).struct_id)].name == "string";
                if (mixed && !class_obj(ta)) ta = tb;
                if (mixed || (cur_locals != nullptr && typed && ta.base == 3 &&
                    ta.pointers == 0 && !ta.is_array() && tb.base == 3 && tb.pointers == 0 &&
                    ta.struct_id >= 0 && ta.struct_id == tb.struct_id && !(lvalue(mid) && lvalue(els)))) {
                    Local hidden;
                    hidden.name = "__tn" + std::to_string(tmp_count++);
                    hidden.type = ta;
                    hidden.type.is_const = false;
                    place_local(cur_locals, hidden);
                    Node chosen;
                    chosen.kind = Node::If;
                    chosen.kids.push_back(std::move(tern.kids[0]));
                    for (Node* branch : {&mid, &els}) {
                        Type bt;
                        if (mixed && static_type(*branch, &bt) && !class_obj(bt)) {
                            // The operand that is not an object: the hidden object is made from it by a constructor.
                            Node ctor_stmt;
                            std::vector<Node> cargs;
                            cargs.push_back(std::move(*branch));
                            if (!special_call(ta.struct_id, "<ctor>", node_addr(node_name(hidden.name)), std::move(cargs),
                                              &ctor_stmt))
                                return false;
                            chosen.kids.push_back(std::move(ctor_stmt));
                            continue;
                        }
                        Node assign;
                        assign.kind = Node::Assign;
                        assign.op = TAssign;
                        assign.number = 1;
                        assign.kids.push_back(node_name(hidden.name));
                        assign.kids.push_back(std::move(*branch));
                        Node stmt;
                        stmt.kind = Node::ExprStmt;
                        stmt.kids.push_back(std::move(assign));
                        chosen.kids.push_back(std::move(stmt));
                    }
                    stmt_pre.push_back(std::move(chosen));
                    // A class with a destructor: the hidden object is destroyed after the full expression, like any
                    // temporary ([class.temporary]).
                    if (class_needs_dtor(structs[static_cast<size_t>(ta.struct_id)]))
                        stmt_temps.push_back(Temp{hidden.name, ta.struct_id});
                    *out = node_name(hidden.name);
                    continue;
                }
            }
            if (tern.kids[0].kind == Node::IntLit && tern.kids[0].type.base < 5 && mid.kind == Node::IntLit &&
                els.kind == Node::IntLit && mid.type.base < 5 && els.type.base < 5 && mid.type.pointers == 0 &&
                els.type.pointers == 0) {
                // c ? a : b with integer constants is a constant ([expr.cond] 7, [expr.const]): the chosen operand
                // in the common type of both (a static member such as `value = same ? 0 : 1 + next::value`).
                // Operands of one type keep it ('-' : '+' stays a char, [expr.cond] 7.1); otherwise the usual
                // arithmetic conversions.
                const bool same = mid.type.base == els.type.base && mid.type.wide == els.type.wide &&
                                  mid.type.is_unsigned == els.type.is_unsigned && mid.type.is_bool == els.type.is_bool &&
                                  mid.text == els.text;
                const Type common = const_common(mid.type, els.type);
                Node chosen = tern.kids[0].number != 0 ? std::move(mid) : std::move(els);
                if (!same) {
                    chosen.number = const_norm(chosen.number, common);
                    chosen.type = common;
                    chosen.text.clear();
                }
                *out = std::move(chosen);
                continue;
            }
            tern.kids.push_back(std::move(mid));
            tern.kids.push_back(std::move(els));
            *out = std::move(tern);
            continue;
        }
        Node right;
        const int next_min = is_assign_op(op) ? p : p + 1;
        Type assigned;
        if (op == TAssign && is(TLBrace) && cur_locals != nullptr && class_object_type(*out, &assigned)) {
            // x = {a, b}; assigns a temporary made from the list ([expr.ass]).
            if (!braced_temp(assigned.struct_id, &right)) return false;
        } else if (!parse_expr(next_min, &right)) {
            return false;
        }
        {
            bool handled = false;
            if (!operator_binary(op, out, &right, &handled)) return false;
            if (handled) continue;
        }
        {
            // p = derived; and p == derived with several bases (Round 29, [conv.ptr] 3, [expr.eq] 3): the pointer to
            // the derived class is converted to the base pointer first.
            Type lt, rt;
            if (op == TAssign && static_type(*out, &lt)) {
                lt.is_ref = false;
                class_ptr_convert(&right, lt);
            } else if ((op == TEq || op == TNe) && static_type(*out, &lt) && static_type(right, &rt)) {
                lt.is_ref = false;
                rt.is_ref = false;
                if (lt.pointers == 1 && rt.pointers == 1 && lt.base == 3 && rt.base == 3) {
                    class_ptr_convert(&right, lt);
                    class_ptr_convert(out, rt);
                }
            }
        }
        Node bin;
        const bool compound = is_compound_op(op);
        bin.kind = op == TAssign || compound ? Node::Assign : Node::Binary;
        bin.op = op;
        bin.kids.push_back(std::move(*out));
        bin.kids.push_back(std::move(right));
        if (bin.kind == Node::Binary && bin.kids[0].kind == Node::IntLit && bin.kids[1].kind == Node::IntLit &&
            (bin.kids[0].type.base >= 5 || bin.kids[1].type.base >= 5)) {
            Node folded_node;
            if (fold_fp_binary(op, bin.kids[0], bin.kids[1], &folded_node)) bin = std::move(folded_node);
        } else if (bin.kind == Node::Binary && bin.kids[0].kind == Node::IntLit && bin.kids[1].kind == Node::IntLit &&
            bin.kids[0].type.base < 5 && bin.kids[1].type.base < 5) {
            Node folded_node;
            std::string fold_error;
            if (fold_int_binary(op, bin.kids[0], bin.kids[1], &folded_node, &fold_error)) bin = std::move(folded_node);
            else if (!fold_error.empty()) {
                error = fold_error;
                return false;
            }
        }
        *out = std::move(bin);
    }
    return true;
}


// typeid(type) and typeid(expression) ([expr.typeid]): an lvalue of std::type_info (from <typeinfo>). For an object of a
// class with virtual functions (named, a member, an element or *p) the type is read at run time from the slot before
// its virtual table; otherwise it is the static type, with references, top-level const and the array of a name kept
// as written. Own model (see type_info_symbol).
bool Parser::parse_typeid(Node* out) {
    next();  // typeid
    next();  // (
    const int ti_sid = find_struct("type_info");
    if (ti_sid < 0) {
        error = "typeid needs #include <typeinfo>";
        return false;
    }
    Type ti_ptr;
    ti_ptr.base = 3;
    ti_ptr.struct_id = ti_sid;
    ti_ptr.pointers = 1;
    Type ty;
    Node inner;
    bool dynamic = false;
    const bool plain_struct = is(TIdent) && find_struct(cur().text) >= 0 && !local_visible(cur().text) &&
                              at + 1 < tokens.size() &&
                              (tokens[at + 1].kind == TRParen || tokens[at + 1].kind == TStar || tokens[at + 1].kind == TAmp);
    if ((is_decl_start() || plain_struct) && !is(TStatic)) {
        bool imp = false;
        bool exp = false;
        if (plain_struct) {
            ty = Type{};
            ty.base = 3;
            ty.struct_id = find_struct(cur().text);
            next();
        } else if (!parse_type(&ty, &imp, &exp)) {
            return false;
        }
        while (accept(TStar)) ++ty.pointers;
        if (accept(TAmp)) ty.is_ref = true;
        else if (accept(TAndAnd)) ty.is_ref = true;
        if (!parse_brackets(&ty)) return false;
    } else {
        if (!parse_expr(1, &inner)) return false;
        if (!static_type(inner, &ty)) {
            error = "typeid of an expression whose type is not known";
            return false;
        }
        const bool object = inner.kind == Node::Name || inner.kind == Node::Field || inner.kind == Node::Index ||
                            (inner.kind == Node::Unary && inner.op == TStar);
        dynamic = object && ty.base == 3 && ty.pointers == 0 && !ty.is_array() && ty.struct_id >= 0 &&
                  structs[static_cast<size_t>(ty.struct_id)].poly;
    }
    if (!expect(TRParen, "')'")) return false;
    Node ptr;
    if (dynamic) {
        // *((type_info **)obj.__vptr - 1)
        Node vptr;
        vptr.kind = Node::Field;
        vptr.op = TDot;
        vptr.text = "__vptr";
        vptr.kids.push_back(std::move(inner));
        Node as_pp;
        as_pp.kind = Node::Unary;
        as_pp.type = ti_ptr;
        as_pp.type.pointers = 2;
        as_pp.kids.push_back(std::move(vptr));
        Node one;
        one.kind = Node::IntLit;
        one.number = 1;
        one.type.base = 1;
        Node back;
        back.kind = Node::Binary;
        back.op = TMinus;
        back.kids.push_back(std::move(as_pp));
        back.kids.push_back(std::move(one));
        ptr.kind = Node::Unary;
        ptr.op = TStar;
        ptr.kids.push_back(std::move(back));
    } else {
        // [expr.typeid] 5: references and cv-qualifiers on the type are ignored.
        ty.is_ref = false;
        ty.rref = false;
        ty.is_const = false;
        ty.pconst = false;
        const bool cls = ty.base == 3 && ty.pointers == 0 && !ty.is_array() && ty.struct_id >= 0;
        const std::string sym = cls ? type_info_symbol(ty.struct_id, structs[static_cast<size_t>(ty.struct_id)].symname())
                                    : type_info_symbol(-1, cxx_type(ty, &structs));
        Node addr = node_addr(node_name(sym));
        ptr.kind = Node::Unary;
        ptr.type = ti_ptr;
        ptr.kids.push_back(std::move(addr));
    }
    *out = Node{};
    out->kind = Node::Unary;
    out->op = TStar;
    out->kids.push_back(std::move(ptr));
    return true;
}

// The type-trait intrinsics <type_traits> is built on, written from the trait definitions of ISO/IEC 14882 [meta.unary.cat],
// [meta.unary.prop] and [meta.rel] (the names follow the compiler intrinsics MSVC documents; the code is our own). Each is a
// constant 1 or 0 of type bool. __is_final is 0 (okay-ms drops `final`).
bool Parser::parse_type_trait(Node* out) {
    const std::string which = cur().text;
    next();
    next();
    std::vector<Type> types;
    while (!is(TRParen) && !is(TEnd)) {
        Type t;
        bool imp = false;
        bool exp = false;
        if (is(TIdent) && type_packs.count(cur().text) > 0 && at + 1 < tokens.size() && tokens[at + 1].kind == TEllipsis) {
            // Args...: the types of a template parameter pack, in order ([temp.variadic] 5).
            for (const Type& pt : type_packs[cur().text]) types.push_back(pt);
            next();
            next();
            if (!accept(TComma)) break;
            continue;
        }
        if (is(TIdent) && find_struct(cur().text) >= 0 && typedefs.count(cur().text) == 0) {
            t = Type{};
            t.base = 3;
            t.struct_id = find_struct(cur().text);
            next();
        } else if (!parse_type(&t, &imp, &exp)) {
            return false;
        }
        while (accept(TStar)) ++t.pointers;
        if (accept(TAmp) || accept(TAndAnd)) t.is_ref = true;
        if (!parse_brackets(&t)) return false;
        types.push_back(t);
        if (!accept(TComma)) break;
    }
    if (!expect(TRParen, "')'")) return false;
    auto is_cls = [&](const Type& t) {
        return t.base == 3 && t.pointers == 0 && !t.is_ref && !t.is_array() && t.struct_id >= 0;
    };
    bool v = false;
    if (types.empty()) {
        error = which + " needs a type";
        return false;
    }
    const Type& a = types[0];
    if (which == "__is_enum") v = a.is_enum() && a.pointers == 0 && !a.is_ref && !a.is_array();
    else if (which == "__is_class") v = is_cls(a) && !structs[static_cast<size_t>(a.struct_id)].is_union;
    else if (which == "__is_union") v = is_cls(a) && structs[static_cast<size_t>(a.struct_id)].is_union;
    else if (which == "__is_polymorphic") v = is_cls(a) && structs[static_cast<size_t>(a.struct_id)].poly;
    else if (which == "__is_abstract") {
        if (is_cls(a))
            for (const Method& m : structs[static_cast<size_t>(a.struct_id)].methods)
                if (m.pure) v = true;
    } else if (which == "__is_empty") {
        v = is_cls(a) && !structs[static_cast<size_t>(a.struct_id)].poly;
        if (v)
            for (const Field& f : structs[static_cast<size_t>(a.struct_id)].fields)
                if (f.name.compare(0, 6, "__vptr") != 0) v = false;
    } else if (which == "__is_final") {
        v = false;
    } else if (which == "__is_trivially_destructible") {
        v = !is_cls(a) || !class_needs_dtor(structs[static_cast<size_t>(a.struct_id)]);
    } else if (which == "__has_virtual_destructor") {
        if (is_cls(a))
            for (const Method& m : structs[static_cast<size_t>(a.struct_id)].methods)
                if (m.name == "<dtor>" && m.is_virtual) v = true;
    } else if (which == "__is_trivially_copyable") {
        v = !is_cls(a) || (!class_needs_dtor(structs[static_cast<size_t>(a.struct_id)]) &&
                           !structs[static_cast<size_t>(a.struct_id)].poly && copy_ctor(a.struct_id) == nullptr);
    } else if (which == "__is_constructible") {
        // T(args) for the argument types given; for a class: a constructor of that arity, or none written (an
        // aggregate or the implicit default constructor) when there are no arguments.
        if (!is_cls(a)) {
            v = types.size() <= 2;
        } else {
            const StructDef& d = structs[static_cast<size_t>(a.struct_id)];
            bool any_ctor = false;
            for (const Method& m : d.methods)
                if (m.name == "<ctor>") {
                    if (!m.implicit) any_ctor = true;
                    if (m.params.size() >= types.size() - 1 &&
                        required_args(m.defaults, m.params.size()) <= types.size() - 1)
                        v = true;
                }
            if (!any_ctor && types.size() == 1) v = true;
            if (has_ctor_template_taking(a.struct_id, types.size() - 1)) v = true;
        }
    } else if (which == "__is_base_of") {
        if (types.size() != 2) {
            error = "__is_base_of needs two types";
            return false;
        }
        const Type& b = types[1];
        v = is_cls(a) && is_cls(b) && (a.struct_id == b.struct_id || derives_from(b.struct_id, a.struct_id));
    } else if (which == "__is_convertible_to") {
        if (types.size() != 2) {
            error = "__is_convertible_to needs two types";
            return false;
        }
        const Type& b = types[1];
        if (is_cls(b)) v = (is_cls(a) && (a.struct_id == b.struct_id || derives_from(a.struct_id, b.struct_id))) ||
                           (!is_cls(a) && converts_by_ctor(b.struct_id, Node{}, a));
        else v = !is_cls(a) && a.pointers == b.pointers && (a.pointers == 0 || a.base == b.base || b.base == 0);
    } else if (which == "__is_invocable") {
        // INVOKE(f, args...) is well formed ([func.require], [meta.rel] is_invocable), for a callable that is a class
        // with operator() (a lambda's closure), a function or a pointer to one. Own simplification: a call operator fits
        // by its number of parameters (defaults counted) and each argument by a loose conversion (a class from the same
        // class, a derived class or a converting constructor; a scalar from a scalar; a pointer from a pointer of as
        // many levels); a call operator template fits by its number of parameters. Pointers to members are not
        // invocable here.
        Type f = a;
        f.is_ref = false;
        f.rref = false;
        const std::vector<Type> args(types.begin() + 1, types.end());
        auto arg_fits = [&](const Type& from_ref, const Type& to_ref) {
            Type from = from_ref;
            Type to = to_ref;
            from.is_ref = to.is_ref = false;
            if (is_cls(to))
                return (is_cls(from) && (from.struct_id == to.struct_id || derives_from(from.struct_id, to.struct_id))) ||
                       (!is_cls(from) && converts_by_ctor(to.struct_id, Node{}, from));
            if (is_cls(from)) return false;
            if (to.pointers > 0 || from.pointers > 0)
                return from.pointers == to.pointers || (from.pointers == 0 && from.base == 1);  // 0 / nullptr
            return true;
        };
        auto params_fit = [&](const std::vector<Type>& params, const std::vector<Node>* defaults, bool vararg) {
            const size_t need = defaults != nullptr ? required_args(*defaults, params.size()) : params.size();
            if (args.size() < need || (args.size() > params.size() && !vararg)) return false;
            for (size_t i = 0; i < args.size() && i < params.size(); ++i)
                if (!arg_fits(args[i], params[i])) return false;
            return true;
        };
        if (is_cls(f)) {
            for (int sid = f.struct_id; sid >= 0 && !v; sid = structs[static_cast<size_t>(sid)].base)
                for (const Method& m : structs[static_cast<size_t>(sid)].methods)
                    if (m.name == "operator()" && params_fit(m.params, &m.defaults, false)) v = true;
            int owner = -1;
            const MemberTmpl* mt = v ? nullptr : find_member_tmpl(f.struct_id, "operator()", &owner);
            if (mt != nullptr) {
                size_t fixed = 0;
                bool pack = false;
                for (const Type& pt : mt->td.proto) {
                    if (pt.pack) pack = true;
                    else ++fixed;
                }
                v = mt->td.proto.empty() || (pack ? args.size() >= fixed : args.size() == fixed);
            }
        } else if (f.is_func() && f.pointers <= 1 && static_cast<size_t>(f.func_id) < func_types.size()) {
            const FuncType& ft = func_types[static_cast<size_t>(f.func_id)];
            v = params_fit(ft.params, nullptr, ft.vararg);
        }
    } else {
        error = "unknown type trait " + which;
        return false;
    }
    *out = Node{};
    out->kind = Node::IntLit;
    out->number = v ? 1 : 0;
    out->type.base = 1;
    return parse_postfix(out);
}

}  // namespace compiler
}  // namespace okayms

namespace okayms {
namespace compiler {
// At '&': `&C::m` where m is a non-static data member of the class C (also Outer::Inner::m) ([expr.unary.op] 4).
bool Parser::member_pointer_ahead() const {
    size_t k = at + 1;
    if (k + 2 >= tokens.size() || tokens[k].kind != TIdent || tokens[k + 1].kind != TColonColon) return false;
    int sid = scope_class(tokens[k].text);
    if (sid < 0 && typedefs.count(tokens[k].text) == 0) sid = find_struct(tokens[k].text);  // a plain struct too
    if (sid < 0) return false;
    k += 2;
    while (k + 2 < tokens.size() && tokens[k].kind == TIdent && tokens[k + 1].kind == TColonColon) {
        int nested = -1;
        for (size_t n = 0; n < structs.size(); ++n)
            if (structs[n].name == tokens[k].text && structs[n].parent == sid) nested = static_cast<int>(n);
        if (nested < 0) return false;
        sid = nested;
        k += 2;
    }
    if (k + 1 >= tokens.size() || tokens[k].kind != TIdent) return false;
    if (tokens[k + 1].kind == TLParen || tokens[k + 1].kind == TLt || tokens[k + 1].kind == TColonColon) return false;
    const StructDef& d = structs[static_cast<size_t>(sid)];
    if (d.statics.count(tokens[k].text) > 0) return false;
    return find_field_named(d, tokens[k].text) >= 0;
}

// &C::m: a constant of type `T C::*`, the member's offset in C plus one (Type::memptr; Round 30).
bool Parser::parse_member_pointer(Node* out) {
    next();  // &
    int sid = scope_class(cur().text);
    if (sid < 0) sid = find_struct(cur().text);
    next();
    next();
    while (is(TIdent) && at + 1 < tokens.size() && tokens[at + 1].kind == TColonColon) {
        for (size_t n = 0; n < structs.size(); ++n)
            if (structs[n].name == cur().text && structs[n].parent == sid) sid = static_cast<int>(n);
        next();
        next();
    }
    const StructDef& d = structs[static_cast<size_t>(sid)];
    const int fi = find_field_named(d, cur().text);
    next();
    if (fi < 0) {
        error = "no data member for a pointer to member";
        return false;
    }
    const Field& f = d.fields[static_cast<size_t>(fi)];
    if (f.type.bits >= 0) {
        error = "a pointer to a bit-field member";
        return false;
    }
    Type cls;
    cls.base = 3;
    cls.struct_id = sid;
    Type member = f.type;
    Type mp;
    mp.base = 1;
    mp.memptr = static_cast<int>(memptr_types().size());
    memptr_types().emplace_back(cls, member);
    *out = Node{};
    out->kind = Node::IntLit;
    out->number = static_cast<uint64_t>(f.offset + 1);
    out->type = mp;
    return true;
}

// obj.*pm and ptr->*pm ([expr.mptr.oper]): the member at the offset pm holds, as an lvalue of its type:
// *(T*)((char*)&obj + pm - 1). A null pm is not checked (undefined behaviour in the standard).
bool Parser::apply_member_pointer(Node object, bool arrow, Node* out) {
    Node pm;
    if (!parse_unary(&pm)) return false;
    Type pt;
    if (!static_type(pm, &pt) || pt.memptr < 0 || pt.pointers != 0) {
        error = "the right side of .* or ->* is not a pointer to member";
        return false;
    }
    const std::pair<Type, Type> mp = memptr_types()[static_cast<size_t>(pt.memptr)];
    Type ot;
    if (!static_type(object, &ot)) {
        error = "cannot work out the class on the left of .* or ->*";
        return false;
    }
    Node addr = arrow ? std::move(object) : node_addr(std::move(object));
    if (ot.base == 3 && ot.struct_id >= 0 && mp.first.base == 3 && mp.first.struct_id >= 0 &&
        ot.struct_id != mp.first.struct_id) {
        // A member of a base class reached through a derived object: the base subobject first.
        const int off = base_offset(ot.struct_id, mp.first.struct_id);
        if (off > 0) addr = offset_ptr(std::move(addr), off, mp.first.struct_id);
    }
    Type bytes;
    bytes.base = 2;
    bytes.pointers = 1;
    Node sum;
    sum.kind = Node::Binary;
    sum.op = TPlus;
    sum.kids.push_back(cast_node(bytes, std::move(addr)));
    Node off;
    off.kind = Node::Binary;
    off.op = TMinus;
    Type wide;
    wide.base = 1;
    wide.wide = true;
    off.kids.push_back(cast_node(wide, std::move(pm)));
    off.kids.push_back(int_node(1));
    sum.kids.push_back(std::move(off));
    Type target = mp.second;
    target.is_ref = false;
    target.rref = false;
    target.pointers += 1;
    Node deref;
    deref.kind = Node::Unary;
    deref.op = TStar;
    deref.kids.push_back(cast_node(target, std::move(sum)));
    *out = std::move(deref);
    return true;
}
}  // namespace compiler
}  // namespace okayms
