// The OkayScript parser (recursive descent, one function per precedence level). Moved from the engine's
// OkayScriptVM.cpp (written for this project); changes: statement and call lines for runtime errors, lines inside
// interpolated strings, declaration words stop at the end of their line, top-level statement words are not functions.
#include "okayscript/Parser.hpp"
#include "okayscript/Environment.hpp"
#include "okayscript/Error.hpp"
#include "okayscript/Lexer.hpp"
#include <algorithm>
#include <atomic>
#include <set>
#include <sstream>
#include <unordered_map>

namespace okayscript {

namespace {
std::atomic<int> g_nextLambda{1};   // lambda ids are unique across every program in the process
} // namespace

class ParserImpl {
    friend class Parser;
public:
    explicit ParserImpl(std::vector<Token> toks) : m_toks(std::move(toks)) {
        if (m_toks.empty() || m_toks.back().type != Tok::End) m_toks.push_back(Token{Tok::End, "", 0, 0});
        for (int i = 0; i < 4; ++i) m_toks.push_back(m_toks.back());   // look-ahead padding past End
        ScanEnums(); ScanUserClasses();
    }
    bool AtEnd() const { return Check(Tok::End); }

    // C# enums anywhere in the file: `enum State { Idle, Run = 5, Jump }` makes
    // State.Idle / State.Run / State.Jump number constants (0, 5, 6).
    void ScanEnums() {
        for (std::size_t i = 0; i + 2 < m_toks.size(); ++i) {
            if (m_toks[i].type != Tok::Ident || m_toks[i].text != "enum" || m_toks[i + 1].type != Tok::Ident) continue;
            const std::string en = m_toks[i + 1].text;
            std::size_t j = i + 2;
            while (m_toks[j].type != Tok::LBrace && m_toks[j].type != Tok::End) ++j;   // `: byte`
            if (m_toks[j].type != Tok::LBrace) continue;
            m_enums.insert(en);
            double next = 0;
            for (++j; m_toks[j].type != Tok::RBrace && m_toks[j].type != Tok::End; ++j) {
                if (m_toks[j].type != Tok::Ident) continue;
                std::string member = m_toks[j].text;
                if (m_toks[j + 1].type == Tok::Assign) {
                    bool neg = m_toks[j + 2].type == Tok::Minus;
                    const Token& v = m_toks[j + 2 + (neg ? 1 : 0)];
                    if (v.type == Tok::Number) next = neg ? -v.number : v.number;
                    else if (auto it = m_enumValues.find(en + "." + v.text); it != m_enumValues.end()) next = it->second;
                }
                m_enumValues[en + "." + member] = next;
                next += 1;
                while (m_toks[j + 1].type != Tok::Comma && m_toks[j + 1].type != Tok::RBrace && m_toks[j + 1].type != Tok::End) ++j;
            }
        }
    }
    // Plain classes (`class Item { ... }`, no base) are real types made with `new`.
    // A class with a base (`class Hero : Behaviour`) is the component and is not recorded.
    void ScanUserClasses() {
        for (std::size_t i = 0; i + 1 < m_toks.size(); ++i) {
            if (m_toks[i].type != Tok::Ident) continue;
            if (m_toks[i].text != "class" && m_toks[i].text != "struct") continue;
            if (m_toks[i + 1].type != Tok::Ident) continue;
            const std::size_t j = SkipTypeSuffix(i + 2);
            if (m_toks[j].type == Tok::LBrace) m_userClasses.insert(m_toks[i + 1].text);
        }
    }
    // Skip `[modifiers] enum Name [: type] { ... }` (already collected by ScanEnums).
    bool SkipEnumDecl() {
        std::size_t i = m_pos;
        while (m_toks[i].type == Tok::Ident && m_toks[i].text != "enum") ++i;
        if (m_toks[i].type != Tok::Ident || m_toks[i].text != "enum") return false;
        while (m_toks[i].type != Tok::LBrace && m_toks[i].type != Tok::End) ++i;
        int depth = 0;
        for (; m_toks[i].type != Tok::End; ++i) {
            if (m_toks[i].type == Tok::LBrace) ++depth;
            else if (m_toks[i].type == Tok::RBrace && --depth == 0) { ++i; break; }
        }
        m_pos = i;
        Match(Tok::Semicolon);
        return true;
    }

    std::vector<StmtPtr> ParseProgram(std::unordered_map<std::string, FunctionDecl>& funcs) {
        std::vector<StmtPtr> top;
        while (!Check(Tok::End)) ParseMember(top, funcs);
        return top;
    }
    std::unordered_map<std::string, UserClass> TakeClasses() { return std::move(m_classes); }

    // Parse for diagnostics only: keep going after a parse error by recording it
    // and re-synchronizing to the next statement boundary, so the editor can list
    // EVERY syntax error at once instead of just the first. The parsed output is
    // discarded — this exists purely to collect Diagnostic entries.
    // Syntax OkayScript accepts (so pasted C# runs) but that has no effect. Each is
    // reported once per line as a warning, so the editor can say exactly what to
    // delete instead of letting it look meaningful.
    std::vector<Diagnostic> NoEffectWarnings() const {
        std::vector<Diagnostic> w;
        std::set<std::pair<int, std::string>> seen;
        auto add = [&](int line, int col, const std::string& key, const std::string& msg) {
            if (seen.insert({line, key}).second) w.push_back({line, msg, 1, col});
        };
        static const std::unordered_map<std::string, const char*> kMods = {
            {"virtual",   "`virtual` does nothing: scripts don't inherit from each other. Delete it."},
            {"override",  "`override` does nothing: Start, Update and the other events are found by name. Delete it."},
            {"abstract",  "`abstract` does nothing in a script. Delete it."},
            {"sealed",    "`sealed` does nothing in a script. Delete it."},
            {"internal",  "`internal` does nothing: use `public` to show a field in the Inspector, or leave it off."},
            {"protected", "`protected` does nothing: use `public` to show a field in the Inspector, or leave it off."},
            {"partial",   "`partial` does nothing in a script. Delete it."},
            {"extern",    "`extern` does nothing in a script. Delete it."},
            {"unsafe",    "`unsafe` does nothing in a script. Delete it."},
            {"private",   "`private` is already the default (only `public` fields show in the Inspector). You can delete it."},
        };
        static const std::set<std::string> kNeverCalled = {
            "OnGUI", "OnApplicationQuit", "OnApplicationPause", "OnApplicationFocus", "OnBecameVisible",
            "OnBecameInvisible", "OnValidate", "Reset", "OnDrawGizmos", "OnDrawGizmosSelected", "OnPreRender",
            "OnPostRender", "OnRenderObject", "OnWillRenderObject", "OnParticleCollision", "OnControllerColliderHit",
            "OnJointBreak", "OnAnimatorMove", "OnAnimatorIK", "OnTransformParentChanged", "OnTransformChildrenChanged"};
        for (std::size_t i = 0; m_toks[i].type != Tok::End; ++i) {
            const Token& t = m_toks[i];
            const Token& nx = m_toks[i + 1];
            if (t.type == Tok::Number && t.suffix) {
                add(t.line, t.column, "suffix", "`" + t.text + "f`: the letter after the number does nothing (every number is "
                                      "already a decimal). Write " + t.text + ".");
                continue;
            }
            if (t.type != Tok::Ident) continue;
            if (t.text == "using" && nx.type == Tok::Ident)
                add(t.line, t.column, "using", "`using` does nothing: everything is already available. Delete this line.");
            else if (t.text == "namespace" && nx.type == Tok::Ident)
                add(t.line, t.column, "namespace", "`namespace` does nothing. Delete it (and its braces); keep what's inside.");
            else if ((t.text == "class" || t.text == "struct") && nx.type == Tok::Ident) {
                const std::size_t after = SkipTypeSuffix(i + 2);
                if (m_toks[after].type == Tok::Colon)
                    add(t.line, t.column, "class", "The class wrapper and `: " + m_toks[after + 1].text + "` do nothing: the fields "
                                         "and methods in this file are the component. Delete the `class " + nx.text +
                                         "` line and its braces.");
                // A plain `class Name { }` is a real script class. Only `: Base`
                // is still the old component wrapper, warned about above.
            } else if (nx.type == Tok::Ident) {
                auto m = kMods.find(t.text);
                if (m != kMods.end()) add(t.line, t.column, t.text, m->second);
            }
            if (kNeverCalled.count(t.text) && nx.type == Tok::LParen && i > 0 && m_toks[i - 1].type == Tok::Ident)
                add(t.line, t.column, "never", "`" + t.text + "()` is never called by the engine, so this code won't run.");
        }
        return w;
    }

    std::vector<Diagnostic> ParseProgramRecovering() {
        std::vector<Diagnostic> diags;
        std::vector<StmtPtr> top;
        std::unordered_map<std::string, FunctionDecl> funcs;
        while (!Check(Tok::End)) {
            std::size_t before = m_pos;
            try {
                ParseMember(top, funcs);
            } catch (const ScriptError& e) {
                diags.push_back({ e.line, e.what(), 0, e.column });
                if (diags.size() >= 100) break;          // stop runaway cascades
                Synchronize(before);
            }
            if (m_pos == before) ++m_pos;                // guarantee forward progress
        }
        for (auto& d : m_semantic) diags.push_back(d);
        for (auto& d : NoEffectWarnings()) diags.push_back(d);
        std::stable_sort(diags.begin(), diags.end(),
                         [](const Diagnostic& a, const Diagnostic& b) { return a.line < b.line; });
        return diags;
    }

    // Parse one top-level (or in-class) member: a function, a C#-style method,
    // a `[public] class Name : OkaySource { ... }` wrapper (its methods/fields
    // are hoisted out, so pasted C# scripts work), or a statement.
    void ParseMember(std::vector<StmtPtr>& top, std::unordered_map<std::string, FunctionDecl>& funcs) {
        StmtNestGuard guard(m_stmtNesting, *this);
        SkipAttributes();   // C# attributes: [SerializeField], [Header("..")], ...
        // C# `using X;` lines — nothing to import.
        if (Check(Tok::Ident) && Peek().text == "using" && m_toks[m_pos + 1].type == Tok::Ident) {
            m_pos += 2;
            while (Check(Tok::Ident) || Check(Tok::Assign) || Check(Tok::Dot)) ++m_pos;   // using X = Y.Z;
            Match(Tok::Semicolon);
            return;
        }
        if (IsEnumAhead()) { SkipEnumDecl(); return; }
        // `namespace Game.Player { ... }` (or the file-scoped `namespace Game;`): its members are ours.
        if (Check(Tok::Ident) && Peek().text == "namespace" && m_toks[m_pos + 1].type == Tok::Ident) {
            m_pos += 2;
            while (Check(Tok::Ident) || Check(Tok::Dot)) ++m_pos;
            if (Match(Tok::Semicolon)) return;
            Expect(Tok::LBrace, "'{'");
            while (!Check(Tok::RBrace) && !Check(Tok::End)) ParseMember(top, funcs);
            Expect(Tok::RBrace, "'}'");
            return;
        }
        if (IsClassAhead()) {
            if (!ClassHasBase()) { ParseUserClass(); return; }
            while (Check(Tok::Ident) && Peek().text != "class" && Peek().text != "struct") ++m_pos;  // modifiers
            Expect(Tok::Ident, "'class'");                                // the 'class' keyword
            Expect(Tok::Ident, "class name");                            // its name
            m_pos = SkipTypeSuffix(m_pos);                               // generic class Foo<T>
            if (Match(Tok::Colon)) {
                do { Expect(Tok::Ident, "base type"); m_pos = SkipTypeSuffix(m_pos); } while (Match(Tok::Comma));
            }
            Expect(Tok::LBrace, "'{'");
            while (!Check(Tok::RBrace) && !Check(Tok::End)) ParseMember(top, funcs);
            Expect(Tok::RBrace, "'}'");
            return;
        }
        if (Check(Tok::Function)) {
            const int fl = Peek().line; std::string name; FunctionDecl decl = ParseFunction(name);
            decl.name = name; decl.line = fl; funcs[name] = std::move(decl);
        } else if (IsTypedFunctionAhead()) {            // C#-style: void Start() { }
            const int fl = Peek().line; std::string name; FunctionDecl decl = ParseTypedFunction(name);
            decl.name = name; decl.line = fl; funcs[name] = std::move(decl);
        } else if (IsBareFunctionAhead()) {             // OkayScript: update(dt) { }
            const int fl = Peek().line;
            std::string name = Expect(Tok::Ident, "function name").text;
            FunctionDecl decl = ParseParamsAndBody();
            decl.name = name; decl.line = fl;
            funcs[name] = std::move(decl);
        } else {
            top.push_back(ParseStatement());
        }
    }

    // A brace-less OkayScript function: `name(params) { ... }` — a single
    // identifier, then a parenthesised list, then a '{'. That last '{' is what
    // tells it apart from an ordinary call statement like `on_key_move(5)` (which
    // is followed by a newline/';'), so the `function` keyword is optional.
    bool IsBareFunctionAhead() const {
        std::size_t i = m_pos;
        if (m_toks[i].type != Tok::Ident) return false;
        // Control-flow words that are lexed as identifiers (not keyword tokens)
        // can be `word (...) { ... }`, which otherwise looks exactly like a bare
        // function definition — exclude them so e.g. `foreach (x in xs) {}` is
        // parsed as a statement, not a function named "foreach".
        // switch / try / catch / throw too (the engine parser read a top-level `switch (x) { }` as a function
        // named switch; fixed here, test_parser "top-level statement words").
        static const std::set<std::string> kStatementWords = {"foreach", "do", "switch", "try", "catch", "throw"};
        if (kStatementWords.count(m_toks[i].text)) return false;
        if (m_toks[i + 1].type != Tok::LParen) return false;
        int depth = 0;
        for (i += 1; m_toks[i].type != Tok::End; ++i) {
            if (m_toks[i].type == Tok::LParen) ++depth;
            else if (m_toks[i].type == Tok::RParen && --depth == 0) { ++i; break; }
        }
        return m_toks[i].type == Tok::LBrace;
    }

    // Skip C# attributes like [SerializeField] or [Header("Stats")] that may
    // precede a field/method. Balanced brackets, so [Range(0, 1)] works too.
    void SkipAttributes() {
        while (Check(Tok::LBracket)) {
            int depth = 0;
            do {
                if (Check(Tok::LBracket)) ++depth;
                else if (Check(Tok::RBracket)) --depth;
                ++m_pos;
            } while (depth > 0 && !Check(Tok::End));
        }
    }

    bool IsEnumAhead() const {
        std::size_t i = m_pos;
        while (m_toks[i].type == Tok::Ident) { if (m_toks[i].text == "enum") return m_toks[i + 1].type == Tok::Ident; ++i; }
        return false;
    }
    // A run of identifiers containing "class" (e.g. `public class Foo`).
    bool IsClassAhead() const {
        std::size_t i = m_pos;
        while (m_toks[i].type == Tok::Ident) {
            if (m_toks[i].text == "class" || m_toks[i].text == "struct") return m_toks[i + 1].type == Tok::Ident;
            ++i;
        }
        return false;
    }
    // `class Item {` is a plain class. `class Hero : Behaviour {` is the component.
    bool ClassHasBase() const {
        std::size_t i = m_pos;
        while (m_toks[i].type == Tok::Ident && m_toks[i].text != "class" && m_toks[i].text != "struct") ++i;
        if (m_toks[i].type != Tok::Ident) return false;
        ++i;
        if (m_toks[i].type != Tok::Ident) return false;
        i = SkipTypeSuffix(i + 1);
        return m_toks[i].type == Tok::Colon;
    }

private:
    const Token& Peek() const { return m_toks[m_pos]; }
    const Token& Prev() const { return m_toks[m_pos - 1]; }
    bool Check(Tok t) const { return Peek().type == t; }
    bool Match(Tok t) { if (Check(t)) { ++m_pos; return true; } return false; }
    // What stopped the parse, for messages: 'name' for an identifier, else the token kind ("end of file", "';'").
    std::string GotText() const { return Peek().type == Tok::Ident ? "'" + Peek().text + "'" : std::string(TokName(Peek().type)); }
    const Token& Expect(Tok t, const char* msg) {
        if (!Check(t)) throw ScriptError(std::string("parse error: expected ") + msg + ", got " + GotText(), Peek().line, Peek().column);
        return m_toks[m_pos++];
    }

    // Error-recovery: after a parse error at `errStart`, skip past the offending
    // token and advance to the next likely statement start (just after a ';', or
    // before a top-level keyword) so parsing can resume and find further errors.
    void Synchronize(std::size_t errStart) {
        if (m_pos <= errStart) m_pos = errStart + 1;
        while (!Check(Tok::End)) {
            if (m_toks[m_pos - 1].type == Tok::Semicolon) return;
            switch (Peek().type) {
                case Tok::Function: case Tok::Var: case Tok::If:
                case Tok::While:    case Tok::For: case Tok::Return:
                    return;
                default: ++m_pos;
            }
        }
    }

    FunctionDecl ParseFunction(std::string& nameOut) {
        Expect(Tok::Function, "'function'");
        nameOut = Expect(Tok::Ident, "function name").text;
        return ParseParamsAndBody();
    }

    // A C#-style method declaration like `void Update()` / `private void Start()`
    // is a run of >= 2 identifiers (modifiers + return type + name) ending in a
    // name immediately followed by '('. (A plain call `foo()` is a single ident.)
    bool IsTypedFunctionAhead() const {
        int idents = 0;
        std::size_t i = ScanIdentRun(m_pos, idents);
        return idents >= 2 && m_toks[i].type == Tok::LParen;
    }

    // Skip a C# type's generic arguments and array ranks at token `i` (just past
    // its identifier): List<int>, Dictionary<string, int>, int[], float[,].
    // Returns `i` unchanged when what follows isn't type syntax.
    std::size_t SkipTypeSuffix(std::size_t i) const {
        if (m_toks[i].type == Tok::Lt) {
            int depth = 0; std::size_t j = i;
            for (; m_toks[j].type != Tok::End; ++j) {
                const Tok t = m_toks[j].type;
                if (t == Tok::Lt) ++depth;
                else if (t == Tok::Gt) { if (--depth == 0) { ++j; break; } }
                else if (t != Tok::Ident && t != Tok::Comma && t != Tok::LBracket && t != Tok::RBracket) return i;
            }
            if (depth != 0) return i;
            i = j;
        }
        while (m_toks[i].type == Tok::LBracket) {
            std::size_t j = i + 1;
            while (m_toks[j].type == Tok::Comma) ++j;
            if (m_toks[j].type != Tok::RBracket) break;
            i = j + 1;
        }
        return i;
    }
    // A run of identifiers (modifiers, a possibly generic / array type, a name).
    // The words stay on one line: the engine parser also ran on into the next line, so `var c` followed by a line
    // starting with a name took that name as the variable's (test_interpreter "declaration words stop").
    std::size_t ScanIdentRun(std::size_t i, int& idents) const {
        idents = 0;
        const int line = m_toks[i].line;
        while (m_toks[i].type == Tok::Ident && m_toks[i].line == line) { ++idents; ++i; i = SkipTypeSuffix(i); }
        return i;
    }
    // Consume such a run and return its last identifier (the declared name).
    std::string ConsumeIdentRun(const char* what) {
        const Token& first = Expect(Tok::Ident, what);
        std::string last = first.text;
        const int line = first.line;
        for (;;) {
            m_pos = SkipTypeSuffix(m_pos);
            if (!Check(Tok::Ident) || Peek().line != line) break;
            last = m_toks[m_pos++].text;
        }
        return last;
    }

    FunctionDecl ParseTypedFunction(std::string& nameOut) {
        // Consume modifier/type identifiers, keeping the last one (before '(')
        // as the function name. The return type is real: `void` can't return a
        // value, `int` / `float` / `bool` / `string` convert what's returned.
        bool isVoid = false;
        for (std::size_t i = m_pos; m_toks[i].type == Tok::Ident && m_toks[i + 1].type != Tok::LParen; i = SkipTypeSuffix(i + 1))
            if (m_toks[i].text == "void") isVoid = true;
        const unsigned char ret = (unsigned char)(DeclKindAhead() & 0x0F);
        nameOut = ConsumeIdentRun("type");
        const bool prevVoid = m_inVoid; const std::string prevFn = m_fnName;
        m_inVoid = isVoid; m_fnName = nameOut;
        FunctionDecl decl = ParseParamsAndBody();
        m_inVoid = prevVoid; m_fnName = prevFn;
        decl.returnKind = ret;
        return decl;
    }

    // Shared by `function f(...)` and `void F(...)`: parse the parameter list
    // (params may be bare `a` or typed `int a`) and the body block.
    FunctionDecl ParseParamsAndBody() {
        FunctionDecl decl;
        Expect(Tok::LParen, "'('");
        if (!Check(Tok::RParen)) {
            do {
                // `dt`, `float dt`, `List<int> xs`, `ref int n` — the last identifier is the name.
                const unsigned char pk = (unsigned char)(DeclKindAhead() & 0x0F);
                std::string p = ConsumeIdentRun("parameter");
                ExprPtr def;
                if (Match(Tok::Assign)) def = ParseExpression();   // `void Heal(int hp, int bonus = 2)`
                else if (!decl.defaults.empty() && decl.defaults.back())
                    throw ScriptError("parameter '" + p + "' needs a default too (ones with defaults come last)", Peek().line);
                for (const std::string& prev : decl.params)
                    if (prev == p) throw ScriptError("parameter '" + p + "' is already in this function", Peek().line);
                decl.params.push_back(p);
                decl.paramKinds.push_back(pk);
                decl.defaults.push_back(std::move(def));
            } while (Match(Tok::Comma));
        }
        Expect(Tok::RParen, "')'");
        decl.body = ParseBlock();
        return decl;
    }

    std::vector<StmtPtr> ParseBlock() {
        Expect(Tok::LBrace, "'{'");
        std::vector<StmtPtr> stmts;
        while (!Check(Tok::RBrace) && !Check(Tok::End)) stmts.push_back(ParseStatement());
        Expect(Tok::RBrace, "'}'");
        return stmts;
    }

    // A control-flow body: either a `{ ... }` block, or — to keep simple code short —
    // a single statement with no braces (C/JS style). So `if (hit) jump(8)` and
    // `while (alive) step()` both work without the ceremony of braces.
    std::vector<StmtPtr> ParseBlockOrStmt() {
        if (Check(Tok::LBrace)) return ParseBlock();
        return SingleStmt();
    }

    // The declared type + const-ness of the typed declaration starting at m_pos
    // (`const int MAX`, `public float speed`, `List<int> xs`). Generic / array
    // types (`int[]`, `List<int>`) hold anything.
    unsigned char DeclKindAhead() const {
        unsigned char kind = KAny, flags = 0;
        std::vector<std::pair<std::size_t, bool>> run;   // (token, has a generic / array suffix)
        for (std::size_t i = m_pos; m_toks[i].type == Tok::Ident;) {
            const std::size_t next = SkipTypeSuffix(i + 1);
            run.push_back({i, next != i + 1});
            i = next;
        }
        for (std::size_t r = 0; r + 1 < run.size(); ++r) {   // everything before the name
            const std::string& w = m_toks[run[r].first].text;
            if (w == "const" || w == "readonly") flags |= KConst;
            else if (w == "static") flags |= KStatic;
            else if (!run[r].second && TypeKind(w)) kind = TypeKind(w);
        }
        return (unsigned char)(kind | flags);
    }
    // Catch the obvious mistakes while typing: `int lives = "three";`,
    // `string name = 5;`, `int half = 0.5;` (becomes 0).
    void CheckLiteralInit(unsigned char kind, const std::string& name, Expr* init, int line, int column) {
        const unsigned char t = kind & 0x0F;
        if (!t || !init) return;
        const bool isStr = dynamic_cast<StringExpr*>(init) || dynamic_cast<InterpStringExpr*>(init);
        auto* num = dynamic_cast<NumberExpr*>(init);
        const bool isBool = dynamic_cast<BoolExpr*>(init) != nullptr;
        if ((t == KInt || t == KFloat || t == KBool) && isStr)
            m_semantic.push_back({line, "'" + name + "' is " + KindName(kind) + ", so it can't hold text", 0, column});
        else if (t == KString && (num || isBool))
            m_semantic.push_back({line, "'" + name + "' is a string: write the value in quotes", 0, column});
        else if (t == KInt && num && num->v != (double)(long long)num->v)
            m_semantic.push_back({line, "'" + name + "' is an int, so " + Fmt(num->v) + " becomes " +
                                        Fmt((double)(long long)num->v) + ". Use float for decimals", 1, column});
    }
    static std::string Fmt(double v) { std::ostringstream o; o << v; return o.str(); }

    // A C#-style typed local/field: a run of >= 2 identifiers (type + modifiers
    // + name) ending in '=' or ';' (e.g. `int score = 0;`, `Vector3 v;`).
    bool IsTypedVarDeclAhead() const {
        int idents = 0;
        std::size_t i = ScanIdentRun(m_pos, idents);
        if (idents < 2) return false;
        const Tok t = m_toks[i].type;
        // `int n` alone on its line is a declaration too (the line ends it).
        return t == Tok::Assign || t == Tok::Semicolon || t == Tok::Comma || IsAutoPropertyAt(i) ||
               t == Tok::End || t == Tok::RBrace || m_toks[i].line != m_toks[m_pos].line;
    }
    // `{ get; set; }` / `{ get; private set; }` right after a declared name.
    bool IsAutoPropertyAt(std::size_t i) const {
        if (m_toks[i].type != Tok::LBrace || m_toks[i + 1].type != Tok::Ident) return false;
        static const std::set<std::string> kAcc = {"get", "set", "init", "private", "protected", "internal", "public"};
        for (++i; m_toks[i].type != Tok::RBrace; ++i) {
            if (m_toks[i].type == Tok::Semicolon) continue;
            if (m_toks[i].type != Tok::Ident || !kAcc.count(m_toks[i].text)) return false;
        }
        return true;
    }
    void SkipAutoPropertyBody() {
        if (!IsAutoPropertyAt(m_pos)) return;
        while (!Check(Tok::RBrace)) ++m_pos;
        ++m_pos;
    }
    bool HasPublicModifier() const {
        int idents = 0;
        const std::size_t end = ScanIdentRun(m_pos, idents);
        for (std::size_t i = m_pos; i < end; ++i)
            if (m_toks[i].type == Tok::Ident && m_toks[i].text == "public") return true;
        return false;
    }
    // `class Item { fields, constructors, methods }`. No base type: this is data,
    // not the component. `static` and `const` aren't supported on these yet.
    void ParseUserClass() {
        StmtNestGuard guard(m_stmtNesting, *this);
        while (Check(Tok::Ident) && Peek().text != "class" && Peek().text != "struct") ++m_pos;
        Expect(Tok::Ident, "'class'");
        const int line = Peek().line;
        const std::string name = Expect(Tok::Ident, "class name").text;
        m_pos = SkipTypeSuffix(m_pos);
        if (m_classes.count(name))
            throw ScriptError("there's already a class named '" + name + "'", line);
        Expect(Tok::LBrace, "'{'");
        UserClass cls;
        cls.name = name;
        auto addField = [&](std::string fname, unsigned char kind, bool isPublic, ExprPtr init) {
            for (auto& f : cls.fields)
                if (f.name == fname) throw ScriptError(name + " already has a field named '" + fname + "'", Peek().line);
            FieldDef fd;
            fd.name = std::move(fname);
            fd.kind = (unsigned char)(kind & 0x0F);
            fd.isPublic = isPublic;
            fd.init = std::move(init);
            cls.fields.push_back(std::move(fd));
        };
        while (!Check(Tok::RBrace) && !Check(Tok::End)) {
            SkipAttributes();
            if (Check(Tok::RBrace) || Check(Tok::End)) break;
            if (Match(Tok::Semicolon)) continue;
            if (IsClassAhead()) {
                if (ClassHasBase()) throw ScriptError("a class inside " + name + " can't have a base type", Peek().line);
                ParseUserClass();
                continue;
            }
            if (IsEnumAhead()) { SkipEnumDecl(); continue; }
            const bool isPublic = HasPublicModifier();
            const unsigned char flags = DeclKindAhead();
            if (flags & (KStatic | KConst))
                throw ScriptError("plain classes can't be static or const yet", Peek().line);
            if (Check(Tok::Function) || IsTypedFunctionAhead() || IsBareFunctionAhead()) {
                std::string mname;
                FunctionDecl decl;
                if (Check(Tok::Function)) decl = ParseFunction(mname);
                else if (IsTypedFunctionAhead()) decl = ParseTypedFunction(mname);
                else {
                    mname = Expect(Tok::Ident, "function name").text;
                    decl = ParseParamsAndBody();
                }
                MethodDef md;
                md.decl = std::move(decl);
                md.isPublic = isPublic;
                if (mname == name) {
                    const int arity = (int)md.decl.params.size();
                    if (cls.ctors.count(arity))
                        throw ScriptError(name + " already has a constructor that takes " + std::to_string(arity) +
                                          (arity == 1 ? " value" : " values"), Peek().line);
                    cls.ctors.emplace(arity, std::move(md));
                } else {
                    if (cls.methods.count(mname))
                        throw ScriptError(name + " already has a method named '" + mname + "'", Peek().line);
                    cls.methods.emplace(mname, std::move(md));
                }
                continue;
            }
            if (Match(Tok::Var) || IsTypedVarDeclAhead()) {
                const unsigned char kind = Check(Tok::Ident) ? flags : KAny;
                const int declLine = Peek().line;
                std::string fname = ConsumeIdentRun("field name");
                SkipAutoPropertyBody();
                ExprPtr init;
                int initCol = Peek().column;
                if (Match(Tok::Assign)) { initCol = Peek().column; init = ParseExpression(); }
                CheckLiteralInit(kind, fname, init.get(), declLine, initCol);
                addField(std::move(fname), kind, isPublic, std::move(init));
                while (Match(Tok::Comma)) {
                    std::string n2 = Expect(Tok::Ident, "field name").text;
                    ExprPtr i2;
                    int col2 = Peek().column;
                    if (Match(Tok::Assign)) { col2 = Peek().column; i2 = ParseExpression(); }
                    CheckLiteralInit(kind, n2, i2.get(), declLine, col2);
                    addField(std::move(n2), kind, isPublic, std::move(i2));
                }
                Match(Tok::Semicolon);
                continue;
            }
            throw ScriptError("this isn't a field or a method of " + name, Peek().line);
        }
        Expect(Tok::RBrace, "'}'");
        Match(Tok::Semicolon);
        m_classes.emplace(name, std::move(cls));
    }

    // Blocks, statements and lambdas nest by recursion: cap it so thousands of them are a parse error, not a stack
    // overflow (the editor parses on every key press).
    static constexpr int kMaxStatementNesting = 200;
    struct StmtNestGuard {
        int& n;
        StmtNestGuard(int& d, const ParserImpl& p) : n(d) {
            if (n >= kMaxStatementNesting)
                throw ScriptError("parse error: blocks nested too deeply (more than " + std::to_string(kMaxStatementNesting) +
                                  " levels)", p.Peek().line, p.Peek().column);
            ++n;
        }
        ~StmtNestGuard() { --n; }
    };
    StmtPtr ParseStatement() {
        StmtNestGuard guard(m_stmtNesting, *this);
        const int line = Peek().line;
        StmtPtr s = ParseStatementInner();
        if (s && s->line == 0) s->line = line;
        return s;
    }
    StmtPtr ParseStatementInner() {
        // yield return <expr>; / yield break;  (coroutines)
        if (Check(Tok::Ident) && Peek().text == "yield") {
            ++m_pos;
            if (Match(Tok::Break)) { Match(Tok::Semicolon); return std::make_unique<ReturnStmt>(nullptr); }
            Expect(Tok::Return, "'return' after 'yield'");
            auto st = std::make_unique<YieldStmt>();
            if (!Check(Tok::Semicolon) && !Check(Tok::RBrace)) st->e = ParseExpression();
            Match(Tok::Semicolon);
            return st;
        }
        // throw <expr>;  — raise an error caught by an enclosing try/catch.
        if (Check(Tok::Ident) && Peek().text == "throw") {
            ++m_pos;
            ExprPtr e = Check(Tok::Semicolon) ? nullptr : ParseExpression();
            Match(Tok::Semicolon);
            return std::make_unique<ThrowStmt>(std::move(e));
        }
        // try { ... } catch (e) { ... }  (catch clause optional)
        if (Check(Tok::Ident) && Peek().text == "try") {
            ++m_pos;
            auto st = std::make_unique<TryStmt>();
            st->body = ParseBlock();
            if (Check(Tok::Ident) && Peek().text == "catch") {
                ++m_pos;
                st->hasCatch = true;
                if (Match(Tok::LParen)) {            // optional `(e)` binding
                    Match(Tok::Var);
                    if (Check(Tok::Ident)) {
                        st->catchVar = m_toks[m_pos++].text;
                        if (Check(Tok::Ident)) st->catchVar = m_toks[m_pos++].text;  // typed
                    }
                    Expect(Tok::RParen, "')'");
                }
                st->catchB = ParseBlock();
            }
            return st;
        }
        // C#-style: foreach (var item in collection) { ... }
        if (Check(Tok::Ident) && Peek().text == "foreach") {
            ++m_pos;
            Expect(Tok::LParen, "'('");
            Match(Tok::Var);
            const bool kvType = Check(Tok::Ident) && Peek().text == "KeyValuePair";
            std::string var = ConsumeIdentRun("loop variable");   // `var x`, `int x`, `KeyValuePair<K, V> kv`
            Expect(Tok::In, "'in'");
            auto fe = std::make_unique<ForEachStmt>();
            fe->var = var;
            fe->iterable = ParseExpression();
            Expect(Tok::RParen, "')'");
            // Over a dictionary, items are {Key, Value} pairs when the loop says so
            // (KeyValuePair<K, V> kv, or kv.Key / kv.Value in the body); otherwise
            // the keys, as OkayScript always did.
            const std::size_t bodyStart = m_pos;
            fe->body = ParseBlockOrStmt();
            fe->pairs = kvType;
            for (std::size_t i = bodyStart; i < m_pos && !fe->pairs; ++i)
                if (m_toks[i].type == Tok::Ident && (m_toks[i].text.rfind(var + ".Key", 0) == 0 || m_toks[i].text.rfind(var + ".Value", 0) == 0))
                    fe->pairs = true;
            return fe;
        }
        // do { body } while (cond);
        if (Check(Tok::Ident) && Peek().text == "do") {
            ++m_pos;
            auto st = std::make_unique<DoWhileStmt>();
            st->body = ParseBlock();
            if (!(Check(Tok::While) || (Check(Tok::Ident) && Peek().text == "while")))
                throw ScriptError("parse error: expected 'while' after do-block", Peek().line, Peek().column);
            ++m_pos;   // 'while'
            Expect(Tok::LParen, "'('");
            st->cond = ParseExpression();
            Expect(Tok::RParen, "')'");
            Match(Tok::Semicolon);
            return st;
        }
        // C#-style: switch (expr) { case A: ...; break; default: ...; }
        if (Check(Tok::Ident) && Peek().text == "switch") {
            ++m_pos;
            Expect(Tok::LParen, "'('");
            auto sw = std::make_unique<SwitchStmt>();
            sw->subject = ParseExpression();
            Expect(Tok::RParen, "')'");
            Expect(Tok::LBrace, "'{'");
            while (!Check(Tok::RBrace) && !Check(Tok::End)) {
                SwitchStmt::Case c;
                if (Check(Tok::Ident) && Peek().text == "case") {
                    ++m_pos; c.match = ParseExpression(); Expect(Tok::Colon, "':'");
                } else if (Check(Tok::Ident) && Peek().text == "default") {
                    ++m_pos; c.isDefault = true; Expect(Tok::Colon, "':'");
                } else {
                    throw ScriptError("parse error: expected 'case' or 'default' in switch", Peek().line, Peek().column);
                }
                while (!Check(Tok::RBrace) && !Check(Tok::End) &&
                       !(Check(Tok::Ident) && (Peek().text == "case" || Peek().text == "default")))
                    c.body.push_back(ParseStatement());
                sw->cases.push_back(std::move(c));
            }
            Expect(Tok::RBrace, "'}'");
            return sw;
        }
        const bool isVar = Match(Tok::Var);
        if (isVar || IsTypedVarDeclAhead()) {
            // `int x`, `const float y`, `public string name`: the type and const-ness
            // are kept (they're enforced at runtime); the name is the last identifier.
            const unsigned char kind = isVar ? KAny : DeclKindAhead();
            const int declLine = Peek().line;
            std::string name = ConsumeIdentRun("variable name");
            SkipAutoPropertyBody();   // public int Lives { get; set; } = 3;
            ExprPtr init;
            int initCol = Peek().column;
            if (Match(Tok::Assign)) { initCol = Peek().column; init = ParseExpression(); }
            CheckLiteralInit(kind, name, init.get(), declLine, initCol);
            if (!Check(Tok::Comma)) { Match(Tok::Semicolon); return std::make_unique<VarDeclStmt>(name, std::move(init), kind); }
            auto multi = std::make_unique<MultiStmt>();   // int a = 1, b = 2;
            multi->list.push_back(std::make_unique<VarDeclStmt>(name, std::move(init), kind));
            while (Match(Tok::Comma)) {
                std::string n2 = Expect(Tok::Ident, "variable name").text;
                ExprPtr i2;
                int col2 = Peek().column;
                if (Match(Tok::Assign)) { col2 = Peek().column; i2 = ParseExpression(); }
                CheckLiteralInit(kind, n2, i2.get(), declLine, col2);
                multi->list.push_back(std::make_unique<VarDeclStmt>(n2, std::move(i2), kind));
            }
            Match(Tok::Semicolon);
            return multi;
        }
        if (Match(Tok::If)) {
            Expect(Tok::LParen, "'('");
            auto cond = ParseExpression();
            Expect(Tok::RParen, "')'");
            auto st = std::make_unique<IfStmt>();
            st->cond = std::move(cond);
            st->thenB = ParseBlockOrStmt();
            if (Match(Tok::Else)) st->elseB = Check(Tok::If) ? SingleStmt() : ParseBlockOrStmt();
            return st;
        }
        if (Match(Tok::While)) {
            Expect(Tok::LParen, "'('");
            auto cond = ParseExpression();
            Expect(Tok::RParen, "')'");
            auto st = std::make_unique<WhileStmt>();
            st->cond = std::move(cond);
            st->body = ParseBlockOrStmt();
            return st;
        }
        if (Match(Tok::For)) {
            // foreach form: for x in <array> { ... }
            if (Check(Tok::Ident)) {
                std::size_t save = m_pos;
                std::string var = m_toks[m_pos++].text;
                if (Match(Tok::In)) {
                    auto fe = std::make_unique<ForEachStmt>();
                    fe->var = var;
                    fe->iterable = ParseExpression();
                    fe->body = ParseBlockOrStmt();
                    return fe;
                }
                m_pos = save; // not foreach; fall back to C-style for
            }
            Expect(Tok::LParen, "'('");
            auto st = std::make_unique<ForStmt>();
            // init: a var declaration (var or typed), an expression, or empty.
            if (!Match(Tok::Semicolon)) {
                if (Match(Tok::Var) || IsTypedVarDeclAhead()) {
                    std::string name = ConsumeIdentRun("variable name");
                    ExprPtr in;
                    if (Match(Tok::Assign)) in = ParseExpression();
                    st->init = std::make_unique<VarDeclStmt>(name, std::move(in));
                } else {
                    st->init = std::make_unique<ExprStmt>(ParseExpression());
                }
                Expect(Tok::Semicolon, "';'");
            }
            // condition (optional).
            if (!Check(Tok::Semicolon)) st->cond = ParseExpression();
            Expect(Tok::Semicolon, "';'");
            // step (optional).
            if (!Check(Tok::RParen)) st->step = ParseExpression();
            Expect(Tok::RParen, "')'");
            st->body = ParseBlockOrStmt();
            return st;
        }
        if (Match(Tok::Return)) {
            ExprPtr e;
            const int retLine = Prev().line, retCol = Prev().column;
            if (!Check(Tok::Semicolon) && !Check(Tok::RBrace)) e = ParseExpression();
            if (e && m_inVoid)
                m_semantic.push_back({retLine, m_fnName + "() is void, so it can't return a value. "
                                               "Give it a return type (int, float, bool or string) or just write `return;`", 0, retCol});
            Match(Tok::Semicolon);
            return std::make_unique<ReturnStmt>(std::move(e));
        }
        if (Match(Tok::Break))    { Match(Tok::Semicolon); return std::make_unique<BreakStmt>(); }
        if (Match(Tok::Continue)) { Match(Tok::Semicolon); return std::make_unique<ContinueStmt>(); }
        auto e = ParseExpression();
        Match(Tok::Semicolon);
        return std::make_unique<ExprStmt>(std::move(e));
    }

    std::vector<StmtPtr> SingleStmt() {
        std::vector<StmtPtr> v;
        v.push_back(ParseStatement());
        return v;
    }

    // Expression grammar with precedence climbing.
    ExprPtr ParseExpression() {
        if (m_nesting == 0) m_operators = 0;   // a new top-level expression
        return ParseAssignment();
    }

    // Ternary: cond ? a : b (lower precedence than assignment's RHS).
    ExprPtr ParseTernary() {
        ExprPtr c = ParseCoalesce();
        if (Match(Tok::Question)) {
            ExprPtr a = ParseAssignment();
            Expect(Tok::Colon, "':'");
            ExprPtr b = ParseAssignment();
            return std::make_unique<TernaryExpr>(std::move(c), std::move(a), std::move(b));
        }
        return c;
    }
    // `x =>`, `(x) =>`, `(int a, int b) =>`, `() =>` ?
    bool IsLambdaAhead() const {
        if (Check(Tok::Ident) && m_toks[m_pos + 1].type == Tok::Arrow) return true;
        if (!Check(Tok::LParen)) return false;
        int depth = 0;
        for (std::size_t i = m_pos; i < m_toks.size(); ++i) {
            if (m_toks[i].type == Tok::LParen) ++depth;
            else if (m_toks[i].type == Tok::RParen) {
                if (--depth == 0) return i + 1 < m_toks.size() && m_toks[i + 1].type == Tok::Arrow;
            }
        }
        return false;
    }
    ExprPtr ParseLambda() {
        StmtNestGuard guard(m_stmtNesting, *this);
        auto lam = std::make_unique<LambdaExpr>();
        lam->id = g_nextLambda++;
        if (Check(Tok::Ident)) lam->params.push_back(m_toks[m_pos++].text);
        else {
            Expect(Tok::LParen, "'('");
            bool sawDefault = false;
            while (!Check(Tok::RParen)) {
                std::string p = ConsumeIdentRun("parameter");
                ExprPtr def;
                if (Match(Tok::Assign)) { def = ParseExpression(); sawDefault = true; }
                else if (sawDefault)
                    throw ScriptError("parameter '" + p + "' needs a default too (ones with defaults come last)", Peek().line);
                lam->params.push_back(std::move(p));
                lam->defaults.push_back(std::move(def));
                if (!Match(Tok::Comma)) break;
            }
            Expect(Tok::RParen, "')'");
        }
        Expect(Tok::Arrow, "'=>'");
        const bool prevVoid = m_inVoid; m_inVoid = false;   // a lambda may return a value
        if (Check(Tok::LBrace)) lam->body = ParseBlock();
        else lam->body.push_back(std::make_unique<ReturnStmt>(ParseExpression()));
        m_inVoid = prevVoid;
        return lam;
    }
    ExprPtr ParseAssignment() {
        if (IsLambdaAhead()) return ParseLambda();
        ExprPtr left = ParseTernary();
        if (Match(Tok::QuestionQuestionEq)) {
            if (auto* var = dynamic_cast<VarExpr*>(left.get()))
                return std::make_unique<NullishAssignExpr>(var->n, ParseAssignment());
            throw ScriptError("invalid ??=", Peek().line);
        }
        if (Match(Tok::Assign)) {
            ExprPtr value = ParseAssignment();
            if (auto* var = dynamic_cast<VarExpr*>(left.get()))
                return std::make_unique<AssignExpr>(var->n, std::move(value));
            // Assigning a member of an expression: GetComponent<SpriteRenderer>().color = c.
            if (dynamic_cast<MemberExpr*>(left.get())) {
                auto* me = static_cast<MemberExpr*>(left.release());
                auto out = std::make_unique<MemberAssignExpr>(std::move(me->obj), me->name, std::move(value));
                delete me;
                return out;
            }
            // Assigning to an array element: arr[i] = value.
            if (dynamic_cast<IndexExpr*>(left.get())) {
                auto* ix = static_cast<IndexExpr*>(left.release());
                auto out = std::make_unique<IndexAssignExpr>(
                    std::move(ix->arr), std::move(ix->index), std::move(value));
                delete ix;
                return out;
            }
            throw ScriptError("invalid assignment target", Peek().line, Peek().column);
        }
        // Compound assignment: x += e  desugars to  x = x + e. Also works on an
        // element target: arr[i] += e / map["k"] += e.
        if (Check(Tok::PlusEq) || Check(Tok::MinusEq) ||
            Check(Tok::StarEq) || Check(Tok::SlashEq) || Check(Tok::PercentEq)) {
            Tok t = Peek().type; ++m_pos;
            Tok bin = t == Tok::PlusEq ? Tok::Plus : t == Tok::MinusEq ? Tok::Minus
                    : t == Tok::StarEq ? Tok::Star : t == Tok::SlashEq ? Tok::Slash : Tok::Percent;
            ExprPtr value = ParseAssignment();
            if (auto* var = dynamic_cast<VarExpr*>(left.get())) {
                std::string name = var->n;
                // += / -= on a name may subscribe a function. A number still adds.
                if (t == Tok::PlusEq || t == Tok::MinusEq)
                    return std::make_unique<EventOrCompoundExpr>(
                        name, std::move(value), t == Tok::MinusEq, bin);
                auto combined = std::make_unique<BinaryExpr>(
                    bin, std::make_unique<VarExpr>(name), std::move(value));
                return std::make_unique<AssignExpr>(name, std::move(combined));
            }
            if (dynamic_cast<IndexExpr*>(left.get())) {
                auto* ix = static_cast<IndexExpr*>(left.release());
                auto out = std::make_unique<IndexCompoundAssignExpr>(
                    std::move(ix->arr), std::move(ix->index), bin, std::move(value));
                delete ix;
                return out;
            }
            throw ScriptError("invalid assignment target", Peek().line, Peek().column);
        }
        // Postfix increment/decrement: x++  ->  x = x + 1  (also x--). Works on an
        // element target too: arr[i]++ / map["k"]--.
        if (Check(Tok::Inc) || Check(Tok::Dec)) {
            Tok bin = Peek().type == Tok::Inc ? Tok::Plus : Tok::Minus; ++m_pos;
            if (auto* var = dynamic_cast<VarExpr*>(left.get())) {
                std::string name = var->n;
                auto combined = std::make_unique<BinaryExpr>(
                    bin, std::make_unique<VarExpr>(name), std::make_unique<NumberExpr>(1.0));
                return std::make_unique<AssignExpr>(name, std::move(combined));
            }
            if (dynamic_cast<IndexExpr*>(left.get())) {
                auto* ix = static_cast<IndexExpr*>(left.release());
                auto out = std::make_unique<IndexCompoundAssignExpr>(
                    std::move(ix->arr), std::move(ix->index), bin, std::make_unique<NumberExpr>(1.0));
                delete ix;
                return out;
            }
            throw ScriptError("invalid increment target", Peek().line, Peek().column);
        }
        return left;
    }
    // a + b + c ... parses in a loop, but builds a tree as deep as it is long that
    // running and freeing it walk recursively: cap the operators in one expression.
    static constexpr int kMaxOperators = 1000;
    void CountOperator() {
        if (++m_operators > kMaxOperators)
            throw ScriptError("parse error: expression is too long (more than " + std::to_string(kMaxOperators) + " operators)", Peek().line, Peek().column);
    }
    // a ?? b ?? c groups to the right, below || and above ?:.
    ExprPtr ParseCoalesce() {
        ExprPtr l = ParseOr();
        if (Match(Tok::QuestionQuestion)) {
            CountOperator();
            l = std::make_unique<CoalesceExpr>(std::move(l), ParseCoalesce());
        }
        return l;
    }
    ExprPtr ParseOr() {
        auto l = ParseAnd();
        while (Match(Tok::Or)) { CountOperator(); l = std::make_unique<LogicalExpr>(Tok::Or, std::move(l), ParseAnd()); }
        return l;
    }
    ExprPtr ParseAnd() {
        auto l = ParseBitOr();
        while (Match(Tok::And)) { CountOperator(); l = std::make_unique<LogicalExpr>(Tok::And, std::move(l), ParseBitOr()); }
        return l;
    }
    ExprPtr ParseBitOr() {
        auto l = ParseBitXor();
        while (Match(Tok::BitOr)) { CountOperator(); l = std::make_unique<BinaryExpr>(Tok::BitOr, std::move(l), ParseBitXor()); }
        return l;
    }
    ExprPtr ParseBitXor() {
        auto l = ParseBitAnd();
        while (Match(Tok::BitXor)) { CountOperator(); l = std::make_unique<BinaryExpr>(Tok::BitXor, std::move(l), ParseBitAnd()); }
        return l;
    }
    ExprPtr ParseBitAnd() {
        auto l = ParseEquality();
        while (Match(Tok::BitAnd)) { CountOperator(); l = std::make_unique<BinaryExpr>(Tok::BitAnd, std::move(l), ParseEquality()); }
        return l;
    }
    ExprPtr ParseEquality() {
        auto l = ParseComparison();
        while (Check(Tok::Eq) || Check(Tok::Ne)) { Tok op = Peek().type; ++m_pos; CountOperator(); l = std::make_unique<BinaryExpr>(op, std::move(l), ParseComparison()); }
        return l;
    }
    ExprPtr ParseComparison() {
        auto l = ParseTerm();
        while (Check(Tok::Lt) || Check(Tok::Gt) || Check(Tok::Le) || Check(Tok::Ge)) { Tok op = Peek().type; ++m_pos; CountOperator(); l = std::make_unique<BinaryExpr>(op, std::move(l), ParseTerm()); }
        return l;
    }
    ExprPtr ParseTerm() {
        auto l = ParseFactor();
        while (Check(Tok::Plus) || Check(Tok::Minus)) { Tok op = Peek().type; ++m_pos; CountOperator(); l = std::make_unique<BinaryExpr>(op, std::move(l), ParseFactor()); }
        return l;
    }
    ExprPtr ParseFactor() {
        auto l = ParseUnary();
        while (Check(Tok::Star) || Check(Tok::Slash) || Check(Tok::Percent)) { Tok op = Peek().type; ++m_pos; CountOperator(); l = std::make_unique<BinaryExpr>(op, std::move(l), ParseUnary()); }
        return l;
    }
    ExprPtr ParseUnary() {
        // Every nested expression ('(', '-', '!', ...) comes through here: cap the
        // depth so thousands of them are a parse error, not a stack overflow.
        if (m_nesting >= 256) throw ScriptError("parse error: expression nested too deeply", Peek().line, Peek().column);
        struct NestGuard { int& n; explicit NestGuard(int& d) : n(d) { ++n; } ~NestGuard() { --n; } } guard(m_nesting);
        if (Check(Tok::Minus) || Check(Tok::Not)) { Tok op = Peek().type; ++m_pos; return std::make_unique<UnaryExpr>(op, ParseUnary()); }
        // Prefix ++x / --x  ->  x = x + 1 / x = x - 1.
        if (Check(Tok::Inc) || Check(Tok::Dec)) {
            Tok bin = Peek().type == Tok::Inc ? Tok::Plus : Tok::Minus; ++m_pos;
            ExprPtr target = ParseUnary();
            auto* var = dynamic_cast<VarExpr*>(target.get());
            if (!var) throw ScriptError("invalid increment target", Peek().line, Peek().column);
            std::string name = var->n;
            auto combined = std::make_unique<BinaryExpr>(
                bin, std::make_unique<VarExpr>(name), std::make_unique<NumberExpr>(1.0));
            return std::make_unique<AssignExpr>(name, std::move(combined));
        }
        return ParsePostfix();
    }
    // Postfix indexing: base[i][j]...
    // One `.name` or `.name(args)` step. `safe` skips the step when the receiver is null.
    ExprPtr ParseMemberStep(ExprPtr e, bool safe) {
        std::string path = Expect(Tok::Ident, "member name").text;
        std::vector<std::string> parts;
        for (std::size_t a = 0, b; a <= path.size(); a = b + 1) {
            b = path.find('.', a); if (b == std::string::npos) b = path.size();
            parts.push_back(path.substr(a, b - a));
        }
        for (std::size_t k = 0; k + 1 < parts.size(); ++k)
            e = std::make_unique<MemberExpr>(std::move(e), parts[k], safe);
        if (Match(Tok::LParen)) {
            auto call = std::make_unique<MethodCallExpr>(std::move(e), parts.back(), safe);
            if (!Check(Tok::RParen)) {
                do { call->args.push_back(ParseExpression()); } while (Match(Tok::Comma));
            }
            Expect(Tok::RParen, "')'");
            return call;
        }
        return std::make_unique<MemberExpr>(std::move(e), parts.back(), safe);
    }
    ExprPtr ParsePostfix() {
        ExprPtr e = ParsePrimary();
        // After `?.`, the rest of this chain is skipped when a step is null.
        bool safe = false;
        for (;;) {
            if (Match(Tok::LBracket)) {
                ExprPtr idx = ParseExpression();
                Expect(Tok::RBracket, "']'");
                e = std::make_unique<IndexExpr>(std::move(e), std::move(idx), safe);
            } else if (Match(Tok::QuestionDot)) {
                safe = true;
                e = ParseMemberStep(std::move(e), true);
            } else if (Match(Tok::Dot)) {
                e = ParseMemberStep(std::move(e), safe);
            } else break;
        }
        return e;
    }
    // `{ a, b, c }` — a C# collection / array initializer (nested for dictionaries).
    ExprPtr ParseBraceList() {
        Expect(Tok::LBrace, "'{'");
        auto arr = std::make_unique<ArrayExpr>();
        while (!Check(Tok::RBrace) && !Check(Tok::End)) {
            arr->elems.push_back(ParseExpression());
            if (!Match(Tok::Comma)) break;
        }
        Expect(Tok::RBrace, "'}'");
        return arr;
    }
    // After `new`: arrays, lists, dictionaries, or a constructor call (new Vector3(...)).
    ExprPtr ParseNew() {
        if (Check(Tok::LBracket)) {                        // new[] { 1, 2, 3 }
            m_pos = SkipTypeSuffix(m_pos);
            return Check(Tok::LBrace) ? ParseBraceList() : std::make_unique<ArrayExpr>();
        }
        const std::size_t typeTok = m_pos;
        std::string type = Expect(Tok::Ident, "type after 'new'").text;
        const auto dotp = type.rfind('.');
        const std::string base = dotp == std::string::npos ? type : type.substr(dotp + 1);
        if (Check(Tok::Lt)) m_pos = SkipTypeSuffix(m_pos);   // generic arguments
        if (Match(Tok::LBracket)) {                         // new int[n] / new int[] { ... }
            ExprPtr size;
            if (!Check(Tok::RBracket)) size = ParseExpression();
            while (Match(Tok::Comma)) ParseExpression();
            Expect(Tok::RBracket, "']'");
            if (Check(Tok::LBrace)) return ParseBraceList();
            auto call = std::make_unique<CallExpr>("__new_array");
            if (size) call->args.push_back(std::move(size));
            return call;
        }
        static const std::set<std::string> kLists = {"List", "HashSet", "Queue", "Stack", "LinkedList", "ArrayList", "SortedSet"};
        static const std::set<std::string> kMaps  = {"Dictionary", "SortedDictionary", "Hashtable", "SortedList"};
        if (kLists.count(base) || kMaps.count(base)) {
            ExprPtr from;                                    // new List<int>(other) copies
            if (Match(Tok::LParen)) {
                if (!Check(Tok::RParen)) from = ParseExpression();
                while (Match(Tok::Comma)) ParseExpression();
                Expect(Tok::RParen, "')'");
            }
            ExprPtr init = Check(Tok::LBrace) ? ParseBraceList() : nullptr;
            auto call = std::make_unique<CallExpr>(kMaps.count(base) ? "__new_map" : "__new_list");
            if (init) call->args.push_back(std::move(init));
            else if (from) call->args.push_back(std::move(from));
            return call;
        }
        if (m_userClasses.count(base)) {                 // new Item("Potion") / new Item()
            auto made = std::make_unique<NewExpr>(base);
            if (Match(Tok::LParen)) {
                if (!Check(Tok::RParen)) {
                    do { made->args.push_back(ParseExpression()); } while (Match(Tok::Comma));
                }
                Expect(Tok::RParen, "')'");
            }
            return made;
        }
        if (m_pos == typeTok + 1) { m_pos = typeTok; return ParsePrimary(); }   // new Vector3(1, 2, 3)
        // Some other generic type: build a call named after it.
        auto call = std::make_unique<CallExpr>(type);
        if (Match(Tok::LParen)) {
            if (!Check(Tok::RParen)) { do { call->args.push_back(ParseExpression()); } while (Match(Tok::Comma)); }
            Expect(Tok::RParen, "')'");
        }
        return call;
    }
    // Split an interpolated-string template into literal + {expression} parts,
    // compiling each embedded expression with a sub-parser. `{{`/`}}` are literals.
    ExprPtr BuildInterp(const std::string& tpl) {
        auto node = std::make_unique<InterpStringExpr>();
        std::string lit;
        for (std::size_t i = 0; i < tpl.size(); ++i) {
            char c = tpl[i];
            if (c == '{' && i + 1 < tpl.size() && tpl[i + 1] == '{') { lit += '{'; ++i; continue; }
            if (c == '}' && i + 1 < tpl.size() && tpl[i + 1] == '}') { lit += '}'; ++i; continue; }
            if (c == '{') {
                node->AddLiteral(lit); lit.clear();
                std::string expr; int depth = 1; ++i;
                for (; i < tpl.size(); ++i) {
                    if (tpl[i] == '{') ++depth;
                    else if (tpl[i] == '}') { if (--depth == 0) break; }
                    expr += tpl[i];
                }
                Lexer lx(expr);
                std::vector<Token> sub = lx.Scan();
                for (Token& tk : sub) tk.line += Prev().line - 1;   // lines inside {} count from the string's line
                ParserImpl p(std::move(sub));
                node->AddExpr(p.ParseExpression());
            } else {
                lit += c;
            }
        }
        node->AddLiteral(lit);
        return node;
    }

    // `Heal(5)` or `Heal(bonus: 4, hp: 1)`. A name is a bare identifier followed by ':'.
    void ParseCallArguments(std::vector<ExprPtr>& args, std::vector<std::string>& names) {
        if (Check(Tok::RParen)) return;
        do {
            std::string nm;
            if (Check(Tok::Ident) && m_pos + 1 < m_toks.size() && m_toks[m_pos + 1].type == Tok::Colon) {
                nm = m_toks[m_pos].text;
                m_pos += 2;
            }
            names.push_back(std::move(nm));
            args.push_back(ParseExpression());
        } while (Match(Tok::Comma));
    }

    ExprPtr ParsePrimary() {
        if (Match(Tok::Number)) return std::make_unique<NumberExpr>(Prev().number);
        if (Match(Tok::String)) return std::make_unique<StringExpr>(Prev().text);
        if (Match(Tok::InterpString)) return BuildInterp(Prev().text);
        if (Match(Tok::True))   return std::make_unique<BoolExpr>(true);
        if (Match(Tok::False))  return std::make_unique<BoolExpr>(false);
        // C# cast: (int)x, (float)a / b, (string)v.
        if (Check(Tok::LParen) && m_toks[m_pos + 1].type == Tok::Ident && m_toks[m_pos + 2].type == Tok::RParen) {
            static const std::set<std::string> kCastTypes = {
                "int", "float", "double", "long", "short", "byte", "uint", "ulong", "ushort", "sbyte",
                "decimal", "string", "bool", "char"};
            const std::string& ty = m_toks[m_pos + 1].text;
            const Tok after = m_toks[m_pos + 3].type;
            const bool operand = after == Tok::Ident || after == Tok::Number || after == Tok::LParen ||
                                 after == Tok::String || after == Tok::Minus || after == Tok::Not;
            if (kCastTypes.count(ty) && operand) {
                m_pos += 3;
                return std::make_unique<CastExpr>(ty, ParseUnary());
            }
            if (m_enums.count(ty) && operand) { m_pos += 3; return ParseUnary(); }   // (State)2
        }
        if (Match(Tok::LParen)) { auto e = ParseExpression(); Expect(Tok::RParen, "')'"); return e; }
        if (Check(Tok::LBrace)) return ParseBraceList();   // int[] a = { 1, 2, 3 };
        if (Match(Tok::LBracket)) { // array literal [a, b, c]
            auto arr = std::make_unique<ArrayExpr>();
            if (!Check(Tok::RBracket)) {
                do { arr->elems.push_back(ParseExpression()); } while (Match(Tok::Comma));
            }
            Expect(Tok::RBracket, "']'");
            return arr;
        }
        if (Check(Tok::Ident)) {
            std::string name = m_toks[m_pos++].text;
            // C#-style `new Vector3(...)`, `new List<int>()`, `new int[] { 1, 2 }`.
            if (name == "new") return ParseNew();
            // Generic call `GetComponent<Type>(args)` — pass the type name as the
            // first argument so builtins can dispatch on it. (Bounds-checked: the
            // token stream has a single End sentinel, so peeking +3 can run off.)
            if (Check(Tok::Lt) && m_pos + 3 < m_toks.size() &&
                m_toks[m_pos + 1].type == Tok::Ident &&
                m_toks[m_pos + 2].type == Tok::Gt && m_toks[m_pos + 3].type == Tok::LParen) {
                std::string typeName = m_toks[m_pos + 1].text;
                m_pos += 4;   // consume  < Type > (
                auto call = std::make_unique<CallExpr>(name); call->line = Peek().line;
                call->args.push_back(std::make_unique<StringExpr>(typeName));
                if (!Check(Tok::RParen)) {
                    do { call->args.push_back(ParseExpression()); } while (Match(Tok::Comma));
                }
                Expect(Tok::RParen, "')'");
                return call;
            }
            if (Match(Tok::LParen)) {
                auto call = std::make_unique<CallExpr>(name); call->line = Peek().line;
                ParseCallArguments(call->args, call->argNames);
                Expect(Tok::RParen, "')'");
                // StartCoroutine(Blink(0.2f)) names the routine instead of calling it now.
                // Named arguments inside it (`Blink(delay: 0.2)`) travel with the values.
                if ((name == "StartCoroutine" || name == "StopCoroutine") && call->args.size() == 1)
                    if (auto* inner = dynamic_cast<CallExpr*>(call->args[0].get())) {
                        auto co = std::make_unique<CallExpr>(name); co->line = call->line;
                        co->args.push_back(std::make_unique<StringExpr>(inner->name));
                        co->argNames.push_back("");
                        for (std::size_t i = 0; i < inner->args.size(); ++i) {
                            co->args.push_back(std::move(inner->args[i]));
                            co->argNames.push_back(i < inner->argNames.size() ? inner->argNames[i] : "");
                        }
                        return co;
                    }
                return call;
            }
            if (auto it = m_enumValues.find(name); it != m_enumValues.end())
                return std::make_unique<NumberExpr>(it->second);   // State.Run -> 1
            return std::make_unique<VarExpr>(name);
        }
        throw ScriptError("parse error: unexpected " + GotText(), Peek().line, Peek().column);
    }

    std::vector<Token> m_toks;
    std::size_t m_pos = 0;
    int m_nesting = 0;                                      // ParseUnary recursion depth
    int m_stmtNesting = 0;                                  // statement / block / lambda recursion depth
    int m_operators = 0;                                    // binary operators in this expression
    std::set<std::string> m_enums;                          // enum type names
    std::set<std::string> m_userClasses;                    // plain classes (no base), from the prepass
    std::unordered_map<std::string, UserClass> m_classes;
    std::vector<Diagnostic> m_semantic;               // type mistakes found while parsing
    bool m_inVoid = false;                                  // parsing the body of a `void` function
    std::string m_fnName;
    std::unordered_map<std::string, double> m_enumValues;   // "State.Run" -> 1
};


Parser::Parser(std::vector<Token> toks) : m_impl(std::make_unique<ParserImpl>(std::move(toks))) {}
Parser::~Parser() = default;
void Parser::ParseProgram(Program& out) {
    std::vector<StmtPtr> top = m_impl->ParseProgram(out.functions);
    for (auto& s : top) out.top.push_back(std::move(s));
    for (auto& kv : m_impl->TakeClasses()) out.classes[kv.first] = std::move(kv.second);
    CollectLocalNames(out);
}
std::vector<Diagnostic> Parser::ParseProgramRecovering() { return m_impl->ParseProgramRecovering(); }
ExprPtr Parser::ParseExpression() { return m_impl->ParseExpression(); }
bool Parser::AtEnd() const { return m_impl->AtEnd(); }

void ParseSource(const std::string& source, Program& out) {
    std::vector<Token> toks;
    try {
        Lexer lx(source);
        toks = lx.Scan();
    } catch (const ScriptError& e) {
        throw SyntaxError(e);
    } catch (const std::exception& e) {   // a number too large for a double
        throw SyntaxError(std::string("bad number: ") + e.what(), 0);
    }
    Parser p(std::move(toks));
    try {
        p.ParseProgram(out);
    } catch (const SyntaxError&) {
        throw;
    } catch (const ScriptError& e) {
        throw SyntaxError(e);
    }
}

std::vector<Diagnostic> ValidateSource(const std::string& source) {
    std::vector<Token> toks;
    try {
        Lexer lx(source);
        toks = lx.Scan();
    } catch (const ScriptError& e) {
        return {Diagnostic{e.line, e.what(), 0, e.column}};
    } catch (const std::exception& e) {
        return {Diagnostic{0, std::string("bad number: ") + e.what(), 0, 0}};
    }
    Parser p(std::move(toks));
    return p.ParseProgramRecovering();
}

} // namespace okayscript
