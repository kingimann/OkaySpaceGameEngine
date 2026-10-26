#include "ScriptInternal.hpp"
#include <set>

namespace okay::script {

namespace {

struct ParseError {
    SrcLoc loc;
    std::string msg;
    bool tooDeep = false;   ///< nesting limit: the whole member is skipped, so it's reported once
};

// C# words that aren't part of OkayScript (yet). Using one is an error that says
// so, rather than syntax that is silently skipped.
const std::unordered_map<std::string, const char*>& NotYet() {
    static const std::unordered_map<std::string, const char*> m = {
        {"struct", "structs aren't supported yet"},
        {"readonly", "readonly isn't supported yet: use const for a value that never changes"},
        {"sealed", "sealed isn't supported"},
        {"internal", "internal isn't supported: members are private unless marked public (or protected)"},
        {"async", "async isn't supported"},
        {"await", "await isn't supported"},
        {"yield", "coroutines (yield) aren't supported yet"},
        {"delegate", "delegate types aren't supported: use Func<...> or Action<...>"},
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
                if (IsEnumAhead()) { Enum(); continue; }
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
    void Error(SrcLoc l, const std::string& m) { prog_.diags.push_back({l.line, m, 0, l.col}); }

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

    // enum Name [: int] { A, B = 5, C }, after any public/private.
    bool IsEnumAhead() const {
        std::size_t o = 0;
        while (Is(T::KwPublic, o) || Is(T::KwPrivate, o)) ++o;
        return IsWord("enum", o) && Is(T::Ident, o + 1);
    }
    void Enum() {
        while (Match(T::KwPublic) || Match(T::KwPrivate)) {}
        ++pos_;   // enum
        EnumDecl e;
        e.loc = Peek().loc;
        e.name = Expect(T::Ident, "the enum's name").text;
        if (Is(T::Colon)) throw ParseError{Peek().loc, "Enums are always ints: delete the ': type'"};
        Expect(T::LBrace, "'{'");
        while (!Is(T::RBrace) && !Is(T::End)) {
            EnumMember m;
            m.loc = Peek().loc;
            m.name = Expect(T::Ident, "a name for the enum value").text;
            if (Match(T::Assign)) m.value = Expression();
            e.members.push_back(std::move(m));
            if (!Match(T::Comma)) break;
        }
        Expect(T::RBrace, "'}' to close the enum");
        Match(T::Semicolon);
        prog_.enums.push_back(std::move(e));
    }

    // { a, b, c } (a trailing comma is fine).
    std::vector<ExprPtr> ArrayItems() {
        std::vector<ExprPtr> items;
        Expect(T::LBrace, "'{'");
        while (!Is(T::RBrace) && !Is(T::End)) {
            if (Is(T::LBrace)) throw ParseError{Peek().loc, "An array inside an array is written new int[] { ... }"};
            items.push_back(Expression());
            if (!Match(T::Comma)) break;
        }
        Expect(T::RBrace, "'}' to close the list");
        return items;
    }
    // `= { ... }` after a field or variable of an array type.
    ExprPtr Initializer() {
        if (!Is(T::LBrace)) return Expression();
        auto e = Make(EK::ArrayLit, Peek().loc);
        e->args = ArrayItems();
        return e;
    }

    ClassDecl Class() {
        Attributes();
        ClassDecl c;
        for (;;) {
            if (Match(T::KwPublic) || Match(T::KwPrivate)) continue;
            if (IsWord("abstract")) { c.isAbstract = true; ++pos_; continue; }
            break;
        }
        RejectNotYet();
        if (IsWord("static")) throw ParseError{Peek().loc, "A script class can't be static: it's a component on each object"};
        if (IsWord("interface") && Is(T::Ident, 1)) {
            if (c.isAbstract) throw ParseError{Peek().loc, "An interface is already abstract: delete `abstract`"};
            c.isInterface = true;
            ++pos_;
        } else if (!Is(T::KwClass)) {
            throw ParseError{Peek().loc, "Expected a class here: class Name : Behaviour { ... }"};
        } else {
            ++pos_;
        }
        c.loc = Peek().loc;
        c.name = Expect(T::Ident, c.isInterface ? "the interface name" : "the class name").text;
        if (Match(T::Colon)) {
            do {
                c.baseLocs.push_back(Peek().loc);
                c.bases.push_back(Expect(T::Ident, "what the class extends (Behaviour, a class or an interface)").text);
            } while (Match(T::Comma));
        }
        Expect(T::LBrace, "'{'");
        while (!Is(T::RBrace) && !Is(T::End)) {
            std::size_t start = pos_;
            try { if (c.isInterface) InterfaceMember(c); else Member(c); }
            catch (const ParseError& e) {
                Error(e.loc, e.msg);
                if (e.tooDeep) pos_ = start;   // skip the whole member, not just the innermost statement
                SyncStatement(start);
            }
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
        if (Is(T::Lt)) {   // List<int>, Dictionary<string, List<int>>
            NestGuard guard(nesting_, Peek().loc, "type");
            ++pos_;
            do { t.args.push_back(Type(false)); } while (Match(T::Comma));
            Expect(T::Gt, "'>'");
        }
        for (;;) {
            if (Is(T::LBracket) && Is(T::Comma, 1))
                throw ParseError{Peek(1).loc, "Multi-dimensional arrays (" + t.name + "[,]) aren't supported: use an array of arrays, " + t.name + "[][]"};
            if (!(Is(T::LBracket) && Is(T::RBracket, 1))) break;
            pos_ += 2;
            if (++t.rank > 32) throw ParseError{t.loc, "Too many [] in this type"};
        }
        if (Is(T::Question)) throw ParseError{Peek().loc, "Nullable types (" + t.name + "?) aren't supported"};
        return t;
    }

    std::vector<Param> Params() {
        std::vector<Param> ps;
        Expect(T::LParen, "'('");
        if (!Is(T::RParen)) {
            do {
                Param p;
                p.type = Type(false);
                p.loc = Peek().loc;
                p.name = Expect(T::Ident, "a parameter name").text;
                if (Is(T::Assign)) throw ParseError{Peek().loc, "Default parameter values aren't supported yet"};
                ps.push_back(std::move(p));
            } while (Match(T::Comma));
        }
        Expect(T::RParen, "')'");
        return ps;
    }

    // interface IDamageable { void TakeDamage(int amount); }: methods without bodies.
    void InterfaceMember(ClassDecl& c) {
        if (Is(T::KwPublic)) throw ParseError{Peek().loc, "An interface's methods are already public: delete `public`"};
        RejectNotYet();
        MethodDecl m;
        m.isPublic = true;
        m.isAbstract = true;
        m.ret = Type(false);
        m.loc = Peek().loc;
        m.name = Expect(T::Ident, "a method name").text;
        if (!Is(T::LParen)) throw ParseError{m.loc, "An interface only has methods, like void " + m.name + "();"};
        m.params = Params();
        if (Is(T::LBrace)) throw ParseError{Peek().loc, "An interface's method has no body: end it with ';' and write the body in each class"};
        Expect(T::Semicolon, "';'");
        c.methods.push_back(std::move(m));
    }

    void Member(ClassDecl& c) {
        std::vector<Attribute> attrs = Attributes();
        bool isPublic = false, isProtected = false, isConst = false, isStatic = false, isEvent = false;
        bool isVirtual = false, isOverride = false, isAbstract = false;
        SrcLoc staticLoc, eventLoc, modLoc;
        for (;;) {
            if (Match(T::KwPublic)) { isPublic = true; continue; }
            if (IsWord("protected")) { isProtected = true; ++pos_; continue; }
            if (IsWord("virtual")) { isVirtual = true; modLoc = Peek().loc; ++pos_; continue; }
            if (IsWord("override")) { isOverride = true; modLoc = Peek().loc; ++pos_; continue; }
            if (IsWord("abstract")) { isAbstract = true; modLoc = Peek().loc; ++pos_; continue; }
            if (Is(T::KwPrivate)) {
                Error(Peek().loc, "`private` is already the default (only `public` fields show in the Inspector). Delete it");
                ++pos_; continue;
            }
            if (Match(T::KwConst)) { isConst = true; continue; }
            if (IsWord("static")) { isStatic = true; staticLoc = Peek().loc; ++pos_; continue; }
            if (IsWord("event") && IsTypeStart(1)) { isEvent = true; eventLoc = Peek().loc; ++pos_; continue; }
            break;
        }
        if (IsWord("enum") && Is(T::Ident, 1)) {
            if (isConst) throw ParseError{Peek().loc, "An enum can't be const (its values already are)"};
            if (!attrs.empty()) Error(attrs[0].loc, "Attributes only work on fields");
            Enum();
            return;
        }
        if (isStatic && isConst) Error(staticLoc, "A const is already shared: delete `static`");
        RejectNotYet();
        if (Is(T::KwClass)) throw ParseError{Peek().loc, "Classes inside classes aren't supported"};
        // A constructor, Name(...), is a void method named after its class.
        const bool isCtor = Is(T::Ident) && Peek().text == c.name && Is(T::LParen, 1);
        TypeRef type;
        if (isCtor) { type.name = "void"; type.loc = Peek().loc; }
        else type = Type(false);
        SrcLoc nameLoc = Peek().loc;
        std::string name = Expect(T::Ident, "a name").text;
        if (Is(T::LParen)) {
            if (isConst) throw ParseError{nameLoc, "Methods can't be const"};
            if (isEvent) throw ParseError{eventLoc, "Only fields can be events: event Action<int> Damaged;"};
            if (!attrs.empty()) Error(attrs[0].loc, "Attributes only work on fields");
            MethodDecl m;
            m.isPublic = isPublic; m.isProtected = isProtected; m.isStatic = isStatic; m.isCtor = isCtor; m.ret = type; m.name = name; m.loc = nameLoc;
            m.isVirtual = isVirtual; m.isOverride = isOverride; m.isAbstract = isAbstract;
            if (isCtor && (isVirtual || isOverride || isAbstract)) throw ParseError{modLoc, "A constructor can't be virtual, override or abstract"};
            m.params = Params();
            if (isCtor && Match(T::Colon)) {   // Boss(int hp) : base(hp) { }
                m.baseLoc = Peek().loc;
                if (IsWord("this")) throw ParseError{m.baseLoc, ": this(...) isn't supported: put the shared set-up in a method"};
                if (!IsWord("base")) throw ParseError{m.baseLoc, "Expected base(...) after ':'"};
                ++pos_;
                m.hasBaseCall = true;
                m.baseArgs = Args();
            }
            if (isAbstract) {
                if (Is(T::LBrace)) throw ParseError{Peek().loc, "An abstract method has no body: end it with ';' (or remove `abstract`)"};
                Expect(T::Semicolon, "';' after an abstract method");
                c.methods.push_back(std::move(m));
                return;
            }
            if (Is(T::Assign) || IsWord("where") || Is(T::Semicolon)) throw ParseError{Peek().loc, "Expected the method body { ... }"};
            m.body = Block();
            c.methods.push_back(std::move(m));
            return;
        }
        if (isVirtual || isOverride || isAbstract) throw ParseError{modLoc, "Only methods can be virtual, override or abstract"};
        if (Is(T::LBrace)) throw ParseError{Peek().loc, "Properties ({ get; set; }) aren't supported yet: use a field"};
        if (type.name == "void") throw ParseError{type.loc, "A field can't be void"};
        for (;;) {
            FieldDecl f;
            f.attrs = std::move(attrs); attrs.clear();
            f.isPublic = isPublic; f.isConst = isConst; f.isStatic = isStatic && !isConst; f.type = type; f.name = name; f.loc = nameLoc;
            f.isEvent = isEvent;
            f.isProtected = isProtected;
            if (Match(T::Assign)) f.init = Initializer();
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
            catch (const ParseError& e) { if (e.tooDeep) throw; Error(e.loc, e.msg); SyncStatement(start); }
        }
        Expect(T::RBrace, "'}'");
        return b;
    }

    // When a type (Name, Name<...> with nested type arguments, then any []) starts
    // here, the offset just after it; otherwise 0.
    std::size_t TypeEndAhead() const {
        std::size_t o = 1;
        if (Is(T::Lt, o)) {
            for (int depth = 0;; ++o) {
                if (Is(T::Lt, o)) ++depth;
                else if (Is(T::Gt, o)) { if (--depth == 0) { ++o; break; } }
                else if (!IsTypeStart(o) && !Is(T::Comma, o) && !Is(T::LBracket, o) && !Is(T::RBracket, o)) return 0;
            }
        }
        while (Is(T::LBracket, o) && Is(T::RBracket, o + 1)) o += 2;
        return o;
    }

    bool IsLocalDeclStart() const {
        switch (Peek().type) {
            case T::KwInt: case T::KwFloat: case T::KwBool: case T::KwString: case T::KwVar: case T::KwConst: return true;
            case T::Ident: {
                const std::size_t end = TypeEndAhead();
                return (end > 0 && Is(T::Ident, end)) || (Is(T::LBracket, 1) && Is(T::Comma, 2));
            }
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
            if (Match(T::Assign)) d.init = Initializer();
            else if (type.name == "var") throw ParseError{d.loc, "`var` needs a value to work out its type: var " + d.name + " = ...;"};
            s->locals.push_back(std::move(d));
        } while (Match(T::Comma));
        return s;
    }

    std::unique_ptr<Stmt> Statement() {
        RejectNotYet();
        if (IsWord("static") && IsTypeStart(1))
            throw ParseError{Peek().loc, "static only works on fields and methods, not on a variable inside a method"};
        SrcLoc loc = Peek().loc;
        NestGuard guard(stmtNesting_, loc, "code");   // blocks, ifs, loops and switches recurse here
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
        if (IsWord("switch") && Is(T::LParen, 1)) return Switch();
        if (IsWord("do") && IsDoBodyAhead()) {   // do { ... } while (cond);
            ++pos_;
            auto s = std::make_unique<Stmt>(); s->kind = SK::DoWhile; s->loc = loc;
            s->loopBody = Statement();
            if (!Match(T::KwWhile)) throw ParseError{Peek().loc, "Expected while (...); after the do body"};
            Expect(T::LParen, "'(' after while");
            s->expr = Expression();
            Expect(T::RParen, "')'");
            Expect(T::Semicolon, "';'");
            return s;
        }
        if (IsWord("foreach") && Is(T::LParen, 1)) {
            ++pos_;
            auto s = std::make_unique<Stmt>(); s->kind = SK::Foreach; s->loc = loc;
            Expect(T::LParen, "'(' after foreach");
            LocalDecl d;
            d.type = Type(true);
            d.loc = Peek().loc;
            d.name = Expect(T::Ident, "the loop variable's name").text;
            if (!IsWord("in")) throw ParseError{Peek().loc, "Expected 'in' here: foreach (var item in items)"};
            ++pos_;
            s->expr = Expression();
            Expect(T::RParen, "')'");
            s->locals.push_back(std::move(d));
            s->loopBody = Statement();
            return s;
        }
        if (IsWord("try") && Is(T::LBrace, 1)) return Try();
        if (IsWord("catch") || IsWord("finally")) throw ParseError{loc, "This " + Peek().text + " has no try before it"};
        if (IsWord("throw")) {   // throw new X(...); or throw; inside a catch
            ++pos_;
            auto s = std::make_unique<Stmt>(); s->kind = SK::Throw; s->loc = loc;
            if (!Is(T::Semicolon)) s->expr = Expression();
            Expect(T::Semicolon, "';'");
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

    // `do` starts a do / while unless it's a variable called do (do = 1; do++; do.x).
    bool IsDoBodyAhead() const {
        switch (Peek(1).type) {
            case T::Assign: case T::PlusEq: case T::MinusEq: case T::StarEq: case T::SlashEq: case T::PercentEq:
            case T::PlusPlus: case T::MinusMinus: case T::Dot: case T::LBracket: case T::LParen: case T::Semicolon:
            case T::QuestionQuestionEq: case T::QuestionDot:
                return false;
            default: return true;
        }
    }
    // try { } catch (T e) { } ... finally { }
    std::unique_ptr<Stmt> Try() {
        auto s = std::make_unique<Stmt>();
        s->kind = SK::Try;
        s->loc = Peek().loc;
        ++pos_;
        s->then = Block();
        while (IsWord("catch")) {
            CatchClause c;
            c.loc = Peek().loc;
            ++pos_;
            if (Match(T::LParen)) {
                c.typed = true;
                c.type = Type(false);
                if (Is(T::Ident)) { c.nameLoc = Peek().loc; c.name = Peek().text; ++pos_; }
                Expect(T::RParen, "')'");
            }
            if (IsWord("when")) throw ParseError{Peek().loc, "catch ... when isn't supported: use an if inside the catch (and throw; to pass it on)"};
            c.body = Block();
            s->catches.push_back(std::move(c));
        }
        if (IsWord("finally")) { ++pos_; s->finallyBody = Block(); }
        if (s->catches.empty() && !s->finallyBody) throw ParseError{s->loc, "A try needs a catch or a finally after it"};
        return s;
    }

    bool IsCaseLabel() const { return IsWord("case") || (IsWord("default") && Is(T::Colon, 1)); }
    // switch (x) { case 1: case 2: ... break; default: ... break; }
    std::unique_ptr<Stmt> Switch() {
        auto s = std::make_unique<Stmt>(); s->kind = SK::Switch; s->loc = Peek().loc;
        ++pos_;
        Expect(T::LParen, "'(' after switch");
        s->expr = Expression();
        Expect(T::RParen, "')'");
        Expect(T::LBrace, "'{' to start the switch's cases");
        while (!Is(T::RBrace) && !Is(T::End)) {
            if (!IsCaseLabel()) {
                Error(Peek().loc, "Expected case or default here: switch (x) { case 1: ... break; }");
                SyncStatement(pos_);
                continue;
            }
            SwitchSection sec;
            sec.loc = Peek().loc;
            while (IsCaseLabel()) {
                SrcLoc l = Peek().loc;
                if (IsWord("default")) {
                    if (sec.isDefault) Error(l, "A switch has only one default");
                    sec.isDefault = true; sec.defaultLoc = l;
                    pos_ += 2;
                    continue;
                }
                ++pos_;
                std::size_t start = pos_;
                try {
                    ExprPtr v = Expression();
                    if (IsWord("when")) throw ParseError{Peek().loc, "case ... when isn't supported yet: use an if inside the case"};
                    Expect(T::Colon, "':' after the case value");
                    sec.labels.push_back(std::move(v));
                    sec.labelLocs.push_back(l);
                } catch (const ParseError& e) {
                    if (e.tooDeep) throw;
                    Error(e.loc, e.msg);
                    while (!Is(T::Colon) && !Is(T::RBrace) && !Is(T::End) && pos_ >= start) ++pos_;
                    Match(T::Colon);
                }
            }
            while (!Is(T::RBrace) && !Is(T::End) && !IsCaseLabel()) {
                std::size_t start = pos_;
                try { sec.body.push_back(Statement()); }
                catch (const ParseError& e) { if (e.tooDeep) throw; Error(e.loc, e.msg); SyncStatement(start); }
            }
            s->sections.push_back(std::move(sec));
        }
        Expect(T::RBrace, "'}' to close the switch");
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
    int stmtNesting_ = 0;
    struct NestGuard {
        int& n;
        NestGuard(int& d, SrcLoc l, const char* what = "expression") : n(d) {
            if (++n > kMaxNesting) { --n; throw ParseError{l, std::string("This ") + what + " is nested too deeply", true}; }
        }
        ~NestGuard() { --n; }
    };

    // x =>, () =>, (a, b) =>
    bool IsLambdaAhead() const {
        if (Is(T::Ident)) return Is(T::Arrow, 1);
        if (!Is(T::LParen)) return false;
        std::size_t o = 1;
        if (!Is(T::RParen, o)) {
            for (;;) {
                if (!Is(T::Ident, o)) return false;
                ++o;
                if (Is(T::RParen, o)) break;
                if (!Is(T::Comma, o)) return false;
                ++o;
            }
        }
        return Is(T::Arrow, o + 1);
    }
    ExprPtr Lambda() {
        auto e = Make(EK::Lambda, Peek().loc);
        e->lambda = std::make_unique<LambdaDecl>();
        auto param = [&] {
            Param p;
            p.loc = Peek().loc;
            p.name = Expect(T::Ident, "a parameter name").text;
            e->lambda->params.push_back(std::move(p));
        };
        if (Match(T::LParen)) {
            if (!Is(T::RParen)) do { param(); } while (Match(T::Comma));
            Expect(T::RParen, "')'");
        } else {
            param();
        }
        Expect(T::Arrow, "'=>'");
        if (Is(T::LBrace)) {
            NestGuard guard(stmtNesting_, Peek().loc, "code");
            e->lambda->body = Block();
        } else {
            e->lambda->value = Assignment();
        }
        return e;
    }

    ExprPtr Assignment() {
        NestGuard guard(nesting_, Peek().loc);
        if (IsLambdaAhead()) return Lambda();
        ExprPtr lhs = Conditional();
        switch (Peek().type) {
            case T::Assign: case T::PlusEq: case T::MinusEq: case T::StarEq: case T::SlashEq: case T::PercentEq:
            case T::QuestionQuestionEq: {
                auto e = Make(EK::Assign, Peek().loc);
                e->op = Peek().type; ++pos_;
                e->lhs = std::move(lhs);
                e->rhs = Assignment();
                return e;
            }
            default: return lhs;
        }
    }
    // a ?? b ?? c: below || and above ?:, grouping from the right.
    ExprPtr Coalesce() {
        ExprPtr lhs = Binary(0);
        if (!Is(T::QuestionQuestion)) return lhs;
        NestGuard guard(nesting_, Peek().loc);
        auto e = Make(EK::Binary, Peek().loc);
        e->op = T::QuestionQuestion; ++pos_;
        e->lhs = std::move(lhs);
        e->rhs = Coalesce();
        return e;
    }
    ExprPtr Conditional() {
        ExprPtr c = Coalesce();
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
            if ((IsWord("is") || IsWord("as")) && minPrec <= 4) {   // x is Boss, x as Boss: as tight as < and >
                if (++operators_ > kMaxOperators)
                    throw ParseError{Peek().loc, "This expression is too long (more than " + std::to_string(kMaxOperators) + " operators): split it up"};
                auto e = Make(IsWord("is") ? EK::Is : EK::As, Peek().loc);
                ++pos_;
                e->typeArg = Type(false);
                if (Is(T::Ident)) throw ParseError{Peek().loc, "`is` with a name (x is T t) isn't supported: write T t = x as T; then check t != null"};
                e->lhs = std::move(lhs);
                lhs = std::move(e);
                continue;
            }
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
        switch (Peek(1).type) {
            case T::KwInt: case T::KwFloat: case T::KwBool: case T::KwString: return true;
            case T::Ident:
                // (Name)x is a cast when what follows can only start a value, as in C#.
                switch (Peek(3).type) {
                    case T::Ident: case T::IntLit: case T::FloatLit: case T::StringLit: case T::LParen: case T::Not:
                    case T::KwTrue: case T::KwFalse: case T::KwNull: case T::KwThis: case T::KwNew: return true;
                    default: return false;
                }
            default: return false;
        }
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
        // With ?. in the chain, the whole chain gives null when a ?. finds null.
        SrcLoc condLoc;
        std::string condName;
        bool cond = false;
        for (;;) {
            if (Is(T::Dot) || Is(T::QuestionDot)) {
                const bool q = Is(T::QuestionDot);
                if (q && !cond) {
                    cond = true; condLoc = Peek().loc;
                    condName = e->kind == EK::Name || e->kind == EK::Member || e->kind == EK::Call ? e->name : "this value";
                }
                ++pos_;
                SrcLoc l = Peek().loc;
                std::string name = Expect(T::Ident, "a member name after '.'").text;
                if (IsGenericCallAhead() || Is(T::LParen)) {
                    auto c = Make(EK::Call, l);
                    c->name = name; c->object = std::move(e);
                    if (IsGenericCallAhead()) { ++pos_; c->typeArg = Type(false); Expect(T::Gt, "'>'"); }
                    c->args = Args();
                    c->nullCond = q;
                    e = std::move(c);
                } else {
                    auto m = Make(EK::Member, l);
                    m->name = name; m->object = std::move(e);
                    m->nullCond = q;
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
            if (Is(T::LParen) && (e->kind == EK::Call || e->kind == EK::Index)) {   // calling the function a call or an item gives
                auto c = Make(EK::Call, Peek().loc);
                c->name = "Invoke";
                c->object = std::move(e);
                c->args = Args();
                c->bind = Bind::Invoke;   // marks the implicit Invoke for the checker
                e = std::move(c);
                continue;
            }
            if (Is(T::LBracket)) {
                auto ix = Make(EK::Index, Peek().loc);
                ++pos_;
                ix->lhs = Expression();
                if (Is(T::Comma)) throw ParseError{Peek().loc, "Multi-dimensional arrays aren't supported: use a[i][j]"};
                Expect(T::RBracket, "']'");
                ix->object = std::move(e);
                e = std::move(ix);
                continue;
            }
            if (Is(T::PlusPlus) || Is(T::MinusMinus)) {
                if (cond) throw ParseError{Peek().loc, "Something reached with ?. can't be changed: check for null first"};
                auto i = Make(EK::IncDec, Peek().loc); i->op = Peek().type; ++pos_;
                i->lhs = std::move(e);
                e = std::move(i);
                continue;
            }
            break;
        }
        if (cond) {
            auto w = Make(EK::NullCond, condLoc);
            w->name = condName;
            w->lhs = std::move(e);
            return w;
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
            case T::InterpBegin: {
                ++pos_;
                auto e = Make(EK::Interp, l);
                while (!Is(T::InterpEnd) && !Is(T::End)) {
                    if (Is(T::StringLit)) {
                        auto part = Make(EK::StringLit, Peek().loc);
                        part->sval = Peek().text;
                        ++pos_;
                        e->args.push_back(std::move(part));
                        continue;
                    }
                    Expect(T::InterpHole, "'{'");
                    ExprPtr v = Expression();
                    if (Is(T::InterpFormat)) {
                        auto f = Make(EK::Format, Peek().loc);
                        f->sval = Peek().text;
                        ++pos_;
                        f->lhs = std::move(v);
                        v = std::move(f);
                    }
                    Expect(T::InterpHoleEnd, "'}' to close the { }");
                    e->args.push_back(std::move(v));
                }
                Expect(T::InterpEnd, "the end of the text");
                return e;
            }
            case T::KwTrue: case T::KwFalse: { ++pos_; auto e = Make(EK::BoolLit, l); e->bval = t.type == T::KwTrue; return e; }
            case T::KwNull: { ++pos_; return Make(EK::NullLit, l); }
            case T::KwThis: { ++pos_; return Make(EK::This, l); }
            case T::KwNew: {
                ++pos_;
                auto e = Make(EK::New, l);
                if (Is(T::LBracket) && Is(T::RBracket, 1)) {   // new[] { ... }: the items give the type
                    pos_ += 2;
                    e->kind = EK::NewArray;
                    e->hasInit = true;
                    e->args = ArrayItems();
                    return e;
                }
                e->typeArg = Type(false);
                if (Is(T::LBracket)) {   // new T[n], new T[n][]
                    e->kind = EK::NewArray;
                    ++pos_;
                    e->lhs = Expression();
                    if (Is(T::Comma)) throw ParseError{Peek().loc, "Multi-dimensional arrays aren't supported: use an array of arrays, new int[n][]"};
                    Expect(T::RBracket, "']'");
                    ++e->typeArg.rank;
                    while (Is(T::LBracket) && Is(T::RBracket, 1)) {
                        pos_ += 2;
                        if (++e->typeArg.rank > 32) throw ParseError{l, "Too many [] in this type"};
                    }
                    if (Is(T::LBracket)) throw ParseError{Peek().loc, "Only the first [ ] of new can have a length: new int[3][]"};
                } else if (e->typeArg.rank > 0) {
                    e->kind = EK::NewArray;
                    if (!Is(T::LBrace)) throw ParseError{Peek().loc, "An array needs a length or its items: new int[3] or new int[] { 1, 2, 3 }"};
                }
                if (e->kind == EK::NewArray) {
                    if (Is(T::LBrace)) { e->hasInit = true; e->args = ArrayItems(); }
                    return e;
                }
                const bool parens = Is(T::LParen);
                if (parens) e->args = Args();
                if (Is(T::LBrace)) {   // new List<int> { 1, 2 }
                    if (!e->args.empty()) throw ParseError{Peek().loc, "Give either the values in ( ) or the items in { }, not both"};
                    e->hasInit = true;
                    e->args = ArrayItems();
                } else if (!parens) {
                    throw ParseError{Peek().loc, "Expected ( ) or { ... } after new " + e->typeArg.name};
                }
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
