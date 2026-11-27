// okay::js parser: tokens -> syntax tree. Written from ECMA-262 chapters 13
// (expressions), 14 (statements and declarations), 15 (functions and classes) and
// 16 (scripts), as recursive descent with precedence climbing for the binary
// operators (13.6-13.13; ** is right-associative and its base can not be a bare
// unary expression; ?? does not mix with && and || without parentheses).
// Arrow functions are found by looking past the matching ')' for '=>'
// (the cover grammar of 15.3); destructuring assignment re-reads an array or
// object literal as a pattern (13.15.5). Automatic semicolon insertion follows
// 12.10. All code is strict mode code (as module and class code is, 11.2.2).
//
// The parser also records, per function, the names used inside nested functions,
// so the compiler knows which variables a closure captures.
#include "JsInternal.hpp"
#include <algorithm>
#include <cstring>

namespace okay::js {

namespace {

struct ParseFail {};

// Nesting levels the parser allows (statements, expressions and their operands); each costs a few
// C++ frames in the parser and the compiler, so this bounds their stack use.
constexpr int kMaxNesting = 600;

const char* PunctName(int p) {
    static const char* const names[] = {
        "", "{", "}", "(", ")", "[", "]", ".", "...", ";", ",", "<", ">", "<=", ">=", "==", "!=", "===", "!==",
        "+", "-", "*", "/", "%", "**", "++", "--", "<<", ">>", ">>>", "&", "|", "^", "!", "~", "&&", "||", "??",
        "?", "?.", ":", "=", "+=", "-=", "*=", "/=", "%=", "**=", "<<=", ">>=", ">>>=", "&=", "|=", "^=", "&&=",
        "||=", "?" "?=", "=>",
    };
    return p >= 0 && p < (int)(sizeof(names) / sizeof(names[0])) ? names[p] : "?";
}

bool IsReserved(const std::u16string& s) {
    for (int i = 0; i < kReservedCount; ++i) {
        const char* w = kKeywords[i];
        std::size_t n = std::strlen(w);
        if (s.size() != n) continue;
        std::size_t k = 0;
        while (k < n && s[k] == (char16_t)w[k]) ++k;
        if (k == n) return true;
    }
    return false;
}

bool Eq(const std::u16string& s, const char* w) {
    std::size_t n = std::strlen(w);
    if (s.size() != n) return false;
    for (std::size_t k = 0; k < n; ++k)
        if (s[k] != (char16_t)w[k]) return false;
    return true;
}

int BinaryPrec(const Token& t, bool noIn) {
    if (t.t == TT::Name && !t.escaped) {
        if (Eq(t.s, "instanceof")) return 8;
        if (Eq(t.s, "in") && !noIn) return 8;
        return -1;
    }
    if (t.t != TT::Punct) return -1;
    switch (t.p) {
        case P_Nullish: return 1;
        case P_Or: return 2;
        case P_And: return 3;
        case P_Bar: return 4;
        case P_Caret: return 5;
        case P_Amp: return 6;
        case P_Eq2: case P_Ne: case P_Eq3: case P_Ne2: return 7;
        case P_Lt: case P_Gt: case P_Le: case P_Ge: return 8;
        case P_Shl: case P_Sar: case P_Shr: return 9;
        case P_Plus: case P_Minus: return 10;
        case P_Star: case P_Slash: case P_Percent: return 11;
        case P_Star2: return 12;
        default: return -1;
    }
}

bool IsAssignOp(int p) {
    switch (p) {
        case P_Assign: case P_PlusEq: case P_MinusEq: case P_StarEq: case P_SlashEq: case P_PercentEq: case P_Star2Eq:
        case P_ShlEq: case P_SarEq: case P_ShrEq: case P_AmpEq: case P_BarEq: case P_CaretEq: case P_AndEq:
        case P_OrEq: case P_NullishEq: return true;
        default: return false;
    }
}

struct Parser {
    const std::vector<Token>& T;
    std::size_t pos = 0;
    Ast& ast;
    SyntaxError& err;
    std::vector<FuncInfo*> fstack;
    std::vector<std::unordered_set<std::u16string>> refs;   // per open function: every name used in it or nested
    std::vector<Node*> cover;                               // {a = 1} literals not yet read as patterns
    bool noIn = false;
    int depth = 0;
    int loopDepth = 0, switchDepth = 0;
    std::vector<std::u16string> labels;

    Parser(const std::vector<Token>& t, Ast& a, SyntaxError& e) : T(t), ast(a), err(e) {}

    // ---- tokens
    const Token& Cur() const { return T[pos]; }
    const Token& At(std::size_t k) const { return T[std::min(k, T.size() - 1)]; }
    const Token& Peek(std::size_t k = 1) const { return At(pos + k); }
    bool IsP(int p) const { return Cur().t == TT::Punct && Cur().p == p; }
    bool IsPAt(std::size_t k, int p) const { return At(k).t == TT::Punct && At(k).p == p; }
    bool IsWord(const char* w) const { return Cur().t == TT::Name && !Cur().escaped && Eq(Cur().s, w); }
    bool IsWordAt(std::size_t k, const char* w) const { return At(k).t == TT::Name && !At(k).escaped && Eq(At(k).s, w); }
    void Next() { if (pos + 1 < T.size()) ++pos; }
    bool Eat(int p) {
        if (!IsP(p)) return false;
        Next();
        return true;
    }
    bool EatWord(const char* w) {
        if (!IsWord(w)) return false;
        Next();
        return true;
    }

    [[noreturn]] void FailAt(const Token& t, const std::string& m) {
        err.line = t.line;
        err.column = t.col;
        err.message = m;
        throw ParseFail{};
    }
    [[noreturn]] void Fail(const std::string& m) { FailAt(Cur(), m); }
    std::string Describe(const Token& t) const {
        switch (t.t) {
            case TT::End: return "the end of the script";
            case TT::Name: return "'" + Utf16To8(t.s) + "'";
            case TT::Num: case TT::BigInt: return "a number";
            case TT::Str: return "a string";
            case TT::Template: return "a template";
            case TT::PrivateName: return "a #private name";
            case TT::Regex: return "a regular expression";
            case TT::Punct: return std::string("'") + PunctName(t.p) + "'";
        }
        return "?";
    }
    [[noreturn]] void Unexpected(const char* wanted = nullptr) {
        std::string m = "unexpected " + Describe(Cur());
        if (wanted) m += std::string(", expected ") + wanted;
        Fail(m);
    }
    void Expect(int p) {
        if (!Eat(p)) Unexpected((std::string("'") + PunctName(p) + "'").c_str());
    }
    // 12.10 automatic semicolon insertion
    void Semicolon() {
        if (Eat(P_Semi)) return;
        if (IsP(P_RBrace) || Cur().t == TT::End || Cur().nl) return;
        Unexpected("';'");
    }

    struct Depth {
        Parser& p;
        explicit Depth(Parser& x) : p(x) {
            if (++p.depth > kMaxNesting) p.Fail("the code is nested too deeply");
        }
        ~Depth() { --p.depth; }
    };
    struct NoInGuard {
        Parser& p;
        bool saved;
        NoInGuard(Parser& x, bool v) : p(x), saved(x.noIn) { p.noIn = v; }
        ~NoInGuard() { p.noIn = saved; }
    };

    Node* N(NK k, const Token& t) { return ast.NewNode(k, t.line, t.col); }
    Node* N(NK k) { return N(k, Cur()); }

    FuncInfo* Fn() { return fstack.back(); }
    bool InGenerator() const { return !fstack.empty() && fstack.back()->generator; }
    bool InAsync() const { return !fstack.empty() && fstack.back()->async; }
    void Ref(const std::u16string& name) { refs.back().insert(name); }

    // ---- private names (15.7: each class body is a private environment; a #name must be declared by an enclosing class)
    struct PrivScope {
        std::unordered_map<std::u16string, int> declared;   ///< "#x" -> PK_* bits
        std::vector<std::pair<std::u16string, std::pair<int, int>>> refs;   ///< names used here, with line and column
    };
    std::vector<PrivScope> privScopes;
    enum { PK_Field = 1, PK_Method = 2, PK_Get = 4, PK_Set = 8, PK_Static = 16 };
    void PrivRef(const std::u16string& name, const Token& at) {
        if (privScopes.empty()) FailAt(at, "'" + Utf16To8(name) + "' is not declared in an enclosing class");
        privScopes.back().refs.push_back({name, {at.line, at.col}});
        Ref(name);
    }
    Node* PrivateMember(Node* obj, const Token& dot, bool optional) {
        const Token& pt = Cur();
        Node* m = ast.NewNode(NK::Member, dot.line, dot.col);
        m->a = obj;
        m->str = u"#" + pt.s;
        m->flags |= NF_Private;
        if (optional) m->flags |= NF_Optional;
        PrivRef(m->str, pt);
        Next();
        return m;
    }
    // 15.7.1 static semantics: a #name is declared once, except a getter and a setter pair of the same placement
    void DeclarePrivate(ClassInfo* ci, const std::u16string& name, int kind, const Token& at) {
        if (name == u"#constructor") FailAt(at, "a class member can not be named #constructor");
        auto& d = privScopes.back().declared;
        auto it = d.find(name);
        if (it == d.end()) {
            d[name] = kind;
            ci->privNames.push_back(name);
            return;
        }
        const int had = it->second;
        const bool pair = ((had & ~PK_Static) == PK_Get && (kind & ~PK_Static) == PK_Set) ||
                          ((had & ~PK_Static) == PK_Set && (kind & ~PK_Static) == PK_Get);
        if (!pair || (had & PK_Static) != (kind & PK_Static)) FailAt(at, "'" + Utf16To8(name) + "' is already declared in this class");
        it->second = had | kind;
    }
    void EndPrivScope() {
        PrivScope done = std::move(privScopes.back());
        privScopes.pop_back();
        for (auto& r : done.refs) {
            if (done.declared.count(r.first)) continue;
            if (!privScopes.empty()) {
                privScopes.back().refs.push_back(r);
                continue;
            }
            err.line = r.second.first;
            err.column = r.second.second;
            err.message = "'" + Utf16To8(r.first) + "' is not declared in an enclosing class";
            throw ParseFail{};
        }
    }

    void PushFunc(FuncInfo* fi) {
        fstack.push_back(fi);
        refs.emplace_back();
    }
    void PopFunc() {
        FuncInfo* child = fstack.back();
        std::unordered_set<std::u16string> all = std::move(refs.back());
        fstack.pop_back();
        refs.pop_back();
        if (fstack.empty()) return;
        FuncInfo* parent = fstack.back();
        for (const auto& n : all) {
            parent->innerRefs.insert(n);
            refs.back().insert(n);
        }
        if (child->arrow) {
            parent->usesThis |= child->usesThis;
            parent->usesSuper |= child->usesSuper;
            parent->usesNewTarget |= child->usesNewTarget;
            parent->usesArguments |= child->usesArguments;
        }
    }
    // `this`, `super`, `new.target` inside an arrow use the enclosing function's: a hidden captured name.
    void UseThis() {
        Fn()->usesThis = true;
        if (Fn()->arrow) Ref(u"%this");
    }
    void UseSuper() {
        Fn()->usesSuper = true;
        UseThis();
        if (Fn()->arrow) Ref(u"%home");
    }
    void UseNewTarget() {
        Fn()->usesNewTarget = true;
        if (Fn()->arrow) Ref(u"%newtarget");
    }

    // A binding name (12.7.1 / 13.1.1 static semantics).
    std::u16string BindingName() {
        if (Cur().t != TT::Name) Unexpected("a name");
        const Token& t = Cur();
        if (!t.escaped && IsReserved(t.s)) Fail("'" + Utf16To8(t.s) + "' is a reserved word and can not be a name");
        if (Eq(t.s, "eval") || Eq(t.s, "arguments")) Fail("'" + Utf16To8(t.s) + "' can not be declared in strict code");
        if (Eq(t.s, "await") && InAsync()) Fail("'await' can not be a name inside an async function");
        std::u16string s = t.s;
        Next();
        return s;
    }

    // ================================================================ scripts and statements
    bool isModule = false;

    // 16.2.1 Module: the top level is an async function (top-level await), strict, with import and export declarations
    void ParseModule() {
        isModule = true;
        ast.module = std::make_unique<ModuleInfo>();
        FuncInfo* fi = ast.NewFunc();
        fi->name = u"module";
        fi->line = 1;
        fi->async = true;
        fi->module = true;
        ast.script = fi;
        PushFunc(fi);
        Node* body = N(NK::Block);
        while (Cur().t != TT::End) body->list.push_back(ModuleItem());
        fi->body = body;
        PopFunc();
        // 16.2.1.6.1 step 10: an export of an imported binding is an indirect export of what it imports
        ModuleInfo& mi = *ast.module;
        for (ModuleInfo::Export& e : mi.exports) {
            if (e.localName.empty()) continue;
            for (const ModuleInfo::Import& im : mi.imports)
                if (im.localName == e.localName && im.importName != u"*") {
                    e.req = im.req;
                    e.importName = im.importName;
                    e.localName.clear();
                    break;
                }
        }
        // early errors: each export name once (16.2.1.1)
        std::unordered_set<std::u16string> names;
        for (const ModuleInfo::Export& e : mi.exports) {
            if (e.exportName.empty()) continue;
            if (!names.insert(e.exportName).second) {
                err.line = e.line;
                err.column = e.col;
                err.message = "'" + Utf16To8(e.exportName) + "' is exported twice";
                throw ParseFail{};
            }
        }
    }

    int Request(const std::u16string& spec) {
        auto& r = ast.module->requests;
        for (std::size_t i = 0; i < r.size(); ++i)
            if (r[i] == spec) return (int)i;
        r.push_back(spec);
        return (int)r.size() - 1;
    }
    // FromClause (16.2.2): from 'specifier' [with { ... }]
    int FromClause() {
        if (!EatWord("from")) Unexpected("'from'");
        if (Cur().t != TT::Str) Unexpected("a module specifier string");
        int r = Request(Cur().s);
        Next();
        SkipAttributes();
        return r;
    }
    void SkipAttributes() {   // 16.2.2 WithClause: import attributes are read and not used
        if ((IsWord("with") || (IsWord("assert") && !Cur().nl)) && IsPAt(pos + 1, P_LBrace)) {
            Next();
            const Token& open = Cur();
            if (open.match < 0) Unexpected();
            pos = (std::size_t)open.match;
            Next();
        }
    }
    // ModuleExportName: an IdentifierName (reserved words too) or a string
    std::u16string ExportName(bool* isString = nullptr) {
        const Token& t = Cur();
        if (t.t != TT::Name && t.t != TT::Str) Unexpected("a name");
        if (isString) *isString = t.t == TT::Str;
        std::u16string s = t.s;
        Next();
        return s;
    }
    void AddExport(const std::u16string& exportName, const std::u16string& localName, const Token& at) {
        ModuleInfo::Export e;
        e.exportName = exportName;
        e.localName = localName;
        e.line = at.line;
        e.col = at.col;
        ast.module->exports.push_back(e);
    }
    static void BoundNames(Node* p, std::vector<std::u16string>& out) {
        if (!p) return;
        switch (p->k) {
            case NK::Ident: out.push_back(p->str); break;
            case NK::AssignPat: case NK::RestElem: BoundNames(p->a, out); break;
            case NK::ArrayPat: for (Node* e : p->list) BoundNames(e, out); break;
            case NK::ObjectPat: for (Node* e : p->list) BoundNames(e->k == NK::PatProp ? e->b : e, out); break;
            default: break;
        }
    }

    Node* ModuleItem() {
        if (IsWord("import") && !IsPAt(pos + 1, P_LParen) && !IsPAt(pos + 1, P_Dot)) return ImportDecl();
        if (IsWord("export")) return ExportDecl();
        return StatementListItem();
    }

    // 16.2.2 ImportDeclaration
    Node* ImportDecl() {
        const Token& t = Cur();
        Next();
        Node* n = N(NK::Empty, t);
        if (Cur().t == TT::Str) {   // import 'specifier';
            Request(Cur().s);
            Next();
            SkipAttributes();
            Semicolon();
            return n;
        }
        std::vector<ModuleInfo::Import> list;
        auto add = [&](const std::u16string& importName, const Token& at) {
            const std::u16string local = BindingName();
            list.push_back(ModuleInfo::Import{-1, importName, local, at.line, at.col});
        };
        bool more = true;
        if (Cur().t == TT::Name && !IsP(P_LBrace)) {   // the default binding
            add(u"default", Cur());
            more = Eat(P_Comma);
        }
        if (more) {
            if (Eat(P_Star)) {
                if (!EatWord("as")) Unexpected("'as'");
                add(u"*", Cur());
            } else if (Eat(P_LBrace)) {
                while (!IsP(P_RBrace)) {
                    const Token& nt = Cur();
                    bool str = false;
                    if (nt.t == TT::Name && !IsWordAt(pos + 1, "as")) {   // import { x }: the name is the binding
                        add(nt.s, nt);
                    } else {
                        std::u16string name = ExportName(&str);
                        if (!EatWord("as")) Unexpected("'as'");
                        add(name, nt);
                    }
                    if (!Eat(P_Comma)) break;
                }
                Expect(P_RBrace);
            } else Unexpected("'{' or '*'");
        }
        const int req = FromClause();
        for (ModuleInfo::Import& im : list) {
            im.req = req;
            ast.module->imports.push_back(im);
        }
        Semicolon();
        return n;
    }

    // 16.2.3 ExportDeclaration
    Node* ExportDecl() {
        const Token& t = Cur();
        Next();
        ModuleInfo& mi = *ast.module;
        if (Eat(P_Star)) {   // export * from 'm' / export * as name from 'm'
            std::u16string as;
            const bool named = EatWord("as");
            if (named) as = ExportName();
            const int req = FromClause();
            ModuleInfo::Export e;
            e.req = req;
            e.line = t.line;
            e.col = t.col;
            if (named) {
                e.exportName = as;
                e.importName = u"*";
            }
            mi.exports.push_back(e);
            Semicolon();
            return N(NK::Empty, t);
        }
        if (Eat(P_LBrace)) {   // export { a, b as c } [from 'm']
            struct Spec { std::u16string local, exported; bool str; int line, col; };
            std::vector<Spec> specs;
            while (!IsP(P_RBrace)) {
                const Token& nt = Cur();
                bool str = false;
                std::u16string local = ExportName(&str);
                std::u16string exported = local;
                if (EatWord("as")) exported = ExportName();
                specs.push_back({local, exported, str || (nt.t == TT::Name && IsReserved(nt.s)), nt.line, nt.col});
                if (!Eat(P_Comma)) break;
            }
            Expect(P_RBrace);
            if (IsWord("from")) {
                const int req = FromClause();
                for (const Spec& sp : specs) {
                    ModuleInfo::Export e;
                    e.exportName = sp.exported;
                    e.importName = sp.local;
                    e.req = req;
                    e.line = sp.line;
                    e.col = sp.col;
                    mi.exports.push_back(e);
                }
            } else {
                for (const Spec& sp : specs) {
                    if (sp.str) {
                        err.line = sp.line;
                        err.column = sp.col;
                        err.message = "'" + Utf16To8(sp.local) + "' is not a local name that can be exported (use export { ... } from)";
                        throw ParseFail{};
                    }
                    ModuleInfo::Export e;
                    e.exportName = sp.exported;
                    e.localName = sp.local;
                    e.line = sp.line;
                    e.col = sp.col;
                    mi.exports.push_back(e);
                    Ref(sp.local);
                }
            }
            Semicolon();
            return N(NK::Empty, t);
        }
        if (EatWord("default")) {
            Node* n = ExportDefault(t);
            // 16.2.3.7: a named function or class is its own binding; anything else is the hidden "*default*" one,
            // and an anonymous function or class is named "default" (NamedEvaluation)
            const std::u16string local = n->b ? (n->b->k == NK::FuncDecl ? n->a->fn->name : n->a->str) : u"*default*";
            if (!n->b) {
                if (n->a->k == NK::Func) NameAnon(n->a, u"default");
                else if (n->a->k == NK::Class) NameAnonClass(n->a, u"default");
            }
            AddExport(u"default", local, t);
            return n;
        }
        if (IsWord("var") || IsWord("let") || IsWord("const") || IsWord("function") || IsWord("class") ||
            (IsWord("async") && IsWordAt(pos + 1, "function"))) {
            Node* d = StatementListItem();
            std::vector<std::u16string> names;
            if (d->k == NK::VarDecl)
                for (Node* dc : d->list) BoundNames(dc->a, names);
            else if (d->k == NK::FuncDecl) names.push_back(d->a->fn->name);
            else if (d->k == NK::ClassDecl) names.push_back(d->a->str);
            for (const auto& nm : names) AddExport(nm, nm, t);
            return d;
        }
        Unexpected("a declaration, '{', '*' or 'default' after 'export'");
    }
    static void NameAnonClass(Node* c, const std::u16string& name) {
        if (c->k == NK::Class && c->str.empty() && c->fn && c->fn->name.empty()) c->fn->name = name;
    }

    void ParseScript() {
        FuncInfo* fi = ast.NewFunc();
        fi->name = u"script";
        fi->line = 1;
        ast.script = fi;
        PushFunc(fi);
        Node* body = N(NK::Block);
        while (Cur().t != TT::End) body->list.push_back(TopLevelItem());
        fi->body = body;
        PopFunc();
    }

    // `export default` (after the word default): a function or class declaration (named ones are also declared:
    // n->b) or an expression. Scripts and modules share it.
    Node* ExportDefault(const Token& t) {
        Node* n = N(NK::ExportDefault, t);
        if (IsWord("function") || (IsWord("async") && IsWordAt(pos + 1, "function") && !Peek().nl)) {
            bool isAsync = EatWord("async");
            n->a = FunctionExpr(isAsync, true);
            if (!n->a->fn->name.empty()) {   // export default function name() {}: also a declaration
                Node* d = N(NK::FuncDecl, t);
                d->a = n->a;
                n->b = d;
            }
            return n;
        }
        if (IsWord("class")) {
            n->a = ClassExpr(true);
            if (!n->a->str.empty()) n->b = n->a;   // a named class is also declared
            return n;
        }
        n->a = Assignment();
        Semicolon();
        CheckCover();
        return n;
    }

    Node* TopLevelItem() {
        if (IsWord("import") && !IsPAt(pos + 1, P_LParen) && !IsPAt(pos + 1, P_Dot))
            Fail("import declarations are not supported (game scripts are one file)");
        if (IsWord("export")) {
            const Token& t = Cur();
            Next();
            if (EatWord("default")) return ExportDefault(t);
            if (IsWord("var") || IsWord("let") || IsWord("const") || IsWord("function") || IsWord("class") ||
                (IsWord("async") && IsWordAt(pos + 1, "function")))
                return StatementListItem();
            Fail("only 'export default ...' and 'export <declaration>' are supported");
        }
        return StatementListItem();
    }

    void CheckCover() {
        if (!cover.empty()) {
            Node* c = cover.front();
            err.line = c->line;
            err.column = c->col;
            err.message = "'{ name = value }' is only allowed when destructuring";
            throw ParseFail{};
        }
    }

    Node* StatementListItem() {
        Depth dg(*this);
        if (IsWord("function")) return FunctionDecl(false);
        if (IsWord("async") && IsWordAt(pos + 1, "function") && !Peek().nl) {
            Next();
            return FunctionDecl(true);
        }
        if (IsWord("class")) {
            Node* c = ClassExpr(false);
            Node* d = N(NK::ClassDecl);
            d->line = c->line;
            d->col = c->col;
            d->a = c;
            return d;
        }
        if (IsWord("let") || IsWord("const")) {
            Node* n = VarDecl(IsWord("let") ? DK_Let : DK_Const, false);
            Semicolon();
            CheckCover();
            return n;
        }
        return Statement();
    }

    Node* FunctionDecl(bool isAsync) {
        const Token& t = Cur();
        Node* f = FunctionExpr(isAsync, false);
        if (f->fn->name.empty()) FailAt(t, "a function declaration needs a name");
        Node* d = N(NK::FuncDecl, t);
        d->a = f;
        return d;
    }

    Node* VarDecl(int kind, bool inForHead) {
        Node* n = N(NK::VarDecl);
        n->op = kind;
        Next();   // var / let / const
        do {
            Node* d = N(NK::Declarator);
            d->a = BindingTarget();
            if (Eat(P_Assign)) {
                d->b = Assignment();
                if (d->a->k == NK::Ident && (d->b->k == NK::Func || d->b->k == NK::Class)) NameAnon(d->b, d->a->str);
            }
            else if (!inForHead) {
                if (kind == DK_Const) Fail("a const needs a value (const x = ...)");
                if (d->a->k != NK::Ident) Fail("a destructuring declaration needs a value");
            }
            n->list.push_back(d);
        } while (Eat(P_Comma));
        return n;
    }

    // BindingIdentifier | BindingPattern (14.3.3)
    Node* BindingTarget() {
        Depth dg(*this);
        if (IsP(P_LBrack)) return ArrayBindingPattern();
        if (IsP(P_LBrace)) return ObjectBindingPattern();
        Node* n = N(NK::Ident);
        n->str = BindingName();
        return n;
    }
    Node* BindingElement() {
        Node* t = BindingTarget();
        if (IsP(P_Assign)) {
            Node* a = N(NK::AssignPat);
            Next();
            a->a = t;
            a->b = Assignment();
            if (t->k == NK::Ident && (a->b->k == NK::Func || a->b->k == NK::Class)) NameAnon(a->b, t->str);
            return a;
        }
        return t;
    }
    Node* ArrayBindingPattern() {
        Node* n = N(NK::ArrayPat);
        Expect(P_LBrack);
        NoInGuard ng(*this, false);
        while (!IsP(P_RBrack)) {
            if (IsP(P_Comma)) { n->list.push_back(N(NK::Hole)); Next(); continue; }
            if (IsP(P_Ellipsis)) {
                Node* r = N(NK::RestElem);
                Next();
                r->a = BindingTarget();
                n->list.push_back(r);
                if (!IsP(P_RBrack)) Fail("a ...rest element must be the last one");
                break;
            }
            n->list.push_back(BindingElement());
            if (!IsP(P_RBrack)) Expect(P_Comma);
        }
        Expect(P_RBrack);
        return n;
    }
    // A property name (13.2.5): name, string, number or [computed]. Sets key->str or key->a (computed).
    void PropertyName(Node* into) {
        const Token& t = Cur();
        if (t.t == TT::Name) { into->str = t.s; Next(); return; }
        if (t.t == TT::Str) { into->str = t.s; Next(); return; }
        if (t.t == TT::Num) { into->str = AsciiTo16(Runtime::NumberToStdString(t.n).c_str()); Next(); return; }
        if (IsP(P_LBrack)) {
            Next();
            NoInGuard ng(*this, false);
            into->a = Assignment();
            into->flags |= NF_Computed;
            Expect(P_RBrack);
            return;
        }
        Unexpected("a property name");
    }
    Node* ObjectBindingPattern() {
        Node* n = N(NK::ObjectPat);
        Expect(P_LBrace);
        NoInGuard ng(*this, false);
        while (!IsP(P_RBrace)) {
            if (IsP(P_Ellipsis)) {
                Node* r = N(NK::RestElem);
                Next();
                Node* id = N(NK::Ident);
                id->str = BindingName();
                r->a = id;
                n->list.push_back(r);
                if (!IsP(P_RBrace)) Fail("a ...rest property must be the last one");
                break;
            }
            Node* p = N(NK::PatProp);
            const Token& kt = Cur();
            bool simpleName = kt.t == TT::Name;
            PropertyName(p);
            if (Eat(P_Colon)) {
                p->b = BindingElement();
            } else {
                if (!simpleName) FailAt(kt, "expected ':' after a property name in a pattern");
                if (!kt.escaped && IsReserved(kt.s)) FailAt(kt, "'" + Utf16To8(kt.s) + "' is a reserved word and can not be a name");
                Node* id = ast.NewNode(NK::Ident, kt.line, kt.col);
                id->str = kt.s;
                p->flags |= NF_Shorthand;
                if (IsP(P_Assign)) {
                    Node* a = N(NK::AssignPat);
                    Next();
                    a->a = id;
                    a->b = Assignment();
                    p->b = a;
                } else p->b = id;
            }
            n->list.push_back(p);
            if (!IsP(P_RBrace)) Expect(P_Comma);
        }
        Expect(P_RBrace);
        return n;
    }

    Node* Statement() {
        Depth dg(*this);
        const Token& t = Cur();
        if (t.t == TT::Punct) {
            if (t.p == P_LBrace) return Block();
            if (t.p == P_Semi) { Next(); return N(NK::Empty, t); }
        }
        if (t.t == TT::Name && !t.escaped) {
            if (Eq(t.s, "var")) {
                Node* n = VarDecl(DK_Var, false);
                Semicolon();
                CheckCover();
                return n;
            }
            if (Eq(t.s, "if")) return IfStmt();
            if (Eq(t.s, "for")) return ForStmt();
            if (Eq(t.s, "while")) {
                Node* n = N(NK::While);
                Next();
                Expect(P_LParen);
                n->a = Expression();
                Expect(P_RParen);
                CheckCover();
                n->b = LoopBody();
                return n;
            }
            if (Eq(t.s, "do")) {
                Node* n = N(NK::DoWhile);
                Next();
                n->b = LoopBody();
                if (!EatWord("while")) Unexpected("'while'");
                Expect(P_LParen);
                n->a = Expression();
                Expect(P_RParen);
                CheckCover();
                Eat(P_Semi);   // 12.10.1: a ';' after do-while may always be left out
                return n;
            }
            if (Eq(t.s, "continue") || Eq(t.s, "break")) {
                bool isBreak = Eq(t.s, "break");
                Node* n = N(isBreak ? NK::Break : NK::Continue);
                Next();
                if (Cur().t == TT::Name && !Cur().nl && !IsReserved(Cur().s)) {
                    n->str = Cur().s;
                    if (std::find(labels.begin(), labels.end(), n->str) == labels.end())
                        Fail("there is no label '" + Utf16To8(n->str) + "' around this statement");
                    Next();
                } else if (isBreak ? (loopDepth == 0 && switchDepth == 0) : loopDepth == 0)
                    FailAt(t, isBreak ? "'break' is only allowed inside a loop or switch" : "'continue' is only allowed inside a loop");
                Semicolon();
                return n;
            }
            if (Eq(t.s, "return")) {
                if (fstack.size() <= 1) Fail("'return' is only allowed inside a function");
                Node* n = N(NK::Return);
                Next();
                if (!IsP(P_Semi) && !IsP(P_RBrace) && Cur().t != TT::End && !Cur().nl) n->a = Expression();
                Semicolon();
                CheckCover();
                return n;
            }
            if (Eq(t.s, "throw")) {
                Node* n = N(NK::Throw);
                Next();
                if (Cur().nl) Fail("a line break is not allowed between 'throw' and its value");
                n->a = Expression();
                Semicolon();
                CheckCover();
                return n;
            }
            if (Eq(t.s, "try")) return TryStmt();
            if (Eq(t.s, "switch")) return SwitchStmt();
            if (Eq(t.s, "debugger")) { Next(); Semicolon(); return N(NK::Debugger, t); }
            if (Eq(t.s, "with")) Fail("'with' is not allowed in strict code");
            if (Eq(t.s, "function")) Fail("a function declaration is not allowed here (wrap it in { })");
            if (Eq(t.s, "class") || Eq(t.s, "let") || Eq(t.s, "const")) Fail("a declaration is not allowed here (wrap it in { })");
            if (Eq(t.s, "import") && !IsPAt(pos + 1, P_LParen)) Fail("import is not supported");
            // a label
            if (IsPAt(pos + 1, P_Colon) && !IsReserved(t.s)) {
                Node* n = N(NK::Labeled);
                n->str = t.s;
                if (std::find(labels.begin(), labels.end(), n->str) != labels.end())
                    Fail("the label '" + Utf16To8(n->str) + "' is already used around here");
                Next();
                Next();
                labels.push_back(n->str);
                if (IsWord("function")) Fail("a labelled function is not allowed in strict code");
                n->a = Statement();
                labels.pop_back();
                return n;
            }
        }
        Node* n = N(NK::ExprStmt);
        n->a = Expression();
        Semicolon();
        CheckCover();
        return n;
    }

    Node* LoopBody() {
        ++loopDepth;
        Node* b = Statement();
        --loopDepth;
        return b;
    }

    Node* Block() {
        Node* n = N(NK::Block);
        Expect(P_LBrace);
        while (!IsP(P_RBrace)) {
            if (Cur().t == TT::End) Fail("this '{' is never closed with '}'");
            n->list.push_back(StatementListItem());
        }
        Next();
        return n;
    }

    Node* IfStmt() {
        Node* n = N(NK::If);
        Next();
        Expect(P_LParen);
        n->a = Expression();
        Expect(P_RParen);
        CheckCover();
        n->b = Statement();
        if (EatWord("else")) n->c = Statement();
        return n;
    }

    // 14.7.4 / 14.7.5: for (;;), for-in, for-of
    Node* ForStmt() {
        const Token& t = Cur();
        Next();
        bool isAwait = false;
        if (IsWord("await")) {   // 14.7.5: for await (... of ...) in async functions
            if (!InAsync()) Fail("'for await' is only allowed inside an async function");
            isAwait = true;
            Next();
        }
        Expect(P_LParen);
        Node* init = nullptr;
        if (IsP(P_Semi)) {
        } else if (IsWord("var") || IsWord("let") || IsWord("const")) {
            int kind = IsWord("var") ? DK_Var : IsWord("let") ? DK_Let : DK_Const;
            NoInGuard ng(*this, true);
            init = VarDecl(kind, true);
            if (init->list.size() == 1 && !init->list[0]->b && (IsWord("of") || IsWord("in"))) {
                bool of = IsWord("of");
                if (isAwait && !of) Fail("'for await' needs 'of'");
                Next();
                return ForInOfRest(t, of, init, isAwait);
            }
            for (Node* d : init->list)
                if (!d->b && (kind == DK_Const || d->a->k != NK::Ident)) Fail("this declaration needs a value");
        } else {
            NoInGuard ng(*this, true);
            Node* e = Expression();
            if (IsWord("of") || IsWord("in")) {
                bool of = IsWord("of");
                if (isAwait && !of) Fail("'for await' needs 'of'");
                Next();
                return ForInOfRest(t, of, ToTarget(e, true), isAwait);
            }
            init = N(NK::ExprStmt);
            init->a = e;
        }
        CheckCover();
        if (isAwait) Fail("'for await' needs 'of'");
        Node* n = ast.NewNode(NK::For, t.line, t.col);
        n->a = init;
        Expect(P_Semi);
        if (!IsP(P_Semi)) n->b = Expression();
        Expect(P_Semi);
        if (!IsP(P_RParen)) n->c = Expression();
        Expect(P_RParen);
        CheckCover();
        n->d = LoopBody();
        return n;
    }
    Node* ForInOfRest(const Token& t, bool of, Node* head, bool isAwait = false) {
        // The head's [~In] ends at `in` / `of`: the right side is Expression[+In] (14.7.5) and the body is a
        // Statement, so the `in` operator is allowed again (the caller's guard is still in scope).
        NoInGuard ng(*this, false);
        Node* n = ast.NewNode(of ? NK::ForOf : NK::ForIn, t.line, t.col);
        if (isAwait) n->flags |= NF_Await;
        n->a = head;
        n->b = of ? Assignment() : Expression();
        Expect(P_RParen);
        CheckCover();
        n->d = LoopBody();
        return n;
    }

    Node* TryStmt() {
        Node* n = N(NK::Try);
        Next();
        n->a = Block();
        if (EatWord("catch")) {
            if (Eat(P_LParen)) {
                n->b = BindingTarget();
                Expect(P_RParen);
            }
            n->c = Block();
            n->flags |= NF_Computed;   // has a catch clause
        }
        if (EatWord("finally")) n->d = Block();
        if (!(n->flags & NF_Computed) && !n->d) Fail("'try' needs a 'catch' or a 'finally'");
        return n;
    }

    Node* SwitchStmt() {
        Node* n = N(NK::Switch);
        Next();
        Expect(P_LParen);
        n->a = Expression();
        Expect(P_RParen);
        CheckCover();
        Expect(P_LBrace);
        bool sawDefault = false;
        ++switchDepth;
        while (!IsP(P_RBrace)) {
            Node* c = N(NK::Case);
            if (EatWord("case")) {
                c->a = Expression();
                CheckCover();
            } else if (EatWord("default")) {
                if (sawDefault) Fail("a switch can have only one 'default'");
                sawDefault = true;
            } else Unexpected("'case' or 'default'");
            Expect(P_Colon);
            while (!IsP(P_RBrace) && !IsWord("case") && !IsWord("default")) {
                if (Cur().t == TT::End) Fail("this switch is never closed with '}'");
                c->list.push_back(StatementListItem());
            }
            n->list.push_back(c);
        }
        --switchDepth;
        Next();
        return n;
    }

    // ================================================================ expressions
    Node* Expression() {
        Node* e = Assignment();
        if (!IsP(P_Comma)) return e;
        Node* s = ast.NewNode(NK::Seq, e->line, e->col);
        s->list.push_back(e);
        while (Eat(P_Comma)) s->list.push_back(Assignment());
        return s;
    }

    bool ArrowAhead() const {
        const Token& t = Cur();
        if (t.t == TT::Name && IsPAt(pos + 1, P_Arrow)) return true;
        if (t.t == TT::Punct && t.p == P_LParen && t.match > 0 && IsPAt((std::size_t)t.match + 1, P_Arrow)) return true;
        return false;
    }
    bool AsyncArrowAhead() const {
        if (!IsWord("async") || Peek().nl) return false;
        const Token& n = Peek();
        if (n.t == TT::Name && IsPAt(pos + 2, P_Arrow)) return true;
        if (n.t == TT::Punct && n.p == P_LParen && n.match > 0 && IsPAt((std::size_t)n.match + 1, P_Arrow)) return true;
        return false;
    }

    Node* Assignment() {
        Depth dg(*this);
        if (InGenerator() && IsWord("yield")) return YieldExpr();
        if (AsyncArrowAhead()) {
            Next();
            return ArrowFunction(true);
        }
        if (ArrowAhead()) return ArrowFunction(false);
        const Token& start = Cur();
        Node* lhs = Conditional();
        if (Cur().t == TT::Punct && IsAssignOp(Cur().p)) {
            int op = Cur().p;
            Node* n = N(NK::Assign);
            n->line = start.line;
            n->col = start.col;
            n->op = op;
            if (op == P_Assign) n->a = ToTarget(lhs, false);
            else {
                if (lhs->k != NK::Ident && lhs->k != NK::Member && lhs->k != NK::Index)
                    FailAt(start, "the left side of this assignment can not be assigned to");
                n->a = lhs;
                CheckSimpleName(lhs, start);
            }
            Next();
            n->b = Assignment();
            if (op == P_Assign && (n->b->k == NK::Func || n->b->k == NK::Class) && n->a->k == NK::Ident) NameAnon(n->b, n->a->str);
            return n;
        }
        return lhs;
    }
    void CheckSimpleName(Node* n, const Token& at) {
        if (n->k == NK::Ident && (Eq(n->str, "eval") || Eq(n->str, "arguments")))
            FailAt(at, "'" + Utf16To8(n->str) + "' can not be assigned to in strict code");
    }
    static void NameAnon(Node* f, const std::u16string& name) {
        // 13.15.2 / 8.4 NamedEvaluation: an anonymous function or class takes the name it is assigned to
        if (f->fn && f->fn->name.empty()) f->fn->name = name;
    }

    // Read an expression as an assignment target or pattern (13.15.5.1).
    Node* ToTarget(Node* e, bool forHead) {
        switch (e->k) {
            case NK::Ident:
                CheckSimpleName(e, Cur());
                return e;
            case NK::Member: case NK::Index:
                return e;
            case NK::Array: {
                if (e->flags & NF_Paren) break;
                Node* p = ast.NewNode(NK::ArrayPat, e->line, e->col);
                for (std::size_t i = 0; i < e->list.size(); ++i) {
                    Node* el = e->list[i];
                    if (el->k == NK::Hole) { p->list.push_back(el); continue; }
                    if (el->k == NK::Spread) {
                        if (i + 1 != e->list.size()) FailAt(*TokOf(el), "a ...rest element must be the last one");
                        Node* r = ast.NewNode(NK::RestElem, el->line, el->col);
                        r->a = ToTarget(el->a, forHead);
                        p->list.push_back(r);
                        continue;
                    }
                    p->list.push_back(ElementTarget(el, forHead));
                }
                if (e->flags & NF_Shorthand) FailAt(*TokOf(e), "a ...rest element can not have a trailing comma");
                return p;
            }
            case NK::Object: {
                if (e->flags & NF_Paren) break;
                Node* p = ast.NewNode(NK::ObjectPat, e->line, e->col);
                for (std::size_t i = 0; i < e->list.size(); ++i) {
                    Node* pr = e->list[i];
                    if (pr->k == NK::PropSpread) {
                        if (i + 1 != e->list.size()) FailAt(*TokOf(pr), "a ...rest property must be the last one");
                        if (pr->a->k != NK::Ident && pr->a->k != NK::Member && pr->a->k != NK::Index)
                            FailAt(*TokOf(pr), "a ...rest property must be a name");
                        Node* r = ast.NewNode(NK::RestElem, pr->line, pr->col);
                        r->a = pr->a;
                        p->list.push_back(r);
                        continue;
                    }
                    if (pr->k != NK::PropInit || (pr->flags & NF_Method)) FailAt(*TokOf(pr), "methods can not be destructuring targets");
                    Node* pp = ast.NewNode(NK::PatProp, pr->line, pr->col);
                    pp->str = pr->str;
                    pp->a = pr->a;
                    pp->flags = pr->flags & (NF_Computed | NF_Shorthand);
                    if (pr->flags & NF_CoverInit) {
                        auto it = std::find(cover.begin(), cover.end(), pr);
                        if (it != cover.end()) cover.erase(it);
                        Node* ap = ast.NewNode(NK::AssignPat, pr->line, pr->col);
                        ap->a = pr->b->a;
                        ap->b = pr->b->b;
                        pp->b = ap;
                    } else pp->b = ElementTarget(pr->b, forHead);
                    p->list.push_back(pp);
                }
                return p;
            }
            default: break;
        }
        FailAt(*TokOf(e), "the left side of this assignment can not be assigned to");
    }
    Node* ElementTarget(Node* el, bool forHead) {
        if (el->k == NK::Assign && el->op == P_Assign && !(el->flags & NF_Paren)) {
            Node* ap = ast.NewNode(NK::AssignPat, el->line, el->col);
            ap->a = el->a;   // already a target
            ap->b = el->b;
            return ap;
        }
        return ToTarget(el, forHead);
    }
    // a position-only token for an error at a node
    std::unique_ptr<Token> tokBuf;
    const Token* TokOf(Node* n) {
        tokBuf = std::make_unique<Token>();
        tokBuf->line = n->line;
        tokBuf->col = n->col;
        return tokBuf.get();
    }

    Node* YieldExpr() {
        Node* n = N(NK::Yield);
        if (Fn()->fieldInit) Fail("'yield' is not allowed here");
        Next();
        if (Eat(P_Star)) {
            n->flags |= NF_Delegate;
            n->a = Assignment();
            return n;
        }
        const Token& t = Cur();
        bool none = t.nl || t.t == TT::End ||
                    (t.t == TT::Punct && (t.p == P_RParen || t.p == P_RBrack || t.p == P_RBrace || t.p == P_Comma ||
                                          t.p == P_Semi || t.p == P_Colon)) ||
                    (t.t == TT::Name && Eq(t.s, "in"));
        if (!none) n->a = Assignment();
        return n;
    }

    Node* Conditional() {
        const Token& start = Cur();
        Node* c = Binary(0);
        if (!IsP(P_Quest)) return c;
        Next();
        Node* n = ast.NewNode(NK::Cond, start.line, start.col);
        n->a = c;
        {
            NoInGuard ng(*this, false);
            n->b = Assignment();
        }
        Expect(P_Colon);
        n->c = Assignment();
        return n;
    }

    Node* Binary(int minPrec) {
        Node* left;
        if (Cur().t == TT::PrivateName && !noIn && At(pos + 1).t == TT::Name && Eq(At(pos + 1).s, "in") && !At(pos + 1).escaped) {
            // 13.10 RelationalExpression: PrivateIdentifier in ShiftExpression (an ergonomic brand check)
            const Token& pt = Cur();
            left = N(NK::PrivName, pt);
            left->str = u"#" + pt.s;
            PrivRef(left->str, pt);
            Next();
        } else left = Unary();
        while (true) {
            const Token& t = Cur();
            int prec = BinaryPrec(t, noIn);
            if (prec < 0 || prec < minPrec) break;
            int op = t.t == TT::Punct ? t.p : (Eq(t.s, "in") ? 1000 : 1001);   // 1000 in, 1001 instanceof
            if (op == P_Star2 && left->k == NK::Unary && !(left->flags & NF_Paren))
                Fail("put the unary expression before ** in parentheses, e.g. (-x) ** 2");
            if (op == P_Star2 && left->k == NK::Await && !(left->flags & NF_Paren))
                Fail("put 'await x' before ** in parentheses");
            Next();
            Node* right = op == P_Star2 ? Binary(prec) : Binary(prec + 1);
            bool logical = op == P_And || op == P_Or || op == P_Nullish;
            Node* n = ast.NewNode(logical ? NK::Logical : NK::Binary, left->line, left->col);
            n->op = op;
            n->a = left;
            n->b = right;
            auto mixes = [&](Node* x, bool wantNullish) {
                return x->k == NK::Logical && !(x->flags & NF_Paren) && ((x->op == P_Nullish) == wantNullish);
            };
            if (op == P_Nullish && (mixes(left, false) || mixes(right, false)))
                FailAt(t, "?? can not be mixed with && or || without parentheses");
            if ((op == P_And || op == P_Or) && (mixes(left, true) || mixes(right, true)))
                FailAt(t, "?? can not be mixed with && or || without parentheses");
            left = n;
        }
        return left;
    }

    Node* Unary() {
        Depth dg(*this);
        const Token& t = Cur();
        if (t.t == TT::Punct) {
            switch (t.p) {
                case P_Bang: case P_Tilde: case P_Plus: case P_Minus: {
                    Node* n = N(NK::Unary);
                    n->op = t.p;
                    Next();
                    n->a = Unary();
                    return n;
                }
                case P_Inc: case P_Dec: {
                    Node* n = N(NK::Update);
                    n->op = t.p;
                    n->flags |= NF_Prefix;
                    Next();
                    const Token& at = Cur();
                    n->a = Unary();
                    if (n->a->k != NK::Ident && n->a->k != NK::Member && n->a->k != NK::Index)
                        FailAt(at, "++ and -- need a variable or a property");
                    CheckSimpleName(n->a, at);
                    return n;
                }
                default: break;
            }
        }
        if (t.t == TT::Name && !t.escaped) {
            if (Eq(t.s, "typeof") || Eq(t.s, "void") || Eq(t.s, "delete")) {
                Node* n = N(NK::Unary);
                n->op = Eq(t.s, "typeof") ? 2000 : Eq(t.s, "void") ? 2001 : 2002;
                Next();
                n->a = Unary();
                if (n->op == 2002 && n->a->k == NK::Ident) FailAt(t, "deleting a plain variable is not allowed in strict code");
                if (n->op == 2002) {   // 13.5.1.1: delete of a private reference is an early error
                    Node* x = n->a->k == NK::OptChain ? n->a->a : n->a;
                    if (x && x->k == NK::Member && (x->flags & NF_Private)) FailAt(t, "a #private member can not be deleted");
                }
                return n;
            }
            if (Eq(t.s, "await") && InAsync()) {
                if (Fn()->fieldInit) Fail("'await' is not allowed here");
                Node* n = N(NK::Await);
                Next();
                n->a = Unary();
                return n;
            }
        }
        return Postfix();
    }

    Node* Postfix() {
        const Token& start = Cur();
        Node* e = LeftHandSide();
        if (Cur().t == TT::Punct && (Cur().p == P_Inc || Cur().p == P_Dec) && !Cur().nl) {
            if (e->k != NK::Ident && e->k != NK::Member && e->k != NK::Index)
                FailAt(start, "++ and -- need a variable or a property");
            CheckSimpleName(e, start);
            Node* n = N(NK::Update);
            n->line = start.line;
            n->col = start.col;
            n->op = Cur().p;
            n->a = e;
            Next();
            return n;
        }
        return e;
    }

    std::vector<Node*> Arguments() {
        std::vector<Node*> args;
        Expect(P_LParen);
        NoInGuard ng(*this, false);
        while (!IsP(P_RParen)) {
            if (IsP(P_Ellipsis)) {
                Node* s = N(NK::Spread);
                Next();
                s->a = Assignment();
                args.push_back(s);
            } else args.push_back(Assignment());
            if (!IsP(P_RParen)) Expect(P_Comma);
        }
        Next();
        return args;
    }

    Node* LeftHandSide() {
        Node* e;
        bool chain = false;
        if (IsWord("new")) e = NewExpr();
        else e = Primary();
        // member, call and optional-chain suffixes (13.3)
        while (true) {
            const Token& t = Cur();
            if (t.t == TT::Punct && t.p == P_Dot) {
                Next();
                if (Cur().t == TT::PrivateName) {   // 13.3.1 MemberExpression . PrivateIdentifier
                    if (e->k == NK::Super) Fail("super.#name is not allowed");
                    e = PrivateMember(e, t, false);
                    continue;
                }
                if (Cur().t != TT::Name) Unexpected("a property name after '.'");
                Node* m = ast.NewNode(NK::Member, t.line, t.col);
                m->a = e;
                m->str = Cur().s;
                Next();
                e = m;
                continue;
            }
            if (t.t == TT::Punct && t.p == P_QDot) {
                chain = true;
                Next();
                if (e->k == NK::Super) FailAt(t, "super?. is not allowed");
                if (IsP(P_LParen)) {
                    Node* c = ast.NewNode(NK::Call, t.line, t.col);
                    c->a = e;
                    c->flags |= NF_Optional;
                    c->list = Arguments();
                    e = c;
                } else if (IsP(P_LBrack)) {
                    Next();
                    Node* m = ast.NewNode(NK::Index, t.line, t.col);
                    m->a = e;
                    m->flags |= NF_Optional;
                    NoInGuard ng(*this, false);
                    m->b = Expression();
                    Expect(P_RBrack);
                    e = m;
                } else if (Cur().t == TT::PrivateName) {
                    e = PrivateMember(e, t, true);
                } else {
                    if (Cur().t != TT::Name) Unexpected("a property name after '?.'");
                    Node* m = ast.NewNode(NK::Member, t.line, t.col);
                    m->a = e;
                    m->flags |= NF_Optional;
                    m->str = Cur().s;
                    Next();
                    e = m;
                }
                continue;
            }
            if (t.t == TT::Punct && t.p == P_LBrack) {
                Next();
                Node* m = ast.NewNode(NK::Index, t.line, t.col);
                m->a = e;
                NoInGuard ng(*this, false);
                m->b = Expression();
                Expect(P_RBrack);
                e = m;
                continue;
            }
            if (t.t == TT::Punct && t.p == P_LParen) {
                Node* c = ast.NewNode(NK::Call, t.line, t.col);
                c->a = e;
                c->list = Arguments();
                if (e->k == NK::Super) Fn()->hasSuperCall = true;
                e = c;
                continue;
            }
            if (t.t == TT::Template && (t.tpl == 0 || t.tpl == 1)) {
                if (chain) Fail("a tagged template can not follow an optional chain");
                e = TaggedTemplate(e, t);
                continue;
            }
            break;
        }
        if (e->k == NK::Super) Fail("'super' must be followed by '.', '[' or '(' ");
        if (chain) {
            Node* c = ast.NewNode(NK::OptChain, e->line, e->col);
            c->a = e;
            return c;
        }
        return e;
    }

    // new MemberExpression Arguments? (13.3.5), new.target (13.3.12)
    Node* NewExpr() {
        const Token& t = Cur();
        Next();
        if (IsP(P_Dot)) {
            Next();
            if (!IsWord("target")) Unexpected("'target' after 'new.'");
            if (fstack.size() <= 1 && !Fn()->arrow) FailAt(t, "new.target is only allowed inside a function");
            Next();
            UseNewTarget();
            return ast.NewNode(NK::NewTarget, t.line, t.col);
        }
        Node* callee;
        if (IsWord("new")) callee = NewExpr();
        else callee = Primary();
        while (true) {   // member suffixes only (a call ends the new callee)
            const Token& m = Cur();
            if (m.t == TT::Punct && m.p == P_Dot) {
                Next();
                if (Cur().t == TT::PrivateName) {
                    callee = PrivateMember(callee, m, false);
                    continue;
                }
                if (Cur().t != TT::Name) Unexpected("a property name after '.'");
                Node* x = ast.NewNode(NK::Member, m.line, m.col);
                x->a = callee;
                x->str = Cur().s;
                Next();
                callee = x;
                continue;
            }
            if (m.t == TT::Punct && m.p == P_LBrack) {
                Next();
                Node* x = ast.NewNode(NK::Index, m.line, m.col);
                x->a = callee;
                NoInGuard ng(*this, false);
                x->b = Expression();
                Expect(P_RBrack);
                callee = x;
                continue;
            }
            if (m.t == TT::Template && (m.tpl == 0 || m.tpl == 1)) {   // new tag`x` (MemberExpression TemplateLiteral)
                callee = TaggedTemplate(callee, m);
                continue;
            }
            if (m.t == TT::Punct && m.p == P_QDot) Fail("an optional chain is not allowed in a 'new' expression");
            break;
        }
        if (callee->k == NK::Super) FailAt(t, "'new super' is not allowed");
        Node* n = ast.NewNode(NK::New, t.line, t.col);
        n->a = callee;
        if (IsP(P_LParen)) n->list = Arguments();
        return n;
    }

    Node* Primary() {
        Depth dg(*this);
        const Token& t = Cur();
        switch (t.t) {
            case TT::Num: { Node* n = N(NK::Num); n->num = t.n; Next(); return n; }
            case TT::BigInt: { Node* n = N(NK::BigInt); n->str = t.s; Next(); return n; }
            case TT::Str: { Node* n = N(NK::Str); n->str = t.s; Next(); return n; }
            case TT::Template: return TemplateLit();
            case TT::Regex: {
                Node* n = N(NK::Regex);
                n->str = t.s;
                Node* f = N(NK::Str);
                f->str = t.flags;
                n->a = f;
                Next();
                return n;
            }
            case TT::End: Unexpected();
            case TT::PrivateName: Fail("'#" + Utf16To8(t.s) + "' can only be used as obj.#" + Utf16To8(t.s) + " or before 'in'");
            case TT::Punct:
                if (t.p == P_LParen) {
                    Next();
                    NoInGuard ng(*this, false);
                    if (IsP(P_RParen)) Unexpected("an expression");
                    Node* e = Expression();
                    Expect(P_RParen);
                    e->flags |= NF_Paren;
                    return e;
                }
                if (t.p == P_LBrack) return ArrayLit();
                if (t.p == P_LBrace) return ObjectLit();
                Unexpected();
            case TT::Name: break;
        }
        if (!t.escaped) {
            const std::u16string& s = t.s;
            if (Eq(s, "this")) { Next(); UseThis(); return N(NK::This, t); }
            if (Eq(s, "null")) { Next(); return N(NK::Null, t); }
            if (Eq(s, "true")) { Next(); return N(NK::True, t); }
            if (Eq(s, "false")) { Next(); return N(NK::False, t); }
            if (Eq(s, "function")) return FunctionExpr(false, true);
            if (Eq(s, "async") && IsWordAt(pos + 1, "function") && !Peek().nl) {
                Next();
                return FunctionExpr(true, true);
            }
            if (Eq(s, "class")) return ClassExpr(true);
            if (Eq(s, "super")) {
                FuncInfo* home = nullptr;
                for (auto it = fstack.rbegin(); it != fstack.rend(); ++it)
                    if (!(*it)->arrow) { home = *it; break; }
                bool call = IsPAt(pos + 1, P_LParen);
                if (!home || !(home->method || home->classCtor || home->fieldInit))
                    Fail("'super' is only allowed inside methods and class constructors");
                if (call && !home->derived) Fail("super() is only allowed in the constructor of a class that extends another");
                if (call && Fn()->arrow) Fail("super() inside an arrow function is not supported");
                Next();
                if (!call) UseSuper();
                else UseThis();
                return N(NK::Super, t);
            }
            if (Eq(s, "new")) return NewExpr();
            if (Eq(s, "import") && IsPAt(pos + 1, P_Dot)) {   // 13.3.12 import.meta, only in module code
                if (!isModule) Fail("import.meta is only allowed in a module");
                Next();
                Next();
                if (!IsWord("meta")) Unexpected("'meta' after 'import.'");
                Node* n = N(NK::Ident, t);
                n->str = u"*meta*";
                Next();
                ast.module->usesMeta = true;
                Ref(n->str);
                return n;
            }
            if (Eq(s, "import")) {   // 13.3.10 import(specifier [, options]): a promise for a module (see OP_ImportCall)
                if (!IsPAt(pos + 1, P_LParen)) Fail("import is not supported");
                Node* n = N(NK::ImportCall, t);
                Next();   // import
                Next();   // (
                NoInGuard ng(*this, false);
                n->a = Assignment();
                if (Eat(P_Comma) && !IsP(P_RParen)) {
                    n->b = Assignment();
                    Eat(P_Comma);
                }
                Expect(P_RParen);
                return n;
            }
            if (IsReserved(s) && !Eq(s, "let") && !Eq(s, "static") && !Eq(s, "yield"))
                Fail("unexpected reserved word '" + Utf16To8(s) + "'");
            if (Eq(s, "yield") || Eq(s, "let") || Eq(s, "static")) Fail("'" + Utf16To8(s) + "' is a reserved word in strict code");
            if (Eq(s, "await") && InAsync()) Fail("'await' needs a value");
        }
        Node* n = N(NK::Ident);
        n->str = t.s;
        Next();
        if (Eq(n->str, "arguments")) {
            FuncInfo* f = nullptr;
            for (auto it = fstack.rbegin(); it != fstack.rend(); ++it)
                if (!(*it)->arrow) { f = *it; break; }
            if (f && fstack.size() > 1 && f != ast.script) f->usesArguments = true;
            if (Fn()->fieldInit) Fail("'arguments' is not allowed in a class field");
        }
        Ref(n->str);
        return n;
    }

    // 13.3.11 tagged templates: tag`a${x}b` is the call tag(templateObject, x). The template object node
    // (NK::TaggedTemplate) carries the cooked strings (undefined for a piece with a bad escape) in `list` and
    // the raw strings in `b->list`; the compiler makes one frozen object per site (13.2.8.4 GetTemplateObject).
    Node* TaggedTemplate(Node* tag, const Token& at) {
        Node* call = ast.NewNode(NK::Call, at.line, at.col);
        call->a = tag;
        Node* obj = ast.NewNode(NK::TaggedTemplate, at.line, at.col);
        obj->b = ast.NewNode(NK::Array, at.line, at.col);
        call->list.push_back(obj);
        while (true) {
            const Token& t = Cur();
            if (t.t != TT::Template) Unexpected("the rest of the template");
            Node* cooked = N(t.badEscape ? NK::Hole : NK::Str);
            cooked->str = t.s;
            obj->list.push_back(cooked);
            Node* raw = N(NK::Str);
            raw->str = t.raw;
            obj->b->list.push_back(raw);
            std::uint8_t kind = t.tpl;
            Next();
            if (kind == 0 || kind == 3) break;
            NoInGuard ng(*this, false);
            call->list.push_back(Assignment());
            if (Cur().t != TT::Template || (Cur().tpl != 2 && Cur().tpl != 3)) Unexpected("'}' closing the ${ }");
        }
        return call;
    }

    Node* TemplateLit() {
        Node* n = N(NK::Template);
        while (true) {
            const Token& t = Cur();
            if (t.t != TT::Template) Unexpected("the rest of the template");
            if (t.badEscape) Fail("bad escape sequence in a template");
            Node* q = N(NK::Str);
            q->str = t.s;
            n->list.push_back(q);
            std::uint8_t kind = t.tpl;
            Next();
            if (kind == 0 || kind == 3) break;
            NoInGuard ng(*this, false);
            n->list.push_back(Expression());
            if (Cur().t != TT::Template || (Cur().tpl != 2 && Cur().tpl != 3)) Unexpected("'}' closing the ${ }");
        }
        return n;
    }

    Node* ArrayLit() {
        Node* n = N(NK::Array);
        Next();
        NoInGuard ng(*this, false);
        while (!IsP(P_RBrack)) {
            if (IsP(P_Comma)) { n->list.push_back(N(NK::Hole)); Next(); continue; }
            Node* el;
            if (IsP(P_Ellipsis)) {
                el = N(NK::Spread);
                Next();
                el->a = Assignment();
            } else el = Assignment();
            n->list.push_back(el);
            if (!IsP(P_RBrack)) {
                Expect(P_Comma);
                if (IsP(P_RBrack) && el->k == NK::Spread) n->flags |= NF_Shorthand;   // [...a,] (not valid as a pattern)
            }
        }
        Next();
        return n;
    }

    Node* ObjectLit() {
        Node* n = N(NK::Object);
        Next();
        NoInGuard ng(*this, false);
        while (!IsP(P_RBrace)) {
            const Token& t = Cur();
            if (IsP(P_Ellipsis)) {
                Node* s = N(NK::PropSpread);
                Next();
                s->a = Assignment();
                n->list.push_back(s);
                if (!IsP(P_RBrace)) Expect(P_Comma);
                continue;
            }
            bool isAsync = false, isGen = false;
            int accessor = 0;   // 1 get, 2 set
            auto followsKey = [&](std::size_t k) {   // is the token at k the start of a property key?
                const Token& x = At(k);
                return x.t == TT::Name || x.t == TT::Str || x.t == TT::Num || (x.t == TT::Punct && (x.p == P_LBrack || x.p == P_Star));
            };
            if ((IsWord("get") || IsWord("set")) && followsKey(pos + 1) && !IsPAt(pos + 1, P_Star)) {
                accessor = IsWord("get") ? 1 : 2;
                Next();
            } else if (IsWord("async") && followsKey(pos + 1) && !Peek().nl) {
                isAsync = true;
                Next();
            }
            if (!accessor && Eat(P_Star)) isGen = true;
            Node* p = N(accessor == 1 ? NK::PropGet : accessor == 2 ? NK::PropSet : NK::PropInit);
            p->line = t.line;
            p->col = t.col;
            const Token& kt = Cur();
            bool simpleName = kt.t == TT::Name;
            PropertyName(p);
            if (accessor || isAsync || isGen || IsP(P_LParen)) {
                Node* f = MethodFunction(p, isAsync, isGen, accessor, false, nullptr);
                p->b = f;
                p->flags |= NF_Method;
            } else if (Eat(P_Colon)) {
                p->b = Assignment();
                if (!(p->flags & NF_Computed) && (p->b->k == NK::Func || p->b->k == NK::Class)) NameAnon(p->b, p->str);
            } else {
                if (!simpleName) FailAt(kt, "expected ':' after a property name");
                if (!kt.escaped && IsReserved(kt.s)) FailAt(kt, "'" + Utf16To8(kt.s) + "' can not be used as a shorthand property");
                Node* id = ast.NewNode(NK::Ident, kt.line, kt.col);
                id->str = kt.s;
                Ref(id->str);
                p->flags |= NF_Shorthand;
                if (IsP(P_Assign)) {   // CoverInitializedName: only valid as a pattern
                    Node* a = N(NK::Assign);
                    Next();
                    a->op = P_Assign;
                    a->a = id;
                    a->b = Assignment();
                    p->b = a;
                    p->flags |= NF_CoverInit;
                    cover.push_back(p);
                } else p->b = id;
            }
            n->list.push_back(p);
            if (!IsP(P_RBrace)) Expect(P_Comma);
        }
        Next();
        return n;
    }

    // ================================================================ functions (15.1-15.8)
    void Params(FuncInfo* fi) {
        Expect(P_LParen);
        NoInGuard ng(*this, false);
        bool seenDefault = false;
        while (!IsP(P_RParen)) {
            if (IsP(P_Ellipsis)) {
                Node* r = N(NK::RestElem);
                Next();
                r->a = BindingTarget();
                fi->params.push_back(r);
                fi->simpleParams = false;
                if (!IsP(P_RParen)) Fail("a ...rest parameter must be the last one");
                break;
            }
            Node* p = BindingElement();
            if (p->k != NK::Ident) fi->simpleParams = false;
            if (p->k == NK::AssignPat) seenDefault = true;
            if (!seenDefault) ++fi->length;
            fi->params.push_back(p);
            if (!IsP(P_RParen)) Expect(P_Comma);
        }
        Next();
        // duplicate parameter names are an error in strict code (15.2.1)
        std::vector<std::u16string> names;
        for (Node* p : fi->params) CollectNames(p, names);
        for (std::size_t i = 0; i < names.size(); ++i)
            for (std::size_t j = i + 1; j < names.size(); ++j)
                if (names[i] == names[j]) Fail("the parameter '" + Utf16To8(names[i]) + "' is named twice");
    }
    static void CollectNames(Node* p, std::vector<std::u16string>& out) {
        if (!p) return;
        switch (p->k) {
            case NK::Ident: out.push_back(p->str); break;
            case NK::AssignPat: CollectNames(p->a, out); break;
            case NK::RestElem: CollectNames(p->a, out); break;
            case NK::ArrayPat: for (Node* e : p->list) CollectNames(e, out); break;
            case NK::ObjectPat: for (Node* e : p->list) CollectNames(e->k == NK::PatProp ? e->b : e, out); break;
            default: break;
        }
    }

    void FunctionBody(FuncInfo* fi) {
        NoInGuard ng(*this, false);   // a body is FunctionBody[~In]-free: `for (var f = function () { a in b }; ;)` is fine
        std::vector<Node*> savedCover;
        savedCover.swap(cover);
        int savedLoop = loopDepth, savedSwitch = switchDepth;
        std::vector<std::u16string> savedLabels;
        savedLabels.swap(labels);
        loopDepth = switchDepth = 0;
        Node* body = N(NK::Block);
        Expect(P_LBrace);
        while (!IsP(P_RBrace)) {
            if (Cur().t == TT::End) Fail("this function's '{' is never closed with '}'");
            body->list.push_back(StatementListItem());
        }
        Next();
        fi->body = body;
        loopDepth = savedLoop;
        switchDepth = savedSwitch;
        labels.swap(savedLabels);
        cover.swap(savedCover);
    }

    // function [*] [name] (params) { body }
    Node* FunctionExpr(bool isAsync, bool isExpr) {
        const Token& t = Cur();
        Next();   // function
        FuncInfo* fi = ast.NewFunc();
        fi->async = isAsync;
        fi->line = t.line;
        fi->col = t.col;
        if (Eat(P_Star)) fi->generator = true;
        if (Cur().t == TT::Name) {
            // the name of a generator / async expression is bound inside it with its own rules
            fi->name = BindingName();
            if ((fi->generator && Eq(fi->name, "yield")) || (fi->async && Eq(fi->name, "await")))
                FailAt(t, "this function can not be named '" + Utf16To8(fi->name) + "'");
        } else if (!isExpr) Unexpected("a function name");
        Node* n = ast.NewNode(NK::Func, t.line, t.col);
        n->fn = fi;
        if (isExpr && !fi->name.empty()) n->flags |= NF_Computed;   // a named function expression binds its own name
        PushFunc(fi);
        Params(fi);
        FunctionBody(fi);
        PopFunc();
        return n;
    }

    Node* ArrowFunction(bool isAsync) {
        const Token& t = Cur();
        FuncInfo* fi = ast.NewFunc();
        fi->arrow = true;
        fi->async = isAsync;
        fi->line = t.line;
        fi->col = t.col;
        Node* n = ast.NewNode(NK::Func, t.line, t.col);
        n->fn = fi;
        PushFunc(fi);
        if (Cur().t == TT::Name) {
            Node* p = N(NK::Ident);
            p->str = BindingName();
            fi->params.push_back(p);
            fi->length = 1;
        } else Params(fi);
        if (Cur().nl) Fail("a line break is not allowed before '=>'");
        Expect(P_Arrow);
        if (IsP(P_LBrace)) FunctionBody(fi);
        else {
            std::vector<Node*> savedCover;
            savedCover.swap(cover);
            fi->exprBody = true;
            fi->body = Assignment();
            CheckCover();
            cover.swap(savedCover);
        }
        PopFunc();
        return n;
    }

    // A method of an object literal or class: the key is already read into `keyNode`.
    Node* MethodFunction(Node* keyNode, bool isAsync, bool isGen, int accessor, bool isClass, ClassInfo* ci) {
        FuncInfo* fi = ast.NewFunc();
        fi->method = true;
        fi->async = isAsync;
        fi->generator = isGen;
        fi->getter = accessor == 1;
        fi->setter = accessor == 2;
        fi->line = keyNode->line;
        fi->col = keyNode->col;
        if (!(keyNode->flags & NF_Computed)) fi->name = keyNode->str;
        if (isClass && ci && ci->heritage) fi->derived = false;
        Node* n = ast.NewNode(NK::Func, keyNode->line, keyNode->col);
        n->fn = fi;
        PushFunc(fi);
        Params(fi);
        if (fi->getter && !fi->params.empty()) Fail("a getter takes no parameters");
        if (fi->setter && (fi->params.size() != 1 || fi->params[0]->k == NK::RestElem)) Fail("a setter takes exactly one parameter");
        FunctionBody(fi);
        PopFunc();
        return n;
    }

    // The static fields' initializer function (made on first use): static fields and static blocks run in it, in order.
    FuncInfo* StaticInit(ClassInfo* ci, const Token& at) { return FieldInit(ci->staticFields, at); }
    // A class's instance or static field initializer function, made on first use.
    FuncInfo* FieldInit(FuncInfo*& init, const Token& at) {
        if (!init) {
            init = ast.NewFunc();
            init->method = true;
            init->fieldInit = true;
            init->line = at.line;
            init->col = at.col;
            init->name = u"<fields>";
            init->body = ast.NewNode(NK::Block, at.line, at.col);
        }
        return init;
    }
    // static { ... }: an arrow function (so `this`, `super` and `new.target` are the static initializer's) called in
    // place; its `var`s stay inside it as a ClassStaticBlockBody's do.
    void StaticBlock(ClassInfo* ci, const Token& at) {
        FuncInfo* init = StaticInit(ci, at);
        PushFunc(init);
        FuncInfo* fi = ast.NewFunc();
        fi->arrow = true;
        fi->line = at.line;
        fi->col = at.col;
        fi->name = u"static";
        Node* fn = ast.NewNode(NK::Func, at.line, at.col);
        fn->fn = fi;
        PushFunc(fi);
        int savedLoop = loopDepth;
        FunctionBody(fi);
        loopDepth = savedLoop;
        PopFunc();
        PopFunc();
        Node* call = ast.NewNode(NK::Call, at.line, at.col);
        call->a = fn;
        Node* stmt = ast.NewNode(NK::ExprStmt, at.line, at.col);
        stmt->a = call;
        init->body->list.push_back(stmt);
    }

    // A #method / get #x / set #x: declared, kept in a hidden binding of the class scope ("#x:m", "#x:g", "#x:s"), and,
    // for an instance one, added to each new object by the field initializer before the fields (7.3.34 InitializeInstanceElements).
    void PrivateMethod(ClassInfo* ci, Node* m, bool isStatic, int accessor, const Token& kt) {
        DeclarePrivate(ci, m->str, (accessor == 1 ? PK_Get : accessor == 2 ? PK_Set : PK_Method) | (isStatic ? PK_Static : 0), kt);
        const std::u16string hidden = m->str + (accessor == 1 ? u":g" : accessor == 2 ? u":s" : u":m");
        ci->privHidden.push_back(hidden);
        if (isStatic) return;
        FuncInfo* init = FieldInit(ci->instFields, kt);
        Node* f = ast.NewNode(NK::ClassField, kt.line, kt.col);
        f->str = m->str;
        f->flags |= NF_Private | NF_Method;
        f->op = accessor;
        f->c = ast.NewNode(NK::Ident, kt.line, kt.col);
        f->c->str = hidden;
        init->body->list.insert(init->body->list.begin() + ci->privInstMethods++, f);
        PushFunc(init);
        Ref(m->str);
        Ref(hidden);
        PopFunc();
    }

    // class [name] [extends expr] { members } (15.7)
    Node* ClassExpr(bool isExpr) {
        const Token& t = Cur();
        Next();   // class
        Node* n = ast.NewNode(NK::Class, t.line, t.col);
        ast.classes.push_back(std::make_unique<ClassInfo>());
        ClassInfo* ci = ast.classes.back().get();
        n->ci = ci;
        if (Cur().t == TT::Name && !IsWord("extends")) n->str = BindingName();
        else if (!isExpr) Unexpected("a class name");
        if (EatWord("extends")) ci->heritage = LeftHandSide();
        n->a = ci->heritage;
        NoInGuard ng(*this, false);   // ClassBody has no [In] parameter: its keys and fields allow `in`
        Expect(P_LBrace);
        privScopes.emplace_back();
        bool derived = ci->heritage != nullptr;
        while (!IsP(P_RBrace)) {
            if (Eat(P_Semi)) continue;
            const Token& mt = Cur();
            bool isStatic = false;
            if (IsWord("static") && !IsPAt(pos + 1, P_LParen) && !IsPAt(pos + 1, P_Assign) && !IsPAt(pos + 1, P_Semi) &&
                !IsPAt(pos + 1, P_RBrace)) {
                isStatic = true;
                Next();
                if (IsP(P_LBrace)) {   // 15.7.11 ClassStaticBlock: runs once, in order with the static fields, this = the class
                    StaticBlock(ci, mt);
                    continue;
                }
            }
            bool isAsync = false, isGen = false;
            int accessor = 0;
            auto startsKey = [&](std::size_t k) {
                const Token& x = At(k);
                return x.t == TT::Name || x.t == TT::Str || x.t == TT::Num || x.t == TT::PrivateName ||
                       (x.t == TT::Punct && (x.p == P_LBrack || x.p == P_Star));
            };
            if ((IsWord("get") || IsWord("set")) && startsKey(pos + 1) && !IsPAt(pos + 1, P_Star)) {
                accessor = IsWord("get") ? 1 : 2;
                Next();
            } else if (IsWord("async") && startsKey(pos + 1) && !Peek().nl) {
                isAsync = true;
                Next();
            }
            if (!accessor && Eat(P_Star)) isGen = true;
            Node* m = ast.NewNode(NK::ClassMethod, mt.line, mt.col);
            if (isStatic) m->flags |= NF_Static;
            const Token& kt = Cur();
            if (kt.t == TT::PrivateName) {
                m->str = u"#" + kt.s;
                m->flags |= NF_Private;
                Next();
            } else PropertyName(m);
            bool isCtorName = !(m->flags & NF_Computed) && kt.t != TT::Num && Eq(m->str, "constructor");
            if (IsP(P_LParen)) {
                if (isCtorName && !isStatic) {
                    if (accessor || isAsync || isGen) FailAt(kt, "the class constructor can not be a getter, setter, async or generator");
                    if (ci->ctor) FailAt(kt, "a class can have only one constructor");
                    FuncInfo* fi = ast.NewFunc();
                    fi->classCtor = true;
                    fi->derived = derived;
                    fi->name = n->str;
                    fi->line = kt.line;
                    fi->col = kt.col;
                    PushFunc(fi);
                    Params(fi);
                    FunctionBody(fi);
                    PopFunc();
                    ci->ctor = fi;
                    continue;
                }
                if (isStatic && !(m->flags & NF_Computed) && Eq(m->str, "prototype"))
                    FailAt(kt, "a class can not have a static member named 'prototype'");
                m->op = accessor;   // 0 method, 1 get, 2 set
                m->b = MethodFunction(m, isAsync, isGen, accessor, true, ci);
                n->list.push_back(m);
                if (m->flags & NF_Private) PrivateMethod(ci, m, isStatic, accessor, kt);
                continue;
            }
            // a field
            if (accessor || isAsync || isGen) Unexpected("'('");
            if (m->flags & NF_Computed) FailAt(kt, "computed class field names are not supported yet");
            if (isCtorName) FailAt(kt, "a class field can not be named 'constructor'");
            if (isStatic && Eq(m->str, "prototype")) FailAt(kt, "a class can not have a static member named 'prototype'");
            if (isStatic) StaticInit(ci, kt);
            FuncInfo* init = FieldInit(isStatic ? ci->staticFields : ci->instFields, kt);
            Node* f = ast.NewNode(NK::ClassField, kt.line, kt.col);
            f->str = m->str;
            if (m->flags & NF_Private) {
                f->flags |= NF_Private;
                DeclarePrivate(ci, f->str, PK_Field | (isStatic ? PK_Static : 0), kt);
                PushFunc(init);
                Ref(f->str);
                PopFunc();
            }
            if (Eat(P_Assign)) {
                PushFunc(init);
                std::vector<Node*> savedCover;
                savedCover.swap(cover);
                f->a = Assignment();
                CheckCover();
                cover.swap(savedCover);
                PopFunc();
                if (f->a->k == NK::Func || f->a->k == NK::Class) NameAnon(f->a, f->str);
            }
            init->body->list.push_back(f);
            Semicolon();
        }
        EndPrivScope();
        Next();
        if (!ci->ctor) {
            FuncInfo* fi = ast.NewFunc();
            fi->classCtor = true;
            fi->derived = derived;
            fi->name = n->str;
            fi->line = t.line;
            fi->col = t.col;
            fi->body = nullptr;   // the default constructor (15.7.14 step 10)
            if (derived) fi->usesThis = true;
            ci->ctor = fi;
        }
        ci->ctor->hasFields = ci->instFields != nullptr;
        ci->ctor->fields = ci->instFields;
        n->fn = ci->ctor;
        return n;
    }

};

} // namespace

bool Parse(const std::vector<Token>& toks, Ast& ast, SyntaxError& err, bool module) {
    Parser p(toks, ast, err);
    try {
        if (module) p.ParseModule();
        else p.ParseScript();
    } catch (const ParseFail&) {
        return false;
    }
    return true;
}

} // namespace okay::js
