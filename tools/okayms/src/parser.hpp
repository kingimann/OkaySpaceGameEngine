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
    // template <class T> using X = type; ([temp.alias]) becomes a class template holding the type,
    //   template <class T> struct X__alias { typedef type type; };
    // and each later X<args> becomes X__alias<args>::type. Own stand-in for alias templates, at the token level.
    void expand_alias_templates() {
        std::set<std::string> names;
        std::vector<Token> out;
        out.reserve(tokens.size());
        auto make = [&](const Token& like, Tok kind, const std::string& text) {
            Token t = like;
            t.kind = kind;
            t.text = text;
            return t;
        };
        // The braces open at this point, each with whether it is a class body; for the outermost class body open, where
        // its declaration starts in `out` (an alias template declared in a class is put there, before the class: a
        // member class template is not read here; Round 29, EventBus::Handler).
        std::vector<bool> brace_is_class;
        size_t class_depth = 0;
        size_t hoist_at = 0;
        for (size_t i = 0; i < tokens.size(); ++i) {
            const Token& t = tokens[i];
            if (t.kind == TLBrace) {
                bool is_class = false;
                size_t decl_start = out.size();
                for (size_t k = out.size(); k-- > 0;) {
                    const Tok kk = out[k].kind;
                    if (kk == TSemi || kk == TLBrace || kk == TRBrace || kk == TRParen || kk == TAssign) break;
                    if (kk == TStruct || kk == TUnion) {
                        is_class = true;
                        decl_start = k;
                        if (k > 0 && out[k - 1].kind == TGt) {
                            // template <...> class X {: the declaration starts at `template`
                            int angle = 0;
                            size_t m = k;
                            while (m-- > 0) {
                                if (out[m].kind == TGt) ++angle;
                                else if (out[m].kind == TLt && --angle == 0) break;
                            }
                            if (m > 0 && m < k && out[m - 1].kind == TIdent && out[m - 1].text == "template") decl_start = m - 1;
                        }
                        break;
                    }
                }
                if (is_class && class_depth == 0) hoist_at = decl_start;
                if (is_class) ++class_depth;
                brace_is_class.push_back(is_class);
            } else if (t.kind == TRBrace && !brace_is_class.empty()) {
                if (brace_is_class.back()) --class_depth;
                brace_is_class.pop_back();
            }
            if (t.kind == TIdent && t.text == "template" && i + 1 < tokens.size() && tokens[i + 1].kind == TLt) {
                // the parameter list
                size_t j = i + 1;
                int angle = 0;
                for (; j < tokens.size(); ++j) {
                    if (tokens[j].kind == TLt) ++angle;
                    else if (tokens[j].kind == TGt && --angle == 0) break;
                }
                if (j + 4 < tokens.size() && tokens[j + 1].kind == TIdent && tokens[j + 1].text == "using" &&
                    tokens[j + 2].kind == TIdent && tokens[j + 3].kind == TAssign) {
                    size_t e = j + 4;
                    while (e < tokens.size() && tokens[e].kind != TSemi && tokens[e].kind != TEnd) ++e;
                    const std::string name = tokens[j + 2].text;
                    const size_t gen_from = out.size();
                    for (size_t k = i; k <= j; ++k) out.push_back(tokens[k]);
                    Token st = make(tokens[j + 1], TStruct, "struct");
                    out.push_back(st);
                    out.push_back(make(tokens[j + 2], TIdent, name + "__alias"));
                    out.push_back(make(tokens[j + 2], TLBrace, "{"));
                    out.push_back(make(tokens[j + 2], TTypedef, "typedef"));
                    // the aliased type, itself with earlier aliases expanded
                    std::vector<Token> body(tokens.begin() + static_cast<std::ptrdiff_t>(j + 4),
                                            tokens.begin() + static_cast<std::ptrdiff_t>(e));
                    std::vector<Token> fixed;
                    expand_alias_uses(body, names, &fixed);
                    for (const Token& b : fixed) out.push_back(b);
                    out.push_back(make(tokens[j + 2], TIdent, "type"));
                    out.push_back(make(tokens[j + 2], TSemi, ";"));
                    out.push_back(make(tokens[j + 2], TRBrace, "}"));
                    out.push_back(make(tokens[j + 2], TSemi, ";"));
                    names.insert(name);
                    if (class_depth > 0 && hoist_at <= gen_from) {
                        // In a class: the made class template goes before the outermost class.
                        std::vector<Token> made(out.begin() + static_cast<std::ptrdiff_t>(gen_from), out.end());
                        out.resize(gen_from);
                        out.insert(out.begin() + static_cast<std::ptrdiff_t>(hoist_at), made.begin(), made.end());
                        hoist_at += made.size();
                    }
                    i = e;  // the ';'
                    continue;
                }
            }
            out.push_back(t);
        }
        std::vector<Token> fixed;
        expand_alias_uses(out, names, &fixed);
        tokens = std::move(fixed);
    }
    // X<args> for an alias template X: X__alias<args>::type.
    static void expand_alias_uses(const std::vector<Token>& in, const std::set<std::string>& names, std::vector<Token>* out) {
        for (size_t i = 0; i < in.size(); ++i) {
            const Token& t = in[i];
            if (t.kind == TIdent && names.count(t.text) > 0 && i + 1 < in.size() && in[i + 1].kind == TLt &&
                !(i > 0 && (in[i - 1].kind == TStruct))) {
                size_t j = i + 1;
                int angle = 0;
                int paren = 0;
                for (; j < in.size(); ++j) {
                    if (in[j].kind == TLParen) ++paren;
                    else if (in[j].kind == TRParen) --paren;
                    else if (paren == 0 && in[j].kind == TLt) ++angle;
                    else if (paren == 0 && in[j].kind == TGt && --angle == 0) break;
                }
                if (j >= in.size()) {
                    out->push_back(t);
                    continue;
                }
                Token n = t;
                n.text = t.text + "__alias";
                out->push_back(n);
                std::vector<Token> inner(in.begin() + static_cast<std::ptrdiff_t>(i + 1),
                                         in.begin() + static_cast<std::ptrdiff_t>(j + 1));
                expand_alias_uses(inner, names, out);
                Token cc = in[j];
                cc.kind = TColonColon;
                cc.text = "::";
                out->push_back(cc);
                Token ty = in[j];
                ty.kind = TIdent;
                ty.text = "type";
                out->push_back(ty);
                i = j;
                continue;
            }
            out->push_back(t);
        }
    }
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
        // The enumerators of each scoped enumeration (of every one of that name): State::IkKey, with a class State and
        // an `enum class State` elsewhere, is not an enumerator and stays three tokens (Round 29).
        std::map<std::string, std::set<std::string>> scoped_names;
        for (size_t i = 0; i + 2 < tokens.size(); ++i)
            if (tokens[i].kind == TEnum && (tokens[i + 1].kind == TStruct) && tokens[i + 2].kind == TIdent) {
                scoped.insert(tokens[i + 2].text);
                std::set<std::string>& names = scoped_names[tokens[i + 2].text];
                size_t j = i + 3;
                while (j < tokens.size() && tokens[j].kind != TLBrace && tokens[j].kind != TSemi && tokens[j].kind != TEnd) ++j;
                if (j >= tokens.size() || tokens[j].kind != TLBrace) continue;
                bool want = true;
                int depth = 0;
                for (++j; j < tokens.size() && tokens[j].kind != TEnd; ++j) {
                    const Tok k = tokens[j].kind;
                    if (k == TRBrace && depth == 0) break;
                    if (k == TLParen || k == TLBrace) ++depth;
                    else if (k == TRParen || k == TRBrace) --depth;
                    else if (k == TComma && depth == 0) want = true;
                    else if (want && k == TIdent) {
                        names.insert(tokens[j].text);
                        want = false;
                    } else {
                        want = false;
                    }
                }
            }
        const auto is_enumerator = [&](const std::string& en, const std::string& name) {
            const auto it = scoped_names.find(en);
            return it == scoped_names.end() || it->second.count(name) > 0;
        };
        // using B = Outer::BarStyle; (or typedef Outer::BarStyle B;) for a scoped enumeration: B::Segmented is then read
        // as BarStyle::Segmented ([dcl.typedef] 2, [enum.udecl]). Own simplification: the alias holds for the rest of
        // the file, whatever block it was declared in.
        // Each alias declaration by position: the latest one before a use counts (another `using S = ...;` for a type
        // that is not a scoped enumeration ends the earlier one).
        std::map<std::string, std::vector<std::pair<size_t, std::string>>> scoped_alias;
        for (size_t i = 0; i + 3 < tokens.size(); ++i) {
            if (is_word(i, "using") && tokens[i + 1].kind == TIdent && tokens[i + 2].kind == TAssign) {
                size_t j = i + 3;
                while (j + 2 < tokens.size() && tokens[j].kind == TIdent && tokens[j + 1].kind == TColonColon) j += 2;
                const bool to_enum = j + 1 < tokens.size() && tokens[j].kind == TIdent && scoped.count(tokens[j].text) > 0 &&
                                     tokens[j + 1].kind == TSemi;
                if (scoped.count(tokens[i + 1].text) == 0)
                    scoped_alias[tokens[i + 1].text].push_back({i, to_enum ? tokens[j].text : std::string()});
            } else if (tokens[i].kind == TTypedef) {
                size_t j = i + 1;
                while (j + 2 < tokens.size() && tokens[j].kind == TIdent && tokens[j + 1].kind == TColonColon) j += 2;
                if (j + 2 < tokens.size() && tokens[j].kind == TIdent && scoped.count(tokens[j].text) > 0 &&
                    tokens[j + 1].kind == TIdent && tokens[j + 2].kind == TSemi && scoped.count(tokens[j + 1].text) == 0)
                    scoped_alias[tokens[j + 1].text].push_back({i, tokens[j].text});
            }
        }
        const auto alias_at = [&scoped_alias](const std::string& name, size_t pos) {
            const auto it = scoped_alias.find(name);
            std::string found;
            if (it != scoped_alias.end())
                for (const auto& decl : it->second)
                    if (decl.first < pos) found = decl.second;
            return found;
        };
        std::vector<Token> out;
        out.reserve(tokens.size());
        std::string in_scoped;  // the scoped enum whose braces are open
        std::set<std::string> scoped_seen;  // its enumerators read so far
        int enum_depth = 0;
        bool want_name = false;
        // The closing parentheses of rewritten casts still to be written, by the index of the token after which each
        // goes (innermost last): static_cast<T>(x)->m is ((T)(x))->m, not (T)((x)->m).
        std::vector<size_t> cast_closers;
        Token closer;
        closer.kind = TRParen;
        closer.text = ")";
        for (size_t i = 0; i < tokens.size(); ++i) {
            while (!cast_closers.empty() && cast_closers.back() < i) {
                closer.line = tokens[i].line;
                out.push_back(closer);
                cast_closers.pop_back();
            }
            const Token& t = tokens[i];
            if (t.kind == TIdent && t.text == "thread_local") {
                // thread_local ([dcl.stc] 4): at block scope it implies static; here it is a `static` that says so.
                if (!out.empty() && out.back().kind == TStatic) {
                    out.back().tls = true;
                } else {
                    Token st = t;
                    st.kind = TStatic;
                    st.text = "static";
                    st.tls = true;
                    out.push_back(st);
                }
                continue;
            }
            if (t.kind == TStatic && !out.empty() && out.back().kind == TStatic && out.back().tls) continue;  // thread_local static
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
            // new (std::nothrow) T ([new.delete.single]): here `new` calls malloc and never throws, so the tag is dropped
            // and it is a plain new. Own simplification: the constructor still runs when malloc returns null.
            if (t.kind == TIdent && t.text == "new" && i + 3 < tokens.size() && tokens[i + 1].kind == TLParen) {
                size_t k = i + 2;
                if (k + 1 < tokens.size() && tokens[k].kind == TIdent && tokens[k].text == "std" && tokens[k + 1].kind == TColonColon)
                    k += 2;
                if (k + 1 < tokens.size() && tokens[k].kind == TIdent && tokens[k].text == "nothrow" && tokens[k + 1].kind == TRParen) {
                    out.push_back(t);
                    i = k + 1;
                    continue;
                }
            }
            // Microsoft keywords that change nothing on x64: the calling-convention words (x64 has one convention; the
            // compiler accepts and ignores them, per Microsoft's x64 calling convention documentation), the pointer-size
            // words, and __forceinline / __inline (read as inline).
            if (t.kind == TIdent && (t.text == "__forceinline" || t.text == "__inline")) {
                if (i + 1 < tokens.size()) tokens[i + 1].inl = true;
                continue;
            }
            if (t.kind == TIdent && (t.text == "__cdecl" || t.text == "__stdcall" || t.text == "__fastcall" ||
                                      t.text == "__thiscall" || t.text == "__ptr64" || t.text == "__ptr32" ||
                                      t.text == "__unaligned" || t.text == "__restrict" || t.text == "__w64"))
                continue;
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
                    scoped_seen.clear();
                } else if (t.kind == TComma) {
                    want_name = true;
                } else if (want_name && t.kind == TIdent) {
                    Token n = t;
                    n.text = in_scoped + "::" + t.text;
                    out.push_back(n);
                    scoped_seen.insert(t.text);
                    want_name = false;
                    continue;
                } else {
                    want_name = false;
                    // World = Pause | Bag: an enumerator named in a later one's value is in scope there ([enum.enum] 12).
                    if (t.kind == TIdent && scoped_seen.count(t.text) > 0 &&
                        !(i > 0 && (tokens[i - 1].kind == TColonColon || tokens[i - 1].kind == TDot ||
                                    tokens[i - 1].kind == TArrow))) {
                        Token n = t;
                        n.text = in_scoped + "::" + t.text;
                        out.push_back(n);
                        continue;
                    }
                }
            }
            if (t.kind == TIdent && scoped.count(t.text) == 0 && i + 4 < tokens.size() && tokens[i + 1].kind == TColonColon &&
                tokens[i + 2].kind == TIdent && scoped.count(tokens[i + 2].text) > 0 && tokens[i + 3].kind == TColonColon &&
                tokens[i + 4].kind == TIdent && is_enumerator(tokens[i + 2].text, tokens[i + 4].text)) {
                // Input::PadFamily::Xbox: a scoped enumerator named through the class (or namespace) that declares the
                // enumeration. Enumerators are kept in one table here, so the outer qualifier is dropped.
                ++i;
                continue;
            }
            if (t.kind == TIdent && scoped_alias.count(t.text) > 0 && i + 2 < tokens.size() &&
                tokens[i + 1].kind == TColonColon && tokens[i + 2].kind == TIdent &&
                (i == 0 || tokens[i - 1].kind != TColonColon) && !alias_at(t.text, i).empty()) {
                Token n = t;
                n.text = alias_at(t.text, i) + "::" + tokens[i + 2].text;
                out.push_back(n);
                i += 2;
                continue;
            }
            if (t.kind == TIdent && scoped.count(t.text) > 0 && i + 2 < tokens.size() && tokens[i + 1].kind == TColonColon &&
                tokens[i + 2].kind == TIdent && is_enumerator(t.text, tokens[i + 2].text)) {
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
                    {
                        // The whole cast in parentheses (Round 29): find the ')' that closes the operand.
                        int depth = 0;
                        size_t m = j + 1;
                        for (; m < tokens.size() && tokens[m].kind != TEnd; ++m) {
                            if (tokens[m].kind == TLParen) ++depth;
                            else if (tokens[m].kind == TRParen && --depth == 0) break;
                        }
                        Token wrap = tokens[i + 1];
                        wrap.kind = TLParen;
                        wrap.text = "(";
                        out.push_back(wrap);
                        cast_closers.push_back(m);
                    }
                    Token open = tokens[i + 1];
                    open.kind = TLParen;
                    // reinterpret_cast keeps the address as it is (no base adjustment, [expr.reinterpret.cast] 7).
                    open.text = t.text == "reinterpret_cast" ? "(reinterpret" : "(";
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
        for (; !cast_closers.empty(); cast_closers.pop_back()) out.push_back(closer);
        tokens = std::move(out);
    }

    // A name written with its namespaces in front (geo::unit::kScale, std::uint8_t) is read as the
    // bare name: types, variables and enumerators of a namespace share one table. A call keeps its
    // qualification, because functions are registered with it (and decorated with it).
    // namespace fs = std::filesystem; ([namespace.alias]): the declaration is removed and each later `fs::` is
    // replaced by the namespace it names. Own simplification: the alias holds from its declaration to the end of the
    // file, also when it was declared inside a block.
    void expand_namespace_aliases() {
        std::map<std::string, std::vector<Token>> aliases;
        std::vector<Token> out;
        out.reserve(tokens.size());
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i].kind == TNamespace && i + 3 < tokens.size() && tokens[i + 1].kind == TIdent &&
                tokens[i + 2].kind == TAssign) {
                size_t j = i + 3;
                std::vector<Token> target;
                if (j < tokens.size() && tokens[j].kind == TColonColon) ++j;  // namespace x = ::a::b;
                while (j < tokens.size() && (tokens[j].kind == TIdent || tokens[j].kind == TColonColon)) {
                    // an alias of an alias is expanded in place
                    if (tokens[j].kind == TIdent && aliases.count(tokens[j].text) > 0 && target.empty() &&
                        j + 1 < tokens.size() && tokens[j + 1].kind == TColonColon) {
                        const std::vector<Token>& inner = aliases[tokens[j].text];
                        target.insert(target.end(), inner.begin(), inner.end());
                        j += 2;  // the inner alias's `::` is the last token of its replacement
                        continue;
                    }
                    target.push_back(tokens[j]);
                    ++j;
                }
                if (j < tokens.size() && tokens[j].kind == TSemi && !target.empty() && target.back().kind == TIdent) {
                    Token cc = target.back();
                    cc.kind = TColonColon;
                    cc.text = "::";
                    target.push_back(cc);  // the replacement of `fs::` ends in its own `::`
                    aliases[tokens[i + 1].text] = target;
                    i = j;
                    continue;
                }
            }
            if (tokens[i].kind == TIdent && aliases.count(tokens[i].text) > 0 && i + 1 < tokens.size() &&
                tokens[i + 1].kind == TColonColon && !(i > 0 && (tokens[i - 1].kind == TColonColon ||
                                                                 tokens[i - 1].kind == TDot || tokens[i - 1].kind == TArrow))) {
                for (Token t : aliases[tokens[i].text]) {
                    t.line = tokens[i].line;
                    out.push_back(t);
                }
                ++i;  // the alias's own `::` is in the replacement
                continue;
            }
            out.push_back(tokens[i]);
        }
        tokens = std::move(out);
    }
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
            // ::okay::Scope: a leading :: names the global namespace ([namespace.qual]); it goes with the names.
            const bool global_qual = i > 0 && tokens[i - 1].kind == TColonColon &&
                                     !(i > 1 && (tokens[i - 2].kind == TIdent || tokens[i - 2].kind == TGt));
            if (tokens[i].kind == TIdent && namespaces.count(tokens[i].text) > 0 && i + 1 < tokens.size() &&
                tokens[i + 1].kind == TColonColon && (!(i > 0 && tokens[i - 1].kind == TColonColon) || global_qual)) {
                size_t k = i;
                while (k + 1 < tokens.size() && tokens[k].kind == TIdent && namespaces.count(tokens[k].text) > 0 &&
                       tokens[k + 1].kind == TColonColon)
                    k += 2;
                bool call = k + 1 < tokens.size() && tokens[k].kind == TIdent && tokens[k + 1].kind == TLParen;
                // (ns::f)(args): a parenthesized name that may be called keeps its namespace names too, where the '('
                // before it cannot be a call's (Round 31). parse_unary passes them over when the name is a type
                // (a cast), and otherwise reads the call.
                const size_t open = global_qual ? i - 2 : i - 1;
                if (!call && i > 0 && open < tokens.size() && tokens[open].kind == TLParen && k + 2 < tokens.size() &&
                    tokens[k].kind == TIdent && tokens[k + 1].kind == TRParen && tokens[k + 2].kind == TLParen) {
                    const Tok before = open > 0 ? tokens[open - 1].kind : TEnd;
                    call = !(before == TIdent || before == TRParen || before == TRBrack || before == TGt ||
                             before == TNumber || before == TString || before == TIf || before == TWhile ||
                             before == TFor || before == TSwitch || before == TSizeof);
                }
                if (!call && k < tokens.size() && tokens[k].kind == TIdent) {
                    if (global_qual && !out.empty() && out.back().kind == TColonColon) out.pop_back();
                    if (tokens[i].inl) tokens[k].inl = true;  // `inline std::uint32_t f()`: the mark moves to the type name
                    tokens[k].nsq = true;
                    tokens[k].qual.clear();
                    for (size_t q = i; q < k; q += 2) tokens[k].qual += (tokens[k].qual.empty() ? "" : "::") + tokens[q].text;
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
    // The adjustor thunks the secondary tables name (Round 29): made as functions at the end of the file.
    struct Thunk {
        std::string symbol;
        std::string target;
        int adj = 0;
        bool rdx = false;
    };
    std::vector<Thunk> thunks{};

    // A constructor template is a constructor too, before any instance of it is made.
    bool has_ctor_template(int sid) const {
        for (const auto& mt : member_tmpls)
            if (mt.first.first == sid && mt.first.second == mt.second.owner) return true;
        return false;
    }
    // A constructor template of class sid that takes n arguments (one for a conversion, [over.ics.user]): string's
    // template <class It> string(It, It) does not make a string from one value.
    bool has_ctor_template_taking(int sid, size_t n) const {
        for (const auto& all : member_tmpl_all) {
            if (all.first.first != sid) continue;
            for (const MemberTmpl& mt : all.second) {
                if (all.first.second != mt.owner) continue;
                size_t fixed = 0;
                bool pack = false;
                for (const Type& t : mt.td.proto) {
                    if (t.pack) pack = true;
                    else ++fixed;
                }
                if (pack ? n >= fixed : n == fixed) return true;
            }
        }
        return false;
    }
    bool class_needs_ctor(const StructDef& d) const {
        for (const Method& m : d.methods)
            if (m.name == "<ctor>") return true;
        if (!structs.empty() && &d >= &structs.front() && &d <= &structs.back() &&
            has_ctor_template(static_cast<int>(&d - &structs.front())))
            return true;
        if (d.poly) return true;  // the constructor stores the table pointer
        if (d.base >= 0 && class_needs_ctor(structs[static_cast<size_t>(d.base)])) return true;
        for (const StructDef::BaseInfo& bi : d.bases)
            if (class_needs_ctor(structs[static_cast<size_t>(bi.sid)])) return true;
        if (!d.sec_tables.empty()) return true;  // the constructor stores the bases' table pointers
        for (size_t i = d.base_fields; i < d.fields.size(); ++i) {
            const Type& t = d.fields[i].type;
            if (d.fields[i].init_at >= 0 || d.fields[i].init_zero) return true;
            if (t.base == 3 && t.pointers == 0 && !t.is_ref && t.struct_id >= 0 &&
                class_needs_ctor(structs[static_cast<size_t>(t.struct_id)]))
                return true;
        }
        return false;
    }
    // The braced list a class's default member initializers amount to, when the class has no constructor of its own, no
    // destructor, no base and no virtual functions, and every member is a scalar or a pointer: then an object of it can
    // be constant-initialized from that list ([dcl.init.aggr], [class.default.ctor]). False otherwise.
    bool dmi_constant_init(int sid, Node* out) {
        if (sid < 0) return false;
        const StructDef& d = structs[static_cast<size_t>(sid)];
        if (d.poly || d.base >= 0 || class_needs_dtor(d)) return false;
        for (const Method& m : d.methods)
            if (m.name == "<ctor>" && !m.implicit) return false;
        *out = Node{};
        out->kind = Node::Block;
        out->text = "init";
        for (const Field& f : d.fields) {
            if (f.type.is_array() || f.type.is_ref || (f.type.base == 3 && f.type.pointers == 0) || f.init_aggr) return false;
            Node v;
            if (f.init_at >= 0) {
                const size_t saved = at;
                at = static_cast<size_t>(f.init_at);
                const bool ok = parse_expr(1, &v);
                at = saved;
                if (!ok) return false;
            } else {
                v.kind = Node::IntLit;
                v.type.base = 1;
            }
            if (f.type.memptr >= 0) class_ptr_convert(&v, f.type);  // a null member pointer is -1 (Round 31)
            out->kids.push_back(std::move(v));
        }
        return true;
    }
    bool class_needs_dtor(const StructDef& d) const {
        for (const Method& m : d.methods)
            if (m.name == "<dtor>") return true;
        if (d.base >= 0 && class_needs_dtor(structs[static_cast<size_t>(d.base)])) return true;
        for (const StructDef::BaseInfo& bi : d.bases)
            if (class_needs_dtor(structs[static_cast<size_t>(bi.sid)])) return true;
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
        if (sd.poly || sd.base >= 0 || sd.fields.empty() || has_ctor_template(sid)) return false;
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
                if (f.name.compare(0, 6, "__vptr") == 0) continue;
                if (k >= args.size()) break;
                Node fn;
                fn.kind = Node::Field;
                fn.op = TArrow;
                fn.text = f.name;
                fn.kids.push_back(self);
                if (dflt == nullptr && f.type.base == 3 && f.type.pointers == 0 && !f.type.is_ref && !f.type.is_array() &&
                    f.type.struct_id >= 0 && obj_needs_ctor(f.type)) {
                    // No default constructor ran (the class has none: a member has no default constructor), so a
                    // member of class type is made from its item by its own constructor ([dcl.init.aggr] 4.2).
                    std::vector<Node> margs;
                    margs.push_back(std::move(args[k]));
                    ++k;
                    Node mcall;
                    if (!special_call(f.type.struct_id, "<ctor>", node_addr(std::move(fn)), std::move(margs), &mcall))
                        return false;
                    steps.push_back(std::move(mcall));
                    continue;
                }
                if (obj_needs_ctor(f.type) || obj_needs_dtor(f.type)) {
                    // A member of class type set from its item by the class's own operator= (G g{"text"} with a
                    // std::string member; before Round 29 the item's bytes were stored into the member).
                    Node lhs = fn;
                    Node rhs = args[k];
                    bool done = false;
                    if (!operator_binary(TAssign, &lhs, &rhs, &done)) return false;
                    if (done) {
                        ++k;
                        Node st;
                        st.kind = Node::ExprStmt;
                        st.kids.push_back(std::move(lhs));
                        steps.push_back(std::move(st));
                        continue;
                    }
                }
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
        // A copy: adjust_args can add constructor template instances to a class and move its methods.
        Method m_copy;
        if (m != nullptr) {
            m_copy = *m;
            m = &m_copy;
        }
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
        // The locals declared so far (Node::vis): the name means one of them when it can (Round 30).
        name.vis = local_seq + 1;
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
                if (f.name.compare(0, 6, "__vptr") == 0) continue;
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
        // A pointer to a data member left out, or given nullptr, is null: -1 ([dcl.init] 9, value-initialization).
        if (type.memptr >= 0 && type.pointers == 0 && !type.is_array()) class_ptr_convert(&value, type);
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
    // The table pointer stores of a class's constructor and destructor: its own table at offset 0, then (Round 29) the
    // table of each base subobject that is not at offset 0: *(void ***)((char *)this + off) = (void **)&table.
    void vptr_stores(const StructDef& def, std::vector<Node>* out) const {
        if (!def.poly) return;
        out->push_back(vptr_store_this(def.vtable_sym));
        Type pp;
        pp.base = 0;
        pp.pointers = 2;
        for (const StructDef::SecTable& st : def.sec_tables) {
            Node slot;
            slot.kind = Node::Unary;
            slot.op = TStar;
            Type ppp = pp;
            ppp.pointers = 3;
            Node at_off = offset_ptr(node_name("this"), st.off, st.sid);
            slot.kids.push_back(cast_node(ppp, std::move(at_off)));
            Node store;
            store.kind = Node::Assign;
            store.op = TAssign;
            store.kids.push_back(std::move(slot));
            store.kids.push_back(cast_node(pp, node_addr(node_name(st.sym))));
            Node stmt;
            stmt.kind = Node::ExprStmt;
            stmt.kids.push_back(std::move(store));
            out->push_back(std::move(stmt));
        }
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
    void next() {
        if (tokens[at].kind == TStatic && tokens[at].tls) pending_tls = true;
        if (at + 1 < tokens.size()) ++at;
    }
    // A thread_local was read for the declaration being parsed; the variable it makes takes it (and clears it).
    bool pending_tls = false;
    bool local_decl_tls = false;  // the block-scope declaration being read is thread_local
    bool take_tls() {
        const bool t = pending_tls;
        pending_tls = false;
        return t;
    }
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

    // typeid ([expr.typeid]): one type information object per type and file, `__okay_ti_` and the decorated name of the
    // type. Its layout is std::type_info's in <typeinfo>: one pointer to the decorated name as text (".?AVC@@" for a
    // class C, "H" for int). Objects of two files for the same type compare equal by that text, not by address. Own
    // model, not the MSVC run-time type information records (no ??_R0 type descriptor, no complete object locator).
    std::string type_info_symbol(int sid, const std::string& decorated) {
        const std::string text = sid >= 0 ? ".?A" + std::string(structs[static_cast<size_t>(sid)].is_union ? "T" :
                                                                structs[static_cast<size_t>(sid)].is_class ? "V" : "U") +
                                                decorated + "@@"
                                          : decorated;
        std::string sym;
        for (char c : text) sym += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '$';
        sym = "__okay_ti_" + sym;
        for (const Global& g : globals)
            if (g.name == sym) return sym;
        Global name;
        name.name = sym + "_name";
        name.is_static = true;
        name.bss = false;
        name.type.base = 2;
        name.type.array_len = static_cast<int>(text.size()) + 1;
        name.init_bytes.assign(text.begin(), text.end());
        name.init_bytes.push_back(0);
        globals.push_back(name);
        // A second pointer (Round 29; Round 26 had one base): to the list of the class's direct bases, each the address
        // of its type information and its offset in the class (8 + 8 bytes), ended by a zero entry; 0 when the class has
        // no base or the type is not a class. dynamic_cast walks these from the dynamic type to the class it wants.
        std::string base_sym;
        if (sid >= 0 && (structs[static_cast<size_t>(sid)].base >= 0 || !structs[static_cast<size_t>(sid)].bases.empty())) {
            std::vector<std::pair<int, int>> list;  // (base, offset)
            for (const StructDef::BaseInfo& bi : structs[static_cast<size_t>(sid)].bases) list.push_back({bi.sid, bi.off});
            if (list.empty()) list.push_back({structs[static_cast<size_t>(sid)].base, 0});
            Global bases;
            bases.name = sym + "_bases";
            bases.is_static = true;
            bases.bss = false;
            bases.type.base = 0;
            bases.type.pointers = 1;
            bases.type.array_len = static_cast<int>(list.size() * 2 + 2);
            bases.init_bytes.assign((list.size() + 1) * 16, 0);
            for (size_t k = 0; k < list.size(); ++k) {
                const std::string bti = type_info_symbol(list[k].first, structs[static_cast<size_t>(list[k].first)].symname());
                bases.addr_relocs.push_back({static_cast<uint32_t>(k * 16), bti});
                for (int b = 0; b < 8; ++b)
                    bases.init_bytes[k * 16 + 8 + static_cast<size_t>(b)] =
                        static_cast<uint8_t>(static_cast<uint64_t>(static_cast<long long>(list[k].second)) >> (8 * b));
            }
            base_sym = bases.name;
            globals.push_back(std::move(bases));
        }
        Global ti;
        ti.name = sym;
        ti.is_static = true;
        ti.bss = false;
        ti.type.base = 0;
        ti.type.pointers = 1;
        ti.type.array_len = 2;
        ti.init_bytes.assign(16, 0);
        ti.addr_relocs.push_back({0u, name.name});
        if (!base_sym.empty()) ti.addr_relocs.push_back({8u, base_sym});
        globals.push_back(ti);
        return sym;
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
    // The value comes from the class's own member table (the flat enum_values keeps only the last enumerator of a
    // name, so A::Size would read B's). A name that is not an enumerator of that class (or its bases) is not taken here.
    bool class_enumerator(const std::string& name, uint64_t* value) const {
        const size_t last = name.rfind("::");
        if (last == std::string::npos || local_visible(name)) return false;
        const std::string qual = name.substr(0, last);
        const size_t prev = qual.rfind("::");
        const std::string cls = prev == std::string::npos ? qual : qual.substr(prev + 2);
        const std::string member = name.substr(last + 2);
        if (enum_values.find(member) == enum_values.end()) return false;
        int sid = find_struct(cls);
        if (sid < 0) {
            // K::Key with `using K = BtParamDef::Kind;` (an alias of an unscoped enumeration; Round 30): the enumerator
            // when the flat table says it belongs to that enumeration.
            const auto td = typedefs.find(cls);
            const auto eo = enum_of.find(member);
            if (td == typedefs.end() || !td->second.is_enum() || td->second.pointers != 0 || eo == enum_of.end() ||
                eo->second != td->second.enum_id)
                return false;
            *value = enum_values.at(member);
            return true;
        }
        const auto& statics = structs[static_cast<size_t>(sid)].statics;
        const auto it = statics.find(member);
        if (it == statics.end() || !it->second.has_const) return false;
        *value = static_cast<uint64_t>(it->second.cval);
        return true;
    }
    // A class of this name nested in a class being read or in the class whose member function is being compiled (or a
    // class around those), or -1.
    int nested_class_in_scope(const std::string& name) const {
        auto search_up = [&](int ctx) {
            for (int depth = 0; ctx >= 0 && static_cast<size_t>(ctx) < structs.size() && depth < 64; ++depth) {
                for (size_t i = 0; i < structs.size(); ++i)
                    if (structs[i].name == name && structs[i].parent == ctx) return static_cast<int>(i);
                ctx = structs[static_cast<size_t>(ctx)].parent;
            }
            return -1;
        };
        for (size_t k = open_structs.size(); k-- > 0;) {
            const int hit = search_up(open_structs[k]);
            if (hit >= 0) return hit;
        }
        return search_up(cur_class);
    }
    int find_struct(const std::string& name) const {
        int first = -1;
        int count = 0;
        for (size_t i = 0; i < structs.size(); ++i)
            if (structs[i].name == name) {
                if (first < 0) first = static_cast<int>(i);
                ++count;
            }
        if (count <= 1) return first;
        // Several classes of this name ([basic.lookup.unqual], [class.nest]): the one nested in the class being read, or
        // in the class whose member function is being compiled, or in a class around those (innermost first); then
        // one at namespace scope; else the first.
        auto nested_in = [&](int ctx) {
            for (size_t i = 0; i < structs.size(); ++i)
                if (structs[i].name == name && structs[i].parent == ctx) return static_cast<int>(i);
            return -1;
        };
        auto search_up = [&](int ctx) {
            for (int depth = 0; ctx >= 0 && static_cast<size_t>(ctx) < structs.size() && depth < 64; ++depth) {
                const int hit = nested_in(ctx);
                if (hit >= 0) return hit;
                ctx = structs[static_cast<size_t>(ctx)].parent;
            }
            return -1;
        };
        for (size_t k = open_structs.size(); k-- > 0;) {
            const int hit = search_up(open_structs[k]);
            if (hit >= 0) return hit;
        }
        const int in_method = search_up(cur_class);
        if (in_method >= 0) return in_method;
        // At namespace scope ([namespace.qual], [basic.lookup.unqual]): the namespace written in front of the name
        // (okay::crypto::Sha256, also as a suffix: crypto::Sha256), else the namespace being read or the nearest one
        // around it.
        const std::string qual = at < tokens.size() && tokens[at].text == name ? tokens[at].qual : std::string();
        auto ns_match = [](const std::string& ns, const std::string& q) {
            return ns == q || (ns.size() > q.size() && ns.compare(ns.size() - q.size(), q.size(), q) == 0 &&
                               ns.compare(ns.size() - q.size() - 2, 2, "::") == 0);
        };
        if (!qual.empty())
            for (size_t i = 0; i < structs.size(); ++i)
                if (structs[i].name == name && structs[i].parent < 0 && ns_match(structs[i].ns, qual)) return static_cast<int>(i);
        int best = -1;
        size_t best_len = 0;
        for (size_t i = 0; i < structs.size(); ++i) {
            if (structs[i].name != name || structs[i].parent >= 0) continue;
            const std::string& ns = structs[i].ns;
            const bool encloses = ns.empty() || current_ns == ns ||
                                  (current_ns.size() > ns.size() && current_ns.compare(0, ns.size(), ns) == 0 &&
                                   current_ns.compare(ns.size(), 2, "::") == 0);
            if (encloses && (best < 0 || ns.size() > best_len)) {
                best = static_cast<int>(i);
                best_len = ns.size();
            }
        }
        if (best >= 0) return best;
        const int top = nested_in(-1);
        return top >= 0 ? top : first;
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
    int local_seq = 0;  // locals declared so far, in the whole file (Local::seq)
    void place_local(std::vector<Local>* locals, Local local) {
        const int used = locals->empty() ? 0 : -locals->back().offset;
        int align = align_of(local.type);
        int size = size_of(local.type);
        if (align < 1) align = 1;
        if (size < 1) size = 1;
        const int aligned = (used + align - 1) & ~(align - 1);
        local.offset = -(aligned + size);
        local.scope = scope_depth;
        local.seq = ++local_seq;
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
            if (t->base == 3 && t->struct_id >= 0 && t->pointers == 0 &&
                !(at + 2 < tokens.size() && tokens[at + 2].kind == TLParen)) {
                // Character::Bone where Bone is a class nested in Character: that class, also when another namespace
                // has a type of the same name (HumanoidMotion::Bone, an enumeration; Round 29).
                int nested = -1;
                for (size_t k = 0; k < structs.size(); ++k)
                    if (structs[k].parent == t->struct_id && structs[k].name == tokens[at + 1].text) nested = static_cast<int>(k);
                if (nested >= 0) {
                    t->struct_id = nested;
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
        bool lead_const = false;  // only kept for a member pointer's member type (`const int S::*`)
        while (accept(TConst)) lead_const = true;
        // const ::okay::Vec3&: a type named from the global namespace (see parse_stmt_inner).
        if (is(TColonColon) && at + 1 < tokens.size() && tokens[at + 1].kind == TIdent) next();
        // std::string(int, float) in function<std::string(int, float)>: the namespace names were kept because a '('
        // follows; where a type is read they are dropped here.
        while (is(TIdent) && namespaces.count(cur().text) > 0 && at + 2 < tokens.size() &&
               tokens[at + 1].kind == TColonColon && tokens[at + 2].kind == TIdent)
            at += 2;
        if (!parse_type_core(type, imported, exported)) return false;
        while (accept(TConst)) lead_const = true;
        if (!memptr_suffix(type, lead_const)) return false;
        return true;
    }
    // After the type T: `C::*` makes it a pointer to a data member of C ([dcl.mptr]; Round 30): float VehicleTuning::*f,
    // const std::string UIFocusHint::*link, I Owner::*m in a template. `T (C::*f)(...)`, a pointer to a member function,
    // is not read.
    bool memptr_suffix(Type* type, bool member_const = false) {
        size_t k = at;
        bool found = false;
        while (k + 2 < tokens.size() && tokens[k].kind == TIdent && tokens[k + 1].kind == TColonColon) {
            if (tokens[k + 2].kind == TStar) {
                found = true;
                break;
            }
            k += 2;
        }
        if (!found) return true;
        Type cls;
        bool imp = false;
        bool exp = false;
        if (!parse_type_core(&cls, &imp, &exp)) return false;
        if (!expect(TColonColon, "'::'") || !expect(TStar, "'*'")) return false;
        while (accept(TConst)) {
        }
        if (!(cls.base == 3 || cls.base >= 100) || cls.pointers != 0) {
            error = "a pointer to member needs a class";
            return false;
        }
        Type member = *type;
        member.is_ref = false;
        member.rref = false;
        if (member_const && member.pointers == 0) member.is_const = true;
        Type mp;
        mp.base = 1;
        mp.memptr = static_cast<int>(memptr_types().size());
        memptr_types().emplace_back(cls, member);
        *type = mp;
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
        type->deduce = 0;
        if (is(TIdent) && cur().text == "auto") {
            // A placeholder type ([dcl.spec.auto]); only a function's return type is read this way (auto variables are
            // read where they are declared). The type is int until the function's body tells.
            next();
            type->deduce = 1;
            return true;
        }
        if (is(TIdent) && cur().text == "decltype" && at + 3 < tokens.size() && tokens[at + 1].kind == TLParen &&
            tokens[at + 2].kind == TIdent && tokens[at + 2].text == "auto" && tokens[at + 3].kind == TRParen) {
            at += 4;
            type->deduce = 2;
            return true;
        }
        if (is(TIdent) && cur().text == "decltype" && at + 1 < tokens.size() && tokens[at + 1].kind == TLParen) {
            // decltype(expression) ([dcl.type.decltype]): the expression's type as okay-ms knows it. Own simplification:
            // a reference is not added for an lvalue in parentheses, and the expression is not evaluated (its code is
            // dropped; a lambda in it is still made).
            next();
            next();
            Node e;
            const std::vector<Node> keep_pre = stmt_pre;
            const std::vector<Temp> keep_temps = stmt_temps;
            if (!parse_expr(1, &e)) return false;
            stmt_pre = keep_pre;
            stmt_temps = keep_temps;
            if (!expect(TRParen, "')'")) return false;
            Type t;
            bool outer_found = false;
            if (e.kind == Node::Name && !local_visible(e.text))
                for (size_t li = lam_outer_locals_.size(); li-- > 0 && !outer_found;) {
                    const std::vector<Local>* outer = lam_outer_locals_[li];
                    if (outer == nullptr) continue;
                    for (size_t k = outer->size(); k-- > 0;)
                        if ((*outer)[k].name == e.text) {
                            t = (*outer)[k].type;
                            outer_found = true;
                            break;
                        }
                }
            if (!outer_found && !static_type(e, &t)) {
                error = "decltype of an expression whose type is not known";
                return false;
            }
            t.is_ref = false;
            t.rref = false;
            *type = t;
            return true;
        }
        if (is(TStruct) || is(TUnion)) {
            const bool as_union = is(TUnion);
            const bool as_class = is(TStruct) && cur().text == "class";
            next();
            std::string name;
            if (is(TIdent)) {
                name = cur().text;
                next();
            }
            // struct Outer::Inner { ... }; at namespace scope: the definition of a class declared in Outer ([class.nest]
            // 3). It is read as if inside Outer (its parent), so it completes Outer's declaration of Inner.
            int defined_in = -1;
            if (!name.empty() && is(TColonColon) && at + 2 < tokens.size() && tokens[at + 1].kind == TIdent &&
                (tokens[at + 2].kind == TLBrace || tokens[at + 2].kind == TColon) && find_struct(name) >= 0) {
                defined_in = find_struct(name);
                next();
                name = cur().text;
                next();
            }
            // Outer's member types (typedef and using names) are names inside Inner's body too ([class.nest] 1).
            struct OuterOpen {
                Parser& p;
                bool pushed;
                TdefSaves shadowed;
                ~OuterOpen() {
                    if (pushed) p.open_structs.pop_back();
                    p.unbind_class_tdefs(shadowed);
                }
            } outer_open{*this, defined_in >= 0, {}};
            if (defined_in >= 0) {
                open_structs.push_back(defined_in);
                outer_open.shadowed = bind_class_tdefs(defined_in);
            }
            // The base classes ([class.derived] 1): one or more, each with an optional access word; with several,
            // each base is its own subobject (Round 29, [class.mi]).
            std::vector<int> base_ids;
            if (accept(TColon)) {
                while (true) {
                    bool virt = false;
                    while (is(TIdent) && (cur().text == "public" || cur().text == "private" ||
                                          cur().text == "protected" || cur().text == "virtual")) {
                        if (cur().text == "virtual") virt = true;
                        next();
                    }
                    int base_id = -1;
                    if (is(TIdent) && (is_template_name_at(at) || (typedefs.count(cur().text) > 0 &&
                                                                     find_struct(cur().text) < 0) ||
                                       (at + 1 < tokens.size() && tokens[at + 1].kind == TColonColon))) {
                        // class X : public Base<X> ([class.derived], [temp.names]): a base named by a template-id, a
                        // typedef or a qualified name. X is declared (incomplete) first, so the base's arguments may name
                        // it (CRTP).
                        if (!name.empty() && find_struct(name) < 0) {
                            StructDef fwd;
                            fwd.name = name;
                            fwd.incomplete = true;
                            fwd.ns = current_ns;
                            if (!open_structs.empty()) fwd.parent = open_structs.back();  // class X; inside a class
                            fwd.cxx = true;
                            fwd.is_class = as_class;
                            fwd.is_union = as_union;
                            structs.push_back(fwd);
                        }
                        Type bt;
                        bool imp = false;
                        bool exp = false;
                        if (!parse_type(&bt, &imp, &exp)) return false;
                        if (bt.base != 3 || bt.pointers != 0 || bt.struct_id < 0) {
                            error = "expected a base class";
                            return false;
                        }
                        base_id = bt.struct_id;
                    } else {
                        if (!is(TIdent) || find_struct(cur().text) < 0) {
                            error = "expected a base class";
                            return false;
                        }
                        base_id = find_struct(cur().text);
                        next();
                    }
                    if (virt && (!base_ids.empty() || is(TComma))) {
                        error = "virtual base classes are not supported with several bases";
                        return false;
                    }
                    base_ids.push_back(base_id);
                    if (!accept(TComma)) break;
                }
                if (!is(TLBrace)) {
                    error = "expected '{' after the base classes";
                    return false;
                }
            }
            if (is(TLBrace)) {
                int id = -1;
                if (!parse_struct_body(as_union, name, &id, as_class, base_ids)) return false;
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
                fwd.ns = current_ns;
                if (!open_structs.empty()) fwd.parent = open_structs.back();  // class X; inside a class
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
                enum_type.enum_id = static_cast<int>(enum_names().size());
                enum_names().push_back(cur().text);
                {
                    EnumDecl ed;
                    ed.name = cur().text;
                    ed.type = enum_type;
                    const auto old = typedefs.find(ed.name);
                    ed.had = old != typedefs.end();
                    if (ed.had) ed.old = old->second;
                    ed.nest = struct_nest;
                    enum_types_declared.push_back(ed);
                }
                typedefs[cur().text] = enum_type;
                cur_enum_id = enum_type.enum_id;
                next();
            }
            const bool enum_ok = parse_enum_body();
            cur_enum_id = -1;
            if (!enum_ok) return false;
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
        if (is(TIdent) && typedefs.find(cur().text) != typedefs.end() && nested_class_in_scope(cur().text) < 0) {
            *type = typedefs[cur().text];
            next();
            return qualify(type, had_typename);
        }
        if (is(TIdent) && nested_class_in_scope(cur().text) >= 0) {
            // State inside AnimStateMachine, which has a nested class State, while another class's enumeration State is
            // in the one table of type names: the nested class ([basic.lookup.unqual] 8; Round 29).
            type->base = 3;
            type->struct_id = nested_class_in_scope(cur().text);
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
            if (cur_enum_id >= 0) enum_of[name] = cur_enum_id;
            else enum_of.erase(name);
            enums_declared.push_back(name);  // the class being read gets it as a member (parse_struct_body)
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
                if (f.name.compare(0, 6, "__vptr") != 0) parts.push_back(f.type);
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
    void memptr_null_bytes(const Type& type, std::vector<uint8_t>* bytes, int at) const;
    int conv_depth = 0;
    // The enumeration being read and, for each enumerator of a named enumeration, its enum_id: an enumerator's type is
    // its enumeration ([dcl.enum] 5), so a template deduces the enumeration from it.
    int cur_enum_id = -1;
    std::map<std::string, int> enum_of{};
    int enum_id_of(const std::string& name) const {
        const auto it = enum_of.find(name);
        return it == enum_of.end() ? -1 : it->second;
    }
    // A call through a function pointer of type fpt: the arguments from index `first` are bound as for a direct call
    // (a reference parameter gets an address, a temporary or a converted value).
    bool adjust_fp_args(const Type& fpt, std::vector<Node>* args, size_t first) {
        if (fpt.func_id < 0 || fpt.func_id >= static_cast<int>(func_types.size())) return true;
        const std::vector<Type> params = func_types[static_cast<size_t>(fpt.func_id)].params;
        return adjust_args(params, args, first);
    }
    // A call through a pointer to a function that returns a reference: the call gives an address, read through.
    void fp_reference_result(const Type& fpt, Node* call) {
        if (fpt.func_id < 0 || fpt.func_id >= static_cast<int>(func_types.size())) return;
        FuncType& ft = func_types[static_cast<size_t>(fpt.func_id)];
        if (!ft.ret.is_ref) return;
        const bool rref = ft.ret.rref;
        call->type = ft.ret;
        *call = reference_result(std::move(*call), true, rref);
    }
    // A name that is a function (and not a local or global variable).
    bool names_function(const std::string& name) const {
        if (local_visible(name)) return false;
        for (const Global& g : globals)
            if (g.name == name) return false;
        for (const FuncDecl& f : funcs)
            if (f.name == name) return true;
        return false;
    }
    // Members of class template instances whose bodies did not compile, with the error: a call of one is an error.
    std::map<std::string, std::string> unusable_members{};
    bool find_unusable_call(const Node& n, std::string* which) const {
        if (n.kind == Node::Call && unusable_members.count(n.text) > 0) {
            *which = n.text;
            return true;
        }
        for (const Node& k : n.kids)
            if (find_unusable_call(k, which)) return true;
        return false;
    }
    // Every enumerator name in the order read; a class body takes the ones declared inside it as members.
    std::vector<std::string> enums_declared{};
    // The named enumerations declared so far, with their types: the class being read keeps those of its body as member
    // types (StructDef::tdefs), so HealthWatch::Event finds the class's enumeration though a class Event exists
    // elsewhere, also in a member defined outside the class (Round 30).
    struct EnumDecl {
        std::string name;
        Type type{};
        bool had = false;  // the type-name table had this name before (put back at the end of the class)
        Type old{};
        int nest = 0;      // struct_nest where it was declared
    };
    std::vector<EnumDecl> enum_types_declared{};
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
            {
                // R& (*f)(A): the '*' and '&' before the parenthesis belong to the return type.
                size_t k = at;
                while (k < tokens.size() && (tokens[k].kind == TStar || tokens[k].kind == TAmp || tokens[k].kind == TAndAnd)) ++k;
                if (k > at && k + 1 < tokens.size() && tokens[k].kind == TLParen && tokens[k + 1].kind == TStar) {
                    while (at < k) {
                        if (is(TStar)) ++pt.pointers;
                        else pt.is_ref = true;
                        next();
                    }
                }
            }
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
                pt.is_ref = false;  // a reference result is in ft.ret; the parameter itself is a pointer
                pt.rref = false;
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
                // (A class-template type read as a pattern, `const std::vector<T>& v = {}`, has no class yet: tpat.)
                if ((is(TLBrace) && pt.base == 3 && pt.pointers == 0 && (pt.struct_id >= 0 || pt.tpat >= 0)) ||
                    (is(TIdent) && [&]() {
                         // A class named directly, or through a typedef of a class, maybe qualified (`std::string()`,
                         // where string is a typedef of basic_string<char>; Round 31, SaveFramework.hpp's SaveWriter).
                         size_t k = at;
                         while (k + 2 < tokens.size() && tokens[k].kind == TIdent && tokens[k + 1].kind == TColonColon &&
                                tokens[k + 2].kind == TIdent)
                             k += 2;
                         if (!(k + 1 < tokens.size() && (tokens[k + 1].kind == TLParen || tokens[k + 1].kind == TLBrace)))
                             return false;
                         const std::string& tn = tokens[k].text;
                         const auto td = typedefs.find(tn);
                         if (td == typedefs.end()) return find_struct(tn) >= 0;
                         return td->second.base == 3 && td->second.pointers == 0 && !td->second.is_array() &&
                                td->second.struct_id >= 0;
                     }())) {
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
        {
            // R* (*f)(A) and R& (*f)(A): the '*' and '&' before the parenthesis belong to the return type.
            size_t k = at;
            while (k < tokens.size() && (tokens[k].kind == TStar || tokens[k].kind == TAmp || tokens[k].kind == TAndAnd)) ++k;
            if (k > at && k + 1 < tokens.size() && tokens[k].kind == TLParen && tokens[k + 1].kind == TStar) {
                while (at < k) {
                    if (is(TStar)) ++base.pointers;
                    else base.is_ref = true;
                    next();
                }
            }
        }
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
            out->type.is_ref = false;  // a reference result is in ft.ret
            out->type.rref = false;
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
        // int (max)(int a, int b): a declarator-id in parentheses is the same declarator ([dcl.meaning] 6), written so
        // that a function-like macro of the name is not expanded (Round 31).
        const bool paren_id = is(TLParen) && at + 2 < tokens.size() && tokens[at + 1].kind == TIdent &&
                              tokens[at + 2].kind == TRParen;
        if (paren_id) next();
        if (!is(TIdent)) {
            error = "expected a name";
            return false;
        }
        out->name = cur().text;
        next();
        if (paren_id) next();  // ')'
        if (!parse_brackets(&out->type)) return false;
        if (!is(TLParen)) return true;
        next();
        out->is_function = true;
        if (!parse_param_list(out)) return false;
        if (out->type.deduce == 1 && is(TArrow)) {
            // auto f(A) -> R ([dcl.fct]): a trailing return type. Own limit: it must follow the ')' directly.
            next();
            Type r;
            bool imp = false;
            bool exp = false;
            const bool lead_const = accept(TConst);
            // The parameters are in scope in a trailing return type ([basic.scope.param]): -> decltype(f()).
            std::vector<Local> param_locals;
            for (size_t i = 0; i < out->params.size() && i < out->param_names.size(); ++i) {
                if (out->param_names[i].empty()) continue;
                Local pl;
                pl.name = out->param_names[i];
                pl.type = out->params[i];
                param_locals.push_back(pl);
            }
            std::vector<Local>* const saved_locals = cur_locals;
            if (!param_locals.empty()) cur_locals = &param_locals;
            const bool type_ok = parse_type(&r, &imp, &exp);
            cur_locals = saved_locals;
            if (!type_ok) return false;
            if (accept(TConst) || lead_const) r.pconst = true;
            while (accept(TStar)) ++r.pointers;
            if (accept(TAmp)) r.is_ref = true;
            else if (accept(TAndAnd)) {
                r.is_ref = true;
                if (r.pointers == 0) r.rref = true;
            }
            out->type = r;
        }
        return true;
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
        // UIBind::Mode with Mode an enumeration declared in the class (enumeration names are kept in the one table of
        // type names): a type, unless the class has a member of that name (Round 29, UIWidgetsMore.hpp).
        const StructDef& sd = structs[static_cast<size_t>(id)];
        if (typedefs.count(member) > 0 && sd.statics.count(member) == 0 && find_field_named(sd, member) < 0) {
            bool method = false;
            for (const Method& m : sd.methods)
                if (m.name == member) method = true;
            if (!method) return false;
        }
        return find_struct(member) < 0;
    }

    bool extern_decl_ = false;  // `extern` was read before the declaration being parsed

    // True inside a member function of a class with a data member of this name: the member hides a type of the same
    // name ([basic.scope.hiding]; `duration` in a Tween, `thread` in a watcher, while std::chrono::duration and
    // std::thread are bare type names here).
    // `next` is the kind of the token after the name: a name followed by another name is a declaration (`Grid g;` with a
    // class Grid declared in the function, though the class also has a member function Grid), so it is not hidden.
    bool member_hides(const std::string& name, Tok next = TEnd) const {
        if (cur_class < 0 || static_cast<size_t>(cur_class) >= structs.size()) return false;
        if (next == TIdent) return false;
        const StructDef& d = structs[static_cast<size_t>(cur_class)];
        if (find_field_named(d, name) >= 0) return true;
        // A member function hides it too when it is called: `Bounds(lo, hi);` in a Mesh member calls Mesh::Bounds,
        // though a struct Bounds exists elsewhere.
        if (next != TLParen) return false;
        for (const Method& m : d.methods)
            if (m.name == name) return true;
        return false;
    }

    bool is_decl_start() const {
        if (qualified_expr_ahead()) return false;
        // A parameter or local variable hides a type of the same name ([basic.scope.hiding]): `(seconds * rate)` with a
        // parameter `seconds` is not a cast to std::chrono::seconds (namespaces are not kept apart here).
        if (is(TIdent) && !cur().nsq && cur_locals != nullptr && local_visible(cur().text)) return false;
        // A data member hides a type of the same name inside its class's member functions: `thread = CreateThread(...)`
        // with a member `HANDLE thread` assigns to the member (std::thread is a bare type name here).
        if (is(TIdent) && !cur().nsq && member_hides(cur().text, at + 1 < tokens.size() ? tokens[at + 1].kind : TEnd))
            return false;
        if (is(TInt) || is(TVoid) || is(TChar) || is(TUnsigned) || is(TSigned) || is(TLong) || is(TShort) ||
            is(TStruct) || is(TUnion) || is(TEnum) || is(TTypedef) || is(TDeclspec) || is(TStatic) || is(TConst) ||
            is(TFloat) || is(TDouble))
            return true;
        if (is(TIdent) && cur().text == "decltype" && at + 1 < tokens.size() && tokens[at + 1].kind == TLParen) return true;
        return (is(TIdent) && typedefs.find(cur().text) != typedefs.end()) || is_class_name() ||
               is_template_name_at(at) || (is(TIdent) && cur().text == "typename") ||
               (is(TIdent) && cur().text == "auto" && at + 1 < tokens.size() &&
                (tokens[at + 1].kind == TIdent || tokens[at + 1].kind == TAmp || tokens[at + 1].kind == TStar ||
                 tokens[at + 1].kind == TConst || tokens[at + 1].kind == TAndAnd ||
                 tokens[at + 1].kind == TLBrack));  // auto [a, b] = e; (Round 29)
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
        if (i >= 1 && tokens[i - 1].kind == TIdent && tokens[i - 1].text == "new") return true;  // new S, new S()
        if (i >= 2 && tokens[i - 1].kind == TRParen && i + 1 < tokens.size() && tokens[i + 1].kind == TLParen) {
            // new (place) S(args): the '(' of the placement goes back to a `new`
            int depth = 0;
            for (size_t k = i - 1; k > 0; --k) {
                if (tokens[k].kind == TRParen) ++depth;
                else if (tokens[k].kind == TLParen && --depth == 0) {
                    if (tokens[k - 1].kind == TIdent && tokens[k - 1].text == "new") return true;
                    break;
                }
            }
        }
        if (i + 2 < tokens.size() && tokens[i + 1].kind == TColonColon && tokens[i + 2].kind == TStar)
            return true;  // S::* in a pointer to member (Round 30)
        if (i + 3 < tokens.size() && tokens[i + 1].kind == TColonColon && tokens[i + 2].kind == TIdent &&
            (find_struct(tokens[i + 2].text) >= 0 || typedefs.count(tokens[i + 2].text) > 0 ||
             structs[static_cast<size_t>(id)].tdefs.count(tokens[i + 2].text) > 0) &&
            tokens[i + 3].kind != TLParen)
            return true;  // S::Nested names a nested class, enumeration or typedef of a plain struct
        if (i >= 1 && tokens[i - 1].kind == TLt && i + 1 < tokens.size() && tokens[i + 1].kind == TLParen)
            return true;  // a function type as a template argument: std::function<S(int)> ([dcl.fct])
        return i + 1 < tokens.size() &&
               (tokens[i + 1].kind == TIdent || tokens[i + 1].kind == TStar || tokens[i + 1].kind == TAmp ||
                tokens[i + 1].kind == TAndAnd ||
                ((tokens[i + 1].kind == TComma || tokens[i + 1].kind == TGt) && i >= 1 &&
                 (tokens[i - 1].kind == TLt || tokens[i - 1].kind == TComma ||
                  tokens[i - 1].kind == TAssign ||  // a default template argument: template <class H = Tag>
                  (tokens[i - 1].kind == TConst && i >= 2 &&
                   (tokens[i - 2].kind == TLt || tokens[i - 2].kind == TComma)))));  // a template argument: Box<P, 3>, Box<const P>
    }
    bool is_class_name() const {
        if (!is(TIdent) || typedefs.find(cur().text) != typedefs.end()) return false;
        return class_name_at(at);
    }

    bool local_visible(const std::string& name) const;
    bool static_type(const Node& node, Type* out) const;
    bool parse_call_args(std::vector<Node>* args);
    bool expand_pack_arg(Node arg, std::vector<Node>* args);
    bool pack_expansion_items(bool allow_bare, std::vector<Node>* items, bool* done);
    std::vector<std::string> packs_in(size_t from, size_t to) const;
    bool parse_pack_element(size_t from, size_t to, size_t k, bool unary, Node* out);
    bool try_fold(Node* out, bool* folded);
    bool parse_new(Node* out);
    Node malloc_of(const Type& ty) const;
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
    bool parse_typeid(Node* out);
    bool skip_stmt();
    bool parse_type_trait(Node* out);
    bool parse_postfix(Node* out);
    bool member_pointer_ahead() const;
    bool parse_member_pointer(Node* out);
    bool apply_member_pointer(Node object, bool arrow, Node* out);
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
    // C::f or f (inside C) without a call, where f is C's only member function of that name and it is static: a pointer
    // to it ([expr.prim.id.qual], [conv.func]). The node is a cast of the function's name to the pointer type, marked
    // "<static-fn>" so that &C::f gives the same pointer.
    bool static_method_ref(int sid, const std::string& name, Node* out) {
        if (sid < 0 || static_cast<size_t>(sid) >= structs.size()) return false;
        const Method* found = nullptr;
        int count = 0;
        for (const Method& m : structs[static_cast<size_t>(sid)].methods)
            if (m.name == name) {
                ++count;
                found = &m;
            }
        if (count != 1 || !found->is_static) return false;
        FuncType ft;
        ft.ret = found->ret;
        ft.params = found->params;
        Type fp;
        fp.base = 1;
        fp.pointers = 1;
        fp.func_id = static_cast<int>(func_types.size());
        func_types.push_back(ft);
        Node fn_name;
        fn_name.kind = Node::Name;
        fn_name.text = found->qname;
        *out = Node{};
        out->kind = Node::Unary;
        out->type = fp;
        out->text = "<static-fn>";
        out->kids.push_back(std::move(fn_name));
        return true;
    }

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
    // The integer literal for the stored bits v of a constant of integer type t: bool is 0 or 1, char and short are cut to
    // their width (and sign-extended when signed), int and long long keep their width and signedness.
    static void int_literal_of(uint64_t v, const Type& t, Node* out) {
        *out = Node{};
        out->kind = Node::IntLit;
        out->type.base = 1;
        out->type.wide = t.wide;
        if (t.is_bool) v = v != 0 ? 1 : 0;
        else if (t.base == 2) v = t.is_unsigned ? (v & 0xFFu) : static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(v)));
        else if (t.base == 4) v = t.is_unsigned ? (v & 0xFFFFu) : static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(v)));
        else {
            out->type.is_unsigned = t.is_unsigned;
            v = const_norm(v, out->type);
        }
        out->number = v;
    }

    // The innermost visible local of this name, when it is a const integer with a constant value: that value as a
    // literal of its type.
    bool local_const_node(const std::string& name, Node* out) const {
        if (cur_locals == nullptr) return false;
        for (size_t i = cur_locals->size(); i-- > 0;) {
            const Local& local = (*cur_locals)[i];
            if (local.name != name || !scope_visible(local.scope)) continue;
            if (!local.has_cval) return false;
            int_literal_of(local.cval, local.type, out);
            return true;
        }
        return false;
    }

    // A global declared const (or constexpr) with a constant initial value, as a literal node.
    bool global_const_node(const std::string& name, Node* out) const {
        for (const Global& g : globals) {
            if (g.name != name) continue;
            const Type& t = g.type;
            if (!t.is_const || g.bss || g.dynamic || g.vtable || t.pointers != 0 || t.array_len >= 0 || t.is_ref) return false;
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
            int_literal_of(v, t, out);
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
        if (i == at + 1 && after != TLParen && after != TLBrace) {
            // C::E where E is a member type of the class C (an enumeration or a typedef; Round 30): a type.
            const int sid = scope_class(cur().text);
            if (sid >= 0 && structs[static_cast<size_t>(sid)].tdefs.count(tokens[i + 1].text) > 0) return false;
        }
        return after != TIdent && after != TStar && after != TAmp && after != TAndAnd && after != TColonColon;
    }
    // fn(args) where fn is a static data member of class sid holding an object with operator() (a std::function): a
    // call of that object ([expr.call], [over.call]), not of a member function.
    bool static_object_call(int sid, const std::string& name) const {
        if (sid < 0 || sid >= static_cast<int>(structs.size())) return false;
        const StructDef& def = structs[static_cast<size_t>(sid)];
        for (const Method& m : def.methods)
            if (m.name == name) return false;
        const auto st = def.statics.find(name);
        if (st == def.statics.end()) return false;
        const Type& t = st->second.type;
        return t.base == 3 && t.pointers == 0 && t.struct_id >= 0 && !st->second.has_const;
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
    // `auto [a, b]` (with & or && before the '['): a structured binding ([dcl.struct.bind]), Round 29.
    bool binding_ahead() const;
    bool parse_binding_decl(const Type& declared, bool is_const, Node* out, std::vector<Local>* locals, int stmt_line);
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
        int questions = 0;  // the ':' of c ? a : b in the init is not the range's ([stmt.ranged])
        for (size_t i = at; i < tokens.size() && tokens[i].kind != TEnd; ++i) {
            const Tok k = tokens[i].kind;
            if (k == TLParen || k == TLBrack || k == TLBrace) ++depth;
            else if (k == TRParen || k == TRBrack || k == TRBrace) {
                if (depth == 0) return false;
                --depth;
            } else if (depth == 0 && k == TSemi) return false;
            else if (depth == 0 && k == TQuestion) ++questions;
            else if (depth == 0 && k == TColon) {
                if (questions == 0) return true;
                --questions;
            }
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
    bool fold_loop_expr(size_t pre_from, size_t temps_from, Node* e);
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
    // The function whose returns the probe looks at (a lambda or a function inside it has its own returns), and
    // whether the first return's expression was a reference (for decltype(auto)).
    std::string lam_probe_fn{};
    bool lam_probe_ref = false;

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
    bool derives_from(int sid, int base) const { return base_offset(sid, base) >= 0; }
    // Where a base class's subobject is in an object of class sid (0 for the class itself, [class.mi]), or -1 when
    // base is not a base of it. With one base per class this is always 0.
    int base_offset(int sid, int base) const {
        if (sid == base) return 0;
        if (sid < 0 || sid >= static_cast<int>(structs.size()) || base < 0) return -1;
        const StructDef& d = structs[static_cast<size_t>(sid)];
        if (d.bases.empty()) return d.base >= 0 ? base_offset(d.base, base) : -1;
        for (const StructDef::BaseInfo& bi : d.bases) {
            const int inner = base_offset(bi.sid, base);
            if (inner >= 0) return bi.off + inner;
        }
        return -1;
    }
    // p moved by off bytes, as a pointer to class sid: the address of a base subobject (off > 0) or, from a base, of
    // the whole object (off < 0) ([conv.ptr] 3, [expr.static.cast] 11).
    Node offset_ptr(Node p, int off, int sid) const {
        Type to;
        to.base = 3;
        to.struct_id = sid;
        to.pointers = 1;
        if (off == 0) return cast_node(to, std::move(p));
        Type bytes;
        bytes.base = 2;
        bytes.pointers = 1;
        Node moved;
        moved.kind = Node::Binary;
        moved.op = off > 0 ? TPlus : TMinus;
        moved.kids.push_back(cast_node(bytes, std::move(p)));
        moved.kids.push_back(int_node(off > 0 ? off : -off));
        return cast_node(to, std::move(moved));
    }
    // The same, keeping a null pointer null: p ? p + off : 0. A value with side effects is computed once, into a
    // hidden local (in a comma expression, so it stays where it was written).
    Node offset_ptr_checked(Node p, int off, int sid) {
        if (off == 0) return p;
        const bool never_null = (p.kind == Node::Unary && p.op == TAmp) || (p.kind == Node::Name && p.text == "this");
        if (never_null) return offset_ptr(std::move(p), off, sid);
        Type to;
        to.base = 3;
        to.struct_id = sid;
        to.pointers = 1;
        Node assign;
        if (has_effects(p) && cur_locals != nullptr) {
            Type held;
            if (!static_type(p, &held)) held = void_ptr();
            held.is_ref = false;
            Local hidden;
            hidden.name = "__up" + std::to_string(tmp_count++);
            hidden.type = held;
            place_local(cur_locals, hidden);
            assign.kind = Node::Assign;
            assign.op = TAssign;
            assign.kids.push_back(node_name(hidden.name));
            assign.kids.push_back(std::move(p));
            p = node_name(hidden.name);
        }
        Node tern;
        tern.kind = Node::Ternary;
        tern.kids.push_back(p);
        tern.kids.push_back(offset_ptr(p, off, sid));
        tern.kids.push_back(cast_node(to, int_node(0)));
        if (assign.kind != Node::Assign) return tern;
        Node comma;
        comma.kind = Node::Binary;
        comma.op = TComma;
        comma.type = to;
        comma.kids.push_back(std::move(assign));
        comma.kids.push_back(std::move(tern));
        return comma;
    }
    // A value converted to `to`, a pointer to a class: when the value is a pointer to a class derived from it and the
    // base is not at offset 0, the address moves to the base subobject ([conv.ptr] 3). With `down`, a pointer to a
    // base converted to a pointer to a derived class moves back ([expr.static.cast] 11).
    void class_ptr_convert(Node* value, const Type& to, bool down = false) {
        if (to.memptr >= 0 && to.pointers == 0 && !to.is_ref) {
            if (memptr_null_constant(*value)) {
                *value = memptr_null(to);
                return;
            }
            memptr_convert(value, to, down);
            return;
        }
        if (to.pointers != 1 || to.base != 3 || to.struct_id < 0 || to.is_array() || to.is_ref) return;
        Type from;
        if (!static_type(*value, &from)) return;
        from.is_ref = false;
        if (from.pointers != 1 || from.base != 3 || from.struct_id < 0 || from.is_array() || from.struct_id == to.struct_id)
            return;
        if (from.func_id >= 0 || to.func_id >= 0) return;
        const int up = base_offset(from.struct_id, to.struct_id);
        if (up > 0) {
            *value = offset_ptr_checked(std::move(*value), up, to.struct_id);
            return;
        }
        if (!down || up == 0) return;
        const int back = base_offset(to.struct_id, from.struct_id);
        if (back > 0) *value = offset_ptr_checked(std::move(*value), -back, to.struct_id);
    }
    // A null pointer constant (nullptr or 0, an integer constant that is not itself a member pointer) converted to a
    // pointer to a data member ([conv.mem] 1).
    static bool memptr_null_constant(const Node& value) {
        return value.kind == Node::IntLit && value.type.memptr < 0 && value.type.base < 5 && value.number == 0;
    }
    // The null pointer to a data member of type t: -1 (Microsoft's representation; offset 0 is a real member).
    static Node memptr_null(const Type& t) {
        Node n = int_node(-1);
        n.type = t;
        n.type.is_const = false;
        return n;
    }
    // Whether an object of type t holds a pointer to a data member (in a member or an element, at any depth).
    bool has_memptr(const Type& t, int depth = 0) const {
        if (t.is_ref || t.pointers > 0 || depth > 16) return false;
        if (t.memptr >= 0) return true;
        if (t.is_array()) return has_memptr(peel_array(t), depth + 1);
        if (t.base != 3 || t.struct_id < 0 || t.struct_id >= static_cast<int>(structs.size())) return false;
        for (const Field& f : structs[static_cast<size_t>(t.struct_id)].fields)
            if (has_memptr(f.type, depth + 1)) return true;
        return false;
    }
    // A braced initializer of a local of type t (already regular, every level braced): a member pointer given as
    // nullptr or 0, or left out, is the null value -1 ([dcl.init.aggr] 5: a member left out is initialized from its
    // default member initializer or else from {}, which for a member pointer is its null value). Members left out
    // before the last member pointer are added as `{}` (zero) so the list reaches it.
    void memptr_fix_list(const Type& t, Node* node) {
        if (!has_memptr(t)) return;
        const bool list = node->kind == Node::Block && node->text == "init";
        if (t.memptr >= 0 && !t.is_array()) {
            if (list && node->kids.size() == 1) *node = node->kids[0];
            if (list && node->kids.empty()) *node = memptr_null(t);
            else class_ptr_convert(node, t);
            return;
        }
        if (!list) return;
        std::vector<Type> parts;
        if (t.is_array()) {
            const Type elem = peel_array(t);
            for (int i = 0; i < t.array_len; ++i) parts.push_back(elem);
        } else {
            for (const Field& f : structs[static_cast<size_t>(t.struct_id)].fields)
                if (f.name.compare(0, 6, "__vptr") != 0) parts.push_back(f.type);
        }
        size_t last = 0;  // one past the last part that holds a member pointer
        for (size_t i = 0; i < parts.size(); ++i)
            if (has_memptr(parts[i])) last = i + 1;
        while (node->kids.size() < last) {
            Node empty;
            empty.kind = Node::Block;
            empty.text = "init";
            empty.number = 7;
            const Type& part = parts[node->kids.size()];
            if (obj_needs_ctor(part)) break;  // a member with a constructor is left to it (own limit)
            if (part.memptr >= 0 && !part.is_array()) node->kids.push_back(memptr_null(part));
            else if (has_memptr(part) || part.is_array() || part.base == 3) node->kids.push_back(empty);
            else node->kids.push_back(int_node(0));
        }
        for (size_t i = 0; i < node->kids.size() && i < parts.size(); ++i) memptr_fix_list(parts[i], &node->kids[i]);
    }
    // A pointer to a data member used as a condition ([conv.bool]): `pm != -1` (Round 31, with the representation).
    void memptr_truth(Node* value) {
        Type t;
        if (!static_type(*value, &t) || t.memptr < 0 || t.pointers != 0 || t.is_array()) return;
        Node ne;
        ne.kind = Node::Binary;
        ne.op = TNe;
        ne.kids.push_back(std::move(*value));
        ne.kids.push_back(memptr_null(t));
        *value = std::move(ne);
    }
    // A pointer to a member of class B used as a pointer to a member of a class D derived from B ([conv.mem] 2), or
    // back with static_cast ([expr.static.cast] 12): the offset moves by where B is in D, and null (0) stays null
    // (Round 30: VehicleTuning's members as VehicleController's in BuiltinPropertiesGameplay.cpp).
    void memptr_convert(Node* value, const Type& to, bool down) {
        Type from;
        if (!static_type(*value, &from) || from.memptr < 0 || from.pointers != 0 || from.memptr == to.memptr) return;
        const Type fc = memptr_types()[static_cast<size_t>(from.memptr)].first;
        const Type tc = memptr_types()[static_cast<size_t>(to.memptr)].first;
        if (fc.base != 3 || tc.base != 3 || fc.struct_id < 0 || tc.struct_id < 0 || fc.struct_id == tc.struct_id) return;
        int off = base_offset(tc.struct_id, fc.struct_id);  // B's place in D
        if (off < 0 && down) {
            const int back = base_offset(fc.struct_id, tc.struct_id);
            off = back > 0 ? -back : -1;
            if (back <= 0) return;
        }
        if (off <= 0) return;
        if (value->kind == Node::IntLit) {
            if (static_cast<int32_t>(value->number) != -1) value->number += static_cast<uint64_t>(off);
            value->type = to;
            return;
        }
        // v != -1 ? v + off : -1, with v read once: through a hidden local when it is not a plain name.
        Node read = *value;
        if (read.kind != Node::Name && cur_locals != nullptr) {
            Local hidden;
            hidden.name = "__mp" + std::to_string(tmp_count++);
            hidden.type = from;
            place_local(cur_locals, hidden);
            Node assign;
            assign.kind = Node::Assign;
            assign.op = TAssign;
            assign.kids.push_back(node_name(hidden.name));
            assign.kids.push_back(std::move(read));
            Node stmt;
            stmt.kind = Node::ExprStmt;
            stmt.kids.push_back(std::move(assign));
            stmt_pre.push_back(std::move(stmt));
            read = node_name(hidden.name);
        } else if (read.kind != Node::Name) {
            return;
        }
        Node moved;
        moved.kind = Node::Binary;
        moved.op = TPlus;
        moved.kids.push_back(read);
        moved.kids.push_back(int_node(off));
        Node valid;
        valid.kind = Node::Binary;
        valid.op = TNe;
        valid.kids.push_back(read);
        valid.kids.push_back(int_node(-1));
        Node tern;
        tern.kind = Node::Ternary;
        tern.kids.push_back(std::move(valid));
        tern.kids.push_back(std::move(moved));
        tern.kids.push_back(int_node(-1));
        *value = cast_node(to, std::move(tern));
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
    // A constructor of class sid that can make an object from one argument of type a: its first parameter takes a by
    // a standard conversion (or is a's own class), and any other parameters have defaults. Copy and move constructors
    // do not count, and a parameter of another class is not tried, so only one user-defined conversion is made
    // ([over.best.ics] 4). `explicit` is not recorded here, so an explicit constructor counts too.
    bool converts_by_ctor(int sid, const Node& arg, const Type& a) const {
        if (sid < 0 || sid >= static_cast<int>(structs.size())) return false;
        // A constructor template (std::function's) is taken to accept the argument; its instance is made by the call.
        if (has_ctor_template_taking(sid, 1) && !(a.base == 3 && a.pointers == 0 && !a.is_array() && a.struct_id == sid))
            return true;
        for (const Method& m : structs[static_cast<size_t>(sid)].methods) {
            if (m.name != "<ctor>" || m.params.empty() || required_args(m.defaults, m.params.size()) > 1) continue;
            Type first = m.params[0];
            if (first.base == 3 && first.pointers == 0 && !first.is_array()) {
                if (first.struct_id == sid) continue;  // copy or move
                if (a.base == 3 && a.pointers == 0 && !a.is_array() && a.struct_id == first.struct_id) return true;
                continue;
            }
            if (a.base == 3 && a.pointers == 0 && !a.is_array()) continue;
            if (arg_score(arg, first) >= 0) return true;
        }
        return false;
    }
    // Two function types with the same result and parameters (a reference and a pointer to the same type count as the
    // same here, as both are passed as an address).
    bool same_signature(int f1, int f2) const {
        if (f1 == f2) return true;
        if (f1 < 0 || f2 < 0 || f1 >= static_cast<int>(func_types.size()) || f2 >= static_cast<int>(func_types.size()))
            return false;
        const FuncType& a = func_types[static_cast<size_t>(f1)];
        const FuncType& b = func_types[static_cast<size_t>(f2)];
        if (a.params.size() != b.params.size() || a.vararg != b.vararg) return false;
        auto norm = [&](Type t) {
            if (t.is_ref) {
                t.is_ref = false;
                t.rref = false;
                t.pointers += 1;
            }
            t.pconst = false;
            t.is_const = false;
            return cxx_type(t, &structs);
        };
        if (norm(a.ret) != norm(b.ret)) return false;
        for (size_t i = 0; i < a.params.size(); ++i)
            if (norm(a.params[i]) != norm(b.params[i])) return false;
        return true;
    }
    // The number of elements of the braced list whose '{' is token `open` (commas at its own level).
    int braced_count(size_t open) const {
        int depth = 0;
        int count = 0;
        bool any = false;
        for (size_t k = open; k < tokens.size() && tokens[k].kind != TEnd; ++k) {
            const Tok t = tokens[k].kind;
            if (t == TLBrace || t == TLParen || t == TLBrack) {
                if (depth == 1) any = true;
                ++depth;
                continue;
            }
            if (t == TRBrace || t == TRParen || t == TRBrack) {
                if (--depth == 0) break;
                continue;
            }
            if (depth == 1) {
                if (t == TComma) {
                    ++count;
                    any = false;
                } else {
                    any = true;
                }
            }
        }
        return count + (any ? 1 : 0);
    }
    // Can an object of class sid be made from a braced list of n elements ([over.ics.list], [dcl.init.list] 3)? By an
    // initializer-list constructor, a constructor that takes n arguments, or as an aggregate with at least n members.
    bool class_takes_list(int sid, int n) const {
        if (sid < 0 || sid >= static_cast<int>(structs.size())) return true;
        Type list;
        if (n > 0 && has_init_list_ctor(sid, &list)) return true;
        bool any_ctor = false;
        for (const Method& m : structs[static_cast<size_t>(sid)].methods) {
            if (m.name != "<ctor>") continue;
            any_ctor = true;
            if (static_cast<int>(m.params.size()) >= n && static_cast<int>(required_args(m.defaults, m.params.size())) <= n)
                return true;
        }
        if (has_ctor_template(sid)) return true;
        if (any_ctor) return n == 0 && !structs[static_cast<size_t>(sid)].poly;
        int members = 0;
        for (const Field& f : structs[static_cast<size_t>(sid)].fields)
            if (f.name.compare(0, 6, "__vptr") != 0) ++members;
        return n <= members || n == 0;
    }
    int arg_score(const Node& arg, Type p) const {
        if (arg.kind == Node::Block && arg.text == "tokbrace") {
            // A braced list argument ([over.ics.list]): a class parameter takes it by a constructor or as an
            // aggregate (a user-defined conversion); a pointer or a number takes {} or {x} only. Before (Round 28),
            // set.erase({a, b}) chose erase(const T*) (Round 29).
            const int n = braced_count(static_cast<size_t>(arg.number));
            Type q = p;
            q.is_ref = false;
            if (q.pointers > 0 || q.is_array() || q.func_id >= 0) return n <= 1 ? 2 : -1;
            if (q.base != 3) return n <= 1 ? 0 : -1;
            return class_takes_list(q.struct_id, n) ? 4 : -1;
        }
        Type a;
        if (!static_type(arg, &a)) return 1;
        if (arg.kind == Node::IntLit && arg.text == "char" && a.base == 1) a.base = 2;  // a character literal is a char
        if (arg.kind == Node::IntLit && arg.text == "bool" && a.base == 1) a = bool_type();  // true and false are bool
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
            // A function (or pointer to one) for a function pointer parameter: the signatures decide ([over.ics.scs]),
            // so operator<<(ios_base& (*)(ios_base&)) takes std::hex and the const void* overload does not win.
            if (a.func_id >= 0 && p.func_id >= 0) return same_signature(a.func_id, p.func_id) ? 0 : -1;
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
            if (a.pointers == 0 && a.base == 3 && a.struct_id == p.struct_id)
                return want_ref && !want_rref && is_rvalue(arg) ? 1 : 0;
            if (a.pointers == 0 && a.base == 3 && derives_from(a.struct_id, p.struct_id)) return 2;
            // A user-defined conversion by a converting constructor of the parameter's class ([over.ics.user]): it
            // ranks after every standard conversion. Only by value or through a const reference (a temporary).
            if (p.pointers == 0 && p.struct_id >= 0 && (!want_ref || p.is_const || p.pconst || want_rref) &&
                converts_by_ctor(p.struct_id, arg, a))
                return 4;
            // Or by a conversion function of the argument's class to the parameter's class (path to string).
            if (a.pointers == 0 && a.base == 3 && a.struct_id >= 0 && p.struct_id >= 0 && !a.is_array() &&
                (!want_ref || p.is_const || p.pconst || want_rref))
                for (const Method& cm : structs[static_cast<size_t>(a.struct_id)].methods)
                    if (cm.name.compare(0, 13, "operator conv") == 0 && cm.params.empty() && cm.ret.base == 3 &&
                        cm.ret.pointers == 0 && cm.ret.struct_id == p.struct_id)
                        return 4;
            return -1;
        }
        if (a.pointers > 0) return p.is_bool && p.base == 2 ? 3 : -1;
        // A conversion function (operator bool, operator int) is a user-defined conversion too ([over.ics.user]): it
        // ranks with a converting constructor, so a constructor template that takes the class itself is preferred to
        // `S(int)` reached through `operator bool`.
        if (a.base == 3) return conv_for(a.struct_id, p) != nullptr ? 4 : -1;
        if (a.base == 0 || p.base == 0) return -1;
        // A reference to a non-const scalar binds only to an lvalue of that same type ([dcl.init.ref] 5): int& does not
        // take a double, so from_chars(first, last, int_value) does not pick the double overload.
        if (want_ref && !want_rref && !p.is_const && !p.pconst &&
            !(a.base == p.base && a.wide == p.wide && a.is_bool == p.is_bool && (a.base >= 5 || a.is_unsigned == p.is_unsigned)))
            return -1;
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
    // True when a member function (not a template) named `name` can take these arguments: find_method falls back to a
    // method that cannot, which must not hide a member template that can ([over.match.viable]).
    bool has_viable_method(int sid, const std::string& name, const std::vector<Node>& args) const {
        if (sid < 0 || sid >= static_cast<int>(structs.size())) return false;
        for (const Method& m : structs[static_cast<size_t>(sid)].methods) {
            if (m.name != name) continue;
            const bool exact = m.params.size() == args.size();
            if (!exact && !(args.size() < m.params.size() && args.size() >= required_args(m.defaults, m.params.size())))
                continue;
            if (call_score(m.params, args) >= 0) return true;
        }
        return false;
    }
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
            if (m.name != "<ctor>") continue;
            // A constructor whose last parameters have defaults takes fewer arguments ([dcl.fct.default]).
            const bool exact = m.params.size() == args.size();
            if (!exact && !(args.size() < m.params.size() && required_args(m.defaults, m.params.size()) <= args.size())) continue;
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
            int score = args.empty() ? 0 : call_score(m.params, args);
            if (score >= 0 && !exact) score += 1;  // an exact count wins a tie
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

    // A hidden local of type t given `value` before the statement (stmt_pre), destroyed after it when its class has a
    // destructor (stmt_temps). Returns its name.
    std::string materialize_temp(const Type& t, Node value) {
        Local hidden;
        hidden.name = "__bt" + std::to_string(tmp_count++);
        hidden.type = t;
        place_local(cur_locals, hidden);
        Node assign;
        assign.kind = Node::Assign;
        assign.op = TAssign;
        assign.number = 1;
        assign.kids.push_back(node_name(hidden.name));
        assign.kids.push_back(std::move(value));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(assign));
        stmt_pre.push_back(std::move(stmt));
        if (t.base == 3 && t.pointers == 0 && t.struct_id >= 0 && class_needs_dtor(structs[static_cast<size_t>(t.struct_id)]))
            stmt_temps.push_back(Temp{hidden.name, t.struct_id});
        return hidden.name;
    }
    // Arguments for a call: a reference parameter gets the address of its argument, a class
    // parameter with a copy constructor gets a copy made by that constructor into a hidden local
    // (destroyed after the statement).
    bool adjust_args(const std::vector<Type>& params, std::vector<Node>* args, size_t first = 0) {
        for (size_t i = first; i < args->size() && i - first < params.size(); ++i) {
            const Type& pt = params[i - first];
            Node& arg = (*args)[i];
            const bool from_braces = arg.kind == Node::Block && arg.text == "tokbrace";
            if (from_braces) {
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
            if (pt.pointers > 0 && !pt.is_ref && !pt.is_array() && !(arg.kind == Node::IntLit && arg.number == 0) &&
                arg.kind != Node::Ternary && !from_braces) {
                // An integer is not converted to a pointer parameter ([conv.ptr] 1: only a null pointer constant is);
                // before, f('x') reached a const char* parameter as an address. (c ? 0 : p is a pointer; a braced
                // argument is left as before: see the note on overloads in the plan.)
                Type from;
                if (static_type(arg, &from) && from.pointers == 0 && !from.is_array() && !from.is_ref && !from.is_func() &&
                    (from.base == 1 || from.base == 2 || from.base == 4) && !from.is_enum()) {
                    error = "an integer argument for a pointer parameter (no conversion)";
                    return false;
                }
            }
            {
                Type from;
                if (cur_locals != nullptr && (pt.is_ref || !wants_copy(pt, arg)) && pt.base == 3 && pt.pointers == 0 &&
                    !pt.is_array() && pt.struct_id >= 0 &&
                    class_needs_ctor(structs[static_cast<size_t>(pt.struct_id)]) && static_type(arg, &from) &&
                    !(from.base == 3 && from.pointers == 0 && !from.is_array() &&
                      (from.struct_id == pt.struct_id || derives_from(from.struct_id, pt.struct_id) ||
                       !converts_by_ctor(pt.struct_id, arg, from))) &&
                    !(from.base == 0 && from.pointers == 0)) {
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
                    // Only one user-defined conversion is made for an argument ([over.best.ics] 4): the constructor's
                    // own arguments are not converted again by this path.
                    ++conv_depth;
                    const bool made = conv_depth <= 1 &&
                                      special_call(pt.struct_id, "<ctor>", node_addr(node_name(hidden.name)), std::move(cargs), &call);
                    --conv_depth;
                    if (!made) {
                        if (error.empty()) error = "no conversion to " + structs[static_cast<size_t>(pt.struct_id)].name;
                        return false;
                    }
                    stmt_pre.push_back(std::move(call));
                    if (class_needs_dtor(structs[static_cast<size_t>(pt.struct_id)]))
                        stmt_temps.push_back(Temp{hidden.name, pt.struct_id});
                    arg = pt.is_ref ? node_addr(node_name(hidden.name)) : node_name(hidden.name);
                    continue;
                }
            }
            if (pt.is_ref && pt.base == 3 && pt.pointers == 0 && !pt.is_array() && pt.struct_id >= 0) {
                // const string& given a filesystem::path: the argument's conversion function to the parameter's class
                // ([over.ics.user], [class.conv.fct]) makes the object the reference is bound to (Round 29; before, the
                // path's bytes were taken as a string).
                Type from;
                if (static_type(arg, &from) && from.base == 3 && from.pointers == 0 && !from.is_array() &&
                    from.struct_id >= 0 && from.struct_id != pt.struct_id && !derives_from(from.struct_id, pt.struct_id)) {
                    for (const Method& cm : structs[static_cast<size_t>(from.struct_id)].methods) {
                        if (cm.name.compare(0, 13, "operator conv") != 0 || !cm.params.empty() || cm.ret.base != 3 ||
                            cm.ret.pointers != 0 || cm.ret.struct_id != pt.struct_id)
                            continue;
                        std::vector<Node> none;
                        Node converted;
                        const std::string cname = cm.name;
                        if (!method_call(&converted, std::move(arg), false, cname, {}, &none)) return false;
                        arg = std::move(converted);
                        break;
                    }
                }
            }
            if (pt.is_ref) {
                // The name of a function is not an object: a reference to a function pointer is bound to a temporary
                // holding its address.
                const bool object = (arg.kind == Node::Name && !names_function(arg.text)) || arg.kind == Node::Field ||
                                    arg.kind == Node::Index || (arg.kind == Node::Unary && arg.op == TStar);
                if (!object && cur_locals != nullptr) {
                    // A reference bound to a temporary: the value goes into a hidden local first.
                    Type held = pt;
                    held.is_ref = false;
                    arg = node_addr(node_name(materialize_temp(held, std::move(arg))));
                    continue;
                }
                Node a = node_addr(std::move(arg));
                arg = std::move(a);
                // A reference to a base class bound to an object of a derived class: the base subobject ([dcl.init.ref]).
                Type to_ptr = pt;
                to_ptr.is_ref = false;
                to_ptr.rref = false;
                to_ptr.pointers += 1;
                class_ptr_convert(&arg, to_ptr);
            } else if (pt.pointers == 1 && !pt.is_array() && pt.base == 3) {
                class_ptr_convert(&arg, pt);
            } else if (cur_locals != nullptr && pt.base == 3 && pt.pointers == 0 && !pt.is_array() && pt.struct_id >= 0 &&
                       arg.kind == Node::Call && class_prvalue(pt, arg)) {
                // f(g()) with g returning the parameter's class by value: the result is made in a hidden local (the
                // callee writes it there) and that object is the argument ([class.temporary]); the code generator
                // takes the address of a class argument, which a call is not.
                Type held = pt;
                held.is_const = false;
                held.pconst = false;
                arg = node_name(materialize_temp(held, std::move(arg)));
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
        m.this_adj = 0;
        m.this_sid = -1;
        m.vt_adj = 0;
        m.vt_sid = -1;
        size_t found = def->methods.size();
        const std::string new_sig = param_sig(m.params);
        bool collide = false;
        // Virtual functions of the same signature in the bases that are not at offset 0 ([class.virtual] 2: this
        // function overrides each of them): their slots in the secondary tables get an adjustor thunk below.
        std::vector<std::pair<int, int>> sec_overrides;  // (subobject offset, slot)
        std::vector<size_t> sec_entries;
        for (size_t i = 0; i < def->methods.size(); ++i)
            if (def->methods[i].name == m.name && def->methods[i].params.size() == m.params.size()) {
                bool same = true;
                if (m.name == "<ctor>") {
                    for (size_t k = 0; k < m.params.size(); ++k)
                        if (!same_shape(def->methods[i].params[k], m.params[k])) same = false;
                } else if (m.name != "<dtor>") {
                    same = param_sig(def->methods[i].params) == new_sig && def->methods[i].is_const == m.is_const;
                }
                if (same && def->methods[i].is_virtual && def->methods[i].vt_adj != 0) {
                    sec_overrides.push_back({def->methods[i].vt_adj, def->methods[i].slot});
                    sec_entries.push_back(i);
                    if (found < def->methods.size() && def->methods[found].is_virtual) continue;  // keep the first base's
                }
                if (same) found = i;
                else collide = true;
            }
        if (found < def->methods.size() && def->methods[found].is_virtual) {
            m.is_virtual = true;
            m.slot = def->methods[found].slot;
            m.vt_adj = def->methods[found].vt_adj;
            m.vt_sid = def->methods[found].vt_sid;
        }
        // A destructor overrides the virtual destructor of every base that has one ([class.dtor] 13).
        if (m.name == "<dtor>")
            for (const StructDef::SecTable& st : def->sec_tables)
                if (st.dtor_slot >= 0) {
                    m.is_virtual = true;
                    sec_overrides.push_back({st.off, st.dtor_slot});
                }
        // The other entries of this signature are replaced by this one (only the entry kept is found by a call).
        for (size_t k = sec_entries.size(); k-- > 0;) {
            const size_t i = sec_entries[k];
            if (i == found) continue;
            def->methods.erase(def->methods.begin() + static_cast<std::ptrdiff_t>(i));
            if (found < def->methods.size() && found > i) --found;
        }
        if (m.is_virtual && m.vt_adj == 0) {
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
        m.qname = (def->parent >= 0 || def->ns_dup ? def->symname() : def->name) + "::" + m.name + "/" +
                  std::to_string(m.params.size());
        if (collide) m.qname += "~" + new_sig + (m.is_const ? "K" : "");
        if (m.name == "<ctor>")
            for (const Type& param : m.params) m.qname += param.is_ref ? "r" : "v";  // constructors overload by type
        if (!m.tsym.empty()) m.qname += "<" + m.tsym + ">";  // a constructor template instance
        m.symbol = cxx_method_symbol(structs, *def, m);
        const std::string target = m.pure ? "" : m.name == "<dtor>" ? sdd_symbol(*def, m) : m.symbol;
        if (m.is_virtual && m.vt_adj == 0)
            def->vtable[static_cast<size_t>(m.slot)] = target;
        for (const auto& ov : sec_overrides)
            for (StructDef::SecTable& st : def->sec_tables)
                if (st.off == ov.first && ov.second >= 0 && ov.second < static_cast<int>(st.slots.size()))
                    st.slots[static_cast<size_t>(ov.second)] =
                        target.empty() ? std::string() : adjustor_thunk(*def, m, target, ov.first);
        if (found < def->methods.size()) def->methods[found] = std::move(m);
        else def->methods.push_back(std::move(m));
        return true;
    }

    // An adjustor thunk for a secondary table's slot (Round 29): the slot is called with the address of the base
    // subobject at `adj`; the thunk subtracts adj and jumps to `target`, the function of the whole class. Its name is
    // the target's with the access letter replaced by W (public), O (protected) or G (private) and the adjustment as a
    // decorated number, as Microsoft decorates virtual adjustor thunks. One per file (an internal symbol).
    std::string adjustor_thunk(const StructDef& def, const Method& m, const std::string& target, int adj) {
        std::string sym = target;
        const std::string scope = "@" + def.symname() + "@@";
        const size_t at_scope = sym.find(scope);
        size_t letter = at_scope == std::string::npos ? std::string::npos : at_scope + scope.size();
        // ??_GC@@UEAA...: the scalar deleting destructor's name starts with the class itself.
        if (sym.compare(0, 4, "??_G") == 0 && sym.compare(4, def.symname().size() + 2, def.symname() + "@@") == 0)
            letter = 4 + def.symname().size() + 2;
        const char access = m.access == 'A' ? 'G' : m.access == 'I' ? 'O' : 'W';
        std::string num;
        if (adj >= 1 && adj <= 10) {
            num = std::string(1, static_cast<char>('0' + adj - 1));
        } else {
            for (unsigned v = static_cast<unsigned>(adj); v != 0; v >>= 4) num.insert(num.begin(), static_cast<char>('A' + (v & 15u)));
            num += "@";
        }
        if (letter != std::string::npos && letter < sym.size()) sym = sym.substr(0, letter) + access + num + sym.substr(letter + 1);
        else sym = "__okay_thunk" + std::to_string(adj) + "_" + sym;
        for (const Thunk& t : thunks)
            if (t.symbol == sym) return sym;
        Thunk t;
        t.symbol = sym;
        t.target = target;
        t.adj = adj;
        // A class result returned through memory: its address is the first argument and this the second.
        t.rdx = m.name != "<dtor>" && m.ret.base == 3 && m.ret.pointers == 0 && !m.ret.is_ref && !m.ret.is_array() &&
                !fits_register(m.ret);
        thunks.push_back(t);
        return sym;
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
    // Classes whose body is being read: not complete yet ([class.mem] 7).
    std::vector<int> open_structs{};
    // The member bodies of a class template instance made inside the body of a class that one of its arguments names
    // (std::vector<path> in a member declaration of path): they are compiled when the outermost class is complete
    // ([temp.inst] 3, [temp.point]), with the instance's template arguments bound again. Before, they were compiled at
    // once, while the argument had no members and no size (a copy of a path was a plain memory copy, its destructor
    // was never called).
    bool names_open_struct(const std::vector<TArg>& args) const {
        for (const TArg& a : args) {
            if (a.is_value) continue;
            if (a.is_pack) {
                for (const Type& t : a.pack)
                    if (t.base == 3 && t.pointers == 0 &&
                        std::find(open_structs.begin(), open_structs.end(), t.struct_id) != open_structs.end())
                        return true;
                continue;
            }
            if (a.type.base == 3 && a.type.pointers == 0 &&
                std::find(open_structs.begin(), open_structs.end(), a.type.struct_id) != open_structs.end())
                return true;
        }
        return false;
    }
    // The member types (typedef and using names) of class sid as plain names, for code that is in the class's scope
    // without being inside its braces (a member defined outside the class, [basic.scope.class]; a nested class defined
    // outside its class). unbind_class_tdefs puts back what they hid.
    using TdefSaves = std::vector<std::pair<std::string, std::pair<bool, Type>>>;
    TdefSaves bind_class_tdefs(int sid) {
        TdefSaves saved;
        if (sid < 0) return saved;
        for (const auto& td : structs[static_cast<size_t>(sid)].tdefs) {
            const auto old = typedefs.find(td.first);
            saved.push_back({td.first, {old != typedefs.end(), old != typedefs.end() ? old->second : Type{}}});
            typedefs[td.first] = td.second;
        }
        return saved;
    }
    void unbind_class_tdefs(const TdefSaves& saved) {
        for (size_t k = saved.size(); k-- > 0;) {
            if (saved[k].second.first) typedefs[saved[k].first] = saved[k].second.second;
            else typedefs.erase(saved[k].first);
        }
    }
    // From a constructor's ':' to the '{' of its body: past the member initializers, whose own braces (m{}, m{a, b},
    // [class.base.init] 3) follow a name or a template-id's '>' and are skipped whole.
    void skip_mem_initializers() {
        int paren = 0;
        while (!is(TEnd) && !is(TSemi)) {
            if (is(TLParen)) ++paren;
            else if (is(TRParen)) --paren;
            else if (is(TLBrace) && paren == 0) {
                const Tok before = at > 0 ? tokens[at - 1].kind : TEnd;
                if (before != TIdent && before != TGt) return;  // the body
                for (int depth = 0; !is(TEnd); next()) {
                    if (is(TLBrace)) ++depth;
                    else if (is(TRBrace) && --depth == 0) break;
                }
            }
            next();
        }
    }
    bool parse_struct_body(bool as_union, std::string name, int* out_id, bool is_class = false,
                           const std::vector<int>& base_ids = {}) {
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
        // A class nested in another ([class.nest]): remembered with its outer class, and decorated as Inner@Outer.
        if (pending_sym.empty() && !open_structs.empty() && name.compare(0, 6, "__anon") != 0 &&
            name.compare(0, 5, "__lam") != 0) {
            def.parent = open_structs.back();
            def.sym_name = name + "@" + structs[static_cast<size_t>(def.parent)].symname();
        }
        def.ns = current_ns;
        if (def.parent < 0 && pending_sym.empty() && cur_class >= 0 && cur_locals != nullptr) def.local_in = cur_class;
        if (def.parent < 0 && pending_sym.empty() && cur_locals != nullptr) def.local_fn = true;
        if (pending_sym.empty() && def.parent < 0 && name.compare(0, 6, "__anon") != 0 && name.compare(0, 5, "__lam") != 0 &&
            !ns_mangled.empty())
            for (const StructDef& other : structs)
                if (other.name == name && other.parent < 0 && !other.incomplete && other.ns != current_ns) {
                    // A second class of this name in another namespace: decorated with its namespace, as MSVC decorates
                    // every class (V Name@ns@@), so the two have separate members and symbols.
                    def.sym_name = name + "@" + ns_mangled;
                    def.ns_dup = true;
                    break;
                }
        pending_sym.clear();
        // The instance itself is named by its template (Box<int> has the constructor Box); a class nested in it by its own name.
        const std::string ctor_name = inst_base_name.empty() || def.sym_name.empty() ? name : inst_base_name;
        def.is_union = as_union;
        def.is_class = is_class;
        def.cxx = is_class;
        // The first base (Microsoft x64 layout, Round 29): the first base with virtual functions, else the first one
        // written. It is at offset 0, its table is this class's table, and its members are copied as they are.
        int base_id = -1;
        for (int bid : base_ids)
            if (base_id < 0 && structs[static_cast<size_t>(bid)].poly) base_id = bid;
        if (base_id < 0 && !base_ids.empty()) base_id = base_ids.front();
        if (base_id >= 0) {
            const StructDef& base = structs[static_cast<size_t>(base_id)];
            def.base = base_id;
            def.cxx = true;
            def.fields = base.fields;
            def.poly = base.poly;
            def.methods = base.methods;
            // Constructors are not inherited.
            for (size_t i = def.methods.size(); i-- > 0;)
                if (def.methods[i].name == "<ctor>") def.methods.erase(def.methods.begin() + static_cast<std::ptrdiff_t>(i));
            for (Method& inherited : def.methods) inherited.body_at = 0;
            def.vtable = base.vtable;
            def.statics = base.statics;
            def.sec_tables = base.sec_tables;
            def.bases_size = base.size;
            def.align = base.align;
            StructDef::BaseInfo first;
            first.sid = base_id;
            first.f0 = 0;
            first.f1 = def.fields.size();
            def.bases.push_back(first);
        }
        // The other bases ([class.mi]), in declaration order, each at the next offset its alignment allows after the
        // bases before it. Their fields are copied with that offset added; their member functions are reached through
        // the subobject (this_adj), their virtual functions through the subobject's own table (vt_adj), which this
        // class keeps as a secondary table.
        for (int bid : base_ids) {
            if (bid == base_id) continue;
            const StructDef& base = structs[static_cast<size_t>(bid)];
            if (base.incomplete) {
                error = "base class " + base.name + " is incomplete";
                return false;
            }
            for (const StructDef::BaseInfo& had : def.bases)
                if (had.sid == bid) {
                    error = "the same base class twice";
                    return false;
                }
            const int balign = base.align < 1 ? 1 : base.align;
            const int off = (def.bases_size + balign - 1) / balign * balign;
            StructDef::BaseInfo info;
            info.sid = bid;
            info.off = off;
            info.f0 = def.fields.size();
            for (Field f : base.fields) {
                f.offset += off;
                // The base's table pointer keeps a name of its own ("__vptr16"): "__vptr" is the class's own, at offset 0.
                if (f.name.compare(0, 6, "__vptr") == 0) f.name = "__vptr" + std::to_string(f.offset);
                def.fields.push_back(f);
            }
            info.f1 = def.fields.size();
            def.bases.push_back(info);
            for (Method m : base.methods) {
                if (m.name == "<ctor>" || m.name == "<dtor>") continue;
                bool dup = false;
                for (const Method& have : def.methods)
                    if (have.name == m.name && have.params.size() == m.params.size() && have.is_const == m.is_const &&
                        param_sig(have.params) == param_sig(m.params) && !(m.is_virtual && have.is_virtual))
                        dup = true;
                if (dup) continue;
                m.body_at = 0;
                if (m.this_sid < 0) m.this_sid = m.owner;
                if (m.vt_sid < 0) m.vt_sid = m.owner;
                if (!m.is_static) m.this_adj += off;
                if (m.is_virtual) m.vt_adj += off;
                def.methods.push_back(std::move(m));
            }
            for (const auto& st : base.statics) def.statics.insert(st);
            if (base.poly) {
                StructDef::SecTable table;
                table.off = off;
                table.sid = bid;
                table.slots = base.vtable;
                for (const Method& bm : base.methods)
                    if (bm.name == "<dtor>" && bm.is_virtual) table.dtor_slot = bm.slot;
                def.sec_tables.push_back(std::move(table));
            }
            for (StructDef::SecTable table : base.sec_tables) {
                table.off += off;
                def.sec_tables.push_back(std::move(table));
            }
            def.bases_size = off + base.size;
            if (base.align > def.align) def.align = base.align;
        }
        // Keep the bases in declaration order (construction order, [class.base.init] 13).
        if (base_ids.size() > 1) {
            std::vector<StructDef::BaseInfo> ordered;
            for (int bid : base_ids)
                for (const StructDef::BaseInfo& bi : def.bases)
                    if (bi.sid == bid) ordered.push_back(bi);
            def.bases = ordered;
        }
        def.base_fields = def.fields.size();
        const size_t enums_from = enums_declared.size();
        const size_t enum_types_from = enum_types_declared.size();
        int self = -1;
        for (size_t k = 0; k < structs.size(); ++k)
            if (structs[k].incomplete && structs[k].name == name && (structs[k].parent == def.parent || def.parent < 0) &&
                (structs[k].ns == def.ns || def.parent >= 0))
                self = static_cast<int>(k);
        if (self >= 0) {
            structs[static_cast<size_t>(self)] = def;  // the definition of a class declared before
        } else {
            structs.push_back(def);
            self = static_cast<int>(structs.size()) - 1;
        }
        struct OpenGuard {
            std::vector<int>& open;
            int id;
            void close() {
                for (size_t k = open.size(); k-- > 0;)
                    if (open[k] == id) {
                        open.erase(open.begin() + static_cast<std::ptrdiff_t>(k));
                        break;
                    }
                id = -2;
            }
            ~OpenGuard() { close(); }
        } open_guard{open_structs, self};
        open_structs.push_back(self);
        if (!expect(TLBrace, "'{'")) return false;
        char access = is_class ? 'A' : 'Q';
        // typedef and using names of this class are visible to the members after them.
        struct ScopedTd {
            std::string name;
            bool had = false;
            Type old{};
        };
        std::vector<ScopedTd> scoped_tds;
        if (base_id >= 0) {
            // The member types of the base classes are names in this class too ([class.member.lookup]).
            def.tdefs = structs[static_cast<size_t>(base_id)].tdefs;
            for (int bid : base_ids)
                for (const auto& td : structs[static_cast<size_t>(bid)].tdefs) def.tdefs.insert(td);
            for (const auto& td : def.tdefs) {
                ScopedTd saved_td;
                saved_td.name = td.first;
                auto old_td = typedefs.find(td.first);
                if (old_td != typedefs.end()) {
                    saved_td.had = true;
                    saved_td.old = old_td->second;
                }
                scoped_tds.push_back(std::move(saved_td));
                typedefs[td.first] = td.second;
            }
        }
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
                    skip_mem_initializers();
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
            if (!static_member && is(TSemi) && field.type.base == 3 && field.type.pointers == 0 && field.type.struct_id >= 0 &&
                structs[static_cast<size_t>(field.type.struct_id)].name.compare(0, 6, "__anon") == 0) {
                // struct { ... }; or union { ... }; as a member: an anonymous member ([class.union.anon]). It is kept as
                // a hidden field; its members are found through it by name (anon_path).
                next();
                field.name = "__anonm" + std::to_string(def.anon_fields.size());
                def.anon_fields.push_back(field.name);
                def.fields.push_back(field);
                continue;
            }
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
            // T& name; a reference member ([dcl.ref], [class.base.init]): the field holds the address of what it refers
            // to, set by the constructor's member initializer, and every use of the name goes through it.
            bool ref_member = false;
            if (!static_member && (is(TAmp) || is(TAndAnd)) && at + 1 < tokens.size() && tokens[at + 1].kind == TIdent) {
                next();
                ++field.type.pointers;
                ref_member = true;
            }
            if (!ref_member && is(TLParen) && at + 1 < tokens.size() && tokens[at + 1].kind == TStar) {
                // R (*name)(A); a member that points to a function ([dcl.fct], [class.mem]).
                Declarator fd;
                if (!parse_declarator(field.type, &fd)) return false;
                field.name = fd.name;
                field.type = fd.type;
            } else {
                if (!is(TIdent)) {
                    error = "expected a field name";
                    return false;
                }
                field.name = cur().text;
                next();
                if (ref_member) {
                    def.ref_caps.insert(field.name);
                    def.ref_members.insert(field.name);
                }
                if (!parse_brackets(&field.type)) return false;
            }
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
                    const bool object_init = (is(TLBrace) || is(TAssign)) && !sm.type.is_array() && sm.type.base == 3 &&
                                             sm.type.pointers == 0 && sm.type.struct_id >= 0 && sm.type.struct_id != self &&
                                             (obj_needs_ctor(sm.type) || obj_needs_dtor(sm.type));
                    if (object_init) {
                        // static inline std::atomic<bool> s_quit{false}; static inline C c = value; ([dcl.inline],
                        // [class.static.data]): an object made by its constructor at startup and destroyed at the end,
                        // like a namespace-scope one. Own simplification: each file keeps its own copy (internal symbol).
                        std::vector<Node> cargs;
                        const bool braced = accept(TAssign) ? is(TLBrace) : true;
                        if (braced) {
                            if (!expect(TLBrace, "'{'")) return false;
                            while (!is(TRBrace) && !is(TEnd)) {
                                Node a;
                                if (!parse_expr(1, &a)) return false;
                                cargs.push_back(std::move(a));
                                if (!accept(TComma)) break;
                            }
                            if (!expect(TRBrace, "'}'")) return false;
                        } else {
                            Node a;
                            if (!parse_expr(1, &a)) return false;
                            cargs.push_back(std::move(a));
                        }
                        sm.symbol = static_symbol(def, field.name, sm.type);
                        Global g;
                        g.name = sm.symbol;
                        g.type = sm.type;
                        g.bss = true;
                        g.is_static = true;
                        globals.push_back(g);
                        if (!register_global_object(sm.symbol, sm.type, std::move(cargs))) return false;
                    } else if (accept(TAssign)) {
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
        // The enumerators declared in this body (nested classes included) are members of the class with their own
        // values: A::Size and B::Size stay apart even though the flat table keeps only the last one.
        for (size_t k = enums_from; k < enums_declared.size(); ++k) {
            const std::string& ename = enums_declared[k];
            StaticMem sm;
            sm.type.base = 1;
            sm.has_const = true;
            sm.cval = static_cast<long long>(enum_values[ename]);
            def.statics[ename] = sm;
        }
        for (size_t k = enum_types_from; k < enum_types_declared.size(); ++k)
            def.tdefs.insert({enum_types_declared[k].name, enum_types_declared[k].type});
        int offset = 0;
        int align = 1;
        int cursor_bits = 0;
        size_t first_field = 0;
        if (def.base >= 0) {
            // A derived class starts after the whole base objects, as the
            // MSVC layout does. The base fields keep their offsets.
            offset = def.bases_size;
            align = def.align < 1 ? 1 : def.align;
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
            for (const StructDef::BaseInfo& bi : def.bases)
                if (class_has_copy(bi.sid)) part_copies = true;
            for (size_t fi = def.base_fields; fi < def.fields.size(); ++fi) {
                const Type& ft = def.fields[fi].type;
                if (ft.base == 3 && ft.pointers == 0 && !ft.is_ref && !ft.is_array() && ft.struct_id >= 0 &&
                    ft.struct_id != self && class_has_copy(ft.struct_id))
                    part_copies = true;
            }
            if (!has_ctor && class_needs_ctor(structs[static_cast<size_t>(self)]) && parts_default_constructible(def, self)) {
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
            table.rtti = type_info_symbol(self, def.symname());
            globals.push_back(std::move(table));
        }
        // One table for each base subobject that is not at offset 0 and has virtual functions (Round 29): the slots
        // of that base's table, with this class's overrides as adjustor thunks, the class's type information, and the
        // subobject's offset in the slot before it.
        for (size_t k = 0; k < def.sec_tables.size(); ++k) {
            StructDef::SecTable& st = def.sec_tables[k];
            st.sym = "??_7" + def.symname() + "@@6B" + structs[static_cast<size_t>(st.sid)].symname() + "@@";
            for (size_t j = 0; j < k; ++j)
                if (def.sec_tables[j].sid == st.sid) st.sym += "$" + std::to_string(st.off);
            Global table;
            table.name = st.sym;
            table.type.base = 0;
            table.type.pointers = 1;
            table.type.array_len = static_cast<int>(st.slots.size());
            table.bss = false;
            table.vtable = true;
            table.slots = st.slots;
            table.rtti = type_info_symbol(self, def.symname());
            table.top_off = st.off;
            globals.push_back(std::move(table));
        }
        structs[static_cast<size_t>(self)] = def;
        *out_id = self;
        open_guard.close();  // complete now: its member bodies may make instances that need it
        // Member function bodies are compiled now that the class is complete,
        // so a body may use members declared after it. A nested class is complete before its enclosing class is, and
        // its bodies may use the enclosing class's members declared later ([class.mem] complete-class context), so
        // they wait until the outermost class ends. A local class ([class.local]) is not nested in that sense: in a member
        // function of a class its bodies are compiled now, while the function's static locals are still in sight (Round
        // 31: VisualScriptComponent.hpp:106's `struct Depth` in SendEvent; they waited for the end of the outer class
        // and then found no `depth`).
        if (struct_nest > 1 && inst_base_name.empty() && !def.local_fn) {
            for (size_t mi = 0; mi < def.methods.size(); ++mi)
                if (def.methods[mi].body_at != 0 && def.methods[mi].owner == self)
                    deferred_bodies.push_back({self, mi});
        } else if (!inst_base_name.empty() && names_open_struct(inst_args)) {
            DeferredInst d;
            d.sid = self;
            for (size_t mi = 0; mi < def.methods.size(); ++mi)
                if (def.methods[mi].body_at != 0 && def.methods[mi].owner == self) d.methods.push_back(mi);
            d.name = inst_base_name;
            d.td = inst_td;
            d.args = inst_args;
            d.bind_args = inst_spec_args;
            d.primary = inst_primary;
            d.spec_key = inst_spec_key;
            deferred_insts.push_back(std::move(d));
        } else {
            for (size_t mi = 0; mi < def.methods.size(); ++mi) {
                if (def.methods[mi].body_at == 0 || def.methods[mi].owner != self) continue;
                const size_t pre_before = stmt_pre.size();
                const size_t temps_before = stmt_temps.size();
                if (define_method(self, mi)) continue;
                if (inst_base_name.empty()) return false;
                // A member of a class template instance is made only when it is used ([temp.inst] 11): one whose body
                // does not compile for these arguments (pair<K, V>'s default constructor when V has none) is an error
                // only if a call of it is made (checked in finish_program).
                unusable_members[def.methods[mi].qname] = error;
                error.clear();
                stmt_pre.resize(std::min(stmt_pre.size(), pre_before));
                stmt_temps.resize(std::min(stmt_temps.size(), temps_before));
            }
            if (struct_nest == 1) {
                while (!deferred_bodies.empty()) {
                    const std::vector<std::pair<int, size_t>> batch = std::move(deferred_bodies);
                    deferred_bodies.clear();
                    for (const auto& item : batch)
                        if (!define_method(item.first, item.second)) return false;
                }
                while (!deferred_insts.empty()) {
                    const std::vector<DeferredInst> batch = std::move(deferred_insts);
                    deferred_insts.clear();
                    for (const DeferredInst& d : batch)
                        if (!define_deferred_inst(d)) return false;
                }
            }
        }
        for (size_t k = scoped_tds.size(); k-- > 0;) {
            typedefs.erase(scoped_tds[k].name);
            if (scoped_tds[k].had) typedefs[scoped_tds[k].name] = scoped_tds[k].old;
        }
        // The enumerations of this class are its member types (def.tdefs): after the class the name means again what it
        // meant before (Round 30: ModelAnimator's `enum Slot` hid Inventory::Slot in Inventory's members, and
        // HealthWatch::Event hid struct okay::Event). The class's members, derived classes and members defined outside
        // the class get them through def.tdefs (bind_class_tdefs).
        for (size_t k = enum_types_declared.size(); k-- > enum_types_from;) {
            const EnumDecl& ed = enum_types_declared[k];
            if (ed.nest != struct_nest) continue;
            if (ed.had) typedefs[ed.name] = ed.old;
            else typedefs.erase(ed.name);
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
        // The class's member types (typedefs, using names, enumerations) are names in its member functions
        // ([basic.scope.class]), also when the body is read after the class (a nested class's members; Round 30).
        const TdefSaves class_names = bind_class_tdefs(sid);
        const bool ok = parse_function_body(&fn, sid, m.name == "<ctor>" ? 1 : m.name == "<dtor>" ? 2 : 0, m.implicit);
        unbind_class_tdefs(class_names);
        unbind_params(alias_bound);
        // A failure outside a template instance stops the compile: the error then names the line it happened on, not
        // the end of the class (an instance's member may fail harmlessly and is set aside, see unusable_members).
        if (ok || !inst_base_name.empty()) at = resume;
        if (!ok) return false;
        structs[static_cast<size_t>(sid)].methods[mi].tok_body = m.body_at;
        structs[static_cast<size_t>(sid)].methods[mi].body_at = 0;
        if (m.ret.deduce != 0) {
            // auto: the method's result type is the one its body gave (in the method's own form: a reference is is_ref).
            Type r = fn.ret;
            if (fn.ret_ref) {
                r.pointers -= 1;
                r.is_ref = true;
            }
            structs[static_cast<size_t>(sid)].methods[mi].ret = r;
        }
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
    // Can a class with no constructor written get the implicit default constructor ([class.default.ctor] 2)? Not when
    // its base or a member of class type (without a default member initializer) has constructors and none of them takes
    // no arguments: the implicit one is then deleted, and only aggregate initialization makes such an object.
    bool takes_no_args(int sid) const {
        const StructDef& d = structs[static_cast<size_t>(sid)];
        bool any = false;
        for (const Method& m : d.methods) {
            if (m.name != "<ctor>" || m.owner != sid) continue;
            any = true;
            if (m.implicit && !m.implicit_copy) return true;
            size_t required = m.params.size();
            while (required > 0 && required - 1 < m.defaults.size() &&
                   !(m.defaults[required - 1].kind == Node::Block && m.defaults[required - 1].kids.empty() &&
                     m.defaults[required - 1].text.empty()))
                --required;
            if (required == 0) return true;
        }
        return !any || has_ctor_template_taking(sid, 0);
    }
    bool parts_default_constructible(const StructDef& d, int self) const {
        for (const StructDef::BaseInfo& bi : d.bases)
            if (class_needs_ctor(structs[static_cast<size_t>(bi.sid)]) && !takes_no_args(bi.sid)) return false;
        if (d.base >= 0 && class_needs_ctor(structs[static_cast<size_t>(d.base)]) && !takes_no_args(d.base)) return false;
        for (size_t fi = d.base_fields; fi < d.fields.size(); ++fi) {
            const Field& f = d.fields[fi];
            const Type& ft = f.type;
            if (ft.base != 3 || ft.pointers != 0 || ft.is_ref || ft.struct_id < 0 || ft.struct_id == self) continue;
            if (f.init_at >= 0 || f.init_zero) continue;
            if (class_needs_ctor(structs[static_cast<size_t>(ft.struct_id)]) && !takes_no_args(ft.struct_id)) return false;
        }
        return true;
    }

    // The hidden anonymous-member fields to go through to reach `name` in class sid (outermost first), when name is not
    // a direct member but a member of an anonymous struct or union in it (searched depth first). False when none has it.
    bool anon_path(int sid, const std::string& name, std::vector<std::string>* path) const {
        if (sid < 0 || static_cast<size_t>(sid) >= structs.size()) return false;
        const StructDef& d = structs[static_cast<size_t>(sid)];
        for (const std::string& hidden : d.anon_fields) {
            const int k = find_field_named(d, hidden);
            if (k < 0) continue;
            const int inner = d.fields[static_cast<size_t>(k)].type.struct_id;
            if (inner < 0 || static_cast<size_t>(inner) >= structs.size()) continue;
            if (find_field_named(structs[static_cast<size_t>(inner)], name) >= 0) {
                path->push_back(hidden);
                return true;
            }
            path->push_back(hidden);
            if (anon_path(inner, name, path)) return true;
            path->pop_back();
        }
        return false;
    }

    static int find_field_named(const StructDef& d, const std::string& name) {
        for (size_t i = d.fields.size(); i-- > 0;)  // the last: a member hides a base's member of the same name
            if (d.fields[i].name == name) return static_cast<int>(i);
        return -1;
    }

    bool parse_special_body(FuncDecl* fn, int cls, int special, bool implicit, std::vector<Local>* locals) {
        // The body is a "scope" block around the user's block (below): the code generator opens a scope for it, so
        // the parser opens one too, first, and the scope numbers of the locals match the generator's (Round 30:
        // they were one apart in constructors and destructors, and of two locals of one name the wrong one was found).
        struct ScopeGuard {
            Parser& p;
            explicit ScopeGuard(Parser& q) : p(q) { p.push_scope(); }
            ~ScopeGuard() { p.pop_scope(); }
        } scope_guard(*this);
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
                size_t after_ns = at;
                while (after_ns + 2 < tokens.size() && tokens[after_ns].kind == TIdent &&
                       namespaces.count(tokens[after_ns].text) > 0 && tokens[after_ns + 1].kind == TColonColon &&
                       tokens[after_ns + 2].kind == TIdent)
                    after_ns += 2;  // std::string(s): the namespace names were kept because a '(' follows
                const bool type_named = after_ns != at ||
                                        (at + 1 < tokens.size() &&
                                         (tokens[at + 1].kind == TColonColon || tokens[at + 1].kind == TLt ||
                                          (typedefs.count(cur().text) > 0 && find_field_named(def, cur().text) < 0)));
                at = after_ns;
                init.name = cur().text;
                if (type_named && is(TIdent) && at + 1 < tokens.size() && tokens[at + 1].kind == TLParen &&
                    typedefs.count(cur().text) == 0) {
                    next();  // ns::Base(x): the class's own name
                } else if (type_named) {
                    // std::string(s) or Base<T>(x) ([class.base.init]): a base named by a qualified name, a template-id
                    // or a typedef; it is matched to the base by the class it names.
                    Type bt;
                    bool imp = false;
                    bool exp = false;
                    if (!parse_type(&bt, &imp, &exp)) return false;
                    if (bt.base == 3 && bt.pointers == 0 && bt.struct_id >= 0)
                        init.name = structs[static_cast<size_t>(bt.struct_id)].name;
                } else {
                    next();
                }
                const size_t hoist_from = stmt_pre.size();
                if (accept(TLBrace)) {
                    // m{} and m{a, b} ([class.base.init] 3, [dcl.init.list]): {} value-initializes (zero for a scalar or
                    // an array), {a, b} passes the values as m(a, b) would. Own simplification: a class's
                    // initializer-list constructor is not chosen here.
                    while (!is(TRBrace) && !is(TEnd)) {
                        Node a;
                        if (!parse_expr(1, &a)) return false;
                        init.args.push_back(std::move(a));
                        if (!accept(TComma)) break;
                    }
                    if (!expect(TRBrace, "'}'")) return false;
                } else {
                    if (!expect(TLParen, "'('")) return false;
                    if (!parse_call_args(&init.args)) return false;
                }
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
            // Each base in declaration order ([class.copy.ctor] 14): by its copy constructor, given the base subobject of
            // the source, or field by field.
            for (const StructDef::BaseInfo& bi : def.bases) {
                if (class_has_copy(bi.sid)) {
                    std::vector<Node> args;
                    args.push_back(node_name("__o"));
                    Node call;
                    if (!special_call(bi.sid, "<ctor>", bi.off == 0 ? node_name("this") : offset_ptr(node_name("this"), bi.off, bi.sid),
                                      std::move(args), &call))
                        return false;
                    pre.push_back(std::move(call));
                } else {
                    for (size_t fi = bi.f0; fi < bi.f1; ++fi)
                        if (def.fields[fi].name.compare(0, 6, "__vptr") != 0) pre.push_back(assign_field(def.fields[fi].name));
                }
            }
            vptr_stores(def, &pre);
            for (size_t fi = def.base_fields; fi < def.fields.size(); ++fi) {
                const Field& field = def.fields[fi];
                if (field.name.compare(0, 6, "__vptr") == 0) continue;
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
            // The bases in declaration order ([class.base.init] 13), each with the arguments of its initializer.
            for (const StructDef::BaseInfo& bi : def.bases) {
                if (!class_needs_ctor(structs[static_cast<size_t>(bi.sid)])) continue;
                std::vector<Node> args;
                for (Init& init : inits)
                    if (init.name == structs[static_cast<size_t>(bi.sid)].name ||
                        (!structs[static_cast<size_t>(bi.sid)].tmpl.empty() && init.name == structs[static_cast<size_t>(bi.sid)].tmpl &&
                         def.bases.size() > 1))
                        args = std::move(init.args);
                const Node sub = bi.off == 0 ? node_name("this") : offset_ptr(node_name("this"), bi.off, bi.sid);
                Node call;
                if (!special_call(bi.sid, "<ctor>", sub, std::move(args), &call)) return false;
                pre.push_back(std::move(call));
                if (eh_ && class_needs_dtor(structs[static_cast<size_t>(bi.sid)])) {
                    eh_push_object(&pre, sub, bi.sid, 0, locals);
                    ++eh_held;
                }
            }
            vptr_stores(def, &pre);
            for (size_t fi = def.base_fields; fi < def.fields.size(); ++fi) {
                const Field& field = def.fields[fi];
                if (field.name.compare(0, 6, "__vptr") == 0) continue;
                Init* init = nullptr;
                for (Init& candidate : inits)
                    if (candidate.name == field.name) init = &candidate;
                if (def.ref_members.count(field.name) > 0) {
                    // r(x) for a reference member: the field is bound to x, it keeps x's address.
                    if (init == nullptr || init->args.size() != 1) {
                        error = "the reference member " + field.name + " needs a member initializer";
                        return false;
                    }
                    Node assign;
                    assign.kind = Node::Assign;
                    assign.op = TAssign;
                    assign.kids.push_back(node_this_field(field.name));
                    assign.kids.push_back(node_addr(std::move(init->args[0])));
                    Node stmt;
                    stmt.kind = Node::ExprStmt;
                    stmt.kids.push_back(std::move(assign));
                    pre.push_back(std::move(stmt));
                    continue;
                }
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
                    std::vector<Node> after;  // destructors of the list's element temporaries, after the constructor
                    Type list_type;
                    const size_t brace = field.init_at < 0 ? 0 : static_cast<size_t>(field.init_at) - (field.init_aggr ? 0 : 1);
                    if (init == nullptr && field.init_at >= 0 && (field.init_braced || field.init_aggr) &&
                        tokens[brace].kind == TLBrace && tokens[brace + 1].kind != TRBrace &&
                        has_init_list_ctor(field.type.struct_id, &list_type)) {
                        // std::vector<std::string> options{"a", "b"}; as a default member initializer: the constructor
                        // that takes an initializer_list ([over.match.list], [class.base.init] 9).
                        const size_t saved_at = at;
                        std::vector<Node> keep_pre = std::move(stmt_pre);
                        std::vector<Temp> keep_temps = std::move(stmt_temps);
                        stmt_pre.clear();
                        stmt_temps.clear();
                        at = brace;
                        Node list;
                        const bool ok = init_list_temp(list_type, &list);
                        at = saved_at;
                        for (Node& n : stmt_pre) pre.push_back(std::move(n));
                        for (size_t ti = stmt_temps.size(); ok && ti-- > 0;) {
                            Node dt;
                            if (!special_call(stmt_temps[ti].sid, "<dtor>", node_addr(node_name(stmt_temps[ti].name)), {}, &dt))
                                return false;
                            after.push_back(std::move(dt));
                        }
                        stmt_pre = std::move(keep_pre);
                        stmt_temps = std::move(keep_temps);
                        if (!ok) return false;
                        args.push_back(std::move(list));
                    } else if (init != nullptr) args = std::move(init->args);
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
                    for (Node& n : after) pre.push_back(std::move(n));
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
                    if (field.type.memptr >= 0) class_ptr_convert(&value, field.type);  // `int C::*pm = nullptr;`: -1
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
                    if (field.type.memptr >= 0) class_ptr_convert(&init->args[0], field.type);  // pm(nullptr): -1
                    assign.kids.push_back(std::move(init->args[0]));
                    Node stmt;
                    stmt.kind = Node::ExprStmt;
                    stmt.kids.push_back(std::move(assign));
                    pre.push_back(std::move(stmt));
                }
            }
            for (Init& init : inits) {
                bool known = false;
                for (const StructDef::BaseInfo& bi : def.bases)
                    if (init.name == structs[static_cast<size_t>(bi.sid)].name ||
                        (def.bases.size() > 1 && init.name == structs[static_cast<size_t>(bi.sid)].tmpl))
                        known = true;
                for (size_t fi = def.base_fields; fi < def.fields.size(); ++fi)
                    if (def.fields[fi].name == init.name) known = true;
                std::vector<std::string> path;
                if (!known && init.args.size() <= 1 && anon_path(cls, init.name, &path)) {
                    // V() : i(0) with i a member of an anonymous union of V ([class.base.init] 2, [class.union.anon]):
                    // stored through the hidden member that holds it.
                    Node target = node_this_field(path[0]);
                    for (size_t pk = 1; pk < path.size(); ++pk) {
                        Node step;
                        step.kind = Node::Field;
                        step.op = TDot;
                        step.text = path[pk];
                        step.kids.push_back(std::move(target));
                        target = std::move(step);
                    }
                    Node member;
                    member.kind = Node::Field;
                    member.op = TDot;
                    member.text = init.name;
                    member.kids.push_back(std::move(target));
                    Node value;
                    if (init.args.empty()) {
                        value.kind = Node::IntLit;
                        value.type.base = 1;
                    } else {
                        value = std::move(init.args[0]);
                    }
                    Node assign;
                    assign.kind = Node::Assign;
                    assign.op = TAssign;
                    assign.kids.push_back(std::move(member));
                    assign.kids.push_back(std::move(value));
                    Node stmt;
                    stmt.kind = Node::ExprStmt;
                    stmt.kids.push_back(std::move(assign));
                    pre.push_back(std::move(stmt));
                    known = true;
                }
                if (!known) {
                    error = "no member or base class named " + init.name;
                    return false;
                }
            }
        } else {
            vptr_stores(def, &pre);
            for (size_t fi = def.fields.size(); fi-- > def.base_fields;) {
                const Field& field = def.fields[fi];
                if (!obj_needs_dtor(field.type)) continue;
                Node call;
                if (!special_call(field.type.struct_id, "<dtor>", node_addr(node_this_field(field.name)), {}, &call))
                    return false;
                post.push_back(std::move(call));
            }
            // The bases in reverse declaration order ([class.dtor] 13).
            for (size_t bk = def.bases.size(); bk-- > 0;) {
                const StructDef::BaseInfo& bi = def.bases[bk];
                if (!class_needs_dtor(structs[static_cast<size_t>(bi.sid)])) continue;
                Node call;
                if (!special_call(bi.sid, "<dtor>", bi.off == 0 ? node_name("this") : offset_ptr(node_name("this"), bi.off, bi.sid),
                                  {}, &call))
                    return false;
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
    // auto f() { ... } and decltype(auto) f() { ... } ([dcl.spec.auto]): the body is read once with no result type,
    // to learn the type of the first return statement (no return: void), and the function gets that type. `auto` drops
    // a reference and a top-level const and turns an array into a pointer; `auto&` keeps the written reference;
    // `decltype(auto)` keeps a reference when the returned expression is one (a call returning T&, a reference name).
    // Own simplification: only the first return is looked at, and `return (x);` is not made a reference.
    bool deduce_return(FuncDecl* fn, int cls, int special, bool implicit) {
        const int mode = fn->ret.deduce;
        const bool written_ref = fn->ret_ref;
        FuncDecl probe = *fn;
        probe.ret = Type{};
        probe.ret.base = 0;
        probe.ret_ref = false;
        probe.sret = false;
        const bool saved_probe = lam_probe;
        const bool saved_seen = lam_probe_seen;
        const Type saved_type = lam_probe_type;
        const std::string saved_fn = lam_probe_fn;
        const bool saved_ref = lam_probe_ref;
        lam_probe = true;
        lam_probe_seen = false;
        lam_probe_ref = false;
        lam_probe_type = Type{};
        lam_probe_fn = fn->name;
        const size_t funcs_before = funcs.size();
        std::vector<Node> keep_pre = std::move(stmt_pre);
        stmt_pre.clear();
        std::vector<Temp> keep_temps = std::move(stmt_temps);
        stmt_temps.clear();
        const size_t probe_at = at;
        const bool ok = parse_function_body(&probe, cls, special, implicit);
        at = probe_at;
        stmt_pre = std::move(keep_pre);
        stmt_temps = std::move(keep_temps);
        (void)funcs_before;  // what the probe made (template instances, lambdas) is kept: a class records an instance as
                             // made, so it would not be made again when the body is read for real
        Type found = lam_probe_type;
        const bool seen = lam_probe_seen;
        const bool seen_ref = lam_probe_ref;
        lam_probe = saved_probe;
        lam_probe_seen = saved_seen;
        lam_probe_type = saved_type;
        lam_probe_fn = saved_fn;
        lam_probe_ref = saved_ref;
        if (!ok) return false;
        Type r;
        if (seen) {
            r = found;
        } else {
            r.base = 0;
        }
        r.deduce = 0;
        if (written_ref || (mode == 2 && seen_ref)) r.is_ref = true;
        fn->ret = r;
        fn->ret_ref = false;
        if (fn->ret.is_ref) {
            fn->ret_ref = true;
            fn->ret.is_ref = false;
            fn->ret.pointers += 1;
        }
        if (fn->func_id >= 0) func_types[static_cast<size_t>(fn->func_id)].ret = fn->ret;
        fn->sret = fn->ret.base == 3 && fn->ret.pointers == 0 && !fits_register(fn->ret);
        return true;
    }

    bool parse_function_body(FuncDecl* fn, int cls, int special = 0, bool implicit = false) {
        if (fn->ret.deduce != 0 && !deduce_return(fn, cls, special, implicit)) return false;
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
        if (cls >= 0 && structs[static_cast<size_t>(cls)].local_fn && saved_locals != nullptr &&
            structs[static_cast<size_t>(cls)].name.compare(0, 5, "__lam") != 0) {
            // A class defined in a function body may name the function's static locals ([class.local] 1; Round 30,
            // VisualScriptComponent.hpp's `struct Depth { Depth() { ++depth; } ... }` beside `static int depth`).
            for (const Local& outer : *saved_locals) {
                if (!outer.is_static) continue;
                bool hidden = false;
                for (const Local& own : locals)
                    if (own.name == outer.name) hidden = true;
                if (!hidden) place_local(&locals, outer);
            }
        }
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
        // The pointer's signature keeps the reference parameters as references, as a declared function pointer type
        // has them: a call through it binds its arguments the same way (adjust_fp_args), and the code generator passes
        // a reference as an address.
        for (size_t i = 0; i < ft.params.size(); ++i)
            if (fn->param_ref[i]) {
                ft.params[i].pointers -= 1;
                ft.params[i].is_ref = true;
            }
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
        // Function templates: how many leading parameters have no default argument (a call may leave the others out,
        // [dcl.fct.default]; Round 30). -1: every parameter is needed.
        int required = -1;
        bool takes(size_t nargs) const {
            size_t fixed = 0;
            bool has_pack = false;
            for (const Type& pt : proto) {
                if (pt.pack) has_pack = true;
                else ++fixed;
            }
            if (has_pack) return nargs >= fixed;
            return nargs == proto.size() || (required >= 0 && nargs >= static_cast<size_t>(required) && nargs < proto.size());
        }
        // The namespace the template was declared in: a bare call in an instance's body finds the functions of that
        // namespace, wherever the instance is made ([temp.nondep], [basic.lookup.unqual]; Round 29).
        std::string ns;
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
    // Every member template of one name, in the order declared (overloaded constructor templates: shared_ptr(Y*) and
    // shared_ptr(const shared_ptr<Y>&)); member_tmpls keeps the last one.
    std::map<std::pair<int, std::string>, std::vector<MemberTmpl>> member_tmpl_all{};
    TDecl inst_td{};
    struct DeferredInst {
        int sid = -1;
        std::vector<size_t> methods;
        std::string name;
        TDecl td;
        std::vector<TArg> args, bind_args;
        bool primary = true;
        std::string spec_key;
    };
    std::vector<DeferredInst> deferred_insts{};
    bool define_deferred_inst(const DeferredInst& d) {
        const auto saved = bind_params(d.td, d.bind_args);
        const std::string saved_base = inst_base_name;
        const std::vector<TArg> saved_inst_args = inst_args;
        const bool saved_primary = inst_primary;
        const std::string saved_spec_key = inst_spec_key;
        const std::vector<TArg> saved_spec_args = inst_spec_args;
        const TDecl saved_inst_td = inst_td;
        inst_base_name = d.name;
        inst_args = d.args;
        inst_primary = d.primary;
        inst_spec_key = d.spec_key;
        inst_spec_args = d.bind_args;
        inst_td = d.td;
        // The injected class name and the member typedefs, as they were inside the class body.
        std::vector<std::pair<std::string, std::pair<bool, Type>>> shadowed;
        auto put = [&](const std::string& n, const Type& t) {
            auto old = typedefs.find(n);
            shadowed.push_back({n, {old != typedefs.end(), old != typedefs.end() ? old->second : Type{}}});
            typedefs[n] = t;
        };
        Type self;
        self.base = 3;
        self.struct_id = d.sid;
        put(d.name, self);
        const std::map<std::string, Type> tds = structs[static_cast<size_t>(d.sid)].tdefs;
        for (const auto& td : tds) put(td.first, td.second);
        for (size_t mi : d.methods) {
            if (structs[static_cast<size_t>(d.sid)].methods[mi].body_at == 0) continue;
            const size_t pre_before = stmt_pre.size();
            const size_t temps_before = stmt_temps.size();
            if (define_method(d.sid, mi)) continue;
            unusable_members[structs[static_cast<size_t>(d.sid)].methods[mi].qname] = error;
            error.clear();
            stmt_pre.resize(std::min(stmt_pre.size(), pre_before));
            stmt_temps.resize(std::min(stmt_temps.size(), temps_before));
        }
        for (size_t k = shadowed.size(); k-- > 0;) {
            if (shadowed[k].second.first) typedefs[shadowed[k].first] = shadowed[k].second.second;
            else typedefs.erase(shadowed[k].first);
        }
        inst_base_name = saved_base;
        inst_args = saved_inst_args;
        inst_primary = saved_primary;
        inst_spec_key = saved_spec_key;
        inst_spec_args = saved_spec_args;
        inst_td = saved_inst_td;
        unbind_params(saved);
        return true;
    }
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
    // Variable templates by bare name: the template's parameters and the first token of the initializer.
    struct VarTmpl {
        TDecl td;
        size_t init_at = 0;
    };
    std::map<std::string, VarTmpl> var_templates{};
    // The local variables of the functions around the lambda bodies being read (innermost last), for decltype.
    std::vector<const std::vector<Local>*> lam_outer_locals_{};
    // name<args> for a variable template: the initializer read again with the parameters bound to args, as a value
    // (okay-ms's variable templates are constants: the address of one cannot be taken).
    bool parse_var_template_use(const VarTmpl& vt, Node* out) {
        std::vector<TArg> targs;
        if (!parse_template_args(vt.td, &targs, false)) return false;
        const auto bound = bind_params(vt.td, targs);
        const size_t saved = at;
        at = vt.init_at;
        const bool ok = parse_expr(1, out);
        const bool ended = ok && is(TSemi);
        at = saved;
        unbind_params(bound);
        if (ok && !ended) {
            error = "expected ';' after the initializer of a variable template";
            return false;
        }
        return ok;
    }
    // Further templates of the same name (overloads), told apart by the number of arguments: one
    // with a parameter pack takes any number from its fixed parameters up, and loses to a template
    // with exactly the number of parameters ([temp.func.order], simplified).
    std::map<std::string, std::vector<TDecl>> func_template_extra{};
    // The overloads whose template parameters accept the explicit template arguments of the call being read (f<0>(t)
    // cannot be a template <class X> f overload): set by parse_func_template_args, used once by pick_func_template.
    std::vector<const TDecl*> explicit_fit_{};
    std::string explicit_fit_name_{};
    // Reads f<...> for the function template `name`: the arguments are read against each overload in turn, since an
    // overload set may mix type and value parameters (std::get<0>(tuple) and std::get<T>(variant), [temp.over]); the
    // overloads they fit are remembered for pick_func_template.
    bool parse_func_template_args(const std::string& name, std::vector<TArg>* out) {
        std::vector<const TDecl*> cands{&func_templates[name]};
        auto extra = func_template_extra.find(name);
        if (extra != func_template_extra.end())
            for (const TDecl& cand : extra->second) cands.push_back(&cand);
        const size_t start = at;
        size_t end = start;
        std::string first_error;
        std::vector<const TDecl*> fit;
        for (const TDecl* cand : cands) {
            at = start;
            error.clear();
            std::vector<TArg> args;
            if (!parse_template_args(*cand, &args, true)) {
                if (first_error.empty()) first_error = error;
                continue;
            }
            if (fit.empty()) {
                *out = std::move(args);
                end = at;
            }
            fit.push_back(cand);
            if (cands.size() == 1) break;
        }
        error.clear();
        if (fit.empty()) {
            error = first_error;
            return false;
        }
        at = end;
        explicit_fit_ = fit.size() == cands.size() ? std::vector<const TDecl*>() : fit;
        explicit_fit_name_ = name;
        return true;
    }
    // The number of arguments of the call whose '(' is at `open` (top-level commas; 0 for "()").
    size_t call_arg_count_at(size_t open) const {
        int depth = 0;
        size_t count = 0;
        bool any = false;
        for (size_t k = open; k < tokens.size() && tokens[k].kind != TEnd; ++k) {
            const Tok kk = tokens[k].kind;
            if (kk == TLParen || kk == TLBrack || kk == TLBrace) {
                if (depth++ > 0) any = true;
                continue;
            }
            if (kk == TRParen || kk == TRBrack || kk == TRBrace) {
                if (--depth == 0) break;
                continue;
            }
            any = true;
            if (kk == TComma && depth == 1) ++count;
        }
        return any ? count + 1 : 0;
    }
    // Whether an overload of the function template `name` takes nargs arguments (by its parameters; a pack takes any
    // number from its fixed parameters up). Defaults of function templates are not counted.
    bool func_template_takes(const std::string& name, size_t nargs) {
        std::vector<const TDecl*> cands{&func_templates[name]};
        auto extra = func_template_extra.find(name);
        if (extra != func_template_extra.end())
            for (const TDecl& cand : extra->second) cands.push_back(&cand);
        for (const TDecl* cand : cands)
            if (cand->takes(nargs)) return true;
        return false;
    }
    const TDecl& pick_func_template(const std::string& name, size_t nargs) {
        std::vector<const TDecl*> only;
        if (explicit_fit_name_ == name) only.swap(explicit_fit_);
        explicit_fit_.clear();
        explicit_fit_name_.clear();
        const TDecl* best = &func_templates[name];
        if (!only.empty()) best = only.front();
        bool best_exact = false;
        auto consider = [&](const TDecl& cand) {
            if (!only.empty() && std::find(only.begin(), only.end(), &cand) == only.end()) return;
            bool has_pack = false;
            for (const Type& pt : cand.proto)
                if (pt.pack) has_pack = true;
            if (!cand.takes(nargs)) return;
            if (!has_pack && !best_exact) {
                best = &cand;
                best_exact = true;
            } else if (!best_exact && best == (only.empty() ? &func_templates[name] : only.front())) {
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
    // The decorated spelling of a template argument's type. A pointer to a function (a lambda without captures is
    // one here) is P6A, the result, the parameters and @Z, so two lambdas of other signatures make two instances
    // (Round 30: both were PEAH, and the second call used the first one's instance: wrong code).
    std::string targ_type_code(const Type& t) const {
        if (t.func_id >= 0 && t.pointers == 1 && !t.is_ref && static_cast<size_t>(t.func_id) < func_types.size()) {
            const FuncType& ft = func_types[static_cast<size_t>(t.func_id)];
            std::string out = "P6A" + targ_type_code(ft.ret);
            if (ft.params.empty() && !ft.vararg) return out + "XZ";
            for (const Type& p : ft.params) out += targ_type_code(p);
            return out + (ft.vararg ? "ZZ" : "@Z");
        }
        return cxx_type(t, &structs);
    }
    std::string arg_code(const TArg& a) const {
        if (a.is_pack) {  // MSVC writes the elements of a pack one after the other
            std::string out;
            for (const Type& t : a.pack) out += targ_type_code(t);
            return out;
        }
        return a.is_value ? value_code(a.value) : targ_type_code(a.type);
    }
    std::string arg_key(const TArg& a) const {
        if (a.is_pack) {
            std::string out = "{";
            for (const Type& t : a.pack) out += targ_type_code(t) + ";";
            return out + "}";
        }
        return a.is_value ? "$" + std::to_string(a.value) : targ_type_code(a.type);
    }
    // Packs in scope: typename... Ts is a list of types, a function parameter pack names the
    // parameters it was expanded to.
    std::map<std::string, std::vector<Type>> type_packs{};
    std::map<std::string, std::vector<std::string>> param_packs{};

    // At a class template name inside '(': is it `(Box<T>)`, `(Box<T>*)` or `(Box<T>&)`, a cast? Not when the argument list
    // is followed by anything else (`(is_arithmetic<It>::value)`, `(Box<int>(x) + 1)`).
    bool template_id_cast_at(size_t i) const {
        size_t k = i + 1;
        int angle = 0;
        for (; k < tokens.size() && tokens[k].kind != TEnd; ++k) {
            if (tokens[k].kind == TLt) ++angle;
            else if (tokens[k].kind == TGt && --angle == 0) break;
        }
        if (k + 1 >= tokens.size()) return false;
        size_t j = k + 1;
        while (j < tokens.size() && (tokens[j].kind == TStar || tokens[j].kind == TAmp || tokens[j].kind == TAndAnd ||
                                     tokens[j].kind == TConst))
            ++j;
        return j < tokens.size() && tokens[j].kind == TRParen;
    }
    bool is_template_name_at(size_t i) const {
        if (!(i + 1 < tokens.size() && tokens[i].kind == TIdent && tokens[i + 1].kind == TLt &&
              class_templates.count(tokens[i].text) > 0))
            return false;
        // A local variable or a data member of the same name hides the template ([basic.scope.hiding]): `duration < k`
        // in a Tween member compares its `duration`.
        if (!tokens[i].nsq && ((cur_locals != nullptr && local_visible(tokens[i].text)) || member_hides(tokens[i].text, tokens[i + 1].kind)))
            return false;
        return true;
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
                // f<X>(box) with f's pack not written: it is deduced from the call ([temp.arg.explicit] 4).
                if (partial) break;
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
            // A default argument may name the parameters before it (class C = vector<T>, [temp.param] 14): they are
            // bound to the arguments read so far while it is read.
            TDecl head = td;
            head.params.resize(std::min(idx, out->size()));
            const std::vector<TArg> head_args(out->begin(), out->begin() + static_cast<std::ptrdiff_t>(head.params.size()));
            const auto bound = bind_params(head, head_args);
            const size_t saved = at;
            at = td.params[idx].def_at;
            TArg a;
            const bool ok = parse_one_arg(td.params[idx], &a);
            at = saved;
            unbind_params(bound);
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
            if (accept(TAmp)) {
                a->type.is_ref = true;
            } else if (accept(TAndAnd)) {
                a->type.is_ref = true;
                a->type.rref = true;
            }
            if (is(TLParen)) {
                // A function type as a template argument: R(A, B) or a function pointer R(*)(A, B) ([dcl.fct]).
                next();
                int stars = 0;
                if (is(TStar)) {
                    while (accept(TStar)) ++stars;
                    if (!expect(TRParen, "')'") || !expect(TLParen, "'('")) return false;
                }
                Declarator fd;
                if (!parse_param_list(&fd)) return false;
                FuncType ft;
                ft.ret = a->type;
                ft.params = fd.params;
                ft.vararg = fd.vararg;
                // A function pointer is its return type with one more '*' and the signature (as parse_declarator makes
                // it); a function type itself is void with the signature and no '*' (so a function returning a pointer
                // stays apart from a pointer to a function).
                Type fnt;
                if (stars > 0) {
                    fnt = a->type;
                    fnt.pointers += stars;
                } else {
                    fnt.base = 0;
                }
                fnt.func_id = static_cast<int>(func_types.size());
                func_types.push_back(ft);
                a->type = fnt;
            }
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
        if (pat.type.tpat < 0 && pat.type.base >= 100 && pat.type.pointers == 0 && !pat.type.is_array()) {
            // A specialization pattern T, T& or T&& against a template argument: T& takes only an lvalue reference
            // and T&& only an rvalue reference, and T itself is the argument without that reference
            // ([temp.class.spec.match], [temp.deduct.type] 8). Before, T& matched int as well (is_reference<int>
            // was true).
            if (pat.type.is_ref && (!act.type.is_ref || pat.type.rref != act.type.rref)) return false;
            Type bound = act.type;
            if (pat.type.is_ref) {
                bound.is_ref = false;
                bound.rref = false;
            }
            const size_t idx = static_cast<size_t>(pat.type.base - 100);
            if (idx >= args->size()) return false;
            if ((*got)[idx]) return !(*args)[idx].is_value && arg_key((*args)[idx]) == cxx_type(bound, &structs);
            (*args)[idx].is_value = false;
            (*args)[idx].type = bound;
            (*got)[idx] = true;
            return true;
        }
        return match_type(pat.type, act.type, args, got);
    }
    bool match_type(const Type& pat, const Type& act, std::vector<TArg>* args, std::vector<bool>* got) const {
        if (pat.tpat == -2) return true;
        if (pat.memptr >= 0 && pat.pointers == 0) {
            // I Owner::*m from &C::x: Owner is C and I the member's type ([temp.deduct.type] 8; Round 30).
            if (act.memptr < 0 || act.pointers != 0) return false;
            const std::pair<Type, Type> pm = memptr_types()[static_cast<size_t>(pat.memptr)];
            const std::pair<Type, Type> am = memptr_types()[static_cast<size_t>(act.memptr)];
            Type member = am.second;
            member.is_const = false;
            member.pconst = false;
            return match_type(pm.first, am.first, args, got) && match_type(pm.second, member, args, got);
        }
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
        if (args_in.size() == 1 && !args_in[0].is_value && !args_in[0].is_pack && args_in[0].type.func_id >= 0 &&
            args_in[0].type.pointers == 0 && args_in[0].type.base == 0 && class_templates.count(name + "__fn") > 0) {
            // function<R(A...)>: a class template given a function type, with a companion template name__fn<R, A...>.
            // Own stand-in for the partial specialization `template <class R, class... A> class function<R(A...)>`
            // ([temp.class.spec]); okay-ms does not match patterns made of function types.
            const FuncType ft = func_types[static_cast<size_t>(args_in[0].type.func_id)];
            std::vector<TArg> split(2);
            split[0].type = ft.ret;
            split[1].is_pack = true;
            split[1].pack = ft.params;
            return instantiate_class(name + "__fn", split, out);
        }
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
            std::vector<int> base_ids;
            if (accept(TColon)) {
                while (ok) {
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
                        base_ids.push_back(bt.struct_id);
                    }
                    if (!accept(TComma)) break;
                }
            }
            if (ok && !is(TLBrace)) {
                error = "expected a class template body";
                ok = false;
            }
            // The base class may itself be an instance made just now (A<T> : B<T>), which used and cleared pending_sym.
            pending_sym = sym;
            if (ok) ok = parse_struct_body(false, key, &id, as_class, base_ids);
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
            if (tp.is_type && (is(TAssign) || is(TComma) || is(TGt))) {
                // template <class T, class = enable_if_t<...>>: a parameter without a name ([temp.param]).
                tp.name = "__tparam" + std::to_string(td.params.size());
            } else if (!is(TIdent)) {
                error = "expected a template parameter name";
                return false;
            } else {
                tp.name = cur().text;
                next();
            }
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
    // The token after a template's declaration, which starts at k: after its first top-level body (and the ';' after a
    // class body when class_semi), or after its ';'. Braces inside a top-level parenthesis (a default argument
    // `const V3& grow = V3{}`, Round 30) do not start the body.
    size_t template_decl_end(size_t k, bool class_semi) const {
        int depth = 0;
        int parens = 0;
        bool body = false;
        while (k < tokens.size() && tokens[k].kind != TEnd) {
            const Tok kk = tokens[k].kind;
            if (depth == 0 && kk == TLParen) {
                ++parens;
            } else if (depth == 0 && kk == TRParen) {
                if (parens > 0) --parens;
            } else if (parens > 0) {
                // inside the parameter list (or another top-level parenthesis)
            } else if (kk == TLBrace) {
                ++depth;
                body = true;
            } else if (kk == TRBrace) {
                if (--depth == 0) {
                    ++k;
                    if (class_semi && k < tokens.size() && tokens[k].kind == TSemi) ++k;
                    break;
                }
            } else if (kk == TSemi && depth == 0 && !body) {
                ++k;
                break;
            }
            ++k;
        }
        return k;
    }
    bool parse_template_decl(bool headless = false) {
        TDecl td;
        td.ns = current_ns;
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
        // template <typename T> static T f(...) at namespace scope: an instance has an internal symbol here anyway, so
        // the `static` is dropped (in a class it means a static member function and stays).
        if (struct_nest == 0 && is(TStatic)) next();
        td.start = at;
        // The declaration ends after its first top-level body or semicolon.
        td.end = template_decl_end(at, tokens[td.start].kind == TStruct);
        // template <class T> struct tm *f(...): a function template whose result type is written with `struct`
        // ([dcl.type.elab]), not a class template: '*', '&' or a name and '(' follow the class name (Round 29).
        const bool elaborated_fn =
            tokens[td.start].kind == TStruct && td.start + 3 < tokens.size() && tokens[td.start + 1].kind == TIdent &&
            (tokens[td.start + 2].kind == TStar || tokens[td.start + 2].kind == TAmp ||
             (tokens[td.start + 2].kind == TIdent && tokens[td.start + 3].kind == TLParen));
        if (tokens[td.start].kind == TStruct && !elaborated_fn) {
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
        if (struct_nest == 0 && !td.params.empty()) {
            // template <class T> constexpr bool name = e; a variable template ([temp.pre] 1): a name, then '=' at the
            // top level before any '(' or '{'. Kept as the token range of its initializer; a use name<args> reads that
            // expression again with the parameters bound (parse_var_template_use).
            int nest = 0;
            for (size_t k = td.start; k < td.end; ++k) {
                const Tok kk = tokens[k].kind;
                if (kk == TLParen || kk == TLBrace) break;
                if (kk == TLt) ++nest;
                else if (kk == TGt) --nest;
                else if (kk == TAssign && nest == 0) {
                    if (k > td.start && tokens[k - 1].kind == TIdent &&
                        !(k > td.start + 1 && tokens[k - 2].kind == TColonColon)) {  // not X<T>::member = v
                        VarTmpl vt;
                        vt.td = td;
                        vt.init_at = k + 1;
                        var_templates[tokens[k - 1].text] = vt;
                        at = td.end;
                        return true;
                    }
                    break;
                }
            }
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
        size_t paren = first_decl_paren(td.start, td.end);
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
        pat_rest_ok = true;  // a parameter of type Box<T...> deduces the pack T ([temp.deduct.type] 9)
        const bool ok = parse_param_list(&pd);
        pat_rest_ok = false;
        pat_values = nullptr;
        td.proto = pd.params;
        td.required = static_cast<int>(required_args(pd.param_defaults, pd.params.size()));
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
    // The '(' that opens a declared function's parameters: the first one that is not part of a decltype(...) in the
    // return type (decltype(auto) f(...)).
    size_t first_decl_paren(size_t from, size_t end) const {
        size_t k = from;
        while (k < end && tokens[k].kind != TLParen) {
            if (tokens[k].kind == TIdent && tokens[k].text == "decltype" && k + 1 < end && tokens[k + 1].kind == TLParen) {
                int depth = 0;
                for (++k; k < end; ++k) {
                    if (tokens[k].kind == TLParen) ++depth;
                    else if (tokens[k].kind == TRParen && --depth == 0) break;
                }
            }
            ++k;
        }
        return k;
    }

    bool parse_member_template(int self) {
        next();  // template
        if (!expect(TLt, "'<'")) return false;
        MemberTmpl mt;
        if (!parse_tparams(&mt.td)) return false;
        if (!expect(TGt, "'>'")) return false;
        mt.td.start = at;
        mt.td.end = template_decl_end(at, false);
        size_t paren = first_decl_paren(mt.td.start, mt.td.end);
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
        pat_rest_ok = true;
        const bool ok = parse_param_list(&pd);
        pat_rest_ok = false;
        pat_values = nullptr;
        mt.td.proto = pd.params;
        at = was_at;
        unbind_params(saved);
        if (!ok) return false;
        at = mt.td.end;
        member_tmpl_all[std::make_pair(self, mname)].push_back(mt);
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
        // The constructor templates of the class that take this many arguments, in the order declared.
        std::vector<MemberTmpl> cands;
        for (auto it = member_tmpl_all.begin(); it != member_tmpl_all.end(); ++it) {
            if (it->first.first != sid) continue;
            for (const MemberTmpl& c : it->second)
                if (it->first.second == c.owner && c.td.proto.size() == args.size()) cands.push_back(c);
        }
        (void)owner;
        if (cands.empty()) return true;
        if (args.size() == 1) {
            Type at;
            if (static_type(args[0], &at) && at.base == 3 && at.pointers == 0 && !at.is_array()) {
                for (int walk = at.struct_id; walk >= 0; walk = structs[static_cast<size_t>(walk)].base)
                    if (walk == sid) return true;  // a copy
            }
        }
        // A constructor that is not a template and takes the arguments without a user-defined conversion is the
        // better match ([over.match.best]: a non-template wins a tie); otherwise the template is tried.
        for (const Method& m : def.methods) {
            // A constructor whose last parameters have defaults counts too (string(const string&, size_t pos,
            // size_t n = npos) for std::string(s, 5); Round 29).
            if (m.name != "<ctor>" || !m.tsym.empty() || m.params.size() < args.size() ||
                required_args(m.defaults, m.params.size()) > args.size())
                continue;
            const int score = call_score(m.params, args);
            if (score >= 0 && score < 4) return true;
        }
        // The first template whose arguments can be deduced from the call ([temp.deduct.call]; own simplification: no
        // partial ordering between two that both deduce, the one declared first is taken).
        size_t pick = 0;
        std::vector<TArg> targs;
        bool deduced = false;
        std::string first_error;
        for (size_t ci = 0; ci < cands.size() && !deduced; ++ci) {
            targs.clear();
            const std::string keep_error = error;
            if (deduce_args(cands[ci].td, cands[ci].owner, &targs, args)) {
                deduced = true;
                pick = ci;
            } else {
                if (first_error.empty()) first_error = error;
                error = keep_error;
            }
        }
        if (!deduced) {
            error = first_error;
            return false;
        }
        const MemberTmpl mt = cands[pick];
        std::string codes;
        for (const TArg& a : targs) codes += arg_code(a);
        // Two templates deduced with the same arguments are different instances: the later ones get their position.
        size_t index = 0;
        for (size_t ci = 0; ci < cands.size(); ++ci)
            if (cands[ci].td.start == mt.td.start) index = ci;
        const std::string tsym = "?$?0" + codes + (index > 0 ? "@" + std::to_string(index) : std::string());
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
                skip_mem_initializers();
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
        MemberTmpl mt = *found;  // a copy: the map may change while the instance is compiled
        size_t overload = 0;
        auto all = member_tmpl_all.find(std::make_pair(owner, name));
        if (all != member_tmpl_all.end() && all->second.size() > 1) {
            // Overloaded member templates (wait_for(lk, d) and wait_for(lk, d, pred)): the first, in the order declared,
            // that takes this many arguments and whose arguments can be deduced ([temp.over]; own simplification: no
            // partial ordering). The first pass wants the exact number of parameters, the second allows defaults and packs.
            const std::vector<TArg> given = targs;
            bool picked = false;
            std::string first_error;
            for (int pass = 0; pass < 2 && !picked; ++pass)
                for (size_t ci = 0; ci < all->second.size() && !picked; ++ci) {
                    const MemberTmpl& c = all->second[ci];
                    bool pack = false;
                    for (const Type& t : c.td.proto)
                        if (t.pack) pack = true;
                    if (pass == 0 && (pack || c.td.proto.size() != call_args.size())) continue;
                    if (pass == 1 && !pack && c.td.proto.size() < call_args.size()) continue;
                    std::vector<TArg> trial = given;
                    const std::string keep_error = error;
                    if (deduce_args(c.td, name, &trial, call_args)) {
                        mt = c;
                        targs = trial;
                        overload = ci;
                        picked = true;
                    } else {
                        if (first_error.empty()) first_error = error;
                        error = keep_error;
                    }
                }
            if (!picked) {
                error = first_error.empty() ? "cannot deduce the arguments of " + name : first_error;
                return false;
            }
        } else if (!deduce_args(mt.td, name, &targs, call_args)) {
            return false;
        }
        std::string key = name + "<";
        std::string codes;
        for (size_t i = 0; i < targs.size(); ++i) {
            if (i) key += ",";
            key += arg_key(targs[i]);
            codes += arg_code(targs[i]);
        }
        key += ">";
        if (overload > 0) {
            key += "#" + std::to_string(overload);
            codes += "@" + std::to_string(overload);
        }
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
        if (ok && mt.bodyless) {
            // T* S::f() or T& S::f(): the '*' and '&' of the result come before the class name.
            while (accept(TStar)) ++type.pointers;
            if (accept(TAmp)) type.is_ref = true;
            else if (accept(TAndAnd)) {
                type.is_ref = true;
                if (type.pointers == 0) type.rref = true;
            }
        }
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
            if (mt.bodyless) {
                // Defined outside the class: the default arguments are the ones written in the class
                // ([dcl.fct.default] 4), read from that declaration with the same parameters bound.
                const size_t def_at = at;
                at = mt.td.start;
                if (is(TStatic)) next();
                Type in_type;
                bool imp2 = false;
                bool exp2 = false;
                Declarator in_decl;
                if (parse_type(&in_type, &imp2, &exp2) && parse_declarator(in_type, &in_decl) &&
                    in_decl.params.size() == m.params.size()) {
                    for (size_t di = 0; di < m.params.size(); ++di)
                        if (!has_default(m.defaults, di) && has_default(in_decl.param_defaults, di)) {
                            if (m.defaults.size() < m.params.size()) m.defaults.resize(m.params.size());
                            m.defaults[di] = in_decl.param_defaults[di];
                        }
                }
                error.clear();
                at = def_at;
            }
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
    // f(true) deduces T = bool ([lex.bool]) and f('a') T = char ([lex.ccon]); their nodes are int constants here.
    static void literal_type(const Node& arg, Type* t) {
        if (arg.kind != Node::IntLit) return;
        if (arg.text == "bool") {
            *t = bool_type();
        } else if (arg.text == "char") {
            *t = Type{};
            t->base = 2;
        }
    }
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
                        literal_type(call_args[j], &t);
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
            if (pt.tpat == -2 || (pt.base < 100 && pt.tpat < 0 && pt.memptr < 0)) continue;
            if (pt.tpat < 0 && pt.base >= 100 && explicit_given[static_cast<size_t>(pt.base - 100)]) continue;
            {
                // Every template parameter given explicitly (Reg<int>(name, lambda) for a std::function<shared_ptr<T>(...)>
                // parameter): the explicit arguments are substituted and nothing is deduced from this argument
                // ([temp.deduct.call] 1, [temp.arg.explicit] 6; Round 29).
                bool all_given = !explicit_given.empty();
                for (bool g : explicit_given) all_given = all_given && g;
                if (all_given) continue;
            }
            if (pt.tpat >= 0 && call_args[i].kind == Node::Block && call_args[i].text == "tokbrace" &&
                tpats[static_cast<size_t>(pt.tpat)].name == "initializer_list" &&
                tpats[static_cast<size_t>(pt.tpat)].args.size() == 1) {
                // max({a, b, c}) for a parameter initializer_list<T> ([temp.deduct.call] 1): T is deduced from each
                // element of the list, and they must agree. The list is read here only for its types (what reading it
                // made is dropped; adjust_args reads it again for the call).
                const TArg elem_pat = tpats[static_cast<size_t>(pt.tpat)].args[0];
                Node list;
                {
                    const size_t keep_at = at;
                    const size_t keep_pre = stmt_pre.size();
                    const size_t keep_temps = stmt_temps.size();
                    at = static_cast<size_t>(call_args[i].number);
                    const bool read = parse_init(&list);
                    at = keep_at;
                    stmt_pre.resize(keep_pre);
                    stmt_temps.resize(keep_temps);
                    if (!read || list.kids.empty()) {
                        error = "cannot deduce template arguments of " + name;
                        return false;
                    }
                }
                for (const Node& kid : list.kids) {
                    Type et;
                    if (!static_type(kid, &et)) {
                        error = "cannot deduce template arguments of " + name;
                        return false;
                    }
                    literal_type(kid, &et);
                    et.is_ref = false;
                    et.is_const = false;
                    et.pconst = false;
                    TArg ea;
                    ea.type = et;
                    if (!match_arg(elem_pat, ea, &args, &given)) {
                        error = "cannot deduce template arguments of " + name + " (the list's elements differ)";
                        return false;
                    }
                }
                continue;
            }
            Type at_type;
            if (!static_type(call_args[i], &at_type)) {
                error = "cannot deduce template arguments of " + name;
                return false;
            }
            literal_type(call_args[i], &at_type);
            if (at_type.is_array()) {
                at_type = peel_array(at_type);
                at_type.pointers += 1;
                at_type.array_len = -1;
            }
            at_type.is_ref = false;
            match_loose = true;
            const bool fits = match_type(pt, at_type, &args, &given);
            match_loose = false;
            if (!fits && pt.tpat >= 0 && at_type.base == 3 && at_type.pointers == 0 && at_type.struct_id >= 0 &&
                structs[static_cast<size_t>(at_type.struct_id)].name.compare(0, 5, "__lam") == 0)
                continue;  // a lambda for a std::function<...> parameter: converted later, the parameters come from elsewhere
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
        for (size_t i = 0; i < td.params.size(); ++i) {
            if (given[i] || !td.params[i].has_def) continue;
            // A default template argument ([temp.deduct] 5), read with the parameters before it bound. Own
            // simplification: one that does not compile is an error (no SFINAE: the template is not set aside).
            bool all_before = true;
            for (size_t k = 0; k < i; ++k)
                if (!given[k]) all_before = false;
            if (!all_before) continue;
            TDecl head = td;
            head.params.resize(i);
            std::vector<TArg> head_args(args.begin(), args.begin() + static_cast<std::ptrdiff_t>(i));
            const auto bound = bind_params(head, head_args);
            const size_t saved = at;
            at = td.params[i].def_at;
            TArg a;
            const bool ok = parse_one_arg(td.params[i], &a);
            at = saved;
            unbind_params(bound);
            if (!ok) return false;
            args[i] = a;
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
        {
            // Overloads of one template name (next(It) and next(It, n)) are separate instances ([temp.over]).
            auto extra = func_template_extra.find(name);
            if (extra != func_template_extra.end())
                for (size_t k = 0; k < extra->second.size(); ++k)
                    if (&extra->second[k] == &td) key += "#" + std::to_string(k + 1);
        }
        *out = key;
        for (const FuncDecl& fn : funcs)
            if (fn.name == key) return true;
        if (in_instance.count(key)) return true;  // a call inside its own body
        in_instance.insert(key);
        const size_t saved_at = at;
        const std::string saved_ns = current_ns;
        current_ns = td.ns;
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
            fn.defaults = decl.param_defaults;  // the call pads the arguments it left out (Round 30)
            finish_signature(&fn);
            // A call of the instance inside its own body (FindInTree<T> calling itself, Round 29) needs its
            // signature: a declaration first, merged with the definition at the end of the file (finish_program).
            if (fn.ret.deduce == 0) {
                FuncDecl early = fn;
                early.defined = false;
                funcs.push_back(std::move(early));
            }
            if (!parse_function_body(&fn, -1)) ok = false;
            else funcs.push_back(std::move(fn));
        }
        unbind_params(saved);
        at = saved_at;
        current_ns = saved_ns;
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
        const size_t before = leaves->size();
        const bool ok = flatten_init(type, init, &g->init_bytes, 0);
        dyn_leaves = nullptr;
        init_relocs = nullptr;
        if (leaves->size() > before) g->dynamic = true;
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
        // A template instance's function that calls one of them is not made either (vector<pair<K, V>>::resize when
        // pair<K, V>() cannot be); any other function that calls one is an error.
        for (bool changed = !unusable_members.empty(); changed;) {
            changed = false;
            for (size_t i = 0; i < funcs.size(); ++i) {
                std::string which;
                if (!funcs[i].defined || funcs[i].symbol.find("?$") == std::string::npos ||
                    !find_unusable_call(funcs[i].body, &which))
                    continue;
                unusable_members[funcs[i].name] = unusable_members[which];
                funcs.erase(funcs.begin() + static_cast<std::ptrdiff_t>(i));
                --i;
                changed = true;
            }
        }
        for (const FuncDecl& f : funcs) {
            std::string which;
            if (f.defined && find_unusable_call(f.body, &which)) {
                error = which + " (called in " + f.name + ") cannot be made for these template arguments: " + unusable_members[which];
                return false;
            }
        }
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
        for (const Thunk& t : thunks) {
            FuncDecl fn;
            fn.name = t.symbol;
            fn.symbol = t.symbol;
            fn.defined = true;
            fn.is_static = true;
            fn.thunk_target = t.target;
            fn.thunk_adj = t.adj;
            fn.thunk_rdx = t.rdx;
            funcs.push_back(std::move(fn));
        }
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

    // A friend function defined in its class ([class.friend] 6, a "hidden friend": friend bool operator==(S a, S b)
    // { ... }) is a function of the namespace around the class. Its tokens are moved to just after the outermost
    // enclosing class (`};`), marked inline, and read there as any namespace-scope function. Own simplifications: one in a
    // class template (or a class inside one) stays where it is and is not supported; a type nested in the class must be
    // named with its class.
    void hoist_friend_functions() {
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (!(tokens[i].kind == TIdent && tokens[i].text == "friend")) continue;
            // The declaration: to the first ';' or '{' at this level; a function body follows a ')' (or `const`).
            size_t k = i + 1;
            int paren = 0;
            while (k < tokens.size() && tokens[k].kind != TEnd) {
                if (tokens[k].kind == TLParen) ++paren;
                else if (tokens[k].kind == TRParen) --paren;
                else if (paren == 0 && (tokens[k].kind == TSemi || tokens[k].kind == TLBrace)) break;
                ++k;
            }
            if (k >= tokens.size() || tokens[k].kind != TLBrace || k == i + 1) continue;
            if (!(tokens[k - 1].kind == TRParen || tokens[k - 1].kind == TConst)) continue;  // friend class X { ...: no
            size_t body_end = k;
            for (int depth = 0; body_end < tokens.size() && tokens[body_end].kind != TEnd; ++body_end) {
                if (tokens[body_end].kind == TLBrace) ++depth;
                else if (tokens[body_end].kind == TRBrace && --depth == 0) break;
            }
            if (body_end >= tokens.size() || tokens[body_end].kind != TRBrace) continue;
            // The braces open around the friend: for each, whether it is a class body and whether a template is in front.
            std::vector<std::pair<size_t, bool>> open;  // (index of '{', class body)
            bool in_template = false;
            for (size_t q = 0; q < i; ++q) {
                if (tokens[q].kind == TLBrace) {
                    // A class body: `struct|class|union Name [final] [: bases] {` (the head goes back to the keyword).
                    bool cls = false;
                    size_t h = q;
                    int angle = 0;
                    while (h > 0) {
                        --h;
                        const Tok hk = tokens[h].kind;
                        if (hk == TGt) ++angle;
                        else if (hk == TLt) --angle;
                        if (angle > 0) continue;
                        if (hk == TStruct || hk == TUnion) {
                            cls = true;
                            break;
                        }
                        if (hk == TSemi || hk == TLBrace || hk == TRBrace || hk == TRParen || hk == TAssign ||
                            hk == TNamespace)
                            break;
                    }
                    open.push_back({q, cls});
                } else if (tokens[q].kind == TRBrace && !open.empty()) {
                    open.pop_back();
                }
            }
            size_t outer_class = 0;
            bool found = false;
            for (const auto& o : open)
                if (o.second) {
                    outer_class = o.first;
                    found = true;
                    break;
                }
            if (!found) continue;
            // A template in front of the outermost class (template <...> struct X {): not moved.
            for (size_t h = outer_class; h > 0 && !in_template;) {
                --h;
                if (tokens[h].kind == TSemi || tokens[h].kind == TLBrace || tokens[h].kind == TRBrace) break;
                if (tokens[h].kind == TIdent && tokens[h].text == "template") in_template = true;
            }
            if (in_template) continue;
            size_t class_end = outer_class;
            for (int depth = 0; class_end < tokens.size() && tokens[class_end].kind != TEnd; ++class_end) {
                if (tokens[class_end].kind == TLBrace) ++depth;
                else if (tokens[class_end].kind == TRBrace && --depth == 0) break;
            }
            if (class_end + 1 >= tokens.size() || tokens[class_end + 1].kind != TSemi) continue;
            std::vector<Token> fn(tokens.begin() + static_cast<std::ptrdiff_t>(i + 1),
                                  tokens.begin() + static_cast<std::ptrdiff_t>(body_end + 1));
            if (!fn.empty()) fn.front().inl = true;
            const size_t insert_at = class_end + 2;
            tokens.insert(tokens.begin() + static_cast<std::ptrdiff_t>(insert_at), fn.begin(), fn.end());
            tokens.erase(tokens.begin() + static_cast<std::ptrdiff_t>(i), tokens.begin() + static_cast<std::ptrdiff_t>(body_end + 1));
            --i;
        }
    }

    bool parse() {
        hoist_friend_functions();
        collapse_operators();
        expand_alias_templates();
        lower_cxx_spellings();
        expand_namespace_aliases();
        strip_namespace_qualifiers();
        // Room for the call operators of generic lambdas, which are added after the end (parse_lambda): the vector must
        // not move while it is read.
        tokens.reserve(tokens.size() + tokens.size() / 2 + 4096);
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
            if (is(TIdent) && cur().text == "extern" && at + 1 < tokens.size() && tokens[at + 1].kind != TString) {
                // extern T name; ([dcl.stc] 5): a declaration of a variable defined elsewhere (or of a function, where it
                // changes nothing). The next declaration reads it (extern_decl_).
                next();
                extern_decl_ = true;
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
                const bool inline_ns = cur().inl;  // inline namespace X { } ([namespace.def] 5)
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
                // The members of an inline namespace are members of the namespace around it too: its functions can be
                // called by their bare names, as after a using-directive (Round 29; StringId.hpp's literals).
                if (inline_ns && pushed > 0) using_namespaces.push_back(current_ns);
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
            pending_tls = false;
            while (is(TTypedef) || is(TStatic) || is(TConst)) {
                if (accept(TTypedef)) is_typedef = true;
                else if (accept(TStatic)) is_static = true;
                else if (accept(TConst)) is_const = true;
            }
            const bool decl_tls = take_tls();
            bool imported = false;
            bool exported = false;
            Type type;
            if (!parse_type(&type, &imported, &exported)) return false;
            while (!is_typedef && is(TStar) && at + 2 < tokens.size() && tokens[at + 1].kind == TIdent &&
                   tokens[at + 2].kind == TColonColon) {
                ++type.pointers;
                next();
            }
            if (!is_typedef && (is(TAmp) || is(TAndAnd)) && at + 2 < tokens.size() && tokens[at + 1].kind == TIdent &&
                tokens[at + 2].kind == TColonColon && class_name_at(at + 1)) {
                // R& C::f() { ... }: a member function returning a reference, defined outside its class (the result type
                // is the one declared in the class).
                type.is_ref = true;
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
                if (is(TAssign) && at + 2 < tokens.size() && tokens[at + 1].kind == TDefault &&
                    tokens[at + 2].kind == TSemi) {
                    // C::~C() = default; and C::C() = default; outside the class ([dcl.fct.def.default] 5): the body the
                    // compiler would have made (bases and members), defined here (Round 29; ClockCheck.cpp with a
                    // unique_ptr to an incomplete class).
                    Method& dm = owner.methods[mi];
                    dm.has_body = true;
                    dm.out_of_class = true;
                    dm.implicit = true;
                    dm.implicit_copy = !is_dtor && dm.params.size() == 1 && is_copy_param(dm, sid);
                    dm.body_at = 1;
                    dm.param_names = decl.param_names;
                    if (dm.implicit_copy) dm.param_names = {"__o"};
                    next();
                    next();
                    next();
                    const size_t resume = at;
                    if (!define_method(sid, mi)) return false;
                    at = resume;
                    continue;
                }
                owner.methods[mi].has_body = true;
                owner.methods[mi].out_of_class = true;
                owner.methods[mi].body_at = at;
                owner.methods[mi].param_names = decl.param_names;
                skip_mem_initializers();
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
                int sid = find_struct(cur().text);
                next();
                next();
                // void Outer::Inner::f() { ... }: a member of a class nested in C ([class.nest] 3; Round 30,
                // RoomReverb::Ring::Resize in SpatialFx.cpp).
                while (is(TIdent) && at + 2 < tokens.size() && tokens[at + 1].kind == TColonColon &&
                       (tokens[at + 2].kind == TIdent || tokens[at + 2].kind == TTilde)) {
                    int nested = -1;
                    for (size_t k = 0; k < structs.size(); ++k)
                        if (structs[k].name == cur().text && structs[k].parent == sid && !structs[k].incomplete)
                            nested = static_cast<int>(k);
                    if (nested < 0) break;
                    sid = nested;
                    next();
                    next();
                }
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
                const TdefSaves class_names = bind_class_tdefs(sid);
                const bool params_ok = parse_param_list(&decl);
                unbind_class_tdefs(class_names);
                if (!params_ok) return false;
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
                      (typedefs.count(tokens[at + 2].text) > 0 || class_name_text(tokens[at + 2].text) ||
                       find_struct(tokens[at + 2].text) >= 0)));  // `Vec3 f(PlainStruct& s)` is a function too
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
            const bool decl_extern = extern_decl_;
            extern_decl_ = false;
            if (decl_extern && !decl.is_function && !is(TAssign) && !is(TLBrace)) {
                // extern T a, b; : names only. Not when the file already defines the variable.
                for (;;) {
                    bool defined_here = false;
                    for (const Global& earlier : globals)
                        if (earlier.name == decl.name) defined_here = true;
                    if (!defined_here) {
                        Global global;
                        global.name = decl.name;
                        global.type = decl.type;
                        global.type.is_const = is_const || decl.type.is_const;
                        global.external = true;
                        globals.push_back(global);
                    }
                    if (!accept(TComma)) break;
                    decl = Declarator{};
                    if (!parse_declarator(type, &decl)) return false;
                }
                if (!expect(TSemi, "';'")) return false;
                continue;
            }
            if (is_typedef) {
                decl.type.is_const = is_const;
                typedefs[decl.name] = decl.type;
                // typedef struct _X { ... } X, *PX, *LPX; ([dcl.typedef]): every declarator names the type read first.
                while (accept(TComma)) {
                    Declarator more;
                    if (!parse_declarator(type, &more)) return false;
                    more.type.is_const = is_const;
                    typedefs[more.name] = more.type;
                }
                if (!expect(TSemi, "';'")) return false;
                continue;
            }
            decl.type.is_const = is_const;
            // An inline variable ([dcl.inline]): own simplification as for inline functions, each file keeps its own
            // copy (an internal symbol), so two files that include it link.
            bool inline_var = false;
            if (!decl.is_function)
                for (size_t k = decl_begin; k < at && k < tokens.size(); ++k)
                    if (tokens[k].inl) inline_var = true;
            if (inline_var) is_static = true;
            if (!decl.is_function && (obj_needs_ctor(decl.type) || obj_needs_dtor(decl.type))) {
                if (decl_tls) {
                    // thread_local C t; for a class whose only constructor is the implicit one and whose default member
                    // initializers are constants (Jobs.cpp's ThreadCtx): constant initialization ([basic.start.static]
                    // 2), so each thread's copy starts from the image in the TLS section and nothing runs at thread start.
                    Node tls_init;
                    Global global;
                    std::vector<DynLeaf> leaves;
                    if (is(TSemi) && !decl.type.is_array() && dmi_constant_init(decl.type.struct_id, &tls_init)) {
                        global.name = decl.name;
                        global.type = decl.type;
                        global.is_static = is_static || anon_depth > 0;
                        global.tls = true;
                        global.bss = false;
                        if (!flatten_dyn(decl.type, tls_init, &global, &leaves)) return false;
                    }
                    if (global.name.empty() || !leaves.empty()) {
                        error = "a thread_local object with a constructor or destructor at namespace scope is not supported "
                                "yet (" + decl.name + ")";
                        return false;
                    }
                    global.dynamic = false;
                    globals.push_back(global);
                    next();  // ';'
                    continue;
                }
                Node ginit;
                bool has_ginit = false;
                Type gil_type;
                if (!decl.type.is_array() && !gctor_args && is(TLBrace) &&
                    !(tokens[at + 1].kind != TRBrace && has_init_list_ctor(decl.type.struct_id, &gil_type))) {
                    // C g{a, b}; ([dcl.init.list] 3.7): the constructor that takes the arguments runs at startup.
                    next();
                    while (!is(TRBrace) && !is(TEnd)) {
                        Node a;
                        if (!parse_expr(1, &a)) return false;
                        gargs.push_back(std::move(a));
                        if (!accept(TComma)) break;
                    }
                    if (!expect(TRBrace, "'}'")) return false;
                    gctor_args = true;
                }
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
                    global.tls = decl_tls;
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
                        for (size_t gi = globals.size(); gi-- > 0;)
                            if (globals[gi].name == global.name && globals[gi].external) {
                                // The definition of a variable declared extern above: it takes the declaration's place
                                // and has external linkage, also when it is const ([basic.link] 3.2).
                                globals.erase(globals.begin() + static_cast<std::ptrdiff_t>(gi));
                                global.is_static = is_static || anon_depth > 0;
                            }
                        if (decl_extern) global.is_static = false;
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
            if (!current_ns.empty() && fn.ret.deduce == 0) {
                // A function of a namespace that calls itself (Round 29): its name is known in its own body
                // ([basic.scope.pdecl] 1, the point of declaration is before the body). The declaration made here is
                // merged with the definition at the end of the file (finish_program).
                bool known = false;
                for (const FuncDecl& f : funcs)
                    if (f.name == fn.name) known = true;
                if (!known) {
                    FuncDecl early = fn;
                    early.defined = false;
                    funcs.push_back(std::move(early));
                }
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
