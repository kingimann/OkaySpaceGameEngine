#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scripting/ScriptUI.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/SceneManager.hpp"
#include "okay/Physics/Physics2D.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/SpriteAnimator.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Components/NetworkSync.hpp"
#include "okay/Net/Matchmaking.hpp"
#include "okay/Platform/Steam/Steam.hpp"
#include "okay/Platform/Account/Account.hpp"
#include "okay/Components/UIImage.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/Tilemap.hpp"
#include "okay/Audio/AudioMixer.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Core/DataAsset.hpp"
#include "okay/Core/SaveData.hpp"
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

// ===================== JSON (for to_json / from_json builtins) =====================
// A compact, dependency-free JSON writer/reader over VsValue. Numbers, bools,
// strings, arrays and maps round-trip; vec3 is written as a [x,y,z] array.
inline void JsonEscape(const std::string& s, std::string& out) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            default:   out += c;      break;
        }
    }
    out += '"';
}

inline void ToJson(const Value& v, std::string& out) {
    if (auto arr = v.AsArray()) {
        out += '[';
        for (std::size_t i = 0; i < arr->size(); ++i) { if (i) out += ','; ToJson((*arr)[i], out); }
        out += ']';
    } else if (auto m = v.AsMap()) {
        out += '{';
        bool first = true;
        for (auto& kv : *m) { if (!first) out += ','; first = false; JsonEscape(kv.first, out); out += ':'; ToJson(kv.second, out); }
        out += '}';
    } else if (v.IsString()) {
        JsonEscape(v.AsString(), out);
    } else if (v.IsBool()) {
        out += v.AsBool() ? "true" : "false";
    } else if (v.IsVec3()) {
        Vec3 p = v.AsVec3();
        out += '['; out += vs::VsValue(p.x).AsString(); out += ',';
        out += vs::VsValue(p.y).AsString(); out += ','; out += vs::VsValue(p.z).AsString(); out += ']';
    } else {
        out += v.AsString();   // number -> its formatted form
    }
}

// A tiny recursive-descent JSON parser. On malformed input it returns whatever it
// managed to parse (best-effort) — scripts should validate important data anyway.
struct JsonReader {
    const std::string& s; std::size_t i = 0;
    explicit JsonReader(const std::string& src) : s(src) {}
    void Ws() { while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) ++i; }
    Value Parse() { Ws(); if (i >= s.size()) return Value{}; char c = s[i];
        if (c == '{') return Obj();
        if (c == '[') return Arr();
        if (c == '"') return Value{Str()};
        if (c == 't') { i += 4; return Value{true}; }
        if (c == 'f') { i += 5; return Value{false}; }
        if (c == 'n') { i += 4; return Value{}; }
        return Num();
    }
    std::string Str() {
        std::string out; ++i;  // opening quote
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char e = s[++i];
                out += (e=='n')?'\n':(e=='t')?'\t':(e=='r')?'\r':e;
            } else out += s[i];
            ++i;
        }
        if (i < s.size()) ++i;  // closing quote
        return out;
    }
    Value Num() {
        std::size_t start = i;
        while (i < s.size() && (std::isdigit((unsigned char)s[i])||s[i]=='-'||s[i]=='+'||s[i]=='.'||s[i]=='e'||s[i]=='E')) ++i;
        return Value{(float)std::atof(s.substr(start, i - start).c_str())};
    }
    Value Arr() {
        Value v = Value::MakeArray(); auto a = v.AsArray(); ++i;  // '['
        Ws(); if (i < s.size() && s[i] == ']') { ++i; return v; }
        while (i < s.size()) { a->push_back(Parse()); Ws();
            if (i < s.size() && s[i] == ',') { ++i; Ws(); continue; }
            if (i < s.size() && s[i] == ']') { ++i; break; }
            break;
        }
        return v;
    }
    Value Obj() {
        Value v = Value::MakeMap(); auto m = v.AsMap(); ++i;  // '{'
        Ws(); if (i < s.size() && s[i] == '}') { ++i; return v; }
        while (i < s.size()) { Ws();
            if (s[i] != '"') break;
            std::string key = Str(); Ws();
            if (i < s.size() && s[i] == ':') ++i;
            (*m)[key] = Parse(); Ws();
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == '}') { ++i; break; }
            break;
        }
        return v;
    }
};

// ===================== Lexer =====================
enum class Tok {
    End, Number, String, InterpString, Ident,
    Var, If, Else, While, For, In, Function, Return, True, False, Break, Continue,
    Question, Colon,
    Plus, Minus, Star, Slash, Percent,
    Assign, PlusEq, MinusEq, StarEq, SlashEq, Inc, Dec,
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
    int line = 0;                                  // source line (0 = unknown)
    ScriptError(const std::string& msg, int ln) : std::runtime_error(msg), line(ln) {}
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
            else if (c == '$' && Peek(1) == '"')
                out.push_back(InterpString());
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
        // Allow C#/Unity numeric suffixes (5f, 1.0f, 2d) — just drop them.
        if (Peek() == 'f' || Peek() == 'F' || Peek() == 'd' || Peek() == 'D') Advance();
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

    // C#-style interpolated string: $"Score: {score}". The raw inner text (with
    // {expr} markers preserved) is stored; the parser splits and compiles it.
    Token InterpString() {
        Advance(); // '$'
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
        return {Tok::InterpString, s, 0, m_line};
    }

    Token Ident() {
        std::string s;
        while (std::isalnum((unsigned char)Peek()) || Peek() == '_') s += Advance();
        // Unity-style dotted names are a single token: Input.GetKeyDown,
        // Time.deltaTime, transform.position.x, gameObject.SetActive.
        while (Peek() == '.' && (std::isalpha((unsigned char)Peek(1)) || Peek(1) == '_')) {
            s += Advance();                                   // the '.'
            while (std::isalnum((unsigned char)Peek()) || Peek() == '_') s += Advance();
        }
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
            case '+': { if (Peek() == '+') { Advance(); return {Tok::Inc, "", 0, m_line}; }
                        auto t = two('=', Tok::PlusEq);  return t.type != Tok::End ? t : Token{Tok::Plus, "", 0, m_line}; }
            case '-': { if (Peek() == '-') { Advance(); return {Tok::Dec, "", 0, m_line}; }
                        auto t = two('=', Tok::MinusEq); return t.type != Tok::End ? t : Token{Tok::Minus, "", 0, m_line}; }
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
        throw ScriptError(std::string("unexpected character '") + c + "'", m_line);
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

    // Details of the most recent raycast()/raycast3() call, exposed to scripts
    // through ray_object()/ray_x()/ray_dist()/... accessors.
    struct LastHit {
        bool hit = false;
        std::string object;
        float px = 0, py = 0, pz = 0;   // hit point
        float nx = 0, ny = 0, nz = 0;   // surface normal
        float dist = 0;
    };
    LastHit lastHit2D;
    LastHit lastHit3D;

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
    bool Has(const std::string& n) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            if (it->vars.count(n)) return true;
        return false;
    }

    Value Call(const std::string& name, std::vector<Value>& args);

    // Unity-style dotted property read/write (transform.position.x, Time.timeScale,
    // gameObject.name, ...). GetDotted sets `ok` when it recognized the name;
    // SetDotted returns true when it handled the assignment.
    Value GetDotted(const std::string& name, bool& ok);
    bool  SetDotted(const std::string& name, const Value& v);
};

// ---- Expressions ----
struct NumberExpr : Expr { double v; explicit NumberExpr(double x) : v(x) {} Value Eval(Runtime&) override { return (float)v; } };
struct StringExpr : Expr { std::string v; explicit StringExpr(std::string s) : v(std::move(s)) {} Value Eval(Runtime&) override { return v; } };
struct BoolExpr   : Expr { bool v; explicit BoolExpr(bool b) : v(b) {} Value Eval(Runtime&) override { return v; } };

// $"text {expr} more" — a list of literal/expression segments concatenated.
struct InterpStringExpr : Expr {
    struct Seg { bool isExpr; std::string lit; ExprPtr expr; };
    std::vector<Seg> segs;
    void AddLiteral(const std::string& s) { if (!s.empty()) segs.push_back({false, s, nullptr}); }
    void AddExpr(ExprPtr e) { segs.push_back({true, "", std::move(e)}); }
    Value Eval(Runtime& r) override {
        std::string out;
        for (auto& s : segs) out += s.isExpr ? s.expr->Eval(r).AsString() : s.lit;
        return Value{out};
    }
};
struct VarExpr    : Expr {
    std::string n; explicit VarExpr(std::string s) : n(std::move(s)) {}
    Value Eval(Runtime& r) override {
        // Unity-style property reads: transform.position.x, Time.deltaTime, ...
        if (n.find('.') != std::string::npos) {
            bool ok = false; Value v = r.GetDotted(n, ok);
            if (ok) return v;
        }
        return r.Get(n);
    }
};

struct AssignExpr : Expr {
    std::string n; ExprPtr value;
    AssignExpr(std::string s, ExprPtr e) : n(std::move(s)), value(std::move(e)) {}
    Value Eval(Runtime& r) override {
        Value v = value->Eval(r);
        if (n.find('.') != std::string::npos && r.SetDotted(n, v)) return v;
        r.Assign(n, v);
        return v;
    }
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
// throw <expr>; — raise a script error carrying the value's text.
struct ThrowStmt : Stmt {
    ExprPtr e; explicit ThrowStmt(ExprPtr x) : e(std::move(x)) {}
    void Exec(Runtime& r) override { throw ScriptError(e ? e->Eval(r).AsString() : std::string("error")); }
};
// try { ... } catch (e) { ... } — run the body; on a script/runtime error, bind the
// message to the catch variable and run the handler. Control-flow signals (return/
// break/continue) are plain structs, not std::exception, so they pass through cleanly.
struct TryStmt : Stmt {
    std::vector<StmtPtr> body, catchB;
    std::string catchVar;
    bool hasCatch = false;
    void Exec(Runtime& r) override {
        try {
            for (auto& s : body) s->Exec(r);
        } catch (const std::exception& ex) {
            if (!hasCatch) return;   // no handler: swallow the error
            if (!catchVar.empty()) r.Define(catchVar, Value{std::string(ex.what())});
            for (auto& s : catchB) s->Exec(r);
        }
    }
};
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
// C#-style switch: matches the subject against case labels, runs from the first
// match (with C-style fall-through) until a `break`. `default` runs if no match.
struct SwitchStmt : Stmt {
    struct Case { ExprPtr match; std::vector<StmtPtr> body; bool isDefault = false; };
    ExprPtr subject;
    std::vector<Case> cases;
    static bool Eq(const Value& a, const Value& b) {
        return (a.IsString() || b.IsString()) ? a.AsString() == b.AsString()
                                              : a.AsFloat() == b.AsFloat();
    }
    void Exec(Runtime& r) override {
        Value v = subject->Eval(r);
        int start = -1;
        for (std::size_t i = 0; i < cases.size(); ++i)
            if (!cases[i].isDefault && Eq(v, cases[i].match->Eval(r))) { start = (int)i; break; }
        if (start < 0)
            for (std::size_t i = 0; i < cases.size(); ++i)
                if (cases[i].isDefault) { start = (int)i; break; }
        if (start < 0) return;
        try {
            for (std::size_t i = start; i < cases.size(); ++i)   // fall-through until break
                for (auto& s : cases[i].body) s->Exec(r);
        } catch (BreakSignal&) {}
    }
};
// do { body } while (cond); — runs the body at least once.
struct DoWhileStmt : Stmt {
    ExprPtr cond; std::vector<StmtPtr> body;
    void Exec(Runtime& r) override {
        int guard = 0;
        do {
            try { for (auto& s : body) s->Exec(r); }
            catch (ContinueSignal&) {}
            catch (BreakSignal&) { break; }
            if (++guard > 1000000) throw ScriptError("do-while loop exceeded iteration limit");
        } while (cond->Eval(r).AsBool());
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
        r.Define(var, Value{}); // loop variable lives in the current scope
        if (auto arr = coll.AsArray()) {
            // Iterate a snapshot of the size so push during iteration is bounded.
            std::size_t n = arr->size();
            for (std::size_t i = 0; i < n && i < arr->size(); ++i) {
                r.Assign(var, (*arr)[i]);
                try { for (auto& s : body) s->Exec(r); }
                catch (ContinueSignal&) {}
                catch (BreakSignal&) { break; }
            }
        } else if (auto m = coll.AsMap()) {
            // foreach over a map yields its keys (like other scripting languages);
            // use map_get(m, key) in the body to read each value. Snapshot the keys
            // so mutation during iteration is safe.
            std::vector<std::string> keys; keys.reserve(m->size());
            for (auto& kv : *m) keys.push_back(kv.first);
            for (auto& k : keys) {
                r.Assign(var, Value{k});
                try { for (auto& s : body) s->Exec(r); }
                catch (ContinueSignal&) {}
                catch (BreakSignal&) { break; }
            }
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

// ---- Unity-style dotted property access --------------------------------------
namespace {
// Euler Z angle (degrees) of a quaternion, for transform.eulerAngles.z.
float EulerZ(const Quat& q) {
    return std::atan2(2.0f * (q.w * q.z + q.x * q.y),
                      1.0f - 2.0f * (q.y * q.y + q.z * q.z)) * Mathf::Rad2Deg;
}
} // namespace

Value Runtime::GetDotted(const std::string& name, bool& ok) {
    ok = true;
    Transform* t = host ? host->transform : nullptr;
    GameObject* g = host ? host->gameObject : nullptr;

    // Time
    if (name == "Time.deltaTime")  return Value{host ? host->deltaTime : 0.0f};
    if (name == "Time.time")       return Value{Time::ElapsedTime()};
    if (name == "Time.timeScale")  return Value{Time::TimeScale()};
    // Mathf constants
    if (name == "Mathf.PI")        return Value{Mathf::PI};
    if (name == "Mathf.Deg2Rad")   return Value{Mathf::Deg2Rad};
    if (name == "Mathf.Rad2Deg")   return Value{Mathf::Rad2Deg};
    if (name == "Mathf.Infinity")  return Value{1e30f};
    if (name == "Mathf.Epsilon")   return Value{1e-6f};
    if (name == "Random.value")    return Value{Random::Shared().Range(0.0f, 1.0f)};
    // Screen / Input
    if (name == "Screen.width")        return Value{UICanvas::Width()};
    if (name == "Screen.height")       return Value{UICanvas::Height()};
    if (name == "Input.mousePosition.x") return Value{Input::MousePosition().x};
    if (name == "Input.mousePosition.y") return Value{Input::MousePosition().y};
    if (name == "Input.mousePosition")   { Vec2 m = Input::MousePosition(); return Value{Vec3{m.x, m.y, 0.0f}}; }
    // Vector constants
    if (name == "Vector3.zero" || name == "Vector2.zero") return Value{Vec3::Zero};
    if (name == "Vector3.one"  || name == "Vector2.one")  return Value{Vec3{1, 1, 1}};
    if (name == "Quaternion.identity") return Value{Vec3::Zero};   // euler (0,0,0)
    // Color constants (RGB as a Vec3; pass to set_color/set_tint).
    if (name == "Color.red")     return Value{Vec3{1, 0, 0}};
    if (name == "Color.green")   return Value{Vec3{0, 1, 0}};
    if (name == "Color.blue")    return Value{Vec3{0, 0, 1}};
    if (name == "Color.white")   return Value{Vec3{1, 1, 1}};
    if (name == "Color.black")   return Value{Vec3{0, 0, 0}};
    if (name == "Color.yellow")  return Value{Vec3{1, 1, 0}};
    if (name == "Color.cyan")    return Value{Vec3{0, 1, 1}};
    if (name == "Color.magenta") return Value{Vec3{1, 0, 1}};
    if (name == "Color.gray" || name == "Color.grey") return Value{Vec3{0.5f, 0.5f, 0.5f}};
    if (name == "Vector3.up")    return Value{Vec3{0, 1, 0}};
    if (name == "Vector3.down")  return Value{Vec3{0, -1, 0}};
    if (name == "Vector3.right") return Value{Vec3{1, 0, 0}};
    if (name == "Vector3.left")  return Value{Vec3{-1, 0, 0}};

    if (t) {
        Vec3 wp = t->Position();
        if (name == "transform.position")   return Value{wp};
        if (name == "transform.position.x") return Value{wp.x};
        if (name == "transform.position.y") return Value{wp.y};
        if (name == "transform.position.z") return Value{wp.z};
        if (name == "transform.localPosition")   return Value{t->localPosition};
        if (name == "transform.localPosition.x") return Value{t->localPosition.x};
        if (name == "transform.localPosition.y") return Value{t->localPosition.y};
        if (name == "transform.localPosition.z") return Value{t->localPosition.z};
        if (name == "transform.localScale")   return Value{t->localScale};
        if (name == "transform.localScale.x") return Value{t->localScale.x};
        if (name == "transform.localScale.y") return Value{t->localScale.y};
        if (name == "transform.localScale.z") return Value{t->localScale.z};
        if (name == "transform.eulerAngles.z" || name == "transform.rotation")
            return Value{EulerZ(t->localRotation)};
        if (name == "transform.forward") return Value{t->Forward()};
        if (name == "transform.up")      return Value{t->Up()};
        if (name == "transform.right")   return Value{t->Right()};
    }
    if (g) {
        if (name == "gameObject.name") return Value{g->name};
        if (name == "gameObject.activeSelf" || name == "gameObject.active") return Value{g->active};
        if (name == "gameObject.tag")  return Value{g->tag};
    }
    // Generic properties on a variable: Vector3 (.x/.magnitude/.normalized),
    // strings (.Length), and arrays (.Length / .Count). e.g.  v.magnitude,
    // name.Length, items.Count.
    {
        auto dot = name.rfind('.');
        if (dot != std::string::npos) {
            std::string base = name.substr(0, dot), prop = name.substr(dot + 1);
            if (base.find('.') == std::string::npos && Has(base)) {
                Value bv = Get(base);
                if (bv.IsString()) {
                    if (prop == "Length" || prop == "length") return Value{(float)bv.AsString().size()};
                } else if (bv.IsArray()) {
                    if (prop == "Length" || prop == "length" || prop == "Count" || prop == "count")
                        return Value{bv.AsArray() ? (float)bv.AsArray()->size() : 0.0f};
                } else {
                    Vec3 v = bv.AsVec3();
                    if (prop == "x") return Value{v.x};
                    if (prop == "y") return Value{v.y};
                    if (prop == "z") return Value{v.z};
                    if (prop == "magnitude")    return Value{v.Magnitude()};
                    if (prop == "sqrMagnitude") return Value{v.x * v.x + v.y * v.y + v.z * v.z};
                    if (prop == "normalized") { float m = v.Magnitude(); return Value{m > 1e-6f ? v * (1.0f / m) : Vec3::Zero}; }
                }
            }
        }
    }
    ok = false;
    return Value{};
}

bool Runtime::SetDotted(const std::string& name, const Value& v) {
    Transform* t = host ? host->transform : nullptr;
    GameObject* g = host ? host->gameObject : nullptr;

    if (name == "Time.timeScale") { Time::SetTimeScale(v.AsFloat()); return true; }
    if (t) {
        if (name == "transform.position")        { t->SetPosition(v.AsVec3()); return true; }
        if (name == "transform.localPosition")   { t->localPosition = v.AsVec3(); return true; }
        if (name == "transform.localScale")      { t->localScale = v.AsVec3(); return true; }
        if (name == "transform.position.x")      { Vec3 p = t->Position(); p.x = v.AsFloat(); t->SetPosition(p); return true; }
        if (name == "transform.position.y")      { Vec3 p = t->Position(); p.y = v.AsFloat(); t->SetPosition(p); return true; }
        if (name == "transform.position.z")      { Vec3 p = t->Position(); p.z = v.AsFloat(); t->SetPosition(p); return true; }
        if (name == "transform.localPosition.x") { t->localPosition.x = v.AsFloat(); return true; }
        if (name == "transform.localPosition.y") { t->localPosition.y = v.AsFloat(); return true; }
        if (name == "transform.localPosition.z") { t->localPosition.z = v.AsFloat(); return true; }
        if (name == "transform.localScale.x")    { t->localScale.x = v.AsFloat(); return true; }
        if (name == "transform.localScale.y")    { t->localScale.y = v.AsFloat(); return true; }
        if (name == "transform.localScale.z")    { t->localScale.z = v.AsFloat(); return true; }
        if (name == "transform.eulerAngles.z")
            { t->localRotation = Quat::Euler({0, 0, v.AsFloat()}); return true; }
        // transform.rotation accepts a Quaternion.Euler(...) (a Vec3 euler) or a
        // bare Z angle; use the Z component either way.
        if (name == "transform.rotation")
            { t->localRotation = Quat::Euler({0, 0, v.AsVec3().z}); return true; }
    }
    if (g) {
        if (name == "gameObject.name")       { g->name = v.AsString(); return true; }
        if (name == "gameObject.activeSelf" || name == "gameObject.active") { g->active = v.AsBool(); return true; }
        if (name == "gameObject.tag")        { g->tag = v.AsString(); return true; }
    }
    // Generic: <var>.x/.y/.z = value, for a variable holding a Vector3.
    {
        auto dot = name.rfind('.');
        if (dot != std::string::npos && dot + 2 == name.size()) {
            std::string base = name.substr(0, dot); char comp = name[dot + 1];
            if (base.find('.') == std::string::npos && (comp == 'x' || comp == 'y' || comp == 'z') && Has(base)) {
                Vec3 vec = Get(base).AsVec3();
                if (comp == 'x') vec.x = v.AsFloat(); else if (comp == 'y') vec.y = v.AsFloat(); else vec.z = v.AsFloat();
                Assign(base, Value{vec});
                return true;
            }
        }
    }
    return false;
}

// ===================== Parser =====================
class Parser {
public:
    explicit Parser(std::vector<Token> toks) : m_toks(std::move(toks)) {}

    std::vector<StmtPtr> ParseProgram(std::unordered_map<std::string, FunctionDecl>& funcs) {
        std::vector<StmtPtr> top;
        while (!Check(Tok::End)) ParseMember(top, funcs);
        return top;
    }

    // Parse for diagnostics only: keep going after a parse error by recording it
    // and re-synchronizing to the next statement boundary, so the editor can list
    // EVERY syntax error at once instead of just the first. The parsed output is
    // discarded — this exists purely to collect ScriptDiagnostic entries.
    std::vector<ScriptDiagnostic> ParseProgramRecovering() {
        std::vector<ScriptDiagnostic> diags;
        std::vector<StmtPtr> top;
        std::unordered_map<std::string, FunctionDecl> funcs;
        while (!Check(Tok::End)) {
            std::size_t before = m_pos;
            try {
                ParseMember(top, funcs);
            } catch (const ScriptError& e) {
                diags.push_back({ e.line, e.what() });
                if (diags.size() >= 100) break;          // stop runaway cascades
                Synchronize(before);
            }
            if (m_pos == before) ++m_pos;                // guarantee forward progress
        }
        return diags;
    }

    // Parse one top-level (or in-class) member: a function, a C#-style method,
    // a `[public] class Name : OkaySource { ... }` wrapper (its methods/fields
    // are hoisted out, so real Unity scripts paste in), or a statement.
    void ParseMember(std::vector<StmtPtr>& top, std::unordered_map<std::string, FunctionDecl>& funcs) {
        SkipAttributes();   // C# attributes: [SerializeField], [Header("..")], ...
        if (IsClassAhead()) {
            while (Check(Tok::Ident) && Peek().text != "class") ++m_pos;  // skip modifiers
            Expect(Tok::Ident, "'class'");                                // the 'class' keyword
            Expect(Tok::Ident, "class name");                            // its name
            if (Match(Tok::Colon)) { do { Expect(Tok::Ident, "base type"); } while (Match(Tok::Comma)); }
            Expect(Tok::LBrace, "'{'");
            while (!Check(Tok::RBrace) && !Check(Tok::End)) ParseMember(top, funcs);
            Expect(Tok::RBrace, "'}'");
            return;
        }
        if (Check(Tok::Function)) {
            std::string name; FunctionDecl decl = ParseFunction(name);
            funcs[name] = std::move(decl);
        } else if (IsTypedFunctionAhead()) {            // C#-style: void Start() { }
            std::string name; FunctionDecl decl = ParseTypedFunction(name);
            funcs[name] = std::move(decl);
        } else {
            top.push_back(ParseStatement());
        }
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

    // A run of identifiers containing "class" (e.g. `public class Foo`).
    bool IsClassAhead() const {
        std::size_t i = m_pos;
        while (m_toks[i].type == Tok::Ident) { if (m_toks[i].text == "class") return true; ++i; }
        return false;
    }

private:
    const Token& Peek() const { return m_toks[m_pos]; }
    const Token& Prev() const { return m_toks[m_pos - 1]; }
    bool Check(Tok t) const { return Peek().type == t; }
    bool Match(Tok t) { if (Check(t)) { ++m_pos; return true; } return false; }
    const Token& Expect(Tok t, const char* msg) {
        if (!Check(t)) throw ScriptError(std::string("parse error: expected ") + msg, Peek().line);
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
        std::size_t i = m_pos;
        int idents = 0;
        while (m_toks[i].type == Tok::Ident) { ++idents; ++i; }
        return idents >= 2 && m_toks[i].type == Tok::LParen;
    }

    FunctionDecl ParseTypedFunction(std::string& nameOut) {
        // Consume modifier/type identifiers, keeping the last one (before '(')
        // as the function name.
        std::string last = Expect(Tok::Ident, "type").text;
        while (Check(Tok::Ident)) last = m_toks[m_pos++].text;
        nameOut = last;
        return ParseParamsAndBody();
    }

    // Shared by `function f(...)` and `void F(...)`: parse the parameter list
    // (params may be bare `a` or typed `int a`) and the body block.
    FunctionDecl ParseParamsAndBody() {
        FunctionDecl decl;
        Expect(Tok::LParen, "'('");
        if (!Check(Tok::RParen)) {
            do {
                std::string p = Expect(Tok::Ident, "parameter").text;
                if (Check(Tok::Ident)) p = m_toks[m_pos++].text; // was a type; real name follows
                decl.params.push_back(p);
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

    // A C#-style typed local/field: a run of >= 2 identifiers (type + modifiers
    // + name) ending in '=' or ';' (e.g. `int score = 0;`, `Vector3 v;`).
    bool IsTypedVarDeclAhead() const {
        std::size_t i = m_pos;
        int idents = 0;
        while (m_toks[i].type == Tok::Ident) { ++idents; ++i; }
        return idents >= 2 && (m_toks[i].type == Tok::Assign || m_toks[i].type == Tok::Semicolon);
    }

    StmtPtr ParseStatement() {
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
            std::string var = Expect(Tok::Ident, "loop variable").text;
            if (Check(Tok::Ident)) var = m_toks[m_pos++].text;   // typed: `Type name`
            Expect(Tok::In, "'in'");
            auto fe = std::make_unique<ForEachStmt>();
            fe->var = var;
            fe->iterable = ParseExpression();
            Expect(Tok::RParen, "')'");
            fe->body = ParseBlock();
            return fe;
        }
        // do { body } while (cond);
        if (Check(Tok::Ident) && Peek().text == "do") {
            ++m_pos;
            auto st = std::make_unique<DoWhileStmt>();
            st->body = ParseBlock();
            if (!(Check(Tok::While) || (Check(Tok::Ident) && Peek().text == "while")))
                throw ScriptError("parse error: expected 'while' after do-block", Peek().line);
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
                    throw ScriptError("parse error: expected 'case' or 'default' in switch", Peek().line);
                }
                while (!Check(Tok::RBrace) && !Check(Tok::End) &&
                       !(Check(Tok::Ident) && (Peek().text == "case" || Peek().text == "default")))
                    c.body.push_back(ParseStatement());
                sw->cases.push_back(std::move(c));
            }
            Expect(Tok::RBrace, "'}'");
            return sw;
        }
        if (Match(Tok::Var) || IsTypedVarDeclAhead()) {
            // For a typed decl, drop the leading type/modifier idents, keep the last.
            std::string name = Expect(Tok::Ident, "variable name").text;
            while (Check(Tok::Ident)) name = m_toks[m_pos++].text;
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
            // init: a var declaration (var or typed), an expression, or empty.
            if (!Match(Tok::Semicolon)) {
                if (Match(Tok::Var) || IsTypedVarDeclAhead()) {
                    std::string name = Expect(Tok::Ident, "variable name").text;
                    while (Check(Tok::Ident)) name = m_toks[m_pos++].text;
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
        // Postfix increment/decrement: x++  ->  x = x + 1  (also x--).
        if (Check(Tok::Inc) || Check(Tok::Dec)) {
            auto* var = dynamic_cast<VarExpr*>(left.get());
            if (!var) throw ScriptError("invalid increment target");
            std::string name = var->n;
            Tok bin = Peek().type == Tok::Inc ? Tok::Plus : Tok::Minus; ++m_pos;
            auto combined = std::make_unique<BinaryExpr>(
                bin, std::make_unique<VarExpr>(name), std::make_unique<NumberExpr>(1.0));
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
        // Prefix ++x / --x  ->  x = x + 1 / x = x - 1.
        if (Check(Tok::Inc) || Check(Tok::Dec)) {
            Tok bin = Peek().type == Tok::Inc ? Tok::Plus : Tok::Minus; ++m_pos;
            ExprPtr target = ParseUnary();
            auto* var = dynamic_cast<VarExpr*>(target.get());
            if (!var) throw ScriptError("invalid increment target");
            std::string name = var->n;
            auto combined = std::make_unique<BinaryExpr>(
                bin, std::make_unique<VarExpr>(name), std::make_unique<NumberExpr>(1.0));
            return std::make_unique<AssignExpr>(name, std::move(combined));
        }
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
                Parser p(lx.Scan());
                node->AddExpr(p.ParseExpression());
            } else {
                lit += c;
            }
        }
        node->AddLiteral(lit);
        return node;
    }

    ExprPtr ParsePrimary() {
        if (Match(Tok::Number)) return std::make_unique<NumberExpr>(Prev().number);
        if (Match(Tok::String)) return std::make_unique<StringExpr>(Prev().text);
        if (Match(Tok::InterpString)) return BuildInterp(Prev().text);
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
            // C#-style `new Vector3(...)` — drop `new`, the T(...) is just a call.
            if (name == "new") return ParsePrimary();
            // Generic call `GetComponent<Type>(args)` — pass the type name as the
            // first argument so builtins can dispatch on it. (Bounds-checked: the
            // token stream has a single End sentinel, so peeking +3 can run off.)
            if (Check(Tok::Lt) && m_pos + 3 < m_toks.size() &&
                m_toks[m_pos + 1].type == Tok::Ident &&
                m_toks[m_pos + 2].type == Tok::Gt && m_toks[m_pos + 3].type == Tok::LParen) {
                std::string typeName = m_toks[m_pos + 1].text;
                m_pos += 4;   // consume  < Type > (
                auto call = std::make_unique<CallExpr>(name);
                call->args.push_back(std::make_unique<StringExpr>(typeName));
                if (!Check(Tok::RParen)) {
                    do { call->args.push_back(ParseExpression()); } while (Match(Tok::Comma));
                }
                Expect(Tok::RParen, "')'");
                return call;
            }
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
        throw ScriptError("parse error: unexpected token", Peek().line);
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

        // ---- Immediate-mode UI (forwarded to the host's ScriptUIBridge) ----
        // A game draws its UI each frame; the host brackets the frame and renders it.
        //   ui_begin("Title", x, y, w, h) ... widgets ... ui_end()
        //   hp = ui_slider("HP", hp, 0, 100);  if (ui_button("Reset")) hp = 100
        b["ui_begin"] = [](std::vector<Value>& a) {
            if (auto* u = GetScriptUI())
                u->Begin(a.size() > 0 ? a[0].AsString().c_str() : "",
                         a.size() > 1 ? a[1].AsFloat() : 20.0f, a.size() > 2 ? a[2].AsFloat() : 20.0f,
                         a.size() > 3 ? a[3].AsFloat() : 260.0f, a.size() > 4 ? a[4].AsFloat() : 320.0f);
            return Value{};
        };
        b["ui_end"]       = [](std::vector<Value>&)    { if (auto* u = GetScriptUI()) u->End(); return Value{}; };
        b["ui_text"]      = [](std::vector<Value>& a)  { if (auto* u = GetScriptUI()) u->Text(a.empty() ? "" : a[0].AsString().c_str()); return Value{}; };
        b["ui_sameline"]  = [](std::vector<Value>&)    { if (auto* u = GetScriptUI()) u->SameLine(); return Value{}; };
        b["ui_separator"] = [](std::vector<Value>&)    { if (auto* u = GetScriptUI()) u->Separator(); return Value{}; };
        b["ui_progress"]  = [](std::vector<Value>& a)  { if (auto* u = GetScriptUI()) u->ProgressBar(a.empty() ? 0.0f : a[0].AsFloat()); return Value{}; };
        b["ui_button"]    = [](std::vector<Value>& a)  { auto* u = GetScriptUI(); return Value{u && u->Button(a.empty() ? "" : a[0].AsString().c_str()) ? 1.0f : 0.0f}; };
        b["ui_checkbox"]  = [](std::vector<Value>& a)  {
            auto* u = GetScriptUI(); bool cur = a.size() > 1 && a[1].AsFloat() != 0.0f;
            bool nv = u ? u->Checkbox(a.empty() ? "" : a[0].AsString().c_str(), cur) : cur;
            return Value{nv ? 1.0f : 0.0f};
        };
        b["ui_slider"]    = [](std::vector<Value>& a)  {
            auto* u = GetScriptUI();
            float v = a.size() > 1 ? a[1].AsFloat() : 0.0f;
            float lo = a.size() > 2 ? a[2].AsFloat() : 0.0f, hi = a.size() > 3 ? a[3].AsFloat() : 1.0f;
            return Value{u ? u->Slider(a.empty() ? "" : a[0].AsString().c_str(), v, lo, hi) : v};
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
            // Accept either set_color(r, g, b[, a]) or a single Color/Vector3
            // value: set_color(Color.red) / set_color(myColor [, alpha]).
            Color c;
            if (!a.empty() && a[0].IsVec3()) {
                Vec3 v = a[0].AsVec3();
                c = Color{v.x, v.y, v.z, a.size() > 1 ? a[1].AsFloat() : 1.0f};
            } else {
                c = Color{a.size() > 0 ? a[0].AsFloat() : 1.0f, a.size() > 1 ? a[1].AsFloat() : 1.0f,
                          a.size() > 2 ? a[2].AsFloat() : 1.0f, a.size() > 3 ? a[3].AsFloat() : 1.0f};
            }
            if (GameObject* g = go()) {
                if (auto* sr = g->GetComponent<SpriteRenderer>()) sr->color = c;
                if (auto* tr = g->GetComponent<TextRenderer>()) tr->color = c;
            }
            return Value{};
        };
        // ---- UI by name: read/drive any widget from script -----------------
        // All take the GameObject name as the first argument so one script can
        // wire up a whole menu. Missing objects/components are no-ops / defaults.
        b["ui_set_text"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString())) {
                if (auto* tr = g->GetComponent<TextRenderer>()) tr->text = a[1].AsString();
                else if (auto* bt = g->GetComponent<UIButton>()) bt->label = a[1].AsString();
                else if (auto* in = g->GetComponent<UIInputField>()) in->text = a[1].AsString();
            }
            return Value{};
        };
        b["ui_get_text"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString())) {
                if (auto* tr = g->GetComponent<TextRenderer>()) return Value{tr->text};
                if (auto* in = g->GetComponent<UIInputField>()) return Value{in->text};
                if (auto* bt = g->GetComponent<UIButton>()) return Value{bt->label};
            }
            return Value{std::string{}};
        };
        b["ui_clicked"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                if (auto* bt = g->GetComponent<UIButton>()) return Value{bt->WasClicked()};
            return Value{false};
        };
        b["ui_set_interactable"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                if (auto* bt = g->GetComponent<UIButton>()) bt->interactable = a[1].AsBool();
            return Value{};
        };
        b["ui_slider_value"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                if (auto* sl = g->GetComponent<UISlider>()) return Value{sl->value};
            return Value{0.0f};
        };
        b["ui_set_slider"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                if (auto* sl = g->GetComponent<UISlider>()) sl->value = a[1].AsFloat();
            return Value{};
        };
        b["ui_toggle_value"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                if (auto* tg = g->GetComponent<UIToggle>()) return Value{tg->on};
            return Value{false};
        };
        b["ui_set_toggle"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                if (auto* tg = g->GetComponent<UIToggle>()) tg->on = a[1].AsBool();
            return Value{};
        };
        b["ui_dropdown_value"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                if (auto* dd = g->GetComponent<UIDropdown>()) return Value{(float)dd->value};
            return Value{0.0f};
        };
        b["ui_dropdown_text"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                if (auto* dd = g->GetComponent<UIDropdown>()) return Value{dd->Selected()};
            return Value{std::string{}};
        };
        b["ui_set_dropdown"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                if (auto* dd = g->GetComponent<UIDropdown>()) dd->SetValue((int)a[1].AsFloat());
            return Value{};
        };
        b["ui_set_progress"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                if (auto* pb = g->GetComponent<UIProgressBar>()) pb->value = a[1].AsFloat();
            return Value{};
        };
        b["ui_set_fill"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                if (auto* im = g->GetComponent<UIImage>()) im->fillAmount = a[1].AsFloat();
            return Value{};
        };
        // Pointer / hover over UI. uiHit returns the topmost widget under the mouse.
        auto uiHit = [sceneOf]() -> GameObject* {
            Scene* s = sceneOf(); if (!s) return nullptr;
            Vec2 m = Input::MousePosition();
            GameObject* hit = nullptr;
            for (const auto& up : s->Objects())
                if (up->active && UIScreenContains(up.get(), m, UICanvas::Width(), UICanvas::Height()))
                    hit = up.get();
            return hit;
        };
        b["ui_pointer_over"] = [uiHit](std::vector<Value>&) -> Value { return Value{uiHit() != nullptr}; };
        b["ui_hovered"] = [uiHit](std::vector<Value>&) -> Value {
            GameObject* h = uiHit(); return Value{h ? h->name : std::string{}};
        };
        b["ui_is_hovered"] = [uiHit](std::vector<Value>& a) -> Value {
            GameObject* h = uiHit(); return Value{h && !a.empty() && h->name == a[0].AsString()};
        };
        // The last drag-and-drop result (set by UIDraggable via Prefs).
        b["ui_drop_source"] = [](std::vector<Value>&) -> Value { return Value{Prefs::GetString("ui_drop_source", "")}; };
        b["ui_drop_target"] = [](std::vector<Value>&) -> Value { return Value{Prefs::GetString("ui_drop_target", "")}; };
        b["drop_source"] = [](std::vector<Value>&) -> Value { return Value{Prefs::GetString("drop_source", "")}; };
        b["drop_target"] = [](std::vector<Value>&) -> Value { return Value{Prefs::GetString("drop_target", "")}; };
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
        // raycast(ox, oy, dx, dy [, maxDist]) casts a ray and returns the name of
        // the object hit (empty string = nothing). Hit details are then available
        // via ray_hit()/ray_object()/ray_x()/ray_y()/ray_dist()/ray_nx()/ray_ny().
        b["raycast"] = [this](std::vector<Value>& a) -> Value {
            rt.lastHit2D = Runtime::LastHit{};
            if (a.size() < 4 || !rt.host || !rt.host->gameObject) return Value{std::string{}};
            Scene* sc = rt.host->gameObject->scene();
            if (!sc) return Value{std::string{}};
            Vec2 origin{a[0].AsFloat(), a[1].AsFloat()};
            Vec2 dir{a[2].AsFloat(), a[3].AsFloat()};
            float maxDist = a.size() > 4 ? a[4].AsFloat() : 1e9f;
            RaycastHit2D h = sc->physics().Raycast(*sc, origin, dir, maxDist);
            if (h.hit) {
                rt.lastHit2D.hit = true;
                rt.lastHit2D.object = h.gameObject ? h.gameObject->name : std::string{};
                rt.lastHit2D.px = h.point.x;   rt.lastHit2D.py = h.point.y;
                rt.lastHit2D.nx = h.normal.x;  rt.lastHit2D.ny = h.normal.y;
                rt.lastHit2D.dist = h.distance;
            }
            return Value{rt.lastHit2D.object};
        };
        b["ray_hit"]    = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit2D.hit}; };
        b["ray_object"] = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit2D.object}; };
        b["ray_x"]      = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit2D.px}; };
        b["ray_y"]      = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit2D.py}; };
        b["ray_nx"]     = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit2D.nx}; };
        b["ray_ny"]     = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit2D.ny}; };
        b["ray_dist"]   = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit2D.dist}; };
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
        // ---- Character animation (a sibling Character component) ----------
        // play_clip/stop_clip/etc. drive a Character on this script's object, so a
        // custom keyframe clip (see docs/animation.md) can be played from script.
        auto charSelf = [this]() -> Character* {
            return (rt.host && rt.host->gameObject) ? rt.host->gameObject->GetComponent<Character>() : nullptr;
        };
        b["play_clip"] = [charSelf](std::vector<Value>& a) {
            Character* c = charSelf();
            return Value{(c && !a.empty() && c->PlayClip(a[0].AsString())) ? 1.0f : 0.0f};
        };
        b["stop_clip"] = [charSelf](std::vector<Value>&) { if (Character* c = charSelf()) c->StopClip(); return Value{}; };
        b["playing_clip"] = [charSelf](std::vector<Value>&) {
            Character* c = charSelf(); return Value{c ? c->PlayingClip() : std::string{}};
        };
        b["is_playing_clip"] = [charSelf](std::vector<Value>&) {
            Character* c = charSelf(); return Value{(c && c->IsPlayingClip()) ? 1.0f : 0.0f};
        };
        b["load_clips"] = [charSelf](std::vector<Value>& a) {
            Character* c = charSelf();
            return Value{(c && !a.empty()) ? (float)c->LoadClips(a[0].AsString()) : 0.0f};
        };
        // Partial-body layer: play_layer("wave", "arms"|"upper"|<bone>); stop_layer().
        b["play_layer"] = [charSelf](std::vector<Value>& a) {
            Character* c = charSelf();
            if (!c || a.empty()) return Value{0.0f};
            std::string m = a.size() > 1 ? a[1].AsString() : "arms";
            std::uint32_t mask = (m == "upper" || m == "upper_body") ? Character::UpperBodyMask()
                               : (m == "arms" || m.empty())          ? Character::ArmsMask()
                               : Character::BoneBit(Character::BoneIndex(m));
            if (mask == 0) mask = Character::ArmsMask();
            return Value{c->PlayLayer(a[0].AsString(), mask) ? 1.0f : 0.0f};
        };
        b["stop_layer"] = [charSelf](std::vector<Value>&) { if (Character* c = charSelf()) c->StopLayer(); return Value{}; };
        // 1D blend tree: blend_tree("idle",0, "walk",2, "run",5); blend_param(speed).
        b["blend_tree"] = [charSelf](std::vector<Value>& a) {
            Character* c = charSelf();
            if (!c) return Value{0.0f};
            std::vector<Character::BlendStop> stops;
            for (std::size_t i = 0; i + 1 < a.size(); i += 2)
                stops.push_back({a[i + 1].AsFloat(), a[i].AsString()});
            c->SetBlendTree(stops);
            return Value{(float)stops.size()};
        };
        b["blend_param"] = [charSelf](std::vector<Value>& a) {
            if (Character* c = charSelf(); c && !a.empty()) c->SetBlendParam(a[0].AsFloat());
            return Value{};
        };
        b["clear_blend_tree"] = [charSelf](std::vector<Value>&) { if (Character* c = charSelf()) c->ClearBlendTree(); return Value{}; };
        b["clip_speed"] = [charSelf](std::vector<Value>& a) {
            if (Character* c = charSelf(); c && !a.empty()) c->animSpeed = a[0].AsFloat();
            return Value{};
        };
        // Set/get the built-in animation index (0 none,1 idle,2 walk,3 run,...).
        b["set_anim"] = [charSelf](std::vector<Value>& a) {
            if (Character* c = charSelf(); c && !a.empty()) c->anim = (int)a[0].AsFloat();
            return Value{};
        };
        b["get_anim"] = [charSelf](std::vector<Value>&) {
            Character* c = charSelf(); return Value{c ? (float)c->anim : 0.0f};
        };
        // Clip queries: timing + existence, so scripts can react to where a clip is.
        b["clip_time"] = [charSelf](std::vector<Value>&) {
            Character* c = charSelf(); return Value{c ? c->ClipTime() : 0.0f};
        };
        b["clip_normalized"] = [charSelf](std::vector<Value>&) {
            Character* c = charSelf(); return Value{c ? c->ClipNormalizedTime() : 0.0f};
        };
        b["clip_finished"] = [charSelf](std::vector<Value>&) {
            Character* c = charSelf(); return Value{(c && c->ClipFinished()) ? 1.0f : 0.0f};
        };
        b["clip_duration"] = [charSelf](std::vector<Value>& a) {
            Character* c = charSelf();
            return Value{(c && !a.empty()) ? c->ClipDuration(a[0].AsString()) : 0.0f};
        };
        b["has_clip"] = [charSelf](std::vector<Value>& a) {
            Character* c = charSelf();
            return Value{(c && !a.empty() && c->HasClip(a[0].AsString())) ? 1.0f : 0.0f};
        };
        // Pop the next fired animation event name ("" if none) — footsteps, hit windows.
        b["anim_event"] = [charSelf](std::vector<Value>&) {
            Character* c = charSelf(); return Value{c ? c->NextAnimEvent() : std::string{}};
        };

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
        // Relay (NAT traversal): host/join through a shared relay + session code,
        // so peers behind routers connect without port-forwarding.
        b["net_host_relay"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            if (!n || a.size() < 3) return Value{0.0f};
            return Value{n->HostViaRelay(a[0].AsString(), (std::uint16_t)(int)a[1].AsFloat(),
                                         a[2].AsString()) ? 1.0f : 0.0f};
        };
        b["net_join_relay"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            if (!n || a.size() < 3) return Value{0.0f};
            return Value{n->JoinViaRelay(a[0].AsString(), (std::uint16_t)(int)a[1].AsFloat(),
                                         a[2].AsString()) ? 1.0f : 0.0f};
        };
        // 1 once the relay has paired this peer (host wired up / slot assigned).
        b["net_relay_ready"] = [this](std::vector<Value>&) {
            NetworkManager* n = Net(); return Value{(n && n->RelayReady()) ? 1.0f : 0.0f};
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
        // Set the identity token presented on join (e.g. a Supabase access token);
        // returns the current token. The host verifies it if it has a verifier.
        b["net_auth_token"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net()) { if (!a.empty()) n->SetAuthToken(a[0].AsString()); return Value{n->AuthToken()}; }
            return Value{std::string{}};
        };
        // This peer's verified account id once joined ("" if anonymous/unverified).
        b["net_user_id"] = [this](std::vector<Value>&) {
            NetworkManager* n = Net(); return Value{n ? n->LocalUserId() : std::string{}};
        };
        // Turn packet encryption on/off (set on both ends before host/join); returns
        // the current setting (1/0). Needs a build with libsodium, else a no-op.
        b["net_encrypt"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net()) { if (!a.empty()) n->encryption = (a[0].AsFloat() != 0.0f); return Value{n->encryption ? 1.0f : 0.0f}; }
            return Value{0.0f};
        };
        // 1 once an encrypted session is established, else 0.
        b["net_encrypted"] = [this](std::vector<Value>&) {
            NetworkManager* n = Net(); return Value{(n && n->Encrypted()) ? 1.0f : 0.0f};
        };
        // ---- Supabase matchmaking / server browser --------------------
        // A shared cache of the last browse, so scripts can list then join by index.
        auto mmList = std::make_shared<std::vector<GameSession>>();
        // mm_host(name, addr, port [, max, room, region]) -> session id ("" on fail).
        b["mm_host"] = [](std::vector<Value>& a) {
            if (a.size() < 3) return Value{std::string{}};
            int max = a.size() > 3 ? (int)a[3].AsFloat() : 8;
            std::string room   = a.size() > 4 ? a[4].AsString() : std::string{};
            std::string region = a.size() > 5 ? a[5].AsString() : std::string{};
            return Value{Matchmaking::Host(a[0].AsString(), a[1].AsString(),
                                           (int)a[2].AsFloat(), max, room, region)};
        };
        b["mm_heartbeat"] = [](std::vector<Value>& a) {
            if (a.size() < 2) return Value{0.0f};
            return Value{Matchmaking::Heartbeat(a[0].AsString(), (int)a[1].AsFloat()) ? 1.0f : 0.0f};
        };
        b["mm_unregister"] = [](std::vector<Value>& a) {
            return Value{(!a.empty() && Matchmaking::Unregister(a[0].AsString())) ? 1.0f : 0.0f};
        };
        // mm_refresh([room]) -> number of open sessions found (cached for the getters).
        b["mm_refresh"] = [mmList](std::vector<Value>& a) {
            *mmList = Matchmaking::List(a.empty() ? std::string{} : a[0].AsString());
            return Value{(float)mmList->size()};
        };
        b["mm_count"] = [mmList](std::vector<Value>&) { return Value{(float)mmList->size()}; };
        // Field getters for a cached session by index.
        auto at = [mmList](std::vector<Value>& a) -> const GameSession* {
            if (a.empty()) return nullptr;
            std::size_t i = (std::size_t)a[0].AsFloat();
            return i < mmList->size() ? &(*mmList)[i] : nullptr;
        };
        b["mm_name"]    = [at](std::vector<Value>& a) { auto* g = at(a); return Value{g ? g->name : std::string{}}; };
        b["mm_addr"]    = [at](std::vector<Value>& a) { auto* g = at(a); return Value{g ? g->hostAddr : std::string{}}; };
        b["mm_port"]    = [at](std::vector<Value>& a) { auto* g = at(a); return Value{g ? (float)g->port : 0.0f}; };
        b["mm_players"] = [at](std::vector<Value>& a) { auto* g = at(a); return Value{g ? (float)g->players : 0.0f}; };
        b["mm_max"]     = [at](std::vector<Value>& a) { auto* g = at(a); return Value{g ? (float)g->maxPlayers : 0.0f}; };
        b["mm_room"]    = [at](std::vector<Value>& a) { auto* g = at(a); return Value{g ? g->room : std::string{}}; };
        // Join a cached session by index over the normal UDP transport.
        b["mm_join"] = [this, at](std::vector<Value>& a) {
            const GameSession* g = at(a);
            if (!g) return Value{0.0f};
            NetworkManager* n = EnsureNet();
            return Value{(n && n->StartClient(g->hostAddr, (std::uint16_t)g->port)) ? 1.0f : 0.0f};
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
        // Broadcast a chat line (stamped with your name, logged everywhere).
        b["net_chat"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && !a.empty()) n->Chat(a[0].AsString());
            return Value{};
        };
        // Fire a named RPC on every other peer: net_rpc("name"[, data]).
        b["net_rpc"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && !a.empty()) n->Rpc(a[0].AsString(), a.size() > 1 ? a[1].AsString() : std::string{});
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
        // Spawn a prefab THIS peer owns + auto-syncs (bullets, items); returns its
        // sync id for a later net_despawn: net_spawn_owned("file", x, y[, z]).
        b["net_spawn_owned"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && !a.empty()) {
                GameObject* g = n->SpawnOwned(a[0].AsString(),
                                              {a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                               a.size() > 2 ? a[2].AsFloat() : 0.0f,
                                               a.size() > 3 ? a[3].AsFloat() : 0.0f});
                if (g) if (auto* ns = g->GetComponent<NetworkSync>()) return Value{ns->netId};
            }
            return Value{std::string{}};
        };
        // Despawn a net_spawn_owned object on every peer by its sync id.
        b["net_despawn"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && !a.empty()) n->Despawn(a[0].AsString());
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
        // Steam Cloud extras.
        b["steam_cloud_has"] = [](std::vector<Value>& a) {
            return Value{(!a.empty() && Steam::Get().CloudHasFile(a[0].AsString())) ? 1.0f : 0.0f};
        };
        b["steam_cloud_delete"] = [](std::vector<Value>& a) {
            return Value{(!a.empty() && Steam::Get().CloudDelete(a[0].AsString())) ? 1.0f : 0.0f};
        };
        b["steam_cloud_count"] = [](std::vector<Value>&) {
            return Value{(float)Steam::Get().CloudFiles().size()};
        };
        // Steam Workshop (community content).
        b["steam_workshop_publish"] = [](std::vector<Value>& a) {
            WorkshopItem it;
            if (a.size() >= 1) it.title = a[0].AsString();
            if (a.size() >= 2) it.contentPath = a[1].AsString();
            if (a.size() >= 3) it.description = a[2].AsString();
            return Value{(float)Steam::Get().WorkshopPublish(it)};
        };
        b["steam_workshop_subscribe"] = [](std::vector<Value>& a) {
            return Value{(!a.empty() && Steam::Get().WorkshopSubscribe((std::uint64_t)a[0].AsFloat())) ? 1.0f : 0.0f};
        };
        b["steam_workshop_unsubscribe"] = [](std::vector<Value>& a) {
            return Value{(!a.empty() && Steam::Get().WorkshopUnsubscribe((std::uint64_t)a[0].AsFloat())) ? 1.0f : 0.0f};
        };
        b["steam_workshop_count"] = [](std::vector<Value>&) {
            return Value{(float)Steam::Get().WorkshopSubscribedItems().size()};
        };
        b["steam_workshop_path"] = [](std::vector<Value>& a) {
            return Value{a.empty() ? std::string{} : Steam::Get().WorkshopItemPath((std::uint64_t)a[0].AsFloat())};
        };
        // ---- Player accounts (shared process-wide service) ------------
        // account_login("user","pass") / account_register(...) return 1 on
        // success and 0 on failure; account_error() carries the reason.
        b["account_register"] = [](std::vector<Value>& a) {
            if (a.size() < 2) return Value{0.0f};
            return Value{Account::Register(a[0].AsString(), a[1].AsString()).ok ? 1.0f : 0.0f};
        };
        b["account_login"] = [](std::vector<Value>& a) {
            if (a.size() < 2) return Value{0.0f};
            return Value{Account::Login(a[0].AsString(), a[1].AsString()).ok ? 1.0f : 0.0f};
        };
        b["account_logout"] = [](std::vector<Value>&) { Account::Logout(); return Value{}; };
        b["account_is_logged_in"] = [](std::vector<Value>&) { return Value{Account::IsLoggedIn() ? 1.0f : 0.0f}; };
        b["account_is_online"] = [](std::vector<Value>&) { return Value{Account::IsOnline() ? 1.0f : 0.0f}; };
        b["account_username"] = [](std::vector<Value>&) { return Value{Account::Username()}; };
        b["account_token"]    = [](std::vector<Value>&) { return Value{Account::Token()}; };
        b["account_error"]    = [](std::vector<Value>&) { return Value{Account::LastError()}; };
        // Re-check the saved session with the server (signs out if rejected).
        b["account_verify"]   = [](std::vector<Value>&) { return Value{Account::VerifySession() ? 1.0f : 0.0f}; };
        // Authenticated requests to the account server (bearer token attached).
        // account_get(path) / account_post(path, json) return the response body,
        // or "" if the request didn't reach a 2xx (offline, not signed in, ...).
        b["account_get"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            auto r = Account::Api(a[0].AsString(), "GET");
            return Value{r.ok ? r.body : std::string{}};
        };
        b["account_post"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            auto r = Account::Api(a[0].AsString(), "POST", a.size() > 1 ? a[1].AsString() : std::string{});
            return Value{r.ok ? r.body : std::string{}};
        };
        // Cloud saves: per-account storage on the server so progress follows the
        // player across devices. cloud_save("slot1", save_text) etc.
        b["cloud_save"] = [](std::vector<Value>& a) {
            return Value{(a.size() >= 2 && Account::CloudSave(a[0].AsString(), a[1].AsString())) ? 1.0f : 0.0f};
        };
        b["cloud_load"] = [](std::vector<Value>& a) {
            return Value{a.empty() ? std::string{} : Account::CloudLoad(a[0].AsString())};
        };
        b["cloud_has"] = [](std::vector<Value>& a) {
            return Value{(!a.empty() && Account::CloudHas(a[0].AsString())) ? 1.0f : 0.0f};
        };
        b["cloud_delete"] = [](std::vector<Value>& a) {
            return Value{(!a.empty() && Account::CloudDelete(a[0].AsString())) ? 1.0f : 0.0f};
        };
        b["cloud_list"] = [](std::vector<Value>&) {
            Value v = Value::MakeArray();
            if (auto arr = v.AsArray())
                for (auto& k : Account::CloudList()) arr->push_back(Value{k});
            return v;
        };
        // Server leaderboards: leaderboard_submit("high", score) keeps the
        // player's best; leaderboard_top("high", n) returns "rank,name,score"
        // strings (same shape as steam_leaderboard_top).
        b["leaderboard_submit"] = [](std::vector<Value>& a) {
            return Value{(a.size() >= 2 && Account::LeaderboardSubmit(a[0].AsString(), (long)a[1].AsFloat())) ? 1.0f : 0.0f};
        };
        b["leaderboard_top"] = [](std::vector<Value>& a) {
            Value v = Value::MakeArray();
            auto arr = v.AsArray();
            if (!a.empty() && arr) {
                int n = a.size() > 1 ? (int)a[1].AsFloat() : 10;
                for (auto& e : Account::LeaderboardTop(a[0].AsString(), n))
                    arr->push_back(Value{std::to_string(e.rank) + "," + e.name + "," + std::to_string(e.score)});
            }
            return v;
        };
        // Scriptable Objects: read reusable .okaydata assets (item/enemy/level
        // definitions, config). Loaded + cached by path.
        b["data_num"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{0.0f};
            double def = a.size() > 2 ? a[2].AsFloat() : 0.0;
            return Value{(float)DataAsset::Cached(a[0].AsString()).GetNumber(a[1].AsString(), def)};
        };
        b["data_str"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{std::string{}};
            std::string def = a.size() > 2 ? a[2].AsString() : std::string{};
            return Value{DataAsset::Cached(a[0].AsString()).GetString(a[1].AsString(), def)};
        };
        b["data_has"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{false};
            return Value{DataAsset::Cached(a[0].AsString()).Has(a[1].AsString())};
        };
        // Customize a data asset from code (then data_save to persist it).
        b["data_set"] = [](std::vector<Value>& a) {
            if (a.size() >= 3) DataAsset::Cached(a[0].AsString()).Set(a[1].AsString(), a[2].AsString());
            return Value{};
        };
        b["data_save"] = [](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{false};
            return Value{DataAsset::Cached(a[0].AsString()).Save(a[0].AsString())};
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

        // Easy-Save-3-style save system: typed values, many files, write-through.
        //   save("coins", 10)                 -> default file
        //   save("spawn", new Vector3(1,2,3), "slot1.okaysave")
        //   load("coins", 0)  /  load("name", "Hero")  /  load("spawn", vec)
        // The 3rd arg (file) is optional everywhere; values keep their type.
        auto saveFile = [](std::vector<Value>& a, size_t idx) -> std::string {
            return (a.size() > idx && a[idx].IsString()) ? a[idx].AsString() : Save::DefaultFile();
        };
        b["save"] = [saveFile](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{false};
            std::string key = a[0].AsString(), file = saveFile(a, 2);
            if (a[1].IsVec3())        Save::SetVec3(key, a[1].AsVec3(), file);
            else if (a[1].IsString()) Save::SetString(key, a[1].AsString(), file);
            else                      Save::SetFloat(key, a[1].AsFloat(), file);
            return Value{true};
        };
        b["load"] = [saveFile](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{0.0f};
            std::string key = a[0].AsString(), file = saveFile(a, 2);
            if (a.size() > 1 && a[1].IsVec3())   return Value{Save::GetVec3(key, a[1].AsVec3(), file)};
            if (a.size() > 1 && a[1].IsString()) return Value{Save::GetString(key, a[1].AsString(), file)};
            float def = a.size() > 1 ? a[1].AsFloat() : 0.0f;
            return Value{Save::GetFloat(key, def, file)};
        };
        b["save_has"] = [saveFile](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{false};
            return Value{Save::Has(a[0].AsString(), saveFile(a, 1))};
        };
        b["save_delete"] = [saveFile](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{};
            Save::Delete(a[0].AsString(), saveFile(a, 1)); return Value{};
        };
        b["save_clear"] = [](std::vector<Value>& a) -> Value {
            Save::Clear(a.empty() || !a[0].IsString() ? Save::DefaultFile() : a[0].AsString());
            return Value{};
        };
        b["save_exists"] = [](std::vector<Value>& a) -> Value {
            return Value{Save::FileExists(a.empty() || !a[0].IsString() ? Save::DefaultFile() : a[0].AsString())};
        };
        b["save_delete_file"] = [](std::vector<Value>& a) -> Value {
            return Value{Save::DeleteFile(a.empty() || !a[0].IsString() ? Save::DefaultFile() : a[0].AsString())};
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
        // Clamp to [0,1] (Mathf.Clamp01).
        b["clamp01"] = [](std::vector<Value>& a) {
            return Value{Mathf::Clamp01(a.empty() ? 0.0f : a[0].AsFloat())};
        };
        // Where v lies between a and b, as 0..1 (Mathf.InverseLerp).
        b["inverse_lerp"] = [](std::vector<Value>& a) {
            float lo = a.size() > 0 ? a[0].AsFloat() : 0, hi = a.size() > 1 ? a[1].AsFloat() : 1,
                  v  = a.size() > 2 ? a[2].AsFloat() : 0;
            if (hi - lo == 0.0f) return Value{0.0f};
            return Value{Mathf::Clamp01((v - lo) / (hi - lo))};
        };
        // ---- Easing curves: map a normalized time t (0..1) to an eased 0..1 ----
        // For tweening/animation. Pass the raw progress; multiply the result into a
        // range yourself, or use with lerp(a, b, ease_out(t)).
        auto E = [](std::vector<Value>& a) { return Mathf::Clamp01(a.empty() ? 0.0f : a[0].AsFloat()); };
        b["ease_in"]  = [E](std::vector<Value>& a) { float t = E(a); return Value{t * t}; };
        b["ease_out"] = [E](std::vector<Value>& a) { float t = E(a); return Value{1.0f - (1.0f - t) * (1.0f - t)}; };
        b["ease_in_out"] = [E](std::vector<Value>& a) {
            float t = E(a);
            return Value{t < 0.5f ? 2.0f * t * t : 1.0f - 0.5f * (2.0f - 2.0f * t) * (2.0f - 2.0f * t)};
        };
        b["ease_in_cubic"]  = [E](std::vector<Value>& a) { float t = E(a); return Value{t * t * t}; };
        b["ease_out_cubic"] = [E](std::vector<Value>& a) { float t = 1.0f - E(a); return Value{1.0f - t * t * t}; };
        b["ease_in_out_cubic"] = [E](std::vector<Value>& a) {
            float t = E(a);
            if (t < 0.5f) return Value{4.0f * t * t * t};
            float f = -2.0f * t + 2.0f;
            return Value{1.0f - 0.5f * f * f * f};
        };
        b["ease_back"] = [E](std::vector<Value>& a) {   // slight overshoot backward then in
            float t = E(a); const float c1 = 1.70158f, c3 = c1 + 1.0f;
            return Value{c3 * t * t * t - c1 * t * t};
        };
        b["ease_elastic"] = [E](std::vector<Value>& a) {  // springy ease-out
            float t = E(a);
            if (t == 0.0f || t == 1.0f) return Value{t};
            const float c4 = 2.0944f;  // (2*pi)/3
            return Value{Mathf::Pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f};
        };
        b["ease_bounce"] = [E](std::vector<Value>& a) {   // bounce ease-out
            float t = E(a); const float n1 = 7.5625f, d1 = 2.75f;
            if (t < 1.0f / d1)      return Value{n1 * t * t};
            else if (t < 2.0f / d1) { t -= 1.5f / d1;  return Value{n1 * t * t + 0.75f}; }
            else if (t < 2.5f / d1) { t -= 2.25f / d1; return Value{n1 * t * t + 0.9375f}; }
            else                    { t -= 2.625f / d1; return Value{n1 * t * t + 0.984375f}; }
        };
        // Loop t into [0,length) (Mathf.Repeat).
        b["math_repeat"] = [](std::vector<Value>& a) {
            float t = a.size() > 0 ? a[0].AsFloat() : 0, len = a.size() > 1 ? a[1].AsFloat() : 1;
            if (len <= 0.0f) return Value{0.0f};
            float r = std::fmod(t, len); if (r < 0) r += len;
            return Value{r};
        };
        // Shortest signed difference between two angles, in degrees (Mathf.DeltaAngle).
        b["delta_angle"] = [](std::vector<Value>& a) {
            float from = a.size() > 0 ? a[0].AsFloat() : 0, to = a.size() > 1 ? a[1].AsFloat() : 0;
            float d = std::fmod(to - from, 360.0f);
            if (d < -180.0f) d += 360.0f; else if (d > 180.0f) d -= 360.0f;
            return Value{d};
        };
        // Interpolate angles taking the shortest path (Mathf.LerpAngle).
        b["lerp_angle"] = [](std::vector<Value>& a) {
            float from = a.size() > 0 ? a[0].AsFloat() : 0, to = a.size() > 1 ? a[1].AsFloat() : 0,
                  t = a.size() > 2 ? a[2].AsFloat() : 0;
            float d = std::fmod(to - from, 360.0f);
            if (d < -180.0f) d += 360.0f; else if (d > 180.0f) d -= 360.0f;
            return Value{from + d * t};
        };
        // Nearly equal (Mathf.Approximately).
        b["approximately"] = [](std::vector<Value>& a) {
            float x = a.size() > 0 ? a[0].AsFloat() : 0, y = a.size() > 1 ? a[1].AsFloat() : 0;
            return Value{std::fabs(x - y) < 1e-4f};
        };
        // Blend two colors/vectors by t (Color.Lerp / Vector3.Lerp on whole values).
        b["lerp_color"] = [](std::vector<Value>& a) {
            if (a.size() < 3) return Value{Vec3::Zero};
            Vec3 c1 = a[0].AsVec3(), c2 = a[1].AsVec3(); float t = Mathf::Clamp01(a[2].AsFloat());
            return Value{c1 + (c2 - c1) * t};
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
        // More array helpers.
        b["first"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray()) if (!arr->empty()) return arr->front();
            return Value{};
        };
        b["last"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray()) if (!arr->empty()) return arr->back();
            return Value{};
        };
        b["clear"] = [](std::vector<Value>& a) {
            if (!a.empty()) { if (auto arr = a[0].AsArray()) arr->clear(); else if (auto m = a[0].AsMap()) m->clear(); }
            return a.empty() ? Value{} : a[0];
        };
        b["insert_at"] = [](std::vector<Value>& a) {
            if (a.size() >= 3) if (auto arr = a[0].AsArray()) {
                int i = (int)a[1].AsFloat();
                if (i < 0) i = 0; if (i > (int)arr->size()) i = (int)arr->size();
                arr->insert(arr->begin() + i, a[2]);
            }
            return a.empty() ? Value{} : a[0];
        };
        b["slice"] = [](std::vector<Value>& a) {
            Value out = Value::MakeArray(); auto res = out.AsArray();
            if (!a.empty()) if (auto arr = a[0].AsArray()) {
                int n = (int)arr->size();
                int start = a.size() > 1 ? (int)a[1].AsFloat() : 0;
                int end   = a.size() > 2 ? (int)a[2].AsFloat() : n;
                if (start < 0) start += n; if (end < 0) end += n;
                start = start < 0 ? 0 : (start > n ? n : start);
                end   = end   < 0 ? 0 : (end   > n ? n : end);
                for (int i = start; i < end; ++i) res->push_back((*arr)[i]);
            }
            return out;
        };
        b["range"] = [](std::vector<Value>& a) {
            // range(n) -> [0..n), range(lo, hi) -> [lo..hi), range(lo, hi, step).
            Value out = Value::MakeArray(); auto res = out.AsArray();
            float lo = 0, hi = 0, step = 1;
            if (a.size() == 1) hi = a[0].AsFloat();
            else if (a.size() >= 2) { lo = a[0].AsFloat(); hi = a[1].AsFloat(); if (a.size() >= 3) step = a[2].AsFloat(); }
            if (step == 0) step = 1;
            int guard = 0;
            if (step > 0) for (float v = lo; v < hi && guard < 1000000; v += step, ++guard) res->push_back(Value{v});
            else          for (float v = lo; v > hi && guard < 1000000; v += step, ++guard) res->push_back(Value{v});
            return out;
        };
        b["sort_str"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto arr = a[0].AsArray())
                std::sort(arr->begin(), arr->end(),
                          [](const Value& x, const Value& y) { return x.AsString() < y.AsString(); });
            return a.empty() ? Value{} : a[0];
        };
        // More map helpers.
        b["map_values"] = [](std::vector<Value>& a) {
            Value out = Value::MakeArray(); auto res = out.AsArray();
            if (!a.empty()) if (auto m = a[0].AsMap()) for (auto& kv : *m) res->push_back(kv.second);
            return out;
        };
        b["map_clear"] = [](std::vector<Value>& a) {
            if (!a.empty()) if (auto m = a[0].AsMap()) m->clear();
            return a.empty() ? Value{} : a[0];
        };
        b["map_merge"] = [](std::vector<Value>& a) {
            // map_merge(dst, src): copy src's entries into dst (src wins on conflict).
            if (a.size() >= 2) if (auto d = a[0].AsMap()) if (auto s = a[1].AsMap())
                for (auto& kv : *s) (*d)[kv.first] = kv.second;
            return a.empty() ? Value{} : a[0];
        };
        // Type introspection: what kind of value is this?
        b["typeof"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string("null")};
            const Value& v = a[0];
            if (v.IsArray())  return Value{std::string("array")};
            if (v.IsMap())    return Value{std::string("map")};
            if (v.IsString()) return Value{std::string("string")};
            if (v.IsBool())   return Value{std::string("bool")};
            if (v.IsVec3())   return Value{std::string("vec3")};
            return Value{std::string("number")};
        };
        b["is_array"] = [](std::vector<Value>& a) { return Value{!a.empty() && a[0].IsArray()}; };
        b["is_map"]   = [](std::vector<Value>& a) { return Value{!a.empty() && a[0].IsMap()}; };
        b["is_str"]   = [](std::vector<Value>& a) { return Value{!a.empty() && a[0].IsString()}; };
        b["is_bool"]  = [](std::vector<Value>& a) { return Value{!a.empty() && a[0].IsBool()}; };
        b["is_num"]   = [](std::vector<Value>& a) { return Value{!a.empty() && a[0].IsNumber()}; };
        // fract(x): the fractional part of x (x - floor(x)).
        b["fract"] = [](std::vector<Value>& a) {
            float x = a.empty() ? 0.0f : a[0].AsFloat();
            return Value{x - Mathf::Floor(x)};
        };
        // JSON: serialize any value (arrays/maps included) to a string, and parse a
        // JSON string back into arrays/maps/numbers/strings/bools — for save data,
        // config, and network payloads. Aliased as json_stringify / json_parse.
        b["to_json"] = [](std::vector<Value>& a) {
            std::string out; if (!a.empty()) ToJson(a[0], out); else out = "null";
            return Value{out};
        };
        b["from_json"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            std::string js = a[0].AsString();   // keep alive: JsonReader holds a reference
            JsonReader r(js);
            return r.Parse();
        };
        // Higher-order array ops: each takes an array and the NAME of a script
        // function to apply per element, so games can write functional-style code
        // (map/filter/reduce) with a named callback. (call() lives further down.)
        b["map_fn"] = [this](std::vector<Value>& a) {
            Value out = Value::MakeArray(); auto res = out.AsArray();
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                std::string fn = a[1].AsString();
                for (auto& e : *arr) { std::vector<Value> ca{e}; res->push_back(rt.Call(fn, ca)); }
            }
            return out;
        };
        b["filter_fn"] = [this](std::vector<Value>& a) {
            Value out = Value::MakeArray(); auto res = out.AsArray();
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                std::string fn = a[1].AsString();
                for (auto& e : *arr) { std::vector<Value> ca{e}; if (rt.Call(fn, ca).AsBool()) res->push_back(e); }
            }
            return out;
        };
        b["reduce_fn"] = [this](std::vector<Value>& a) {
            // reduce_fn(arr, "fn", init): acc = fn(acc, element) folded left.
            if (a.size() < 2) return Value{};
            Value acc = a.size() >= 3 ? a[2] : Value{};
            if (auto arr = a[0].AsArray()) {
                std::string fn = a[1].AsString();
                for (auto& e : *arr) { std::vector<Value> ca{acc, e}; acc = rt.Call(fn, ca); }
            }
            return acc;
        };
        b["for_each"] = [this](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                std::string fn = a[1].AsString();
                for (auto& e : *arr) { std::vector<Value> ca{e}; rt.Call(fn, ca); }
            }
            return a.empty() ? Value{} : a[0];
        };
        b["find_fn"] = [this](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                std::string fn = a[1].AsString();
                for (auto& e : *arr) { std::vector<Value> ca{e}; if (rt.Call(fn, ca).AsBool()) return e; }
            }
            return Value{};
        };
        b["any_fn"] = [this](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                std::string fn = a[1].AsString();
                for (auto& e : *arr) { std::vector<Value> ca{e}; if (rt.Call(fn, ca).AsBool()) return Value{true}; }
            }
            return Value{false};
        };
        b["all_fn"] = [this](std::vector<Value>& a) {
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                std::string fn = a[1].AsString();
                for (auto& e : *arr) { std::vector<Value> ca{e}; if (!rt.Call(fn, ca).AsBool()) return Value{false}; }
            }
            return Value{true};
        };
        b["count_fn"] = [this](std::vector<Value>& a) {
            float n = 0;
            if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
                std::string fn = a[1].AsString();
                for (auto& e : *arr) { std::vector<Value> ca{e}; if (rt.Call(fn, ca).AsBool()) n += 1.0f; }
            }
            return Value{n};
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
        // Trim only the leading / only the trailing whitespace.
        b["trim_start"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            std::string s = a[0].AsString();
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c){ return !std::isspace(c); }));
            return Value{s};
        };
        b["trim_end"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            std::string s = a[0].AsString();
            s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c){ return !std::isspace(c); }).base(), s.end());
            return Value{s};
        };
        // Reverse the characters of a string.
        b["str_reverse"] = [](std::vector<Value>& a) {
            std::string s = a.empty() ? "" : a[0].AsString();
            std::reverse(s.begin(), s.end());
            return Value{s};
        };
        // capitalize("hello") -> "Hello" (first letter up, rest untouched).
        b["capitalize"] = [](std::vector<Value>& a) {
            std::string s = a.empty() ? "" : a[0].AsString();
            if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
            return Value{s};
        };
        // title_case("hello world") -> "Hello World" (capitalize each word).
        b["title_case"] = [](std::vector<Value>& a) {
            std::string s = a.empty() ? "" : a[0].AsString();
            bool start = true;
            for (auto& c : s) {
                if (std::isspace((unsigned char)c)) { start = true; }
                else { c = start ? (char)std::toupper((unsigned char)c) : (char)std::tolower((unsigned char)c); start = false; }
            }
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
        // pad_left("7", 3)      -> "  7"   ;  pad_left("7", 3, "0") -> "007"
        b["pad_left"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            std::string s = a[0].AsString();
            int width = a.size() > 1 ? (int)a[1].AsFloat() : 0;
            char fill = (a.size() > 2 && !a[2].AsString().empty()) ? a[2].AsString()[0] : ' ';
            while ((int)s.size() < width) s.insert(s.begin(), fill);
            return Value{s};
        };
        b["pad_right"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            std::string s = a[0].AsString();
            int width = a.size() > 1 ? (int)a[1].AsFloat() : 0;
            char fill = (a.size() > 2 && !a[2].AsString().empty()) ? a[2].AsString()[0] : ' ';
            while ((int)s.size() < width) s += fill;
            return Value{s};
        };
        // format("hp={} of {}", 7, 9) -> "hp=7 of 9" (sequential), and also
        // C#-style positional placeholders: format("{0} / {1}", a, b).
        b["format"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            const std::string tmpl = a[0].AsString();
            std::string out; std::size_t seq = 1;
            for (std::size_t i = 0; i < tmpl.size(); ++i) {
                if (tmpl[i] == '{') {
                    std::size_t close = tmpl.find('}', i);
                    if (close != std::string::npos) {
                        std::string inside = tmpl.substr(i + 1, close - i - 1);
                        if (inside.empty()) {                 // {} -> next arg in order
                            if (seq < a.size()) out += a[seq++].AsString();
                        } else {
                            bool num = !inside.empty();
                            for (char ch : inside) if (!std::isdigit((unsigned char)ch)) num = false;
                            if (num) { std::size_t idx = (std::size_t)std::stoi(inside) + 1;
                                       if (idx < a.size()) out += a[idx].AsString(); }
                            else { out += '{'; out += inside; out += '}'; }   // leave literal
                        }
                        i = close;
                        continue;
                    }
                }
                out += tmpl[i];
            }
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
        // raycast3(ox,oy,oz, dx,dy,dz [, maxDist]) returns the hit object's name
        // ("" = miss); details via ray3_hit/ray3_object/ray3_x/y/z/dist/nx/ny/nz.
        b["raycast3"] = [this](std::vector<Value>& a) -> Value {
            rt.lastHit3D = Runtime::LastHit{};
            if (a.size() < 6 || !rt.host || !rt.host->gameObject) return Value{std::string{}};
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value{std::string{}};
            Vec3 o{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            Vec3 d{a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()};
            float maxDist = a.size() > 6 ? a[6].AsFloat() : 1e9f;
            RaycastHit3D h = sc->physics3D().Raycast(*sc, o, d, maxDist);
            if (h.hit) {
                rt.lastHit3D.hit = true;
                rt.lastHit3D.object = h.gameObject ? h.gameObject->name : std::string{};
                rt.lastHit3D.px = h.point.x;  rt.lastHit3D.py = h.point.y;  rt.lastHit3D.pz = h.point.z;
                rt.lastHit3D.nx = h.normal.x; rt.lastHit3D.ny = h.normal.y; rt.lastHit3D.nz = h.normal.z;
                rt.lastHit3D.dist = h.distance;
            }
            return Value{rt.lastHit3D.object};
        };
        b["ray3_hit"]    = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit3D.hit}; };
        b["ray3_object"] = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit3D.object}; };
        b["ray3_x"]      = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit3D.px}; };
        b["ray3_y"]      = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit3D.py}; };
        b["ray3_z"]      = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit3D.pz}; };
        b["ray3_nx"]     = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit3D.nx}; };
        b["ray3_ny"]     = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit3D.ny}; };
        b["ray3_nz"]     = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit3D.nz}; };
        b["ray3_dist"]   = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit3D.dist}; };
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
        // call("fn", args...) invokes a function (user or builtin) by name, passing
        // through any extra arguments and returning its result.
        b["call"] = [this](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{};
            std::string fn = a[0].AsString();
            std::vector<Value> callArgs(a.begin() + 1, a.end());
            if (rt.functions.count(fn) || rt.builtins.count(fn)) return rt.Call(fn, callArgs);
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
        // Optional trailing on-complete: if arg `i` is the name of a script
        // function, returns a callback that invokes it when the tween finishes
        // (DOTween's OnComplete). Anything else yields an empty callback.
        auto onDoneFromArg = [this](std::vector<Value>& a, std::size_t i) -> std::function<void()> {
            if (i >= a.size() || !a[i].IsString()) return {};
            std::string fn = a[i].AsString();
            if (!rt.functions.count(fn)) return {};
            return [this, fn]() { std::vector<Value> none; rt.Call(fn, none); };
        };
        b["tween_move"] = [this, sched, easeFromArg, onDoneFromArg](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.size() < 3) return Value{};
            Vec3 start = t->localPosition, target{a[0].AsFloat(), a[1].AsFloat(), start.z};
            Ease e = easeFromArg(a, 3);
            s->Tween(a[2].AsFloat(), [t, start, target, e](float u) { float k = Easing::Evaluate(e, u); t->localPosition = start + (target - start) * k; }, onDoneFromArg(a, 4));
            return Value{};
        };
        b["tween_move3"] = [this, sched, easeFromArg, onDoneFromArg](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.size() < 4) return Value{};
            Vec3 start = t->localPosition, target{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            Ease e = easeFromArg(a, 4);
            s->Tween(a[3].AsFloat(), [t, start, target, e](float u) { float k = Easing::Evaluate(e, u); t->localPosition = start + (target - start) * k; }, onDoneFromArg(a, 5));
            return Value{};
        };
        b["tween_scale"] = [this, sched, easeFromArg, onDoneFromArg](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.size() < 2) return Value{};
            Vec3 start = t->localScale; float v = a[0].AsFloat(); Vec3 target{v, v, v};
            Ease e = easeFromArg(a, 2);
            s->Tween(a[1].AsFloat(), [t, start, target, e](float u) { float k = Easing::Evaluate(e, u); t->localScale = start + (target - start) * k; }, onDoneFromArg(a, 3));
            return Value{};
        };
        // Spin by `deg` degrees about Z over `dur` seconds.
        b["tween_rotate"] = [this, sched, easeFromArg, onDoneFromArg](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.size() < 2) return Value{};
            Quat start = t->localRotation; float deg = a[0].AsFloat(); Ease e = easeFromArg(a, 2);
            s->Tween(a[1].AsFloat(), [t, start, deg, e](float u) { float k = Easing::Evaluate(e, u); t->localRotation = start * Quat::Euler({0, 0, deg * k}); }, onDoneFromArg(a, 3));
            return Value{};
        };
        // Ping-pong move forever between the current spot and (x, y) — great for
        // floating pickups, patrolling platforms, hovering UI. Each leg eases.
        b["tween_loop_move"] = [this, sched, easeFromArg](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.size() < 3) return Value{};
            Vec3 start = t->localPosition, target{a[0].AsFloat(), a[1].AsFloat(), start.z};
            float dur = a[2].AsFloat(); Ease e = easeFromArg(a, 3);
            auto step = std::make_shared<std::function<void(bool)>>();
            *step = [s, t, start, target, e, dur, step](bool fwd) {
                Vec3 from = fwd ? start : target, to = fwd ? target : start;
                s->Tween(dur, [t, from, to, e](float u) { float k = Easing::Evaluate(e, u); t->localPosition = from + (to - from) * k; },
                         [step, fwd]() { (*step)(!fwd); });
            };
            (*step)(true);
            return Value{};
        };
        // Punch the scale outward and settle back (DOTween's DOPunchScale) —
        // perfect for button presses, pickups, "juice". `amount` is the bulge.
        b["tween_punch_scale"] = [this, sched](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.size() < 2) return Value{};
            Vec3 start = t->localScale; float amount = a[0].AsFloat(); float dur = a[1].AsFloat();
            float vib = a.size() > 2 ? a[2].AsFloat() : 6.0f;
            s->Tween(dur, [t, start, amount, vib](float u) {
                float osc = Mathf::Sin(u * Mathf::PI * vib) * amount * (1.0f - u);
                t->localScale = start + Vec3{osc, osc, osc};
            }, [t, start]() { t->localScale = start; });
            return Value{};
        };
        // Punch the position toward (dx, dy) and settle back.
        b["tween_punch_pos"] = [this, sched](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.size() < 3) return Value{};
            Vec3 start = t->localPosition; Vec2 dir{a[0].AsFloat(), a[1].AsFloat()}; float dur = a[2].AsFloat();
            float vib = a.size() > 3 ? a[3].AsFloat() : 6.0f;
            s->Tween(dur, [t, start, dir, vib](float u) {
                float osc = Mathf::Sin(u * Mathf::PI * vib) * (1.0f - u);
                t->localPosition = start + Vec3{dir.x * osc, dir.y * osc, 0.0f};
            }, [t, start]() { t->localPosition = start; });
            return Value{};
        };
        // Shake the position randomly, decaying to a stop (camera/impact shake).
        b["tween_shake"] = [this, sched](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.size() < 2) return Value{};
            Vec3 start = t->localPosition; float intensity = a[0].AsFloat(); float dur = a[1].AsFloat();
            s->Tween(dur, [t, start, intensity](float u) {
                float decay = (1.0f - u) * intensity;
                float ox = Random::Shared().Range(-1.0f, 1.0f) * decay;
                float oy = Random::Shared().Range(-1.0f, 1.0f) * decay;
                t->localPosition = start + Vec3{ox, oy, 0.0f};
            }, [t, start]() { t->localPosition = start; });
            return Value{};
        };
        // Relative move (DOTween's DOBlendableMoveBy): glide by an offset.
        b["tween_move_by"] = [this, sched, easeFromArg, onDoneFromArg](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.size() < 3) return Value{};
            Vec3 start = t->localPosition, target{start.x + a[0].AsFloat(), start.y + a[1].AsFloat(), start.z};
            Ease e = easeFromArg(a, 3);
            s->Tween(a[2].AsFloat(), [t, start, target, e](float u) { float k = Easing::Evaluate(e, u); t->localPosition = start + (target - start) * k; }, onDoneFromArg(a, 4));
            return Value{};
        };
        // Jump in an arc to (x, y) peaking `height` units up (DOTween's DOJump) —
        // coins flying to the HUD, hop-to-tile, loot pop.
        b["tween_jump"] = [this, sched, onDoneFromArg](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.size() < 4) return Value{};
            Vec3 start = t->localPosition, target{a[0].AsFloat(), a[1].AsFloat(), start.z};
            float height = a[2].AsFloat();
            s->Tween(a[3].AsFloat(), [t, start, target, height](float u) {
                Vec3 base = start + (target - start) * u;
                base.y += height * 4.0f * u * (1.0f - u);   // parabola, peak at u=0.5
                t->localPosition = base;
            }, [t, target, onDone = onDoneFromArg(a, 4)]() { t->localPosition = target; if (onDone) onDone(); });
            return Value{};
        };
        // Move through a list of waypoints (DOTween's DOPath): tween_path(dur, x1,y1, x2,y2, ...).
        b["tween_path"] = [this, sched](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.size() < 3) return Value{};
            float dur = a[0].AsFloat();
            auto pts = std::make_shared<std::vector<Vec3>>();
            pts->push_back(t->localPosition);
            for (std::size_t i = 1; i + 1 < a.size(); i += 2)
                pts->push_back(Vec3{a[i].AsFloat(), a[i + 1].AsFloat(), t->localPosition.z});
            if (pts->size() < 2) return Value{};
            s->Tween(dur, [t, pts](float u) {
                int segs = (int)pts->size() - 1;
                float f = u * segs; int i = (int)f; if (i >= segs) i = segs - 1;
                float local = f - i;
                t->localPosition = (*pts)[i] + ((*pts)[i + 1] - (*pts)[i]) * local;
            }, [t, pts]() { t->localPosition = pts->back(); });
            return Value{};
        };
        // Ping-pong the uniform scale forever (pulsing pickups, breathing UI).
        b["tween_loop_scale"] = [this, sched, easeFromArg](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.size() < 2) return Value{};
            Vec3 start = t->localScale; float v = a[0].AsFloat(); Vec3 target{v, v, v};
            float dur = a[1].AsFloat(); Ease e = easeFromArg(a, 2);
            auto step = std::make_shared<std::function<void(bool)>>();
            *step = [s, t, start, target, e, dur, step](bool fwd) {
                Vec3 from = fwd ? start : target, to = fwd ? target : start;
                s->Tween(dur, [t, from, to, e](float u) { float k = Easing::Evaluate(e, u); t->localScale = from + (to - from) * k; },
                         [step, fwd]() { (*step)(!fwd); });
            };
            (*step)(true);
            return Value{};
        };
        // Spin continuously forever: a full turn every `dur` seconds (loaders, coins).
        b["tween_loop_rotate"] = [this, sched](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.empty()) return Value{};
            float dur = a[0].AsFloat(); if (dur < 1e-3f) return Value{};
            float dir = a.size() > 1 ? a[1].AsFloat() : 1.0f;   // +1 = CCW, -1 = CW
            auto base = std::make_shared<Quat>(t->localRotation);
            auto step = std::make_shared<std::function<void()>>();
            *step = [s, t, dur, dir, base, step]() {
                Quat from = *base;
                s->Tween(dur, [t, from, dir](float u) { t->localRotation = from * Quat::Euler({0, 0, 360.0f * dir * u}); },
                         [t, base, step]() { *base = t->localRotation; (*step)(); });
            };
            (*step)();
            return Value{};
        };
        // Count a sibling TextRenderer's number from->to over dur (score ticks,
        // damage popups). Optional trailing string is a prefix: "Score: ".
        b["tween_number"] = [this, sched, go](std::vector<Value>& a) {
            Scheduler* s = sched();
            if (!s || a.size() < 3) return Value{};
            float from = a[0].AsFloat(), to = a[1].AsFloat(), dur = a[2].AsFloat();
            std::string prefix = (a.size() > 3 && a[3].IsString()) ? a[3].AsString() : std::string{};
            s->Tween(dur, [go, from, to, prefix](float u) {
                int val = (int)std::lround(from + (to - from) * u);
                if (GameObject* g = go())
                    if (auto* tr = g->GetComponent<TextRenderer>())
                        tr->text = prefix + std::to_string(val);
            }, [go, to, prefix]() {
                if (GameObject* g = go())
                    if (auto* tr = g->GetComponent<TextRenderer>())
                        tr->text = prefix + std::to_string((int)std::lround(to));
            });
            return Value{};
        };
        // Rotate to an ABSOLUTE Z angle over dur (DOTween's DORotate) — unlike
        // tween_rotate which spins by a relative amount.
        b["tween_rotate_to"] = [this, sched, easeFromArg, onDoneFromArg](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.size() < 2) return Value{};
            Quat start = t->localRotation, target = Quat::Euler({0, 0, a[0].AsFloat()});
            Ease e = easeFromArg(a, 2);
            s->Tween(a[1].AsFloat(), [t, start, target, e](float u) { float k = Easing::Evaluate(e, u); t->localRotation = Quat::Slerp(start, target, k); }, onDoneFromArg(a, 3));
            return Value{};
        };
        // Non-uniform scale to (sx, sy) (DOTween's DOScale with a Vector).
        b["tween_scale_xy"] = [this, sched, easeFromArg, onDoneFromArg](std::vector<Value>& a) {
            Transform* t = rt.host ? rt.host->transform : nullptr; Scheduler* s = sched();
            if (!t || !s || a.size() < 3) return Value{};
            Vec3 start = t->localScale, target{a[0].AsFloat(), a[1].AsFloat(), start.z};
            Ease e = easeFromArg(a, 3);
            s->Tween(a[2].AsFloat(), [t, start, target, e](float u) { float k = Easing::Evaluate(e, u); t->localScale = start + (target - start) * k; }, onDoneFromArg(a, 4));
            return Value{};
        };
        // Tween a UI widget's anchored position (UI widgets don't use the
        // Transform) — slide menus/panels in and out.
        b["tween_ui_move"] = [this, sched, easeFromArg, onDoneFromArg](std::vector<Value>& a) {
            Scheduler* s = sched();
            if (!s || !rt.host || !rt.host->gameObject || a.size() < 3) return Value{};
            UIRect r = GetUIRect(rt.host->gameObject);
            if (!r.valid || !r.position) return Value{};
            Vec2* pos = r.position; Vec2 start = *pos, target{a[0].AsFloat(), a[1].AsFloat()};
            Ease e = easeFromArg(a, 3);
            s->Tween(a[2].AsFloat(), [pos, start, target, e](float u) { float k = Easing::Evaluate(e, u); *pos = start + (target - start) * k; }, onDoneFromArg(a, 4));
            return Value{};
        };
        // Tween a UI widget's size (grow/shrink panels, popups).
        b["tween_ui_size"] = [this, sched, easeFromArg, onDoneFromArg](std::vector<Value>& a) {
            Scheduler* s = sched();
            if (!s || !rt.host || !rt.host->gameObject || a.size() < 3) return Value{};
            UIRect r = GetUIRect(rt.host->gameObject);
            if (!r.valid || !r.sizePtr) return Value{};
            Vec2* sz = r.sizePtr; Vec2 start = *sz, target{a[0].AsFloat(), a[1].AsFloat()};
            Ease e = easeFromArg(a, 3);
            s->Tween(a[2].AsFloat(), [sz, start, target, e](float u) { float k = Easing::Evaluate(e, u); *sz = start + (target - start) * k; }, onDoneFromArg(a, 4));
            return Value{};
        };
        // Returns the host's tweenable albedo color, if any (sprite, mesh, or UI).
        auto colorPtr = [this]() -> Color* {
            if (!rt.host || !rt.host->gameObject) return nullptr;
            GameObject* g = rt.host->gameObject;
            if (auto* sr = g->GetComponent<SpriteRenderer>()) return &sr->color;
            if (auto* mr = g->GetComponent<MeshRenderer>())   return &mr->color;
            if (auto* im = g->GetComponent<UIImage>())        return &im->color;
            if (auto* pn = g->GetComponent<UIPanel>())        return &pn->color;
            return nullptr;
        };
        b["tween_color"] = [this, sched, easeFromArg, colorPtr, onDoneFromArg](std::vector<Value>& a) {
            Scheduler* s = sched(); Color* c = colorPtr();
            if (!s || !c || a.size() < 4) return Value{};
            Color start = *c, target{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat(), start.a};
            Ease e = easeFromArg(a, 4);
            s->Tween(a[3].AsFloat(), [c, start, target, e](float u) {
                float k = Easing::Evaluate(e, u);
                c->r = start.r + (target.r - start.r) * k;
                c->g = start.g + (target.g - start.g) * k;
                c->b = start.b + (target.b - start.b) * k;
            }, onDoneFromArg(a, 5));
            return Value{};
        };
        b["tween_fade"] = [this, sched, easeFromArg, colorPtr, onDoneFromArg](std::vector<Value>& a) {
            Scheduler* s = sched(); Color* c = colorPtr();
            if (!s || !c || a.size() < 2) return Value{};
            float start = c->a, target = a[0].AsFloat(); Ease e = easeFromArg(a, 2);
            s->Tween(a[1].AsFloat(), [c, start, target, e](float u) { float k = Easing::Evaluate(e, u); c->a = start + (target - start) * k; }, onDoneFromArg(a, 3));
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

        // ---- Unity-style API: write scripts that look like C# in Unity. -----
        //      Dotted names are single tokens (see the lexer), so these read as
        //      Input.GetKeyDown("space"), Mathf.Sin(t), Debug.Log(x), etc.
        b["Vector3"] = [](std::vector<Value>& a) -> Value {
            return Value{Vec3{a.size() > 0 ? a[0].AsFloat() : 0.0f,
                              a.size() > 1 ? a[1].AsFloat() : 0.0f,
                              a.size() > 2 ? a[2].AsFloat() : 0.0f}};
        };
        b["Vector2"] = [](std::vector<Value>& a) -> Value {
            return Value{Vec3{a.size() > 0 ? a[0].AsFloat() : 0.0f,
                              a.size() > 1 ? a[1].AsFloat() : 0.0f, 0.0f}};
        };
        // Color(r, g, b) -> a Vec3 of RGB (pass to set_color / set_tint).
        b["Color"] = [](std::vector<Value>& a) -> Value {
            return Value{Vec3{a.size() > 0 ? a[0].AsFloat() : 1.0f,
                              a.size() > 1 ? a[1].AsFloat() : 1.0f,
                              a.size() > 2 ? a[2].AsFloat() : 1.0f}};
        };
        // Vector math on Vec3 values (from Vector3(...)/new Vector3(...)). Read
        // components with v.x/v.y/v.z (see property access) or vec_x/y/z(v).
        b["vec_add"]   = [](std::vector<Value>& a) -> Value { return Value{(a.size() > 0 ? a[0].AsVec3() : Vec3::Zero) + (a.size() > 1 ? a[1].AsVec3() : Vec3::Zero)}; };
        b["vec_sub"]   = [](std::vector<Value>& a) -> Value { return Value{(a.size() > 0 ? a[0].AsVec3() : Vec3::Zero) - (a.size() > 1 ? a[1].AsVec3() : Vec3::Zero)}; };
        b["vec_scale"] = [](std::vector<Value>& a) -> Value { return Value{(a.size() > 0 ? a[0].AsVec3() : Vec3::Zero) * (a.size() > 1 ? a[1].AsFloat() : 1.0f)}; };
        b["vec_length"]= [](std::vector<Value>& a) -> Value { return Value{a.empty() ? 0.0f : a[0].AsVec3().Magnitude()}; };
        b["vec_dot"]   = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{0.0f};
            Vec3 u = a[0].AsVec3(), v = a[1].AsVec3();
            return Value{u.x * v.x + u.y * v.y + u.z * v.z};
        };
        b["vec_distance"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{0.0f};
            return Value{(a[0].AsVec3() - a[1].AsVec3()).Magnitude()};
        };
        b["vec_normalize"] = [](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{Vec3::Zero};
            Vec3 v = a[0].AsVec3(); float m = v.Magnitude();
            return Value{m > 1e-6f ? v * (1.0f / m) : Vec3::Zero};
        };
        // Angle in degrees between two vectors (Vector3.Angle).
        b["vec_angle"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{0.0f};
            Vec3 u = a[0].AsVec3(), v = a[1].AsVec3();
            float mu = u.Magnitude(), mv = v.Magnitude();
            if (mu < 1e-6f || mv < 1e-6f) return Value{0.0f};
            float c = (u.x * v.x + u.y * v.y + u.z * v.z) / (mu * mv);
            return Value{std::acos(Mathf::Clamp(c, -1.0f, 1.0f)) * Mathf::Rad2Deg};
        };
        b["vec_lerp"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 3) return Value{Vec3::Zero};
            Vec3 u = a[0].AsVec3(), v = a[1].AsVec3(); float t = a[2].AsFloat();
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            return Value{u + (v - u) * t};
        };
        b["vec_x"] = [](std::vector<Value>& a) -> Value { return Value{a.empty() ? 0.0f : a[0].AsVec3().x}; };
        b["vec_y"] = [](std::vector<Value>& a) -> Value { return Value{a.empty() ? 0.0f : a[0].AsVec3().y}; };
        b["vec_z"] = [](std::vector<Value>& a) -> Value { return Value{a.empty() ? 0.0f : a[0].AsVec3().z}; };
        b["vec_move_towards"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 3) return Value{Vec3::Zero};
            Vec3 c = a[0].AsVec3(), t = a[1].AsVec3(); float md = a[2].AsFloat();
            Vec3 d = t - c; float dist = d.Magnitude();
            if (dist <= md || dist < 1e-6f) return Value{t};
            return Value{c + d * (md / dist)};
        };
        // Smooth 2D value noise in [0,1] (a stand-in for Unity's Mathf.PerlinNoise).
        b["noise"] = [](std::vector<Value>& a) -> Value {
            float x = a.size() > 0 ? a[0].AsFloat() : 0.0f;
            float y = a.size() > 1 ? a[1].AsFloat() : 0.0f;
            auto hash = [](int xi, int yi) {
                unsigned h = (unsigned)(xi * 374761393 + yi * 668265263);
                h = (h ^ (h >> 13)) * 1274126177u;
                return (float)((h ^ (h >> 16)) & 0xFFFFFFu) / (float)0xFFFFFFu;
            };
            int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
            float fx = x - x0, fy = y - y0;
            auto sm = [](float t) { return t * t * (3.0f - 2.0f * t); };
            float sx = sm(fx), sy = sm(fy);
            float n00 = hash(x0, y0), n10 = hash(x0 + 1, y0);
            float n01 = hash(x0, y0 + 1), n11 = hash(x0 + 1, y0 + 1);
            float nx0 = n00 + (n10 - n00) * sx, nx1 = n01 + (n11 - n01) * sx;
            return Value{nx0 + (nx1 - nx0) * sy};
        };
        alias("vec3", "Vector3");
        alias("vec2", "Vector2");
        alias("Vector3.Lerp", "vec_lerp");
        alias("Vector3.Dot", "vec_dot");
        alias("Vector3.Normalize", "vec_normalize");
        alias("Vector3.MoveTowards", "vec_move_towards");
        alias("Mathf.PerlinNoise", "noise");
        alias("Mathf.Clamp01", "clamp01");
        alias("Mathf.InverseLerp", "inverse_lerp");
        alias("Mathf.Repeat", "math_repeat");
        alias("Mathf.DeltaAngle", "delta_angle");
        alias("Mathf.LerpAngle", "lerp_angle");
        alias("Mathf.Approximately", "approximately");
        alias("Mathf.Log", "log");   alias("Mathf.Exp", "exp");
        alias("Mathf.Asin", "asin"); alias("Mathf.Acos", "acos"); alias("Mathf.Atan", "atan");
        alias("Color.Lerp", "lerp_color");
        alias("Vector3.LerpUnclamped", "vec_lerp");
        alias("Vector2.Lerp", "vec_lerp");
        alias("Vector3.Angle", "vec_angle");
        alias("Vector2.Angle", "vec_angle");
        alias("Input.GetButton", "key");
        alias("Input.GetButtonDown", "key_down");
        alias("Input.GetButtonUp", "key_up");
        alias("string.Format", "format");
        alias("String.Format", "format");
        // Quaternion.Euler(x, y, z) -> a Vec3 of euler angles (assignable to
        // transform.rotation, which uses the Z component for 2D).
        b["Quaternion.Euler"] = [](std::vector<Value>& a) -> Value {
            return Value{Vec3{a.size() > 0 ? a[0].AsFloat() : 0.0f,
                              a.size() > 1 ? a[1].AsFloat() : 0.0f,
                              a.size() > 2 ? a[2].AsFloat() : 0.0f}};
        };
        // AddComponent<T>() / AddComponent("T") — add a known component at runtime.
        b["AddComponent"] = [this](std::vector<Value>& a) -> Value {
            if (!rt.host || !rt.host->gameObject || a.empty()) return Value{false};
            const std::string& c = a[0].AsString();
            GameObject* g = rt.host->gameObject;
            if (c == "SpriteRenderer") { g->AddComponent<SpriteRenderer>(); return Value{true}; }
            if (c == "Rigidbody2D")    { g->AddComponent<Rigidbody2D>();    return Value{true}; }
            if (c == "TextRenderer")   { g->AddComponent<TextRenderer>();   return Value{true}; }
            if (c == "BoxCollider2D")  { g->AddComponent<BoxCollider2D>();  return Value{true}; }
            return Value{false};
        };
        // Input.GetAxis("Horizontal"/"Vertical") -> WASD/stick axis.
        b["Input.GetAxis"] = [](std::vector<Value>& a) -> Value {
            std::string ax = a.empty() ? std::string{} : a[0].AsString();
            return Value{ax == "Vertical" ? Input::AxisWASD().y : Input::AxisWASD().x};
        };
        // gameObject.SetActive(bool) toggles this object.
        b["gameObject.SetActive"] = [this](std::vector<Value>& a) -> Value {
            if (rt.host && rt.host->gameObject)
                rt.host->gameObject->active = a.empty() ? true : a[0].AsBool();
            return Value{};
        };
        b["GetComponent"] = [this](std::vector<Value>& a) -> Value {
            // Lightweight: true if this object has the named component (so scripts
            // can guard with `if (GetComponent("Rigidbody2D")) ...`).
            if (!rt.host || !rt.host->gameObject || a.empty()) return Value{false};
            const std::string& c = a[0].AsString();
            GameObject* g = rt.host->gameObject;
            bool has = (c == "SpriteRenderer" && g->GetComponent<SpriteRenderer>())
                    || (c == "Rigidbody2D"   && g->GetComponent<Rigidbody2D>())
                    || (c == "TextRenderer"  && g->GetComponent<TextRenderer>());
            return Value{has};
        };
        // A few more math helpers Unity scripts reach for (clamp01 / lerp_angle
        // already exist; these are new and avoid clobbering string repeat()).
        b["math_repeat"] = [](std::vector<Value>& a) -> Value {   // Mathf.Repeat: wrap into [0, len)
            if (a.size() < 2) return Value{0.0f};
            float t = a[0].AsFloat(), len = a[1].AsFloat();
            if (len == 0.0f) return Value{0.0f};
            float m = std::fmod(t, len); if (m < 0.0f) m += len;
            return Value{m};
        };
        b["inverse_lerp"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 3) return Value{0.0f};
            float lo = a[0].AsFloat(), hi = a[1].AsFloat(), v = a[2].AsFloat();
            if (hi == lo) return Value{0.0f};
            float t = (v - lo) / (hi - lo);
            return Value{t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t)};
        };
        b["approximately"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{false};
            return Value{Mathf::Abs(a[0].AsFloat() - a[1].AsFloat()) < 1e-4f};
        };
        b["delta_angle"] = [](std::vector<Value>& a) -> Value {   // shortest signed diff
            if (a.size() < 2) return Value{0.0f};
            float d = std::fmod(a[1].AsFloat() - a[0].AsFloat(), 360.0f);
            if (d > 180.0f) d -= 360.0f; if (d < -180.0f) d += 360.0f;
            return Value{d};
        };
        alias("Mathf.Clamp01", "clamp01");
        alias("Mathf.Repeat", "math_repeat");
        alias("Mathf.InverseLerp", "inverse_lerp");
        alias("Mathf.Approximately", "approximately");
        alias("Mathf.DeltaAngle", "delta_angle");
        alias("Mathf.LerpAngle", "lerp_angle");
        alias("inverse_lerp_value", "inverse_lerp");

        // Method-name aliases mapping Unity calls onto existing builtins.
        alias("Input.GetKey", "key");
        alias("Input.GetKeyDown", "key_down");
        alias("Input.GetKeyUp", "key_up");
        alias("Input.GetMouseButton", "mouse");
        alias("Input.GetMouseButtonDown", "mouse_down");
        alias("Input.GetMouseButtonUp", "mouse_up");
        alias("Debug.Log", "print");
        alias("Debug.LogWarning", "print");
        alias("Debug.LogError", "print");
        alias("Mathf.Sin", "sin");   alias("Mathf.Cos", "cos");   alias("Mathf.Tan", "tan");
        alias("Mathf.Sqrt", "sqrt"); alias("Mathf.Abs", "abs");   alias("Mathf.Sign", "sign");
        alias("Mathf.Floor", "floor"); alias("Mathf.Ceil", "ceil"); alias("Mathf.Round", "round");
        alias("Mathf.Min", "min");   alias("Mathf.Max", "max");   alias("Mathf.Pow", "pow");
        alias("Mathf.Clamp", "clamp"); alias("Mathf.Lerp", "lerp"); alias("Mathf.Atan2", "atan2");
        alias("Mathf.PingPong", "ping_pong"); alias("Mathf.SmoothStep", "smoothstep");
        alias("Mathf.MoveTowards", "move_toward");
        alias("Random.Range", "rand");
        alias("Vector3.Distance", "vec_distance"); alias("Vector2.Distance", "vec_distance");
        alias("transform.Translate", "move"); alias("transform.Rotate", "rotate");
        alias("transform.LookAt", "look_at3");
        alias("Instantiate", "spawn"); alias("Object.Instantiate", "spawn");
        alias("Destroy", "destroy"); alias("Object.Destroy", "destroy");
        alias("Physics2D.Raycast", "raycast"); alias("Physics.Raycast", "raycast3");
        alias("SceneManager.LoadScene", "load_scene_name");
        alias("Application.Quit", "quit");
        alias("GameObject.Find", "exists");

        // ---- Plain-English aliases: say what you mean. Every name below points
        //      at an existing builtin, so beginners can guess the right word. ----
        // Input
        alias("is_key_held", "key");
        alias("key_pressed", "key_down");
        alias("key_released", "key_up");
        alias("input_horizontal", "axis_x");
        alias("input_vertical", "axis_y");
        alias("move_input_x", "axis_x");
        alias("move_input_y", "axis_y");
        alias("is_mouse_held", "mouse");
        alias("mouse_pressed", "mouse_down");
        alias("mouse_released", "mouse_up");
        alias("mouse_position_x", "mouse_x");
        alias("mouse_position_y", "mouse_y");
        // Transform / movement
        alias("move_by", "move");
        alias("place_at", "set_pos");
        alias("set_position_x", "set_x");
        alias("set_position_y", "set_y");
        alias("position_x", "pos_x");
        alias("position_y", "pos_y");
        alias("position_z", "pos_z");
        alias("turn", "rotate");
        alias("face", "look_at");
        alias("face_3d", "look_at3");
        alias("set_size", "set_scale");
        alias("move_towards_object", "move_toward3");
        // Time & scheduling
        alias("frame_time", "dt");
        alias("time_elapsed", "time");
        alias("game_time", "time");
        alias("call_later", "after");
        alias("invoke_after", "after");
        alias("repeat_every", "every");
        alias("set_time_scale", "set_timescale");
        alias("time_scale", "timescale");
        // Objects & scene
        alias("create_object", "spawn");
        alias("create_object_3d", "spawn3");
        alias("remove_object", "destroy");
        alias("show_object", "activate");
        alias("hide_object", "deactivate");
        alias("object_exists", "exists");
        alias("is_object_active", "is_active");
        alias("object_x", "obj_x");
        alias("object_y", "obj_y");
        alias("object_z", "obj_z");
        alias("nearest_with_tag", "nearest_tag");
        alias("count_with_tag", "count_tag");
        // Rendering & components
        alias("set_tint", "set_color");
        alias("set_sprite", "set_texture");
        alias("set_background_color", "set_bg");
        alias("set_background", "set_bg");
        alias("set_light_direction", "set_light");
        alias("set_ui_image", "set_image");
        alias("set_button_enabled", "set_interactable");
        alias("set_progress_bar", "set_progress");
        alias("emit_particles", "emit");
        alias("play_sfx", "play_sound");
        alias("set_flip_x", "flip_x");
        alias("set_flip_y", "flip_y");
        // Camera
        alias("camera_x", "cam_x");
        alias("camera_y", "cam_y");
        alias("set_camera", "set_cam");
        alias("move_camera", "move_cam");
        alias("camera_zoom", "cam_zoom");
        alias("set_camera_zoom", "set_cam_zoom");
        alias("set_zoom", "set_cam_zoom");
        // Physics
        alias("is_overlapping", "overlap");
        alias("point_overlaps", "overlap");
        alias("cast_ray", "raycast");
        alias("apply_force", "add_force");
        alias("apply_impulse", "add_impulse");
        alias("get_velocity_x", "velocity_x");
        alias("get_velocity_y", "velocity_y");
        // Saving / prefs
        alias("save_value", "prefs_set");
        alias("load_number", "prefs_get");
        alias("load_value", "prefs_get");
        alias("load_text", "prefs_get_str");
        alias("write_save", "prefs_save");
        alias("read_save", "prefs_load");
        alias("set_master_volume", "set_volume");
        // Math
        alias("random_float", "rand");
        alias("random_chance", "chance");
        alias("roll_chance", "chance");
        alias("blend", "lerp");
        // Logging
        alias("log", "print");
        alias("log_message", "print");
        alias("json_stringify", "to_json");
        alias("json_parse", "from_json");
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
    } catch (const ScriptError& e) {
        // Prefix the source line when known, so editors can show a diagnostic.
        std::string msg = e.line > 0 ? "line " + std::to_string(e.line) + ": " + e.what()
                                     : std::string(e.what());
        if (error) *error = msg;
        Log::Error("OkayScript: ", msg);
        m_impl->loaded = false;
        return false;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        Log::Error("OkayScript: ", e.what());
        m_impl->loaded = false;
        return false;
    }
}

std::vector<std::string> OkayScriptVM::BuiltinNames() const {
    std::vector<std::string> out;
    out.reserve(m_impl->rt.builtins.size());
    for (const auto& kv : m_impl->rt.builtins) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

bool OkayScriptVM::Validate(const std::string& source, std::string* error) {
    // Lex + parse ONLY — never Exec — so live editor diagnostics have no side
    // effects (no logging, no globals mutated, no scene calls). Catches syntax and
    // parse errors; runtime/semantic errors still surface on a real Load/Run.
    try {
        Lexer lex(source);
        Parser parser(lex.Scan());
        std::unordered_map<std::string, FunctionDecl> throwaway;
        parser.ParseProgram(throwaway);
        return true;
    } catch (const ScriptError& e) {
        if (error) *error = e.line > 0 ? "line " + std::to_string(e.line) + ": " + e.what()
                                       : std::string(e.what());
        return false;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

std::vector<ScriptDiagnostic> OkayScriptVM::ValidateAll(const std::string& source) {
    // Like Validate, but recovers after each parse error to report ALL of them for
    // the editor's Problems panel. Lexer errors (unterminated string, bad char) can't
    // be recovered from, so those surface as a single diagnostic.
    std::vector<ScriptDiagnostic> diags;
    try {
        Lexer lex(source);
        Parser parser(lex.Scan());
        diags = parser.ParseProgramRecovering();
    } catch (const ScriptError& e) {
        diags.push_back({ e.line, e.what() });
    } catch (const std::exception& e) {
        diags.push_back({ 0, e.what() });
    }
    return diags;
}

void OkayScriptVM::Bind(ScriptHost* host) { m_impl->rt.host = host; }

namespace {
// Call the first of `names` that the script defines (with the given args).
// Lets a script use Unity's PascalCase (Start/Update) or the classic
// lowercase (start/update) names interchangeably.
void CallFirst(Runtime& rt, std::initializer_list<const char*> names,
               std::vector<Value>& args, const char* label) {
    for (const char* n : names) {
        if (rt.functions.count(n)) {
            try { rt.Call(n, args); }
            catch (const std::exception& e) { Log::Error("OkayScript ", label, "(): ", e.what()); }
            return;
        }
    }
}
} // namespace

void OkayScriptVM::CallStart() {
    if (!m_impl->loaded) return;
    std::vector<Value> none;
    CallFirst(m_impl->rt, {"Awake", "awake"}, none, "Awake");   // Unity runs Awake then Start
    CallFirst(m_impl->rt, {"Start", "start"}, none, "Start");
}

void OkayScriptVM::CallUpdate(float deltaTime) {
    if (!m_impl->loaded) return;
    if (m_impl->rt.host) m_impl->rt.host->deltaTime = deltaTime;
    std::vector<Value> args{Value{deltaTime}};
    CallFirst(m_impl->rt, {"Update", "update"}, args, "Update");
    CallFirst(m_impl->rt, {"LateUpdate", "late_update"}, args, "LateUpdate");
    // Scheduled after()/every() callbacks tick even without an update().
    if (!m_impl->rt.timers.empty()) {
        try { m_impl->rt.TickTimers(deltaTime); }
        catch (const std::exception& e) { Log::Error("OkayScript timer: ", e.what()); }
    }
}

void OkayScriptVM::CallEvent(const std::string& function) {
    if (!m_impl->loaded) return;
    // The requested (engine) name wins; otherwise accept a Unity-style alias so
    // scripts can name handlers the Unity way (OnCollisionEnter, OnClick, ...).
    static const std::unordered_map<std::string, std::vector<std::string>> kAliases = {
        // Physics — distinct enter/stay/exit. The legacy on_trigger/on_collision
        // are accepted as enter handlers so older scripts keep working.
        {"on_trigger_enter",   {"OnTriggerEnter", "OnTriggerEnter2D", "on_trigger"}},
        {"on_trigger_stay",    {"OnTriggerStay",  "OnTriggerStay2D"}},
        {"on_trigger_exit",    {"OnTriggerExit",  "OnTriggerExit2D"}},
        {"on_collision_enter", {"OnCollisionEnter", "OnCollisionEnter2D", "on_collision"}},
        {"on_collision_stay",  {"OnCollisionStay", "OnCollisionStay2D"}},
        {"on_collision_exit",  {"OnCollisionExit", "OnCollisionExit2D"}},
        {"on_collision", {"OnCollisionEnter", "OnCollisionEnter2D"}},
        {"on_trigger",   {"OnTriggerEnter",   "OnTriggerEnter2D"}},
        // Pointer / mouse.
        {"on_mouse_enter", {"OnMouseEnter", "OnPointerEnter"}},
        {"on_mouse_exit",  {"OnMouseExit",  "OnPointerExit"}},
        {"on_mouse_over",  {"OnMouseOver"}},
        {"on_mouse_down",  {"OnMouseDown"}},
        {"on_mouse_up",    {"OnMouseUp", "OnMouseUpAsButton"}},
        {"on_click",     {"OnClick", "OnMouseClick", "OnMouseUpAsButton", "OnPointerClick"}},
        {"on_change",    {"OnValueChanged"}},
        {"on_toggle",    {"OnToggle", "OnValueChanged"}},
        {"on_drop",      {"OnDrop"}},
        {"on_receive",   {"OnReceive"}},
        {"on_drag",      {"OnDrag"}},
        {"on_drag_start",{"OnBeginDrag"}},
    };
    std::string fn = function;
    if (!m_impl->rt.functions.count(fn)) {
        auto it = kAliases.find(function);
        if (it != kAliases.end())
            for (const auto& alt : it->second)
                if (m_impl->rt.functions.count(alt)) { fn = alt; break; }
    }
    if (m_impl->rt.functions.count(fn)) {
        std::vector<Value> none;
        try { m_impl->rt.Call(fn, none); }
        catch (const std::exception& e) { Log::Error("OkayScript ", fn.c_str(), "(): ", e.what()); }
    }
}

vs::VsValue OkayScriptVM::GetGlobal(const std::string& name) const {
    auto& g = m_impl->rt.Global().vars;
    auto it = g.find(name);
    return it != g.end() ? it->second : vs::VsValue{};
}

void OkayScriptVM::SetGlobal(const std::string& name, const vs::VsValue& v) {
    m_impl->rt.Global().vars[name] = v;
}

} // namespace okay
