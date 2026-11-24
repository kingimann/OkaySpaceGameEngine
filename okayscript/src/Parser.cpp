// The OkayScript parser (recursive descent with one function per precedence level).
// dup-allow-begin: ported from the engine's own OkayScriptVM parser (engine/src/Scripting/OkayScriptVM.cpp, written for this project) for the standalone language; the engine moves onto this copy in the integration step (docs/status/UNFINISHED.md "OkayScript standalone core")
// Written for this project: the grammar is the engine OkayScriptVM's, without its engine-only parts (classes,
// coroutines, events, C# attributes and namespaces). Every node records its line for runtime errors.
#include "okayscript/Parser.hpp"
#include "okayscript/Error.hpp"
#include "okayscript/Lexer.hpp"
#include <atomic>

namespace okayscript {

namespace {
std::atomic<int> g_nextLambda{1};   // lambda ids are unique across every program in the process
}

Parser::Parser(std::vector<Token> toks) : m_toks(std::move(toks)) {
    if (m_toks.empty() || m_toks.back().type != Tok::End) m_toks.push_back(Token{Tok::End, "", 0, 0});
    // A few tokens of End padding: the look-ahead below peeks a few tokens past the current one.
    for (int i = 0; i < 4; ++i) m_toks.push_back(m_toks.back());
    ScanEnums();
}

bool Parser::AtEnd() const { return Check(Tok::End); }

void Parser::Fail(const std::string& msg) const {
    throw SyntaxError(msg, Line(), Peek().column);
}
const Token& Parser::Expect(Tok t, const char* what) {
    if (!Check(t)) {
        std::string got = Peek().type == Tok::Ident ? "'" + Peek().text + "'" : TokName(Peek().type);
        Fail(std::string("parse error: expected ") + what + ", got " + got);
    }
    return m_toks[m_pos++];
}

// enum State { Idle, Run = 5, Jump } anywhere in the file: State.Idle / State.Run / State.Jump become 0, 5, 6.
void Parser::ScanEnums() {
    for (std::size_t i = 0; i + 2 < m_toks.size(); ++i) {
        if (m_toks[i].type != Tok::Ident || m_toks[i].text != "enum" || m_toks[i + 1].type != Tok::Ident) continue;
        const std::string en = m_toks[i + 1].text;
        std::size_t j = i + 2;
        while (m_toks[j].type != Tok::LBrace && m_toks[j].type != Tok::End) ++j;
        if (m_toks[j].type != Tok::LBrace) continue;
        m_enums.insert(en);
        double next = 0;
        for (++j; m_toks[j].type != Tok::RBrace && m_toks[j].type != Tok::End; ++j) {
            if (m_toks[j].type != Tok::Ident) continue;
            const std::string member = m_toks[j].text;
            if (m_toks[j + 1].type == Tok::Assign) {
                const bool neg = m_toks[j + 2].type == Tok::Minus;
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
bool Parser::IsEnumAhead() const {
    std::size_t i = m_pos;
    while (m_toks[i].type == Tok::Ident) { if (m_toks[i].text == "enum") return m_toks[i + 1].type == Tok::Ident; ++i; }
    return false;
}
bool Parser::SkipEnumDecl() {
    std::size_t i = m_pos;
    while (m_toks[i].type == Tok::Ident && m_toks[i].text != "enum") ++i;
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

void Parser::ParseProgram(Program& prog) {
    while (!Check(Tok::End)) ParseMember(prog);
}

void Parser::ParseMember(Program& prog) {
    if (IsEnumAhead()) { SkipEnumDecl(); return; }
    FunctionDecl decl;
    bool isFn = true;
    if (Check(Tok::Function)) decl = ParseFunction();
    else if (IsTypedFunctionAhead()) decl = ParseTypedFunction();          // int Add(int a, int b) { }
    else if (IsBareFunctionAhead()) {                                     // add(a, b) { }
        decl.line = Line();
        decl.name = Expect(Tok::Ident, "function name").text;
        decl = ParseParamsAndBody(std::move(decl));
    } else isFn = false;
    if (!isFn) { prog.top.push_back(ParseStatement()); return; }
    if (decl.name.find('.') != std::string::npos) Fail("a function name can't contain '.': '" + decl.name + "'");
    if (prog.functions.count(decl.name))
        throw SyntaxError("function '" + decl.name + "' is defined twice", decl.line);
    const std::string name = decl.name;
    prog.functions.emplace(name, std::move(decl));
}

// `name(params) {`: an identifier, a balanced parenthesised list, then '{'. The '{' is what tells it apart from a
// call statement like `step(5)`.
bool Parser::IsBareFunctionAhead() const {
    std::size_t i = m_pos;
    if (m_toks[i].type != Tok::Ident) return false;
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

// `void Update()`, `int Add(int a, int b)`: two or more identifiers, the last followed by '('.
bool Parser::IsTypedFunctionAhead() const {
    int idents = 0;
    std::size_t i = ScanIdentRun(m_pos, idents);
    return idents >= 2 && m_toks[i].type == Tok::LParen;
}
// `int score = 0;`, `const string name;`, `List<int> xs = ...`: two or more identifiers ending in '=', ';' or ','.
bool Parser::IsTypedVarDeclAhead() const {
    int idents = 0;
    std::size_t i = ScanIdentRun(m_pos, idents);
    if (idents < 2) return false;
    const Tok t = m_toks[i].type;
    // `int n` alone on its line is a declaration too (the line ends it).
    return t == Tok::Assign || t == Tok::Semicolon || t == Tok::Comma || t == Tok::End || t == Tok::RBrace ||
           m_toks[i].line != m_toks[m_pos].line;
}
// Skip a type's generic arguments and array ranks after its name: List<int>, Dictionary<string, int>, int[].
std::size_t Parser::SkipTypeSuffix(std::size_t i) const {
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
// A declaration's words (`const int MAX`, `List<int> xs`) are on one line. The engine's parser also ran on into the
// next line, so `var c` followed by a line starting with a name took that name as the variable's (`var c` /
// `c ??= 4` failed to parse, `var a` / `b = 3` declared b): fixed here, test_interpreter "declaration words stop".
std::size_t Parser::ScanIdentRun(std::size_t i, int& idents) const {
    idents = 0;
    const int line = m_toks[i].line;
    while (m_toks[i].type == Tok::Ident && m_toks[i].line == line) { ++idents; ++i; i = SkipTypeSuffix(i); }
    return i;
}
std::string Parser::ConsumeIdentRun(const char* what) {
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
// The declared type and const-ness of the typed declaration at m_pos (`const int MAX`, `float speed`).
unsigned char Parser::DeclKindAhead() const {
    unsigned char kind = KAny, flags = 0;
    std::vector<std::pair<std::size_t, bool>> run;   // (token, has a generic / array suffix)
    for (std::size_t i = m_pos; m_toks[i].type == Tok::Ident;) {
        const std::size_t next = SkipTypeSuffix(i + 1);
        run.push_back({i, next != i + 1});
        i = next;
    }
    for (std::size_t r = 0; r + 1 < run.size(); ++r) {   // every word before the name
        const std::string& w = m_toks[run[r].first].text;
        if (w == "const" || w == "readonly") flags |= KConst;
        else if (!run[r].second && TypeKind(w)) kind = TypeKind(w);
    }
    return (unsigned char)(kind | flags);
}

FunctionDecl Parser::ParseFunction() {
    FunctionDecl decl;
    decl.line = Line();
    Expect(Tok::Function, "'function'");
    decl.name = Expect(Tok::Ident, "function name").text;
    return ParseParamsAndBody(std::move(decl));
}
FunctionDecl Parser::ParseTypedFunction() {
    FunctionDecl decl;
    decl.line = Line();
    decl.returnKind = (unsigned char)(DeclKindAhead() & 0x0F);
    decl.name = ConsumeIdentRun("type");
    return ParseParamsAndBody(std::move(decl));
}
FunctionDecl Parser::ParseParamsAndBody(FunctionDecl decl) {
    Expect(Tok::LParen, "'('");
    if (!Check(Tok::RParen)) {
        do {
            const unsigned char pk = (unsigned char)(DeclKindAhead() & 0x0F);
            const int pl = Line();
            std::string p = ConsumeIdentRun("parameter name");
            ExprPtr def;
            if (Match(Tok::Assign)) def = ParseExpression();
            else if (!decl.defaults.empty() && decl.defaults.back())
                throw SyntaxError("parameter '" + p + "' needs a default too (ones with defaults come last)", pl);
            for (const std::string& prev : decl.params)
                if (prev == p) throw SyntaxError("parameter '" + p + "' is already in this function", pl);
            decl.params.push_back(p);
            decl.paramKinds.push_back(pk);
            decl.defaults.push_back(std::move(def));
        } while (Match(Tok::Comma));
    }
    Expect(Tok::RParen, "')'");
    decl.body = ParseBlock();
    return decl;
}

StmtList Parser::ParseBlock() {
    Expect(Tok::LBrace, "'{'");
    StmtList stmts;
    while (!Check(Tok::RBrace) && !Check(Tok::End)) stmts.push_back(ParseStatement());
    Expect(Tok::RBrace, "'}'");
    return stmts;
}
// A control-flow body: a { block }, or one statement without braces (`if (x) step()`).
StmtList Parser::ParseBlockOrStmt() {
    if (Check(Tok::LBrace)) return ParseBlock();
    return SingleStmt();
}
StmtList Parser::SingleStmt() {
    StmtList v;
    v.push_back(ParseStatement());
    return v;
}

StmtPtr Parser::ParseStatement() {
    const int line = Line();
    StmtPtr s = ParseStatementInner();
    if (s && s->line == 0) s->line = line;
    return s;
}

StmtPtr Parser::ParseStatementInner() {
    if (CheckWord("throw")) {
        ++m_pos;
        ExprPtr e = (Check(Tok::Semicolon) || Check(Tok::RBrace)) ? nullptr : ParseExpression();
        Match(Tok::Semicolon);
        return std::make_unique<ThrowStmt>(std::move(e));
    }
    if (CheckWord("try")) {
        ++m_pos;
        auto st = std::make_unique<TryStmt>();
        st->body = ParseBlock();
        if (CheckWord("catch")) {
            ++m_pos;
            st->hasCatch = true;
            if (Match(Tok::LParen)) {            // optional `(e)` / `(Exception e)`
                Match(Tok::Var);
                if (Check(Tok::Ident)) {
                    st->catchVar = m_toks[m_pos++].text;
                    if (Check(Tok::Ident)) st->catchVar = m_toks[m_pos++].text;
                }
                Expect(Tok::RParen, "')'");
            }
            st->catchB = ParseBlock();
        }
        return st;
    }
    if (CheckWord("foreach")) {
        ++m_pos;
        Expect(Tok::LParen, "'('");
        Match(Tok::Var);
        const bool kvType = CheckWord("KeyValuePair");
        std::string var = ConsumeIdentRun("loop variable");
        Expect(Tok::In, "'in'");
        auto fe = std::make_unique<ForEachStmt>();
        fe->var = var;
        fe->iterable = ParseExpression();
        Expect(Tok::RParen, "')'");
        // Over a map, items are {Key, Value} pairs when the loop says so (KeyValuePair<K, V> kv, or kv.Key /
        // kv.Value in the body); otherwise its keys.
        const std::size_t bodyStart = m_pos;
        fe->body = ParseBlockOrStmt();
        fe->pairs = kvType;
        for (std::size_t i = bodyStart; i < m_pos && !fe->pairs; ++i)
            if (m_toks[i].type == Tok::Ident &&
                (m_toks[i].text.rfind(var + ".Key", 0) == 0 || m_toks[i].text.rfind(var + ".Value", 0) == 0))
                fe->pairs = true;
        return fe;
    }
    if (CheckWord("do")) {
        ++m_pos;
        auto st = std::make_unique<DoWhileStmt>();
        st->body = ParseBlock();
        if (!Match(Tok::While)) Fail("parse error: expected 'while' after do-block");
        Expect(Tok::LParen, "'('");
        st->cond = ParseExpression();
        Expect(Tok::RParen, "')'");
        Match(Tok::Semicolon);
        return st;
    }
    if (CheckWord("switch")) {
        ++m_pos;
        Expect(Tok::LParen, "'('");
        auto sw = std::make_unique<SwitchStmt>();
        sw->subject = ParseExpression();
        Expect(Tok::RParen, "')'");
        Expect(Tok::LBrace, "'{'");
        while (!Check(Tok::RBrace) && !Check(Tok::End)) {
            SwitchStmt::Case c;
            if (CheckWord("case")) { ++m_pos; c.match = ParseExpression(); Expect(Tok::Colon, "':'"); }
            else if (CheckWord("default")) { ++m_pos; c.isDefault = true; Expect(Tok::Colon, "':'"); }
            else Fail("parse error: expected 'case' or 'default' in switch");
            while (!Check(Tok::RBrace) && !Check(Tok::End) && !CheckWord("case") && !CheckWord("default"))
                c.body.push_back(ParseStatement());
            sw->cases.push_back(std::move(c));
        }
        Expect(Tok::RBrace, "'}'");
        return sw;
    }
    if (Check(Tok::LBrace)) {   // a bare { block } (no new scope, as in the engine)
        auto b = std::make_unique<BlockStmt>();
        b->body = ParseBlock();
        return b;
    }
    const bool isVar = Match(Tok::Var);
    if (isVar || IsTypedVarDeclAhead()) {
        const unsigned char kind = isVar ? (unsigned char)KAny : DeclKindAhead();
        const int declLine = Line();
        std::string name = ConsumeIdentRun("variable name");
        ExprPtr init;
        if (Match(Tok::Assign)) init = ParseExpression();
        if (!Check(Tok::Comma)) {
            Match(Tok::Semicolon);
            return At(std::make_unique<VarDeclStmt>(name, std::move(init), kind), declLine);
        }
        auto multi = std::make_unique<MultiStmt>();   // int a = 1, b = 2;
        multi->list.push_back(At(std::make_unique<VarDeclStmt>(name, std::move(init), kind), declLine));
        while (Match(Tok::Comma)) {
            const int l2 = Line();
            std::string n2 = Expect(Tok::Ident, "variable name").text;
            ExprPtr i2;
            if (Match(Tok::Assign)) i2 = ParseExpression();
            multi->list.push_back(At(std::make_unique<VarDeclStmt>(n2, std::move(i2), kind), l2));
        }
        Match(Tok::Semicolon);
        return multi;
    }
    if (Match(Tok::If)) {
        Expect(Tok::LParen, "'('");
        auto st = std::make_unique<IfStmt>();
        st->cond = ParseExpression();
        Expect(Tok::RParen, "')'");
        st->thenB = ParseBlockOrStmt();
        if (Match(Tok::Else)) st->elseB = Check(Tok::If) ? SingleStmt() : ParseBlockOrStmt();
        return st;
    }
    if (Match(Tok::While)) {
        Expect(Tok::LParen, "'('");
        auto st = std::make_unique<WhileStmt>();
        st->cond = ParseExpression();
        Expect(Tok::RParen, "')'");
        st->body = ParseBlockOrStmt();
        return st;
    }
    if (Match(Tok::For)) {
        if (Check(Tok::Ident)) {   // for x in <list> { ... }
            const std::size_t save = m_pos;
            std::string var = m_toks[m_pos++].text;
            if (Match(Tok::In)) {
                auto fe = std::make_unique<ForEachStmt>();
                fe->var = var;
                fe->iterable = ParseExpression();
                fe->body = ParseBlockOrStmt();
                return fe;
            }
            m_pos = save;
        }
        Expect(Tok::LParen, "'('");
        auto st = std::make_unique<ForStmt>();
        if (!Match(Tok::Semicolon)) {
            const int il = Line();
            if (Match(Tok::Var) || IsTypedVarDeclAhead()) {
                std::string name = ConsumeIdentRun("variable name");
                ExprPtr in;
                if (Match(Tok::Assign)) in = ParseExpression();
                st->init = At(std::make_unique<VarDeclStmt>(name, std::move(in)), il);
            } else {
                st->init = At(std::make_unique<ExprStmt>(ParseExpression()), il);
            }
            Expect(Tok::Semicolon, "';'");
        }
        if (!Check(Tok::Semicolon)) st->cond = ParseExpression();
        Expect(Tok::Semicolon, "';'");
        if (!Check(Tok::RParen)) st->step = ParseExpression();
        Expect(Tok::RParen, "')'");
        st->body = ParseBlockOrStmt();
        return st;
    }
    if (Match(Tok::Return)) {
        ExprPtr e;
        if (!Check(Tok::Semicolon) && !Check(Tok::RBrace)) e = ParseExpression();
        Match(Tok::Semicolon);
        return std::make_unique<ReturnStmt>(std::move(e));
    }
    if (Match(Tok::Break))    { Match(Tok::Semicolon); return std::make_unique<BreakStmt>(); }
    if (Match(Tok::Continue)) { Match(Tok::Semicolon); return std::make_unique<ContinueStmt>(); }
    if (Check(Tok::Function) || IsTypedFunctionAhead()) Fail("parse error: functions can only be defined at the top level");
    auto e = ParseExpression();
    Match(Tok::Semicolon);
    return std::make_unique<ExprStmt>(std::move(e));
}

ExprPtr Parser::ParseExpression() {
    if (m_nesting == 0) m_operators = 0;
    return ParseAssignment();
}

ExprPtr Parser::ParseTernary() {
    const int line = Line();
    ExprPtr c = ParseCoalesce();
    if (Match(Tok::Question)) {
        ExprPtr a = ParseAssignment();
        Expect(Tok::Colon, "':'");
        ExprPtr b = ParseAssignment();
        return At(std::make_unique<TernaryExpr>(std::move(c), std::move(a), std::move(b)), line);
    }
    return c;
}

bool Parser::IsLambdaAhead() const {
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

ExprPtr Parser::ParseLambda() {
    auto lam = std::make_unique<LambdaExpr>();
    lam->line = Line();
    lam->id = g_nextLambda++;
    if (Check(Tok::Ident)) { lam->params.push_back(m_toks[m_pos++].text); lam->defaults.push_back(nullptr); }
    else {
        Expect(Tok::LParen, "'('");
        bool sawDefault = false;
        while (!Check(Tok::RParen)) {
            std::string p = ConsumeIdentRun("parameter name");
            ExprPtr def;
            if (Match(Tok::Assign)) { def = ParseExpression(); sawDefault = true; }
            else if (sawDefault) Fail("parameter '" + p + "' needs a default too (ones with defaults come last)");
            lam->params.push_back(std::move(p));
            lam->defaults.push_back(std::move(def));
            if (!Match(Tok::Comma)) break;
        }
        Expect(Tok::RParen, "')'");
    }
    Expect(Tok::Arrow, "'=>'");
    if (Check(Tok::LBrace)) lam->body = ParseBlock();
    else {
        const int l = Line();
        lam->body.push_back(At(std::make_unique<ReturnStmt>(ParseExpression()), l));
    }
    return lam;
}

ExprPtr Parser::ParseAssignment() {
    if (IsLambdaAhead()) return ParseLambda();
    const int line = Line();
    ExprPtr left = ParseTernary();
    if (Match(Tok::QuestionQuestionEq)) {
        if (auto* var = dynamic_cast<VarExpr*>(left.get()))
            return At(std::make_unique<NullishAssignExpr>(var->n, ParseAssignment()), line);
        Fail("parse error: ?\?= needs a variable on its left");
    }
    if (Match(Tok::Assign)) {
        ExprPtr value = ParseAssignment();
        if (auto* var = dynamic_cast<VarExpr*>(left.get()))
            return At(std::make_unique<AssignExpr>(var->n, std::move(value)), line);
        if (auto* me = dynamic_cast<MemberExpr*>(left.get()))
            return At(std::make_unique<MemberAssignExpr>(std::move(me->obj), me->name, std::move(value)), line);
        if (auto* ix = dynamic_cast<IndexExpr*>(left.get()))
            return At(std::make_unique<IndexAssignExpr>(std::move(ix->arr), std::move(ix->index), std::move(value)), line);
        Fail("parse error: invalid assignment target");
    }
    // x op= e is x = x op e; also on an element: xs[i] += e.
    if (Check(Tok::PlusEq) || Check(Tok::MinusEq) || Check(Tok::StarEq) || Check(Tok::SlashEq) || Check(Tok::PercentEq)) {
        const Tok t = Peek().type; ++m_pos;
        const Tok bin = t == Tok::PlusEq ? Tok::Plus : t == Tok::MinusEq ? Tok::Minus
                      : t == Tok::StarEq ? Tok::Star : t == Tok::SlashEq ? Tok::Slash : Tok::Percent;
        ExprPtr value = ParseAssignment();
        if (auto* var = dynamic_cast<VarExpr*>(left.get())) {
            const std::string name = var->n;
            auto combined = At(std::make_unique<BinaryExpr>(bin, At(std::make_unique<VarExpr>(name), line), std::move(value)), line);
            return At(std::make_unique<AssignExpr>(name, std::move(combined)), line);
        }
        if (auto* ix = dynamic_cast<IndexExpr*>(left.get()))
            return At(std::make_unique<IndexCompoundAssignExpr>(std::move(ix->arr), std::move(ix->index), bin, std::move(value)), line);
        Fail("parse error: invalid assignment target");
    }
    // Postfix x++ / x-- (and xs[i]++).
    if (Check(Tok::Inc) || Check(Tok::Dec)) {
        const Tok bin = Peek().type == Tok::Inc ? Tok::Plus : Tok::Minus; ++m_pos;
        if (auto* var = dynamic_cast<VarExpr*>(left.get())) {
            const std::string name = var->n;
            auto combined = At(std::make_unique<BinaryExpr>(bin, At(std::make_unique<VarExpr>(name), line),
                                                            At(std::make_unique<NumberExpr>(1.0), line)), line);
            return At(std::make_unique<AssignExpr>(name, std::move(combined)), line);
        }
        if (auto* ix = dynamic_cast<IndexExpr*>(left.get()))
            return At(std::make_unique<IndexCompoundAssignExpr>(std::move(ix->arr), std::move(ix->index), bin,
                                                                At(std::make_unique<NumberExpr>(1.0), line)), line);
        Fail("parse error: invalid increment target");
    }
    return left;
}

// a + b + c ... builds a tree as deep as it is long: cap the operators in one expression.
void Parser::CountOperator() {
    static constexpr int kMaxOperators = 1000;
    if (++m_operators > kMaxOperators)
        Fail("parse error: expression is too long (more than " + std::to_string(kMaxOperators) + " operators)");
}

ExprPtr Parser::ParseCoalesce() {
    const int line = Line();
    ExprPtr l = ParseOr();
    if (Match(Tok::QuestionQuestion)) {
        CountOperator();
        l = At(std::make_unique<CoalesceExpr>(std::move(l), ParseCoalesce()), line);
    }
    return l;
}
ExprPtr Parser::ParseOr() {
    const int line = Line();
    auto l = ParseAnd();
    while (Match(Tok::Or)) { CountOperator(); l = At(std::make_unique<LogicalExpr>(Tok::Or, std::move(l), ParseAnd()), line); }
    return l;
}
ExprPtr Parser::ParseAnd() {
    const int line = Line();
    auto l = ParseBitOr();
    while (Match(Tok::And)) { CountOperator(); l = At(std::make_unique<LogicalExpr>(Tok::And, std::move(l), ParseBitOr()), line); }
    return l;
}
ExprPtr Parser::ParseBitOr() {
    const int line = Line();
    auto l = ParseBitXor();
    while (Match(Tok::BitOr)) { CountOperator(); l = At(std::make_unique<BinaryExpr>(Tok::BitOr, std::move(l), ParseBitXor()), line); }
    return l;
}
ExprPtr Parser::ParseBitXor() {
    const int line = Line();
    auto l = ParseBitAnd();
    while (Match(Tok::BitXor)) { CountOperator(); l = At(std::make_unique<BinaryExpr>(Tok::BitXor, std::move(l), ParseBitAnd()), line); }
    return l;
}
ExprPtr Parser::ParseBitAnd() {
    const int line = Line();
    auto l = ParseEquality();
    while (Match(Tok::BitAnd)) { CountOperator(); l = At(std::make_unique<BinaryExpr>(Tok::BitAnd, std::move(l), ParseEquality()), line); }
    return l;
}
ExprPtr Parser::ParseEquality() {
    const int line = Line();
    auto l = ParseComparison();
    while (Check(Tok::Eq) || Check(Tok::Ne)) {
        const Tok op = Peek().type; ++m_pos; CountOperator();
        l = At(std::make_unique<BinaryExpr>(op, std::move(l), ParseComparison()), line);
    }
    return l;
}
ExprPtr Parser::ParseComparison() {
    const int line = Line();
    auto l = ParseTerm();
    while (Check(Tok::Lt) || Check(Tok::Gt) || Check(Tok::Le) || Check(Tok::Ge)) {
        const Tok op = Peek().type; ++m_pos; CountOperator();
        l = At(std::make_unique<BinaryExpr>(op, std::move(l), ParseTerm()), line);
    }
    return l;
}
ExprPtr Parser::ParseTerm() {
    const int line = Line();
    auto l = ParseFactor();
    while (Check(Tok::Plus) || Check(Tok::Minus)) {
        const Tok op = Peek().type; ++m_pos; CountOperator();
        l = At(std::make_unique<BinaryExpr>(op, std::move(l), ParseFactor()), line);
    }
    return l;
}
ExprPtr Parser::ParseFactor() {
    const int line = Line();
    auto l = ParseUnary();
    while (Check(Tok::Star) || Check(Tok::Slash) || Check(Tok::Percent)) {
        const Tok op = Peek().type; ++m_pos; CountOperator();
        l = At(std::make_unique<BinaryExpr>(op, std::move(l), ParseUnary()), line);
    }
    return l;
}
ExprPtr Parser::ParseUnary() {
    // Every nested expression comes through here: cap the depth so a deep nest is a parse error, not a crash.
    if (m_nesting >= 256) Fail("parse error: expression nested too deeply");
    struct NestGuard { int& n; explicit NestGuard(int& d) : n(d) { ++n; } ~NestGuard() { --n; } } guard(m_nesting);
    const int line = Line();
    if (Check(Tok::Minus) || Check(Tok::Not)) {
        const Tok op = Peek().type; ++m_pos;
        return At(std::make_unique<UnaryExpr>(op, ParseUnary()), line);
    }
    if (Check(Tok::Inc) || Check(Tok::Dec)) {   // ++x / --x
        const Tok bin = Peek().type == Tok::Inc ? Tok::Plus : Tok::Minus; ++m_pos;
        ExprPtr target = ParseUnary();
        auto* var = dynamic_cast<VarExpr*>(target.get());
        if (!var) Fail("parse error: invalid increment target");
        const std::string name = var->n;
        auto combined = At(std::make_unique<BinaryExpr>(bin, At(std::make_unique<VarExpr>(name), line),
                                                        At(std::make_unique<NumberExpr>(1.0), line)), line);
        return At(std::make_unique<AssignExpr>(name, std::move(combined)), line);
    }
    return ParsePostfix();
}

// One `.name` or `.name(args)` step; `safe` (after ?.) skips the rest when the receiver is null.
ExprPtr Parser::ParseMemberStep(ExprPtr e, bool safe) {
    const int line = Line();
    const std::string path = Expect(Tok::Ident, "member name").text;
    std::vector<std::string> parts;
    for (std::size_t a = 0, b; a <= path.size(); a = b + 1) {
        b = path.find('.', a); if (b == std::string::npos) b = path.size();
        parts.push_back(path.substr(a, b - a));
    }
    for (std::size_t k = 0; k + 1 < parts.size(); ++k) e = At(std::make_unique<MemberExpr>(std::move(e), parts[k], safe), line);
    if (Match(Tok::LParen)) {
        auto call = At(std::make_unique<MethodCallExpr>(std::move(e), parts.back(), safe), line);
        if (!Check(Tok::RParen)) {
            do { call->args.push_back(ParseExpression()); } while (Match(Tok::Comma));
        }
        Expect(Tok::RParen, "')'");
        return call;
    }
    return At(std::make_unique<MemberExpr>(std::move(e), parts.back(), safe), line);
}
ExprPtr Parser::ParsePostfix() {
    ExprPtr e = ParsePrimary();
    bool safe = false;
    for (;;) {
        const int line = Line();
        if (Match(Tok::LBracket)) {
            ExprPtr idx = ParseExpression();
            Expect(Tok::RBracket, "']'");
            e = At(std::make_unique<IndexExpr>(std::move(e), std::move(idx), safe), line);
        } else if (Match(Tok::QuestionDot)) {
            safe = true;
            e = ParseMemberStep(std::move(e), true);
        } else if (Match(Tok::Dot)) {
            e = ParseMemberStep(std::move(e), safe);
        } else break;
    }
    return e;
}
// `{ a, b, c }`: a list initializer (nested for maps).
ExprPtr Parser::ParseBraceList() {
    auto arr = std::make_unique<ArrayExpr>();
    arr->line = Line();
    Expect(Tok::LBrace, "'{'");
    while (!Check(Tok::RBrace) && !Check(Tok::End)) {
        arr->elems.push_back(ParseExpression());
        if (!Match(Tok::Comma)) break;
    }
    Expect(Tok::RBrace, "'}'");
    return arr;
}
// After `new`: new List<T>(), new Dictionary<K, V>(), new T[n], new[] { ... }.
ExprPtr Parser::ParseNew() {
    const int line = Line();
    if (Check(Tok::LBracket)) {
        m_pos = SkipTypeSuffix(m_pos);
        if (Check(Tok::LBrace)) return ParseBraceList();
        return At(std::make_unique<ArrayExpr>(), line);
    }
    std::string type = Expect(Tok::Ident, "type after 'new'").text;
    const auto dotp = type.rfind('.');
    const std::string base = dotp == std::string::npos ? type : type.substr(dotp + 1);
    if (Check(Tok::Lt)) m_pos = SkipTypeSuffix(m_pos);
    if (Match(Tok::LBracket)) {
        ExprPtr size;
        if (!Check(Tok::RBracket)) size = ParseExpression();
        Expect(Tok::RBracket, "']'");
        if (Check(Tok::LBrace)) return ParseBraceList();
        auto call = At(std::make_unique<CallExpr>("__new_array"), line);
        if (size) { call->args.push_back(std::move(size)); call->argNames.push_back(""); }
        return call;
    }
    static const std::set<std::string> kLists = {"List", "HashSet", "Queue", "Stack", "LinkedList", "ArrayList", "SortedSet"};
    static const std::set<std::string> kMaps  = {"Dictionary", "SortedDictionary", "Hashtable", "SortedList"};
    if (!kLists.count(base) && !kMaps.count(base)) Fail("parse error: can't make a new '" + type + "' (only lists and dictionaries)");
    ExprPtr from;
    if (Match(Tok::LParen)) {
        if (!Check(Tok::RParen)) from = ParseExpression();
        Expect(Tok::RParen, "')'");
    }
    ExprPtr init = Check(Tok::LBrace) ? ParseBraceList() : nullptr;
    auto call = At(std::make_unique<CallExpr>(kMaps.count(base) ? "__new_map" : "__new_list"), line);
    if (init) { call->args.push_back(std::move(init)); call->argNames.push_back(""); }
    else if (from) { call->args.push_back(std::move(from)); call->argNames.push_back(""); }
    return call;
}
// $"a {expr} b": literal and {expression} parts; {{ and }} are literal braces.
ExprPtr Parser::BuildInterp(const std::string& tpl, int line) {
    auto node = std::make_unique<InterpStringExpr>();
    node->line = line;
    std::string lit;
    for (std::size_t i = 0; i < tpl.size(); ++i) {
        const char c = tpl[i];
        if (c == '{' && i + 1 < tpl.size() && tpl[i + 1] == '{') { lit += '{'; ++i; continue; }
        if (c == '}' && i + 1 < tpl.size() && tpl[i + 1] == '}') { lit += '}'; ++i; continue; }
        if (c != '{') { lit += c; continue; }
        node->AddLiteral(lit); lit.clear();
        std::string expr; int depth = 1; ++i;
        for (; i < tpl.size(); ++i) {
            if (tpl[i] == '{') ++depth;
            else if (tpl[i] == '}') { if (--depth == 0) break; }
            expr += tpl[i];
        }
        if (depth != 0) throw SyntaxError("parse error: '{' without '}' in an interpolated string", line);
        Lexer lx(expr);
        std::vector<Token> toks;
        try { toks = lx.Scan(); }
        catch (const ScriptError& e) { throw SyntaxError(e.what(), line); }
        Parser p(std::move(toks));
        p.SetLineOffset(line - 1);
        ExprPtr e = p.ParseExpression();
        if (!p.AtEnd()) throw SyntaxError("parse error: unexpected text after the expression in {" + expr + "}", line);
        node->AddExpr(std::move(e));
    }
    node->AddLiteral(lit);
    return node;
}
// f(5) or f(b: 4, a: 1): a name is a bare identifier followed by ':'.
void Parser::ParseCallArguments(std::vector<ExprPtr>& args, std::vector<std::string>& names) {
    if (Check(Tok::RParen)) return;
    do {
        std::string nm;
        if (Check(Tok::Ident) && m_toks[m_pos + 1].type == Tok::Colon) { nm = m_toks[m_pos].text; m_pos += 2; }
        names.push_back(std::move(nm));
        args.push_back(ParseExpression());
    } while (Match(Tok::Comma));
}

ExprPtr Parser::ParsePrimary() {
    const int line = Line();
    if (Match(Tok::Number)) return At(std::make_unique<NumberExpr>(Prev().number), line);
    if (Match(Tok::String)) return At(std::make_unique<StringExpr>(Prev().text), line);
    if (Match(Tok::InterpString)) return BuildInterp(Prev().text, line);
    if (Match(Tok::True))   return At(std::make_unique<BoolExpr>(true), line);
    if (Match(Tok::False))  return At(std::make_unique<BoolExpr>(false), line);
    // Casts: (int)x, (float)a / b, (string)v.
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
            return At(std::make_unique<CastExpr>(ty, ParseUnary()), line);
        }
        if (m_enums.count(ty) && operand) { m_pos += 3; return ParseUnary(); }   // (State)2
    }
    if (Match(Tok::LParen)) { auto e = ParseExpression(); Expect(Tok::RParen, "')'"); return e; }
    if (Check(Tok::LBrace)) return ParseBraceList();
    if (Match(Tok::LBracket)) {
        auto arr = At(std::make_unique<ArrayExpr>(), line);
        if (!Check(Tok::RBracket)) {
            do { arr->elems.push_back(ParseExpression()); } while (Match(Tok::Comma));
        }
        Expect(Tok::RBracket, "']'");
        return arr;
    }
    if (Check(Tok::Ident)) {
        std::string name = m_toks[m_pos++].text;
        if (name == "new") return ParseNew();
        if (Match(Tok::LParen)) {
            auto call = At(std::make_unique<CallExpr>(name), line);
            ParseCallArguments(call->args, call->argNames);
            Expect(Tok::RParen, "')'");
            return call;
        }
        if (auto it = m_enumValues.find(name); it != m_enumValues.end()) return At(std::make_unique<NumberExpr>(it->second), line);
        return At(std::make_unique<VarExpr>(name), line);
    }
    std::string got = TokName(Peek().type);
    Fail("parse error: unexpected " + got);
}

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

} // namespace okayscript
// dup-allow-end
