#include "ScriptInternal.hpp"
#include <set>

namespace okay::script {

namespace {

struct ParseError { SrcLoc loc; std::string msg; };

// C# words that aren't part of OkayScript (yet). Using one is an error that says
// so, rather than syntax that is silently skipped.
const std::unordered_map<std::string, const char*>& NotYet() {
    static const std::unordered_map<std::string, const char*> m = {
        {"foreach", "foreach loops aren't supported yet: use a for loop"},
        {"switch", "switch isn't supported yet: use if / else if"},
        {"do", "do-while loops aren't supported yet: use while"},
        {"try", "try / catch isn't supported yet"},
        {"throw", "throw isn't supported yet"},
        {"enum", "enums aren't supported yet"},
        {"struct", "structs aren't supported yet"},
        {"interface", "interfaces aren't supported yet"},
        {"static", "static members aren't supported yet"},
        {"readonly", "readonly isn't supported yet: use const for a value that never changes"},
        {"override", "override isn't supported: scripts don't inherit from each other yet"},
        {"virtual", "virtual isn't supported: scripts don't inherit from each other yet"},
        {"abstract", "abstract isn't supported"},
        {"sealed", "sealed isn't supported"},
        {"protected", "protected isn't supported: members are private unless marked public"},
        {"internal", "internal isn't supported: members are private unless marked public"},
        {"async", "async isn't supported"},
        {"await", "await isn't supported"},
        {"yield", "coroutines (yield) aren't supported yet"},
        {"delegate", "delegates aren't supported"},
        {"event", "events aren't supported yet"},
        {"partial", "partial classes aren't supported"},
        {"lock", "lock isn't supported"},
        {"goto", "goto isn't supported"},
    };
    return m;
}

class Parser {
public:
    Parser(const std::vector<Token>& t, Program& p) : toks_(t), prog_(p) {}

    void File() {
        int nsDepth = 0;
        while (!Is(T::End)) {
            std::size_t start = pos_;
            try {
                if (IsWord("using")) {
                    Error(Peek().loc, "`using` isn't needed in OkayScript: every engine type is already available. Delete this line");
                    while (!Is(T::Semicolon) && !Is(T::End)) ++pos_;
                    Match(T::Semicolon);
                    continue;
                }
                if (IsWord("namespace")) {
                    Error(Peek().loc, "`namespace` isn't needed in OkayScript. Delete it (and its braces), keep what's inside");
                    ++pos_;
                    while (Is(T::Ident) || Is(T::Dot)) ++pos_;
                    if (Match(T::LBrace)) ++nsDepth; else Match(T::Semicolon);
                    continue;
                }
                if (Is(T::RBrace) && nsDepth > 0) { ++pos_; --nsDepth; continue; }
                ClassDecl c = Class();
                prog_.classes.push_back(std::move(c));
            } catch (const ParseError& e) {
                Error(e.loc, e.msg);
                SyncTopLevel(start);
            }
        }
    }

private:
    const std::vector<Token>& toks_;
    Program& prog_;
    std::size_t pos_ = 0;

    const Token& Peek(std::size_t o = 0) const {
        std::size_t i = pos_ + o;
        return i < toks_.size() ? toks_[i] : toks_.back();
    }
    bool Is(T t, std::size_t o = 0) const { return Peek(o).type == t; }
    bool IsWord(const char* w, std::size_t o = 0) const { return Is(T::Ident, o) && Peek(o).text == w; }
    bool Match(T t) { if (Is(t)) { ++pos_; return true; } return false; }
    const Token& Expect(T t, const char* what) {
        if (!Is(t)) {
            std::string got = Is(T::End) ? "the end of the file" : (Peek().text.empty() ? TokenName(Peek().type) : "'" + Peek().text + "'");
            throw ParseError{Peek().loc, std::string("Expected ") + what + " here, not " + got};
        }
        return toks_[pos_++];
    }
    void Error(SrcLoc l, const std::string& m) { prog_.diags.push_back({l.line, m, 0}); }

    void SyncTopLevel(std::size_t start) {
        if (pos_ == start) ++pos_;
        int depth = 0;
        while (!Is(T::End)) {
            if (Is(T::LBrace)) ++depth;
            else if (Is(T::RBrace)) { if (--depth <= 0) { ++pos_; return; } }
            ++pos_;
        }
    }
    // Skip to the end of the current member / statement after an error.
    void SyncStatement(std::size_t start) {
        if (pos_ == start) ++pos_;
        int depth = 0;
        while (!Is(T::End)) {
            if (Is(T::LBrace)) ++depth;
            else if (Is(T::RBrace)) { if (depth == 0) return; if (--depth == 0) { ++pos_; return; } }
            else if (Is(T::Semicolon) && depth == 0) { ++pos_; return; }
            ++pos_;
        }
    }

    void RejectNotYet() {
        if (Is(T::Ident)) {
            auto it = NotYet().find(Peek().text);
            if (it != NotYet().end()) throw ParseError{Peek().loc, it->second};
        }
    }

    std::vector<Attribute> Attributes() {
        std::vector<Attribute> out;
        while (Is(T::LBracket)) {
            ++pos_;
            do {
                Attribute a;
                a.loc = Peek().loc;
                a.name = Expect(T::Ident, "an attribute name").text;
                if (Match(T::LParen)) {
                    if (!Is(T::RParen)) do { a.args.push_back(Expression()); } while (Match(T::Comma));
                    Expect(T::RParen, "')'");
                }
                out.push_back(std::move(a));
            } while (Match(T::Comma));
            Expect(T::RBracket, "']'");
        }
        return out;
    }

    ClassDecl Class() {
        Attributes();
        while (Match(T::KwPublic) || Match(T::KwPrivate)) {}
        RejectNotYet();
        if (!Is(T::KwClass)) throw ParseError{Peek().loc, "Expected a class here: class Name : Behaviour { ... }"};
        ClassDecl c;
        c.loc = Expect(T::KwClass, "class").loc;
        c.name = Expect(T::Ident, "the class name").text;
        if (Match(T::Colon)) {
            c.baseLoc = Peek().loc;
            c.base = Expect(T::Ident, "the base class (Behaviour)").text;
            if (Is(T::Comma)) throw ParseError{Peek().loc, "A class can only extend one thing (Behaviour)"};
        }
        Expect(T::LBrace, "'{'");
        while (!Is(T::RBrace) && !Is(T::End)) {
            std::size_t start = pos_;
            try { Member(c); }
            catch (const ParseError& e) { Error(e.loc, e.msg); SyncStatement(start); }
        }
        Expect(T::RBrace, "'}' to close the class");
        return c;
    }

    bool IsTypeStart(std::size_t o = 0) const {
        switch (Peek(o).type) {
            case T::KwInt: case T::KwFloat: case T::KwBool: case T::KwString: case T::KwVoid: case T::Ident: return true;
            default: return false;
        }
    }

    TypeRef Type(bool allowVar) {
        TypeRef t;
        t.loc = Peek().loc;
        switch (Peek().type) {
            case T::KwInt: t.name = "int"; break;
            case T::KwFloat: t.name = "float"; break;
            case T::KwBool: t.name = "bool"; break;
            case T::KwString: t.name = "string"; break;
            case T::KwVoid: t.name = "void"; break;
            case T::KwVar:
                if (!allowVar) throw ParseError{Peek().loc, "`var` only works for local variables inside a method: write the type here"};
                t.name = "var"; break;
            case T::Ident: t.name = Peek().text; break;
            default: throw ParseError{Peek().loc, "Expected a type (int, float, bool, string, Vector3, GameObject, ...)"};
        }
        ++pos_;
        if (Is(T::Lt)) throw ParseError{Peek().loc, "Generic types like " + t.name + "<...> aren't supported yet"};
        if (Is(T::LBracket)) throw ParseError{Peek().loc, "Arrays aren't supported yet"};
        if (Is(T::Question)) throw ParseError{Peek().loc, "Nullable types (" + t.name + "?) aren't supported"};
        return t;
    }

    void Member(ClassDecl& c) {
        std::vector<Attribute> attrs = Attributes();
        bool isPublic = false, isConst = false;
        for (;;) {
            if (Match(T::KwPublic)) { isPublic = true; continue; }
            if (Is(T::KwPrivate)) {
                Error(Peek().loc, "`private` is already the default (only `public` fields show in the Inspector). Delete it");
                ++pos_; continue;
            }
            if (Match(T::KwConst)) { isConst = true; continue; }
            break;
        }
        RejectNotYet();
        if (Is(T::KwClass)) throw ParseError{Peek().loc, "Classes inside classes aren't supported"};
        // Constructor: Name(
        if (Is(T::Ident) && Peek().text == c.name && Is(T::LParen, 1))
            throw ParseError{Peek().loc, "Constructors aren't supported: set things up in Awake() or Start()"};
        TypeRef type = Type(false);
        SrcLoc nameLoc = Peek().loc;
        std::string name = Expect(T::Ident, "a name").text;
        if (Is(T::LParen)) {
            if (isConst) throw ParseError{nameLoc, "Methods can't be const"};
            if (!attrs.empty()) Error(attrs[0].loc, "Attributes only work on fields");
            MethodDecl m;
            m.isPublic = isPublic; m.ret = type; m.name = name; m.loc = nameLoc;
            Expect(T::LParen, "'('");
            if (!Is(T::RParen)) {
                do {
                    Param p;
                    p.type = Type(false);
                    p.loc = Peek().loc;
                    p.name = Expect(T::Ident, "a parameter name").text;
                    if (Is(T::Assign)) throw ParseError{Peek().loc, "Default parameter values aren't supported yet"};
                    m.params.push_back(std::move(p));
                } while (Match(T::Comma));
            }
            Expect(T::RParen, "')'");
            if (Is(T::Assign) || IsWord("where")) throw ParseError{Peek().loc, "Expected the method body { ... }"};
            m.body = Block();
            c.methods.push_back(std::move(m));
            return;
        }
        if (Is(T::LBrace)) throw ParseError{Peek().loc, "Properties ({ get; set; }) aren't supported yet: use a field"};
        if (type.name == "void") throw ParseError{type.loc, "A field can't be void"};
        for (;;) {
            FieldDecl f;
            f.attrs = std::move(attrs); attrs.clear();
            f.isPublic = isPublic; f.isConst = isConst; f.type = type; f.name = name; f.loc = nameLoc;
            if (Match(T::Assign)) f.init = Expression();
            else if (isConst) throw ParseError{nameLoc, "A const needs a value: const " + type.name + " " + name + " = ...;"};
            c.fields.push_back(std::move(f));
            if (!Match(T::Comma)) break;
            nameLoc = Peek().loc;
            name = Expect(T::Ident, "a name").text;
        }
        Expect(T::Semicolon, "';'");
    }

    // ---- Statements ----
    std::unique_ptr<Stmt> Block() {
        auto b = std::make_unique<Stmt>();
        b->kind = SK::Block;
        b->loc = Expect(T::LBrace, "'{'").loc;
        while (!Is(T::RBrace) && !Is(T::End)) {
            std::size_t start = pos_;
            try { b->body.push_back(Statement()); }
            catch (const ParseError& e) { Error(e.loc, e.msg); SyncStatement(start); }
        }
        Expect(T::RBrace, "'}'");
        return b;
    }

    bool IsLocalDeclStart() const {
        switch (Peek().type) {
            case T::KwInt: case T::KwFloat: case T::KwBool: case T::KwString: case T::KwVar: case T::KwConst: return true;
            case T::Ident: return Is(T::Ident, 1) || (Is(T::Lt, 1) && Is(T::Ident, 2) && Is(T::Gt, 3) && Is(T::Ident, 4));
            default: return false;
        }
    }

    std::unique_ptr<Stmt> LocalDeclStmt() {
        auto s = std::make_unique<Stmt>();
        s->kind = SK::Local;
        s->loc = Peek().loc;
        if (Is(T::KwConst)) throw ParseError{Peek().loc, "const only works on fields"};
        TypeRef type = Type(true);
        do {
            LocalDecl d;
            d.type = type;
            d.loc = Peek().loc;
            d.name = Expect(T::Ident, "a variable name").text;
            if (Match(T::Assign)) d.init = Expression();
            else if (type.name == "var") throw ParseError{d.loc, "`var` needs a value to work out its type: var " + d.name + " = ...;"};
            s->locals.push_back(std::move(d));
        } while (Match(T::Comma));
        return s;
    }

    std::unique_ptr<Stmt> Statement() {
        RejectNotYet();
        SrcLoc loc = Peek().loc;
        if (Is(T::LBrace)) return Block();
        if (Match(T::Semicolon)) throw ParseError{loc, "An empty statement does nothing. Delete the extra ';'"};
        if (Match(T::KwIf)) {
            auto s = std::make_unique<Stmt>(); s->kind = SK::If; s->loc = loc;
            Expect(T::LParen, "'(' after if");
            s->expr = Expression();
            Expect(T::RParen, "')'");
            s->then = Statement();
            if (Match(T::KwElse)) s->otherwise = Statement();
            return s;
        }
        if (Match(T::KwWhile)) {
            auto s = std::make_unique<Stmt>(); s->kind = SK::While; s->loc = loc;
            Expect(T::LParen, "'(' after while");
            s->expr = Expression();
            Expect(T::RParen, "')'");
            s->loopBody = Statement();
            return s;
        }
        if (Match(T::KwFor)) {
            auto s = std::make_unique<Stmt>(); s->kind = SK::For; s->loc = loc;
            Expect(T::LParen, "'(' after for");
            if (!Is(T::Semicolon)) {
                if (IsLocalDeclStart()) s->init = LocalDeclStmt();
                else {
                    auto e = std::make_unique<Stmt>(); e->kind = SK::Expr; e->loc = Peek().loc;
                    e->expr = Expression();
                    s->init = std::move(e);
                }
            }
            Expect(T::Semicolon, "';'");
            if (!Is(T::Semicolon)) s->expr = Expression();
            Expect(T::Semicolon, "';'");
            if (!Is(T::RParen)) do { s->step.push_back(Expression()); } while (Match(T::Comma));
            Expect(T::RParen, "')'");
            s->loopBody = Statement();
            return s;
        }
        if (Match(T::KwReturn)) {
            auto s = std::make_unique<Stmt>(); s->kind = SK::Return; s->loc = loc;
            if (!Is(T::Semicolon)) s->expr = Expression();
            Expect(T::Semicolon, "';'");
            return s;
        }
        if (Match(T::KwBreak)) { Expect(T::Semicolon, "';'"); auto s = std::make_unique<Stmt>(); s->kind = SK::Break; s->loc = loc; return s; }
        if (Match(T::KwContinue)) { Expect(T::Semicolon, "';'"); auto s = std::make_unique<Stmt>(); s->kind = SK::Continue; s->loc = loc; return s; }
        if (Is(T::KwElse)) throw ParseError{loc, "This else has no if before it"};
        if (IsLocalDeclStart()) {
            auto s = LocalDeclStmt();
            Expect(T::Semicolon, "';'");
            return s;
        }
        auto s = std::make_unique<Stmt>(); s->kind = SK::Expr; s->loc = loc;
        s->expr = Expression();
        Expect(T::Semicolon, "';'");
        return s;
    }

    // ---- Expressions ----
    ExprPtr Make(EK k, SrcLoc l) { auto e = std::make_unique<Expr>(); e->kind = k; e->loc = l; return e; }

    ExprPtr Expression() {
        if (nesting_ == 0) operators_ = 0;   // a new top-level expression
        return Assignment();
    }

    // Expressions nest by recursion, so cap how deep they go: thousands of '('
    // or '-' would otherwise overflow the stack instead of giving an error.
    static constexpr int kMaxNesting = 256;
    int nesting_ = 0;
    // a + b + ... parses in a loop but builds a tree as deep as it is long, which
    // checking, running and freeing walk recursively: cap the operators too.
    static constexpr int kMaxOperators = 1000;
    int operators_ = 0;
    struct NestGuard {
        int& n;
        NestGuard(int& d, SrcLoc l) : n(d) { if (++n > kMaxNesting) { --n; throw ParseError{l, "This expression is nested too deeply"}; } }
        ~NestGuard() { --n; }
    };

    ExprPtr Assignment() {
        NestGuard guard(nesting_, Peek().loc);
        ExprPtr lhs = Conditional();
        switch (Peek().type) {
            case T::Assign: case T::PlusEq: case T::MinusEq: case T::StarEq: case T::SlashEq: case T::PercentEq: {
                auto e = Make(EK::Assign, Peek().loc);
                e->op = Peek().type; ++pos_;
                e->lhs = std::move(lhs);
                e->rhs = Assignment();
                return e;
            }
            default: return lhs;
        }
    }
    ExprPtr Conditional() {
        ExprPtr c = Binary(0);
        if (Is(T::Question)) {
            auto e = Make(EK::Conditional, Peek().loc); ++pos_;
            e->third = std::move(c);
            e->lhs = Expression();
            Expect(T::Colon, "':'");
            e->rhs = Conditional();
            return e;
        }
        return c;
    }
    static int Prec(T t) {
        switch (t) {
            case T::OrOr: return 1; case T::AndAnd: return 2;
            case T::EqEq: case T::NotEq: return 3;
            case T::Lt: case T::Gt: case T::Le: case T::Ge: return 4;
            case T::Plus: case T::Minus: return 5;
            case T::Star: case T::Slash: case T::Percent: return 6;
            default: return -1;
        }
    }
    ExprPtr Binary(int minPrec) {
        ExprPtr lhs = Unary();
        for (;;) {
            int p = Prec(Peek().type);
            if (p < 0 || p <= minPrec - 1 || p < minPrec) break;
            if (++operators_ > kMaxOperators)
                throw ParseError{Peek().loc, "This expression is too long (more than " + std::to_string(kMaxOperators) + " operators): split it up"};
            auto e = Make(EK::Binary, Peek().loc);
            e->op = Peek().type; ++pos_;
            e->lhs = std::move(lhs);
            e->rhs = Binary(p + 1);
            lhs = std::move(e);
        }
        return lhs;
    }
    bool IsCastAhead() const {
        if (!Is(T::LParen) || !Is(T::RParen, 2)) return false;
        switch (Peek(1).type) { case T::KwInt: case T::KwFloat: case T::KwBool: case T::KwString: return true; default: return false; }
    }
    ExprPtr Unary() {
        SrcLoc l = Peek().loc;
        const bool prefixed = Is(T::Minus) || Is(T::Not) || Is(T::Plus) || Is(T::PlusPlus) || Is(T::MinusMinus) || IsCastAhead();
        if (!prefixed) return Postfix();
        NestGuard guard(nesting_, l);   // - - - ... x recurses here, not through Assignment
        if (Is(T::Minus) || Is(T::Not)) {
            auto e = Make(EK::Unary, l); e->op = Peek().type; ++pos_;
            e->lhs = Unary();
            return e;
        }
        if (Is(T::Plus)) { ++pos_; return Unary(); }
        if (Is(T::PlusPlus) || Is(T::MinusMinus)) {
            auto e = Make(EK::IncDec, l); e->op = Peek().type; e->prefix = true; ++pos_;
            e->lhs = Unary();
            return e;
        }
        if (IsCastAhead()) {
            auto e = Make(EK::Cast, l); ++pos_;
            e->typeArg = Type(false);
            Expect(T::RParen, "')'");
            e->lhs = Unary();
            return e;
        }
        return Postfix();
    }
    std::vector<ExprPtr> Args() {
        std::vector<ExprPtr> a;
        Expect(T::LParen, "'('");
        if (!Is(T::RParen)) do { a.push_back(Expression()); } while (Match(T::Comma));
        Expect(T::RParen, "')'");
        return a;
    }
    bool IsGenericCallAhead() const { return Is(T::Lt) && Is(T::Ident, 1) && Is(T::Gt, 2) && Is(T::LParen, 3); }
    ExprPtr Postfix() {
        ExprPtr e = Primary();
        for (;;) {
            if (Is(T::Dot)) {
                ++pos_;
                SrcLoc l = Peek().loc;
                std::string name = Expect(T::Ident, "a member name after '.'").text;
                if (IsGenericCallAhead() || Is(T::LParen)) {
                    auto c = Make(EK::Call, l);
                    c->name = name; c->object = std::move(e);
                    if (IsGenericCallAhead()) { ++pos_; c->typeArg = Type(false); Expect(T::Gt, "'>'"); }
                    c->args = Args();
                    e = std::move(c);
                } else {
                    auto m = Make(EK::Member, l);
                    m->name = name; m->object = std::move(e);
                    e = std::move(m);
                }
                continue;
            }
            if (Is(T::LParen) && e->kind == EK::Name) {
                auto c = Make(EK::Call, e->loc);
                c->name = e->name;
                c->args = Args();
                e = std::move(c);
                continue;
            }
            if (Is(T::LBracket)) throw ParseError{Peek().loc, "Indexing with [ ] isn't supported yet"};
            if (Is(T::PlusPlus) || Is(T::MinusMinus)) {
                auto i = Make(EK::IncDec, Peek().loc); i->op = Peek().type; ++pos_;
                i->lhs = std::move(e);
                e = std::move(i);
                continue;
            }
            break;
        }
        return e;
    }
    ExprPtr Primary() {
        const Token& t = Peek();
        SrcLoc l = t.loc;
        switch (t.type) {
            case T::IntLit: {
                ++pos_;
                auto e = Make(EK::IntLit, l);
                try {
                    long long v = std::stoll(t.text);
                    if (v > 2147483647LL) throw ParseError{l, t.text + " is too big for an int"};
                    e->ival = (std::int32_t)v;
                } catch (const std::out_of_range&) { throw ParseError{l, t.text + " is too big for an int"}; }
                return e;
            }
            case T::FloatLit: {
                ++pos_;
                auto e = Make(EK::FloatLit, l);
                try { e->fval = std::stof(t.text); }   // throws when the value doesn't fit a float
                catch (const std::exception&) { throw ParseError{l, "number out of range"}; }
                return e;
            }
            case T::StringLit: { ++pos_; auto e = Make(EK::StringLit, l); e->sval = t.text; return e; }
            case T::KwTrue: case T::KwFalse: { ++pos_; auto e = Make(EK::BoolLit, l); e->bval = t.type == T::KwTrue; return e; }
            case T::KwNull: { ++pos_; return Make(EK::NullLit, l); }
            case T::KwThis: { ++pos_; return Make(EK::This, l); }
            case T::KwNew: {
                ++pos_;
                auto e = Make(EK::New, l);
                e->typeArg = Type(false);
                e->args = Args();
                return e;
            }
            case T::LParen: {
                ++pos_;
                ExprPtr e = Expression();
                Expect(T::RParen, "')'");
                return e;
            }
            case T::Ident: {
                ++pos_;
                if (IsGenericCallAhead()) {
                    auto c = Make(EK::Call, l);
                    c->name = t.text;
                    ++pos_; c->typeArg = Type(false); Expect(T::Gt, "'>'");
                    c->args = Args();
                    return c;
                }
                auto e = Make(EK::Name, l);
                e->name = t.text;
                return e;
            }
            case T::KwInt: case T::KwFloat: case T::KwBool: case T::KwString: {
                // int.Parse etc. would go here; not supported yet.
                throw ParseError{l, "'" + t.text + "' can't be used as a value here"};
            }
            default:
                throw ParseError{l, std::string("Expected a value here, not ") + (t.text.empty() ? TokenName(t.type) : "'" + t.text + "'")};
        }
    }
};

} // namespace

void Parse(const std::vector<Token>& toks, Program& out) {
    Parser p(toks, out);
    p.File();
}

} // namespace okay::script
