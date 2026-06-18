#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Core/Random.hpp"

#include <cctype>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace okay {
namespace {

using Value = vs::VsValue;

// ===================== Lexer =====================
enum class Tok {
    End, Number, String, Ident,
    Var, If, Else, While, For, Function, Return, True, False,
    Plus, Minus, Star, Slash, Percent,
    Assign, PlusEq, MinusEq, StarEq, SlashEq,
    Eq, Ne, Lt, Gt, Le, Ge, Not, And, Or,
    LParen, RParen, LBrace, RBrace, Comma, Semicolon
};

struct Token {
    Tok type;
    std::string text;
    double number = 0.0;
    int line = 0;
};

struct ScriptError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Lexer {
public:
    explicit Lexer(const std::string& src) : m_src(src) {}

    std::vector<Token> Scan() {
        std::vector<Token> out;
        while (!AtEnd()) {
            SkipTrivia();
            if (AtEnd()) break;
            char c = Peek();
            if (std::isdigit((unsigned char)c) || (c == '.' && std::isdigit((unsigned char)Peek(1))))
                out.push_back(Number());
            else if (std::isalpha((unsigned char)c) || c == '_')
                out.push_back(Ident());
            else if (c == '"')
                out.push_back(String());
            else
                out.push_back(Operator());
        }
        out.push_back({Tok::End, "", 0, m_line});
        return out;
    }

private:
    bool AtEnd() const { return m_pos >= m_src.size(); }
    char Peek(int o = 0) const { return (m_pos + o < m_src.size()) ? m_src[m_pos + o] : '\0'; }
    char Advance() { char c = m_src[m_pos++]; if (c == '\n') ++m_line; return c; }

    void SkipTrivia() {
        while (!AtEnd()) {
            char c = Peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { Advance(); }
            else if (c == '/' && Peek(1) == '/') { while (!AtEnd() && Peek() != '\n') Advance(); }
            else if (c == '#') { while (!AtEnd() && Peek() != '\n') Advance(); }
            else break;
        }
    }

    Token Number() {
        std::string s;
        while (std::isdigit((unsigned char)Peek()) || Peek() == '.') s += Advance();
        return {Tok::Number, s, std::stod(s), m_line};
    }

    Token String() {
        Advance(); // opening quote
        std::string s;
        while (!AtEnd() && Peek() != '"') {
            char c = Advance();
            if (c == '\\' && !AtEnd()) {
                char e = Advance();
                switch (e) { case 'n': c = '\n'; break; case 't': c = '\t'; break;
                             case '"': c = '"'; break; case '\\': c = '\\'; break;
                             default: c = e; }
            }
            s += c;
        }
        if (AtEnd()) throw ScriptError("unterminated string");
        Advance(); // closing quote
        return {Tok::String, s, 0, m_line};
    }

    Token Ident() {
        std::string s;
        while (std::isalnum((unsigned char)Peek()) || Peek() == '_') s += Advance();
        static const std::unordered_map<std::string, Tok> kw = {
            {"var", Tok::Var}, {"if", Tok::If}, {"else", Tok::Else},
            {"while", Tok::While}, {"for", Tok::For},
            {"function", Tok::Function}, {"return", Tok::Return},
            {"true", Tok::True}, {"false", Tok::False}};
        auto it = kw.find(s);
        return {it != kw.end() ? it->second : Tok::Ident, s, 0, m_line};
    }

    Token Operator() {
        char c = Advance();
        auto two = [&](char n, Tok t) { if (Peek() == n) { Advance(); return Token{t, "", 0, m_line}; } return Token{Tok::End, "", 0, m_line}; };
        switch (c) {
            case '+': { auto t = two('=', Tok::PlusEq);  return t.type != Tok::End ? t : Token{Tok::Plus, "", 0, m_line}; }
            case '-': { auto t = two('=', Tok::MinusEq); return t.type != Tok::End ? t : Token{Tok::Minus, "", 0, m_line}; }
            case '*': { auto t = two('=', Tok::StarEq);  return t.type != Tok::End ? t : Token{Tok::Star, "", 0, m_line}; }
            case '/': { auto t = two('=', Tok::SlashEq); return t.type != Tok::End ? t : Token{Tok::Slash, "", 0, m_line}; }
            case '%': return {Tok::Percent, "", 0, m_line};
            case '(': return {Tok::LParen, "", 0, m_line};
            case ')': return {Tok::RParen, "", 0, m_line};
            case '{': return {Tok::LBrace, "", 0, m_line};
            case '}': return {Tok::RBrace, "", 0, m_line};
            case ',': return {Tok::Comma, "", 0, m_line};
            case ';': return {Tok::Semicolon, "", 0, m_line};
            case '=': { auto t = two('=', Tok::Eq); return t.type != Tok::End ? t : Token{Tok::Assign, "", 0, m_line}; }
            case '!': { auto t = two('=', Tok::Ne); return t.type != Tok::End ? t : Token{Tok::Not, "", 0, m_line}; }
            case '<': { auto t = two('=', Tok::Le); return t.type != Tok::End ? t : Token{Tok::Lt, "", 0, m_line}; }
            case '>': { auto t = two('=', Tok::Ge); return t.type != Tok::End ? t : Token{Tok::Gt, "", 0, m_line}; }
            case '&': if (Peek() == '&') { Advance(); return {Tok::And, "", 0, m_line}; } break;
            case '|': if (Peek() == '|') { Advance(); return {Tok::Or, "", 0, m_line}; } break;
        }
        throw ScriptError(std::string("unexpected character '") + c + "'");
    }

    const std::string& m_src;
    std::size_t m_pos = 0;
    int m_line = 1;
};

// ===================== AST =====================
struct Runtime;

struct Expr { virtual ~Expr() = default; virtual Value Eval(Runtime&) = 0; };
struct Stmt { virtual ~Stmt() = default; virtual void Exec(Runtime&) = 0; };
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

struct ReturnSignal { Value value; };

// ===================== Environment / Runtime =====================
struct Environment {
    std::unordered_map<std::string, Value> vars;
};

struct FunctionDecl {
    std::vector<std::string> params;
    std::vector<StmtPtr> body;
};

struct Runtime {
    std::vector<Environment> scopes{1};                 // scopes[0] = global
    std::unordered_map<std::string, FunctionDecl> functions;
    std::unordered_map<std::string, std::function<Value(std::vector<Value>&)>> builtins;
    ScriptHost* host = nullptr;

    Environment& Global() { return scopes.front(); }

    void Define(const std::string& n, const Value& v) { scopes.back().vars[n] = v; }
    void Assign(const std::string& n, const Value& v) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            if (it->vars.count(n)) { it->vars[n] = v; return; }
        Global().vars[n] = v; // create global on first assignment
    }
    Value Get(const std::string& n) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto f = it->vars.find(n);
            if (f != it->vars.end()) return f->second;
        }
        return Value{}; // undefined reads as 0/false/empty
    }

    Value Call(const std::string& name, std::vector<Value>& args);
};

// ---- Expressions ----
struct NumberExpr : Expr { double v; explicit NumberExpr(double x) : v(x) {} Value Eval(Runtime&) override { return (float)v; } };
struct StringExpr : Expr { std::string v; explicit StringExpr(std::string s) : v(std::move(s)) {} Value Eval(Runtime&) override { return v; } };
struct BoolExpr   : Expr { bool v; explicit BoolExpr(bool b) : v(b) {} Value Eval(Runtime&) override { return v; } };
struct VarExpr    : Expr { std::string n; explicit VarExpr(std::string s) : n(std::move(s)) {} Value Eval(Runtime& r) override { return r.Get(n); } };

struct AssignExpr : Expr {
    std::string n; ExprPtr value;
    AssignExpr(std::string s, ExprPtr e) : n(std::move(s)), value(std::move(e)) {}
    Value Eval(Runtime& r) override { Value v = value->Eval(r); r.Assign(n, v); return v; }
};

struct UnaryExpr : Expr {
    Tok op; ExprPtr e;
    UnaryExpr(Tok o, ExprPtr x) : op(o), e(std::move(x)) {}
    Value Eval(Runtime& r) override {
        Value v = e->Eval(r);
        if (op == Tok::Minus) return -v.AsFloat();
        if (op == Tok::Not) return !v.AsBool();
        return v;
    }
};

struct LogicalExpr : Expr {
    Tok op; ExprPtr l, r;
    LogicalExpr(Tok o, ExprPtr a, ExprPtr b) : op(o), l(std::move(a)), r(std::move(b)) {}
    Value Eval(Runtime& rt) override {
        Value lv = l->Eval(rt);
        if (op == Tok::And) return lv.AsBool() ? r->Eval(rt).AsBool() : false;
        return lv.AsBool() ? true : r->Eval(rt).AsBool();
    }
};

struct BinaryExpr : Expr {
    Tok op; ExprPtr l, r;
    BinaryExpr(Tok o, ExprPtr a, ExprPtr b) : op(o), l(std::move(a)), r(std::move(b)) {}
    Value Eval(Runtime& rt) override {
        Value a = l->Eval(rt), b = r->Eval(rt);
        switch (op) {
            case Tok::Plus:
                if (a.IsString() || b.IsString()) return a.AsString() + b.AsString();
                return a.AsFloat() + b.AsFloat();
            case Tok::Minus:   return a.AsFloat() - b.AsFloat();
            case Tok::Star:    return a.AsFloat() * b.AsFloat();
            case Tok::Slash:   { float d = b.AsFloat(); return d != 0 ? a.AsFloat() / d : 0.0f; }
            case Tok::Percent: { float d = b.AsFloat(); return d != 0 ? std::fmod(a.AsFloat(), d) : 0.0f; }
            case Tok::Lt: return a.AsFloat() <  b.AsFloat();
            case Tok::Gt: return a.AsFloat() >  b.AsFloat();
            case Tok::Le: return a.AsFloat() <= b.AsFloat();
            case Tok::Ge: return a.AsFloat() >= b.AsFloat();
            case Tok::Eq: return (a.IsString() || b.IsString()) ? a.AsString() == b.AsString()
                                                                : a.AsFloat() == b.AsFloat();
            case Tok::Ne: return (a.IsString() || b.IsString()) ? a.AsString() != b.AsString()
                                                                : a.AsFloat() != b.AsFloat();
            default: return Value{};
        }
    }
};

struct CallExpr : Expr {
    std::string name; std::vector<ExprPtr> args;
    explicit CallExpr(std::string n) : name(std::move(n)) {}
    Value Eval(Runtime& rt) override {
        std::vector<Value> vals;
        vals.reserve(args.size());
        for (auto& a : args) vals.push_back(a->Eval(rt));
        return rt.Call(name, vals);
    }
};

// ---- Statements ----
struct ExprStmt : Stmt { ExprPtr e; explicit ExprStmt(ExprPtr x) : e(std::move(x)) {} void Exec(Runtime& r) override { e->Eval(r); } };
struct VarDeclStmt : Stmt {
    std::string n; ExprPtr init;
    VarDeclStmt(std::string s, ExprPtr e) : n(std::move(s)), init(std::move(e)) {}
    void Exec(Runtime& r) override { r.Define(n, init ? init->Eval(r) : Value{}); }
};
struct ReturnStmt : Stmt { ExprPtr e; explicit ReturnStmt(ExprPtr x) : e(std::move(x)) {} void Exec(Runtime& r) override { throw ReturnSignal{e ? e->Eval(r) : Value{}}; } };
struct BlockStmt : Stmt {
    std::vector<StmtPtr> body;
    void Exec(Runtime& r) override { for (auto& s : body) s->Exec(r); }
};
struct IfStmt : Stmt {
    ExprPtr cond; std::vector<StmtPtr> thenB, elseB;
    void Exec(Runtime& r) override {
        if (cond->Eval(r).AsBool()) for (auto& s : thenB) s->Exec(r);
        else for (auto& s : elseB) s->Exec(r);
    }
};
struct WhileStmt : Stmt {
    ExprPtr cond; std::vector<StmtPtr> body;
    void Exec(Runtime& r) override {
        int guard = 0;
        while (cond->Eval(r).AsBool()) {
            for (auto& s : body) s->Exec(r);
            if (++guard > 1000000) throw ScriptError("while loop exceeded iteration limit");
        }
    }
};
struct ForStmt : Stmt {
    // Function-level scoping (like the rest of the language): the init runs in
    // the current scope, so `return` inside the body can't corrupt the stack.
    StmtPtr init; ExprPtr cond; ExprPtr step; std::vector<StmtPtr> body;
    void Exec(Runtime& r) override {
        if (init) init->Exec(r);
        int guard = 0;
        while (!cond || cond->Eval(r).AsBool()) {
            for (auto& s : body) s->Exec(r);
            if (step) step->Eval(r);
            if (++guard > 1000000) throw ScriptError("for loop exceeded iteration limit");
        }
    }
};
struct FunctionStmt : Stmt {
    std::string name; FunctionDecl decl;
    void Exec(Runtime&) override { /* hoisted at compile time */ }
};

Value Runtime::Call(const std::string& name, std::vector<Value>& args) {
    auto b = builtins.find(name);
    if (b != builtins.end()) return b->second(args);

    auto f = functions.find(name);
    if (f == functions.end()) throw ScriptError("call to undefined function '" + name + "'");

    scopes.push_back(Environment{});
    const FunctionDecl& fn = f->second;
    for (std::size_t i = 0; i < fn.params.size(); ++i)
        Define(fn.params[i], i < args.size() ? args[i] : Value{});

    Value ret;
    try {
        for (auto& s : fn.body) s->Exec(*this);
    } catch (ReturnSignal& rs) {
        ret = rs.value;
    }
    scopes.pop_back();
    return ret;
}

// ===================== Parser =====================
class Parser {
public:
    explicit Parser(std::vector<Token> toks) : m_toks(std::move(toks)) {}

    std::vector<StmtPtr> ParseProgram(std::unordered_map<std::string, FunctionDecl>& funcs) {
        std::vector<StmtPtr> top;
        while (!Check(Tok::End)) {
            if (Check(Tok::Function)) {
                std::string name;
                FunctionDecl decl = ParseFunction(name);
                funcs[name] = std::move(decl);
            } else {
                top.push_back(ParseStatement());
            }
        }
        return top;
    }

private:
    const Token& Peek() const { return m_toks[m_pos]; }
    const Token& Prev() const { return m_toks[m_pos - 1]; }
    bool Check(Tok t) const { return Peek().type == t; }
    bool Match(Tok t) { if (Check(t)) { ++m_pos; return true; } return false; }
    const Token& Expect(Tok t, const char* msg) {
        if (!Check(t)) throw ScriptError(std::string("parse error: expected ") + msg);
        return m_toks[m_pos++];
    }

    FunctionDecl ParseFunction(std::string& nameOut) {
        Expect(Tok::Function, "'function'");
        nameOut = Expect(Tok::Ident, "function name").text;
        Expect(Tok::LParen, "'('");
        FunctionDecl decl;
        if (!Check(Tok::RParen)) {
            do { decl.params.push_back(Expect(Tok::Ident, "parameter").text); }
            while (Match(Tok::Comma));
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

    StmtPtr ParseStatement() {
        if (Match(Tok::Var)) {
            std::string name = Expect(Tok::Ident, "variable name").text;
            ExprPtr init;
            if (Match(Tok::Assign)) init = ParseExpression();
            Match(Tok::Semicolon);
            return std::make_unique<VarDeclStmt>(name, std::move(init));
        }
        if (Match(Tok::If)) {
            Expect(Tok::LParen, "'('");
            auto cond = ParseExpression();
            Expect(Tok::RParen, "')'");
            auto st = std::make_unique<IfStmt>();
            st->cond = std::move(cond);
            st->thenB = ParseBlock();
            if (Match(Tok::Else)) st->elseB = Check(Tok::If) ? SingleStmt() : ParseBlock();
            return st;
        }
        if (Match(Tok::While)) {
            Expect(Tok::LParen, "'('");
            auto cond = ParseExpression();
            Expect(Tok::RParen, "')'");
            auto st = std::make_unique<WhileStmt>();
            st->cond = std::move(cond);
            st->body = ParseBlock();
            return st;
        }
        if (Match(Tok::For)) {
            Expect(Tok::LParen, "'('");
            auto st = std::make_unique<ForStmt>();
            // init: a var declaration, an expression, or empty.
            if (!Match(Tok::Semicolon)) {
                if (Match(Tok::Var)) {
                    std::string name = Expect(Tok::Ident, "variable name").text;
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
            st->body = ParseBlock();
            return st;
        }
        if (Match(Tok::Return)) {
            ExprPtr e;
            if (!Check(Tok::Semicolon) && !Check(Tok::RBrace)) e = ParseExpression();
            Match(Tok::Semicolon);
            return std::make_unique<ReturnStmt>(std::move(e));
        }
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
    ExprPtr ParseExpression() { return ParseAssignment(); }

    ExprPtr ParseAssignment() {
        ExprPtr left = ParseOr();
        if (Match(Tok::Assign)) {
            auto* var = dynamic_cast<VarExpr*>(left.get());
            if (!var) throw ScriptError("invalid assignment target");
            std::string name = var->n;
            ExprPtr value = ParseAssignment();
            return std::make_unique<AssignExpr>(name, std::move(value));
        }
        // Compound assignment: x += e  desugars to  x = x + e.
        if (Check(Tok::PlusEq) || Check(Tok::MinusEq) ||
            Check(Tok::StarEq) || Check(Tok::SlashEq)) {
            auto* var = dynamic_cast<VarExpr*>(left.get());
            if (!var) throw ScriptError("invalid assignment target");
            std::string name = var->n;
            Tok t = Peek().type; ++m_pos;
            Tok bin = t == Tok::PlusEq ? Tok::Plus : t == Tok::MinusEq ? Tok::Minus
                    : t == Tok::StarEq ? Tok::Star : Tok::Slash;
            ExprPtr value = ParseAssignment();
            auto combined = std::make_unique<BinaryExpr>(
                bin, std::make_unique<VarExpr>(name), std::move(value));
            return std::make_unique<AssignExpr>(name, std::move(combined));
        }
        return left;
    }
    ExprPtr ParseOr() {
        auto l = ParseAnd();
        while (Match(Tok::Or)) l = std::make_unique<LogicalExpr>(Tok::Or, std::move(l), ParseAnd());
        return l;
    }
    ExprPtr ParseAnd() {
        auto l = ParseEquality();
        while (Match(Tok::And)) l = std::make_unique<LogicalExpr>(Tok::And, std::move(l), ParseEquality());
        return l;
    }
    ExprPtr ParseEquality() {
        auto l = ParseComparison();
        while (Check(Tok::Eq) || Check(Tok::Ne)) { Tok op = Peek().type; ++m_pos; l = std::make_unique<BinaryExpr>(op, std::move(l), ParseComparison()); }
        return l;
    }
    ExprPtr ParseComparison() {
        auto l = ParseTerm();
        while (Check(Tok::Lt) || Check(Tok::Gt) || Check(Tok::Le) || Check(Tok::Ge)) { Tok op = Peek().type; ++m_pos; l = std::make_unique<BinaryExpr>(op, std::move(l), ParseTerm()); }
        return l;
    }
    ExprPtr ParseTerm() {
        auto l = ParseFactor();
        while (Check(Tok::Plus) || Check(Tok::Minus)) { Tok op = Peek().type; ++m_pos; l = std::make_unique<BinaryExpr>(op, std::move(l), ParseFactor()); }
        return l;
    }
    ExprPtr ParseFactor() {
        auto l = ParseUnary();
        while (Check(Tok::Star) || Check(Tok::Slash) || Check(Tok::Percent)) { Tok op = Peek().type; ++m_pos; l = std::make_unique<BinaryExpr>(op, std::move(l), ParseUnary()); }
        return l;
    }
    ExprPtr ParseUnary() {
        if (Check(Tok::Minus) || Check(Tok::Not)) { Tok op = Peek().type; ++m_pos; return std::make_unique<UnaryExpr>(op, ParseUnary()); }
        return ParsePrimary();
    }
    ExprPtr ParsePrimary() {
        if (Match(Tok::Number)) return std::make_unique<NumberExpr>(Prev().number);
        if (Match(Tok::String)) return std::make_unique<StringExpr>(Prev().text);
        if (Match(Tok::True))   return std::make_unique<BoolExpr>(true);
        if (Match(Tok::False))  return std::make_unique<BoolExpr>(false);
        if (Match(Tok::LParen)) { auto e = ParseExpression(); Expect(Tok::RParen, "')'"); return e; }
        if (Check(Tok::Ident)) {
            std::string name = m_toks[m_pos++].text;
            if (Match(Tok::LParen)) {
                auto call = std::make_unique<CallExpr>(name);
                if (!Check(Tok::RParen)) {
                    do { call->args.push_back(ParseExpression()); } while (Match(Tok::Comma));
                }
                Expect(Tok::RParen, "')'");
                return call;
            }
            return std::make_unique<VarExpr>(name);
        }
        throw ScriptError("parse error: unexpected token");
    }

    std::vector<Token> m_toks;
    std::size_t m_pos = 0;
};

} // namespace

// ===================== OkayScriptVM::Impl =====================
struct OkayScriptVM::Impl {
    Runtime rt;
    std::vector<StmtPtr> topLevel;
    bool loaded = false;

    void RegisterBuiltins() {
        auto& b = rt.builtins;
        auto tf = [this]() -> Transform* { return rt.host ? rt.host->transform : nullptr; };

        b["print"] = [](std::vector<Value>& a) {
            std::string s;
            for (std::size_t i = 0; i < a.size(); ++i) { if (i) s += " "; s += a[i].AsString(); }
            Log::Info("[script] ", s);
            return Value{};
        };
        b["move"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf())
                t->Translate({a.size() > 0 ? a[0].AsFloat() : 0.0f,
                              a.size() > 1 ? a[1].AsFloat() : 0.0f, 0.0f});
            return Value{};
        };
        b["set_pos"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf())
                t->localPosition = {a.size() > 0 ? a[0].AsFloat() : 0.0f,
                                    a.size() > 1 ? a[1].AsFloat() : 0.0f, 0.0f};
            return Value{};
        };
        b["rotate"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf()) t->Rotate({0, 0, a.empty() ? 0.0f : a[0].AsFloat()});
            return Value{};
        };
        b["pos_x"] = [tf](std::vector<Value>&) { Transform* t = tf(); return Value{t ? t->localPosition.x : 0.0f}; };
        b["pos_y"] = [tf](std::vector<Value>&) { Transform* t = tf(); return Value{t ? t->localPosition.y : 0.0f}; };
        b["time"]  = [](std::vector<Value>&) { return Value{Time::ElapsedTime()}; };
        b["dt"]    = [this](std::vector<Value>&) { return Value{rt.host ? rt.host->deltaTime : 0.0f}; };
        b["axis_x"] = [](std::vector<Value>&) { return Value{Input::AxisWASD().x}; };
        b["axis_y"] = [](std::vector<Value>&) { return Value{Input::AxisWASD().y}; };
        b["key"]    = [](std::vector<Value>& a) {
            if (a.empty()) return Value{false};
            std::string s = a[0].AsString();
            return Value{!s.empty() && Input::GetKey(s[0])};
        };
        b["key_down"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{false};
            std::string s = a[0].AsString();
            return Value{!s.empty() && Input::GetKeyDown(s[0])};
        };
        b["rand"] = [](std::vector<Value>& a) {
            float lo = a.size() > 0 ? a[0].AsFloat() : 0.0f;
            float hi = a.size() > 1 ? a[1].AsFloat() : 1.0f;
            return Value{Random::Shared().Range(lo, hi)};
        };
        b["dist"] = [](std::vector<Value>& a) {
            if (a.size() < 4) return Value{0.0f};
            float dx = a[2].AsFloat() - a[0].AsFloat(), dy = a[3].AsFloat() - a[1].AsFloat();
            return Value{Mathf::Sqrt(dx * dx + dy * dy)};
        };
        b["get"] = [this](std::vector<Value>& a) {
            if (a.empty() || !rt.host) return Value{};
            auto it = rt.host->globals.find(a[0].AsString());
            return it != rt.host->globals.end() ? it->second : Value{};
        };
        b["set"] = [this](std::vector<Value>& a) {
            if (a.size() >= 2 && rt.host) rt.host->globals[a[0].AsString()] = a[1];
            return Value{};
        };
        // Math helpers
        b["abs"]   = [](std::vector<Value>& a) { return Value{Mathf::Abs(a.empty() ? 0 : a[0].AsFloat())}; };
        b["sin"]   = [](std::vector<Value>& a) { return Value{Mathf::Sin(a.empty() ? 0 : a[0].AsFloat())}; };
        b["cos"]   = [](std::vector<Value>& a) { return Value{Mathf::Cos(a.empty() ? 0 : a[0].AsFloat())}; };
        b["sqrt"]  = [](std::vector<Value>& a) { return Value{Mathf::Sqrt(a.empty() ? 0 : a[0].AsFloat())}; };
        b["floor"] = [](std::vector<Value>& a) { return Value{Mathf::Floor(a.empty() ? 0 : a[0].AsFloat())}; };
        b["min"]   = [](std::vector<Value>& a) { return Value{Mathf::Min(a.size() > 0 ? a[0].AsFloat() : 0, a.size() > 1 ? a[1].AsFloat() : 0)}; };
        b["max"]   = [](std::vector<Value>& a) { return Value{Mathf::Max(a.size() > 0 ? a[0].AsFloat() : 0, a.size() > 1 ? a[1].AsFloat() : 0)}; };
        b["tan"]   = [](std::vector<Value>& a) { return Value{std::tan(a.empty() ? 0 : a[0].AsFloat())}; };
        b["pow"]   = [](std::vector<Value>& a) { return Value{std::pow(a.size() > 0 ? a[0].AsFloat() : 0, a.size() > 1 ? a[1].AsFloat() : 1)}; };
        b["round"] = [](std::vector<Value>& a) { return Value{Mathf::Round(a.empty() ? 0 : a[0].AsFloat())}; };
        b["ceil"]  = [](std::vector<Value>& a) { return Value{Mathf::Ceil(a.empty() ? 0 : a[0].AsFloat())}; };
        b["sign"]  = [](std::vector<Value>& a) { float v = a.empty() ? 0 : a[0].AsFloat(); return Value{v > 0 ? 1.0f : v < 0 ? -1.0f : 0.0f}; };
        b["atan2"] = [](std::vector<Value>& a) { return Value{std::atan2(a.size() > 0 ? a[0].AsFloat() : 0, a.size() > 1 ? a[1].AsFloat() : 0)}; };
        b["clamp"] = [](std::vector<Value>& a) {
            float v = a.size() > 0 ? a[0].AsFloat() : 0, lo = a.size() > 1 ? a[1].AsFloat() : 0, hi = a.size() > 2 ? a[2].AsFloat() : 1;
            return Value{Mathf::Clamp(v, lo, hi)};
        };
        b["lerp"]  = [](std::vector<Value>& a) {
            float x = a.size() > 0 ? a[0].AsFloat() : 0, y = a.size() > 1 ? a[1].AsFloat() : 0, t = a.size() > 2 ? a[2].AsFloat() : 0;
            return Value{Mathf::Lerp(x, y, t)};
        };
        b["len"]   = [](std::vector<Value>& a) { float x = a.size() > 0 ? a[0].AsFloat() : 0, y = a.size() > 1 ? a[1].AsFloat() : 0; return Value{Mathf::Sqrt(x * x + y * y)}; };
        b["pi"]    = [](std::vector<Value>&) { return Value{Mathf::PI}; };
        b["deg2rad"] = [](std::vector<Value>& a) { return Value{(a.empty() ? 0 : a[0].AsFloat()) * Mathf::Deg2Rad}; };
        b["rad2deg"] = [](std::vector<Value>& a) { return Value{(a.empty() ? 0 : a[0].AsFloat()) * Mathf::Rad2Deg}; };
        // Transform helpers
        b["set_x"] = [tf](std::vector<Value>& a) { if (Transform* t = tf()) t->localPosition.x = a.empty() ? 0 : a[0].AsFloat(); return Value{}; };
        b["set_y"] = [tf](std::vector<Value>& a) { if (Transform* t = tf()) t->localPosition.y = a.empty() ? 0 : a[0].AsFloat(); return Value{}; };
    }
};

OkayScriptVM::OkayScriptVM() : m_impl(std::make_unique<Impl>()) {
    m_impl->RegisterBuiltins();
}
OkayScriptVM::~OkayScriptVM() = default;

bool OkayScriptVM::Load(const std::string& source, std::string* error) {
    try {
        Lexer lex(source);
        Parser parser(lex.Scan());
        m_impl->rt.functions.clear();
        m_impl->topLevel = parser.ParseProgram(m_impl->rt.functions);
        // Run top-level statements once so globals/setup execute.
        for (auto& s : m_impl->topLevel) s->Exec(m_impl->rt);
        m_impl->loaded = true;
        return true;
    } catch (const ReturnSignal&) {
        return true; // a bare top-level return is harmless
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        Log::Error("OkayScript: ", e.what());
        m_impl->loaded = false;
        return false;
    }
}

void OkayScriptVM::Bind(ScriptHost* host) { m_impl->rt.host = host; }

void OkayScriptVM::CallStart() {
    if (!m_impl->loaded) return;
    if (m_impl->rt.functions.count("start")) {
        std::vector<Value> none;
        try { m_impl->rt.Call("start", none); }
        catch (const std::exception& e) { Log::Error("OkayScript start(): ", e.what()); }
    }
}

void OkayScriptVM::CallUpdate(float deltaTime) {
    if (!m_impl->loaded) return;
    if (m_impl->rt.host) m_impl->rt.host->deltaTime = deltaTime;
    if (m_impl->rt.functions.count("update")) {
        std::vector<Value> args{Value{deltaTime}};
        try { m_impl->rt.Call("update", args); }
        catch (const std::exception& e) { Log::Error("OkayScript update(): ", e.what()); }
    }
}

vs::VsValue OkayScriptVM::GetGlobal(const std::string& name) const {
    auto& g = m_impl->rt.Global().vars;
    auto it = g.find(name);
    return it != g.end() ? it->second : vs::VsValue{};
}

} // namespace okay
