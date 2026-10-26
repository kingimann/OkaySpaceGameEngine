#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Components/UIAnimation.hpp"
#include "okay/Components/TweenAnimation.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Input/Cursor.hpp"
#include "okay/Scripting/ScriptUI.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Layers.hpp"
#include "okay/Physics/Collider3D.hpp"
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
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/AnimControl.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/BlendShapes.hpp"
#include "okay/Components/LimbIK.hpp"
#include "okay/Components/ChainIK.hpp"
#include "okay/Components/LookAtIK.hpp"
#include "okay/Components/AimIK.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Components/ProceduralObjectHolding.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/SpriteAnimator.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/SessionTicket.hpp"
#include "okay/Components/NetValidator.hpp"
#include "okay/Platform/GameIpc.hpp"
#include "okay/Net/ServerBrowser.hpp"
#include "okay/Net/MatchResults.hpp"
#include "okay/Net/Matchmaker.hpp"
#include "okay/Components/NetworkSync.hpp"
#include "okay/Net/Matchmaking.hpp"
#include "okay/Platform/Steam/Steam.hpp"
#include "okay/Platform/PlayFab/PlayFab.hpp"
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
#include "okay/Components/UITabs.hpp"
#include "okay/Components/Tilemap.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/Spawner.hpp"
#include "okay/Components/BuildMode.hpp"
#include "okay/Components/LogicLinks.hpp"
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
#include <condition_variable>
#include <mutex>
#include <thread>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>


// Script-supplied file names must stay inside the game's own folders: no
// absolute paths, no "..". Refusals are logged so the game's author sees why.
namespace okay {
static bool ScriptPathOk(const std::string& name, const char* api) {
    if (Save::IsSafeRelative(name)) return true;
    OKAY_ERROR("[script] ", api, ": refusing file '", name,
               "' (use a name or a relative path; absolute paths and '..' aren't allowed)");
    return false;
}
// A file that used to live beside the game (before saves moved to the save
// folder): read it from there if the new place has nothing yet.
static std::string ScriptReadPath(const std::string& name) {
    std::string p = Save::Resolve(name);
    if (p != name && !std::ifstream(p).good() && std::ifstream(name).good()) return name;
    return p;
}
} // namespace okay

namespace okay {
namespace {

using Value = vs::VsValue;

// Friendly key name -> Input char code (shared with the visual-script nodes).
static char KeyCharFromName(const std::string& s) { return Input::KeyFromName(s); }

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
    int depth = 0;   // array/object nesting: too deep is an error, not a stack overflow
    explicit JsonReader(const std::string& src) : s(src) {}
    void Ws() { while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) ++i; }
    Value Parse() { Ws(); if (i >= s.size()) return Value{}; char c = s[i];
        if (c == '{' || c == '[') {
            if (++depth > 256) throw std::runtime_error("from_json: nested too deeply (more than 256 levels)");
            Value v = c == '{' ? Obj() : Arr();
            --depth;
            return v;
        }
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
    LParen, RParen, LBrace, RBrace, LBracket, RBracket, Comma, Semicolon,
    Dot,  // a '.' after ')' or ']' — member access on an expression: (a + b).magnitude
    PercentEq, BitAnd, BitOr, BitXor, Arrow   // Arrow: '=>' (lambdas)
};

struct Token {
    Tok type;
    std::string text;
    double number = 0.0;
    int line = 0;
    bool suffix = false;   ///< a number written with a C# suffix (5f, 2d)
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
            else if (c == '\'')
                out.push_back(CharLiteral());
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
            else if (c == '/' && Peek(1) == '*') {           // C#-style /* block comment */
                Advance(); Advance();
                while (!AtEnd() && !(Peek() == '*' && Peek(1) == '/')) Advance();
                if (!AtEnd()) { Advance(); Advance(); }
            }
            else if (c == '#') { while (!AtEnd() && Peek() != '\n') Advance(); }
            else break;
        }
    }

    Token Number() {
        std::string s;
        while (std::isdigit((unsigned char)Peek()) || Peek() == '.') s += Advance();
        // Accept C# numeric suffixes (5f, 1.0f, 2d) so pasted code runs; every
        // number is already a decimal, so the editor flags them as unnecessary.
        bool suffix = false;
        if (Peek() == 'f' || Peek() == 'F' || Peek() == 'd' || Peek() == 'D') { Advance(); suffix = true; }
        Token t{Tok::Number, s, std::stod(s), m_line};
        t.suffix = suffix;
        return t;
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

    // C# char literal 'x' (or '\n') — a one-character string.
    Token CharLiteral() {
        Advance();   // opening quote
        std::string s;
        while (!AtEnd() && Peek() != '\'' && Peek() != '\n') {
            char c = Advance();
            if (c == '\\' && !AtEnd()) {
                char e = Advance();
                switch (e) { case 'n': c = '\n'; break; case 't': c = '\t'; break; case '0': c = '\0'; break; default: c = e; }
            }
            s += c;
        }
        if (AtEnd() || Peek() != '\'') throw ScriptError("unterminated character literal", m_line);
        Advance();   // closing quote
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
        // dotted names are a single token: Input.GetKeyDown,
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
        // C# `this.hp` is just `hp` (a script's fields are its globals).
        if (s.rfind("this.", 0) == 0 && s.size() > 5) s = s.substr(5);
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
            case '%': { auto t = two('=', Tok::PercentEq); return t.type != Tok::End ? t : Token{Tok::Percent, "", 0, m_line}; }
            case '^': return {Tok::BitXor, "", 0, m_line};
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
            case '.': return {Tok::Dot, "", 0, m_line};
            case '=': { if (Peek() == '>') { Advance(); return {Tok::Arrow, "", 0, m_line}; }
                        auto t = two('=', Tok::Eq); return t.type != Tok::End ? t : Token{Tok::Assign, "", 0, m_line}; }
            case '!': { auto t = two('=', Tok::Ne); return t.type != Tok::End ? t : Token{Tok::Not, "", 0, m_line}; }
            case '<': { auto t = two('=', Tok::Le); return t.type != Tok::End ? t : Token{Tok::Lt, "", 0, m_line}; }
            case '>': { auto t = two('=', Tok::Ge); return t.type != Tok::End ? t : Token{Tok::Gt, "", 0, m_line}; }
            case '&': if (Peek() == '&') { Advance(); return {Tok::And, "", 0, m_line}; } return {Tok::BitAnd, "", 0, m_line};
            case '|': if (Peek() == '|') { Advance(); return {Tok::Or, "", 0, m_line}; } return {Tok::BitOr, "", 0, m_line};
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
// What a declared variable may hold. `int x`, `float x`, `bool x` and `string x`
// are real types: assignments convert (an int drops its fraction) or fail with a
// clear error instead of silently storing the wrong kind of value. `const` /
// `readonly` variables can't change after they're set. `var` and other types
// (Vector3, List<int>, ...) hold anything.
enum VarKind : unsigned char { KAny = 0, KInt = 1, KFloat = 2, KBool = 3, KString = 4, KConst = 0x10, KStatic = 0x20 };

inline unsigned char TypeKind(const std::string& t) {
    static const std::unordered_map<std::string, unsigned char> k = {
        {"int", KInt}, {"long", KInt}, {"short", KInt}, {"byte", KInt}, {"uint", KInt},
        {"ulong", KInt}, {"ushort", KInt}, {"sbyte", KInt},
        {"float", KFloat}, {"double", KFloat}, {"decimal", KFloat},
        {"bool", KBool}, {"string", KString}};
    auto it = k.find(t);
    return it == k.end() ? KAny : it->second;
}
inline const char* KindName(unsigned char k) {
    switch (k & 0x0F) { case KInt: return "an int"; case KFloat: return "a float"; case KBool: return "a bool";
                        case KString: return "a string"; default: return "a variable"; }
}
inline const char* ValueKindName(const Value& v) {
    if (v.IsString()) return "text";
    if (v.IsBool()) return "true/false";
    if (v.IsNumber()) return "a number";
    if (v.IsVec3()) return "a vector";
    if (v.IsArray()) return "a list";
    if (v.IsMap()) return "a map";
    return "that value";
}
/// Convert `v` for a variable of `kind`, or throw a readable error.
inline Value CoerceToKind(unsigned char kind, const Value& v, const std::string& name) {
    const unsigned char t = kind & 0x0F;
    auto fail = [&](const char* hint) -> Value {
        throw ScriptError("'" + name + "' is " + KindName(kind) + ", so it can't hold " + ValueKindName(v) + hint, 0);
    };
    switch (t) {
        case KInt:
            if (v.IsNumber()) return Value{(float)(long long)v.AsFloat()};   // drops the fraction, like (int)
            if (v.IsBool()) return Value{v.AsBool() ? 1.0f : 0.0f};
            return fail(v.IsString() ? " (use int.Parse(text))" : "");
        case KFloat:
            if (v.IsNumber()) return v;
            if (v.IsBool()) return Value{v.AsBool() ? 1.0f : 0.0f};
            return fail(v.IsString() ? " (use float.Parse(text))" : "");
        case KBool:
            if (v.IsBool()) return v;
            if (v.IsNumber()) return Value{v.AsFloat() != 0.0f};
            return fail("");
        case KString:
            if (v.IsString()) return v;
            return fail(" (turn it into text with $\"{value}\" or .ToString())");
        default: return v;
    }
}
inline Value DefaultForKind(unsigned char kind) {
    switch (kind & 0x0F) { case KBool: return Value{false}; case KString: return Value{std::string()};
                           default: return Value{}; }
}

struct Environment {
    std::unordered_map<std::string, Value> vars;
    std::unordered_map<std::string, unsigned char> kinds;   ///< typed / const variables only
};

// `static` variables: one copy shared by every object running the same script
// (keyed by its source), so e.g. `static int alive;` counts all the enemies.
inline std::shared_ptr<Environment> SharedStatics(const std::string& source) {
    static std::unordered_map<std::size_t, std::shared_ptr<Environment>> all;
    auto& e = all[std::hash<std::string>{}(source)];
    if (!e) e = std::make_shared<Environment>();
    return e;
}

struct FunctionDecl {
    std::vector<std::string> params;
    std::vector<unsigned char> paramKinds;   ///< `int dmg` converts / checks its argument
    std::vector<StmtPtr> body;
    unsigned char returnKind = 0;            ///< `int Score()` converts what it returns
};

struct Runtime {
    std::vector<Environment> scopes{1};                 // scopes[0] = global
    std::shared_ptr<Environment> statics;               // `static` variables shared by this script's objects
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
    std::vector<LastHit> lastHitAll;    // raycast_all / raycast_all3, nearest first

    // Scheduled callbacks (after / every). interval == 0 means fire once.
    struct Timer { float remaining; float interval; std::string fn; bool dead; };
    std::vector<Timer> timers;

    // Destroy(obj, seconds): {seconds left, object name ("" = this object)}.
    std::vector<std::pair<float, std::string>> pendingDestroy;

    // ---- C#-style coroutines -------------------------------------------------
    // StartCoroutine(Routine()) runs Routine on its own thread, but only one side
    // runs at a time: each `yield return` hands control back to the game loop,
    // which resumes the routine when its wait is over (next frame,
    // WaitForSeconds, or another coroutine finishing). The routine's local scope
    // frames are parked while it waits so the shared scope stack stays balanced.
    struct CoroutineAbort {};
    struct Coroutine {
        int id = 0;
        std::string fn; std::vector<Value> args;
        std::thread th;
        std::mutex m; std::condition_variable cv;
        bool coTurn = false, started = false, done = false, abort = false;
        float wait = 0.0f; bool nextFrame = false; int waitFor = 0;   // waitFor: a coroutine id
        long long yieldFrame = -1;
        std::vector<Environment> frames;
        std::size_t baseDepth = 0;
    };
    std::vector<std::unique_ptr<Coroutine>> coroutines;
    Coroutine* currentCo = nullptr;
    int nextCoId = 1;
    long long frame = 0;

    Coroutine* FindCo(int id) { for (auto& c : coroutines) if (c->id == id) return c.get(); return nullptr; }
    void Resume(Coroutine* co) {
        if (co->done) return;
        Coroutine* prev = currentCo; currentCo = co;
        co->baseDepth = scopes.size();
        for (auto& f : co->frames) scopes.push_back(std::move(f));
        co->frames.clear();
        if (!co->started) {
            co->started = true;
            co->th = std::thread([this, co] {
                { std::unique_lock<std::mutex> lk(co->m); co->cv.wait(lk, [co] { return co->coTurn; }); }
                try { if (!co->abort) Call(co->fn, co->args); }
                catch (const CoroutineAbort&) {}
                catch (const std::exception& e) { Log::Error("OkayScript coroutine ", co->fn, "(): ", e.what()); }
                catch (...) { Log::Error("OkayScript coroutine ", co->fn, "(): error"); }
                std::lock_guard<std::mutex> lk(co->m);
                co->done = true; co->coTurn = false; co->cv.notify_all();
            });
        }
        {
            std::unique_lock<std::mutex> lk(co->m);
            co->coTurn = true; co->cv.notify_all();
            co->cv.wait(lk, [co] { return !co->coTurn; });
        }
        if (co->done && scopes.size() > co->baseDepth) scopes.resize(co->baseDepth);   // unwound by an error
        currentCo = prev;
    }
    // Called on the coroutine's own thread by `yield return`.
    void YieldCoroutine(float wait, bool nextFrame, int waitFor) {
        Coroutine* co = currentCo;
        if (!co) throw ScriptError("'yield' only works inside a coroutine started with StartCoroutine()");
        co->wait = wait; co->nextFrame = nextFrame; co->waitFor = waitFor; co->yieldFrame = frame;
        for (std::size_t i = co->baseDepth; i < scopes.size(); ++i) co->frames.push_back(std::move(scopes[i]));
        scopes.resize(co->baseDepth);
        std::unique_lock<std::mutex> lk(co->m);
        co->coTurn = false; co->cv.notify_all();
        co->cv.wait(lk, [co] { return co->coTurn; });
        if (co->abort) throw CoroutineAbort{};
    }
    int StartCoroutine(const std::string& fn, std::vector<Value> args) {
        if (!functions.count(fn)) throw ScriptError("StartCoroutine: no function named '" + fn + "'");
        auto co = std::make_unique<Coroutine>();
        co->id = nextCoId++; co->fn = fn; co->args = std::move(args);
        Coroutine* raw = co.get();
        coroutines.push_back(std::move(co));
        Resume(raw);   // runs until its first yield
        return raw->id;
    }
    void StopCoroutine(Coroutine* co) {
        if (co->done) return;
        if (!co->started) { co->done = true; return; }
        co->abort = true;
        Resume(co);
    }
    void StopCoroutines(const std::string& fn) {   // "" = all
        for (std::size_t i = 0; i < coroutines.size(); ++i)
            if (fn.empty() || coroutines[i]->fn == fn) StopCoroutine(coroutines[i].get());
        ReapCoroutines();
    }
    void ReapCoroutines() {
        if (currentCo) return;   // never join from inside a coroutine
        for (auto it = coroutines.begin(); it != coroutines.end();) {
            if ((*it)->done) { if ((*it)->th.joinable()) (*it)->th.join(); it = coroutines.erase(it); }
            else ++it;
        }
    }
    void TickCoroutines(float dt) {
        for (std::size_t i = 0; i < coroutines.size(); ++i) {   // index loop: the list can grow
            Coroutine* co = coroutines[i].get();
            if (co->done || co->yieldFrame >= frame) continue;   // yielded this frame: wait for the next
            if (co->waitFor) { Coroutine* w = FindCo(co->waitFor); if (w && !w->done) continue; co->waitFor = 0; Resume(co); continue; }
            if (co->nextFrame) { Resume(co); continue; }
            co->wait -= dt;
            if (co->wait <= 0.0f) Resume(co);
        }
        ReapCoroutines();
    }

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

    void Define(const std::string& n, const Value& v) {
        Environment& e = scopes.back();
        e.vars[n] = v;
        if (!e.kinds.empty()) e.kinds.erase(n);
    }
    /// A typed / const declaration: converts the value and remembers the type.
    void DefineTyped(const std::string& n, const Value& v, unsigned char kind) {
        Environment& e = scopes.back();
        e.vars[n] = (kind & 0x0F) ? CoerceToKind(kind, v, n) : v;
        if (kind) e.kinds[n] = kind; else if (!e.kinds.empty()) e.kinds.erase(n);
    }
    /// A `static` declaration: only the first object to run it sets the value;
    /// everyone else shares it from then on.
    void DefineStatic(const std::string& n, const Value& v, unsigned char kind) {
        if (!statics) statics = std::make_shared<Environment>();
        if (statics->vars.count(n)) return;
        statics->vars[n] = (kind & 0x0F) ? CoerceToKind(kind, v, n) : v;
        statics->kinds[n] = kind;
    }
    static bool AssignIn(Environment& e, const std::string& n, const Value& v) {
        auto f = e.vars.find(n);
        if (f == e.vars.end()) return false;
        if (!e.kinds.empty()) {
            auto k = e.kinds.find(n);
            if (k != e.kinds.end()) {
                if (k->second & KConst) throw ScriptError("'" + n + "' is const, so it can't change after it's set", 0);
                f->second = (k->second & 0x0F) ? CoerceToKind(k->second, v, n) : v;
                return true;
            }
        }
        f->second = v;
        return true;
    }
    void Assign(const std::string& n, const Value& v) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) if (AssignIn(*it, n, v)) return;
        if (statics && AssignIn(*statics, n, v)) return;
        Global().vars[n] = v; // create global on first assignment
    }
    /// Set a global from outside (the Inspector): converted to its declared type
    /// when it has one; a value that can't convert is ignored.
    void SetGlobalChecked(const std::string& n, const Value& v) {
        Environment& g = Global();
        auto k = g.kinds.find(n);
        if (k == g.kinds.end()) { g.vars[n] = v; return; }
        try { g.vars[n] = CoerceToKind(k->second, v, n); } catch (const ScriptError&) {}
    }
    Value Get(const std::string& n) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto f = it->vars.find(n);
            if (f != it->vars.end()) return f->second;
        }
        if (statics) { auto f = statics->vars.find(n); if (f != statics->vars.end()) return f->second; }
        return Value{}; // undefined reads as 0/false/empty
    }
    bool Has(const std::string& n) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            if (it->vars.count(n)) return true;
        return statics && statics->vars.count(n);
    }

    Value Call(const std::string& name, std::vector<Value>& args);

    // C# lambdas (x => x * 2, (a, b) => { ... }): a lambda value is {"__lambda": id};
    // calling it runs its body in a new frame on top of the current scopes, so it
    // sees the locals of the code that called it (fine for ForEach/Find/Where...).
    struct LambdaBody { const std::vector<std::string>* params; const std::vector<StmtPtr>* body; };
    std::unordered_map<int, LambdaBody> lambdas;
    static bool IsLambda(const Value& v) { auto m = v.AsMap(); return m && m->count("__lambda"); }
    Value CallLambda(const Value& f, std::vector<Value>& args);

    // dotted property read/write (transform.position.x, Time.timeScale,
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
        // property reads: transform.position.x, Time.deltaTime, ...
        if (n.find('.') != std::string::npos) {
            bool ok = false; Value v = r.GetDotted(n, ok);
            if (ok) return v;
        }
        // Bare `gameObject` (as in Destroy(gameObject)) is this object's name.
        if (n == "gameObject" && !r.Has(n) && r.host && r.host->gameObject) return Value{r.host->gameObject->name};
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
        if (op == Tok::Minus) return v.IsVec3() ? Value{v.AsVec3() * -1.0f} : Value{-v.AsFloat()};
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
        // Vector3 math: v + w, v - w, v * 2, 2 * v, v / 2,
        // v * w (component-wise, like Vector3.Scale), v == w.
        if ((a.IsVec3() || b.IsVec3()) && !a.IsString() && !b.IsString()) {
            const Vec3 x = a.AsVec3(), y = b.AsVec3();
            const bool sa = !a.IsVec3(), sb = !b.IsVec3();   // which side is a plain number
            switch (op) {
                case Tok::Plus:  return sa ? Vec3{a.AsFloat() + y.x, a.AsFloat() + y.y, a.AsFloat() + y.z}
                                      : sb ? Vec3{x.x + b.AsFloat(), x.y + b.AsFloat(), x.z + b.AsFloat()} : x + y;
                case Tok::Minus: return sb ? Vec3{x.x - b.AsFloat(), x.y - b.AsFloat(), x.z - b.AsFloat()}
                                      : sa ? Vec3{a.AsFloat() - y.x, a.AsFloat() - y.y, a.AsFloat() - y.z} : x - y;
                case Tok::Star:  return sa ? y * a.AsFloat() : sb ? x * b.AsFloat() : Vec3{x.x * y.x, x.y * y.y, x.z * y.z};
                case Tok::Slash: {
                    if (sb) { float d = b.AsFloat(); return d != 0 ? x * (1.0f / d) : Vec3::Zero; }
                    auto dv = [](float n, float d) { return d != 0 ? n / d : 0.0f; };
                    return sa ? Vec3{dv(a.AsFloat(), y.x), dv(a.AsFloat(), y.y), dv(a.AsFloat(), y.z)}
                              : Vec3{dv(x.x, y.x), dv(x.y, y.y), dv(x.z, y.z)};
                }
                case Tok::Eq: return !sa && !sb && x.x == y.x && x.y == y.y && x.z == y.z;
                case Tok::Ne: return !(!sa && !sb && x.x == y.x && x.y == y.y && x.z == y.z);
                default: break;   // comparisons fall through to magnitudes below
            }
        }
        switch (op) {
            case Tok::Plus:
                if (a.IsString() || b.IsString()) return a.AsString() + b.AsString();
                return a.AsFloat() + b.AsFloat();
            case Tok::Minus:   return a.AsFloat() - b.AsFloat();
            case Tok::Star:    return a.AsFloat() * b.AsFloat();
            case Tok::Slash:   { float d = b.AsFloat(); return d != 0 ? a.AsFloat() / d : 0.0f; }
            case Tok::Percent: { float d = b.AsFloat(); return d != 0 ? std::fmod(a.AsFloat(), d) : 0.0f; }
            case Tok::BitAnd: return (float)((long long)a.AsFloat() & (long long)b.AsFloat());
            case Tok::BitOr:  return (float)((long long)a.AsFloat() | (long long)b.AsFloat());
            case Tok::BitXor: return (float)((long long)a.AsFloat() ^ (long long)b.AsFloat());
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

// ---- C#-style members and methods on values ----------------------------------
// s.Length, list.Count, dict.Keys, v.magnitude ... — false when `prop` isn't one.
bool ValueProperty(const Value& v, const std::string& prop, Value& out) {
    if (v.IsString()) {
        if (prop == "Length" || prop == "length") { out = Value{(float)v.AsString().size()}; return true; }
        return false;
    }
    if (v.IsArray()) {
        if (prop == "Length" || prop == "length" || prop == "Count" || prop == "count") {
            auto a = v.AsArray(); out = Value{a ? (float)a->size() : 0.0f}; return true;
        }
        return false;
    }
    if (v.IsMap()) {
        auto m = v.AsMap();
        if (m) if (auto it = m->find(prop); it != m->end()) { out = it->second; return true; }   // kv.Key / kv.Value
        if (prop == "Count" || prop == "count") { out = Value{m ? (float)m->size() : 0.0f}; return true; }
        if (prop == "Keys" || prop == "Values") {
            out = Value::MakeArray();
            if (m) for (auto& kv : *m) out.AsArray()->push_back(prop == "Keys" ? Value{kv.first} : kv.second);
            return true;
        }
        return false;
    }
    const Vec3 p = v.AsVec3();
    if (prop == "x") { out = Value{p.x}; return true; }
    if (prop == "y") { out = Value{p.y}; return true; }
    if (prop == "z") { out = Value{p.z}; return true; }
    if (prop == "magnitude")    { out = Value{p.Magnitude()}; return true; }
    if (prop == "sqrMagnitude") { out = Value{p.x * p.x + p.y * p.y + p.z * p.z}; return true; }
    if (prop == "normalized") { float m = p.Magnitude(); out = Value{m > 1e-6f ? p * (1.0f / m) : Vec3::Zero}; return true; }
    return false;
}
bool ValuesEqual(const Value& a, const Value& b) {
    if (a.IsString() || b.IsString()) return a.AsString() == b.AsString();
    return a.AsFloat() == b.AsFloat();
}
// s.ToUpper(), xs.Add(3), d.ContainsKey("a"), n.ToString("F2") ... Arrays and
// maps are shared references, so Add/Remove/Clear change the variable's value.
using LambdaInvoker = std::function<Value(const Value&, std::vector<Value>&)>;
bool CallValueMethod(const Value& v, const std::string& m, std::vector<Value>& a, Value& out,
                     const LambdaInvoker* invoke = nullptr) {
    auto arg = [&](std::size_t i) { return i < a.size() ? a[i] : Value{}; };
    out = Value{};
    auto isFn = [&](std::size_t i) { return invoke && i < a.size() && Runtime::IsLambda(a[i]); };
    auto call1 = [&](const Value& x) { std::vector<Value> xs{x}; return (*invoke)(a[0], xs); };
    if (m == "Invoke" && invoke && Runtime::IsLambda(v)) { out = (*invoke)(v, a); return true; }
    if (m == "ToString") {
        if (v.IsNumber() && !a.empty()) {          // "F2" / "N0" / "0.00"
            std::string f = a[0].AsString(); int dec = -1;
            if (!f.empty() && (f[0] == 'F' || f[0] == 'f' || f[0] == 'N' || f[0] == 'n'))
                dec = f.size() > 1 ? std::atoi(f.c_str() + 1) : 2;
            else if (auto d = f.find('.'); d != std::string::npos) dec = (int)(f.size() - d - 1);
            else if (!f.empty() && f.find_first_not_of('0') == std::string::npos) dec = 0;
            if (dec >= 0) { char buf[64]; std::snprintf(buf, sizeof buf, "%.*f", dec > 9 ? 9 : dec, v.AsFloat()); out = Value{std::string(buf)}; return true; }
        }
        out = Value{v.AsString()}; return true;
    }
    if (m == "Equals")    { out = Value{ValuesEqual(v, arg(0))}; return true; }
    if (m == "CompareTo") { float x = v.AsFloat(), y = arg(0).AsFloat(); out = Value{x < y ? -1.0f : (x > y ? 1.0f : 0.0f)}; return true; }
    if (v.IsString()) {
        const std::string str = v.AsString();
        auto up = [](std::string x, bool u) { for (char& c : x) c = (char)(u ? std::toupper((unsigned char)c) : std::tolower((unsigned char)c)); return x; };
        auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
        if (m == "ToUpper" || m == "ToUpperInvariant") { out = Value{up(str, true)}; return true; }
        if (m == "ToLower" || m == "ToLowerInvariant") { out = Value{up(str, false)}; return true; }
        if (m == "Contains")   { out = Value{str.find(arg(0).AsString()) != std::string::npos}; return true; }
        if (m == "StartsWith") { out = Value{str.rfind(arg(0).AsString(), 0) == 0}; return true; }
        if (m == "EndsWith")   { std::string e = arg(0).AsString(); out = Value{str.size() >= e.size() && str.compare(str.size() - e.size(), e.size(), e) == 0}; return true; }
        if (m == "IndexOf")    { auto p = str.find(arg(0).AsString()); out = Value{p == std::string::npos ? -1.0f : (float)p}; return true; }
        if (m == "LastIndexOf"){ auto p = str.rfind(arg(0).AsString()); out = Value{p == std::string::npos ? -1.0f : (float)p}; return true; }
        if (m == "Substring") {
            int st = (int)arg(0).AsFloat(); if (st < 0) st = 0; if (st > (int)str.size()) st = (int)str.size();
            int n = a.size() > 1 ? (int)a[1].AsFloat() : (int)str.size() - st; if (n < 0) n = 0;
            out = Value{str.substr((std::size_t)st, (std::size_t)n)}; return true;
        }
        if (m == "Replace") {
            std::string from = arg(0).AsString(), to = arg(1).AsString(), r = str;
            if (!from.empty()) for (std::size_t p = 0; (p = r.find(from, p)) != std::string::npos; p += to.size()) r.replace(p, from.size(), to);
            out = Value{r}; return true;
        }
        if (m == "Trim" || m == "TrimStart" || m == "TrimEnd") {
            std::size_t b = 0, e = str.size();
            if (m != "TrimEnd")   while (b < e && ws(str[b])) ++b;
            if (m != "TrimStart") while (e > b && ws(str[e - 1])) --e;
            out = Value{str.substr(b, e - b)}; return true;
        }
        if (m == "Split") {
            std::string sep = a.empty() ? " " : a[0].AsString();
            out = Value::MakeArray(); auto arr = out.AsArray();
            if (sep.empty()) { arr->push_back(Value{str}); return true; }
            std::size_t p = 0, q;
            while ((q = str.find(sep, p)) != std::string::npos) { arr->push_back(Value{str.substr(p, q - p)}); p = q + sep.size(); }
            arr->push_back(Value{str.substr(p)}); return true;
        }
        if (m == "PadLeft" || m == "PadRight") {
            int w = (int)arg(0).AsFloat(); char c = a.size() > 1 && !a[1].AsString().empty() ? a[1].AsString()[0] : ' ';
            std::string r = str;
            if ((int)r.size() < w) r = m == "PadLeft" ? std::string(w - r.size(), c) + r : r + std::string(w - r.size(), c);
            out = Value{r}; return true;
        }
        if (m == "Insert") { std::size_t p = (std::size_t)std::max(0.0f, arg(0).AsFloat()); std::string r = str; r.insert(std::min(p, r.size()), arg(1).AsString()); out = Value{r}; return true; }
        if (m == "Remove") {
            std::size_t p = (std::size_t)std::max(0.0f, arg(0).AsFloat()); std::string r = str;
            if (p < r.size()) r.erase(p, a.size() > 1 ? (std::size_t)std::max(0.0f, a[1].AsFloat()) : std::string::npos);
            out = Value{r}; return true;
        }
        return false;
    }
    if (v.IsArray()) {
        auto arr = v.AsArray(); if (!arr) return false;
        auto find = [&](const Value& x) { for (std::size_t i = 0; i < arr->size(); ++i) if (ValuesEqual((*arr)[i], x)) return (int)i; return -1; };
        // LINQ-style helpers that take a lambda: xs.Where(x => x > 2).Select(x => x * 10).ToList()
        if (isFn(0)) {
            const auto items = *arr;   // snapshot: the lambda may change the list
            if (m == "ForEach") { for (auto& x : items) call1(x); return true; }
            if (m == "Where" || m == "FindAll") { out = Value::MakeArray(); for (auto& x : items) if (call1(x).AsBool()) out.AsArray()->push_back(x); return true; }
            if (m == "Select") { out = Value::MakeArray(); for (auto& x : items) out.AsArray()->push_back(call1(x)); return true; }
            if (m == "Find" || m == "First" || m == "FirstOrDefault") { for (auto& x : items) if (call1(x).AsBool()) { out = x; return true; } return true; }
            if (m == "FindIndex") { for (std::size_t i = 0; i < items.size(); ++i) if (call1(items[i]).AsBool()) { out = Value{(float)i}; return true; } out = Value{-1.0f}; return true; }
            if (m == "Any" || m == "Exists") { bool r = false; for (auto& x : items) if (call1(x).AsBool()) { r = true; break; } out = Value{r}; return true; }
            if (m == "All" || m == "TrueForAll") { bool r = true; for (auto& x : items) if (!call1(x).AsBool()) { r = false; break; } out = Value{r}; return true; }
            if (m == "Count") { float n = 0; for (auto& x : items) if (call1(x).AsBool()) n += 1; out = Value{n}; return true; }
            if (m == "RemoveAll") {
                float n = 0; arr->clear();
                for (auto& x : items) { if (call1(x).AsBool()) n += 1; else arr->push_back(x); }
                out = Value{n}; return true;
            }
            if (m == "Sum" || m == "Max" || m == "Min" || m == "Average") {
                std::vector<Value> mapped; for (auto& x : items) mapped.push_back(call1(x));
                Value tmp = Value::MakeArray(); *tmp.AsArray() = mapped;
                std::vector<Value> none;
                return CallValueMethod(tmp, m, none, out);
            }
            if (m == "OrderBy" || m == "OrderByDescending") {
                std::vector<std::pair<Value, Value>> keyed; for (auto& x : items) keyed.push_back({call1(x), x});
                const bool desc = m == "OrderByDescending";
                std::stable_sort(keyed.begin(), keyed.end(), [&](const auto& p, const auto& q) {
                    bool lt = (p.first.IsString() || q.first.IsString()) ? p.first.AsString() < q.first.AsString() : p.first.AsFloat() < q.first.AsFloat();
                    bool gt = (p.first.IsString() || q.first.IsString()) ? q.first.AsString() < p.first.AsString() : q.first.AsFloat() < p.first.AsFloat();
                    return desc ? gt : lt; });
                out = Value::MakeArray(); for (auto& kq : keyed) out.AsArray()->push_back(kq.second);
                return true;
            }
            if (m == "Sort") {   // xs.Sort((a, b) => a - b)
                std::vector<Value> sorted = items;
                std::stable_sort(sorted.begin(), sorted.end(), [&](const Value& x, const Value& y) {
                    std::vector<Value> xy{x, y}; return (*invoke)(a[0], xy).AsFloat() < 0.0f; });
                *arr = sorted; return true;
            }
        }
        if (m == "Add" || m == "Push" || m == "Enqueue") { arr->push_back(arg(0)); return true; }
        if (m == "AddRange") { if (auto o = arg(0).AsArray()) { auto copy = *o; arr->insert(arr->end(), copy.begin(), copy.end()); } return true; }
        if (m == "Insert")   { int i = (int)arg(0).AsFloat(); i = std::max(0, std::min(i, (int)arr->size())); arr->insert(arr->begin() + i, arg(1)); return true; }
        if (m == "Remove")   { int i = find(arg(0)); if (i >= 0) arr->erase(arr->begin() + i); out = Value{i >= 0}; return true; }
        if (m == "RemoveAt") { int i = (int)arg(0).AsFloat(); if (i >= 0 && i < (int)arr->size()) arr->erase(arr->begin() + i); return true; }
        if (m == "Contains") { out = Value{find(arg(0)) >= 0}; return true; }
        if (m == "IndexOf")  { out = Value{(float)find(arg(0))}; return true; }
        if (m == "Clear")    { arr->clear(); return true; }
        if (m == "Pop")      { if (!arr->empty()) { out = arr->back(); arr->pop_back(); } return true; }
        if (m == "Dequeue")  { if (!arr->empty()) { out = arr->front(); arr->erase(arr->begin()); } return true; }
        if (m == "First")    { if (!arr->empty()) out = arr->front(); return true; }
        if (m == "Last")     { if (!arr->empty()) out = arr->back(); return true; }
        if (m == "Count")    { out = Value{(float)arr->size()}; return true; }
        if (m == "Reverse")  { std::reverse(arr->begin(), arr->end()); return true; }
        if (m == "Sort") {
            bool nums = true; for (auto& x : *arr) nums = nums && !x.IsString();
            std::stable_sort(arr->begin(), arr->end(), [&](const Value& x, const Value& y) {
                return nums ? x.AsFloat() < y.AsFloat() : x.AsString() < y.AsString(); });
            return true;
        }
        if (m == "ToArray" || m == "ToList") { out = Value::MakeArray(); *out.AsArray() = *arr; return true; }
        if (m == "Sum" || m == "Average" || m == "Max" || m == "Min") {
            if (arr->empty()) { out = Value{0.0f}; return true; }
            float acc = m == "Max" ? -1e30f : (m == "Min" ? 1e30f : 0.0f);
            for (auto& x : *arr) { float f = x.AsFloat(); acc = m == "Max" ? std::max(acc, f) : (m == "Min" ? std::min(acc, f) : acc + f); }
            out = Value{m == "Average" ? acc / (float)arr->size() : acc}; return true;
        }
        return false;
    }
    if (v.IsMap()) {
        auto mp = v.AsMap(); if (!mp) return false;
        if (m == "Add" || m == "TryAdd") { auto k = arg(0).AsString(); bool fresh = !mp->count(k); if (fresh) (*mp)[k] = arg(1); out = Value{fresh}; return true; }
        if (m == "ContainsKey") { out = Value{mp->count(arg(0).AsString()) > 0}; return true; }
        if (m == "ContainsValue") { bool f = false; for (auto& kv : *mp) f = f || ValuesEqual(kv.second, arg(0)); out = Value{f}; return true; }
        if (m == "Remove") { out = Value{mp->erase(arg(0).AsString()) > 0}; return true; }
        if (m == "Clear")  { mp->clear(); return true; }
        if (m == "GetValueOrDefault") { auto it = mp->find(arg(0).AsString()); out = it != mp->end() ? it->second : arg(1); return true; }
        return false;
    }
    return false;
}

// A component reference returned by GetComponent<T>(): the string "@T".
inline bool IsComponentRef(const Value& v) {
    return v.IsString() && v.AsString().size() > 1 && v.AsString()[0] == '@';
}
// expr.member  — a property read on any expression's value.
struct MemberExpr : Expr {
    ExprPtr obj; std::string name;
    MemberExpr(ExprPtr o, std::string n) : obj(std::move(o)), name(std::move(n)) {}
    Value Eval(Runtime& rt) override {
        Value v = obj->Eval(rt), out;
        if (IsComponentRef(v)) { std::vector<Value> a{Value{v.AsString().substr(1)}, Value{name}}; return rt.Call("__get_component", a); }
        if (ValueProperty(v, name, out)) return out;
        throw ScriptError("no property '" + name + "' on this value");
    }
};
// expr.member = value  — GetComponent<SpriteRenderer>().color = Color.red.
struct MemberAssignExpr : Expr {
    ExprPtr obj; std::string name; ExprPtr value;
    MemberAssignExpr(ExprPtr o, std::string n, ExprPtr v) : obj(std::move(o)), name(std::move(n)), value(std::move(v)) {}
    Value Eval(Runtime& rt) override {
        Value target = obj->Eval(rt), v = value->Eval(rt);
        if (!IsComponentRef(target))
            throw ScriptError(target.IsNumber() && target.AsFloat() == 0.0f
                ? "can't set '" + name + "': the object is null (e.g. GetComponent found no such component)"
                : "can't assign '" + name + "' on this value");
        std::vector<Value> a{Value{target.AsString().substr(1)}, Value{name}, v};
        rt.Call("__set_component", a);
        return v;
    }
};
// (int)x / (float)x / (string)x.
struct CastExpr : Expr {
    std::string type; ExprPtr e;
    CastExpr(std::string t, ExprPtr x) : type(std::move(t)), e(std::move(x)) {}
    Value Eval(Runtime& rt) override {
        Value v = e->Eval(rt);
        if (type == "string") return Value{v.AsString()};
        if (type == "bool") return Value{v.AsBool()};
        if (type == "char") return v.IsString() ? v : Value{std::string(1, (char)(int)v.AsFloat())};
        if (type == "float" || type == "double" || type == "decimal") {
            if (v.IsString()) { try { return Value{std::stof(v.AsString())}; } catch (...) { return Value{0.0f}; } }
            return Value{v.AsFloat()};
        }
        float f = v.IsString() && v.AsString().size() == 1 ? (float)(unsigned char)v.AsString()[0] : v.AsFloat();
        return Value{(float)(long long)f};   // integer casts truncate toward zero
    }
};
// expr.Method(args)  — a C#-style method on a value (string, list, dictionary, number).
struct MethodCallExpr : Expr {
    ExprPtr obj; std::string name; std::vector<ExprPtr> args;
    MethodCallExpr(ExprPtr o, std::string n) : obj(std::move(o)), name(std::move(n)) {}
    Value Eval(Runtime& rt) override {
        Value v = obj->Eval(rt), out;
        std::vector<Value> vals; vals.reserve(args.size());
        for (auto& a : args) vals.push_back(a->Eval(rt));
        if (IsComponentRef(v)) {
            vals.insert(vals.begin(), {Value{v.AsString().substr(1)}, Value{name}});
            return rt.Call("__call_component", vals);
        }
        LambdaInvoker inv = [&rt](const Value& f, std::vector<Value>& xs) { return rt.CallLambda(f, xs); };
        if (CallValueMethod(v, name, vals, out, &inv)) return out;
        throw ScriptError("no method '" + name + "' on this value");
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

// Read an element: arr[index] for arrays, or map["key"] for dictionaries.
struct IndexExpr : Expr {
    ExprPtr arr, index;
    IndexExpr(ExprPtr a, ExprPtr i) : arr(std::move(a)), index(std::move(i)) {}
    Value Eval(Runtime& rt) override {
        Value a = arr->Eval(rt);
        if (auto v = a.AsArray()) {
            int i = (int)index->Eval(rt).AsFloat();
            if (i < 0 || i >= (int)v->size()) return Value{};
            return (*v)[i];
        }
        if (auto m = a.AsMap()) {
            auto it = m->find(index->Eval(rt).AsString());
            return it == m->end() ? Value{} : it->second;
        }
        return Value{};
    }
};

// Assign an element: arr[index] = value, or map["key"] = value.
struct IndexAssignExpr : Expr {
    ExprPtr arr, index, value;
    IndexAssignExpr(ExprPtr a, ExprPtr i, ExprPtr v)
        : arr(std::move(a)), index(std::move(i)), value(std::move(v)) {}
    Value Eval(Runtime& rt) override {
        Value a = arr->Eval(rt);
        Value val = value->Eval(rt);
        if (auto v = a.AsArray()) {
            int i = (int)index->Eval(rt).AsFloat();
            if (i >= 0 && i < (int)v->size()) (*v)[i] = val;
            else if (i == (int)v->size()) v->push_back(val); // append at end
        } else if (auto m = a.AsMap()) {
            (*m)[index->Eval(rt).AsString()] = val;
        }
        return val;
    }
};

// Compound assign to an element: arr[index] op= value (and ++/--). Evaluates the
// container and index once, reads the current value, applies op, writes it back.
struct IndexCompoundAssignExpr : Expr {
    ExprPtr arr, index, value; Tok op;
    IndexCompoundAssignExpr(ExprPtr a, ExprPtr i, Tok o, ExprPtr v)
        : arr(std::move(a)), index(std::move(i)), value(std::move(v)), op(o) {}
    Value Eval(Runtime& rt) override {
        Value a = arr->Eval(rt);
        Value rhs = value->Eval(rt);
        auto apply = [&](const Value& cur) -> Value {
            switch (op) {
                case Tok::Plus:  if (cur.IsString() || rhs.IsString()) return cur.AsString() + rhs.AsString();
                                 return cur.AsFloat() + rhs.AsFloat();
                case Tok::Minus: return cur.AsFloat() - rhs.AsFloat();
                case Tok::Star:  return cur.AsFloat() * rhs.AsFloat();
                case Tok::Slash: { float d = rhs.AsFloat(); return d != 0 ? cur.AsFloat() / d : 0.0f; }
                default:         return rhs;
            }
        };
        if (auto v = a.AsArray()) {
            int i = (int)index->Eval(rt).AsFloat();
            if (i >= 0 && i < (int)v->size()) { (*v)[i] = apply((*v)[i]); return (*v)[i]; }
        } else if (auto m = a.AsMap()) {
            std::string k = index->Eval(rt).AsString();
            Value cur = (*m).count(k) ? (*m)[k] : Value{};
            (*m)[k] = apply(cur); return (*m)[k];
        }
        return Value{};
    }
};

// ---- Statements ----
struct ExprStmt : Stmt { ExprPtr e; explicit ExprStmt(ExprPtr x) : e(std::move(x)) {} void Exec(Runtime& r) override { e->Eval(r); } };
struct VarDeclStmt : Stmt {
    std::string n; ExprPtr init; unsigned char kind = KAny;
    VarDeclStmt(std::string s, ExprPtr e, unsigned char k = KAny) : n(std::move(s)), init(std::move(e)), kind(k) {}
    void Exec(Runtime& r) override {
        if (kind & KStatic) {
            if (r.statics && r.statics->vars.count(n)) return;   // already set by another object
            r.DefineStatic(n, init ? init->Eval(r) : DefaultForKind(kind), (unsigned char)(kind & ~KStatic));
            return;
        }
        if (!kind) { r.Define(n, init ? init->Eval(r) : Value{}); return; }
        r.DefineTyped(n, init ? init->Eval(r) : DefaultForKind(kind), kind);
    }
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
    bool pairs = false;   // C# foreach over a Dictionary: items are {Key, Value}
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
                if (pairs) {
                    Value kv = Value::MakeMap();
                    (*kv.AsMap())["Key"] = Value{k};
                    auto it = m->find(k);
                    (*kv.AsMap())["Value"] = it != m->end() ? it->second : Value{};
                    r.Assign(var, kv);
                } else
                r.Assign(var, Value{k});
                try { for (auto& s : body) s->Exec(r); }
                catch (ContinueSignal&) {}
                catch (BreakSignal&) { break; }
            }
        }
    }
};
// x => x * 2  /  (a, b) => { ... }
struct LambdaExpr : Expr {
    int id = 0;
    std::vector<std::string> params;
    std::vector<StmtPtr> body;
    Value Eval(Runtime& r) override {
        r.lambdas[id] = Runtime::LambdaBody{&params, &body};
        Value v = Value::MakeMap(); (*v.AsMap())["__lambda"] = Value{(float)id};
        return v;
    }
};
// yield return <wait>;  — inside a coroutine: hand control back to the game loop.
struct YieldStmt : Stmt {
    ExprPtr e;
    void Exec(Runtime& r) override {
        Value v = e ? e->Eval(r) : Value{};
        if (auto m = v.AsMap()) {
            if (auto it = m->find("__wait"); it != m->end()) { r.YieldCoroutine(it->second.AsFloat(), false, 0); return; }
            if (auto it = m->find("__co"); it != m->end()) { r.YieldCoroutine(0.0f, false, (int)it->second.AsFloat()); return; }
        }
        r.YieldCoroutine(0.0f, true, 0);   // null / WaitForEndOfFrame / anything else: next frame
    }
};
// Several statements in the current scope (C# `int a = 1, b = 2;`).
struct MultiStmt : Stmt {
    std::vector<StmtPtr> list;
    void Exec(Runtime& r) override { for (auto& st : list) st->Exec(r); }
};
struct FunctionStmt : Stmt {
    std::string name; FunctionDecl decl;
    void Exec(Runtime&) override { /* hoisted at compile time */ }
};

// Cap script call depth so endless recursion is an error, not a stack overflow.
// Counted per thread: each coroutine runs on its own thread (and stack).
thread_local int t_callDepth = 0;
struct CallDepthGuard {
    explicit CallDepthGuard(const std::string& what) {
        if (t_callDepth >= 200) throw ScriptError(what + " calls itself too deeply (endless recursion?)");
        ++t_callDepth;
    }
    ~CallDepthGuard() { --t_callDepth; }
};

Value Runtime::Call(const std::string& name, std::vector<Value>& args) {
    auto b = builtins.find(name);
    if (b != builtins.end()) return b->second(args);

    auto f = functions.find(name);
    if (f == functions.end()) {
        // `name.Method(...)` on a variable (or a dotted property like gameObject.name):
        // s.ToUpper(), enemies.Add(e), scores.ContainsKey("bob"), hp.ToString().
        auto dot = name.rfind('.');
        if (dot != std::string::npos && dot > 0) {
            const std::string base = name.substr(0, dot), method = name.substr(dot + 1);
            bool ok = Has(base);
            Value obj = ok ? Get(base) : Value{};
            if (!ok) obj = GetDotted(base, ok);
            Value out;
            if (ok && IsComponentRef(obj)) {   // rb.AddForce(...), anim.SetTrigger("Jump")
                std::vector<Value> a{Value{obj.AsString().substr(1)}, Value{method}};
                a.insert(a.end(), args.begin(), args.end());
                return Call("__call_component", a);
            }
            if (ok && (method == "CompareTag" || method == "GetComponent" || method == "SetActive")) {
                // `other` in a collision handler is the other object's name.
                std::vector<Value> a{obj};
                a.insert(a.end(), args.begin(), args.end());
                return Call("__object_" + method, a);
            }
            LambdaInvoker inv = [this](const Value& fv, std::vector<Value>& xs) { return CallLambda(fv, xs); };
            if (ok && CallValueMethod(obj, method, args, out, &inv)) return out;
        }
        // A variable holding a lambda: Action onHit = () => ...; onHit();
        if (Has(name) && IsLambda(Get(name))) return CallLambda(Get(name), args);
        throw ScriptError("call to undefined function '" + name + "'");
    }

    CallDepthGuard depthGuard(name + "()");
    scopes.push_back(Environment{});
    const FunctionDecl& fn = f->second;
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        const unsigned char k = i < fn.paramKinds.size() ? fn.paramKinds[i] : 0;
        if (!k) { Define(fn.params[i], i < args.size() ? args[i] : Value{}); continue; }
        try { DefineTyped(fn.params[i], i < args.size() ? args[i] : DefaultForKind(k), k); }
        catch (...) { scopes.pop_back(); throw; }
    }

    Value ret;
    try {
        for (auto& s : fn.body) s->Exec(*this);
    } catch (ReturnSignal& rs) {
        ret = rs.value;
    } catch (BreakSignal&) {       // stray break/continue (no enclosing loop):
    } catch (ContinueSignal&) {    // end the function rather than escaping it.
    } catch (...) {
        scopes.pop_back();
        throw;
    }
    scopes.pop_back();
    if (fn.returnKind & 0x0F) return CoerceToKind(fn.returnKind, ret, name + "()");
    return ret;
}

Value Runtime::CallLambda(const Value& f, std::vector<Value>& args) {
    auto m = f.AsMap();
    if (!m) throw ScriptError("not a function");
    auto it = m->find("__lambda");
    if (it == m->end()) throw ScriptError("not a function");
    auto lb = lambdas.find((int)it->second.AsFloat());
    if (lb == lambdas.end()) throw ScriptError("lambda is no longer available");
    CallDepthGuard depthGuard("a lambda");
    scopes.push_back(Environment{});
    const auto& ps = *lb->second.params;
    for (std::size_t i = 0; i < ps.size(); ++i) Define(ps[i], i < args.size() ? args[i] : Value{});
    Value ret;
    try { for (auto& st : *lb->second.body) st->Exec(*this); }
    catch (ReturnSignal& rs) { ret = rs.value; }
    catch (...) { scopes.pop_back(); throw; }
    scopes.pop_back();
    return ret;
}

// ---- dotted property access --------------------------------------
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
    if (name == "Time.frameCount") return Value{(float)Time::FrameCount()};
    if (name == "Time.fixedDeltaTime" || name == "Time.unscaledDeltaTime")
        return Value{host ? host->deltaTime : 0.0f};
    if (name == "Space.World")     return Value{1.0f};
    if (name == "Space.Self")      return Value{0.0f};
    if (name == "null")            return Value{};
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
    if (name == "Vector3.forward") return Value{Vec3{0, 0, 1}};
    if (name == "Vector3.back")    return Value{Vec3{0, 0, -1}};
    if (name == "Vector2.up")    return Value{Vec3{0, 1, 0}};
    if (name == "Vector2.down")  return Value{Vec3{0, -1, 0}};
    if (name == "Vector2.right") return Value{Vec3{1, 0, 0}};
    if (name == "Vector2.left")  return Value{Vec3{-1, 0, 0}};

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
        if (name == "transform.eulerAngles" || name == "transform.localEulerAngles") return Value{t->localRotation.ToEuler()};
        if (name == "transform.eulerAngles.x" || name == "transform.localEulerAngles.x") return Value{t->localRotation.ToEuler().x};
        if (name == "transform.eulerAngles.y" || name == "transform.localEulerAngles.y") return Value{t->localRotation.ToEuler().y};
        if (name == "transform.localEulerAngles.z") return Value{t->localRotation.ToEuler().z};
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
                Value out, bv = Get(base);
                if (IsComponentRef(bv)) { std::vector<Value> a{Value{bv.AsString().substr(1)}, Value{prop}}; return Call("__get_component", a); }
                if (ValueProperty(bv, prop, out)) return out;
                // An object's name (e.g. `other` in a collision handler): other.name, other.tag,
                // other.gameObject, other.transform.position.
                if (bv.IsString() && g && g->scene()) {
                    if (prop == "name" || prop == "gameObject" || prop == "transform") return bv;
                    if (GameObject* o = g->scene()->Find(bv.AsString())) {
                        if (prop == "tag") return Value{o->tag};
                        if (prop == "position" && o->transform) return Value{o->transform->Position()};
                        if (prop == "activeSelf") return Value{o->active};
                    }
                }
            } else if (base.find('.') != std::string::npos) {
                // A property of a property: transform.position.normalized, gameObject.name.Length.
                bool inner = false;
                Value bv = GetDotted(base, inner), out;
                if (inner && ValueProperty(bv, prop, out)) return out;
                if (inner && bv.IsString() && g && g->scene() && base.rfind(".transform") == base.size() - 10)
                    if (GameObject* o = g->scene()->Find(bv.AsString()))   // other.transform.position
                        if (prop == "position" && o->transform) return Value{o->transform->Position()};
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
    // rb.velocity = ... on a GetComponent<T>() reference held in a variable.
    if (auto dot = name.find('.'); dot != std::string::npos && name.find('.', dot + 1) == std::string::npos) {
        const std::string base = name.substr(0, dot);
        if (Has(base)) {
            Value bv = Get(base);
            if (IsComponentRef(bv)) {
                std::vector<Value> a{Value{bv.AsString().substr(1)}, Value{name.substr(dot + 1)}, v};
                Call("__set_component", a);
                return true;
            }
        }
    }
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
        if (name == "transform.eulerAngles" || name == "transform.localEulerAngles")
            { t->localRotation = Quat::Euler(v.AsVec3()); return true; }
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
    explicit Parser(std::vector<Token> toks) : m_toks(std::move(toks)) { ScanEnums(); }

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

    // Parse for diagnostics only: keep going after a parse error by recording it
    // and re-synchronizing to the next statement boundary, so the editor can list
    // EVERY syntax error at once instead of just the first. The parsed output is
    // discarded — this exists purely to collect ScriptDiagnostic entries.
    // Syntax OkayScript accepts (so pasted C# runs) but that has no effect. Each is
    // reported once per line as a warning, so the editor can say exactly what to
    // delete instead of letting it look meaningful.
    std::vector<ScriptDiagnostic> NoEffectWarnings() const {
        std::vector<ScriptDiagnostic> w;
        std::set<std::pair<int, std::string>> seen;
        auto add = [&](int line, const std::string& key, const std::string& msg) {
            if (seen.insert({line, key}).second) w.push_back({line, msg, 1});
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
                add(t.line, "suffix", "`" + t.text + "f`: the letter after the number does nothing (every number is "
                                      "already a decimal). Write " + t.text + ".");
                continue;
            }
            if (t.type != Tok::Ident) continue;
            if (t.text == "using" && nx.type == Tok::Ident)
                add(t.line, "using", "`using` does nothing: everything is already available. Delete this line.");
            else if (t.text == "namespace" && nx.type == Tok::Ident)
                add(t.line, "namespace", "`namespace` does nothing. Delete it (and its braces); keep what's inside.");
            else if ((t.text == "class" || t.text == "struct") && nx.type == Tok::Ident) {
                const std::size_t after = SkipTypeSuffix(i + 2);
                if (m_toks[after].type == Tok::Colon)
                    add(t.line, "class", "The class wrapper and `: " + m_toks[after + 1].text + "` do nothing: the fields "
                                         "and methods in this file are the component. Delete the `class " + nx.text +
                                         "` line and its braces.");
                else
                    add(t.line, "class", "The class wrapper does nothing: the fields and methods in this file are the "
                                         "component. Delete the `class " + nx.text + "` line and its braces.");
            } else if (nx.type == Tok::Ident) {
                auto m = kMods.find(t.text);
                if (m != kMods.end()) add(t.line, t.text, m->second);
            }
            if (kNeverCalled.count(t.text) && nx.type == Tok::LParen && i > 0 && m_toks[i - 1].type == Tok::Ident)
                add(t.line, "never", "`" + t.text + "()` is never called by the engine, so this code won't run.");
        }
        return w;
    }

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
        for (auto& d : m_semantic) diags.push_back(d);
        for (auto& d : NoEffectWarnings()) diags.push_back(d);
        std::stable_sort(diags.begin(), diags.end(),
                         [](const ScriptDiagnostic& a, const ScriptDiagnostic& b) { return a.line < b.line; });
        return diags;
    }

    // Parse one top-level (or in-class) member: a function, a C#-style method,
    // a `[public] class Name : OkaySource { ... }` wrapper (its methods/fields
    // are hoisted out, so pasted C# scripts work), or a statement.
    void ParseMember(std::vector<StmtPtr>& top, std::unordered_map<std::string, FunctionDecl>& funcs) {
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
            std::string name; FunctionDecl decl = ParseFunction(name);
            funcs[name] = std::move(decl);
        } else if (IsTypedFunctionAhead()) {            // C#-style: void Start() { }
            std::string name; FunctionDecl decl = ParseTypedFunction(name);
            funcs[name] = std::move(decl);
        } else if (IsBareFunctionAhead()) {             // OkayScript: update(dt) { }
            std::string name = Expect(Tok::Ident, "function name").text;
            FunctionDecl decl = ParseParamsAndBody();
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
        if (m_toks[i].text == "foreach" || m_toks[i].text == "do") return false;
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
    std::size_t ScanIdentRun(std::size_t i, int& idents) const {
        idents = 0;
        while (m_toks[i].type == Tok::Ident) { ++idents; ++i; i = SkipTypeSuffix(i); }
        return i;
    }
    // Consume such a run and return its last identifier (the declared name).
    std::string ConsumeIdentRun(const char* what) {
        std::string last = Expect(Tok::Ident, what).text;
        for (;;) {
            m_pos = SkipTypeSuffix(m_pos);
            if (!Check(Tok::Ident)) break;
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
                if (Match(Tok::Assign)) ParseExpression();   // C# default value (ignored)
                decl.params.push_back(p);
                decl.paramKinds.push_back(pk);
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
    void CheckLiteralInit(unsigned char kind, const std::string& name, Expr* init, int line) {
        const unsigned char t = kind & 0x0F;
        if (!t || !init) return;
        const bool isStr = dynamic_cast<StringExpr*>(init) || dynamic_cast<InterpStringExpr*>(init);
        auto* num = dynamic_cast<NumberExpr*>(init);
        const bool isBool = dynamic_cast<BoolExpr*>(init) != nullptr;
        if ((t == KInt || t == KFloat || t == KBool) && isStr)
            m_semantic.push_back({line, "'" + name + "' is " + KindName(kind) + ", so it can't hold text"});
        else if (t == KString && (num || isBool))
            m_semantic.push_back({line, "'" + name + "' is a string: write the value in quotes"});
        else if (t == KInt && num && num->v != (double)(long long)num->v)
            m_semantic.push_back({line, "'" + name + "' is an int, so " + Fmt(num->v) + " becomes " +
                                        Fmt((double)(long long)num->v) + ". Use float for decimals", 1});
    }
    static std::string Fmt(double v) { std::ostringstream o; o << v; return o.str(); }

    // A C#-style typed local/field: a run of >= 2 identifiers (type + modifiers
    // + name) ending in '=' or ';' (e.g. `int score = 0;`, `Vector3 v;`).
    bool IsTypedVarDeclAhead() const {
        int idents = 0;
        std::size_t i = ScanIdentRun(m_pos, idents);
        if (idents < 2) return false;
        const Tok t = m_toks[i].type;
        return t == Tok::Assign || t == Tok::Semicolon || t == Tok::Comma || IsAutoPropertyAt(i);
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

    StmtPtr ParseStatement() {
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
        const bool isVar = Match(Tok::Var);
        if (isVar || IsTypedVarDeclAhead()) {
            // `int x`, `const float y`, `public string name`: the type and const-ness
            // are kept (they're enforced at runtime); the name is the last identifier.
            const unsigned char kind = isVar ? KAny : DeclKindAhead();
            const int declLine = Peek().line;
            std::string name = ConsumeIdentRun("variable name");
            SkipAutoPropertyBody();   // public int Lives { get; set; } = 3;
            ExprPtr init;
            if (Match(Tok::Assign)) init = ParseExpression();
            CheckLiteralInit(kind, name, init.get(), declLine);
            if (!Check(Tok::Comma)) { Match(Tok::Semicolon); return std::make_unique<VarDeclStmt>(name, std::move(init), kind); }
            auto multi = std::make_unique<MultiStmt>();   // int a = 1, b = 2;
            multi->list.push_back(std::make_unique<VarDeclStmt>(name, std::move(init), kind));
            while (Match(Tok::Comma)) {
                std::string n2 = Expect(Tok::Ident, "variable name").text;
                ExprPtr i2;
                if (Match(Tok::Assign)) i2 = ParseExpression();
                CheckLiteralInit(kind, n2, i2.get(), declLine);
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
            const int retLine = Prev().line;
            if (!Check(Tok::Semicolon) && !Check(Tok::RBrace)) e = ParseExpression();
            if (e && m_inVoid)
                m_semantic.push_back({retLine, m_fnName + "() is void, so it can't return a value. "
                                               "Give it a return type (int, float, bool or string) or just write `return;`"});
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
        ExprPtr c = ParseOr();
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
        std::size_t i = m_pos + 1;
        while (m_toks[i].type == Tok::Ident || m_toks[i].type == Tok::Comma) ++i;
        return m_toks[i].type == Tok::RParen && m_toks[i + 1].type == Tok::Arrow;
    }
    ExprPtr ParseLambda() {
        static int s_nextLambda = 1;
        auto lam = std::make_unique<LambdaExpr>();
        lam->id = s_nextLambda++;
        if (Check(Tok::Ident)) lam->params.push_back(m_toks[m_pos++].text);
        else {
            Expect(Tok::LParen, "'('");
            while (!Check(Tok::RParen)) {
                lam->params.push_back(ConsumeIdentRun("parameter"));
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
            throw ScriptError("invalid assignment target");
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
            throw ScriptError("invalid assignment target");
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
            throw ScriptError("invalid increment target");
        }
        return left;
    }
    // a + b + c ... parses in a loop, but builds a tree as deep as it is long that
    // running and freeing it walk recursively: cap the operators in one expression.
    static constexpr int kMaxOperators = 1000;
    void CountOperator() {
        if (++m_operators > kMaxOperators)
            throw ScriptError("parse error: expression is too long (more than " + std::to_string(kMaxOperators) + " operators)", Peek().line);
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
        if (m_nesting >= 256) throw ScriptError("parse error: expression nested too deeply", Peek().line);
        struct NestGuard { int& n; explicit NestGuard(int& d) : n(d) { ++n; } ~NestGuard() { --n; } } guard(m_nesting);
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
        for (;;) {
            if (Match(Tok::LBracket)) {
                ExprPtr idx = ParseExpression();
                Expect(Tok::RBracket, "']'");
                e = std::make_unique<IndexExpr>(std::move(e), std::move(idx));
            } else if (Match(Tok::Dot)) {
                // Member access on any expression: (a - b).magnitude, xs[0].ToString(),
                // GetName().ToUpper(). The lexer may glue a dotted run ("position.x").
                std::string path = Expect(Tok::Ident, "member name").text;
                std::vector<std::string> parts;
                for (std::size_t a = 0, b; a <= path.size(); a = b + 1) {
                    b = path.find('.', a); if (b == std::string::npos) b = path.size();
                    parts.push_back(path.substr(a, b - a));
                }
                for (std::size_t k = 0; k + 1 < parts.size(); ++k)
                    e = std::make_unique<MemberExpr>(std::move(e), parts[k]);
                if (Match(Tok::LParen)) {
                    auto call = std::make_unique<MethodCallExpr>(std::move(e), parts.back());
                    if (!Check(Tok::RParen)) {
                        do { call->args.push_back(ParseExpression()); } while (Match(Tok::Comma));
                    }
                    Expect(Tok::RParen, "')'");
                    e = std::move(call);
                } else {
                    e = std::make_unique<MemberExpr>(std::move(e), parts.back());
                }
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
                // StartCoroutine(Blink(0.2f)) names the routine instead of calling it now.
                if ((name == "StartCoroutine" || name == "StopCoroutine") && call->args.size() == 1)
                    if (auto* inner = dynamic_cast<CallExpr*>(call->args[0].get())) {
                        auto co = std::make_unique<CallExpr>(name);
                        co->args.push_back(std::make_unique<StringExpr>(inner->name));
                        for (auto& a : inner->args) co->args.push_back(std::move(a));
                        return co;
                    }
                return call;
            }
            if (auto it = m_enumValues.find(name); it != m_enumValues.end())
                return std::make_unique<NumberExpr>(it->second);   // State.Run -> 1
            return std::make_unique<VarExpr>(name);
        }
        throw ScriptError("parse error: unexpected token", Peek().line);
    }

    std::vector<Token> m_toks;
    std::size_t m_pos = 0;
    int m_nesting = 0;                                      // ParseUnary recursion depth
    int m_operators = 0;                                    // binary operators in this expression
    std::set<std::string> m_enums;                          // enum type names
    std::vector<ScriptDiagnostic> m_semantic;               // type mistakes found while parsing
    bool m_inVoid = false;                                  // parsing the body of a `void` function
    std::string m_fnName;
    std::unordered_map<std::string, double> m_enumValues;   // "State.Run" -> 1
};

} // namespace

// ===================== OkayScriptVM::Impl =====================
struct OkayScriptVM::Impl {
    Runtime rt;
    // Tweens and timers outlive a call; their callbacks check this so they do
    // nothing once the script is gone (its component removed or reloaded).
    std::shared_ptr<int> life = std::make_shared<int>(0);
    std::vector<StmtPtr> topLevel;
    bool loaded = false;
    ~Impl() { rt.StopCoroutines(""); }   // unwind every suspended coroutine thread
    // "Bare mode": a script with NO functions at all — its top-level statements ARE the
    // per-frame loop. Lets a whole script be one line, e.g. `on_key_move(5)`. Setup-style
    // top-level code (globals) still runs once at load whenever any function is defined.
    bool bareMode = false;
    // In bare mode the top-level runs once at Load (so globals exist immediately,
    // as for any script) AND every frame. To keep the per-frame tick count the
    // same as before, the first bare Update skips the re-run that Load already did.
    bool bareSkipFirstUpdate = false;

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
        b["ui_switch"]    = [](std::vector<Value>& a)  {   // sliding on/off switch
            auto* u = GetScriptUI(); bool cur = a.size() > 1 && a[1].AsFloat() != 0.0f;
            bool nv = u ? u->Switch(a.empty() ? "" : a[0].AsString().c_str(), cur) : cur;
            return Value{nv ? 1.0f : 0.0f};
        };
        // Launcher-style building blocks: ui_heading("Lobby"), ui_caption("NOW PLAYING"[, r, g, b]),
        // ui_text_dim, ui_text_wrapped, ui_primary_button, ui_badge("3/4"[, r, g, b]),
        // ui_avatar("Maya"[, size]), ui_card_begin("id") ... ui_card_end(),
        // ui_list_item(title[, subtitle[, right[, selected]]]) -> clicked, ui_key_value(k, v),
        // ui_spinner(), ui_toast(text[, kind 0/1/2[, seconds]]),
        // ui_tabs("A|B|C", current) -> index, ui_input(label, text) -> text, ui_spacing(h),
        // ui_style("launcher" | "classic").
        auto uiS = [](std::vector<Value>& a, std::size_t i) { return a.size() > i ? a[i].AsString() : std::string(); };
        auto uiN = [](std::vector<Value>& a, std::size_t i, float d) { return a.size() > i ? a[i].AsFloat() : d; };
        b["ui_heading"]   = [uiS](std::vector<Value>& a) { if (auto* u = GetScriptUI()) u->Heading(uiS(a, 0).c_str()); return Value{}; };
        b["ui_caption"]   = [uiS, uiN](std::vector<Value>& a) {
            if (auto* u = GetScriptUI()) u->Caption(uiS(a, 0).c_str(), (int)uiN(a, 1, 115), (int)uiN(a, 2, 209), (int)uiN(a, 3, 143));
            return Value{};
        };
        b["ui_text_dim"]     = [uiS](std::vector<Value>& a) { if (auto* u = GetScriptUI()) u->TextDim(uiS(a, 0).c_str()); return Value{}; };
        b["ui_text_wrapped"] = [uiS](std::vector<Value>& a) { if (auto* u = GetScriptUI()) u->TextWrapped(uiS(a, 0).c_str()); return Value{}; };
        b["ui_primary_button"] = [uiS](std::vector<Value>& a) { auto* u = GetScriptUI(); return Value{u && u->PrimaryButton(uiS(a, 0).c_str()) ? 1.0f : 0.0f}; };
        b["ui_badge"]     = [uiS, uiN](std::vector<Value>& a) {
            if (auto* u = GetScriptUI()) u->Badge(uiS(a, 0).c_str(), (int)uiN(a, 1, 0), (int)uiN(a, 2, 0), (int)uiN(a, 3, 0));
            return Value{};
        };
        b["ui_avatar"]    = [uiS, uiN](std::vector<Value>& a) { if (auto* u = GetScriptUI()) u->Avatar(uiS(a, 0).c_str(), uiN(a, 1, 0.0f)); return Value{}; };
        b["ui_card_begin"] = [uiS](std::vector<Value>& a) { if (auto* u = GetScriptUI()) u->BeginCard(a.empty() ? "card" : uiS(a, 0).c_str()); return Value{}; };
        b["ui_card_end"]  = [](std::vector<Value>&) { if (auto* u = GetScriptUI()) u->EndCard(); return Value{}; };
        b["ui_list_item"] = [uiS, uiN](std::vector<Value>& a) {
            auto* u = GetScriptUI();
            const std::string t = uiS(a, 0), sub = uiS(a, 1), r = uiS(a, 2);
            return Value{u && u->ListItem(t.c_str(), sub.empty() ? nullptr : sub.c_str(), r.empty() ? nullptr : r.c_str(), uiN(a, 3, 0.0f) != 0.0f) ? 1.0f : 0.0f};
        };
        b["ui_key_value"] = [uiS](std::vector<Value>& a) { if (auto* u = GetScriptUI()) u->KeyValue(uiS(a, 0).c_str(), uiS(a, 1).c_str()); return Value{}; };
        b["ui_spinner"]   = [](std::vector<Value>&) { if (auto* u = GetScriptUI()) u->Spinner(); return Value{}; };
        b["ui_toast"]     = [uiS, uiN](std::vector<Value>& a) { if (auto* u = GetScriptUI()) u->Toast(uiS(a, 0).c_str(), (int)uiN(a, 1, 0), uiN(a, 2, 3.0f)); return Value{}; };
        b["ui_tabs"]      = [uiS, uiN](std::vector<Value>& a) {
            auto* u = GetScriptUI(); const int cur = (int)uiN(a, 1, 0);
            return Value{(float)(u ? u->Tabs(uiS(a, 0).c_str(), cur) : cur)};
        };
        b["ui_input"]     = [uiS](std::vector<Value>& a) {
            auto* u = GetScriptUI(); const std::string cur = uiS(a, 1);
            return Value{std::string(u ? u->Input(uiS(a, 0).c_str(), cur.c_str()) : cur.c_str())};
        };
        b["ui_spacing"]   = [uiN](std::vector<Value>& a) { if (auto* u = GetScriptUI()) u->Spacing(uiN(a, 0, 8.0f)); return Value{}; };
        b["ui_style"]     = [uiS](std::vector<Value>& a) { if (auto* u = GetScriptUI()) u->Style(uiS(a, 0).c_str()); return Value{}; };
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
        // ---- High-level one-liners: dt-scaled INTERNALLY so beginners never write
        // "* dt". walk(dx,dy,speed) heads in a direction; spin(degPerSec) rotates
        // smoothly. (WASD control is the existing on_key_move(speed).) Much less code.
        b["walk"] = [this, tf](std::vector<Value>& a) {
            float dx = a.size() > 0 ? a[0].AsFloat() : 0.0f, dy = a.size() > 1 ? a[1].AsFloat() : 0.0f;
            float sp = a.size() > 2 ? a[2].AsFloat() : 1.0f;
            float dt = rt.host ? rt.host->deltaTime : 0.0f;
            float len = Mathf::Sqrt(dx * dx + dy * dy); if (len > 0.0001f) { dx /= len; dy /= len; }
            if (Transform* t = tf()) t->Translate({dx * sp * dt, dy * sp * dt, 0.0f});
            return Value{};
        };
        b["spin"] = [this, tf](std::vector<Value>& a) {
            float dps = a.empty() ? 90.0f : a[0].AsFloat();
            float dt = rt.host ? rt.host->deltaTime : 0.0f;
            if (Transform* t = tf()) t->Rotate({0, 0, dps * dt});
            return Value{};
        };
        // move_to(x, y, speed): walk toward a world POINT, stopping when it arrives.
        // dt-scaled (no "* dt"); great for "go to a spot" without object lookups.
        b["move_to"] = [this, tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || !rt.host || a.size() < 2) return Value{};
            float tx = a[0].AsFloat(), ty = a[1].AsFloat(), sp = a.size() > 2 ? a[2].AsFloat() : 3.0f;
            Vec3 me = t->Position();
            float dx = tx - me.x, dy = ty - me.y, len = Mathf::Sqrt(dx * dx + dy * dy);
            float step = sp * rt.host->deltaTime;
            if (len <= step || len < 1e-5f) { t->Translate({dx, dy, 0.0f}); return Value{}; }
            t->Translate({dx / len * step, dy / len * step, 0.0f});
            return Value{};
        };
        // bob(amount, speed): hover up/down around the start height (juice, one line).
        b["bob"] = [this, tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || !rt.host) return Value{};
            float amt = a.size() > 0 ? a[0].AsFloat() : 0.5f, sp = a.size() > 1 ? a[1].AsFloat() : 2.0f;
            auto& g = rt.host->globals;
            auto it = g.find("__bob_y");
            float baseY = it != g.end() ? it->second.AsFloat() : t->localPosition.y;
            if (it == g.end()) g["__bob_y"] = Value{baseY};
            t->localPosition.y = baseY + Mathf::Sin(Time::ElapsedTime() * sp) * amt;
            return Value{};
        };
        // pulse(amount, speed): gently grow/shrink around normal size (juice).
        b["pulse"] = [tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t) return Value{};
            float amt = a.size() > 0 ? a[0].AsFloat() : 0.2f, sp = a.size() > 1 ? a[1].AsFloat() : 3.0f;
            float s = 1.0f + Mathf::Sin(Time::ElapsedTime() * sp) * amt;
            t->localScale = {s, s, s};
            return Value{};
        };
        // wander(speed): roam in a random direction, picking a new one every ~1s.
        b["wander"] = [this, tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || !rt.host) return Value{};
            float sp = a.empty() ? 2.0f : a[0].AsFloat();
            auto& g = rt.host->globals;
            float dt = rt.host->deltaTime;
            float tmr = g.count("__wander_t")  ? g["__wander_t"].AsFloat()  : 999.0f;
            float dx  = g.count("__wander_dx") ? g["__wander_dx"].AsFloat() : 0.0f;
            float dy  = g.count("__wander_dy") ? g["__wander_dy"].AsFloat() : 0.0f;
            tmr += dt;
            if (tmr > 1.0f) { tmr = 0.0f; float ang = Random::Shared().Range(0.0f, 6.2831853f); dx = Mathf::Cos(ang); dy = Mathf::Sin(ang); }
            g["__wander_t"] = Value{tmr}; g["__wander_dx"] = Value{dx}; g["__wander_dy"] = Value{dy};
            t->Translate({dx * sp * dt, dy * sp * dt, 0.0f});
            return Value{};
        };
        b["pos_x"] = [tf](std::vector<Value>&) { Transform* t = tf(); return Value{t ? t->localPosition.x : 0.0f}; };
        b["pos_y"] = [tf](std::vector<Value>&) { Transform* t = tf(); return Value{t ? t->localPosition.y : 0.0f}; };
        b["time"]  = [](std::vector<Value>&) { return Value{Time::ElapsedTime()}; };
        b["set_timescale"] = [](std::vector<Value>& a) { Time::SetTimeScale(a.empty() ? 1.0f : a[0].AsFloat()); return Value{}; };
        b["timescale"]     = [](std::vector<Value>&) { return Value{Time::TimeScale()}; };
        // has_var("score"): is a script variable defined (in any scope)?
        b["has_var"] = [this](std::vector<Value>& a) { return Value{!a.empty() && rt.Has(a[0].AsString())}; };
        b["frame_count"]   = [](std::vector<Value>&) { return Value{(float)Time::FrameCount()}; };
        b["mouse_wheel"]   = [](std::vector<Value>&) { return Value{Input::MouseWheel()}; };
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
        // timer("name", seconds): returns true about once every `seconds` — kills the
        // manual `t = t + dt; if (t >= N)` boilerplate. e.g.
        //   if (timer("spawn", 2)) { spawn("enemy.okayprefab", 0, 5) }
        b["timer"] = [this](std::vector<Value>& a) -> Value {
            if (!rt.host || a.size() < 2) return Value{false};
            float period = a[1].AsFloat(); if (period <= 0.0f) return Value{true};
            std::string key = "__timer_" + a[0].AsString();
            float acc = rt.host->globals.count(key) ? rt.host->globals[key].AsFloat() : 0.0f;
            acc += rt.host->deltaTime;
            if (acc >= period) { rt.host->globals[key] = Value{acc - period}; return Value{true}; }
            rt.host->globals[key] = Value{acc};
            return Value{false};
        };
        b["axis_x"] = [](std::vector<Value>&) { return Value{Input::AxisWASD().x}; };
        b["axis_y"] = [](std::vector<Value>&) { return Value{Input::AxisWASD().y}; };
        b["key"]    = [](std::vector<Value>& a) {
            if (a.empty()) return Value{false};
            char c = KeyCharFromName(a[0].AsString());
            return Value{c != 0 && Input::GetKey(c)};
        };
        b["key_down"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{false};
            char c = KeyCharFromName(a[0].AsString());
            return Value{c != 0 && Input::GetKeyDown(c)};
        };
        b["mouse_x"] = [](std::vector<Value>&) { return Value{Input::MousePosition().x}; };
        b["mouse_y"] = [](std::vector<Value>&) { return Value{Input::MousePosition().y}; };
        b["mouse"] = [](std::vector<Value>& a) {
            return Value{Input::GetMouseButton(a.empty() ? 0 : (int)a[0].AsFloat())};
        };
        b["mouse_held"] = [](std::vector<Value>& a) {
            return Value{Input::GetMouseButton(a.empty() ? 0 : (int)a[0].AsFloat())};
        };
        b["move_towards"] = [](std::vector<Value>& a) {   // (current, target, maxDelta)
            float c = a.size() > 0 ? a[0].AsFloat() : 0.0f, t = a.size() > 1 ? a[1].AsFloat() : 0.0f, m = a.size() > 2 ? a[2].AsFloat() : 0.0f;
            return Value{std::fabs(t - c) <= m ? t : c + (t > c ? m : -m)};
        };
        b["dot"] = [](std::vector<Value>& a) {
            if (a.size() < 2) return Value{0.0f};
            return Value{Vec3::Dot(a[0].AsVec3(), a[1].AsVec3())};
        };
        b["to_fixed"] = [](std::vector<Value>& a) {   // (value, decimals) -> text
            int d = a.size() > 1 ? (int)a[1].AsFloat() : 2; d = d < 0 ? 0 : (d > 6 ? 6 : d);
            char buf[64]; std::snprintf(buf, sizeof(buf), "%.*f", d, a.empty() ? 0.0f : a[0].AsFloat());
            return Value{std::string(buf)};
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
        // score()/add_score(n): the shared "score" value in one call (less than
        // set("score", get("score") + n)). set_score(n) overwrites it.
        b["score"] = [this](std::vector<Value>&) -> Value {
            if (!rt.host) return Value{0.0f};
            auto it = rt.host->globals.find("score");
            return it != rt.host->globals.end() ? it->second : Value{0.0f};
        };
        b["add_score"] = [this](std::vector<Value>& a) {
            if (!rt.host) return Value{};
            float n = a.empty() ? 1.0f : a[0].AsFloat();
            auto it = rt.host->globals.find("score");
            float cur = it != rt.host->globals.end() ? it->second.AsFloat() : 0.0f;
            rt.host->globals["score"] = Value{cur + n};
            return Value{cur + n};
        };
        b["set_score"] = [this](std::vector<Value>& a) {
            if (rt.host) rt.host->globals["score"] = Value{a.empty() ? 0.0f : a[0].AsFloat()};
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
        // ---- Named-object helpers (also what visual-graph code export uses) ----
        auto findNamed = [this, sceneOf](const std::string& n) -> GameObject* {
            if (n.empty() || n == "self") return rt.host ? rt.host->gameObject : nullptr;
            Scene* s = sceneOf(); return s ? s->Find(n) : nullptr;
        };
        b["position_of"] = [findNamed](std::vector<Value>& a) {
            GameObject* g = findNamed(a.empty() ? std::string() : a[0].AsString());
            return Value{(g && g->transform) ? g->transform->Position() : Vec3{0, 0, 0}};
        };
        b["tag_of"] = [findNamed](std::vector<Value>& a) {
            GameObject* g = findNamed(a.empty() ? std::string() : a[0].AsString());
            return Value{g ? g->tag : std::string()};
        };
        b["set_active_of"] = [sceneOf](std::vector<Value>& a) {   // finds inactive objects too
            if (a.empty()) return Value{};
            Scene* s = sceneOf(); if (!s) return Value{};
            std::string n = a[0].AsString();
            for (const auto& up : s->Objects()) if (up && up->name == n) { up->active = a.size() < 2 || a[1].AsBool(); break; }
            return Value{};
        };
        b["set_text_of"] = [findNamed](std::vector<Value>& a) {
            if (GameObject* g = findNamed(a.empty() ? std::string() : a[0].AsString()))
                if (auto* tr = g->GetComponent<TextRenderer>()) tr->text = a.size() > 1 ? a[1].AsString() : std::string();
            return Value{};
        };
        b["move_toward_object"] = [this, findNamed](std::vector<Value>& a) {   // (name, speed) in 2D or 3D
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            GameObject* g = findNamed(a.empty() ? std::string() : a[0].AsString());
            if (!me || !g || g == me || !me->transform || !g->transform) return Value{};
            Vec3 d = g->transform->Position() - me->transform->Position();
            float dist = d.Magnitude(), step = (a.size() > 1 ? a[1].AsFloat() : 2.0f) * rt.host->deltaTime;
            if (dist > 1e-5f) me->transform->SetPosition(me->transform->Position() + d * ((step >= dist ? dist : step) / dist));
            return Value{};
        };
        b["face"] = [this, findNamed](std::vector<Value>& a) {   // turn toward a named object (3D yaw, else 2D Z)
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            GameObject* g = findNamed(a.empty() ? std::string() : a[0].AsString());
            if (!me || !g || g == me || !me->transform || !g->transform) return Value{};
            Vec3 d = g->transform->Position() - me->transform->Position();
            if (std::fabs(d.z) > 1e-4f) me->transform->localRotation = Quat::LookRotation(Vec3{-d.x, 0.0f, -d.z}.Normalized());
            else me->transform->localRotation = Quat::Euler(0, 0, std::atan2(d.y, d.x) * 57.2957795f);
            return Value{};
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
        // ---- NPC commands: order an NPC Controller around from a script -----
        // npc_goto("Guard", x, y, z): send the named NPC to a world point (it
        // pathfinds there if pathfinding is on, then broadcasts npc_arrived).
        // Omit the name ("" or self name) to command a sibling NPCController.
        auto npcOf = [this, sceneOf](const std::string& name) -> NPCController* {
            if (name.empty())
                return (rt.host && rt.host->gameObject) ? rt.host->gameObject->GetComponent<NPCController>() : nullptr;
            Scene* s = sceneOf(); if (!s) return nullptr;
            GameObject* g = s->Find(name);
            return g ? g->GetComponent<NPCController>() : nullptr;
        };
        b["npc_goto"] = [npcOf](std::vector<Value>& a) -> Value {
            if (a.size() < 4) return Value{false};
            NPCController* n = npcOf(a[0].AsString()); if (!n) return Value{false};
            n->CommandGoTo({a[1].AsFloat(), a[2].AsFloat(), a[3].AsFloat()});
            return Value{true};
        };
        b["npc_stop"] = [npcOf](std::vector<Value>& a) -> Value {
            NPCController* n = npcOf(a.empty() ? std::string{} : a[0].AsString());
            if (!n) return Value{false};
            n->CancelCommand(); return Value{true};
        };
        // True while the NPC is still walking to its npc_goto point.
        b["npc_busy"] = [npcOf](std::vector<Value>& a) -> Value {
            NPCController* n = npcOf(a.empty() ? std::string{} : a[0].AsString());
            return Value{n && n->Commanded()};
        };
        // Current AI state name: "Idle","Wander","Patrol","Follow","Flee",
        // "Chase","Search","Return" ("" if the object has no NPC Controller).
        b["npc_state"] = [npcOf](std::vector<Value>& a) -> Value {
            NPCController* n = npcOf(a.empty() ? std::string{} : a[0].AsString());
            return Value{std::string{n ? n->StateName() : ""}};
        };
        // ---- Spawner control: run enemy/pickup waves from a script ----------
        // Omit the name ("" ) to drive a sibling Spawner on this object.
        auto spawnerOf = [this, sceneOf](const std::string& name) -> Spawner* {
            if (name.empty())
                return (rt.host && rt.host->gameObject) ? rt.host->gameObject->GetComponent<Spawner>() : nullptr;
            Scene* s = sceneOf(); if (!s) return nullptr;
            GameObject* g = s->Find(name);
            return g ? g->GetComponent<Spawner>() : nullptr;
        };
        b["spawner_start"] = [spawnerOf](std::vector<Value>& a) -> Value {
            Spawner* s = spawnerOf(a.empty() ? std::string{} : a[0].AsString());
            if (!s) return Value{false};
            s->StartWaves(); return Value{true};
        };
        b["spawner_stop"] = [spawnerOf](std::vector<Value>& a) -> Value {
            Spawner* s = spawnerOf(a.empty() ? std::string{} : a[0].AsString());
            if (!s) return Value{false};
            s->StopWaves(); return Value{true};
        };
        // Live objects a spawner created (wave HUDs: "3 enemies left").
        b["spawner_alive"] = [spawnerOf](std::vector<Value>& a) -> Value {
            Spawner* s = spawnerOf(a.empty() ? std::string{} : a[0].AsString());
            return Value{(float)(s ? s->AliveCount() : 0)};
        };
        // ---- In-game build mode + logic links (docs/build_mode.md) -----------
        // They drive the scene's Build Mode component (the player's).
        auto buildOf = [sceneOf]() -> BuildMode* {
            Scene* s = sceneOf();
            return s ? s->FindObjectOfType<BuildMode>() : nullptr;
        };
        auto argStr = [](std::vector<Value>& a, std::size_t i) { return a.size() > i ? a[i].AsString() : std::string{}; };
        b["build_enter"] = [buildOf](std::vector<Value>&) -> Value {
            BuildMode* m = buildOf(); if (!m) return Value{false};
            m->SetActive(true); return Value{true};
        };
        b["build_exit"] = [buildOf](std::vector<Value>&) -> Value {
            BuildMode* m = buildOf(); if (!m) return Value{false};
            m->SetActive(false); return Value{true};
        };
        b["build_toggle"] = [buildOf](std::vector<Value>&) -> Value {
            BuildMode* m = buildOf(); if (!m) return Value{false};
            m->SetActive(!m->Active()); return Value{m->Active()};
        };
        b["build_active"] = [buildOf](std::vector<Value>&) -> Value {
            BuildMode* m = buildOf(); return Value{m && m->Active()};
        };
        // build_link_mode(on): link mode on / off (no argument = toggle).
        b["build_link_mode"] = [buildOf](std::vector<Value>& a) -> Value {
            BuildMode* m = buildOf(); if (!m) return Value{false};
            m->SetLinkMode(a.empty() ? !m->LinkMode() : a[0].AsBool()); return Value{m->LinkMode()};
        };
        b["build_palette_clear"] = [buildOf](std::vector<Value>&) -> Value {
            BuildMode* m = buildOf(); if (!m) return Value{false};
            m->ClearPalette(); return Value{true};
        };
        // build_palette_add(name, prefab, icon): returns the new slot (0-based), -1 without Build Mode.
        b["build_palette_add"] = [buildOf, argStr](std::vector<Value>& a) -> Value {
            BuildMode* m = buildOf(); if (!m) return Value{-1.0f};
            return Value{(float)m->AddToPalette(argStr(a, 0), argStr(a, 1), argStr(a, 2))};
        };
        b["build_palette_select"] = [buildOf](std::vector<Value>& a) -> Value {
            BuildMode* m = buildOf(); if (!m || a.empty()) return Value{false};
            m->SelectSlot((int)a[0].AsFloat()); return Value{true};
        };
        b["build_selected"] = [buildOf](std::vector<Value>&) -> Value {
            BuildMode* m = buildOf(); return Value{(float)(m ? m->SelectedSlot() : -1)};
        };
        // Creations: save / load what players placed (a name in the save folder's
        // "creations" folder, or a path). Work without Build Mode too.
        b["build_save"] = [buildOf, sceneOf, argStr](std::vector<Value>& a) -> Value {
            std::string name = argStr(a, 0);
            if (!name.empty() && !ScriptPathOk(name, "build_save")) return Value{false};
            if (BuildMode* m = buildOf()) return Value{m->SaveCreation(name)};
            Scene* s = sceneOf(); if (!s) return Value{false};
            return Value{BuildCreation::Save(*s, BuildMode::CreationPath(name))};
        };
        b["build_load"] = [buildOf, sceneOf, argStr](std::vector<Value>& a) -> Value {
            std::string name = argStr(a, 0);
            if (!name.empty() && !ScriptPathOk(name, "build_load")) return Value{false};
            if (BuildMode* m = buildOf()) return Value{m->LoadCreation(name)};
            Scene* s = sceneOf(); if (!s) return Value{false};
            return Value{BuildCreation::Load(*s, BuildMode::CreationPath(name))};
        };
        b["build_exists"] = [buildOf, argStr](std::vector<Value>& a) -> Value {
            if (!argStr(a, 0).empty() && !ScriptPathOk(argStr(a, 0), "build_exists")) return Value{false};
            BuildMode* m = buildOf();
            std::ifstream f(BuildMode::CreationPath(argStr(a, 0), m ? m->creationFolder : "creations"));
            return Value{(bool)f};
        };
        b["build_clear"] = [buildOf, sceneOf](std::vector<Value>&) -> Value {
            if (BuildMode* m = buildOf()) { m->ClearCreation(); return Value{true}; }
            Scene* s = sceneOf(); if (!s) return Value{false};
            BuildCreation::Clear(*s); return Value{true};
        };
        b["build_undo"] = [buildOf](std::vector<Value>&) -> Value { BuildMode* m = buildOf(); return Value{m && m->Undo()}; };
        b["build_redo"] = [buildOf](std::vector<Value>&) -> Value { BuildMode* m = buildOf(); return Value{m && m->Redo()}; };
        // Objects players have placed.
        b["build_count"] = [sceneOf](std::vector<Value>&) -> Value {
            Scene* s = sceneOf(); return Value{(float)(s ? BuildCreation::PlacedRoots(*s).size() : 0)};
        };
        // logic_press(name): press a Logic Source button (any source: flip its output).
        b["logic_press"] = [this, sceneOf, argStr](std::vector<Value>& a) -> Value {
            Scene* s = sceneOf(); if (!s) return Value{false};
            std::string n = argStr(a, 0);
            GameObject* g = n.empty() ? (rt.host ? rt.host->gameObject : nullptr) : s->Find(n);
            LogicSource* ls = g ? g->GetComponent<LogicSource>() : nullptr;
            if (!ls) return Value{false};
            if (ls->kind == LogicSource::Kind::Button) ls->Press(); else ls->SetOutput(!ls->Output());
            return Value{true};
        };
        // logic_state(name): a source's output, or a target's on / off state.
        b["logic_state"] = [this, sceneOf, argStr](std::vector<Value>& a) -> Value {
            Scene* s = sceneOf(); if (!s) return Value{false};
            std::string n = argStr(a, 0);
            GameObject* g = n.empty() ? (rt.host ? rt.host->gameObject : nullptr) : s->Find(n);
            if (!g) return Value{false};
            if (auto* lt = g->GetComponent<LogicTarget>()) return Value{lt->State()};
            if (auto* ls = g->GetComponent<LogicSource>()) return Value{ls->Output()};
            return Value{false};
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
        // aim(x, y): rotate (Z) to face a world POINT — turrets, arrows, "look where
        // I'm going". Like look_at but toward coordinates instead of a named object.
        b["aim"] = [this, tf](std::vector<Value>& a) {
            if (a.size() < 2) return Value{};
            Transform* t = tf(); if (!t || !rt.host || !rt.host->gameObject) return Value{};
            Vec3 me = rt.host->gameObject->transform->Position();
            float deg = std::atan2(a[1].AsFloat() - me.y, a[0].AsFloat() - me.x) * 57.2957795f;
            t->localRotation = Quat::Euler({0, 0, deg});
            return Value{};
        };
        // grid_snap(size): snap this object's position to the nearest multiple of
        // `size` on X and Y — tile placement, building games, chunky movement.
        b["grid_snap"] = [tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t) return Value{};
            float g = a.empty() ? 1.0f : a[0].AsFloat();
            if (g <= 1e-6f) return Value{};
            t->localPosition.x = std::round(t->localPosition.x / g) * g;
            t->localPosition.y = std::round(t->localPosition.y / g) * g;
            return Value{};
        };

        // ---- High-level one-liners -----------------------------------------
        // Each is a whole behaviour in a single call, meant to be invoked every
        // frame from update(): the "write less, do more" layer over the low-level
        // move/spawn/input builtins.
        b["follow"] = [this, tf, sceneOf](std::vector<Value>& a) {
            // follow("name", speed[, stopDist]): chase a named object on the XY plane.
            if (a.empty() || !rt.host) return Value{};
            Transform* t = tf(); Scene* s = sceneOf();
            if (!t || !s) return Value{};
            GameObject* g = s->Find(a[0].AsString()); if (!g || !g->transform) return Value{};
            float speed = a.size() > 1 ? a[1].AsFloat() : 3.0f;
            float stop  = a.size() > 2 ? a[2].AsFloat() : 0.0f;
            Vec3 me = t->Position(), ot = g->transform->Position();
            float dx = ot.x - me.x, dy = ot.y - me.y;
            float dist = Mathf::Sqrt(dx * dx + dy * dy);
            if (dist > stop && dist > 1e-5f) {
                float step = Mathf::Min(speed * rt.host->deltaTime, dist - stop);
                t->Translate({dx / dist * step, dy / dist * step, 0.0f});
            }
            return Value{};
        };
        b["follow3"] = [this, tf, sceneOf](std::vector<Value>& a) {
            // follow3("name", speed[, stopDist]): chase a named object in full 3D.
            if (a.empty() || !rt.host) return Value{};
            Transform* t = tf(); Scene* s = sceneOf();
            if (!t || !s) return Value{};
            GameObject* g = s->Find(a[0].AsString()); if (!g || !g->transform) return Value{};
            float speed = a.size() > 1 ? a[1].AsFloat() : 3.0f;
            float stop  = a.size() > 2 ? a[2].AsFloat() : 0.0f;
            Vec3 d = g->transform->Position() - t->Position();
            float dist = d.Magnitude();
            if (dist > stop && dist > 1e-5f) {
                float step = Mathf::Min(speed * rt.host->deltaTime, dist - stop);
                t->Translate(d * (step / dist));
            }
            return Value{};
        };
        b["flee"] = [this, tf, sceneOf](std::vector<Value>& a) {
            // flee("name", speed): run directly away from a named object (XY plane).
            if (a.empty() || !rt.host) return Value{};
            Transform* t = tf(); Scene* s = sceneOf();
            if (!t || !s) return Value{};
            GameObject* g = s->Find(a[0].AsString()); if (!g || !g->transform) return Value{};
            float speed = a.size() > 1 ? a[1].AsFloat() : 3.0f;
            Vec3 me = t->Position(), ot = g->transform->Position();
            float dx = me.x - ot.x, dy = me.y - ot.y;
            float dist = Mathf::Sqrt(dx * dx + dy * dy);
            if (dist > 1e-5f) {
                float k = speed * rt.host->deltaTime / dist;
                t->Translate({dx * k, dy * k, 0.0f});
            }
            return Value{};
        };
        // platformer(speed[, jump]): a whole 2D side-scroller controller in one line.
        // A/D or Left/Right set horizontal velocity; W / Up jumps (edge-triggered).
        // Drives a Rigidbody2D if present (so gravity/ground work), else slides the
        // Transform horizontally. dt-scaled — no `* dt` needed.
        b["platformer"] = [this, tf](std::vector<Value>& a) {
            if (!rt.host || !rt.host->gameObject) return Value{};
            float speed = a.size() > 0 ? a[0].AsFloat() : 5.0f;
            float jump  = a.size() > 1 ? a[1].AsFloat() : 9.0f;
            Vec2 ax = Input::AxisWASD();
            bool upNow = ax.y > 0.5f;
            auto& g = rt.host->globals;
            bool upWas = g.count("__plat_up") && g["__plat_up"].AsFloat() != 0.0f;
            g["__plat_up"] = Value{upNow ? 1.0f : 0.0f};
            if (auto* rb = rt.host->gameObject->GetComponent<Rigidbody2D>()) {
                rb->velocity.x = ax.x * speed;
                if (upNow && !upWas) rb->velocity.y = jump;      // rising edge = one jump
            } else if (Transform* t = tf()) {
                t->Translate({ax.x * speed * rt.host->deltaTime, 0.0f, 0.0f});
            }
            return Value{};
        };
        // on_key("key", "fn"): call this script's function `fn` once, the frame `key`
        // is pressed. Event-style input with no if/edge bookkeeping.
        b["on_key"] = [this](std::vector<Value>& a) {
            if (a.size() < 2) return Value{};
            char c = KeyCharFromName(a[0].AsString());
            if (c != 0 && Input::GetKeyDown(c)) {
                std::string fn = a[1].AsString();
                if (rt.functions.count(fn) || rt.builtins.count(fn)) { std::vector<Value> none; rt.Call(fn, none); }
            }
            return Value{};
        };
        // smooth_follow("name", speed): chase a named object, but ease in (exponential
        // smoothing) so it glides instead of tracking rigidly. dt-scaled.
        b["smooth_follow"] = [this, tf, sceneOf](std::vector<Value>& a) {
            if (a.empty() || !rt.host) return Value{};
            Transform* t = tf(); Scene* s = sceneOf();
            if (!t || !s) return Value{};
            GameObject* g = s->Find(a[0].AsString()); if (!g || !g->transform) return Value{};
            float speed = a.size() > 1 ? a[1].AsFloat() : 4.0f;
            float k = 1.0f - std::exp(-speed * rt.host->deltaTime);   // 0..1 ease factor
            Vec3 me = t->Position(), ot = g->transform->Position();
            t->Translate({(ot.x - me.x) * k, (ot.y - me.y) * k, 0.0f});
            return Value{};
        };
        // spring_to(x, y[, speed]): ease toward a world POINT with the same smoothing,
        // slowing as it arrives. Great for cameras, cursors, snapping. dt-scaled.
        b["spring_to"] = [this, tf](std::vector<Value>& a) {
            if (a.size() < 2 || !rt.host) return Value{};
            Transform* t = tf(); if (!t) return Value{};
            float tx = a[0].AsFloat(), ty = a[1].AsFloat(), sp = a.size() > 2 ? a[2].AsFloat() : 6.0f;
            float k = 1.0f - std::exp(-sp * rt.host->deltaTime);
            Vec3 me = t->Position();
            t->Translate({(tx - me.x) * k, (ty - me.y) * k, 0.0f});
            return Value{};
        };
        b["patrol"] = [this, tf](std::vector<Value>& a) {
            // patrol(x1, y1, x2, y2, speed): walk back and forth between two points.
            // Uses a per-script time accumulator so it advances with update()'s dt.
            if (a.size() < 5 || !rt.host) return Value{};
            Transform* t = tf(); if (!t) return Value{};
            float x1 = a[0].AsFloat(), y1 = a[1].AsFloat(), x2 = a[2].AsFloat(), y2 = a[3].AsFloat(), speed = a[4].AsFloat();
            float dx = x2 - x1, dy = y2 - y1; float len = Mathf::Sqrt(dx * dx + dy * dy);
            if (len < 1e-4f || speed <= 0.0f) return Value{};
            auto& g = rt.host->globals;
            float acc = 0.0f;
            auto it = g.find("__patrol_t"); if (it != g.end()) acc = it->second.AsFloat();
            acc += rt.host->deltaTime;
            g["__patrol_t"] = Value{acc};
            float period = 2.0f * len / speed;                             // full round trip
            float ph = std::fmod(acc, period) / (period * 0.5f);           // 0..2
            float f = ph < 1.0f ? ph : 2.0f - ph;                          // triangle 0..1..0
            Vec3 me = t->Position();
            t->SetPosition({x1 + dx * f, y1 + dy * f, me.z});
            return Value{};
        };
        b["orbit"] = [this, tf, sceneOf](std::vector<Value>& a) {
            // orbit("name", radius, degPerSec): circle a target each frame.
            if (a.empty() || !rt.host) return Value{};
            Transform* t = tf(); Scene* s = sceneOf();
            if (!t || !s) return Value{};
            GameObject* g = s->Find(a[0].AsString()); if (!g || !g->transform) return Value{};
            float radius = a.size() > 1 ? a[1].AsFloat() : 2.0f;
            float spd    = a.size() > 2 ? a[2].AsFloat() : 90.0f;
            Vec3 c = g->transform->Position(), me = t->Position();
            float ang = std::atan2(me.y - c.y, me.x - c.x) + spd * 0.01745329f * rt.host->deltaTime;
            t->SetPosition({c.x + Mathf::Cos(ang) * radius, c.y + Mathf::Sin(ang) * radius, me.z});
            return Value{};
        };
        b["on_key_move"] = [this, tf](std::vector<Value>& a) {
            // on_key_move(speed): WASD/arrow keys move this object on the XY plane
            // (drives a Rigidbody2D's velocity if present, else the Transform).
            Transform* t = tf(); if (!t || !rt.host) return Value{};
            float speed = a.empty() ? 5.0f : a[0].AsFloat();
            Vec2 ax = Input::AxisWASD();
            float len = Mathf::Sqrt(ax.x * ax.x + ax.y * ax.y);
            if (len > 1.0f) { ax.x /= len; ax.y /= len; }
            GameObject* go = rt.host->gameObject;
            if (auto* rb = go ? go->GetComponent<Rigidbody2D>() : nullptr)
                rb->velocity = {ax.x * speed, ax.y * speed};
            else
                t->Translate({ax.x * speed * rt.host->deltaTime, ax.y * speed * rt.host->deltaTime, 0.0f});
            return Value{};
        };
        b["on_key_move3"] = [this, tf](std::vector<Value>& a) {
            // on_key_move3(speed): WASD/arrow keys move this object on the XZ ground
            // plane (drives a Rigidbody3D's velocity if present, else the Transform).
            Transform* t = tf(); if (!t || !rt.host) return Value{};
            float speed = a.empty() ? 5.0f : a[0].AsFloat();
            Vec2 ax = Input::AxisWASD();
            float len = Mathf::Sqrt(ax.x * ax.x + ax.y * ax.y);
            if (len > 1.0f) { ax.x /= len; ax.y /= len; }
            GameObject* go = rt.host->gameObject;
            if (auto* rb = go ? go->GetComponent<Rigidbody3D>() : nullptr)
                { rb->velocity.x = ax.x * speed; rb->velocity.z = ax.y * speed; }
            else
                t->Translate({ax.x * speed * rt.host->deltaTime, 0.0f, ax.y * speed * rt.host->deltaTime});
            return Value{};
        };
        b["shoot_at"] = [this, tf, sceneOf](std::vector<Value>& a) {
            // shoot_at("target", "prefab", speed): spawn a projectile at self flying
            // toward the target (sets the new object's Rigidbody velocity).
            if (a.size() < 2 || !rt.host || !rt.host->gameObject) return Value{false};
            Scene* s = sceneOf(); if (!s) return Value{false};
            GameObject* tgt = s->Find(a[0].AsString()); if (!tgt || !tgt->transform) return Value{false};
            Vec3 me = rt.host->gameObject->transform->Position();
            GameObject* go = SceneSerializer::InstantiateFromFile(*s, a[1].AsString(), nullptr);
            if (!go || !go->transform) return Value{false};
            go->transform->localPosition = me;
            float speed = a.size() > 2 ? a[2].AsFloat() : 8.0f;
            Vec3 d = tgt->transform->Position() - me; float dist = d.Magnitude();
            if (dist > 1e-5f) {
                Vec3 v = d * (speed / dist);
                if (auto* rb = go->GetComponent<Rigidbody2D>()) rb->velocity = {v.x, v.y};
                else if (auto* rb3 = go->GetComponent<Rigidbody3D>()) rb3->velocity = v;
            }
            return Value{true};
        };
        b["spawn_wave"] = [this, sceneOf](std::vector<Value>& a) {
            // spawn_wave("prefab", count[, radius]): ring of prefabs around self.
            if (a.empty() || !rt.host || !rt.host->gameObject) return Value{0.0f};
            Scene* s = sceneOf(); if (!s) return Value{0.0f};
            int count = a.size() > 1 ? (int)a[1].AsFloat() : 8;
            if (count < 1) count = 1;
            float radius = a.size() > 2 ? a[2].AsFloat() : 3.0f;
            Vec3 c = rt.host->gameObject->transform->Position();
            int made = 0;
            for (int i = 0; i < count; ++i) {
                GameObject* go = SceneSerializer::InstantiateFromFile(*s, a[0].AsString(), nullptr);
                if (!go || !go->transform) continue;
                float ang = (6.2831853f * i) / count;
                go->transform->localPosition = {c.x + Mathf::Cos(ang) * radius, c.y + Mathf::Sin(ang) * radius, c.z};
                ++made;
            }
            return Value{(float)made};
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
        // Camera shake (0..1, stacks) and smooth zoom on the main camera.
        b["camera_shake"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera) s->mainCamera->Shake(a.empty() ? 0.5f : a[0].AsFloat());
            return Value{};
        };
        b["camera_zoom_to"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera && !a.empty())
                s->mainCamera->ZoomTo(a[0].AsFloat(), a.size() > 1 ? a[1].AsFloat() : 0.0f);
            return Value{};
        };
        b["camera_dutch"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera) s->mainCamera->dutch = a.empty() ? 0.0f : a[0].AsFloat();
            return Value{};
        };
        b["camera_fov"] = [sceneOf](std::vector<Value>& a) {
            if (Scene* s = sceneOf()) if (s->mainCamera) {
                if (a.empty()) return Value{s->mainCamera->fieldOfView};
                s->mainCamera->fieldOfView = Mathf::Clamp(a[0].AsFloat(), 1.0f, 179.0f);
            }
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
                if (auto* mr = g->GetComponent<MeshRenderer>()) mr->color = c;
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
        // Tab bars: read / select the tab (fires on_change), badge a tab (0 none,
        // -1 dot, N number) and enable / disable a tab. Tab indices are 0-based.
        b["ui_tab_value"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                if (auto* tb = g->GetComponent<UITabs>()) return Value{(float)tb->value};
            return Value{0.0f};
        };
        b["ui_tab_text"] = [sceneOf](std::vector<Value>& a) -> Value {
            if (!a.empty()) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                if (auto* tb = g->GetComponent<UITabs>()) return Value{tb->SelectedLabel()};
            return Value{std::string{}};
        };
        b["ui_set_tab"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                if (auto* tb = g->GetComponent<UITabs>()) tb->Select((int)a[1].AsFloat());
            return Value{};
        };
        b["ui_set_tab_badge"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 3) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                if (auto* tb = g->GetComponent<UITabs>()) tb->SetBadge((int)a[1].AsFloat(), (int)a[2].AsFloat());
            return Value{};
        };
        b["ui_set_tab_enabled"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 3) if (Scene* s = sceneOf()) if (GameObject* g = s->Find(a[0].AsString()))
                if (auto* tb = g->GetComponent<UITabs>()) tb->SetTabEnabled((int)a[1].AsFloat(), a[2].AsBool());
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
        // UI tabs on this object (react to switches via on_change).
        b["tab_value"] = [go](std::vector<Value>&) -> Value {
            if (GameObject* g = go())
                if (auto* tb = g->GetComponent<UITabs>()) return Value{(float)tb->value};
            return Value{0.0f};
        };
        b["set_tab"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* tb = g->GetComponent<UITabs>()) tb->Select(a.empty() ? 0 : (int)a[0].AsFloat());
            return Value{};
        };
        b["set_tab_badge"] = [go](std::vector<Value>& a) {
            if (a.size() >= 2) if (GameObject* g = go())
                if (auto* tb = g->GetComponent<UITabs>()) tb->SetBadge((int)a[0].AsFloat(), (int)a[1].AsFloat());
            return Value{};
        };
        b["set_tab_enabled"] = [go](std::vector<Value>& a) {
            if (a.size() >= 2) if (GameObject* g = go())
                if (auto* tb = g->GetComponent<UITabs>()) tb->SetTabEnabled((int)a[0].AsFloat(), a[1].AsBool());
            return Value{};
        };
        // Sibling Rigidbody2D control (jump, dash, knockback, top-down movement).
        // (No Rigidbody2D? The same calls drive a Rigidbody3D, with an optional z.)
        auto arg3 = [](std::vector<Value>& a, std::size_t i) { return a.size() > i ? a[i].AsFloat() : 0.0f; };
        b["velocity_x"] = [go](std::vector<Value>&) -> Value {
            if (GameObject* g = go()) {
                if (auto* rb = g->GetComponent<Rigidbody2D>()) return Value{rb->velocity.x};
                if (auto* r3 = g->GetComponent<Rigidbody3D>()) return Value{r3->velocity.x};
            }
            return Value{0.0f};
        };
        b["velocity_y"] = [go](std::vector<Value>&) -> Value {
            if (GameObject* g = go()) {
                if (auto* rb = g->GetComponent<Rigidbody2D>()) return Value{rb->velocity.y};
                if (auto* r3 = g->GetComponent<Rigidbody3D>()) return Value{r3->velocity.y};
            }
            return Value{0.0f};
        };
        b["set_velocity"] = [go, arg3](std::vector<Value>& a) {
            if (GameObject* g = go()) {
                if (auto* rb = g->GetComponent<Rigidbody2D>()) rb->velocity = {arg3(a, 0), arg3(a, 1)};
                else if (auto* r3 = g->GetComponent<Rigidbody3D>()) { r3->velocity = {arg3(a, 0), arg3(a, 1), arg3(a, 2)}; r3->WakeUp(); }
            }
            return Value{};
        };
        b["set_vx"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) {
                if (auto* rb = g->GetComponent<Rigidbody2D>()) rb->velocity.x = a.empty() ? 0.0f : a[0].AsFloat();
                else if (auto* r3 = g->GetComponent<Rigidbody3D>()) { r3->velocity.x = a.empty() ? 0.0f : a[0].AsFloat(); r3->WakeUp(); }
            }
            return Value{};
        };
        b["set_vy"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) {
                if (auto* rb = g->GetComponent<Rigidbody2D>()) rb->velocity.y = a.empty() ? 0.0f : a[0].AsFloat();
                else if (auto* r3 = g->GetComponent<Rigidbody3D>()) { r3->velocity.y = a.empty() ? 0.0f : a[0].AsFloat(); r3->WakeUp(); }
            }
            return Value{};
        };
        // ---- Stay-in-bounds helpers (rectangle minX,minY,maxX,maxY) ----
        // keep_in_box: clamp inside. wrap_in_box: teleport to the opposite edge
        // (asteroids). bounce_in_box: reverse the Rigidbody2D velocity at each wall.
        b["keep_in_box"] = [tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || a.size() < 4) return Value{};
            Vec3 p = t->localPosition;
            p.x = Mathf::Clamp(p.x, a[0].AsFloat(), a[2].AsFloat());
            p.y = Mathf::Clamp(p.y, a[1].AsFloat(), a[3].AsFloat());
            t->localPosition = p;
            return Value{};
        };
        b["wrap_in_box"] = [tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || a.size() < 4) return Value{};
            float x0 = a[0].AsFloat(), y0 = a[1].AsFloat(), x1 = a[2].AsFloat(), y1 = a[3].AsFloat();
            Vec3 p = t->localPosition;
            if (p.x < x0) p.x = x1; else if (p.x > x1) p.x = x0;
            if (p.y < y0) p.y = y1; else if (p.y > y1) p.y = y0;
            t->localPosition = p;
            return Value{};
        };
        b["bounce_in_box"] = [go, tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || a.size() < 4) return Value{};
            GameObject* g = go(); auto* rb = g ? g->GetComponent<Rigidbody2D>() : nullptr;
            if (!rb) return Value{};
            Vec3 p = t->localPosition;
            float x0 = a[0].AsFloat(), y0 = a[1].AsFloat(), x1 = a[2].AsFloat(), y1 = a[3].AsFloat();
            if ((p.x <= x0 && rb->velocity.x < 0) || (p.x >= x1 && rb->velocity.x > 0)) rb->velocity.x = -rb->velocity.x;
            if ((p.y <= y0 && rb->velocity.y < 0) || (p.y >= y1 && rb->velocity.y > 0)) rb->velocity.y = -rb->velocity.y;
            return Value{};
        };
        b["add_force"] = [go, arg3](std::vector<Value>& a) {
            if (GameObject* g = go()) {
                if (auto* rb = g->GetComponent<Rigidbody2D>()) rb->AddForce({arg3(a, 0), arg3(a, 1)});
                else if (auto* r3 = g->GetComponent<Rigidbody3D>()) r3->AddForce(Vec3{arg3(a, 0), arg3(a, 1), arg3(a, 2)});
            }
            return Value{};
        };
        b["add_impulse"] = [go, arg3](std::vector<Value>& a) {
            if (GameObject* g = go()) {
                if (auto* rb = g->GetComponent<Rigidbody2D>()) rb->AddImpulse({arg3(a, 0), arg3(a, 1)});
                else if (auto* r3 = g->GetComponent<Rigidbody3D>()) r3->AddImpulse(Vec3{arg3(a, 0), arg3(a, 1), arg3(a, 2)});
            }
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
        // explode([count]): burst particles AND remove this object — the classic death
        // effect in one call (needs a Particle System for the burst).
        b["explode"] = [this, go](std::vector<Value>& a) {
            int n = a.empty() ? 20 : (int)a[0].AsFloat();
            if (GameObject* g = go()) if (auto* ps = g->GetComponent<ParticleSystem>()) ps->Emit(n);
            if (rt.host && rt.host->gameObject && rt.host->gameObject->scene())
                rt.host->gameObject->scene()->Destroy(rt.host->gameObject);
            return Value{};
        };
        // Show/hide this object's graphics (sprite/mesh/text) without deactivating it, so
        // its script keeps running. blink(rate) flashes visibility (invincibility/pickups).
        auto setVis = [go](bool v) {
            if (GameObject* g = go()) {
                if (auto* sr = g->GetComponent<SpriteRenderer>()) sr->enabled = v;
                if (auto* mr = g->GetComponent<MeshRenderer>())   mr->enabled = v;
                if (auto* tr = g->GetComponent<TextRenderer>())   tr->enabled = v;
            }
        };
        b["set_visible"] = [setVis](std::vector<Value>& a) { setVis(a.empty() || a[0].AsBool()); return Value{}; };
        b["show"] = [setVis](std::vector<Value>&) { setVis(true);  return Value{}; };
        b["hide"] = [setVis](std::vector<Value>&) { setVis(false); return Value{}; };
        b["blink"] = [setVis](std::vector<Value>& a) {
            float r = a.empty() ? 6.0f : a[0].AsFloat();
            setVis(Mathf::Sin(Time::ElapsedTime() * r) > 0.0f);
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
            // Optional filters: [, layers [, ignore_triggers]] (as for raycast3).
            std::uint32_t mask = Physics2D::kAllLayers;
            if (a.size() > 5) mask = a[5].IsString() ? Layers::MaskFromList(a[5].AsString())
                                   : (a[5].AsFloat() <= 0.0f ? Physics2D::kAllLayers : (std::uint32_t)a[5].AsFloat());
            bool trig = a.size() > 6 ? !a[6].AsBool() : true;
            RaycastHit2D h = sc->physics().Raycast(*sc, origin, dir, maxDist, nullptr, mask, trig);
            sc->physics().RecordQueryRay(origin, dir, maxDist, h);
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
        // raycast_all(ox, oy, dx, dy [, maxDist [, layers [, ignore_triggers]]]) -> names of
        // everything the 2D ray passes through, nearest first (details: ray_all_*).
        b["raycast_all"] = [this](std::vector<Value>& a) -> Value {
            rt.lastHitAll.clear();
            Value out = Value::MakeArray();
            if (a.size() < 4 || !rt.host || !rt.host->gameObject) return out;
            Scene* sc = rt.host->gameObject->scene();
            if (!sc) return out;
            Vec2 origin{a[0].AsFloat(), a[1].AsFloat()};
            Vec2 dir{a[2].AsFloat(), a[3].AsFloat()};
            float maxDist = a.size() > 4 ? a[4].AsFloat() : 1e9f;
            std::uint32_t mask = Physics2D::kAllLayers;
            if (a.size() > 5) mask = a[5].IsString() ? Layers::MaskFromList(a[5].AsString())
                                   : (a[5].AsFloat() <= 0.0f ? Physics2D::kAllLayers : (std::uint32_t)a[5].AsFloat());
            bool trig = a.size() > 6 ? !a[6].AsBool() : true;
            auto hits = sc->physics().RaycastAll(*sc, origin, dir, maxDist, nullptr, mask, trig);
            sc->physics().RecordQueryRay(origin, dir, maxDist, hits.empty() ? RaycastHit2D{} : hits.front());
            for (const RaycastHit2D& h : hits) {
                Runtime::LastHit l;
                l.hit = true; l.object = h.gameObject ? h.gameObject->name : std::string{};
                l.px = h.point.x; l.py = h.point.y; l.nx = h.normal.x; l.ny = h.normal.y;
                l.dist = h.distance;
                rt.lastHitAll.push_back(l);
                out.AsArray()->push_back(Value{l.object});
            }
            return out;
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
            if (!a.empty() && rt.host && rt.host->gameObject && rt.host->gameObject->scene() &&
                ScriptPathOk(a[0].AsString(), "load_scene"))
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
            if (s && !a.empty()) {
                // A build-list name is fine; a raw path must stay inside the game.
                const std::string name = a[0].AsString();
                if (SceneManager::IndexOf(name) < 0 && !ScriptPathOk(name, "load_scene_name")) return Value{0.0f};
                return Value{SceneManager::LoadSceneByName(*s, name) ? 1.0f : 0.0f};
            }
            return Value{0.0f};
        };
        b["load_next_scene"] = [this](std::vector<Value>&) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            return Value{(s && SceneManager::LoadNextScene(*s)) ? 1.0f : 0.0f};
        };
        // Additively MERGE another scene into the running one (seamless worlds): the
        // current scene is kept and the named scene's objects are added, offset by
        // (x,y[,z]). load_scene_additive("Town", 50, 0) drops the Town chunk 50 units
        // to the right of what's already loaded. Returns 1 if the merge was queued.
        b["load_scene_additive"] = [this](std::vector<Value>& a) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            if (s && !a.empty()) {
                const std::string name = a[0].AsString();
                if (SceneManager::IndexOf(name) < 0 && !ScriptPathOk(name, "load_scene_additive")) return Value{0.0f};
                std::string path = SceneManager::PathForName(name);
                if (path.empty()) return Value{0.0f};
                Vec3 off{0, 0, 0};
                if (a.size() >= 4)      off = {a[1].AsFloat(), a[2].AsFloat(), a[3].AsFloat()};
                else if (a.size() >= 3) off = {a[1].AsFloat(), a[2].AsFloat(), 0.0f};
                s->RequestMerge(path, off);
                return Value{1.0f};
            }
            return Value{0.0f};
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
        // ---- Character / imported-model animation --------------------------
        // The Character and the model's ModelAnimator are found the same way as in
        // visual scripts (AnimControl.hpp): this object, then its children (a
        // player whose model sits under it), then its parents (a script on a part).
        auto selfObj = [this]() -> GameObject* { return rt.host ? rt.host->gameObject : nullptr; };
        auto charSelf = [selfObj]() -> Character* { return FindAnimComponent<Character>(selfObj()); };
        auto modelSelf = [selfObj]() -> ModelAnimator* { return FindAnimComponent<ModelAnimator>(selfObj()); };
        // play_clip holds the clip (locomotion and the humanoid retarget wait) until stop_clip.
        auto warnedNoAnimator = std::make_shared<bool>(false);
        b["play_clip"] = [selfObj, warnedNoAnimator](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            return Value{PlayClipFrom(selfObj(), a[0].AsString(), *warnedNoAnimator) ? 1.0f : 0.0f};
        };
        b["stop_clip"] = [selfObj](std::vector<Value>&) { StopClipFrom(selfObj()); return Value{}; };
        b["playing_clip"] = [charSelf, modelSelf](std::vector<Value>&) {
            Character* c = charSelf();
            if (c) return Value{c->PlayingClip()};
            ModelAnimator* m = modelSelf();
            return Value{m ? m->CurrentName() : std::string{}};
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
        b["clip_speed"] = [selfObj](std::vector<Value>& a) {
            if (!a.empty()) SetClipSpeedFrom(selfObj(), a[0].AsFloat());
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
        b["clip_time"] = [charSelf, modelSelf](std::vector<Value>&) {
            Character* c = charSelf();
            if (c) return Value{c->ClipTime()};
            ModelAnimator* m = modelSelf();
            return Value{m ? m->Time() : 0.0f};
        };
        b["clip_normalized"] = [selfObj](std::vector<Value>&) { return Value{ClipNormalizedFrom(selfObj())}; };
        b["clip_finished"] = [charSelf, modelSelf](std::vector<Value>&) {
            Character* c = charSelf();
            if (c) return Value{c->ClipFinished() ? 1.0f : 0.0f};
            ModelAnimator* m = modelSelf();
            return Value{(m && m->ClipFinished()) ? 1.0f : 0.0f};
        };
        b["clip_duration"] = [charSelf, modelSelf](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            Character* c = charSelf();
            if (c) return Value{c->ClipDuration(a[0].AsString())};
            ModelAnimator* m = modelSelf();
            return Value{m ? m->ClipLength(m->FindClip(a[0].AsString())) : 0.0f};
        };
        b["has_clip"] = [charSelf, modelSelf](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            Character* c = charSelf();
            if (c) return Value{c->HasClip(a[0].AsString()) ? 1.0f : 0.0f};
            ModelAnimator* m = modelSelf();
            return Value{(m && m->FindClip(a[0].AsString()) >= 0) ? 1.0f : 0.0f};
        };
        // Play a clip ONCE (attack / jump / hit reaction) and automatically return
        // to the previous clip — imported models via ModelAnimator; a Character
        // falls back to PlayClip (its clips carry their own once/loop mode).
        b["play_clip_once"] = [charSelf, modelSelf](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            ModelAnimator* m = modelSelf();
            if (m && m->PlayOnce(a[0].AsString())) return Value{1.0f};
            Character* c = charSelf();
            return Value{(c && c->PlayClip(a[0].AsString())) ? 1.0f : 0.0f};
        };
        // Pop the next fired animation event name ("" if none) — footsteps, hit windows.
        // ---- Animation state machine (found like the animators above) ----
        auto smSelf = [selfObj]() -> AnimStateMachine* { return FindAnimComponent<AnimStateMachine>(selfObj()); };
        b["anim_set_float"] = [smSelf](std::vector<Value>& a) {
            if (AnimStateMachine* m = smSelf(); m && a.size() >= 2) m->SetFloat(a[0].AsString(), a[1].AsFloat());
            return Value{};
        };
        b["anim_set_bool"] = [smSelf](std::vector<Value>& a) {
            if (AnimStateMachine* m = smSelf(); m && a.size() >= 2) m->SetBool(a[0].AsString(), a[1].AsFloat() != 0.0f);
            return Value{};
        };
        b["anim_trigger"] = [smSelf](std::vector<Value>& a) {
            if (AnimStateMachine* m = smSelf(); m && !a.empty()) m->SetTrigger(a[0].AsString());
            return Value{};
        };
        // ---- Blend shapes (morph targets) on this object and everything under it ----
        //   blend_shape("Smile", 0.8)   set a shape's weight 0..1 on every mesh that has it
        //   blend_shape_get("Smile")    read it back (0 if no mesh has it)
        auto bsEach = [this](auto fn) {
            GameObject* g = rt.host ? rt.host->gameObject : nullptr;
            if (!g || !g->scene()) return;
            for (const auto& up : g->scene()->Objects())
                if (up && up->IsSelfOrDescendantOf(g))
                    if (auto* bs = up->GetComponent<BlendShapes>()) fn(bs);
        };
        b["blend_shape"] = [bsEach](std::vector<Value>& a) {
            if (a.size() < 2) return Value{0.0f};
            bool any = false;
            const std::string nm = a[0].AsString(); const float w = a[1].AsFloat();
            bsEach([&](BlendShapes* bs) { any = bs->SetWeight(nm, w) || any; });
            return Value{any ? 1.0f : 0.0f};
        };
        b["blend_shape_get"] = [bsEach](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            float w = 0.0f; const std::string nm = a[0].AsString();
            bsEach([&](BlendShapes* bs) { if (bs->Find(nm) >= 0) w = bs->Weight(nm); });
            return Value{w};
        };
        b["anim_state"] = [smSelf](std::vector<Value>&) {
            AnimStateMachine* m = smSelf(); return Value{m ? m->Current() : std::string{}};
        };
        // Previous state, seconds in the current state, and normalized clip time
        // (0..1 per loop; keeps counting past 1 on loops) — for gameplay timing.
        // ---- IK control on this object (any of LimbIK / ChainIK / LookAtIK / AimIK) ----
        //   ik_enable(on)            switch IK on/off (eases over each component's fadeTime)
        //   ik_set_weight(w)         0..1 influence
        //   ik_set_target(x, y, z)   world point to reach / look at / aim at
        //   ik_set_pole(x, y, z)     bend direction point (LimbIK pole / ChainIK pole)
        auto ikEach = [this](auto fn) {
            GameObject* g = rt.host ? rt.host->gameObject : nullptr;
            if (!g) return;
            if (auto* c = g->GetComponent<LimbIK>())   fn(c);
            if (auto* c = g->GetComponent<ChainIK>())  fn(c);
            if (auto* c = g->GetComponent<LookAtIK>()) fn(c);
            if (auto* c = g->GetComponent<AimIK>())    fn(c);
        };
        auto ikToggle = [this](auto fn) {   // ik_enable / ik_set_weight also reach Full Body IK
            GameObject* g = rt.host ? rt.host->gameObject : nullptr;
            if (g) if (auto* c = g->GetComponent<FullBodyIK>()) fn(c);
        };
        b["ik_enable"] = [ikEach, ikToggle](std::vector<Value>& a) {
            bool on = a.empty() || a[0].AsFloat() != 0.0f;
            ikEach([&](auto* c) { c->ikActive = on; });
            ikToggle([&](auto* c) { c->ikActive = on; });
            return Value{};
        };
        b["ik_set_weight"] = [ikEach, ikToggle](std::vector<Value>& a) {
            float w = a.empty() ? 1.0f : a[0].AsFloat();
            ikEach([&](auto* c) { c->weight = w; });
            ikToggle([&](auto* c) { c->weight = w; });
            return Value{};
        };
        b["ik_set_target"] = [ikEach](std::vector<Value>& a) {
            Vec3 t{a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f, a.size() > 2 ? a[2].AsFloat() : 0.0f};
            ikEach([&](auto* c) { c->targetObject = nullptr; c->target = t; });
            return Value{};
        };
        // Full Body IK goals on this object:
        //   ik_hand("left"|"right", x, y, z [, weight])   reach a point (weight 0 releases)
        //   ik_foot("left"|"right", x, y, z [, weight])
        //   ik_look(x, y, z [, weight])                   turn head + chest toward a point
        //   ik_release()                                  drop every goal
        auto fbik = [this]() -> FullBodyIK* {
            GameObject* g = rt.host ? rt.host->gameObject : nullptr;
            return g ? g->GetComponent<FullBodyIK>() : nullptr;
        };
        auto setGoal = [fbik](std::vector<Value>& a, bool hand) {
            FullBodyIK* f = fbik();
            if (!f || a.empty()) return Value{};
            std::string side = a[0].AsString();
            bool left = !side.empty() && (side[0] == 'l' || side[0] == 'L');
            int l = hand ? (left ? FullBodyIK::LeftHand : FullBodyIK::RightHand) : (left ? FullBodyIK::LeftFoot : FullBodyIK::RightFoot);
            FullBodyIK::Effector& e = f->effectors[l];
            e.target = nullptr;
            e.position = {a.size() > 1 ? a[1].AsFloat() : 0.0f, a.size() > 2 ? a[2].AsFloat() : 0.0f, a.size() > 3 ? a[3].AsFloat() : 0.0f};
            float w = a.size() > 4 ? a[4].AsFloat() : 1.0f;
            e.positionWeight = w; e.enabled = w > 0.0f;
            return Value{};
        };
        b["ik_hand"] = [setGoal](std::vector<Value>& a) { return setGoal(a, true); };
        b["ik_foot"] = [setGoal](std::vector<Value>& a) { return setGoal(a, false); };
        b["ik_look"] = [fbik](std::vector<Value>& a) {
            if (FullBodyIK* f = fbik()) {
                f->lookTarget = nullptr;
                f->lookPosition = {a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f, a.size() > 2 ? a[2].AsFloat() : 0.0f};
                f->lookWeight = a.size() > 3 ? a[3].AsFloat() : 1.0f;
                f->lookEnabled = f->lookWeight > 0.0f;
            }
            return Value{};
        };
        // ik_reached("left_hand" | "right_hand" | "left_foot" | "right_foot" [, tolerance]) -> 1 when there.
        b["ik_reached"] = [fbik](std::vector<Value>& a) {
            FullBodyIK* f = fbik();
            if (!f || a.empty()) return Value{0.0f};
            std::string n = a[0].AsString();
            for (auto& ch : n) ch = (char)std::tolower((unsigned char)ch);
            const bool left = n.find("left") != std::string::npos || (!n.empty() && n[0] == 'l');
            const bool foot = n.find("foot") != std::string::npos;
            int l = foot ? (left ? FullBodyIK::LeftFoot : FullBodyIK::RightFoot) : (left ? FullBodyIK::LeftHand : FullBodyIK::RightHand);
            return Value{f->Reached(l, a.size() > 1 ? a[1].AsFloat() : 0.05f) ? 1.0f : 0.0f};
        };
        b["ik_release"] = [fbik](std::vector<Value>&) {
            if (FullBodyIK* f = fbik()) { for (auto& e : f->effectors) e.enabled = false; f->lookEnabled = false; }
            return Value{};
        };
        // Procedural Object Holding on this object:
        //   hold_pickup(name) / hold_nearest([range])   take an object into the hands -> 1 if held
        //                                               (range defaults to the component's Pickup Range)
        //   hold_drop()                                 let go of it
        //   hold_object()                               name of what's held ("" = nothing)
        auto holder = [this]() -> ProceduralObjectHolding* {
            GameObject* g = rt.host ? rt.host->gameObject : nullptr;
            return g ? g->GetComponent<ProceduralObjectHolding>() : nullptr;
        };
        b["hold_pickup"] = [this, holder](std::vector<Value>& a) {
            ProceduralObjectHolding* h = holder();
            Scene* sc = rt.host && rt.host->gameObject ? rt.host->gameObject->scene() : nullptr;
            GameObject* obj = (sc && !a.empty()) ? sc->Find(a[0].AsString()) : nullptr;
            return Value{h && h->Pickup(obj) ? 1.0f : 0.0f};
        };
        b["hold_nearest"] = [holder](std::vector<Value>& a) {
            ProceduralObjectHolding* h = holder();
            return Value{h && h->PickupNearest(a.empty() ? -1.0f : a[0].AsFloat()) ? 1.0f : 0.0f};
        };
        b["hold_drop"] = [holder](std::vector<Value>&) {
            if (ProceduralObjectHolding* h = holder()) h->Drop();
            return Value{};
        };
        b["hold_object"] = [holder](std::vector<Value>&) {
            ProceduralObjectHolding* h = holder();
            return Value{h && h->Held() ? h->Held()->name : std::string()};
        };
        b["ik_set_pole"] = [this](std::vector<Value>& a) {
            Vec3 p{a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f, a.size() > 2 ? a[2].AsFloat() : 0.0f};
            if (GameObject* g = rt.host ? rt.host->gameObject : nullptr) {
                if (auto* l = g->GetComponent<LimbIK>())  { l->poleObject = nullptr; l->pole = p - (l->upper ? l->upper->Position() : Vec3{0, 0, 0}); }
                if (auto* c = g->GetComponent<ChainIK>()) { c->poleObject = nullptr; c->usePole = true; c->pole = p; }
            }
            return Value{};
        };
        b["anim_prev_state"] = [smSelf](std::vector<Value>&) {
            AnimStateMachine* m = smSelf(); return Value{m ? m->Previous() : std::string{}};
        };
        b["anim_state_time"] = [smSelf](std::vector<Value>&) {
            AnimStateMachine* m = smSelf(); return Value{m ? m->StateTime() : 0.0f};
        };
        b["anim_normalized_time"] = [smSelf](std::vector<Value>&) {
            AnimStateMachine* m = smSelf(); return Value{m ? m->NormalizedTime() : 0.0f};
        };
        b["anim_goto"] = [smSelf](std::vector<Value>& a) {
            AnimStateMachine* m = smSelf();
            return Value{(m && !a.empty() && m->GoTo(a[0].AsString())) ? 1.0f : 0.0f};
        };
        b["anim_event"] = [charSelf, modelSelf](std::vector<Value>&) {
            if (Character* c = charSelf()) {
                std::string n = c->NextAnimEvent();
                if (!n.empty()) return Value{n};
            }
            ModelAnimator* m = modelSelf();       // imported-model clip events
            return Value{m ? m->NextAnimEvent() : std::string{}};
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
        // Launcher integration (Steam-style): was this run started to join a friend
        // / invite / server? (Skip your menu and go straight to the match.)
        b["net_launch_join"] = [](std::vector<Value>&) {
            return Value{NetworkManager::LaunchedToJoin() ? 1.0f : 0.0f};
        };
        // Join from an okay://join link (e.g. one a player pasted).
        b["net_join_link"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            if (!n || a.empty()) return Value{0.0f};
            return Value{n->JoinByLink(a[0].AsString()) ? 1.0f : 0.0f};
        };
        // In-game server browser (filtered to this game; refreshes in the background).
        // net_browser_open() shows the built-in "Find a server" window; the rest let a
        // game draw its own list.
        b["net_browser_open"]  = [](std::vector<Value>&) { ServerBrowser::Shared().windowOpen = true; return Value{1.0f}; };
        b["net_browser_close"] = [](std::vector<Value>&) { ServerBrowser::Shared().windowOpen = false; return Value{1.0f}; };
        b["net_browser_is_open"] = [](std::vector<Value>&) { return Value{ServerBrowser::Shared().windowOpen ? 1.0f : 0.0f}; };
        b["net_browser_refresh"] = [](std::vector<Value>& a) {   // ["all" | "internet" | "lan"]
            const std::string w = a.empty() ? std::string("all") : a[0].AsString();
            ServerBrowser::Shared().Refresh(w == "internet" ? ServerBrowser::kInternet : w == "lan" ? ServerBrowser::kLan : ServerBrowser::kAll);
            return Value{1.0f};
        };
        b["net_browser_busy"]  = [](std::vector<Value>&) { return Value{ServerBrowser::Shared().Busy() ? 1.0f : 0.0f}; };
        b["net_browser_count"] = [](std::vector<Value>&) { return Value{(float)ServerBrowser::Shared().Results().size()}; };
        auto browserAt = [](std::vector<Value>& a, ServerBrowser::Entry& out) {
            if (a.empty()) return false;
            const auto r = ServerBrowser::Shared().Results(); const int i = (int)a[0].AsFloat();
            if (i < 0 || i >= (int)r.size()) return false;
            out = r[i]; return true;
        };
        b["net_browser_name"] = [browserAt](std::vector<Value>& a) { ServerBrowser::Entry e; return Value{browserAt(a, e) ? (e.info.name.empty() ? e.info.Address() : e.info.name) : std::string{}}; };
        b["net_browser_map"]  = [browserAt](std::vector<Value>& a) { ServerBrowser::Entry e; return Value{browserAt(a, e) ? e.info.map : std::string{}}; };
        b["net_browser_address"] = [browserAt](std::vector<Value>& a) { ServerBrowser::Entry e; return Value{browserAt(a, e) ? e.info.Address() : std::string{}}; };
        b["net_browser_players"] = [browserAt](std::vector<Value>& a) { ServerBrowser::Entry e; return Value{browserAt(a, e) ? (float)e.info.players : 0.0f}; };
        b["net_browser_max"]  = [browserAt](std::vector<Value>& a) { ServerBrowser::Entry e; return Value{browserAt(a, e) ? (float)e.info.maxPlayers : 0.0f}; };
        b["net_browser_ping"] = [browserAt](std::vector<Value>& a) { ServerBrowser::Entry e; return Value{browserAt(a, e) ? (float)e.info.pingMs : -1.0f}; };
        b["net_browser_locked"] = [browserAt](std::vector<Value>& a) { ServerBrowser::Entry e; return Value{browserAt(a, e) && e.info.password ? 1.0f : 0.0f}; };
        b["net_browser_join"] = [this, browserAt](std::vector<Value>& a) {   // (index[, password])
            ServerBrowser::Entry e;
            NetworkManager* n = EnsureNet();
            if (!n || !browserAt(a, e)) return Value{0.0f};
            JoinLink l = e.Link();
            if (a.size() > 1) l.password = a[1].AsString();
            ServerBrowser::Shared().windowOpen = false;
            return Value{n->JoinByLink(l.Format()) ? 1.0f : 0.0f};
        };

        // Scores (server-owned, replicated to everyone in the room) + the roster.
        b["net_set_score"] = [this](std::vector<Value>& a) {   // server only: (id, score)
            NetworkManager* n = Net();
            if (!n || a.size() < 2 || !n->IsServer()) return Value{0.0f};
            n->SetPeerScore((std::uint32_t)(int)a[0].AsFloat(), (int)a[1].AsFloat());
            return Value{1.0f};
        };
        b["net_score"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            return Value{(n && !a.empty()) ? (float)n->PeerScore((std::uint32_t)(int)a[0].AsFloat()) : 0.0f};
        };
        b["net_peer_name"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            return Value{(n && !a.empty()) ? n->PeerName((std::uint32_t)(int)a[0].AsFloat()) : std::string{}};
        };
        b["net_roster_count"] = [this](std::vector<Value>&) {
            NetworkManager* n = Net();
            return Value{n ? (float)n->Roster().size() : 0.0f};
        };
        b["net_roster_id"] = [this](std::vector<Value>& a) {   // (index) -> peer id, -1 if out of range
            NetworkManager* n = Net();
            if (!n || a.empty()) return Value{-1.0f};
            const auto r = n->Roster(); const int i = (int)a[0].AsFloat();
            return Value{(i >= 0 && i < (int)r.size()) ? (float)r[i].id : -1.0f};
        };
        // ---- Matchmaking queues (docs/sql/matchmaking.sql) ----
        // net_queue_join("duel", 2[, minPlayers[, "EU"]]) puts this player (and their
        // party, if they lead one) in the queue; when a match is found the game joins
        // it — a dedicated server's room, or a player who hosts. The player shows a
        // "Finding a match" banner with Cancel; scripts can draw their own with these.
        b["net_queue_join"] = [this](std::vector<Value>& a) {
            EnsureNet();   // the found match needs a NetworkManager to connect
            MatchRequest r;
            r.mode = a.size() > 0 ? a[0].AsString() : std::string();
            r.players = a.size() > 1 ? (int)a[1].AsFloat() : 2;
            r.minPlayers = a.size() > 2 ? (int)a[2].AsFloat() : 0;
            r.region = a.size() > 3 ? a[3].AsString() : std::string();
            return Value{Matchmaker::Shared().Start(r) ? 1.0f : 0.0f};
        };
        b["net_queue_cancel"] = [](std::vector<Value>&) { Matchmaker::Shared().Cancel(); return Value{1.0f}; };
        // "idle" | "searching" | "waiting" (found; the host is starting) | "found" | "failed"
        b["net_queue_state"] = [](std::vector<Value>&) {
            return Value{std::string(Matchmaker::StateName(Matchmaker::Shared().GetState()))};
        };
        b["net_queue_time"]  = [](std::vector<Value>&) { return Value{Matchmaker::Shared().Elapsed()}; };
        b["net_queue_count"] = [](std::vector<Value>&) { return Value{(float)Matchmaker::Shared().Searching()}; };
        b["net_queue_error"] = [](std::vector<Value>&) { return Value{Matchmaker::Shared().Error()}; };
        b["net_queue_is_host"] = [](std::vector<Value>&) { return Value{Matchmaker::Shared().Assignment().isHost ? 1.0f : 0.0f}; };
        // Turn off auto-connect to show "Match found — Accept"; net_queue_accept() then connects.
        b["net_queue_auto"] = [](std::vector<Value>& a) {
            if (!a.empty()) Matchmaker::Shared().autoConnect = a[0].AsFloat() != 0.0f;
            return Value{Matchmaker::Shared().autoConnect ? 1.0f : 0.0f};
        };
        b["net_queue_accept"] = [this](std::vector<Value>&) {
            NetworkManager* n = EnsureNet();
            return Value{(n && Matchmaker::Shared().Connect(n)) ? 1.0f : 0.0f};
        };

        // ---- Matches: results -> stats + leaderboards (docs/sql/matches.sql) ----
        // Server: net_match_start() zeroes scores and starts the clock;
        // net_match_end([winner ids...]) reports everyone's score / place / win
        // (winners default to first place) when the server is signed in.
        b["net_match_start"] = [this](std::vector<Value>&) {
            NetworkManager* n = Net();
            if (!n || !n->IsServer()) return Value{0.0f};
            n->BeginMatch();
            return Value{1.0f};
        };
        b["net_match_end"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            if (!n || !n->IsServer()) return Value{0.0f};
            std::vector<std::uint32_t> winners;
            for (auto& v : a) {
                if (auto arr = v.AsArray()) { for (auto& w : *arr) winners.push_back((std::uint32_t)(int)w.AsFloat()); }
                else winners.push_back((std::uint32_t)(int)v.AsFloat());
            }
            Scene* sc = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            return Value{EndMatchAndReport(*n, winners, std::string(), sc ? sc->Name() : std::string()) ? 1.0f : 0.0f};
        };
        b["net_match_seconds"] = [this](std::vector<Value>&) {
            NetworkManager* n = Net();
            return Value{n ? n->MatchSeconds() : 0.0f};
        };
        // "" until a report finished, then "ok" or why it failed.
        b["net_match_status"] = [](std::vector<Value>&) { return Value{LastMatchReportStatus()}; };
        // Leaderboards from match results: net_stats_top("wins"|"score"|"best"|"matches"|"time", n)
        // -> ["1,maya,42", ...]; net_stats_mine(stat) -> my value. Optional last arg: another game's title.
        b["net_stats_top"] = [this](std::vector<Value>& a) {
            Value v = Value::MakeArray();
            auto arr = v.AsArray();
            if (!arr || !Account::Exists() || !Account::IsLoggedIn()) return v;
            const std::string stat = a.empty() ? std::string("wins") : a[0].AsString();
            const int count = a.size() > 1 ? std::max(1, (int)a[1].AsFloat()) : 10;
            std::string game = a.size() > 2 ? a[2].AsString() : std::string();
            if (game.empty()) { NetworkManager* n = Net(); game = n && !n->gameId.empty() ? n->gameId : ServerBrowser::Shared().Game(); }
            for (const auto& e : MatchResults(Account::Get()).Leaderboard(game, stat, count)) {
                const long val = stat == "score" ? e.totalScore : stat == "best" ? e.bestScore : stat == "matches" ? e.matches
                               : stat == "time" ? e.seconds : e.wins;
                arr->push_back(Value{std::to_string(e.rank) + "," + e.Name() + "," + std::to_string(val)});
            }
            return v;
        };
        b["net_stats_mine"] = [this](std::vector<Value>& a) {
            if (!Account::Exists() || !Account::IsLoggedIn()) return Value{0.0f};
            const std::string stat = a.empty() ? std::string("wins") : a[0].AsString();
            std::string game = a.size() > 1 ? a[1].AsString() : std::string();
            if (game.empty()) { NetworkManager* n = Net(); game = n && !n->gameId.empty() ? n->gameId : ServerBrowser::Shared().Game(); }
            PlayerStats e;
            if (!MatchResults(Account::Get()).MyStats(game, e)) return Value{0.0f};
            if (stat == "rank") return Value{(float)e.rank};
            const long val = stat == "score" ? e.totalScore : stat == "best" ? e.bestScore : stat == "matches" ? e.matches
                           : stat == "time" ? e.seconds : e.wins;
            return Value{(float)val};
        };
        // Server only: move a player (respawn / teleport) — passes the speed check.
        b["net_teleport"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            if (!n || a.size() < 3 || !n->IsServer()) return Value{0.0f};
            n->ServerTeleport((std::uint32_t)(int)a[0].AsFloat(),
                              Vec3(a[1].AsFloat(), a[2].AsFloat(), a.size() > 3 ? a[3].AsFloat() : 0.0f));
            return Value{1.0f};
        };
        // Server only: anti-cheat basics. (max units/second, 0 = off) / (messages per second, 0 = off).
        b["net_max_speed"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet(); if (n && !a.empty()) n->maxMoveSpeed = a[0].AsFloat();
            return Value{n ? n->maxMoveSpeed : 0.0f};
        };
        b["net_rate_limit"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet(); if (n && !a.empty()) n->maxMessagesPerSecond = (int)a[0].AsFloat();
            return Value{n ? (float)n->maxMessagesPerSecond : 0.0f};
        };
        // Server only: the anti-cheat (see okay/Net/AntiCheat.hpp). net_anticheat([on])
        // reads / sets whether it's on; net_anticheat_load(path) applies an .okayac
        // file from the game's folders (true if it loaded); net_violations(peer)
        // is that peer's refusals this minute.
        b["net_anticheat"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet(); if (n && !a.empty()) n->anticheat.settings.enabled = a[0].AsBool();
            return Value{n ? n->anticheat.settings.enabled : false};
        };
        b["net_anticheat_load"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            if (!n || a.empty() || !ScriptPathOk(a[0].AsString(), "net_anticheat_load")) return Value{false};
            AntiCheatSettings s; std::string err;
            if (!s.LoadFile(a[0].AsString(), &err)) { Log::Error("net_anticheat_load: ", err); return Value{false}; }
            n->anticheat.settings = s;
            return Value{true};
        };
        b["net_violations"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            return Value{n && !a.empty() ? (float)n->anticheat.Violations((std::uint32_t)a[0].AsFloat()) : 0.0f};
        };
        // Claim that this peer's shot hit a networked object (its sync id), fired
        // from (ox, oy, oz) along (dx, dy, dz). The server checks it and applies
        // the damage; on the host it's applied at once.
        b["net_hit"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            if (!n || a.size() < 8) return Value{};
            n->ClaimHit(a[0].AsString(), a[1].AsString(), Vec3(a[2].AsFloat(), a[3].AsFloat(), a[4].AsFloat()),
                        Vec3(a[5].AsFloat(), a[6].AsFloat(), a[7].AsFloat()));
            return Value{};
        };
        // Server: what's left of a player's budget for RPC `name` (a Net Validator
        // with a budget: ammo); a third argument adds to it first (a reload, a
        // pickup; negative takes away). -1 if no rule has a budget for it.
        b["net_budget"] = [this](std::vector<Value>& a) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            if (!s || a.size() < 2) return Value{-1.0f};
            NetValidator* nv = NetValidator::BudgetFor(*s, a[1].AsString());
            if (!nv) return Value{-1.0f};
            const auto peer = (std::uint32_t)a[0].AsFloat();
            return Value{a.size() >= 3 ? nv->TopUp(peer, a[2].AsFloat()) : nv->Budget(peer)};
        };
        // Open the OkaySpace launcher's "Invite friends" window for this game.
        b["net_invite_friends"] = [](std::vector<Value>&) {
#if !defined(__EMSCRIPTEN__)
            return Value{GameIpc::RequestInvite() ? 1.0f : 0.0f};
#else
            return Value{0.0f};
#endif
        };
        // 1 once the relay has paired this peer (host wired up / slot assigned).
        // The fixed network tick (0 without one) and its rate; net_tick_rate(hz)
        // sets the host's rate before it starts (0 = off, as before).
        b["net_tick"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? (float)n->NetTick() : 0.0f}; };
        b["net_tick_rate"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            if (!n) return Value{0.0f};
            if (!a.empty()) n->netTickRate = std::clamp(a[0].AsFloat(), 0.0f, 240.0f);
            return Value{n->NetTickRate() > 0.0f ? n->NetTickRate() : n->netTickRate};
        };
        // Authoritative movement: the object this player's inputs drive
        // (client), and the host's switch.
        b["net_set_pawn"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            if (!n || !s || a.empty()) return Value{0.0f};
            GameObject* go = a[0].AsString().empty() ? rt.host->gameObject : s->Find(a[0].AsString());
            n->SetPawn(go);
            return Value{go ? 1.0f : 0.0f};
        };
        b["net_authoritative"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            if (!n) return Value{0.0f};
            if (!a.empty()) n->authoritativeMovement = a[0].AsFloat() != 0.0f;
            return Value{n->AuthoritativeMovement() || n->authoritativeMovement ? 1.0f : 0.0f};
        };
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
        // Client: pin the host's identity key (base64url, the key a dedicated
        // server prints for --host-key); "" removes the pin. 1 if it's well formed.
        // A pinned client joins only that host, over the sealed join.
        b["net_pin_host"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            if (!n || a.empty()) return Value{0.0f};
            const std::string key = a[0].AsString();
            std::vector<std::uint8_t> raw;
            if (!key.empty() && (!net::SessionTicket::Base64UrlDecode(key, raw) || raw.size() != 32)) return Value{0.0f};
            n->pinnedHostKey = key;
            return Value{1.0f};
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
        // Integer stats (kills, wins, ...). Floats lose precision past 2^24, so
        // counters that climb high belong here rather than in steam_*_stat.
        b["steam_set_stat_int"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) Steam::Get().SetStatInt(a[0].AsString(), (int)a[1].AsFloat());
            return Value{};
        };
        b["steam_get_stat_int"] = [](std::vector<Value>& a) {
            return Value{a.empty() ? 0.0f : (float)Steam::Get().GetStatInt(a[0].AsString())};
        };
        b["steam_inc_stat_int"] = [](std::vector<Value>& a) {
            return Value{a.size() < 2 ? 0.0f : (float)Steam::Get().IncrementStatInt(a[0].AsString(), (int)a[1].AsFloat())};
        };
        // Achievement name by index (0..steam_achievement_count-1).
        b["steam_achievement_name"] = [](std::vector<Value>& a) {
            return Value{a.empty() ? std::string{} : Steam::Get().AchievementName((int)a[0].AsFloat())};
        };
        // Read back a rich-presence value previously set with steam_presence.
        b["steam_get_presence"] = [](std::vector<Value>& a) {
            return Value{a.empty() ? std::string{} : Steam::Get().GetRichPresence(a[0].AsString())};
        };
        // ---- Friends -------------------------------------------------------
        // Big Steam ids exceed float precision, so friend/lobby ids are passed
        // as strings in and out of these builtins.
        b["steam_friend_name"] = [](std::vector<Value>& a) {
            return Value{a.empty() ? std::string{} : Steam::Get().FriendName((int)a[0].AsFloat())};
        };
        b["steam_friend_id"] = [](std::vector<Value>& a) {
            return Value{a.empty() ? std::string{} : std::to_string(Steam::Get().FriendId((int)a[0].AsFloat()))};
        };
        b["steam_invite"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            std::uint64_t fid = (std::uint64_t)std::strtoull(a[0].AsString().c_str(), nullptr, 10);
            return Value{Steam::Get().InviteFriend(fid) ? 1.0f : 0.0f};
        };
        // ---- Lobbies (Steam matchmaking) -----------------------------------
        b["steam_lobby_create"] = [](std::vector<Value>& a) {
            int maxm = a.empty() ? 4 : (int)a[0].AsFloat();
            std::string name = a.size() > 1 ? a[1].AsString() : std::string{};
            return Value{std::to_string(Steam::Get().CreateLobby(maxm, name))};
        };
        b["steam_lobby_join"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            std::uint64_t id = (std::uint64_t)std::strtoull(a[0].AsString().c_str(), nullptr, 10);
            return Value{Steam::Get().JoinLobby(id) ? 1.0f : 0.0f};
        };
        b["steam_lobby_leave"] = [](std::vector<Value>&) { Steam::Get().LeaveLobby(); return Value{}; };
        b["steam_lobby_current"] = [](std::vector<Value>&) { return Value{std::to_string(Steam::Get().CurrentLobby())}; };
        b["steam_lobby_set_data"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) Steam::Get().SetLobbyData(a[0].AsString(), a[1].AsString());
            return Value{};
        };
        b["steam_lobby_get_data"] = [](std::vector<Value>& a) {
            if (a.size() < 2) return Value{std::string{}};
            std::uint64_t id = (std::uint64_t)std::strtoull(a[0].AsString().c_str(), nullptr, 10);
            return Value{Steam::Get().GetLobbyData(id, a[1].AsString())};
        };
        // Open lobbies as an array of "id,name,members,max,joinable" strings.
        b["steam_lobby_list"] = [](std::vector<Value>&) {
            Value v = Value::MakeArray(); auto arr = v.AsArray();
            if (arr) for (auto& l : Steam::Get().LobbyList())
                arr->push_back(Value{std::to_string(l.id) + "," + l.name + "," +
                                     std::to_string(l.memberCount) + "," + std::to_string(l.maxMembers) + "," +
                                     (l.joinable ? "1" : "0")});
            return v;
        };
        // Member display names in a lobby, as a string array.
        b["steam_lobby_members"] = [](std::vector<Value>& a) {
            Value v = Value::MakeArray(); auto arr = v.AsArray();
            if (arr && !a.empty()) {
                std::uint64_t id = (std::uint64_t)std::strtoull(a[0].AsString().c_str(), nullptr, 10);
                for (auto& n : Steam::Get().LobbyMembers(id)) arr->push_back(Value{n});
            }
            return v;
        };
        // ---- PlayFab (cloud login / leaderboards / saves; blocking HTTP) ----
        // Mirrors the playfab_* visual-scripting instructions. Call pf_login once
        // (e.g. in start()), not per frame — each call is a real web request.
        b["pf_login"] = [](std::vector<Value>& a) {           // pf_login(titleId, customId)
            if (a.size() < 2) return Value{0.0f};
            PlayFab::Get().Configure(a[0].AsString());
            return Value{PlayFab::Get().LoginWithCustomID(a[1].AsString()) ? 1.0f : 0.0f};
        };
        b["pf_login_password"] = [](std::vector<Value>& a) {  // pf_login_password(titleId, user, pass)
            if (a.size() < 3) return Value{0.0f};
            PlayFab::Get().Configure(a[0].AsString());
            return Value{PlayFab::Get().LoginWithPassword(a[1].AsString(), a[2].AsString()) ? 1.0f : 0.0f};
        };
        b["pf_register"] = [](std::vector<Value>& a) {        // pf_register(titleId, user, pass)
            if (a.size() < 3) return Value{0.0f};
            PlayFab::Get().Configure(a[0].AsString());
            return Value{PlayFab::Get().RegisterWithPassword(a[1].AsString(), a[2].AsString()) ? 1.0f : 0.0f};
        };
        b["pf_logged_in"] = [](std::vector<Value>&) { return Value{PlayFab::Get().IsLoggedIn() ? 1.0f : 0.0f}; };
        b["pf_id"]        = [](std::vector<Value>&) { return Value{PlayFab::Get().PlayFabId()}; };
        b["pf_error"]     = [](std::vector<Value>&) { return Value{PlayFab::Get().LastError()}; };
        b["pf_name"] = [](std::vector<Value>& a) {            // leaderboard display name
            return Value{(!a.empty() && PlayFab::Get().SetDisplayName(a[0].AsString())) ? 1.0f : 0.0f};
        };
        b["pf_set_stat"] = [](std::vector<Value>& a) {        // pf_set_stat(stat, value)
            return Value{(a.size() >= 2 &&
                          PlayFab::Get().SetStat(a[0].AsString(), (int)a[1].AsFloat())) ? 1.0f : 0.0f};
        };
        // Top-N entries as an array of "rank,name,score" strings (Steam parity).
        b["pf_leaderboard_top"] = [](std::vector<Value>& a) {
            Value v = Value::MakeArray();
            auto arr = v.AsArray();
            if (!a.empty() && arr) {
                int n = a.size() > 1 ? (int)a[1].AsFloat() : 10;
                std::vector<PlayFab::Entry> rows;
                if (PlayFab::Get().GetLeaderboard(a[0].AsString(), n, rows))
                    for (auto& e : rows)
                        arr->push_back(Value{std::to_string(e.position + 1) + "," + e.name + "," +
                                             std::to_string(e.value)});
            }
            return v;
        };
        b["pf_save"] = [](std::vector<Value>& a) {            // pf_save(key, value)
            return Value{(a.size() >= 2 &&
                          PlayFab::Get().SetUserData(a[0].AsString(), a[1].AsString())) ? 1.0f : 0.0f};
        };
        b["pf_load"] = [](std::vector<Value>& a) {            // pf_load(key) -> text
            std::string v2;
            if (!a.empty()) PlayFab::Get().GetUserData(a[0].AsString(), v2);
            return Value{v2};
        };
        b["pf_delete"] = [](std::vector<Value>& a) {
            return Value{(!a.empty() && PlayFab::Get().DeleteUserData(a[0].AsString())) ? 1.0f : 0.0f};
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
            if (!ScriptPathOk(a[0].AsString(), "data_num")) return Value{(float)def};
            return Value{(float)DataAsset::Cached(a[0].AsString()).GetNumber(a[1].AsString(), def)};
        };
        b["data_str"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{std::string{}};
            std::string def = a.size() > 2 ? a[2].AsString() : std::string{};
            if (!ScriptPathOk(a[0].AsString(), "data_str")) return Value{def};
            return Value{DataAsset::Cached(a[0].AsString()).GetString(a[1].AsString(), def)};
        };
        b["data_has"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{false};
            if (!ScriptPathOk(a[0].AsString(), "data_has")) return Value{false};
            return Value{DataAsset::Cached(a[0].AsString()).Has(a[1].AsString())};
        };
        // Customize a data asset from code (then data_save to persist it).
        b["data_set"] = [](std::vector<Value>& a) {
            if (a.size() >= 3 && ScriptPathOk(a[0].AsString(), "data_set"))
                DataAsset::Cached(a[0].AsString()).Set(a[1].AsString(), a[2].AsString());
            return Value{};
        };
        b["data_save"] = [](std::vector<Value>& a) -> Value {
            if (a.empty() || !ScriptPathOk(a[0].AsString(), "data_save")) return Value{false};
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
        // Into the game's save folder; loading falls back to the old spot beside the game.
        b["prefs_save"] = [](std::vector<Value>& a) {
            const std::string name = a.empty() ? "game.okayprefs" : a[0].AsString();
            if (!ScriptPathOk(name, "prefs_save")) return Value{false};
            return Value{Prefs::Save(Save::Resolve(name))};
        };
        b["prefs_load"] = [](std::vector<Value>& a) {
            const std::string name = a.empty() ? "game.okayprefs" : a[0].AsString();
            if (!ScriptPathOk(name, "prefs_load")) return Value{false};
            return Value{Prefs::Load(ScriptReadPath(name))};
        };

        // Easy-Save-3-style save system: typed values, many files, write-through.
        //   save("coins", 10)                 -> default file
        //   save("spawn", new Vector3(1,2,3), "slot1.okaysave")
        //   load("coins", 0)  /  load("name", "Hero")  /  load("spawn", vec)
        // The 3rd arg (file) is optional everywhere; values keep their type.
        // The file argument, or "" when it would escape the save folder (refused + logged).
        auto saveFile = [](std::vector<Value>& a, size_t idx) -> std::string {
            std::string f = (a.size() > idx && a[idx].IsString()) ? a[idx].AsString() : Save::DefaultFile();
            return ScriptPathOk(f, "save file") ? f : std::string();
        };
        b["save"] = [saveFile](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{false};
            std::string key = a[0].AsString(), file = saveFile(a, 2);
            if (file.empty()) return Value{false};
            if (a[1].IsVec3())        Save::SetVec3(key, a[1].AsVec3(), file);
            else if (a[1].IsString()) Save::SetString(key, a[1].AsString(), file);
            else                      Save::SetFloat(key, a[1].AsFloat(), file);
            return Value{true};
        };
        b["load"] = [saveFile](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{0.0f};
            std::string key = a[0].AsString(), file = saveFile(a, 2);
            if (file.empty()) return a.size() > 1 ? a[1] : Value{0.0f};
            if (a.size() > 1 && a[1].IsVec3())   return Value{Save::GetVec3(key, a[1].AsVec3(), file)};
            if (a.size() > 1 && a[1].IsString()) return Value{Save::GetString(key, a[1].AsString(), file)};
            float def = a.size() > 1 ? a[1].AsFloat() : 0.0f;
            return Value{Save::GetFloat(key, def, file)};
        };
        b["save_has"] = [saveFile](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{false};
            const std::string file = saveFile(a, 1);
            return Value{!file.empty() && Save::Has(a[0].AsString(), file)};
        };
        b["save_delete"] = [saveFile](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{};
            const std::string file = saveFile(a, 1);
            if (!file.empty()) Save::Delete(a[0].AsString(), file);
            return Value{};
        };
        b["save_clear"] = [saveFile](std::vector<Value>& a) -> Value {
            const std::string file = saveFile(a, 0);
            if (!file.empty()) Save::Clear(file);
            return Value{};
        };
        b["save_exists"] = [saveFile](std::vector<Value>& a) -> Value {
            const std::string file = saveFile(a, 0);
            return Value{!file.empty() && Save::FileExists(file)};
        };
        b["save_delete_file"] = [saveFile](std::vector<Value>& a) -> Value {
            const std::string file = saveFile(a, 0);
            return Value{!file.empty() && Save::DeleteFile(file)};
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
        // ---- Bitwise / integer helpers (flags, masks, tile ids, packing) ------
        // Values are treated as 32-bit integers. Handy for bit flags and tilemaps.
        auto I = [](std::vector<Value>& a, std::size_t i) -> std::int32_t {
            return i < a.size() ? (std::int32_t)a[i].AsFloat() : 0;
        };
        b["bit_and"] = [I](std::vector<Value>& a) { return Value{(float)(I(a,0) & I(a,1))}; };
        b["bit_or"]  = [I](std::vector<Value>& a) { return Value{(float)(I(a,0) | I(a,1))}; };
        b["bit_xor"] = [I](std::vector<Value>& a) { return Value{(float)(I(a,0) ^ I(a,1))}; };
        b["bit_not"] = [I](std::vector<Value>& a) { return Value{(float)(~I(a,0))}; };
        b["shl"]     = [I](std::vector<Value>& a) { return Value{(float)(I(a,0) << (I(a,1) & 31))}; };
        b["shr"]     = [I](std::vector<Value>& a) { return Value{(float)(I(a,0) >> (I(a,1) & 31))}; };
        // Set / clear / toggle / test a single bit (bit index 0..31).
        b["bit_set"]    = [I](std::vector<Value>& a) { return Value{(float)(I(a,0) |  (1 << (I(a,1) & 31)))}; };
        b["bit_clear"]  = [I](std::vector<Value>& a) { return Value{(float)(I(a,0) & ~(1 << (I(a,1) & 31)))}; };
        b["bit_toggle"] = [I](std::vector<Value>& a) { return Value{(float)(I(a,0) ^  (1 << (I(a,1) & 31)))}; };
        b["bit_test"]   = [I](std::vector<Value>& a) { return Value{(I(a,0) & (1 << (I(a,1) & 31))) != 0}; };
        // hex(n) -> "0x1f" ; parse_int("ff", 16) -> 255 (base defaults to 10).
        b["hex"] = [I](std::vector<Value>& a) {
            char buf[24]; std::snprintf(buf, sizeof(buf), "0x%x", (unsigned)I(a, 0));
            return Value{std::string(buf)};
        };
        b["parse_int"] = [](std::vector<Value>& a) {
            std::string s = a.empty() ? "" : a[0].AsString();
            int base = a.size() > 1 ? (int)a[1].AsFloat() : 10;
            return Value{(float)std::strtol(s.c_str(), nullptr, base)};
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
        // Current local rotation as Euler degrees (vec3).
        b["rotation_euler"] = [tf](std::vector<Value>&) {
            Transform* t = tf();
            return Value{t ? t->localRotation.ToEuler() : Vec3::Zero};
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
        // stop(): zero this object's Rigidbody velocity (halt in place).
        b["stop"] = [go](std::vector<Value>&) {
            if (GameObject* g = go()) {
                if (auto* rb = g->GetComponent<Rigidbody2D>()) rb->velocity = {0.0f, 0.0f};
                if (auto* rb3 = g->GetComponent<Rigidbody3D>()) rb3->velocity = {0.0f, 0.0f, 0.0f};
            }
            return Value{};
        };
        // is_moving(): true when the Rigidbody2D is moving (for anim/state).
        b["is_moving"] = [go](std::vector<Value>&) -> Value {
            if (GameObject* g = go())
                if (auto* rb = g->GetComponent<Rigidbody2D>())
                    return Value{(rb->velocity.x * rb->velocity.x + rb->velocity.y * rb->velocity.y) > 1e-4f};
            return Value{false};
        };
        // face_velocity(): rotate (Z) to point the way the Rigidbody2D is moving.
        b["face_velocity"] = [go, tf](std::vector<Value>&) {
            GameObject* g = go(); Transform* t = tf(); if (!g || !t) return Value{};
            if (auto* rb = g->GetComponent<Rigidbody2D>()) {
                float vx = rb->velocity.x, vy = rb->velocity.y;
                if (vx * vx + vy * vy > 1e-6f)
                    t->localRotation = Quat::Euler({0, 0, std::atan2(vy, vx) * 57.2957795f});
            }
            return Value{};
        };
        // cooldown("name", secs): true (and starts the timer) only when at least
        // `secs` have passed since it last returned true — shooting, dashes, abilities.
        //   if (cooldown("shoot", 0.3)) spawn("bullet.okayprefab", pos_x(), pos_y())
        b["cooldown"] = [this](std::vector<Value>& a) -> Value {
            if (!rt.host || a.size() < 2) return Value{false};
            std::string key = "__cd_" + a[0].AsString();
            float now = Time::ElapsedTime(), secs = a[1].AsFloat();
            auto& g = rt.host->globals;
            float last = g.count(key) ? g[key].AsFloat() : -1e9f;
            if (now - last >= secs) { g[key] = Value{now}; return Value{true}; }
            return Value{false};
        };
        // once("name"): true exactly the first time it's reached, false ever after
        // (per name) — fire one-time events without a manual "did I already?" flag.
        b["once"] = [this](std::vector<Value>& a) -> Value {
            if (!rt.host || a.empty()) return Value{false};
            std::string key = "__once_" + a[0].AsString();
            auto& g = rt.host->globals;
            if (g.count(key)) return Value{false};
            g[key] = Value{1.0f};
            return Value{true};
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
            char c = KeyCharFromName(a[0].AsString());
            return Value{c != 0 && Input::GetKeyUp(c)};
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
        // Optional trailing query filters shared by the 3D/2D casts below:
        // layers = a layer mask number, or a list "0,3" / "Default,Enemy"
        // (see layer_mask), and ignore_triggers = true to pass through triggers.
        auto maskArg = [](const std::vector<Value>& a, std::size_t i) -> std::uint32_t {
            if (a.size() <= i) return Physics3D::kAllLayers;
            if (a[i].IsString()) return Layers::MaskFromList(a[i].AsString());
            float f = a[i].AsFloat();
            return f <= 0.0f ? Physics3D::kAllLayers : (std::uint32_t)f;
        };
        auto trigArg = [](const std::vector<Value>& a, std::size_t i, bool dflt) {
            return a.size() > i ? !a[i].AsBool() : dflt;   // arg is "ignore triggers"
        };
        auto toHit = [](const RaycastHit3D& h) {
            Runtime::LastHit l;
            if (!h.hit) return l;
            l.hit = true;
            l.object = h.gameObject ? h.gameObject->name : std::string{};
            l.px = h.point.x;  l.py = h.point.y;  l.pz = h.point.z;
            l.nx = h.normal.x; l.ny = h.normal.y; l.nz = h.normal.z;
            l.dist = h.distance;
            return l;
        };
        // raycast3(ox,oy,oz, dx,dy,dz [, maxDist [, layers [, ignore_triggers]]])
        b["raycast3"] = [this, maskArg, trigArg, toHit](std::vector<Value>& a) -> Value {
            rt.lastHit3D = Runtime::LastHit{};
            if (a.size() < 6 || !rt.host || !rt.host->gameObject) return Value{std::string{}};
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value{std::string{}};
            Vec3 o{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            Vec3 d{a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()};
            float maxDist = a.size() > 6 ? a[6].AsFloat() : 1e9f;
            RaycastHit3D h = sc->physics3D().Raycast(*sc, o, d, maxDist, nullptr, maskArg(a, 7), trigArg(a, 8, true));
            sc->physics3D().RecordQueryRay(o, d, maxDist, h);
            rt.lastHit3D = toHit(h);
            return Value{rt.lastHit3D.object};
        };
        // raycast_all3(ox,oy,oz, dx,dy,dz [, maxDist [, layers [, ignore_triggers]]]) ->
        // the names of EVERYTHING the ray passes through, nearest first. Details per
        // hit: ray_all_count(), ray_all_object(i), ray_all_dist(i), ray_all_point(i),
        // ray_all_normal(i).
        b["raycast_all3"] = [this, maskArg, trigArg, toHit](std::vector<Value>& a) -> Value {
            rt.lastHitAll.clear();
            Value out = Value::MakeArray();
            if (a.size() < 6 || !rt.host || !rt.host->gameObject) return out;
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return out;
            Vec3 o{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            Vec3 d{a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()};
            float maxDist = a.size() > 6 ? a[6].AsFloat() : 1e9f;
            auto hits = sc->physics3D().RaycastAll(*sc, o, d, maxDist, nullptr, maskArg(a, 7), trigArg(a, 8, true));
            sc->physics3D().RecordQueryRay(o, d, maxDist, hits.empty() ? RaycastHit3D{} : hits.front());
            for (const RaycastHit3D& h : hits) {
                rt.lastHitAll.push_back(toHit(h));
                out.AsArray()->push_back(Value{rt.lastHitAll.back().object});
            }
            return out;
        };
        auto allAt = [this](std::vector<Value>& a) -> const Runtime::LastHit* {
            int i = a.empty() ? 0 : (int)a[0].AsFloat();
            return (i >= 0 && i < (int)rt.lastHitAll.size()) ? &rt.lastHitAll[(std::size_t)i] : nullptr;
        };
        b["ray_all_count"]  = [this](std::vector<Value>&) -> Value { return Value{(float)rt.lastHitAll.size()}; };
        b["ray_all_object"] = [allAt](std::vector<Value>& a) -> Value { auto* h = allAt(a); return Value{h ? h->object : std::string{}}; };
        b["ray_all_dist"]   = [allAt](std::vector<Value>& a) -> Value { auto* h = allAt(a); return Value{h ? h->dist : 0.0f}; };
        b["ray_all_point"]  = [allAt](std::vector<Value>& a) -> Value { auto* h = allAt(a); return Value{h ? Vec3{h->px, h->py, h->pz} : Vec3::Zero}; };
        b["ray_all_normal"] = [allAt](std::vector<Value>& a) -> Value { auto* h = allAt(a); return Value{h ? Vec3{h->nx, h->ny, h->nz} : Vec3::Zero}; };
        // Thick casts (fill the same ray3_* details as raycast3; skip triggers unless
        // the last arg is false): sphere_cast3(ox,oy,oz, dx,dy,dz, radius [, maxDist [, layers [, ignore_triggers]]])
        b["sphere_cast3"] = [this, maskArg, trigArg, toHit](std::vector<Value>& a) -> Value {
            rt.lastHit3D = Runtime::LastHit{};
            if (a.size() < 7 || !rt.host || !rt.host->gameObject) return Value{std::string{}};
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value{std::string{}};
            Vec3 o{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            Vec3 d{a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()};
            float r = a[6].AsFloat(), maxDist = a.size() > 7 ? a[7].AsFloat() : 1e9f;
            RaycastHit3D h = sc->physics3D().SphereCast(*sc, o, d, r, maxDist, nullptr, maskArg(a, 8), trigArg(a, 9, false));
            sc->physics3D().RecordQueryRay(o, d, maxDist, h, r);
            rt.lastHit3D = toHit(h);
            return Value{rt.lastHit3D.object};
        };
        // box_cast3(cx,cy,cz, hx,hy,hz, dx,dy,dz [, maxDist [, layers [, ignore_triggers]]]) — h = half size
        b["box_cast3"] = [this, maskArg, trigArg, toHit](std::vector<Value>& a) -> Value {
            rt.lastHit3D = Runtime::LastHit{};
            if (a.size() < 9 || !rt.host || !rt.host->gameObject) return Value{std::string{}};
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value{std::string{}};
            Vec3 c{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            Vec3 h3{a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()};
            Vec3 d{a[6].AsFloat(), a[7].AsFloat(), a[8].AsFloat()};
            float maxDist = a.size() > 9 ? a[9].AsFloat() : 1e9f;
            RaycastHit3D h = sc->physics3D().BoxCast(*sc, c, h3, d, Quat::Identity, maxDist, nullptr,
                                                     maskArg(a, 10), trigArg(a, 11, false));
            sc->physics3D().RecordQueryRay(c, d, maxDist, h, Mathf::Min(h3.x, Mathf::Min(h3.y, h3.z)));
            rt.lastHit3D = toHit(h);
            return Value{rt.lastHit3D.object};
        };
        // capsule_cast3(ax,ay,az, bx,by,bz, radius, dx,dy,dz [, maxDist [, layers [, ignore_triggers]]])
        b["capsule_cast3"] = [this, maskArg, trigArg, toHit](std::vector<Value>& a) -> Value {
            rt.lastHit3D = Runtime::LastHit{};
            if (a.size() < 10 || !rt.host || !rt.host->gameObject) return Value{std::string{}};
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value{std::string{}};
            Vec3 p0{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            Vec3 p1{a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()};
            float r = a[6].AsFloat();
            Vec3 d{a[7].AsFloat(), a[8].AsFloat(), a[9].AsFloat()};
            float maxDist = a.size() > 10 ? a[10].AsFloat() : 1e9f;
            RaycastHit3D h = sc->physics3D().CapsuleCast(*sc, p0, p1, r, d, maxDist, nullptr,
                                                         maskArg(a, 11), trigArg(a, 12, false));
            sc->physics3D().RecordQueryRay((p0 + p1) * 0.5f, d, maxDist, h, r);
            rt.lastHit3D = toHit(h);
            return Value{rt.lastHit3D.object};
        };
        // Overlaps -> the names of every collider's object inside the shape.
        auto names3 = [](const std::vector<Collider3D*>& cols) {
            Value out = Value::MakeArray();
            for (Collider3D* c : cols) out.AsArray()->push_back(Value{c->gameObject ? c->gameObject->name : std::string{}});
            return out;
        };
        // overlap_sphere3(cx,cy,cz, radius [, layers [, ignore_triggers]])
        b["overlap_sphere3"] = [this, maskArg, trigArg, names3](std::vector<Value>& a) -> Value {
            if (a.size() < 4 || !rt.host || !rt.host->gameObject) return Value::MakeArray();
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value::MakeArray();
            return names3(sc->physics3D().OverlapSphere(*sc, {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()},
                                                        a[3].AsFloat(), maskArg(a, 4), trigArg(a, 5, true)));
        };
        // overlap_box3(cx,cy,cz, hx,hy,hz [, layers [, ignore_triggers]]) — h = half size
        b["overlap_box3"] = [this, maskArg, trigArg, names3](std::vector<Value>& a) -> Value {
            if (a.size() < 6 || !rt.host || !rt.host->gameObject) return Value::MakeArray();
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value::MakeArray();
            return names3(sc->physics3D().OverlapBox(*sc, {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()},
                                                     {a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()}, Quat::Identity,
                                                     maskArg(a, 6), trigArg(a, 7, true)));
        };
        // overlap_capsule3(ax,ay,az, bx,by,bz, radius [, layers [, ignore_triggers]])
        b["overlap_capsule3"] = [this, maskArg, trigArg, names3](std::vector<Value>& a) -> Value {
            if (a.size() < 7 || !rt.host || !rt.host->gameObject) return Value::MakeArray();
            Scene* sc = rt.host->gameObject->scene(); if (!sc) return Value::MakeArray();
            return names3(sc->physics3D().OverlapCapsule(*sc, {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()},
                                                         {a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()},
                                                         a[6].AsFloat(), maskArg(a, 7), trigArg(a, 8, true)));
        };
        // layer_mask("Default", "Enemy", 3, ...) -> a mask number for the layers arg.
        b["layer_mask"] = [](std::vector<Value>& a) -> Value {
            std::uint32_t m = 0;
            for (const Value& v : a)
                m |= v.IsString() ? Layers::MaskFromList(v.AsString()) : (1u << ((int)v.AsFloat() & 31));
            return Value{(float)(a.empty() ? Physics3D::kAllLayers : m)};
        };
        // ray3_cast(what, origin, direction [, maxDist [, layers [, ignore_triggers [, radius]]]])
        // — one value from a cast that skips this object: what = "hit" (true/false),
        // "point", "normal" (vectors), "distance" or "object" (name). Used by
        // visual-script Raycast 3D / Sphere Cast nodes turned into code.
        b["ray3_cast"] = [this, maskArg, trigArg](std::vector<Value>& a) -> Value {
            const std::string what = a.empty() ? std::string("hit") : a[0].AsString();
            RaycastHit3D h;
            Scene* sc = rt.host && rt.host->gameObject ? rt.host->gameObject->scene() : nullptr;
            if (what == "all") {   // names of everything along the ray, nearest first
                Value out = Value::MakeArray();
                if (!sc || a.size() < 3) return out;
                for (const RaycastHit3D& x : sc->physics3D().RaycastAll(*sc, a[1].AsVec3(), a[2].AsVec3(),
                        a.size() > 3 ? a[3].AsFloat() : 1e9f, rt.host->gameObject, maskArg(a, 4), trigArg(a, 5, true)))
                    out.AsArray()->push_back(Value{x.gameObject ? x.gameObject->name : std::string{}});
                return out;
            }
            if (sc && a.size() >= 3) {
                Vec3 o = a[1].AsVec3(), d = a[2].AsVec3();
                float maxDist = a.size() > 3 ? a[3].AsFloat() : 1e9f;
                float r = a.size() > 6 ? a[6].AsFloat() : 0.0f;
                bool trig = trigArg(a, 5, r <= 0.0f);
                h = r > 0.0f
                    ? sc->physics3D().SphereCast(*sc, o, d, r, maxDist, rt.host->gameObject, maskArg(a, 4), trig)
                    : sc->physics3D().Raycast(*sc, o, d, maxDist, rt.host->gameObject, maskArg(a, 4), trig);
                sc->physics3D().RecordQueryRay(o, d, maxDist, h, r);
            }
            if (what == "point")    return Value{h.point};
            if (what == "normal")   return Value{h.normal};
            if (what == "distance") return Value{h.distance};
            if (what == "object")   return Value{h.gameObject ? h.gameObject->name : std::string{}};
            return Value{h.hit};
        };
        // overlap3("sphere" | "box", center, radius_or_half_size [, layers]) -> names of
        // the objects inside, leaving out this object (the Overlap nodes as code).
        b["overlap3"] = [this, maskArg, names3](std::vector<Value>& a) -> Value {
            Scene* sc = rt.host && rt.host->gameObject ? rt.host->gameObject->scene() : nullptr;
            if (!sc || a.size() < 3) return Value::MakeArray();
            GameObject* self = rt.host->gameObject;
            std::vector<Collider3D*> cols = a[0].AsString() == "box"
                ? sc->physics3D().OverlapBox(*sc, a[1].AsVec3(), a[2].AsVec3(), Quat::Identity, maskArg(a, 3), true, self)
                : sc->physics3D().OverlapSphere(*sc, a[1].AsVec3(), a[2].AsFloat(), maskArg(a, 3), true, self);
            Value out = Value::MakeArray();   // one entry per object
            std::vector<GameObject*> seen;
            for (Collider3D* c : cols) {
                if (std::find(seen.begin(), seen.end(), c->gameObject) != seen.end()) continue;
                seen.push_back(c->gameObject);
                out.AsArray()->push_back(Value{c->gameObject ? c->gameObject->name : std::string{}});
            }
            return out;
        };
        b["ray3_point"]  = [this](std::vector<Value>&) -> Value { return Value{Vec3{rt.lastHit3D.px, rt.lastHit3D.py, rt.lastHit3D.pz}}; };
        b["ray3_normal"] = [this](std::vector<Value>&) -> Value { return Value{Vec3{rt.lastHit3D.nx, rt.lastHit3D.ny, rt.lastHit3D.nz}}; };
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
            return Value{ActionList::GetVar(a[0].AsString())};   // shares stats/prefs too
        };
        // Broadcast a named signal to every Actions (OnMessage) list in the scene.
        b["send_message"] = [this](std::vector<Value>& a) {
            if (!a.empty() && rt.host && rt.host->gameObject && rt.host->gameObject->scene())
                for (ActionList* al : rt.host->gameObject->scene()->FindObjectsOfType<ActionList>())
                    al->ReceiveMessage(a[0].AsString());
            return Value{};
        };

        // --- Custom events + object variables shared with visual-script graphs ---
        // send_event("Target", "Hit", value): runs the target's On Custom Event "Hit"
        // graph nodes and its scripts' OnEvent_Hit(value). "" / "self" = this object.
        auto evTarget = [this](const std::string& name) -> GameObject* {
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            if (!me) return nullptr;
            if (name.empty() || name == "self") return me;
            return me->scene() ? me->scene()->Find(name) : nullptr;
        };
        auto deliverEvent = [this](GameObject* o, const std::string& ev, const Value& v) {
            if (!o) return;
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            for (auto* vc : o->GetComponents<VisualScriptComponent>()) vc->SendEvent(ev, v, me);
            for (auto* sc : o->GetComponents<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEventWith("OnEvent_" + ev, v);
        };
        b["send_event"] = [evTarget, deliverEvent](std::vector<Value>& a) {
            if (a.size() < 2) return Value{};
            deliverEvent(evTarget(a[0].AsString()), a[1].AsString(), a.size() > 2 ? a[2] : Value{});
            return Value{};
        };
        // run_action("move 1 0 0") / check_action("var_gt hp 0"): one step of the
        // no-code Actions library, run on this object (what a graph's Action /
        // Condition node exports to).
        b["run_action"] = [this](std::vector<Value>& a) {
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            if (a.empty() || !me) return Value{};
            ActionList runner;
            runner.gameObject = me; runner.transform = me->transform;
            runner.instructions = ActionList::DecodeItems(a[0].AsString());
            if (runner.BeginRun()) runner.Update(0.0f);
            return Value{};
        };
        b["check_action"] = [this](std::vector<Value>& a) {
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            if (a.empty() || !me) return Value{false};
            ActionList tester;
            tester.gameObject = me; tester.transform = me->transform;
            return Value{tester.TestConditions({ActionList::DecodeItem(a[0].AsString())})};
        };
        b["broadcast_event"] = [this, deliverEvent](std::vector<Value>& a) {
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            if (a.empty() || !me || !me->scene()) return Value{};
            std::vector<GameObject*> all;
            for (const auto& up : me->scene()->Objects()) if (up && up->active) all.push_back(up.get());
            for (GameObject* o : all) deliverEvent(o, a[0].AsString(), a.size() > 1 ? a[1] : Value{});
            return Value{};
        };
        // set_object_var("Enemy", "hp", 10) / get_object_var("Enemy", "hp"): a graph's
        // blackboard variable or a script's global on another object.
        b["set_object_var"] = [evTarget](std::vector<Value>& a) {
            if (a.size() < 3) return Value{};
            if (GameObject* o = evTarget(a[0].AsString())) {
                for (auto* vc : o->GetComponents<VisualScriptComponent>()) vc->SetVariable(a[1].AsString(), a[2]);
                for (auto* sc : o->GetComponents<ScriptComponent>()) if (sc->VM()) sc->VM()->SetGlobal(a[1].AsString(), a[2]);
            }
            return Value{};
        };
        b["get_object_var"] = [evTarget](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{};
            if (GameObject* o = evTarget(a[0].AsString())) {
                if (auto* vc = o->GetComponent<VisualScriptComponent>()) return vc->GetVariable(a[1].AsString());
                if (auto* sc = o->GetComponent<ScriptComponent>()) if (sc->VM()) return sc->VM()->GetGlobal(a[1].AsString());
            }
            return Value{};
        };
        // set_object_position("Door", x, y, z): a non-number axis keeps its value.
        b["set_object_position"] = [evTarget](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            GameObject* o = evTarget(a[0].AsString());
            if (!o || !o->transform) return Value{};
            Vec3 p = o->transform->Position();
            if (a.size() > 1 && !a[1].IsString()) p.x = a[1].AsFloat();
            if (a.size() > 2 && !a[2].IsString()) p.y = a[2].AsFloat();
            if (a.size() > 3 && !a[3].IsString()) p.z = a[3].AsFloat();
            o->transform->SetPosition(p);
            return Value{};
        };
        // UI Animation: ui_show("Menu") activates it and plays its intro, ui_hide("Menu")
        // plays its outro then hides it (instantly when it has no UI Animation),
        // ui_play("Menu") replays the intro.
        b["ui_show"] = [evTarget](std::vector<Value>& a) {
            if (GameObject* o = evTarget(a.empty() ? std::string() : a[0].AsString())) {
                o->active = true;
                if (auto* ua = o->GetComponent<UIAnimation>()) ua->Play();
            }
            return Value{};
        };
        b["ui_hide"] = [evTarget](std::vector<Value>& a) {
            if (GameObject* o = evTarget(a.empty() ? std::string() : a[0].AsString())) {
                if (auto* ua = o->GetComponent<UIAnimation>(); ua && o->active) ua->PlayOut();
                else o->active = false;
            }
            return Value{};
        };
        b["ui_play"] = [evTarget](std::vector<Value>& a) {
            if (GameObject* o = evTarget(a.empty() ? std::string() : a[0].AsString()))
                if (auto* ua = o->GetComponent<UIAnimation>()) ua->Play();
            return Value{};
        };
        b["pause_game"] = [](std::vector<Value>& a) { Game::SetPaused(a.empty() || a[0].AsBool()); return Value{}; };
        if (!b.count("quit")) b["quit"] = [](std::vector<Value>&) { Game::RequestQuit(); return Value{}; };   // Application.Quit()
        b["show_cursor"] = [](std::vector<Value>& a) { Cursor::Capture(!(a.empty() || a[0].AsBool())); return Value{}; };

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
        // Every tween_* call starts a tween on this object and returns its id, so it
        // can be configured and controlled afterwards (tween_loops(id, -1, "yoyo"),
        // tween_kill(id) ...). Tweens die with their object. Ease names are forgiving:
        // "out_quad", "OutQuad", "in_out_sine"...
        auto easeFromArg = [](std::vector<Value>& a, std::size_t i) -> Ease {
            if (i >= a.size() || !a[i].IsString()) return Ease::Linear;
            return Easing::FromName(a[i].AsString(), Ease::Linear);
        };
        auto tweens = [this]() -> TweenManager* {
            if (rt.host && rt.host->gameObject && rt.host->gameObject->scene())
                return &rt.host->gameObject->scene()->tweens();
            return nullptr;
        };
        // Optional on-complete: if arg `i` names a script function, a callback that
        // calls it (DOTween's OnComplete). Anything else yields an empty callback.
        auto fnFromArg = [this](std::vector<Value>& a, std::size_t i) -> std::function<void()> {
            if (i >= a.size() || !a[i].IsString()) return {};
            std::string fn = a[i].AsString();
            if (!rt.functions.count(fn)) return {};
            return [this, fn, alive = std::weak_ptr<int>(life)]() {
                if (alive.expired()) return;
                std::vector<Value> none; rt.Call(fn, none);
            };
        };
        // Script-facing ids for tweens (also those moved into a sequence).
        struct TweenRefs { std::unordered_map<int, std::weak_ptr<Tweener>> map; int next = 1; };
        auto refs = std::make_shared<TweenRefs>();
        auto reg = [refs](const std::shared_ptr<Tweener>& t) -> Value {
            if (!t) return Value{0.0f};
            if (refs->map.size() > 256 && (refs->next % 64) == 0)   // forget finished ones now and then
                for (auto it = refs->map.begin(); it != refs->map.end();) it = it->second.expired() ? refs->map.erase(it) : std::next(it);
            const int id = refs->next++;
            refs->map[id] = t;
            return Value{(float)id};
        };
        auto lookup = [refs](std::vector<Value>& a, std::size_t i = 0) -> std::shared_ptr<Tweener> {
            if (i >= a.size()) return nullptr;
            auto it = refs->map.find((int)std::lround(a[i].AsFloat()));
            if (it == refs->map.end()) return nullptr;
            auto t = it->second.lock();
            return t && t->IsActive() ? t : nullptr;
        };
        // Finish a new tween: ease from arg `ei` (else keep its own), on-complete from `di`.
        auto finish = [easeFromArg, fnFromArg, reg](std::vector<Value>& a, std::shared_ptr<Tweener> t, std::size_t ei, std::size_t di) -> Value {
            if (!t) return Value{0.0f};
            if (ei < a.size() && a[ei].IsString()) t->SetEase(easeFromArg(a, ei));
            else if (ei != (std::size_t)-1) t->SetEase(Ease::Linear);
            if (auto f = fnFromArg(a, di)) t->OnComplete(f);
            return reg(t);
        };
        const std::size_t kNone = (std::size_t)-1;
        auto self = go;
        auto num = [](std::vector<Value>& a, std::size_t i, float def) { return i < a.size() && !a[i].IsString() ? a[i].AsFloat() : def; };

        b["tween_move"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            const float z = g->transform->localPosition.z;
            return finish(a, DOTween::LocalMove(g, {a[0].AsFloat(), a[1].AsFloat(), z}, a[2].AsFloat()), 3, 4);
        };
        b["tween_move3"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 4) return Value{0.0f};
            return finish(a, DOTween::LocalMove(g, {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()}, a[3].AsFloat()), 4, 5);
        };
        // Glide by an offset (DOTween's DOBlendableMoveBy).
        b["tween_move_by"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            auto t = DOTween::LocalMove(g, {a[0].AsFloat(), a[1].AsFloat(), 0.0f}, a[2].AsFloat());
            t->SetRelative();
            return finish(a, t, 3, 4);
        };
        // World-space move (ignores the parent).
        b["tween_move_world"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 4) return Value{0.0f};
            return finish(a, DOTween::Move(g, {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()}, a[3].AsFloat()), 4, 5);
        };
        b["tween_scale"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            return finish(a, DOTween::Scale(g, a[0].AsFloat(), a[1].AsFloat()), 2, 3);
        };
        b["tween_scale_xy"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            return finish(a, DOTween::Scale(g, {a[0].AsFloat(), a[1].AsFloat(), g->transform->localScale.z}, a[2].AsFloat()), 3, 4);
        };
        // Spin by `deg` degrees about Z (full turns work).
        b["tween_rotate"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            auto t = DOTween::Rotate(g, {0, 0, a[0].AsFloat()}, a[1].AsFloat());
            t->SetRelative();
            return finish(a, t, 2, 3);
        };
        // Rotate to an absolute Z angle (DOTween's DORotate).
        b["tween_rotate_to"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            return finish(a, DOTween::Rotate(g, {0, 0, a[0].AsFloat()}, a[1].AsFloat()), 2, 3);
        };
        // Rotate to x, y, z degrees (3D).
        b["tween_rotate3"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 4) return Value{0.0f};
            return finish(a, DOTween::Rotate(g, {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()}, a[3].AsFloat()), 4, 5);
        };
        // Ping-pong forever between here and (x, y) — floating pickups, patrols.
        b["tween_loop_move"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            auto t = DOTween::LocalMove(g, {a[0].AsFloat(), a[1].AsFloat(), g->transform->localPosition.z}, a[2].AsFloat());
            t->SetLoops(-1, LoopType::Yoyo);
            return finish(a, t, 3, kNone);
        };
        b["tween_loop_scale"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            auto t = DOTween::Scale(g, a[0].AsFloat(), a[1].AsFloat());
            t->SetLoops(-1, LoopType::Yoyo);
            return finish(a, t, 2, kNone);
        };
        // Spin forever: a full turn every `dur` seconds (dir +1 CCW, -1 CW).
        b["tween_loop_rotate"] = [self, reg](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.empty() || a[0].AsFloat() < 1e-3f) return Value{0.0f};
            const float dir = a.size() > 1 ? (a[1].AsFloat() < 0 ? -1.0f : 1.0f) : 1.0f;
            auto t = DOTween::Rotate(g, {0, 0, 360.0f * dir}, a[0].AsFloat());
            t->SetRelative().SetEase(Ease::Linear).SetLoops(-1, LoopType::Incremental);
            return reg(t);
        };
        // Punch the scale out and settle back (DOPunchScale) — button presses, pickups.
        b["tween_punch_scale"] = [self, reg, num](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            const float p = a[0].AsFloat();
            return reg(DOTween::PunchScale(g, {p, p, p}, a[1].AsFloat(), (int)num(a, 2, 6.0f)));
        };
        b["tween_punch_pos"] = [self, reg, num](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            return reg(DOTween::PunchPosition(g, {a[0].AsFloat(), a[1].AsFloat(), 0}, a[2].AsFloat(), (int)num(a, 3, 6.0f)));
        };
        b["tween_punch_rotate"] = [self, reg, num](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            return reg(DOTween::PunchRotation(g, {0, 0, a[0].AsFloat()}, a[1].AsFloat(), (int)num(a, 2, 6.0f)));
        };
        // Shake, fading out (camera / impact shake).
        b["tween_shake"] = [self, reg, num](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            const float s = a[0].AsFloat();
            return reg(DOTween::ShakePosition(g, a[1].AsFloat(), {s, s, 0}, (int)num(a, 2, 20.0f)));
        };
        b["tween_shake_rotate"] = [self, reg, num](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            return reg(DOTween::ShakeRotation(g, a[1].AsFloat(), {0, 0, a[0].AsFloat()}, (int)num(a, 2, 14.0f)));
        };
        b["tween_shake_scale"] = [self, reg, num](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            const float s = a[0].AsFloat();
            return reg(DOTween::ShakeScale(g, a[1].AsFloat(), {s, s, s}, (int)num(a, 2, 14.0f)));
        };
        // Jump to (x, y) in an arc `height` high: tween_jump(x, y, height, dur[, jumps][, "done"]).
        b["tween_jump"] = [self, finish, num](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 4) return Value{0.0f};
            const int jumps = (int)num(a, 4, 1.0f);
            const std::size_t di = a.size() > 4 && a[4].IsString() ? 4 : 5;
            auto t = DOTween::Jump(g, {a[0].AsFloat(), a[1].AsFloat(), g->transform->localPosition.z}, a[2].AsFloat(), jumps, a[3].AsFloat());
            return finish(a, t, kNone, di);
        };
        // Through waypoints: tween_path(dur, x1, y1, x2, y2, ...). tween_path_smooth curves through them.
        auto pathFn = [self, reg](std::vector<Value>& a, DOTween::PathType type) -> Value {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            std::vector<Vec3> pts;
            for (std::size_t i = 1; i + 1 < a.size(); i += 2) pts.push_back({a[i].AsFloat(), a[i + 1].AsFloat(), g->transform->localPosition.z});
            auto t = DOTween::Path(g, pts, a[0].AsFloat(), type);
            t->SetEase(Ease::Linear);
            return reg(t);
        };
        b["tween_path"] = [pathFn](std::vector<Value>& a) { return pathFn(a, DOTween::PathType::Linear); };
        b["tween_path_smooth"] = [pathFn](std::vector<Value>& a) { return pathFn(a, DOTween::PathType::CatmullRom); };
        // Count this object's text from -> to (score ticks); optional prefix "Score: ".
        b["tween_number"] = [self, reg](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            const std::string prefix = (a.size() > 3 && a[3].IsString()) ? a[3].AsString() : std::string{};
            auto t = DOTween::Counter(g, a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat(), prefix);
            t->SetEase(Ease::Linear);
            return reg(t);
        };
        // Type this object's text out letter by letter (dialogue): tween_text("Hello!", dur[, "done"]).
        b["tween_text"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            return finish(a, DOTween::Text(g, a[0].AsString(), a[1].AsFloat()), kNone, 2);
        };
        // A UI widget's anchored position / size — slide menus and panels in and out.
        b["tween_ui_move"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            return finish(a, DOTween::AnchorPos(g, {a[0].AsFloat(), a[1].AsFloat()}, a[2].AsFloat()), 3, 4);
        };
        b["tween_ui_size"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            return finish(a, DOTween::SizeDelta(g, {a[0].AsFloat(), a[1].AsFloat()}, a[2].AsFloat()), 3, 4);
        };
        // This object's colour (sprite, mesh, UI or text) / its alpha.
        b["tween_color"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); Color* c = DOTween::ColorOf(g);
            if (!c || a.size() < 4) return Value{0.0f};
            return finish(a, DOTween::ColorTo(g, Color{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat(), c->a}, a[3].AsFloat()), 4, 5);
        };
        b["tween_fade"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            return finish(a, DOTween::Fade(g, a[0].AsFloat(), a[1].AsFloat()), 2, 3);
        };
        // flash(r, g, b[, dur]): snap to a colour, then fade back (hit feedback).
        b["flash"] = [self, reg](std::vector<Value>& a) {
            GameObject* g = self(); Color* c = DOTween::ColorOf(g);
            if (!c || a.size() < 3) return Value{0.0f};
            const Color orig = *c;
            auto t = DOTween::ColorTo(g, orig, a.size() > 3 ? a[3].AsFloat() : 0.2f);
            *c = Color{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat(), orig.a};
            t->SetEase(Ease::Linear);
            return reg(t);
        };
        // A script variable: tween_var("speed", 10, 2[, ease][, "done"]) (DOTween.To).
        b["tween_var"] = [this, tweens, finish](std::vector<Value>& a) {
            TweenManager* m = tweens(); if (!m || !rt.host || a.size() < 3) return Value{0.0f};
            const std::string name = a[0].AsString();
            std::weak_ptr<int> alive = life;
            auto t = m->To([this, alive, name]() {
                               if (alive.expired()) return 0.0f;
                               auto& g = rt.Global().vars; auto it = g.find(name);
                               return it != g.end() ? it->second.AsFloat() : 0.0f; },
                           [this, alive, name](float v) { if (!alive.expired()) rt.Global().vars[name] = Value{v}; },
                           a[1].AsFloat(), a[2].AsFloat());
            t->SetTarget(rt.host->gameObject);
            return finish(a, t, 3, 4);
        };
        // Call a function after a delay: tween_call(seconds, "fn") (DOVirtual.DelayedCall).
        b["tween_call"] = [this, tweens, fnFromArg, reg](std::vector<Value>& a) {
            TweenManager* m = tweens(); if (!m || a.size() < 2) return Value{0.0f};
            auto t = m->DelayedCall(a[0].AsFloat(), fnFromArg(a, 1));
            if (rt.host) t->SetTarget(rt.host->gameObject);
            return reg(t);
        };

        // ---- Settings: each returns the id, so they nest ----
        auto setting = [lookup](std::function<void(Tweener&, std::vector<Value>&)> fn) {
            return [lookup, fn](std::vector<Value>& a) -> Value {
                auto t = lookup(a, 0);
                if (t) fn(*t, a);
                return a.empty() ? Value{0.0f} : a[0];
            };
        };
        b["tween_delay"]    = setting([](Tweener& t, std::vector<Value>& a) { t.SetDelay(a.size() > 1 ? a[1].AsFloat() : 0.0f); });
        b["tween_ease"]     = setting([easeFromArg](Tweener& t, std::vector<Value>& a) { t.SetEase(easeFromArg(a, 1)); });
        b["tween_speed"]    = setting([](Tweener& t, std::vector<Value>& a) { t.SetSpeed(a.size() > 1 ? a[1].AsFloat() : 1.0f); });
        b["tween_unscaled"] = setting([](Tweener& t, std::vector<Value>& a) { t.SetUpdate(a.size() > 1 ? a[1].AsBool() : true); });
        b["tween_set_id"]   = setting([](Tweener& t, std::vector<Value>& a) { t.SetId(a.size() > 1 ? a[1].AsString() : std::string{}); });
        b["tween_from"]     = setting([](Tweener& t, std::vector<Value>&) { t.From(); });
        b["tween_relative"] = setting([](Tweener& t, std::vector<Value>&) { t.SetRelative(); });
        b["tween_keep"]     = setting([](Tweener& t, std::vector<Value>&) { t.SetAutoKill(false); });
        // tween_loops(id, count[, "restart" | "yoyo" | "incremental"]); count -1 = forever.
        b["tween_loops"] = setting([](Tweener& t, std::vector<Value>& a) {
            std::string ty = a.size() > 2 && a[2].IsString() ? a[2].AsString() : std::string("restart");
            for (auto& ch : ty) ch = (char)std::tolower((unsigned char)ch);
            const LoopType lt = ty == "yoyo" ? LoopType::Yoyo : ty == "incremental" ? LoopType::Incremental : LoopType::Restart;
            t.SetLoops(a.size() > 1 ? (int)a[1].AsFloat() : 1, lt);
        });
        b["tween_on_complete"] = setting([fnFromArg](Tweener& t, std::vector<Value>& a) { t.OnComplete(fnFromArg(a, 1)); });
        b["tween_on_step"]     = setting([fnFromArg](Tweener& t, std::vector<Value>& a) { t.OnStepComplete(fnFromArg(a, 1)); });
        b["tween_on_start"]    = setting([fnFromArg](Tweener& t, std::vector<Value>& a) { t.OnStart(fnFromArg(a, 1)); });
        b["tween_on_update"]   = setting([fnFromArg](Tweener& t, std::vector<Value>& a) { t.OnUpdate(fnFromArg(a, 1)); });

        // ---- Control ----
        b["tween_kill"]      = setting([](Tweener& t, std::vector<Value>& a) { t.Kill(a.size() > 1 && a[1].AsBool()); });
        b["tween_pause"]     = setting([](Tweener& t, std::vector<Value>&) { t.Pause(); });
        b["tween_play"]      = setting([](Tweener& t, std::vector<Value>&) { t.Play(); });
        b["tween_toggle"]    = setting([](Tweener& t, std::vector<Value>&) { t.TogglePause(); });
        b["tween_restart"]   = setting([](Tweener& t, std::vector<Value>&) { t.Restart(); });
        b["tween_rewind"]    = setting([](Tweener& t, std::vector<Value>&) { t.Rewind(); });
        b["tween_complete"]  = setting([](Tweener& t, std::vector<Value>&) { t.Complete(); });
        b["tween_backwards"] = setting([](Tweener& t, std::vector<Value>&) { t.PlayBackwards(); });
        b["tween_goto"]      = setting([](Tweener& t, std::vector<Value>& a) { t.Goto(a.size() > 1 ? a[1].AsFloat() : 0.0f, a.size() > 2 && a[2].AsBool()); });
        b["tween_playing"] = [lookup](std::vector<Value>& a) { auto t = lookup(a, 0); return Value{t && t->IsPlaying()}; };
        b["tween_active"]  = [lookup](std::vector<Value>& a) { auto t = lookup(a, 0); return Value{t != nullptr}; };
        b["tween_progress"] = [lookup](std::vector<Value>& a) { auto t = lookup(a, 0); return Value{t ? t->ElapsedPercentage() : 1.0f}; };
        // Everything on this object.
        b["tween_kill_all"]  = [self](std::vector<Value>& a) { return Value{(float)DOTween::Kill(self(), !a.empty() && a[0].AsBool())}; };
        b["tween_pause_all"] = [self](std::vector<Value>&) { return Value{(float)DOTween::Pause(self())}; };
        b["tween_play_all"]  = [self](std::vector<Value>&) { return Value{(float)DOTween::Play(self())}; };
        b["is_tweening"] = [self](std::vector<Value>&) {
            GameObject* g = self();
            return Value{g && g->scene() && g->scene()->tweens().IsTweening(g)};
        };
        // Everything with an id set by tween_set_id (or a Tween Animation's Id), scene-wide.
        auto byId = [this, tweens](int op) {
            return [this, tweens, op](std::vector<Value>& a) -> Value {
                TweenManager* m = tweens(); if (!m || a.empty()) return Value{0.0f};
                const std::string id = a[0].AsString();
                int n = op == 0 ? m->Kill(id) : op == 1 ? m->Pause(id) : op == 2 ? m->Play(id) : m->Restart(id);
                if (op == 2 || op == 3)   // Tween Animations that haven't started yet
                    n += TweenAnimation::PlayAll(*rt.host->gameObject->scene(), id, op == 3);
                return Value{(float)n};
            };
        };
        b["tween_kill_id"]    = byId(0);
        b["tween_pause_id"]   = byId(1);
        b["tween_play_id"]    = byId(2);
        b["tween_restart_id"] = byId(3);

        // ---- Sequences: tweens one after another / together on one timeline ----
        b["seq_new"] = [this, tweens, reg](std::vector<Value>&) {
            TweenManager* m = tweens(); if (!m) return Value{0.0f};
            auto s = m->Sequence();
            if (rt.host) s->SetTarget(rt.host->gameObject);
            return reg(s);
        };
        auto seqOp = [lookup](std::function<void(TweenSequence&, std::vector<Value>&)> fn) {
            return [lookup, fn](std::vector<Value>& a) -> Value {
                auto s = std::dynamic_pointer_cast<TweenSequence>(lookup(a, 0));
                if (s) fn(*s, a);
                return a.empty() ? Value{0.0f} : a[0];
            };
        };
        b["seq_append"] = seqOp([lookup](TweenSequence& s, std::vector<Value>& a) { if (auto t = lookup(a, 1)) s.Append(t); });
        b["seq_join"]   = seqOp([lookup](TweenSequence& s, std::vector<Value>& a) { if (auto t = lookup(a, 1)) s.Join(t); });
        b["seq_insert"] = seqOp([lookup](TweenSequence& s, std::vector<Value>& a) { if (auto t = lookup(a, 2)) s.Insert(a[1].AsFloat(), t); });
        b["seq_interval"] = seqOp([](TweenSequence& s, std::vector<Value>& a) { s.AppendInterval(a.size() > 1 ? a[1].AsFloat() : 0.0f); });
        b["seq_prepend_interval"] = seqOp([](TweenSequence& s, std::vector<Value>& a) { s.PrependInterval(a.size() > 1 ? a[1].AsFloat() : 0.0f); });
        b["seq_callback"] = seqOp([fnFromArg](TweenSequence& s, std::vector<Value>& a) { if (auto f = fnFromArg(a, 1)) s.AppendCallback(f); });
        b["seq_insert_callback"] = seqOp([fnFromArg](TweenSequence& s, std::vector<Value>& a) {
            if (auto f = fnFromArg(a, 2)) s.InsertCallback(a.size() > 1 ? a[1].AsFloat() : 0.0f, f);
        });
        // ---- Save system: snapshot the whole scene to a slot file ------
        b["save_game"] = [this](std::vector<Value>& a) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            if (!s) return Value{0.0f};
            std::string slot = a.empty() ? "save1" : a[0].AsString();
            if (!ScriptPathOk(slot, "save_game")) return Value{0.0f};
            const std::string path = Save::Resolve("saves/" + slot + ".okaysave");
            std::error_code ec; std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
            return Value{SceneSerializer::SaveToFile(*s, path) ? 1.0f : 0.0f};
        };
        b["load_game"] = [this](std::vector<Value>& a) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            std::string slot = a.empty() ? "save1" : a[0].AsString();
            if (!ScriptPathOk(slot, "load_game")) return Value{0.0f};
            std::string path = ScriptReadPath("saves/" + slot + ".okaysave");
            if (s && std::ifstream(path).good()) { s->RequestLoad(path); return Value{1.0f}; }
            return Value{0.0f};
        };
        b["save_exists"] = [](std::vector<Value>& a) {
            std::string slot = a.empty() ? "save1" : a[0].AsString();
            if (!ScriptPathOk(slot, "save_exists")) return Value{0.0f};
            return Value{std::ifstream(ScriptReadPath("saves/" + slot + ".okaysave")).good() ? 1.0f : 0.0f};
        };
        b["delete_save"] = [](std::vector<Value>& a) {
            std::string slot = a.empty() ? "save1" : a[0].AsString();
            if (!ScriptPathOk(slot, "delete_save")) return Value{0.0f};
            return Value{std::remove(ScriptReadPath("saves/" + slot + ".okaysave").c_str()) == 0 ? 1.0f : 0.0f};
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

        // ---- API: write scripts that look like C#. -----
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
        // Construct a vector value from components: vec3(x, y, z) / vec2(x, y).
        b["vec3"] = [](std::vector<Value>& a) -> Value {
            return Value{Vec3{a.size() > 0 ? a[0].AsFloat() : 0.0f,
                              a.size() > 1 ? a[1].AsFloat() : 0.0f,
                              a.size() > 2 ? a[2].AsFloat() : 0.0f}};
        };
        b["vec2"] = [](std::vector<Value>& a) -> Value {
            return Value{Vec3{a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f, 0.0f}};
        };
        b["vec_cross"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{Vec3::Zero};
            Vec3 u = a[0].AsVec3(), v = a[1].AsVec3();
            return Value{Vec3{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x}};
        };
        b["vec_reflect"] = [](std::vector<Value>& a) -> Value {
            // Reflect direction d about a normal n:  d - 2*(d·n)*n.
            if (a.size() < 2) return Value{Vec3::Zero};
            Vec3 d = a[0].AsVec3(), n = a[1].AsVec3();
            float dn = d.x * n.x + d.y * n.y + d.z * n.z;
            return Value{Vec3{d.x - 2.0f * dn * n.x, d.y - 2.0f * dn * n.y, d.z - 2.0f * dn * n.z}};
        };
        b["vec_clamp_len"] = [](std::vector<Value>& a) -> Value {
            // Clamp a vector's magnitude to at most maxLen (Vector3.ClampMagnitude).
            if (a.empty()) return Value{Vec3::Zero};
            Vec3 v = a[0].AsVec3(); float maxLen = a.size() > 1 ? a[1].AsFloat() : 1.0f;
            float m = v.Magnitude();
            if (m > maxLen && m > 0.0001f) { float s = maxLen / m; v = Vec3{v.x * s, v.y * s, v.z * s}; }
            return Value{v};
        };
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
        // Smooth 2D value noise in [0,1].
        b["noise"] = [](std::vector<Value>& a) -> Value {
            float x = a.size() > 0 ? a[0].AsFloat() : 0.0f;
            float y = a.size() > 1 ? a[1].AsFloat() : 0.0f;
            // Unsigned arithmetic wraps instead of overflowing (same values as before).
            auto hash = [](unsigned xi, unsigned yi) {
                unsigned h = xi * 374761393u + yi * 668265263u;
                h = (h ^ (h >> 13)) * 1274126177u;
                return (float)((h ^ (h >> 16)) & 0xFFFFFFu) / (float)0xFFFFFFu;
            };
            // Lattice cell (floor of v, wrapped mod 2^32 like the hash) and the
            // fraction within it, safe for huge or non-finite coordinates.
            auto cell = [](float v, unsigned& c) -> float {
                if (!std::isfinite(v)) { c = 0; return 0.0f; }
                const double f = std::floor((double)v);
                c = (unsigned)(long long)std::fmod(f, 4294967296.0);
                return (float)((double)v - f);
            };
            unsigned x0, y0;
            float fx = cell(x, x0), fy = cell(y, y0);
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
        // A few more math helpers scripts reach for (clamp01 / lerp_angle
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

        // Method-name aliases mapping C#-style calls onto existing builtins.
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
        alias("shake", "tween_shake");   // shake(intensity, dur) — shorter name
        alias("face", "look_at");        // face("Name") — turn to look at an object
        alias("place_at", "set_pos");
        alias("set_position_x", "set_x");
        alias("set_position_y", "set_y");
        alias("position_x", "pos_x");
        alias("position_y", "pos_y");
        alias("position_z", "pos_z");
        alias("turn", "rotate");
        alias("face", "look_at");
        alias("chase", "follow");
        alias("run_from", "flee");
        alias("wasd_move", "on_key_move");
        alias("wasd_move3", "on_key_move3");
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
        alias("shake_camera", "camera_shake");
        alias("cam_shake", "camera_shake");
        alias("zoom_camera", "camera_zoom_to");
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

        // ---- C# compatibility -------------------------------------------
        // Collections created by `new List<T>()`, `new Dictionary<K,V>()`, `new T[n]`
        // (the parser lowers those to these calls). Lists/arrays share one type.
        b["__new_list"] = [](std::vector<Value>& a) {
            Value v = Value::MakeArray();
            if (!a.empty()) if (auto src = a[0].AsArray()) *v.AsArray() = *src;   // initializer / copy
            return v;
        };
        b["__new_array"] = [](std::vector<Value>& a) {
            Value v = Value::MakeArray();
            int n = a.empty() ? 0 : (int)a[0].AsFloat();
            if (n > 0) v.AsArray()->assign((std::size_t)std::min(n, 1 << 20), Value{0.0f});
            return v;
        };
        b["__new_map"] = [](std::vector<Value>& a) {
            Value v = Value::MakeMap();
            if (a.empty()) return v;
            if (auto src = a[0].AsMap()) { *v.AsMap() = *src; return v; }
            if (auto pairs = a[0].AsArray())                          // { {"a", 1}, {"b", 2} }
                for (auto& pr : *pairs)
                    if (auto kv = pr.AsArray(); kv && kv->size() >= 2) (*v.AsMap())[(*kv)[0].AsString()] = (*kv)[1];
            return v;
        };
        // Invoke("Fire", 2f) / InvokeRepeating("Spawn", 1f, 0.5f) / CancelInvoke().
        b["Invoke"] = [this](std::vector<Value>& a) {
            if (!a.empty()) rt.timers.push_back({a.size() > 1 ? a[1].AsFloat() : 0.0f, 0.0f, a[0].AsString(), false});
            return Value{};
        };
        b["InvokeRepeating"] = [this](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            float first = a.size() > 1 ? a[1].AsFloat() : 0.0f, rate = a.size() > 2 ? a[2].AsFloat() : 1.0f;
            if (rate <= 0.0f) rate = 0.0001f;
            rt.timers.push_back({first, rate, a[0].AsString(), false});
            return Value{};
        };
        b["CancelInvoke"] = [this](std::vector<Value>& a) {
            if (a.empty()) rt.timers.clear();
            else { std::string fn = a[0].AsString(); for (auto& t : rt.timers) if (t.fn == fn) t.dead = true; }
            return Value{};
        };
        // transform.Translate(Vector3 / x, y, z) moves in the object's own space
        //; pass Space.World to move in world axes.
        // The two-number OkayScript form translate(x, y) stays a plain 2D move.
        b["transform.Translate"] = [tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || a.empty()) return Value{};
            if (a.size() == 2 && !a[0].IsVec3()) { t->Translate({a[0].AsFloat(), a[1].AsFloat(), 0.0f}); return Value{}; }
            Vec3 d; std::size_t next;
            if (a[0].IsVec3()) { d = a[0].AsVec3(); next = 1; }
            else { d = {a[0].AsFloat(), a.size() > 1 ? a[1].AsFloat() : 0.0f, a.size() > 2 ? a[2].AsFloat() : 0.0f}; next = 3; }
            const bool world = a.size() > next && a[next].AsFloat() != 0.0f;
            t->Translate(world ? d : t->localRotation * d);
            return Value{};
        };
        // transform.Rotate(x, y, z) / Rotate(Vector3) / Rotate(axis, angle); one number = 2D spin about Z.
        b["transform.Rotate"] = [tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || a.empty()) return Value{};
            if (a[0].IsVec3() && a.size() >= 2 && !a[1].IsVec3() && a[1].AsFloat() != 0.0f && a.size() == 2 &&
                std::fabs(a[0].AsVec3().Magnitude() - 1.0f) < 1e-3f) {
                t->localRotation = (t->localRotation * Quat::AngleAxis(a[1].AsFloat(), a[0].AsVec3())).Normalized();
                return Value{};
            }
            Vec3 e = a[0].IsVec3() ? a[0].AsVec3()
                   : (a.size() == 1 ? Vec3{0, 0, a[0].AsFloat()}
                                    : Vec3{a[0].AsFloat(), a.size() > 1 ? a[1].AsFloat() : 0.0f, a.size() > 2 ? a[2].AsFloat() : 0.0f});
            t->Rotate(e);
            return Value{};
        };
        alias("Debug.LogWarning", "log_warn");
        alias("Debug.LogError", "log_error");
        alias("Debug.LogFormat", "format");
        b["Input.GetAxisRaw"] = [](std::vector<Value>& a) -> Value {
            std::string ax = a.empty() ? std::string{} : a[0].AsString();
            float v = ax == "Vertical" ? Input::AxisWASD().y : Input::AxisWASD().x;
            return Value{v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f)};
        };
        b["string.IsNullOrEmpty"] = [](std::vector<Value>& a) { return Value{a.empty() || a[0].AsString().empty()}; };
        b["string.IsNullOrWhiteSpace"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{true};
            for (char c : a[0].AsString()) if (!std::isspace((unsigned char)c)) return Value{false};
            return Value{true};
        };
        b["string.Join"] = [](std::vector<Value>& a) {   // string.Join(", ", items)
            std::string sep = a.empty() ? "" : a[0].AsString(), out;
            if (a.size() > 1)
                if (auto arr = a[1].AsArray())
                    for (std::size_t i = 0; i < arr->size(); ++i) { if (i) out += sep; out += (*arr)[i].AsString(); }
            return Value{out};
        };
        alias("String.IsNullOrEmpty", "string.IsNullOrEmpty");
        alias("String.Join", "string.Join");
        auto parseNum = [](std::vector<Value>& a, bool asInt) {
            float f = 0.0f;
            if (!a.empty()) { try { f = std::stof(a[0].AsString()); } catch (...) { f = 0.0f; } }
            return Value{asInt ? (float)(long long)f : f};
        };
        b["int.Parse"]   = [parseNum](std::vector<Value>& a) { return parseNum(a, true); };
        b["float.Parse"] = [parseNum](std::vector<Value>& a) { return parseNum(a, false); };
        alias("double.Parse", "float.Parse");
        alias("Convert.ToInt32", "int.Parse");
        alias("Convert.ToSingle", "float.Parse");
        b["Mathf.RoundToInt"] = [](std::vector<Value>& a) { return Value{a.empty() ? 0.0f : std::round(a[0].AsFloat())}; };
        b["Mathf.FloorToInt"] = [](std::vector<Value>& a) { return Value{a.empty() ? 0.0f : std::floor(a[0].AsFloat())}; };
        b["Mathf.CeilToInt"]  = [](std::vector<Value>& a) { return Value{a.empty() ? 0.0f : std::ceil(a[0].AsFloat())}; };
        b["Mathf.Log10"]      = [](std::vector<Value>& a) { return Value{a.empty() ? 0.0f : std::log10(a[0].AsFloat())}; };
        alias("Vector3.Cross", "vec_cross");
        alias("Vector3.ClampMagnitude", "vec_clamp_len");
        alias("Vector3.Reflect", "vec_reflect");
        alias("Mathf.Clamp01", "clamp01");

        // ---- Coroutines: StartCoroutine(Routine()) + yield return new WaitForSeconds(t) ----
        b["WaitForSeconds"] = [](std::vector<Value>& a) {
            Value v = Value::MakeMap(); (*v.AsMap())["__wait"] = Value{a.empty() ? 0.0f : a[0].AsFloat()}; return v;
        };
        alias("WaitForSecondsRealtime", "WaitForSeconds");
        b["WaitForEndOfFrame"] = [](std::vector<Value>&) { return Value{}; };
        alias("WaitForFixedUpdate", "WaitForEndOfFrame");
        b["StartCoroutine"] = [this](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            std::vector<Value> args(a.begin() + 1, a.end());
            int id = rt.StartCoroutine(a[0].AsString(), std::move(args));
            Value v = Value::MakeMap(); (*v.AsMap())["__co"] = Value{(float)id}; return v;
        };
        b["StopCoroutine"] = [this](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            if (auto m = a[0].AsMap()) {
                if (auto it = m->find("__co"); it != m->end())
                    if (auto* co = rt.FindCo((int)it->second.AsFloat())) { rt.StopCoroutine(co); rt.ReapCoroutines(); }
                return Value{};
            }
            rt.StopCoroutines(a[0].AsString().empty() ? std::string("\x01") : a[0].AsString());
            return Value{};
        };
        b["StopAllCoroutines"] = [this](std::vector<Value>&) { rt.StopCoroutines(""); return Value{}; };

        // ---- Objects: Destroy(obj[, seconds]), Instantiate(prefab, pos, rot), CompareTag ----
        b["Destroy"] = [this, findNamed](std::vector<Value>& a) {
            std::string target = a.empty() ? std::string() : a[0].AsString();
            if (IsComponentRef(a.empty() ? Value{} : a[0])) return Value{};   // Destroy(component): ignore
            const float delay = a.size() > 1 ? a[1].AsFloat() : 0.0f;
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            if (me && target == me->name) target.clear();
            if (delay > 0.0f) { rt.pendingDestroy.push_back({delay, target}); return Value{}; }
            GameObject* g = findNamed(target);
            if (g && g->scene()) g->scene()->Destroy(g);
            return Value{};
        };
        alias("Object.Destroy", "Destroy");
        b["Instantiate"] = [this](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            if (a.size() >= 2 && a[1].IsVec3()) {   // Instantiate(prefab, position, rotation)
                Vec3 p = a[1].AsVec3();
                std::vector<Value> b3{a[0], Value{p.x}, Value{p.y}, Value{p.z}};
                return rt.Call("spawn3", b3);
            }
            return rt.Call("spawn", a);
        };
        alias("Object.Instantiate", "Instantiate");
        // GameObject.Find("Enemy") -> a reference to it (its name), or null.
        b["GameObject.Find"] = [findNamed](std::vector<Value>& a) -> Value {
            if (a.empty() || a[0].AsString().empty()) return Value{};
            GameObject* g = findNamed(a[0].AsString());
            return g ? Value{g->name} : Value{};
        };
        b["GameObject.FindWithTag"] = [this](std::vector<Value>& a) -> Value {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            if (s && !a.empty()) for (const auto& up : s->Objects()) if (up && up->active && up->tag == a[0].AsString()) return Value{up->name};
            return Value{};
        };
        alias("GameObject.FindGameObjectWithTag", "GameObject.FindWithTag");
        b["GameObject.FindGameObjectsWithTag"] = [this](std::vector<Value>& a) -> Value {
            Value out = Value::MakeArray();
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            if (s && !a.empty()) for (const auto& up : s->Objects()) if (up && up->active && up->tag == a[0].AsString()) out.AsArray()->push_back(Value{up->name});
            return out;
        };
        b["CompareTag"] = [go](std::vector<Value>& a) {
            GameObject* g = go(); return Value{g && !a.empty() && g->tag == a[0].AsString()};
        };
        alias("gameObject.CompareTag", "CompareTag");
        b["__object_CompareTag"] = [findNamed](std::vector<Value>& a) {
            GameObject* g = a.empty() ? nullptr : findNamed(a[0].AsString());
            return Value{g && a.size() > 1 && g->tag == a[1].AsString()};
        };
        b["__object_SetActive"] = [this](std::vector<Value>& a) {
            if (Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr)
                if (!a.empty())
                    for (const auto& up : s->Objects())
                        if (up && up->name == a[0].AsString()) { up->active = a.size() < 2 || a[1].AsBool(); break; }
            return Value{};
        };
        b["transform.LookAt"] = [this, tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || a.empty()) return Value{};
            if (!a[0].IsVec3()) return rt.Call("look_at", a);
            Vec3 d = a[0].AsVec3() - t->Position();
            if (d.Magnitude() > 1e-6f) t->localRotation = Quat::LookRotation(d.Normalized());
            return Value{};
        };

        // ---- Components: GetComponent<T>() returns a reference ("@T") whose
        // fields / methods map onto the engine component. ----
        auto compOf = [](GameObject* g, const std::string& t) -> bool {
            if (!g) return false;
            if (t == "Transform" || t == "GameObject") return true;
            if (t == "SpriteRenderer") return g->GetComponent<SpriteRenderer>();
            if (t == "MeshRenderer" || t == "Renderer" || t == "SkinnedMeshRenderer")
                return g->GetComponent<MeshRenderer>() || g->GetComponent<SpriteRenderer>();
            if (t == "TextRenderer" || t == "Text" || t == "TMP_Text" || t == "TextMeshPro" || t == "TextMeshProUGUI")
                return g->GetComponent<TextRenderer>();
            if (t == "Rigidbody2D") return g->GetComponent<Rigidbody2D>();
            if (t == "Rigidbody")   return g->GetComponent<Rigidbody3D>();
            if (t == "AudioSource") return g->GetComponent<AudioSource>();
            if (t == "Animator" || t == "Animation") return true;   // resolved by the anim_* builtins
            return false;
        };
        // The engine component a C# type name refers to (for .enabled).
        auto compPtr = [](GameObject* g, const std::string& t) -> Component* {
            if (!g) return nullptr;
            if (t == "SpriteRenderer") return g->GetComponent<SpriteRenderer>();
            if (t == "MeshRenderer" || t == "SkinnedMeshRenderer") return g->GetComponent<MeshRenderer>();
            if (t == "Renderer") { if (auto* m = g->GetComponent<MeshRenderer>()) return m; return g->GetComponent<SpriteRenderer>(); }
            if (t == "TextRenderer" || t == "Text" || t == "TMP_Text" || t == "TextMeshPro" || t == "TextMeshProUGUI")
                return g->GetComponent<TextRenderer>();
            if (t == "Rigidbody2D") return g->GetComponent<Rigidbody2D>();
            if (t == "Rigidbody")   return g->GetComponent<Rigidbody3D>();
            if (t == "AudioSource") return g->GetComponent<AudioSource>();
            return nullptr;
        };
        b["GetComponent"] = [this, compOf](std::vector<Value>& a) -> Value {
            if (!rt.host || !rt.host->gameObject || a.empty()) return Value{};
            const std::string c = a[0].AsString();
            return compOf(rt.host->gameObject, c) ? Value{"@" + c} : Value{};
        };
        alias("gameObject.GetComponent", "GetComponent");
        alias("TryGetComponent", "GetComponent");
        b["__object_GetComponent"] = [findNamed, compOf](std::vector<Value>& a) -> Value {
            GameObject* g = a.empty() ? nullptr : findNamed(a[0].AsString());
            return (g && a.size() > 1 && compOf(g, a[1].AsString())) ? Value{"@" + a[1].AsString()} : Value{};
        };
        b["__get_component"] = [this, go, compPtr](std::vector<Value>& a) -> Value {
            GameObject* g = go(); if (!g || a.size() < 2) return Value{};
            const std::string t = a[0].AsString(), p = a[1].AsString();
            auto rgb = [](const Color& c) { return Value{Vec3{c.r, c.g, c.b}}; };
            if (t == "Transform") { bool ok = false; Value v = rt.GetDotted("transform." + p, ok); return v; }
            if (p == "enabled") { Component* c = compPtr(g, t); return Value{c ? c->enabled : true}; }
            if (auto* sr = g->GetComponent<SpriteRenderer>(); sr && (t == "SpriteRenderer" || t == "Renderer")) {
                if (p == "color" || p == "material.color") return rgb(sr->color);
                if (p == "flipX") return Value{sr->flipX};
            }
            if (auto* mr = g->GetComponent<MeshRenderer>(); mr && (t == "MeshRenderer" || t == "Renderer" || t == "SkinnedMeshRenderer"))
                if (p == "color" || p == "material.color") return rgb(mr->color);
            if (auto* tr = g->GetComponent<TextRenderer>()) {
                if (p == "text") return Value{tr->text};
                if (p == "color") return rgb(tr->color);
            }
            if (auto* rb = g->GetComponent<Rigidbody2D>(); rb && t == "Rigidbody2D") {
                if (p == "velocity" || p == "linearVelocity") return Value{Vec3{rb->velocity.x, rb->velocity.y, 0.0f}};
                if (p == "gravityScale") return Value{rb->gravityScale};
            }
            if (auto* rb = g->GetComponent<Rigidbody3D>(); rb && t == "Rigidbody")
                if (p == "velocity" || p == "linearVelocity") return Value{rb->velocity};
            if (auto* au = g->GetComponent<AudioSource>(); au && t == "AudioSource")
                if (p == "volume") return Value{au->volume};
            return Value{};
        };
        b["__set_component"] = [this, go, compPtr](std::vector<Value>& a) -> Value {
            GameObject* g = go(); if (!g || a.size() < 3) return Value{};
            const std::string t = a[0].AsString(), p = a[1].AsString();
            const Value& v = a[2];
            auto col = [&](const Color& old) {
                if (v.IsVec3()) { Vec3 c = v.AsVec3(); return Color{c.x, c.y, c.z, old.a}; }
                return old;
            };
            if (t == "Transform") { rt.SetDotted("transform." + p, v); return Value{}; }
            if (p == "enabled") { if (Component* c = compPtr(g, t)) c->enabled = v.AsBool(); return Value{}; }
            if (p == "color" || p == "material.color") {
                if (auto* sr = g->GetComponent<SpriteRenderer>(); sr && (t == "SpriteRenderer" || t == "Renderer")) sr->color = col(sr->color);
                if (auto* mr = g->GetComponent<MeshRenderer>(); mr && t != "SpriteRenderer") mr->color = col(mr->color);
                if (auto* tr = g->GetComponent<TextRenderer>(); tr && (t == "Text" || t == "TextRenderer" || t.rfind("TMP", 0) == 0 || t.rfind("TextMesh", 0) == 0)) tr->color = col(tr->color);
                return Value{};
            }
            if (p == "flipX") if (auto* sr = g->GetComponent<SpriteRenderer>()) sr->flipX = v.AsBool();
            if (p == "text") if (auto* tr = g->GetComponent<TextRenderer>()) tr->text = v.AsString();
            if (t == "Rigidbody2D") if (auto* rb = g->GetComponent<Rigidbody2D>()) {
                if (p == "velocity" || p == "linearVelocity") { Vec3 q = v.AsVec3(); rb->velocity = {q.x, q.y}; }
                if (p == "gravityScale") rb->gravityScale = v.AsFloat();
            }
            if (t == "Rigidbody") if (auto* rb = g->GetComponent<Rigidbody3D>())
                if (p == "velocity" || p == "linearVelocity") rb->velocity = v.AsVec3();
            if (t == "AudioSource") if (auto* au = g->GetComponent<AudioSource>()) if (p == "volume") au->volume = v.AsFloat();
            return Value{};
        };
        b["__call_component"] = [this, go](std::vector<Value>& a) -> Value {
            GameObject* g = go(); if (!g || a.size() < 2) return Value{};
            const std::string t = a[0].AsString(), m = a[1].AsString();
            std::vector<Value> rest(a.begin() + 2, a.end());
            auto arg = [&](std::size_t i) { return i < rest.size() ? rest[i] : Value{}; };
            if (t == "Animator" || t == "Animation") {
                if (m == "SetTrigger") { std::vector<Value> x{arg(0)}; return rt.Call("anim_trigger", x); }
                if (m == "SetBool")    { std::vector<Value> x{arg(0), Value{arg(1).AsBool() ? 1.0f : 0.0f}}; return rt.Call("anim_set_bool", x); }
                if (m == "SetFloat" || m == "SetInteger") { std::vector<Value> x{arg(0), arg(1)}; return rt.Call("anim_set_float", x); }
                if (m == "Play" || m == "CrossFade" || m == "CrossFadeInFixedTime") { std::vector<Value> x{arg(0)}; return rt.Call("play_clip", x); }
                return Value{};
            }
            if (t == "Rigidbody2D") if (auto* rb = g->GetComponent<Rigidbody2D>()) {
                Vec3 f = arg(0).IsVec3() ? arg(0).AsVec3() : Vec3{arg(0).AsFloat(), arg(1).AsFloat(), 0.0f};
                const bool impulse = rest.size() >= 2 && arg(0).IsVec3() && arg(1).AsFloat() != 0.0f;   // ForceMode2D.Impulse
                if (m == "AddForce") { if (impulse) rb->AddImpulse({f.x, f.y}); else rb->AddForce({f.x, f.y}); }
                if (m == "MovePosition" && g->transform) g->transform->localPosition = {f.x, f.y, g->transform->localPosition.z};
                return Value{};
            }
            if (t == "Rigidbody") if (g->GetComponent<Rigidbody3D>()) {
                Vec3 f = arg(0).IsVec3() ? arg(0).AsVec3() : Vec3{arg(0).AsFloat(), arg(1).AsFloat(), arg(2).AsFloat()};
                if (m == "AddForce") { std::vector<Value> x{Value{f.x}, Value{f.y}, Value{f.z}}; return rt.Call("add_force3", x); }
                if (m == "MovePosition" && g->transform) g->transform->localPosition = f;
                return Value{};
            }
            if (t == "AudioSource") if (auto* au = g->GetComponent<AudioSource>()) {
                if (m == "Play") au->Play();
                if (m == "Stop") au->Stop();
                return Value{};
            }
            if (t == "Transform") {
                std::vector<Value> x = rest;
                return rt.Call("transform." + m, x);
            }
            return Value{};
        };
        b["ForceMode2D.Impulse"] = [](std::vector<Value>&) { return Value{1.0f}; };
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
        m_impl->rt.StopCoroutines("");
        m_impl->rt.pendingDestroy.clear();
        m_impl->rt.functions.clear();
        m_impl->rt.statics = SharedStatics(source);
        m_impl->topLevel = parser.ParseProgram(m_impl->rt.functions);
        // Bare mode: no functions defined -> the top-level statements ARE the update
        // loop (run every frame), so a whole script can be a single line. Otherwise run
        // top-level once now for globals/setup.
        m_impl->bareMode = m_impl->rt.functions.empty() && !m_impl->topLevel.empty();
        // Always run the top-level once now so globals/setup exist right after
        // Load (the contract every caller relies on). In bare mode the top-level
        // is also the per-frame loop, so this first run is the first tick — the
        // first Update skips re-running it to keep the overall tick count intact.
        for (auto& s : m_impl->topLevel) s->Exec(m_impl->rt);
        m_impl->bareSkipFirstUpdate = m_impl->bareMode;
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

void OkayScriptVM::AddBuiltin(const std::string& name, Builtin fn) {
    m_impl->rt.builtins[name] = std::move(fn);
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
// Lets a script use PascalCase (Start/Update) or the classic
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
    CallFirst(m_impl->rt, {"Awake", "awake"}, none, "Awake");   // Awake, then OnEnable, then Start
    CallFirst(m_impl->rt, {"OnEnable", "on_enable"}, none, "OnEnable");
    CallFirst(m_impl->rt, {"Start", "start"}, none, "Start");
}

void OkayScriptVM::CallUpdate(float deltaTime) {
    if (!m_impl->loaded) return;
    if (m_impl->rt.host) m_impl->rt.host->deltaTime = deltaTime;
    std::vector<Value> args{Value{deltaTime}};
    // Bare mode: re-run the top-level statements every frame (they ARE the loop).
    // Load already ran them once, so skip that first re-run to avoid double-ticking.
    if (m_impl->bareMode) {
        if (m_impl->bareSkipFirstUpdate) {
            m_impl->bareSkipFirstUpdate = false;
        } else {
            try { for (auto& s : m_impl->topLevel) s->Exec(m_impl->rt); }
            catch (const ReturnSignal&) {}     // a stray top-level return/break/continue
            catch (const BreakSignal&) {}      // ends this frame's run, like it does in Load
            catch (const ContinueSignal&) {}
            catch (const std::exception& e) { Log::Error("OkayScript (bare): ", e.what()); }
        }
    }
    CallFirst(m_impl->rt, {"Update", "update"}, args, "Update");
    CallFirst(m_impl->rt, {"LateUpdate", "late_update"}, args, "LateUpdate");
    // Scheduled after()/every() callbacks tick even without an update().
    if (!m_impl->rt.timers.empty()) {
        try { m_impl->rt.TickTimers(deltaTime); }
        catch (const std::exception& e) { Log::Error("OkayScript timer: ", e.what()); }
    }
    Runtime& rt = m_impl->rt;
    if (!rt.coroutines.empty()) {
        try { rt.TickCoroutines(deltaTime); }
        catch (const std::exception& e) { Log::Error("OkayScript coroutine: ", e.what()); }
    }
    ++rt.frame;
    // Destroy(obj, seconds) countdowns.
    if (!rt.pendingDestroy.empty() && rt.host && rt.host->gameObject && rt.host->gameObject->scene()) {
        Scene* sc = rt.host->gameObject->scene();
        std::vector<std::string> due;
        for (auto& pd : rt.pendingDestroy) { pd.first -= deltaTime; if (pd.first <= 0.0f) due.push_back(pd.second); }
        rt.pendingDestroy.erase(std::remove_if(rt.pendingDestroy.begin(), rt.pendingDestroy.end(),
                                               [](const auto& pd) { return pd.first <= 0.0f; }), rt.pendingDestroy.end());
        GameObject* me = rt.host->gameObject;
        for (const auto& n : due) {
            GameObject* g = n.empty() ? me : sc->Find(n);
            if (g) sc->Destroy(g);
            if (g == me) break;   // this script is going away with its object
        }
    }
}

void OkayScriptVM::CallEvent(const std::string& function) {
    CallEventWith(function, vs::VsValue{});
}

namespace {
// The handler a script defines for an engine event name: the requested name
// wins; otherwise an alias, so scripts can name handlers in PascalCase
// (OnCollisionEnter, OnClick, ...). "" = none.
template <class Functions>
std::string ResolveEventHandler(const Functions& functions, const std::string& function) {
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
        // Lifecycle the ScriptComponent drives.
        {"on_fixed_update", {"FixedUpdate", "fixed_update"}},
        {"on_enable",       {"OnEnable"}},
        {"on_disable",      {"OnDisable"}},
        {"on_destroy",      {"OnDestroy"}},
        // Build mode / logic links (BuildMode, LogicTarget).
        {"on_object_placed",  {"OnObjectPlaced"}},
        {"on_object_removed", {"OnObjectRemoved"}},
        {"on_logic_signal",   {"OnLogicSignal"}},
        // Anti-cheat (AntiCheatConfig raises these on the host).
        {"on_cheat_detected", {"OnCheatDetected"}},
        {"on_validate_rpc",   {"OnValidateRpc"}},
        {"on_validate_hit",   {"OnValidateHit"}},
    };
    if (functions.count(function)) return function;
    auto it = kAliases.find(function);
    if (it != kAliases.end())
        for (const auto& alt : it->second)
            if (functions.count(alt)) return alt;
    return {};
}
} // namespace

void OkayScriptVM::CallEventWith(const std::string& function, const vs::VsValue& arg) {
    CallEventArgs(function, std::vector<vs::VsValue>{arg});
}

bool OkayScriptVM::HasEvent(const std::string& function) const {
    return m_impl->loaded && !ResolveEventHandler(m_impl->rt.functions, function).empty();
}

vs::VsValue OkayScriptVM::CallEventArgs(const std::string& function, const std::vector<vs::VsValue>& given,
                                        std::string* error) {
    if (!m_impl->loaded) return {};
    const std::string fn = ResolveEventHandler(m_impl->rt.functions, function);
    if (fn.empty()) return {};
    // Up to as many arguments as the handler declares (OnTriggerEnter(Collider
    // other) takes one; a handler with none gets none). Call gives any it
    // declares beyond those its usual default for the parameter's type.
    const std::size_t want = std::min(m_impl->rt.functions[fn].params.size(), given.size());
    std::vector<Value> args(given.begin(), given.begin() + static_cast<std::ptrdiff_t>(want));
    try { return m_impl->rt.Call(fn, args); }
    catch (const std::exception& e) {
        if (error) *error = e.what();
        else Log::Error("OkayScript ", fn.c_str(), "(): ", e.what());
    }
    return {};
}

vs::VsValue OkayScriptVM::GetGlobal(const std::string& name) const {
    auto& g = m_impl->rt.Global().vars;
    auto it = g.find(name);
    return it != g.end() ? it->second : vs::VsValue{};
}

void OkayScriptVM::SetGlobal(const std::string& name, const vs::VsValue& v) {
    m_impl->rt.SetGlobalChecked(name, v);
}

} // namespace okay
