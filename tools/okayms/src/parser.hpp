#pragma once
// The parser: declarations, types, classes, templates and functions. The statement, expression and call
// parts of the parser are in parse_stmt.cpp, parse_expr.cpp and parse_call.cpp.
// Moved out of compile.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "compile_internal.hpp"

namespace okayms {
namespace compiler {
struct Parser {
    std::vector<Token>& tokens;
    size_t at = 0;
    std::string error;
    std::vector<FuncDecl> funcs;
    std::vector<std::string> strings;
    std::vector<StructDef> structs;
    std::vector<Global> globals;
    std::vector<FuncType> func_types;
    std::map<std::string, Type> typedefs;
    std::map<std::string, uint64_t> enum_values;
    std::vector<Local>* cur_locals = nullptr;
    std::string current_fn;
    // The namespaces open at the top level: "a::b" and its decorated form "b@a" (innermost first).
    std::string current_ns;
    // The namespaces named by `using namespace`, in the order written.
    std::vector<std::string> using_namespaces{};
    std::string ns_mangled{};
    std::vector<std::string> ns_parts{};
    std::vector<size_t> ns_open{};  // how many names each open brace pushed (0 for an unnamed namespace)
    // extern "C" ([dcl.link]): a function declared under it keeps its plain name as the symbol, also inside a namespace.
    std::map<std::string, std::string> global_alias{};  // variable name -> the internal name of its latest declaration
    std::vector<bool> ns_is_c{};
    std::vector<bool> ns_is_anon{};
    int anon_depth = 0;
    int c_linkage = 0;
    bool c_single = false;
    std::set<std::string> namespaces{};
    void update_ns() {
        current_ns.clear();
        ns_mangled.clear();
        for (size_t i = 0; i < ns_parts.size(); ++i) current_ns += (i == 0 ? "" : "::") + ns_parts[i];
        for (size_t i = ns_parts.size(); i-- > 0;) ns_mangled += (ns_mangled.empty() ? "" : "@") + ns_parts[i];
    }
    // Spellings that only restate what the compiler already does are rewritten before parsing, each from
    // its rule in ISO/IEC 14882:
    //   constexpr -> const; inline, explicit, noexcept (with its operand), override, final, [[attributes]]
    //   are dropped; static_assert(...) is dropped (it is not checked); nullptr -> 0
    //   using Name = type; -> typedef type Name;  using namespace x; is dropped
    //   enum class E : T { } and enum E : T { } -> enum E { } (the underlying type is int)
    //   Scoped enumerators are renamed "E::X" (one token), so two enums may share an enumerator name
    //   static_cast<T>(x), reinterpret_cast and const_cast -> (T)(x)
    // `operator` and the operator symbol become one identifier token ("operator+", "operator[]", "operator()"),
    // so a declaration or a call of an operator function reads like any other function. A conversion function
    // `operator T()` is written the way a function with a return type is read: T moves in front and the name is
    // "operator conv" and T's spelling ([class.conv.fct], [over.oper]).
    void collapse_operators() {
        std::vector<Token> out;
        out.reserve(tokens.size());
        for (size_t i = 0; i < tokens.size(); ++i) {
            const Token& t = tokens[i];
            if (t.kind != TIdent || t.text != "operator" || i + 1 >= tokens.size()) {
                out.push_back(t);
                continue;
            }
            const Token& n = tokens[i + 1];
            std::string name;
            size_t used = 1;
            switch (n.kind) {
            case TPlus: name = "+"; break;
            case TMinus: name = "-"; break;
            case TStar: name = "*"; break;
            case TSlash: name = "/"; break;
            case TPercent: name = "%"; break;
            case TAmp: name = "&"; break;
            case TPipe: name = "|"; break;
            case TCaret: name = "^"; break;
            case TTilde: name = "~"; break;
            case TNot: name = "!"; break;
            case TAssign: name = "="; break;
            case TLt: name = "<"; break;
            case TLe: name = "<="; break;
            case TGe: name = ">="; break;
            case TEq: name = "=="; break;
            case TNe: name = "!="; break;
            case TPlusPlus: name = "++"; break;
            case TMinusMinus: name = "--"; break;
            case TPlusEq: name = "+="; break;
            case TMinusEq: name = "-="; break;
            case TStarEq: name = "*="; break;
            case TSlashEq: name = "/="; break;
            case TPercentEq: name = "%="; break;
            case TAndEq: name = "&="; break;
            case TOrEq: name = "|="; break;
            case TXorEq: name = "^="; break;
            case TShl: name = "<<"; break;
            case TShlEq: name = "<<="; break;
            case TAndAnd: name = "&&"; break;
            case TOrOr: name = "||"; break;
            case TArrow: name = "->"; break;
            case TComma: name = ","; break;
            case TGt:
                name = ">";
                if (i + 2 < tokens.size() && tokens[i + 2].adj && tokens[i + 2].kind == TGt) {
                    name = ">>";
                    used = 2;
                } else if (i + 2 < tokens.size() && tokens[i + 2].adj && tokens[i + 2].kind == TGe) {
                    name = ">>=";
                    used = 2;
                }
                break;
            case TLParen:
                if (i + 2 < tokens.size() && tokens[i + 2].kind == TRParen) {
                    name = "()";
                    used = 2;
                }
                break;
            case TLBrack:
                if (i + 2 < tokens.size() && tokens[i + 2].kind == TRBrack) {
                    name = "[]";
                    used = 2;
                }
                break;
            default: break;
            }
            if (!name.empty()) {
                Token op = t;
                op.text = "operator" + name;
                out.push_back(op);
                i += used;
                continue;
            }
            if (n.kind == TIdent && (n.text == "new" || n.text == "delete")) {
                out.push_back(t);
                continue;
            }
            // A conversion function: the type runs up to the '('.
            size_t j = i + 1;
            std::string key;
            while (j < tokens.size() && tokens[j].kind != TLParen && tokens[j].kind != TEnd) {
                key += std::to_string(static_cast<int>(tokens[j].kind)) + tokens[j].text + ",";
                ++j;
            }
            if (j >= tokens.size() || tokens[j].kind != TLParen || j == i + 1 ||
                (!out.empty() && out.back().kind == TColonColon)) {
                out.push_back(t);
                continue;
            }
            for (size_t k = i + 1; k < j; ++k) out.push_back(tokens[k]);
            Token op = t;
            op.text = "operator conv:" + key;
            out.push_back(op);
            i = j - 1;
        }
        tokens = std::move(out);
    }

    void lower_cxx_spellings() {
        const auto is_word = [this](size_t i, const char* w) {
            return i < tokens.size() && tokens[i].kind == TIdent && tokens[i].text == w;
        };
        std::set<std::string> scoped;
        for (size_t i = 0; i + 2 < tokens.size(); ++i)
            if (tokens[i].kind == TEnum && (tokens[i + 1].kind == TStruct) && tokens[i + 2].kind == TIdent)
                scoped.insert(tokens[i + 2].text);
        std::vector<Token> out;
        out.reserve(tokens.size());
        std::string in_scoped;  // the scoped enum whose braces are open
        int enum_depth = 0;
        bool want_name = false;
        for (size_t i = 0; i < tokens.size(); ++i) {
            const Token& t = tokens[i];
            if (t.kind == TIdent && t.text == "constexpr") {
                Token c = t;
                c.kind = TConst;
                c.text = "const";
                out.push_back(c);
                continue;
            }
            if (t.kind == TIdent && (t.text == "inline" || t.text == "explicit" || t.text == "override" || t.text == "mutable" ||
                                      t.text == "final" || t.text == "consteval" || t.text == "constinit" ||
                                      t.text == "volatile")) {
                if (t.text == "inline" && i + 1 < tokens.size()) tokens[i + 1].inl = true;
                continue;
            }
            if (t.kind == TConst && !out.empty() && out.back().kind == TStar) continue;  // int *const p
            if (t.kind == TIdent && t.text == "nullptr") {
                Token z = t;
                z.kind = TNumber;
                z.number = 0;
                out.push_back(z);
                continue;
            }
            if (is_word(i, "noexcept")) {
                if (i + 1 < tokens.size() && tokens[i + 1].kind == TLParen) {
                    int depth = 0;
                    ++i;
                    do {
                        if (tokens[i].kind == TLParen) ++depth;
                        else if (tokens[i].kind == TRParen) --depth;
                        ++i;
                    } while (i < tokens.size() && depth > 0);
                    --i;
                }
                continue;
            }
            if (t.kind == TLBrack && i + 1 < tokens.size() && tokens[i + 1].kind == TLBrack) {
                size_t j = i + 2;
                while (j + 1 < tokens.size() && !(tokens[j].kind == TRBrack && tokens[j + 1].kind == TRBrack)) ++j;
                if (j + 1 < tokens.size()) {
                    i = j + 1;
                    continue;
                }
            }
            if (is_word(i, "static_assert") && i + 1 < tokens.size() && tokens[i + 1].kind == TLParen) {
                int depth = 0;
                ++i;
                do {
                    if (tokens[i].kind == TLParen) ++depth;
                    else if (tokens[i].kind == TRParen) --depth;
                    ++i;
                } while (i < tokens.size() && depth > 0);
                if (i < tokens.size() && tokens[i].kind == TSemi) ++i;
                --i;
                continue;
            }
            if (is_word(i, "using") && i + 1 < tokens.size()) {
                if (tokens[i + 1].kind == TNamespace) {
                    // using namespace a::b; : the functions of that namespace can be called by their bare names
                    // (names of types and variables are in one table already). The directive is remembered for
                    // the whole file, not only for the block it is written in.
                    std::string ns;
                    size_t j = i + 2;
                    for (; j < tokens.size() && tokens[j].kind != TSemi && tokens[j].kind != TEnd; ++j) {
                        if (tokens[j].kind == TIdent) ns += tokens[j].text;
                        else if (tokens[j].kind == TColonColon) ns += "::";
                    }
                    if (!ns.empty()) using_namespaces.push_back(ns);
                    i = j;
                    continue;
                }
                if (tokens[i + 1].kind == TIdent && i + 3 < tokens.size() && tokens[i + 2].kind == TColonColon) {
                    // using a::b::name; ([namespace.udecl]): read as a directive for the namespace a::b, which is a
                    // little wider than the declaration (every function of it can be called by its bare name).
                    std::string ns;
                    size_t j = i + 1;
                    std::string last;
                    for (; j < tokens.size() && tokens[j].kind != TSemi && tokens[j].kind != TEnd; ++j) {
                        if (tokens[j].kind == TIdent) {
                            if (!last.empty()) ns += (ns.empty() ? "" : "::") + last;
                            last = tokens[j].text;
                        }
                    }
                    if (!ns.empty()) using_namespaces.push_back(ns);
                    i = j;
                    continue;
                }
                if (tokens[i + 1].kind == TIdent && i + 2 < tokens.size() && tokens[i + 2].kind == TAssign) {
                    size_t j = i + 3;
                    while (j < tokens.size() && tokens[j].kind != TSemi && tokens[j].kind != TEnd) ++j;
                    Token td = t;
                    td.kind = TTypedef;
                    td.text = "typedef";
                    out.push_back(td);
                    // The name goes where the declarator would have it ([dcl.typedef], [dcl.name]): after the stars of
                    // `R (*)(A)` (a function pointer), before the first '[' of `T[N]`, else at the end.
                    size_t name_at = j;
                    int angle = 0;
                    for (size_t k = i + 3; k < j; ++k) {
                        if (tokens[k].kind == TLt) ++angle;
                        else if (tokens[k].kind == TGt && angle > 0) --angle;
                        if (angle > 0) continue;
                        if (tokens[k].kind == TLParen && k + 1 < j && tokens[k + 1].kind == TStar) {
                            name_at = k + 1;
                            while (name_at < j && tokens[name_at].kind == TStar) ++name_at;
                            break;
                        }
                        if (tokens[k].kind == TLBrack) {
                            name_at = k;
                            break;
                        }
                    }
                    for (size_t k = i + 3; k < j; ++k) {
                        if (k == name_at) out.push_back(tokens[i + 1]);
                        out.push_back(tokens[k]);
                    }
                    if (name_at == j) out.push_back(tokens[i + 1]);
                    i = j - 1;
                    continue;
                }
            }
            if (t.kind == TEnum) {
                out.push_back(t);
                size_t j = i + 1;
                const bool is_scoped = j < tokens.size() && tokens[j].kind == TStruct;
                if (is_scoped) ++j;
                if (j < tokens.size() && tokens[j].kind == TIdent) {
                    out.push_back(tokens[j]);
                    if (is_scoped) in_scoped = tokens[j].text;
                    ++j;
                }
                if (j < tokens.size() && tokens[j].kind == TColon) {  // : underlying type
                    ++j;
                    while (j < tokens.size() && tokens[j].kind != TLBrace && tokens[j].kind != TSemi && tokens[j].kind != TEnd) ++j;
                }
                if (j < tokens.size() && tokens[j].kind == TLBrace) {
                    out.push_back(tokens[j]);
                    enum_depth = is_scoped ? 1 : 0;
                    want_name = is_scoped;
                    ++j;
                } else {
                    in_scoped.clear();
                }
                i = j - 1;
                continue;
            }
            if (enum_depth > 0) {
                if (t.kind == TRBrace) {
                    enum_depth = 0;
                    in_scoped.clear();
                } else if (t.kind == TComma) {
                    want_name = true;
                } else if (want_name && t.kind == TIdent) {
                    Token n = t;
                    n.text = in_scoped + "::" + t.text;
                    out.push_back(n);
                    want_name = false;
                    continue;
                } else {
                    want_name = false;
                }
            }
            if (t.kind == TIdent && scoped.count(t.text) == 0 && i + 4 < tokens.size() && tokens[i + 1].kind == TColonColon &&
                tokens[i + 2].kind == TIdent && scoped.count(tokens[i + 2].text) > 0 && tokens[i + 3].kind == TColonColon &&
                tokens[i + 4].kind == TIdent) {
                // Input::PadFamily::Xbox: a scoped enumerator named through the class (or namespace) that declares the
                // enumeration. Enumerators are kept in one table here, so the outer qualifier is dropped.
                ++i;
                continue;
            }
            if (t.kind == TIdent && scoped.count(t.text) > 0 && i + 2 < tokens.size() && tokens[i + 1].kind == TColonColon &&
                tokens[i + 2].kind == TIdent) {
                Token n = t;
                n.text = t.text + "::" + tokens[i + 2].text;
                out.push_back(n);
                i += 2;
                continue;
            }
            if (t.kind == TIdent && (t.text == "static_cast" || t.text == "reinterpret_cast" || t.text == "const_cast") &&
                i + 1 < tokens.size() && tokens[i + 1].kind == TLt) {
                int angle = 0;
                size_t j = i + 1;
                for (; j < tokens.size() && tokens[j].kind != TEnd; ++j) {
                    if (tokens[j].kind == TLt) ++angle;
                    else if (tokens[j].kind == TGt && --angle == 0) break;
                }
                if (j + 1 < tokens.size() && tokens[j].kind == TGt && tokens[j + 1].kind == TLParen) {
                    Token open = tokens[i + 1];
                    open.kind = TLParen;
                    open.text = "(";
                    Token close = tokens[j];
                    close.kind = TRParen;
                    close.text = ")";
                    out.push_back(open);
                    for (size_t k = i + 2; k < j; ++k) {
                        // volatile is dropped here too; `T *const` inside the cast loses its const as above.
                        if (tokens[k].kind == TIdent && tokens[k].text == "volatile") continue;
                        if (tokens[k].kind == TConst && !out.empty() && out.back().kind == TStar) continue;
                        out.push_back(tokens[k]);
                    }
                    out.push_back(close);
                    i = j;
                    continue;
                }
            }
            out.push_back(t);
        }
        tokens = std::move(out);
    }

    // A name written with its namespaces in front (geo::unit::kScale, std::uint8_t) is read as the
    // bare name: types, variables and enumerators of a namespace share one table. A call keeps its
    // qualification, because functions are registered with it (and decorated with it).
    void strip_namespace_qualifiers() {
        for (size_t i = 0; i + 1 < tokens.size(); ++i) {
            if (tokens[i].kind != TNamespace) continue;
            for (size_t j = i + 1; j < tokens.size() && (tokens[j].kind == TIdent || tokens[j].kind == TColonColon); ++j)
                if (tokens[j].kind == TIdent) namespaces.insert(tokens[j].text);
        }
        if (namespaces.empty()) return;
        std::vector<Token> out;
        out.reserve(tokens.size());
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i].kind == TNamespace) {
                while (i < tokens.size() && tokens[i].kind != TLBrace && tokens[i].kind != TEnd) out.push_back(tokens[i++]);
                if (i < tokens.size()) out.push_back(tokens[i]);
                continue;
            }
            if (tokens[i].kind == TIdent && namespaces.count(tokens[i].text) > 0 && i + 1 < tokens.size() &&
                tokens[i + 1].kind == TColonColon && !(i > 0 && tokens[i - 1].kind == TColonColon)) {
                size_t k = i;
                while (k + 1 < tokens.size() && tokens[k].kind == TIdent && namespaces.count(tokens[k].text) > 0 &&
                       tokens[k + 1].kind == TColonColon)
                    k += 2;
                const bool call = k + 1 < tokens.size() && tokens[k].kind == TIdent && tokens[k + 1].kind == TLParen;
                if (!call && k < tokens.size() && tokens[k].kind == TIdent) {
                    if (tokens[i].inl) tokens[k].inl = true;  // `inline std::uint32_t f()`: the mark moves to the type name
                    i = k - 1;  // the leading namespace names and their '::' are dropped
                    continue;
                }
            }
            out.push_back(tokens[i]);
        }
        tokens = std::move(out);
    }
    // The class whose member function body is being compiled, or -1.
    int cur_class = -1;
    int scope_depth = 0;
    std::vector<int> scope_parent{0};
    // Objects with destructors, one list per open block. A block end, return, break and
    // continue run the destructors of the blocks they leave, innermost and latest first.
    struct DObj {
        std::string name;
        int sid = -1;
        int count = 0;  // above 0: an array of that many objects
        int kind = 0;   // 0 object, 1 try frame (popped), 2 handler (ended)
    };
    // A global or static object whose destructor runs when the program ends.
    struct GlobalObj {
        std::string name;
        int sid = -1;
        int count = 0;
        std::string guard;  // a static local: destroyed only if this flag is set
    };
    std::vector<Local> init_locals{};
    // While a global's initial data is read: where the addresses it holds are recorded.
    std::vector<std::pair<uint32_t, std::string>>* init_relocs = nullptr;
    int gstr_count = 0;
    Node init_body{};
    bool init_used = false;
    std::vector<GlobalObj> global_objs{};
    std::vector<GlobalObj> static_objs{};
    std::vector<Node> global_init{};
    std::vector<std::vector<DObj>> dscopes{};
    // The local that `return name;` hands to the caller as the result: its destructor does not run ([class.copy.elision]).
    std::string dtor_skip{};
    size_t fn_dbase = 0;
    std::vector<size_t> brk_dbase{};
    std::vector<size_t> cont_dbase{};
    Type cur_ret{};
    int cur_special = 0;
    bool cur_ret_ref = false;
    bool cur_implicit_copy = false;
    int rv_count = 0;

    bool class_needs_ctor(const StructDef& d) const {
        for (const Method& m : d.methods)
            if (m.name == "<ctor>") return true;
        if (d.poly) return true;  // the constructor stores the table pointer
        if (d.base >= 0 && class_needs_ctor(structs[static_cast<size_t>(d.base)])) return true;
        for (size_t i = d.base_fields; i < d.fields.size(); ++i) {
            const Type& t = d.fields[i].type;
            if (d.fields[i].init_at >= 0 || d.fields[i].init_zero) return true;
            if (t.base == 3 && t.pointers == 0 && !t.is_ref && t.struct_id >= 0 &&
                class_needs_ctor(structs[static_cast<size_t>(t.struct_id)]))
                return true;
        }
        return false;
    }
    bool class_needs_dtor(const StructDef& d) const {
        for (const Method& m : d.methods)
            if (m.name == "<dtor>") return true;
        if (d.base >= 0 && class_needs_dtor(structs[static_cast<size_t>(d.base)])) return true;
        for (size_t i = d.base_fields; i < d.fields.size(); ++i) {
            const Type& t = d.fields[i].type;
            if (t.base == 3 && t.pointers == 0 && !t.is_ref && t.struct_id >= 0 &&
                class_needs_dtor(structs[static_cast<size_t>(t.struct_id)]))
                return true;
        }
        return false;
    }
    bool obj_needs_ctor(const Type& t) const {
        return t.base == 3 && t.pointers == 0 && !t.is_ref && t.struct_id >= 0 &&
               class_needs_ctor(structs[static_cast<size_t>(t.struct_id)]);
    }
    bool obj_needs_dtor(const Type& t) const {
        return t.base == 3 && t.pointers == 0 && !t.is_ref && t.struct_id >= 0 &&
               class_needs_dtor(structs[static_cast<size_t>(t.struct_id)]);
    }

    // A class with no constructor written by the programmer, none of its own bases, and a braced list of members:
    // an aggregate whose members have default initializers.
    bool aggregate_with_defaults(int sid, const std::vector<Node>& args) const {
        const StructDef& sd = structs[static_cast<size_t>(sid)];
        if (sd.poly || sd.base >= 0 || sd.fields.empty()) return false;
        for (const Method& m : sd.methods)
            if (m.name == "<ctor>" && !m.implicit) return false;
        if (args.size() == 1) {
            Type at;
            if (static_type(args[0], &at) && at.base == 3 && at.pointers == 0 && !at.is_array() && at.struct_id == sid)
                return false;  // a copy
        }
        return true;
    }
    // A call of a constructor or destructor on the object whose address is 'self'.
    bool special_call(int sid, const char* which, Node self, std::vector<Node> args, Node* out) {
        std::string tctor;
        if (which[1] == 'c' && !args.empty() && aggregate_with_defaults(sid, args)) {
            // C{a, b} for a struct with default member initializers and no constructor written: the default
            // constructor runs (it stores the defaults), then the members are set in order ([dcl.init.aggr]).
            std::vector<Node> steps;
            const Method* dflt = find_method(sid, "<ctor>", 0);
            if (dflt != nullptr) {
                Node call;
                call.kind = Node::Call;
                call.text = dflt->qname;
                call.number = 1;
                call.type = dflt->ret;
                call.kids.push_back(self);
                Node st;
                st.kind = Node::ExprStmt;
                st.kids.push_back(std::move(call));
                steps.push_back(std::move(st));
            }
            const StructDef& sd = structs[static_cast<size_t>(sid)];
            size_t k = 0;
            for (const Field& f : sd.fields) {
                if (f.name == "__vptr") continue;
                if (k >= args.size()) break;
                Node fn;
                fn.kind = Node::Field;
                fn.op = TArrow;
                fn.text = f.name;
                fn.kids.push_back(self);
                Node assign;
                assign.kind = Node::Assign;
                assign.op = TAssign;
                assign.number = obj_needs_ctor(f.type) || obj_needs_dtor(f.type) ? 0 : 1;  // an object: its operator=
                assign.kids.push_back(std::move(fn));
                assign.kids.push_back(std::move(args[k]));
                ++k;
                Node st;
                st.kind = Node::ExprStmt;
                st.kids.push_back(std::move(assign));
                steps.push_back(std::move(st));
            }
            Node block;
            block.kind = Node::Block;
            block.kids = std::move(steps);
            *out = std::move(block);
            return true;
        }
        if (which[1] == 'c' && !ensure_ctor_template(sid, args, &tctor)) return false;
        const Method* m = which[1] == 'c' ? find_ctor(sid, args) : find_method(sid, which, args.size());
        if (!tctor.empty()) m = find_ctor_instance(sid, tctor);
        if (m != nullptr && which[1] == 'c' && !pad_defaults(m->defaults, m->params.size(), &args)) return false;
        if (m != nullptr && which[1] == 'c' && !adjust_args(m->params, &args)) return false;
        if (m == nullptr && which[1] == 'c' && args.size() == 1 && !class_has_copy(sid)) {
            // C x(other) with no copy constructor written or implied: the members are copied as they are ([class.copy]).
            Type from;
            if (static_type(args[0], &from) && from.base == 3 && from.pointers == 0 && !from.is_array() && from.struct_id == sid) {
                Node target;
                target.kind = Node::Unary;
                target.op = TStar;
                target.kids.push_back(std::move(self));
                Node copy;
                copy.kind = Node::Assign;
                copy.op = TAssign;
                copy.number = 1;
                copy.kids.push_back(std::move(target));
                copy.kids.push_back(std::move(args[0]));
                Node stmt;
                stmt.kind = Node::ExprStmt;
                stmt.kids.push_back(std::move(copy));
                *out = std::move(stmt);
                return true;
            }
        }
        if (m == nullptr) {
            error = std::string("no matching ") + (which[1] == 'c' ? "constructor" : "destructor") + " in " +
                    structs[static_cast<size_t>(sid)].name;
            return false;
        }
        Node call;
        call.kind = Node::Call;
        call.text = m->qname;
        call.number = 1;
        call.type = m->ret;
        call.kids.push_back(std::move(self));
        for (Node& arg : args) call.kids.push_back(std::move(arg));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(call));
        *out = std::move(stmt);
        return true;
    }
    // A call of a function that returns a reference is the pointer it returns, dereferenced.
    static Node reference_result(Node call, bool is_ref, bool rvalue = false) {
        if (!is_ref) return call;
        call.type.is_ref = false;
        call.type.rref = false;
        call.type.pointers += 1;
        Node d;
        d.kind = Node::Unary;
        d.op = TStar;
        if (rvalue) d.text = "xval";  // std::move(x) and the like: an rvalue
        d.kids.push_back(std::move(call));
        return d;
    }
    Node node_name(const std::string& n) const {
        Node name;
        name.kind = Node::Name;
        name.text = n;
        return name;
    }
    Node node_addr(Node inner) const {
        Node a;
        a.kind = Node::Unary;
        a.op = TAmp;
        a.kids.push_back(std::move(inner));
        return a;
    }
    // Fills `target` (an lvalue of `type`) from a braced list, element by element; what the list does not name is
    // zero ([dcl.init.aggr]). `item` is null for "nothing named".
    bool lower_aggr(const Type& type, const Node& target, const Node* item, std::vector<Node>* out) {
        const bool list = item != nullptr && item->kind == Node::Block && item->text == "init";
        if (type.is_array()) {
            if (item != nullptr && !list) {
                error = "an array member needs a braced initializer";
                return false;
            }
            const Type elem = peel_array(type);
            for (int i = 0; i < type.array_len; ++i) {
                Node idx;
                idx.kind = Node::IntLit;
                idx.type.base = 1;
                idx.number = static_cast<uint64_t>(i);
                Node at_i;
                at_i.kind = Node::Index;
                at_i.kids.push_back(target);
                at_i.kids.push_back(std::move(idx));
                const Node* kid = list && static_cast<size_t>(i) < item->kids.size() ? &item->kids[static_cast<size_t>(i)] : nullptr;
                if (!lower_aggr(elem, at_i, kid, out)) return false;
            }
            return true;
        }
        if (type.base == 3 && type.pointers == 0 && !type.is_ref && type.struct_id >= 0 && (item == nullptr || list)) {
            const StructDef& sd = structs[static_cast<size_t>(type.struct_id)];
            size_t k = 0;
            for (const Field& f : sd.fields) {
                if (f.name == "__vptr") continue;
                Node fn;
                fn.kind = Node::Field;
                fn.op = TDot;
                fn.text = f.name;
                fn.kids.push_back(target);
                const Node* kid = list && k < item->kids.size() ? &item->kids[k] : nullptr;
                ++k;
                if (!lower_aggr(f.type, fn, kid, out)) return false;
            }
            return true;
        }
        Node value;
        if (list) {
            if (item->kids.empty()) {
                value.kind = Node::IntLit;
                value.type.base = 1;
            } else {
                value = item->kids[0];
            }
        } else if (item != nullptr) {
            value = *item;
        } else {
            value.kind = Node::IntLit;
            value.type.base = 1;
        }
        Node assign;
        assign.kind = Node::Assign;
        assign.op = TAssign;
        assign.kids.push_back(target);
        assign.kids.push_back(std::move(value));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(assign));
        out->push_back(std::move(stmt));
        return true;
    }
    bool field_is_aggr(const Field& field) const {
        if (!(field.init_aggr || field.init_braced || field.init_zero)) return false;
        if (field.type.is_array()) return !obj_needs_ctor(peel_array(field.type));
        return field.type.base == 3 && field.type.pointers == 0 && !field.type.is_ref && field.type.struct_id >= 0 &&
               !obj_needs_ctor(field.type);
    }
    Node node_this_field(const std::string& field) const {
        Node f;
        f.kind = Node::Field;
        f.op = TArrow;
        f.text = field;
        f.kids.push_back(node_name("this"));
        return f;
    }
    // this->__vptr = (void **)&table;
    Node vptr_store_this(const std::string& table_sym) const {
        Node table_addr = node_addr(node_name(table_sym));
        Node as_ptr;
        as_ptr.kind = Node::Unary;
        as_ptr.type.base = 0;
        as_ptr.type.pointers = 2;
        as_ptr.kids.push_back(std::move(table_addr));
        Node store;
        store.kind = Node::Assign;
        store.op = TAssign;
        store.kids.push_back(node_this_field("__vptr"));
        store.kids.push_back(std::move(as_ptr));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(store));
        return stmt;
    }
    // for (i = 0; i < n; i = i + 1) ctor(&name[i]) (or, going down, for the destructors). The
    // counter is a hidden local of the function being compiled.
    bool element_loop(const std::string& name, int count, bool reverse, int sid, const char* which, Node* out,
                      const Node* count_expr = nullptr) {
        if (cur_locals == nullptr) {
            error = "an array of objects needs a function";
            return false;
        }
        Local hidden;
        hidden.name = "__ai" + std::to_string(rv_count++);
        hidden.type.base = 1;
        place_local(cur_locals, hidden);
        auto int_lit = [](int v) {
            Node n;
            n.kind = Node::IntLit;
            n.number = static_cast<uint64_t>(v);
            n.type.base = 1;
            return n;
        };
        auto assign = [&](Node value) {
            Node a;
            a.kind = Node::Assign;
            a.op = TAssign;
            a.kids.push_back(node_name(hidden.name));
            a.kids.push_back(std::move(value));
            return a;
        };
        Node start;
        if (count_expr != nullptr && reverse) {
            start.kind = Node::Binary;
            start.op = TMinus;
            start.kids.push_back(*count_expr);
            start.kids.push_back(int_lit(1));
        } else {
            start = int_lit(reverse ? count - 1 : 0);
        }
        Node init = assign(std::move(start));
        Node cond;
        cond.kind = Node::Binary;
        cond.op = reverse ? TGe : TLt;
        cond.kids.push_back(node_name(hidden.name));
        if (count_expr != nullptr && !reverse) cond.kids.push_back(*count_expr);
        else cond.kids.push_back(int_lit(reverse ? 0 : count));
        Node step_value;
        step_value.kind = Node::Binary;
        step_value.op = reverse ? TMinus : TPlus;
        step_value.kids.push_back(node_name(hidden.name));
        step_value.kids.push_back(int_lit(1));
        Node step = assign(std::move(step_value));
        Node element;
        element.kind = Node::Index;
        element.kids.push_back(node_name(name));
        element.kids.push_back(node_name(hidden.name));
        Node call;
        if (!special_call(sid, which, node_addr(std::move(element)), {}, &call)) return false;
        Node loop;
        loop.kind = Node::For;
        loop.kids.push_back(std::move(init));
        loop.kids.push_back(std::move(cond));
        loop.kids.push_back(std::move(step));
        loop.kids.push_back(std::move(call));
        *out = std::move(loop);
        return true;
    }

    // C a[N] = { x, { y, z } }: element i is built by a constructor given its item (a brace is a
    // list of arguments); elements without an item use the default constructor.
    bool array_init_calls(const std::string& name, const Type& type, Node& init, std::vector<Node>* calls) {
        const int count = type.array_len;
        const int sid = type.struct_id;
        if (init.kind != Node::Block || init.text != "init") {
            error = "an array of objects is initialized with a brace list";
            return false;
        }
        if (static_cast<int>(init.kids.size()) > count) {
            error = "too many initializers for the array";
            return false;
        }
        for (int i = 0; i < count; ++i) {
            std::vector<Node> args;
            if (i < static_cast<int>(init.kids.size())) {
                Node& item = init.kids[static_cast<size_t>(i)];
                if (item.kind == Node::Block && item.text == "init") {
                    for (Node& a : item.kids) args.push_back(std::move(a));
                } else {
                    args.push_back(std::move(item));
                }
            }
            Node element;
            element.kind = Node::Index;
            element.kids.push_back(node_name(name));
            Node index;
            index.kind = Node::IntLit;
            index.number = static_cast<uint64_t>(i);
            index.type.base = 1;
            element.kids.push_back(std::move(index));
            Node call;
            if (!special_call(sid, "<ctor>", node_addr(std::move(element)), std::move(args), &call)) return false;
            calls->push_back(std::move(call));
        }
        return true;
    }

    // Destructor calls for the open blocks from 'base' up, latest object first.
    bool dtor_nodes(size_t base, std::vector<Node>* out) {
        for (size_t sc = dscopes.size(); sc-- > base;) {
            const std::vector<DObj>& list = dscopes[sc];
            for (size_t i = list.size(); i-- > 0;) {
                Node call;
                if (list[i].kind != 0) {
                    out->push_back(eh_call(list[i].kind == 1 ? "__okay_eh_pop" : "__okay_eh_end", {}));
                    continue;
                }
                if (list[i].count > 0) {
                    if (!element_loop(list[i].name, list[i].count, true, list[i].sid, "<dtor>", &call)) return false;
                    out->push_back(std::move(call));
                    if (eh_) out->push_back(eh_call("__okay_eh_pop", {}));
                    continue;
                }
                if (list[i].name != dtor_skip) {
                    if (!special_call(list[i].sid, "<dtor>", node_addr(node_name(list[i].name)), {}, &call)) return false;
                    out->push_back(std::move(call));
                }
                if (eh_) out->push_back(eh_call("__okay_eh_pop", {}));
            }
        }
        return true;
    }

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

    const Token& cur() const { return tokens[at]; }
    bool is(Tok k) const { return cur().kind == k; }
    void next() { if (at + 1 < tokens.size()) ++at; }
    bool accept(Tok k) {
        if (!is(k)) return false;
        next();
        return true;
    }
    bool expect(Tok k, const char* what) {
        if (accept(k)) return true;
        if (error.empty()) error = std::string("expected ") + what;
        return false;
    }

    // The first token at or after k that is not one of the keywords of a built-in type (unsigned long, double).
    size_t after_type_keywords(size_t k) const {
        while (k < tokens.size() && (tokens[k].kind == TInt || tokens[k].kind == TFloat || tokens[k].kind == TDouble ||
                                     tokens[k].kind == TChar || tokens[k].kind == TShort || tokens[k].kind == TUnsigned ||
                                     tokens[k].kind == TSigned || tokens[k].kind == TLong))
            ++k;
        return k;
    }
    // C::name (or ns::C::name) where C is a class and name an enumerator: its value.
    bool class_enumerator(const std::string& name, uint64_t* value) const {
        const size_t last = name.rfind("::");
        if (last == std::string::npos || local_visible(name)) return false;
        const std::string qual = name.substr(0, last);
        const size_t prev = qual.rfind("::");
        const std::string cls = prev == std::string::npos ? qual : qual.substr(prev + 2);
        const auto it = enum_values.find(name.substr(last + 2));
        if (find_struct(cls) < 0 || it == enum_values.end()) return false;
        *value = it->second;
        return true;
    }
    int find_struct(const std::string& name) const {
        for (size_t i = 0; i < structs.size(); ++i)
            if (structs[i].name == name) return static_cast<int>(i);
        return -1;
    }
    int size_of(const Type& type) const {
        if (type.is_ref) return 8;
        if (type.array_len >= 0) return type.array_len * size_of(peel_array(type));
        if (type.pointers > 0) return 8;
        if (type.base == 2) return 1;
        if (type.base == 4) return 2;
        if (type.base == 5) return 4;
        if (type.base == 6) return 8;
        if (type.base == 3 && type.struct_id >= 0 && type.struct_id < static_cast<int>(structs.size()))
            return structs[type.struct_id].size;
        if (type.base == 0) return 0;
        return type.wide ? 8 : 4;
    }
    int align_of(const Type& type) const {
        if (type.is_ref) return 8;
        if (type.array_len >= 0) return align_of(peel_array(type));
        if (type.pointers > 0) return 8;
        if (type.base == 2) return 1;
        if (type.base == 4) return 2;
        if (type.base == 5) return 4;
        if (type.base == 6) return 8;
        if (type.base == 3 && type.struct_id >= 0 && type.struct_id < static_cast<int>(structs.size()))
            return std::max(1, structs[type.struct_id].align);
        return type.wide ? 8 : 4;
    }
    bool fits_register(const Type& type) const {
        if (type.array_len >= 0 || type.pointers > 0) return true;
        const int size = size_of(type);
        return size == 1 || size == 2 || size == 4 || size == 8;
    }
    void place_local(std::vector<Local>* locals, Local local) {
        const int used = locals->empty() ? 0 : -locals->back().offset;
        int align = align_of(local.type);
        int size = size_of(local.type);
        if (align < 1) align = 1;
        if (size < 1) size = 1;
        const int aligned = (used + align - 1) & ~(align - 1);
        local.offset = -(aligned + size);
        local.scope = scope_depth;
        locals->push_back(local);
    }

    // After a class type: C::name, where name is a typedef or using declared in the class. While a
    // pattern is read, a name qualified by a template parameter cannot be deduced (tpat -2).
    // ISO/IEC 14882 [temp.res] (typename) and [temp.deduct.type] (non-deduced contexts).
    bool qualify(Type* t, bool must) {
        bool any = false;
        while (is(TColonColon) && at + 1 < tokens.size() && tokens[at + 1].kind == TIdent) {
            if (pat_values != nullptr && (t->tpat != -1 || (t->base >= 100 && t->pointers == 0))) {
                next();
                next();
                t->base = 3;
                t->struct_id = -1;
                t->tpat = -2;
                any = true;
                continue;
            }
            if (t->base == 3 && t->struct_id >= 0 && t->pointers == 0) {
                const StructDef& sd = structs[static_cast<size_t>(t->struct_id)];
                auto it = sd.tdefs.find(tokens[at + 1].text);
                if (it != sd.tdefs.end()) {
                    *t = it->second;
                    next();
                    next();
                    any = true;
                    continue;
                }
            }
            if (t->base == 3 && t->struct_id >= 0 && t->pointers == 0 && tokens[at + 1].kind == TIdent &&
                typedefs.find(tokens[at + 1].text) != typedefs.end() && find_struct(tokens[at + 1].text) < 0 &&
                !(at + 2 < tokens.size() && (tokens[at + 2].kind == TLParen || tokens[at + 2].kind == TColonColon))) {
                // Input::PadFamily: an enumeration (or typedef) declared in the class. Names of types are kept in one
                // table here, as for namespaces, so the class qualifier is dropped.
                *t = typedefs[tokens[at + 1].text];
                next();
                next();
                any = true;
                continue;
            }
            if (t->base == 3 && t->struct_id >= 0 && t->pointers == 0 && tokens[at + 1].kind == TIdent &&
                typedefs.find(tokens[at + 1].text) == typedefs.end()) {
                // Outer::Nested: nested classes are kept under their plain name.
                const int nested = find_struct(tokens[at + 1].text);
                if (nested >= 0 && nested != t->struct_id &&
                    !(at + 2 < tokens.size() && tokens[at + 2].kind == TLParen)) {
                    t->struct_id = nested;
                    next();
                    next();
                    any = true;
                    continue;
                }
            }
            break;
        }
        if (must && !any) {
            error = "typename names no member type";
            return false;
        }
        return true;
    }

    // A type with the qualifiers around it ([dcl.type.cv]): const T and T const are read and the
    // const is dropped (the type does not record it). The name to look for is after them.
    bool parse_type(Type* type, bool* imported, bool* exported) {
        while (accept(TConst)) {
        }
        if (!parse_type_core(type, imported, exported)) return false;
        while (accept(TConst)) {
        }
        return true;
    }
    bool parse_type_core(Type* type, bool* imported, bool* exported) {
        *imported = false;
        *exported = false;
        while (is(TDeclspec)) {
            next();
            if (!expect(TLParen, "'('")) return false;
            if (!is(TIdent)) {
                error = "expected dllimport or dllexport";
                return false;
            }
            if (cur().text == "dllimport") *imported = true;
            else if (cur().text == "dllexport") *exported = true;
            else {
                error = "unknown declspec " + cur().text;
                return false;
            }
            next();
            if (!expect(TRParen, "')'")) return false;
        }
        bool saw = false;
        bool had_typename = false;
        if (is(TIdent) && cur().text == "typename") {
            next();
            had_typename = true;
        }
        type->base = 1;
        type->pointers = 0;
        type->array_len = -1;
        type->struct_id = -1;
        type->func_id = -1;
        type->tpat = -1;
        type->bits = -1;
        type->bit_off = 0;
        type->is_unsigned = false;
        type->is_const = false;
        type->is_bool = false;
        type->wide = false;
        if (is(TStruct) || is(TUnion)) {
            const bool as_union = is(TUnion);
            const bool as_class = is(TStruct) && cur().text == "class";
            next();
            std::string name;
            if (is(TIdent)) {
                name = cur().text;
                next();
            }
            int base_id = -1;
            if (accept(TColon)) {
                while (is(TIdent) && (cur().text == "public" || cur().text == "private" ||
                                      cur().text == "protected" || cur().text == "virtual"))
                    next();
                if (!is(TIdent) || find_struct(cur().text) < 0) {
                    error = "expected a base class";
                    return false;
                }
                base_id = find_struct(cur().text);
                next();
                if (!is(TLBrace)) {
                    error = "only one base class is supported";
                    return false;
                }
            }
            if (is(TLBrace)) {
                int id = -1;
                if (!parse_struct_body(as_union, name, &id, as_class, base_id)) return false;
                type->base = 3;
                type->struct_id = id;
                return true;
            }
            if (name.empty()) {
                error = "expected a struct name";
                return false;
            }
            int id = find_struct(name);
            if (id < 0) {
                // class X; or struct X *p; before X is defined: an incomplete class, completed by its definition later.
                StructDef fwd;
                fwd.name = name;
                fwd.incomplete = true;
                fwd.cxx = true;
                fwd.is_class = as_class;
                fwd.is_union = as_union;
                structs.push_back(fwd);
                id = static_cast<int>(structs.size()) - 1;
            }
            type->base = 3;
            type->struct_id = id;
            return true;
        }
        if (is(TEnum)) {
            next();
            if (is(TIdent) && tokens[at + 1].kind != TLBrace) {
                next();
                type->base = 1;
                return true;
            }
            if (is(TIdent)) {
                // The enumeration's name is a type name too, as in C++ ([dcl.enum]); its values are int.
                Type enum_type;
                enum_type.base = 1;
                typedefs[cur().text] = enum_type;
                next();
            }
            if (!parse_enum_body()) return false;
            type->base = 1;
            return true;
        }
        if (is_template_name_at(at)) {
            const std::string tname = cur().text;
            next();
            std::vector<TArg> targs;
            if (!parse_template_args(class_templates[tname], &targs, false)) return false;
            if (pat_values != nullptr) {
                // Reading a pattern: keep the name and the arguments, make no instance.
                TPat pattern;
                pattern.name = tname;
                pattern.args = std::move(targs);
                tpats.push_back(std::move(pattern));
                type->base = 3;
                type->struct_id = -1;
                type->tpat = static_cast<int>(tpats.size()) - 1;
                return qualify(type, had_typename);
            }
            if (!instantiate_class(tname, targs, type)) return false;
            return qualify(type, had_typename);
        }
        if (is(TIdent) && typedefs.find(cur().text) != typedefs.end()) {
            *type = typedefs[cur().text];
            next();
            return qualify(type, had_typename);
        }
        if (is_class_name()) {
            type->base = 3;
            type->struct_id = find_struct(cur().text);
            next();
            return qualify(type, had_typename);
        }
        bool unsign = false;
        bool sign = false;
        int longs = 0;
        bool int64_word = false;
        while (is(TUnsigned) || is(TSigned) || is(TLong) || is(TShort) || is(TInt) || is(TVoid) || is(TChar) ||
               is(TFloat) || is(TDouble) || (is(TIdent) && (cur().text == "__int64" || cur().text == "__int32"))) {
            if (is(TIdent)) {
                // __int64 is long long and __int32 is int (the Microsoft spellings).
                if (cur().text == "__int64") int64_word = true;
                saw = true;
                next();
                continue;
            }
            if (is(TLong)) ++longs;
            if (is(TVoid)) type->base = 0;
            else if (is(TChar)) type->base = 2;
            else if (is(TShort)) type->base = 4;
            else if (is(TFloat)) type->base = 5;
            else if (is(TDouble)) type->base = 6;
            else if (is(TInt) || is(TLong)) {
                if (type->base != 2 && type->base != 4) type->base = 1;
            }
            if (is(TUnsigned)) unsign = true;
            if (is(TSigned)) sign = true;
            saw = true;
            next();
        }
        type->is_unsigned = unsign && !sign;
        if ((longs >= 2 || int64_word) && type->base == 1) type->wide = true;
        if (!saw) {
            error = "expected a type";
            return false;
        }
        return true;
    }

    bool parse_enum_body() {
        if (!expect(TLBrace, "'{'")) return false;
        uint64_t next_value = 0;
        if (is(TRBrace)) return expect(TRBrace, "'}'");
        while (!is(TEnd)) {
            if (!is(TIdent)) {
                error = "expected an enumerator";
                return false;
            }
            const std::string name = cur().text;
            next();
            if (accept(TAssign)) {
                if (!parse_const(&next_value, "an enumerator value")) return false;
            }
            enum_values[name] = next_value;
            ++next_value;
            if (!accept(TComma)) break;
            if (is(TRBrace)) break;
        }
        return expect(TRBrace, "'}'");
    }

    struct Declarator {
        std::string name;
        Type type{};
        bool is_function = false;
        bool vararg = false;
        std::vector<Type> params;
        std::vector<std::string> param_names;
        std::vector<Node> param_defaults;
    };

    // Default arguments ([dcl.fct.default]): defaults[i] is a real expression only when parameter i has one.
    static bool has_default(const std::vector<Node>& defaults, size_t i) {
        return i < defaults.size() && !(defaults[i].kind == Node::Block && defaults[i].text.empty());
    }
    // How many leading parameters have no default: the fewest arguments a call needs.
    static size_t required_args(const std::vector<Node>& defaults, size_t nparams) {
        for (size_t i = 0; i < nparams; ++i)
            if (has_default(defaults, i)) return i;
        return nparams;
    }
    // The same for a free function: a declaration may give a default that a later definition leaves out.
    bool pad_func_defaults(const std::string& name, std::vector<Node>* args) {
        size_t nparams = 0;
        bool seen = false;
        for (const FuncDecl& f : funcs)
            if (f.name == name) {
                nparams = f.params.size();
                seen = true;
                break;
            }
        if (!seen || args->size() >= nparams) return true;
        std::vector<Node> merged;
        for (const FuncDecl& f : funcs) {
            if (f.name != name) continue;
            for (size_t i = 0; i < f.defaults.size(); ++i) {
                if (!has_default(f.defaults, i)) continue;
                if (merged.size() <= i) merged.resize(i + 1);
                if (!has_default(merged, i)) merged[i] = f.defaults[i];
            }
        }
        return pad_defaults(merged, nparams, args);
    }
    // Appends the default of every parameter a call left out. Returns false (with an error) if one has none.
    bool pad_defaults(const std::vector<Node>& defaults, size_t nparams, std::vector<Node>* args) {
        while (args->size() < nparams) {
            if (!has_default(defaults, args->size())) {
                error = "too few arguments";
                return false;
            }
            const Node& d = defaults[args->size()];
            if (d.kind == Node::Block && d.text == "tokdef") {
                const size_t saved = at;
                at = static_cast<size_t>(d.number);
                Node made;
                const bool ok = tokens[at].kind == TLBrace ? braced_temp(d.type.struct_id, &made) : parse_expr(1, &made);
                at = saved;
                if (!ok) return false;
                args->push_back(std::move(made));
            } else {
                args->push_back(d);
            }
        }
        return true;
    }

    bool add_dim(Type* type, int length) {
        if (type->array_len < 0) type->array_len = length;
        else type->more.push_back(length);
        return true;
    }
    // A constant expression ([expr.const]): read as a conditional-expression and folded by the parser.
    bool parse_const(uint64_t* value, const char* what) {
        Node n;
        if (!parse_expr(2, &n)) return false;
        if (n.kind != Node::IntLit || n.type.base >= 5) {
            error = std::string("expected ") + what;
            return false;
        }
        *value = n.number;
        return true;
    }
    // [] is read as length 0 ("unsized"): the initializer gives the length (fix_unsized), a
    // parameter becomes a pointer, anything else is an error where it is used.
    bool parse_brackets(Type* type) {
        while (accept(TLBrack)) {
            int length = 0;
            uint64_t folded = 0;
            if (is(TRBrack)) {
                if (type->array_len >= 0) {
                    error = "only the first array length may be left out";
                    return false;
                }
            } else {
                if (!parse_const(&folded, "an array length")) return false;  // numbers, enumerators, template values
            }
            length = static_cast<int>(folded);
            if (!expect(TRBrack, "']'")) return false;
            if (!add_dim(type, length)) return false;
        }
        return true;
    }
    // T a[] = {...}; and char s[] = "..."; the length comes from the initializer ([dcl.array]).
    bool fix_unsized(Type* type, const Node& init) {
        if (type->array_len != 0) return true;
        if (init.kind == Node::Block && init.text == "init") type->array_len = static_cast<int>(init.kids.size());
        else if (init.kind == Node::StringLit && type->base == 2 && type->pointers == 0)
            type->array_len = static_cast<int>(init.text.size()) + 1;
        else {
            error = "an array without a length needs an initializer list";
            return false;
        }
        return true;
    }
    // Brace elision ([dcl.init.aggr]): in `std::array<int, 4> a = {1, 2, 3, 4};` the list names the elements of the
    // array inside the struct, not the struct's one member. Returns the list written with every level braced.
    bool is_aggregate_type(const Type& t) const {
        if (t.is_ref || t.pointers > 0) return false;
        if (t.is_array()) return true;
        return t.base == 3 && t.struct_id >= 0 && !structs[static_cast<size_t>(t.struct_id)].is_union &&
               !class_needs_ctor(structs[static_cast<size_t>(t.struct_id)]);
    }
    Node fill_aggregate(const Type& t, const std::vector<Node>& items, size_t* pos) const {
        Node list;
        list.kind = Node::Block;
        list.text = "init";
        list.number = 7;  // already regular
        std::vector<Type> parts;
        if (t.is_array()) {
            const Type elem = peel_array(t);
            for (int i = 0; i < t.array_len; ++i) parts.push_back(elem);
        } else {
            for (const Field& f : structs[static_cast<size_t>(t.struct_id)].fields)
                if (f.name != "__vptr") parts.push_back(f.type);
        }
        for (const Type& part : parts) {
            if (*pos >= items.size()) break;
            const Node& item = items[*pos];
            const bool braced = item.kind == Node::Block && item.text == "init";
            if (braced) {
                ++*pos;
                list.kids.push_back(is_aggregate_type(part) ? elide_braces(part, item) : item);
            } else if (is_aggregate_type(part) && item.kind != Node::StringLit) {
                list.kids.push_back(fill_aggregate(part, items, pos));
            } else {
                list.kids.push_back(item);
                ++*pos;
            }
        }
        return list;
    }
    Node elide_braces(const Type& t, const Node& list) const {
        if (list.number == 7 || !is_aggregate_type(t)) return list;
        size_t pos = 0;
        return fill_aggregate(t, list.kids, &pos);
    }
    bool parse_init(Node* out);
    bool flatten_init(const Type& type, const Node& node, std::vector<uint8_t>* bytes, int at);
    // The parts of a static initializer that are not constants, while flatten_init runs (see dyn_leaf_stmts).
    std::vector<DynLeaf>* dyn_leaves = nullptr;
    bool static_guard_block(const std::string& symbol, Node* then_block);
    void finish_static_guard(Node* then_block, Node init_stmt, const std::string& symbol, Node* out);

    bool parse_param_list(Declarator* out) {
        if (is(TVoid) && at + 1 < tokens.size() && tokens[at + 1].kind == TRParen) {
            next();
            next();
            return true;
        }
        if (is(TRParen)) {
            next();
            return true;
        }
        do {
            if (accept(TEllipsis)) {
                out->vararg = true;
                break;
            }
            Type pt;
            bool imp = false;
            bool exp = false;
            bool lead_const = accept(TConst);
            const std::string type_name = is(TIdent) ? cur().text : std::string();
            if (!parse_type(&pt, &imp, &exp)) return false;
            if (accept(TConst)) lead_const = true;
            pt.pconst = lead_const;
            if (is(TLParen) && at + 1 < tokens.size() && tokens[at + 1].kind == TStar) {
                next();
                int stars = 0;
                while (accept(TStar)) ++stars;
                std::string pname;
                if (!is(TIdent)) {
                    error = "expected a name";
                    return false;
                }
                pname = cur().text;
                next();
                if (!expect(TRParen, "')'")) return false;
                if (!expect(TLParen, "'('")) return false;
                Declarator fp;
                if (!parse_param_list(&fp)) return false;
                FuncType ft;
                ft.ret = pt;
                ft.params = fp.params;
                ft.vararg = fp.vararg;
                pt.pointers += stars;
                pt.func_id = static_cast<int>(func_types.size());
                func_types.push_back(ft);
                out->params.push_back(pt);
                out->param_names.push_back(pname);
                continue;
            }
            while (accept(TStar)) ++pt.pointers;
            if (accept(TAmp)) pt.is_ref = true;
            else if (accept(TAndAnd)) {
                pt.is_ref = true;
                if (pt.pointers == 0) pt.rref = true;
            }
            if (is(TEllipsis) && !type_name.empty()) {
                // Rest... rest: a function parameter pack ([temp.variadic]).
                next();
                std::string pack_name;
                if (is(TIdent)) {
                    pack_name = cur().text;
                    next();
                }
                auto tp = type_packs.find(type_name);
                if (tp == type_packs.end()) {
                    pt.pack = true;  // reading the template's parameter types: one entry stands for them all
                    out->params.push_back(pt);
                    out->param_names.push_back(pack_name);
                    continue;
                }
                std::vector<std::string> names;
                for (size_t k = 0; k < tp->second.size(); ++k) {
                    Type elem = tp->second[k];
                    elem.pointers += pt.pointers;
                    if (pt.is_ref) elem.is_ref = true;
                    if (pt.is_const) elem.is_const = true;
                    out->params.push_back(elem);
                    names.push_back(pack_name + "#" + std::to_string(k));
                    out->param_names.push_back(names.back());
                }
                if (!pack_name.empty()) param_packs[pack_name] = names;
                continue;
            }
            std::string pname;
            if (is(TIdent)) {
                pname = cur().text;
                next();
            }
            if (!parse_brackets(&pt)) return false;
            if (pt.array_len >= 0 && pt.more.empty()) {  // T a[] and T a[16] as a parameter are T *a ([dcl.fct])
                pt.array_len = -1;
                ++pt.pointers;
            }
            const size_t param_index = out->params.size();
            if (accept(TAssign)) {
                // A default argument: read where it is declared, so its names mean what they mean here.
                Node def_expr;
                if ((is(TLBrace) && pt.base == 3 && pt.pointers == 0 && pt.struct_id >= 0) ||
                    (is(TIdent) && find_struct(cur().text) >= 0 && typedefs.find(cur().text) == typedefs.end() &&
                     at + 1 < tokens.size() && (tokens[at + 1].kind == TLParen || tokens[at + 1].kind == TLBrace))) {
                    // `= {}`, `= {a, b}` or `= Color(1, 1, 1, 1)`: a class object made for each call. The text is
                    // kept as a position in the token stream and read again at each call, where the temporary
                    // can be made.
                    def_expr.kind = Node::Block;
                    def_expr.text = "tokdef";
                    def_expr.number = at;
                    def_expr.type = pt;
                    int depth = 0;
                    while (!is(TEnd)) {
                        if (is(TLBrace) || is(TLParen) || is(TLBrack)) ++depth;
                        else if (is(TRBrace) || is(TRParen) || is(TRBrack)) {
                            if (depth == 0) break;
                            --depth;
                        } else if (is(TComma) && depth == 0) break;
                        next();
                    }
                } else if (!parse_expr(1, &def_expr)) {
                    return false;
                }
                out->param_defaults.resize(param_index + 1);
                out->param_defaults[param_index] = std::move(def_expr);
            }
            out->params.push_back(pt);
            out->param_names.push_back(pname);
        } while (accept(TComma));
        return expect(TRParen, "')'");
    }

    bool parse_declarator(Type base, Declarator* out) {
        out->type = base;
        if (is(TLParen) && at + 1 < tokens.size() && tokens[at + 1].kind == TStar) {
            next();
            int stars = 0;
            while (accept(TStar)) ++stars;
            if (!is(TIdent)) {
                error = "expected a name";
                return false;
            }
            out->name = cur().text;
            next();
            if (!parse_brackets(&out->type)) return false;
            if (!expect(TRParen, "')'")) return false;
            if (!expect(TLParen, "'('")) return false;
            if (!parse_param_list(out)) return false;
            FuncType ft;
            ft.ret = base;
            ft.params = out->params;
            ft.vararg = out->vararg;
            out->type.pointers += stars;
            out->type.func_id = static_cast<int>(func_types.size());
            func_types.push_back(ft);
            out->params.clear();
            out->param_names.clear();
            out->vararg = false;
            return true;
        }
        while (accept(TStar)) ++out->type.pointers;
        if (accept(TAmp)) out->type.is_ref = true;
        else if (accept(TAndAnd)) {
            out->type.is_ref = true;
            if (out->type.pointers == 0) out->type.rref = true;
        }
        if (!is(TIdent)) {
            error = "expected a name";
            return false;
        }
        out->name = cur().text;
        next();
        if (!parse_brackets(&out->type)) return false;
        if (!is(TLParen)) return true;
        next();
        out->is_function = true;
        return parse_param_list(out);
    }

    // At a global declaration: R Box<args>::member( or Box<args>::member = ... where Box is a class
    // template, the definition of a member of an explicit or partial specialization.
    bool explicit_member_def_ahead() const {
        size_t k = at;
        for (; k + 3 < tokens.size() && k < at + 60; ++k) {
            const Tok kind = tokens[k].kind;
            if (kind == TLParen || kind == TAssign || kind == TLBrace || kind == TSemi || kind == TEnd) return false;
            if (kind != TColonColon || tokens[k - 1].kind != TGt) continue;
            size_t m = k + 1;
            if (tokens[m].kind == TTilde) ++m;
            if (tokens[m].kind != TIdent) return false;
            const Tok after = tokens[m + 1].kind;
            if (after != TLParen && after != TAssign && after != TSemi) return false;
            size_t lt = k - 1;
            int angle = 0;
            while (lt > at) {
                if (tokens[lt].kind == TGt) ++angle;
                else if (tokens[lt].kind == TLt && --angle == 0) break;
                --lt;
            }
            return tokens[lt].kind == TLt && lt > at && tokens[lt - 1].kind == TIdent &&
                   class_templates.count(tokens[lt - 1].text) > 0;
        }
        return false;
    }

    // C::name where C is a class and name is not a type of it (a static member or constant): an expression,
    // so a '(' in front of it does not start a cast.
    bool qualified_value_at(size_t i) const {
        if (i + 2 >= tokens.size() || tokens[i].kind != TIdent || tokens[i + 1].kind != TColonColon ||
            tokens[i + 2].kind != TIdent)
            return false;
        int id = find_struct(tokens[i].text);
        if (id < 0) {
            // A typedef (a template parameter bound to a class) names the class too.
            const auto td = typedefs.find(tokens[i].text);
            if (td != typedefs.end() && td->second.base == 3 && td->second.pointers == 0) id = td->second.struct_id;
        }
        if (id < 0) return false;
        const std::string& member = tokens[i + 2].text;
        if (structs[static_cast<size_t>(id)].tdefs.count(member) > 0) return false;
        return find_struct(member) < 0;
    }

    bool is_decl_start() const {
        if (qualified_expr_ahead()) return false;
        if (is(TInt) || is(TVoid) || is(TChar) || is(TUnsigned) || is(TSigned) || is(TLong) || is(TShort) ||
            is(TStruct) || is(TUnion) || is(TEnum) || is(TTypedef) || is(TDeclspec) || is(TStatic) || is(TConst) ||
            is(TFloat) || is(TDouble))
            return true;
        return (is(TIdent) && typedefs.find(cur().text) != typedefs.end()) || is_class_name() ||
               is_template_name_at(at) || (is(TIdent) && cur().text == "typename") ||
               (is(TIdent) && cur().text == "auto" && at + 1 < tokens.size() &&
                (tokens[at + 1].kind == TIdent || tokens[at + 1].kind == TAmp || tokens[at + 1].kind == TStar ||
                 tokens[at + 1].kind == TConst));
    }

    // A class or struct used by its bare name, as in C++ ("C c;").
    bool class_name_at(size_t i) const {
        if (i >= tokens.size() || tokens[i].kind != TIdent || typedefs.find(tokens[i].text) != typedefs.end())
            return false;
        const int id = find_struct(tokens[i].text);
        if (id < 0) return false;
        if (structs[static_cast<size_t>(id)].cxx) return true;
        // A plain struct named without the word struct, as C++ allows: only where a declaration
        // clearly follows (a name, '*' or '&').
        if (i >= 2 && tokens[i - 1].kind == TLParen && tokens[i - 2].kind == TSizeof && i + 1 < tokens.size() &&
            tokens[i + 1].kind == TRParen)
            return true;  // sizeof(S)
        if (i + 3 < tokens.size() && tokens[i + 1].kind == TColonColon && tokens[i + 2].kind == TIdent &&
            (find_struct(tokens[i + 2].text) >= 0 || typedefs.count(tokens[i + 2].text) > 0) && tokens[i + 3].kind != TLParen)
            return true;  // S::Nested names a nested class, enumeration or typedef of a plain struct
        return i + 1 < tokens.size() &&
               (tokens[i + 1].kind == TIdent || tokens[i + 1].kind == TStar || tokens[i + 1].kind == TAmp ||
                tokens[i + 1].kind == TAndAnd ||
                ((tokens[i + 1].kind == TComma || tokens[i + 1].kind == TGt) && i >= 1 &&
                 (tokens[i - 1].kind == TLt || tokens[i - 1].kind == TComma)));  // a template argument: Box<P, 3>
    }
    bool is_class_name() const {
        if (!is(TIdent) || typedefs.find(cur().text) != typedefs.end()) return false;
        return class_name_at(at);
    }

    bool local_visible(const std::string& name) const;
    bool static_type(const Node& node, Type* out) const;
    bool parse_call_args(std::vector<Node>* args);
    bool expand_pack_arg(Node arg, std::vector<Node>* args);
    std::vector<std::string> packs_in(size_t from, size_t to) const;
    bool parse_pack_element(size_t from, size_t to, size_t k, bool unary, Node* out);
    bool try_fold(Node* out, bool* folded);
    bool parse_new(Node* out);
    bool ensure_vector_helpers(int sid);
    bool parse_delete(Node* out);
    bool method_call(Node* out, Node object, bool arrow, const std::string& name, std::vector<TArg> targs = {},
                     std::vector<Node>* preparsed = nullptr);
    bool operator_binary(Tok op, Node* left, Node* right, bool* done);
    bool operator_unary(Tok op, bool postfix, Node* operand, bool* done);
    bool operator_call(Node* object, std::vector<Node> args, bool* done);
    bool operator_index(Node* object, Node index, bool* done);
    bool free_call(const std::string& name, std::vector<Node> args, Node* out, bool* found);
    bool convert_class(Node* node, const Type& target);
    bool class_object_type(const Node& node, Type* out) const;
    bool operator_name(Tok op, std::string* name) const;
    static Type bool_type() {
        Type t;
        t.base = 2;
        t.is_bool = true;
        t.is_unsigned = true;
        return t;
    }
    bool qualified_call(Node* out, int cls, const std::string& name, std::vector<TArg> targs = {});
    bool parse_expr(int min_prec, Node* out);
    bool parse_unary(Node* out);
    bool parse_postfix(Node* out);
    // The class a qualifier names: a class, or a typedef (a template parameter) of a class type.
    int scope_class(const std::string& text) const {
        auto td = typedefs.find(text);
        if (td != typedefs.end()) {
            const Type& t = td->second;
            if (t.base == 3 && t.pointers == 0 && !t.is_array() && t.struct_id >= 0) return t.struct_id;
            return -1;
        }
        const int id = find_struct(text);
        return id >= 0 && structs[static_cast<size_t>(id)].cxx ? id : -1;
    }
    // A static data member or class constant used as an expression: a constant is its value, a
    // data member names its global.
    bool static_member_expr(int sid, const std::string& name, Node* out) const {
        if (sid < 0) return false;
        const StructDef& def = structs[static_cast<size_t>(sid)];
        auto it = def.statics.find(name);
        if (it == def.statics.end()) return false;
        if (it->second.has_const) {
            const_node(it->second, out);
            return true;
        }
        out->kind = Node::Name;
        out->text = it->second.symbol;
        return true;
    }
    // The literal a class constant stands for: integers keep their width and signedness, a float or double
    // its bits.
    static void const_node(const StaticMem& sm, Node* out) {
        out->kind = Node::IntLit;
        out->number = static_cast<uint64_t>(sm.cval);
        out->type = Type{};
        out->type.base = sm.type.base == 5 || sm.type.base == 6 ? sm.type.base : 1;
        if (out->type.base == 1) {
            out->type.wide = sm.type.wide;
            out->type.is_unsigned = sm.type.is_unsigned;
            out->number = const_norm(out->number, out->type);
        }
    }
    // The scalar type a name stands for when it is written like a call, T(x): a typedef of an arithmetic type
    // (also as std::uint64_t); base is -1 when the name is not such a type or a function of that name exists.
    Type functional_cast_type(const std::string& name) const {
        Type none;
        none.base = -1;
        for (const FuncDecl& fd : funcs)
            if (fd.name == name) return none;
        const size_t cc = name.rfind("::");
        const std::string bare = cc == std::string::npos ? name : name.substr(cc + 2);
        const auto td = typedefs.find(bare);
        if (td == typedefs.end()) return none;
        const Type& t = td->second;
        if (t.array_len >= 0 || t.is_ref) return none;
        if (t.pointers != 0) return t.func_id < 0 ? t : none;  // T(x) for a pointer type T
        if (t.base != 1 && t.base != 2 && t.base != 4 && t.base != 5 && t.base != 6) return none;
        return t;
    }
    // True while the operand of & is read: a name there stays a name so its address can be taken.
    bool no_const_subst = false;
    // A global declared const (or constexpr) with a constant initial value, as a literal node.
    bool global_const_node(const std::string& name, Node* out) const {
        for (const Global& g : globals) {
            if (g.name != name) continue;
            const Type& t = g.type;
            if (!t.is_const || g.bss || g.vtable || t.pointers != 0 || t.array_len >= 0 || t.is_ref) return false;
            if (t.base != 1 && t.base != 2 && t.base != 4 && t.base != 5 && t.base != 6) return false;
            const int width = size_of(t);
            if (width < 1 || width > 8 || g.init_bytes.size() < static_cast<size_t>(width)) return false;
            uint64_t v = 0;
            for (int i = width; i-- > 0;) v = (v << 8) | g.init_bytes[static_cast<size_t>(i)];
            *out = Node{};
            out->kind = Node::IntLit;
            if (t.base == 5 || t.base == 6) {
                out->type.base = t.base;
                out->number = v;
                return true;
            }
            out->type.base = 1;
            out->type.wide = t.wide;
            if (t.is_bool) {
                out->number = v != 0 ? 1 : 0;
                return true;
            }
            if (t.base == 2) v = t.is_unsigned ? (v & 0xFFu) : static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(v)));
            else if (t.base == 4) v = t.is_unsigned ? (v & 0xFFFFu) : static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(v)));
            else {
                out->type.is_unsigned = t.is_unsigned;
                v = const_norm(v, out->type);
            }
            out->number = v;
            return true;
        }
        return false;
    }
    // A call of a static member function without parameters whose body is just `return <constant>;` is that
    // constant ([expr.const]: a constexpr function called with constant arguments), so
    // std::numeric_limits<float>::infinity() can start a constant initializer.
    bool try_const_call(const Method& m, Node* out) {
        if (!m.params.empty() || !m.has_body || m.tok_body == 0 || m.tok_body + 3 >= tokens.size()) return false;
        if (tokens[m.tok_body].kind != TLBrace || tokens[m.tok_body + 1].kind != TReturn) return false;
        const size_t saved_at = at;
        const std::string saved_error = error;
        // The names in the body mean what they mean in the class: a member of a class template instance is read with
        // the instance's own template arguments bound, not the ones of the code that calls it.
        std::vector<Binding> bound;
        if (m.owner >= 0 && m.owner < static_cast<int>(structs.size()) && !structs[static_cast<size_t>(m.owner)].tmpl.empty()) {
            const StructDef& owner = structs[static_cast<size_t>(m.owner)];
            auto tmpl = class_templates.find(owner.tmpl);
            if (tmpl != class_templates.end()) bound = bind_params(tmpl->second, owner.targs);
        }
        at = m.tok_body + 2;
        Node value;
        bool ok = parse_expr(0, &value) && is(TSemi) && at + 1 < tokens.size() && tokens[at + 1].kind == TRBrace;
        unbind_params(bound);
        at = saved_at;
        error = saved_error;
        if (!ok || value.kind != Node::IntLit) return false;
        uint64_t bits = 0;
        if (!const_convert(value, m.ret, &bits)) return false;
        *out = Node{};
        out->kind = Node::IntLit;
        out->type.base = m.ret.base >= 5 ? m.ret.base : 1;
        out->number = bits;
        if (out->type.base == 1) {
            out->type.wide = m.ret.wide;
            out->type.is_unsigned = m.ret.is_unsigned;
            out->number = const_norm(bits, out->type);
        }
        return true;
    }
    // The StructDef being read by parse_struct_body: a constant declared earlier in the class body can be
    // used by the initializers that follow it.
    StructDef* building = nullptr;
    // At a name, an optional template argument list and '::' name, where what follows is not a
    // declarator: an expression such as Box<int>::count = 1; or P::n + 2; ([class.static.data]).
    bool qualified_expr_ahead() const {
        if (!is(TIdent) || cur().text == "typename") return false;
        size_t i = at + 1;
        if (i < tokens.size() && tokens[i].kind == TLt) {
            int depth = 0;
            for (; i < tokens.size() && tokens[i].kind != TEnd; ++i) {
                if (tokens[i].kind == TLt) ++depth;
                else if (tokens[i].kind == TGt && --depth == 0) break;
            }
            ++i;
        }
        if (i + 2 >= tokens.size() || tokens[i].kind != TColonColon || tokens[i + 1].kind != TIdent) return false;
        const Tok after = tokens[i + 2].kind;
        return after != TIdent && after != TStar && after != TAmp && after != TAndAnd && after != TColonColon;
    }
    bool class_name_text(const std::string& text) const {
        if (typedefs.find(text) != typedefs.end()) return false;
        const int id = find_struct(text);
        return id >= 0 && structs[static_cast<size_t>(id)].cxx;
    }
    bool parse_stmt(Node* out, std::vector<Local>* locals);
    bool parse_stmt_core(Node* out, std::vector<Local>* locals);
    bool parse_stmt_inner(Node* out, std::vector<Local>* locals, int stmt_line);
    bool parse_local_decl_one(const Type& type, bool is_typedef, bool is_static, bool is_const, Node* out,
                              std::vector<Local>* locals, int stmt_line, bool* more);
    // The end of a declarator: ',' means another follows, otherwise ';'.
    // `auto x = e;` and `for (T x : array)`: the initializer was read before the declaration, to learn its type;
    // the declaration then takes it from here, where it would have read `= e` (or `: e`).
    bool has_preset = false;
    Node preset_init{};
    size_t preset_end = 0;
    bool range_decl = false;
    int range_count = 0;
    bool end_decl(bool* more) {
        if (range_decl) {
            *more = false;
            return true;
        }
        if (accept(TComma)) {
            *more = true;
            return true;
        }
        *more = false;
        return expect(TSemi, "';'");
    }
    bool parse_block(Node* out, std::vector<Local>* locals);
    // for (declaration : range): a ':' before any ';' at the top level of the parentheses ([stmt.ranged]).
    bool range_for_ahead() const {
        int depth = 0;
        for (size_t i = at; i < tokens.size() && tokens[i].kind != TEnd; ++i) {
            const Tok k = tokens[i].kind;
            if (k == TLParen || k == TLBrack || k == TLBrace) ++depth;
            else if (k == TRParen || k == TRBrack || k == TRBrace) {
                if (depth == 0) return false;
                --depth;
            } else if (depth == 0 && k == TSemi) return false;
            else if (depth == 0 && k == TColon) return true;
        }
        return false;
    }
    bool parse_range_for(Node* out, std::vector<Local>* locals, int stmt_line);
    bool parse_lambda(Node* out);
    bool braced_temp(int bsid, Node* out);
    // At a '{': is there another '{' inside it, before its matching '}'?
    bool has_nested_braces_ahead() const {
        int depth = 0;
        for (size_t i = at; i < tokens.size() && tokens[i].kind != TEnd; ++i) {
            if (tokens[i].kind == TLBrace) {
                if (depth >= 1) return true;
                ++depth;
            } else if (tokens[i].kind == TRBrace && --depth == 0) return false;
        }
        return false;
    }
    bool parse_condition(Node* cond, Node* decl, bool* scoped, std::vector<Local>* locals);
    void close_condition_scope(Node* stmt, Node decl, bool scoped);
    bool init_list_temp(const Type& list_type, Node* out);
    bool is_init_list_class(const Type& t) const {
        return t.base == 3 && t.pointers == 0 && t.struct_id >= 0 && structs[static_cast<size_t>(t.struct_id)].tmpl == "initializer_list";
    }
    bool has_init_list_ctor(int sid, Type* list_type) const {
        for (const Method& m : structs[static_cast<size_t>(sid)].methods)
            if (m.name == "<ctor>" && m.params.size() == 1 && is_init_list_class(m.params[0])) {
                *list_type = m.params[0];
                return true;
            }
        return false;
    }
    int lambda_count = 0;
    // While a lambda body is read once only to learn the type of its first return.
    bool lam_probe = false;
    bool lam_probe_seen = false;
    Type lam_probe_type{};

    // ISO/IEC 14882 [expr]: the binary operators from the loosest to the tightest.
    int prec(Tok op) const {
        if (op == TComma) return 0;
        if (is_assign_op(op)) return 1;
        if (op == TQuestion) return 2;
        if (op == TOrOr) return 3;
        if (op == TAndAnd) return 4;
        if (op == TPipe) return 5;
        if (op == TCaret) return 6;
        if (op == TAmp) return 7;
        if (op == TEq || op == TNe) return 8;
        if (op == TLt || op == TGt || op == TLe || op == TGe) return 9;
        if (op == TShl || op == TShr) return 10;
        if (op == TPlus || op == TMinus) return 11;
        if (op == TStar || op == TSlash || op == TPercent) return 12;
        return -1;
    }

    // The binary operator starting at token i and how many tokens it takes. '>' '>' written together
    // is a right shift and '>' '>=' is '>>='; the lexer keeps them apart so that nested template
    // argument lists close.
    Tok op_at(size_t i, size_t* width) const {
        *width = 1;
        if (i >= tokens.size()) return TEnd;
        if (tokens[i].kind == TGt && i + 1 < tokens.size() && tokens[i + 1].adj) {
            if (tokens[i + 1].kind == TGt) {
                *width = 2;
                return TShr;
            }
            if (tokens[i + 1].kind == TGe) {
                *width = 2;
                return TShrEq;
            }
        }
        return tokens[i].kind;
    }
    // The operator whose last token is token i (the one written before a fold's '...').
    Tok op_ending_at(size_t i, size_t* width) const {
        *width = 1;
        if (i >= 1 && tokens[i].kind == TGt && tokens[i].adj && tokens[i - 1].kind == TGt) {
            *width = 2;
            return TShr;
        }
        return tokens[i].kind;
    }

    // Looks ahead from the current token: stars or '&', a name, then '('.
    bool method_ahead() const {
        size_t i = at;
        while (i < tokens.size() && (tokens[i].kind == TStar || tokens[i].kind == TAmp || tokens[i].kind == TAndAnd)) ++i;
        return i + 1 < tokens.size() && tokens[i].kind == TIdent && tokens[i + 1].kind == TLParen;
    }

    bool skip_body() {
        if (!is(TLBrace)) {
            error = "expected a function body";
            return false;
        }
        int depth = 0;
        while (!is(TEnd)) {
            if (is(TLBrace)) ++depth;
            if (is(TRBrace)) {
                --depth;
                if (depth == 0) {
                    next();
                    return true;
                }
            }
            next();
        }
        error = "unterminated function body";
        return false;
    }

    // ---- overload choice (ISO/IEC 14882 [over.match]) ----
    // The rank of converting the argument to a parameter of type p: 0 exact, 1 promotion, 2 conversion, 3 a
    // user-defined conversion; -1 when it cannot be converted. An argument whose type is not known counts as 1.
    bool derives_from(int sid, int base) const {
        for (int walk = sid; walk >= 0; walk = structs[static_cast<size_t>(walk)].base)
            if (walk == base) return true;
        return false;
    }
    static bool arith_type(const Type& t) {
        return t.pointers == 0 && !t.is_array() && t.base != 3 && t.base != 0 && t.func_id < 0;
    }
    // The conversion function of class sid that yields an arithmetic type like p (or a pointer, for bool), or null.
    const Method* conv_for(int sid, const Type& p) const {
        if (sid < 0 || sid >= static_cast<int>(structs.size())) return nullptr;
        const Method* any = nullptr;
        for (const Method& m : structs[static_cast<size_t>(sid)].methods) {
            if (m.name.compare(0, 13, "operator conv") != 0) continue;
            Type r = m.ret;
            r.is_ref = false;
            if (arith_type(p) ? arith_type(r) : (p.pointers == r.pointers && p.base == r.base)) {
                if (r.base == p.base && r.wide == p.wide && r.is_unsigned == p.is_unsigned) return &m;
                if (any == nullptr) any = &m;
            } else if (p.is_bool && r.pointers > 0 && any == nullptr) {
                any = &m;
            }
        }
        return any;
    }
    // An rvalue ([basic.lval]): a call that returns a class by value, the result of std::move, or a hidden temporary.
    bool is_rvalue(const Node& arg) const {
        if (arg.kind == Node::Unary && arg.op == TStar && arg.text == "xval") return true;
        if (arg.kind == Node::Name && (arg.text.compare(0, 4, "__bt") == 0 || arg.text.compare(0, 5, "__tmp") == 0 ||
                                       arg.text.compare(0, 4, "__br") == 0 || arg.text.compare(0, 4, "__il") == 0))
            return true;
        if (arg.kind == Node::Call) {
            Type t;
            return static_type(arg, &t) && t.base == 3 && t.pointers == 0 && !t.is_ref && !t.is_array();
        }
        return false;
    }
    int arg_score(const Node& arg, Type p) const {
        Type a;
        if (!static_type(arg, &a)) return 1;
        const bool want_ref = p.is_ref;
        const bool want_rref = p.rref;
        a.is_ref = false;
        p.is_ref = false;
        if (want_rref && p.pointers == 0 && p.base == 3 && !is_rvalue(arg)) return -1;
        if (a.is_array()) {
            a = peel_array(a);
            a.pointers += 1;
        }
        if (p.pointers > 0) {
            if (a.pointers == p.pointers && a.base == p.base && (p.base != 3 || a.struct_id == p.struct_id) &&
                (a.func_id >= 0) == (p.func_id >= 0))
                return 0;
            if (p.pointers == 1 && p.base == 0 && a.pointers >= 1) return 2;
            if (a.pointers == 1 && p.pointers == 1 && a.base == 3 && p.base == 3 && derives_from(a.struct_id, p.struct_id))
                return 2;
            if (arg.kind == Node::IntLit && arg.number == 0 && a.pointers == 0 && a.base < 5) return 2;
            return -1;
        }
        if (p.base == 3) {
            if (a.pointers != 0 || a.base != 3) return -1;
            if (a.struct_id == p.struct_id) return want_ref && !want_rref && is_rvalue(arg) ? 1 : 0;
            return derives_from(a.struct_id, p.struct_id) ? 2 : -1;
        }
        if (a.pointers > 0) return p.is_bool && p.base == 2 ? 3 : -1;
        if (a.base == 3) return conv_for(a.struct_id, p) != nullptr ? 3 : -1;
        if (a.base == 0 || p.base == 0) return -1;
        if (a.base == p.base && a.wide == p.wide && a.is_bool == p.is_bool && (a.base >= 5 || a.is_unsigned == p.is_unsigned))
            return 0;
        if (p.base == 6 && a.base == 5) return 1;
        if (p.base == 1 && !p.wide && !p.is_unsigned && (a.base == 2 || a.base == 4)) return 1;
        return 2;
    }
    // The total rank of a call with these arguments, or -1 when one cannot convert.
    int call_score(const std::vector<Type>& params, const std::vector<Node>& args) const {
        int total = 0;
        for (size_t i = 0; i < args.size() && i < params.size(); ++i) {
            const int r = arg_score(args[i], params[i]);
            if (r < 0) return -1;
            total += r;
        }
        return total;
    }

    // The member function name taking these arguments. Among several of the same name the best match by the
    // ranks above wins; obj_const is 1 for a const object, 0 for a non-const one, -1 when not known.
    const Method* find_method(int sid, const std::string& name, size_t nargs, const std::vector<Node>* args = nullptr,
                              int obj_const = -1) const {
        if (sid < 0 || sid >= static_cast<int>(structs.size())) return nullptr;
        const StructDef& def = structs[static_cast<size_t>(sid)];
        if (args != nullptr) {
            const Method* best = nullptr;
            int best_score = 1 << 30;
            const Method* fallback = nullptr;
            for (size_t i = def.methods.size(); i-- > 0;) {
                const Method& m = def.methods[i];
                if (m.name != name) continue;
                const bool exact = m.params.size() == nargs;
                if (!exact && !(nargs < m.params.size() && nargs >= required_args(m.defaults, m.params.size()))) continue;
                if (fallback == nullptr || (exact && fallback->params.size() != nargs)) fallback = &m;
                int score = call_score(m.params, *args);
                if (score < 0) continue;
                if (!exact) score += 100;
                if (obj_const == 1 && !m.is_const) score += 50;
                if (obj_const == 0 && m.is_const) score += 1;
                if (score < best_score) {
                    best_score = score;
                    best = &m;
                }
            }
            return best != nullptr ? best : fallback;
        }
        for (size_t i = def.methods.size(); i-- > 0;)
            if (def.methods[i].name == name && def.methods[i].params.size() == nargs) return &def.methods[i];
        // A function whose last parameters have defaults takes fewer arguments than it has parameters.
        for (size_t i = def.methods.size(); i-- > 0;) {
            const Method& m = def.methods[i];
            if (m.name == name && nargs < m.params.size() && nargs >= required_args(m.defaults, m.params.size())) return &m;
        }
        return nullptr;
    }

    // Free functions that share a name, by the full name ("ns::f"). Every free function is entered, so that an
    // operator function or an overload can be found by its arguments; the first signature keeps the plain
    // internal name, a later one is "name#decorated-parameters".
    struct OvFn {
        std::string sig;
        std::string internal;
        std::string symbol;
        std::vector<Type> params;
        size_t required = 0;
        bool vararg = false;
    };
    std::map<std::string, std::vector<OvFn>> ovl{};

    // The best of these overloads for the arguments: its internal name, or "" when there are none.
    std::string pick_from(const std::vector<const OvFn*>& list, const std::vector<Node>& args, bool strict = false) const {
        if (list.empty()) return std::string();
        if (list.size() == 1 && !strict) return list[0]->internal;
        const OvFn* best = nullptr;
        const OvFn* fallback = nullptr;
        int best_score = 1 << 30;
        for (const OvFn* f : list) {
            const bool exact = f->params.size() == args.size();
            if (!exact && !(args.size() < f->params.size() && args.size() >= f->required) &&
                !(f->vararg && args.size() > f->params.size()))
                continue;
            if (fallback == nullptr || (exact && fallback->params.size() != args.size())) fallback = f;
            int score = call_score(f->params, args);
            if (score < 0) continue;
            if (!exact) score += 100;
            if (score < best_score) {
                best_score = score;
                best = f;
            }
        }
        if (strict) return best != nullptr ? best->internal : std::string();
        return best != nullptr ? best->internal : fallback != nullptr ? fallback->internal : list[0]->internal;
    }
    // The best overload of `base` (the full name) for these arguments, or "" when base has no entry.
    std::string pick_free(const std::string& base, const std::vector<Node>& args) const {
        const auto it = ovl.find(base);
        if (it == ovl.end()) return std::string();
        std::vector<const OvFn*> list;
        for (const OvFn& f : it->second) list.push_back(&f);
        return pick_from(list, args);
    }
    // The same over every namespace: the functions called `name` (an operator is found in the namespaces of
    // its operands in the standard; here every namespace is looked at).
    std::string pick_any_ns(const std::string& name, const std::vector<Node>& args, bool strict = false) const {
        std::vector<const OvFn*> list;
        for (const auto& entry : ovl) {
            const std::string& key = entry.first;
            if (key == name || (key.size() > name.size() + 2 && key.compare(key.size() - name.size(), name.size(), name) == 0 &&
                                key.compare(key.size() - name.size() - 2, 2, "::") == 0))
                for (const OvFn& f : entry.second) list.push_back(&f);
        }
        return pick_from(list, args, strict);
    }

    static bool same_shape(const Type& a, const Type& b) {
        return a.base == b.base && a.pointers == b.pointers && a.is_ref == b.is_ref && a.rref == b.rref &&
               a.struct_id == b.struct_id;
    }
    static bool is_copy_param(const Method& m, int sid) {
        return m.name == "<ctor>" && m.params.size() == 1 && m.params[0].is_ref && !m.params[0].rref &&
               m.params[0].base == 3 && m.params[0].pointers == 0 && m.params[0].struct_id == sid;
    }
    static bool is_move_param(const Method& m, int sid) {
        return m.name == "<ctor>" && m.params.size() == 1 && m.params[0].is_ref && m.params[0].rref &&
               m.params[0].base == 3 && m.params[0].pointers == 0 && m.params[0].struct_id == sid;
    }
    const Method* copy_ctor(int sid) const {
        if (sid < 0 || sid >= static_cast<int>(structs.size())) return nullptr;
        for (const Method& m : structs[static_cast<size_t>(sid)].methods)
            if (is_copy_param(m, sid)) return &m;
        return nullptr;
    }
    // Chooses among constructors with this many arguments: an argument that is an object of the
    // class picks the copy constructor, anything else the others.
    const Method* find_ctor(int sid, const std::vector<Node>& args) const {
        const StructDef& def = structs[static_cast<size_t>(sid)];
        const Method* best = nullptr;
        int best_score = -1;
        for (const Method& m : def.methods) {
            if (m.name != "<ctor>" || m.params.size() != args.size()) continue;
            if (args.size() == 1) {
                Type at;
                bool is_obj = static_type(args[0], &at) && at.base == 3 && at.pointers == 0 && !at.is_array();
                if (is_obj) {
                    // The object is of the class or of a class derived from it.
                    bool related = false;
                    for (int walk = at.struct_id; walk >= 0; walk = structs[static_cast<size_t>(walk)].base)
                        if (walk == sid) related = true;
                    is_obj = related;
                }
                if (is_obj != (is_copy_param(m, sid) || is_move_param(m, sid))) continue;
            }
            // Of several constructors of one arity the best match by the ranks of [over.match] wins; a tie, or no
            // viable one, goes to the one declared last (the order used before).
            const int score = args.empty() ? 0 : call_score(m.params, args);
            if (best == nullptr || best_score < 0 || (score >= 0 && score <= best_score)) {
                best = &m;
                best_score = score;
            }
        }
        if (best == nullptr && args.size() == 1) {
            // An object of the class itself and no copy constructor: not a call of some other one-argument constructor.
            Type at;
            if (static_type(args[0], &at) && at.base == 3 && at.pointers == 0 && !at.is_array() && at.struct_id == sid)
                return nullptr;
        }
        if (best == nullptr) return find_method(sid, "<ctor>", args.size());
        return best;
    }
    bool class_has_copy(int sid) const { return copy_ctor(sid) != nullptr; }
    const Method* move_ctor(int sid) const {
        if (sid < 0 || sid >= static_cast<int>(structs.size())) return nullptr;
        for (const Method& m : structs[static_cast<size_t>(sid)].methods)
            if (is_move_param(m, sid)) return &m;
        return nullptr;
    }
    bool has_move_ctor(int sid) const { return move_ctor(sid) != nullptr; }
    // The object (not a pointer or array) is an lvalue expression of a class with a copy constructor.
    bool wants_copy(const Type& t, const Node& arg) const {
        if (!(t.base == 3 && t.pointers == 0 && !t.is_ref && t.struct_id >= 0 && !t.is_array())) return false;
        if (!class_has_copy(t.struct_id) && !has_move_ctor(t.struct_id)) return false;
        Type at;
        if (!(arg.kind == Node::Name || arg.kind == Node::Field || arg.kind == Node::Index ||
              (arg.kind == Node::Unary && arg.op == TStar)))
            return false;
        return static_type(arg, &at) && at.base == 3 && at.pointers == 0 && at.struct_id == t.struct_id &&
               !at.is_array();
    }
    // A value of class t that is not an object (a call returning t by value): it initializes an object of t in place.
    bool class_prvalue(const Type& t, const Node& value) const {
        if (!(t.base == 3 && t.pointers == 0 && !t.is_ref && t.struct_id >= 0 && !t.is_array())) return false;
        if (value.kind == Node::Name || value.kind == Node::Field || value.kind == Node::Index ||
            (value.kind == Node::Unary && value.op == TStar))
            return false;
        Type vt;
        return static_type(value, &vt) && vt.base == 3 && vt.pointers == 0 && !vt.is_ref && vt.struct_id == t.struct_id &&
               !vt.is_array();
    }
    struct Temp {
        std::string name;
        int sid = -1;
    };
    std::vector<Temp> stmt_temps{};
    std::vector<Node> stmt_pre{};
    int tmp_count = 0;

    // Arguments for a call: a reference parameter gets the address of its argument, a class
    // parameter with a copy constructor gets a copy made by that constructor into a hidden local
    // (destroyed after the statement).
    bool adjust_args(const std::vector<Type>& params, std::vector<Node>* args, size_t first = 0) {
        for (size_t i = first; i < args->size() && i - first < params.size(); ++i) {
            const Type& pt = params[i - first];
            Node& arg = (*args)[i];
            if (arg.kind == Node::Block && arg.text == "tokbrace") {
                const size_t saved = at;
                at = static_cast<size_t>(arg.number);
                Node made;
                bool ok;
                if (is_init_list_class(pt)) {
                    ok = init_list_temp(pt, &made);
                } else if (pt.base == 3 && pt.pointers == 0 && pt.struct_id >= 0) {
                    ok = braced_temp(pt.struct_id, &made);
                } else if (tokens[at + 1].kind == TRBrace) {
                    made.kind = Node::IntLit;
                    made.number = 0;
                    made.type.base = 1;
                    ok = true;
                } else {
                    next();
                    ok = parse_expr(1, &made);
                }
                at = saved;
                if (!ok) return false;
                arg = std::move(made);
            }
            if (pt.is_ref) {
                const bool object = arg.kind == Node::Name || arg.kind == Node::Field || arg.kind == Node::Index ||
                                    (arg.kind == Node::Unary && arg.op == TStar);
                Type from;
                if (cur_locals != nullptr && pt.base == 3 && pt.pointers == 0 && !pt.is_array() && pt.struct_id >= 0 &&
                    class_needs_ctor(structs[static_cast<size_t>(pt.struct_id)]) && static_type(arg, &from) &&
                    !(from.base == 3 && from.pointers == 0 && !from.is_array()) && !(from.base == 0 && from.pointers == 0)) {
                    // const C& given a value that is not a class object ("abc" for a const std::string&): a temporary C
                    // is made from it by a converting constructor ([dcl.init.ref], [over.ics.user]) and bound to the
                    // reference; it is destroyed after the statement.
                    Local hidden;
                    hidden.name = "__bt" + std::to_string(tmp_count++);
                    hidden.type = pt;
                    hidden.type.is_ref = false;
                    hidden.type.rref = false;
                    hidden.type.is_const = false;
                    hidden.type.pconst = false;
                    place_local(cur_locals, hidden);
                    std::vector<Node> cargs;
                    cargs.push_back(std::move(arg));
                    Node call;
                    if (!special_call(pt.struct_id, "<ctor>", node_addr(node_name(hidden.name)), std::move(cargs), &call))
                        return false;
                    stmt_pre.push_back(std::move(call));
                    if (class_needs_dtor(structs[static_cast<size_t>(pt.struct_id)]))
                        stmt_temps.push_back(Temp{hidden.name, pt.struct_id});
                    arg = node_addr(node_name(hidden.name));
                    continue;
                }
                if (!object && cur_locals != nullptr) {
                    // A reference bound to a temporary: the value goes into a hidden local first.
                    Local hidden;
                    hidden.name = "__bt" + std::to_string(tmp_count++);
                    hidden.type = pt;
                    hidden.type.is_ref = false;
                    place_local(cur_locals, hidden);
                    Node assign;
                    assign.kind = Node::Assign;
                    assign.op = TAssign;
                    assign.number = 1;
                    assign.kids.push_back(node_name(hidden.name));
                    assign.kids.push_back(std::move(arg));
                    Node stmt;
                    stmt.kind = Node::ExprStmt;
                    stmt.kids.push_back(std::move(assign));
                    stmt_pre.push_back(std::move(stmt));
                    if (hidden.type.base == 3 && hidden.type.pointers == 0 && class_needs_dtor(structs[static_cast<size_t>(hidden.type.struct_id)]))
                        stmt_temps.push_back(Temp{hidden.name, hidden.type.struct_id});
                    arg = node_addr(node_name(hidden.name));
                    continue;
                }
                Node a = node_addr(std::move(arg));
                arg = std::move(a);
            } else if (cur_locals != nullptr && wants_copy(pt, arg)) {
                Local hidden;
                hidden.name = "__tmp" + std::to_string(tmp_count++);
                hidden.type = pt;
                place_local(cur_locals, hidden);
                const Method* cm = is_rvalue(arg) && has_move_ctor(pt.struct_id) ? move_ctor(pt.struct_id) : copy_ctor(pt.struct_id);
                if (cm == nullptr) {
                    error = "this class cannot be copied";
                    return false;
                }
                Node call;
                call.kind = Node::Call;
                call.text = cm->qname;
                call.number = 1;
                call.type = cm->ret;
                call.kids.push_back(node_addr(node_name(hidden.name)));
                call.kids.push_back(node_addr(std::move(arg)));
                Node deref;
                deref.kind = Node::Unary;
                deref.op = TStar;
                deref.kids.push_back(std::move(call));
                arg = std::move(deref);
                if (class_needs_dtor(structs[static_cast<size_t>(pt.struct_id)]))
                    stmt_temps.push_back(Temp{hidden.name, pt.struct_id});
            }
        }
        return true;
    }

    // Adds or replaces a member function. A function with the same name and
    // parameter count that a base class declared is replaced; if that one was
    // virtual, this one is virtual in the same slot.
    // ?count@?$Box@H@@2HA: the decorated name of a static data member.
    std::string static_symbol(const StructDef& owner, const std::string& name, const Type& t) const {
        std::string out = "?" + name + "@" + owner.symname() + "@@2" + cxx_type(t, &structs);
        if (t.pointers > 0 && !t.is_ref) out += "EA";
        out += t.is_const ? 'B' : 'A';
        return out;
    }

    // The parameter types of a function as decorated-name text: two functions of one name are the same function
    // when this and the const-ness agree ([over.load]).
    std::string param_sig(const std::vector<Type>& params) const {
        std::string out;
        for (const Type& t : params) out += cxx_type(t, &structs);
        return out;
    }
    bool add_method(StructDef* def, int self, Method m) {
        m.owner = self;
        size_t found = def->methods.size();
        const std::string new_sig = param_sig(m.params);
        bool collide = false;
        for (size_t i = 0; i < def->methods.size(); ++i)
            if (def->methods[i].name == m.name && def->methods[i].params.size() == m.params.size()) {
                bool same = true;
                if (m.name == "<ctor>") {
                    for (size_t k = 0; k < m.params.size(); ++k)
                        if (!same_shape(def->methods[i].params[k], m.params[k])) same = false;
                } else if (m.name != "<dtor>") {
                    same = param_sig(def->methods[i].params) == new_sig && def->methods[i].is_const == m.is_const;
                }
                if (same) found = i;
                else collide = true;
            }
        if (found < def->methods.size() && def->methods[found].is_virtual) {
            m.is_virtual = true;
            m.slot = def->methods[found].slot;
        }
        if (m.is_virtual) {
            if (!def->poly) {
                if (def->base >= 0) {
                    error = "a virtual function in a class derived from a class without virtual functions is not supported";
                    return false;
                }
                Field vptr;
                vptr.name = "__vptr";
                vptr.type.base = 0;
                vptr.type.pointers = 2;
                def->fields.insert(def->fields.begin(), vptr);
                def->poly = true;
            }
            if (m.slot < 0) {
                m.slot = static_cast<int>(def->vtable.size());
                def->vtable.push_back("");
            }
        }
        m.qname = def->name + "::" + m.name + "/" + std::to_string(m.params.size());
        if (collide) m.qname += "~" + new_sig + (m.is_const ? "K" : "");
        if (m.name == "<ctor>")
            for (const Type& param : m.params) m.qname += param.is_ref ? "r" : "v";  // constructors overload by type
        if (!m.tsym.empty()) m.qname += "<" + m.tsym + ">";  // a constructor template instance
        m.symbol = cxx_method_symbol(structs, *def, m);
        if (m.is_virtual)
            def->vtable[static_cast<size_t>(m.slot)] = m.pure ? "" : m.name == "<dtor>" ? sdd_symbol(*def, m) : m.symbol;
        if (found < def->methods.size()) def->methods[found] = std::move(m);
        else def->methods.push_back(std::move(m));
        return true;
    }

    // `= expr` or `{expr}` or `{}` after a member name. The expression is parsed again, where it is
    // used, in the constructor ([class.base.init]); here its tokens are only stepped over.
    bool read_member_init(Field* field) {
        const bool braced = is(TLBrace);
        if (!braced && !accept(TAssign)) return true;
        if (is(TLBrace) && !braced) {
            // `T x = {...};` ([dcl.init.list]): the list is read again where it is used.
            field->init_at = static_cast<int>(at);
            field->init_aggr = true;
            int brace_depth = 0;
            while (!is(TEnd)) {
                const Tok k = cur().kind;
                if (k == TLParen || k == TLBrack || k == TLBrace) ++brace_depth;
                else if (k == TRParen || k == TRBrack || k == TRBrace) {
                    if (--brace_depth == 0) {
                        next();
                        break;
                    }
                }
                next();
            }
            return true;
        }
        if (braced) {
            next();
            if (accept(TRBrace)) {
                field->init_zero = true;
                return true;
            }
        }
        field->init_at = static_cast<int>(at);
        field->init_braced = braced;
        int depth = 0;
        while (!is(TEnd)) {
            const Tok k = cur().kind;
            if (k == TLParen || k == TLBrack || k == TLBrace) ++depth;
            else if (k == TRParen || k == TRBrack || k == TRBrace) {
                if (depth == 0) break;
                --depth;
            } else if (depth == 0 && (k == TSemi || (k == TComma && !braced))) break;
            next();
        }
        if (braced && !expect(TRBrace, "'}'")) return false;
        return true;
    }

    int struct_nest = 0;
    std::vector<std::pair<int, size_t>> deferred_bodies{};
    bool parse_struct_body(bool as_union, std::string name, int* out_id, bool is_class = false, int base_id = -1) {
        struct NestGuard {
            int& n;
            explicit NestGuard(int& x) : n(x) { ++n; }
            ~NestGuard() { --n; }
        } nest_guard(struct_nest);
        if (name.empty()) name = "__anon" + std::to_string(structs.size());
        StructDef def;
        struct BuildGuard {
            StructDef** slot;
            StructDef* old;
            ~BuildGuard() { *slot = old; }
        } build_guard{&building, building};
        building = &def;
        def.name = name;
        if (!pending_sym.empty()) {
            def.tmpl = inst_base_name;
            def.targs = inst_args;
        }
        def.sym_name = pending_sym;
        pending_sym.clear();
        const std::string ctor_name = inst_base_name.empty() ? name : inst_base_name;
        def.is_union = as_union;
        def.is_class = is_class;
        def.cxx = is_class;
        if (base_id >= 0) {
            const StructDef& base = structs[static_cast<size_t>(base_id)];
            def.base = base_id;
            def.cxx = true;
            def.fields = base.fields;
            def.base_fields = base.fields.size();
            def.poly = base.poly;
            def.methods = base.methods;
            // Constructors are not inherited.
            for (size_t i = def.methods.size(); i-- > 0;)
                if (def.methods[i].name == "<ctor>") def.methods.erase(def.methods.begin() + static_cast<std::ptrdiff_t>(i));
            for (Method& inherited : def.methods) inherited.body_at = 0;
            def.vtable = base.vtable;
            def.statics = base.statics;
        }
        const std::map<std::string, uint64_t> enums_before = enum_values;
        int self = -1;
        for (size_t k = 0; k < structs.size(); ++k)
            if (structs[k].incomplete && structs[k].name == name) self = static_cast<int>(k);
        if (self >= 0) {
            structs[static_cast<size_t>(self)] = def;  // the definition of a class declared before
        } else {
            structs.push_back(def);
            self = static_cast<int>(structs.size()) - 1;
        }
        if (!expect(TLBrace, "'{'")) return false;
        char access = is_class ? 'A' : 'Q';
        // typedef and using names of this class are visible to the members after them.
        struct ScopedTd {
            std::string name;
            bool had = false;
            Type old{};
        };
        std::vector<ScopedTd> scoped_tds;
        while (!is(TRBrace) && !is(TEnd)) {
            if (is(TTypedef) || (is(TIdent) && cur().text == "using" && at + 2 < tokens.size() &&
                                 tokens[at + 1].kind == TIdent && tokens[at + 2].kind == TAssign)) {
                const bool is_using = !is(TTypedef);
                next();
                std::string alias;
                Type aliased;
                bool imp = false;
                bool exp = false;
                if (is_using) {
                    alias = cur().text;
                    next();
                    next();  // =
                }
                if (!parse_type(&aliased, &imp, &exp)) return false;
                if (!is_using) {
                    // typedef T* p; typedef T& r; typedef R (*fn)(A); typedef T arr[N]; : the declarator gives the
                    // name and the rest of the type ([dcl.typedef]).
                    Declarator td;
                    if (!parse_declarator(aliased, &td)) {
                        if (error == "expected a name") error = "expected a typedef name";
                        return false;
                    }
                    if (td.is_function) {
                        error = "a typedef of a function type is not supported";
                        return false;
                    }
                    alias = td.name;
                    aliased = td.type;
                } else {
                    while (accept(TStar)) ++aliased.pointers;
                    if (accept(TAmp)) aliased.is_ref = true;  // using reference = T&;
                    else if (accept(TAndAnd)) {
                        aliased.is_ref = true;
                        if (aliased.pointers == 0) aliased.rref = true;
                    }
                }
                if (!expect(TSemi, "';'")) return false;
                def.tdefs[alias] = aliased;
                ScopedTd saved_td;
                saved_td.name = alias;
                auto old_td = typedefs.find(alias);
                if (old_td != typedefs.end()) {
                    saved_td.had = true;
                    saved_td.old = old_td->second;
                }
                scoped_tds.push_back(std::move(saved_td));
                typedefs[alias] = aliased;
                continue;
            }
            if (is(TIdent) && at + 1 < tokens.size() && tokens[at + 1].kind == TColon &&
                (cur().text == "public" || cur().text == "private" || cur().text == "protected")) {
                access = cur().text == "public" ? 'Q' : cur().text == "private" ? 'A' : 'I';
                next();
                next();
                continue;
            }
            if (is(TIdent) && cur().text == "template" && at + 1 < tokens.size() && tokens[at + 1].kind == TLt) {
                def.cxx = true;
                if (!parse_member_template(self)) return false;
                continue;
            }
            bool is_virtual = false;
            if (is(TIdent) && cur().text == "virtual") {
                is_virtual = true;
                next();
            }
            bool static_member = false;
            bool static_const = false;
            if (is(TIdent) && cur().text == "friend") {
                // friend class X; and friend struct X; ([class.friend]): access is not checked here, so nothing to record.
                // (A friend function with a body is not supported.)
                size_t k = at;
                while (k < tokens.size() && tokens[k].kind != TSemi && tokens[k].kind != TLBrace && tokens[k].kind != TEnd) ++k;
                if (k < tokens.size() && tokens[k].kind == TSemi) {
                    at = k + 1;
                    continue;
                }
            }
            if (is(TStatic)) {
                static_member = true;
                next();
                if (is(TConst)) {
                    static_const = true;
                    next();
                }
            }
            // constexpr (read as const) in front of a constructor means nothing here.
            if (is(TConst) && at + 2 < tokens.size() && tokens[at + 1].kind == TIdent && tokens[at + 1].text == ctor_name &&
                tokens[at + 2].kind == TLParen)
                next();
            const bool is_dtor = is(TTilde) && at + 2 < tokens.size() && tokens[at + 1].kind == TIdent &&
                                 tokens[at + 1].text == ctor_name && tokens[at + 2].kind == TLParen;
            const bool is_ctor = is(TIdent) && cur().text == ctor_name && at + 1 < tokens.size() &&
                                 tokens[at + 1].kind == TLParen;
            if (is_dtor || is_ctor) {
                if (is_dtor) next();
                next();
                next();
                Declarator decl;
                if (!parse_param_list(&decl)) return false;
                if (is_dtor && !decl.params.empty()) {
                    error = "a destructor takes no arguments";
                    return false;
                }
                if (is(TAssign) && at + 2 < tokens.size() &&
                    (tokens[at + 1].kind == TDefault || (tokens[at + 1].kind == TIdent && tokens[at + 1].text == "delete")) &&
                    tokens[at + 2].kind == TSemi) {
                    // `= default` and `= delete` ([dcl.fct.def.default], [dcl.fct.def.delete]). A defaulted
                    // default constructor is the one the compiler makes (it runs the member initializers); a
                    // defaulted copy constructor or destructor is what the compiler makes anyway; a deleted
                    // function is simply not declared (a call of it then finds no such function).
                    const bool want_default = tokens[at + 1].kind == TDefault;
                    next();
                    next();
                    next();
                    def.cxx = true;
                    if (want_default && is_ctor && decl.params.empty()) {
                        Method dm;
                        dm.name = "<ctor>";
                        dm.ret.base = 3;
                        dm.ret.struct_id = self;
                        dm.ret.pointers = 1;
                        dm.implicit = true;
                        dm.has_body = true;
                        dm.body_at = 1;
                        dm.access = access;
                        if (!add_method(&def, self, std::move(dm))) return false;
                    }
                    continue;
                }
                Method m;
                m.name = is_dtor ? "<dtor>" : "<ctor>";
                m.ret.base = 0;
                if (is_ctor) {
                    // A constructor returns its object, so that "new C(args)" is one call.
                    m.ret.base = 3;
                    m.ret.struct_id = self;
                    m.ret.pointers = 1;
                }
                m.params = decl.params;
                m.param_names = decl.param_names;
                m.defaults = decl.param_defaults;
                m.is_virtual = is_virtual && is_dtor;
                if (is_virtual && !is_dtor) {
                    error = "a constructor cannot be virtual";
                    return false;
                }
                m.access = access;
                if (is(TColon) && is_ctor) {
                    m.body_at = at;
                    m.has_body = true;
                    while (!is(TLBrace) && !is(TEnd) && !is(TSemi)) next();
                    if (!is(TLBrace)) {
                        error = "expected a constructor body";
                        return false;
                    }
                    if (!skip_body()) return false;
                } else if (is(TLBrace)) {
                    m.body_at = at;
                    m.has_body = true;
                    if (!skip_body()) return false;
                } else {
                    if (!expect(TSemi, "';'")) return false;
                    if (!attach_outside(&m, is_dtor ? "<dtor>" : "<ctor>")) return false;
                }
                def.cxx = true;
                if (!add_method(&def, self, std::move(m))) return false;
                continue;
            }
            {
                // `T& operator=(const T&) = default;` and `= delete;`: the compiler's own copy assignment (a
                // plain member copy) is used, or none is declared. Other operators are not read.
                size_t semi = at;
                int paren_depth = 0;
                bool has_operator = false;
                for (; semi < tokens.size() && tokens[semi].kind != TEnd; ++semi) {
                    if (tokens[semi].kind == TIdent && tokens[semi].text.compare(0, 8, "operator") == 0) has_operator = true;
                    if (tokens[semi].kind == TLParen) ++paren_depth;
                    else if (tokens[semi].kind == TRParen) --paren_depth;
                    else if (paren_depth == 0 && (tokens[semi].kind == TSemi || tokens[semi].kind == TLBrace ||
                                                  tokens[semi].kind == TRBrace))
                        break;
                }
                if (has_operator && semi >= at + 3 && semi < tokens.size() && tokens[semi].kind == TSemi &&
                    tokens[semi - 2].kind == TAssign &&
                    (tokens[semi - 1].kind == TDefault || (tokens[semi - 1].kind == TIdent && tokens[semi - 1].text == "delete"))) {
                    at = semi + 1;
                    def.cxx = true;
                    continue;
                }
            }
            Field field;
            bool imp = false;
            bool exp = false;
            if (!parse_type(&field.type, &imp, &exp)) return false;
            if (is(TSemi) && !static_member) {  // enum { E = 3 };
                next();
                continue;
            }
            if (method_ahead()) {
                Declarator decl;
                if (!parse_declarator(field.type, &decl)) return false;
                Method m;
                m.name = decl.name;
                m.ret = decl.type;
                m.params = decl.params;
                m.param_names = decl.param_names;
                m.defaults = decl.param_defaults;
                m.is_virtual = is_virtual;
                m.is_static = static_member;
                m.access = access;
                if (accept(TConst)) m.is_const = true;
                if (accept(TAssign)) {
                    if (!is(TNumber) || cur().number != 0) {
                        error = "expected = 0 for a pure virtual function";
                        return false;
                    }
                    next();
                    m.pure = true;
                    if (!m.is_virtual) {
                        error = "only a virtual function can be pure";
                        return false;
                    }
                }
                if (is(TLBrace)) {
                    m.body_at = at;
                    m.has_body = true;
                    if (!skip_body()) return false;
                } else {
                    if (!expect(TSemi, "';'")) return false;
                    if (!m.pure && !attach_outside(&m, m.name)) return false;
                }
                def.cxx = true;
                if (!add_method(&def, self, std::move(m))) return false;
                continue;
            }
            const Type base_type = field.type;
            while (accept(TStar)) ++field.type.pointers;
            if (!is(TIdent)) {
                error = "expected a field name";
                return false;
            }
            field.name = cur().text;
            next();
            if (!parse_brackets(&field.type)) return false;
            if (field.type.array_len == 0 && !static_member) {
                error = "an array member needs a length";
                return false;
            }
            if (static_member) {
                // static T name; static const T name = constant; static inline T name = value; ([class.static.data])
                for (;;) {
                    StaticMem sm;
                    sm.type = field.type;
                    sm.type.is_const = static_const || field.type.is_const;
                    def.cxx = true;
                    const bool scalar = sm.type.pointers == 0 && !sm.type.is_array() && sm.type.base != 3;
                    if (accept(TAssign)) {
                        Node init;
                        if (!parse_init(&init)) return false;
                        if (sm.type.is_const && scalar) {
                            std::vector<uint8_t> bytes;
                            if (!flatten_init(sm.type, init, &bytes, 0)) return false;
                            long long v = 0;
                            for (size_t bi = bytes.size(); bi-- > 0;) v = (v << 8) | bytes[bi];
                            const size_t nb = bytes.size();
                            // A signed char, short or int constant is sign-extended from its width (static_cast<char>(153)
                            // is -103).
                            if (nb > 0 && nb < 8 && (bytes[nb - 1] & 0x80) != 0 && !sm.type.is_unsigned && !sm.type.is_bool &&
                                (sm.type.base == 1 || sm.type.base == 2 || sm.type.base == 4))
                                v -= (1LL << (8 * nb));
                            sm.has_const = true;
                            sm.cval = v;
                        } else {
                            // static inline float s = 1.0f; static inline int a[4] = {...}; static constexpr T t[] = {...}:
                            // a variable with that first value ([dcl.inline]). Own simplification: each file that includes
                            // the class keeps its own copy (an internal symbol), as for inline functions here.
                            if (obj_needs_ctor(sm.type) || obj_needs_dtor(sm.type)) {
                                error = "an in-class initialized static member of a class with a constructor is not supported";
                                return false;
                            }
                            if (!fix_unsized(&sm.type, init)) return false;
                            sm.symbol = static_symbol(def, field.name, sm.type);
                            Global g;
                            g.name = sm.symbol;
                            g.type = sm.type;
                            g.bss = false;
                            g.is_static = true;
                            std::vector<DynLeaf> leaves;
                            if (!flatten_dyn(sm.type, init, &g, &leaves)) return false;
                            globals.push_back(g);
                            add_init_stmts(dyn_leaf_stmts(g.name, g.type, &leaves));
                        }
                    } else {
                        sm.symbol = static_symbol(def, field.name, sm.type);
                        Global g;
                        g.name = sm.symbol;
                        g.type = sm.type;
                        g.bss = true;
                        globals.push_back(g);
                    }
                    def.statics[field.name] = sm;
                    if (!accept(TComma)) break;
                    // static int a = 0, b = 0; : the next name has the type read before the first one.
                    field = Field{};
                    field.type = base_type;
                    while (accept(TStar)) ++field.type.pointers;
                    if (!is(TIdent)) {
                        error = "expected a field name";
                        return false;
                    }
                    field.name = cur().text;
                    next();
                    if (!parse_brackets(&field.type)) return false;
                }
                if (!expect(TSemi, "';'")) return false;
                continue;
            }
            for (;;) {  // float x, y, z; every name gets the type read before the first one
                if (accept(TColon)) {
                    if (!is(TNumber)) {
                        error = "expected a bitfield width";
                        return false;
                    }
                    field.type.bits = static_cast<int>(cur().number);
                    next();
                }
                if (!read_member_init(&field)) return false;
                if (field.type.base == 3 && field.type.pointers == 0 && !field.type.is_ref && field.type.struct_id >= 0 &&
                    (field.type.struct_id == self || structs[static_cast<size_t>(field.type.struct_id)].incomplete)) {
                    error = "struct field is incomplete";
                    return false;
                }
                def.fields.push_back(field);
                if (!accept(TComma)) break;
                field = Field{};
                field.type = base_type;
                while (accept(TStar)) ++field.type.pointers;
                if (!is(TIdent)) {
                    error = "expected a field name";
                    return false;
                }
                field.name = cur().text;
                next();
                if (!parse_brackets(&field.type)) return false;
            }
            if (!expect(TSemi, "';'")) return false;
        }
        if (!expect(TRBrace, "'}'")) return false;
        for (const auto& ev : enum_values) {
            auto was = enums_before.find(ev.first);
            if (was != enums_before.end() && was->second == ev.second) continue;
            StaticMem sm;
            sm.type.base = 1;
            sm.has_const = true;
            sm.cval = static_cast<long long>(ev.second);
            def.statics[ev.first] = sm;
        }
        int offset = 0;
        int align = 1;
        int cursor_bits = 0;
        size_t first_field = 0;
        if (def.base >= 0) {
            // A derived class starts after the whole base object, as the
            // MSVC layout does. The base fields keep their offsets.
            const StructDef& base = structs[static_cast<size_t>(def.base)];
            offset = base.size;
            align = base.align;
            cursor_bits = offset * 8;
            first_field = def.base_fields;
        }
        for (size_t fi = first_field; fi < def.fields.size(); ++fi) {
            Field& field = def.fields[fi];
            int field_align = align_of(field.type);
            int field_size = size_of(field.type);
            if (field_align < 1) field_align = 1;
            if (field_size < 1) field_size = 1;
            if (field.type.bits >= 0 && !as_union) {
                const int container = field_size * 8;
                if (field.type.bits > container) {
                    error = "bitfield is wider than its type";
                    return false;
                }
                if (container > 0 && (cursor_bits % container) + field.type.bits > container) {
                    cursor_bits = ((cursor_bits + container - 1) / container) * container;
                }
                field.offset = (cursor_bits / 8) & ~(field_size - 1);
                field.type.bit_off = cursor_bits % container;
                cursor_bits += field.type.bits;
                const int end_byte = (cursor_bits + 7) / 8;
                if (end_byte > offset) offset = end_byte;
                if (field_align > align) align = field_align;
                continue;
            }
            if (as_union) {
                field.offset = 0;
                if (field_size > offset) offset = field_size;
            } else {
                if (cursor_bits > offset * 8) offset = (cursor_bits + 7) / 8;
                offset = (offset + field_align - 1) & ~(field_align - 1);
                field.offset = offset;
                offset += field_size;
                cursor_bits = offset * 8;
            }
            if (field_align > align) align = field_align;
        }
        if (cursor_bits > offset * 8) offset = (cursor_bits + 7) / 8;
        def.align = align;
        def.size = (offset + align - 1) & ~(align - 1);
        // A class that has a base or a member needing construction or destruction, and declares no
        // such function itself, gets one that only runs those of its parts.
        {
            bool has_ctor = false;
            bool has_dtor = false;
            for (const Method& m : def.methods) {
                if (m.name == "<ctor>") has_ctor = true;
                if (m.name == "<dtor>" && m.owner == self) has_dtor = true;
            }
            structs[static_cast<size_t>(self)] = def;
            bool user_copy = false;
            for (const Method& m : def.methods)
                if (is_copy_param(m, self)) user_copy = true;
            bool part_copies = def.base >= 0 && class_has_copy(def.base);
            for (size_t fi = def.base_fields; fi < def.fields.size(); ++fi) {
                const Type& ft = def.fields[fi].type;
                if (ft.base == 3 && ft.pointers == 0 && !ft.is_ref && !ft.is_array() && ft.struct_id >= 0 &&
                    ft.struct_id != self && class_has_copy(ft.struct_id))
                    part_copies = true;
            }
            if (!has_ctor && class_needs_ctor(structs[static_cast<size_t>(self)])) {
                Method m;
                m.name = "<ctor>";
                m.ret.base = 3;
                m.ret.struct_id = self;
                m.ret.pointers = 1;
                m.implicit = true;
                m.has_body = true;
                m.body_at = 1;
                if (!add_method(&def, self, std::move(m))) return false;
            }
            if (!user_copy && part_copies) {
                // The implicit copy constructor: copies the base and each member.
                Method m;
                m.name = "<ctor>";
                m.ret.base = 3;
                m.ret.struct_id = self;
                m.ret.pointers = 1;
                Type self_ref;
                self_ref.base = 3;
                self_ref.struct_id = self;
                self_ref.is_ref = true;
                m.params.push_back(self_ref);
                m.param_names.push_back("__o");
                m.implicit = true;
                m.implicit_copy = true;
                m.has_body = true;
                m.body_at = 1;
                if (!add_method(&def, self, std::move(m))) return false;
            }
            if (!has_dtor && class_needs_dtor(structs[static_cast<size_t>(self)])) {
                Method m;
                m.name = "<dtor>";
                m.ret.base = 0;
                m.implicit = true;
                m.has_body = true;
                m.body_at = 1;
                if (!add_method(&def, self, std::move(m))) return false;
            }
        }
        if (def.poly) {
            def.vtable_sym = "??_7" + def.symname() + "@@6B@";
            Global table;
            table.name = def.vtable_sym;
            table.type.base = 0;
            table.type.pointers = 1;
            table.type.array_len = static_cast<int>(def.vtable.size());
            table.bss = false;
            table.vtable = true;
            table.slots = def.vtable;
            globals.push_back(std::move(table));
        }
        structs[static_cast<size_t>(self)] = def;
        *out_id = self;
        // Member function bodies are compiled now that the class is complete,
        // so a body may use members declared after it. A nested class is complete before its enclosing class is, and
        // its bodies may use the enclosing class's members declared later ([class.mem] complete-class context), so
        // they wait until the outermost class ends.
        if (struct_nest > 1 && inst_base_name.empty()) {
            for (size_t mi = 0; mi < def.methods.size(); ++mi)
                if (def.methods[mi].body_at != 0 && def.methods[mi].owner == self)
                    deferred_bodies.push_back({self, mi});
        } else {
            for (size_t mi = 0; mi < def.methods.size(); ++mi)
                if (def.methods[mi].body_at != 0 && def.methods[mi].owner == self)
                    if (!define_method(self, mi)) return false;
            if (struct_nest == 1) {
                while (!deferred_bodies.empty()) {
                    const std::vector<std::pair<int, size_t>> batch = std::move(deferred_bodies);
                    deferred_bodies.clear();
                    for (const auto& item : batch)
                        if (!define_method(item.first, item.second)) return false;
                }
            }
        }
        for (size_t k = scoped_tds.size(); k-- > 0;) {
            typedefs.erase(scoped_tds[k].name);
            if (scoped_tds[k].had) typedefs[scoped_tds[k].name] = scoped_tds[k].old;
        }
        return true;
    }

    // A member declared in a class template instance and defined outside it:
    // template <typename T> T Box<T>::get() const { ... }. The definition's parameter names are
    // bound to the instance's arguments, its parameter names are kept and its body is compiled with
    // the others. Only the primary template has such definitions here.
    bool attach_outside(Method* m, const std::string& member) {
        if (inst_base_name.empty()) return true;
        auto defs = ool_defs.find(inst_base_name);
        if (defs == ool_defs.end()) return true;
        for (int pass = 0; pass < 2; ++pass)
        for (const OolDef& ool : defs->second) {
            if (ool.member != member) continue;
            if (pass == 0) {
                if (!ool.explicit_inst || !inst_primary || ool.eargs.size() != inst_args.size()) continue;
                bool same = true;
                for (size_t ai = 0; ai < inst_args.size(); ++ai)
                    if (arg_key(ool.eargs[ai]) != arg_key(inst_args[ai])) same = false;
                if (!same) continue;
            } else if (ool.explicit_inst || ool.spec_key != inst_spec_key) {
                continue;
            }
            const auto bound = bind_params(ool.td, inst_bind_args());
            const size_t saved_at = at;
            at = ool.paren + 1;
            Declarator decl;
            bool ok = parse_param_list(&decl);
            const bool is_const = ok && accept(TConst);
            const bool match = ok && decl.params.size() == m->params.size() && is_const == m->is_const;
            const size_t body = at;
            at = saved_at;
            unbind_params(bound);
            if (!ok) return false;
            if (!match) continue;
            if (m->has_body) {
                error = "member function " + member + " is defined twice";
                return false;
            }
            m->has_body = true;
            m->body_at = body;
            m->param_names = decl.param_names;
            for (const TParam& tp : ool.td.params) m->ool_names.push_back(tp.name);
            return true;
        }
        return true;
    }

    // Compiles the body of a member function from its saved token position.
    bool define_method(int sid, size_t mi) {
        const Method m = structs[static_cast<size_t>(sid)].methods[mi];
        FuncDecl fn;
        fn.name = m.qname;
        fn.symbol = m.symbol;
        fn.ret = m.ret;
        fn.is_static = !m.out_of_class;  // a member written in its class is an inline function: an internal symbol
        Type self_ptr;
        self_ptr.base = 3;
        self_ptr.struct_id = sid;
        self_ptr.pointers = 1;
        if (!m.is_static) {
            fn.params.push_back(self_ptr);
            fn.param_names.push_back("this");
        }
        for (size_t i = 0; i < m.params.size(); ++i) {
            fn.params.push_back(m.params[i]);
            fn.param_names.push_back(i < m.param_names.size() ? m.param_names[i] : std::string());
        }
        finish_signature(&fn);
        const size_t resume = at;
        at = m.body_at;
        cur_implicit_copy = m.implicit_copy;
        // An out-of-class definition may call the class template's parameters by other names.
        TDecl alias;
        for (const std::string& n : m.ool_names) {
            TParam tp;
            tp.name = n;
            alias.params.push_back(tp);
        }
        std::vector<TArg> alias_args;
        for (size_t i = 0; i < alias.params.size() && i < inst_bind_args().size(); ++i) {
            alias.params[i].is_type = !inst_bind_args()[i].is_value;
            alias_args.push_back(inst_bind_args()[i]);
        }
        const auto alias_bound = bind_params(alias, alias_args);
        const bool ok = parse_function_body(&fn, sid, m.name == "<ctor>" ? 1 : m.name == "<dtor>" ? 2 : 0, m.implicit);
        unbind_params(alias_bound);
        at = resume;
        if (!ok) return false;
        structs[static_cast<size_t>(sid)].methods[mi].tok_body = m.body_at;
        structs[static_cast<size_t>(sid)].methods[mi].body_at = 0;
        funcs.push_back(std::move(fn));
        if (m.name == "<dtor>" && m.is_virtual) return define_scalar_dtor(sid, m);
        return true;
    }

    // void *C::`scalar deleting destructor'(unsigned flags) { this->~C(); if (flags) free(this); return this; }
    bool define_scalar_dtor(int sid, const Method& m) {
        FuncDecl fn;
        fn.name = m.qname + "/sdd";
        fn.symbol = sdd_symbol(structs[static_cast<size_t>(sid)], m);
        fn.ret.base = 0;
        fn.ret.pointers = 1;
        Type self_ptr;
        self_ptr.base = 3;
        self_ptr.struct_id = sid;
        self_ptr.pointers = 1;
        fn.params.push_back(self_ptr);
        fn.param_names.push_back("this");
        Type flag;
        flag.base = 1;
        flag.is_unsigned = true;
        fn.params.push_back(flag);
        fn.param_names.push_back("flags");
        finish_signature(&fn);
        std::vector<Local> locals;
        for (size_t p = 0; p < fn.params.size(); ++p) {
            Local local;
            local.name = fn.param_names[p];
            local.type = fn.params[p];
            place_local(&locals, local);
        }
        fn.nparams = static_cast<int>(fn.params.size());
        Node block;
        block.kind = Node::Block;
        block.text = "scope";
        Node call;
        if (!special_call(sid, "<dtor>", node_name("this"), {}, &call)) return false;
        block.kids.push_back(std::move(call));
        Node release;
        release.kind = Node::Call;
        release.text = "free";
        release.kids.push_back(node_name("this"));
        Node release_stmt;
        release_stmt.kind = Node::ExprStmt;
        release_stmt.kids.push_back(std::move(release));
        Node guard;
        guard.kind = Node::If;
        guard.kids.push_back(node_name("flags"));
        guard.kids.push_back(std::move(release_stmt));
        block.kids.push_back(std::move(guard));
        Node ret;
        ret.kind = Node::Return;
        ret.kids.push_back(node_name("this"));
        block.kids.push_back(std::move(ret));
        fn.body = std::move(block);
        fn.defined = true;
        fn.locals = std::move(locals);
        funcs.push_back(std::move(fn));
        return true;
    }

    // Parses a function body at the current token with the parameters
    // already in fn. cls is the class whose members are visible, or -1.
    // The body of a constructor (initializer list, then the block) or a destructor. A
    // constructor runs the base constructor, stores the table pointer, then each member in
    // declaration order; a destructor stores the table pointer, runs its block, then destroys the
    // members in reverse order and the base. Written from ISO/IEC 14882 [class.base.init] and
    // [class.dtor].
    bool parse_special_body(FuncDecl* fn, int cls, int special, bool implicit, std::vector<Local>* locals) {
        const StructDef def = structs[static_cast<size_t>(cls)];
        std::vector<Node> pre;
        std::vector<Node> post;
        int eh_held = 0;  // sub-objects recorded for unwinding while the constructor runs
        struct Init {
            std::string name;
            std::vector<Node> args;
        };
        std::vector<Init> inits;
        if (special == 1 && !implicit && accept(TColon)) {
            do {
                if (!is(TIdent)) {
                    error = "expected a member or base class name";
                    return false;
                }
                Init init;
                init.name = cur().text;
                next();
                if (!expect(TLParen, "'('")) return false;
                const size_t hoist_from = stmt_pre.size();
                if (!parse_call_args(&init.args)) return false;
                // Statements an argument needs before it (`p(new int(k))`: the allocation and the store) run before
                // the members are made.
                for (size_t h = hoist_from; h < stmt_pre.size(); ++h) pre.push_back(std::move(stmt_pre[h]));
                stmt_pre.resize(hoist_from);
                inits.push_back(std::move(init));
            } while (accept(TComma));
        }
        if (special == 1 && implicit && cur_implicit_copy) {
            // Copy constructor made by the compiler: the base (its copy constructor if it has one,
            // else its fields one by one), the table pointer, then each member.
            auto other_field = [&](const std::string& f) {
                Node o;
                o.kind = Node::Field;
                o.op = TDot;
                o.text = f;
                o.kids.push_back(node_name("__o"));
                return o;
            };
            auto assign_field = [&](const std::string& f) {
                Node assign;
                assign.kind = Node::Assign;
                assign.op = TAssign;
                assign.number = 1;
                assign.kids.push_back(node_this_field(f));
                assign.kids.push_back(other_field(f));
                Node stmt;
                stmt.kind = Node::ExprStmt;
                stmt.kids.push_back(std::move(assign));
                return stmt;
            };
            if (def.base >= 0 && class_has_copy(def.base)) {
                std::vector<Node> args;
                args.push_back(node_name("__o"));
                Node call;
                if (!special_call(def.base, "<ctor>", node_name("this"), std::move(args), &call)) return false;
                pre.push_back(std::move(call));
            } else {
                for (size_t fi = 0; fi < def.base_fields; ++fi)
                    if (def.fields[fi].name != "__vptr") pre.push_back(assign_field(def.fields[fi].name));
            }
            if (def.poly) pre.push_back(vptr_store_this(def.vtable_sym));
            for (size_t fi = def.base_fields; fi < def.fields.size(); ++fi) {
                const Field& field = def.fields[fi];
                if (field.name == "__vptr") continue;
                const Type& ft = field.type;
                if (ft.base == 3 && ft.pointers == 0 && !ft.is_ref && !ft.is_array() && ft.struct_id >= 0 &&
                    class_has_copy(ft.struct_id)) {
                    std::vector<Node> args;
                    args.push_back(other_field(field.name));
                    Node call;
                    if (!special_call(ft.struct_id, "<ctor>", node_addr(node_this_field(field.name)),
                                      std::move(args), &call))
                        return false;
                    pre.push_back(std::move(call));
                } else if (obj_needs_ctor(ft) && !class_has_copy(ft.struct_id)) {
                    // A member with only a non-copy constructor is copied bytewise.
                    pre.push_back(assign_field(field.name));
                } else {
                    pre.push_back(assign_field(field.name));
                }
            }
        } else if (special == 1 && inits.size() == 1 && (inits[0].name == def.name || (!def.tmpl.empty() && inits[0].name == def.tmpl))) {
            // C() : C(42) {} : a delegating constructor ([class.base.init] 6): the target constructor makes the whole
            // object (base, table pointer, members), then this body runs.
            Node call;
            if (!special_call(cls, "<ctor>", node_name("this"), std::move(inits[0].args), &call)) return false;
            pre.push_back(std::move(call));
        } else if (special == 1) {
            if (def.base >= 0 && class_needs_ctor(structs[static_cast<size_t>(def.base)])) {
                std::vector<Node> args;
                for (Init& init : inits)
                    if (init.name == structs[static_cast<size_t>(def.base)].name) args = std::move(init.args);
                Node call;
                if (!special_call(def.base, "<ctor>", node_name("this"), std::move(args), &call)) return false;
                pre.push_back(std::move(call));
                if (eh_ && class_needs_dtor(structs[static_cast<size_t>(def.base)])) {
                    eh_push_object(&pre, node_name("this"), def.base, 0, locals);
                    ++eh_held;
                }
            }
            if (def.poly) pre.push_back(vptr_store_this(def.vtable_sym));
            for (size_t fi = def.base_fields; fi < def.fields.size(); ++fi) {
                const Field& field = def.fields[fi];
                if (field.name == "__vptr") continue;
                Init* init = nullptr;
                for (Init& candidate : inits)
                    if (candidate.name == field.name) init = &candidate;
                if (init == nullptr && field_is_aggr(field)) {
                    // `float m[16] = {...};` or `P p = {1, 2};`: element by element, then the rest is zero.
                    Node list;
                    const Node* item = nullptr;
                    if (field.init_at >= 0) {
                        const size_t saved_at = at;
                        at = static_cast<size_t>(field.init_at) - (field.init_aggr ? 0 : 1);
                        const bool ok = parse_init(&list);
                        at = saved_at;
                        if (!ok) return false;
                        if (list.kind == Node::Block && list.text == "init") list = elide_braces(field.type, list);
                        item = &list;
                    }
                    if (!lower_aggr(field.type, node_this_field(field.name), item, &pre)) return false;
                    continue;
                }
                if (obj_needs_ctor(field.type)) {
                    std::vector<Node> args;
                    if (init != nullptr) args = std::move(init->args);
                    else if (field.init_at >= 0) {
                        // A default member initializer of a class member: `T x{a, b};` or `T x = e;`.
                        const size_t saved_at = at;
                        at = static_cast<size_t>(field.init_at);
                        if (field.init_aggr) ++at;  // `T x = {a, b};`: past the '{'
                        bool ok = true;
                        do {
                            Node arg;
                            ok = parse_expr(1, &arg);
                            if (ok) args.push_back(std::move(arg));
                        } while (ok && (field.init_braced || field.init_aggr) && accept(TComma));
                        at = saved_at;
                        if (!ok) return false;
                    }
                    Node call;
                    {
                        // A member made from an object of its own class that has no copy constructor: the bytes are copied.
                        Type at_arg;
                        if (args.size() == 1 && !field.type.is_array() && static_type(args[0], &at_arg) &&
                            at_arg.base == 3 && at_arg.pointers == 0 && !at_arg.is_array() &&
                            at_arg.struct_id == field.type.struct_id && !class_has_copy(field.type.struct_id)) {
                            Node assign;
                            assign.kind = Node::Assign;
                            assign.op = TAssign;
                            assign.kids.push_back(node_this_field(field.name));
                            assign.kids.push_back(std::move(args[0]));
                            Node stmt;
                            stmt.kind = Node::ExprStmt;
                            stmt.kids.push_back(std::move(assign));
                            pre.push_back(std::move(stmt));
                            continue;
                        }
                    }
                    if (!special_call(field.type.struct_id, "<ctor>", node_addr(node_this_field(field.name)),
                                      std::move(args), &call))
                        return false;
                    pre.push_back(std::move(call));
                    if (eh_ && obj_needs_dtor(field.type)) {
                        eh_push_object(&pre, node_addr(node_this_field(field.name)), field.type.struct_id, 0, locals);
                        ++eh_held;
                    }
                } else if (init == nullptr && (field.init_at >= 0 || field.init_zero)) {
                    // The default member initializer of a member the constructor does not name.
                    Node value;
                    if (field.init_at >= 0) {
                        const size_t saved_at = at;
                        at = static_cast<size_t>(field.init_at);
                        const bool ok = parse_expr(1, &value);
                        at = saved_at;
                        if (!ok) return false;
                    } else {
                        value.kind = Node::IntLit;
                        value.number = 0;
                        value.type.base = 1;
                    }
                    Node assign;
                    assign.kind = Node::Assign;
                    assign.op = TAssign;
                    assign.kids.push_back(node_this_field(field.name));
                    assign.kids.push_back(std::move(value));
                    Node stmt;
                    stmt.kind = Node::ExprStmt;
                    stmt.kids.push_back(std::move(assign));
                    pre.push_back(std::move(stmt));
                } else if (init != nullptr && init->args.empty()) {
                    // `m()`: value-initialisation, zero for a scalar or an aggregate ([dcl.init]).
                    if (!lower_aggr(field.type, node_this_field(field.name), nullptr, &pre)) return false;
                } else if (init != nullptr) {
                    if (init->args.size() != 1) {
                        error = "member " + field.name + " takes one initializer";
                        return false;
                    }
                    Node assign;
                    assign.kind = Node::Assign;
                    assign.op = TAssign;
                    assign.kids.push_back(node_this_field(field.name));
                    assign.kids.push_back(std::move(init->args[0]));
                    Node stmt;
                    stmt.kind = Node::ExprStmt;
                    stmt.kids.push_back(std::move(assign));
                    pre.push_back(std::move(stmt));
                }
            }
            for (const Init& init : inits) {
                bool known = init.name == (def.base >= 0 ? structs[static_cast<size_t>(def.base)].name : std::string());
                for (size_t fi = def.base_fields; fi < def.fields.size(); ++fi)
                    if (def.fields[fi].name == init.name) known = true;
                if (!known) {
                    error = "no member or base class named " + init.name;
                    return false;
                }
            }
        } else {
            if (def.poly) pre.push_back(vptr_store_this(def.vtable_sym));
            for (size_t fi = def.fields.size(); fi-- > def.base_fields;) {
                const Field& field = def.fields[fi];
                if (!obj_needs_dtor(field.type)) continue;
                Node call;
                if (!special_call(field.type.struct_id, "<dtor>", node_addr(node_this_field(field.name)), {}, &call))
                    return false;
                post.push_back(std::move(call));
            }
            if (def.base >= 0 && class_needs_dtor(structs[static_cast<size_t>(def.base)])) {
                Node call;
                if (!special_call(def.base, "<dtor>", node_name("this"), {}, &call)) return false;
                post.push_back(std::move(call));
            }
        }
        Node block;
        block.kind = Node::Block;
        block.text = "scope";
        for (Node& n : pre) block.kids.push_back(std::move(n));
        if (!implicit) {
            Node user;
            if (!parse_block(&user, locals)) return false;
            if (eh_held > 0 && contains_return(user)) {
                error = "a return in a constructor body is not supported with exceptions";
                return false;
            }
            block.kids.push_back(std::move(user));
        }
        for (int k = 0; k < eh_held; ++k) block.kids.push_back(eh_call("__okay_eh_pop", {}));
        for (Node& n : post) block.kids.push_back(std::move(n));
        if (special == 1) {
            Node ret;
            ret.kind = Node::Return;
            ret.kids.push_back(node_name("this"));
            block.kids.push_back(std::move(ret));
        }
        fn->body = std::move(block);
        return true;
    }

    // special is 1 for a constructor and 2 for a destructor: the parts of the object are
    // constructed before the body and destroyed after it. An implicit one has no body.
    bool parse_function_body(FuncDecl* fn, int cls, int special = 0, bool implicit = false) {
        std::vector<Local>* saved_locals = cur_locals;
        const std::vector<std::vector<DObj>> saved_dscopes = dscopes;
        const size_t saved_dbase = fn_dbase;
        const Type saved_ret = cur_ret;
        dscopes.clear();
        fn_dbase = 0;
        cur_ret = fn->ret;
        const int saved_special = cur_special;
        cur_special = special;
        const bool saved_ret_ref = cur_ret_ref;
        cur_ret_ref = fn->ret_ref;
        const std::string saved_fn = current_fn;
        const int saved_depth = scope_depth;
        const std::vector<int> saved_parent = scope_parent;
        const int saved_class = cur_class;
        scope_depth = 0;
        scope_parent.clear();
        scope_parent.push_back(0);
        cur_class = cls;
        std::vector<Local> locals;
        if (fn->sret) {
            Local hidden;
            hidden.name = "__sret";
            hidden.type.pointers = 1;
            hidden.type.base = 3;
            hidden.type.struct_id = fn->ret.struct_id;
            place_local(&locals, hidden);
            fn->sret_off = locals.back().offset;
        }
        fn->param_base = static_cast<int>(locals.size());
        for (size_t p = 0; p < fn->param_names.size(); ++p) {
            Local local;
            local.name = fn->param_names[p];
            local.type = fn->params[p];
            if (p < fn->param_ref.size() && fn->param_ref[p]) {
                local.type.pointers -= 1;
                local.type.is_ref = true;
            }
            place_local(&locals, local);
        }
        fn->nparams = static_cast<int>(fn->params.size());
        current_fn = fn->name;
        cur_locals = &locals;
        fn->line = cur().line;
        bool ok = true;
        if (special == 0) {
            ok = parse_block(&fn->body, &locals);
        } else {
            ok = parse_special_body(fn, cls, special, implicit, &locals);
        }
        fn->end_line = at > 0 ? tokens[at - 1].line : 0;
        dscopes = saved_dscopes;
        fn_dbase = saved_dbase;
        cur_ret = saved_ret;
        cur_special = saved_special;
        cur_ret_ref = saved_ret_ref;
        cur_locals = saved_locals;  // a template instance is parsed in the middle of another function
        current_fn = saved_fn;
        scope_depth = saved_depth;
        scope_parent = saved_parent;
        cur_class = saved_class;
        if (!ok) return false;
        fn->defined = true;
        fn->locals = std::move(locals);
        return true;
    }

    void finish_signature(FuncDecl* fn) {
        if (fn->ret.is_ref) {
            fn->ret_ref = true;
            fn->ret.is_ref = false;
            fn->ret.pointers += 1;
        }
        fn->param_ref.assign(fn->params.size(), false);
        for (size_t i = 0; i < fn->params.size(); ++i) {
            if (!fn->params[i].is_ref) continue;
            fn->param_ref[i] = true;
            fn->params[i].is_ref = false;
            fn->params[i].pointers += 1;
        }
        FuncType ft;
        ft.ret = fn->ret;
        ft.params = fn->params;
        ft.vararg = fn->vararg;
        fn->func_id = static_cast<int>(func_types.size());
        func_types.push_back(ft);
        fn->sret = fn->ret.base == 3 && fn->ret.pointers == 0 && !fits_register(fn->ret);
    }


    // ---- templates (ISO/IEC 14882 [temp]) ----
    // A template is kept as a range of tokens and parsed again with its parameters bound: a type
    // parameter is a typedef, a value parameter an enumerator. An instance is named
    // Box<H> (H is the decorated spelling of int) and its symbols use ?$Box@H, the public MSVC
    // decoration of a template instance; a function instance is ??$name@args@@YA<ret><params>@Z.
    struct TParam {
        std::string name;
        bool is_type = true;
        bool has_def = false;
        size_t def_at = 0;
        bool is_pack = false;
    };
    struct TDecl {
        std::vector<TParam> params;
        size_t start = 0;
        size_t end = 0;
        std::vector<Type> proto;  // function templates: parameter types, a parameter T is base 100 + index
    };
    struct TPat {
        std::string name;
        std::vector<TArg> args;
    };
    struct ClassSpec {
        TDecl td;
        std::vector<TArg> pattern;
        std::string key;  // the argument list spelled with parameters numbered, to pair definitions
    };
    struct OolDef {
        TDecl td;
        std::string member;  // a name, "<ctor>" or "<dtor>"
        size_t paren = 0;
        std::string spec_key;  // empty: the primary template; else the specialization it belongs to
        // template <> int Box<int>::get() { ... }: a member of the instance Box<int> of the primary
        // template, replacing the primary's definition for those arguments only.
        bool explicit_inst = false;
        std::vector<TArg> eargs;
    };
    struct OolStatic {
        TDecl td;
        std::string member;
        size_t init_at = 0;  // the token after '=', or 0 when there is no initializer
        std::string spec_key;
    };
    std::map<std::string, std::vector<OolStatic>> ool_statics{};
    // template <typename U> U conv(U x) { ... } declared inside a class ([temp.mem]): kept as a
    // token range with the class's own parameter values at that point, instantiated at a call.
    struct MemberTmpl {
        TDecl td;
        TDecl ctx;
        std::vector<TArg> ctx_args;
        bool is_static = false;
        std::string owner;      // the class (or class template) name the definition is written with
        bool bodyless = false;  // declared in the class, defined outside it
    };
    // template <typename U> U S::add(U x) { ... } or template <typename T> template <typename U>
    // U B<T>::add(U x) { ... }: the definition of a member function template outside its class.
    struct OolMemberTmpl {
        TDecl outer;  // the class template's parameters (empty for a plain class)
        TDecl td;     // the member template's own parameters; start/end is the definition
    };
    std::map<std::pair<std::string, std::string>, OolMemberTmpl> ool_member_tmpls{};
    std::map<std::pair<int, std::string>, MemberTmpl> member_tmpls{};
    TDecl inst_td{};
    std::vector<TPat> tpats{};
    std::map<std::string, std::vector<ClassSpec>> class_specs{};
    std::map<std::string, std::vector<OolDef>> ool_defs{};
    std::map<std::string, size_t> func_specs{};  // key of an explicit specialization -> its declaration
    const std::map<std::string, size_t>* pat_values = nullptr;  // set while a pattern is read
    bool pat_rest_ok = false;  // a class specialization's pattern may end in `Rest...`
    std::vector<TArg> inst_args{};
    std::vector<TArg> inst_spec_args{};
    std::string inst_spec_key{};
    const std::vector<TArg>& inst_bind_args() const { return inst_primary ? inst_args : inst_spec_args; }
    // The tokens from the '<' at lt to its '>', a parameter name replaced by its number, so a
    // specialization and the out-of-class definitions of its members can be matched ([temp.spec]).
    std::string pattern_key(size_t lt, const TDecl& td) const {
        std::string out;
        int depth = 0;
        for (size_t i = lt; i < tokens.size() && tokens[i].kind != TEnd; ++i) {
            if (tokens[i].kind == TLt) ++depth;
            else if (tokens[i].kind == TGt) --depth;
            std::string piece = tokens[i].kind == TIdent ? tokens[i].text : std::to_string(static_cast<int>(tokens[i].kind));
            if (tokens[i].kind == TNumber) piece += "#" + std::to_string(tokens[i].number);
            for (size_t k = 0; k < td.params.size(); ++k)
                if (tokens[i].kind == TIdent && tokens[i].text == td.params[k].name) piece = "$" + std::to_string(k);
            out += piece + " ";
            if (depth == 0) break;
        }
        return out;
    }
    bool match_loose = false;  // deducing from call arguments: a later argument may convert
    bool inst_primary = false;
    std::map<std::string, TDecl> class_templates{};
    std::map<std::string, TDecl> func_templates{};
    // Further templates of the same name (overloads), told apart by the number of arguments: one
    // with a parameter pack takes any number from its fixed parameters up, and loses to a template
    // with exactly the number of parameters ([temp.func.order], simplified).
    std::map<std::string, std::vector<TDecl>> func_template_extra{};
    const TDecl& pick_func_template(const std::string& name, size_t nargs) {
        const TDecl* best = &func_templates[name];
        bool best_exact = false;
        auto consider = [&](const TDecl& cand) {
            size_t fixed = 0;
            bool has_pack = false;
            for (const Type& pt : cand.proto) {
                if (pt.pack) has_pack = true;
                else ++fixed;
            }
            const bool fits = has_pack ? nargs >= fixed : nargs == cand.proto.size();
            if (!fits) return;
            if (!has_pack && !best_exact) {
                best = &cand;
                best_exact = true;
            } else if (!best_exact && best == &func_templates[name]) {
                best = &cand;
            }
        };
        consider(func_templates[name]);
        auto extra = func_template_extra.find(name);
        if (extra != func_template_extra.end())
            for (const TDecl& cand : extra->second) consider(cand);
        return *best;
    }
    std::string pending_sym{};
    std::string inst_base_name{};

    static std::string value_code(long long v) {
        // $0 and the value: 1..10 is one digit, 0 is A@, larger values are hex digits A-P ending in @.
        std::string out = "$0";
        if (v >= 1 && v <= 10) return out + static_cast<char>('0' + (v - 1));
        if (v == 0) return out + "A@";
        std::string hex;
        unsigned long long u = static_cast<unsigned long long>(v);
        while (u > 0) {
            hex.insert(hex.begin(), static_cast<char>('A' + (u & 15)));
            u >>= 4;
        }
        return out + hex + "@";
    }
    std::string arg_code(const TArg& a) const {
        if (a.is_pack) {  // MSVC writes the elements of a pack one after the other
            std::string out;
            for (const Type& t : a.pack) out += cxx_type(t, &structs);
            return out;
        }
        return a.is_value ? value_code(a.value) : cxx_type(a.type, &structs);
    }
    std::string arg_key(const TArg& a) const {
        if (a.is_pack) {
            std::string out = "{";
            for (const Type& t : a.pack) out += cxx_type(t, &structs) + ";";
            return out + "}";
        }
        return a.is_value ? "$" + std::to_string(a.value) : cxx_type(a.type, &structs);
    }
    // Packs in scope: typename... Ts is a list of types, a function parameter pack names the
    // parameters it was expanded to.
    std::map<std::string, std::vector<Type>> type_packs{};
    std::map<std::string, std::vector<std::string>> param_packs{};

    bool is_template_name_at(size_t i) const {
        return i + 1 < tokens.size() && tokens[i].kind == TIdent && tokens[i + 1].kind == TLt &&
               class_templates.count(tokens[i].text) > 0;
    }

    // At '<': the arguments up to '>', then the defaults. 'partial' lets a function template be
    // given only its first arguments.
    bool parse_template_args(const TDecl& td, std::vector<TArg>* out, bool partial) {
        if (!expect(TLt, "'<'")) return false;
        size_t idx = 0;
        while (!is(TGt) && !is(TEnd)) {
            if (idx >= td.params.size()) {
                error = "too many template arguments";
                return false;
            }
            if (td.params[idx].is_pack) {
                // The rest of the arguments belong to the pack; Rest... splices another pack in.
                TArg pa;
                pa.is_pack = true;
                while (!is(TGt) && !is(TEnd)) {
                    if (is(TIdent) && type_packs.count(cur().text) > 0 && at + 1 < tokens.size() &&
                        tokens[at + 1].kind == TEllipsis) {
                        for (const Type& t : type_packs[cur().text]) pa.pack.push_back(t);
                        next();
                        next();
                    } else if (pat_rest_ok && is(TIdent) && at + 2 < tokens.size() && tokens[at + 1].kind == TEllipsis &&
                               tokens[at + 2].kind == TGt && typedefs.count(cur().text) > 0 &&
                               typedefs[cur().text].base >= 100) {
                        // A specialization's pattern ends in its own pack parameter: Box<T, Rest...>.
                        pa.rest = typedefs[cur().text].base - 100;
                        next();
                        next();
                    } else {
                        TArg one;
                        if (!parse_one_arg(td.params[idx], &one)) return false;
                        pa.pack.push_back(one.type);
                    }
                    if (!accept(TComma)) break;
                }
                out->push_back(std::move(pa));
                ++idx;
                break;
            }
            TArg a;
            if (!parse_one_arg(td.params[idx], &a)) return false;
            out->push_back(std::move(a));
            ++idx;
            if (!accept(TComma)) break;
        }
        if (!expect(TGt, "'>'")) return false;
        for (; idx < td.params.size(); ++idx) {
            if (td.params[idx].is_pack) {
                TArg empty;
                empty.is_pack = true;
                out->push_back(std::move(empty));
                continue;
            }
            if (!td.params[idx].has_def) {
                if (partial) break;
                error = "too few template arguments";
                return false;
            }
            const size_t saved = at;
            at = td.params[idx].def_at;
            TArg a;
            const bool ok = parse_one_arg(td.params[idx], &a);
            at = saved;
            if (!ok) return false;
            out->push_back(std::move(a));
        }
        return true;
    }
    bool parse_one_arg(const TParam& tp, TArg* a) {
        if (tp.is_type) {
            bool imp = false;
            bool exp = false;
            if (!parse_type(&a->type, &imp, &exp)) return false;
            while (accept(TStar)) ++a->type.pointers;
            return true;
        }
        if (pat_values != nullptr && is(TIdent) && pat_values->count(cur().text) > 0 && at + 1 < tokens.size() &&
            (tokens[at + 1].kind == TComma || tokens[at + 1].kind == TGt)) {
            a->is_value = true;
            a->marker = static_cast<int>(pat_values->at(cur().text));
            next();
            return true;
        }
        Node value;
        if (!parse_expr(11, &value)) return false;
        if (value.kind != Node::IntLit) {
            error = "a template argument must be a constant";
            return false;
        }
        a->is_value = true;
        a->value = static_cast<long long>(value.number);
        return true;
    }

    struct Binding {
        std::string name;
        bool had_typedef = false;
        Type old_typedef{};
        bool had_enum = false;
        uint64_t old_enum = 0;
        bool had_pack = false;
        std::vector<Type> old_pack;
    };
    std::vector<Binding> bind_params(const TDecl& td, const std::vector<TArg>& args) {
        std::vector<Binding> saved;
        for (size_t i = 0; i < td.params.size() && i < args.size(); ++i) {
            Binding b;
            b.name = td.params[i].name;
            auto t = typedefs.find(b.name);
            if (t != typedefs.end()) {
                b.had_typedef = true;
                b.old_typedef = t->second;
            }
            auto e = enum_values.find(b.name);
            if (e != enum_values.end()) {
                b.had_enum = true;
                b.old_enum = e->second;
            }
            auto pk = type_packs.find(b.name);
            if (pk != type_packs.end()) {
                b.had_pack = true;
                b.old_pack = pk->second;
            }
            if (args[i].is_pack) {
                type_packs[b.name] = args[i].pack;
                typedefs[b.name] = Type{};
                enum_values.erase(b.name);
            } else if (args[i].is_value) {
                enum_values[b.name] = static_cast<uint64_t>(args[i].value);
                typedefs.erase(b.name);
                type_packs.erase(b.name);
            } else {
                type_packs.erase(b.name);
                typedefs[b.name] = args[i].type;
                enum_values.erase(b.name);
            }
            saved.push_back(std::move(b));
        }
        return saved;
    }
    void unbind_params(const std::vector<Binding>& saved) {
        for (size_t i = saved.size(); i-- > 0;) {
            const Binding& b = saved[i];
            typedefs.erase(b.name);
            enum_values.erase(b.name);
            if (b.had_typedef) typedefs[b.name] = b.old_typedef;
            if (b.had_enum) enum_values[b.name] = b.old_enum;
            type_packs.erase(b.name);
            if (b.had_pack) type_packs[b.name] = b.old_pack;
        }
    }

    // Deduction by matching a pattern (a parameter type or a specialization's argument) against an
    // actual type or argument; ISO/IEC 14882 [temp.deduct.type]. A template parameter is a marker
    // (base 100 + index); Node<T> is a pattern naming the template. got[i] says parameter i is bound.
    bool match_arg(const TArg& pat, const TArg& act, std::vector<TArg>* args, std::vector<bool>* got) const {
        if (pat.is_pack || act.is_pack) {
            // Box<T, Rest...> against the arguments of a pack: the leading patterns match one type
            // each and Rest takes the others ([temp.class.spec.match], [temp.deduct.type]).
            if (!pat.is_pack || !act.is_pack) return false;
            if (pat.rest < 0 ? act.pack.size() != pat.pack.size() : act.pack.size() < pat.pack.size()) return false;
            for (size_t i = 0; i < pat.pack.size(); ++i)
                if (!match_type(pat.pack[i], act.pack[i], args, got)) return false;
            if (pat.rest >= 0) {
                const size_t idx = static_cast<size_t>(pat.rest);
                if (idx >= args->size()) return false;
                TArg rest;
                rest.is_pack = true;
                rest.pack.assign(act.pack.begin() + static_cast<std::ptrdiff_t>(pat.pack.size()), act.pack.end());
                if ((*got)[idx]) return arg_key((*args)[idx]) == arg_key(rest);
                (*args)[idx] = std::move(rest);
                (*got)[idx] = true;
            }
            return true;
        }
        if (pat.is_value != act.is_value) return false;
        if (pat.is_value) {
            if (pat.marker < 0) return pat.value == act.value;
            const size_t idx = static_cast<size_t>(pat.marker);
            if (idx >= args->size()) return false;
            if ((*got)[idx]) return (*args)[idx].is_value && (*args)[idx].value == act.value;
            (*args)[idx] = act;
            (*got)[idx] = true;
            return true;
        }
        return match_type(pat.type, act.type, args, got);
    }
    bool match_type(const Type& pat, const Type& act, std::vector<TArg>* args, std::vector<bool>* got) const {
        if (pat.tpat == -2) return true;
        if (pat.tpat >= 0) {
            if (act.base != 3 || act.struct_id < 0 || act.pointers != pat.pointers) return false;
            const StructDef& sd = structs[static_cast<size_t>(act.struct_id)];
            const TPat& tp = tpats[static_cast<size_t>(pat.tpat)];
            if (sd.tmpl != tp.name || sd.targs.size() != tp.args.size()) return false;
            for (size_t i = 0; i < tp.args.size(); ++i)
                if (!match_arg(tp.args[i], sd.targs[i], args, got)) return false;
            return true;
        }
        if (pat.base >= 100) {
            const size_t idx = static_cast<size_t>(pat.base - 100);
            if (idx >= args->size() || act.pointers < pat.pointers) return false;
            Type bound = act;
            bound.pointers -= pat.pointers;
            bound.is_ref = false;
            if (bound.is_array()) return false;
            if ((*got)[idx])
                return match_loose || (!(*args)[idx].is_value && arg_key((*args)[idx]) == cxx_type(bound, &structs));
            (*args)[idx].is_value = false;
            (*args)[idx].type = bound;
            (*got)[idx] = true;
            return true;
        }
        return pat.base == act.base && pat.pointers == act.pointers && (pat.base != 3 || pat.struct_id == act.struct_id);
    }

    // Box<int>: the class instance, made the first time it is named.
    // template <typename T> int Box<T>::count = 10; gives the instance's static data member its
    // initial value; the parameter names of the definition are bound to the instance's arguments.
    bool apply_ool_statics(const std::string& name, int id) {
        auto defs = ool_statics.find(name);
        if (defs == ool_statics.end()) return true;
        for (const OolStatic& os : defs->second) {
            auto it = structs[static_cast<size_t>(id)].statics.find(os.member);
            if (os.spec_key != inst_spec_key) continue;
            if (it == structs[static_cast<size_t>(id)].statics.end() || it->second.has_const || os.init_at == 0) continue;
            const auto bound = bind_params(os.td, inst_bind_args());
            const size_t saved_at = at;
            at = os.init_at;
            Node init;
            const bool ok = parse_init(&init);
            at = saved_at;
            unbind_params(bound);
            if (!ok) return false;
            for (Global& g : globals)
                if (g.name == it->second.symbol) {
                    g.bss = false;
                    g.init_bytes.clear();
                    if (!flatten_init(g.type, init, &g.init_bytes, 0)) return false;
                }
        }
        return true;
    }

    bool instantiate_class(const std::string& name, const std::vector<TArg>& args_in, Type* out) {
        const TDecl* tdp = &class_templates[name];
        std::vector<TArg> args = args_in;
        // A specialization that matches the arguments replaces the primary template: the one with
        // the fewest parameters is the most specialized here.
        bool primary = true;
        std::string spec_key_now;
        std::vector<TArg> spec_args_now;
        {
            const ClassSpec* best = nullptr;
            std::vector<TArg> best_args;
            auto specs = class_specs.find(name);
            if (specs != class_specs.end())
                for (const ClassSpec& spec : specs->second) {
                    if (spec.pattern.size() != args.size()) continue;
                    std::vector<TArg> deduced(spec.td.params.size());
                    std::vector<bool> got(spec.td.params.size(), false);
                    bool fits = true;
                    for (size_t i = 0; i < args.size() && fits; ++i)
                        fits = match_arg(spec.pattern[i], args[i], &deduced, &got);
                    for (size_t i = 0; i < got.size() && fits; ++i) fits = got[i];
                    if (!fits) continue;
                    if (best == nullptr || spec.td.params.size() < best->td.params.size()) {
                        best = &spec;
                        best_args = deduced;
                    }
                }
            if (best != nullptr) {
                tdp = &best->td;
                primary = false;
                spec_args_now = best_args;
                spec_key_now = best->key;
            }
        }
        const TDecl& td = *tdp;
        const std::vector<TArg> bind_args = primary ? args : spec_args_now;
        std::string key = name + "<";
        std::string sym = "?$" + name + "@";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i) key += ",";
            key += arg_key(args[i]);
            sym += arg_code(args[i]);
        }
        key += ">";
        int id = find_struct(key);
        if (id < 0) {
            const size_t saved_at = at;
            const std::string saved_base = inst_base_name;
            const std::vector<TArg> saved_inst_args = inst_args;
            const bool saved_primary = inst_primary;
            const std::string saved_spec_key = inst_spec_key;
            const std::vector<TArg> saved_spec_args = inst_spec_args;
            const auto saved = bind_params(td, bind_args);
            inst_base_name = name;
            inst_args = args;
            inst_primary = primary;
            inst_spec_key = spec_key_now;
            inst_spec_args = bind_args;
            const TDecl saved_inst_td = inst_td;
            inst_td = td;
            pending_sym = sym;
            Type self;
            self.base = 3;
            self.struct_id = static_cast<int>(structs.size());
            auto old = typedefs.find(name);
            const bool had = old != typedefs.end();
            const Type old_type = had ? old->second : Type{};
            typedefs[name] = self;  // the class names itself inside its body
            at = td.start;
            bool ok = true;
            const bool as_class = is(TStruct) && cur().text == "class";
            next();
            if (is(TIdent)) next();
            if (is(TLt)) {  // the argument list of a specialization
                int angle = 0;
                while (!is(TEnd)) {
                    if (is(TLt)) ++angle;
                    else if (is(TGt) && --angle == 0) {
                        next();
                        break;
                    }
                    next();
                }
            }
            int base_id = -1;
            if (accept(TColon)) {
                while (is(TIdent) && (cur().text == "public" || cur().text == "private" ||
                                      cur().text == "protected" || cur().text == "virtual"))
                    next();
                Type bt;
                bool imp = false;
                bool exp = false;
                if (!parse_type(&bt, &imp, &exp) || bt.base != 3) {
                    if (error.empty()) error = "expected a base class";
                    ok = false;
                } else {
                    base_id = bt.struct_id;
                }
            }
            if (ok && !is(TLBrace)) {
                error = "expected a class template body";
                ok = false;
            }
            if (ok) ok = parse_struct_body(false, key, &id, as_class, base_id);
            if (ok) ok = apply_ool_statics(name, id);
            if (had) typedefs[name] = old_type;
            else typedefs.erase(name);
            inst_args = saved_inst_args;
            inst_primary = saved_primary;
            inst_spec_key = saved_spec_key;
            inst_spec_args = saved_spec_args;
            inst_td = saved_inst_td;
            inst_base_name = saved_base;
            unbind_params(saved);
            at = saved_at;
            pending_sym.clear();
            if (!ok) return false;
        }
        out->base = 3;
        out->struct_id = id;
        return true;
    }

    // template <typename T, int N = 3> ... : remember the declaration as a token range.
    // The parameters of template <...>, from after '<' up to (not including) '>'.
    bool parse_tparams(TDecl* tdp) {
        TDecl& td = *tdp;
        while (!is(TGt) && !is(TEnd)) {
            TParam tp;
            if (is(TStruct) && cur().text == "class") {
                next();
            } else if (is(TIdent) && cur().text == "typename") {
                next();
            } else {
                tp.is_type = false;
                // The parameter's type (int, unsigned, size_t, std::size_t, bool ...) is skipped; its name is the last
                // identifier before ',', '=' or '>'.
                size_t k = at;
                size_t last_ident = 0;
                bool seen_ident = false;
                while (k < tokens.size() && tokens[k].kind != TComma && tokens[k].kind != TGt && tokens[k].kind != TAssign &&
                       tokens[k].kind != TEnd) {
                    if (tokens[k].kind == TIdent) {
                        last_ident = k;
                        seen_ident = true;
                    }
                    ++k;
                }
                if (seen_ident && tokens[last_ident].text != "typename") {
                    while (at < last_ident) next();
                } else {
                    while (!is(TIdent) && !is(TComma) && !is(TGt) && !is(TEnd)) next();
                }
            }
            if (tp.is_type && accept(TEllipsis)) tp.is_pack = true;
            if (!is(TIdent)) {
                error = "expected a template parameter name";
                return false;
            }
            tp.name = cur().text;
            next();
            if (accept(TAssign)) {
                tp.has_def = true;
                tp.def_at = at;
                int depth = 0;
                while (!is(TEnd)) {
                    if (is(TLt)) ++depth;
                    else if (is(TGt)) {
                        if (depth == 0) break;
                        --depth;
                    } else if (is(TComma) && depth == 0) break;
                    next();
                }
            }
            td.params.push_back(tp);
            if (!accept(TComma)) break;
        }
        return true;
    }

    // 'headless': a member of an explicitly specialized class defined outside it, which is written
    // without the template<> prefix (int Box<char>::get() const { ... }, [temp.expl.spec]).
    bool parse_template_decl(bool headless = false) {
        TDecl td;
        if (!headless) {
            next();  // template
            if (!expect(TLt, "'<'")) return false;
        }
        if (!headless && !parse_tparams(&td)) return false;
        if (!headless && !expect(TGt, "'>'")) return false;
        // template <typename T> template <typename U> ... B<T>::f(...): the class's parameters, then
        // the member template's own ([temp.mem]).
        TDecl outer;
        bool has_outer = false;
        if (!headless && is(TIdent) && cur().text == "template" && at + 1 < tokens.size() && tokens[at + 1].kind == TLt) {
            next();
            next();
            outer = td;
            td = TDecl();
            if (!parse_tparams(&td)) return false;
            if (!expect(TGt, "'>'")) return false;
            has_outer = true;
        }
        td.start = at;
        // The declaration ends after its first top-level body or semicolon.
        size_t k = at;
        int depth = 0;
        bool body = false;
        while (k < tokens.size() && tokens[k].kind != TEnd) {
            if (tokens[k].kind == TLBrace) {
                ++depth;
                body = true;
            } else if (tokens[k].kind == TRBrace) {
                --depth;
                if (depth == 0) {
                    ++k;
                    if (k < tokens.size() && tokens[k].kind == TSemi && (tokens[td.start].kind == TStruct)) ++k;
                    break;
                }
            } else if (tokens[k].kind == TSemi && depth == 0 && !body) {
                ++k;
                break;
            }
            ++k;
        }
        td.end = k;
        if (tokens[td.start].kind == TStruct) {
            if (td.start + 1 >= tokens.size() || tokens[td.start + 1].kind != TIdent) {
                error = "expected a class template name";
                return false;
            }
            const std::string cname = tokens[td.start + 1].text;
            if (td.start + 2 < tokens.size() && tokens[td.start + 2].kind == TLt) {
                // template <> struct Box<char> { ... } or template <typename T> struct Box<T *> { ... }
                auto primary = class_templates.find(cname);
                if (primary == class_templates.end()) {
                    error = "specialization of " + cname + ", which is not a class template";
                    return false;
                }
                ClassSpec spec;
                spec.td = td;
                spec.key = pattern_key(td.start + 2, td);
                std::map<std::string, size_t> values;
                for (size_t i = 0; i < td.params.size(); ++i)
                    if (!td.params[i].is_type) values[td.params[i].name] = i;
                const auto bound = bind_params(td, make_markers(td));
                const size_t was_at = at;
                at = td.start + 2;
                pat_values = &values;
                pat_rest_ok = true;
                const bool ok = parse_template_args(primary->second, &spec.pattern, false);
                pat_rest_ok = false;
                pat_values = nullptr;
                at = was_at;
                unbind_params(bound);
                if (!ok) return false;
                class_specs[cname].push_back(std::move(spec));
                at = td.end;
                return true;
            }
            class_templates[cname] = td;
            at = td.end;
            return true;
        }
        // A function template: its name is the identifier before the first '('. The parameter
        // types are read once with each type parameter standing for itself (base 100 + index), so
        // a call can deduce them.
        {
            // template <typename T> int Box<T>::count = 10; a static data member of a class template.
            size_t cc = td.start;
            while (cc < td.end && tokens[cc].kind != TColonColon && tokens[cc].kind != TLParen &&
                   tokens[cc].kind != TLBrace)
                ++cc;
            if (cc < td.end && tokens[cc].kind != TColonColon) cc = td.end;
            if (cc > td.start + 2 && cc + 2 < td.end && tokens[cc - 1].kind == TGt && tokens[cc + 1].kind == TIdent &&
                (tokens[cc + 2].kind == TAssign || tokens[cc + 2].kind == TSemi)) {
                size_t lt = cc - 1;
                int angle = 0;
                while (lt > td.start) {
                    if (tokens[lt].kind == TGt) ++angle;
                    else if (tokens[lt].kind == TLt && --angle == 0) break;
                    --lt;
                }
                if (tokens[lt].kind == TLt && lt > td.start && tokens[lt - 1].kind == TIdent &&
                    class_templates.count(tokens[lt - 1].text) > 0) {
                    OolStatic os;
                    os.td = td;
                    os.member = tokens[cc + 1].text;
                    {
                        bool plain = (cc - 1 - lt - 1) == td.params.size() * 2 - (td.params.empty() ? 0 : 1);
                        for (size_t i = 0; plain && i < td.params.size(); ++i)
                            plain = tokens[lt + 1 + i * 2].kind == TIdent &&
                                    tokens[lt + 1 + i * 2].text == td.params[i].name;
                        if (!plain) os.spec_key = pattern_key(lt, td);
                    }
                    if (tokens[cc + 2].kind == TAssign) os.init_at = cc + 3;
                    ool_statics[tokens[lt - 1].text].push_back(std::move(os));
                    at = td.end;
                    return true;
                }
            }
        }
        size_t paren = td.start;
        while (paren < td.end && tokens[paren].kind != TLParen) ++paren;
        if (paren == td.start || paren >= td.end) {
            error = "expected a function template";
            return false;
        }
        // template <typename U> R S::name(...) { ... }: the definition of a member function template
        // declared in a class (a class template when the class's own parameters came first).
        if (paren >= td.start + 3 && tokens[paren - 1].kind == TIdent && tokens[paren - 2].kind == TColonColon) {
            std::string owner;
            if (has_outer && tokens[paren - 3].kind == TGt) {
                size_t lt = paren - 3;
                int angle = 0;
                while (lt > td.start) {
                    if (tokens[lt].kind == TGt) ++angle;
                    else if (tokens[lt].kind == TLt && --angle == 0) break;
                    --lt;
                }
                if (tokens[lt].kind == TLt && lt > td.start && tokens[lt - 1].kind == TIdent) owner = tokens[lt - 1].text;
            } else if (!has_outer && tokens[paren - 3].kind == TIdent && class_templates.count(tokens[paren - 3].text) == 0) {
                owner = tokens[paren - 3].text;
            }
            if (!owner.empty()) {
                OolMemberTmpl om;
                om.outer = outer;
                om.td = td;
                ool_member_tmpls[std::make_pair(owner, tokens[paren - 1].text)] = std::move(om);
                at = td.end;
                return true;
            }
        }
        // template <typename T> R Box<T>::member(...) { ... }: a member of a class template.
        {
            size_t m = paren - 1;
            bool dtor = false;
            if (tokens[m].kind == TIdent) {
                size_t q = m;
                if (q >= 1 && tokens[q - 1].kind == TTilde) {
                    dtor = true;
                    --q;
                }
                if (q >= 3 && tokens[q - 1].kind == TColonColon && tokens[q - 2].kind == TGt) {
                    size_t gt = q - 2;
                    size_t lt = gt;
                    int angle = 0;
                    while (lt > td.start) {
                        if (tokens[lt].kind == TGt) ++angle;
                        else if (tokens[lt].kind == TLt && --angle == 0) break;
                        --lt;
                    }
                    if (tokens[lt].kind == TLt && lt > td.start && tokens[lt - 1].kind == TIdent &&
                        class_templates.count(tokens[lt - 1].text) > 0) {
                        const std::string cname = tokens[lt - 1].text;
                        // The argument list must be the definition's own parameters, in order.
                        bool plain = (gt - lt - 1) == td.params.size() * 2 - (td.params.empty() ? 0 : 1);
                        for (size_t i = 0; plain && i < td.params.size(); ++i)
                            plain = tokens[lt + 1 + i * 2].kind == TIdent &&
                                    tokens[lt + 1 + i * 2].text == td.params[i].name;
                        OolDef ool;
                        ool.td = td;
                        if (!plain) ool.spec_key = pattern_key(lt, td);  // int Box<char>::get()
                        if (!plain && td.params.empty()) {
                            bool is_spec = false;
                            for (const ClassSpec& cs : class_specs[cname])
                                if (cs.key == ool.spec_key) is_spec = true;
                            if (!is_spec) {
                                const size_t was_at = at;
                                at = lt;
                                const bool args_ok = parse_template_args(class_templates[cname], &ool.eargs, false);
                                at = was_at;
                                if (!args_ok) return false;
                                ool.explicit_inst = true;
                            }
                        }
                        ool.member = dtor ? "<dtor>" : tokens[m].text == cname ? "<ctor>" : tokens[m].text;
                        ool.paren = paren;
                        ool_defs[cname].push_back(std::move(ool));
                        at = td.end;
                        return true;
                    }
                }
            }
        }
        if (tokens[paren - 1].kind != TIdent && tokens[paren - 1].kind != TGt) {
            error = "expected a function template";
            return false;
        }
        // template <> int twice<int>(int x) { ... } or template <> int twice(int x): an explicit
        // specialization, used instead of the instance the template would make.
        if (td.params.empty()) {
            std::string fname;
            std::vector<TArg> given_args;
            size_t erased = 0;
            if (tokens[paren - 1].kind == TGt) {
                size_t lt = paren - 1;
                int angle = 0;
                while (lt > td.start) {
                    if (tokens[lt].kind == TGt) ++angle;
                    else if (tokens[lt].kind == TLt && --angle == 0) break;
                    --lt;
                }
                if (tokens[lt].kind != TLt || lt == td.start || tokens[lt - 1].kind != TIdent) {
                    error = "expected a function template";
                    return false;
                }
                fname = tokens[lt - 1].text;
                if (func_templates.count(fname) == 0) {
                    error = "specialization of " + fname + ", which is not a function template";
                    return false;
                }
                at = lt;
                if (!parse_template_args(func_templates[fname], &given_args, true)) return false;
                erased = paren - lt;
                tokens.erase(tokens.begin() + static_cast<std::ptrdiff_t>(lt),
                             tokens.begin() + static_cast<std::ptrdiff_t>(paren));
                paren = lt;
            } else {
                fname = tokens[paren - 1].text;
                if (func_templates.count(fname) == 0) {
                    error = "specialization of " + fname + ", which is not a function template";
                    return false;
                }
            }
            td.end -= erased;
            const TDecl& primary = func_templates[fname];
            std::vector<TArg> deduced(primary.params.size());
            std::vector<bool> got(primary.params.size(), false);
            for (size_t i = 0; i < given_args.size() && i < deduced.size(); ++i) {
                deduced[i] = given_args[i];
                got[i] = true;
            }
            at = paren + 1;
            Declarator pd;
            if (!parse_param_list(&pd)) return false;
            for (size_t i = 0; i < primary.proto.size() && i < pd.params.size(); ++i) {
                Type actual = pd.params[i];
                actual.is_ref = false;
                if (!match_type(primary.proto[i], actual, &deduced, &got)) {
                    error = "specialization does not match the template " + fname;
                    return false;
                }
            }
            std::string key = fname + "<";
            for (size_t i = 0; i < deduced.size(); ++i) {
                if (!got[i]) {
                    error = "cannot deduce template argument " + primary.params[i].name;
                    return false;
                }
                if (i) key += ",";
                key += arg_key(deduced[i]);
            }
            key += ">";
            func_specs[key] = td.start;
            at = td.end;
            return true;
        }
        if (tokens[paren - 1].kind != TIdent) {
            error = "expected a function template";
            return false;
        }
        const std::string fname = tokens[paren - 1].text;
        std::vector<Binding> saved;
        std::map<std::string, size_t> values;
        for (size_t i = 0; i < td.params.size(); ++i)
            if (!td.params[i].is_type) values[td.params[i].name] = i;
        saved = bind_params(td, make_markers(td));
        const size_t resume = td.end;
        const size_t was_at = at;
        at = paren + 1;
        Declarator pd;
        pat_values = &values;
        const bool ok = parse_param_list(&pd);
        pat_values = nullptr;
        td.proto = pd.params;
        at = was_at;
        unbind_params(saved);
        if (!ok) return false;
        if (func_templates.count(fname) > 0) func_template_extra[fname].push_back(std::move(td));  // an overload
        else func_templates[fname] = td;
        at = resume;
        return true;
    }

    // In a class body, at 'template': a member function template. Its declaration is kept as a token
    // range together with the class's own template arguments at this point ([temp.mem]).
    bool parse_member_template(int self) {
        next();  // template
        if (!expect(TLt, "'<'")) return false;
        MemberTmpl mt;
        if (!parse_tparams(&mt.td)) return false;
        if (!expect(TGt, "'>'")) return false;
        mt.td.start = at;
        size_t k = at;
        int depth = 0;
        bool body = false;
        while (k < tokens.size() && tokens[k].kind != TEnd) {
            if (tokens[k].kind == TLBrace) {
                ++depth;
                body = true;
            } else if (tokens[k].kind == TRBrace) {
                if (--depth == 0) {
                    ++k;
                    break;
                }
            } else if (tokens[k].kind == TSemi && depth == 0 && !body) {
                ++k;
                break;
            }
            ++k;
        }
        mt.td.end = k;
        size_t paren = mt.td.start;
        while (paren < mt.td.end && tokens[paren].kind != TLParen) ++paren;
        if (paren == mt.td.start || paren >= mt.td.end || tokens[paren - 1].kind != TIdent) {
            error = "expected a member function template";
            return false;
        }
        const std::string mname = tokens[paren - 1].text;
        mt.is_static = tokens[mt.td.start].kind == TStatic;
        mt.bodyless = tokens[mt.td.end - 1].kind == TSemi;
        mt.owner = inst_base_name.empty() ? structs[static_cast<size_t>(self)].name : inst_base_name;
        if (!inst_base_name.empty()) {
            mt.ctx = inst_td;
            mt.ctx_args = inst_bind_args();
        }
        std::map<std::string, size_t> values;
        for (size_t i = 0; i < mt.td.params.size(); ++i)
            if (!mt.td.params[i].is_type) values[mt.td.params[i].name] = i;
        const auto saved = bind_params(mt.td, make_markers(mt.td));
        const size_t was_at = at;
        at = paren + 1;
        Declarator pd;
        pat_values = &values;
        const bool ok = parse_param_list(&pd);
        pat_values = nullptr;
        mt.td.proto = pd.params;
        at = was_at;
        unbind_params(saved);
        if (!ok) return false;
        at = mt.td.end;
        member_tmpls[std::make_pair(self, mname)] = std::move(mt);
        return true;
    }

    // The member function template name of class sid or of a base of it.
    const MemberTmpl* find_member_tmpl(int sid, const std::string& name, int* owner) const {
        while (sid >= 0) {
            auto it = member_tmpls.find(std::make_pair(sid, name));
            if (it != member_tmpls.end()) {
                *owner = sid;
                return &it->second;
            }
            sid = structs[static_cast<size_t>(sid)].base;
        }
        return nullptr;
    }

    const Method* find_ctor_instance(int sid, const std::string& tsym) const {
        for (const Method& m : structs[static_cast<size_t>(sid)].methods)
            if (m.name == "<ctor>" && m.tsym == tsym) return &m;
        return nullptr;
    }

    // A constructor template (template <typename T> S(T x) { ... }, [temp.mem]) is instantiated
    // when a constructor call has its number of arguments, is not a copy of the class, and no
    // ordinary constructor takes that many arguments. Returns the instance's tsym, or empty.
    bool ensure_ctor_template(int sid, const std::vector<Node>& args, std::string* tsym_out) {
        tsym_out->clear();
        int owner = -1;
        const StructDef& def = structs[static_cast<size_t>(sid)];
        const MemberTmpl* found = nullptr;
        for (auto it = member_tmpls.begin(); it != member_tmpls.end(); ++it)
            if (it->first.first == sid && it->first.second == it->second.owner) found = &it->second;
        (void)owner;
        if (found == nullptr || found->td.proto.size() != args.size()) return true;
        if (args.size() == 1) {
            Type at;
            if (static_type(args[0], &at) && at.base == 3 && at.pointers == 0 && !at.is_array()) {
                for (int walk = at.struct_id; walk >= 0; walk = structs[static_cast<size_t>(walk)].base)
                    if (walk == sid) return true;  // a copy
            }
        }
        for (const Method& m : def.methods)
            if (m.name == "<ctor>" && m.tsym.empty() && m.params.size() == args.size()) return true;
        const MemberTmpl mt = *found;
        std::vector<TArg> targs;
        if (!deduce_args(mt.td, mt.owner, &targs, args)) return false;
        std::string codes;
        for (const TArg& a : targs) codes += arg_code(a);
        const std::string tsym = "?$?0" + codes;
        *tsym_out = tsym;
        if (find_ctor_instance(sid, tsym) != nullptr) return true;
        const auto ctx_bound = bind_params(mt.ctx, mt.ctx_args);
        const auto bound = bind_params(mt.td, targs);
        const size_t saved_at = at;
        at = mt.td.start;
        bool ok = true;
        Method m;
        m.name = "<ctor>";
        m.ret.base = 3;
        m.ret.struct_id = sid;
        m.ret.pointers = 1;
        next();  // the class name
        next();  // (
        Declarator decl;
        ok = parse_param_list(&decl);
        if (ok) {
            m.params = decl.params;
            m.param_names = decl.param_names;
            m.defaults = decl.param_defaults;
            if (is(TColon)) {
                m.body_at = at;
                m.has_body = true;
                while (!is(TLBrace) && !is(TEnd) && !is(TSemi)) next();
                if (!is(TLBrace)) {
                    error = "expected a constructor body";
                    ok = false;
                }
            } else if (is(TLBrace)) {
                m.body_at = at;
                m.has_body = true;
            } else {
                error = "a constructor template needs a body in the class";
                ok = false;
            }
        }
        size_t mi = 0;
        if (ok) {
            m.tsym = tsym;
            ok = add_method(&structs[static_cast<size_t>(sid)], sid, std::move(m));
            const StructDef& od = structs[static_cast<size_t>(sid)];
            for (size_t i = 0; i < od.methods.size(); ++i)
                if (od.methods[i].name == "<ctor>" && od.methods[i].tsym == tsym) mi = i;
        }
        at = saved_at;
        if (ok) ok = define_method(sid, mi);
        unbind_params(bound);
        unbind_params(ctx_bound);
        return ok;
    }

    // obj.name<args>(call_args): makes the instance of a member function template the first time
    // it is called, as a member named name<args> of the class that declared the template. Its
    // symbol follows the public MSVC decoration ??$name@args@@QEAA... Returns the member's name.
    bool instantiate_member(int sid, const std::string& name, std::vector<TArg> targs,
                            const std::vector<Node>& call_args, std::string* out, int* owner_out) {
        int owner = -1;
        const MemberTmpl* found = find_member_tmpl(sid, name, &owner);
        if (found == nullptr) {
            error = "no member function template " + name;
            return false;
        }
        const MemberTmpl mt = *found;  // the map may change while the instance is compiled
        if (!deduce_args(mt.td, name, &targs, call_args)) return false;
        std::string key = name + "<";
        std::string codes;
        for (size_t i = 0; i < targs.size(); ++i) {
            if (i) key += ",";
            key += arg_key(targs[i]);
            codes += arg_code(targs[i]);
        }
        key += ">";
        *out = key;
        *owner_out = owner;
        for (const Method& m : structs[static_cast<size_t>(owner)].methods)
            if (m.name == key) return true;
        TDecl ctx_decl = mt.ctx;
        TDecl body_decl = mt.td;
        if (mt.bodyless) {
            auto def = ool_member_tmpls.find(std::make_pair(mt.owner, name));
            if (def == ool_member_tmpls.end()) {
                error = "the member function template " + name + " has no definition";
                return false;
            }
            body_decl = def->second.td;
            if (!def->second.outer.params.empty()) ctx_decl = def->second.outer;
        }
        const auto ctx_bound = bind_params(ctx_decl, mt.ctx_args);
        const auto bound = bind_params(body_decl, targs);
        const size_t saved_at = at;
        at = body_decl.start;
        bool ok = true;
        if (is(TStatic)) next();
        bool imported = false;
        bool exported = false;
        Type type;
        ok = parse_type(&type, &imported, &exported);
        if (ok && mt.bodyless && is(TIdent)) {
            // S :: or B<T> :: of the definition outside the class
            size_t q = at + 1;
            if (q < tokens.size() && tokens[q].kind == TLt) {
                int angle = 0;
                for (; q < tokens.size() && tokens[q].kind != TEnd; ++q) {
                    if (tokens[q].kind == TLt) ++angle;
                    else if (tokens[q].kind == TGt && --angle == 0) {
                        ++q;
                        break;
                    }
                }
            }
            if (q < tokens.size() && tokens[q].kind == TColonColon) at = q + 1;
        }
        Declarator decl;
        if (ok) ok = parse_declarator(type, &decl);
        if (ok && !decl.is_function) {
            error = "expected a member function template";
            ok = false;
        }
        size_t mi = 0;
        if (ok) {
            Method m;
            m.name = key;
            m.tsym = "?$" + name + "@" + codes;
            m.ret = decl.type;
            m.params = decl.params;
            m.param_names = decl.param_names;
            m.defaults = decl.param_defaults;
            m.is_static = mt.is_static;
            if (accept(TConst)) m.is_const = true;
            if (!is(TLBrace)) {
                error = "a member function template needs a body in the class";
                ok = false;
            } else {
                m.body_at = at;
                m.has_body = true;
                ok = add_method(&structs[static_cast<size_t>(owner)], owner, std::move(m));
                if (ok) {
                    const StructDef& od = structs[static_cast<size_t>(owner)];
                    for (size_t i = 0; i < od.methods.size(); ++i)
                        if (od.methods[i].name == key) mi = i;
                }
            }
        }
        at = saved_at;
        if (ok) ok = define_method(owner, mi);
        unbind_params(bound);
        unbind_params(ctx_bound);
        return ok;
    }

    // Each type parameter standing for itself (base 100 + index), each value parameter for 0.
    static std::vector<TArg> make_markers(const TDecl& td) {
        std::vector<TArg> markers;
        for (size_t i = 0; i < td.params.size(); ++i) {
            TArg m;
            if (td.params[i].is_type) {
                m.type.base = static_cast<int>(100 + i);
            } else {
                m.is_value = true;
                m.value = 0;
            }
            markers.push_back(m);
        }
        return markers;
    }

    // Fills the template arguments that were not given from the types of the call arguments
    // ([temp.deduct.call]); args holds the explicitly given ones on entry.
    bool deduce_args(const TDecl& td, const std::string& name, std::vector<TArg>* args_io,
                     const std::vector<Node>& call_args) {
        std::vector<TArg>& args = *args_io;
        std::vector<bool> given(td.params.size(), false);
        for (size_t i = 0; i < args.size(); ++i) given[i] = true;
        const std::vector<bool> explicit_given = given;
        args.resize(td.params.size());
        for (size_t i = 0; i < td.proto.size() && i < call_args.size() + 1; ++i) {
            const Type& pt = td.proto[i];
            if (pt.pack && pt.tpat < 0 && pt.base >= 100) {
                // Rest... rest takes every remaining argument ([temp.deduct.call], [temp.variadic]).
                const size_t pi = static_cast<size_t>(pt.base - 100);
                if (!explicit_given[pi]) {
                    TArg pa;
                    pa.is_pack = true;
                    for (size_t j = i; j < call_args.size(); ++j) {
                        Type t;
                        if (!static_type(call_args[j], &t)) {
                            error = "cannot deduce template arguments of " + name;
                            return false;
                        }
                        if (t.is_array()) {
                            t = peel_array(t);
                            t.pointers += 1;
                            t.array_len = -1;
                        }
                        t.is_ref = false;
                        pa.pack.push_back(t);
                    }
                    args[pi] = std::move(pa);
                    given[pi] = true;
                }
                break;
            }
            if (i >= call_args.size()) break;
            if (pt.tpat == -2 || (pt.base < 100 && pt.tpat < 0)) continue;
            if (pt.tpat < 0 && explicit_given[static_cast<size_t>(pt.base - 100)]) continue;
            Type at_type;
            if (!static_type(call_args[i], &at_type)) {
                error = "cannot deduce template arguments of " + name;
                return false;
            }
            if (at_type.is_array()) {
                at_type = peel_array(at_type);
                at_type.pointers += 1;
                at_type.array_len = -1;
            }
            at_type.is_ref = false;
            match_loose = true;
            const bool fits = match_type(pt, at_type, &args, &given);
            match_loose = false;
            if (!fits) {
                error = "cannot deduce template arguments of " + name + " from argument " + std::to_string(i + 1);
                return false;
            }
        }
        for (size_t i = 0; i < td.params.size(); ++i)
            if (!given[i] && td.params[i].is_pack) {
                args[i].is_pack = true;  // a pack with no arguments
                given[i] = true;
            }
        for (size_t i = 0; i < td.params.size(); ++i)
            if (!given[i]) {
                error = "cannot deduce template argument " + td.params[i].name;
                return false;
            }
        return true;
    }

    // name<args>(...) or name(...): the instance of a function template, made the first time it is
    // called. Returns its name through *out.
    bool instantiate_function(const std::string& name, std::vector<TArg> args, const std::vector<Node>& call_args,
                              std::string* out) {
        const TDecl& td = pick_func_template(name, call_args.size());
        if (!deduce_args(td, name, &args, call_args)) return false;
        std::string key = name + "<";
        std::string codes;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i) key += ",";
            key += arg_key(args[i]);
            codes += arg_code(args[i]);
        }
        key += ">";
        *out = key;
        for (const FuncDecl& fn : funcs)
            if (fn.name == key) return true;
        if (in_instance.count(key)) return true;  // a call inside its own body
        in_instance.insert(key);
        const size_t saved_at = at;
        // An explicit specialization is an ordinary function: no parameters are bound.
        const auto spec = func_specs.find(key);
        const auto saved = bind_params(td, spec != func_specs.end() ? std::vector<TArg>() : args);
        at = spec != func_specs.end() ? spec->second : td.start;
        bool imported = false;
        bool exported = false;
        Type type;
        bool ok = parse_type(&type, &imported, &exported);
        Declarator decl;
        if (ok) ok = parse_declarator(type, &decl);
        if (ok && !decl.is_function) {
            error = "expected a function template";
            ok = false;
        }
        if (ok) {
            FuncDecl fn;
            fn.name = key;
            fn.symbol = "??$" + name + "@" + codes + "@@YA" + cxx_type(decl.type, &structs);
            if (decl.params.empty()) fn.symbol += "X";
            else {
                for (const Type& param : decl.params) fn.symbol += cxx_type(param, &structs);
                fn.symbol += "@";
            }
            fn.symbol += "Z";
            fn.ret = decl.type;
            fn.params = decl.params;
            fn.param_names = decl.param_names;
            fn.vararg = decl.vararg;
            finish_signature(&fn);
            if (!parse_function_body(&fn, -1)) ok = false;
            else funcs.push_back(std::move(fn));
        }
        unbind_params(saved);
        at = saved_at;
        in_instance.erase(key);
        return ok;
    }
    std::set<std::string> in_instance{};


    // ---- exceptions (ISO/IEC 14882 [except]; not the MSVC C++ exception model) ----
    // A stack of records in the runtime (runtime/eh.c), the same scheme as tools/okaycc: an object
    // with a destructor pushes a record when it is built and pops it when it is destroyed; a try
    // block pushes a frame holding the registers saved by __okay_setjmp; throw copies the object into
    // runtime memory and unwinds the records, destroying objects, until a try frame is entered; its
    // handlers test the type name and continue the unwinding when none fits. It does not use .pdata
    // and cannot interoperate with code that throws through SEH or __CxxFrameHandler.
    bool eh_ = false;
    int eh_count = 0;

    Node eh_call(const std::string& fn, std::vector<Node> args) const {
        Node call;
        call.kind = Node::Call;
        call.text = fn;
        for (Node& a : args) call.kids.push_back(std::move(a));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(call));
        return stmt;
    }
    Node eh_call_value(const std::string& fn, std::vector<Node> args, const Type& ret) const {
        Node call;
        call.kind = Node::Call;
        call.text = fn;
        call.type = ret;
        for (Node& a : args) call.kids.push_back(std::move(a));
        return call;
    }
    static Node int_node(long long v) {
        Node n;
        n.kind = Node::IntLit;
        n.number = static_cast<uint64_t>(v);
        n.type.base = 1;
        return n;
    }
    static Type void_ptr() {
        Type t;
        t.base = 0;
        t.pointers = 1;
        return t;
    }
    static Node cast_node(const Type& t, Node inner) {
        Node c;
        c.kind = Node::Unary;
        c.type = t;
        c.kids.push_back(std::move(inner));
        return c;
    }
    // A hidden record (14 pointers) in the current function; returns its name.
    std::string eh_record_local(std::vector<Local>* locals) {
        Local rec;
        rec.name = "__eh" + std::to_string(eh_count++);
        rec.type = void_ptr();
        rec.type.array_len = 16;
        place_local(locals, rec);
        return rec.name;
    }
    Node rec_slot(const std::string& rec, int i) const {
        Node ix;
        ix.kind = Node::Index;
        ix.kids.push_back(node_name(rec));
        ix.kids.push_back(int_node(i));
        return ix;
    }
    Node rec_store(const std::string& rec, int i, Node value) const {
        Node a;
        a.kind = Node::Assign;
        a.op = TAssign;
        a.kids.push_back(rec_slot(rec, i));
        a.kids.push_back(cast_node(void_ptr(), std::move(value)));
        Node st;
        st.kind = Node::ExprStmt;
        st.kids.push_back(std::move(a));
        return st;
    }
    // After an object of class sid is built (count elements when count > 0): record it so that
    // unwinding destroys it. The nodes go to 'out'.
    bool eh_push_object(std::vector<Node>* out, Node obj_addr, int sid, int count, std::vector<Local>* locals) {
        if (!eh_ || locals == nullptr) return true;
        const Method* dm = find_method(sid, "<dtor>", 0);
        if (dm == nullptr) return true;
        const std::string rec = eh_record_local(locals);
        out->push_back(rec_store(rec, 1, int_node(count > 0 ? 1 : 0)));
        out->push_back(rec_store(rec, 2, std::move(obj_addr)));
        out->push_back(rec_store(rec, 3, node_name(dm->qname)));
        if (count > 0) {
            out->push_back(rec_store(rec, 4, int_node(count)));
            Type elem;
            elem.base = 3;
            elem.struct_id = sid;
            out->push_back(rec_store(rec, 5, int_node(size_of(elem))));
        }
        out->push_back(eh_call("__okay_eh_push", {node_name(rec)}));
        return true;
    }
    // The names a thrown type can be caught as: a class and then its bases, or the decorated type.
    std::string eh_type_names(const Type& t) const {
        if (t.base == 3 && t.pointers == 0 && t.struct_id >= 0) {
            std::string out;
            for (int walk = t.struct_id; walk >= 0; walk = structs[static_cast<size_t>(walk)].base) {
                Type one;
                one.base = 3;
                one.struct_id = walk;
                out += cxx_type(one, &structs) + "\n";
            }
            return out;
        }
        Type plain = t;
        plain.is_ref = false;
        plain.is_const = false;
        return cxx_type(plain, &structs);
    }
    void declare_eh_builtins() {
        auto add = [&](const std::string& name, Type ret, std::vector<Type> params) {
            for (const FuncDecl& fn : funcs)
                if (fn.name == name) return;
            FuncDecl fn;
            fn.name = name;
            fn.ret = ret;
            fn.params = params;
            fn.param_names.assign(params.size(), "");
            finish_signature(&fn);
            funcs.push_back(std::move(fn));
        };
        Type v;
        v.base = 0;
        Type i;
        i.base = 1;
        Type p = void_ptr();
        Type cp;
        cp.base = 2;
        cp.pointers = 1;
        add("__okay_eh_push", v, {p});
        add("__okay_eh_pop", v, {});
        add("__okay_eh_begin", v, {p});
        add("__okay_eh_end", v, {});
        add("__okay_eh_match", i, {cp});
        add("__okay_eh_obj", p, {});
        add("__okay_eh_resume", v, {});
        add("__okay_eh_rethrow", v, {});
        add("__okay_throw", v, {p, i, cp, p});
        add("__okay_eh_alloc", p, {i});
        add("__okay_throw_built", v, {p, i, cp, p});
    }

    static bool contains_return(const Node& n) {
        if (n.kind == Node::Return) return true;
        for (const Node& k : n.kids)
            if (contains_return(k)) return true;
        return false;
    }

    // try { } catch (T e) { } catch (...) { }
    bool parse_try(Node* out, std::vector<Local>* locals, int stmt_line) {
        next();  // try
        const std::string tr = eh_record_local(locals);
        Node block;
        block.kind = Node::Block;
        block.line = stmt_line;
        block.kids.push_back(rec_store(tr, 1, int_node(2)));
        block.kids.push_back(eh_call("__okay_eh_push", {node_name(tr)}));
        dscopes.emplace_back();
        DObj frame;
        frame.kind = 1;
        dscopes.back().push_back(frame);
        Node body;
        const bool body_ok = parse_block(&body, locals);
        dscopes.pop_back();
        if (!body_ok) return false;
        body.kids.push_back(eh_call("__okay_eh_pop", {}));
        struct Handler {
            bool all = false;
            std::string name;
            Node block;
        };
        std::vector<Handler> handlers;
        if (!(is(TIdent) && cur().text == "catch")) {
            error = "a try block needs a handler";
            return false;
        }
        while (is(TIdent) && cur().text == "catch") {
            next();
            if (!expect(TLParen, "'('")) return false;
            Handler h;
            dscopes.emplace_back();
            DObj hv;
            hv.kind = 2;
            dscopes.back().push_back(hv);
            Node hb;
            hb.kind = Node::Block;
            const std::string cr = eh_record_local(locals);
            hb.kids.push_back(rec_store(cr, 1, int_node(3)));
            hb.kids.push_back(eh_call("__okay_eh_begin", {node_name(cr)}));
            if (accept(TEllipsis)) {
                h.all = true;
            } else {
                accept(TConst);
                Type t;
                bool imp = false;
                bool exp = false;
                if (!parse_type(&t, &imp, &exp)) return false;
                accept(TConst);
                while (accept(TStar)) ++t.pointers;
                if (accept(TAmp)) t.is_ref = true;
                h.name = eh_type_names(t);
                if (!h.name.empty() && h.name.back() == '\n') h.name.pop_back();
                // Only the first name (the class itself) is the handler's type.
                const size_t nl = h.name.find('\n');
                if (nl != std::string::npos) h.name = h.name.substr(0, nl);
                if (is(TIdent)) {
                    Local var;
                    var.name = cur().text;
                    var.type = t;
                    next();
                    place_local(locals, var);
                    Type ptr = t;
                    ptr.is_ref = false;
                    ptr.pointers += 1;
                    Node obj = cast_node(ptr, eh_call_value("__okay_eh_obj", {}, void_ptr()));
                    Node deref;
                    deref.kind = Node::Unary;
                    deref.op = TStar;
                    deref.kids.push_back(std::move(obj));
                    Node assign;
                    assign.kind = Node::Assign;
                    assign.op = TAssign;
                    assign.number = 1;
                    assign.kids.push_back(node_name(var.name));
                    assign.kids.push_back(std::move(deref));
                    Node st;
                    st.kind = Node::ExprStmt;
                    st.kids.push_back(std::move(assign));
                    hb.kids.push_back(std::move(st));
                    if (obj_needs_dtor(t) && !t.is_ref) {
                        std::vector<Node> rec_nodes;
                        eh_push_object(&rec_nodes, node_addr(node_name(var.name)), t.struct_id, 0, locals);
                        for (Node& n : rec_nodes) hb.kids.push_back(std::move(n));
                        DObj obj_d;
                        obj_d.name = var.name;
                        obj_d.sid = t.struct_id;
                        dscopes.back().push_back(std::move(obj_d));
                    }
                }
            }
            if (!expect(TRParen, "')'")) return false;
            Node hbody;
            if (!parse_block(&hbody, locals)) return false;
            hb.kids.push_back(std::move(hbody));
            std::vector<Node> dt;
            if (!dtor_nodes(dscopes.size() - 1, &dt)) return false;
            for (Node& n : dt) hb.kids.push_back(std::move(n));
            dscopes.pop_back();
            h.block = std::move(hb);
            handlers.push_back(std::move(h));
        }
        // The chain: the first handler whose type fits, else keep unwinding.
        Node chain = eh_call("__okay_eh_resume", {});
        for (size_t i = handlers.size(); i-- > 0;) {
            Node iff;
            iff.kind = Node::If;
            if (handlers[i].all) {
                iff.kids.push_back(int_node(1));
            } else {
                Node str;
                str.kind = Node::StringLit;
                str.text = handlers[i].name;
                str.type.pointers = 1;
                str.type.base = 2;
                Type ir;
                ir.base = 1;
                iff.kids.push_back(eh_call_value("__okay_eh_match", {std::move(str)}, ir));
            }
            iff.kids.push_back(std::move(handlers[i].block));
            iff.kids.push_back(std::move(chain));
            chain = std::move(iff);
        }
        Node top;
        top.kind = Node::If;
        {
            Type ir;
            ir.base = 1;
            Node sj = eh_call_value("__okay_setjmp", {node_addr(rec_slot(tr, 6))}, ir);
            Node not_c;
            not_c.kind = Node::Unary;
            not_c.op = TNot;
            not_c.kids.push_back(std::move(sj));
            top.kids.push_back(std::move(not_c));
        }
        top.kids.push_back(std::move(body));
        top.kids.push_back(std::move(chain));
        block.kids.push_back(std::move(top));
        *out = std::move(block);
        return true;
    }

    // throw e;  throw;
    bool parse_throw(Node* out, std::vector<Local>* locals) {
        next();  // throw
        if (accept(TSemi)) {
            *out = eh_call("__okay_eh_rethrow", {});
            return true;
        }
        Node value;
        if (!parse_expr(0, &value)) return false;
        if (!expect(TSemi, "';'")) return false;
        return build_throw(std::move(value), locals, out);
    }

    // The statements that throw 'value': the exception object is a copy of it ([except.throw]).
    // A named class object with a copy constructor is copied by that constructor into storage from
    // the runtime; anything else is copied byte by byte (a temporary is built where it is used, as
    // C++17 requires, so it is not copied twice).
    bool build_throw(Node value, std::vector<Local>* locals, Node* out, bool force_prvalue = false) {
        Type t;
        if (!static_type(value, &t)) {
            error = "cannot tell the type of the thrown value";
            return false;
        }
        if (t.is_array()) {
            t = peel_array(t);
            t.pointers += 1;
            t.array_len = -1;
        }
        t.is_ref = false;
        Node block;
        block.kind = Node::Block;
        const bool named = value.kind == Node::Name || value.kind == Node::Field || value.kind == Node::Index ||
                           (value.kind == Node::Unary && value.op == TStar);
        const bool is_obj = t.base == 3 && t.pointers == 0 && t.struct_id >= 0;
        Node str;
        str.kind = Node::StringLit;
        str.text = eh_type_names(t);
        str.type.pointers = 1;
        str.type.base = 2;
        Node dtor = int_node(0);
        if (is_obj) {
            const Method* dm = find_method(t.struct_id, "<dtor>", 0);
            if (dm != nullptr) dtor = node_name(dm->qname);
        }
        // A temporary made by C(args) (hidden name __bt) is the exception object itself: no copy.
        const bool prvalue = force_prvalue || (value.kind == Node::Name && value.text.compare(0, 4, "__bt") == 0);
        if (named && !prvalue && is_obj && class_has_copy(t.struct_id)) {
            // __okay_eh_alloc, then the copy constructor builds the object in that storage.
            Type obj_ptr = t;
            obj_ptr.pointers = 1;
            Local mem;
            mem.name = "__thm" + std::to_string(eh_count++);
            mem.type = obj_ptr;
            place_local(locals, mem);
            Node assign;
            assign.kind = Node::Assign;
            assign.op = TAssign;
            assign.number = 1;
            assign.kids.push_back(node_name(mem.name));
            assign.kids.push_back(cast_node(obj_ptr, eh_call_value("__okay_eh_alloc", {int_node(std::max(1, size_of(t)))}, void_ptr())));
            Node st;
            st.kind = Node::ExprStmt;
            st.kids.push_back(std::move(assign));
            block.kids.push_back(std::move(st));
            Node ctor_call;
            std::vector<Node> ctor_args;
            ctor_args.push_back(std::move(value));
            if (!special_call(t.struct_id, "<ctor>", node_name(mem.name), std::move(ctor_args), &ctor_call)) return false;
            block.kids.push_back(std::move(ctor_call));
            block.kids.push_back(eh_call("__okay_throw_built", {cast_node(void_ptr(), node_name(mem.name)),
                                                                int_node(std::max(1, size_of(t))), std::move(str),
                                                                cast_node(void_ptr(), std::move(dtor))}));
            *out = std::move(block);
            return true;
        }
        // The object to copy: an object that has a name is used where it is, anything else goes
        // into a hidden local first.
        Node addr;
        if (named) {
            addr = node_addr(std::move(value));
        } else {
            Local tmp;
            tmp.name = "__th" + std::to_string(eh_count++);
            tmp.type = t;
            place_local(locals, tmp);
            Node assign;
            assign.kind = Node::Assign;
            assign.op = TAssign;
            assign.number = 1;
            assign.kids.push_back(node_name(tmp.name));
            assign.kids.push_back(std::move(value));
            Node st;
            st.kind = Node::ExprStmt;
            st.kids.push_back(std::move(assign));
            block.kids.push_back(std::move(st));
            addr = node_addr(node_name(tmp.name));
        }
        block.kids.push_back(eh_call("__okay_throw", {cast_node(void_ptr(), std::move(addr)),
                                                      int_node(std::max(1, size_of(t))), std::move(str),
                                                      cast_node(void_ptr(), std::move(dtor))}));
        *out = std::move(block);
        return true;
    }

    // throw as an expression (c ? 1 : throw E(); [expr.throw]): a call of a small function made once
    // per thrown type that throws its argument. A class object is passed by reference, so the copy
    // that becomes the exception object is made once.
    bool ensure_throw_helper(const Type& t, bool prvalue, std::string* name) {
        const bool is_obj = t.base == 3 && t.pointers == 0 && t.struct_id >= 0;
        *name = "__throwx_" + std::to_string(t.base) + "_" + std::to_string(t.struct_id) + "_" +
                std::to_string(t.pointers) + (t.is_unsigned ? "u" : "") + (prvalue ? "p" : "");
        for (const FuncDecl& known : funcs)
            if (known.name == *name) return true;
        FuncDecl fn;
        fn.name = *name;
        fn.ret.base = 0;
        Type pt = t;
        if (is_obj) pt.is_ref = true;
        fn.params.push_back(pt);
        fn.param_names.push_back("v");
        finish_signature(&fn);
        std::vector<Local> locals;
        Local lv;
        lv.name = "v";
        lv.type = fn.params[0];
        if (fn.param_ref[0]) {
            lv.type.pointers -= 1;
            lv.type.is_ref = true;
        }
        const int saved_depth = scope_depth;
        std::vector<Local>* saved_locals = cur_locals;
        scope_depth = 0;
        place_local(&locals, lv);
        cur_locals = &locals;
        Node body;
        body.kind = Node::Block;
        Node th;
        const bool ok = build_throw(node_name("v"), &locals, &th, prvalue);
        cur_locals = saved_locals;
        scope_depth = saved_depth;
        if (!ok) return false;
        body.kids.push_back(std::move(th));
        fn.body = std::move(body);
        fn.locals = std::move(locals);
        fn.nparams = static_cast<int>(fn.params.size());
        fn.defined = true;
        funcs.push_back(std::move(fn));
        return true;
    }

    // A variable with static storage whose initializer is not a constant expression ([basic.start.static],
    // [basic.start.dynamic]): flatten_init keeps the constant parts in the bytes and records each part that is not a
    // constant (a call, a variable) as a leaf: its offset, its type and its expression. The leaves become stores that
    // run before main (__okay_init) or, for a static local, the first time its declaration is reached.
    std::vector<Node> dyn_leaf_stmts(const std::string& name, const Type& whole, std::vector<DynLeaf>* leaves) {
        std::vector<Node> out;
        for (DynLeaf& leaf : *leaves) {
            Node target;
            if (leaf.at == 0 && !whole.is_array() && leaf.type.base == whole.base && leaf.type.pointers == whole.pointers &&
                leaf.type.struct_id == whole.struct_id) {
                target = node_name(name);
            } else {
                // *(T *)((char *)&name + at)
                Node bytes;
                bytes.kind = Node::Unary;
                bytes.type.base = 2;
                bytes.type.pointers = 1;
                bytes.kids.push_back(node_addr(node_name(name)));
                Node off;
                off.kind = Node::IntLit;
                off.type.base = 1;
                off.number = static_cast<uint64_t>(leaf.at);
                Node sum;
                sum.kind = Node::Binary;
                sum.op = TPlus;
                sum.kids.push_back(std::move(bytes));
                sum.kids.push_back(std::move(off));
                Node typed;
                typed.kind = Node::Unary;
                typed.type = leaf.type;
                typed.type.is_const = false;
                typed.type.pointers += 1;
                typed.kids.push_back(std::move(sum));
                target.kind = Node::Unary;
                target.op = TStar;
                target.kids.push_back(std::move(typed));
            }
            Node assign;
            assign.kind = Node::Assign;
            assign.op = TAssign;
            assign.number = 1;
            assign.kids.push_back(std::move(target));
            assign.kids.push_back(std::move(leaf.value));
            Node stmt;
            stmt.kind = Node::ExprStmt;
            stmt.kids.push_back(std::move(assign));
            out.push_back(std::move(stmt));
        }
        leaves->clear();
        return out;
    }
    // flatten_init with leaves allowed: false only for a real error.
    bool flatten_dyn(const Type& type, const Node& init, Global* g, std::vector<DynLeaf>* leaves) {
        init_relocs = &g->addr_relocs;
        dyn_leaves = leaves;
        const bool ok = flatten_init(type, init, &g->init_bytes, 0);
        dyn_leaves = nullptr;
        init_relocs = nullptr;
        return ok;
    }
    void add_init_stmts(std::vector<Node> stmts) {
        if (stmts.empty()) return;
        init_body.kind = Node::Block;
        for (Node& n : stmts) init_body.kids.push_back(std::move(n));
        init_used = true;
    }

    // The constructor calls of the global objects, in declaration order, go into __okay_init; the
    // destructors (globals, then static locals, latest first) into __okay_fini. main (or entry)
    // calls them. Written from ISO/IEC 14882 [basic.start.init] and [basic.start.term].
    bool register_global_object(const std::string& name, const Type& type, std::vector<Node> args,
                                Node* array_init = nullptr) {
        std::vector<Local>* saved_locals = cur_locals;
        const int saved_depth = scope_depth;
        cur_locals = &init_locals;
        scope_depth = 0;
        const int sid = type.struct_id;
        bool ok = true;
        if (array_init != nullptr && ok) {
            std::vector<Node> calls;
            ok = array_init_calls(name, type, *array_init, &calls);
            if (ok) {
                for (Node& c : calls) init_body.kids.push_back(std::move(c));
                init_body.kind = Node::Block;
                init_used = true;
            }
        } else if (obj_needs_ctor(type)) {
            Node call;
            if (type.is_array()) {
                if (!type.more.empty() || !args.empty()) {
                    error = "a global array of objects needs one dimension and no arguments";
                    ok = false;
                } else {
                    ok = element_loop(name, type.array_len, false, sid, "<ctor>", &call);
                }
            } else {
                ok = special_call(sid, "<ctor>", node_addr(node_name(name)), std::move(args), &call);
            }
            if (ok) {
                if (init_body.kind != Node::Block) init_body.kind = Node::Block;
                init_body.kids.push_back(std::move(call));
                init_used = true;
            }
        }
        cur_locals = saved_locals;
        scope_depth = saved_depth;
        if (ok && obj_needs_dtor(type)) {
            GlobalObj g;
            g.name = name;
            g.sid = sid;
            g.count = type.is_array() ? type.array_len : 0;
            global_objs.push_back(std::move(g));
        }
        return ok;
    }

    // Wraps every return of fn so that __okay_fini runs after the value is computed.
    void rewrite_returns(Node& n, FuncDecl& fn) {
        if (n.kind == Node::Return) {
            Node call;
            call.kind = Node::Call;
            call.text = "__okay_fini";
            Node call_stmt;
            call_stmt.kind = Node::ExprStmt;
            call_stmt.kids.push_back(std::move(call));
            Node block;
            block.kind = Node::Block;
            block.line = n.line;
            if (!n.kids.empty()) {
                Local hidden;
                hidden.name = "__rv" + std::to_string(rv_count++);
                hidden.type = fn.ret;
                const int saved_depth = scope_depth;
                scope_depth = 0;
                place_local(&fn.locals, hidden);
                scope_depth = saved_depth;
                Node assign;
                assign.kind = Node::Assign;
                assign.op = TAssign;
                assign.number = 1;
                assign.kids.push_back(node_name(hidden.name));
                assign.kids.push_back(std::move(n.kids[0]));
                Node assign_stmt;
                assign_stmt.kind = Node::ExprStmt;
                assign_stmt.kids.push_back(std::move(assign));
                block.kids.push_back(std::move(assign_stmt));
                block.kids.push_back(std::move(call_stmt));
                Node ret;
                ret.kind = Node::Return;
                ret.kids.push_back(node_name(hidden.name));
                block.kids.push_back(std::move(ret));
            } else {
                block.kids.push_back(std::move(call_stmt));
                Node ret;
                ret.kind = Node::Return;
                block.kids.push_back(std::move(ret));
            }
            n = std::move(block);
            return;
        }
        for (Node& kid : n.kids) rewrite_returns(kid, fn);
    }

    // A member function that is declared in its class and defined in another file (the engine's Sha512::Update): a call of
    // it needs the function's signature and an undefined symbol with its decorated name for the linker to find.
    void declare_external_methods() {
        for (size_t sid = 0; sid < structs.size(); ++sid) {
            for (size_t mi = 0; mi < structs[sid].methods.size(); ++mi) {
                const Method& m = structs[sid].methods[mi];
                if (m.has_body || m.pure || m.implicit || m.owner != static_cast<int>(sid) || m.qname.empty()) continue;
                bool known = false;
                for (const FuncDecl& f : funcs)
                    if (f.name == m.qname) known = true;
                if (known) continue;
                FuncDecl fn;
                fn.name = m.qname;
                fn.symbol = m.symbol;
                fn.ret = m.ret;
                Type self_ptr;
                self_ptr.base = 3;
                self_ptr.struct_id = static_cast<int>(sid);
                self_ptr.pointers = 1;
                if (!m.is_static) {
                    fn.params.push_back(self_ptr);
                    fn.param_names.push_back("this");
                }
                for (size_t i = 0; i < m.params.size(); ++i) {
                    fn.params.push_back(m.params[i]);
                    fn.param_names.push_back(std::string());
                }
                finish_signature(&fn);
                fn.defined = false;
                funcs.push_back(std::move(fn));
            }
        }
    }

    bool finish_program() {
        {
            // static int f(int); ... static int f(int p) { ... }: the declaration and the definition are one function
            // ([basic.link]); a declaration left beside an internal definition would ask the linker for an external f.
            // The linkage of the first declaration holds for the definition ([dcl.stc] 4).
            for (size_t i = 0; i < funcs.size(); ++i) {
                if (funcs[i].defined) continue;
                for (FuncDecl& d : funcs) {
                    if (!d.defined || d.name != funcs[i].name || d.symbol != funcs[i].symbol) continue;
                    if (funcs[i].is_static) d.is_static = true;
                    funcs[i].name.clear();  // marked for removal
                    break;
                }
            }
            funcs.erase(std::remove_if(funcs.begin(), funcs.end(), [](const FuncDecl& f) { return f.name.empty(); }),
                        funcs.end());
        }
        declare_external_methods();
        // What a header can make in every file that includes it has an internal symbol here (each file keeps its own copy,
        // so two files can both use vector<int> or the same inline function): template instances and virtual tables.
        for (FuncDecl& f : funcs)
            if (f.defined && f.symbol.find("?$") != std::string::npos) f.is_static = true;
        for (Global& g : globals)
            if (g.vtable || g.name.find("?$") != std::string::npos) g.is_static = true;
        for (GlobalObj& g : static_objs) global_objs.push_back(std::move(g));
        static_objs.clear();
        const bool need_fini = !global_objs.empty();
        if (!init_used && !need_fini) return true;
        auto make_fn = [&](const std::string& name, Node body, std::vector<Local> locals) {
            FuncDecl fn;
            fn.name = name;
            fn.ret.base = 0;
            finish_signature(&fn);
            fn.body = std::move(body);
            fn.locals = std::move(locals);
            fn.defined = true;
            funcs.push_back(std::move(fn));
        };
        if (init_used) {
            init_body.kind = Node::Block;
            make_fn("__okay_init", std::move(init_body), init_locals);
        }
        if (need_fini) {
            std::vector<Local> fini_locals;
            std::vector<Local>* saved_locals = cur_locals;
            const int saved_depth = scope_depth;
            cur_locals = &fini_locals;
            scope_depth = 0;
            Node body;
            body.kind = Node::Block;
            for (size_t i = global_objs.size(); i-- > 0;) {
                const GlobalObj& g = global_objs[i];
                Node call;
                if (g.count > 0) {
                    if (!element_loop(g.name, g.count, true, g.sid, "<dtor>", &call)) return false;
                } else if (!special_call(g.sid, "<dtor>", node_addr(node_name(g.name)), {}, &call)) {
                    return false;
                }
                if (!g.guard.empty()) {
                    Node test;
                    test.kind = Node::If;
                    test.kids.push_back(node_name(g.guard));
                    test.kids.push_back(std::move(call));
                    call = std::move(test);
                }
                body.kids.push_back(std::move(call));
            }
            cur_locals = saved_locals;
            scope_depth = saved_depth;
            make_fn("__okay_fini", std::move(body), std::move(fini_locals));
        }
        // main (or entry when there is no main) runs them.
        FuncDecl* start = nullptr;
        for (FuncDecl& fn : funcs)
            if (fn.name == "main" && fn.defined) start = &fn;
        if (start == nullptr)
            for (FuncDecl& fn : funcs)
                if (fn.name == "entry" && fn.defined) start = &fn;
        if (start == nullptr) return true;
        if (need_fini) {
            rewrite_returns(start->body, *start);
            Node call;
            call.kind = Node::Call;
            call.text = "__okay_fini";
            Node call_stmt;
            call_stmt.kind = Node::ExprStmt;
            call_stmt.kids.push_back(std::move(call));
            start->body.kids.push_back(std::move(call_stmt));
        }
        if (init_used) {
            Node call;
            call.kind = Node::Call;
            call.text = "__okay_init";
            Node call_stmt;
            call_stmt.kind = Node::ExprStmt;
            call_stmt.kids.push_back(std::move(call));
            start->body.kids.insert(start->body.kids.begin(), std::move(call_stmt));
        }
        return true;
    }

    bool parse() {
        collapse_operators();
        lower_cxx_spellings();
        strip_namespace_qualifiers();
        for (const Token& t : tokens)
            if (t.kind == TIdent && (t.text == "try" || t.text == "throw")) eh_ = true;
        while (!is(TEnd)) {
            if (!ns_open.empty() && accept(TRBrace)) {
                for (size_t n = 0; n < ns_open.back(); ++n) ns_parts.pop_back();
                ns_open.pop_back();
                if (!ns_is_c.empty()) {
                    if (ns_is_c.back()) --c_linkage;
                    ns_is_c.pop_back();
                }
                if (!ns_is_anon.empty()) {
                    if (ns_is_anon.back()) --anon_depth;
                    ns_is_anon.pop_back();
                }
                update_ns();
                continue;
            }
            if (is(TIdent) && cur().text == "extern" && at + 1 < tokens.size() && tokens[at + 1].kind == TString) {
                // extern "C" { ... } and extern "C" declaration ([dcl.link]).
                const bool c_lang = tokens[at + 1].text == "C";
                next();
                next();
                if (is(TLBrace)) {
                    next();
                    ns_open.push_back(0);
                    ns_is_c.push_back(c_lang);
                    ns_is_anon.push_back(false);
                    if (c_lang) ++c_linkage;
                } else if (c_lang) {
                    ++c_linkage;
                    c_single = true;
                }
                continue;
            }
            if (is(TNamespace)) {
                next();
                size_t pushed = 0;
                if (is(TIdent)) {
                    do {  // namespace a::b { } ([namespace.def])
                        if (!is(TIdent)) {
                            error = "expected a namespace name";
                            return false;
                        }
                        ns_parts.push_back(cur().text);
                        ++pushed;
                        next();
                    } while (accept(TColonColon));
                }
                if (!expect(TLBrace, "'{'")) return false;
                ns_open.push_back(pushed);
                ns_is_c.push_back(false);
                ns_is_anon.push_back(pushed == 0);
                if (pushed == 0) ++anon_depth;
                update_ns();
                continue;
            }
            if (is(TIdent) && cur().text == "template" && at + 1 < tokens.size() && tokens[at + 1].kind == TLt) {
                if (!parse_template_decl()) return false;
                continue;
            }
            if (explicit_member_def_ahead()) {
                if (!parse_template_decl(true)) return false;
                continue;
            }
            bool is_typedef = false;
            bool is_static = false;
            bool is_const = false;
            const size_t decl_begin = at;
            while (is(TTypedef) || is(TStatic) || is(TConst)) {
                if (accept(TTypedef)) is_typedef = true;
                else if (accept(TStatic)) is_static = true;
                else if (accept(TConst)) is_const = true;
            }
            bool imported = false;
            bool exported = false;
            Type type;
            if (!parse_type(&type, &imported, &exported)) return false;
            while (!is_typedef && is(TStar) && at + 2 < tokens.size() && tokens[at + 1].kind == TIdent &&
                   tokens[at + 2].kind == TColonColon) {
                ++type.pointers;
                next();
            }
            if (!is_typedef && type.base == 3 && type.pointers == 0 && !type.is_ref && type.struct_id >= 0 && is(TColonColon) &&
                at + 3 < tokens.size() &&
                ((tokens[at + 1].kind == TIdent && tokens[at + 2].kind == TLParen) ||
                 (tokens[at + 1].kind == TTilde && tokens[at + 2].kind == TIdent && tokens[at + 3].kind == TLParen))) {
                // C::C(args) : members { ... } and C::~C() { ... }: a constructor or destructor declared in the
                // class and defined here.
                const int sid = type.struct_id;
                next();
                const bool is_dtor = accept(TTilde);
                if (cur().text != structs[static_cast<size_t>(sid)].name &&
                    cur().text != (structs[static_cast<size_t>(sid)].tmpl.empty() ? std::string() : structs[static_cast<size_t>(sid)].tmpl)) {
                    error = "expected the class name after ::";
                    return false;
                }
                next();
                next();
                Declarator decl;
                if (!parse_param_list(&decl)) return false;
                StructDef& owner = structs[static_cast<size_t>(sid)];
                size_t mi = owner.methods.size();
                for (size_t i = 0; i < owner.methods.size(); ++i) {
                    const Method& cand = owner.methods[i];
                    if (cand.name != (is_dtor ? "<dtor>" : "<ctor>") || cand.owner != sid || cand.has_body ||
                        cand.params.size() != decl.params.size())
                        continue;
                    bool same = true;
                    for (size_t k = 0; k < cand.params.size(); ++k)
                        if (!same_shape(cand.params[k], decl.params[k])) same = false;
                    if (same) mi = i;
                }
                if (mi == owner.methods.size()) {
                    error = std::string("no matching ") + (is_dtor ? "destructor" : "constructor") + " declared in the class";
                    return false;
                }
                owner.methods[mi].has_body = true;
                owner.methods[mi].out_of_class = true;
                owner.methods[mi].body_at = at;
                owner.methods[mi].param_names = decl.param_names;
                while (!is(TLBrace) && !is(TEnd) && !is(TSemi)) next();
                if (!is(TLBrace)) {
                    error = "expected a constructor body";
                    return false;
                }
                if (!skip_body()) return false;
                const size_t resume = at;
                if (!define_method(sid, mi)) return false;
                at = resume;
                continue;
            }
            if (!is_typedef && is(TIdent) && at + 3 < tokens.size() && tokens[at + 1].kind == TColonColon &&
                class_name_at(at)) {
                // int C::f(int x) { ... } for a function declared in class C.
                const int sid = find_struct(cur().text);
                next();
                next();
                if (!is(TIdent)) {
                    error = "expected a member function name";
                    return false;
                }
                const std::string mname = cur().text;
                next();
                if (!is(TLParen)) {
                    // int P::n = 7; the definition of a static data member.
                    auto sm = structs[static_cast<size_t>(sid)].statics.find(mname);
                    if (sm == structs[static_cast<size_t>(sid)].statics.end()) {
                        error = "no static member " + mname + " in the class";
                        return false;
                    }
                    Node sinit;
                    bool has_init = false;
                    if (is(TLBrace) && !sm->second.has_const && obj_needs_ctor(sm->second.type)) {
                        // inline const V V::Zero {0, 0}; the braces are the constructor's arguments.
                        next();
                        std::vector<Node> sargs;
                        if (!is(TRBrace)) {
                            do {
                                Node a;
                                if (!parse_expr(1, &a)) return false;
                                sargs.push_back(std::move(a));
                            } while (accept(TComma));
                        }
                        if (!expect(TRBrace, "'}'") || !expect(TSemi, "';'")) return false;
                        if (!register_global_object(sm->second.symbol, sm->second.type, std::move(sargs))) return false;
                        continue;
                    }
                    if (accept(TAssign) || is(TLBrace)) {
                        if (!parse_init(&sinit)) return false;
                        has_init = true;
                    }
                    if (!expect(TSemi, "';'")) return false;
                    if (sm->second.has_const) continue;  // const P::k; the value is already known
                    if (!has_init && obj_needs_ctor(sm->second.type) && !sm->second.type.is_array()) {
                        // std::string C::name; : the default constructor runs at startup.
                        if (!register_global_object(sm->second.symbol, sm->second.type, {})) return false;
                        continue;
                    }
                    if (has_init && obj_needs_ctor(sm->second.type) && !sm->second.type.is_array() &&
                        !(sinit.kind == Node::Block && sinit.text == "init")) {
                        // std::string C::name = "x"; : a constructor with that value.
                        std::vector<Node> sargs;
                        sargs.push_back(std::move(sinit));
                        if (!register_global_object(sm->second.symbol, sm->second.type, std::move(sargs))) return false;
                        continue;
                    }
                    std::vector<DynLeaf> leaves;
                    Type gtype;
                    for (Global& g : globals)
                        if (g.name == sm->second.symbol && has_init) {
                            g.bss = false;
                            g.init_bytes.clear();
                            gtype = g.type;
                            if (!flatten_dyn(g.type, sinit, &g, &leaves)) return false;
                        }
                    add_init_stmts(dyn_leaf_stmts(sm->second.symbol, gtype, &leaves));
                    continue;
                }
                next();
                Declarator decl;
                if (!parse_param_list(&decl)) return false;
                const bool def_const = accept(TConst);
                const Method* found = nullptr;
                {
                    const std::string def_sig = param_sig(decl.params);
                    for (const Method& cand : structs[static_cast<size_t>(sid)].methods)
                        if (cand.name == mname && cand.owner == sid && !cand.has_body && cand.is_const == def_const &&
                            param_sig(cand.params) == def_sig)
                            found = &cand;
                    if (found == nullptr)
                        for (const Method& cand : structs[static_cast<size_t>(sid)].methods)
                            if (cand.name == mname && cand.owner == sid && !cand.has_body &&
                                cand.params.size() == decl.params.size())
                                found = &cand;
                    if (found == nullptr) found = find_method(sid, mname, decl.params.size());
                }
                if (found == nullptr || found->owner != sid) {
                    error = "no member function " + mname + " in the class";
                    return false;
                }
                size_t mi = 0;
                StructDef& owner = structs[static_cast<size_t>(sid)];
                for (size_t i = 0; i < owner.methods.size(); ++i)
                    if (&owner.methods[i] == found) mi = i;
                if (owner.methods[mi].has_body) {
                    error = "member function " + mname + " is defined twice";
                    return false;
                }
                owner.methods[mi].has_body = true;
                owner.methods[mi].out_of_class = true;
                owner.methods[mi].body_at = at;
                owner.methods[mi].param_names = decl.param_names;
                if (!skip_body()) return false;
                const size_t resume = at;
                if (!define_method(sid, mi)) return false;
                at = resume;
                continue;
            }
            if (!is_typedef && is(TSemi)) {
                next();
                continue;
            }
            Declarator decl;
            std::vector<Node> gargs;
            bool gctor_args = false;
            if (!is_typedef && (obj_needs_ctor(type) || obj_needs_dtor(type)) && is(TIdent) && at + 2 < tokens.size() &&
                tokens[at + 1].kind == TLParen) {
                const Tok after = tokens[at + 2].kind;
                const bool type_start = after == TRParen || after == TInt || after == TVoid || after == TChar ||
                                        after == TUnsigned || after == TSigned || after == TLong || after == TShort ||
                                        after == TStruct || after == TUnion || after == TEnum || after == TFloat ||
                                        after == TDouble || after == TConst;
                // `Ease e`, `Vec3 v`, `const Name& n`: a type name and a parameter name are a parameter list.
                const bool named_type_start =
                    after == TIdent && at + 3 < tokens.size() &&
                    (tokens[at + 3].kind == TIdent ||
                     ((tokens[at + 3].kind == TAmp || tokens[at + 3].kind == TStar) && at + 4 < tokens.size() &&
                      tokens[at + 4].kind == TIdent &&
                      (typedefs.count(tokens[at + 2].text) > 0 || class_name_text(tokens[at + 2].text))));
                const bool template_type_start = after == TIdent && is_template_name_at(at + 2);  // `Foo(vector<int>& n)`
                if (!type_start && !named_type_start && !template_type_start) {
                    // C g(args); a global object, the arguments go to a constructor.
                    decl.name = cur().text;
                    decl.type = type;
                    next();
                    next();
                    if (!parse_call_args(&gargs)) return false;
                    gctor_args = true;
                }
            }
            if (!gctor_args && !parse_declarator(type, &decl)) return false;
            if (is_typedef) {
                decl.type.is_const = is_const;
                typedefs[decl.name] = decl.type;
                if (!expect(TSemi, "';'")) return false;
                continue;
            }
            decl.type.is_const = is_const;
            if (!decl.is_function && (obj_needs_ctor(decl.type) || obj_needs_dtor(decl.type))) {
                Node ginit;
                bool has_ginit = false;
                if (!decl.type.is_array() && !gctor_args && is(TAssign) && at + 1 < tokens.size() &&
                    tokens[at + 1].kind != TLBrace) {
                    // C g = value; ([dcl.init]): the constructor that takes the value (a converting or copy
                    // constructor) runs at startup.
                    next();
                    Node value;
                    if (!parse_expr(1, &value)) return false;
                    if (class_prvalue(decl.type, value)) {
                        // C g = f(); : f writes g at startup; only the destructor is registered.
                        if (!expect(TSemi, "';'")) return false;
                        Global global;
                        global.name = decl.name;
                        global.type = decl.type;
                        global.bss = true;
                        global.is_static = is_static || anon_depth > 0;
                        globals.push_back(global);
                        {
                            std::vector<DynLeaf> one(1);
                            one[0].type = decl.type;
                            one[0].value = std::move(value);
                            add_init_stmts(dyn_leaf_stmts(decl.name, decl.type, &one));
                        }
                        if (obj_needs_dtor(decl.type)) {
                            GlobalObj g;
                            g.name = decl.name;
                            g.sid = decl.type.struct_id;
                            global_objs.push_back(std::move(g));
                        }
                        continue;
                    }
                    gargs.clear();
                    gargs.push_back(std::move(value));
                } else if (accept(TAssign)) {
                    if (!decl.type.is_array() || !decl.type.more.empty() || !parse_init(&ginit)) {
                        error = error.empty() ? "a global object with a constructor takes arguments in parentheses" : error;
                        return false;
                    }
                    has_ginit = true;
                }
                if (!expect(TSemi, "';'")) return false;
                Global global;
                global.name = decl.name;
                global.type = decl.type;
                global.bss = true;
                global.is_static = is_static || anon_depth > 0;
                globals.push_back(global);
                if (!register_global_object(decl.name, decl.type, std::move(gargs), has_ginit ? &ginit : nullptr))
                    return false;
                continue;
            }
            if (!decl.is_function) {
                for (;;) {  // int a = 1, *b, c[2]; : every name gets the type read before the first one
                    Global global;
                    std::vector<DynLeaf> dyn_leaves_here;
                    global.name = decl.name;
                    global.type = decl.type;
                    // A const object at namespace scope and anything in an unnamed namespace has internal linkage
                    // ([basic.link]): every file keeps its own copy, so two files may use the same name.
                    global.is_static = is_static || decl.type.is_const || anon_depth > 0;
                    if (accept(TAssign)) {
                        Node init;
                        if (!parse_init(&init)) return false;
                        global.bss = false;
                        if (!fix_unsized(&decl.type, init)) return false;
                        global.type = decl.type;
                        if (!flatten_dyn(decl.type, init, &global, &dyn_leaves_here)) return false;
                    }
                    {
                        // The same variable name in two namespaces (Sha256.hpp's kK and Sha512.cpp's kK): names of
                        // variables are not kept apart by namespace here, so the later one gets its own internal name
                        // and what is read after it means that one.
                        bool taken = false;
                        for (const Global& earlier : globals)
                            if (earlier.name == global.name) taken = true;
                        if (taken) {
                            global.name = decl.name + "@" + std::to_string(globals.size());
                            global_alias[decl.name] = global.name;
                        } else {
                            global_alias.erase(decl.name);
                        }
                    }
                    globals.push_back(global);
                    add_init_stmts(dyn_leaf_stmts(global.name, global.type, &dyn_leaves_here));
                    if (!accept(TComma)) break;
                    decl = Declarator{};
                    if (!parse_declarator(type, &decl)) return false;
                    decl.type.is_const = is_const;
                    if (decl.is_function || obj_needs_ctor(decl.type) || obj_needs_dtor(decl.type)) {
                        error = "only plain data can follow a comma in a global declaration";
                        return false;
                    }
                }
                if (!expect(TSemi, "';'")) return false;
                continue;
            }
            FuncDecl fn;
            fn.name = current_ns.empty() ? decl.name : current_ns + "::" + decl.name;
            if (!current_ns.empty()) fn.symbol = cxx_symbol(ns_mangled, decl.name, decl.type, decl.params, &structs);
            if (c_linkage > 0) fn.symbol = decl.name;  // extern "C": the plain name
            {
                // Overloads ([over.load]): a second signature under one name gets its own internal name, and
                // every function of an overloaded name is decorated (?f@@YAHH@Z), as an operator always is.
                const std::string base_name = fn.name;
                OvFn entry;
                entry.sig = param_sig(decl.params) + (decl.vararg ? "Z" : "");
                entry.params = decl.params;
                entry.required = required_args(decl.param_defaults, decl.params.size());
                entry.vararg = decl.vararg;
                entry.symbol = c_linkage > 0 ? decl.name : cxx_symbol(ns_mangled, decl.name, decl.type, decl.params, &structs);
                std::vector<OvFn>& list = ovl[base_name];
                OvFn* hit = nullptr;
                for (OvFn& e : list)
                    if (e.sig == entry.sig) hit = &e;
                if (hit != nullptr) {
                    hit->required = std::min(hit->required, entry.required);
                    fn.name = hit->internal;
                } else {
                    entry.internal = list.empty() ? base_name : base_name + "#" + entry.sig;
                    fn.name = entry.internal;
                    list.push_back(entry);
                    hit = &list.back();
                }
                const bool is_op = decl.name.compare(0, 8, "operator") == 0;
                if ((list.size() > 1 || is_op) && !imported) {
                    fn.symbol = hit->symbol;
                    for (const OvFn& e : list)
                        for (FuncDecl& earlier : funcs)
                            if (earlier.name == e.internal && earlier.symbol.empty() && !earlier.imported)
                                earlier.symbol = e.symbol;
                }
            }
            fn.ret = decl.type;
            fn.params = decl.params;
            fn.param_names = decl.param_names;
            fn.defaults = decl.param_defaults;
            fn.vararg = decl.vararg;
            fn.imported = imported;
            fn.exported = exported;
            bool inline_fn = false;
            for (size_t k = decl_begin; k < at && k < tokens.size(); ++k)
                if (tokens[k].inl) inline_fn = true;
            fn.is_static = is_static || anon_depth > 0 || inline_fn;
            finish_signature(&fn);
            if (c_single) {
                --c_linkage;
                c_single = false;
            }
            if (accept(TSemi)) {
                fn.defined = false;
                funcs.push_back(std::move(fn));
                continue;
            }
            if (!parse_function_body(&fn, -1)) return false;
            funcs.push_back(std::move(fn));
        }
        if (!error.empty()) return false;
        if (eh_) declare_eh_builtins();
        return finish_program();
    }
};


}  // namespace compiler
}  // namespace okayms
