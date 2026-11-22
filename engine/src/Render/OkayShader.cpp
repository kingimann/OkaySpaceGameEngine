// OkayShader: lexer, parser, type checker, GLSL / HLSL emitters, CPU register program,
// material property text and the shader file cache. See okay/Render/OkayShader.hpp and
// docs/rendering/shaders.md.
//
// Written by us from the GLSL 1.20 / GLSL ES 3.00 and HLSL Shader Model 4 language
// references and general compiler techniques (hand-written lexer, recursive descent with
// one function per precedence level, a separate checking pass over a node pool, tree-walking
// emitters, a linear register code with forward jumps and counted loops). No code taken from
// any other shading language, compiler or engine.
#include "okay/Render/OkayShader.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Debug/DebugUtil.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace okay {
namespace oshader {

const char* TypeName(Type t) {
    switch (t) {
        case Type::Void: return "void";
        case Type::Bool: return "bool";
        case Type::Float: return "float";
        case Type::Vec2: return "vec2";
        case Type::Vec3: return "vec3";
        case Type::Vec4: return "vec4";
        case Type::Texture: return "texture";
    }
    return "?";
}
int Width(Type t) {
    switch (t) {
        case Type::Bool: case Type::Float: return 1;
        case Type::Vec2: return 2;
        case Type::Vec3: return 3;
        case Type::Vec4: return 4;
        default: return 0;
    }
}
static bool IsNumeric(Type t) { return t == Type::Float || t == Type::Vec2 || t == Type::Vec3 || t == Type::Vec4; }
static Type VecOf(int n) { return n == 1 ? Type::Float : n == 2 ? Type::Vec2 : n == 3 ? Type::Vec3 : n == 4 ? Type::Vec4 : Type::Void; }

std::string Diagnostic::ToString(const std::string& file) const {
    std::string s;
    if (!file.empty()) s = file + ":";
    s += std::to_string(line) + ":" + std::to_string(col) + ": error: " + message;
    return s;
}
std::string Compiled::FirstError(const std::string& file) const {
    return errors.empty() ? std::string() : errors[0].ToString(file);
}

namespace {

// ---- limits (keep hostile input cheap) -----------------------------------------------
constexpr std::size_t kMaxSource = 1u << 20;   // 1 MB of text
constexpr int kMaxDepth = 96;                  // nested expressions / statements
constexpr int kMaxLoopCount = 64;              // iterations of one loop
constexpr long kMaxLoopProduct = 4096;         // iterations of nested loops together
constexpr int kMaxRegs = 1024;                 // CPU register file (floats), values + temporaries
constexpr int kMaxConsts = 1024;
constexpr int kMaxErrors = 16;

// =====================================================================================
// Lexer
// =====================================================================================
enum class Tk : std::uint8_t { End, Ident, Number, String, Punct };
struct Token {
    Tk kind = Tk::End;
    std::string text;   // identifier / string value / punctuation
    double num = 0.0;
    int line = 1, col = 1;
};

struct Fail {};   // thrown inside the parser on the first syntax error (caught in Compile)

class Lexer {
public:
    explicit Lexer(const std::string& s) : m_s(s) {}
    bool Run(std::vector<Token>& out, Diagnostic& err) {
        for (;;) {
            SkipSpace();
            if (m_failed) { err = m_err; return false; }
            Token t; t.line = m_line; t.col = m_col;
            if (m_i >= m_s.size()) { t.kind = Tk::End; out.push_back(t); return true; }
            const unsigned char c = (unsigned char)m_s[m_i];
            if (std::isalpha(c) || c == '_') {
                std::size_t b = m_i;
                while (m_i < m_s.size() && (std::isalnum((unsigned char)m_s[m_i]) || m_s[m_i] == '_')) Adv();
                t.kind = Tk::Ident; t.text = m_s.substr(b, m_i - b);
                if (t.text.size() > 64) { err = {t.line, t.col, "name is too long (at most 64 characters)"}; return false; }
            } else if (std::isdigit(c) || (c == '.' && m_i + 1 < m_s.size() && std::isdigit((unsigned char)m_s[m_i + 1]))) {
                std::size_t b = m_i;
                while (m_i < m_s.size() && std::isdigit((unsigned char)m_s[m_i])) Adv();
                if (m_i < m_s.size() && m_s[m_i] == '.' && !(m_i + 1 < m_s.size() && m_s[m_i + 1] == '.')) {
                    Adv();
                    while (m_i < m_s.size() && std::isdigit((unsigned char)m_s[m_i])) Adv();
                }
                if (m_i < m_s.size() && (m_s[m_i] == 'e' || m_s[m_i] == 'E')) {
                    std::size_t save = m_i; int sl = m_line, sc = m_col;
                    Adv();
                    if (m_i < m_s.size() && (m_s[m_i] == '+' || m_s[m_i] == '-')) Adv();
                    if (m_i < m_s.size() && std::isdigit((unsigned char)m_s[m_i])) {
                        while (m_i < m_s.size() && std::isdigit((unsigned char)m_s[m_i])) Adv();
                    } else { m_i = save; m_line = sl; m_col = sc; }
                }
                const std::string txt = m_s.substr(b, m_i - b);
                if (txt.size() > 40) { err = {t.line, t.col, "number is too long"}; return false; }
                const double v = std::strtod(txt.c_str(), nullptr);
                if (!std::isfinite(v) || v > 3.0e38) { err = {t.line, t.col, "number '" + txt + "' is out of range"}; return false; }
                t.kind = Tk::Number; t.num = v; t.text = txt;
            } else if (c == '"') {
                Adv();
                std::string v;
                for (;;) {
                    if (m_i >= m_s.size() || m_s[m_i] == '\n') { err = {t.line, t.col, "string is never closed"}; return false; }
                    char ch = m_s[m_i];
                    if (ch == '"') { Adv(); break; }
                    if (ch == '\\' && m_i + 1 < m_s.size()) { Adv(); ch = m_s[m_i]; }
                    v += ch; Adv();
                    if (v.size() > 1024) { err = {t.line, t.col, "string is too long"}; return false; }
                }
                t.kind = Tk::String; t.text = v;
            } else {
                static const char* two[] = {"..", "==", "!=", "<=", ">=", "&&", "||", "+=", "-=", "*=", "/="};
                bool got = false;
                if (m_i + 1 < m_s.size())
                    for (const char* p : two)
                        if (m_s[m_i] == p[0] && m_s[m_i + 1] == p[1]) { t.text = p; Adv(); Adv(); got = true; break; }
                if (!got) {
                    if (!std::strchr("(){};.,=+-*/<>!?:", (char)c) || c == 0) {
                        char buf[32];
                        if (c >= 32 && c < 127) std::snprintf(buf, sizeof buf, "'%c'", (char)c);
                        else std::snprintf(buf, sizeof buf, "byte 0x%02X", (unsigned)c);
                        err = {t.line, t.col, std::string("unexpected character ") + buf};
                        return false;
                    }
                    t.text = std::string(1, (char)c); Adv();
                }
                t.kind = Tk::Punct;
            }
            out.push_back(std::move(t));
            if (out.size() > 200000) { err = {m_line, m_col, "source has too many tokens"}; return false; }
        }
    }
private:
    void Adv() {
        if (m_s[m_i] == '\n') { ++m_line; m_col = 1; } else ++m_col;
        ++m_i;
    }
    void SkipSpace() {
        for (;;) {
            while (m_i < m_s.size() && (m_s[m_i] == ' ' || m_s[m_i] == '\t' || m_s[m_i] == '\n' || m_s[m_i] == '\r')) Adv();
            if (m_i + 1 < m_s.size() && m_s[m_i] == '/' && m_s[m_i + 1] == '/') {
                while (m_i < m_s.size() && m_s[m_i] != '\n') Adv();
                continue;
            }
            if (m_i + 1 < m_s.size() && m_s[m_i] == '/' && m_s[m_i + 1] == '*') {
                const int l = m_line, c = m_col;
                Adv(); Adv();
                for (;;) {
                    if (m_i + 1 >= m_s.size()) { m_failed = true; m_err = {l, c, "comment is never closed"}; return; }
                    if (m_s[m_i] == '*' && m_s[m_i + 1] == '/') { Adv(); Adv(); break; }
                    Adv();
                }
                continue;
            }
            return;
        }
    }
    const std::string& m_s;
    std::size_t m_i = 0;
    int m_line = 1, m_col = 1;
    bool m_failed = false;
    Diagnostic m_err;
};

// =====================================================================================
// Syntax tree (node pools: no deep destructor recursion)
// =====================================================================================
enum class SymKind : std::uint8_t { Input, Output, Property, Texture, Local, LoopVar, VInput, VOutput };
struct Symbol {
    std::string name;
    SymKind kind = SymKind::Local;
    Type type = Type::Float;
    int index = 0;      // input/output id, property / texture index, local id
    int slot = -1;      // CPU register
    int depth = 0;      // scope depth (locals)
    std::uint8_t stage = 0;   // 0 = any block, 1 = surface block only, 2 = vertex block only
};

// Fixed inputs and outputs, in CPU slot order.
struct Fixed { const char* name; Type type; const char* glsl; };
const Fixed kInputs[] = {
    {"uv", Type::Vec2, "ok_uv"}, {"position", Type::Vec3, "ok_pos"}, {"view", Type::Vec3, "ok_view"},
    {"vcolor", Type::Vec3, "ok_vcol"}, {"time", Type::Float, "ok_time"},
};
const Fixed kOutputs[] = {
    {"albedo", Type::Vec3, "s.albedo"}, {"emission", Type::Vec3, "s.emission"}, {"alpha", Type::Float, "s.alpha"},
    {"normal", Type::Vec3, "s.normal"}, {"metallic", Type::Float, "s.metallic"}, {"specular", Type::Float, "s.specular"},
    {"shininess", Type::Float, "s.shininess"},
};
constexpr int kInputSlots = 12;    // 2+3+3+3+1
constexpr int kOutputSlots = 13;   // 3+3+1+3+1+1+1
int InputSlot(int i) { static const int s[] = {0, 2, 5, 8, 11}; return s[i]; }
int OutputSlot(int i) { static const int s[] = {12, 15, 18, 19, 22, 23, 24}; return s[i]; }
// The vertex block's inputs and outputs. They reuse the surface register layout (uv, local in the
// position slot, vcolor, time; position in the albedo slot, normal in the normal slot), so one
// register file and one interpreter serve both blocks.
const Fixed kVInputs[] = {
    {"uv", Type::Vec2, "ok_uv"}, {"local", Type::Vec3, "ok_local"}, {"vcolor", Type::Vec3, "ok_vcol"}, {"time", Type::Float, "ok_time"},
};
const Fixed kVOutputs[] = { {"position", Type::Vec3, "ok_vpos"}, {"normal", Type::Vec3, "ok_vnrm"} };
int VInputSlot(int i) { static const int s[] = {0, 2, 8, 11}; return s[i]; }
int VOutputSlot(int i) { static const int s[] = {12, 19}; return s[i]; }

enum class Bi : std::uint8_t {
    None,
    // one generic argument, same type out
    Sin, Cos, Tan, Asin, Acos, Atan, Exp, Log, Exp2, Log2, Sqrt, Abs, Sign, Floor, Ceil, Fract, Saturate, Radians, Degrees, Normalize,
    // two generic
    Atan2, Pow, Min, Max, Mod, Step, Reflect,
    // three generic
    Clamp, Mix, Smoothstep,
    // to float
    Length, Distance, Dot, Luminance,
    Cross, Noise, Hash, Sample,
};
struct BiInfo { const char* name; Bi id; int args; };
const BiInfo kBuiltins[] = {
    {"sin", Bi::Sin, 1}, {"cos", Bi::Cos, 1}, {"tan", Bi::Tan, 1}, {"asin", Bi::Asin, 1}, {"acos", Bi::Acos, 1},
    {"atan", Bi::Atan, 1}, {"exp", Bi::Exp, 1}, {"log", Bi::Log, 1}, {"exp2", Bi::Exp2, 1}, {"log2", Bi::Log2, 1},
    {"sqrt", Bi::Sqrt, 1}, {"abs", Bi::Abs, 1}, {"sign", Bi::Sign, 1}, {"floor", Bi::Floor, 1}, {"ceil", Bi::Ceil, 1},
    {"fract", Bi::Fract, 1}, {"saturate", Bi::Saturate, 1}, {"radians", Bi::Radians, 1}, {"degrees", Bi::Degrees, 1},
    {"normalize", Bi::Normalize, 1},
    {"atan2", Bi::Atan2, 2}, {"pow", Bi::Pow, 2}, {"min", Bi::Min, 2}, {"max", Bi::Max, 2}, {"mod", Bi::Mod, 2},
    {"step", Bi::Step, 2}, {"reflect", Bi::Reflect, 2},
    {"clamp", Bi::Clamp, 3}, {"mix", Bi::Mix, 3}, {"smoothstep", Bi::Smoothstep, 3},
    {"length", Bi::Length, 1}, {"distance", Bi::Distance, 2}, {"dot", Bi::Dot, 2}, {"luminance", Bi::Luminance, 1},
    {"cross", Bi::Cross, 2}, {"noise", Bi::Noise, 1}, {"hash", Bi::Hash, 1}, {"sample", Bi::Sample, 2},
};
const BiInfo* FindBuiltin(const std::string& n) {
    for (const BiInfo& b : kBuiltins) if (n == b.name) return &b;
    return nullptr;
}

enum class NK : std::uint8_t { Num, Bool, Var, Neg, Not, Bin, Cond, Call, Ctor, Swz };
struct Node {
    NK k = NK::Num;
    Type t = Type::Void;
    int line = 0, col = 0;
    float num = 0.0f;
    int sym = -1;
    std::string op;               // binary operator
    Bi bi = Bi::None;
    int kids[4] = {-1, -1, -1, -1};
    int nk = 0;                   // used kids (ctor may have up to 4)
    std::uint8_t swz[4] = {0, 0, 0, 0};
    int swzN = 0;
    Type promote = Type::Void;    // calls: generic type every numeric argument is widened to
};

enum class SK : std::uint8_t { Decl, Assign, If, For, Block, Discard };
struct Stmt {
    SK k = SK::Block;
    int line = 0, col = 0;
    int sym = -1;
    int expr = -1;
    std::string op;               // assignment operator
    std::uint8_t swz[4] = {0, 0, 0, 0};
    int swzN = 0;
    int a = -1, b = -1;           // if: then / else statement; for: body
    std::vector<int> list;        // block
    int from = 0, to = 0;         // for
};

// =====================================================================================
// Parser + checker (one pass: names are resolved and types checked while parsing)
// =====================================================================================
class Parser {
public:
    Parser(const std::vector<Token>& toks, Compiled& out) : m_t(toks), m_out(out) {
        for (int i = 0; i < 5; ++i) syms[(std::size_t)AddSym(kInputs[i].name, SymKind::Input, kInputs[i].type, i)].stage = 1;
        for (int i = 0; i < 7; ++i) syms[(std::size_t)AddSym(kOutputs[i].name, SymKind::Output, kOutputs[i].type, i)].stage = 1;
        for (int i = 0; i < 4; ++i) syms[(std::size_t)AddSym(kVInputs[i].name, SymKind::VInput, kVInputs[i].type, i)].stage = 2;
        for (int i = 0; i < 2; ++i) syms[(std::size_t)AddSym(kVOutputs[i].name, SymKind::VOutput, kVOutputs[i].type, i)].stage = 2;
    }

    std::vector<Node> nodes;
    std::vector<Stmt> stmts;
    std::vector<Symbol> syms;
    int body = -1;                 // the surface block statement
    int vbody = -1;                // the vertex block statement
    std::vector<Diagnostic> errs;  // type errors (parsing continues after them)

    void ParseFile() {
        while (Peek().kind != Tk::End) {
            const Token& t = Peek();
            if (IsWord("shader")) {
                Next();
                const Token& s = Expect(Tk::String, "a quoted shader name");
                m_out.name = s.text;
                ExpectP(";", "after the shader name");
            } else if (IsWord("mode")) {
                Next();
                const Token& m = Expect(Tk::Ident, "lit or unlit");
                if (m.text == "unlit") m_out.unlit = true;
                else if (m.text == "lit") m_out.unlit = false;
                else Error(m, "unknown mode '" + m.text + "' (use lit or unlit)");
                ExpectP(";", "after the mode");
            } else if (IsWord("property")) {
                ParseProperty();
            } else if (IsWord("texture")) {
                Next();
                const Token& n = Expect(Tk::Ident, "a texture name");
                CheckNewName(n);
                if ((int)m_out.textures.size() >= kMaxTextures) Error(n, "too many textures (at most " + std::to_string(kMaxTextures) + ")");
                TextureSlot ts; ts.name = n.text;
                if (IsP("=")) { Next(); ts.defaultPath = Expect(Tk::String, "a quoted picture path").text; }
                ExpectP(";", "after the texture");
                AddSym(n.text, SymKind::Texture, Type::Texture, (int)m_out.textures.size());
                m_out.textures.push_back(ts);
            } else if (IsWord("surface")) {
                if (body >= 0) Error(t, "only one surface block is allowed");
                Next();
                if (!IsP("{")) Error(Peek(), "expected '{' after surface");
                m_stage = 1;
                body = ParseBlock();
                m_stage = 0;
            } else if (IsWord("vertex")) {
                if (vbody >= 0) Error(t, "only one vertex block is allowed");
                Next();
                if (!IsP("{")) Error(Peek(), "expected '{' after vertex");
                m_stage = 2;
                vbody = ParseBlock();
                m_stage = 0;
            } else {
                Error(t, "expected shader, mode, property, texture, vertex or surface, found " + Describe(t));
            }
        }
        if (body < 0 && vbody < 0) Error(Peek(), "the file has no surface or vertex block");
    }

private:
    const std::vector<Token>& m_t;
    Compiled& m_out;
    std::size_t m_p = 0;
    int m_depth = 0;
    int m_scope = 0;
    long m_loopProduct = 1;
    int m_stage = 0;               // the block being parsed: 0 none, 1 surface, 2 vertex

    // ---- tokens ----
    const Token& Peek(int k = 0) const { std::size_t i = m_p + k; return i < m_t.size() ? m_t[i] : m_t.back(); }
    const Token& Next() { const Token& t = Peek(); if (m_p < m_t.size() - 1) ++m_p; return t; }
    bool IsP(const char* p) const { return Peek().kind == Tk::Punct && Peek().text == p; }
    bool IsWord(const char* w) const { return Peek().kind == Tk::Ident && Peek().text == w; }
    static std::string Describe(const Token& t) {
        switch (t.kind) {
            case Tk::End: return "the end of the file";
            case Tk::Ident: return "'" + t.text + "'";
            case Tk::Number: return "the number " + t.text;
            case Tk::String: return "a string";
            default: return "'" + t.text + "'";
        }
    }
    [[noreturn]] void Error(const Token& t, const std::string& msg) { Error(t.line, t.col, msg); }
    [[noreturn]] void Error(int line, int col, const std::string& msg) {
        errs.insert(errs.begin(), Diagnostic{line, col, msg});   // a syntax error is reported first
        throw Fail{};
    }
    // A type error: recorded, parsing goes on (more than one can be shown).
    void TypeError(int line, int col, const std::string& msg) {
        if ((int)errs.size() < kMaxErrors) errs.push_back(Diagnostic{line, col, msg});
    }
    const Token& Expect(Tk k, const char* what) {
        if (Peek().kind != k) Error(Peek(), std::string("expected ") + what + ", found " + Describe(Peek()));
        return Next();
    }
    void ExpectP(const char* p, const char* where) {
        if (!IsP(p)) Error(Peek(), std::string("expected '") + p + "' " + where + ", found " + Describe(Peek()));
        Next();
    }
    struct DepthGuard {
        Parser& p;
        explicit DepthGuard(Parser& pp, const Token& t) : p(pp) {
            if (++p.m_depth > kMaxDepth) p.Error(t, "nesting is too deep (at most " + std::to_string(kMaxDepth) + " levels)");
        }
        ~DepthGuard() { --p.m_depth; }
    };

    // ---- names ----
    int AddSym(const std::string& n, SymKind k, Type t, int index) {
        Symbol s; s.name = n; s.kind = k; s.type = t; s.index = index; s.depth = m_scope;
        syms.push_back(s);
        return (int)syms.size() - 1;
    }
    int Lookup(const std::string& n) const {
        for (int i = (int)syms.size() - 1; i >= 0; --i) {
            if (m_stage != 0 && syms[i].stage != 0 && syms[i].stage != m_stage) continue;   // the other block's inputs / outputs
            if (syms[i].name == n && syms[i].depth >= 0) return i;
        }
        return -1;
    }
    static bool IsTypeWord(const std::string& w) {
        return w == "float" || w == "vec2" || w == "vec3" || w == "vec4" || w == "bool";
    }
    static bool IsKeyword(const std::string& w) {
        // Only the words a statement or expression can start with; the top-level words
        // (shader, mode, property, texture, surface, color, range) are free as names.
        static const char* k[] = {"if", "else", "for", "in", "discard", "true", "false"};
        for (const char* x : k) if (w == x) return true;
        return IsTypeWord(w);
    }
    void CheckNewName(const Token& n) {
        if (IsKeyword(n.text)) Error(n, "'" + n.text + "' is a keyword and cannot be a name");
        if (FindBuiltin(n.text)) Error(n, "'" + n.text + "' is a built-in function and cannot be a name");
        const int s = Lookup(n.text);
        if (s >= 0) {
            const SymKind k = syms[s].kind;
            if (k == SymKind::Input || k == SymKind::Output) Error(n, "'" + n.text + "' is a surface input or output and cannot be declared");
            if (k == SymKind::VInput || k == SymKind::VOutput) Error(n, "'" + n.text + "' is a vertex input or output and cannot be declared");
            Error(n, "'" + n.text + "' is already declared");
        }
    }
    static Type TypeFromWord(const std::string& w) {
        if (w == "float") return Type::Float;
        if (w == "vec2") return Type::Vec2;
        if (w == "vec3") return Type::Vec3;
        if (w == "vec4") return Type::Vec4;
        if (w == "bool") return Type::Bool;
        return Type::Void;
    }

    // ---- declarations ----
    float SignedNumber() {
        bool neg = false;
        if (IsP("-")) { Next(); neg = true; }
        const Token& n = Expect(Tk::Number, "a number");
        return (float)(neg ? -n.num : n.num);
    }
    void ParseProperty() {
        Next();
        const Token& tw = Expect(Tk::Ident, "a property type (float, vec2, vec3, vec4 or color)");
        Property p;
        if (tw.text == "color") { p.type = Type::Vec3; p.isColor = true; }
        else {
            p.type = TypeFromWord(tw.text);
            if (!IsNumeric(p.type)) Error(tw, "unknown property type '" + tw.text + "' (use float, vec2, vec3, vec4 or color)");
        }
        const Token& n = Expect(Tk::Ident, "a property name");
        CheckNewName(n);
        if ((int)m_out.props.size() >= kMaxProperties)
            Error(n, "too many properties (at most " + std::to_string(kMaxProperties) + ")");
        p.name = n.text;
        ExpectP("=", "after the property name (every property needs a default value)");
        const int w = Width(p.type);
        std::vector<float> vals;
        const Token& at = Peek();
        if (Peek().kind == Tk::Ident && (TypeFromWord(Peek().text) != Type::Void || Peek().text == "color")) Next();
        if (IsP("(")) {
            Next();
            for (;;) {
                vals.push_back(SignedNumber());
                if (vals.size() > 4) Error(at, "too many values in the default");
                if (IsP(",")) { Next(); continue; }
                ExpectP(")", "after the default values");
                break;
            }
        } else vals.push_back(SignedNumber());
        if ((int)vals.size() != w && vals.size() != 1)
            Error(at, std::string(TypeName(p.type)) + " property needs " + std::to_string(w) + " values, got " + std::to_string(vals.size()));
        for (int i = 0; i < w; ++i) p.def[i] = vals.size() == 1 ? vals[0] : vals[(std::size_t)i];
        if (IsWord("range")) {
            Next();
            ExpectP("(", "after range");
            p.minV = SignedNumber();
            ExpectP(",", "between the range limits");
            p.maxV = SignedNumber();
            ExpectP(")", "after the range");
            if (!(p.minV < p.maxV)) Error(n, "range of '" + p.name + "' must go from low to high");
            p.hasRange = true;
        }
        ExpectP(";", "after the property");
        AddSym(p.name, SymKind::Property, p.type, (int)m_out.props.size());
        m_out.props.push_back(p);
    }

    // ---- statements ----
    int NewStmt(SK k, const Token& t) { Stmt s; s.k = k; s.line = t.line; s.col = t.col; stmts.push_back(s); return (int)stmts.size() - 1; }

    int ParseBlock() {
        const Token& open = Peek();
        DepthGuard g(*this, open);
        ExpectP("{", "to open a block");
        const int id = NewStmt(SK::Block, open);
        ++m_scope;
        const std::size_t symMark = syms.size();
        while (!IsP("}")) {
            if (Peek().kind == Tk::End) Error(Peek(), "expected '}' to close the block opened at line " + std::to_string(open.line));
            const int s = ParseStatement();
            stmts[(std::size_t)id].list.push_back(s);
        }
        Next();
        for (std::size_t i = symMark; i < syms.size(); ++i) syms[i].depth = -1;   // out of scope (kept for codegen)
        --m_scope;
        return id;
    }

    int ParseStatement() {
        const Token& t = Peek();
        DepthGuard g(*this, t);
        if (IsP("{")) return ParseBlock();
        if (t.kind == Tk::Ident) {
            if (t.text == "if") return ParseIf();
            if (t.text == "for") return ParseFor();
            if (t.text == "discard") {
                if (m_stage == 2) Error(t, "discard is only allowed in the surface block");
                Next();
                ExpectP(";", "after discard");
                m_out.usesDiscard = true;
                return NewStmt(SK::Discard, t);
            }
            if (IsTypeWord(t.text)) return ParseDecl();
            return ParseAssign();
        }
        Error(t, "expected a statement, found " + Describe(t));
    }

    int ParseDecl() {
        const Token& tw = Next();
        const Type ty = TypeFromWord(tw.text);
        const Token& n = Expect(Tk::Ident, "a variable name");
        CheckNewName(n);
        int init = -1;
        if (IsP("=")) {
            Next();
            init = ParseExpr();
            const Node& e = nodes[(std::size_t)init];
            if (e.t != ty && e.t != Type::Void)
                TypeError(e.line, e.col, std::string("cannot use ") + TypeName(e.t) + " as " + TypeName(ty) +
                                         (IsNumeric(ty) && e.t == Type::Float ? std::string(" (write ") + TypeName(ty) + "(...))" : std::string()));
        }
        ExpectP(";", "after the declaration");
        const int id = NewStmt(SK::Decl, tw);
        stmts[(std::size_t)id].sym = AddSym(n.text, SymKind::Local, ty, (int)syms.size());
        stmts[(std::size_t)id].expr = init;
        return id;
    }

    bool ParseSwizzle(const Token& s, std::uint8_t out[4], int& n, int width, bool forWrite) {
        const std::string& w = s.text;
        if (w.empty() || w.size() > 4) { Error(s, "invalid swizzle '" + w + "'"); }
        static const char* sets[2] = {"xyzw", "rgba"};
        int set = -1;
        for (int k = 0; k < 2; ++k) if (std::strchr(sets[k], w[0])) set = k;
        if (set < 0) Error(s, "invalid swizzle '" + w + "' (use x y z w or r g b a)");
        n = (int)w.size();
        unsigned used = 0;
        for (int i = 0; i < n; ++i) {
            const char* p = std::strchr(sets[set], w[(std::size_t)i]);
            if (!p || w[(std::size_t)i] == 0) Error(s, "invalid swizzle '" + w + "' (use x y z w or r g b a, not mixed)");
            const int c = (int)(p - sets[set]);
            if (c >= width) Error(s, "swizzle '" + w + "' reads past the end of a " + std::string(TypeName(VecOf(width))));
            if (forWrite && (used & (1u << c))) Error(s, "swizzle '" + w + "' writes a component twice");
            used |= 1u << c;
            out[i] = (std::uint8_t)c;
        }
        return true;
    }

    int ParseAssign() {
        const Token& n = Next();
        const int s = Lookup(n.text);
        if (s < 0) Error(n, "unknown name '" + n.text + "'");
        const Symbol& sy = syms[(std::size_t)s];
        if (sy.kind == SymKind::Input || sy.kind == SymKind::VInput || sy.kind == SymKind::Property || sy.kind == SymKind::LoopVar || sy.kind == SymKind::Texture)
            Error(n, "'" + n.text + "' is read-only");
        const int id = NewStmt(SK::Assign, n);
        Type target = sy.type;
        if (IsP(".")) {
            Next();
            const Token& sw = Expect(Tk::Ident, "a swizzle after '.'");
            if (!IsNumeric(sy.type) || sy.type == Type::Float) Error(sw, "cannot swizzle a " + std::string(TypeName(sy.type)));
            std::uint8_t sz[4]; int sn = 0;
            ParseSwizzle(sw, sz, sn, Width(sy.type), true);
            std::memcpy(stmts[(std::size_t)id].swz, sz, 4);
            stmts[(std::size_t)id].swzN = sn;
            target = VecOf(sn);
        }
        const Token& op = Peek();
        if (!(IsP("=") || IsP("+=") || IsP("-=") || IsP("*=") || IsP("/=")))
            Error(op, "expected '=' after '" + n.text + "', found " + Describe(op));
        Next();
        const int e = ParseExpr();
        const Node& en = nodes[(std::size_t)e];
        if (en.t != Type::Void) {
            bool okT = en.t == target;
            if (op.text != "=" && en.t == Type::Float && IsNumeric(target)) okT = true;   // v *= 2.0
            if (op.text != "=" && target == Type::Bool) okT = false;
            if (!okT) TypeError(en.line, en.col, std::string("cannot ") + (op.text == "=" ? "assign " : "combine ") + TypeName(en.t) +
                                                 (op.text == "=" ? " to " : " with ") + TypeName(target));
        }
        ExpectP(";", "after the assignment");
        Stmt& st = stmts[(std::size_t)id];
        st.sym = s; st.expr = e; st.op = op.text;
        if (sy.kind == SymKind::Output && std::string(kOutputs[sy.index].name) == "alpha") m_out.writesAlpha = true;
        return id;
    }

    int ParseIf() {
        const Token& t = Next();
        ExpectP("(", "after if");
        const int c = ParseExpr();
        const Node& cn = nodes[(std::size_t)c];
        if (cn.t != Type::Bool && cn.t != Type::Void) TypeError(cn.line, cn.col, std::string("condition must be bool, not ") + TypeName(cn.t));
        ExpectP(")", "after the condition");
        const int id = NewStmt(SK::If, t);
        stmts[(std::size_t)id].expr = c;
        const int a = ParseStatement();
        int b = -1;
        if (IsWord("else")) { Next(); b = ParseStatement(); }
        stmts[(std::size_t)id].a = a; stmts[(std::size_t)id].b = b;
        return id;
    }

    int ParseFor() {
        const Token& t = Next();
        const Token& n = Expect(Tk::Ident, "a loop variable name");
        CheckNewName(n);
        if (!IsWord("in")) Error(Peek(), "expected 'in' after the loop variable, found " + Describe(Peek()));
        Next();
        const Token& a = Expect(Tk::Number, "the first loop number");
        ExpectP("..", "between the loop numbers");
        const Token& b = Expect(Tk::Number, "the last loop number");
        if (a.num != std::floor(a.num) || b.num != std::floor(b.num)) Error(a, "loop bounds must be whole numbers");
        if (b.num <= a.num) Error(b, "loop end must be greater than its start");
        const double count = b.num - a.num;
        if (count > kMaxLoopCount) Error(b, "loop runs " + b.text + " - " + a.text + " times (at most " + std::to_string(kMaxLoopCount) + ")");
        if (a.num > 100000 || b.num > 100000) Error(b, "loop numbers must be at most 100000");
        if (m_loopProduct * (long)count > kMaxLoopProduct) Error(b, "nested loops run too many times together (at most " + std::to_string(kMaxLoopProduct) + ")");
        const long save = m_loopProduct;
        m_loopProduct *= (long)count;
        const int id = NewStmt(SK::For, t);
        ++m_scope;
        const int s = AddSym(n.text, SymKind::LoopVar, Type::Float, (int)syms.size());
        if (!IsP("{")) Error(Peek(), "expected '{' after the loop range");
        const int body0 = ParseBlock();
        syms[(std::size_t)s].depth = -1;
        --m_scope;
        m_loopProduct = save;
        Stmt& st = stmts[(std::size_t)id];
        st.sym = s; st.a = body0; st.from = (int)a.num; st.to = (int)b.num;
        return id;
    }

    // ---- expressions ----
    int NewNode(NK k, const Token& t, Type ty) {
        Node n; n.k = k; n.line = t.line; n.col = t.col; n.t = ty;
        nodes.push_back(n);
        if (nodes.size() > 500000) Error(t, "shader is too big");
        return (int)nodes.size() - 1;
    }
    Type T(int n) const { return nodes[(std::size_t)n].t; }

    int ParseExpr() {
        DepthGuard g(*this, Peek());
        const int c = ParseOr();
        if (IsP("?")) {
            const Token& q = Next();
            const int a = ParseExpr();
            ExpectP(":", "in the ?: choice");
            const int b = ParseExpr();
            Type ty = T(a);
            if (T(c) != Type::Bool && T(c) != Type::Void) { TypeError(nodes[(std::size_t)c].line, nodes[(std::size_t)c].col, std::string("condition must be bool, not ") + TypeName(T(c))); ty = Type::Void; }
            if (T(a) != T(b) && T(a) != Type::Void && T(b) != Type::Void) {
                TypeError(q.line, q.col, std::string("both sides of ?: must have the same type (") + TypeName(T(a)) + " and " + TypeName(T(b)) + ")");
                ty = Type::Void;
            }
            if (T(a) == Type::Void || T(b) == Type::Void) ty = Type::Void;
            const int id = NewNode(NK::Cond, q, ty);
            Node& nd = nodes[(std::size_t)id];
            nd.kids[0] = c; nd.kids[1] = a; nd.kids[2] = b; nd.nk = 3;
            nd.line = nodes[(std::size_t)c].line; nd.col = nodes[(std::size_t)c].col;
            return id;
        }
        return c;
    }
    int Binary(const Token& op, int a, int b) {
        const Type ta = T(a), tb = T(b);
        Type ty = Type::Void;
        const std::string& o = op.text;
        if (ta == Type::Void || tb == Type::Void) ty = Type::Void;
        else if (o == "&&" || o == "||") {
            if (ta == Type::Bool && tb == Type::Bool) ty = Type::Bool;
            else TypeError(op.line, op.col, "'" + o + "' needs bool on both sides, not " + TypeName(ta) + " and " + TypeName(tb));
        } else if (o == "==" || o == "!=") {
            if ((ta == Type::Float && tb == Type::Float) || (ta == Type::Bool && tb == Type::Bool)) ty = Type::Bool;
            else TypeError(op.line, op.col, "'" + o + "' compares two floats or two bools, not " + TypeName(ta) + " and " + TypeName(tb));
        } else if (o == "<" || o == ">" || o == "<=" || o == ">=") {
            if (ta == Type::Float && tb == Type::Float) ty = Type::Bool;
            else TypeError(op.line, op.col, "'" + o + "' compares two floats, not " + TypeName(ta) + " and " + TypeName(tb));
        } else {
            const char* verb = o == "+" ? "add" : o == "-" ? "subtract" : o == "*" ? "multiply" : "divide";
            if (IsNumeric(ta) && IsNumeric(tb) && (ta == tb || ta == Type::Float || tb == Type::Float))
                ty = ta == Type::Float ? tb : ta;
            else TypeError(op.line, op.col, std::string("cannot ") + verb + " " + TypeName(ta) + " and " + TypeName(tb));
        }
        const int id = NewNode(NK::Bin, op, ty);
        Node& nd = nodes[(std::size_t)id];
        nd.op = o; nd.kids[0] = a; nd.kids[1] = b; nd.nk = 2;
        return id;
    }
    int ParseOr() {
        int a = ParseAnd();
        while (IsP("||")) { const Token& op = Next(); const int b = ParseAnd(); a = Binary(op, a, b); }
        return a;
    }
    int ParseAnd() {
        int a = ParseEq();
        while (IsP("&&")) { const Token& op = Next(); const int b = ParseEq(); a = Binary(op, a, b); }
        return a;
    }
    int ParseEq() {
        int a = ParseRel();
        while (IsP("==") || IsP("!=")) { const Token& op = Next(); const int b = ParseRel(); a = Binary(op, a, b); }
        return a;
    }
    int ParseRel() {
        int a = ParseAdd();
        while (IsP("<") || IsP(">") || IsP("<=") || IsP(">=")) { const Token& op = Next(); const int b = ParseAdd(); a = Binary(op, a, b); }
        return a;
    }
    int ParseAdd() {
        int a = ParseMul();
        while (IsP("+") || IsP("-")) { const Token& op = Next(); const int b = ParseMul(); a = Binary(op, a, b); }
        return a;
    }
    int ParseMul() {
        int a = ParseUnary();
        while (IsP("*") || IsP("/")) { const Token& op = Next(); const int b = ParseUnary(); a = Binary(op, a, b); }
        return a;
    }
    int ParseUnary() {
        const Token& t = Peek();
        DepthGuard g(*this, t);
        if (IsP("-") || IsP("!")) {
            Next();
            const int a = ParseUnary();
            const bool neg = t.text == "-";
            Type ty = T(a);
            if (neg && !IsNumeric(ty) && ty != Type::Void) { TypeError(t.line, t.col, std::string("cannot negate a ") + TypeName(ty)); ty = Type::Void; }
            if (!neg && ty != Type::Bool && ty != Type::Void) { TypeError(t.line, t.col, std::string("'!' needs a bool, not ") + TypeName(ty)); ty = Type::Void; }
            const int id = NewNode(neg ? NK::Neg : NK::Not, t, ty);
            nodes[(std::size_t)id].kids[0] = a; nodes[(std::size_t)id].nk = 1;
            return id;
        }
        return ParsePostfix();
    }
    int ParsePostfix() {
        int a = ParsePrimary();
        while (IsP(".")) {
            Next();
            const Token& sw = Expect(Tk::Ident, "a swizzle after '.'");
            const Type bt = T(a);
            if (bt == Type::Void) continue;
            if (!IsNumeric(bt) || bt == Type::Float) Error(sw, std::string("cannot swizzle a ") + TypeName(bt));
            std::uint8_t sz[4]; int sn = 0;
            ParseSwizzle(sw, sz, sn, Width(bt), false);
            const int id = NewNode(NK::Swz, sw, VecOf(sn));
            Node& nd = nodes[(std::size_t)id];
            nd.kids[0] = a; nd.nk = 1; std::memcpy(nd.swz, sz, 4); nd.swzN = sn;
            nd.line = nodes[(std::size_t)a].line; nd.col = nodes[(std::size_t)a].col;
            a = id;
        }
        return a;
    }
    int ParsePrimary() {
        const Token& t = Peek();
        if (t.kind == Tk::Number) {
            Next();
            const int id = NewNode(NK::Num, t, Type::Float);
            nodes[(std::size_t)id].num = (float)t.num;
            return id;
        }
        if (IsP("(")) {
            Next();
            const int e = ParseExpr();
            ExpectP(")", "to close '('");
            return e;
        }
        if (t.kind != Tk::Ident) Error(t, "expected a value, found " + Describe(t));
        Next();
        if (t.text == "true" || t.text == "false") {
            const int id = NewNode(NK::Bool, t, Type::Bool);
            nodes[(std::size_t)id].num = t.text == "true" ? 1.0f : 0.0f;
            return id;
        }
        if (IsP("(")) return ParseCall(t);
        const int s = Lookup(t.text);
        if (s < 0) {
            TypeError(t.line, t.col, "unknown name '" + t.text + "'");
            return NewNode(NK::Num, t, Type::Void);
        }
        const Symbol& sy = syms[(std::size_t)s];
        const int id = NewNode(NK::Var, t, sy.type);
        nodes[(std::size_t)id].sym = s;
        if (sy.kind == SymKind::Texture) {
            // Only sample(...) may name a texture: ParseCall marks its first argument.
            nodes[(std::size_t)id].t = Type::Texture;
        }
        return id;
    }
    std::vector<int> ParseArgs() {
        std::vector<int> args;
        ExpectP("(", "before the arguments");
        if (!IsP(")")) {
            for (;;) {
                args.push_back(ParseExpr());
                if (args.size() > 16) Error(Peek(), "too many arguments");
                if (IsP(",")) { Next(); continue; }
                break;
            }
        }
        ExpectP(")", "after the arguments");
        return args;
    }
    void RejectTextures(const std::vector<int>& args, std::size_t from) {
        for (std::size_t i = from; i < args.size(); ++i) {
            const Node& a = nodes[(std::size_t)args[i]];
            if (a.t == Type::Texture) TypeError(a.line, a.col, "texture '" + syms[(std::size_t)a.sym].name + "' can only be used in sample(texture, uv)");
        }
    }
    int ParseCall(const Token& fn) {
        const Type ctorT = TypeFromWord(fn.text);
        if (ctorT != Type::Void && ctorT != Type::Bool) {
            std::vector<int> args = ParseArgs();
            RejectTextures(args, 0);
            const int want = Width(ctorT);
            int got = 0; bool bad = false, voidArg = false;
            for (int a : args) {
                if (T(a) == Type::Void) voidArg = true;
                else if (!IsNumeric(T(a))) bad = true;
                else got += Width(T(a));
            }
            Type ty = ctorT;
            if (voidArg) ty = Type::Void;
            else if (bad) { TypeError(fn.line, fn.col, fn.text + "(...) takes numbers and vectors"); ty = Type::Void; }
            else if (args.empty() || (got != want && !(args.size() == 1 && got == 1)) || args.size() > 4) {
                TypeError(fn.line, fn.col, fn.text + " needs " + std::to_string(want) + " values, got " + std::to_string(got));
                ty = Type::Void;
            }
            const int id = NewNode(NK::Ctor, fn, ty);
            Node& nd = nodes[(std::size_t)id];
            nd.nk = (int)std::min<std::size_t>(args.size(), 4);
            for (int i = 0; i < nd.nk; ++i) nd.kids[i] = args[(std::size_t)i];
            return id;
        }
        const BiInfo* bi = FindBuiltin(fn.text);
        if (bi && bi->id == Bi::Sample && m_stage == 2) Error(fn, "sample() is only allowed in the surface block");
        std::vector<int> args = ParseArgs();
        if (!bi) {
            TypeError(fn.line, fn.col, "unknown function '" + fn.text + "'");
            return NewNode(NK::Num, fn, Type::Void);
        }
        if ((int)args.size() != bi->args) {
            TypeError(fn.line, fn.col, std::string(bi->name) + " takes " + std::to_string(bi->args) + (bi->args == 1 ? " argument" : " arguments") +
                                       ", got " + std::to_string(args.size()));
            return NewNode(NK::Num, fn, Type::Void);
        }
        for (int a : args) if (T(a) == Type::Void) return NewNode(NK::Num, fn, Type::Void);
        if (bi->id == Bi::Sample) {
            const Node& t0 = nodes[(std::size_t)args[0]];
            if (t0.t != Type::Texture) { TypeError(t0.line, t0.col, "the first argument of sample must be a texture"); return NewNode(NK::Num, fn, Type::Void); }
        }
        RejectTextures(args, bi->id == Bi::Sample ? 1 : 0);
        for (int a : args) if (T(a) == Type::Texture && bi->id != Bi::Sample) return NewNode(NK::Num, fn, Type::Void);
        Type ty = Type::Void, promote = Type::Void;
        std::vector<Type> at;
        for (int a : args) at.push_back(T(a));
        auto noVersion = [&]() {
            std::string s = std::string("no version of ") + bi->name + " takes (";
            for (std::size_t i = 0; i < at.size(); ++i) s += (i ? ", " : "") + std::string(TypeName(at[i]));
            TypeError(fn.line, fn.col, s + ")");
        };
        auto G = [](Type t) { return IsNumeric(t); };
        switch (bi->id) {
            case Bi::Atan2: case Bi::Pow: case Bi::Reflect:
                if (G(at[0]) && at[0] == at[1]) ty = promote = at[0]; else noVersion();
                break;
            case Bi::Min: case Bi::Max: case Bi::Mod:
                if (G(at[0]) && (at[1] == at[0] || at[1] == Type::Float)) ty = promote = at[0]; else noVersion();
                break;
            case Bi::Step:
                if (G(at[1]) && (at[0] == at[1] || at[0] == Type::Float)) ty = promote = at[1]; else noVersion();
                break;
            case Bi::Clamp:
                if (G(at[0]) && ((at[1] == at[0] && at[2] == at[0]) || (at[1] == Type::Float && at[2] == Type::Float))) ty = promote = at[0]; else noVersion();
                break;
            case Bi::Mix:
                if (G(at[0]) && at[1] == at[0] && (at[2] == at[0] || at[2] == Type::Float)) ty = promote = at[0]; else noVersion();
                break;
            case Bi::Smoothstep:
                if (G(at[2]) && ((at[0] == at[2] && at[1] == at[2]) || (at[0] == Type::Float && at[1] == Type::Float))) ty = promote = at[2]; else noVersion();
                break;
            case Bi::Length:
                if (G(at[0])) { ty = Type::Float; promote = at[0]; } else noVersion();
                break;
            case Bi::Distance: case Bi::Dot:
                if (G(at[0]) && at[0] == at[1]) { ty = Type::Float; promote = at[0]; } else noVersion();
                break;
            case Bi::Luminance:
                if (at[0] == Type::Vec3) { ty = Type::Float; promote = Type::Vec3; } else noVersion();
                break;
            case Bi::Cross:
                if (at[0] == Type::Vec3 && at[1] == Type::Vec3) ty = promote = Type::Vec3; else noVersion();
                break;
            case Bi::Noise: case Bi::Hash:
                if (at[0] == Type::Vec2 || at[0] == Type::Vec3) { ty = Type::Float; promote = at[0]; } else noVersion();
                break;
            case Bi::Sample:
                if (at[1] == Type::Vec2) { ty = Type::Vec4; promote = Type::Vec2; } else noVersion();
                break;
            default:   // one generic argument
                if (G(at[0])) ty = promote = at[0]; else noVersion();
                break;
        }
        const int id = NewNode(NK::Call, fn, ty);
        Node& nd = nodes[(std::size_t)id];
        nd.bi = bi->id; nd.promote = promote; nd.nk = (int)args.size();
        for (int i = 0; i < nd.nk; ++i) nd.kids[i] = args[(std::size_t)i];
        return id;
    }
};

// =====================================================================================
// GLSL / HLSL emitter
// =====================================================================================
class Emitter {
public:
    Emitter(const Parser& p, const Compiled& c, bool hlsl) : P(p), C(c), H(hlsl) {}

    // The fragment-stage text: uniforms, helpers and okaySurface(...).
    std::string Run() {
        std::string o = Header(true);
        o += "struct OkaySurface { " + Ty(Type::Vec3) + " albedo; " + Ty(Type::Vec3) + " emission; float alpha; " + Ty(Type::Vec3) +
             " normal; float metallic; float specular; float shininess; };\n";
        const std::string v2 = Ty(Type::Vec2), v3 = Ty(Type::Vec3);
        o += "OkaySurface okaySurface(" + v2 + " ok_uv, " + v3 + " ok_pos, " + v3 + " ok_view, " + v3 + " ok_vcol, float ok_time, " +
             v3 + " i_albedo, " + v3 + " i_emission, float i_alpha, " + v3 + " i_normal, float i_metallic, float i_specular, float i_shininess) {\n";
        Unpack(o);
        o += "  OkaySurface s;\n  s.albedo = i_albedo; s.emission = i_emission; s.alpha = i_alpha; s.normal = i_normal;\n"
             "  s.metallic = i_metallic; s.specular = i_specular; s.shininess = i_shininess;\n";
        if (P.body >= 0) for (int st : P.stmts[(std::size_t)P.body].list) Stmt(st, 1, o);
        o += "  return s;\n}\n";
        return o;
    }
    // The vertex-stage text: uniforms, helpers and okayVertex(...) (no textures: sample() is not allowed there).
    std::string RunVertex() {
        std::string o = Header(false);
        const std::string v2 = Ty(Type::Vec2), v3 = Ty(Type::Vec3);
        o += "void okayVertex(" + v2 + " ok_uv, " + v3 + " ok_local, " + v3 + " ok_vcol, float ok_time, inout " + v3 + " ok_vpos, inout " +
             v3 + " ok_vnrm) {\n";
        Unpack(o);
        if (P.vbody >= 0) for (int st : P.stmts[(std::size_t)P.vbody].list) Stmt(st, 1, o);
        o += "}\n";
        return o;
    }

private:
    const Parser& P;
    const Compiled& C;
    bool H;
    int m_loop = 0;

    std::string Header(bool textures) const {
        std::string o;
        o += "// generated from an .okayshader file\n";
        if (H) {
            o += "cbuffer OkayCustom : register(b" + std::to_string(kHlslPropRegister) + ") {\n  float4 uOkTimeV;\n";
            for (std::size_t i = 0; i < C.props.size(); ++i) o += "  float4 okp_" + C.props[i].name + "V;\n";
            o += "};\n";
            o += "static float uOkTime;\n";
            for (std::size_t i = 0; i < C.props.size(); ++i)
                o += "static " + Ty(C.props[i].type) + " okp_" + C.props[i].name + ";\n";
            if (textures)
                for (std::size_t i = 0; i < C.textures.size(); ++i)
                    o += "Texture2D okt_" + C.textures[i].name + " : register(t" + std::to_string(kFirstTextureUnit + (int)i) + ");\n";
        } else {
            o += "uniform float uOkTime;\n";
            for (const Property& p : C.props) o += "uniform " + Ty(p.type) + " okp_" + p.name + ";\n";
            if (textures) for (const TextureSlot& t : C.textures) o += "uniform sampler2D okt_" + t.name + ";\n";
        }
        o += Helpers();
        return o;
    }
    void Unpack(std::string& o) const {
        if (!H) return;   // HLSL: unpack the property block
        o += "  uOkTime = uOkTimeV.x;\n";
        for (const Property& p : C.props) {
            static const char* sw[] = {"", ".x", ".xy", ".xyz", ".xyzw"};
            o += "  okp_" + p.name + " = okp_" + p.name + "V" + sw[Width(p.type)] + ";\n";
        }
    }
    std::string Ty(Type t) const {
        switch (t) {
            case Type::Bool: return "bool";
            case Type::Float: return "float";
            case Type::Vec2: return H ? "float2" : "vec2";
            case Type::Vec3: return H ? "float3" : "vec3";
            case Type::Vec4: return H ? "float4" : "vec4";
            default: return "float";
        }
    }
    std::string Helpers() const {
        // Written the same way for both languages (and in RunCpu) so the renderers agree.
        std::string o;
        const std::string types[4] = {"float", Ty(Type::Vec2), Ty(Type::Vec3), Ty(Type::Vec4)};
        for (const std::string& t : types) {
            o += t + " ok_mod(" + t + " a, " + t + " b) { return a - b * floor(a / b); }\n";
            o += t + " ok_smooth(" + t + " a, " + t + " b, " + t + " x) { " + t + " t = clamp((x - a) / (b - a), 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }\n";
            o += t + " ok_norm(" + t + " v) { float l = length(v); return l > 1e-20 ? v / l : v * 0.0; }\n";
        }
        const std::string v2 = Ty(Type::Vec2), v3 = Ty(Type::Vec3);
        o += "float ok_perm(float x) { return ok_mod((x * 29.0 + 7.0) * x, 251.0); }\n";
        o += "float ok_cell2(" + v2 + " c) { c = ok_mod(c, " + v2 + "(251.0, 251.0)); return ok_perm(ok_perm(c.x) + c.y); }\n";
        o += "float ok_cell3(" + v3 + " c) { c = ok_mod(c, " + v3 + "(251.0, 251.0, 251.0)); return ok_perm(ok_perm(ok_perm(c.x) + c.y) + c.z); }\n";
        o += "float ok_hash2(" + v2 + " p) { return ok_cell2(floor(p)) / 251.0; }\n";
        o += "float ok_hash3(" + v3 + " p) { return ok_cell3(floor(p)) / 251.0; }\n";
        o += "float ok_lerp1(float a, float b, float t) { return a + (b - a) * t; }\n";
        o += "float ok_noise2(" + v2 + " p) { " + v2 + " i = floor(p); " + v2 + " f = p - i; " + v2 + " u = f * f * (3.0 - 2.0 * f);\n"
             "  float a = ok_cell2(i); float b = ok_cell2(i + " + v2 + "(1.0, 0.0));\n"
             "  float c = ok_cell2(i + " + v2 + "(0.0, 1.0)); float d = ok_cell2(i + " + v2 + "(1.0, 1.0));\n"
             "  return ok_lerp1(ok_lerp1(a, b, u.x), ok_lerp1(c, d, u.x), u.y) / 251.0; }\n";
        o += "float ok_noise3(" + v3 + " p) { " + v3 + " i = floor(p); " + v3 + " f = p - i; " + v3 + " u = f * f * (3.0 - 2.0 * f);\n"
             "  float a = ok_cell3(i); float b = ok_cell3(i + " + v3 + "(1.0, 0.0, 0.0));\n"
             "  float c = ok_cell3(i + " + v3 + "(0.0, 1.0, 0.0)); float d = ok_cell3(i + " + v3 + "(1.0, 1.0, 0.0));\n"
             "  float e = ok_cell3(i + " + v3 + "(0.0, 0.0, 1.0)); float f1 = ok_cell3(i + " + v3 + "(1.0, 0.0, 1.0));\n"
             "  float g = ok_cell3(i + " + v3 + "(0.0, 1.0, 1.0)); float h = ok_cell3(i + " + v3 + "(1.0, 1.0, 1.0));\n"
             "  float lo = ok_lerp1(ok_lerp1(a, b, u.x), ok_lerp1(c, d, u.x), u.y);\n"
             "  float hi = ok_lerp1(ok_lerp1(e, f1, u.x), ok_lerp1(g, h, u.x), u.y);\n"
             "  return ok_lerp1(lo, hi, u.z) / 251.0; }\n";
        return o;
    }
    static std::string Num(float v) {
        char b[48];
        std::snprintf(b, sizeof b, "%.9g", (double)v);
        std::string s = b;
        if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
        return s;
    }
    std::string SymName(int s) const {
        const Symbol& sy = P.syms[(std::size_t)s];
        switch (sy.kind) {
            case SymKind::Input: return kInputs[sy.index].glsl;
            case SymKind::Output: return kOutputs[sy.index].glsl;
            case SymKind::VInput: return kVInputs[sy.index].glsl;
            case SymKind::VOutput: return kVOutputs[sy.index].glsl;
            case SymKind::Property: return "okp_" + sy.name;
            case SymKind::Texture: return "okt_" + sy.name;
            default: return "l_" + sy.name;
        }
    }
    // A value of type `from` widened to `to` (scalar broadcast).
    std::string Widen(const std::string& e, Type from, Type to) const {
        if (from == to || from != Type::Float) return e;
        if (H) return "((" + Ty(to) + ")(" + e + "))";
        return Ty(to) + "(" + e + ")";
    }
    std::string Expr(int n) const {
        const Node& nd = P.nodes[(std::size_t)n];
        switch (nd.k) {
            case NK::Num: return Num(nd.num);
            case NK::Bool: return nd.num != 0.0f ? "true" : "false";
            case NK::Var: return SymName(nd.sym);
            case NK::Neg: return "(-" + Expr(nd.kids[0]) + ")";
            case NK::Not: return "(!" + Expr(nd.kids[0]) + ")";
            case NK::Bin: return "(" + Expr(nd.kids[0]) + " " + nd.op + " " + Expr(nd.kids[1]) + ")";
            case NK::Cond: return "(" + Expr(nd.kids[0]) + " ? " + Expr(nd.kids[1]) + " : " + Expr(nd.kids[2]) + ")";
            case NK::Swz: {
                static const char xyzw[] = "xyzw";
                std::string s = Expr(nd.kids[0]) + ".";
                for (int i = 0; i < nd.swzN; ++i) s += xyzw[nd.swz[i]];
                return s;
            }
            case NK::Ctor: {
                if (nd.nk == 1 && P.nodes[(std::size_t)nd.kids[0]].t == Type::Float) return Widen(Expr(nd.kids[0]), Type::Float, nd.t);
                std::string s = Ty(nd.t) + "(";
                for (int i = 0; i < nd.nk; ++i) s += (i ? ", " : "") + Expr(nd.kids[i]);
                return s + ")";
            }
            case NK::Call: return Call(nd);
        }
        return "0.0";
    }
    std::string Call(const Node& nd) const {
        auto arg = [&](int i) { const Node& a = P.nodes[(std::size_t)nd.kids[i]]; return Widen(Expr(nd.kids[i]), a.t, nd.promote); };
        auto f1 = [&](const char* name) { return std::string(name) + "(" + arg(0) + ")"; };
        auto f2 = [&](const char* name) { return std::string(name) + "(" + arg(0) + ", " + arg(1) + ")"; };
        auto f3 = [&](const char* name) { return std::string(name) + "(" + arg(0) + ", " + arg(1) + ", " + arg(2) + ")"; };
        switch (nd.bi) {
            case Bi::Sin: return f1("sin");
            case Bi::Cos: return f1("cos");
            case Bi::Tan: return f1("tan");
            case Bi::Asin: return f1("asin");
            case Bi::Acos: return f1("acos");
            case Bi::Atan: return f1("atan");
            case Bi::Exp: return f1("exp");
            case Bi::Log: return f1("log");
            case Bi::Exp2: return f1("exp2");
            case Bi::Log2: return f1("log2");
            case Bi::Sqrt: return f1("sqrt");
            case Bi::Abs: return f1("abs");
            case Bi::Sign: return f1("sign");
            case Bi::Floor: return f1("floor");
            case Bi::Ceil: return f1("ceil");
            case Bi::Fract: return H ? f1("frac") : "(" + arg(0) + " - floor(" + arg(0) + "))";
            case Bi::Saturate: return H ? f1("saturate") : "clamp(" + arg(0) + ", 0.0, 1.0)";
            case Bi::Radians: return f1("radians");
            case Bi::Degrees: return f1("degrees");
            case Bi::Normalize: return f1("ok_norm");
            case Bi::Atan2: return H ? f2("atan2") : f2("atan");
            case Bi::Pow: return f2("pow");
            case Bi::Min: return f2("min");
            case Bi::Max: return f2("max");
            case Bi::Mod: return f2("ok_mod");
            case Bi::Step: return f2("step");
            case Bi::Reflect: return f2("reflect");
            case Bi::Clamp: return f3("clamp");
            case Bi::Mix: return H ? f3("lerp") : f3("mix");
            case Bi::Smoothstep: return f3("ok_smooth");
            case Bi::Length: return f1("length");
            case Bi::Distance: return f2("distance");
            case Bi::Dot: return f2("dot");
            case Bi::Luminance: return "dot(" + arg(0) + ", " + Ty(Type::Vec3) + "(0.2126, 0.7152, 0.0722))";
            case Bi::Cross: return f2("cross");
            case Bi::Noise: return nd.promote == Type::Vec2 ? f1("ok_noise2") : f1("ok_noise3");
            case Bi::Hash: return nd.promote == Type::Vec2 ? f1("ok_hash2") : f1("ok_hash3");
            case Bi::Sample: {
                const std::string tex = Expr(nd.kids[0]);
                return H ? tex + ".Sample(uSamp, " + arg(1) + ")" : "texture2D(" + tex + ", " + arg(1) + ")";
            }
            default: break;
        }
        return "0.0";
    }
    void Stmt(int id, int ind, std::string& o) {
        const okay::oshader::Stmt& st = P.stmts[(std::size_t)id];
        const std::string pad((std::size_t)ind * 2, ' ');
        switch (st.k) {
            case SK::Block:
                o += pad + "{\n";
                for (int s : st.list) Stmt(s, ind + 1, o);
                o += pad + "}\n";
                break;
            case SK::Decl: {
                const Symbol& sy = P.syms[(std::size_t)st.sym];
                std::string init;
                if (st.expr >= 0) init = Expr(st.expr);
                else if (sy.type == Type::Bool) init = "false";
                else init = Widen("0.0", Type::Float, sy.type);
                o += pad + Ty(sy.type) + " " + SymName(st.sym) + " = " + init + ";\n";
                break;
            }
            case SK::Assign: {
                std::string lhs = SymName(st.sym);
                if (st.swzN > 0) {
                    static const char xyzw[] = "xyzw";
                    lhs += ".";
                    for (int i = 0; i < st.swzN; ++i) lhs += xyzw[st.swz[i]];
                }
                o += pad + lhs + " " + st.op + " " + Expr(st.expr) + ";\n";
                break;
            }
            case SK::If:
                o += pad + "if (" + Expr(st.expr) + ")\n";
                Stmt(st.a, ind + (P.stmts[(std::size_t)st.a].k == SK::Block ? 0 : 1), o);
                if (st.b >= 0) {
                    o += pad + "else\n";
                    Stmt(st.b, ind + (P.stmts[(std::size_t)st.b].k == SK::Block ? 0 : 1), o);
                }
                break;
            case SK::For: {
                const std::string ctr = "li" + std::to_string(m_loop++);
                o += pad + "for (int " + ctr + " = " + std::to_string(st.from) + "; " + ctr + " < " + std::to_string(st.to) + "; " + ctr + "++) {\n";
                o += pad + "  float " + SymName(st.sym) + " = float(" + ctr + ");\n";
                Stmt(st.a, ind + 1, o);
                o += pad + "}\n";
                break;
            }
            case SK::Discard:
                o += pad + "discard;\n";
                break;
        }
    }
};

} // namespace

// =====================================================================================
// CPU register program
// =====================================================================================
enum class Op : std::uint8_t {
    Mov, Swz, StSwz, Add, Sub, Mul, Div, Neg, Not, Lt, Gt, Le, Ge, Eq, Ne, And, Or, Sel,
    Sin, Cos, Tan, Asin, Acos, Atan, Exp, Log, Exp2, Log2, Sqrt, Abs, Sign, Floor, Ceil, Fract, Saturate, Radians, Degrees,
    Norm, Atan2, Pow, Min, Max, Mod, Step, Reflect, Clamp, Mix, Smooth, Length, Dist, Dot, Luma, Cross,
    Noise2, Noise3, Hash2, Hash3, Sample, Jmp, Jz, Discard, End,
};
struct Ins {
    Op op = Op::End;
    std::uint8_t n = 1;      // components
    std::uint8_t bc = 0;     // bit 0/1/2: operand a/b/c is a scalar broadcast to n
    std::uint8_t ex = 0;     // swizzle (2 bits per component) / texture slot
    std::uint16_t d = 0, a = 0, b = 0, c = 0;
};
struct CpuProgram {
    std::vector<Ins> code;
    std::vector<float> consts;   // copied to regs[constBase..]
    int constBase = 0;
    int regCount = 0;
    int propCount = 0;
};

namespace {

constexpr std::uint16_t kConstFlag = 0x8000;

class CpuGen {
public:
    CpuGen(Parser& p, const Compiled& c) : P(p), C(c) {}
    std::shared_ptr<CpuProgram> Run(std::vector<Diagnostic>& errs, int block) {
        auto prog = std::make_shared<CpuProgram>();
        m_prog = prog.get();
        m_top = kInputSlots + kOutputSlots + 4 * (int)C.props.size();
        m_peak = m_top;
        prog->propCount = (int)C.props.size();
        try {
            if (block >= 0) Stmt(block);
        } catch (const Fail&) {
            errs.push_back(m_err);
            return nullptr;
        }
        Emit(Op::End, 1, 0, 0, 0, 0);
        prog->constBase = m_peak;
        prog->regCount = m_peak + (int)prog->consts.size();
        for (Ins& in : prog->code) {
            auto fix = [&](std::uint16_t& r) { if (r & kConstFlag) r = (std::uint16_t)(prog->constBase + (r & ~kConstFlag)); };
            if (in.op == Op::Jmp || in.op == Op::Jz) { fix(in.a); continue; }   // d is a code address
            fix(in.a); fix(in.b); fix(in.c);
        }
        return prog;
    }
private:
    Parser& P;
    const Compiled& C;
    CpuProgram* m_prog = nullptr;
    int m_top = 0, m_peak = 0;
    Diagnostic m_err;

    [[noreturn]] void Fail2(int line, int col, const std::string& m) { m_err = {line, col, m}; throw Fail{}; }
    int Alloc(int n, int line, int col) {
        const int r = m_top;
        m_top += n;
        if (m_top > kMaxRegs) Fail2(line, col, "shader is too big: too many values (at most " + std::to_string(kMaxRegs) + " numbers)");
        if (m_top > m_peak) m_peak = m_top;
        return r;
    }
    std::uint16_t Const(float v, int line, int col) {
        auto& k = m_prog->consts;
        for (std::size_t i = 0; i < k.size(); ++i) if (std::memcmp(&k[i], &v, sizeof v) == 0) return (std::uint16_t)(kConstFlag | i);
        if ((int)k.size() >= kMaxConsts) Fail2(line, col, "shader is too big: too many constants");
        k.push_back(v);
        return (std::uint16_t)(kConstFlag | (k.size() - 1));
    }
    int Emit(Op op, int n, int d, int a, int b, int c, std::uint8_t bc = 0, std::uint8_t ex = 0) {
        Ins in; in.op = op; in.n = (std::uint8_t)n; in.d = (std::uint16_t)d; in.a = (std::uint16_t)a; in.b = (std::uint16_t)b; in.c = (std::uint16_t)c;
        in.bc = bc; in.ex = ex;
        m_prog->code.push_back(in);
        if (m_prog->code.size() > 60000) Fail2(1, 1, "shader is too big: too much code");
        return (int)m_prog->code.size() - 1;
    }
    int SymSlot(int s) {
        Symbol& sy = P.syms[(std::size_t)s];
        switch (sy.kind) {
            case SymKind::Input: return InputSlot(sy.index);
            case SymKind::Output: return OutputSlot(sy.index);
            case SymKind::VInput: return VInputSlot(sy.index);
            case SymKind::VOutput: return VOutputSlot(sy.index);
            case SymKind::Property: return kInputSlots + kOutputSlots + 4 * sy.index;
            default: return sy.slot;
        }
    }
    // Value of expression n: its register (a variable's own slot, a constant, or a new temporary).
    int Expr(int n) {
        const Node& nd = P.nodes[(std::size_t)n];
        const int w = Width(nd.t);
        auto kid = [&](int i) { return Expr(nd.kids[i]); };
        auto kidW = [&](int i) { return Width(P.nodes[(std::size_t)nd.kids[i]].t); };
        switch (nd.k) {
            case NK::Num: case NK::Bool: return Const(nd.num, nd.line, nd.col);
            case NK::Var: return SymSlot(nd.sym);
            case NK::Neg: { const int a = kid(0); const int d = Alloc(w, nd.line, nd.col); Emit(Op::Neg, w, d, a, 0, 0); return d; }
            case NK::Not: { const int a = kid(0); const int d = Alloc(1, nd.line, nd.col); Emit(Op::Not, 1, d, a, 0, 0); return d; }
            case NK::Bin: {
                const int a = kid(0), b = kid(1);
                const int wa = kidW(0), wb = kidW(1);
                const int d = Alloc(w, nd.line, nd.col);
                static const struct { const char* s; Op op; } map[] = {
                    {"+", Op::Add}, {"-", Op::Sub}, {"*", Op::Mul}, {"/", Op::Div}, {"<", Op::Lt}, {">", Op::Gt}, {"<=", Op::Le},
                    {">=", Op::Ge}, {"==", Op::Eq}, {"!=", Op::Ne}, {"&&", Op::And}, {"||", Op::Or}};
                Op op = Op::Add;
                for (const auto& m : map) if (nd.op == m.s) op = m.op;
                const int n2 = std::max(wa, wb);
                Emit(op, n2, d, a, b, 0, (std::uint8_t)((wa == 1 && n2 > 1 ? 1 : 0) | (wb == 1 && n2 > 1 ? 2 : 0)));
                return d;
            }
            case NK::Cond: {
                const int c = kid(0), a = kid(1), b = kid(2);
                const int d = Alloc(w, nd.line, nd.col);
                Emit(Op::Sel, w, d, c, a, b);
                return d;
            }
            case NK::Swz: {
                const int a = kid(0);
                const int d = Alloc(w, nd.line, nd.col);
                std::uint8_t ex = 0;
                for (int i = 0; i < nd.swzN; ++i) ex |= (std::uint8_t)(nd.swz[i] << (2 * i));
                Emit(Op::Swz, nd.swzN, d, a, 0, 0, 0, ex);
                return d;
            }
            case NK::Ctor: {
                const int d = Alloc(w, nd.line, nd.col);
                if (nd.nk == 1 && kidW(0) == 1) { const int a = kid(0); Emit(Op::Mov, w, d, a, 0, 0, 1); return d; }
                int at = d;
                for (int i = 0; i < nd.nk; ++i) { const int a = kid(i); const int kw = kidW(i); Emit(Op::Mov, kw, at, a, 0, 0); at += kw; }
                return d;
            }
            case NK::Call: return Call(nd);
        }
        return Const(0.0f, nd.line, nd.col);
    }
    int Call(const Node& nd) {
        const int pw = Width(nd.promote);
        const int w = Width(nd.t);
        int r[3] = {0, 0, 0};
        std::uint8_t bc = 0;
        const int first = nd.bi == Bi::Sample ? 1 : 0;
        for (int i = first; i < nd.nk; ++i) {
            r[i] = Expr(nd.kids[i]);
            if (Width(P.nodes[(std::size_t)nd.kids[i]].t) == 1 && pw > 1) bc |= (std::uint8_t)(1 << i);
        }
        const int d = Alloc(std::max(w, 1), nd.line, nd.col);
        Op op = Op::Mov;
        switch (nd.bi) {
            case Bi::Sin: op = Op::Sin; break; case Bi::Cos: op = Op::Cos; break; case Bi::Tan: op = Op::Tan; break;
            case Bi::Asin: op = Op::Asin; break; case Bi::Acos: op = Op::Acos; break; case Bi::Atan: op = Op::Atan; break;
            case Bi::Exp: op = Op::Exp; break; case Bi::Log: op = Op::Log; break; case Bi::Exp2: op = Op::Exp2; break;
            case Bi::Log2: op = Op::Log2; break; case Bi::Sqrt: op = Op::Sqrt; break; case Bi::Abs: op = Op::Abs; break;
            case Bi::Sign: op = Op::Sign; break; case Bi::Floor: op = Op::Floor; break; case Bi::Ceil: op = Op::Ceil; break;
            case Bi::Fract: op = Op::Fract; break; case Bi::Saturate: op = Op::Saturate; break; case Bi::Radians: op = Op::Radians; break;
            case Bi::Degrees: op = Op::Degrees; break; case Bi::Normalize: op = Op::Norm; break; case Bi::Atan2: op = Op::Atan2; break;
            case Bi::Pow: op = Op::Pow; break; case Bi::Min: op = Op::Min; break; case Bi::Max: op = Op::Max; break;
            case Bi::Mod: op = Op::Mod; break; case Bi::Step: op = Op::Step; break; case Bi::Reflect: op = Op::Reflect; break;
            case Bi::Clamp: op = Op::Clamp; break; case Bi::Mix: op = Op::Mix; break; case Bi::Smoothstep: op = Op::Smooth; break;
            case Bi::Length: op = Op::Length; break; case Bi::Distance: op = Op::Dist; break; case Bi::Dot: op = Op::Dot; break;
            case Bi::Luminance: op = Op::Luma; break; case Bi::Cross: op = Op::Cross; break;
            case Bi::Noise: op = pw == 2 ? Op::Noise2 : Op::Noise3; break;
            case Bi::Hash: op = pw == 2 ? Op::Hash2 : Op::Hash3; break;
            case Bi::Sample: {
                const int slot = P.syms[(std::size_t)P.nodes[(std::size_t)nd.kids[0]].sym].index;
                Emit(Op::Sample, 4, d, r[1], 0, 0, 0, (std::uint8_t)slot);
                return d;
            }
            default: break;
        }
        Emit(op, pw, d, r[0], r[1], r[2], bc);
        return d;
    }
    void Stmt(int id) {
        const okay::oshader::Stmt st = P.stmts[(std::size_t)id];   // copy: P.syms changes below
        switch (st.k) {
            case SK::Block: for (int s : st.list) Stmt(s); break;
            case SK::Decl: {
                Symbol& sy = P.syms[(std::size_t)st.sym];
                const int w = Width(sy.type);
                sy.slot = Alloc(w, st.line, st.col);
                const int keep = m_top;
                if (st.expr >= 0) { const int v = Expr(st.expr); Emit(Op::Mov, w, sy.slot, v, 0, 0); }
                else Emit(Op::Mov, w, sy.slot, Const(0.0f, st.line, st.col), 0, 0, 1);
                m_top = keep;
                break;
            }
            case SK::Assign: {
                const int keep = m_top;
                const Symbol& sy = P.syms[(std::size_t)st.sym];
                const int dst = SymSlot(st.sym);
                const int v = Expr(st.expr);
                const int vw = Width(P.nodes[(std::size_t)st.expr].t);
                const int tw = st.swzN > 0 ? st.swzN : Width(sy.type);
                int src = v;
                if (st.op != "=") {   // x op= v  ->  t = x op v
                    Op op = st.op == "+=" ? Op::Add : st.op == "-=" ? Op::Sub : st.op == "*=" ? Op::Mul : Op::Div;
                    int cur = dst;
                    if (st.swzN > 0) {
                        cur = Alloc(tw, st.line, st.col);
                        std::uint8_t ex = 0;
                        for (int i = 0; i < st.swzN; ++i) ex |= (std::uint8_t)(st.swz[i] << (2 * i));
                        Emit(Op::Swz, tw, cur, dst, 0, 0, 0, ex);
                    }
                    src = Alloc(tw, st.line, st.col);
                    Emit(op, tw, src, cur, v, 0, (std::uint8_t)(vw == 1 && tw > 1 ? 2 : 0));
                }
                if (st.swzN > 0) {
                    std::uint8_t ex = 0;
                    for (int i = 0; i < st.swzN; ++i) ex |= (std::uint8_t)(st.swz[i] << (2 * i));
                    Emit(Op::StSwz, st.swzN, dst, src, 0, 0, 0, ex);
                } else Emit(Op::Mov, tw, dst, src, 0, 0);
                m_top = keep;
                break;
            }
            case SK::If: {
                const int keep = m_top;
                const int c = Expr(st.expr);
                m_top = keep;
                const int jz = Emit(Op::Jz, 1, 0, c, 0, 0);
                Stmt(st.a);
                if (st.b >= 0) {
                    const int jmp = Emit(Op::Jmp, 1, 0, 0, 0, 0);
                    m_prog->code[(std::size_t)jz].d = (std::uint16_t)m_prog->code.size();
                    Stmt(st.b);
                    m_prog->code[(std::size_t)jmp].d = (std::uint16_t)m_prog->code.size();
                } else m_prog->code[(std::size_t)jz].d = (std::uint16_t)m_prog->code.size();
                break;
            }
            case SK::For: {
                Symbol& sy = P.syms[(std::size_t)st.sym];
                sy.slot = Alloc(1, st.line, st.col);
                const int cond = Alloc(1, st.line, st.col);
                Emit(Op::Mov, 1, sy.slot, Const((float)st.from, st.line, st.col), 0, 0);
                const int top = (int)m_prog->code.size();
                Stmt(st.a);
                Emit(Op::Add, 1, sy.slot, sy.slot, Const(1.0f, st.line, st.col), 0);
                Emit(Op::Lt, 1, cond, sy.slot, Const((float)st.to, st.line, st.col), 0);
                Emit(Op::Not, 1, cond, cond, 0, 0);
                Emit(Op::Jz, 1, top, cond, 0, 0);   // jump back while i < to
                break;
            }
            case SK::Discard: Emit(Op::Discard, 1, 0, 0, 0, 0); break;
        }
    }
};

// Shared maths (the GLSL / HLSL helpers above are written the same way).
inline float FMod(float a, float b) { return a - b * std::floor(a / b); }
inline float Perm(float x) { return FMod((x * 29.0f + 7.0f) * x, 251.0f); }
inline float Cell2(float x, float y) { x = FMod(x, 251.0f); y = FMod(y, 251.0f); return Perm(Perm(x) + y); }
inline float Cell3(float x, float y, float z) {
    x = FMod(x, 251.0f); y = FMod(y, 251.0f); z = FMod(z, 251.0f);
    return Perm(Perm(Perm(x) + y) + z);
}
inline float Lerp1(float a, float b, float t) { return a + (b - a) * t; }
float Noise2(float px, float py) {
    const float ix = std::floor(px), iy = std::floor(py);
    const float fx = px - ix, fy = py - iy;
    const float ux = fx * fx * (3.0f - 2.0f * fx), uy = fy * fy * (3.0f - 2.0f * fy);
    const float a = Cell2(ix, iy), b = Cell2(ix + 1.0f, iy), c = Cell2(ix, iy + 1.0f), d = Cell2(ix + 1.0f, iy + 1.0f);
    return Lerp1(Lerp1(a, b, ux), Lerp1(c, d, ux), uy) / 251.0f;
}
float Noise3(float px, float py, float pz) {
    const float ix = std::floor(px), iy = std::floor(py), iz = std::floor(pz);
    const float fx = px - ix, fy = py - iy, fz = pz - iz;
    const float ux = fx * fx * (3.0f - 2.0f * fx), uy = fy * fy * (3.0f - 2.0f * fy), uz = fz * fz * (3.0f - 2.0f * fz);
    const float a = Cell3(ix, iy, iz), b = Cell3(ix + 1, iy, iz), c = Cell3(ix, iy + 1, iz), d = Cell3(ix + 1, iy + 1, iz);
    const float e = Cell3(ix, iy, iz + 1), f = Cell3(ix + 1, iy, iz + 1), g = Cell3(ix, iy + 1, iz + 1), h = Cell3(ix + 1, iy + 1, iz + 1);
    const float lo = Lerp1(Lerp1(a, b, ux), Lerp1(c, d, ux), uy);
    const float hi = Lerp1(Lerp1(e, f, ux), Lerp1(g, h, ux), uy);
    return Lerp1(lo, hi, uz) / 251.0f;
}

} // namespace

Compiled Compile(const std::string& source) {
    Compiled out;
    if (source.size() > kMaxSource) { out.errors.push_back({1, 1, "source is too big (at most 1 MB)"}); return out; }
    std::vector<Token> toks;
    Diagnostic lexErr;
    if (!Lexer(source).Run(toks, lexErr)) { out.errors.push_back(lexErr); return out; }
    Parser p(toks, out);
    try {
        p.ParseFile();
    } catch (const Fail&) {
    }
    if (!p.errs.empty()) {
        out.errors = p.errs;
        out.props.clear(); out.textures.clear();
        return out;
    }
    CpuGen gen(p, out);
    std::vector<Diagnostic> cerr;
    std::shared_ptr<CpuProgram> cpu = gen.Run(cerr, p.body);
    if (!cpu) { out.errors = cerr; out.props.clear(); out.textures.clear(); return out; }
    if (p.vbody >= 0) {
        CpuGen vgen(p, out);
        std::shared_ptr<CpuProgram> vcpu = vgen.Run(cerr, p.vbody);
        if (!vcpu) { out.errors = cerr; out.props.clear(); out.textures.clear(); return out; }
        out.cpuVertex = vcpu;
        out.hasVertex = true;
        out.glslVertex = Emitter(p, out, false).RunVertex();
        out.hlslVertex = Emitter(p, out, true).RunVertex();
    }
    out.cpu = cpu;
    out.glsl = Emitter(p, out, false).Run();
    out.hlsl = Emitter(p, out, true).Run();
    out.ok = true;
    return out;
}

int CpuInstructionCount(const CpuProgram& prog) { return (int)prog.code.size(); }

namespace {
// The interpreter: runs `prog` on the register file `r` (inputs and outputs already in place).
// Returns true when the program discarded.
bool Exec(const CpuProgram& prog, float* r, const float* props, SampleFn sample, const void* ctx);
} // namespace

void RunCpuVertex(const CpuProgram& prog, const VertexIn& in, const float* props, VertexOut& out) {
    float R[kMaxRegs + kMaxConsts];
    float* r = R;
    for (int i = 0; i < kInputSlots + kOutputSlots; ++i) r[i] = 0.0f;
    std::memcpy(r + 0, in.uv, sizeof(float) * 2);
    std::memcpy(r + 2, in.local, sizeof(float) * 3);
    std::memcpy(r + 8, in.vcolor, sizeof(float) * 3);
    r[11] = in.time;
    std::memcpy(r + 12, in.position, sizeof(float) * 3);
    std::memcpy(r + 19, in.normal, sizeof(float) * 3);
    Exec(prog, r, props, nullptr, nullptr);
    std::memcpy(out.position, r + 12, sizeof(float) * 3);
    std::memcpy(out.normal, r + 19, sizeof(float) * 3);
}

void RunCpu(const CpuProgram& prog, const SurfaceIn& in, const float* props, SampleFn sample, const void* ctx, SurfaceOut& out) {
    float R[kMaxRegs + kMaxConsts];
    float* r = R;
    std::memcpy(r + 0, in.uv, sizeof(float) * 2);
    std::memcpy(r + 2, in.position, sizeof(float) * 3);
    std::memcpy(r + 5, in.view, sizeof(float) * 3);
    std::memcpy(r + 8, in.vcolor, sizeof(float) * 3);
    r[11] = in.time;
    std::memcpy(r + 12, in.albedo, sizeof(float) * 3);
    std::memcpy(r + 15, in.emission, sizeof(float) * 3);
    r[18] = in.alpha;
    std::memcpy(r + 19, in.normal, sizeof(float) * 3);
    r[22] = in.metallic; r[23] = in.specular; r[24] = in.shininess;
    out.discard = Exec(prog, r, props, sample, ctx);
    std::memcpy(out.albedo, r + 12, sizeof(float) * 3);
    std::memcpy(out.emission, r + 15, sizeof(float) * 3);
    out.alpha = r[18];
    std::memcpy(out.normal, r + 19, sizeof(float) * 3);
    out.metallic = r[22]; out.specular = r[23]; out.shininess = r[24];
}

namespace {
bool Exec(const CpuProgram& prog, float* r, const float* props, SampleFn sample, const void* ctx) {
    bool discarded = false;
    if (prog.propCount > 0 && props) std::memcpy(r + kInputSlots + kOutputSlots, props, sizeof(float) * 4 * (std::size_t)prog.propCount);
    else for (int i = 0; i < 4 * prog.propCount; ++i) r[kInputSlots + kOutputSlots + i] = 0.0f;
    if (!prog.consts.empty()) std::memcpy(r + prog.constBase, prog.consts.data(), sizeof(float) * prog.consts.size());
    const Ins* code = prog.code.data();
    const std::size_t codeN = prog.code.size();
    std::size_t pc = 0;
    long budget = 2000000;   // checked loops cannot exceed this; a guard all the same
    float tmp[4];
    while (pc < codeN && --budget > 0) {
        const Ins& I = code[pc++];
        const int n = I.n;
        const float* A = r + I.a;
        const float* B = r + I.b;
        const float* Cc = r + I.c;
        float* D = r + I.d;
        const int sa = (I.bc & 1) ? 0 : 1, sb = (I.bc & 2) ? 0 : 1, sc = (I.bc & 4) ? 0 : 1;
#define OK_EACH(expr) do { for (int k = 0; k < n; ++k) tmp[k] = (expr); for (int k = 0; k < n; ++k) D[k] = tmp[k]; } while (0)
#define a_ A[k * sa]
#define b_ B[k * sb]
#define c_ Cc[k * sc]
        switch (I.op) {
            case Op::Mov: OK_EACH(a_); break;
            case Op::Swz: OK_EACH(A[(I.ex >> (2 * k)) & 3]); break;
            case Op::StSwz: { for (int k = 0; k < n; ++k) tmp[k] = A[k]; for (int k = 0; k < n; ++k) D[(I.ex >> (2 * k)) & 3] = tmp[k]; break; }
            case Op::Add: OK_EACH(a_ + b_); break;
            case Op::Sub: OK_EACH(a_ - b_); break;
            case Op::Mul: OK_EACH(a_ * b_); break;
            case Op::Div: OK_EACH(a_ / b_); break;
            case Op::Neg: OK_EACH(-a_); break;
            case Op::Not: D[0] = A[0] != 0.0f ? 0.0f : 1.0f; break;
            case Op::Lt: D[0] = A[0] < B[0] ? 1.0f : 0.0f; break;
            case Op::Gt: D[0] = A[0] > B[0] ? 1.0f : 0.0f; break;
            case Op::Le: D[0] = A[0] <= B[0] ? 1.0f : 0.0f; break;
            case Op::Ge: D[0] = A[0] >= B[0] ? 1.0f : 0.0f; break;
            case Op::Eq: D[0] = A[0] == B[0] ? 1.0f : 0.0f; break;
            case Op::Ne: D[0] = A[0] != B[0] ? 1.0f : 0.0f; break;
            case Op::And: D[0] = (A[0] != 0.0f && B[0] != 0.0f) ? 1.0f : 0.0f; break;
            case Op::Or: D[0] = (A[0] != 0.0f || B[0] != 0.0f) ? 1.0f : 0.0f; break;
            case Op::Sel: { const bool c = A[0] != 0.0f; OK_EACH(c ? B[k] : Cc[k]); break; }
            case Op::Sin: OK_EACH(std::sin(a_)); break;
            case Op::Cos: OK_EACH(std::cos(a_)); break;
            case Op::Tan: OK_EACH(std::tan(a_)); break;
            case Op::Asin: OK_EACH(std::asin(a_)); break;
            case Op::Acos: OK_EACH(std::acos(a_)); break;
            case Op::Atan: OK_EACH(std::atan(a_)); break;
            case Op::Exp: OK_EACH(std::exp(a_)); break;
            case Op::Log: OK_EACH(std::log(a_)); break;
            case Op::Exp2: OK_EACH(std::exp2(a_)); break;
            case Op::Log2: OK_EACH(std::log2(a_)); break;
            case Op::Sqrt: OK_EACH(std::sqrt(a_)); break;
            case Op::Abs: OK_EACH(std::fabs(a_)); break;
            case Op::Sign: OK_EACH(a_ > 0.0f ? 1.0f : (a_ < 0.0f ? -1.0f : 0.0f)); break;
            case Op::Floor: OK_EACH(std::floor(a_)); break;
            case Op::Ceil: OK_EACH(std::ceil(a_)); break;
            case Op::Fract: OK_EACH(a_ - std::floor(a_)); break;
            case Op::Saturate: OK_EACH(std::fmin(std::fmax(a_, 0.0f), 1.0f)); break;
            case Op::Radians: OK_EACH(a_ * 0.0174532925f); break;
            case Op::Degrees: OK_EACH(a_ * 57.2957795f); break;
            case Op::Norm: {
                float l = 0.0f; for (int k = 0; k < n; ++k) l += A[k] * A[k];
                l = std::sqrt(l);
                OK_EACH(l > 1e-20f ? A[k] / l : A[k] * 0.0f);
                break;
            }
            case Op::Atan2: OK_EACH(std::atan2(a_, b_)); break;
            case Op::Pow: OK_EACH(std::pow(a_, b_)); break;
            case Op::Min: OK_EACH(std::fmin(a_, b_)); break;
            case Op::Max: OK_EACH(std::fmax(a_, b_)); break;
            case Op::Mod: OK_EACH(FMod(a_, b_)); break;
            case Op::Step: OK_EACH(b_ < a_ ? 0.0f : 1.0f); break;
            case Op::Reflect: {
                float d = 0.0f; for (int k = 0; k < n; ++k) d += A[k] * B[k];
                OK_EACH(A[k] - 2.0f * d * B[k]);
                break;
            }
            case Op::Clamp: OK_EACH(std::fmin(std::fmax(a_, b_), c_)); break;
            case Op::Mix: OK_EACH(a_ * (1.0f - c_) + b_ * c_); break;
            case Op::Smooth: {
                for (int k = 0; k < n; ++k) {
                    float t = (c_ - a_) / (b_ - a_);
                    t = std::fmin(std::fmax(t, 0.0f), 1.0f);
                    tmp[k] = t * t * (3.0f - 2.0f * t);
                }
                for (int k = 0; k < n; ++k) D[k] = tmp[k];
                break;
            }
            case Op::Length: { float l = 0.0f; for (int k = 0; k < n; ++k) l += A[k] * A[k]; D[0] = std::sqrt(l); break; }
            case Op::Dist: { float l = 0.0f; for (int k = 0; k < n; ++k) { const float q = A[k] - B[k]; l += q * q; } D[0] = std::sqrt(l); break; }
            case Op::Dot: { float l = 0.0f; for (int k = 0; k < n; ++k) l += A[k] * B[k]; D[0] = l; break; }
            case Op::Luma: D[0] = A[0] * 0.2126f + A[1] * 0.7152f + A[2] * 0.0722f; break;
            case Op::Cross: {
                const float x = A[1] * B[2] - A[2] * B[1], y = A[2] * B[0] - A[0] * B[2], z = A[0] * B[1] - A[1] * B[0];
                D[0] = x; D[1] = y; D[2] = z;
                break;
            }
            case Op::Noise2: D[0] = Noise2(A[0], A[1]); break;
            case Op::Noise3: D[0] = Noise3(A[0], A[1], A[2]); break;
            case Op::Hash2: D[0] = Cell2(std::floor(A[0]), std::floor(A[1])) / 251.0f; break;
            case Op::Hash3: D[0] = Cell3(std::floor(A[0]), std::floor(A[1]), std::floor(A[2])) / 251.0f; break;
            case Op::Sample: {
                float c[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                if (sample) sample(ctx, I.ex, A[0], A[1], c);
                D[0] = c[0]; D[1] = c[1]; D[2] = c[2]; D[3] = c[3];
                break;
            }
            case Op::Jmp: pc = I.d; break;
            case Op::Jz: if (A[0] == 0.0f) pc = I.d; break;
            case Op::Discard: discarded = true; pc = codeN; break;
            case Op::End: pc = codeN; break;
        }
#undef OK_EACH
#undef a_
#undef b_
#undef c_
    }
    return discarded;
}
} // namespace

// =====================================================================================
// Material property text: "name=1,2,3;tex:slot=path". ';' and '%' in values are written %3B / %25.
// =====================================================================================
namespace {
struct Entry { std::string key, value; };
std::string Unescape(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const std::string h = s.substr(i + 1, 2);
            if (h == "3B") { o += ';'; i += 2; continue; }
            if (h == "25") { o += '%'; i += 2; continue; }
            if (h == "3D") { o += '='; i += 2; continue; }
        }
        o += s[i];
    }
    return o;
}
std::string Escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == ';') o += "%3B";
        else if (c == '%') o += "%25";
        else if (c == '=') o += "%3D";
        else o += c;
    }
    return o;
}
std::vector<Entry> Split(const std::string& text) {
    std::vector<Entry> v;
    std::size_t i = 0;
    while (i <= text.size()) {
        std::size_t j = text.find(';', i);
        if (j == std::string::npos) j = text.size();
        const std::string part = text.substr(i, j - i);
        const std::size_t eq = part.find('=');
        if (eq != std::string::npos && eq > 0) v.push_back({part.substr(0, eq), Unescape(part.substr(eq + 1))});
        i = j + 1;
    }
    return v;
}
std::string Join(const std::vector<Entry>& v) {
    std::string o;
    for (const Entry& e : v) {
        if (!o.empty()) o += ';';
        o += e.key + "=" + Escape(e.value);
    }
    return o;
}
bool ParseFloats(const std::string& s, float* out, int n) {
    float tmp[4];
    std::size_t i = 0;
    for (int k = 0; k < n; ++k) {
        std::size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        const std::string part = s.substr(i, j - i);
        if (part.empty()) return false;
        char* end = nullptr;
        const double v = std::strtod(part.c_str(), &end);
        if (end == part.c_str() || *end != 0 || !std::isfinite(v) || std::fabs(v) > 1e30) return false;
        tmp[k] = (float)v;
        if (k + 1 < n && j >= s.size()) return false;
        i = j + 1;
    }
    for (int k = 0; k < n; ++k) out[k] = tmp[k];
    return true;
}
std::string FormatFloats(const float* v, int n) {
    std::string o;
    for (int k = 0; k < n; ++k) {
        char b[32];
        std::snprintf(b, sizeof b, "%.6g", (double)v[k]);
        if (k) o += ',';
        o += b;
    }
    return o;
}
} // namespace

bool GetParam(const std::string& text, const std::string& name, float* out, int n) {
    if (n < 1 || n > 4) return false;
    for (const Entry& e : Split(text)) if (e.key == name) return ParseFloats(e.value, out, n);
    return false;
}
std::string SetTextParam(const std::string& text, const std::string& key, const std::string& value) {
    std::vector<Entry> v = Split(text);
    bool found = false;
    for (Entry& e : v) if (e.key == key) { e.value = value; found = true; }
    if (!found) v.push_back({key, value});
    return Join(v);
}
std::string SetParam(const std::string& text, const std::string& name, const float* val, int n) {
    if (n < 1 || n > 4) return text;
    return SetTextParam(text, name, FormatFloats(val, n));
}
std::string GetTextParam(const std::string& text, const std::string& key) {
    for (const Entry& e : Split(text)) if (e.key == key) return e.value;
    return std::string();
}
std::vector<float> ResolveProperties(const Compiled& c, const std::string& text) {
    std::vector<float> v(c.props.size() * 4, 0.0f);
    const std::vector<Entry> entries = Split(text);
    for (std::size_t i = 0; i < c.props.size(); ++i) {
        const Property& p = c.props[i];
        for (int k = 0; k < 4; ++k) v[i * 4 + (std::size_t)k] = p.def[k];
        for (const Entry& e : entries)
            if (e.key == p.name) ParseFloats(e.value, &v[i * 4], Width(p.type));
    }
    return v;
}
std::string ResolveTexture(const Compiled& c, int slot, const std::string& text) {
    if (slot < 0 || slot >= (int)c.textures.size()) return std::string();
    const std::string mine = GetTextParam(text, "tex:" + c.textures[(std::size_t)slot].name);
    return mine.empty() ? c.textures[(std::size_t)slot].defaultPath : mine;
}

// =====================================================================================
// Shader file cache (hot reload)
// =====================================================================================
namespace {
struct FileEntry {
    std::shared_ptr<const Compiled> compiled;
    std::uint64_t generation = 0;
    std::filesystem::file_time_type mtime{};
    std::uintmax_t size = 0;
    bool exists = false;
    std::chrono::steady_clock::time_point checked{};
};
debugutil::SpinLock g_fileLock;
std::unordered_map<std::string, FileEntry>& Files() { static std::unordered_map<std::string, FileEntry> m; return m; }
std::atomic<std::uint64_t> g_generation{1};

bool Stat(const std::string& path, std::filesystem::file_time_type& t, std::uintmax_t& size) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
    t = std::filesystem::last_write_time(path, ec);
    if (ec) return false;
    size = std::filesystem::file_size(path, ec);
    return !ec;
}
} // namespace

ShaderFile LoadShaderFile(const std::string& path, int recheckMs) {
    const auto now = std::chrono::steady_clock::now();
    {
        debugutil::SpinGuard g(g_fileLock);
        auto it = Files().find(path);
        if (it != Files().end() && now - it->second.checked < std::chrono::milliseconds(recheckMs))
            return ShaderFile{it->second.compiled, it->second.generation, path};
    }
    std::filesystem::file_time_type mt{};
    std::uintmax_t size = 0;
    const bool exists = Stat(path, mt, size);
    {
        debugutil::SpinGuard g(g_fileLock);
        auto it = Files().find(path);
        if (it != Files().end() && it->second.exists == exists && (!exists || (it->second.mtime == mt && it->second.size == size))) {
            it->second.checked = now;
            return ShaderFile{it->second.compiled, it->second.generation, path};
        }
    }
    auto c = std::make_shared<Compiled>();
    if (!exists) c->errors.push_back({1, 1, "cannot read the shader file"});
    else if (size > kMaxSource) c->errors.push_back({1, 1, "shader file is too big (at most 1 MB)"});
    else {
        std::ifstream f(path, std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        if (!f && !f.eof()) c->errors.push_back({1, 1, "cannot read the shader file"});
        else *c = Compile(ss.str());
    }
    FileEntry e;
    e.compiled = c; e.generation = g_generation.fetch_add(1); e.mtime = mt; e.size = size; e.exists = exists; e.checked = now;
    if (!c->ok) Log::Error("[shader] ", c->FirstError(path), " (drawn in magenta until fixed)");
    else Log::Info("[shader] compiled ", path);
    debugutil::SpinGuard g(g_fileLock);
    Files()[path] = e;
    return ShaderFile{e.compiled, e.generation, path};
}

void ClearShaderFileCache() {
    debugutil::SpinGuard g(g_fileLock);
    Files().clear();
}

} // namespace oshader
} // namespace okay
