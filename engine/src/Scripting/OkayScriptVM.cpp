#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/SceneManager.hpp"
#include "okay/Physics/Physics2D.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/SpriteAnimator.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Platform/Steam/Steam.hpp"
#include "okay/Components/UIImage.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/Tilemap.hpp"
#include "okay/Audio/AudioMixer.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Math/Easing.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
    Var, If, Else, While, For, In, Function, Return, True, False, Break, Continue,
    Question, Colon,
    Plus, Minus, Star, Slash, Percent,
    Assign, PlusEq, MinusEq, StarEq, SlashEq,
    Eq, Ne, Lt, Gt, Le, Ge, Not, And, Or,
    LParen, RParen, LBrace, RBrace, LBracket, RBracket, Comma, Semicolon
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
            {"while", Tok::While}, {"for", Tok::For}, {"in", Tok::In},
            {"function", Tok::Function}, {"return", Tok::Return},
            {"true", Tok::True}, {"false", Tok::False},
            {"break", Tok::Break}, {"continue", Tok::Continue}};
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
            case '[': return {Tok::LBracket, "", 0, m_line};
            case ']': return {Tok::RBracket, "", 0, m_line};
            case '?': return {Tok::Question, "", 0, m_line};
            case ':': return {Tok::Colon, "", 0, m_line};
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
struct BreakSignal {};
struct ContinueSignal {};

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

    // Scheduled callbacks (after / every). interval == 0 means fire once.
    struct Timer { float remaining; float interval; std::string fn; bool dead; };
    std::vector<Timer> timers;

    /// Advance scheduled timers by dt and invoke any that come due. Once-timers
    /// are removed after firing; repeating ones re-arm by their interval.
    void TickTimers(float dt) {
        std::vector<std::string> due;
        const float kEps = 1e-4f;                // absorb fp dust at the boundary
        for (auto& t : timers) {
            t.remaining -= dt;
            int guard = 0;                       // cap multi-fires on a big dt
            while (t.remaining <= kEps && guard++ < 64) {
                due.push_back(t.fn);
                if (t.interval > 0.0f) t.remaining += t.interval;
                else { t.dead = true; break; }
            }
        }
        timers.erase(std::remove_if(timers.begin(), timers.end(),
                                    [](const Timer& t) { return t.dead; }), timers.end());
        std::vector<Value> none;
        for (const auto& fn : due) if (functions.count(fn)) Call(fn, none);
    }

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

// Ternary: cond ? a : b.
struct TernaryExpr : Expr {
    ExprPtr cond, a, b;
    TernaryExpr(ExprPtr c, ExprPtr x, ExprPtr y) : cond(std::move(c)), a(std::move(x)), b(std::move(y)) {}
    Value Eval(Runtime& rt) override { return cond->Eval(rt).AsBool() ? a->Eval(rt) : b->Eval(rt); }
};

// An array literal: [a, b, c].
struct ArrayExpr : Expr {
    std::vector<ExprPtr> elems;
    Value Eval(Runtime& rt) override {
        Value v = Value::MakeArray();
        auto arr = v.AsArray();
        for (auto& e : elems) arr->push_back(e->Eval(rt));
        return v;
    }
};

// Read an element: arr[index].
struct IndexExpr : Expr {
    ExprPtr arr, index;
    IndexExpr(ExprPtr a, ExprPtr i) : arr(std::move(a)), index(std::move(i)) {}
    Value Eval(Runtime& rt) override {
        Value a = arr->Eval(rt);
        auto v = a.AsArray();
        if (!v) return Value{};
        int i = (int)index->Eval(rt).AsFloat();
        if (i < 0 || i >= (int)v->size()) return Value{};
        return (*v)[i];
    }
};

// Assign an element: arr[index] = value.
struct IndexAssignExpr : Expr {
    ExprPtr arr, index, value;
    IndexAssignExpr(ExprPtr a, ExprPtr i, ExprPtr v)
        : arr(std::move(a)), index(std::move(i)), value(std::move(v)) {}
    Value Eval(Runtime& rt) override {
        Value a = arr->Eval(rt);
        Value val = value->Eval(rt);
        auto v = a.AsArray();
        if (v) {
            int i = (int)index->Eval(rt).AsFloat();
            if (i >= 0 && i < (int)v->size()) (*v)[i] = val;
            else if (i == (int)v->size()) v->push_back(val); // append at end
        }
        return val;
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
struct BreakStmt : Stmt { void Exec(Runtime&) override { throw BreakSignal{}; } };
struct ContinueStmt : Stmt { void Exec(Runtime&) override { throw ContinueSignal{}; } };
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
            try { for (auto& s : body) s->Exec(r); }
            catch (ContinueSignal&) {}
            catch (BreakSignal&) { break; }
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
            try { for (auto& s : body) s->Exec(r); }
            catch (ContinueSignal&) {}        // fall through to the step
            catch (BreakSignal&) { break; }
            if (step) step->Eval(r);
            if (++guard > 1000000) throw ScriptError("for loop exceeded iteration limit");
        }
    }
};
// foreach: for x in <array> { body }
struct ForEachStmt : Stmt {
    std::string var; ExprPtr iterable; std::vector<StmtPtr> body;
    void Exec(Runtime& r) override {
        Value coll = iterable->Eval(r);
        auto arr = coll.AsArray();
        if (!arr) return;
        r.Define(var, Value{}); // loop variable lives in the current scope
        // Iterate a snapshot of the size so push during iteration is bounded.
        std::size_t n = arr->size();
        for (std::size_t i = 0; i < n && i < arr->size(); ++i) {
            r.Assign(var, (*arr)[i]);
            try { for (auto& s : body) s->Exec(r); }
            catch (ContinueSignal&) {}
            catch (BreakSignal&) { break; }
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
    } catch (BreakSignal&) {       // stray break/continue (no enclosing loop):
    } catch (ContinueSignal&) {    // end the function rather than escaping it.
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
            // foreach form: for x in <array> { ... }
            if (Check(Tok::Ident)) {
                std::size_t save = m_pos;
                std::string var = m_toks[m_pos++].text;
                if (Match(Tok::In)) {
                    auto fe = std::make_unique<ForEachStmt>();
                    fe->var = var;
                    fe->iterable = ParseExpression();
                    fe->body = ParseBlock();
                    return fe;
                }
                m_pos = save; // not foreach; fall back to C-style for
            }
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
    ExprPtr ParseExpression() { return ParseAssignment(); }

    // Ternary: cond ? a : b (lower precedence than assignment's RHS).
    ExprPtr ParseTernary() {
        ExprPtr c = ParseOr();
        if (Match(Tok::Question)) {
            ExprPtr a = ParseAssignment();
            Expect(Tok::Colon, "':'");
            ExprPtr b = ParseAssignment();
            return std::make_unique<TernaryExpr>(std::move(c), std::move(a), std::move(b));
        }
        return c;
    }
    ExprPtr ParseAssignment() {
        ExprPtr left = ParseTernary();
        if (Match(Tok::Assign)) {
            ExprPtr value = ParseAssignment();
            if (auto* var = dynamic_cast<VarExpr*>(left.get()))
                return std::make_unique<AssignExpr>(var->n, std::move(value));
            // Assigning to an array element: arr[i] = value.
            if (dynamic_cast<IndexExpr*>(left.get())) {
                auto* ix = static_cast<IndexExpr*>(left.release());
                auto out = std::make_unique<IndexAssignExpr>(
                    std::move(ix->arr), std::move(ix->index), std::move(value));
                delete ix;
                return out;
            }
            throw ScriptError("invalid assignment target");
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
        return ParsePostfix();
    }
    // Postfix indexing: base[i][j]...
    ExprPtr ParsePostfix() {
        ExprPtr e = ParsePrimary();
        while (Match(Tok::LBracket)) {
            ExprPtr idx = ParseExpression();
            Expect(Tok::RBracket, "']'");
            e = std::make_unique<IndexExpr>(std::move(e), std::move(idx));
        }
        return e;
    }
    ExprPtr ParsePrimary() {
        if (Match(Tok::Number)) return std::make_unique<NumberExpr>(Prev().number);
        if (Match(Tok::String)) return std::make_unique<StringExpr>(Prev().text);
        if (Match(Tok::True))   return std::make_unique<BoolExpr>(true);
        if (Match(Tok::False))  return std::make_unique<BoolExpr>(false);
        if (Match(Tok::LParen)) { auto e = ParseExpression(); Expect(Tok::RParen, "')'"); return e; }
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

    // Last network message popped by net_poll(), exposed via net_msg_* builtins.
    std::string netMsgChannel, netMsgData;
    float       netMsgFrom = 0.0f;

    // The scene's NetworkManager, if any (multiplayer is opt-in per scene).
    NetworkManager* Net() {
        Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
        return s ? s->FindObjectOfType<NetworkManager>() : nullptr;
    }
    // Find or create the scene's NetworkManager, wiring the host object as the
    // broadcast avatar and a default factory that mirrors remote peers as sprites.
    NetworkManager* EnsureNet() {
        if (NetworkManager* n = Net()) return n;
        if (!rt.host || !rt.host->gameObject || !rt.host->gameObject->scene()) return nullptr;
        Scene* s = rt.host->gameObject->scene();
        GameObject* netObj = s->CreateGameObject("__Network");
        NetworkManager* n = netObj->AddComponent<NetworkManager>();
        if (rt.host->transform) n->SetLocalAvatar(rt.host->transform, '@');
        n->SetRemoteFactory([s](std::uint32_t id, char) {
            GameObject* g = s->CreateGameObject("Peer" + std::to_string(id));
            g->AddComponent<SpriteRenderer>()->color = Color::FromBytes(230, 120, 90);
            return g;
        });
        return n;
    }

    void RegisterBuiltins() {
        auto& b = rt.builtins;
        auto tf = [this]() -> Transform* { return rt.host ? rt.host->transform : nullptr; };

        // ---- Debugging -------------------------------------------------
        // All of these surface in the editor's Console (it installs a log sink).
        auto joinArgs = [](std::vector<Value>& a, const char* sep = " ") {
            std::string s;
            for (std::size_t i = 0; i < a.size(); ++i) { if (i) s += sep; s += a[i].AsString(); }
            return s;
        };
        b["print"]     = [joinArgs](std::vector<Value>& a) { Log::Info("[script] ", joinArgs(a)); return Value{}; };
        b["debug_log"] = [joinArgs](std::vector<Value>& a) { Log::Info("[script] ", joinArgs(a)); return Value{}; };
        b["log_info"]  = [joinArgs](std::vector<Value>& a) { Log::Info("[script] ", joinArgs(a)); return Value{}; };
        b["log_warn"]  = [joinArgs](std::vector<Value>& a) { Log::Warning("[script] ", joinArgs(a)); return Value{}; };
        b["log_error"] = [joinArgs](std::vector<Value>& a) { Log::Error("[script] ", joinArgs(a)); return Value{}; };
        b["trace"]     = [joinArgs](std::vector<Value>& a) { Log::Trace("[script] ", joinArgs(a)); return Value{}; };
        // watch("hp", hp) -> logs "hp = 42" — quick variable inspection.
        b["watch"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) Log::Info("[watch] ", a[0].AsString(), " = ", a[1].AsString());
            else if (!a.empty()) Log::Info("[watch] ", a[0].AsString());
            return Value{};
        };
        // assert(cond [, "message"]) -> logs an error when cond is falsey.
        b["assert"] = [](std::vector<Value>& a) {
            bool ok = !a.empty() && a[0].AsFloat() != 0.0f;
            if (!ok) Log::Error("[assert] failed", a.size() > 1 ? (": " + a[1].AsString()) : std::string{});
            return Value{ok ? 1.0f : 0.0f};
        };
        // format("hp={} of {}", hp, max) -> fills each {} with the next argument.
        b["format"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            std::string fmt = a[0].AsString(), out;
            std::size_t arg = 1;
            for (std::size_t i = 0; i < fmt.size(); ++i) {
                if (i + 1 < fmt.size() && fmt[i] == '{' && fmt[i + 1] == '}') {
                    out += (arg < a.size()) ? a[arg++].AsString() : "";
                    ++i;
                } else out += fmt[i];
            }
            return Value{out};
        };
        b["concat"] = [joinArgs](std::vector<Value>& a) { return Value{joinArgs(a, "")}; };
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
        b["set_timescale"] = [](std::vector<Value>& a) { Time::SetTimeScale(a.empty() ? 1.0f : a[0].AsFloat()); return Value{}; };
        b["timescale"]     = [](std::vector<Value>&) { return Value{Time::TimeScale()}; };
        b["dt"]    = [this](std::vector<Value>&) { return Value{rt.host ? rt.host->deltaTime : 0.0f}; };
        // Scheduled callbacks: after(seconds, "fn") fires once; every(seconds,
        // "fn") repeats; cancel_timers() clears them. fn is a function in this
        // script. Great for spawn waves, respawns, cooldowns, blinking text.
        b["after"] = [this](std::vector<Value>& a) {
            if (a.size() >= 2) rt.timers.push_back({a[0].AsFloat(), 0.0f, a[1].AsString(), false});
            return Value{};
        };
        b["every"] = [this](std::vector<Value>& a) {
            if (a.size() >= 2) {
                float iv = a[0].AsFloat(); if (iv <= 0.0f) iv = 0.0001f;
                rt.timers.push_back({iv, iv, a[1].AsString(), false});
            }
            return Value{};
        };
        b["cancel_timers"] = [this](std::vector<Value>&) { rt.timers.clear(); return Value{}; };
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
        b["mouse_x"] = [](std::vector<Value>&) { return Value{Input::MousePosition().x}; };
        b["mouse_y"] = [](std::vector<Value>&) { return Value{Input::MousePosition().y}; };
        b["mouse"] = [](std::vector<Value>& a) {
            return Value{Input::GetMouseButton(a.empty() ? 0 : (int)a[0].AsFloat())};
        };
        b["mouse_down"] = [](std::vector<Value>& a) {
            return Value{Input::GetMouseButtonDown(a.empty() ? 0 : (int)a[0].AsFloat())};
        };
        b["gamepad_x"] = [](std::vector<Value>&) { return Value{Input::GamepadAxis().x}; };
        b["gamepad_y"] = [](std::vector<Value>&) { return Value{Input::GamepadAxis().y}; };
        b["gamepad"] = [](std::vector<Value>& a) {
            return Value{Input::GetGamepadButton(a.empty() ? 0 : (int)a[0].AsFloat())};
        };
        b["gamepad_down"] = [](std::vector<Value>& a) {
            return Value{Input::GetGamepadButtonDown(a.empty() ? 0 : (int)a[0].AsFloat())};
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
        // Spawn a prefab file at (x, y); returns true on success. New objects
        // are adopted next frame, so this is safe to call from update().
        b["spawn"] = [this](std::vector<Value>& a) {
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{false};
            Scene* sc = rt.host->gameObject->scene();
            if (!sc) return Value{false};
            GameObject* go = SceneSerializer::InstantiateFromFile(*sc, a[0].AsString(), nullptr);
            if (!go) return Value{false};
            if (go->transform)
                go->transform->localPosition = {a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                                 a.size() > 2 ? a[2].AsFloat() : 0.0f, 0.0f};
            return Value{true};
        };
        // spawn3(prefab, x, y, z): like spawn but places the new object in 3D.
        b["spawn3"] = [this](std::vector<Value>& a) {
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{false};
            Scene* sc = rt.host->gameObject->scene();
            if (!sc) return Value{false};
            GameObject* go = SceneSerializer::InstantiateFromFile(*sc, a[0].AsString(), nullptr);
            if (!go) return Value{false};
            if (go->transform)
                go->transform->localPosition = {a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                                 a.size() > 2 ? a[2].AsFloat() : 0.0f,
                                                 a.size() > 3 ? a[3].AsFloat() : 0.0f};
            return Value{true};
        };
        // Destroy this script's own GameObject (deferred to end of frame).
        b["destroy"] = [this](std::vector<Value>&) {
            if (rt.host && rt.host->gameObject && rt.host->gameObject->scene())
                rt.host->gameObject->scene()->Destroy(rt.host->gameObject);
            return Value{};
        };
        // Control other objects by name (show/hide UI, toggle enemies, etc.).
        auto sceneOf = [this]() -> Scene* {
            return (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
        };
        b["activate"] = [sceneOf](std::vector<Value>& a) {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString())) g->active = true;
            return Value{};
        };
        b["deactivate"] = [sceneOf](std::vector<Value>& a) {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString())) g->active = false;
            return Value{};
        };
        b["exists"] = [sceneOf](std::vector<Value>& a) {
            if (!a.empty()) if (Scene* s = sceneOf()) return Value{s->Find(a[0].AsString()) != nullptr};
            return Value{false};
        };
        b["is_active"] = [sceneOf](std::vector<Value>& a) {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString())) return Value{g->active};
            return Value{false};
        };
        // Parent this object under a named one (pick up items, mount turrets on a
        // moving platform); detach() returns it to the scene root. World position
        // is preserved across the change.
        b["set_parent"] = [this, sceneOf](std::vector<Value>& a) {
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{};
            if (Scene* s = sceneOf())
                if (GameObject* p = s->Find(a[0].AsString()))
                    rt.host->gameObject->transform->SetParent(p->transform, true);
            return Value{};
        };
        b["detach"] = [this](std::vector<Value>&) {
            if (rt.host && rt.host->gameObject)
                rt.host->gameObject->transform->SetParent(nullptr, true);
            return Value{};
        };
        b["has_parent"] = [this](std::vector<Value>&) -> Value {
            return Value{rt.host && rt.host->gameObject &&
                         rt.host->gameObject->transform->Parent() != nullptr};
        };
        // Read another object's world position by name (enemy AI chasing the
        // player, doors tracking a key, cameras following a target…).
        b["obj_x"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                return Value{g->transform->Position().x};
            return Value{0.0f};
        };
        b["obj_y"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                return Value{g->transform->Position().y};
            return Value{0.0f};
        };
        b["obj_z"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                return Value{g->transform->Position().z};
            return Value{0.0f};
        };
        // Distance from this object to a named object (0 if missing).
        b["dist_to"] = [this, sceneOf](std::vector<Value>& a) -> Value {
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{0.0f};
            Scene* s = sceneOf(); if (!s) return Value{0.0f};
            GameObject* g = s->Find(a[0].AsString()); if (!g) return Value{0.0f};
            Vec3 me = rt.host->gameObject->transform->Position();
            Vec3 ot = g->transform->Position();
            float dx = ot.x - me.x, dy = ot.y - me.y;
            return Value{Mathf::Sqrt(dx * dx + dy * dy)};
        };
        // Set this object's Rigidbody2D velocity to head toward a named object at
        // `speed` (homing projectiles, flying/seeking enemies).
        b["vel_toward"] = [this, sceneOf](std::vector<Value>& a) {
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{};
            Scene* s = sceneOf(); if (!s) return Value{};
            GameObject* g = s->Find(a[0].AsString()); if (!g) return Value{};
            auto* rb = rt.host->gameObject->GetComponent<Rigidbody2D>(); if (!rb) return Value{};
            Vec3 me = rt.host->gameObject->transform->Position();
            Vec3 ot = g->transform->Position();
            float dx = ot.x - me.x, dy = ot.y - me.y;
            float d = Mathf::Sqrt(dx * dx + dy * dy);
            float speed = a.size() > 1 ? a[1].AsFloat() : 1.0f;
            if (d > 1e-6f) rb->velocity = {dx / d * speed, dy / d * speed};
            return Value{};
        };
        // Rotate this object (about Z) to face a named object — turrets, enemies
        // aiming at the player, signposts.
        b["look_at"] = [this, tf, sceneOf](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            Transform* t = tf(); Scene* s = sceneOf();
            if (!t || !s) return Value{};
            GameObject* g = s->Find(a[0].AsString()); if (!g) return Value{};
            Vec3 me = rt.host->gameObject->transform->Position();
            Vec3 ot = g->transform->Position();
            float deg = std::atan2(ot.y - me.y, ot.x - me.x) * 57.2957795f;
            t->localRotation = Quat::Euler({0, 0, deg});
            return Value{};
        };
        // Main-camera control from script (screen-follow, cutscenes, zoom).
        b["cam_x"] = [sceneOf](std::vector<Value>&) -> Value {
            if (Scene* s = sceneOf()) if (s->mainCamera) return Value{s->mainCamera->gameObject->transform->localPosition.x};
            return Value{0.0f};
        };
        b["cam_y"] = [sceneOf](std::vector<Value>&) -> Value {
            if (Scene* s = sceneOf()) if (s->mainCamera) return Value{s->mainCamera->gameObject->transform->localPosition.y};
            return Value{0.0f};
        };
        b["set_cam"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera) {
                Transform* ct = s->mainCamera->gameObject->transform;
                ct->localPosition.x = a.size() > 0 ? a[0].AsFloat() : ct->localPosition.x;
                ct->localPosition.y = a.size() > 1 ? a[1].AsFloat() : ct->localPosition.y;
            }
            return Value{};
        };
        b["move_cam"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera) {
                Transform* ct = s->mainCamera->gameObject->transform;
                ct->localPosition.x += a.size() > 0 ? a[0].AsFloat() : 0.0f;
                ct->localPosition.y += a.size() > 1 ? a[1].AsFloat() : 0.0f;
            }
            return Value{};
        };
        b["cam_zoom"] = [sceneOf](std::vector<Value>&) -> Value {
            if (Scene* s = sceneOf()) if (s->mainCamera) return Value{s->mainCamera->orthographicSize};
            return Value{0.0f};
        };
        b["set_cam_zoom"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera && !a.empty())
                s->mainCamera->orthographicSize = Mathf::Max(0.01f, a[0].AsFloat());
            return Value{};
        };
        // Set the main camera's clear/background color (flash, day-night, fades).
        b["set_bg"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera)
                s->mainCamera->backgroundColor = {a.size() > 0 ? a[0].AsFloat() : 0.0f,
                                                  a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                                  a.size() > 2 ? a[2].AsFloat() : 0.0f,
                                                  a.size() > 3 ? a[3].AsFloat() : 1.0f};
            return Value{};
        };
        // 3D directional light: set its direction or ambient floor (day-night,
        // mood, hit flashes) — applies to the player and editor shading alike.
        b["set_light"] = [](std::vector<Value>& a) {
            SceneLight::SetDirection({a.size() > 0 ? a[0].AsFloat() : 0.0f,
                                      a.size() > 1 ? a[1].AsFloat() : -1.0f,
                                      a.size() > 2 ? a[2].AsFloat() : 0.0f});
            return Value{};
        };
        b["set_ambient"] = [](std::vector<Value>& a) {
            SceneLight::SetAmbient(a.empty() ? 0.25f : a[0].AsFloat());
            return Value{};
        };
        b["ambient"] = [](std::vector<Value>&) { return Value{SceneLight::Ambient()}; };
        // Screen size in pixels (HUD layout, clamping, spawn-at-edge).
        b["screen_w"] = [](std::vector<Value>&) { return Value{UICanvas::Width()}; };
        b["screen_h"] = [](std::vector<Value>&) { return Value{UICanvas::Height()}; };
        // Tag queries: count active objects with a tag, or find the nearest one
        // to this object (returns its name, "" if none) — enemy targeting, "all
        // coins collected?", proximity doors.
        b["count_tag"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{0.0f};
            int n = 0;
            if (Scene* s = sceneOf())
                for (const auto& o : s->Objects())
                    if (o->active && o->tag == a[0].AsString()) ++n;
            return Value{(float)n};
        };
        b["nearest_tag"] = [this, sceneOf](std::vector<Value>& a) -> Value {
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{std::string{}};
            Scene* s = sceneOf(); if (!s) return Value{std::string{}};
            Vec3 me = rt.host->gameObject->transform->Position();
            std::string best; float bestD = 0.0f;
            for (const auto& o : s->Objects()) {
                if (!o->active || o->tag != a[0].AsString()) continue;
                if (o.get() == rt.host->gameObject) continue;       // skip self
                Vec3 p = o->transform->Position();
                float dx = p.x - me.x, dy = p.y - me.y, d = dx * dx + dy * dy;
                if (best.empty() || d < bestD) { best = o->name; bestD = d; }
            }
            return Value{best};
        };
        // Drive sibling components on this GameObject.
        auto go = [this]() -> GameObject* { return rt.host ? rt.host->gameObject : nullptr; };
        b["set_text"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* tr = g->GetComponent<TextRenderer>())
                    tr->text = a.empty() ? std::string{} : a[0].AsString();
            return Value{};
        };
        b["set_color"] = [go](std::vector<Value>& a) {
            Color c{a.size() > 0 ? a[0].AsFloat() : 1.0f, a.size() > 1 ? a[1].AsFloat() : 1.0f,
                    a.size() > 2 ? a[2].AsFloat() : 1.0f, a.size() > 3 ? a[3].AsFloat() : 1.0f};
            if (GameObject* g = go()) {
                if (auto* sr = g->GetComponent<SpriteRenderer>()) sr->color = c;
                if (auto* tr = g->GetComponent<TextRenderer>()) tr->color = c;
            }
            return Value{};
        };
        b["set_texture"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* sr = g->GetComponent<SpriteRenderer>())
                    sr->texture = a.empty() ? std::string{} : a[0].AsString();
            return Value{};
        };
        b["flip_x"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* sr = g->GetComponent<SpriteRenderer>())
                    sr->flipX = a.empty() ? true : a[0].AsBool();
            return Value{};
        };
        b["flip_y"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* sr = g->GetComponent<SpriteRenderer>())
                    sr->flipY = a.empty() ? true : a[0].AsBool();
            return Value{};
        };
        b["play_sound"] = [go](std::vector<Value>&) {
            if (GameObject* g = go())
                if (auto* au = g->GetComponent<AudioSource>()) au->Play();
            return Value{};
        };
        b["set_progress"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* pb = g->GetComponent<UIProgressBar>())
                    pb->SetValue(a.empty() ? 0.0f : a[0].AsFloat());
            return Value{};
        };
        // UI slider/toggle on this object (settings menus react via on_change/on_toggle).
        b["slider_value"] = [go](std::vector<Value>&) -> Value {
            if (GameObject* g = go())
                if (auto* sl = g->GetComponent<UISlider>()) return Value{sl->value};
            return Value{0.0f};
        };
        b["set_slider"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* sl = g->GetComponent<UISlider>())
                    sl->SetValue(a.empty() ? 0.0f : a[0].AsFloat());
            return Value{};
        };
        b["toggle_on"] = [go](std::vector<Value>&) -> Value {
            if (GameObject* g = go())
                if (auto* tg = g->GetComponent<UIToggle>()) return Value{tg->on};
            return Value{false};
        };
        b["set_toggle"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* tg = g->GetComponent<UIToggle>()) tg->on = !a.empty() && a[0].AsBool();
            return Value{};
        };
        // Sibling Rigidbody2D control (jump, dash, knockback, top-down movement).
        b["velocity_x"] = [go](std::vector<Value>&) -> Value {
            if (GameObject* g = go()) if (auto* rb = g->GetComponent<Rigidbody2D>()) return Value{rb->velocity.x};
            return Value{0.0f};
        };
        b["velocity_y"] = [go](std::vector<Value>&) -> Value {
            if (GameObject* g = go()) if (auto* rb = g->GetComponent<Rigidbody2D>()) return Value{rb->velocity.y};
            return Value{0.0f};
        };
        b["set_velocity"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* rb = g->GetComponent<Rigidbody2D>())
                rb->velocity = {a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f};
            return Value{};
        };
        b["set_vx"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* rb = g->GetComponent<Rigidbody2D>())
                rb->velocity.x = a.empty() ? 0.0f : a[0].AsFloat();
            return Value{};
        };
        b["set_vy"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* rb = g->GetComponent<Rigidbody2D>())
                rb->velocity.y = a.empty() ? 0.0f : a[0].AsFloat();
            return Value{};
        };
        b["add_force"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* rb = g->GetComponent<Rigidbody2D>())
                rb->AddForce({a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f});
            return Value{};
        };
        b["add_impulse"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* rb = g->GetComponent<Rigidbody2D>())
                rb->AddImpulse({a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f});
            return Value{};
        };
        // Set a sibling UIImage's texture from script (swap icons, states).
        b["set_image"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* im = g->GetComponent<UIImage>())
                im->texture = a.empty() ? std::string{} : a[0].AsString();
            return Value{};
        };
        // Enable/disable a sibling UIButton (grey out a "Continue" entry, etc.).
        b["set_interactable"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* bt = g->GetComponent<UIButton>())
                bt->interactable = a.empty() || a[0].AsBool();
            return Value{};
        };
        // Particle FX on a sibling ParticleSystem (explosions, dust, pickups).
        b["emit"] = [go](std::vector<Value>& a) {       // one-off burst of N
            if (GameObject* g = go()) if (auto* ps = g->GetComponent<ParticleSystem>())
                ps->Emit(a.empty() ? 1 : (int)a[0].AsFloat());
            return Value{};
        };
        b["particles_on"] = [go](std::vector<Value>& a) {  // start/stop continuous emission
            if (GameObject* g = go()) if (auto* ps = g->GetComponent<ParticleSystem>())
                ps->playing = a.empty() || a[0].AsBool();
            return Value{};
        };
        b["particles_alive"] = [go](std::vector<Value>&) -> Value {
            if (GameObject* g = go()) if (auto* ps = g->GetComponent<ParticleSystem>())
                return Value{(float)ps->AliveCount()};
            return Value{0.0f};
        };
        // Sprite animation control on a sibling SpriteAnimator.
        b["play_anim"] = [go](std::vector<Value>&) {    // restart from frame 0
            if (GameObject* g = go()) if (auto* an = g->GetComponent<SpriteAnimator>())
                an->Restart();
            return Value{};
        };
        b["stop_anim"] = [go](std::vector<Value>&) {
            if (GameObject* g = go()) if (auto* an = g->GetComponent<SpriteAnimator>())
                an->playing = false;
            return Value{};
        };
        // Tilemap editing on a sibling Tilemap (procedural levels).
        b["tile_resize"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* tm = g->GetComponent<Tilemap>())
                    tm->Resize(a.size() > 0 ? (int)a[0].AsFloat() : 0, a.size() > 1 ? (int)a[1].AsFloat() : 0);
            return Value{};
        };
        b["set_tile"] = [go](std::vector<Value>& a) {
            if (a.size() >= 3) if (GameObject* g = go())
                if (auto* tm = g->GetComponent<Tilemap>())
                    tm->SetTile((int)a[0].AsFloat(), (int)a[1].AsFloat(), (int)a[2].AsFloat());
            return Value{};
        };
        b["get_tile"] = [go](std::vector<Value>& a) {
            if (a.size() >= 2) if (GameObject* g = go())
                if (auto* tm = g->GetComponent<Tilemap>())
                    return Value{(float)tm->GetTile((int)a[0].AsFloat(), (int)a[1].AsFloat())};
            return Value{0.0f};
        };
        b["tile_w"] = [go](std::vector<Value>&) { GameObject* g = go(); auto* tm = g ? g->GetComponent<Tilemap>() : nullptr; return Value{tm ? (float)tm->Width() : 0.0f}; };
        b["tile_h"] = [go](std::vector<Value>&) { GameObject* g = go(); auto* tm = g ? g->GetComponent<Tilemap>() : nullptr; return Value{tm ? (float)tm->Height() : 0.0f}; };
        // Global audio settings (for options menus).
        b["set_volume"] = [](std::vector<Value>& a) { AudioMixer::masterVolume = a.empty() ? 1.0f : a[0].AsFloat(); return Value{}; };
        b["volume"]     = [](std::vector<Value>&) { return Value{AudioMixer::masterVolume}; };
        b["mute"]       = [](std::vector<Value>& a) { AudioMixer::muted = a.empty() ? true : a[0].AsBool(); return Value{}; };
        // Physics queries: raycast_hit(ox, oy, dx, dy [, maxDist]) returns 1 if
        // the ray hits a collider, and overlap(x, y) returns 1 if a collider
        // contains that point. Great for shooting, ground checks, line-of-sight.
        b["raycast_hit"] = [this](std::vector<Value>& a) {
            if (a.size() < 4 || !rt.host || !rt.host->gameObject) return Value{false};
            Scene* sc = rt.host->gameObject->scene();
            if (!sc) return Value{false};
            Vec2 origin{a[0].AsFloat(), a[1].AsFloat()};
            Vec2 dir{a[2].AsFloat(), a[3].AsFloat()};
            float maxDist = a.size() > 4 ? a[4].AsFloat() : 1e9f;
            return Value{sc->physics().Raycast(*sc, origin, dir, maxDist).hit};
        };
        b["overlap"] = [this](std::vector<Value>& a) {
            if (a.size() < 2 || !rt.host || !rt.host->gameObject) return Value{false};
            Scene* sc = rt.host->gameObject->scene();
            if (!sc) return Value{false};
            return Value{sc->physics().OverlapPoint(*sc, Vec2{a[0].AsFloat(), a[1].AsFloat()}) != nullptr};
        };
        b["set_gravity"] = [this](std::vector<Value>& a) {
            if (rt.host && rt.host->gameObject && rt.host->gameObject->scene())
                rt.host->gameObject->scene()->physics().gravity =
                    Vec2{a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f};
            return Value{};
        };
        // Load another scene at end of frame (level transitions, restart).
        b["load_scene"] = [this](std::vector<Value>& a) {
            if (!a.empty() && rt.host && rt.host->gameObject && rt.host->gameObject->scene())
                rt.host->gameObject->scene()->RequestLoad(a[0].AsString());
            return Value{};
        };
        // Scene Manager (build list) — load by index/name, next, reload, query.
        b["load_scene_index"] = [this](std::vector<Value>& a) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            if (s && !a.empty()) return Value{SceneManager::LoadScene(*s, (int)a[0].AsFloat()) ? 1.0f : 0.0f};
            return Value{0.0f};
        };
        b["load_scene_name"] = [this](std::vector<Value>& a) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            if (s && !a.empty()) return Value{SceneManager::LoadSceneByName(*s, a[0].AsString()) ? 1.0f : 0.0f};
            return Value{0.0f};
        };
        b["load_next_scene"] = [this](std::vector<Value>&) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            return Value{(s && SceneManager::LoadNextScene(*s)) ? 1.0f : 0.0f};
        };
        b["reload_scene"] = [this](std::vector<Value>&) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            return Value{(s && SceneManager::ReloadScene(*s)) ? 1.0f : 0.0f};
        };
        b["scene_count"] = [](std::vector<Value>&) { return Value{(float)SceneManager::SceneCount()}; };
        b["scene_index"] = [](std::vector<Value>&) { return Value{(float)SceneManager::ActiveIndex()}; };
        b["scene_name"]  = [this](std::vector<Value>&) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            return Value{s ? s->Name() : std::string{}};
        };
        // ---- Multiplayer (NetworkManager) -----------------------------
        // Start hosting / join from a script; both wire the host object as the
        // broadcast avatar and mirror remote peers as sprites.
        b["net_host"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            std::uint16_t port = (std::uint16_t)(a.empty() ? 45000 : (int)a[0].AsFloat());
            return Value{(n && n->StartServer(port)) ? 1.0f : 0.0f};
        };
        b["net_join"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            std::string host = a.empty() ? "127.0.0.1" : a[0].AsString();
            std::uint16_t port = (std::uint16_t)(a.size() < 2 ? 45000 : (int)a[1].AsFloat());
            return Value{(n && n->StartClient(host, port)) ? 1.0f : 0.0f};
        };
        b["net_disconnect"] = [this](std::vector<Value>&) {
            if (NetworkManager* n = Net()) n->Stop();
            return Value{};
        };
        b["net_connected"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{(n && n->IsConnected()) ? 1.0f : 0.0f}; };
        b["net_is_server"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{(n && n->IsServer()) ? 1.0f : 0.0f}; };
        b["net_is_client"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{(n && n->IsClient()) ? 1.0f : 0.0f}; };
        b["net_id"]    = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? (float)n->LocalId() : 0.0f}; };
        b["net_peers"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? (float)n->PeerCount() : 0.0f}; };
        b["net_ping"]  = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? n->RttMs() : 0.0f}; };
        // Lobby room: set before net_host/net_join so peers only see their room.
        b["net_room"]  = [this](std::vector<Value>& a) {
            if (!a.empty()) { if (NetworkManager* n = EnsureNet()) n->SetRoom(a[0].AsString()); return Value{a[0].AsString()}; }
            NetworkManager* n = Net();
            return Value{n ? n->Room() : std::string{}};
        };
        // Lobby ready-up: clients mark ready; the host polls and starts the match.
        b["net_ready"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net()) n->SetReady(a.empty() ? true : a[0].AsFloat() != 0.0f);
            return Value{};
        };
        b["net_ready_count"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? (float)n->ReadyCount() : 0.0f}; };
        b["net_all_ready"]   = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{(n && n->AllReady()) ? 1.0f : 0.0f}; };
        b["net_start_match"] = [this](std::vector<Value>&) { if (NetworkManager* n = Net()) n->StartMatch(); return Value{}; };
        b["net_match_started"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{(n && n->MatchStarted()) ? 1.0f : 0.0f}; };
        b["net_name"]  = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net()) { if (!a.empty()) n->SetLocalName(a[0].AsString()); return Value{n->LocalName()}; }
            return Value{std::string{}};
        };
        // Send a message to everyone, or to one peer id.
        b["net_send"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && a.size() >= 2) n->Send(a[0].AsString(), a[1].AsString());
            return Value{};
        };
        b["net_send_to"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && a.size() >= 3) n->SendTo((std::uint32_t)a[0].AsFloat(), a[1].AsString(), a[2].AsString());
            return Value{};
        };
        // Reliable (resent until acked) variants for events you can't drop.
        b["net_send_reliable"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && a.size() >= 2) n->SendReliable(a[0].AsString(), a[1].AsString());
            return Value{};
        };
        b["net_send_reliable_to"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && a.size() >= 3) n->SendReliableTo((std::uint32_t)a[0].AsFloat(), a[1].AsString(), a[2].AsString());
            return Value{};
        };
        // Moderation: host kicks a peer; clients can check if they were kicked.
        b["net_kick"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && !a.empty()) n->Kick((std::uint32_t)a[0].AsFloat(), a.size() > 1 ? a[1].AsString() : std::string{});
            return Value{};
        };
        b["net_was_kicked"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{(n && n->WasKicked()) ? 1.0f : 0.0f}; };
        b["net_server_name"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? n->ServerName() : std::string{}}; };
        // Drain one received message into net_msg_* accessors; returns 1 if one
        // was popped (use in a while-loop), 0 when the inbox is empty.
        b["net_poll"] = [this](std::vector<Value>&) {
            NetworkManager* n = Net();
            NetworkManager::NetMessage m;
            if (n && n->PopMessage(m)) {
                netMsgChannel = m.channel; netMsgData = m.data; netMsgFrom = (float)m.from;
                return Value{1.0f};
            }
            netMsgChannel.clear(); netMsgData.clear(); netMsgFrom = 0.0f;
            return Value{0.0f};
        };
        b["net_msg_channel"] = [this](std::vector<Value>&) { return Value{netMsgChannel}; };
        b["net_msg_data"]    = [this](std::vector<Value>&) { return Value{netMsgData}; };
        b["net_msg_from"]    = [this](std::vector<Value>&) { return Value{netMsgFrom}; };
        // Server-authoritative synced variables: net_set on the server (or a
        // client request) propagates to every peer; net_get reads the local copy.
        b["net_set"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && a.size() >= 2) n->SetVar(a[0].AsString(), a[1].AsString());
            return Value{};
        };
        // Spawn a prefab on every peer (replicated): net_spawn("file", x, y[, z]).
        b["net_spawn"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && !a.empty())
                n->Spawn(a[0].AsString(), {a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                           a.size() > 2 ? a[2].AsFloat() : 0.0f,
                                           a.size() > 3 ? a[3].AsFloat() : 0.0f});
            return Value{};
        };
        b["net_get"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            return Value{(n && !a.empty()) ? n->GetVar(a[0].AsString()) : std::string{}};
        };
        // ---- Steam (shared process-wide service) ----------------------
        b["steam_name"]   = [](std::vector<Value>&) { return Value{Steam::Get().UserName()}; };
        b["steam_unlock"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            bool ok = Steam::Get().UnlockAchievement(a[0].AsString());
            Steam::Get().StoreStats();
            return Value{ok ? 1.0f : 0.0f};
        };
        b["steam_is_unlocked"] = [](std::vector<Value>& a) {
            return Value{(!a.empty() && Steam::Get().IsAchievementUnlocked(a[0].AsString())) ? 1.0f : 0.0f};
        };
        b["steam_clear"] = [](std::vector<Value>& a) {
            if (!a.empty()) Steam::Get().ClearAchievement(a[0].AsString());
            return Value{};
        };
        b["steam_set_stat"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) Steam::Get().SetStat(a[0].AsString(), a[1].AsFloat());
            return Value{};
        };
        b["steam_get_stat"] = [](std::vector<Value>& a) {
            return Value{a.empty() ? 0.0f : Steam::Get().GetStat(a[0].AsString())};
        };
        b["steam_inc_stat"] = [](std::vector<Value>& a) {
            return Value{a.size() < 2 ? 0.0f : Steam::Get().IncrementStat(a[0].AsString(), a[1].AsFloat())};
        };
        b["steam_store"] = [](std::vector<Value>&) { return Value{Steam::Get().StoreStats() ? 1.0f : 0.0f}; };
        b["steam_leaderboard"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) Steam::Get().UploadLeaderboardScore(a[0].AsString(), (std::int32_t)a[1].AsFloat());
            return Value{};
        };
        b["steam_cloud_write"] = [](std::vector<Value>& a) {
            return Value{(a.size() >= 2 && Steam::Get().CloudWrite(a[0].AsString(), a[1].AsString())) ? 1.0f : 0.0f};
        };
        b["steam_cloud_read"] = [](std::vector<Value>& a) {
            return Value{a.empty() ? std::string{} : Steam::Get().CloudRead(a[0].AsString())};
        };
        b["steam_progress"] = [](std::vector<Value>& a) {
            if (a.size() >= 3) Steam::Get().IndicateAchievementProgress(a[0].AsString(), (std::uint32_t)a[1].AsFloat(), (std::uint32_t)a[2].AsFloat());
            return Value{};
        };
        b["steam_presence"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) Steam::Get().SetRichPresence(a[0].AsString(), a[1].AsString());
            return Value{};
        };
        b["steam_friends"] = [](std::vector<Value>&) { return Value{(float)Steam::Get().FriendCount()}; };
        b["steam_owns"] = [](std::vector<Value>& a) { return Value{Steam::Get().OwnsApp(a.empty() ? 0u : (std::uint32_t)a[0].AsFloat()) ? 1.0f : 0.0f}; };
        b["steam_owns_dlc"] = [](std::vector<Value>& a) { return Value{Steam::Get().IsDlcInstalled(a.empty() ? 0u : (std::uint32_t)a[0].AsFloat()) ? 1.0f : 0.0f}; };
        b["steam_achievement_count"] = [](std::vector<Value>&) { return Value{(float)Steam::Get().AchievementCount()}; };
        b["steam_language"] = [](std::vector<Value>&) { return Value{Steam::Get().Language()}; };
        // Top-N leaderboard entries as an array of "rank,name,score" strings.
        b["steam_leaderboard_top"] = [](std::vector<Value>& a) {
            Value v = Value::MakeArray();
            auto arr = v.AsArray();
            if (!a.empty() && arr) {
                int n = a.size() > 1 ? (int)a[1].AsFloat() : 10;
                for (auto& e : Steam::Get().DownloadLeaderboardTop(a[0].AsString(), n))
                    arr->push_back(Value{std::to_string(e.rank) + "," + e.name + "," + std::to_string(e.score)});
            }
            return v;
        };
        b["steam_overlay"] = [](std::vector<Value>& a) {
            Steam::Get().ActivateOverlay(a.empty() ? "friends" : a[0].AsString());
            return Value{};
        };
        // Persistent prefs (high scores, settings) — survive across runs.
        b["prefs_set"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) {
                if (a[1].IsString()) Prefs::SetString(a[0].AsString(), a[1].AsString());
                else Prefs::SetFloat(a[0].AsString(), a[1].AsFloat());
            }
            return Value{};
        };
        b["prefs_get"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            return Value{Prefs::GetFloat(a[0].AsString(), a.size() > 1 ? a[1].AsFloat() : 0.0f)};
        };
        b["prefs_get_str"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            return Value{Prefs::GetString(a[0].AsString(), a.size() > 1 ? a[1].AsString() : std::string{})};
        };
        b["prefs_save"] = [](std::vector<Value>& a) {
            return Value{Prefs::Save(a.empty() ? "game.okayprefs" : a[0].AsString())};
        };
        b["prefs_load"] = [](std::vector<Value>& a) {
            return Value{Prefs::Load(a.empty() ? "game.okayprefs" : a[0].AsString())};
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
        b["hypot"] = [](std::vector<Value>& a) { float x = a.size() > 0 ? a[0].AsFloat() : 0, y = a.size() > 1 ? a[1].AsFloat() : 0; return Value{Mathf::Sqrt(x * x + y * y)}; };
        b["atan"]  = [](std::vector<Value>& a) { return Value{std::atan(a.empty() ? 0 : a[0].AsFloat())}; };
        b["asin"]  = [](std::vector<Value>& a) { return Value{std::asin(a.empty() ? 0 : a[0].AsFloat())}; };
        b["acos"]  = [](std::vector<Value>& a) { return Value{std::acos(a.empty() ? 0 : a[0].AsFloat())}; };
        b["log"]   = [](std::vector<Value>& a) { return Value{std::log(a.empty() ? 1 : a[0].AsFloat())}; };
        b["exp"]   = [](std::vector<Value>& a) { return Value{std::exp(a.empty() ? 0 : a[0].AsFloat())}; };
        // Wrap a value into [lo, hi) — angles, looping indices, scrolling.
        b["wrap"]  = [](std::vector<Value>& a) {
            float v = a.size() > 0 ? a[0].AsFloat() : 0, lo = a.size() > 1 ? a[1].AsFloat() : 0, hi = a.size() > 2 ? a[2].AsFloat() : 1;
            float span = hi - lo;
            if (span <= 0.0f) return Value{lo};
            float r = std::fmod(v - lo, span);
            if (r < 0) r += span;
            return Value{lo + r};
        };
        // Triangle wave 0..len..0 — patrols, breathing scale, bobbing.
        b["ping_pong"] = [](std::vector<Value>& a) {
            float t = a.size() > 0 ? a[0].AsFloat() : 0, len = a.size() > 1 ? a[1].AsFloat() : 1;
            if (len <= 0.0f) return Value{0.0f};
            float m = std::fmod(t, 2.0f * len);
            if (m < 0) m += 2.0f * len;
            return Value{m <= len ? m : 2.0f * len - m};
        };
        // Smooth (ease in/out) interpolation between a and b.
        b["smoothstep"] = [](std::vector<Value>& a) {
            float x = a.size() > 0 ? a[0].AsFloat() : 0, y = a.size() > 1 ? a[1].AsFloat() : 1, t = a.size() > 2 ? a[2].AsFloat() : 0;
            t = Mathf::Clamp01(t);
            t = t * t * (3.0f - 2.0f * t);
            return Value{Mathf::Lerp(x, y, t)};
        };
        // Angle in degrees from (x1,y1) toward (x2,y2).
        b["angle_to"] = [](std::vector<Value>& a) {
            if (a.size() < 4) return Value{0.0f};
            float dx = a[2].AsFloat() - a[0].AsFloat(), dy = a[3].AsFloat() - a[1].AsFloat();
            return Value{std::atan2(dy, dx) * 57.2957795f};
        };
        // True with probability p (0..1) — random events, drops, spawns.
        b["chance"] = [](std::vector<Value>& a) {
            float p = a.empty() ? 0.5f : a[0].AsFloat();
            return Value{Random::Shared().Range(0.0f, 1.0f) < p};
        };
        // Arrays/lists.
        b["array"] = [](std::vector<Value>& a) {
            Value v = Value::MakeArray();
            auto arr = v.AsArray();
            for (auto& e : a) arr->push_back(e); // array(1,2,3) -> [1,2,3]
            return v;
        };
        b["count"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray()) return Value{(float)arr->size()};
            return Value{0.0f};
        };
        b["push"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) arr->push_back(a[1]);
            return a.empty() ? Value{} : a[0];
        };
        b["pop"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray())
                if (!arr->empty()) { Value last = arr->back(); arr->pop_back(); return last; }
            return Value{};
        };
        b["contains"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                std::string n = a[1].AsString();
                for (auto& e : *arr) if (e.AsString() == n) return Value{true};
            }
            return Value{false};
        };
        b["index_of"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                std::string n = a[1].AsString();
                for (std::size_t i = 0; i < arr->size(); ++i)
                    if ((*arr)[i].AsString() == n) return Value{(float)i};
            }
            return Value{-1.0f};
        };
        b["remove_at"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                int i = (int)a[1].AsFloat();
                if (i >= 0 && i < (int)arr->size()) arr->erase(arr->begin() + i);
            }
            return a.empty() ? Value{} : a[0];
        };
        b["sum"] = [](std::vector<Value>& a) {
            float s = 0;
            if (!a.empty()) if (auto arr = a[0].AsArray()) for (auto& e : *arr) s += e.AsFloat();
            return Value{s};
        };
        b["min_of"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray()) if (!arr->empty()) {
                float m = (*arr)[0].AsFloat();
                for (auto& e : *arr) m = Mathf::Min(m, e.AsFloat());
                return Value{m};
            }
            return Value{0.0f};
        };
        b["max_of"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray()) if (!arr->empty()) {
                float m = (*arr)[0].AsFloat();
                for (auto& e : *arr) m = Mathf::Max(m, e.AsFloat());
                return Value{m};
            }
            return Value{0.0f};
        };
        b["reverse"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray()) std::reverse(arr->begin(), arr->end());
            return a.empty() ? Value{} : a[0];
        };
        b["sort_num"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray())
                std::sort(arr->begin(), arr->end(),
                          [](const Value& x, const Value& y) { return x.AsFloat() < y.AsFloat(); });
            return a.empty() ? Value{} : a[0];
        };
        b["choose"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray()) if (!arr->empty())
                return (*arr)[(std::size_t)Random::Shared().Range(0, (int)arr->size() - 1)];
            return Value{};
        };
        b["shuffle"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray())
                for (int i = (int)arr->size() - 1; i > 0; --i)
                    std::swap((*arr)[i], (*arr)[Random::Shared().Range(0, i)]);
            return a.empty() ? Value{} : a[0];
        };
        b["randi"] = [](std::vector<Value>& a) {
            int lo = a.size() > 0 ? (int)a[0].AsFloat() : 0;
            int hi = a.size() > 1 ? (int)a[1].AsFloat() : 1;
            if (hi < lo) std::swap(lo, hi);
            return Value{(float)Random::Shared().Range(lo, hi)};
        };
        b["move_toward"] = [](std::vector<Value>& a) {
            float cur = a.size() > 0 ? a[0].AsFloat() : 0, tgt = a.size() > 1 ? a[1].AsFloat() : 0;
            float maxD = a.size() > 2 ? Mathf::Abs(a[2].AsFloat()) : 0;
            if (Mathf::Abs(tgt - cur) <= maxD) return Value{tgt};
            return Value{cur + (tgt > cur ? maxD : -maxD)};
        };
        // String helpers.
        b["str_len"] = [](std::vector<Value>& a) { return Value{a.empty() ? 0.0f : (float)a[0].AsString().size()}; };
        b["upper"] = [](std::vector<Value>& a) {
            std::string s = a.empty() ? "" : a[0].AsString();
            for (auto& c : s) c = (char)std::toupper((unsigned char)c);
            return Value{s};
        };
        b["lower"] = [](std::vector<Value>& a) {
            std::string s = a.empty() ? "" : a[0].AsString();
            for (auto& c : s) c = (char)std::tolower((unsigned char)c);
            return Value{s};
        };
        b["substr"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            std::string s = a[0].AsString();
            int start = a.size() > 1 ? (int)a[1].AsFloat() : 0;
            int n = a.size() > 2 ? (int)a[2].AsFloat() : (int)s.size();
            if (start < 0) start = 0;
            if (start > (int)s.size()) return Value{std::string{}};
            if (n < 0) n = 0;
            return Value{s.substr(start, n)};
        };
        b["char_at"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            std::string s = a[0].AsString();
            int i = a.size() > 1 ? (int)a[1].AsFloat() : 0;
            if (i < 0 || i >= (int)s.size()) return Value{std::string{}};
            return Value{std::string(1, s[i])};
        };
        b["str_find"] = [](std::vector<Value>& a) {
            if (a.size() < 2) return Value{-1.0f};
            auto pos = a[0].AsString().find(a[1].AsString());
            return Value{pos == std::string::npos ? -1.0f : (float)pos};
        };
        b["str_contains"] = [](std::vector<Value>& a) {
            if (a.size() < 2) return Value{false};
            return Value{a[0].AsString().find(a[1].AsString()) != std::string::npos};
        };
        b["starts_with"] = [](std::vector<Value>& a) {
            if (a.size() < 2) return Value{false};
            const std::string s = a[0].AsString(), p = a[1].AsString();
            return Value{s.size() >= p.size() && s.compare(0, p.size(), p) == 0};
        };
        b["ends_with"] = [](std::vector<Value>& a) {
            if (a.size() < 2) return Value{false};
            const std::string s = a[0].AsString(), p = a[1].AsString();
            return Value{s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0};
        };
        // Replace every occurrence of `find` with `repl`.
        b["replace"] = [](std::vector<Value>& a) {
            if (a.size() < 3) return Value{a.empty() ? std::string{} : a[0].AsString()};
            std::string s = a[0].AsString(), find = a[1].AsString(), repl = a[2].AsString();
            if (find.empty()) return Value{s};
            std::size_t pos = 0;
            while ((pos = s.find(find, pos)) != std::string::npos) {
                s.replace(pos, find.size(), repl);
                pos += repl.size();
            }
            return Value{s};
        };
        // Trim leading/trailing ASCII whitespace.
        b["trim"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            std::string s = a[0].AsString();
            auto notspace = [](unsigned char c) { return !std::isspace(c); };
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
            s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
            return Value{s};
        };
        // Repeat a string n times ("=" * 10 style separators/bars).
        b["repeat"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            std::string s = a[0].AsString(), out;
            int n = a.size() > 1 ? (int)a[1].AsFloat() : 0;
            for (int i = 0; i < n; ++i) out += s;
            return Value{out};
        };
        b["to_num"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            try { return Value{std::stof(a[0].AsString())}; } catch (...) { return Value{0.0f}; }
        };
        b["to_str"] = [](std::vector<Value>& a) { return Value{a.empty() ? std::string{} : a[0].AsString()}; };
        b["split"] = [](std::vector<Value>& a) {
            Value out = Value::MakeArray();
            if (a.empty()) return out;
            std::string s = a[0].AsString();
            std::string sep = a.size() > 1 ? a[1].AsString() : " ";
            auto arr = out.AsArray();
            if (sep.empty()) { for (char c : s) arr->push_back(std::string(1, c)); return out; }
            std::size_t pos = 0, next;
            while ((next = s.find(sep, pos)) != std::string::npos) {
                arr->push_back(s.substr(pos, next - pos));
                pos = next + sep.size();
            }
            arr->push_back(s.substr(pos));
            return out;
        };
        b["join"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            auto arr = a[0].AsArray();
            std::string sep = a.size() > 1 ? a[1].AsString() : "";
            if (!arr) return Value{std::string{}};
            std::string s;
            for (std::size_t i = 0; i < arr->size(); ++i) { if (i) s += sep; s += (*arr)[i].AsString(); }
            return Value{s};
        };
        // Maps / dictionaries (string keys).
        b["map"] = [](std::vector<Value>&) { return Value::MakeMap(); };
        b["map_set"] = [](std::vector<Value>& a) {
            if (a.size() >= 3) if (auto m = a[0].AsMap()) (*m)[a[1].AsString()] = a[2];
            return a.empty() ? Value{} : a[0];
        };
        b["map_get"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto m = a[0].AsMap()) {
                auto it = m->find(a[1].AsString());
                if (it != m->end()) return it->second;
            }
            return a.size() > 2 ? a[2] : Value{};
        };
        b["map_has"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto m = a[0].AsMap()) return Value{m->count(a[1].AsString()) != 0};
            return Value{false};
        };
        b["map_remove"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto m = a[0].AsMap()) m->erase(a[1].AsString());
            return a.empty() ? Value{} : a[0];
        };
        b["map_keys"] = [](std::vector<Value>& a) {
            Value out = Value::MakeArray();
            if (!a.empty()) if (auto m = a[0].AsMap()) {
                auto arr = out.AsArray();
                for (auto& kv : *m) arr->push_back(kv.first);
            }
            return out;
        };
        b["map_count"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto m = a[0].AsMap()) return Value{(float)m->size()};
            return Value{0.0f};
        };
        b["pi"]    = [](std::vector<Value>&) { return Value{Mathf::PI}; };
        b["deg2rad"] = [](std::vector<Value>& a) { return Value{(a.empty() ? 0 : a[0].AsFloat()) * Mathf::Deg2Rad}; };
        b["rad2deg"] = [](std::vector<Value>& a) { return Value{(a.empty() ? 0 : a[0].AsFloat()) * Mathf::Rad2Deg}; };
        // Transform helpers
        b["set_x"] = [tf](std::vector<Value>& a) { if (Transform* t = tf()) t->localPosition.x = a.empty() ? 0 : a[0].AsFloat(); return Value{}; };
        b["set_y"] = [tf](std::vector<Value>& a) { if (Transform* t = tf()) t->localPosition.y = a.empty() ? 0 : a[0].AsFloat(); return Value{}; };
        b["set_z"] = [tf](std::vector<Value>& a) { if (Transform* t = tf()) t->localPosition.z = a.empty() ? 0 : a[0].AsFloat(); return Value{}; };
        b["pos_z"] = [tf](std::vector<Value>&) { Transform* t = tf(); return Value{t ? t->localPosition.z : 0.0f}; };
        // 3D movement / placement / rotation.
        b["move3"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf())
                t->Translate({a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f,
                              a.size() > 2 ? a[2].AsFloat() : 0.0f});
            return Value{};
        };
        b["set_pos3"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf())
                t->localPosition = {a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                    a.size() > 2 ? a[2].AsFloat() : 0.0f};
            return Value{};
        };
        b["rotate3"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf())
                t->Rotate({a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f,
                           a.size() > 2 ? a[2].AsFloat() : 0.0f});
            return Value{};
        };
        // Absolute 3D euler rotation (degrees) — face a direction outright.
        b["set_rot3"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf())
                t->localRotation = Quat::Euler({a.size() > 0 ? a[0].AsFloat() : 0.0f,
                                                a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                                a.size() > 2 ? a[2].AsFloat() : 0.0f});
            return Value{};
        };
        // 3D scale: set all axes, or read one.
        b["set_scale3"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf())
                t->localScale = {a.size() > 0 ? a[0].AsFloat() : 1.0f, a.size() > 1 ? a[1].AsFloat() : 1.0f,
                                 a.size() > 2 ? a[2].AsFloat() : 1.0f};
            return Value{};
        };
        b["set_scale"] = [tf](std::vector<Value>& a) {     // uniform scale
            if (Transform* t = tf()) { float s = a.empty() ? 1.0f : a[0].AsFloat(); t->localScale = {s, s, s}; }
            return Value{};
        };
        b["scale_x"] = [tf](std::vector<Value>&) { Transform* t = tf(); return Value{t ? t->localScale.x : 1.0f}; };
        b["scale_y"] = [tf](std::vector<Value>&) { Transform* t = tf(); return Value{t ? t->localScale.y : 1.0f}; };
        b["scale_z"] = [tf](std::vector<Value>&) { Transform* t = tf(); return Value{t ? t->localScale.z : 1.0f}; };
        // Full 3D distance to a named object (vs dist_to which ignores Z).
        b["dist3_to"] = [this, sceneOf](std::vector<Value>& a) -> Value {
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{0.0f};
            Scene* s = sceneOf(); if (!s) return Value{0.0f};
            GameObject* g = s->Find(a[0].AsString()); if (!g) return Value{0.0f};
            Vec3 d = g->transform->Position() - rt.host->gameObject->transform->Position();
            return Value{d.Magnitude()};
        };
        // Move toward a named object in 3D by speed*dt, without overshooting
        // (3D chase / homing). Returns the remaining distance.
        b["move_toward3"] = [this, tf, sceneOf](std::vector<Value>& a) -> Value {
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{0.0f};
            Transform* t = tf(); Scene* s = sceneOf();
            if (!t || !s) return Value{0.0f};
            GameObject* g = s->Find(a[0].AsString()); if (!g) return Value{0.0f};
            Vec3 me = rt.host->gameObject->transform->Position();
            Vec3 to = g->transform->Position() - me;
            float d = to.Magnitude();
            float step = (a.size() > 1 ? a[1].AsFloat() : 1.0f) * rt.host->deltaTime;
            if (d <= step || d < 1e-6f) t->localPosition += to;             // arrive
            else t->localPosition += to * (step / d);
            return Value{d};
        };
        // Rotate this object so its forward (+Z) faces a named object in 3D
        // (turrets/cameras tracking a target in a 3D scene).
        b["look_at3"] = [this, tf, sceneOf](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            Transform* t = tf(); Scene* s = sceneOf();
            if (!t || !s) return Value{};
            GameObject* g = s->Find(a[0].AsString()); if (!g) return Value{};
            Vec3 dir = g->transform->Position() - rt.host->gameObject->transform->Position();
            float len = dir.Magnitude();
            if (len < 1e-6f) return Value{};
            float yaw = std::atan2(dir.x, dir.z) * 57.2957795f;
            float pitch = -std::asin(dir.y / len) * 57.2957795f;
            t->localRotation = Quat::Euler(pitch, yaw, 0.0f);
            return Value{};
        };
        // Swap a sibling MeshRenderer's primitive at runtime (morph, LOD, states).
        b["set_mesh"] = [this](std::vector<Value>& a) {
            if (!a.empty() && rt.host && rt.host->gameObject)
                if (auto* mr = rt.host->gameObject->GetComponent<MeshRenderer>())
                    mr->mesh = Mesh::FromName(a[0].AsString());
            return Value{};
        };

        // ===== Expanded API ============================================

        // --- 3D Rigidbody control (sibling Rigidbody3D) ---
        b["velocity_z"] = [go](std::vector<Value>&) -> Value {
            if (GameObject* g = go()) if (auto* rb = g->GetComponent<Rigidbody3D>()) return Value{rb->velocity.z};
            return Value{0.0f};
        };
        b["set_velocity3"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* rb = g->GetComponent<Rigidbody3D>())
                rb->velocity = {a.size() > 0 ? a[0].AsFloat() : 0.0f,
                                a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                a.size() > 2 ? a[2].AsFloat() : 0.0f};
            return Value{};
        };
        b["set_vz"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* rb = g->GetComponent<Rigidbody3D>())
                rb->velocity.z = a.empty() ? 0.0f : a[0].AsFloat();
            return Value{};
        };
        b["add_force3"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* rb = g->GetComponent<Rigidbody3D>())
                rb->AddForce({a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f,
                              a.size() > 2 ? a[2].AsFloat() : 0.0f});
            return Value{};
        };
        b["add_impulse3"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* rb = g->GetComponent<Rigidbody3D>())
                rb->AddImpulse({a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                a.size() > 2 ? a[2].AsFloat() : 0.0f});
            return Value{};
        };
        // Jump helper: set vertical velocity straight up (2D uses y, 3D uses y too).
        b["jump"] = [go](std::vector<Value>& a) {
            float v = a.empty() ? 5.0f : a[0].AsFloat();
            if (GameObject* g = go()) {
                if (auto* rb = g->GetComponent<Rigidbody2D>()) rb->velocity.y = v;
                if (auto* rb3 = g->GetComponent<Rigidbody3D>()) rb3->velocity.y = v;
            }
            return Value{};
        };

        // --- This object's identity / state ---
        b["name"] = [go](std::vector<Value>&) -> Value {
            GameObject* g = go(); return Value{g ? g->name : std::string{}};
        };
        b["set_name"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) g->name = a.empty() ? std::string{} : a[0].AsString();
            return Value{};
        };
        b["tag"] = [go](std::vector<Value>&) -> Value {
            GameObject* g = go(); return Value{g ? g->tag : std::string{}};
        };
        b["set_tag"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) g->tag = a.empty() ? std::string{} : a[0].AsString();
            return Value{};
        };
        b["has_tag"] = [go](std::vector<Value>& a) -> Value {
            GameObject* g = go(); return Value{g && !a.empty() && g->tag == a[0].AsString()};
        };
        b["set_active"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) g->active = a.empty() || a[0].AsBool();
            return Value{};
        };
        b["self_active"] = [go](std::vector<Value>&) -> Value {
            GameObject* g = go(); return Value{g ? g->active : false};
        };
        // Destroy a named object (e.g. a collected pickup, a defeated enemy).
        b["destroy_obj"] = [sceneOf](std::vector<Value>& a) {
            if (!a.empty()) if (Scene* s = sceneOf())
                if (GameObject* g = s->Find(a[0].AsString())) s->Destroy(g);
            return Value{};
        };

        // --- Facing vectors (from this object's rotation) ---
        b["forward_x"] = [tf](std::vector<Value>&) -> Value { Transform* t = tf(); return Value{t ? t->Forward().x : 0.0f}; };
        b["forward_y"] = [tf](std::vector<Value>&) -> Value { Transform* t = tf(); return Value{t ? t->Forward().y : 0.0f}; };
        b["forward_z"] = [tf](std::vector<Value>&) -> Value { Transform* t = tf(); return Value{t ? t->Forward().z : 1.0f}; };
        b["right_x"]   = [tf](std::vector<Value>&) -> Value { Transform* t = tf(); return Value{t ? t->Right().x : 1.0f}; };
        b["right_y"]   = [tf](std::vector<Value>&) -> Value { Transform* t = tf(); return Value{t ? t->Right().y : 0.0f}; };
        b["right_z"]   = [tf](std::vector<Value>&) -> Value { Transform* t = tf(); return Value{t ? t->Right().z : 0.0f}; };
        // Move along this object's own facing axes (forward / strafe), 3D.
        b["move_forward"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf()) t->localPosition = t->localPosition + t->Forward() * (a.empty() ? 0.0f : a[0].AsFloat());
            return Value{};
        };
        b["move_right"] = [tf](std::vector<Value>& a) {
            if (Transform* t = tf()) t->localPosition = t->localPosition + t->Right() * (a.empty() ? 0.0f : a[0].AsFloat());
            return Value{};
        };

        // --- More input ---
        b["key_up"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{false};
            std::string s = a[0].AsString();
            return Value{!s.empty() && Input::GetKeyUp(s[0])};
        };
        b["mouse_up"] = [](std::vector<Value>& a) {
            return Value{Input::GetMouseButtonUp(a.empty() ? 0 : (int)a[0].AsFloat())};
        };

        // --- More math / utility ---
        b["clamp01"] = [](std::vector<Value>& a) { return Value{Mathf::Clamp01(a.empty() ? 0.0f : a[0].AsFloat())}; };
        b["dist3"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 6) return Value{0.0f};
            float dx = a[3].AsFloat() - a[0].AsFloat(), dy = a[4].AsFloat() - a[1].AsFloat(),
                  dz = a[5].AsFloat() - a[2].AsFloat();
            return Value{Mathf::Sqrt(dx * dx + dy * dy + dz * dz)};
        };
        b["fps"] = [this](std::vector<Value>&) -> Value {
            float d = rt.host ? rt.host->deltaTime : 0.0f;
            return Value{d > 1e-6f ? 1.0f / d : 0.0f};
        };
        // 3D physics: ground/line-of-sight check via a 3D ray, and gravity.
        b["raycast_hit3"] = [this](std::vector<Value>& a) -> Value {
            if (a.size() < 6 || !rt.host || !rt.host->gameObject) return Value{false};
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value{false};
            Vec3 o{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            Vec3 d{a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()};
            float maxDist = a.size() > 6 ? a[6].AsFloat() : 1e9f;
            return Value{sc->physics3D().Raycast(*sc, o, d, maxDist).hit};
        };
        b["set_gravity3"] = [this](std::vector<Value>& a) {
            if (rt.host && rt.host->gameObject && rt.host->gameObject->scene())
                rt.host->gameObject->scene()->physics3D().gravity =
                    Vec3{a.size() > 0 ? a[0].AsFloat() : 0.0f,
                         a.size() > 1 ? a[1].AsFloat() : -9.81f,
                         a.size() > 2 ? a[2].AsFloat() : 0.0f};
            return Value{};
        };

        // --- Material control on a sibling MeshRenderer ---
        b["set_emissive"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* mr = g->GetComponent<MeshRenderer>())
                mr->emissive = {a.size() > 0 ? a[0].AsFloat() : 0.0f,
                                a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                a.size() > 2 ? a[2].AsFloat() : 0.0f, 1.0f};
            return Value{};
        };
        b["set_unlit"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* mr = g->GetComponent<MeshRenderer>())
                mr->unlit = a.empty() ? true : a[0].AsBool();
            return Value{};
        };
        b["set_specular"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* mr = g->GetComponent<MeshRenderer>())
                mr->specular = a.empty() ? 0.0f : a[0].AsFloat();
            return Value{};
        };
        b["set_mesh_texture"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* mr = g->GetComponent<MeshRenderer>())
                mr->texture = a.empty() ? std::string{} : a[0].AsString();
            return Value{};
        };
        b["set_tiling"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* mr = g->GetComponent<MeshRenderer>())
                mr->tiling = {a.size() > 0 ? a[0].AsFloat() : 1.0f, a.size() > 1 ? a[1].AsFloat() : 1.0f};
            return Value{};
        };
        // --- Bridge to the visual-script (ActionList) shared variables, so
        //     OkayScript and the no-code Actions can read/write the same state. ---
        b["gc_set"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) ActionList::Vars()[a[0].AsString()] = a[1].AsFloat();
            return Value{};
        };
        b["gc_get"] = [](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{0.0f};
            auto& m = ActionList::Vars();
            auto it = m.find(a[0].AsString());
            return Value{it != m.end() ? it->second : 0.0f};
        };
        // Broadcast a named signal to every Actions (OnMessage) list in the scene.
        b["send_message"] = [this](std::vector<Value>& a) {
            if (!a.empty() && rt.host && rt.host->gameObject && rt.host->gameObject->scene())
                for (ActionList* al : rt.host->gameObject->scene()->FindObjectsOfType<ActionList>())
                    al->ReceiveMessage(a[0].AsString());
            return Value{};
        };

        // --- Sprite glyph/size on a sibling SpriteRenderer ---
        b["set_glyph"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* sr = g->GetComponent<SpriteRenderer>()) {
                std::string s = a.empty() ? std::string{} : a[0].AsString();
                if (!s.empty()) sr->glyph = s[0];
            }
            return Value{};
        };
        b["sprite_size"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* sr = g->GetComponent<SpriteRenderer>())
                sr->size = {a.size() > 0 ? a[0].AsFloat() : 1.0f, a.size() > 1 ? a[1].AsFloat() : 1.0f};
            return Value{};
        };
        // --- Call one of this script's own functions by name (dynamic dispatch:
        //     state machines, command tables, "call the handler for this state"). ---
        b["call"] = [this](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{};
            std::string fn = a[0].AsString();
            std::vector<Value> none;
            if (rt.functions.count(fn)) return rt.Call(fn, none);
            return Value{};
        };

        // ---- More everyday math / utility helpers ----------------------
        auto N = [](std::vector<Value>& a, std::size_t i, float d = 0.0f) {
            return i < a.size() ? a[i].AsFloat() : d;
        };
        // approach(cur, target, step) -> step toward target without overshooting.
        b["approach"] = [N](std::vector<Value>& a) {
            float c = N(a, 0), t = N(a, 1), s = std::fabs(N(a, 2));
            if (c < t) return Value{std::min(c + s, t)};
            if (c > t) return Value{std::max(c - s, t)};
            return Value{t};
        };
        // remap(v, inLo, inHi, outLo, outHi) -> rescale a value between ranges.
        b["remap"] = [N](std::vector<Value>& a) {
            float v = N(a, 0), il = N(a, 1), ih = N(a, 2), ol = N(a, 3), oh = N(a, 4, 1.0f);
            if (ih == il) return Value{ol};
            return Value{ol + (v - il) / (ih - il) * (oh - ol)};
        };
        b["frac"]   = [N](std::vector<Value>& a) { float v = N(a, 0); return Value{v - std::floor(v)}; };
        b["mod"]    = [N](std::vector<Value>& a) { float b2 = N(a, 1, 1.0f); float r = b2 != 0 ? std::fmod(N(a, 0), b2) : 0; if (r < 0) r += std::fabs(b2); return Value{r}; };
        b["snap"]   = [N](std::vector<Value>& a) { float s = N(a, 1, 1.0f); return Value{s != 0 ? std::round(N(a, 0) / s) * s : N(a, 0)}; };
        b["is_nan"] = [N](std::vector<Value>& a) { float v = N(a, 0); return Value{std::isnan(v) ? 1.0f : 0.0f}; };
        b["is_finite"] = [N](std::vector<Value>& a) { float v = N(a, 0); return Value{std::isfinite(v) ? 1.0f : 0.0f}; };
        b["avg"]    = [](std::vector<Value>& a) { if (a.empty()) return Value{0.0f}; float s = 0; for (auto& v : a) s += v.AsFloat(); return Value{s / a.size()}; };
        b["min3"]   = [N](std::vector<Value>& a) { return Value{std::min(N(a, 0), std::min(N(a, 1), N(a, 2)))}; };
        b["max3"]   = [N](std::vector<Value>& a) { return Value{std::max(N(a, 0), std::max(N(a, 1), N(a, 2)))}; };
        b["lerp_angle"] = [N](std::vector<Value>& a) {
            float fr = N(a, 0), to = N(a, 1), t = N(a, 2);
            float d = std::fmod(to - fr + 540.0f, 360.0f) - 180.0f;   // shortest path
            return Value{fr + d * t};
        };
        b["str_repeat"] = [](std::vector<Value>& a) {
            std::string s = a.empty() ? "" : a[0].AsString(); int n = a.size() > 1 ? (int)a[1].AsFloat() : 0;
            std::string out; for (int i = 0; i < n; ++i) out += s; return Value{out};
        };

        // ---- Tweening (DOTween-style) ----------------------------------
        // Smoothly animate the object's transform over time via the scene's
        // scheduler, with an optional easing name ("out_quad", "in_out_sine"...).
        auto easeFromArg = [](std::vector<Value>& a, std::size_t i) -> Ease {
            if (i >= a.size() || !a[i].IsString()) return Ease::Linear;
            std::string s = a[i].AsString();
            if (s == "in_quad") return Ease::QuadIn;
            if (s == "out_quad") return Ease::QuadOut;
            if (s == "in_out_quad") return Ease::QuadInOut;
            if (s == "in_cubic") return Ease::CubicIn;
            if (s == "out_cubic") return Ease::CubicOut;
            if (s == "in_out_cubic") return Ease::CubicInOut;
            if (s == "in_sine") return Ease::SineIn;
            if (s == "out_sine") return Ease::SineOut;
            if (s == "in_out_sine") return Ease::SineInOut;
            if (s == "in_expo") return Ease::ExpoIn;
            if (s == "out_expo") return Ease::ExpoOut;
            if (s == "in_out_expo") return Ease::ExpoInOut;
            if (s == "out_back") return Ease::BackOut;
            if (s == "in_back") return Ease::BackIn;
            if (s == "out_elastic") return Ease::ElasticOut;
            if (s == "out_bounce") return Ease::BounceOut;
            return Ease::Linear;
        };
        auto sched = [this]() -> Scheduler* {
            if (rt.host && rt.host->gameObject && rt.host->gameObject->scene())
                return &rt.host->gameObject->scene()->scheduler();
            return nullptr;
        };
        b["tween_move"] = [this, sched, easeFromArg](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.size() < 3) return Value{};
            Vec3 start = t->localPosition, target{a[0].AsFloat(), a[1].AsFloat(), start.z};
            Ease e = easeFromArg(a, 3);
            s->Tween(a[2].AsFloat(), [t, start, target, e](float u) { float k = Easing::Evaluate(e, u); t->localPosition = start + (target - start) * k; });
            return Value{};
        };
        b["tween_move3"] = [this, sched, easeFromArg](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.size() < 4) return Value{};
            Vec3 start = t->localPosition, target{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            Ease e = easeFromArg(a, 4);
            s->Tween(a[3].AsFloat(), [t, start, target, e](float u) { float k = Easing::Evaluate(e, u); t->localPosition = start + (target - start) * k; });
            return Value{};
        };
        b["tween_scale"] = [this, sched, easeFromArg](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.size() < 2) return Value{};
            Vec3 start = t->localScale; float v = a[0].AsFloat(); Vec3 target{v, v, v};
            Ease e = easeFromArg(a, 2);
            s->Tween(a[1].AsFloat(), [t, start, target, e](float u) { float k = Easing::Evaluate(e, u); t->localScale = start + (target - start) * k; });
            return Value{};
        };
        // Spin by `deg` degrees about Z over `dur` seconds.
        b["tween_rotate"] = [this, sched, easeFromArg](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.size() < 2) return Value{};
            Quat start = t->localRotation; float deg = a[0].AsFloat(); Ease e = easeFromArg(a, 2);
            s->Tween(a[1].AsFloat(), [t, start, deg, e](float u) { float k = Easing::Evaluate(e, u); t->localRotation = start * Quat::Euler({0, 0, deg * k}); });
            return Value{};
        };
        // Returns the host's tweenable albedo color, if any (sprite then mesh).
        auto colorPtr = [this]() -> Color* {
            if (!rt.host || !rt.host->gameObject) return nullptr;
            if (auto* sr = rt.host->gameObject->GetComponent<SpriteRenderer>()) return &sr->color;
            if (auto* mr = rt.host->gameObject->GetComponent<MeshRenderer>()) return &mr->color;
            return nullptr;
        };
        b["tween_color"] = [this, sched, easeFromArg, colorPtr](std::vector<Value>& a) {
            Scheduler* s = sched(); Color* c = colorPtr();
            if (!s || !c || a.size() < 4) return Value{};
            Color start = *c, target{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat(), start.a};
            Ease e = easeFromArg(a, 4);
            s->Tween(a[3].AsFloat(), [c, start, target, e](float u) {
                float k = Easing::Evaluate(e, u);
                c->r = start.r + (target.r - start.r) * k;
                c->g = start.g + (target.g - start.g) * k;
                c->b = start.b + (target.b - start.b) * k;
            });
            return Value{};
        };
        b["tween_fade"] = [this, sched, easeFromArg, colorPtr](std::vector<Value>& a) {
            Scheduler* s = sched(); Color* c = colorPtr();
            if (!s || !c || a.size() < 2) return Value{};
            float start = c->a, target = a[0].AsFloat(); Ease e = easeFromArg(a, 2);
            s->Tween(a[1].AsFloat(), [c, start, target, e](float u) { float k = Easing::Evaluate(e, u); c->a = start + (target - start) * k; });
            return Value{};
        };
        // ---- Save system: snapshot the whole scene to a slot file ------
        b["save_game"] = [this](std::vector<Value>& a) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            if (!s) return Value{0.0f};
            std::string slot = a.empty() ? "save1" : a[0].AsString();
            std::error_code ec; std::filesystem::create_directories("saves", ec);
            return Value{SceneSerializer::SaveToFile(*s, "saves/" + slot + ".okaysave") ? 1.0f : 0.0f};
        };
        b["load_game"] = [this](std::vector<Value>& a) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            std::string slot = a.empty() ? "save1" : a[0].AsString();
            std::string path = "saves/" + slot + ".okaysave";
            if (s && std::ifstream(path).good()) { s->RequestLoad(path); return Value{1.0f}; }
            return Value{0.0f};
        };
        b["save_exists"] = [](std::vector<Value>& a) {
            std::string slot = a.empty() ? "save1" : a[0].AsString();
            return Value{std::ifstream("saves/" + slot + ".okaysave").good() ? 1.0f : 0.0f};
        };
        b["delete_save"] = [](std::vector<Value>& a) {
            std::string slot = a.empty() ? "save1" : a[0].AsString();
            return Value{std::remove(("saves/" + slot + ".okaysave").c_str()) == 0 ? 1.0f : 0.0f};
        };

        // ---- Friendly aliases: intuitive names for common builtins so games
        //      read naturally. Each points at an already-registered function. ----
        auto alias = [&b](const char* from, const char* to) {
            auto it = b.find(to);
            if (it != b.end()) b[from] = it->second;
        };
        alias("delta_time", "dt");
        alias("get_key", "key");
        alias("get_key_down", "key_down");
        alias("get_key_up", "key_up");
        alias("is_key_down", "key");
        alias("random", "rand");
        alias("random_int", "randi");
        alias("random_range", "rand");
        alias("pick", "choose");
        alias("distance", "dist");
        alias("distance_to", "dist_to");
        alias("instantiate", "spawn");
        alias("instantiate3", "spawn3");
        alias("destroy_self", "destroy");
        alias("translate", "move");
        alias("rotate_z", "rotate");
        alias("set_position", "set_pos");
        alias("set_rotation", "set_rot3");
        alias("play_audio", "play_sound");
        alias("to_string", "to_str");
        alias("to_number", "to_num");
        alias("str", "to_str");
        alias("num", "to_num");
        alias("string_length", "str_len");
        alias("get_x", "pos_x");
        alias("get_y", "pos_y");
        alias("get_z", "pos_z");
        alias("velocity", "set_velocity");
        alias("screen_width", "screen_w");
        alias("screen_height", "screen_h");
        alias("debug", "debug_log");
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
    } catch (const BreakSignal&) {
        m_impl->loaded = true; return true; // stray break/continue: harmless
    } catch (const ContinueSignal&) {
        m_impl->loaded = true; return true;
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
    // Scheduled after()/every() callbacks tick even without an update().
    if (!m_impl->rt.timers.empty()) {
        try { m_impl->rt.TickTimers(deltaTime); }
        catch (const std::exception& e) { Log::Error("OkayScript timer: ", e.what()); }
    }
}

void OkayScriptVM::CallEvent(const std::string& function) {
    if (!m_impl->loaded) return;
    if (m_impl->rt.functions.count(function)) {
        std::vector<Value> none;
        try { m_impl->rt.Call(function, none); }
        catch (const std::exception& e) { Log::Error("OkayScript ", function.c_str(), "(): ", e.what()); }
    }
}

vs::VsValue OkayScriptVM::GetGlobal(const std::string& name) const {
    auto& g = m_impl->rt.Global().vars;
    auto it = g.find(name);
    return it != g.end() ? it->second : vs::VsValue{};
}

} // namespace okay
